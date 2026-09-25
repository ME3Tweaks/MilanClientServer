using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;

namespace MilanServer.Tests;

public sealed class HttpApiTests : IClassFixture<WebApplicationFactory<Program>>
{
    const string Version = "milan-1.0.0";
    readonly WebApplicationFactory<Program> _factory;

    public HttpApiTests(WebApplicationFactory<Program> factory)
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
        Assert.Contains(status.Reservation!.HostCandidates, c => c is { Address: "192.168.1.20", Port: 7777, Kind: CandidateKind.Lan });

        HttpResponseMessage joined = await client.SendAsync(
            Authorized(HttpMethod.Post, $"/v1/reservations/{status.Reservation.ReservationId}/joined", status.Reservation.JoinKey), Ct);
        Assert.Equal(HttpStatusCode.NoContent, joined.StatusCode);

        HttpResponseMessage redeemed = await client.PostAsJsonAsync("/v1/invites/redeem",
            new RedeemInviteRequest(lobby.InviteCode, Version), Ct);
        Assert.Equal(HttpStatusCode.OK, redeemed.StatusCode);

        HttpResponseMessage deleted = await client.SendAsync(
            Authorized(HttpMethod.Delete, $"/v1/lobbies/{lobby.LobbyId}", lobby.HostKey), Ct);
        Assert.Equal(HttpStatusCode.NoContent, deleted.StatusCode);
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
        Assert.Contains("\"kind\":\"Lan\"", json);
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
        ServerInfo? info = await _factory.CreateClient().GetFromJsonAsync<ServerInfo>("/v1/info", Ct);

        Assert.Equal(new ServerInfo(Api.ProtocolVersion, 20, 60, 30, 120, 16), info);
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
}
