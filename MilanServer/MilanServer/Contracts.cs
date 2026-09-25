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
    // Host-reported private address (RFC 1918, link-local, or 100.64/10) with the game port.
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

public sealed record CreateLobbyResponse(
    string LobbyId,
    string HostKey,
    string InviteCode,
    long Revision,
    DateTimeOffset ExpiresAt,
    int HeartbeatSeconds);

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
public sealed record JoinReservation(
    string ReservationId,
    string JoinKey,
    LobbySummary Lobby,
    IReadOnlyList<EndpointCandidate> HostCandidates,
    bool SamePublicAddress,
    DateTimeOffset ExpiresAt);

public sealed record ServerInfo(
    int ProtocolVersion,
    int HeartbeatSeconds,
    int LobbyLeaseSeconds,
    int ReservationSeconds,
    int SearchSeconds,
    int MaxPlayersCeiling);

public sealed record ServerError(string Code, string Message);
