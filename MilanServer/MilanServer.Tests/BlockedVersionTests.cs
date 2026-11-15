namespace MilanServer.Tests;

public sealed class BlockedVersionTests : StoreTestBase
{
    void Block(params string[] versions) => Live.Change(new MilanLiveOptions { BlockedGameVersions = versions });

    [Fact]
    public void BlockedVersion_CannotHostSearchOrRedeemInvites()
    {
        Block(Version);

        Assert.Equal(Failures.VersionBlocked, Store.CreateLobby(new CreateLobbyRequest(Version, State()), HostIp).Failure);
        Assert.Equal(Failures.VersionBlocked, Store.CreateSearch(new SearchRequest(Version), JoinerIp).Failure);
        Assert.Equal(Failures.VersionBlocked, Store.RedeemInvite(new RedeemInviteRequest("ABCD-EFGH", Version), JoinerIp).Failure);
        Assert.Equal(3, Store.GetStats().Details!.Totals.BlockedVersionRequests);
    }

    [Fact]
    public void OtherVersions_AreUnaffected()
    {
        Block("milan-0.9.0");

        CreateLobbyResponse lobby = Host();

        Assert.Equal(lobby.LobbyId, StartSearch().Status.Reservation?.Lobby.LobbyId);
    }

    [Fact]
    public void BlockingWhileRunning_LeavesExistingLobbies_AndUnblockingRestoresMatchmaking()
    {
        CreateLobbyResponse lobby = Host();

        Block(Version);
        Report(lobby, State(2));
        Assert.Equal(Failures.VersionBlocked, Store.CreateSearch(new SearchRequest(Version), JoinerIp).Failure);

        Block();
        Assert.Equal(lobby.LobbyId, StartSearch().Status.Reservation?.Lobby.LobbyId);
    }
}
