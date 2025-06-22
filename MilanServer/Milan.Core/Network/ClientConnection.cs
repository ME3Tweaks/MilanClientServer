using Milan.Core.Payload;
using Milan.Core.Utility;
using System.Diagnostics;
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
public class ClientConnection
{
    readonly TcpClient _client;
    readonly Stream _networkStream;
    readonly CancellationTokenSource _cancelTokenSource;

    readonly ResizableBuffer _frameHeaderBuffer;
    readonly ResizableBuffer _frameBodyBuffer;

    readonly SemaphoreSlim _outputSemaphore;
    readonly byte[] _outputHeaderBuffer;
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


    /// <summary>
    /// Tries to retrieve the client's address.
    /// </summary>
    public IPEndPoint? EndPoint => _client.Client.RemoteEndPoint as IPEndPoint;
    /// <summary>
    /// Checks if this connection is using TLS encryption.
    /// </summary>
    public bool IsSecure => _networkStream is SslStream;


    public ClientConnection(TcpClient client, Stream stream, CancellationTokenSource source)
    {
        _client = client;
        _networkStream = stream;
        _cancelTokenSource = source;

        _frameHeaderBuffer = new(len: FrameHeader.NET_SERIAL_SIZE);
        _frameBodyBuffer = new(len: INITIAL_BODY_CAPACITY);

        _outputSemaphore = new(1);
        _outputHeaderBuffer = new byte[FrameHeader.NET_SERIAL_SIZE];
        _outputBodyStream = new(capacity: INITIAL_BODY_CAPACITY);
    }

    /// <summary>
    /// Implements the client processing loop.
    /// </summary>
    public async Task ServeAsync()
    {
        CancellationToken cancelToken = _cancelTokenSource.Token;

        while (!cancelToken.IsCancellationRequested)
        {
            FrameHeader frameHeader = new();
            await frameHeader.ReadAsync(_networkStream, _frameHeaderBuffer.AsMemory(), cancelToken);

            _frameBodyBuffer.ResizeNoCopy((int)frameHeader.Length);
            await _networkStream.ReadExactlyAsync(_frameBodyBuffer.AsMemory(), cancelToken);

            Interlocked.Increment(ref _lastFrameIndex);

            // TODO: Handle request or notification based on `frameHeader`.
        }

        _networkStream.Close();
        _client.Close();
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

    /// <summary>
    /// Signals to close the connection asynchronously.
    /// </summary>
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

    public async Task SendBytesAsync(byte component, byte command, ReadOnlyMemory<byte> body)
        => await SendBytesAsync(component, command, EServerMessage.Default, body);

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

    public async Task SendJsonAsync(byte component, byte command, RefSingleAction<JsonObjectWriter> callback)
        => await SendJsonAsync(component, command, EServerMessage.Default, callback);

    public async Task SendJsonAsync(byte component, byte command, EServerMessage msg, RefSingleAction<JsonObjectWriter> callback)
    {
        FrameHeader frameHeader = MakeFrameHeader(component, command, msg is EServerMessage.Notification);

        try
        {
            await _outputSemaphore.WaitAsync(_cancelTokenSource.Token);

            Debug.Assert(_outputBodyStream.Position == 0);
            _outputBodyStream.SetLength(0);

            JsonPayloadWriter writer = new(_outputBodyStream, PRETTY_FORMAT_JSON);

            writer.SetRootWith(callback);
            frameHeader.SetLength(_outputBodyStream.Length);

            _outputBodyStream.Position = 0;

            await frameHeader.WriteAsync(_networkStream, _outputHeaderBuffer, _cancelTokenSource.Token);
            await _outputBodyStream.CopyToAsync(_networkStream, _cancelTokenSource.Token);
        }
        finally
        {
            _outputSemaphore.Release();
        }
    }
}
