using System.Text.Json.Serialization;

namespace MilanServer;

// Wire contracts for the /v1 HTTP control API. /v1/info advertises the control
// protocol revision; bump ProtocolVersion on breaking client compatibility changes.
//
// Map, enemy, and difficulty IDs are opaque: the game may add new ones without a
// server update. Only the search "Any" sentinels below carry meaning here.

public static class GameSettingIds
{
    // Search sentinels from SFXOnlineGameSettings.EnsureMatchSettingsAreValid.
    public const int AnyMap = -1;
    public const int AnyEnemy = 127;
    public const int AnyDifficulty = 127;
}

[JsonConverter(typeof(JsonStringEnumConverter<MatchPhase>))]
public enum MatchPhase
{
    Lobby,
    InProgress,
    Extraction,
    Complete
}

[JsonConverter(typeof(JsonStringEnumConverter<SearchState>))]
public enum SearchState
{
    Searching,
    Matched,
    NoMatch
}

[JsonConverter(typeof(JsonStringEnumConverter<SearchStage>))]
public enum SearchStage
{
    Exact,
    AnyMap,
    AnyMapAndEnemy
}

[JsonConverter(typeof(JsonStringEnumConverter<CandidateKind>))]
public enum CandidateKind
{
    // Source address and port of the peer's authenticated UDP binding request. This is
    // the game socket's NAT mapping only if the peer sent it from that socket.
    PublicObserved,
    // Peer-reported private address (RFC 1918 or 100.64/10) with its game port.
    Lan,
    // Address the host's control request came from, with the host-reported game port.
    // Unverified: only reachable when the host has a manual or router-mapped forward.
    PublicReported
}

/// <summary>
/// Effectiveness of clients joining a lobby, used to deprioritize lobbies that suck
/// </summary>
[JsonConverter(typeof(JsonStringEnumConverter<LobbyReachability>))]
public enum LobbyReachability
{
    /// <summary>
    /// A player has successfully joined this lobby
    /// </summary>
    Proven,
    /// <summary>
    /// Nobody has yet successfully joined this lobby
    /// </summary>
    Unknown,
    /// <summary>
    /// Some players have failed to join this lobby
    /// </summary>
    Suspect,
    /// <summary>
    /// Many users cannot join this lobby and will not be offered to others
    /// </summary>
    Unreachable,
}

// Where a member stands after reporting that it lost its host.
[JsonConverter(typeof(JsonStringEnumConverter<MigrationState>))]
public enum MigrationState
{
    // The server is waiting to see whether the host still answers.
    Checking,
    // The host is gone; the server is waiting for the other members to report.
    Electing,
    // The host answered, so this member lost its own connection and should leave.
    HostAlive,
    // This member hosts the lobby now; Host carries its credentials.
    NewHost,
    // Another member hosts the lobby now; Reservation joins it.
    JoinNewHost,
}

// Full host-reported lobby state. Every report replaces the previous one; a higher
// Revision wins, so reordered requests cannot roll state back. EnemyModeId is the
// selected option and stays 0 for Random Faction after the game resolves a faction.
// IsPrivate keeps the lobby out of searches. MemberInvites lets its members, not only the host,
// get the invite code. The game's privacy modes map to the two: Public is listed and members
// invite, Friends is unlisted and members invite, Private is unlisted and only the host invites.
public sealed record LobbySnapshot
{
    public required long Revision { get; init; }
    public required bool IsPrivate { get; init; }
    public required bool Joinable { get; init; }
    public required int MapId { get; init; }
    public required int EnemyModeId { get; init; }
    public required int DifficultyId { get; init; }
    public required int PlayerCount { get; init; }
    public required int MaxPlayers { get; init; }
    public required MatchPhase Phase { get; init; }
    public required int WaveNumber { get; init; }
    public required int GamePort { get; init; }
    public IReadOnlyList<string>? LanAddresses { get; init; }
    public int? ResolvedMapId { get; init; }
    public int? ResolvedEnemyId { get; init; }
    public bool MemberInvites { get; init; }
    // Authoritative admitted reservation IDs. Only the listen host may grant/revoke membership.
    public IReadOnlyList<string>? MemberIds { get; init; }
}

public sealed record CreateLobbyRequest(string GameVersion, LobbySnapshot State);

// Credentials for UDP endpoint discovery (see Discovery.cs): a 16-byte ticket ID and
// a 32-byte HMAC secret, both base64url. The server's UDP port is in /v1/info.
public sealed record UdpTicket(string TicketId, string Secret);

public sealed record CreateLobbyResponse
{
    public required string LobbyId { get; init; }
    public required string HostKey { get; init; }
    public required string InviteCode { get; init; }
    public required long Revision { get; init; }
    public required DateTimeOffset ExpiresAt { get; init; }
    public required int HeartbeatSeconds { get; init; }
    public required UdpTicket UdpTicket { get; init; }
}

// Stale is true when the report was older than the stored revision and was ignored.
// The lease is refreshed either way. FailedJoins is the count of players that
// failed to join the lobby so the host will know it's bad
public sealed record LobbyStateResponse(long Revision, bool Stale, DateTimeOffset ExpiresAt,
    LobbyReachability Reachability = LobbyReachability.Unknown, int FailedJoins = 0);

public sealed record InviteResponse(string InviteCode);

public sealed record SearchRequest
{
    public required string GameVersion { get; init; }
    public int MapId { get; init; } = GameSettingIds.AnyMap;
    public int EnemyModeId { get; init; } = GameSettingIds.AnyEnemy;
    public int DifficultyId { get; init; } = GameSettingIds.AnyDifficulty;
    public bool Widen { get; init; } = true;
    public IReadOnlyList<string>? ExcludeLobbyIds { get; init; }
    /// <summary>
    /// If player has elected to matchmake into expanded lobbies
    /// </summary>
    public bool? ExpandedLobbies { get; init; }
}

// ExpectedGameVersion is Milan:ExpectedGameVersion when the search's version differs from it,
// so the game can tell the player that few others will match. The search runs either way.
public sealed record CreateSearchResponse(string SearchId, string SearchKey, SearchStatus Status,
    string? ExpectedGameVersion = null);

public sealed record SearchStatus(
    SearchState State,
    SearchStage Stage,
    DateTimeOffset ExpiresAt,
    int PollAfterMilliseconds,
    JoinReservation? Reservation = null);

public sealed record RedeemInviteRequest(string InviteCode, string GameVersion);

public sealed record EndpointCandidate(string Address, int Port, CandidateKind Kind);

public sealed record LobbySummary
{
    public required string LobbyId { get; init; }
    public required int MapId { get; init; }
    public required int EnemyModeId { get; init; }
    public required int DifficultyId { get; init; }
    public required int PlayerCount { get; init; }
    public required int MaxPlayers { get; init; }
    public required MatchPhase Phase { get; init; }
    public required int WaveNumber { get; init; }
}

// SamePublicAddress is only a locality hint: CGNAT can put unrelated households
// behind one address, so clients must still probe LAN candidates before using them.
// TraversalKey is shared with the host (through its introduction) to authenticate
// direct peer probes; the server never sees those probes.
public sealed record JoinReservation
{
    public required string ReservationId { get; init; }
    public required string JoinKey { get; init; }
    public required LobbySummary Lobby { get; init; }
    public required IReadOnlyList<EndpointCandidate> HostCandidates { get; init; }
    public required bool SamePublicAddress { get; init; }
    public required DateTimeOffset ExpiresAt { get; init; }
    public required UdpTicket UdpTicket { get; init; }
    public required string TraversalKey { get; init; }
}

// Joiner-reported once its game socket exists, so the host can also try the LAN path.
public sealed record JoinerCandidatesRequest(int GamePort, IReadOnlyList<string>? LanAddresses = null);

// Long-poll results carry the lobby's event version; pass it back as "after" to wait
// for the next change.
public sealed record ReservationStatus(
    long Version,
    IReadOnlyList<EndpointCandidate> HostCandidates,
    bool SamePublicAddress,
    bool Joined,
    DateTimeOffset ExpiresAt);

public sealed record Introduction
{
    public required string ReservationId { get; init; }
    public required string TraversalKey { get; init; }
    public required IReadOnlyList<EndpointCandidate> JoinerCandidates { get; init; }
    public required bool SamePublicAddress { get; init; }
    public required bool Joined { get; init; }
    public required DateTimeOffset ExpiresAt { get; init; }
}

// Every live reservation for the lobby, not just changed ones.
public sealed record IntroductionList(long Version, IReadOnlyList<Introduction> Introductions);

// A host-confirmed joiner becomes a member, under its reservation ID and join
// key, and stays one until it leaves. Members only talk to the server when they lose the
// host. CanHost says whether this member could take over (it has the lobby's state).
public sealed record HostLostReport(bool CanHost = true);

// What the new host needs to take the lobby over: its new host key and UDP ticket, and
// the last reported state and revision to continue from. ExpectedPlayers counts the new
// host and every member the server is sending to join it.
public sealed record HostHandoff
{
    public required string LobbyId { get; init; }
    public required string HostKey { get; init; }
    public required string InviteCode { get; init; }
    public required long Revision { get; init; }
    public required DateTimeOffset ExpiresAt { get; init; }
    public required int HeartbeatSeconds { get; init; }
    public required UdpTicket UdpTicket { get; init; }
    public required int ExpectedPlayers { get; init; }
    public required LobbySnapshot State { get; init; }
}

// Member long-poll result. Host is set for NewHost and Reservation for JoinNewHost; that
// reservation's host candidates stay empty until the new host reports its game socket.
public sealed record MigrationStatus(long Version, MigrationState State, HostHandoff? Host = null,
    JoinReservation? Reservation = null);

public sealed record ServerInfo
{
    public required int ProtocolVersion { get; init; }
    public required int HeartbeatSeconds { get; init; }
    public required int LobbyLeaseSeconds { get; init; }
    public required int ReservationSeconds { get; init; }
    public required int SearchSeconds { get; init; }
    public required int MaxPlayersCeiling { get; init; }
    public required int LongPollSeconds { get; init; }
    public required int UdpPort { get; init; }
    // How often a game in no lobby and no search reports presence; 0 means do not.
    public int PresenceSeconds { get; init; }
    // Milan:ExpectedGameVersion, when set. Setting outdated makes it compare the versions
    public string? LatestGameVersion { get; init; }
    public bool? Outdated { get; init; }
    // MOTD
    public NoticeText? Notice { get; init; }
}

// Id changes whenever the game should show the message again
public sealed record NoticeText(string Id, string Text);

// GET /v1/stats: counts only, nothing that identifies a lobby or a player.
//  - Players are those in registered lobbies, as their hosts report them. A 1-player lobby is
//    never registered, and a player in the menus is not in a lobby.
//  - Idle and Online need presence reports (Milan:Stats:Presence): Idle is the games in no
//    lobby and no search, and Online is Players + Searching + Idle.
//  - Details comes with Milan:Stats:Public=Full or the statistics token.
public sealed record ServerStats
{
    public required DateTimeOffset GeneratedAt { get; init; }
    public required int Players { get; init; }
    public required int Searching { get; init; }
    public required int PublicLobbies { get; init; }
    // Public, joinable, and with a free slot (4 slots or less)
    public required int OpenLobbies { get; init; }
    // Public, joinable, and with a free slot (5+ slots)
    public required int OpenExpandedLobbies { get; init; }
    public int? Idle { get; init; }
    public int? Online { get; init; }
    public StatsDetails? Details { get; init; }
}

public sealed record StatsDetails
{
    public required DateTimeOffset StartedAt { get; init; }
    public required long UptimeSeconds { get; init; }
    public required int Lobbies { get; init; }
    public required int FriendsLobbies { get; init; }
    public required int PrivateLobbies { get; init; }
    public required int LobbiesWaiting { get; init; }
    public required int LobbiesInProgress { get; init; }
    public required int PlayerCapacity { get; init; }
    public required int PendingJoins { get; init; }
    public required int HostMigrations { get; init; }
    // Highest counts since the server started, sampled every few seconds.
    public required int PeakPlayers { get; init; }
    public required int PeakLobbies { get; init; }
    public required StatsTotals Totals { get; init; }
    // Lobbies per game version, the most common first; at most 20 versions.
    public required IReadOnlyDictionary<string, int> GameVersions { get; init; }
    // Deprioritization lobbies
    public int SuspectLobbies { get; init; }
    public int UnreachableLobbies { get; init; }
    public IReadOnlyDictionary<int, int>? LobbiesByPlayers { get; init; }
    // Public lobbies keyed by MaxPlayers, with how full that set is right now
    public IReadOnlyDictionary<int, LobbySizeFill>? PublicFillBySize { get; init; }
}

// FillPercent is Players / Capacity, rounded to one decimal.
public sealed record LobbySizeFill(int Lobbies, int Players, int Capacity, double FillPercent);

// Since the server started.
public sealed record StatsTotals
{
    public required long LobbiesCreated { get; init; }
    public required long LobbiesExpired { get; init; }
    public required long SearchesStarted { get; init; }
    public required long SearchesMatched { get; init; }
    public required long InvitesRedeemed { get; init; }
    public required long JoinsCompleted { get; init; }
    public required long HostMigrationsStarted { get; init; }
    public required long HostMigrationsCompleted { get; init; }
    public required long HostMigrationsFailed { get; init; }
    public long JoinsFailed { get; init; }
    // Hosting, search, and invite requests refused for a version in BlockedGameVersions
    public long BlockedVersionRequests { get; init; }
}

public sealed record ServerError(string Code, string Message);

// Source-generated JSON metadata: the published server is trimmed, which turns off
// reflection-based serialization. Every request and response body needs a root here;
// nested types are picked up from them. Endpoints serialize with the HTTP JSON options
// in Program.cs. These options only apply where a type is written through this context
// directly, which is ServerError.
[JsonSourceGenerationOptions(System.Text.Json.JsonSerializerDefaults.Web)]
[JsonSerializable(typeof(CreateLobbyRequest))]
[JsonSerializable(typeof(CreateLobbyResponse))]
[JsonSerializable(typeof(LobbySnapshot))]
[JsonSerializable(typeof(LobbyStateResponse))]
[JsonSerializable(typeof(InviteResponse))]
[JsonSerializable(typeof(IntroductionList))]
[JsonSerializable(typeof(SearchRequest))]
[JsonSerializable(typeof(CreateSearchResponse))]
[JsonSerializable(typeof(SearchStatus))]
[JsonSerializable(typeof(RedeemInviteRequest))]
[JsonSerializable(typeof(JoinReservation))]
[JsonSerializable(typeof(JoinerCandidatesRequest))]
[JsonSerializable(typeof(ReservationStatus))]
[JsonSerializable(typeof(ServerInfo))]
[JsonSerializable(typeof(ServerStats))]
[JsonSerializable(typeof(HostLostReport))]
[JsonSerializable(typeof(MigrationStatus))]
[JsonSerializable(typeof(ServerError))]
sealed partial class MilanJsonContext : JsonSerializerContext;
