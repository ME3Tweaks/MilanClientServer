using Milan.Core.Messages.Heartbeat;
using Milan.Core.Network;
using Milan.Generators;

namespace Milan.Core.Components;


/// <summary>
/// Heartbeat component allows client to communicate to the server that it's still connected.
/// Communication from server to client is handled using the <see cref="ClientConnection"/> tick mechanism.
/// </summary>
[Component(Id = 2)]
public partial class HeartbeatComponent(IServiceProvider serviceProvider)
    : BaseComponent<HeartbeatComponent>(serviceProvider)
{
    public const byte COMMAND_CLIENT_ALIVE = 1;
    public const byte COMMAND_SERVER_ALIVE = 2;

    [NotificationHandler(Id = COMMAND_CLIENT_ALIVE)]
    public async Task HandleClientAliveNotification(NotificationContext ctx, CancellationToken token)
    {
        ClientAliveNotification clientNotification = new();
        ctx.JsonReader.GetRootWith(clientNotification.Read);

        // TODO: Update context connection's client-side heartbeat timer.

        await Task.CompletedTask;
    }
}
