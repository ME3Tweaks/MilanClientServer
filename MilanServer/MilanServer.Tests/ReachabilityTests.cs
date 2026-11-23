using System.Net;

namespace MilanServer.Tests;

public sealed class ReachabilityTests : StoreTestBase
{
    // Probes wait minutes here; keep lobbies alive without heartbeats.
    public ReachabilityTests() => Options.LobbyLeaseSeconds = 600;

    static IPAddress Player(int n) => IPAddress.Parse($"198.51.100.{100 + n}");

    // A stale report from no known address changes nothing and returns what the host would see.
    LobbyStateResponse Check(CreateLobbyResponse lobby)
    {
        Outcome<LobbyStateResponse> outcome = Store.ReportLobbyState(lobby.LobbyId, lobby.HostKey, State(0), null);
        Assert.Null(outcome.Failure);
        return outcome.Value;
    }

    // The player's game gets no answer from the host, gives up, and searches again without the lobby.
    CreateSearchResponse FailBySearch(CreateLobbyResponse lobby, IPAddress from)
    {
        CreateSearchResponse search = StartSearch(from: from);
        Assert.Equal(lobby.LobbyId, search.Status.Reservation?.Lobby.LobbyId);
        Assert.Null(Store.CancelSearch(search.SearchId, search.SearchKey));
        return StartSearch(from: from, exclude: [lobby.LobbyId]);
    }

    void FailByExpiry(CreateLobbyResponse lobby, IPAddress from)
    {
        Assert.Equal(lobby.LobbyId, StartSearch(from: from).Status.Reservation?.Lobby.LobbyId);
        Advance(Options.ReservationSeconds + 1);
        Store.Sweep();
    }

    [Fact]
    public void UnansweredHost_BecomesSuspect_AfterTwoPlayers()
    {
        CreateLobbyResponse lobby = Host();

        FailBySearch(lobby, Player(1));
        Assert.Equal((LobbyReachability.Unknown, 1), (Check(lobby).Reachability, Check(lobby).FailedJoins));
        FailBySearch(lobby, Player(2));

        LobbyStateResponse state = Check(lobby);
        Assert.Equal(LobbyReachability.Suspect, state.Reachability);
        Assert.Equal(2, state.FailedJoins);
        StatsDetails details = Store.GetStats().Details!;
        Assert.Equal(1, details.SuspectLobbies);
        Assert.Equal(2, details.Totals.JoinsFailed);
    }

    [Fact]
    public void ExpiredReservations_AreFailures_UnlessTheJoinerGotIn()
    {
        CreateLobbyResponse lobby = Host();
        FailByExpiry(lobby, Player(1));
        FailByExpiry(lobby, Player(2));
        Assert.Equal(LobbyReachability.Suspect, Check(lobby).Reachability);

        CreateLobbyResponse other = Host(from: OtherJoinerIp);
        JoinReservation joined = StartSearch(from: Player(3), exclude: [lobby.LobbyId]).Status.Reservation!;
        Assert.Equal(other.LobbyId, joined.Lobby.LobbyId);
        Assert.Null(Store.MarkReservationJoined(joined.ReservationId, joined.JoinKey));
        Advance(Options.ReservationSeconds + 1);
        Store.Sweep();

        Assert.Equal(0, Check(other).FailedJoins);
    }

    [Fact]
    public void CancellingWithoutLeavingTheLobbyOut_IsNotAFailure()
    {
        CreateLobbyResponse lobby = Host();
        CreateSearchResponse search = StartSearch(from: Player(1));
        Assert.Null(Store.CancelSearch(search.SearchId, search.SearchKey));

        Assert.Equal(SearchState.Matched, StartSearch(from: Player(1)).Status.State);
        Assert.Equal(0, Check(lobby).FailedJoins);
    }

    [Fact]
    public void ExcludingALobbyNeverMatched_IsNotAFailure()
    {
        CreateLobbyResponse lobby = Host();

        StartSearch(from: Player(1), exclude: [lobby.LobbyId]);

        Assert.Equal(0, Check(lobby).FailedJoins);
    }

    [Fact]
    public void JoinsFromTheHostsAddress_DoNotCount()
    {
        CreateLobbyResponse lobby = Host();

        FailBySearch(lobby, HostIp);
        FailByExpiry(lobby, HostIp);

        Assert.Equal(0, Check(lobby).FailedJoins);
    }

    [Fact]
    public void SuspectLobby_IsOfferedAfterOthers()
    {
        CreateLobbyResponse suspect = Host();
        FailBySearch(suspect, Player(1));
        FailBySearch(suspect, Player(2));
        Advance(1);
        CreateLobbyResponse newer = Host(from: OtherJoinerIp);

        Assert.Equal(newer.LobbyId, StartSearch(from: Player(3)).Status.Reservation?.Lobby.LobbyId);
    }

    [Fact]
    public void LobbyNobodyJoined_TakesOneSearchJoinerAtATime()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation first = StartSearch(from: Player(1)).Status.Reservation!;
        CreateSearchResponse second = StartSearch(from: Player(2));
        Assert.Equal(SearchState.Searching, second.Status.State);
        // Invites are never held back.
        Assert.True(Redeem(lobby, Player(3)).IsSuccess);

        LobbyStateResponse admitted = Report(lobby, State(2, players: 2) with { MemberIds = [first.ReservationId] });
        Assert.Equal(LobbyReachability.Proven, admitted.Reachability);

        Assert.Equal(SearchState.Matched, Poll(second).State);
    }

    [Fact]
    public void ProvenLobby_TakesSeveralSearchJoiners()
    {
        CreateLobbyResponse lobby = Host(State(maxPlayers: 6));
        JoinReservation first = StartSearch(from: Player(1)).Status.Reservation!;
        Report(lobby, State(2, players: 2, maxPlayers: 6) with { MemberIds = [first.ReservationId] });

        Assert.Equal(SearchState.Matched, StartSearch(from: Player(2)).Status.State);
        Assert.Equal(SearchState.Matched, StartSearch(from: Player(3)).Status.State);
    }

    [Fact]
    public void SuccessfulJoin_ClearsFailures_AndLaterFailuresCountAgain()
    {
        CreateLobbyResponse lobby = Host(State(maxPlayers: 6));
        FailBySearch(lobby, Player(1));
        FailBySearch(lobby, Player(2));
        JoinReservation joined = StartSearch(from: Player(3)).Status.Reservation!;
        Assert.Equal(lobby.LobbyId, joined.Lobby.LobbyId);

        LobbyStateResponse admitted = Report(lobby, State(2, players: 2, maxPlayers: 6) with { MemberIds = [joined.ReservationId] });
        Assert.Equal((LobbyReachability.Proven, 0), (admitted.Reachability, admitted.FailedJoins));

        FailBySearch(lobby, Player(4));
        Assert.Equal(LobbyReachability.Proven, Check(lobby).Reachability);
        FailBySearch(lobby, Player(5));
        Assert.Equal(LobbyReachability.Suspect, Check(lobby).Reachability);
    }

    [Fact]
    public void UnreachableLobby_IsOnlyTestedEveryProbeInterval()
    {
        CreateLobbyResponse lobby = Host();
        for (int n = 1; n <= 4; n++)
        {
            FailBySearch(lobby, Player(n));
        }

        Assert.Equal(LobbyReachability.Unreachable, Check(lobby).Reachability);
        Assert.Equal(1, Store.GetStats().Details!.UnreachableLobbies);

        Assert.Equal(SearchState.Searching, StartSearch(from: Player(5)).Status.State);
        Advance(Options.Reachability.ProbeSeconds);

        Assert.Equal(lobby.LobbyId, StartSearch(from: Player(6)).Status.Reservation?.Lobby.LobbyId);
        Assert.Equal(SearchState.Searching, StartSearch(from: Player(7)).Status.State);
    }

    [Fact]
    public void PlayerWhoFailsEverywhere_DoesNotCount()
    {
        CreateLobbyResponse a = Host();
        List<CreateLobbyResponse> lobbies = [a];
        for (int n = 1; n < Options.Reachability.JoinerFailureLimit; n++)
        {
            lobbies.Add(Host(from: Player(50 + n)));
        }

        // One player fails on JoinerFailureLimit lobbies in turn.
        List<string> excluded = [];
        foreach (CreateLobbyResponse lobby in lobbies)
        {
            CreateSearchResponse search = StartSearch(from: JoinerIp, exclude: [.. excluded]);
            Assert.Equal(lobby.LobbyId, search.Status.Reservation?.Lobby.LobbyId);
            Assert.Null(Store.CancelSearch(search.SearchId, search.SearchKey));
            excluded.Add(lobby.LobbyId);
        }
        StartSearch(from: JoinerIp, exclude: [.. excluded]);
        Assert.Equal(0, Check(a).FailedJoins);

        FailBySearch(a, Player(1));

        LobbyStateResponse state = Check(a);
        Assert.Equal((LobbyReachability.Unknown, 1), (state.Reachability, state.FailedJoins));
    }

    [Fact]
    public void FailuresCarryOverToTheHostsNextLobby()
    {
        CreateLobbyResponse first = Host();
        FailBySearch(first, Player(1));
        FailBySearch(first, Player(2));
        Assert.Null(Store.DeleteLobby(first.LobbyId, first.HostKey));

        CreateLobbyResponse next = Host();

        LobbyStateResponse state = Check(next);
        Assert.Equal((LobbyReachability.Suspect, 2), (state.Reachability, state.FailedJoins));
    }

    [Fact]
    public void FailuresAreForgotten()
    {
        CreateLobbyResponse lobby = Host();
        FailBySearch(lobby, Player(1));
        FailBySearch(lobby, Player(2));

        // Halfway, a heartbeat keeps the lobby's lease.
        Advance(Options.Reachability.FailureSeconds / 2);
        Check(lobby);
        Advance(Options.Reachability.FailureSeconds - Options.Reachability.FailureSeconds / 2);
        Store.Sweep();

        Assert.Equal((LobbyReachability.Unknown, 0), (Check(lobby).Reachability, Check(lobby).FailedJoins));
    }

    [Fact]
    public void Disabled_LeavesQuickMatchUnchanged()
    {
        Options.Reachability.Enabled = false;
        CreateLobbyResponse lobby = Host();
        FailBySearch(lobby, Player(1));
        FailBySearch(lobby, Player(2));

        // Still counted and reported, but two searches get the same unproven lobby at once.
        Assert.Equal(LobbyReachability.Suspect, Check(lobby).Reachability);
        Assert.Equal(lobby.LobbyId, StartSearch(from: Player(3)).Status.Reservation?.Lobby.LobbyId);
        Assert.Equal(lobby.LobbyId, StartSearch(from: Player(4)).Status.Reservation?.Lobby.LobbyId);
    }
}
