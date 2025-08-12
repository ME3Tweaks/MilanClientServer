using Microsoft.Extensions.Logging;
using Milan.Core.Messages.Redirector;
using Milan.Core.Network;
using Milan.Generators;

using Version = Milan.Core.Messages.Version;

namespace Milan.Core.Components;


/// <summary>
/// Redirector component handles the initial communication from client to server,
/// allowing or rejecting the client's admitted version, and sending relevant addresses
/// for the actual game servers.
/// </summary>
[Component(Id = 1)]
public partial class RedirectorComponent(IServiceProvider serviceProvider)
    : BaseComponent<RedirectorComponent>(serviceProvider)
{
    public const byte COMMAND_QUERY = 1;
    public const byte COMMAND_STATUS = 2;

    /// <summary>
    /// Checks client's version and shares other server components' information.
    /// </summary>
    [RequestHandler(Id = COMMAND_QUERY)]
    public async Task HandleQueryRequest(RequestContext ctx, CancellationToken token)
    {
        QueryRequest request = new();
        ctx.JsonReader.GetRootWith(request.Read);

        // TODO: Pull compatible versions from config.
        if (request.HostVersion != new Version(1, 2, 3))
        {
            Logger.LogWarning("unsupported client '{ip}' host version '{version}'",
                ctx.Connection.EndPoint, request.HostVersion);

            await ctx.Connection.CloseAsync();
            return;
        }

        // TODO: Pull other components' addresses from config.
        QueryResponse response = new()
        {
            Match = new(Ip: 0, Port: 0),
            Stun = new(Ip: 0, Port: 0),
            Turn = new(Ip: 0, Port: 0),
        };

        await ctx.Connection.SendJsonAsync(COMPONENT, COMMAND_QUERY, response.Write);
    }

    /// <summary>
    /// Used to tell if the redirector is still alive.
    /// </summary>
    [RequestHandler(Id = COMMAND_STATUS)]
    public async Task HandleStatusRequest(RequestContext ctx, CancellationToken token)
    {
        await ctx.Connection.SendEmptyAsync(COMPONENT, COMMAND_STATUS);
    }
}
