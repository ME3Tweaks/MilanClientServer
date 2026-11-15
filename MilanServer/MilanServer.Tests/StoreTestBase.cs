using System.Net;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Time.Testing;

namespace MilanServer.Tests;

public abstract class StoreTestBase
{
    protected const string Version = "milan-1.0.0";
    protected static readonly IPAddress HostIp = IPAddress.Parse("203.0.113.10");
    protected static readonly IPAddress JoinerIp = IPAddress.Parse("198.51.100.20");
    protected static readonly IPAddress OtherJoinerIp = IPAddress.Parse("198.51.100.30");

    protected FakeTimeProvider Time { get; } = new(new DateTimeOffset(2026, 9, 25, 12, 0, 0, TimeSpan.Zero));
    protected MilanServerOptions Options { get; } = new();

    // The live settings (config/milan.json); Live.Change edits them while the store runs.
    protected LiveOptions Live { get; } = new(new MilanLiveOptions());

    MatchmakingStore? _store;
    protected MatchmakingStore Store =>
        _store ??= new MatchmakingStore(Microsoft.Extensions.Options.Options.Create(Options), Time, NullLogger<MatchmakingStore>.Instance,
            new ServerNotice(Live, NullLogger<ServerNotice>.Instance));

    protected static LobbySnapshot State(
        long revision = 1,
        bool isPrivate = false,
        bool joinable = true,
        int map = 5,
        int enemy = 3,
        int difficulty = 2,
        int players = 1,
        int maxPlayers = 4,
        MatchPhase phase = MatchPhase.Lobby,
        int wave = 0,
        int port = 7777,
        string[]? lan = null) =>
        new(revision, isPrivate, joinable, map, enemy, difficulty, players, maxPlayers, phase, wave, port, lan);

    protected CreateLobbyResponse Host(LobbySnapshot? state = null, IPAddress? from = null, string version = Version)
    {
        Outcome<CreateLobbyResponse> outcome = Store.CreateLobby(new CreateLobbyRequest(version, state ?? State()), from ?? HostIp);
        Assert.Null(outcome.Failure);
        return outcome.Value;
    }

    protected LobbyStateResponse Report(CreateLobbyResponse lobby, LobbySnapshot state)
    {
        Outcome<LobbyStateResponse> outcome = Store.ReportLobbyState(lobby.LobbyId, lobby.HostKey, state, HostIp);
        Assert.Null(outcome.Failure);
        return outcome.Value;
    }

    protected CreateSearchResponse StartSearch(
        int map = GameSettingIds.AnyMap,
        int enemy = GameSettingIds.AnyEnemy,
        int difficulty = GameSettingIds.AnyDifficulty,
        bool widen = true,
        IPAddress? from = null,
        string[]? exclude = null)
    {
        Outcome<CreateSearchResponse> outcome = Store.CreateSearch(
            new SearchRequest(Version, map, enemy, difficulty, widen, exclude), from ?? JoinerIp);
        Assert.Null(outcome.Failure);
        return outcome.Value;
    }

    protected SearchStatus Poll(CreateSearchResponse search)
    {
        Outcome<SearchStatus> outcome = Store.PollSearch(search.SearchId, search.SearchKey);
        Assert.Null(outcome.Failure);
        return outcome.Value;
    }

    protected Outcome<JoinReservation> Redeem(CreateLobbyResponse lobby, IPAddress? from = null, string version = Version) =>
        Store.RedeemInvite(new RedeemInviteRequest(lobby.InviteCode, version), from ?? JoinerIp);

    protected void Advance(int seconds) => Time.Advance(TimeSpan.FromSeconds(seconds));
}
