using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Milan.Core.Network;

/// <summary>
/// Base for all server component types.
/// </summary>
public abstract class BaseComponent<T>(IServiceProvider serviceProvider)
{
    /// <summary>
    /// Injected logger dependency.
    /// </summary>
    protected ILogger<T> Logger { get; } = serviceProvider.GetService<ILogger<T>>()!;


    /// <summary>
    /// Retrieves the component's over-the-wire id.
    /// </summary>
    public abstract byte Id { get; }
    /// <summary>
    /// Retrieves the component's friendly name (might be used in debugging).
    /// </summary>
    public abstract string Name { get; }

    /// <summary>
    /// Registers all handlers within this component with the given <paramref name="registry"/>.
    /// </summary>
    /// <param name="registry">The registry to receive this component's handlers.</param>
    public abstract void Register(IHandlerRegistry registry);

    /// <summary>
    /// Retrieves registered notification handlers.
    /// </summary>
    public abstract IEnumerable<NotificationHandlerDelegate> NotificationHandlers { get; }
    /// <summary>
    /// Retrieves registered request handlers.
    /// </summary>
    public abstract IEnumerable<RequestHandlerDelegate> RequestHandlers { get; }
    /// <summary>
    /// Retrieves registered response handlers.
    /// </summary>
    public abstract IEnumerable<ResponseHandlerDelegate> ResponseHandlers { get;  }
}
