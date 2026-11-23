using System.Net;

namespace MilanServer.Tests;

public sealed class MigrationTests : StoreTestBase
{
    static readonly IPAddress ThirdJoinerIp = IPAddress.Parse("192.0.2.1");
    readonly Dictionary<string, List<string>> _confirmed = new(StringComparer.Ordinal);

    // Redeems, joins, and lets the host count the joiner, as a real join does.
    JoinReservation Join(CreateLobbyResponse lobby, IPAddress from, long revision, int players)
    {
        JoinReservation reservation = Redeem(lobby, from).Value;
        Assert.Null(Store.MarkReservationJoined(reservation.ReservationId, reservation.JoinKey));
        if (!_confirmed.TryGetValue(lobby.LobbyId, out List<string>? roster))
        {
            _confirmed[lobby.LobbyId] = roster = [];
        }

        roster.Add(reservation.ReservationId);
        Report(lobby, State(revision, players: players) with { MemberIds = roster.ToArray() });
        return reservation;
    }

    MigrationStatus ReportLost(JoinReservation member, bool canHost = true)
    {
        Outcome<MigrationStatus> outcome = Store.ReportHostLost(member.ReservationId, member.JoinKey, new HostLostReport(canHost));
        Assert.Null(outcome.Failure);
        return outcome.Value;
    }

    async Task<MigrationStatus> PollNow(JoinReservation member)
    {
        Outcome<MigrationStatus> outcome = await Store.PollMemberAsync(member.ReservationId, member.JoinKey, -1, CancellationToken.None);
        Assert.Null(outcome.Failure);
        return outcome.Value;
    }

    [Fact]
    public void Member_CanLeave()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation member = Join(lobby, JoinerIp, 2, 2);

        Assert.Null(Store.LeaveLobby(member.ReservationId, member.JoinKey));
        Assert.Equal(Failures.MemberNotFound, Store.LeaveLobby(member.ReservationId, member.JoinKey));
        Assert.Equal(Failures.MemberNotFound, Store.LeaveLobby(member.ReservationId, "wrong"));
    }

    [Fact]
    public void Member_GetsTheInviteCode_WhenMembersMayInvite()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation member = Join(lobby, JoinerIp, 2, 2);
        Report(lobby, State(3, players: 2) with { MemberInvites = true });

        Assert.Equal(lobby.InviteCode, Store.GetMemberInvite(member.ReservationId, member.JoinKey).Value.InviteCode);

        // Read each time, so a rotated code is never stale.
        string rotated = Store.RotateInviteCode(lobby.LobbyId, lobby.HostKey).Value.InviteCode;
        Assert.Equal(rotated, Store.GetMemberInvite(member.ReservationId, member.JoinKey).Value.InviteCode);
    }

    [Fact]
    public void Member_IsRefusedTheInviteCode_WhenOnlyTheHostInvites()
    {
        CreateLobbyResponse lobby = Host(State(isPrivate: true));
        JoinReservation member = Join(lobby, JoinerIp, 2, 2);

        Assert.Equal(Failures.InvitesHostOnly, Store.GetMemberInvite(member.ReservationId, member.JoinKey).Failure);
        Assert.Equal(Failures.MemberNotFound, Store.GetMemberInvite(member.ReservationId, "wrong").Failure);
        Assert.Equal(Failures.MemberNotFound, Store.GetMemberInvite("nobody", member.JoinKey).Failure);

        // The host switches to a mode where members invite.
        Report(lobby, State(3, isPrivate: true, players: 2) with { MemberInvites = true });
        Assert.Null(Store.GetMemberInvite(member.ReservationId, member.JoinKey).Failure);
    }

    [Fact]
    public void SelfReportedJoin_CannotReportBeforeHostConfirmation()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation reservation = Redeem(lobby).Value;
        Assert.Null(Store.MarkReservationJoined(reservation.ReservationId, reservation.JoinKey));

        Assert.Equal(Failures.MemberNotFound, Store.ReportHostLost(reservation.ReservationId, reservation.JoinKey, new HostLostReport()).Failure);
    }

    [Fact]
    public async Task HostThatAnswers_KeepsTheLobby()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation lost = Join(lobby, JoinerIp, 2, 3);
        JoinReservation fine = Join(lobby, OtherJoinerIp, 3, 3);

        Assert.Equal(MigrationState.Checking, ReportLost(lost).State);
        Advance(1);
        Report(lobby, State(3, players: 3));

        Assert.Equal(MigrationState.HostAlive, (await PollNow(lost)).State);
        // The host just proved itself, so another report is answered without a new check.
        Assert.Equal(MigrationState.HostAlive, ReportLost(fine).State);
        Assert.Null(Store.ReportLobbyState(lobby.LobbyId, lobby.HostKey, State(3, players: 3), HostIp).Failure);
    }

    [Fact]
    public async Task SilentHost_IsReplacedByTheLongestJoinedReporter()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation first = Join(lobby, JoinerIp, 2, 2);
        Advance(1);
        JoinReservation second = Join(lobby, OtherJoinerIp, 3, 3);
        Report(lobby, State(4, map: 7, enemy: 2, difficulty: 3, players: 3));

        ReportLost(second);
        Assert.Equal(MigrationState.Checking, ReportLost(first).State);
        Advance(Options.HostChallengeSeconds);

        // Everyone reported, so the choice does not wait for the report window.
        MigrationStatus newHost = await PollNow(first);
        Assert.Equal(MigrationState.NewHost, newHost.State);
        HostHandoff handoff = newHost.Host!;
        Assert.Equal((lobby.LobbyId, lobby.InviteCode, 4L, 2), (handoff.LobbyId, handoff.InviteCode, handoff.Revision, handoff.ExpectedPlayers));
        Assert.Equal((7, 2, 3), (handoff.State.MapId, handoff.State.EnemyModeId, handoff.State.DifficultyId));

        MigrationStatus joiner = await PollNow(second);
        Assert.Equal(MigrationState.JoinNewHost, joiner.State);
        JoinReservation toNewHost = joiner.Reservation!;
        Assert.Empty(toNewHost.HostCandidates);

        // The old host is told it was replaced; the new one takes over from its revision.
        Assert.Equal(Failures.HostReplaced, Store.ReportLobbyState(lobby.LobbyId, lobby.HostKey, State(5), HostIp).Failure);
        Assert.Null(Store.ReportLobbyState(lobby.LobbyId, handoff.HostKey,
            State(5, map: 7, enemy: 2, difficulty: 3, port: 7778, lan: ["192.168.1.40"]), JoinerIp).Failure);

        Outcome<ReservationStatus> status = await Store.PollReservationAsync(toNewHost.ReservationId, toNewHost.JoinKey, -1,
            CancellationToken.None);
        Assert.DoesNotContain(status.Value.HostCandidates, c => c.Kind == CandidateKind.Lan);
        Assert.Contains(status.Value.HostCandidates, c => c is { Address: "198.51.100.20", Port: 7778, Kind: CandidateKind.PublicReported });
    }

    [Fact]
    public async Task Election_PrefersMembersThatCanHost()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation first = Join(lobby, JoinerIp, 2, 2);
        JoinReservation second = Join(lobby, OtherJoinerIp, 3, 3);

        ReportLost(first, canHost: false);
        ReportLost(second);
        Advance(Options.HostChallengeSeconds);

        Assert.Equal(MigrationState.NewHost, (await PollNow(second)).State);
        Assert.Equal(MigrationState.JoinNewHost, (await PollNow(first)).State);
    }

    [Fact]
    public void HostThatLeaves_IsReplacedWithoutACheck()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation member = Join(lobby, JoinerIp, 2, 2);

        Assert.Null(Store.DeleteLobby(lobby.LobbyId, lobby.HostKey));
        // The lobby is changing hosts, so it takes no one new.
        Assert.Equal(SearchState.Searching, StartSearch(from: OtherJoinerIp).Status.State);
        Assert.Equal(Failures.LobbyNotJoinable, Redeem(lobby, OtherJoinerIp).Failure);

        Assert.Equal(MigrationState.NewHost, ReportLost(member).State);
    }

    [Fact]
    public async Task LeavingDuringACheck_DoesNotCountAsAnswering()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation member = Join(lobby, JoinerIp, 2, 2);

        ReportLost(member);
        Advance(1);
        Assert.Null(Store.DeleteLobby(lobby.LobbyId, lobby.HostKey));

        Assert.Equal(MigrationState.NewHost, (await PollNow(member)).State);
    }

    [Fact]
    public async Task Election_WaitsForTheReportWindowThenDropsSilentMembers()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation reporter = Join(lobby, JoinerIp, 2, 3);
        JoinReservation silent = Join(lobby, OtherJoinerIp, 3, 3);

        ReportLost(reporter);
        Advance(Options.HostChallengeSeconds);
        Assert.Equal(MigrationState.Electing, (await PollNow(reporter)).State);

        Advance(Options.MigrationReportSeconds);
        MigrationStatus status = await PollNow(reporter);
        Assert.Equal((MigrationState.NewHost, 1), (status.State, status.Host!.ExpectedPlayers));
        Assert.Equal(Failures.MemberNotFound, Store.ReportHostLost(silent.ReservationId, silent.JoinKey, new HostLostReport()).Failure);
    }

    [Fact]
    public void LobbyWithoutReporters_Closes()
    {
        CreateLobbyResponse lobby = Host();
        Join(lobby, JoinerIp, 2, 2);

        Assert.Null(Store.DeleteLobby(lobby.LobbyId, lobby.HostKey));
        Advance(Options.MigrationReportSeconds);
        Store.Sweep();

        Assert.Equal(Failures.InviteNotFound, Redeem(lobby, OtherJoinerIp).Failure);
    }

    [Fact]
    public void NewHostThatNeverReports_LetsTheLobbyExpire()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation member = Join(lobby, JoinerIp, 2, 2);
        Store.DeleteLobby(lobby.LobbyId, lobby.HostKey);
        HostHandoff handoff = ReportLost(member).Host!;

        Advance(Options.LobbyLeaseSeconds);
        Store.Sweep();

        Assert.Equal(Failures.LobbyNotFound, Store.ReportLobbyState(lobby.LobbyId, handoff.HostKey, State(3), JoinerIp).Failure);
    }

    [Fact]
    public async Task MemberPoll_ReturnsWhenTheCheckEnds()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation member = Join(lobby, JoinerIp, 2, 2);
        MigrationStatus checking = ReportLost(member);

        Task<Outcome<MigrationStatus>> poll = Store.PollMemberAsync(member.ReservationId, member.JoinKey, checking.Version,
            CancellationToken.None);
        Assert.False(poll.IsCompleted);

        Advance(Options.HostChallengeSeconds);
        Outcome<MigrationStatus> result = await poll.WaitAsync(TimeSpan.FromSeconds(5), TestContext.Current.CancellationToken);
        Assert.Equal(MigrationState.NewHost, result.Value.State);
    }

    [Fact]
    public void MigrationTimings_AreValidated()
    {
        Assert.True(new MilanServerOptionsValidator().Validate(null, new MilanServerOptions { HostChallengeSeconds = 0 }).Failed);
        Assert.True(new MilanServerOptionsValidator().Validate(null, new MilanServerOptions { MigrationJoinSeconds = 0 }).Failed);
    }
}
