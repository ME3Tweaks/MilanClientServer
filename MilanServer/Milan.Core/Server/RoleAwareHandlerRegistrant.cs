using Microsoft.Extensions.Logging;
using Milan.Core.Components;

namespace Milan.Core.Server;

/// <summary>
/// Registers handlers based on the server's role,
/// retrieved from the resolved <see cref="IServerRoleProvider"/>.
/// </summary>
public class RoleAwareHandlerRegistrant(
    ILogger<RoleAwareHandlerRegistrant> logger,
    IServiceProvider serviceProvider,
    IServerRoleProvider roleProvider)
        : IHandlerRegistrant
{
    public void RegisterHandlers(IHandlerRegistry registry)
    {
        logger.LogInformation("Registering handlers for role {role}.", roleProvider.Role);

        if (roleProvider.Role is EServerRole.Redirector)
        {
            new RedirectorComponent(serviceProvider).Register(registry);
        }
        else if (roleProvider.Role is EServerRole.Matchmaking)
        {
#if DEBUG
            // Debug builds support redirector for DX.
            new RedirectorComponent(serviceProvider).Register(registry);
#endif

            // TODO: Register all the fun components.
        }

        // Heartbeat is registered for all roles.
        new HeartbeatComponent(serviceProvider).Register(registry);
    }
}
