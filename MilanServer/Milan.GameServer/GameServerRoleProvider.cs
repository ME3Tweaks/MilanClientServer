using Milan.Core.Server;
namespace Milan.GameServer;

public sealed class GameServerRoleProvider(ServerRole role) : IServerRoleProvider
{
    public ServerRole Role { get; } = role;
}
