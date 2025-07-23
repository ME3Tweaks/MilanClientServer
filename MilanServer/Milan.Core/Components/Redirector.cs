using Milan.Core.Messages.Redirector;
using Milan.Core.Network;

using Version = Milan.Core.Messages.Version;

namespace Milan.Core.Components;

public static class Redirector
{
    public const byte COMPONENT = 1;

    public const byte COMMAND_QUERY = 4;
    public const byte COMMAND_HEARTBEAT = 5;

    public static void Register(IHandlerRegistry registry)
    {
        // Checks client's version and shares other server components' information.
        registry.SetRequestHandler(COMPONENT, COMMAND_QUERY, static async (ctx, token) =>
        {
            QueryRequest request = new();
            ctx.JsonReader.GetRootWith(request.Read);

            // TODO: Pull compatible versions from config.
            if (request.HostVersion != new Version(1, 2, 3))
            {
                // TODO: Log version mismatch. (as warning?)
                await ctx.Connection.CloseAsync();
                return;
            }

            // TODO: Pull other components' address from config.
            QueryResponse response = new()
            {
                Match = new(0, 0),
                Stun = new(0, 0),
                Turn = new(0, 0),
            };

            await ctx.Connection.SendJsonAsync(COMPONENT, COMMAND_QUERY, response.Write);
        });

        // Used to tell if the redirector is still alive.
        registry.SetRequestHandler(COMPONENT, COMMAND_HEARTBEAT, static async (ctx, token) =>
        {
            await ctx.Connection.SendEmptyAsync(COMPONENT, COMMAND_HEARTBEAT);
        });
    }
}
