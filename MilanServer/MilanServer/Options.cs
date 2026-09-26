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
    public int SearchPollMilliseconds { get; set; } = 2000;
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

    // Reverse proxies whose X-Forwarded-For header is trusted. Loopback is always trusted.
    public string[] TrustedProxies { get; set; } = [];
}

public sealed class MilanServerOptionsValidator : IValidateOptions<MilanServerOptions>
{
    public ValidateOptionsResult Validate(string? name, MilanServerOptions options)
    {
        List<string> errors = [];

        if (options.HeartbeatSeconds < 1)
            errors.Add("HeartbeatSeconds must be at least 1.");
        if (options.LobbyLeaseSeconds <= options.HeartbeatSeconds)
            errors.Add("LobbyLeaseSeconds must exceed HeartbeatSeconds.");
        if (options.ReservationSeconds < 1)
            errors.Add("ReservationSeconds must be at least 1.");
        if (options.MapWidenSeconds < 0
            || options.EnemyWidenSeconds < options.MapWidenSeconds
            || options.SearchSeconds < options.EnemyWidenSeconds
            || options.SearchSeconds < 1)
        {
            errors.Add("Search timings must satisfy 0 <= MapWidenSeconds <= EnemyWidenSeconds <= SearchSeconds, with SearchSeconds >= 1.");
        }
        if (options.InProgressAfterSeconds < 0 || options.InProgressAfterSeconds > options.SearchSeconds)
            errors.Add("InProgressAfterSeconds must be between 0 and SearchSeconds.");
        if (options.SearchPollMilliseconds < 100)
            errors.Add("SearchPollMilliseconds must be at least 100.");
        if (options.LongPollSeconds is < 1 or > 120)
            errors.Add("LongPollSeconds must be between 1 and 120.");
        if (options.UdpPort is < 0 or > 65535)
            errors.Add("UdpPort must be between 0 and 65535.");
        if (options.DiscoveryMinIntervalMilliseconds < 0)
            errors.Add("DiscoveryMinIntervalMilliseconds must not be negative.");
        if (options.MaxPlayersCeiling is < 1 or > 64)
            errors.Add("MaxPlayersCeiling must be between 1 and 64.");

        if (options.MaxLobbies < 1 || options.MaxSearches < 1
            || options.MaxLobbiesPerAddress < 1 || options.MaxSearchesPerAddress < 1
            || options.MaxReservationsPerAddress < 1)
        {
            errors.Add("Lobby, search, and reservation limits must be at least 1.");
        }
        if (options.RequestsPerMinute < 1 || options.CreatesPerMinute < 1 || options.InviteRedeemsPerMinute < 1)
            errors.Add("Rate limits must be at least 1 per minute.");

        foreach (string proxy in options.TrustedProxies)
        {
            if (!System.Net.IPAddress.TryParse(proxy, out _))
                errors.Add($"TrustedProxies entry '{proxy}' is not an IP address.");
        }

        return errors.Count == 0 ? ValidateOptionsResult.Success : ValidateOptionsResult.Fail(errors);
    }
}
