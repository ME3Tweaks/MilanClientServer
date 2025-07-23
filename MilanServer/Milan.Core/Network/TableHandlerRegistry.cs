namespace Milan.Core.Network;

/// <summary>
/// An <see cref="IHandlerRegistry"/> implementation which stores handlers in a small table.
/// </summary>
public class TableHandlerRegistry() : IHandlerRegistry
{
    /// <summary>
    /// Maximum allowed number of components registered.
    /// </summary>
    public const byte MAX_COMPONENT_COUNT = 1 << 6;
    /// <summary>
    /// Maximum allowed number of commands registered per component.
    /// </summary>
    public const byte MAX_COMMAND_COUNT = 1 << 6;

    #region Handler storage.

    record struct HandlerTriplet(NotificationHandlerDelegate? Notification, RequestHandlerDelegate? Request, ResponseHandlerDelegate? Response);
    readonly HandlerTriplet[] _handlers = new HandlerTriplet[MAX_COMPONENT_COUNT * MAX_COMMAND_COUNT];

    /// <summary>
    /// Internal implementation for <see cref="GetNotificationHandler(HandlerIndex)"/>,
    /// <see cref="GetRequestHandler(HandlerIndex)"/>, and <see cref="GetResponseHandler(HandlerIndex)"/>.
    /// </summary>
    /// <param name="index">Component and command indices.</param>
    /// <returns>Handler storage, or <c>null</c> if <paramref name="index"/> is out of bounds.</returns>
    HandlerTriplet? GetHandlerTriplet(HandlerIndex index)
    {
#if DEBUG
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index.Component, MAX_COMPONENT_COUNT, nameof(index));
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index.Command, MAX_COMMAND_COUNT, nameof(index));
#else
        if (index.Component >= MAX_COMPONENT_COUNT) return null;
        if (index.Command >= MAX_COMMAND_COUNT) return null;
#endif
        int linearIndex = index.Component * MAX_COMMAND_COUNT + index.Command;
        return _handlers[linearIndex];
    }

    void UpdateHandlerTriplet(HandlerIndex index, Func<HandlerTriplet, HandlerTriplet> mutator)
    {
        // Index OOB in server initialization should always be fatal.
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index.Component, MAX_COMPONENT_COUNT, nameof(index));
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index.Command, MAX_COMMAND_COUNT, nameof(index));

        int linearIndex = index.Component * MAX_COMMAND_COUNT + index.Command;
        _handlers[linearIndex] = mutator(_handlers[linearIndex]);
    }

    #endregion

    #region Handler registration.

    public void SetNotificationHandler(HandlerIndex index, NotificationHandlerDelegate handler)
        => UpdateHandlerTriplet(index, t => t with { Notification = handler });
    public void SetRequestHandler(HandlerIndex index, RequestHandlerDelegate handler)
        => UpdateHandlerTriplet(index, t => t with { Request = handler });
    public void SetResponseHandler(HandlerIndex index, ResponseHandlerDelegate handler)
        => UpdateHandlerTriplet(index, t => t with { Response = handler });

    #endregion

    #region IHandlerRegistry interface.

    public NotificationHandlerDelegate? GetNotificationHandler(HandlerIndex index) => GetHandlerTriplet(index)?.Notification;
    public RequestHandlerDelegate? GetRequestHandler(HandlerIndex index) => GetHandlerTriplet(index)?.Request;
    public ResponseHandlerDelegate? GetResponseHandler(HandlerIndex index) => GetHandlerTriplet(index)?.Response;

    #endregion
}
