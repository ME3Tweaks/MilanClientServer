namespace Milan.Core.Server;

/// <summary>
/// Subset of server components to register.
/// </summary>
public enum ServerRole
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
