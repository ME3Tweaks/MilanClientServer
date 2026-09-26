using System.Net;
using System.Text.RegularExpressions;

namespace MilanServer.Tests;

public sealed class LobbyTests : StoreTestBase
{
    [Fact]
    public void CreateLobby_ReturnsSeparateCapabilities()
    {
        CreateLobbyResponse lobby = Host();

        Assert.Matches(new Regex("^[0-9A-HJKMNP-TV-Z]{5}-[0-9A-HJKMNP-TV-Z]{5}$"), lobby.InviteCode);
        Assert.Equal(43, lobby.HostKey.Length);
        Assert.NotEqual(lobby.LobbyId, lobby.HostKey);
        Assert.Equal(Options.HeartbeatSeconds, lobby.HeartbeatSeconds);
        Assert.Equal(Time.GetUtcNow().AddSeconds(Options.LobbyLeaseSeconds), lobby.ExpiresAt);
    }

    [Fact]
    public void ReportState_RequiresHostKey()
    {
        CreateLobbyResponse lobby = Host();

        Assert.Equal(Failures.LobbyNotFound, Store.ReportLobbyState(lobby.LobbyId, null, State(2), HostIp).Failure);
        Assert.Equal(Failures.LobbyNotFound, Store.ReportLobbyState(lobby.LobbyId, lobby.InviteCode, State(2), HostIp).Failure);
        Assert.Equal(Failures.LobbyNotFound, Store.DeleteLobby(lobby.LobbyId, "wrong"));
    }

    [Fact]
    public void ReportState_NewerRevisionApplies_OlderRevisionIsIgnored()
    {
        CreateLobbyResponse lobby = Host();

        LobbyStateResponse newer = Report(lobby, State(3, map: 7));
        Assert.False(newer.Stale);
        Assert.Equal(3, newer.Revision);

        LobbyStateResponse older = Report(lobby, State(2, map: 8));
        Assert.True(older.Stale);
        Assert.Equal(3, older.Revision);

        Assert.Equal(7, Redeem(lobby).Value.Lobby.MapId);
    }

    [Fact]
    public void ReportState_SameRevision_IsHeartbeatOnlyWithSameContent()
    {
        CreateLobbyResponse lobby = Host(State(1, lan: ["192.168.1.20"]));

        Assert.False(Report(lobby, State(1, lan: ["192.168.1.20"])).Stale);
        Assert.Equal(Failures.RevisionConflict,
            Store.ReportLobbyState(lobby.LobbyId, lobby.HostKey, State(1, map: 9), HostIp).Failure);
    }

    [Fact]
    public void Lobby_ExpiresWithoutHeartbeat()
    {
        CreateLobbyResponse lobby = Host();

        Advance(Options.LobbyLeaseSeconds - 10);
        Report(lobby, State(1));
        Advance(Options.LobbyLeaseSeconds - 10);
        Assert.True(Redeem(lobby).IsSuccess);

        Advance(10);
        Assert.Equal(Failures.InviteNotFound, Redeem(lobby, OtherJoinerIp).Failure);
        Assert.Equal(Failures.LobbyNotFound, Store.ReportLobbyState(lobby.LobbyId, lobby.HostKey, State(2), HostIp).Failure);
    }

    [Fact]
    public void DeleteLobby_RemovesInviteAndSearchVisibility()
    {
        CreateLobbyResponse lobby = Host();

        Assert.Null(Store.DeleteLobby(lobby.LobbyId, lobby.HostKey));

        Assert.Equal(Failures.InviteNotFound, Redeem(lobby).Failure);
        Assert.Equal(SearchState.Searching, StartSearch().Status.State);
    }

    [Fact]
    public void RotateInviteCode_InvalidatesOldCode()
    {
        CreateLobbyResponse lobby = Host();

        string newCode = Store.RotateInviteCode(lobby.LobbyId, lobby.HostKey).Value.InviteCode;

        Assert.Equal(Failures.InviteNotFound, Redeem(lobby).Failure);
        Assert.True(Store.RedeemInvite(new RedeemInviteRequest(newCode, Version), JoinerIp).IsSuccess);
    }

    [Theory]
    [InlineData("public-lan")]
    [InlineData("too-many-lan")]
    [InlineData("non-canonical-lan")]
    [InlineData("over-ceiling")]
    [InlineData("no-host-player")]
    [InlineData("players-over-max")]
    [InlineData("any-map")]
    [InlineData("any-enemy")]
    [InlineData("any-difficulty")]
    [InlineData("bad-port")]
    public void CreateLobby_RejectsInvalidState(string invalidCase)
    {
        LobbySnapshot state = invalidCase switch
        {
            "public-lan" => State(lan: ["8.8.8.8"]),
            "too-many-lan" => State(lan: ["10.0.0.1", "10.0.0.2", "10.0.0.3", "10.0.0.4", "10.0.0.5"]),
            "non-canonical-lan" => State(lan: ["192.168.001.020"]),
            "over-ceiling" => State(maxPlayers: Options.MaxPlayersCeiling + 1),
            "no-host-player" => State(players: 0),
            "players-over-max" => State(players: 5, maxPlayers: 4),
            "any-map" => State(map: GameSettingIds.AnyMap),
            "any-enemy" => State(enemy: GameSettingIds.AnyEnemy),
            "any-difficulty" => State(difficulty: GameSettingIds.AnyDifficulty),
            "bad-port" => State(port: 0),
            _ => throw new ArgumentOutOfRangeException(nameof(invalidCase)),
        };

        Assert.Equal("invalid_request", Store.CreateLobby(new CreateLobbyRequest(Version, state), HostIp).Failure?.Code);
    }

    [Fact]
    public void CreateLobby_AcceptsCeilingAndUnknownFutureSettingIds()
    {
        Host(State(map: 4000, enemy: 90, difficulty: 55, maxPlayers: Options.MaxPlayersCeiling,
            lan: ["10.1.2.3", "172.20.0.4", "169.254.3.3", "100.100.1.1"]));
    }

    [Theory]
    [InlineData("")]
    [InlineData("has space")]
    [InlineData("0123456789012345678901234567890123456789012345678901234567890123456789")]
    public void CreateLobby_RejectsBadGameVersion(string version)
    {
        Assert.Equal("invalid_request", Store.CreateLobby(new CreateLobbyRequest(version, State()), HostIp).Failure?.Code);
    }

    [Fact]
    public void CreateLobby_EnforcesPerAddressLimit_GroupingIPv6By64()
    {
        Options.MaxLobbiesPerAddress = 2;
        IPAddress v6a = IPAddress.Parse("2001:db8:1:2::10");
        IPAddress v6b = IPAddress.Parse("2001:db8:1:2:ffff::20");

        Host(from: v6a);
        Host(from: v6b);

        Assert.Equal(Failures.AddressLimit, Store.CreateLobby(new CreateLobbyRequest(Version, State()), v6a).Failure);
        Host(from: IPAddress.Parse("2001:db8:1:3::10"));
    }

    [Fact]
    public void CreateLobby_ServerLimitRecoversAfterExpiry()
    {
        Options.MaxLobbies = 1;
        Host();

        Assert.Equal(Failures.ServerFull, Store.CreateLobby(new CreateLobbyRequest(Version, State()), JoinerIp).Failure);

        Advance(Options.LobbyLeaseSeconds);
        Host(from: JoinerIp);
    }

    [Fact]
    public void Options_RejectBadWideningOrder()
    {
        MilanServerOptions options = new() { MapWidenSeconds = 70, EnemyWidenSeconds = 60 };
        MilanServerOptions lateInProgress = new() { InProgressAfterSeconds = 121 };

        Assert.True(new MilanServerOptionsValidator().Validate(null, options).Failed);
        Assert.True(new MilanServerOptionsValidator().Validate(null, lateInProgress).Failed);
        Assert.True(new MilanServerOptionsValidator().Validate(null, new MilanServerOptions()).Succeeded);
    }
}
