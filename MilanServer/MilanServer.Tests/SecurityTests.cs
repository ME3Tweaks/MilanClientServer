using System.Buffers.Binary;
using System.Buffers.Text;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace MilanServer.Tests;

public sealed class SecurityTests : StoreTestBase
{
    [Fact]
    public void SelfReportedJoin_NeverGrantsMembershipOrSurvivesExpiry()
    {
        CreateLobbyResponse lobby = Host(State() with { MemberInvites = true });
        JoinReservation reservation = Redeem(lobby).Value;
        Assert.Null(Store.MarkReservationJoined(reservation.ReservationId, reservation.JoinKey));
        Assert.Equal(Failures.MemberNotFound, Store.GetMemberInvite(reservation.ReservationId, reservation.JoinKey).Failure);
        Assert.Equal(Failures.MemberNotFound, Store.ReportHostLost(reservation.ReservationId, reservation.JoinKey, new HostLostReport()).Failure);
        Report(lobby, State(2, players: 2) with { MemberInvites = true });
        Assert.Equal(Failures.MemberNotFound, Store.GetMemberInvite(reservation.ReservationId, reservation.JoinKey).Failure);
        Advance(Options.ReservationSeconds);
        Store.Sweep();
        Assert.Equal(Failures.MemberNotFound, Store.GetMemberInvite(reservation.ReservationId, reservation.JoinKey).Failure);
    }

    [Fact]
    public void HostRoster_GrantsAndRevokesWithoutTrustingJoinerNotices()
    {
        CreateLobbyResponse lobby = Host(State() with { MemberInvites = true });
        JoinReservation member = Redeem(lobby).Value;
        Report(lobby, State(2, players: 2) with { MemberInvites = true, MemberIds = [member.ReservationId] });
        Assert.Equal(lobby.InviteCode, Store.GetMemberInvite(member.ReservationId, member.JoinKey).Value.InviteCode);
        Report(lobby, State(3) with { MemberInvites = true, MemberIds = [] });
        Assert.Equal(Failures.MemberNotFound, Store.GetMemberInvite(member.ReservationId, member.JoinKey).Failure);
        Assert.Equal(Failures.MemberNotFound, Store.ReportHostLost(member.ReservationId, member.JoinKey, new HostLostReport()).Failure);
        // A revoked ID is no longer a pending reservation and cannot be resurrected.
        Report(lobby, State(4) with { MemberInvites = true, MemberIds = [member.ReservationId] });
        Assert.Equal(Failures.MemberNotFound, Store.GetMemberInvite(member.ReservationId, member.JoinKey).Failure);
    }

    [Fact]
    public void ForeignAndDuplicateRosterIds_CannotGrantMembership()
    {
        CreateLobbyResponse first = Host();
        CreateLobbyResponse other = Host(State() with { MemberInvites = true });
        JoinReservation reservation = Redeem(first).Value;
        Report(other, State(2) with { MemberInvites = true, MemberIds = [reservation.ReservationId] });
        Assert.Equal(Failures.MemberNotFound, Store.GetMemberInvite(reservation.ReservationId, reservation.JoinKey).Failure);
        Assert.Equal("invalid_request", Store.ReportLobbyState(first.LobbyId, first.HostKey,
            State(2) with { MemberIds = [reservation.ReservationId, reservation.ReservationId] }, HostIp).Failure?.Code);
    }

    [Fact]
    public async Task HostConfirmation_CanPrecedeTheJoinerLoadingItsMap()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation joiner = Redeem(lobby).Value;
        long version = (await Store.PollReservationAsync(joiner.ReservationId, joiner.JoinKey, -1, CancellationToken.None)).Value.Version;
        Task<Outcome<ReservationStatus>> poll = Store.PollReservationAsync(joiner.ReservationId, joiner.JoinKey, version, CancellationToken.None);
        Assert.False(poll.IsCompleted);
        Report(lobby, State(2, players: 2) with { MemberIds = [joiner.ReservationId] });
        ReservationStatus confirmed = (await poll.WaitAsync(TimeSpan.FromSeconds(5), TestContext.Current.CancellationToken)).Value;
        Assert.True(confirmed.Joined);
        Assert.Null(Store.MarkReservationJoined(joiner.ReservationId, joiner.JoinKey));
        Assert.Null(Store.ReportJoinerCandidates(joiner.ReservationId, joiner.JoinKey, new JoinerCandidatesRequest(7777)));
        Report(lobby, State(3) with { MemberIds = [] });
        Assert.Equal(Failures.ReservationNotFound, (await Store.PollReservationAsync(joiner.ReservationId, joiner.JoinKey, -1, CancellationToken.None)).Failure);
    }

    [Fact]
    public async Task DuplicatePoll_IsRejectedAndCancellationReleasesTheCredential()
    {
        CreateLobbyResponse lobby = Host();
        long version = (await Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, -1, CancellationToken.None)).Value.Version;
        using CancellationTokenSource cancel = new();
        Task<Outcome<IntroductionList>> first = Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, version, cancel.Token);
        Assert.False(first.IsCompleted);
        Assert.Equal(Failures.PollBusy, (await Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, version, CancellationToken.None)).Failure);
        cancel.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () => await first);
        using CancellationTokenSource secondCancel = new();
        Task<Outcome<IntroductionList>> second = Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, version, secondCancel.Token);
        Assert.False(second.IsCompleted);
        secondCancel.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () => await second);
    }

    [Fact]
    public async Task ReservationAndMemberPolls_RejectDuplicates()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation member = Redeem(lobby).Value;
        long version = (await Store.PollReservationAsync(member.ReservationId, member.JoinKey, -1, CancellationToken.None)).Value.Version;
        using CancellationTokenSource cancel = new();
        Task<Outcome<ReservationStatus>> reservationPoll = Store.PollReservationAsync(member.ReservationId, member.JoinKey, version, cancel.Token);
        Assert.Equal(Failures.PollBusy, (await Store.PollReservationAsync(member.ReservationId, member.JoinKey, version, CancellationToken.None)).Failure);
        cancel.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () => await reservationPoll);
        Report(lobby, State(2, players: 2) with { MemberIds = [member.ReservationId] });
        MigrationStatus initial = (await Store.PollMemberAsync(member.ReservationId, member.JoinKey, -1, CancellationToken.None)).Value;
        using CancellationTokenSource memberCancel = new();
        Task<Outcome<MigrationStatus>> memberPoll = Store.PollMemberAsync(member.ReservationId, member.JoinKey, initial.Version, memberCancel.Token);
        Assert.Equal(Failures.PollBusy, (await Store.PollMemberAsync(member.ReservationId, member.JoinKey, initial.Version, CancellationToken.None)).Failure);
        memberCancel.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () => await memberPoll);
    }

    [Fact]
    public void RequestBudget_BoundsSourcesAndRecoversAfterCompletion()
    {
        RequestBudget budget = new(Microsoft.Extensions.Options.Options.Create(new MilanServerOptions
        { MaxConcurrentRequests = 2, MaxConcurrentRequestsPerAddress = 1 }));
        Assert.True(budget.TryEnter("a"));
        Assert.False(budget.TryEnter("a"));
        Assert.True(budget.TryEnter("b"));
        Assert.False(budget.TryEnter("c"));
        budget.Exit("a");
        Assert.True(budget.TryEnter("c"));
        budget.Exit("b");
        budget.Exit("c");
        for (int i = 0; i < 1000; i++)
        {
            Assert.True(budget.TryEnter(i.ToString()));
            budget.Exit(i.ToString());
        }
    }

    [Fact]
    public void UdpBudgets_IncludeBadTagsAndRecoverWithoutStarvingAnotherAddress()
    {
        Options.DiscoveryPacketsPerSecond = 3;
        Options.DiscoveryPacketsPerSecondPerAddress = 2;
        CreateLobbyResponse lobby = Host();
        UInt128 id = BinaryPrimitives.ReadUInt128BigEndian(Base64Url.DecodeFromChars(lobby.UdpTicket.TicketId));
        byte[] secret = Base64Url.DecodeFromChars(lobby.UdpTicket.Secret);
        byte[] packet = new byte[DiscoveryPacket.RequestLength];
        byte[] response = new byte[DiscoveryPacket.ResponseLength];
        DiscoveryPacket.WriteRequest(packet, id, 1, secret);
        packet[^1] ^= 1;
        for (int i = 0; i < 2; i++)
            Assert.Equal(0, Store.HandleDiscovery(packet, new IPEndPoint(HostIp, 4000), response));
        DiscoveryPacket.WriteRequest(packet, id, 1, secret);
        Assert.Equal(0, Store.HandleDiscovery(packet, new IPEndPoint(HostIp, 4000), response));
        Assert.Equal(DiscoveryPacket.ResponseLength, Store.HandleDiscovery(packet, new IPEndPoint(JoinerIp, 4000), response));
        Advance(1);
        DiscoveryPacket.WriteRequest(packet, id, 2, secret);
        Assert.Equal(DiscoveryPacket.ResponseLength, Store.HandleDiscovery(packet, new IPEndPoint(HostIp, 4000), response));
    }

    [Fact]
    public void UdpGlobalBudget_CannotBeBypassedWithNewSourceAddresses()
    {
        Options.DiscoveryPacketsPerSecond = 2;
        CreateLobbyResponse lobby = Host();
        byte[] unknown = new byte[DiscoveryPacket.RequestLength];
        byte[] response = new byte[DiscoveryPacket.ResponseLength];
        DiscoveryPacket.WriteRequest(unknown, 123, 1, new byte[32]);
        Store.HandleDiscovery(unknown, new IPEndPoint(HostIp, 4000), response);
        Store.HandleDiscovery(unknown, new IPEndPoint(JoinerIp, 4000), response);
        byte[] valid = new byte[DiscoveryPacket.RequestLength];
        DiscoveryPacket.WriteRequest(valid, BinaryPrimitives.ReadUInt128BigEndian(Base64Url.DecodeFromChars(lobby.UdpTicket.TicketId)),
            1, Base64Url.DecodeFromChars(lobby.UdpTicket.Secret));
        Assert.Equal(0, Store.HandleDiscovery(valid, new IPEndPoint(OtherJoinerIp, 4000), response));
        Advance(1);
        Assert.Equal(DiscoveryPacket.ResponseLength, Store.HandleDiscovery(valid, new IPEndPoint(OtherJoinerIp, 4000), response));
    }

    [Fact]
    public void LanCandidates_SupportPrivateLanPartiesButRejectInfrastructureTargets()
    {
        CreateLobbyResponse lobby = Host(State(lan: ["192.168.1.20"]), IPAddress.Parse("192.168.1.20"));
        JoinReservation reservation = Redeem(lobby, IPAddress.Parse("192.168.1.30")).Value;
        Assert.Contains(reservation.HostCandidates, c => c.Kind == CandidateKind.Lan);
        Assert.Equal("invalid_request", Store.ReportJoinerCandidates(reservation.ReservationId, reservation.JoinKey,
            new JoinerCandidatesRequest(7777, ["169.254.169.254"]))?.Code);
        Assert.Equal("invalid_request", Store.CreateLobby(new CreateLobbyRequest(Version, State(lan: ["169.254.3.3"])), HostIp).Failure?.Code);
    }

    [Fact]
    public void ModVersion_IsAnExactMatchForSearchAndInvites()
    {
        CreateLobbyResponse lobby = Host(version: "project-milan-1.2.3");
        Outcome<CreateSearchResponse> search = Store.CreateSearch(new SearchRequest("project-milan-1.2.4"), JoinerIp);
        Assert.Equal(SearchState.Searching, search.Value.Status.State);
        Assert.Equal(Failures.VersionMismatch, Redeem(lobby, version: "project-milan-1.2.4").Failure);
        Assert.Equal(SearchState.Matched, Store.CreateSearch(new SearchRequest("project-milan-1.2.3"), OtherJoinerIp).Value.Status.State);
    }

    [Fact]
    public void Exclusions_CannotRetainLargeAttackerControlledStrings()
    {
        Assert.Equal("invalid_request", Store.CreateSearch(new SearchRequest(Version,
            ExcludeLobbyIds: [new string('a', 15000)]), JoinerIp).Failure?.Code);
        Assert.Equal("invalid_request", Store.CreateSearch(new SearchRequest(Version,
            ExcludeLobbyIds: [null!]), JoinerIp).Failure?.Code);
    }
}

public sealed class TransportSecurityTests : IClassFixture<MilanFactory>
{
    readonly MilanFactory _factory;
    public TransportSecurityTests(MilanFactory factory) => _factory = factory;

    [Fact]
    public async Task PasswordRequiresHttps_PasswordlessHttpRemainsAvailable()
    {
        using WebApplicationFactory<Program> keyed = _factory.WithWebHostBuilder(b => b.UseSetting("Milan:AccessKey", "party-password"));
        using HttpClient http = keyed.CreateClient();
        http.DefaultRequestHeaders.Add(Api.AccessKeyHeader, "party-password");
        Assert.Equal(HttpStatusCode.UpgradeRequired, (await http.GetAsync("/v1/info", TestContext.Current.CancellationToken)).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await http.GetAsync("/healthz", TestContext.Current.CancellationToken)).StatusCode);
        using HttpClient open = _factory.CreateClient();
        Assert.Equal(HttpStatusCode.OK, (await open.GetAsync("/v1/info", TestContext.Current.CancellationToken)).StatusCode);
    }

    [Fact]
    public async Task TotalArrivalBudget_AppliesAcrossSourceAddresses()
    {
        using WebApplicationFactory<Program> limited = _factory.WithWebHostBuilder(b => b
            .UseSetting("Milan:RequestBurst", "2").UseSetting("Milan:TotalRequestsPerSecond", "1"));
        _ = limited.CreateClient();
        for (int i = 0; i < 3; i++)
        {
            var response = await limited.Server.SendAsync(context =>
            {
                context.Connection.RemoteIpAddress = IPAddress.Parse($"203.0.113.{i + 1}");
                context.Request.Method = "GET";
                context.Request.Path = "/v1/info";
            }, TestContext.Current.CancellationToken);
            Assert.Equal(i < 2 ? 200 : 429, response.Response.StatusCode);
        }
    }

    [Theory]
    [InlineData("127.0.0.1", null, 200)]
    [InlineData("203.0.113.40", null, 426)]
    [InlineData("172.18.0.1", "172.18.0.1", 200)]
    [InlineData("::ffff:172.18.0.1", "172.16.0.0/12", 200)]
    [InlineData("172.18.0.1", "172.17.0.0/16", 426)]
    public async Task OnlyTrustedProxies_CanAssertHttps(string source, string? trusted, int expected)
    {
        using WebApplicationFactory<Program> keyed = _factory.WithWebHostBuilder(b =>
        {
            b.UseSetting("Milan:AccessKey", "party-password");
            if (trusted is not null)
                b.UseSetting("Milan:TrustedProxies:0", trusted);
        });
        _ = keyed.CreateClient();
        var response = await SendForwarded(keyed, source);
        Assert.Equal(expected, response.Response.StatusCode);
    }

    [Fact]
    public async Task UntrustedForwarder_IsLoggedOnceWithTheSettingThatTrustsIt()
    {
        CapturedWarnings logs = new();
        using WebApplicationFactory<Program> logged = _factory.WithWebHostBuilder(b => b
            .UseSetting("Milan:TrustedProxies:0", "10.0.0.0/8").ConfigureLogging(l => l.AddProvider(logs)));
        _ = logged.CreateClient();
        foreach (string source in new[] { "127.0.0.1", "10.1.2.3", "::ffff:172.18.0.1", "172.18.0.1" })
            await SendForwarded(logged, source);
        string warning = Assert.Single(logs.Messages, m => m.Contains("TrustedProxies"));
        Assert.Contains("Milan__TrustedProxies__0=172.18.0.1 ", warning);
    }

    [Theory]
    [InlineData("172.18.0.1", true)]
    [InlineData("172.16.0.0/12", true)]
    [InlineData("2001:db8::/32", true)]
    [InlineData("caddy", false)]
    [InlineData("10.0.0.0/33", false)]
    public void TrustedProxies_AcceptAddressesAndRanges(string entry, bool valid) =>
        Assert.Equal(valid, new MilanServerOptionsValidator().Validate(null, new MilanServerOptions { TrustedProxies = [entry] }).Succeeded);

    // A plain HTTP request as a TLS proxy forwards it, arriving from source.
    static Task<Microsoft.AspNetCore.Http.HttpContext> SendForwarded(WebApplicationFactory<Program> app, string source) =>
        app.Server.SendAsync(context =>
        {
            context.Connection.RemoteIpAddress = IPAddress.Parse(source);
            context.Request.Scheme = "http";
            context.Request.Method = "GET";
            context.Request.Path = "/v1/info";
            context.Request.Headers[Api.AccessKeyHeader] = "party-password";
            context.Request.Headers["X-Forwarded-Proto"] = "https";
            context.Request.Headers["X-Forwarded-For"] = "198.51.100.5";
        }, TestContext.Current.CancellationToken);

    sealed class CapturedWarnings : ILoggerProvider, ILogger
    {
        public ConcurrentQueue<string> Messages { get; } = new();
        public ILogger CreateLogger(string categoryName) => this;
        public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;
        public bool IsEnabled(LogLevel logLevel) => logLevel >= LogLevel.Warning;
        public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
        {
            if (IsEnabled(logLevel))
                Messages.Enqueue(formatter(state, exception));
        }
        public void Dispose() { }
    }

    [Fact]
    public async Task StatsToken_RequiresHttpsEvenOnAPasswordlessServer()
    {
        using WebApplicationFactory<Program> stats = _factory.WithWebHostBuilder(b => b
            .UseSetting("Milan:Stats:Public", "Summary").UseSetting("Milan:Stats:Token", "0123456789abcdef"));
        using HttpClient client = stats.CreateClient();
        Assert.Equal(HttpStatusCode.OK, (await client.GetAsync("/v1/stats", TestContext.Current.CancellationToken)).StatusCode);
        client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", "0123456789abcdef");
        Assert.Equal(HttpStatusCode.UpgradeRequired, (await client.GetAsync("/v1/stats", TestContext.Current.CancellationToken)).StatusCode);
    }

    [Fact]
    public async Task ActiveHttpRequests_AreBoundedAcrossAddressesAndReleaseOnCancellation()
    {
        using WebApplicationFactory<Program> limited = _factory.WithWebHostBuilder(b => b.UseSetting("Milan:MaxConcurrentRequests", "1"));
        using HttpClient client = limited.CreateClient();
        LobbySnapshot state = new(1, false, true, 5, 3, 2, 1, 4, MatchPhase.Lobby, 0, 7777);
        using var created = await client.PostAsJsonAsync("/v1/lobbies", new CreateLobbyRequest("security-test", state), TestContext.Current.CancellationToken);
        Assert.Equal("no-store", created.Headers.CacheControl?.ToString());
        CreateLobbyResponse lobby = (await created.Content.ReadFromJsonAsync<CreateLobbyResponse>(TestContext.Current.CancellationToken))!;
        using CancellationTokenSource cancel = new();
        Task<Microsoft.AspNetCore.Http.HttpContext> waiting = limited.Server.SendAsync(context =>
        {
            context.Connection.RemoteIpAddress = IPAddress.Parse("203.0.113.1");
            context.Request.Method = "POST";
            context.Request.Path = $"/v1/lobbies/{lobby.LobbyId}/introductions/poll";
            context.Request.QueryString = new("?after=0");
            context.Request.Headers.Authorization = "Bearer " + lobby.HostKey;
        }, cancel.Token);
        await Task.Delay(50, TestContext.Current.CancellationToken);
        var rejected = await limited.Server.SendAsync(context =>
        {
            context.Connection.RemoteIpAddress = IPAddress.Parse("198.51.100.2");
            context.Request.Method = "GET";
            context.Request.Path = "/v1/info";
        }, TestContext.Current.CancellationToken);
        Assert.Equal(429, rejected.Response.StatusCode);
        Assert.False(waiting.IsCompleted);
        cancel.Cancel();
        try { await waiting; }
        catch (OperationCanceledException) { }
        // Cancellation reaches the request handler asynchronously.
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        while (true)
        {
            using var available = await client.GetAsync("/v1/info", timeout.Token);
            if (available.StatusCode == HttpStatusCode.OK)
                break;
            await Task.Delay(20, timeout.Token);
        }
    }

    [Fact]
    public async Task PollPressure_LeavesCapacityForControlRequests()
    {
        using WebApplicationFactory<Program> limited = _factory.WithWebHostBuilder(b => b
            .UseSetting("Milan:MaxConcurrentRequests", "2").UseSetting("Milan:MaxConcurrentLongPolls", "1"));
        using HttpClient client = limited.CreateClient();
        LobbySnapshot state = new(1, false, true, 5, 3, 2, 1, 4, MatchPhase.Lobby, 0, 7777);
        async Task<CreateLobbyResponse> Create() => (await (await client.PostAsJsonAsync("/v1/lobbies",
            new CreateLobbyRequest("poll-test", state), TestContext.Current.CancellationToken))
            .Content.ReadFromJsonAsync<CreateLobbyResponse>(TestContext.Current.CancellationToken))!;
        CreateLobbyResponse first = await Create();
        CreateLobbyResponse second = await Create();
        using CancellationTokenSource cancel = new();
        Task<Microsoft.AspNetCore.Http.HttpContext> waiting = limited.Server.SendAsync(context =>
        {
            context.Connection.RemoteIpAddress = IPAddress.Parse("203.0.113.1");
            context.Request.Method = "POST";
            context.Request.Path = $"/v1/lobbies/{first.LobbyId}/introductions/poll";
            context.Request.QueryString = new("?after=0");
            context.Request.Headers.Authorization = "Bearer " + first.HostKey;
        }, cancel.Token);
        await Task.Delay(50, TestContext.Current.CancellationToken);
        var denied = await limited.Server.SendAsync(context =>
        {
            context.Connection.RemoteIpAddress = IPAddress.Parse("198.51.100.2");
            context.Request.Method = "POST";
            context.Request.Path = $"/v1/lobbies/{second.LobbyId}/introductions/poll";
            context.Request.QueryString = new("?after=0");
            context.Request.Headers.Authorization = "Bearer " + second.HostKey;
        }, TestContext.Current.CancellationToken);
        Assert.Equal(429, denied.Response.StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await client.GetAsync("/v1/info", TestContext.Current.CancellationToken)).StatusCode);
        cancel.Cancel();
        try { await waiting; } catch (OperationCanceledException) { }
    }
}
