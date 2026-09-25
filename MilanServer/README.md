# MilanServer

Accountless matchmaking and rendezvous server for player-hosted LMP matches. It
tracks lobbies in memory, runs quick-match searches, redeems invite codes, reserves
capacity for joiners, observes each peer's public UDP endpoint, and introduces
joiners to hosts. It never carries UE3 game traffic.

Status: the lobby state machine, v1 HTTP control API, UDP endpoint discovery, host
introductions, peer probes, and MilanHost's console and Quick Match paths are implemented.
Server tests and the MilanHost build pass; two LMP instances successfully joined through
the Quick Match menu on one PC. A local join does not establish an Internet NAT path. See
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
UDP endpoint discovery listens on IPv4 port 5081 (`Milan:UdpPort`) on all interfaces.
Clients read the port from `/v1/info`, so a firewall or container must expose it
with the same number.

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
| POST | `/v1/lobbies/{id}/introductions/poll?after=N` | host key | Long-poll: every live reservation with joiner candidates and traversal key |
| PUT | `/v1/reservations/{id}/candidates` | join key | Joiner's game port and LAN addresses, once its socket exists |
| POST | `/v1/reservations/{id}/poll?after=N` | join key | Long-poll: current host candidates |
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

Long-polls return immediately when `after` is omitted or differs from the lobby's
current event version. Otherwise they wait up to `LongPollSeconds` (25) for a change.
Pass the returned `version` back as `after`. Lease heartbeats with unchanged state do
not wake them.

## UDP endpoint discovery v1

Lobby creation returns a host `udpTicket`, and each reservation returns a joiner
`udpTicket`: a 16-byte ID and a 32-byte HMAC secret, both base64url. A peer sends a
64-byte binding request **from its UE3 game socket** to the server's UDP port. The
server replies with a 56-byte response giving the source address and port it saw.
That becomes the peer's `PublicObserved` candidate, which the other side sees
through its long-poll. The byte layout is documented in `MilanServer/Discovery.cs`.

- Requests carry an HMAC-SHA256 tag (truncated to 16 bytes). They are accepted only
  if the sequence number is higher than any accepted before and at least
  `DiscoveryMinIntervalMilliseconds` (200) has passed. Everything else is dropped
  without a reply.
- Responses are authenticated with the same secret and are smaller than requests,
  so the server cannot amplify traffic. It never forwards packets.
- Resend about every 20 seconds while hosting, to keep the NAT mapping open and
  notice address changes.
- Each reservation also carries a `traversalKey`, which the host receives in its
  introduction. Peers use it to authenticate the probes they send each other; the
  server never sees those probes.

