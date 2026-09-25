using System.Net;

namespace MilanServer.Tests;

public sealed class ReservationTests : StoreTestBase
{
    [Fact]
    public void Reservation_HoldsSlotUntilCancelled()
    {
        Host(State(players: 3));

        CreateSearchResponse first = StartSearch();
        Assert.Equal(SearchState.Matched, first.Status.State);
        CreateSearchResponse second = StartSearch(from: OtherJoinerIp);
        Assert.Equal(SearchState.Searching, second.Status.State);

        JoinReservation reservation = first.Status.Reservation!;
        Assert.Null(Store.CancelReservation(reservation.ReservationId, reservation.JoinKey));
        Assert.Equal(SearchState.Matched, Poll(second).State);
    }

    [Fact]
    public void Reservation_ExpiresAndFreesSlot()
    {
        CreateLobbyResponse lobby = Host(State(players: 3));
        JoinReservation reservation = Redeem(lobby).Value;

        Assert.Equal(Failures.LobbyFull, Redeem(lobby, OtherJoinerIp).Failure);

        Advance(Options.ReservationSeconds);
        Report(lobby, State(1, players: 3));
        Assert.True(Redeem(lobby, OtherJoinerIp).IsSuccess);
        Assert.Equal(Failures.ReservationNotFound, Store.MarkReservationJoined(reservation.ReservationId, reservation.JoinKey));
    }

    [Fact]
    public void JoinedReservation_IsReleasedByNextCurrentHostReport()
    {
        CreateLobbyResponse lobby = Host(State(players: 2));
        JoinReservation joined = Redeem(lobby).Value;
        Redeem(lobby, OtherJoinerIp);
        Assert.Equal(Failures.LobbyFull, Redeem(lobby, IPAddress.Parse("192.0.2.1")).Failure);

        Assert.Null(Store.MarkReservationJoined(joined.ReservationId, joined.JoinKey));
        Assert.Equal(Failures.LobbyFull, Redeem(lobby, IPAddress.Parse("192.0.2.1")).Failure);

        // A stale report does not release the slot; a current one includes the joiner.
        Assert.True(Report(lobby, State(0, players: 2)).Stale);
        Assert.Equal(Failures.LobbyFull, Redeem(lobby, IPAddress.Parse("192.0.2.1")).Failure);
        Report(lobby, State(2, players: 3));
        Assert.Equal(Failures.LobbyFull, Redeem(lobby, IPAddress.Parse("192.0.2.1")).Failure);

        Report(lobby, State(3, players: 2));
        Assert.True(Redeem(lobby, IPAddress.Parse("192.0.2.1")).IsSuccess);
    }

    [Fact]
    public void UnjoinedReservation_SurvivesHostReports()
    {
        CreateLobbyResponse lobby = Host(State(players: 3));
        Redeem(lobby);

        Report(lobby, State(2, players: 3));

        Assert.Equal(Failures.LobbyFull, Redeem(lobby, OtherJoinerIp).Failure);
    }

    [Fact]
    public void CancelSearch_ReleasesUnjoinedReservation()
    {
        CreateLobbyResponse lobby = Host(State(players: 3));
        CreateSearchResponse search = StartSearch();
        Assert.Equal(SearchState.Matched, search.Status.State);

        Assert.Null(Store.CancelSearch(search.SearchId, search.SearchKey));

        Assert.True(Redeem(lobby, OtherJoinerIp).IsSuccess);
        Assert.Equal(Failures.SearchNotFound, Store.PollSearch(search.SearchId, search.SearchKey).Failure);
    }

    [Fact]
    public void Invite_WorksForPrivateLobbiesAndExtraction()
    {
        CreateLobbyResponse lobby = Host(State(isPrivate: true));
        Assert.True(Redeem(lobby).IsSuccess);

        Report(lobby, State(2, isPrivate: true, phase: MatchPhase.Extraction, wave: 11));
        Assert.True(Redeem(lobby, OtherJoinerIp).IsSuccess);
    }

    [Theory]
    [InlineData("complete")]
    [InlineData("not-joinable")]
    [InlineData("full")]
    [InlineData("other-version")]
    public void Invite_RespectsJoinability(string failureCase)
    {
        (LobbySnapshot state, Failure expected) = failureCase switch
        {
            "complete" => (State(phase: MatchPhase.Complete), Failures.LobbyNotJoinable),
            "not-joinable" => (State(joinable: false), Failures.LobbyNotJoinable),
            "full" => (State(players: 4), Failures.LobbyFull),
            "other-version" => (State(), Failures.VersionMismatch),
            _ => throw new ArgumentOutOfRangeException(nameof(failureCase)),
        };
        CreateLobbyResponse lobby = Host(state);

        Assert.Equal(expected, Redeem(lobby, version: failureCase == "other-version" ? "milan-0.9.0" : Version).Failure);
    }

    [Fact]
    public void Invite_AcceptsTypingVariants()
    {
        CreateLobbyResponse lobby = Host();
        string typed = lobby.InviteCode.Replace("-", " ").ToLowerInvariant().Replace('0', 'o').Replace('1', 'l');

        Assert.True(Store.RedeemInvite(new RedeemInviteRequest(typed, Version), JoinerIp).IsSuccess);
    }

    [Theory]
    [InlineData("")]
    [InlineData("ABCDE")]
    [InlineData("ABCDE-FGHJKM")]
    [InlineData("ABCDE-FGHJU")]
    public void Invite_RejectsMalformedCodes(string code)
    {
        Host();

        Assert.Equal(Failures.InviteNotFound, Store.RedeemInvite(new RedeemInviteRequest(code, Version), JoinerIp).Failure);
    }

    [Fact]
    public void Reservation_EnforcesPerAddressLimit()
    {
        Options.MaxReservationsPerAddress = 1;
        CreateLobbyResponse lobby = Host();
        Redeem(lobby);

        Assert.Equal(Failures.AddressLimit, Redeem(lobby).Failure);
    }

    [Fact]
    public void Reservation_ReturnsCandidatesAndLocalityHint()
    {
        CreateLobbyResponse lobby = Host(State(port: 7777, lan: ["192.168.1.20"]));

        JoinReservation remote = Redeem(lobby).Value;
        Assert.False(remote.SamePublicAddress);
        EndpointCandidate[] expected =
        [
            new("203.0.113.10", 7777, CandidateKind.PublicReported),
            new("192.168.1.20", 7777, CandidateKind.Lan),
        ];
        Assert.Equal(expected, remote.HostCandidates);

        // Two players in the host's household get separate reservations and join keys.
        JoinReservation housemate = Redeem(lobby, HostIp).Value;
        JoinReservation secondHousemate = Redeem(lobby, HostIp).Value;
        Assert.True(housemate.SamePublicAddress);
        Assert.NotEqual(housemate.ReservationId, secondHousemate.ReservationId);
        Assert.NotEqual(housemate.JoinKey, secondHousemate.JoinKey);
    }

    [Fact]
    public void Reservation_OmitsPublicCandidateForIPv6Host()
    {
        CreateLobbyResponse lobby = Host(State(lan: ["10.0.0.5"]), from: IPAddress.Parse("2001:db8::5"));

        JoinReservation reservation = Redeem(lobby).Value;

        EndpointCandidate[] expected = [new("10.0.0.5", 7777, CandidateKind.Lan)];
        Assert.Equal(expected, reservation.HostCandidates);
    }

    [Fact]
    public void Reservation_UsesIPv4MappedHostAddress()
    {
        CreateLobbyResponse lobby = Host(from: IPAddress.Parse("::ffff:203.0.113.10"));

        Assert.True(Redeem(lobby, HostIp).Value.SamePublicAddress);
    }

    [Fact]
    public void Sweep_RemovesExpiredState()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation reservation = Redeem(lobby).Value;

        Advance(Options.LobbyLeaseSeconds);
        Store.Sweep();

        Assert.Equal(Failures.ReservationNotFound, Store.CancelReservation(reservation.ReservationId, reservation.JoinKey));
        Assert.Equal(Failures.InviteNotFound, Redeem(lobby).Failure);
    }
}
