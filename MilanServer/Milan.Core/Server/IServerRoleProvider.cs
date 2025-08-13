namespace Milan.Core.Server;

/// <summary>
/// Retrieves the <see cref="ServerRole"/> assumed by the server on initialization.
/// </summary>
public interface IServerRoleProvider
{
    /// <summary>
    /// The <see cref="ServerRole"/> assumed by the server on initialization.
    /// </summary>
    public ServerRole Role { get; }
}
