using System.Buffers.Binary;
using System.Buffers.Text;
using System.Net;
using Microsoft.Extensions.Hosting.Internal;
using Microsoft.Extensions.Logging.Abstractions;

namespace MilanServer.Tests;

public sealed class RestartTests : StoreTestBase, IDisposable
{
    static readonly IPAddress ThirdJoinerIp = IPAddress.Parse("192.0.2.1");
    readonly string _folder = Path.Combine(Path.GetTempPath(), "milan-restart-" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        if (Directory.Exists(_folder))
        {
            Directory.Delete(_folder, true);
        }
    }

    // The next server process, started at the test clock's current time.
    MatchmakingStore NewStore() =>
        new(Microsoft.Extensions.Options.Options.Create(Options), Time, NullLogger<MatchmakingStore>.Instance);

    StoreStateFile File(MatchmakingStore store) =>
        new(store, Microsoft.Extensions.Options.Options.Create(Options), new HostingEnvironment { ContentRootPath = _folder }, Time,
            NullLogger<StoreStateFile>.Instance);

    // A member the host has confirmed, and a joiner still on its way in.
    (CreateLobbyResponse Lobby, JoinReservation Member, JoinReservation Pending) LobbyWithJoiners()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation member = Redeem(lobby).Value;
        Assert.Null(Store.MarkReservationJoined(member.ReservationId, member.JoinKey));
        Report(lobby, State(2, players: 2) with { MemberIds = [member.ReservationId] });
        JoinReservation pending = Redeem(lobby, OtherJoinerIp).Value;
        return (lobby, member, pending);
    }

    [Fact]
    public async Task Restart_KeepsLobbiesMembersAndJoiners_WithTheDowntimeAdded()
    {
        (CreateLobbyResponse lobby, JoinReservation member, JoinReservation pending) = LobbyWithJoiners();
        SavedStore saved = Store.StopForRestart();
        Advance(20);
        MatchmakingStore restarted = NewStore();
        Assert.Equal(1, restarted.Restore(saved));

        // The joiner's 30 s reservation had 10 s left at the restart, and still has.
        Outcome<ReservationStatus> joining = await restarted.PollReservationAsync(pending.ReservationId, pending.JoinKey, -1, CancellationToken.None);
        Assert.Null(joining.Failure);
        Assert.Equal(Failures.InviteNotFound, restarted.RedeemInvite(new RedeemInviteRequest("ZZZZZ-ZZZZZ", Version), ThirdJoinerIp).Failure);
        Assert.Null(restarted.RedeemInvite(new RedeemInviteRequest(lobby.InviteCode, Version), ThirdJoinerIp).Failure);

        // Past the lease the host had at the restart, but not past it plus the downtime.
        Advance(Options.LobbyLeaseSeconds - 10);
        Assert.Null(restarted.ReportLobbyState(lobby.LobbyId, lobby.HostKey, State(3, players: 2), HostIp).Failure);
        Assert.Equal(MigrationState.Checking,
            restarted.ReportHostLost(member.ReservationId, member.JoinKey, new HostLostReport(true)).Value.State);
    }

    [Fact]
    public void Restart_KeepsTheUdpTicket()
    {
        CreateLobbyResponse lobby = Host();
        MatchmakingStore restarted = NewStore();
        restarted.Restore(Store.StopForRestart());

        (UInt128 id, byte[] secret) = (BinaryPrimitives.ReadUInt128BigEndian(Base64Url.DecodeFromChars(lobby.UdpTicket.TicketId)),
            Base64Url.DecodeFromChars(lobby.UdpTicket.Secret));
        byte[] request = new byte[DiscoveryPacket.RequestLength];
        DiscoveryPacket.WriteRequest(request, id, 1, secret);
        byte[] response = new byte[DiscoveryPacket.ResponseLength];

        Assert.Equal(DiscoveryPacket.ResponseLength, restarted.HandleDiscovery(request, new IPEndPoint(HostIp, 40001), response));
    }

    [Fact]
    public void Restart_LeavesOutLobbiesChangingHosts()
    {
        (CreateLobbyResponse _, JoinReservation member, JoinReservation _) = LobbyWithJoiners();
        Assert.Null(Store.ReportHostLost(member.ReservationId, member.JoinKey, new HostLostReport(true)).Failure);

        Assert.Empty(Store.StopForRestart().Lobbies);
    }

    [Fact]
    public async Task StopForRestart_ReleasesWaitingPolls()
    {
        CreateLobbyResponse lobby = Host();
        long version = (await Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, -1, CancellationToken.None)).Value.Version;
        Task<Outcome<IntroductionList>> waiting = Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, version, CancellationToken.None);
        Assert.False(waiting.IsCompleted);

        Store.StopForRestart();

        Assert.True(Store.IsStopped);
        Assert.Null((await waiting.WaitAsync(TimeSpan.FromSeconds(10), TestContext.Current.CancellationToken)).Failure);
    }

    [Fact]
    public void StateFile_RoundTrips_AndIsDeletedOnceRead()
    {
        Options.StateFile = "state/lobbies.json";
        (CreateLobbyResponse lobby, _, _) = LobbyWithJoiners();
        File(Store).Save();
        string path = Path.Combine(_folder, "state", "lobbies.json");
        Assert.True(System.IO.File.Exists(path));
        if (!OperatingSystem.IsWindows())
        {
            Assert.Equal(UnixFileMode.UserRead | UnixFileMode.UserWrite, System.IO.File.GetUnixFileMode(path));
        }

        MatchmakingStore restarted = NewStore();
        File(restarted).Restore();

        Assert.False(System.IO.File.Exists(path));
        Assert.Null(restarted.ReportLobbyState(lobby.LobbyId, lobby.HostKey, State(3, players: 2), HostIp).Failure);
    }

    [Fact]
    public void StateFile_IgnoresOldAndDamagedFiles()
    {
        Options.StateFile = "lobbies.json";
        CreateLobbyResponse lobby = Host();
        File(Store).Save();
        Advance(Options.StateMaxAgeSeconds + 1);

        MatchmakingStore late = NewStore();
        File(late).Restore();
        Assert.Equal(Failures.LobbyNotFound, late.ReportLobbyState(lobby.LobbyId, lobby.HostKey, State(2), HostIp).Failure);
        Assert.False(System.IO.File.Exists(Path.Combine(_folder, "lobbies.json")));

        System.IO.File.WriteAllText(Path.Combine(_folder, "lobbies.json"), "{ \"format\": 1, \"lobbies\": [");
        File(NewStore()).Restore();
        Assert.False(System.IO.File.Exists(Path.Combine(_folder, "lobbies.json")));
    }

    [Theory]
    [InlineData(0, false)]
    [InlineData(1, true)]
    [InlineData(3600, true)]
    [InlineData(3601, false)]
    public void StateMaxAgeSeconds_IsValidated(int seconds, bool valid)
    {
        MilanServerOptions options = new() { StateMaxAgeSeconds = seconds };

        Assert.Equal(valid, new MilanServerOptionsValidator().Validate(null, options).Succeeded);
    }
}
