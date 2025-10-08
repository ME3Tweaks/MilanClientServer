namespace Milan.Core.Server;

/// <summary>
/// Subset of server components to register.
/// </summary>
public enum EServerRole
{
    /// <summary>
    /// Registers redirector component only.
    /// </summary>
    Redirector = 1,
    /// <summary>
    /// Registers every component but redirector in release builds,
    /// and everything in debug builds.
    /// </summary>
    Matchmaking = 2,
}

/// <summary>
/// Retrieves the <see cref="EServerRole"/> assumed by the server on initialization.
/// </summary>
public interface IServerRoleProvider
{
    /// <summary>
    /// The <see cref="EServerRole"/> assumed by the server on initialization.
    /// </summary>
    public EServerRole Role { get; }
}

/// <summary>
/// Implementation of <see cref="IServerRoleProvider"/>
/// which always returns the <paramref name="role"/> received on construction.
/// </summary>
/// <param name="role">The <see cref="EServerRole"/> to provide.</param>
public sealed class StaticRoleProvider(EServerRole role) : IServerRoleProvider
{
    public EServerRole Role { get; } = role;
}
