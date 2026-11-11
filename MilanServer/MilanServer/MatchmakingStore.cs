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
    const int MaxTrackedPerRecord = 64;
    const int MaxTrackedAddresses = 50_000;
    static readonly TimeSpan SearchRetention = TimeSpan.FromSeconds(60);
    /// <summary>
    /// How long an aborted join is excluded from the next result set
    /// </summary>
    static readonly TimeSpan AbandonedWindow = TimeSpan.FromSeconds(120);

    readonly Lock _lock = new();
    readonly MilanServerOptions _options;
    readonly ServerNotice? _notice;
    readonly TimeProvider _time;
    readonly ILogger<MatchmakingStore> _logger;

    readonly Dictionary<string, Lobby> _lobbies = new(StringComparer.Ordinal);
    readonly Dictionary<string, Lobby> _lobbiesByInvite = new(StringComparer.Ordinal);
    readonly Dictionary<string, Search> _searches = new(StringComparer.Ordinal);
    readonly Dictionary<string, Reservation> _reservations = new(StringComparer.Ordinal);
    readonly Dictionary<string, Member> _members = new(StringComparer.Ordinal);
    readonly Dictionary<UInt128, UdpTicketRecord> _udpTickets = [];
    readonly HashSet<string> _activePolls = new(StringComparer.Ordinal);
    readonly DiscoveryWorkBudget _discoveryWork;
    long _nextLobbySequence;

    /// <summary>
    /// IP -> joiner information
    /// </summary>
    readonly Dictionary<string, JoinerRecord> _joiners = new(StringComparer.Ordinal);
    readonly Dictionary<string, Dictionary<string, DateTimeOffset>> _hostFailures = new(StringComparer.Ordinal);

    // Statistics (GetStats): presence reports, the cached snapshot, and counters since the start.
    readonly Dictionary<string, PresenceRecord> _presence = new(StringComparer.Ordinal);
    readonly Dictionary<string, int> _presenceBySource = new(StringComparer.Ordinal);
    readonly DateTimeOffset _startedAt;
    ServerStats? _stats;
    int _peakPlayers;
    int _peakLobbies;
    long _lobbiesCreated;
    long _lobbiesExpired;
    long _searchesStarted;
    long _searchesMatched;
    long _invitesRedeemed;
    long _joinsCompleted;
    long _hostMigrationsStarted;
    long _hostMigrationsCompleted;
    long _hostMigrationsFailed;
    long _joinsFailed;

    public MatchmakingStore(IOptions<MilanServerOptions> options, TimeProvider time, ILogger<MatchmakingStore> logger,
        ServerNotice? notice = null)
    {
        _options = options.Value;
        _notice = notice;
        _time = time;
        _logger = logger;
        _startedAt = time.GetUtcNow();
        _discoveryWork = new DiscoveryWorkBudget(_options, time);
    }

    #region Hosts

    public Outcome<CreateLobbyResponse> CreateLobby(CreateLobbyRequest request, IPAddress? source)
    {
        if (ValidateGameVersion(request.GameVersion) is { } versionFailure)
            return versionFailure;
        if (ValidateSnapshot(request.State, out LobbySnapshot state) is { } stateFailure)
            return stateFailure;
        if (state.MemberIds!.Count != 0)
            return Failures.Invalid("A new lobby cannot have admitted reservation IDs.");

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
                WaitingSince = now,
                PublicAddress = NetworkAddresses.PublicIPv4(source),
                ExpiresAt = now.AddSeconds(_options.LobbyLeaseSeconds),
            };
            _lobbies.Add(lobby.Id, lobby);
            _lobbiesByInvite.Add(lobby.InviteCode, lobby);
            _lobbiesCreated++;
            udpTicket = NewUdpTicketLocked(lobby, null, out UInt128 ticketId);
            lobby.UdpTicketId = ticketId;
            InheritHostFailuresLocked(lobby, now);
        }

        _logger.LogInformation("Created lobby {LobbyId} ({Privacy}, {Phase}, {Joinable}, {Players}/{MaxPlayers}), game version {GameVersion}.",
            lobby.Id, state.IsPrivate ? "private" : "public", state.Phase, state.Joinable ? "joinable" : "not joinable",
            state.PlayerCount, state.MaxPlayers, request.GameVersion);

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
            if (!TryGetHostLobby(lobbyId, hostKey, now, true, out Lobby? lobby, out Failure failure))
                return failure;

            // Tooling may omit the roster to leave it unchanged. An explicit [] revokes
            // all members; protocol-2 game clients always report their complete roster.
            if (snapshot.MemberIds is null)
                state = state with { MemberIds = lobby.State.MemberIds };

            bool stale = state.Revision < lobby.State.Revision;
            if (state.Revision == lobby.State.Revision && !SameContent(state, lobby.State))
                return Failures.RevisionConflict;

            if (!stale)
            {
                if (!SameContent(state, lobby.State))
                    SignalLocked(lobby);
                // Back in the lobby after a match: it queues behind lobbies already waiting.
                if (state.Phase == MatchPhase.Lobby && lobby.State.Phase != MatchPhase.Lobby)
                    lobby.WaitingSince = now;
                if (state.Phase != lobby.State.Phase || state.Joinable != lobby.State.Joinable || state.IsPrivate != lobby.State.IsPrivate)
                {
                    _logger.LogInformation("Lobby {LobbyId} is now {Phase}, {Joinable}, {Privacy} ({Players}/{MaxPlayers}).",
                        lobby.Id, state.Phase, state.Joinable ? "joinable" : "not joinable", state.IsPrivate ? "private" : "public",
                        state.PlayerCount, state.MaxPlayers);
                }
                lobby.State = state;

                // Only the listen host's explicit roster admits joiners and releases
                // their pending slots; omitted/removed IDs cannot gain privileges.
                ReconcileMembersLocked(lobby, state.MemberIds!, now);

                // The new host's first report brings its own game port and LAN addresses,
                // so the members it is waiting for can now be sent to it.
                if (lobby.Migration is { Stage: MigrationStage.Handoff })
                {
                    lobby.Migration = null;
                    _hostMigrationsCompleted++;
                    SignalLocked(lobby);
                    _logger.LogInformation("Lobby {LobbyId}: the new host is up.", lobby.Id);
                }
            }

            IPAddress? publicAddress = NetworkAddresses.PublicIPv4(source);
            if (publicAddress is not null && !publicAddress.Equals(lobby.PublicAddress))
            {
                lobby.PublicAddress = publicAddress;
                SignalLocked(lobby);
            }
            lobby.ExpiresAt = now.AddSeconds(_options.LobbyLeaseSeconds);
            LobbyReachability reachability = ReachabilityOf(lobby, now, out int failedJoins);
            return new LobbyStateResponse(lobby.State.Revision, stale, lobby.ExpiresAt, reachability, failedJoins);
        }
    }

    // A host leaving a lobby that still has members hands it to one of them instead: they
    // lose the connection at the same moment and report it.
    public Failure? DeleteLobby(string lobbyId, string? hostKey)
    {
        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!TryGetHostLobby(lobbyId, hostKey, now, false, out Lobby? lobby, out Failure failure))
                return failure;

            if (PossibleReporters(lobby) > 0)
            {
                if (lobby.Migration is null)
                    StartCheckLocked(lobby, now);
                DeclareHostLostLocked(lobby, now, "left");
                AdvanceMigrationLocked(lobby, now);
                return null;
            }

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
            if (!TryGetHostLobby(lobbyId, hostKey, now, true, out Lobby? lobby, out Failure failure))
                return failure;

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
        if (request.ExcludeLobbyIds?.Any(id => !Secrets.IsId(id)) == true)
            return Failures.Invalid("Excluded lobby IDs must be valid 22-character lobby IDs.");

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
            // With no open lobby at the player's difficulty, matches in progress qualify at
            // once. Otherwise the lobbies get InProgressAfterSeconds first, even if their map
            // or enemy does not fit yet.
            search.InProgressFrom = _lobbies.Values.Any(l => l.State.Phase == MatchPhase.Lobby && IsOpenTo(l, search, now))
                ? now.AddSeconds(_options.InProgressAfterSeconds)
                : now;
            _searches.Add(search.Id, search);
            _searchesStarted++;
            // MilanHost sends lobbies to exclude which we can use to determine
            // failed joins
            foreach (string excludedId in search.Excluded)
            {
                if (_lobbies.TryGetValue(excludedId, out Lobby? excluded) && excluded.Abandoned.Remove(sourceKey, out DateTimeOffset at) && now - at <= AbandonedWindow)
                {
                    RecordJoinFailureLocked(excluded, sourceKey, now, "the host did not answer");
                }
            }
            string current = _notice?.ExpectedGameVersion ?? _options.ExpectedGameVersion;
            string? expectedVersion = current.Length > 0 && current != request.GameVersion ? current : null;
            _logger.LogInformation(
                "Search {SearchId} started: game version {GameVersion}{VersionNote}, map {MapId}, enemy {EnemyModeId}, difficulty {DifficultyId}, widen {Widen}; matches in progress {InProgress}.",
                search.Id, request.GameVersion, expectedVersion is null ? "" : $" (server expects {expectedVersion})",
                request.MapId, request.EnemyModeId, request.DifficultyId, request.Widen,
                search.InProgressFrom == now ? "allowed now (no open lobby)" : $"allowed after {_options.InProgressAfterSeconds} s (lobbies open)");

            Outcome<SearchStatus> status = EvaluateSearchLocked(search, now);
            if (status.Failure is { } failure)
            {
                _searches.Remove(search.Id);
                return failure;
            }
            return new CreateSearchResponse(search.Id, searchKey, status.Value, expectedVersion);
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
            if (search.Reservation is { } reservation && _reservations.ContainsKey(reservation.Id))
            {
                if (CouldBeFailedJoin(reservation))
                {
                    reservation.Lobby.Abandoned[reservation.SourceKey] = now;
                    TrimOldest(reservation.Lobby.Abandoned);
                }
                RemoveReservationLocked(reservation);
            }
        }

        _logger.LogInformation("Search {SearchId} cancelled.", searchId);
        return null;
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
            _logger.LogInformation("Search {SearchId} found no match.", search.Id);
            return search.Result;
        }
        if (stage != search.Stage)
        {
            search.Stage = stage;
            _logger.LogInformation("Search {SearchId} widened to {Stage}.", search.Id, stage);
        }

        Lobby? best = null;
        LobbyReachability bestReachability = LobbyReachability.Unknown;
        (int, int, int, int, int, DateTimeOffset, long) bestRank = default;
        foreach (Lobby lobby in _lobbies.Values)
        {
            if (!IsSearchCandidate(lobby, search, stage, now))
                continue;
            LobbyReachability reachability = ReachabilityOf(lobby, now, out _);
            if (!AcceptsSearchJoiner(lobby, reachability, now))
            {
                continue;
            }

            (int, int, int, int, int, DateTimeOffset, long) rank = Rank(lobby, search.Request, reachability);
            if (best is null || rank.CompareTo(bestRank) < 0)
            {
                best = lobby;
                bestReachability = reachability;
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
        _searchesMatched++;
        if (bestReachability == LobbyReachability.Unreachable)
        {
            best.LastProbeAt = now;
            _logger.LogInformation("Search {SearchId} matched lobby {LobbyId} at stage {Stage}, player matched to determine if suspect lobby is actually reachable", search.Id, best.Id, stage);
        }
        else
        {
            _logger.LogInformation("Search {SearchId} matched lobby {LobbyId} at stage {Stage}.", search.Id, best.Id, stage);
        }
        return search.Result;
    }

    // Whether this player could ever be matched to the lobby, before the map and enemy
    // filters that widen. Private lobbies never appear in search, extraction is
    // invite-only, difficulty never widens, and a lobby changing hosts waits for its own.
    bool IsOpenTo(Lobby lobby, Search search, DateTimeOffset now)
    {
        LobbySnapshot state = lobby.State;
        SearchRequest request = search.Request;
        return lobby.ExpiresAt > now
            && lobby.Migration is null
            && !state.IsPrivate
            && state.Joinable
            && state.Phase is (MatchPhase.Lobby or MatchPhase.InProgress)
            && (request.DifficultyId == GameSettingIds.AnyDifficulty || state.DifficultyId == request.DifficultyId)
            && lobby.GameVersion == request.GameVersion
            && !search.Excluded.Contains(lobby.Id)
            && Occupancy(lobby, now) < state.MaxPlayers;
    }

    bool IsSearchCandidate(Lobby lobby, Search search, SearchStage stage, DateTimeOffset now)
    {
        LobbySnapshot state = lobby.State;
        SearchRequest request = search.Request;

        if (!IsOpenTo(lobby, search, now) || (state.Phase == MatchPhase.InProgress && now < search.InProgressFrom))
            return false;

        // Filters compare the selected setting, so a Random map lobby never satisfies a
        // specific map, and Random Faction or Coalition match by mode ID rather than the
        // faction currently in play.
        bool mapMatches = stage >= SearchStage.AnyMap || request.MapId == GameSettingIds.AnyMap || state.MapId == request.MapId;
        bool enemyMatches = stage >= SearchStage.AnyMapAndEnemy || request.EnemyModeId == GameSettingIds.AnyEnemy
            || state.EnemyModeId == request.EnemyModeId;
        return mapMatches && enemyMatches;
    }

    // Lower sorts first: lobbies players could not connect to last, then lobbies before matches
    // in progress, closer matches to the player's original filters, a lobby someone has joined
    // before one nobody has, earlier waves, and then the lobby that has waited longest
    (int, int, int, int, int, DateTimeOffset, long) Rank(Lobby lobby, SearchRequest request, LobbyReachability reachability)
    {
        LobbySnapshot state = lobby.State;
        int specificity = 0;
        if (request.MapId != GameSettingIds.AnyMap && state.MapId == request.MapId)
            specificity++;
        if (request.EnemyModeId != GameSettingIds.AnyEnemy && state.EnemyModeId == request.EnemyModeId)
            specificity++;

        bool enabled = _options.Reachability.Enabled;
        int demoted = !enabled ? 0 : reachability switch
        {
            LobbyReachability.Suspect => 1,
            LobbyReachability.Unreachable => 2,
            _ => 0,
        };
        int unproven = enabled && reachability != LobbyReachability.Proven ? 1 : 0;
        return (demoted, state.Phase == MatchPhase.Lobby ? 0 : 1, -specificity, unproven, state.WaveNumber, lobby.WaitingSince, lobby.Sequence);
    }

    // A lobby nobody from another network has joined takes one quick-match joiner at a time, so a
    // host that cannot be reached costs one player rather than several. An unreachable lobby only
    // takes one now and then, to see whether its host was fixed
    bool AcceptsSearchJoiner(Lobby lobby, LobbyReachability reachability, DateTimeOffset now)
    {
        MilanReachabilityOptions options = _options.Reachability;
        if (!options.Enabled || reachability == LobbyReachability.Proven)
        {
            // We're good
            return true;
        }
        if (lobby.Reservations.Count(r => r.ExpiresAt > now && r.JoinedAt is null && !r.Migration) >= options.UnprovenPendingJoins)
        {
            return false;
        }
        return reachability != LobbyReachability.Unreachable || now - lobby.LastProbeAt >= TimeSpan.FromSeconds(options.ProbeSeconds);
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
            // Invites work for private lobbies and during extraction, but not while the
            // lobby is changing hosts.
            if (!state.Joinable || state.Phase == MatchPhase.Complete || lobby.Migration is not null)
                return Failures.LobbyNotJoinable;
            if (Occupancy(lobby, now) >= state.MaxPlayers)
                return Failures.LobbyFull;
            if (CountReservationsFrom(sourceKey, now) >= _options.MaxReservationsPerAddress)
                return Failures.AddressLimit;

            reservation = CreateReservationLocked(lobby, sourceKey, NetworkAddresses.PublicIPv4(source), now, out _);
            _invitesRedeemed++;
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
                return TryGetActiveMember(reservationId, joinKey, now, out _) ? null : Failures.ReservationNotFound;

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
                return TryGetActiveMember(reservationId, joinKey, now, out _) ? null : Failures.ReservationNotFound;

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
            if (!TryGetHostLobby(lobbyId, hostKey, _time.GetUtcNow(), true, out Lobby? lobby, out Failure failure))
                return failure;
            if (lobby.EventVersion != after)
                return BuildIntroductionsLocked(lobby, _time.GetUtcNow());
            changed = lobby.Changed.Task;
            if (!_activePolls.Add("host:" + lobbyId))
                return Failures.PollBusy;
        }

        await WaitForPollAsync("host:" + lobbyId, changed, TimeSpan.FromSeconds(_options.LongPollSeconds), cancellationToken);

        lock (_lock)
        {
            if (!TryGetHostLobby(lobbyId, hostKey, _time.GetUtcNow(), false, out Lobby? lobby, out Failure failure))
                return failure;
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
                return TryGetActiveMember(reservationId, joinKey, _time.GetUtcNow(), out Member? confirmed)
                    ? BuildConfirmedReservationStatus(confirmed) : Failures.ReservationNotFound;
            if (reservation.Lobby.EventVersion != after)
                return BuildReservationStatusLocked(reservation);
            changed = reservation.Lobby.Changed.Task;
            if (!_activePolls.Add("reservation:" + reservationId))
                return Failures.PollBusy;
        }

        await WaitForPollAsync("reservation:" + reservationId, changed, TimeSpan.FromSeconds(_options.LongPollSeconds), cancellationToken);

        lock (_lock)
        {
            if (!TryGetReservation(reservationId, joinKey, _time.GetUtcNow(), out Reservation? reservation))
                return TryGetActiveMember(reservationId, joinKey, _time.GetUtcNow(), out Member? confirmed)
                    ? BuildConfirmedReservationStatus(confirmed) : Failures.ReservationNotFound;
            return BuildReservationStatusLocked(reservation);
        }
    }

    async Task WaitForChangeAsync(Task changed, TimeSpan timeout, CancellationToken cancellationToken)
    {
        try
        {
            await changed.WaitAsync(timeout, _time, cancellationToken);
        }
        catch (TimeoutException)
        {
        }
    }

    async Task WaitForPollAsync(string key, Task changed, TimeSpan timeout, CancellationToken cancellationToken)
    {
        try { await WaitForChangeAsync(changed, timeout, cancellationToken); }
        finally { lock (_lock) _activePolls.Remove(key); }
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
            if (reservation.GamePort is int port && CanShareLanCandidates(lobby, reservation))
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
        new(reservation.Lobby.EventVersion, HostCandidates(reservation.Lobby, reservation), SamePublicAddress(reservation.Lobby, reservation),
            reservation.JoinedAt is not null, reservation.ExpiresAt);

    // Host confirmation may arrive before the joiner finishes loading its map. A
    // pending poll must complete as joined, rather than making a real join look expired.
    static ReservationStatus BuildConfirmedReservationStatus(Member member) =>
        new(member.Lobby.EventVersion, HostCandidates(member.Lobby, member.PublicAddress),
            Equals(member.Lobby.ObservedEndpoint?.Address ?? member.Lobby.PublicAddress, member.PublicAddress),
            true, member.Lobby.ExpiresAt);

    #endregion

    #region Members and host migration

    // A member lost its connection to the host. The first report starts a check: the host
    // is woken from its long-poll and has HostChallengeSeconds to send anything. If it does,
    // the reporters lost their own connection. If not, or if the host has already left, the
    // server waits for the other members and hands the lobby to one of them.
    public Outcome<MigrationStatus> ReportHostLost(string memberId, string? memberKey, HostLostReport report)
    {
        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!TryGetMember(memberId, memberKey, now, out Member? member))
                return Failures.MemberNotFound;

            Lobby lobby = member.Lobby;
            AdvanceMigrationLocked(lobby, now);
            // Too late: the lobby closed, or moved on without this member.
            if (lobby.Removed || (member.Outcome is null && !lobby.Members.Contains(member)))
                return Failures.MemberNotFound;

            if (member.Outcome is null && member.ReportedLostAt is null)
            {
                // A report while waiting for a new host's first report checks that host.
                if (lobby.Migration is null or { Stage: MigrationStage.Handoff })
                {
                    // The host proved itself moments ago, so there is nothing to check again.
                    if (lobby.HostConfirmedAliveAt is { } alive && now - alive < TimeSpan.FromSeconds(_options.HostChallengeSeconds))
                    {
                        SettleLocked(member, MigrationState.HostAlive, now);
                        return BuildMigrationStatusLocked(member);
                    }
                    StartCheckLocked(lobby, now);
                    _logger.LogInformation("Lobby {LobbyId}: a member lost the host; checking whether the host still answers.", lobby.Id);
                }
                member.ReportedLostAt = now;
                member.CanHost = report.CanHost;
                AdvanceMigrationLocked(lobby, now);
            }
            return BuildMigrationStatusLocked(member);
        }
    }

    // Member long-poll during a migration. It also returns when the current check or report
    // window ends, so the next step does not wait for another request.
    public async Task<Outcome<MigrationStatus>> PollMemberAsync(string memberId, string? memberKey, long after,
        CancellationToken cancellationToken)
    {
        Task changed;
        TimeSpan wait;
        lock (_lock)
        {
            DateTimeOffset now = _time.GetUtcNow();
            if (!TryGetMember(memberId, memberKey, now, out Member? member))
                return Failures.MemberNotFound;
            AdvanceMigrationLocked(member.Lobby, now);
            if (member.Lobby.EventVersion != after)
                return BuildMigrationStatusLocked(member);

            changed = member.Lobby.Changed.Task;
            if (!_activePolls.Add("member:" + memberId))
                return Failures.PollBusy;
            wait = TimeSpan.FromSeconds(_options.LongPollSeconds);
            if (member.Lobby.Migration is { } migration && migration.Stage != MigrationStage.Handoff)
            {
                DateTimeOffset deadline = migration.Stage == MigrationStage.Checking ? migration.ChallengeDeadline : migration.ReportDeadline;
                wait = TimeSpan.FromTicks(Math.Clamp((deadline - now).Ticks, 0, wait.Ticks));
            }
        }

        await WaitForPollAsync("member:" + memberId, changed, wait, cancellationToken);

        lock (_lock)
        {
            DateTimeOffset now = _time.GetUtcNow();
            if (!TryGetMember(memberId, memberKey, now, out Member? member))
            {
                return Failures.MemberNotFound;
            }
            AdvanceMigrationLocked(member.Lobby, now);
            return BuildMigrationStatusLocked(member);
        }
    }

    public Failure? LeaveLobby(string memberId, string? memberKey)
    {
        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!TryGetMember(memberId, memberKey, now, out Member? member))
            {
                // An early disconnect can release its own unconfirmed reservation.
                if (!TryGetReservation(memberId, memberKey, now, out Reservation? pending))
                    return Failures.MemberNotFound;
                RemoveReservationLocked(pending);
                return null;
            }

            RemoveMemberLocked(member);
            // A migration waiting for this member's report can go ahead without it.
            AdvanceMigrationLocked(member.Lobby, now);
            return null;
        }
    }

    // A member asks for the invite code, to invite someone itself. The host's state says whether
    // members may (MemberInvites); the code is read each time, so a rotated code is never stale.
    public Outcome<InviteResponse> GetMemberInvite(string memberId, string? memberKey)
    {
        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (!TryGetMember(memberId, memberKey, now, out Member? member))
                return Failures.MemberNotFound;
            if (!member.Lobby.Members.Contains(member))
                return Failures.MemberNotFound;
            if (!member.Lobby.State.MemberInvites)
                return Failures.InvitesHostOnly;
            return new InviteResponse(Secrets.FormatInviteCode(member.Lobby.InviteCode));
        }
    }

    MigrationStatus BuildMigrationStatusLocked(Member member)
    {
        long version = member.Lobby.EventVersion;
        if (member.Outcome is { } outcome)
            return new MigrationStatus(version, outcome, member.Handoff, member.Reservation);

        return member.Lobby.Migration?.Stage switch
        {
            MigrationStage.Checking => new MigrationStatus(version, MigrationState.Checking),
            MigrationStage.Electing => new MigrationStatus(version, MigrationState.Electing),
            // A member that never reported only asks while the host is fine.
            _ => new MigrationStatus(version, MigrationState.HostAlive),
        };
    }

    // Only an authenticated host roster grants membership; joiner notices are provisional.
    void ReconcileMembersLocked(Lobby lobby, IReadOnlyList<string> ids, DateTimeOffset now)
    {
        HashSet<string> roster = new(ids, StringComparer.Ordinal);
        foreach (Member member in lobby.Members.Concat(lobby.SettledMembers.Where(m => m.Outcome == MigrationState.HostAlive))
            .Where(m => !roster.Contains(m.Id)).ToList())
            RemoveMemberLocked(member);
        foreach (string id in roster)
        {
            if (_members.TryGetValue(id, out Member? current) && current.Lobby == lobby)
                continue;
            if (_reservations.TryGetValue(id, out Reservation? admitted) && admitted.Lobby == lobby && admitted.ExpiresAt > now)
            {
                RecordJoinSuccessLocked(admitted, now);
                AdmitMemberLocked(admitted);
                _joinsCompleted++;
            }
            // Unknown, expired, and foreign IDs never confer authority. A delayed
            // disconnect report must not prevent the rest of a valid heartbeat.
        }
    }

    // Member records stay small: no ticket and no candidates.
    Member AdmitMemberLocked(Reservation reservation)
    {
        Lobby lobby = reservation.Lobby;
        Member member = new()
        {
            Id = reservation.Id,
            KeyHash = reservation.JoinKeyHash,
            Lobby = lobby,
            SourceKey = reservation.SourceKey,
            PublicAddress = reservation.ObservedEndpoint?.Address ?? reservation.PublicAddress,
            JoinedAt = reservation.JoinedAt ?? _time.GetUtcNow(),
        };
        RemoveReservationLocked(reservation);

        // Members that disconnect without saying so are only dropped by a migration or when
        // the lobby closes, so a long-lived lobby keeps at most this many.
        int maxMembers = 2 * _options.MaxPlayersCeiling;
        if (lobby.Members.Count >= maxMembers)
            RemoveMemberLocked(lobby.Members[0]);

        lobby.Members.Add(member);
        _members[member.Id] = member;
        return member;
    }

    void RemoveMemberLocked(Member member)
    {
        _members.Remove(member.Id);
        member.Lobby.Members.Remove(member);
        member.Lobby.SettledMembers.Remove(member);
    }

    // Only host-confirmed members can report a lost host.
    static int PossibleReporters(Lobby lobby) =>
        lobby.Members.Count;

    void StartCheckLocked(Lobby lobby, DateTimeOffset now)
    {
        lobby.Migration = new Migration
        {
            StartedAt = now,
            ChallengeDeadline = now.AddSeconds(_options.HostChallengeSeconds),
        };
        // The lobby must outlive the check and the report window, whatever its lease says.
        DateTimeOffset needed = lobby.Migration.ChallengeDeadline.AddSeconds(_options.MigrationReportSeconds + 1);
        if (lobby.ExpiresAt < needed)
            lobby.ExpiresAt = needed;
        // Completes the host's pending long-poll: a host that is still there polls again at once.
        SignalLocked(lobby);
    }

    // The old host key stops working, and a later request with it gets host_replaced.
    void DeclareHostLostLocked(Lobby lobby, DateTimeOffset now, string how)
    {
        Migration migration = lobby.Migration!;
        migration.Stage = MigrationStage.Electing;
        migration.ReportDeadline = now.AddSeconds(_options.MigrationReportSeconds);
        DateTimeOffset needed = migration.ReportDeadline.AddSeconds(1);
        if (lobby.ExpiresAt < needed)
            lobby.ExpiresAt = needed;

        lobby.ReplacedHostKeyHashes.Add(lobby.HostKeyHash);
        if (lobby.ReplacedHostKeyHashes.Count > 4)
            lobby.ReplacedHostKeyHashes.RemoveAt(0);
        lobby.HostKeyHash = Secrets.Hash(Secrets.NewKey());
        SignalLocked(lobby);
        _logger.LogInformation("Lobby {LobbyId}: the host {How}; waiting for {Count} member(s) to report.",
            lobby.Id, how, PossibleReporters(lobby));
    }

    void AdvanceMigrationLocked(Lobby lobby, DateTimeOffset now)
    {
        if (lobby.Migration is not { } migration)
            return;

        if (migration.Stage == MigrationStage.Checking)
        {
            if (lobby.HostLastSeen > migration.StartedAt)
            {
                foreach (Member reporter in lobby.Members.Where(m => m.ReportedLostAt is not null).ToList())
                    SettleLocked(reporter, MigrationState.HostAlive, now);
                lobby.Migration = null;
                lobby.HostConfirmedAliveAt = now;
                SignalLocked(lobby);
                _logger.LogInformation("Lobby {LobbyId}: the host answered; the reporting member(s) lost their own connection.", lobby.Id);
                return;
            }
            if (now < migration.ChallengeDeadline)
                return;
            DeclareHostLostLocked(lobby, now, "stopped answering");
        }

        if (migration.Stage == MigrationStage.Electing
            && (now >= migration.ReportDeadline || lobby.Members.Count(m => m.ReportedLostAt is not null) >= PossibleReporters(lobby)))
        {
            ElectLocked(lobby, now);
        }
    }

    // Picks the new host among the members that reported: one that can host, then the one
    // that joined first. Members that did not report are gone, and so are reservations that
    // were heading to the old host.
    void ElectLocked(Lobby lobby, DateTimeOffset now)
    {
        List<Member> reporters = [.. lobby.Members
            .Where(m => m.ReportedLostAt is not null)
            .OrderByDescending(m => m.CanHost)
            .ThenBy(m => m.JoinedAt)
            .ThenBy(m => m.Id, StringComparer.Ordinal)];
        foreach (Member silent in lobby.Members.Except(reporters).ToList())
            RemoveMemberLocked(silent);
        foreach (Reservation stale in lobby.Reservations.ToList())
            RemoveReservationLocked(stale);

        if (reporters.Count == 0)
        {
            RemoveLobbyLocked(lobby);
            _hostMigrationsFailed++;
            _logger.LogInformation("Lobby {LobbyId}: no member reported, so the lobby closed.", lobby.Id);
            return;
        }

        Member newHost = reporters[0];
        string hostKey = Secrets.NewKey();
        lobby.HostKeyHash = Secrets.Hash(hostKey);
        lobby.SourceKey = newHost.SourceKey;
        lobby.PublicAddress = newHost.PublicAddress;
        lobby.ObservedEndpoint = null;
        _udpTickets.Remove(lobby.UdpTicketId);
        UdpTicket udpTicket = NewUdpTicketLocked(lobby, null, out UInt128 ticketId);
        lobby.UdpTicketId = ticketId;
        lobby.ExpiresAt = now.AddSeconds(_options.LobbyLeaseSeconds);
        lobby.Migration!.Stage = MigrationStage.Handoff;
        //Clear out lobby info
        lobby.JoinFailures.Clear();
        lobby.Abandoned.Clear();
        lobby.LastJoinedAt = null;
        lobby.LastProbeAt = DateTimeOffset.MinValue;
        InheritHostFailuresLocked(lobby, now);

        // Settling takes the reporters off the member list; each joins the new host as a new member.
        newHost.Handoff = new HostHandoff(lobby.Id, hostKey, Secrets.FormatInviteCode(lobby.InviteCode), lobby.State.Revision,
            lobby.ExpiresAt, _options.HeartbeatSeconds, udpTicket, reporters.Count, lobby.State);
        SettleLocked(newHost, MigrationState.NewHost, now);
        foreach (Member joiner in reporters.Skip(1))
        {
            joiner.Reservation = CreateReservationLocked(lobby, joiner.SourceKey, joiner.PublicAddress, now, out _,
                _options.MigrationJoinSeconds, migration: true);
            SettleLocked(joiner, MigrationState.JoinNewHost, now);
        }
        SignalLocked(lobby);
        _hostMigrationsStarted++;
        _logger.LogInformation("Lobby {LobbyId}: member {MemberId} is the new host; {Count} member(s) will join it.",
            lobby.Id, newHost.Id, reporters.Count - 1);
    }

    // The member keeps answering polls with this outcome until it is swept.
    void SettleLocked(Member member, MigrationState outcome, DateTimeOffset now)
    {
        member.Outcome = outcome;
        member.SettledAt = now;
        member.ReportedLostAt = null;
        member.Lobby.Members.Remove(member);
        if (member.Lobby.SettledMembers.Count >= 2 * _options.MaxPlayersCeiling)
            RemoveMemberLocked(member.Lobby.SettledMembers[0]);
        member.Lobby.SettledMembers.Add(member);
    }

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
        if (!_discoveryWork.TryEnter(NetworkAddresses.AccountingKey(sourceAddress)))
            return 0;
        DateTimeOffset now = _time.GetUtcNow();
        UdpTicketRecord? ticket;
        lock (_lock)
        {
            if (!_udpTickets.TryGetValue(request.TicketId, out ticket) || !CanAcceptDiscovery(ticket, request.Sequence, now))
                return 0;
        }
        // The secret is immutable. Expiry/removal/another packet may race verification,
        // so recheck the record and all acceptance conditions before committing it.
        if (!DiscoveryPacket.VerifyRequestTag(packet, ticket.Secret))
            return 0;
        lock (_lock)
        {
            now = _time.GetUtcNow();
            if (!_udpTickets.TryGetValue(request.TicketId, out UdpTicketRecord? current) || current != ticket
                || !CanAcceptDiscovery(ticket, request.Sequence, now))
                return 0;

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

        }
        DiscoveryPacket.WriteResponse(response, request.TicketId, request.Sequence, observed, ticket.Secret);
        return DiscoveryPacket.ResponseLength;
    }

    bool CanAcceptDiscovery(UdpTicketRecord ticket, ulong sequence, DateTimeOffset now) =>
        sequence > ticket.LastSequence
        && now - ticket.LastAcceptedAt >= TimeSpan.FromMilliseconds(_options.DiscoveryMinIntervalMilliseconds)
        && !ticket.Lobby.Removed && ticket.Lobby.ExpiresAt > now
        && (ticket.Reservation is null || ticket.Reservation.ExpiresAt > now);

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
        DateTimeOffset now, out Reservation record, int? lifetimeSeconds = null, bool migration = false)
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
            ExpiresAt = now.AddSeconds(lifetimeSeconds ?? _options.ReservationSeconds),
            Migration = migration,
        };
        _reservations.Add(record.Id, record);
        lobby.Reservations.Add(record);
        UdpTicket udpTicket = NewUdpTicketLocked(lobby, record, out UInt128 ticketId);
        record.UdpTicketId = ticketId;
        SignalLocked(lobby);

        LobbySnapshot state = lobby.State;
        LobbySummary summary = new(lobby.Id, state.MapId, state.EnemyModeId, state.DifficultyId,
            state.PlayerCount, state.MaxPlayers, state.Phase, state.WaveNumber);
        return new JoinReservation(record.Id, joinKey, summary, HostCandidates(lobby, record), SamePublicAddress(lobby, record),
            record.ExpiresAt, udpTicket, record.TraversalKey);
    }

    // Observed mapping first, then the unverified forward guess if it differs, then LAN.
    // None while the lobby changes hosts: until the new host reports, they are the old one's.
    static List<EndpointCandidate> HostCandidates(Lobby lobby, Reservation reservation)
        => HostCandidates(lobby, reservation.ObservedEndpoint?.Address ?? reservation.PublicAddress);

    static List<EndpointCandidate> HostCandidates(Lobby lobby, IPAddress? peerAddress)
    {
        LobbySnapshot state = lobby.State;
        List<EndpointCandidate> candidates = [];
        if (lobby.Migration is not null)
            return candidates;
        if (lobby.ObservedEndpoint is { } observed)
            candidates.Add(new EndpointCandidate(observed.Address.ToString(), observed.Port, CandidateKind.PublicObserved));
        if (lobby.PublicAddress is not null
            && !(lobby.PublicAddress.Equals(lobby.ObservedEndpoint?.Address) && state.GamePort == lobby.ObservedEndpoint.Port))
        {
            candidates.Add(new EndpointCandidate(lobby.PublicAddress.ToString(), state.GamePort, CandidateKind.PublicReported));
        }
        if (CanShareLanCandidates(lobby.ObservedEndpoint?.Address ?? lobby.PublicAddress, peerAddress))
            foreach (string lanAddress in state.LanAddresses ?? [])
                candidates.Add(new EndpointCandidate(lanAddress, state.GamePort, CandidateKind.Lan));
        return candidates;
    }

    static bool CanShareLanCandidates(Lobby lobby, Reservation reservation) =>
        CanShareLanCandidates(lobby.ObservedEndpoint?.Address ?? lobby.PublicAddress,
            reservation.ObservedEndpoint?.Address ?? reservation.PublicAddress);

    static bool CanShareLanCandidates(IPAddress? host, IPAddress? peer) =>
        host is not null && peer is not null
        && (host.Equals(peer) || (NetworkAddresses.IsLanAddress(host) && NetworkAddresses.IsLanAddress(peer)));

    // Compares the best-known public IPv4 of each side: the UDP-observed address when
    // present, otherwise the address of the control request.
    static bool SamePublicAddress(Lobby lobby, Reservation reservation)
    {
        IPAddress? host = lobby.ObservedEndpoint?.Address ?? lobby.PublicAddress;
        IPAddress? joiner = reservation.ObservedEndpoint?.Address ?? reservation.PublicAddress;
        return host is not null && host.Equals(joiner);
    }

    #endregion

    #region Reachability
    /// <summary>
    /// Returns the probable reachability of a lobby
    /// </summary>
    /// <param name="lobby"></param>
    /// <param name="now"></param>
    /// <param name="failures"></param>
    /// <returns></returns>
    LobbyReachability ReachabilityOf(Lobby lobby, DateTimeOffset now, out int failures)
    {
        MilanReachabilityOptions options = _options.Reachability;
        failures = CountJoinFailures(lobby, now);
        if (failures >= options.UnreachableAfterFailures)
        {
            return LobbyReachability.Unreachable;
        }
        if (failures >= options.SuspectAfterFailures)
        {
            return LobbyReachability.Suspect;
        }
        return lobby.LastJoinedAt is null ? LobbyReachability.Unknown : LobbyReachability.Proven;
    }

    int CountJoinFailures(Lobby lobby, DateTimeOffset now)
    {
        TimeSpan memory = TimeSpan.FromSeconds(_options.Reachability.FailureSeconds);
        int count = 0;
        foreach ((string joiner, DateTimeOffset at) in lobby.JoinFailures)
        {
            if (now - at < memory && !IsDiscountedJoiner(joiner, now))
            {
                count++;
            }
        }
        return count;
    }

    /// <summary>
    /// A player who fails to join a bunch of lobbies shouldn't count against lobbies who have people failing to join
    /// as it could be due to their crappy internet
    /// </summary>
    /// <param name="joiner"></param>
    /// <param name="now"></param>
    /// <returns></returns>
    bool IsDiscountedJoiner(string joiner, DateTimeOffset now)
    {
        if (!_joiners.TryGetValue(joiner, out JoinerRecord? record))
        {
            return false;
        }
        TimeSpan memory = TimeSpan.FromSeconds(_options.Reachability.FailureSeconds);
        if (record.LastJoinedAt is { } joined && now - joined < memory)
        {
            return false;
        }
        return record.FailedLobbies.Values.Count(at => now - at < memory) >= _options.Reachability.JoinerFailureLimit;
    }

    /// <summary>
    /// Is the given reservation a failed join?
    /// </summary>
    /// <param name="reservation"></param>
    /// <returns></returns>
    static bool CouldBeFailedJoin(Reservation reservation) =>
        // Not a migration
        !reservation.Migration
        // Reservation has no join time
        && reservation.JoinedAt is null
        // The lobby still exists
        && !reservation.Lobby.Removed
        // The lobby is not migrating
        && reservation.Lobby.Migration is null
        // Not the same public address (likely LAN)
        && !SamePublicAddress(reservation.Lobby, reservation);

    void RecordJoinFailureLocked(Lobby lobby, string joiner, DateTimeOffset now, string how)
    {
        if (lobby.Removed)
        {
            // Lobby is gone, joins don't count here
            return;
        }

        LobbyReachability before = ReachabilityOf(lobby, now, out _);
        lobby.JoinFailures[joiner] = now;
        TrimOldest(lobby.JoinFailures);
        if (!_joiners.TryGetValue(joiner, out JoinerRecord? record) && _joiners.Count < MaxTrackedAddresses)
        {
            record = new JoinerRecord();
            _joiners.Add(joiner, record);
        }
        if (record is not null)
        {
            record.FailedLobbies[lobby.Id] = now;
            TrimOldest(record.FailedLobbies);
        }
        _joinsFailed++;

        LobbyReachability after = ReachabilityOf(lobby, now, out int failures);
        // The first test of an unreachable lobby waits a full ProbeSeconds.
        if (after == LobbyReachability.Unreachable && before != LobbyReachability.Unreachable)
        {
            lobby.LastProbeAt = now;
        }
        if (after != before && after >= LobbyReachability.Suspect)
        {
            _logger.LogWarning("Lobby {LobbyId} is now {Reachability}: {Count} players from other networks could not connect since its last successful join ({How}).", lobby.Id, after, failures, how);
        }
        else
        {
            _logger.LogInformation("Lobby {LobbyId}: a player could not connect ({How}); {Count} counted since its last successful join.", lobby.Id, how, failures);
        }
    }

    /// <summary>
    /// Record a succesful join to a lobby
    /// </summary>
    /// <param name="reservation"></param>
    /// <param name="now"></param>
    void RecordJoinSuccessLocked(Reservation reservation, DateTimeOffset now)
    {
        Lobby lobby = reservation.Lobby;
        if (SamePublicAddress(lobby, reservation))
        {
            return;
        }

        if (lobby.JoinFailures.Count > 0)
        {
            _logger.LogInformation("Lobby {LobbyId}: a player joined, so its earlier failed joins no longer count.", lobby.Id);
        }
        lobby.LastJoinedAt = now;
        lobby.JoinFailures.Clear();
        lobby.Abandoned.Clear();
        _hostFailures.Remove(lobby.SourceKey);
        if (!_joiners.TryGetValue(reservation.SourceKey, out JoinerRecord? record) && _joiners.Count < MaxTrackedAddresses)
        {
            record = new JoinerRecord();
            _joiners.Add(reservation.SourceKey, record);
        }
        if (record is not null)
        {
            record.LastJoinedAt = now;
        }
    }

    /// <summary>
    /// Transfer hosting keeps failed joins so you don't open a new poisoning lobby
    /// </summary>
    /// <param name="lobby"></param>
    void KeepHostFailuresLocked(Lobby lobby)
    {
        if (lobby.JoinFailures.Count == 0)
        {
            return;
        }
        if (!_hostFailures.TryGetValue(lobby.SourceKey, out Dictionary<string, DateTimeOffset>? kept))
        {
            if (_hostFailures.Count >= MaxTrackedAddresses)
            {
                return;
            }
            kept = new(StringComparer.Ordinal);
            _hostFailures.Add(lobby.SourceKey, kept);
        }
        foreach ((string joiner, DateTimeOffset at) in lobby.JoinFailures)
        {
            if (!kept.TryGetValue(joiner, out DateTimeOffset existing) || existing < at)
            {
                kept[joiner] = at;
            }
        }
        TrimOldest(kept);
    }

    void InheritHostFailuresLocked(Lobby lobby, DateTimeOffset now)
    {
        if (!_hostFailures.TryGetValue(lobby.SourceKey, out Dictionary<string, DateTimeOffset>? kept))
        {
            return;
        }
        TimeSpan memory = TimeSpan.FromSeconds(_options.Reachability.FailureSeconds);
        foreach ((string joiner, DateTimeOffset at) in kept)
        {
            if (now - at < memory)
            {
                lobby.JoinFailures[joiner] = at;
            }
        }
        if (lobby.JoinFailures.Count > 0)
        {
            LobbyReachability reachability = ReachabilityOf(lobby, now, out int failures);
            _logger.LogInformation("Lobby {LobbyId} starts {Reachability}: {Count} players could not connect to this host's earlier lobbies.",
                lobby.Id, reachability, failures);
        }
    }

    void SweepReachabilityLocked(DateTimeOffset now)
    {
        TimeSpan memory = TimeSpan.FromSeconds(_options.Reachability.FailureSeconds);
        foreach (Lobby lobby in _lobbies.Values)
        {
            RemoveOlderThan(lobby.JoinFailures, now - memory);
            RemoveOlderThan(lobby.Abandoned, now - AbandonedWindow);
        }
        foreach ((string joiner, JoinerRecord record) in _joiners.ToList())
        {
            RemoveOlderThan(record.FailedLobbies, now - memory);
            if (record.FailedLobbies.Count == 0 && (record.LastJoinedAt is not { } joined || now - joined >= memory))
            {
                _joiners.Remove(joiner);
            }
        }
        foreach ((string host, Dictionary<string, DateTimeOffset> kept) in _hostFailures.ToList())
        {
            RemoveOlderThan(kept, now - memory);
            if (kept.Count == 0)
            {
                _hostFailures.Remove(host);
            }
        }
    }

    static void RemoveOlderThan(Dictionary<string, DateTimeOffset> times, DateTimeOffset cutoff)
    {
        if (times.Count == 0)
        {
            return;
        }
        foreach (string key in times.Where(t => t.Value <= cutoff).Select(t => t.Key).ToList())
        {
            times.Remove(key);
        }
    }

    static void TrimOldest(Dictionary<string, DateTimeOffset> times)
    {
        while (times.Count > MaxTrackedPerRecord)
        {
            times.Remove(times.MinBy(t => t.Value).Key);
        }
    }

    #endregion

    #region Statistics

    // Served from a snapshot at most Stats.CacheSeconds old, so polling costs one pass over the
    // lobbies per interval however many clients ask.
    public ServerStats GetStats()
    {
        DateTimeOffset now = _time.GetUtcNow();
        lock (_lock)
        {
            if (_stats is null || now - _stats.GeneratedAt >= TimeSpan.FromSeconds(_options.Stats.CacheSeconds))
                _stats = BuildStatsLocked(now);
            return _stats;
        }
    }

    ServerStats BuildStatsLocked(DateTimeOffset now)
    {
        int lobbies = 0, players = 0, capacity = 0;
        int publicLobbies = 0, friendsLobbies = 0, privateLobbies = 0, openLobbies = 0;
        int waiting = 0, inProgress = 0, migrations = 0, suspect = 0, unreachable = 0;
        Dictionary<string, int> versions = new(StringComparer.Ordinal);
        foreach (Lobby lobby in _lobbies.Values)
        {
            // Not IsLive: that removes an expired lobby, which this loop cannot allow.
            if (lobby.Removed || lobby.ExpiresAt <= now)
                continue;

            LobbySnapshot state = lobby.State;
            lobbies++;
            players += state.PlayerCount;
            capacity += state.MaxPlayers;
            if (!state.IsPrivate)
            {
                publicLobbies++;
                // What a search can match, whatever it asks for (IsOpenTo).
                if (state.Joinable && lobby.Migration is null && Occupancy(lobby, now) < state.MaxPlayers)
                    openLobbies++;
            }
            else if (state.MemberInvites)
            {
                friendsLobbies++;
            }
            else
            {
                privateLobbies++;
            }
            if (state.Phase == MatchPhase.Lobby)
                waiting++;
            else
                inProgress++;
            if (lobby.Migration is not null)
                migrations++;
            LobbyReachability reachability = ReachabilityOf(lobby, now, out _);
            if (reachability == LobbyReachability.Suspect)
            {
                suspect++;
            }
            else if (reachability == LobbyReachability.Unreachable)
            {
                unreachable++;
            }
            versions[lobby.GameVersion] = versions.GetValueOrDefault(lobby.GameVersion) + 1;
        }

        _peakPlayers = Math.Max(_peakPlayers, players);
        _peakLobbies = Math.Max(_peakLobbies, lobbies);

        int searching = _searches.Values.Count(s => s.State == SearchState.Searching && s.Deadline > now);
        int? idle = _options.Stats.Presence ? _presence.Values.Count(p => p.ExpiresAt > now) : null;
        StatsDetails details = new(
            _startedAt,
            (long)(now - _startedAt).TotalSeconds,
            lobbies,
            friendsLobbies,
            privateLobbies,
            waiting,
            inProgress,
            capacity,
            _reservations.Values.Count(r => r.JoinedAt is null && r.ExpiresAt > now),
            migrations,
            _peakPlayers,
            _peakLobbies,
            new StatsTotals(_lobbiesCreated, _lobbiesExpired, _searchesStarted, _searchesMatched, _invitesRedeemed,
                _joinsCompleted, _hostMigrationsStarted, _hostMigrationsCompleted, _hostMigrationsFailed, _joinsFailed),
            versions.OrderByDescending(v => v.Value).ThenBy(v => v.Key, StringComparer.Ordinal).Take(20)
                .ToDictionary(v => v.Key, v => v.Value),
            suspect,
            unreachable);
        return new ServerStats(now, players, searching, publicLobbies, openLobbies, idle,
            idle is null ? null : players + searching + idle, details);
    }

    // Peaks are sampled with the sweep, so they do not depend on anyone asking for statistics.
    void SamplePeaksLocked(DateTimeOffset now)
    {
        int lobbies = 0, players = 0;
        foreach (Lobby lobby in _lobbies.Values)
        {
            if (lobby.Removed || lobby.ExpiresAt <= now)
                continue;
            lobbies++;
            players += lobby.State.PlayerCount;
        }
        _peakPlayers = Math.Max(_peakPlayers, players);
        _peakLobbies = Math.Max(_peakLobbies, lobbies);
    }

    // A game in no lobby and no search says it is still running. All the server keeps is the ID
    // the game made up for this run and its address group, for the per-address cap. A report
    // lasts two intervals and a little more, so one lost report does not drop the game.
    public Failure? ReportPresence(string clientId, IPAddress? source)
    {
        if (!_options.Stats.Presence)
            return Failures.PresenceDisabled;
        if (!IsPresenceId(clientId))
            return Failures.Invalid("The presence ID must be 16 to 64 characters of A-Z, a-z, 0-9, '-' and '_'.");

        DateTimeOffset now = _time.GetUtcNow();
        DateTimeOffset expiresAt = now.AddSeconds(2 * _options.Stats.PresenceSeconds + 30);
        string sourceKey = NetworkAddresses.AccountingKey(source);
        lock (_lock)
        {
            if (_presence.TryGetValue(clientId, out PresenceRecord? record))
            {
                record.ExpiresAt = expiresAt;
                return null;
            }

            // Past a cap the report is dropped without an error: the count is best effort, and
            // the game could do nothing about it.
            if (_presence.Count >= _options.Stats.MaxPresence
                || _presenceBySource.GetValueOrDefault(sourceKey) >= _options.Stats.MaxPresencePerAddress)
            {
                return null;
            }
            _presence.Add(clientId, new PresenceRecord { SourceKey = sourceKey, ExpiresAt = expiresAt });
            _presenceBySource[sourceKey] = _presenceBySource.GetValueOrDefault(sourceKey) + 1;
            return null;
        }
    }

    // The game entered a lobby or a search, which count it from here on.
    public void ClearPresence(string clientId)
    {
        lock (_lock)
        {
            if (_presence.Remove(clientId, out PresenceRecord? record))
                ReleasePresenceSourceLocked(record.SourceKey);
        }
    }

    void SweepPresenceLocked(DateTimeOffset now)
    {
        foreach ((string clientId, PresenceRecord record) in _presence.Where(p => p.Value.ExpiresAt <= now).ToList())
        {
            _presence.Remove(clientId);
            ReleasePresenceSourceLocked(record.SourceKey);
        }
    }

    void ReleasePresenceSourceLocked(string sourceKey)
    {
        int left = _presenceBySource.GetValueOrDefault(sourceKey) - 1;
        if (left > 0)
            _presenceBySource[sourceKey] = left;
        else
            _presenceBySource.Remove(sourceKey);
    }

    static bool IsPresenceId(string clientId) =>
        clientId.Length is >= 16 and <= 64 && clientId.All(c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_');

    #endregion

    #region Expiry

    public void Sweep()
    {
        DateTimeOffset now = _time.GetUtcNow();
        int expiredLobbies;
        lock (_lock)
        {
            expiredLobbies = SweepLocked(now);
            _lobbiesExpired += expiredLobbies;
            SweepPresenceLocked(now);
            SamplePeaksLocked(now);
        }

        if (expiredLobbies > 0)
            _logger.LogInformation("Expired {Count} lobbies after missed heartbeats.", expiredLobbies);
    }

    int SweepLocked(DateTimeOffset now)
    {
        // Checks and report windows end on time even when no member is polling.
        foreach (Lobby lobby in _lobbies.Values.Where(l => l.Migration is not null).ToList())
            AdvanceMigrationLocked(lobby, now);

        List<Lobby> expiredLobbies = [.. _lobbies.Values.Where(l => l.ExpiresAt <= now)];
        foreach (Lobby lobby in expiredLobbies)
            RemoveLobbyLocked(lobby);

        // Unconfirmed reservations expire regardless of a joiner's claimed success. One that never
        // claimed it is a failed join
        foreach (Reservation reservation in _reservations.Values.Where(r => r.ExpiresAt <= now).ToList())
        {
            if (CouldBeFailedJoin(reservation))
                RecordJoinFailureLocked(reservation.Lobby, reservation.SourceKey, now, "its reservation ran out");
            RemoveReservationLocked(reservation);
        }
        SweepReachabilityLocked(now);

        // Settled members only need to answer polls until their next step is under way.
        TimeSpan settledRetention = TimeSpan.FromSeconds(_options.MigrationJoinSeconds);
        foreach (Member member in _members.Values.Where(m => (m.Outcome is not null && now - m.SettledAt >= settledRetention)
            || m.Lobby.Removed).ToList())
        {
            RemoveMemberLocked(member);
        }

        foreach (Search search in _searches.Values.Where(s => s.Deadline + SearchRetention <= now).ToList())
            _searches.Remove(search.Id);

        return expiredLobbies.Count;
    }

    #endregion

    #region Helpers

    // A request that arrives with the host key shows the host is still there, which answers
    // a migration check. A long-poll re-checking its key after waiting does not: the host
    // sent nothing new. Neither does closing the lobby.
    bool TryGetHostLobby(string lobbyId, string? hostKey, DateTimeOffset now, bool arrived,
        [NotNullWhen(true)] out Lobby? lobby, out Failure failure)
    {
        failure = Failures.LobbyNotFound;
        if (!_lobbies.TryGetValue(lobbyId, out lobby) || !IsLive(lobby, now))
        {
            lobby = null;
            return false;
        }
        if (!Secrets.Matches(lobby.HostKeyHash, hostKey))
        {
            if (lobby.ReplacedHostKeyHashes.Any(hash => Secrets.Matches(hash, hostKey)))
                failure = Failures.HostReplaced;
            lobby = null;
            return false;
        }
        if (arrived)
        {
            lobby.HostLastSeen = now;
            AdvanceMigrationLocked(lobby, now);
        }
        return true;
    }

    // Reservation possession never grants membership.
    bool TryGetMember(string memberId, string? memberKey, DateTimeOffset now, [NotNullWhen(true)] out Member? member)
    {
        if (_members.TryGetValue(memberId, out member) && Secrets.Matches(member.KeyHash, memberKey)
            && !member.Lobby.Removed && IsLive(member.Lobby, now))
        {
            return true;
        }
        member = null;
        return false;
    }

    bool TryGetActiveMember(string id, string? key, DateTimeOffset now, [NotNullWhen(true)] out Member? member)
    {
        if (TryGetMember(id, key, now, out member) && member.Lobby.Members.Contains(member))
            return true;
        member = null;
        return false;
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
        if (!lobby.Removed)
        {
            KeepHostFailuresLocked(lobby);
        }
        lobby.Removed = true;
        _lobbies.Remove(lobby.Id);
        _lobbiesByInvite.Remove(lobby.InviteCode);
        _udpTickets.Remove(lobby.UdpTicketId);
        foreach (Reservation reservation in lobby.Reservations)
        {
            _reservations.Remove(reservation.Id);
            _udpTickets.Remove(reservation.UdpTicketId);
        }
        lobby.Reservations.Clear();
        foreach (Member member in lobby.Members)
            _members.Remove(member.Id);
        lobby.Members.Clear();
        foreach (Member member in lobby.SettledMembers)
            _members.Remove(member.Id);
        lobby.SettledMembers.Clear();
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

    internal static Failure? ValidateGameVersion(string? version)
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
        IReadOnlyList<string> ids = state.MemberIds ?? [];
        if (ids.Count > _options.MaxPlayersCeiling - 1
            || ids.Any(id => !Secrets.IsId(id))
            || ids.Distinct(StringComparer.Ordinal).Count() != ids.Count)
            return Failures.Invalid("MemberIds must be distinct reservation IDs, with at most MaxPlayersCeiling - 1 members.");

        normalized = state with { LanAddresses = lanAddresses, MemberIds = ids.Order(StringComparer.Ordinal).ToList() };
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
                return Failures.Invalid("LAN addresses must be canonical RFC1918 or CGNAT IPv4 addresses; link-local addresses are not allowed.");
            if (!distinct.Contains(lanAddress))
                distinct.Add(lanAddress);
        }
        return null;
    }

    static bool SameContent(LobbySnapshot a, LobbySnapshot b) =>
        a with { LanAddresses = null, MemberIds = null } == b with { LanAddresses = null, MemberIds = null }
        && (a.LanAddresses ?? []).SequenceEqual(b.LanAddresses ?? [])
        && (a.MemberIds ?? []).SequenceEqual(b.MemberIds ?? []);

    #endregion

    #region State records

    sealed class Lobby
    {
        public required string Id { get; init; }
        public required long Sequence { get; init; }
        public required byte[] HostKeyHash { get; set; }
        public required string GameVersion { get; init; }
        // The current host's address group, for per-address limits.
        public required string SourceKey { get; set; }
        public required string InviteCode { get; set; }
        public required LobbySnapshot State { get; set; }
        // When the lobby opened or last returned to the Lobby phase; search ranks older first.
        public required DateTimeOffset WaitingSince { get; set; }
        // Address of the host's latest control request, when it was IPv4.
        public required IPAddress? PublicAddress { get; set; }
        public required DateTimeOffset ExpiresAt { get; set; }
        public List<Reservation> Reservations { get; } = [];
        // Admitted joiners, oldest first.
        public List<Member> Members { get; } = [];
        public List<Member> SettledMembers { get; } = [];
        public UInt128 UdpTicketId { get; set; }
        public IPEndPoint? ObservedEndpoint { get; set; }
        // Bumped on every change a long-poll cares about; Changed completes at the same time.
        public long EventVersion { get; set; }
        public TaskCompletionSource Changed { get; set; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        // Host migration.
        public Migration? Migration { get; set; }
        // When the current host last sent a request (not a long-poll re-check).
        public DateTimeOffset HostLastSeen { get; set; } = DateTimeOffset.MinValue;
        public DateTimeOffset? HostConfirmedAliveAt { get; set; }
        public List<byte[]> ReplacedHostKeyHashes { get; } = [];
        public bool Removed { get; set; }

        /// <summary>
        /// Host reachability is traced via excluded lobbies on retry searches
        /// </summary>
        public Dictionary<string, DateTimeOffset> JoinFailures { get; } = new(StringComparer.Ordinal);
        public Dictionary<string, DateTimeOffset> Abandoned { get; } = new(StringComparer.Ordinal);
        public DateTimeOffset? LastJoinedAt { get; set; }
        public DateTimeOffset LastProbeAt { get; set; } = DateTimeOffset.MinValue;
    }

    // A joiner address, for telling a host nobody can reach from a player who can reach nobody.
    sealed class JoinerRecord
    {
        public Dictionary<string, DateTimeOffset> FailedLobbies { get; } = new(StringComparer.Ordinal);
        public DateTimeOffset? LastJoinedAt { get; set; }
    }

    enum MigrationStage
    {
        // Waiting for the host to answer until ChallengeDeadline.
        Checking,
        // The host is lost; waiting for member reports until ReportDeadline.
        Electing,
        // A new host was chosen; waiting for its first state report.
        Handoff,
    }

    sealed class Migration
    {
        public required DateTimeOffset StartedAt { get; init; }
        public required DateTimeOffset ChallengeDeadline { get; init; }
        public MigrationStage Stage { get; set; } = MigrationStage.Checking;
        public DateTimeOffset ReportDeadline { get; set; }
    }

    // Keyed by the reservation ID it joined with; its key is that reservation's join key.
    sealed class Member
    {
        public required string Id { get; init; }
        public required byte[] KeyHash { get; init; }
        public required Lobby Lobby { get; init; }
        public required string SourceKey { get; init; }
        public required IPAddress? PublicAddress { get; init; }
        public required DateTimeOffset JoinedAt { get; init; }
        public DateTimeOffset? ReportedLostAt { get; set; }
        public bool CanHost { get; set; }
        public MigrationState? Outcome { get; set; }
        public DateTimeOffset SettledAt { get; set; }
        public HostHandoff? Handoff { get; set; }
        public JoinReservation? Reservation { get; set; }
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
        /// <summary>
        /// Migration changes hosts, doesn't count as a join
        /// </summary>
        public bool Migration { get; init; }
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

    sealed class PresenceRecord
    {
        public required string SourceKey { get; init; }
        public required DateTimeOffset ExpiresAt { get; set; }
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
        // When matches already in progress become candidates; set once, at the start.
        public DateTimeOffset InProgressFrom { get; set; }
        // Last stage seen by a poll, so each widening is logged once.
        public SearchStage Stage { get; set; }
        public SearchState State { get; set; } = SearchState.Searching;
        public SearchStatus? Result { get; set; }
        public Reservation? Reservation { get; set; }
    }

    #endregion
}
