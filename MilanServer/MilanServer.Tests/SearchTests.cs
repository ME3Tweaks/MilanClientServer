namespace MilanServer.Tests;

public sealed class SearchTests : StoreTestBase
{
    // Searches span minutes here; keep lobbies alive without heartbeats.
    public SearchTests() => Options.LobbyLeaseSeconds = 600;

    [Fact]
    public void AnyFilters_MatchImmediately()
    {
        CreateLobbyResponse lobby = Host();

        SearchStatus status = StartSearch().Status;

        Assert.Equal(SearchState.Matched, status.State);
        Assert.Equal(lobby.LobbyId, status.Reservation?.Lobby.LobbyId);
    }

    [Theory]
    [InlineData("private")]
    [InlineData("not-joinable")]
    [InlineData("extraction")]
    [InlineData("complete")]
    [InlineData("full")]
    [InlineData("other-version")]
    public void Search_ExcludesUnsuitableLobbies(string unsuitableCase)
    {
        LobbySnapshot state = unsuitableCase switch
        {
            "private" => State(isPrivate: true),
            "not-joinable" => State(joinable: false),
            "extraction" => State(phase: MatchPhase.Extraction, wave: 11),
            "complete" => State(phase: MatchPhase.Complete, wave: 11),
            "full" => State(players: 4, maxPlayers: 4),
            "other-version" => State(),
            _ => throw new ArgumentOutOfRangeException(nameof(unsuitableCase)),
        };
        Host(state, version: unsuitableCase == "other-version" ? "milan-0.9.0" : Version);

        Assert.Equal(SearchState.Searching, StartSearch().Status.State);
    }

    [Fact]
    public void Widening_FollowsScheduleAndHoldsDifficulty()
    {
        CreateLobbyResponse otherMap = Host(State(map: 6, enemy: 3, difficulty: 2));
        Host(State(map: 6, enemy: 4, difficulty: 2), from: JoinerIp);
        Host(State(map: 5, enemy: 3, difficulty: 3), from: OtherJoinerIp);

        CreateSearchResponse search = StartSearch(map: 5, enemy: 3, difficulty: 2);
        Assert.Equal((SearchState.Searching, SearchStage.Exact), (search.Status.State, search.Status.Stage));

        Advance(Options.MapWidenSeconds - 1);
        Assert.Equal(SearchState.Searching, Poll(search).State);

        Advance(1);
        SearchStatus widened = Poll(search);
        Assert.Equal((SearchState.Matched, SearchStage.AnyMap), (widened.State, widened.Stage));
        Assert.Equal(otherMap.LobbyId, widened.Reservation?.Lobby.LobbyId);
    }

    [Fact]
    public void Widening_ReachesAnyEnemyThenGivesUpWithoutCreatingLobby()
    {
        CreateLobbyResponse otherEnemy = Host(State(map: 6, enemy: 4, difficulty: 2));
        Host(State(map: 5, enemy: 3, difficulty: 3), from: OtherJoinerIp);

        CreateSearchResponse search = StartSearch(map: 5, enemy: 3, difficulty: 2);
        Advance(Options.MapWidenSeconds);
        Assert.Equal((SearchState.Searching, SearchStage.AnyMap), (Poll(search).State, Poll(search).Stage));

        Advance(Options.EnemyWidenSeconds - Options.MapWidenSeconds);
        SearchStatus anyEnemy = Poll(search);
        Assert.Equal((SearchState.Matched, SearchStage.AnyMapAndEnemy), (anyEnemy.State, anyEnemy.Stage));
        Assert.Equal(otherEnemy.LobbyId, anyEnemy.Reservation?.Lobby.LobbyId);
        Assert.Null(Store.DeleteLobby(otherEnemy.LobbyId, otherEnemy.HostKey));

        // A second search only has the wrong-difficulty lobby left, so it runs out.
        CreateSearchResponse hopeless = StartSearch(map: 5, enemy: 3, difficulty: 2, from: OtherJoinerIp);
        Advance(Options.SearchSeconds - 1);
        Assert.Equal(SearchState.Searching, Poll(hopeless).State);
        Advance(1);
        Assert.Equal(SearchState.NoMatch, Poll(hopeless).State);
        Assert.Equal(SearchState.NoMatch, Poll(hopeless).State);
    }

    [Fact]
    public void WideningDisabled_StaysExact()
    {
        Host(State(map: 6));

        CreateSearchResponse search = StartSearch(map: 5, enemy: 3, difficulty: 2, widen: false);
        Advance(Options.SearchSeconds - 1);

        SearchStatus status = Poll(search);
        Assert.Equal((SearchState.Searching, SearchStage.Exact), (status.State, status.Stage));
    }

    [Fact]
    public void RandomMapLobby_DoesNotSatisfySpecificMap()
    {
        Host(State(map: 0));

        CreateSearchResponse search = StartSearch(map: 5, enemy: 3, difficulty: 2);
        Assert.Equal(SearchState.Searching, search.Status.State);

        Advance(Options.MapWidenSeconds);
        Assert.Equal(SearchState.Matched, Poll(search).State);
    }

    [Fact]
    public void EnemyFilter_MatchesSelectedModeNotResolvedFaction()
    {
        // Coalition (mode 1) currently fighting the faction with ID 4.
        Host(State(enemy: 1) with { ResolvedEnemyId = 4, Phase = MatchPhase.InProgress, WaveNumber = 3 });

        Assert.Equal(SearchState.Searching, StartSearch(enemy: 4).Status.State);
        Assert.Equal(SearchState.Matched, StartSearch(enemy: 1, from: OtherJoinerIp).Status.State);
    }

    [Fact]
    public void Ranking_PrefersLobbiesThenEarlierWaves()
    {
        Host(State(phase: MatchPhase.InProgress, wave: 2));
        CreateLobbyResponse waiting = Host(State(phase: MatchPhase.Lobby), from: JoinerIp);
        CreateLobbyResponse earlyWave = Host(State(phase: MatchPhase.InProgress, wave: 1), from: OtherJoinerIp);

        Assert.Equal(waiting.LobbyId, StartSearch().Status.Reservation?.Lobby.LobbyId);

        Assert.Null(Store.DeleteLobby(waiting.LobbyId, waiting.HostKey));
        Assert.Equal(earlyWave.LobbyId, StartSearch(from: OtherJoinerIp).Status.Reservation?.Lobby.LobbyId);
    }

    [Fact]
    public void Ranking_PrefersOriginalFiltersAfterWidening()
    {
        CreateSearchResponse search = StartSearch(map: 5, enemy: 3, difficulty: 2);
        Advance(Options.EnemyWidenSeconds);

        Host(State(map: 6, enemy: 4, players: 3));
        Host(State(map: 6, enemy: 3, players: 2), from: JoinerIp);
        CreateLobbyResponse exact = Host(State(map: 5, enemy: 3), from: OtherJoinerIp);

        Assert.Equal(exact.LobbyId, Poll(search).Reservation?.Lobby.LobbyId);
    }

    [Fact]
    public void ExcludedLobbies_AreSkipped()
    {
        CreateLobbyResponse failed = Host();

        Assert.Equal(SearchState.Searching, StartSearch(exclude: [failed.LobbyId]).Status.State);
    }

    [Fact]
    public void HostChange_AppliesToNextSearchDecision()
    {
        CreateLobbyResponse lobby = Host();
        CreateSearchResponse search = StartSearch(map: 7, widen: false);
        Assert.Equal(SearchState.Searching, search.Status.State);

        Report(lobby, State(2, map: 7, isPrivate: true));
        Assert.Equal(SearchState.Searching, Poll(search).State);

        Report(lobby, State(3, map: 7));
        Assert.Equal(SearchState.Matched, Poll(search).State);
    }

    [Fact]
    public void Poll_ReturnsSameReservationAfterMatch_AndRequiresSearchKey()
    {
        Host();
        CreateSearchResponse search = StartSearch();

        Assert.Equal(search.Status.Reservation?.ReservationId, Poll(search).Reservation?.ReservationId);
        Assert.Equal(Failures.SearchNotFound, Store.PollSearch(search.SearchId, "wrong").Failure);
    }

    [Fact]
    public void Search_EnforcesPerAddressLimit()
    {
        Options.MaxSearchesPerAddress = 1;
        StartSearch();

        Assert.Equal(Failures.AddressLimit, Store.CreateSearch(new SearchRequest(Version), JoinerIp).Failure);
    }
}
