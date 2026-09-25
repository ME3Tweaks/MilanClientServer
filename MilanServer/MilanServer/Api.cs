using Microsoft.Extensions.Options;

namespace MilanServer;

// v1 HTTP control API. Credentials travel as "Authorization: Bearer <key>": the host
// key for lobby routes, the search key for search routes, and the join key for
// reservation routes. Deploy behind TLS on the Internet because these are bearer secrets.
public static class Api
{
    public const int ProtocolVersion = 1;
    public const string CreatePolicy = "create";
    public const string RedeemPolicy = "redeem";

    public static void MapMilanApi(this IEndpointRouteBuilder app)
    {
        app.MapGet("/healthz", () => Results.Text("ok"));

        RouteGroupBuilder v1 = app.MapGroup("/v1");

        v1.MapGet("/info", (IOptions<MilanServerOptions> options) =>
        {
            MilanServerOptions o = options.Value;
            return new ServerInfo(ProtocolVersion, o.HeartbeatSeconds, o.LobbyLeaseSeconds,
                o.ReservationSeconds, o.SearchSeconds, o.MaxPlayersCeiling);
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

        v1.MapDelete("/reservations/{reservationId}", (string reservationId, HttpContext http, MatchmakingStore store) =>
            Respond(store.CancelReservation(reservationId, BearerToken(http))));
    }

    public static IResult Error(Failure failure) => Results.Json(failure.ToError(), statusCode: failure.StatusCode);

    static IResult Respond<T>(Outcome<T> outcome, Func<T, IResult> onSuccess) =>
        outcome.Failure is { } failure ? Error(failure) : onSuccess(outcome.Value);

    static IResult Respond(Failure? failure) => failure is null ? Results.NoContent() : Error(failure);

    static string? BearerToken(HttpContext http)
    {
        const string Scheme = "Bearer ";
        string? header = http.Request.Headers.Authorization;
        return header is not null && header.StartsWith(Scheme, StringComparison.OrdinalIgnoreCase)
            ? header[Scheme.Length..].Trim()
            : null;
    }
}
