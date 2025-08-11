using Milan.Core.Messages.Heartbeat;
using Milan.Core.Network;

namespace Milan.Core.Components;

public static class Heartbeat
{
    public const byte COMPONENT = 2;

    public const byte COMMAND_CLIENT_ALIVE = 1;
    public const byte COMMAND_SERVER_ALIVE = 2;

    public static void Register(IHandlerRegistry registry)
    {
        registry.SetNotificationHandler(COMPONENT, COMMAND_CLIENT_ALIVE, static async (ctx, token) =>
        {
            ClientAliveNotification clientNotification = new();
            ctx.JsonReader.GetRootWith(clientNotification.Read);

            // TODO: Update context connection's client-side heartbeat timer.

            await Task.CompletedTask;
        });
    }
}
