using System.Net;

namespace MilanServer.Tests;

public sealed class StatsTests : StoreTestBase
{
    static IPAddress Address(int last) => IPAddress.Parse($"203.0.113.{last}");

    [Fact]
    public void Stats_CountLobbiesPlayersAndSearches()
    {
        CreateLobbyResponse open = Host(State(players: 2), Address(11));
        Host(State(players: 4), Address(12));
        Host(State(isPrivate: true, players: 3), Address(13));
        Host(State(isPrivate: true, players: 1, phase: MatchPhase.InProgress) with { MemberInvites = true }, Address(14));
        // No lobby has this difficulty, so the search stays searching.
        StartSearch(difficulty: 1);
        // A join on its way in still leaves the open lobby a free slot.
        Assert.Null(Redeem(open).Failure);

        ServerStats stats = Store.GetStats();

        Assert.Equal(10, stats.Players);
        Assert.Equal(1, stats.Searching);
        Assert.Equal(2, stats.PublicLobbies);
        Assert.Equal(1, stats.OpenLobbies);
        Assert.Equal(0, stats.OpenExpandedLobbies);
        Assert.Null(stats.Idle);
        Assert.Null(stats.Online);

        StatsDetails details = stats.Details!;
        Assert.Equal(4, details.Lobbies);
        Assert.Equal(1, details.FriendsLobbies);
        Assert.Equal(1, details.PrivateLobbies);
        Assert.Equal(3, details.LobbiesWaiting);
        Assert.Equal(1, details.LobbiesInProgress);
        Assert.Equal(16, details.PlayerCapacity);
        Assert.Equal(1, details.PendingJoins);
        Assert.Equal(10, details.PeakPlayers);
        Assert.Equal(4, details.PeakLobbies);
        Assert.Equal(new Dictionary<string, int> { [Version] = 4 }, details.GameVersions);
        StatsTotals expected = new()
        {
            LobbiesCreated = 4,
            LobbiesExpired = 0,
            SearchesStarted = 1,
            SearchesMatched = 0,
            InvitesRedeemed = 1,
            JoinsCompleted = 0,
            HostMigrationsStarted = 0,
            HostMigrationsCompleted = 0,
            HostMigrationsFailed = 0,
        };
        Assert.Equal(expected, details.Totals);
    }

    [Fact]
    public void Stats_CountLobbiesByPlayers()
    {
        Host(State(players: 4), Address(11));
        Host(State(players: 2), Address(12));
        Host(State(isPrivate: true, players: 4, phase: MatchPhase.InProgress), Address(13));

        IReadOnlyDictionary<int, int> sizes = Store.GetStats().Details!.LobbiesByPlayers!;

        Assert.Equal([KeyValuePair.Create(2, 1), KeyValuePair.Create(4, 2)], sizes.ToArray());
    }

    [Fact]
    public void Stats_SplitOpenLobbiesBySize()
    {
        Host(State(players: 2), Address(11));
        Host(State(players: 3, maxPlayers: 8), Address(12));
        Host(State(players: 5, maxPlayers: 6, phase: MatchPhase.InProgress, wave: 2), Address(13));
        // Full or private: open to no search.
        Host(State(players: 4), Address(14));
        Host(State(players: 8, maxPlayers: 8), Address(15));
        Host(State(isPrivate: true, players: 1, maxPlayers: 8), Address(16));

        ServerStats stats = Store.GetStats();

        Assert.Equal(1, stats.OpenLobbies);
        Assert.Equal(2, stats.OpenExpandedLobbies);
    }

    [Fact]
    public void Stats_PublicFillBySize()
    {
        Host(State(players: 4), Address(11));
        Host(State(players: 3, phase: MatchPhase.InProgress, wave: 2), Address(12));
        Host(State(players: 3, maxPlayers: 8), Address(13));
        // Private lobbies are left out.
        Host(State(isPrivate: true, players: 1, maxPlayers: 8), Address(14));

        IReadOnlyDictionary<int, LobbySizeFill> fill = Store.GetStats().Details!.PublicFillBySize!;

        Assert.Equal([4, 8], fill.Keys.ToArray());
        Assert.Equal(new LobbySizeFill(2, 7, 8, 87.5), fill[4]);
        Assert.Equal(new LobbySizeFill(1, 3, 8, 37.5), fill[8]);
    }

    [Fact]
    public void Stats_AreCachedForCacheSeconds()
    {
        Host(State(players: 2), Address(11));
        ServerStats first = Store.GetStats();

        Host(State(players: 3), Address(12));
        Assert.Same(first, Store.GetStats());

        Advance(Options.Stats.CacheSeconds);
        Assert.Equal(5, Store.GetStats().Players);
    }

    [Fact]
    public void Stats_DropExpiredLobbies_AndKeepThePeak()
    {
        Host(State(players: 3), Address(11));
        Store.Sweep();

        Advance(Options.LobbyLeaseSeconds + 1);
        Store.Sweep();
        ServerStats stats = Store.GetStats();

        Assert.Equal(0, stats.Players);
        Assert.Equal(0, stats.Details!.Lobbies);
        Assert.Equal(3, stats.Details.PeakPlayers);
        Assert.Equal(1, stats.Details.PeakLobbies);
        Assert.Equal(1, stats.Details.Totals.LobbiesExpired);
        Assert.Equal(Options.LobbyLeaseSeconds + 1, stats.Details.UptimeSeconds);
    }

    [Fact]
    public void Totals_CountCompletedJoins()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation reservation = Redeem(lobby).Value;
        Assert.Null(Store.MarkReservationJoined(reservation.ReservationId, reservation.JoinKey));
        // Reporting twice is one join.
        Assert.Null(Store.MarkReservationJoined(reservation.ReservationId, reservation.JoinKey));

        Report(lobby, State(2, players: 2) with { MemberIds = [reservation.ReservationId] });
        StatsTotals totals = Store.GetStats().Details!.Totals;

        Assert.Equal(1, totals.InvitesRedeemed);
        Assert.Equal(1, totals.JoinsCompleted);
    }

    [Fact]
    public void Presence_IsRefusedUnlessEnabled()
    {
        Assert.Equal(Failures.PresenceDisabled, Store.ReportPresence("0123456789abcdef", JoinerIp));
    }

    [Fact]
    public void Presence_CountsIdleGamesUntilTheyStopReporting()
    {
        Options.Stats.Presence = true;
        Options.Stats.CacheSeconds = 0;
        Host(State(players: 2), Address(11));

        Assert.Null(Store.ReportPresence("0123456789abcdef", JoinerIp));
        Assert.Null(Store.ReportPresence("0123456789abcdef", JoinerIp));
        Assert.Null(Store.ReportPresence("fedcba9876543210", OtherJoinerIp));
        Assert.Equal(400, Store.ReportPresence("short", JoinerIp)?.StatusCode);
        Assert.Equal(400, Store.ReportPresence("0123456789abcde/", JoinerIp)?.StatusCode);

        ServerStats stats = Store.GetStats();
        Assert.Equal(2, stats.Idle);
        Assert.Equal(4, stats.Online);

        // One keeps reporting; the other is dropped after two missed intervals.
        Advance(Options.Stats.PresenceSeconds);
        Assert.Null(Store.ReportPresence("0123456789abcdef", JoinerIp));
        Advance(Options.Stats.PresenceSeconds + 31);
        Store.Sweep();
        Assert.Equal(1, Store.GetStats().Idle);

        // A game entering a lobby or a search withdraws its report.
        Store.ClearPresence("0123456789abcdef");
        Assert.Equal(0, Store.GetStats().Idle);
    }

    [Fact]
    public void Presence_IsCappedPerAddressAndOverall()
    {
        Options.Stats.Presence = true;
        Options.Stats.CacheSeconds = 0;
        Options.Stats.MaxPresencePerAddress = 2;
        Options.Stats.MaxPresence = 3;

        for (int i = 0; i < 5; i++)
        {
            Assert.Null(Store.ReportPresence($"same-address-id-{i:D2}", JoinerIp));
        }

        Assert.Equal(2, Store.GetStats().Idle);

        Assert.Null(Store.ReportPresence("other-address-id-0", OtherJoinerIp));
        Assert.Null(Store.ReportPresence("other-address-id-1", OtherJoinerIp));
        Assert.Equal(3, Store.GetStats().Idle);

        // A withdrawn report frees its place.
        Store.ClearPresence("same-address-id-00");
        Assert.Null(Store.ReportPresence("same-address-id-09", JoinerIp));
        Assert.Equal(3, Store.GetStats().Idle);
    }
}
