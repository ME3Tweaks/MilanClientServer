using Milan.Core.Payload;

namespace Milan.Core.Network;

/// <summary>
/// Signature for a notification handler.
/// </summary>
public delegate Task NotificationHandlerDelegate(NotificationContext context, CancellationToken token);
/// <summary>
/// Signature for a request handler.
/// </summary>
public delegate Task RequestHandlerDelegate(RequestContext context, CancellationToken token);
/// <summary>
/// Signature for a response handler.
/// </summary>
public delegate Task ResponseHandlerDelegate(ResponseContext context, CancellationToken token);


/// <summary>
/// Message-handling context provided to <see cref="NotificationHandlerDelegate"/>.
/// </summary>
public readonly struct NotificationContext(IConnection connection, ReadOnlyMemory<byte> body)
{
    /// <summary>
    /// Connection which received this notification.
    /// </summary>
    public readonly IConnection Connection => connection;
    /// <summary>
    /// Notification body contents.
    /// </summary>
    public readonly ReadOnlyMemory<byte> Body => body;

    /// <summary>
    /// Local JSON reader over the notification's contents.
    /// </summary>
    /// <remarks>If you want to <em>write</em> JSON payloads, see <see cref="ISendingConnection"/> methods.</remarks>
    public readonly JsonPayloadReader JsonReader => new(body.Span);
}

/// <summary>
/// Message-handling context provided to <see cref="RequestHandlerDelegate"/>.
/// </summary>
public readonly struct RequestContext(ISendingConnection connection, uint frameIndex, ReadOnlyMemory<byte> body)
{
    /// <summary>
    /// Connection which received this request.
    /// </summary>
    public readonly ISendingConnection Connection => connection;
    /// <summary>
    /// Index of the connection's current frame.
    /// </summary>
    public readonly uint FrameIndex => frameIndex;
    /// <summary>
    /// Request body contents.
    /// </summary>
    public readonly ReadOnlyMemory<byte> Body => body;

    /// <summary>
    /// Local JSON reader over the requests's contents.
    /// </summary>
    /// <remarks>If you want to <em>write</em> JSON payloads, see <see cref="ISendingConnection"/> methods.</remarks>
    public JsonPayloadReader JsonReader => new(body.Span);
}

/// <summary>
/// Message-handling context provided to <see cref="ResponseHandlerDelegate"/>.
/// </summary>
public readonly struct ResponseContext(IConnection connection, uint frameIndex, ReadOnlyMemory<byte> body)
{
    /// <summary>
    /// Connection which received this response.
    /// </summary>
    public readonly IConnection Connection => connection;
    /// <summary>
    /// Index of the connection's current frame.
    /// </summary>
    public readonly uint FrameIndex => frameIndex;
    /// <summary>
    /// Response body contents.
    /// </summary>
    public readonly ReadOnlyMemory<byte> Body => body;

    /// <summary>
    /// Local JSON reader over the response's contents.
    /// </summary>
    /// <remarks>If you want to <em>write</em> JSON payloads, see <see cref="ISendingConnection"/> methods.</remarks>
    public readonly JsonPayloadReader JsonReader => new(body.Span);
}
