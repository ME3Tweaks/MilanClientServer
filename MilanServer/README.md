# MilanServer

Accountless matchmaking and rendezvous server for player-hosted LMP matches. It
tracks lobbies in memory, runs quick-match searches, redeems invite codes, and
reserves capacity for joiners. It never carries UE3 game traffic.

Status: the lobby state machine and v1 HTTP control API are implemented and unit
tested. UDP endpoint discovery, NAT traversal, host introductions, and MilanHost
integration do not exist yet. Nothing here has been tested with the game. See
[Docs/Matchmaking-NAT-Handoff.md](../Docs/Matchmaking-NAT-Handoff.md) for the plan.

## Build and test

Requires the .NET 10 SDK. `global.json` opts `dotnet test` into Microsoft.Testing.Platform.

```
dotnet build MilanServer.slnx
dotnet test
```

## Run

```
dotnet run --project MilanServer -- --urls http://0.0.0.0:5080
```

Without `--urls` (or `ASPNETCORE_URLS`), Kestrel listens on `localhost:5000` only.

Host, search, and join keys are bearer secrets. On the Internet, serve the API over
TLS, either through Kestrel certificate configuration or behind a reverse proxy.
When a proxy terminates TLS, list its address in `Milan:TrustedProxies` so the
client's real address is used for rate limiting and endpoint candidates. HTTP is
fine for LAN-only or offline use.

## Configuration

Settings live in the `Milan` section of `appsettings.json`. Environment variables
such as `Milan__LobbyLeaseSeconds=90` and arguments such as `--Milan:SearchSeconds=90`
override it. See `MilanServer/Options.cs` for every setting and its default. Invalid
combinations fail at startup.

## Publish

Self-contained single-file builds (about 100 MB, no runtime needed on the target):

```
dotnet publish MilanServer -c Release -r linux-x64 --self-contained -p:PublishSingleFile=true -o publish/linux-x64
dotnet publish MilanServer -c Release -r win-x64 --self-contained -p:PublishSingleFile=true -o publish/win-x64
```

State is ephemeral. After a restart, a host's next report gets `lobby_not_found`,
and the host must create a new lobby, which gets a new invite code.

## API v1

JSON is camelCase with string enums. Errors return `{ "code": "...", "message": "..." }`.
A wrong key and a missing object return the same 404.

| Method | Route | Credential | Purpose |
| --- | --- | --- | --- |
| GET | `/v1/info` | none | Protocol version and timings |
| POST | `/v1/lobbies` | none | Create a lobby; returns `lobbyId`, `hostKey`, `inviteCode` |
| PUT | `/v1/lobbies/{id}/state` | host key | Full-state report. Used for prompt setting changes and the heartbeat |
| DELETE | `/v1/lobbies/{id}` | host key | Close the lobby |
| POST | `/v1/lobbies/{id}/invite` | host key | Replace the invite code |
| POST | `/v1/searches` | none | Start a quick-match search (may match immediately) |
| POST | `/v1/searches/{id}/poll` | search key | Advance the search; may reserve a slot |
| DELETE | `/v1/searches/{id}` | search key | Cancel; also releases an unjoined reservation |
| POST | `/v1/invites/redeem` | invite code in body | Reserve a slot through an invite |
| POST | `/v1/reservations/{id}/joined` | join key | Joiner was admitted; slot is released on the host's next current report |
| DELETE | `/v1/reservations/{id}` | join key | Give up a reservation |

Credentials use `Authorization: Bearer <key>`.

State reports carry a host-chosen revision that must increase. An older revision is
acknowledged with `stale: true` and ignored. Resending the current revision with the
same content is a heartbeat. The same revision with different content is `revision_conflict`.

Quick match holds difficulty fixed. It widens from the exact map and enemy to any
map after `MapWidenSeconds`, then to any enemy after `EnemyWidenSeconds`, and ends
with state `NoMatch` after `SearchSeconds`. `widen: false` disables widening. Private
lobbies, extraction, and complete matches never match a search. Invites work for
private lobbies and during extraction.

Map, enemy, and difficulty IDs are opaque, so new game options need no server
update. Only the search "Any" sentinels are special: map `-1`, enemy `127`,
difficulty `127`.
