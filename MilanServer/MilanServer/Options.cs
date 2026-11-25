using Microsoft.Extensions.Options;

namespace MilanServer;

// Bound from the "Milan" configuration section (appsettings.json, MILAN__* style
// environment variables such as Milan__LobbyLeaseSeconds, or --Milan:Key=value).
public sealed class MilanServerOptions
{
    public const string SectionName = "Milan";

    // Suggested interval for host full-state reports; returned to hosts on create.
    public int HeartbeatSeconds { get; set; } = 20;
    // A lobby expires this long after its last authenticated state report.
    public int LobbyLeaseSeconds { get; set; } = 60;
    public int ReservationSeconds { get; set; } = 30;
    public int SearchSeconds { get; set; } = 120;
    // Offsets from search start at which quick match widens to any map, then any enemy.
    public int MapWidenSeconds { get; set; } = 30;
    public int EnemyWidenSeconds { get; set; } = 60;
    // When a search starts while any lobby at the player's difficulty is open to them,
    // matches already in progress join its candidates this long afterwards, so the
    // lobbies get the first chance. With no such lobby, they are candidates straight away.
    public int InProgressAfterSeconds { get; set; } = 10;
    // Quick match fills lobbies a joiner would keep at or under this many players before any it
    // would push past it, and those by how far past. 0 turns this off.
    public int PreferredPartySize { get; set; } = 4;
    // Lobbies a joiner would push past PreferredPartySize only become candidates this long after
    // the search starts. Unlike map and enemy widening, this applies with widening off too.
    public int PartySizeWidenSeconds { get; set; } = 15;
    // Lobbies with more slots than this never get quick-match joiners, only invites. 0 means no limit.
    public int MaxSearchLobbySize { get; set; } = 8;
    public int SearchPollMilliseconds { get; set; } = 2000;

    // Host migration. When a member reports its host lost, the host has this long to show
    // it is still there (any authenticated request) before it is declared lost.
    public int HostChallengeSeconds { get; set; } = 5;
    // Once the host is lost, the other members have this long to report before the server
    // picks a new host among those that did. It picks sooner once all of them have.
    public int MigrationReportSeconds { get; set; } = 8;
    // Lifetime of the reservations that move members to the new host, which first has to
    // load its map.
    public int MigrationJoinSeconds { get; set; } = 90;
    // Upper bound on how long introduction and reservation polls wait for a change.
    public int LongPollSeconds { get; set; } = 25;

    public bool UdpEnabled { get; set; } = true;
    // 0 binds an ephemeral port (for tests). Behind port mapping, map it 1:1 because
    // clients learn this port from /v1/info.
    public int UdpPort { get; set; } = 5081;
    public int DiscoveryMinIntervalMilliseconds { get; set; } = 200;
    // Server-side ceiling only. It is not evidence that LMP supports this many players.
    public int MaxPlayersCeiling { get; set; } = 16;

    public int MaxLobbies { get; set; } = 10_000;
    public int MaxSearches { get; set; } = 10_000;
    // Per-address limits group IPv6 clients by /64. CGNAT puts unrelated players
    // behind one IPv4 address, so keep these generous.
    public int MaxLobbiesPerAddress { get; set; } = 8;
    public int MaxSearchesPerAddress { get; set; } = 8;
    public int MaxReservationsPerAddress { get; set; } = 8;

    public int RequestsPerMinute { get; set; } = 600;
    public int CreatesPerMinute { get; set; } = 30;
    public int InviteRedeemsPerMinute { get; set; } = 20;
    public int InviteCodeLength { get; set; } = 10;
    public int TotalRequestsPerSecond { get; set; } = 1000;
    public int RequestBurst { get; set; } = 2000;
    public int MaxConcurrentRequests { get; set; } = 2048;
    public int MaxConcurrentRequestsPerAddress { get; set; } = 64;
    public int MaxConcurrentConnections { get; set; } = 4096;
    // Separate poll ceilings leave capacity for control requests under poll pressure.
    public int MaxConcurrentLongPolls { get; set; } = 1024;
    public int MaxConcurrentLongPollsPerAddress { get; set; } = 32;
    // Budgets include rejected UDP packets, with no queue or unbounded source table.
    public int DiscoveryPacketsPerSecond { get; set; } = 5000;
    public int DiscoveryPacketsPerSecondPerAddress { get; set; } = 100;

    // The current game version, which most players on this server run. An older game is told to
    // update (GET /v1/info), and a quick match from any other version is told few players will
    // match it. Both will still work
    public string ExpectedGameVersion { get; set; } = "";

    // Reverse proxies whose X-Forwarded-For and X-Forwarded-Proto headers are trusted, as single
    // addresses or CIDR ranges such as 172.16.0.0/12. Loopback is always trusted.
    public string[] TrustedProxies { get; set; } = [];

    // A private server: when set, every /v1 request except /v1/stats must send it as the
    // X-Milan-Access-Key header. Players enter it as the server password in the game's
    // Multiplayer Settings. Printable ASCII without spaces, at most 256 characters.
    public string AccessKey { get; set; } = "";

    // Lobbies are written to this file on shutdown and read back at the next start, relative to
    // the server's folder. It holds invite codes and UDP secrets. Empty turns it off.
    public string StateFile { get; set; } = "";
    // A state file older than this at startup is ignored.
    public int StateMaxAgeSeconds { get; set; } = 120;

    // GET /v1/stats and the presence reports that feed it. Both are off by default.
    public MilanStatsOptions Stats { get; set; } = new();

    /// <summary>
    /// Deprioritization stats for lobbies
    /// </summary>
    public MilanReachabilityOptions Reachability { get; set; } = new();
}

/// <summary>
/// Stores reachability information for a lobby
/// </summary>
public sealed class MilanReachabilityOptions
{

    /// <summary>
    /// If the reachability options are actually doing anything
    /// </summary>
    public bool Enabled { get; set; } = true;

    /// <summary>
    /// How many failures triggers a lobby being suspicious and restricting how many in-progress invites are given to it
    /// </summary>
    public int SuspectAfterFailures { get; set; } = 2;


    /// <summary>
    /// How many failures marks a lobby unreachable and stops offering it entirely
    /// </summary>
    public int UnreachableAfterFailures { get; set; } = 4;

    /// <summary>
    /// How long to probe a lobby for reachability after offering a client to it
    /// </summary>
    public int ProbeSeconds { get; set; } = 180;

    /// <summary>
    /// How long a failure counts for
    /// </summary>
    public int FailureSeconds { get; set; } = 900;

    /// <summary>
    /// Number of times a joiner that fails to join lobbies stops trying to join. These don't count against lobbies as failures
    /// </summary>
    public int JoinerFailureLimit { get; set; } = 5;

    /// <summary>
    /// Number of unproven joins to offer before widening to proven
    /// </summary>
    public int UnprovenPendingJoins { get; set; } = 1;
}

/// <summary>
/// Live part of Milan that clients get on game startup
/// </summary>
public sealed class MilanLiveOptions
{
    public string ExpectedGameVersion { get; set; } = "";
    // Exact game versions that may not host, search, or redeem invites
    public string[] BlockedGameVersions { get; set; } = [];
    public MilanNoticeOptions Notice { get; set; } = new();
    public MilanLiveStatsOptions Stats { get; set; } = new();
}

/// <summary>
/// The live part of the "Milan:Stats" section
/// </summary>
public sealed class MilanLiveStatsOptions
{
    // GET /v1/stats/galaxy/{gameVersion} for the lobby's Galaxy at War map
    public MilanGalaxyStatsOptions Galaxy { get; set; } = new();
}

/// <summary>
/// Handles message of the day system
/// </summary>
public sealed class MilanNoticeOptions
{
    /// <summary>
    /// One-time ID that clients track
    /// </summary>
    public string Id { get; set; } = "";

    /// <summary>
    /// Language text for the MOTD
    /// </summary>
    public Dictionary<string, string> Text { get; set; } = [];
}

public enum StatsAccess
{
    // GET /v1/stats answers 404 without the token.
    None,
    // Players, searching, and public and open lobbies (plus idle and online with presence).
    Summary,
    // Everything, as the token gets.
    Full,
}

// The "Milan:Stats" section, e.g. Milan__Stats__Public=Summary or Milan__Stats__Token=....
public sealed class MilanStatsOptions
{
    // What GET /v1/stats shows to anyone.
    public StatsAccess Public { get; set; } = StatsAccess.None;

    // Sent as "Authorization: Bearer <token>", this always gets the full statistics. Empty
    // means no token works. It is a secret: keep it out of game clients and web pages.
    public string Token { get; set; } = "";

    // How long a computed snapshot is served for; 0 computes one per request.
    public int CacheSeconds { get; set; } = 5;

    // Presence: a game that is in no registered lobby and no search reports that it is
    // running, so the statistics can count it. It costs one request per such game every
    // PresenceSeconds, so it is off by default; /v1/info advertises the interval, and games
    // send nothing unless it does.
    public bool Presence { get; set; }
    public int PresenceSeconds { get; set; } = 300;

    // Reports past either cap are dropped, so the count cannot be inflated without limit.
    public int MaxPresence { get; set; } = 100_000;
    public int MaxPresencePerAddress { get; set; } = 16;

    // Stats:Galaxy is a live setting: see MilanLiveStatsOptions
}

// The "Milan:Stats:Galaxy" section of the options file. Live: GalaxySettings checks it and keeps the last good value.
public sealed class MilanGalaxyStatsOptions
{
    public bool Enabled { get; set; } = true;

    /// <summary>
    /// How long between stat updates are recomputed by the server
    /// </summary>
    public int CacheSeconds { get; set; } = 120;

    /// <summary>
    /// Advertised in /v1/info instead of the server's /v1/stats/galaxy endpoint, which lets you redirect
    /// what users use to fetch data. Empty replaces with /v1/stats/galaxy which is the server version.
    /// Clients append "/{gameVersion}" to it, so a CDN must serve one file per version
    /// </summary>
    public string Url { get; set; } = "";

    /// <summary>
    /// Population that changes low population tinting, 0 turns the base tint off
    /// </summary>
    public int FullPopulation { get; set; } = 200;

    /// <summary>
    /// The tint of the low-population color
    /// </summary>
    public double MaxBaseTint { get; set; } = 0.4;
}

public sealed class MilanServerOptionsValidator : IValidateOptions<MilanServerOptions>
{
    public ValidateOptionsResult Validate(string? name, MilanServerOptions options)
    {
        List<string> errors = [];

        if (options.HeartbeatSeconds < 1)
        {
            errors.Add("HeartbeatSeconds must be at least 1.");
        }

        if (options.LobbyLeaseSeconds <= options.HeartbeatSeconds)
        {
            errors.Add("LobbyLeaseSeconds must exceed HeartbeatSeconds.");
        }

        if (options.ReservationSeconds < 1)
        {
            errors.Add("ReservationSeconds must be at least 1.");
        }

        if (options.MapWidenSeconds < 0
            || options.EnemyWidenSeconds < options.MapWidenSeconds
            || options.SearchSeconds < options.EnemyWidenSeconds
            || options.SearchSeconds < 1)
        {
            errors.Add("Search timings must satisfy 0 <= MapWidenSeconds <= EnemyWidenSeconds <= SearchSeconds, with SearchSeconds >= 1.");
        }
        if (options.InProgressAfterSeconds < 0 || options.InProgressAfterSeconds > options.SearchSeconds)
        {
            errors.Add("InProgressAfterSeconds must be between 0 and SearchSeconds.");
        }

        if (options.PreferredPartySize < 0 || options.PreferredPartySize > options.MaxPlayersCeiling)
        {
            errors.Add("PreferredPartySize must be between 0 and MaxPlayersCeiling.");
        }

        if (options.PartySizeWidenSeconds < 0 || options.PartySizeWidenSeconds > options.SearchSeconds)
        {
            errors.Add("PartySizeWidenSeconds must be between 0 and SearchSeconds.");
        }

        if (options.MaxSearchLobbySize < 0 || options.MaxSearchLobbySize > options.MaxPlayersCeiling)
        {
            errors.Add("MaxSearchLobbySize must be between 0 and MaxPlayersCeiling.");
        }

        if (options.SearchPollMilliseconds < 100)
        {
            errors.Add("SearchPollMilliseconds must be at least 100.");
        }

        if (options.HostChallengeSeconds < 1 || options.MigrationReportSeconds < 1 || options.MigrationJoinSeconds < 1)
        {
            errors.Add("HostChallengeSeconds, MigrationReportSeconds, and MigrationJoinSeconds must be at least 1.");
        }

        if (options.LongPollSeconds is < 1 or > 120)
        {
            errors.Add("LongPollSeconds must be between 1 and 120.");
        }

        if (options.UdpPort is < 0 or > 65535)
        {
            errors.Add("UdpPort must be between 0 and 65535.");
        }

        if (options.DiscoveryMinIntervalMilliseconds < 0)
        {
            errors.Add("DiscoveryMinIntervalMilliseconds must not be negative.");
        }

        if (options.MaxPlayersCeiling is < 1 or > 64)
        {
            errors.Add("MaxPlayersCeiling must be between 1 and 64.");
        }

        if (options.StateMaxAgeSeconds is < 1 or > 3600)
        {
            errors.Add("StateMaxAgeSeconds must be between 1 and 3600.");
        }

        if (options.MaxLobbies < 1 || options.MaxSearches < 1
            || options.MaxLobbiesPerAddress < 1 || options.MaxSearchesPerAddress < 1
            || options.MaxReservationsPerAddress < 1)
        {
            errors.Add("Lobby, search, and reservation limits must be at least 1.");
        }
        if (options.RequestsPerMinute < 1 || options.CreatesPerMinute < 1 || options.InviteRedeemsPerMinute < 1)
        {
            errors.Add("Rate limits must be at least 1 per minute.");
        }

        if (options.InviteCodeLength is < Secrets.MinInviteCodeLength or > Secrets.MaxInviteCodeLength)
        {
            errors.Add($"InviteCodeLength must be between {Secrets.MinInviteCodeLength} and {Secrets.MaxInviteCodeLength}.");
        }
        // New codes are drawn until one is unused, so at least half of them must stay free.
        else if (options.MaxLobbies > Secrets.InviteCodeCount(options.InviteCodeLength) / 2)
        {
            errors.Add($"MaxLobbies must be at most {Secrets.InviteCodeCount(options.InviteCodeLength) / 2} with {options.InviteCodeLength}-character invite codes.");
        }

        if (options.TotalRequestsPerSecond < 1 || options.RequestBurst < 1
            || options.MaxConcurrentRequests < 1 || options.MaxConcurrentRequestsPerAddress < 1
            || options.MaxConcurrentConnections < 1 || options.DiscoveryPacketsPerSecond < 1
            || options.MaxConcurrentLongPolls < 1 || options.MaxConcurrentLongPollsPerAddress < 1
            || options.DiscoveryPacketsPerSecondPerAddress < 1)
        {
            errors.Add("Concurrency and UDP work limits must be at least 1.");
        }

        MilanStatsOptions stats = options.Stats;
        if (stats.CacheSeconds is < 0 or > 3600)
        {
            errors.Add("Stats:CacheSeconds must be between 0 and 3600.");
        }

        if (stats.Token.Length is > 0 and < 16)
        {
            errors.Add("Stats:Token must be at least 16 characters, or empty.");
        }

        if (stats.PresenceSeconds is < 30 or > 3600)
        {
            errors.Add("Stats:PresenceSeconds must be between 30 and 3600.");
        }

        if (stats.MaxPresence < 1 || stats.MaxPresencePerAddress < 1)
        {
            errors.Add("Stats:MaxPresence and Stats:MaxPresencePerAddress must be at least 1.");
        }

        MilanReachabilityOptions reachability = options.Reachability;
        if (reachability.SuspectAfterFailures < 1 || reachability.UnreachableAfterFailures < reachability.SuspectAfterFailures)
        {
            errors.Add("Reachability thresholds must satisfy 1 <= SuspectAfterFailures <= UnreachableAfterFailures.");
        }
        if (reachability.ProbeSeconds < 1 || reachability.FailureSeconds < 60)
        {
            errors.Add("Reachability:ProbeSeconds must be at least 1 and Reachability:FailureSeconds at least 60.");
        }
        if (reachability.JoinerFailureLimit < 2 || reachability.UnprovenPendingJoins < 1)
        {
            errors.Add("Reachability:JoinerFailureLimit must be at least 2 and Reachability:UnprovenPendingJoins at least 1.");
        }
        // The game only sends keys it can put in an HTTP header unchanged.
        if (options.AccessKey.Length > 256 || options.AccessKey.Any(c => c is <= ' ' or >= '\x7F'))
        {
            errors.Add("AccessKey must be printable ASCII without spaces, at most 256 characters.");
        }

        foreach (string proxy in options.TrustedProxies)
        {
            if (!ProxyTrust.IsValidEntry(proxy))
            {
                errors.Add($"TrustedProxies entry '{proxy}' is not an IP address or CIDR range.");
            }
        }

        return errors.Count == 0 ? ValidateOptionsResult.Success : ValidateOptionsResult.Fail(errors);
    }
}
