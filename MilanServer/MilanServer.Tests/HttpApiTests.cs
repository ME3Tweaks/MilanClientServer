using System.Buffers.Binary;
using System.Buffers.Text;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Sockets;
using System.Net.Http.Json;
using System.Text;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Extensions.DependencyInjection;

namespace MilanServer.Tests;

// Binds UDP discovery to an ephemeral port so tests never collide with a running server.
// Drops the reflection fallback from the server's JSON options, as trimming does in the
// published server, so a body missing from MilanJsonContext fails here.
public sealed class MilanFactory : WebApplicationFactory<Program>
{
    protected override void ConfigureWebHost(IWebHostBuilder builder)
    {
        builder.UseSetting("Milan:UdpPort", "0");
        builder.ConfigureTestServices(services =>
            services.ConfigureHttpJsonOptions(json => json.SerializerOptions.TypeInfoResolver = MilanJsonContext.Default));
    }
}

public sealed class HttpApiTests : IClassFixture<MilanFactory>
{
    const string Version = "milan-1.0.0";
    readonly WebApplicationFactory<Program> _factory;

    public HttpApiTests(MilanFactory factory)
    {
        _factory = factory;
    }

    static CancellationToken Ct => TestContext.Current.CancellationToken;

    static LobbySnapshot State(long revision = 1, bool isPrivate = false) =>
        new(revision, isPrivate, true, 5, 3, 2, 1, 4, MatchPhase.Lobby, 0, 7777, ["192.168.1.20"]);

    static HttpRequestMessage Authorized(HttpMethod method, string uri, string key, object? body = null)
    {
        HttpRequestMessage request = new(method, uri);
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", key);
        if (body is not null)
            request.Content = JsonContent.Create(body);
        return request;
    }

    [Fact]
    public async Task HostSearchAndJoin_EndToEnd()
    {
        HttpClient client = _factory.CreateClient();

        HttpResponseMessage created = await client.PostAsJsonAsync("/v1/lobbies", new CreateLobbyRequest(Version, State(isPrivate: true)), Ct);
        Assert.Equal(HttpStatusCode.Created, created.StatusCode);
        CreateLobbyResponse lobby = (await created.Content.ReadFromJsonAsync<CreateLobbyResponse>(Ct))!;

        // Private: not found by search until the host reports it public.
        HttpResponseMessage searchCreated = await client.PostAsJsonAsync("/v1/searches",
            new SearchRequest(Version, ExcludeLobbyIds: null), Ct);
        CreateSearchResponse search = (await searchCreated.Content.ReadFromJsonAsync<CreateSearchResponse>(Ct))!;
        Assert.Equal(SearchState.Searching, search.Status.State);

        HttpResponseMessage unauthorized = await client.PutAsJsonAsync($"/v1/lobbies/{lobby.LobbyId}/state", State(2), Ct);
        Assert.Equal(HttpStatusCode.NotFound, unauthorized.StatusCode);
        Assert.Equal("lobby_not_found", (await unauthorized.Content.ReadFromJsonAsync<ServerError>(Ct))?.Code);

        HttpResponseMessage reported = await client.SendAsync(
            Authorized(HttpMethod.Put, $"/v1/lobbies/{lobby.LobbyId}/state", lobby.HostKey, State(2)), Ct);
        Assert.Equal(HttpStatusCode.OK, reported.StatusCode);
        Assert.Equal(2, (await reported.Content.ReadFromJsonAsync<LobbyStateResponse>(Ct))?.Revision);

        HttpResponseMessage polled = await client.SendAsync(
            Authorized(HttpMethod.Post, $"/v1/searches/{search.SearchId}/poll", search.SearchKey), Ct);
        SearchStatus status = (await polled.Content.ReadFromJsonAsync<SearchStatus>(Ct))!;
        Assert.Equal(SearchState.Matched, status.State);
        Assert.Equal(lobby.LobbyId, status.Reservation?.Lobby.LobbyId);
        Assert.DoesNotContain(status.Reservation!.HostCandidates, c => c.Kind == CandidateKind.Lan);

        HttpResponseMessage candidates = await client.SendAsync(Authorized(HttpMethod.Put,
            $"/v1/reservations/{status.Reservation.ReservationId}/candidates", status.Reservation.JoinKey,
            new JoinerCandidatesRequest(7778, ["192.168.1.30"])), Ct);
        Assert.Equal(HttpStatusCode.NoContent, candidates.StatusCode);

        HttpResponseMessage reservation = await client.SendAsync(
            Authorized(HttpMethod.Post, $"/v1/reservations/{status.Reservation.ReservationId}/poll", status.Reservation.JoinKey), Ct);
        Assert.False((await reservation.Content.ReadFromJsonAsync<ReservationStatus>(Ct))?.Joined);

        HttpResponseMessage joined = await client.SendAsync(
            Authorized(HttpMethod.Post, $"/v1/reservations/{status.Reservation.ReservationId}/joined", status.Reservation.JoinKey), Ct);
        Assert.Equal(HttpStatusCode.NoContent, joined.StatusCode);

        HttpResponseMessage rotated = await client.SendAsync(
            Authorized(HttpMethod.Post, $"/v1/lobbies/{lobby.LobbyId}/invite", lobby.HostKey), Ct);
        string inviteCode = (await rotated.Content.ReadFromJsonAsync<InviteResponse>(Ct))!.InviteCode;
        Assert.NotEqual(lobby.InviteCode, inviteCode);

        HttpResponseMessage redeemed = await client.PostAsJsonAsync("/v1/invites/redeem",
            new RedeemInviteRequest(inviteCode, Version), Ct);
        Assert.Equal(HttpStatusCode.OK, redeemed.StatusCode);

        HttpResponseMessage deleted = await client.SendAsync(
            Authorized(HttpMethod.Delete, $"/v1/lobbies/{lobby.LobbyId}", lobby.HostKey), Ct);
        Assert.Equal(HttpStatusCode.NoContent, deleted.StatusCode);
    }

    [Fact]
    public async Task MemberRoutes_ReportPollAndLeave()
    {
        HttpClient client = _factory.CreateClient();
        CreateLobbyResponse lobby = (await (await client.PostAsJsonAsync("/v1/lobbies",
            new CreateLobbyRequest(Version, State()), Ct)).Content.ReadFromJsonAsync<CreateLobbyResponse>(Ct))!;
        JoinReservation member = (await (await client.PostAsJsonAsync("/v1/invites/redeem",
            new RedeemInviteRequest(lobby.InviteCode, Version), Ct)).Content.ReadFromJsonAsync<JoinReservation>(Ct))!;
        await client.SendAsync(Authorized(HttpMethod.Post, $"/v1/reservations/{member.ReservationId}/joined", member.JoinKey), Ct);
        await client.SendAsync(Authorized(HttpMethod.Put, $"/v1/lobbies/{lobby.LobbyId}/state", lobby.HostKey,
            State(2) with { PlayerCount = 2, MemberIds = [member.ReservationId] }), Ct);

        HttpResponseMessage reported = await client.SendAsync(Authorized(HttpMethod.Post,
            $"/v1/members/{member.ReservationId}/host-lost", member.JoinKey, new HostLostReport(CanHost: true)), Ct);
        Assert.Equal(HttpStatusCode.OK, reported.StatusCode);
        Assert.Contains("\"state\":\"Checking\"", await reported.Content.ReadAsStringAsync(Ct));

        // The host answering ends the check, so the poll reports the host alive at once.
        await client.SendAsync(Authorized(HttpMethod.Put, $"/v1/lobbies/{lobby.LobbyId}/state", lobby.HostKey, State(3)), Ct);
        HttpResponseMessage polled = await client.SendAsync(Authorized(HttpMethod.Post,
            $"/v1/members/{member.ReservationId}/poll", member.JoinKey), Ct);
        Assert.Equal(MigrationState.HostAlive, (await polled.Content.ReadFromJsonAsync<MigrationStatus>(Ct))?.State);

        HttpResponseMessage left = await client.SendAsync(Authorized(HttpMethod.Delete, $"/v1/members/{member.ReservationId}", member.JoinKey), Ct);
        Assert.Equal(HttpStatusCode.NoContent, left.StatusCode);
        HttpResponseMessage gone = await client.SendAsync(Authorized(HttpMethod.Delete, $"/v1/members/{member.ReservationId}", member.JoinKey), Ct);
        Assert.Equal("member_not_found", (await gone.Content.ReadFromJsonAsync<ServerError>(Ct))?.Code);
    }

    [Fact]
    public async Task Json_UsesCamelCaseAndStringEnums()
    {
        HttpClient client = _factory.CreateClient();

        HttpResponseMessage created = await client.PostAsJsonAsync("/v1/lobbies", new CreateLobbyRequest(Version, State()), Ct);
        CreateLobbyResponse lobby = (await created.Content.ReadFromJsonAsync<CreateLobbyResponse>(Ct))!;
        HttpResponseMessage redeemed = await client.PostAsJsonAsync("/v1/invites/redeem",
            new RedeemInviteRequest(lobby.InviteCode, Version), Ct);
        string json = await redeemed.Content.ReadAsStringAsync(Ct);

        Assert.Contains("\"hostCandidates\":", json);
        Assert.Contains("\"phase\":\"Lobby\"", json);
        Assert.DoesNotContain("\"kind\":\"Lan\"", json);
    }

    [Theory]
    [InlineData("{not json")]
    [InlineData("{\"gameVersion\":\"milan-1.0.0\"}")]
    [InlineData("{\"gameVersion\":null,\"state\":null}")]
    public async Task MalformedBody_ReturnsServerError(string body)
    {
        HttpClient client = _factory.CreateClient();

        HttpResponseMessage response = await client.PostAsync("/v1/lobbies",
            new StringContent(body, Encoding.UTF8, "application/json"), Ct);

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.Equal("invalid_request", (await response.Content.ReadFromJsonAsync<ServerError>(Ct))?.Code);
    }

    [Fact]
    public async Task Info_ReportsProtocolAndTimings()
    {
        ServerInfo info = (await _factory.CreateClient().GetFromJsonAsync<ServerInfo>("/v1/info", Ct))!;

        Assert.Equal(new ServerInfo(Api.ProtocolVersion, 20, 60, 30, 120, 16, 25, info.UdpPort), info);
        Assert.NotEqual(0, info.UdpPort);
    }

    [Fact]
    public async Task Stats_AreOffUntilConfigured()
    {
        HttpClient client = _factory.CreateClient();

        HttpResponseMessage response = await client.GetAsync("/v1/stats", Ct);
        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
        Assert.Equal("stats_unavailable", (await response.Content.ReadFromJsonAsync<ServerError>(Ct))?.Code);

        // No token is configured, so none works.
        using HttpRequestMessage withToken = new(HttpMethod.Get, "https://localhost/v1/stats");
        withToken.Headers.Authorization = new("Bearer", "0123456789abcdef");
        Assert.Equal(HttpStatusCode.Unauthorized, (await client.SendAsync(withToken, Ct)).StatusCode);

        // Presence is off as well, and /v1/info says so.
        Assert.Equal(HttpStatusCode.NotFound, (await client.PutAsync("/v1/presence/0123456789abcdef", null, Ct)).StatusCode);
    }

    [Fact]
    public async Task Stats_ShowTheSummaryToAnyone_AndEverythingToTheToken()
    {
        const string Token = "a-long-statistics-token";
        using WebApplicationFactory<Program> withStats = _factory.WithWebHostBuilder(builder => builder
            .UseSetting("Milan:Stats:Public", "Summary")
            .UseSetting("Milan:Stats:Token", Token)
            .UseSetting("Milan:Stats:Presence", "true")
            .UseSetting("Milan:Stats:PresenceSeconds", "120"));
        HttpClient client = withStats.CreateClient(new WebApplicationFactoryClientOptions { BaseAddress = new Uri("https://localhost") });

        Assert.Equal(120, (await client.GetFromJsonAsync<ServerInfo>("/v1/info", Ct))!.PresenceSeconds);
        Assert.Equal(HttpStatusCode.NoContent, (await client.PutAsync("/v1/presence/0123456789abcdef", null, Ct)).StatusCode);
        HttpResponseMessage created = await client.PostAsJsonAsync("/v1/lobbies", new CreateLobbyRequest(Version, State()), Ct);
        Assert.Equal(HttpStatusCode.Created, created.StatusCode);

        HttpResponseMessage summaryResponse = await client.GetAsync("/v1/stats", Ct);
        Assert.Equal("public, max-age=5", summaryResponse.Headers.CacheControl?.ToString());
        ServerStats summary = (await summaryResponse.Content.ReadFromJsonAsync<ServerStats>(Ct))!;
        Assert.Equal(1, summary.Players);
        Assert.Equal(1, summary.PublicLobbies);
        Assert.Equal(1, summary.Idle);
        Assert.Equal(2, summary.Online);
        Assert.Null(summary.Details);

        using HttpRequestMessage withToken = new(HttpMethod.Get, "/v1/stats");
        withToken.Headers.Authorization = new("Bearer", Token);
        HttpResponseMessage fullResponse = await client.SendAsync(withToken, Ct);
        Assert.Equal("no-store", fullResponse.Headers.CacheControl?.ToString());
        ServerStats full = (await fullResponse.Content.ReadFromJsonAsync<ServerStats>(Ct))!;
        Assert.Equal(1, full.Details?.Lobbies);
        Assert.Equal(1, full.Details?.Totals.LobbiesCreated);

        using HttpRequestMessage wrongToken = new(HttpMethod.Get, "/v1/stats");
        wrongToken.Headers.Authorization = new("Bearer", "not-the-statistics-token");
        HttpResponseMessage refused = await client.SendAsync(wrongToken, Ct);
        Assert.Equal(HttpStatusCode.Unauthorized, refused.StatusCode);
        Assert.Equal("stats_token_invalid", (await refused.Content.ReadFromJsonAsync<ServerError>(Ct))?.Code);

        Assert.Equal(HttpStatusCode.NoContent, (await client.DeleteAsync("/v1/presence/0123456789abcdef", Ct)).StatusCode);
    }

    [Fact]
    public async Task CreateRequests_AreRateLimited()
    {
        using WebApplicationFactory<Program> limited = _factory.WithWebHostBuilder(
            builder => builder.UseSetting("Milan:CreatesPerMinute", "2"));
        HttpClient client = limited.CreateClient();

        for (int i = 0; i < 2; i++)
        {
            HttpResponseMessage allowed = await client.PostAsJsonAsync("/v1/searches", new SearchRequest(Version), Ct);
            Assert.Equal(HttpStatusCode.Created, allowed.StatusCode);
        }

        HttpResponseMessage rejected = await client.PostAsJsonAsync("/v1/searches", new SearchRequest(Version), Ct);
        Assert.Equal(HttpStatusCode.TooManyRequests, rejected.StatusCode);
        Assert.Equal("rate_limited", (await rejected.Content.ReadFromJsonAsync<ServerError>(Ct))?.Code);
    }

    [Fact]
    public async Task AccessKey_GuardsTheApi_ButNotStatsOrHealth()
    {
        const string Key = "private-server-key";
        using WebApplicationFactory<Program> keyed = _factory.WithWebHostBuilder(builder => builder
            .UseSetting("Milan:AccessKey", Key)
            .UseSetting("Milan:Stats:Public", "Summary"));
        HttpClient client = keyed.CreateClient(new WebApplicationFactoryClientOptions { BaseAddress = new Uri("https://localhost") });

        HttpResponseMessage missing = await client.GetAsync("/v1/info", Ct);
        Assert.Equal(HttpStatusCode.Unauthorized, missing.StatusCode);
        Assert.Equal("access_key_invalid", (await missing.Content.ReadFromJsonAsync<ServerError>(Ct))?.Code);

        using HttpRequestMessage wrong = new(HttpMethod.Post, "/v1/searches") { Content = JsonContent.Create(new SearchRequest(Version)) };
        wrong.Headers.Add(Api.AccessKeyHeader, "not-the-key");
        Assert.Equal(HttpStatusCode.Unauthorized, (await client.SendAsync(wrong, Ct)).StatusCode);

        using HttpRequestMessage right = new(HttpMethod.Post, "/v1/searches") { Content = JsonContent.Create(new SearchRequest(Version)) };
        right.Headers.Add(Api.AccessKeyHeader, Key);
        Assert.Equal(HttpStatusCode.Created, (await client.SendAsync(right, Ct)).StatusCode);

        Assert.Equal(HttpStatusCode.OK, (await client.GetAsync("/v1/stats", Ct)).StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await client.GetAsync("/healthz", Ct)).StatusCode);
    }

    [Fact]
    public async Task SearchFromAnotherVersion_NamesTheExpectedVersion()
    {
        using WebApplicationFactory<Program> expecting = _factory.WithWebHostBuilder(builder => builder
            .UseSetting("Milan:ExpectedGameVersion", "milan-2.0.0"));
        HttpClient client = expecting.CreateClient();
        HttpResponseMessage created = await client.PostAsJsonAsync("/v1/searches", new SearchRequest(Version), Ct);
        Assert.Equal(HttpStatusCode.Created, created.StatusCode);
        using System.Text.Json.JsonDocument body = System.Text.Json.JsonDocument.Parse(await created.Content.ReadAsStringAsync(Ct));
        Assert.Equal("milan-2.0.0", body.RootElement.GetProperty("expectedGameVersion").GetString());
    }

    [Fact]
    public async Task UdpBinding_ReachesJoinersAndWakesHostPoll()
    {
        HttpClient client = _factory.CreateClient();
        ServerInfo info = (await client.GetFromJsonAsync<ServerInfo>("/v1/info", Ct))!;
        HttpResponseMessage created = await client.PostAsJsonAsync("/v1/lobbies", new CreateLobbyRequest(Version, State()), Ct);
        CreateLobbyResponse lobby = (await created.Content.ReadFromJsonAsync<CreateLobbyResponse>(Ct))!;

        // Stand-in for the host's game socket.
        using UdpClient gameSocket = new(new IPEndPoint(IPAddress.Loopback, 0));
        byte[] ticketId = Base64Url.DecodeFromChars(lobby.UdpTicket.TicketId);
        byte[] secret = Base64Url.DecodeFromChars(lobby.UdpTicket.Secret);
        byte[] request = new byte[DiscoveryPacket.RequestLength];
        DiscoveryPacket.WriteRequest(request, BinaryPrimitives.ReadUInt128BigEndian(ticketId), 1, secret);
        await gameSocket.SendAsync(request, new IPEndPoint(IPAddress.Loopback, info.UdpPort), Ct);

        using CancellationTokenSource timeout = CancellationTokenSource.CreateLinkedTokenSource(Ct);
        timeout.CancelAfter(TimeSpan.FromSeconds(10));
        UdpReceiveResult received = await gameSocket.ReceiveAsync(timeout.Token);
        Assert.True(DiscoveryPacket.TryReadResponse(received.Buffer, secret, out _, out _, out IPEndPoint observed));
        Assert.Equal(((IPEndPoint)gameSocket.Client.LocalEndPoint!).Port, observed.Port);

        HttpResponseMessage first = await client.SendAsync(
            Authorized(HttpMethod.Post, $"/v1/lobbies/{lobby.LobbyId}/introductions/poll", lobby.HostKey), Ct);
        IntroductionList initial = (await first.Content.ReadFromJsonAsync<IntroductionList>(Ct))!;
        Assert.Empty(initial.Introductions);

        Task<HttpResponseMessage> waiting = client.SendAsync(
            Authorized(HttpMethod.Post, $"/v1/lobbies/{lobby.LobbyId}/introductions/poll?after={initial.Version}", lobby.HostKey), Ct);
        HttpResponseMessage redeemed = await client.PostAsJsonAsync("/v1/invites/redeem",
            new RedeemInviteRequest(lobby.InviteCode, Version), Ct);
        JoinReservation reservation = (await redeemed.Content.ReadFromJsonAsync<JoinReservation>(Ct))!;
        Assert.Equal(new EndpointCandidate("127.0.0.1", observed.Port, CandidateKind.PublicObserved), reservation.HostCandidates[0]);

        IntroductionList woken = (await (await waiting.WaitAsync(TimeSpan.FromSeconds(10), Ct)).Content.ReadFromJsonAsync<IntroductionList>(Ct))!;
        Assert.Equal(reservation.ReservationId, Assert.Single(woken.Introductions).ReservationId);
    }
}
