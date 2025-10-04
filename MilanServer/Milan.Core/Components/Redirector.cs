using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Milan.Core.Messages;
using Milan.Core.Messages.Redirector;
using Milan.Core.Options;
using Milan.Core.Server;
using Milan.Generators;
using System.Net;
using System.Net.Sockets;
using Version = Milan.Core.Messages.Version;

namespace Milan.Core.Components;


/// <summary>
/// Redirector component handles the initial communication from client to server,
/// allowing or rejecting the client's admitted version, and sending relevant addresses
/// for the actual game servers.
/// </summary>
[Component(Id = 1)]
public sealed partial class RedirectorComponent : BaseComponent<RedirectorComponent>
{
    public const byte COMMAND_QUERY = 1;
    public const byte COMMAND_STATUS = 2;

    Address _addressMatchmaker;
    Address _addressStun;
    Address _addressTurn;

    public RedirectorComponent(IServiceProvider serviceProvider) : base(serviceProvider)
    {
        var options = serviceProvider.GetRequiredService<IOptions<RedirectorOptions>>().Value;

        static Address? fromEndpoint(RedirectorOptionsAddress address)
        {
            if (string.IsNullOrWhiteSpace(address.Ip))
                return null;

            var addr = IPAddress.Parse(address.Ip);
            if (addr.AddressFamily is not AddressFamily.InterNetwork)
                throw new InvalidDataException($"unsupported ip address family");
            var ip = BitConverter.ToUInt32(addr.GetAddressBytes());

            return new(ip, address.Port);
        }

        try
        {
            _addressMatchmaker = fromEndpoint(options.Matchmaking) ?? new();
            _addressStun = fromEndpoint(options.STUN) ?? new();
            _addressTurn = fromEndpoint(options.TURN) ?? new();
        }
        catch (Exception ex)
        {
            Logger.LogError("Failed to parse one or more " +
                "redirector addresses: {exception}.", ex.Message);
        }

        Logger.LogDebug("Matchmaker address is {address}.", _addressMatchmaker);
        Logger.LogDebug("STUN address is {address}.", _addressStun);
        Logger.LogDebug("TURN address is {address}.", _addressTurn);
    }

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
            Logger.LogWarning("Unsupported client '{ip}' version(s): host '{hostVersion}' or content '{contentVersion}'.",
                ctx.Connection.EndPoint, request.HostVersion, request.ContentVersion);

            await ctx.Connection.CloseAsync();
            return;
        }

        QueryResponse response = new()
        {
            Match = _addressMatchmaker,
            Stun = _addressStun,
            Turn = _addressTurn,
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
