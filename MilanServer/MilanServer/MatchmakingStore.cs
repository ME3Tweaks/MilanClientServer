using System.Diagnostics.CodeAnalysis;
using System.Net;
using Microsoft.Extensions.Options;

namespace MilanServer;

// In-memory lobby, search, and reservation state. Every public member runs under a
// single lock, so host state reports, search decisions, and capacity reservations are
// atomic with respect to each other. State is deliberately ephemeral: after a restart
// hosts get lobby_not_found on their next report and must create a new lobby.
public sealed class MatchmakingStore
{
    const int MaxGameVersionLength = 64;
    const int MaxLanAddresses = 4;
    const int MaxExcludedLobbies = 16;
    const int MaxWaveNumber = 10_000;
    static readonly TimeSpan SearchRetention = TimeSpan.FromSeconds(60);

    readonly Lock _lock = new();
    readonly MilanServerOptions _options;
    readonly TimeProvider _time;
    readonly ILogger<MatchmakingStore> _logger;

    readonly Dictionary<string, Lobby> _lobbies = new(StringComparer.Ordinal);
    readonly Dictionary<string, Lobby> _lobbiesByInvite = new(StringComparer.Ordinal);
    readonly Dictionary<string, Search> _searches = new(StringComparer.Ordinal);
    readonly Dictionary<string, Reservation> _reservations = new(StringComparer.Ordinal);
    readonly Dictionary<UInt128, UdpTicketRecord> _udpTickets = [];
    long _nextLobbySequence;

    public MatchmakingStore(IOptions<MilanServerOptions> options, TimeProvider time, ILogger<MatchmakingStore> logger)
    {
        _options = options.Value;
        _time = time;
        _logger = logger;
    }

    #region Hosts

    public Outcome<CreateLobbyResponse> CreateLobby(CreateLobbyRequest request, IPAddress? source)
    {
        if (ValidateGameVersion(request.GameVersion) is { } versionFailure)
            return versionFailure;
        if (ValidateSnapshot(request.State, out LobbySnapshot state) is { } stateFailure)
            return stateFailure;

        DateTimeOffset now = _time.GetUtcNow();
        string sourceKey = NetworkAddresses.AccountingKey(source);
        string hostKey = Secrets.NewKey();
        Lobby lobby;
        UdpTicket udpTicket;

        lock (_lock)
        {
            if (_lobbies.Count >= _options.MaxLobbies)
            {
                SweepLocked(now);
                if (_lobbies.Count >= _options.MaxLobbies)
                    return Failures.ServerFull;
            }
            if (_lobbies.Values.Count(l => l.SourceKey == sourceKey && l.ExpiresAt > now) >= _options.MaxLobbiesPerAddress)
                return Failures.AddressLimit;

            lobby = new Lobby
            {
                Id = NewUniqueId(_lobbies),
                Sequence = ++_nextLobbySequence,
                HostKeyHash = Secrets.Hash(hostKey),
                GameVersion = request.GameVersion,
                SourceKey = sourceKey,
                InviteCode = NewUniqueInviteCode(),
                State = state,
                PublicAddress = NetworkAddresses.PublicIPv4(source),
                ExpiresAt = now.AddSeconds(_options.LobbyLeaseSeconds),
            };
            _lobbies.Add(lobby.Id, lobby);
            _lobbiesByInvite.Add(lobby.InviteCode, lobby);
            udpTicket = NewUdpTicketLocked(lobby, null, out UInt128 ticketId);
            lobby.UdpTicketId = ticketId;
        }

        _logger.LogInformation("Created lobby {LobbyId} ({Privacy}, {Players}/{MaxPlayers}).",
            lobby.Id, state.IsPrivate ? "private" : "public", state.PlayerCount, state.MaxPlayers);

        return new CreateLobbyResponse(lobby.Id, hostKey, Secrets.FormatInviteCode(lobby.InviteCode),
            state.Revision, lobby.ExpiresAt, _options.HeartbeatSeconds, udpTicket);
    }

    // Both prompt setting changes and the periodic heartbeat use this full-state report.
    public Outcome<LobbyStateResponse> ReportLobbyState(string lobbyId, string? hostKey, LobbySnapshot snapshot, IPAddress? source)
    {
        if (ValidateSnapshot(snapshot, out LobbySnapshot state) is { } stateFailure)
            return stateFailure;

        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!TryGetHostLobby(lobbyId, hostKey, now, out Lobby? lobby))
                return Failures.LobbyNotFound;

            bool stale = state.Revision < lobby.State.Revision;
            if (state.Revision == lobby.State.Revision && !SameContent(state, lobby.State))
                return Failures.RevisionConflict;

            if (!stale)
            {
                if (!SameContent(state, lobby.State))
                    SignalLocked(lobby);
                lobby.State = state;

                // A current report reflects the host's player count after any joiner that
                // already reported success was admitted, so those slots stop double counting.
                foreach (Reservation joined in lobby.Reservations.Where(r => r.JoinedAt is not null).ToList())
                    RemoveReservationLocked(joined);
            }

            IPAddress? publicAddress = NetworkAddresses.PublicIPv4(source);
            if (publicAddress is not null && !publicAddress.Equals(lobby.PublicAddress))
            {
                lobby.PublicAddress = publicAddress;
                SignalLocked(lobby);
            }
            lobby.ExpiresAt = now.AddSeconds(_options.LobbyLeaseSeconds);
            return new LobbyStateResponse(lobby.State.Revision, stale, lobby.ExpiresAt);
        }
    }

    public Failure? DeleteLobby(string lobbyId, string? hostKey)
    {
        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!TryGetHostLobby(lobbyId, hostKey, now, out Lobby? lobby))
                return Failures.LobbyNotFound;

            RemoveLobbyLocked(lobby);
        }

        _logger.LogInformation("Deleted lobby {LobbyId}.", lobbyId);
        return null;
    }

    public Outcome<InviteResponse> RotateInviteCode(string lobbyId, string? hostKey)
    {
        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!TryGetHostLobby(lobbyId, hostKey, now, out Lobby? lobby))
                return Failures.LobbyNotFound;

            _lobbiesByInvite.Remove(lobby.InviteCode);
            lobby.InviteCode = NewUniqueInviteCode();
            _lobbiesByInvite.Add(lobby.InviteCode, lobby);
            return new InviteResponse(Secrets.FormatInviteCode(lobby.InviteCode));
        }
    }

    #endregion

    #region Quick match

    public Outcome<CreateSearchResponse> CreateSearch(SearchRequest request, IPAddress? source)
    {
        if (ValidateGameVersion(request.GameVersion) is { } versionFailure)
            return versionFailure;
        if (request.MapId < GameSettingIds.AnyMap || request.EnemyModeId < 0 || request.DifficultyId < 0)
            return Failures.Invalid("Search filters must be a setting ID or the matching Any sentinel.");
        if (request.ExcludeLobbyIds is { Count: > MaxExcludedLobbies })
            return Failures.Invalid($"At most {MaxExcludedLobbies} lobbies may be excluded.");

        DateTimeOffset now = _time.GetUtcNow();
        string sourceKey = NetworkAddresses.AccountingKey(source);
        string searchKey = Secrets.NewKey();

        lock (_lock)
        {
            if (_searches.Count >= _options.MaxSearches)
            {
                SweepLocked(now);
                if (_searches.Count >= _options.MaxSearches)
                    return Failures.ServerFull;
            }
            if (_searches.Values.Count(s => s.SourceKey == sourceKey && s.State == SearchState.Searching)
                >= _options.MaxSearchesPerAddress)
            {
                return Failures.AddressLimit;
            }

            Search search = new()
            {
                Id = NewUniqueId(_searches),
                KeyHash = Secrets.Hash(searchKey),
                Request = request,
                Excluded = request.ExcludeLobbyIds?.ToHashSet(StringComparer.Ordinal) ?? [],
                SourceKey = sourceKey,
                PublicAddress = NetworkAddresses.PublicIPv4(source),
                CreatedAt = now,
                Deadline = now.AddSeconds(_options.SearchSeconds),
            };
            _searches.Add(search.Id, search);

            Outcome<SearchStatus> status = EvaluateSearchLocked(search, now);
            if (status.Failure is { } failure)
            {
                _searches.Remove(search.Id);
                return failure;
            }
            return new CreateSearchResponse(search.Id, searchKey, status.Value);
        }
    }

    public Outcome<SearchStatus> PollSearch(string searchId, string? searchKey)
    {
        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!TryGetSearch(searchId, searchKey, now, out Search? search))
                return Failures.SearchNotFound;

            return EvaluateSearchLocked(search, now);
        }
    }

    // Cancelling a matched search also releases its reservation unless the joiner
    // already reported success.
    public Failure? CancelSearch(string searchId, string? searchKey)
    {
        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!TryGetSearch(searchId, searchKey, now, out Search? search))
                return Failures.SearchNotFound;

            _searches.Remove(search.Id);
            if (search.Reservation is { JoinedAt: null } reservation)
                RemoveReservationLocked(reservation);
            return null;
        }
    }

    public static SearchStage StageAt(TimeSpan elapsed, bool widen, MilanServerOptions options)
    {
        if (!widen || elapsed < TimeSpan.FromSeconds(options.MapWidenSeconds))
            return SearchStage.Exact;
        return elapsed < TimeSpan.FromSeconds(options.EnemyWidenSeconds) ? SearchStage.AnyMap : SearchStage.AnyMapAndEnemy;
    }

    Outcome<SearchStatus> EvaluateSearchLocked(Search search, DateTimeOffset now)
    {
        if (search.State != SearchState.Searching)
            return search.Result!;

        SearchStage stage = StageAt(now - search.CreatedAt, search.Request.Widen, _options);
        if (now >= search.Deadline)
        {
            // No automatic lobby creation: the client reports "no match found".
            search.State = SearchState.NoMatch;
            search.Result = new SearchStatus(SearchState.NoMatch, stage, search.Deadline, 0);
            return search.Result;
        }

        Lobby? best = null;
        (int, int, int, int, long) bestRank = default;
        foreach (Lobby lobby in _lobbies.Values)
        {
            if (!IsSearchCandidate(lobby, search, stage, now))
                continue;

            (int, int, int, int, long) rank = Rank(lobby, search.Request);
            if (best is null || rank.CompareTo(bestRank) < 0)
            {
                best = lobby;
                bestRank = rank;
            }
        }

        if (best is null)
            return new SearchStatus(SearchState.Searching, stage, search.Deadline, _options.SearchPollMilliseconds);

        if (CountReservationsFrom(search.SourceKey, now) >= _options.MaxReservationsPerAddress)
            return Failures.AddressLimit;

        JoinReservation reservation = CreateReservationLocked(best, search.SourceKey, search.PublicAddress, now, out Reservation record);
        search.Reservation = record;
        search.State = SearchState.Matched;
        search.Result = new SearchStatus(SearchState.Matched, stage, reservation.ExpiresAt, 0, reservation);
        _logger.LogInformation("Search {SearchId} matched lobby {LobbyId} at stage {Stage}.", search.Id, best.Id, stage);
        return search.Result;
    }

    bool IsSearchCandidate(Lobby lobby, Search search, SearchStage stage, DateTimeOffset now)
    {
        LobbySnapshot state = lobby.State;
        SearchRequest request = search.Request;

        // Private lobbies never appear in search, and extraction is invite-only.
        if (lobby.ExpiresAt <= now
            || state.IsPrivate
            || !state.Joinable
            || state.Phase is not (MatchPhase.Lobby or MatchPhase.InProgress)
            || lobby.GameVersion != request.GameVersion
            || search.Excluded.Contains(lobby.Id)
            || Occupancy(lobby, now) >= state.MaxPlayers)
        {
            return false;
        }

        // Difficulty stays fixed while widening. Filters compare the selected setting,
        // so a Random map lobby never satisfies a specific map, and Random Faction or
        // Coalition match by mode ID rather than the faction currently in play.
        bool mapMatches = stage >= SearchStage.AnyMap || request.MapId == GameSettingIds.AnyMap || state.MapId == request.MapId;
        bool enemyMatches = stage >= SearchStage.AnyMapAndEnemy || request.EnemyModeId == GameSettingIds.AnyEnemy
            || state.EnemyModeId == request.EnemyModeId;
        bool difficultyMatches = request.DifficultyId == GameSettingIds.AnyDifficulty || state.DifficultyId == request.DifficultyId;
        return mapMatches && enemyMatches && difficultyMatches;
    }

    // Lower sorts first: lobbies before matches in progress, then closer matches to the
    // player's original filters, earlier waves, fuller lobbies, and finally older lobbies.
    static (int, int, int, int, long) Rank(Lobby lobby, SearchRequest request)
    {
        LobbySnapshot state = lobby.State;
        int specificity = 0;
        if (request.MapId != GameSettingIds.AnyMap && state.MapId == request.MapId)
            specificity++;
        if (request.EnemyModeId != GameSettingIds.AnyEnemy && state.EnemyModeId == request.EnemyModeId)
            specificity++;

        return (state.Phase == MatchPhase.Lobby ? 0 : 1, -specificity, state.WaveNumber, -state.PlayerCount, lobby.Sequence);
    }

    #endregion

    #region Invites and reservations

    public Outcome<JoinReservation> RedeemInvite(RedeemInviteRequest request, IPAddress? source)
    {
        if (ValidateGameVersion(request.GameVersion) is { } versionFailure)
            return versionFailure;
        if (!Secrets.TryNormalizeInviteCode(request.InviteCode, out string code))
            return Failures.InviteNotFound;

        DateTimeOffset now = _time.GetUtcNow();
        string sourceKey = NetworkAddresses.AccountingKey(source);
        JoinReservation reservation;

        lock (_lock)
        {
            if (!_lobbiesByInvite.TryGetValue(code, out Lobby? lobby) || !IsLive(lobby, now))
                return Failures.InviteNotFound;

            LobbySnapshot state = lobby.State;
            if (lobby.GameVersion != request.GameVersion)
                return Failures.VersionMismatch;
            // Invites work for private lobbies and during extraction.
            if (!state.Joinable || state.Phase == MatchPhase.Complete)
                return Failures.LobbyNotJoinable;
            if (Occupancy(lobby, now) >= state.MaxPlayers)
                return Failures.LobbyFull;
            if (CountReservationsFrom(sourceKey, now) >= _options.MaxReservationsPerAddress)
                return Failures.AddressLimit;

            reservation = CreateReservationLocked(lobby, sourceKey, NetworkAddresses.PublicIPv4(source), now, out _);
        }

        _logger.LogInformation("Invite redeemed for lobby {LobbyId}.", reservation.Lobby.LobbyId);
        return reservation;
    }

    // The joiner reports that the host admitted it. The slot stays held until the
    // host's next current state report, which then includes the joiner in PlayerCount.
    public Failure? MarkReservationJoined(string reservationId, string? joinKey)
    {
        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!TryGetReservation(reservationId, joinKey, now, out Reservation? reservation))
                return Failures.ReservationNotFound;

            if (reservation.JoinedAt is null)
            {
                reservation.JoinedAt = now;
                SignalLocked(reservation.Lobby);
            }
            return null;
        }
    }

    public Failure? ReportJoinerCandidates(string reservationId, string? joinKey, JoinerCandidatesRequest request)
    {
        if (request.GamePort is < 1 or > 65535)
            return Failures.Invalid("GamePort must be between 1 and 65535.");
        if (ValidateLanAddresses(request.LanAddresses, out List<string> lanAddresses) is { } lanFailure)
            return lanFailure;

        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!TryGetReservation(reservationId, joinKey, now, out Reservation? reservation))
                return Failures.ReservationNotFound;

            if (reservation.GamePort != request.GamePort || !reservation.LanAddresses.SequenceEqual(lanAddresses))
            {
                reservation.GamePort = request.GamePort;
                reservation.LanAddresses = lanAddresses;
                SignalLocked(reservation.Lobby);
            }
            return null;
        }
    }

    #endregion

    #region Introductions

    // Host long-poll. Returns at once when the lobby's event version differs from
    // "after", otherwise when something changes or LongPollSeconds pass.
    public async Task<Outcome<IntroductionList>> PollIntroductionsAsync(string lobbyId, string? hostKey, long after,
        CancellationToken cancellationToken)
    {
        Task changed;
        lock (_lock)
        {
            if (!TryGetHostLobby(lobbyId, hostKey, _time.GetUtcNow(), out Lobby? lobby))
                return Failures.LobbyNotFound;
            if (lobby.EventVersion != after)
                return BuildIntroductionsLocked(lobby, _time.GetUtcNow());
            changed = lobby.Changed.Task;
        }

        await WaitForChangeAsync(changed, cancellationToken);

        lock (_lock)
        {
            if (!TryGetHostLobby(lobbyId, hostKey, _time.GetUtcNow(), out Lobby? lobby))
                return Failures.LobbyNotFound;
            return BuildIntroductionsLocked(lobby, _time.GetUtcNow());
        }
    }

    // Joiner long-poll for host candidate changes, with the same semantics.
    public async Task<Outcome<ReservationStatus>> PollReservationAsync(string reservationId, string? joinKey, long after,
        CancellationToken cancellationToken)
    {
        Task changed;
        lock (_lock)
        {
            if (!TryGetReservation(reservationId, joinKey, _time.GetUtcNow(), out Reservation? reservation))
                return Failures.ReservationNotFound;
            if (reservation.Lobby.EventVersion != after)
                return BuildReservationStatusLocked(reservation);
            changed = reservation.Lobby.Changed.Task;
        }

        await WaitForChangeAsync(changed, cancellationToken);

        lock (_lock)
        {
            if (!TryGetReservation(reservationId, joinKey, _time.GetUtcNow(), out Reservation? reservation))
                return Failures.ReservationNotFound;
            return BuildReservationStatusLocked(reservation);
        }
    }

    async Task WaitForChangeAsync(Task changed, CancellationToken cancellationToken)
    {
        try
        {
            await changed.WaitAsync(TimeSpan.FromSeconds(_options.LongPollSeconds), _time, cancellationToken);
        }
        catch (TimeoutException)
        {
        }
    }

    IntroductionList BuildIntroductionsLocked(Lobby lobby, DateTimeOffset now)
    {
        List<Introduction> introductions = [];
        foreach (Reservation reservation in lobby.Reservations)
        {
            if (reservation.ExpiresAt <= now)
                continue;

            List<EndpointCandidate> candidates = [];
            if (reservation.ObservedEndpoint is { } observed)
                candidates.Add(new EndpointCandidate(observed.Address.ToString(), observed.Port, CandidateKind.PublicObserved));
            if (reservation.GamePort is int port)
            {
                foreach (string lanAddress in reservation.LanAddresses)
                    candidates.Add(new EndpointCandidate(lanAddress, port, CandidateKind.Lan));
            }

            introductions.Add(new Introduction(reservation.Id, reservation.TraversalKey, candidates,
                SamePublicAddress(lobby, reservation), reservation.JoinedAt is not null, reservation.ExpiresAt));
        }
        return new IntroductionList(lobby.EventVersion, introductions);
    }

    ReservationStatus BuildReservationStatusLocked(Reservation reservation) =>
        new(reservation.Lobby.EventVersion, HostCandidates(reservation.Lobby), SamePublicAddress(reservation.Lobby, reservation),
            reservation.JoinedAt is not null, reservation.ExpiresAt);

    #endregion

    #region UDP endpoint discovery

    // Returns the response length to send back, or 0 to drop the datagram silently.
    public int HandleDiscovery(ReadOnlySpan<byte> packet, IPEndPoint source, Span<byte> response)
    {
        if (!DiscoveryPacket.TryReadRequest(packet, out DiscoveryPacket.BindingRequest request)
            || NetworkAddresses.PublicIPv4(source.Address) is not { } sourceAddress)
        {
            return 0;
        }

        IPEndPoint observed = new(sourceAddress, source.Port);
        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!_udpTickets.TryGetValue(request.TicketId, out UdpTicketRecord? ticket)
                || !DiscoveryPacket.VerifyRequestTag(packet, ticket.Secret)
                || request.Sequence <= ticket.LastSequence
                || now - ticket.LastAcceptedAt < TimeSpan.FromMilliseconds(_options.DiscoveryMinIntervalMilliseconds)
                || !IsLive(ticket.Lobby, now)
                || ticket.Reservation?.ExpiresAt <= now)
            {
                return 0;
            }

            ticket.LastSequence = request.Sequence;
            ticket.LastAcceptedAt = now;

            if (ticket.Reservation is { } reservation)
            {
                if (!observed.Equals(reservation.ObservedEndpoint))
                {
                    reservation.ObservedEndpoint = observed;
                    SignalLocked(ticket.Lobby);
                }
            }
            else if (!observed.Equals(ticket.Lobby.ObservedEndpoint))
            {
                ticket.Lobby.ObservedEndpoint = observed;
                SignalLocked(ticket.Lobby);
            }

            DiscoveryPacket.WriteResponse(response, request.TicketId, request.Sequence, observed, ticket.Secret);
            return DiscoveryPacket.ResponseLength;
        }
    }

    public Failure? CancelReservation(string reservationId, string? joinKey)
    {
        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!TryGetReservation(reservationId, joinKey, now, out Reservation? reservation))
                return Failures.ReservationNotFound;

            RemoveReservationLocked(reservation);
            return null;
        }
    }

    JoinReservation CreateReservationLocked(Lobby lobby, string sourceKey, IPAddress? joinerPublicAddress,
        DateTimeOffset now, out Reservation record)
    {
        string joinKey = Secrets.NewKey();
        record = new Reservation
        {
            Id = NewUniqueId(_reservations),
            JoinKeyHash = Secrets.Hash(joinKey),
            Lobby = lobby,
            SourceKey = sourceKey,
            PublicAddress = joinerPublicAddress,
            TraversalKey = Secrets.NewKey(),
            ExpiresAt = now.AddSeconds(_options.ReservationSeconds),
        };
        _reservations.Add(record.Id, record);
        lobby.Reservations.Add(record);
        UdpTicket udpTicket = NewUdpTicketLocked(lobby, record, out UInt128 ticketId);
        record.UdpTicketId = ticketId;
        SignalLocked(lobby);

        LobbySnapshot state = lobby.State;
        LobbySummary summary = new(lobby.Id, state.MapId, state.EnemyModeId, state.DifficultyId,
            state.PlayerCount, state.MaxPlayers, state.Phase, state.WaveNumber);
        return new JoinReservation(record.Id, joinKey, summary, HostCandidates(lobby), SamePublicAddress(lobby, record),
            record.ExpiresAt, udpTicket, record.TraversalKey);
    }

    // Observed mapping first, then the unverified forward guess if it differs, then LAN.
    static List<EndpointCandidate> HostCandidates(Lobby lobby)
    {
        LobbySnapshot state = lobby.State;
        List<EndpointCandidate> candidates = [];
        if (lobby.ObservedEndpoint is { } observed)
            candidates.Add(new EndpointCandidate(observed.Address.ToString(), observed.Port, CandidateKind.PublicObserved));
        if (lobby.PublicAddress is not null
            && !(lobby.PublicAddress.Equals(lobby.ObservedEndpoint?.Address) && state.GamePort == lobby.ObservedEndpoint.Port))
        {
            candidates.Add(new EndpointCandidate(lobby.PublicAddress.ToString(), state.GamePort, CandidateKind.PublicReported));
        }
        foreach (string lanAddress in state.LanAddresses ?? [])
            candidates.Add(new EndpointCandidate(lanAddress, state.GamePort, CandidateKind.Lan));
        return candidates;
    }

    // Compares the best-known public IPv4 of each side: the UDP-observed address when
    // present, otherwise the address of the control request.
    static bool SamePublicAddress(Lobby lobby, Reservation reservation)
    {
        IPAddress? host = lobby.ObservedEndpoint?.Address ?? lobby.PublicAddress;
        IPAddress? joiner = reservation.ObservedEndpoint?.Address ?? reservation.PublicAddress;
        return host is not null && host.Equals(joiner);
    }

    #endregion

    #region Expiry

    public void Sweep()
    {
        DateTimeOffset now = _time.GetUtcNow();
        int expiredLobbies;
        lock (_lock)
        {
            expiredLobbies = SweepLocked(now);
        }

        if (expiredLobbies > 0)
            _logger.LogInformation("Expired {Count} lobbies after missed heartbeats.", expiredLobbies);
    }

    int SweepLocked(DateTimeOffset now)
    {
        List<Lobby> expiredLobbies = [.. _lobbies.Values.Where(l => l.ExpiresAt <= now)];
        foreach (Lobby lobby in expiredLobbies)
            RemoveLobbyLocked(lobby);

        foreach (Reservation reservation in _reservations.Values.Where(r => r.ExpiresAt <= now).ToList())
            RemoveReservationLocked(reservation);

        foreach (Search search in _searches.Values.Where(s => s.Deadline + SearchRetention <= now).ToList())
            _searches.Remove(search.Id);

        return expiredLobbies.Count;
    }

    #endregion

    #region Helpers

    bool TryGetHostLobby(string lobbyId, string? hostKey, DateTimeOffset now, [NotNullWhen(true)] out Lobby? lobby)
    {
        if (!_lobbies.TryGetValue(lobbyId, out lobby) || !IsLive(lobby, now) || !Secrets.Matches(lobby.HostKeyHash, hostKey))
        {
            lobby = null;
            return false;
        }
        return true;
    }

    bool TryGetSearch(string searchId, string? searchKey, DateTimeOffset now, [NotNullWhen(true)] out Search? search)
    {
        if (!_searches.TryGetValue(searchId, out search)
            || search.Deadline + SearchRetention <= now
            || !Secrets.Matches(search.KeyHash, searchKey))
        {
            search = null;
            return false;
        }
        return true;
    }

    bool TryGetReservation(string reservationId, string? joinKey, DateTimeOffset now, [NotNullWhen(true)] out Reservation? reservation)
    {
        if (!_reservations.TryGetValue(reservationId, out reservation)
            || reservation.ExpiresAt <= now
            || !Secrets.Matches(reservation.JoinKeyHash, joinKey))
        {
            reservation = null;
            return false;
        }
        return true;
    }

    // Removes the lobby eagerly when its lease has run out.
    bool IsLive(Lobby lobby, DateTimeOffset now)
    {
        if (lobby.ExpiresAt > now)
            return true;

        RemoveLobbyLocked(lobby);
        return false;
    }

    static int Occupancy(Lobby lobby, DateTimeOffset now) =>
        lobby.State.PlayerCount + lobby.Reservations.Count(r => r.ExpiresAt > now);

    int CountReservationsFrom(string sourceKey, DateTimeOffset now) =>
        _reservations.Values.Count(r => r.SourceKey == sourceKey && r.ExpiresAt > now);

    void RemoveLobbyLocked(Lobby lobby)
    {
        _lobbies.Remove(lobby.Id);
        _lobbiesByInvite.Remove(lobby.InviteCode);
        _udpTickets.Remove(lobby.UdpTicketId);
        foreach (Reservation reservation in lobby.Reservations)
        {
            _reservations.Remove(reservation.Id);
            _udpTickets.Remove(reservation.UdpTicketId);
        }
        lobby.Reservations.Clear();
        // Wakes long-polls so they observe the removal.
        SignalLocked(lobby);
    }

    void RemoveReservationLocked(Reservation reservation)
    {
        _reservations.Remove(reservation.Id);
        _udpTickets.Remove(reservation.UdpTicketId);
        if (reservation.Lobby.Reservations.Remove(reservation))
            SignalLocked(reservation.Lobby);
    }

    static void SignalLocked(Lobby lobby)
    {
        lobby.EventVersion++;
        TaskCompletionSource changed = lobby.Changed;
        lobby.Changed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        changed.TrySetResult();
    }

    UdpTicket NewUdpTicketLocked(Lobby lobby, Reservation? reservation, out UInt128 ticketId)
    {
        (UInt128 id, byte[] secret) = Secrets.NewUdpTicket();
        while (_udpTickets.ContainsKey(id))
            (id, secret) = Secrets.NewUdpTicket();

        _udpTickets.Add(id, new UdpTicketRecord { Secret = secret, Lobby = lobby, Reservation = reservation });
        ticketId = id;
        return Secrets.ToContract(id, secret);
    }

    static string NewUniqueId<T>(Dictionary<string, T> existing)
    {
        string id;
        do
        {
            id = Secrets.NewId();
        } while (existing.ContainsKey(id));
        return id;
    }

    string NewUniqueInviteCode()
    {
        string code;
        do
        {
            code = Secrets.NewInviteCode();
        } while (_lobbiesByInvite.ContainsKey(code));
        return code;
    }

    static Failure? ValidateGameVersion(string? version)
    {
        if (string.IsNullOrEmpty(version) || version.Length > MaxGameVersionLength || version.Any(c => c is < '!' or > '~'))
            return Failures.Invalid($"GameVersion must be 1-{MaxGameVersionLength} printable ASCII characters without spaces.");
        return null;
    }

    Failure? ValidateSnapshot(LobbySnapshot state, out LobbySnapshot normalized)
    {
        normalized = state;

        if (state.Revision < 0)
            return Failures.Invalid("Revision must not be negative.");
        // Hosted settings are concrete IDs; the Any sentinels are only valid in searches.
        if (state.MapId < 0 || state.EnemyModeId < 0 || state.EnemyModeId == GameSettingIds.AnyEnemy
            || state.DifficultyId < 0 || state.DifficultyId == GameSettingIds.AnyDifficulty)
        {
            return Failures.Invalid("Map, enemy, and difficulty must be concrete setting IDs.");
        }
        if (state.ResolvedMapId < 0 || state.ResolvedEnemyId < 0)
            return Failures.Invalid("Resolved setting IDs must not be negative.");
        if (state.MaxPlayers < 1 || state.MaxPlayers > _options.MaxPlayersCeiling)
            return Failures.Invalid($"MaxPlayers must be between 1 and {_options.MaxPlayersCeiling}.");
        if (state.PlayerCount < 1 || state.PlayerCount > state.MaxPlayers)
            return Failures.Invalid("PlayerCount must include the host and not exceed MaxPlayers.");
        if (state.WaveNumber < 0 || state.WaveNumber > MaxWaveNumber)
            return Failures.Invalid($"WaveNumber must be between 0 and {MaxWaveNumber}.");
        if (state.GamePort is < 1 or > 65535)
            return Failures.Invalid("GamePort must be between 1 and 65535.");
        if (!Enum.IsDefined(state.Phase))
            return Failures.Invalid("Phase is not a known match phase.");

        if (ValidateLanAddresses(state.LanAddresses, out List<string> lanAddresses) is { } lanFailure)
            return lanFailure;

        normalized = state with { LanAddresses = lanAddresses };
        return null;
    }

    static Failure? ValidateLanAddresses(IReadOnlyList<string>? input, out List<string> distinct)
    {
        distinct = [];
        IReadOnlyList<string> lanAddresses = input ?? [];
        if (lanAddresses.Count > MaxLanAddresses)
            return Failures.Invalid($"At most {MaxLanAddresses} LAN addresses may be reported.");
        foreach (string lanAddress in lanAddresses)
        {
            if (!NetworkAddresses.TryParseLanAddress(lanAddress, out _))
                return Failures.Invalid("LAN addresses must be canonical private or link-local IPv4 addresses.");
            if (!distinct.Contains(lanAddress))
                distinct.Add(lanAddress);
        }
        return null;
    }

    static bool SameContent(LobbySnapshot a, LobbySnapshot b) =>
        a with { LanAddresses = null } == b with { LanAddresses = null }
        && (a.LanAddresses ?? []).SequenceEqual(b.LanAddresses ?? []);

    #endregion

    #region State records

    sealed class Lobby
    {
        public required string Id { get; init; }
        public required long Sequence { get; init; }
        public required byte[] HostKeyHash { get; init; }
        public required string GameVersion { get; init; }
        public required string SourceKey { get; init; }
        public required string InviteCode { get; set; }
        public required LobbySnapshot State { get; set; }
        // Address of the host's latest control request, when it was IPv4.
        public required IPAddress? PublicAddress { get; set; }
        public required DateTimeOffset ExpiresAt { get; set; }
        public List<Reservation> Reservations { get; } = [];
        public UInt128 UdpTicketId { get; set; }
        public IPEndPoint? ObservedEndpoint { get; set; }
        // Bumped on every change a long-poll cares about; Changed completes at the same time.
        public long EventVersion { get; set; }
        public TaskCompletionSource Changed { get; set; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    sealed class Reservation
    {
        public required string Id { get; init; }
        public required byte[] JoinKeyHash { get; init; }
        public required Lobby Lobby { get; init; }
        public required string SourceKey { get; init; }
        public required IPAddress? PublicAddress { get; init; }
        public required string TraversalKey { get; init; }
        public required DateTimeOffset ExpiresAt { get; init; }
        public DateTimeOffset? JoinedAt { get; set; }
        public UInt128 UdpTicketId { get; set; }
        public IPEndPoint? ObservedEndpoint { get; set; }
        public int? GamePort { get; set; }
        public List<string> LanAddresses { get; set; } = [];
    }

    // The secret stays in plain form because the server must compute HMACs with it.
    sealed class UdpTicketRecord
    {
        public required byte[] Secret { get; init; }
        public required Lobby Lobby { get; init; }
        public required Reservation? Reservation { get; init; }
        public ulong LastSequence { get; set; }
        public DateTimeOffset LastAcceptedAt { get; set; } = DateTimeOffset.MinValue;
    }

    sealed class Search
    {
        public required string Id { get; init; }
        public required byte[] KeyHash { get; init; }
        public required SearchRequest Request { get; init; }
        public required HashSet<string> Excluded { get; init; }
        public required string SourceKey { get; init; }
        public required IPAddress? PublicAddress { get; init; }
        public required DateTimeOffset CreatedAt { get; init; }
        public required DateTimeOffset Deadline { get; init; }
        public SearchState State { get; set; } = SearchState.Searching;
        public SearchStatus? Result { get; set; }
        public Reservation? Reservation { get; set; }
    }

    #endregion
}
