using System.Text.Json.Serialization;

namespace MilanServer;

// Wire contracts for the v1 HTTP control API. They are provisional until the
// MilanHost side of the protocol exists; bump ProtocolVersion on breaking changes.
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
    // Peer-reported private address (RFC 1918, link-local, or 100.64/10) with its game port.
    Lan,
    // Address the host's control request came from, with the host-reported game port.
    // Unverified: only reachable when the host has a manual or router-mapped forward.
    PublicReported
}

// Full host-reported lobby state. Every report replaces the previous one; a higher
// Revision wins, so reordered requests cannot roll state back. EnemyModeId is the
// selected option and stays 0 for Random Faction after the game resolves a faction.
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
    int? ResolvedEnemyId = null);

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

public sealed record CreateSearchResponse(string SearchId, string SearchKey, SearchStatus Status);

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

public sealed record ServerInfo(
    int ProtocolVersion,
    int HeartbeatSeconds,
    int LobbyLeaseSeconds,
    int ReservationSeconds,
    int SearchSeconds,
    int MaxPlayersCeiling,
    int LongPollSeconds,
    int UdpPort);

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
[JsonSerializable(typeof(ServerError))]
sealed partial class MilanJsonContext : JsonSerializerContext;
