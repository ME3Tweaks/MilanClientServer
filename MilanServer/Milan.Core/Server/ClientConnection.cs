using Microsoft.Extensions.Logging;
using Milan.Core.Network;
using Milan.Core.Payload;
using Milan.Core.Utility;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Threading.Channels;

namespace Milan.Core.Server;

/// <summary>
/// Sets whether a server-originating message is a notification.
/// </summary>
public enum EServerMessage
{
    Default = 1,
    Notification = 2,
}

/// <summary>
/// Used to ferry connection open/close status between server workers.
/// </summary>
public readonly record struct ClientStateMessage(ClientConnection Connection, bool Closing);

/// <summary>
/// Type for a delegate to run on a connection "tick",
/// provided by the connection's owner at construction time.
/// </summary>
/// <param name="connection">Connection calling the handler.</param>
/// <param name="token">Cancellation token owned by the connection loop.</param>
public delegate Task ClientTickHandler(ClientConnection connection, CancellationToken token);

/// <summary>
/// Connection to a client.
/// </summary>
public class ClientConnection : ISendingConnection, IDisposable
{
    readonly ChannelWriter<ClientStateMessage> _stateWriter;
    readonly IHandlerRegistry _handlerRegistry;
    readonly TcpClient _client;
    readonly Stream _networkStream;
    readonly CancellationToken _outerCancelToken;
    readonly CancellationTokenSource _innerCancelSource;

    readonly ILogger<ClientConnection> _logger;
    /// <summary>
    /// This is the <see cref="CancellationTokenSource"/> you want to use throughout the client!
    /// </summary>
    readonly CancellationTokenSource _cancelSource;

    readonly ResizableBuffer _frameHeaderBuffer;
    readonly ResizableBuffer _frameBodyBuffer;

    readonly SemaphoreSlim _outputSemaphore;
    readonly ResizableBuffer _outputHeaderBuffer;
    readonly MemoryStream _outputBodyStream;

    uint _lastFrameIndex;


    /// <summary>
    /// Initial buffer size for a frame body.
    /// </summary>
    public const int INITIAL_BODY_CAPACITY = 2048;

    /// <summary>
    /// Whether to pretty-format output json payloads.
    /// </summary>
#if DEBUG
    public const bool PRETTY_FORMAT_JSON = true;
#else
    public const bool PRETTY_FORMAT_JSON = false;
#endif

    public ClientConnection(
        TcpClient client,
        Stream clientStream,
        ChannelWriter<ClientStateMessage> stateWriter,
        ILogger<ClientConnection> logger,
        IHandlerRegistry registry,
        CancellationToken token)
    {
        // TODO(low): Consider replacing some arguments with an IServiceProvider.

        _stateWriter = stateWriter;
        _handlerRegistry = registry;
        _client = client;
        _networkStream = clientStream;
        _outerCancelToken = token;
        _innerCancelSource = new CancellationTokenSource();
        _logger = logger;

        // We need to combine the outer worker-owned cancel token which can be used by the worker
        // with the inner connection-owned cancel token which can be used by the connection, so that this
        // connection can gracefully signal its own cancellation without bringing down other connections with it.
        _cancelSource = CancellationTokenSource.CreateLinkedTokenSource(_outerCancelToken, _innerCancelSource.Token);

        _frameHeaderBuffer = new(len: FrameHeader.NET_SERIAL_SIZE);
        _frameBodyBuffer = new(len: INITIAL_BODY_CAPACITY);

        _outputSemaphore = new(initialCount: 1);
        _outputHeaderBuffer = new(len: FrameHeader.NET_SERIAL_SIZE);
        _outputBodyStream = new(capacity: INITIAL_BODY_CAPACITY);
    }

    /// <summary>
    /// Implements the client processing loop.
    /// </summary>
    public async Task ServeAsync()
    {
        try
        {
            await OnOpenAsync(_cancelSource.Token);

            while (!_cancelSource.Token.IsCancellationRequested)
            {
                FrameHeader frameHeader = new();
                await frameHeader.ReadAsync(_networkStream, _frameHeaderBuffer, _cancelSource.Token);

                if (frameHeader.Index != _lastFrameIndex)
                    throw new InvalidOperationException("frame index mismatch");

                _frameBodyBuffer.ResizeNoCopy((int)frameHeader.Length);
                await _networkStream.ReadExactlyAsync(_frameBodyBuffer, _cancelSource.Token);

                HandlerIndex handlerIndex = new(frameHeader.Component, frameHeader.Command);

                if (frameHeader.IsNotification)
                {
                    NotificationHandlerDelegate handler = _handlerRegistry.GetNotificationHandler(handlerIndex)
                        ?? throw new InvalidOperationException("invalid handler index");
                    await handler(new NotificationContext(this, _frameBodyBuffer), _cancelSource.Token);
                }
                else
                {
                    RequestHandlerDelegate handler = _handlerRegistry.GetRequestHandler(handlerIndex)
                        ?? throw new InvalidOperationException("invalid handler index");
                    await handler(new RequestContext(this, frameHeader.Index, _frameBodyBuffer), _cancelSource.Token);

                    Interlocked.Increment(ref _lastFrameIndex);
                }
            }

            await OnCloseAsync(_cancelSource.Token);
            await _networkStream.FlushAsync();
        }
        catch (Exception ex)
        {
            _logger.LogError("Client connection to '{address}' suffered an exception: {exception}", EndPoint, ex);
#if DEBUG
            _logger.LogWarning("Call stack of last exception on client connection to '{address}': {callstack}", EndPoint, ex.StackTrace);
#endif
        }

        _networkStream.Close();
        _client.Close();
    }

    /// <summary>
    /// Invokes server tick callback with the correct cancellation token source.
    /// </summary>
    public async Task TickAsync()
    {
        // TODO(high): Use linked cancel token between ServerAcceptWorker & ServerTickWorker.
        await OnTickAsync(_cancelSource.Token);
    }

    #region Connection lifecycle hooks.

    private async Task OnOpenAsync(CancellationToken token)
    {
        await _stateWriter.WriteAsync(new ClientStateMessage { Connection = this, Closing = false }, token);
        // ...
    }

    private async Task OnCloseAsync(CancellationToken token)
    {
        await _stateWriter.WriteAsync(new ClientStateMessage { Connection = this, Closing = true }, token);
        // ...
    }

    private async Task OnTickAsync(CancellationToken token)
    {
        await Task.CompletedTask;
    }

    #endregion

    #region IConnection / ISendingConnection implementation.

    public IPEndPoint? EndPoint => _client.Client.RemoteEndPoint as IPEndPoint;
    public bool IsSecure => _networkStream is SslStream;

    public async Task CloseAsync()
    {
        await _innerCancelSource.CancelAsync();
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    FrameHeader MakeFrameHeader(byte component, byte command, bool notification = false)
    {
        FrameHeader header = new() { Component = component, Command = command, Index = _lastFrameIndex };

        if (notification)
        {
            header.Flags |= EFrameFlags.Notification;
            header.Index = 0u;
        }

        return header;
    }

    public async Task SendBytesAsync(byte component, byte command, EServerMessage msg, ReadOnlyMemory<byte> body)
    {
        FrameHeader frameHeader = MakeFrameHeader(component, command, msg is EServerMessage.Notification);
        frameHeader.SetLength(body.Length);

        try
        {
            await _outputSemaphore.WaitAsync(_cancelSource.Token);
            await frameHeader.WriteAsync(_networkStream, _outputHeaderBuffer, _cancelSource.Token);
            await _networkStream.WriteAsync(body, _cancelSource.Token);
        }
        finally
        {
            _outputSemaphore.Release();
        }
    }

    public async Task SendEmptyAsync(byte component, byte command, EServerMessage msg)
    {
        FrameHeader frameHeader = MakeFrameHeader(component, command, msg is EServerMessage.Notification);

        try
        {
            await _outputSemaphore.WaitAsync(_cancelSource.Token);
            await frameHeader.WriteAsync(_networkStream, _outputHeaderBuffer, _cancelSource.Token);
        }
        finally
        {
            _outputSemaphore.Release();
        }
    }

    public async Task SendJsonAsync(byte component, byte command, EServerMessage msg, RefSingleAction<JsonObjectWriter> callback)
    {
        FrameHeader frameHeader = MakeFrameHeader(component, command, msg is EServerMessage.Notification);

        try
        {
            await _outputSemaphore.WaitAsync(_cancelSource.Token);
            
            _outputBodyStream.Seek(0, SeekOrigin.Begin);
            _outputBodyStream.SetLength(0);

            JsonPayloadWriter writer = new(_outputBodyStream, PRETTY_FORMAT_JSON);

            writer.SetRootWith(callback);
            frameHeader.SetLength(_outputBodyStream.Length);

            _outputBodyStream.Seek(0, SeekOrigin.Begin);

            await frameHeader.WriteAsync(_networkStream, _outputHeaderBuffer, _cancelSource.Token);
            await _outputBodyStream.CopyToAsync(_networkStream, _cancelSource.Token);
        }
        finally
        {
            _outputSemaphore.Release();
        }
    }

    public async Task SendStreamAsync(byte component, byte command, EServerMessage msg, Action<Stream> callback)
    {
        FrameHeader frameHeader = MakeFrameHeader(component, command, msg is EServerMessage.Notification);

        try
        {
            await _outputSemaphore.WaitAsync(_cancelSource.Token);

            _outputBodyStream.Seek(0, SeekOrigin.Begin);
            _outputBodyStream.SetLength(0);

            callback(_outputBodyStream);
            frameHeader.SetLength(_outputBodyStream.Length);

            _outputBodyStream.Seek(0, SeekOrigin.Begin);

            await frameHeader.WriteAsync(_networkStream, _outputHeaderBuffer, _cancelSource.Token);
            await _outputBodyStream.CopyToAsync(_networkStream, _cancelSource.Token);
        }
        finally
        {
            _outputSemaphore.Release();
        }
    }

    #endregion

    #region IDisposable implementation.

    private bool _disposed = false;

    public void Dispose()
    {
        Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (!_disposed)
        {
            if (disposing)
            {
                _client.Dispose();
                _networkStream.Dispose();
                _innerCancelSource.Dispose();
                _cancelSource.Dispose();
                _outputSemaphore.Dispose();
                _outputBodyStream.Dispose();
            }

            // (Set large non-readonly fields to null.)

            _disposed = true;
        }
    }

    #endregion
}
