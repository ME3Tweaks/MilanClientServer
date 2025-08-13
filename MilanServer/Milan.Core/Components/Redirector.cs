using Microsoft.Extensions.Logging;
using Milan.Core.Messages;
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
public sealed partial class RedirectorComponent(IServiceProvider serviceProvider)
    : BaseComponent<RedirectorComponent>(serviceProvider)
{
    public const byte COMMAND_QUERY = 1;
    public const byte COMMAND_STATUS = 2;

    // TODO: Pull other components' addresses from config.
    Address _addressMatchmaker;
    Address _addressSTUN;
    Address _addressTURN;

    bool CheckCompatibleHost(Version host)
    {
        // TODO: Pull compatible versions from config.
        return true;
    }

    bool CheckCompatibleContent(Version content)
    {
        // TODO: Pull compatible versions from config.
        return true;
    }

    #region Request handlers.

    /// <summary>
    /// Checks client's version and shares other server components' information.
    /// </summary>
    [RequestHandler(Id = COMMAND_QUERY)]
    public async Task HandleQueryRequest(RequestContext ctx, CancellationToken token)
    {
        QueryRequest request = new();
        ctx.JsonReader.GetRootWith(request.Read);

        if (!CheckCompatibleHost(request.HostVersion) || !CheckCompatibleContent(request.ContentVersion))
        {
            Logger.LogWarning("unsupported client '{ip}' host '{hostVersion}' or content '{contentVersion}' versions",
                ctx.Connection.EndPoint, request.HostVersion, request.ContentVersion);

            await ctx.Connection.CloseAsync();
            return;
        }

        QueryResponse response = new()
        {
            Match = _addressMatchmaker,
            Stun = _addressSTUN,
            Turn = _addressTURN,
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

    #endregion
}
