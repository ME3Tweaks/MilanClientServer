namespace Milan.Core.Network;

/// <summary>
/// Unique identifier for a registered message handler.
/// </summary>
/// <param name="Component">Index of a server component.</param>
/// <param name="Command">Index of a command within the <c>Component</c>.</param>
public readonly record struct HandlerIndex(byte Component, byte Command);

/// <summary>
/// Interface for resolving a message handler.
/// </summary>
public interface IHandlerRegistry
{
    /// <summary>
    /// Tries to resolve a notification handler at the given <see cref="HandlerIndex"/>.
    /// </summary>
    /// <param name="index">Component and command indices.</param>
    /// <returns>A handler delegate if found, and <c>null</c> otherwise.</returns>
    public NotificationHandlerDelegate? GetNotificationHandler(HandlerIndex index);

    public RequestHandlerDelegate? GetRequestHandler(HandlerIndex index);
    public ResponseHandlerDelegate? GetResponseHandler(HandlerIndex index);
}
