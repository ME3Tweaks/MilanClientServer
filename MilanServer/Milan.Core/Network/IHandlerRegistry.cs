namespace Milan.Core.Network;

/// <summary>
/// Unique identifier for a registered message handler.
/// </summary>
/// <param name="Component">Index of a server component.</param>
/// <param name="Command">Index of a command within the <c>Component</c>.</param>
public readonly record struct HandlerIndex(byte Component, byte Command)
{
    public static implicit operator HandlerIndex((byte, byte) pair)
        => new(pair.Item1, pair.Item2);
}

/// <summary>
/// Interface for resolving a message handler.
/// </summary>
public interface IHandlerRegistry
{
    /// <summary>
    /// Tries to resolve a notification handler at the given <see cref="HandlerIndex"/>.
    /// </summary>
    /// <param name="index">Component and command indices.</param>
    /// <returns>A notification handler delegate if found, and <c>null</c> otherwise.</returns>
    public NotificationHandlerDelegate? GetNotificationHandler(HandlerIndex index);

    /// <summary>
    /// Tries to resolve a request handler at the given <see cref="HandlerIndex"/>.
    /// </summary>
    /// <param name="index">Component and command indices.</param>
    /// <returns>A request handler delegate if found, and <c>null</c> otherwise.</returns>
    public RequestHandlerDelegate? GetRequestHandler(HandlerIndex index);

    /// <summary>
    /// Tries to resolve a response handler at the given <see cref="HandlerIndex"/>.
    /// </summary>
    /// <param name="index">Component and command indices.</param>
    /// <returns>A response handler delegate if found, and <c>null</c> otherwise.</returns>
    public ResponseHandlerDelegate? GetResponseHandler(HandlerIndex index);

    /// <summary>
    /// Assigns a notification handler to the given <see cref="HandlerIndex"/>.
    /// </summary>
    /// <param name="index">Component and command indices.</param>
    /// <param name="handler">Notification handler.</param>
    public void SetNotificationHandler(HandlerIndex index, NotificationHandlerDelegate handler);

    /// <summary>
    /// Assigns a request handler to the given <see cref="HandlerIndex"/>.
    /// </summary>
    /// <param name="index">Component and command indices.</param>
    /// <param name="handler">Request handler.</param>
    public void SetRequestHandler(HandlerIndex index, RequestHandlerDelegate handler);

    /// <summary>
    /// Assigns a response handler to the given <see cref="HandlerIndex"/>.
    /// </summary>
    /// <param name="index">Component and command indices.</param>
    /// <param name="handler">Response handler.</param>
    public void SetResponseHandler(HandlerIndex index, ResponseHandlerDelegate handler);
}
