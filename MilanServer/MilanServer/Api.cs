using Microsoft.Extensions.Options;

namespace MilanServer;

// v1 HTTP control API. Credentials travel as "Authorization: Bearer <key>"
public static class Api
{
    public const int ProtocolVersion = 2;
    public const string CreatePolicy = "create";
    public const string RedeemPolicy = "redeem";
    public const string AccessKeyHeader = "X-Milan-Access-Key";

    /// <summary>
    /// Checks if the milan access key is used by this request and errors out if it is missing
    /// </summary>
    /// <param name="app"></param>
    /// <returns></returns>
    public static IApplicationBuilder UseMilanAccessKey(this IApplicationBuilder app)
    {
        string configured = app.ApplicationServices.GetRequiredService<IOptions<MilanServerOptions>>().Value.AccessKey;
        byte[] expectedHash = Secrets.Hash(configured);
        return app.Use(async (http, next) =>
        {
            PathString path = http.Request.Path;
            if (path.StartsWithSegments("/v1") && configured.Length > 0 && !http.Request.IsHttps)
            {
                await Error(Failures.HttpsRequired).ExecuteAsync(http);
                return;
            }
            if (path.StartsWithSegments("/v1") && !path.StartsWithSegments("/v1/stats")
                && configured.Length > 0
                && !Secrets.Matches(expectedHash, http.Request.Headers[AccessKeyHeader].ToString()))
            {
                await Error(Failures.AccessKeyInvalid).ExecuteAsync(http);
                return;
            }
            await next(http);
        });
    }

    public static void MapMilanApi(this IEndpointRouteBuilder app)
    {
        app.MapGet("/healthz", () => Results.Text("ok"));

        RouteGroupBuilder v1 = app.MapGroup("/v1");

        // Clients can send a game version and langauge to learn if its out of day and get message of the day
        v1.MapGet("/info", (IOptions<MilanServerOptions> options, DiscoveryEndpoint discovery, ServerNotice notice,
            string? gameVersion, string? language) =>
        {
            MilanServerOptions o = options.Value;
            (string? latest, bool? outdated, NoticeText? text) = notice.For(gameVersion, language);
            return new ServerInfo(ProtocolVersion, o.HeartbeatSeconds, o.LobbyLeaseSeconds,
                o.ReservationSeconds, o.SearchSeconds, o.MaxPlayersCeiling, o.LongPollSeconds, discovery.Port,
                o.Stats.Presence ? o.Stats.PresenceSeconds : 0, latest, outdated, text);
        });

        // Counts for a dashboard or a "players online" display
        // Milan:Stats:Public sets what unauthenticated sees, and Milan:Stats:Token always sees everything.
        v1.MapGet("/stats", (HttpContext http, IOptions<MilanServerOptions> options, MatchmakingStore store) =>
        {
            MilanStatsOptions o = options.Value.Stats;
            string? token = BearerToken(http);
            if (token is not null)
                http.Response.Headers.CacheControl = "no-store";
            if (token is not null && !http.Request.IsHttps)
                return Error(Failures.HttpsRequired);
            if (token is not null && !IsStatsToken(o.Token, token))
                return Error(Failures.StatsTokenInvalid);

            StatsAccess access = token is not null ? StatsAccess.Full : o.Public;
            if (access == StatsAccess.None)
                return Error(Failures.StatsUnavailable);

            ServerStats stats = store.GetStats();
            // A shared cache may keep what anyone can see
            http.Response.Headers.CacheControl = token is null ? $"public, max-age={o.CacheSeconds}" : "no-store";
            return Results.Ok(access == StatsAccess.Full ? stats : stats with { Details = null });
        });

        // Presence, when Milan:Stats:Presence is on: a game in no lobby and no search says it is
        // running, under an ID it made up for this run. DELETE withdraws it early, when the game
        // enters a lobby or a search, which count it from then on.
        v1.MapPut("/presence/{clientId}", (string clientId, HttpContext http, MatchmakingStore store) =>
            Respond(store.ReportPresence(clientId, http.Connection.RemoteIpAddress)));

        v1.MapDelete("/presence/{clientId}", (string clientId, MatchmakingStore store) =>
        {
            store.ClearPresence(clientId);
            return Results.NoContent();
        });

        v1.MapPost("/lobbies", (CreateLobbyRequest request, HttpContext http, MatchmakingStore store) =>
            Respond(store.CreateLobby(request, http.Connection.RemoteIpAddress),
                created => Results.Created($"/v1/lobbies/{created.LobbyId}", created)))
            .RequireRateLimiting(CreatePolicy);

        v1.MapPut("/lobbies/{lobbyId}/state", (string lobbyId, LobbySnapshot state, HttpContext http, MatchmakingStore store) =>
            Respond(store.ReportLobbyState(lobbyId, BearerToken(http), state, http.Connection.RemoteIpAddress), Results.Ok));

        v1.MapDelete("/lobbies/{lobbyId}", (string lobbyId, HttpContext http, MatchmakingStore store) =>
            Respond(store.DeleteLobby(lobbyId, BearerToken(http))));

        v1.MapPost("/lobbies/{lobbyId}/invite", (string lobbyId, HttpContext http, MatchmakingStore store) =>
            Respond(store.RotateInviteCode(lobbyId, BearerToken(http)), Results.Ok));

        // Long-poll; "after" is the version from the previous response (omit it to return at once).
        v1.MapPost("/lobbies/{lobbyId}/introductions/poll", async (string lobbyId, long? after, HttpContext http, MatchmakingStore store) =>
            Respond(await store.PollIntroductionsAsync(lobbyId, BearerToken(http), after ?? -1, http.RequestAborted), Results.Ok))
            .WithMetadata(new LongPollEndpoint());

        v1.MapPost("/searches", (SearchRequest request, HttpContext http, MatchmakingStore store) =>
            Respond(store.CreateSearch(request, http.Connection.RemoteIpAddress),
                created => Results.Created($"/v1/searches/{created.SearchId}", created)))
            .RequireRateLimiting(CreatePolicy);

        // POST because a poll may atomically reserve a slot.
        v1.MapPost("/searches/{searchId}/poll", (string searchId, HttpContext http, MatchmakingStore store) =>
            Respond(store.PollSearch(searchId, BearerToken(http)), Results.Ok));

        v1.MapDelete("/searches/{searchId}", (string searchId, HttpContext http, MatchmakingStore store) =>
            Respond(store.CancelSearch(searchId, BearerToken(http))));

        v1.MapPost("/invites/redeem", (RedeemInviteRequest request, HttpContext http, MatchmakingStore store) =>
            Respond(store.RedeemInvite(request, http.Connection.RemoteIpAddress), Results.Ok))
            .RequireRateLimiting(RedeemPolicy);

        v1.MapPost("/reservations/{reservationId}/joined", (string reservationId, HttpContext http, MatchmakingStore store) =>
            Respond(store.MarkReservationJoined(reservationId, BearerToken(http))));

        v1.MapPut("/reservations/{reservationId}/candidates",
            (string reservationId, JoinerCandidatesRequest request, HttpContext http, MatchmakingStore store) =>
                Respond(store.ReportJoinerCandidates(reservationId, BearerToken(http), request)));

        v1.MapPost("/reservations/{reservationId}/poll", async (string reservationId, long? after, HttpContext http, MatchmakingStore store) =>
            Respond(await store.PollReservationAsync(reservationId, BearerToken(http), after ?? -1, http.RequestAborted), Results.Ok))
            .WithMetadata(new LongPollEndpoint());

        v1.MapDelete("/reservations/{reservationId}", (string reservationId, HttpContext http, MatchmakingStore store) =>
            Respond(store.CancelReservation(reservationId, BearerToken(http))));

        // Members use their reservation ID and join key. They only call in when they lose the host.
        v1.MapPost("/members/{memberId}/host-lost", (string memberId, HostLostReport? report, HttpContext http, MatchmakingStore store) =>
            Respond(store.ReportHostLost(memberId, BearerToken(http), report ?? new HostLostReport()), Results.Ok));

        v1.MapPost("/members/{memberId}/poll", async (string memberId, long? after, HttpContext http, MatchmakingStore store) =>
            Respond(await store.PollMemberAsync(memberId, BearerToken(http), after ?? -1, http.RequestAborted), Results.Ok))
            .WithMetadata(new LongPollEndpoint());

        v1.MapGet("/members/{memberId}/invite", (string memberId, HttpContext http, MatchmakingStore store) =>
            Respond(store.GetMemberInvite(memberId, BearerToken(http)), Results.Ok));

        v1.MapDelete("/members/{memberId}", (string memberId, HttpContext http, MatchmakingStore store) =>
            Respond(store.LeaveLobby(memberId, BearerToken(http))));
    }

    public static IResult Error(Failure failure) =>
        Results.Json(failure.ToError(), MilanJsonContext.Default.ServerError, statusCode: failure.StatusCode);

    static IResult Respond<T>(Outcome<T> outcome, Func<T, IResult> onSuccess) =>
        outcome.Failure is { } failure ? Error(failure) : onSuccess(outcome.Value);

    static IResult Respond(Failure? failure) => failure is null ? Results.NoContent() : Error(failure);

    // Compares every byte whatever the first difference is, so timing tells nothing but length.
    static bool IsStatsToken(string expected, string presented) =>
        expected.Length > 0 && System.Security.Cryptography.CryptographicOperations.FixedTimeEquals(
            System.Text.Encoding.UTF8.GetBytes(expected), System.Text.Encoding.UTF8.GetBytes(presented));

    static string? BearerToken(HttpContext http)
    {
        const string Scheme = "Bearer ";
        string? header = http.Request.Headers.Authorization;
        return header is not null && header.StartsWith(Scheme, StringComparison.OrdinalIgnoreCase)
            ? header[Scheme.Length..].Trim()
            : null;
    }
}
