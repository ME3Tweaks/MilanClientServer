namespace Milan.Core.Network;

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

    readonly PolymorphicHandler?[] _handlers = new PolymorphicHandler?[MAX_COMPONENT_COUNT * MAX_COMMAND_COUNT];

    PolymorphicHandler? GetPolymorphic(HandlerIndex index)
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

    public NotificationHandlerDelegate? GetNotificationHandler(HandlerIndex index)
        => (GetPolymorphic(index) as NotificationPolymorphicHandler)?.Delegate;
    public RequestHandlerDelegate? GetRequestHandler(HandlerIndex index)
        => (GetPolymorphic(index) as RequestPolymorphicHandler)?.Delegate;
    public ResponseHandlerDelegate? GetResponseHandler(HandlerIndex index)
        => (GetPolymorphic(index) as ResponsePolymorphicHandler)?.Delegate;
}

internal abstract record PolymorphicHandler;

internal sealed record NotificationPolymorphicHandler(NotificationHandlerDelegate Delegate) : PolymorphicHandler;
internal sealed record RequestPolymorphicHandler(RequestHandlerDelegate Delegate) : PolymorphicHandler;
internal sealed record ResponsePolymorphicHandler(ResponseHandlerDelegate Delegate) : PolymorphicHandler;
