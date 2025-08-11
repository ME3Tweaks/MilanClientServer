using Milan.Core.Payload;
using Milan.Core.Utility;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Runtime.CompilerServices;

namespace Milan.Core.Network;

/// <summary>
/// Sets whether a server-originating message is a notification.
/// </summary>
public enum EServerMessage
{
    Default = 1,
    Notification = 2,
}

/// <summary>
/// Connection to a client.
/// </summary>
public class ClientConnection : ISendingConnection, IDisposable
{
    /// <summary>
    /// Type for a delegate to run on a connection "tick",
    /// provided by the connection's owner at construction time.
    /// </summary>
    /// <param name="connection">Connection calling the handler.</param>
    /// <param name="token">Cancellation token owned by <see cref="ServeAsync"/> loop.</param>
    public delegate Task AsyncTickHandler(ClientConnection connection, CancellationToken token);

    readonly IHandlerRegistry _handlerRegistry;
    readonly TcpClient _client;
    readonly Stream _networkStream;
    readonly CancellationTokenSource _cancelTokenSource;

    readonly AsyncTickHandler _tickHandler;
    readonly long _tickSeconds;

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


    public ClientConnection(IHandlerRegistry registry, AsyncTickHandler tick, long interval,
        TcpClient client, Stream stream, CancellationTokenSource source)
    {
        _handlerRegistry = registry;
        _client = client;
        _networkStream = stream;
        _cancelTokenSource = source;

        _tickHandler = tick;
        _tickSeconds = interval;

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
        CancellationToken cancelToken = _cancelTokenSource.Token;
        Task tickTask = TickAsync(cancelToken);
        while (!cancelToken.IsCancellationRequested)
        {
            FrameHeader frameHeader = new();
            await frameHeader.ReadAsync(_networkStream, _frameHeaderBuffer, cancelToken);

            if (frameHeader.Index != _lastFrameIndex)
                throw new InvalidOperationException("frame index mismatch");

            _frameBodyBuffer.ResizeNoCopy((int)frameHeader.Length);
            await _networkStream.ReadExactlyAsync(_frameBodyBuffer, cancelToken);

            HandlerIndex handlerIndex = new(frameHeader.Component, frameHeader.Command);

            if (frameHeader.IsNotification)
            {
                NotificationHandlerDelegate handler = _handlerRegistry.GetNotificationHandler(handlerIndex)
                    ?? throw new InvalidOperationException("invalid handler index");
                await handler(new NotificationContext(this, _frameBodyBuffer), cancelToken);
            }
            else
            {
                RequestHandlerDelegate handler = _handlerRegistry.GetRequestHandler(handlerIndex)
                    ?? throw new InvalidOperationException("invalid handler index");
                await handler(new RequestContext(this, frameHeader.Index, _frameBodyBuffer), cancelToken);

                Interlocked.Increment(ref _lastFrameIndex);
            }
        }

        await tickTask;
        await _networkStream.FlushAsync();
        _networkStream.Close();
        _client.Close();
    }

    async Task TickAsync(CancellationToken token)
    {
        await _tickHandler(this, token);

        using var tickTimer = new PeriodicTimer(TimeSpan.FromSeconds(_tickSeconds));
        while (await tickTimer.WaitForNextTickAsync(token))
            await _tickHandler(this, token);
    }

    /// <summary>
    /// Signals to close the connection.
    /// </summary>
    /// <param name="force">Whether to force connection closure at callsite.</param>
    public void Close(bool force = false)
    {
        _cancelTokenSource.Cancel();

        if (force)
        {
            _networkStream.Close();
            _client.Close();
        }
    }

    #region IConnection / ISendingConnection implementation.

    public IPEndPoint? EndPoint => _client.Client.RemoteEndPoint as IPEndPoint;
    public bool IsSecure => _networkStream is SslStream;

    public async Task CloseAsync()
    {
        await _cancelTokenSource.CancelAsync();
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
            await _outputSemaphore.WaitAsync(_cancelTokenSource.Token);
            await frameHeader.WriteAsync(_networkStream, _outputHeaderBuffer, _cancelTokenSource.Token);
            await _networkStream.WriteAsync(body, _cancelTokenSource.Token);
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
            await _outputSemaphore.WaitAsync(_cancelTokenSource.Token);
            await frameHeader.WriteAsync(_networkStream, _outputHeaderBuffer, _cancelTokenSource.Token);
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
            await _outputSemaphore.WaitAsync(_cancelTokenSource.Token);
            
            _outputBodyStream.Seek(0, SeekOrigin.Begin);
            _outputBodyStream.SetLength(0);

            JsonPayloadWriter writer = new(_outputBodyStream, PRETTY_FORMAT_JSON);

            writer.SetRootWith(callback);
            frameHeader.SetLength(_outputBodyStream.Length);

            _outputBodyStream.Seek(0, SeekOrigin.Begin);

            await frameHeader.WriteAsync(_networkStream, _outputHeaderBuffer, _cancelTokenSource.Token);
            await _outputBodyStream.CopyToAsync(_networkStream, _cancelTokenSource.Token);
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
            await _outputSemaphore.WaitAsync(_cancelTokenSource.Token);

            _outputBodyStream.Seek(0, SeekOrigin.Begin);
            _outputBodyStream.SetLength(0);

            callback(_outputBodyStream);
            frameHeader.SetLength(_outputBodyStream.Length);

            _outputBodyStream.Seek(0, SeekOrigin.Begin);

            await frameHeader.WriteAsync(_networkStream, _outputHeaderBuffer, _cancelTokenSource.Token);
            await _outputBodyStream.CopyToAsync(_networkStream, _cancelTokenSource.Token);
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
                _cancelTokenSource.Dispose();
                _outputSemaphore.Dispose();
                _outputBodyStream.Dispose();
            }

            // (Set large non-readonly fields to null.)

            _disposed = true;
        }
    }

    #endregion
}
