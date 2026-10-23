namespace MilanServer;

public sealed record Failure(int StatusCode, string Code, string Message)
{
    public ServerError ToError() => new(Code, Message);
}

// Either a value or an expected, client-visible failure.
public readonly struct Outcome<T>
{
    readonly T? _value;

    Outcome(T value, Failure? failure)
    {
        _value = value;
        Failure = failure;
    }

    public Failure? Failure { get; }

    public bool IsSuccess => Failure is null;

    public T Value => Failure is null ? _value! : throw new InvalidOperationException($"Outcome failed: {Failure.Code}");

    public static implicit operator Outcome<T>(T value) => new(value, null);

    public static implicit operator Outcome<T>(Failure failure) => new(default!, failure);
}

public static class Failures
{
    public static Failure Invalid(string message) => new(400, "invalid_request", message);

    // Wrong keys and missing objects share one response so neither reveals the other.
    public static readonly Failure LobbyNotFound =
        new(404, "lobby_not_found", "The lobby does not exist, has expired, or the host key is wrong.");
    public static readonly Failure SearchNotFound =
        new(404, "search_not_found", "The search does not exist, has expired, or the search key is wrong.");
    public static readonly Failure ReservationNotFound =
        new(404, "reservation_not_found", "The reservation does not exist, has expired, or the join key is wrong.");
    public static readonly Failure InviteNotFound =
        new(404, "invite_not_found", "The invite code is unknown or its lobby has expired.");
    public static readonly Failure MemberNotFound =
        new(404, "member_not_found", "The member does not exist, its lobby has closed, or the member key is wrong.");
    // The key was valid, but the host was declared lost and another member took the lobby
    // over. Unlike lobby_not_found, the old host must not register the lobby again.
    public static readonly Failure HostReplaced =
        new(409, "host_replaced", "This lobby has a new host after the previous one stopped answering.");

    public static readonly Failure AccessKeyInvalid =
        new(401, "access_key_invalid", "This server needs an access key, and the request's is missing or wrong.");
    public static readonly Failure HttpsRequired =
        new(426, "https_required", "Server passwords and statistics tokens require HTTPS. Use an https:// server URL.");
    public static readonly Failure RequestLimit =
        new(429, "request_limit", "Too many concurrent requests. Retry after an active request finishes.");
    public static readonly Failure PollBusy =
        new(429, "poll_busy", "A poll is already waiting for this credential.");

    public static readonly Failure StatsUnavailable =
        new(404, "stats_unavailable", "Statistics are not public on this server.");
    public static readonly Failure StatsTokenInvalid =
        new(401, "stats_token_invalid", "The statistics token is wrong.");
    public static readonly Failure PresenceDisabled =
        new(404, "presence_disabled", "This server does not take presence reports.");

    public static readonly Failure InvitesHostOnly =
        new(403, "invites_host_only", "Only the host can invite players to this lobby.");

    public static readonly Failure LobbyFull = new(409, "lobby_full", "The lobby has no free slots.");
    public static readonly Failure LobbyNotJoinable = new(409, "lobby_not_joinable", "The lobby is not accepting joins.");
    public static readonly Failure VersionMismatch =
        new(409, "version_mismatch", "The lobby is running a different game version.");
    public static readonly Failure RevisionConflict =
        new(409, "revision_conflict", "A different state was already reported with this revision.");

    public static readonly Failure AddressLimit =
        new(429, "address_limit", "Too many active lobbies, searches, or reservations from this network address.");
    public static readonly Failure ServerFull = new(503, "server_full", "The server has reached its lobby or search limit.");
}
