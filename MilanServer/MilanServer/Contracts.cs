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
public sealed record LobbySnapshot(
    long Revision,
    bool IsPrivate,
    bool Joinable,
    int MapId,
    int EnemyModeId,
    int DifficultyId,
    int PlayerCount,
    int MaxPlayers,
    MatchPhase Phase,
    int WaveNumber,
    int GamePort,
    IReadOnlyList<string>? LanAddresses = null,
    int? ResolvedMapId = null,
    int? ResolvedEnemyId = null,
    bool MemberInvites = false,
    // Authoritative admitted reservation IDs. Only the listen host may grant/revoke membership.
    IReadOnlyList<string>? MemberIds = null);

public sealed record CreateLobbyRequest(string GameVersion, LobbySnapshot State);

// Credentials for UDP endpoint discovery (see Discovery.cs): a 16-byte ticket ID and
// a 32-byte HMAC secret, both base64url. The server's UDP port is in /v1/info.
public sealed record UdpTicket(string TicketId, string Secret);

public sealed record CreateLobbyResponse(
    string LobbyId,
    string HostKey,
    string InviteCode,
    long Revision,
    DateTimeOffset ExpiresAt,
    int HeartbeatSeconds,
    UdpTicket UdpTicket);

// Stale is true when the report was older than the stored revision and was ignored.
// The lease is refreshed either way.
public sealed record LobbyStateResponse(long Revision, bool Stale, DateTimeOffset ExpiresAt);

public sealed record InviteResponse(string InviteCode);

public sealed record SearchRequest(
    string GameVersion,
    int MapId = GameSettingIds.AnyMap,
    int EnemyModeId = GameSettingIds.AnyEnemy,
    int DifficultyId = GameSettingIds.AnyDifficulty,
    bool Widen = true,
    IReadOnlyList<string>? ExcludeLobbyIds = null);

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

public sealed record LobbySummary(
    string LobbyId,
    int MapId,
    int EnemyModeId,
    int DifficultyId,
    int PlayerCount,
    int MaxPlayers,
    MatchPhase Phase,
    int WaveNumber);

// SamePublicAddress is only a locality hint: CGNAT can put unrelated households
// behind one address, so clients must still probe LAN candidates before using them.
// TraversalKey is shared with the host (through its introduction) to authenticate
// direct peer probes; the server never sees those probes.
public sealed record JoinReservation(
    string ReservationId,
    string JoinKey,
    LobbySummary Lobby,
    IReadOnlyList<EndpointCandidate> HostCandidates,
    bool SamePublicAddress,
    DateTimeOffset ExpiresAt,
    UdpTicket UdpTicket,
    string TraversalKey);

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

public sealed record Introduction(
    string ReservationId,
    string TraversalKey,
    IReadOnlyList<EndpointCandidate> JoinerCandidates,
    bool SamePublicAddress,
    bool Joined,
    DateTimeOffset ExpiresAt);

// Every live reservation for the lobby, not just changed ones.
public sealed record IntroductionList(long Version, IReadOnlyList<Introduction> Introductions);

// A host-confirmed joiner becomes a member, under its reservation ID and join
// key, and stays one until it leaves. Members only talk to the server when they lose the
// host. CanHost says whether this member could take over (it has the lobby's state).
public sealed record HostLostReport(bool CanHost = true);

// What the new host needs to take the lobby over: its new host key and UDP ticket, and
// the last reported state and revision to continue from. ExpectedPlayers counts the new
// host and every member the server is sending to join it.
public sealed record HostHandoff(
    string LobbyId,
    string HostKey,
    string InviteCode,
    long Revision,
    DateTimeOffset ExpiresAt,
    int HeartbeatSeconds,
    UdpTicket UdpTicket,
    int ExpectedPlayers,
    LobbySnapshot State);

// Member long-poll result. Host is set for NewHost and Reservation for JoinNewHost; that
// reservation's host candidates stay empty until the new host reports its game socket.
public sealed record MigrationStatus(long Version, MigrationState State, HostHandoff? Host = null,
    JoinReservation? Reservation = null);

public sealed record ServerInfo(
    int ProtocolVersion,
    int HeartbeatSeconds,
    int LobbyLeaseSeconds,
    int ReservationSeconds,
    int SearchSeconds,
    int MaxPlayersCeiling,
    int LongPollSeconds,
    int UdpPort,
    // How often a game in no lobby and no search reports presence; 0 means do not.
    int PresenceSeconds = 0,
    // Milan:ExpectedGameVersion, when set. Setting outdated makes it compare the versions
    string? LatestGameVersion = null,
    bool? Outdated = null,
    // MOTD
    NoticeText? Notice = null);

// Id changes whenever the game should show the message again
public sealed record NoticeText(string Id, string Text);

// GET /v1/stats: counts only, nothing that identifies a lobby or a player.
//  - Players are those in registered lobbies, as their hosts report them. A 1-player lobby is
//    never registered, and a player in the menus is not in a lobby.
//  - Idle and Online need presence reports (Milan:Stats:Presence): Idle is the games in no
//    lobby and no search, and Online is Players + Searching + Idle.
//  - Details comes with Milan:Stats:Public=Full or the statistics token.
public sealed record ServerStats(
    DateTimeOffset GeneratedAt,
    int Players,
    int Searching,
    int PublicLobbies,
    // Public, joinable, and with a free slot: what a search can match.
    int OpenLobbies,
    int? Idle = null,
    int? Online = null,
    StatsDetails? Details = null);

public sealed record StatsDetails(
    DateTimeOffset StartedAt,
    long UptimeSeconds,
    int Lobbies,
    int FriendsLobbies,
    int PrivateLobbies,
    int LobbiesWaiting,
    int LobbiesInProgress,
    int PlayerCapacity,
    int PendingJoins,
    int HostMigrations,
    // Highest counts since the server started, sampled every few seconds.
    int PeakPlayers,
    int PeakLobbies,
    StatsTotals Totals,
    // Lobbies per game version, the most common first; at most 20 versions.
    IReadOnlyDictionary<string, int> GameVersions);

// Since the server started.
public sealed record StatsTotals(
    long LobbiesCreated,
    long LobbiesExpired,
    long SearchesStarted,
    long SearchesMatched,
    long InvitesRedeemed,
    long JoinsCompleted,
    long HostMigrationsStarted,
    long HostMigrationsCompleted,
    long HostMigrationsFailed);

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
