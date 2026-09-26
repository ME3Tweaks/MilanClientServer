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

Publishing is trimmed, so it has to be self-contained (single file about 22 MB, no
runtime needed on the target). JSON bodies need a `[JsonSerializable]` root in
`MilanJsonContext` (`Contracts.cs`), or the trimmed server cannot serialize them:

```
dotnet publish MilanServer -c Release -r linux-x64 --self-contained -p:PublishSingleFile=true -o publish/linux-x64
dotnet publish MilanServer -c Release -r win-x64 --self-contained -p:PublishSingleFile=true -o publish/win-x64
```

State is ephemeral. After a restart, a host's next report gets `lobby_not_found`,
and the host must create a new lobby, which gets a new invite code. MilanHost does this
automatically.

## Docker

From this directory, on Linux or inside WSL:

```
docker build -t milanserver .
docker run -d --name milanserver --network host milanserver
```

`docker compose up -d --build` does the same with published ports instead
(`compose.yaml`). `docker build --target test .` runs the unit tests in the SDK image.
The image listens on TCP 5080 (`ASPNETCORE_HTTP_PORTS`) and UDP 5081 (`Milan__UdpPort`),
runs as the non-root `app` user, and takes any `Milan__<Setting>` environment variable.
It holds the trimmed server on a chiseled Ubuntu base (about 65 MB) with no shell, so
`docker exec` cannot open one; use `docker logs`.

The server has to see each player's real address and UDP port, or endpoint discovery
and per-address rate limits are wrong. How that works depends on where Docker runs:

- **Linux host:** `--network host` is simplest. Published ports (`-p 5080:5080/tcp
  -p 5081:5081/udp`) also keep outside players' addresses. Clients on the same machine
  that connect through `localhost` may then appear as the Docker gateway. Publish UDP
  on the same port number: clients take it from `/v1/info`.
- **WSL2 in NAT mode (the default):** only TCP is forwarded to Windows `localhost`,
  and other PCs cannot reach WSL at all. Games on the same PC can point at the WSL
  address (`wsl hostname -I`), e.g. `-mmserver=http://172.x.y.z:5080`. They all appear
  to come from the Windows side of the WSL switch, which only suits same-PC tests.
- **WSL2 for LAN tests:** switch WSL to mirrored networking and run with
  `--network host`. Put `networkingMode=mirrored` under `[wsl2]` in
  `%UserProfile%\.wslconfig`, then run `wsl --shutdown`. Inbound traffic from other PCs
  also needs the Hyper-V firewall to allow it for WSL, for example
  `Set-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -DefaultInboundAction Allow`
  (elevated PowerShell), plus the usual Windows firewall rules for TCP 5080 and UDP 5081.
  Stop any Windows copy of MilanServer first: mirrored mode shares ports with Windows.
- **Docker Desktop:** published ports go through a proxy, so every player appears as
  the gateway. Do not use it for NAT or LAN tests.

### Deploying the image to a server

From WSL or Linux, `deploy.sh` builds the image, copies it over SSH, and replaces the
running server:

```
./deploy.sh user@server /var/milan
```

- It builds for the server's CPU (x64 or arm64) and saves the image as
  `/var/milan/milanserver.tar.gz`, about 19 MB. It asks for sudo once if the folder
  cannot be created as the login user.
- It replaces the `milanserver` container, which runs with host networking, restarts
  unless stopped, and has a 384 MB memory limit (`MEMORY_LIMIT` in the script). It then
  checks `/healthz`. Lobbies are lost on a redeploy; hosts register again on their own.
- Settings go in `/var/milan/milanserver.env` on the server, one `Milan__<Setting>=value`
  per line, and apply on the next deploy.
- The login user must be able to run Docker without sudo (the `docker` group).

To do it by hand, name the tag when saving: `docker save milanserver` saves every tag in
the repository, including `milanserver:test` from `--target test`, which carries the
whole .NET SDK.

With host networking, the server's own firewall applies: open TCP 5080 and UDP 5081 there
(for example `ufw allow 5080/tcp` and `ufw allow 5081/udp`) and in the provider's
firewall. Published ports (`-p`) bypass ufw. Games connect with
`-mmserver=http://<server IPv4 address>:5080`.

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
with state `NoMatch` after `SearchSeconds`. `widen: false` disables widening. If no lobby
at the player's difficulty is open to them when the search starts, matches already in
progress are candidates straight away. Otherwise they wait `InProgressAfterSeconds`
(10), even if those lobbies' map or enemy does not fit yet, and lobbies still rank first
after that. Among
lobbies, the one that has waited longest is offered first; a lobby that returns from a
match starts waiting again. Private
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

