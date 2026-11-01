# MilanServer

Accountless matchmaking and rendezvous server for player-hosted LMP matches. It
tracks lobbies in memory, runs quick-match searches, redeems invite codes, reserves
capacity for joiners, observes each peer's public UDP endpoint, and introduces
joiners to hosts. It never carries UE3 game traffic.

Status: the lobby state machine, HTTP control protocol 2 (on `/v1` routes), UDP endpoint discovery, host
introductions, peer probes, and MilanHost's console and Quick Match paths are implemented.
Server tests and the MilanHost build pass. The earlier protocol had a two-instance
Quick Match test on one PC; the protocol-2 admission path needs a new in-game test.
A local join does not establish an Internet NAT path. See
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
When a proxy terminates TLS, list its address (or a CIDR range) in `Milan:TrustedProxies`
so the server applies its `X-Forwarded-Proto` and `X-Forwarded-For` headers: requests count
as HTTPS, and the client's real address is used for rate limiting and endpoint candidates. Passwordless
HTTP is supported for simple LAN/group setups and operators who choose it. A configured
server password requires HTTPS on every `/v1` request, even on a LAN. MilanHost refuses
to transmit a server password to an HTTP URL. HTTP `/healthz` remains available.

## Configuration

Settings live in the `Milan` section of `appsettings.json`. Environment variables
such as `Milan__LobbyLeaseSeconds=90` and arguments such as `--Milan:SearchSeconds=90`
override it. See `MilanServer/Options.cs` for every setting and its default. Invalid
combinations fail at startup.

### Private servers

Set `Milan__AccessKey` (printable ASCII without spaces, at most 256 characters) to make the
server private. Every `/v1` request except `/v1/stats` must then send the key as the
`X-Milan-Access-Key` header, or it gets `401 access_key_invalid`. Players enter it as the
server password next to the custom server address in the game's Multiplayer Settings
(MP Options), and MilanHost sends it only to that server. The key travels like the bearer
keys. Plain HTTP requests to a password-protected API get `426 https_required` before
password authentication. Use direct Kestrel TLS or a trusted TLS reverse proxy. `/v1/stats`
does not need the server password, but still requires HTTPS when a server password is set.
On a passwordless server, presenting a statistics token also requires HTTPS; anonymous
public statistics remain available over HTTP.

For Caddy on the Linux host with `deploy.sh`, proxy to `127.0.0.1:5080` and set
`ASPNETCORE_URLS=http://127.0.0.1:5080` in the server environment file. Keep public
TCP 5080 closed and UDP 5081 direct. Loopback proxies are trusted by default; for
another proxy, configure only its actual address in `Milan__TrustedProxies__0`.
Any proxy that sends the standard `X-Forwarded-Proto` header works (Caddy, nginx, Traefik,
HAProxy). With published container ports, the proxy connects from the Docker network's
gateway (for example `172.17.0.1`), not loopback. If the gateway address can change, trust a
range such as `Milan__TrustedProxies__0=172.16.0.0/12`, but only while TCP 5080 is reachable
from nowhere but the proxy. The server logs a warning naming each untrusted address that sends
forwarding headers, with the setting that would trust it; a password-protected server behind
an untrusted proxy answers `426 https_required`.
Never clear proxy trust restrictions or trust arbitrary forwarding headers.

### Mod compatibility and admission

Hosting, searching, and invite redemption submit `gameVersion`, an opaque 1-64 character
identity for the installed **Project Milan mod**. Searches compare it exactly; invite
joins with a different value return `409 version_mismatch`. The server logs it for every
lobby and search. Set `Milan__ExpectedGameVersion` to the version most of the server's
players run: a search from any other version still runs, but its response carries
`expectedGameVersion` so the game can warn that few players will match. It is independent of the
MilanHost ASI release. On each launch, MilanHost reads the second physical line of
`BioGame/DLC/DLC_MOD_ProjectMilan/_metacmm.txt` relative to the running game's root.
Only the DLC folder identity is fixed; the installed version is read from disk.
This is the temporary source until AutoTOC supplies the runtime registry.
An optional explicit override is:

```toml
[milan.game]
version = "project-milan-your-release"
```

Use the actual installed mod identity, with the same exact value only for compatible
releases. A nonempty `[milan.game] version` overrides metadata; leaving it empty enables
metadata detection. `-mmversion=<identity>` overrides both, `mm.version` shows it, and
`mm.version <identity>` changes it during a run. `mm.version off` makes it unknown.
An unknown/invalid identity disables registration, searches, and invite joins while
local hosting remains available. Invalid explicit overrides do not fall back to metadata.
A future game-thread detector can call
`CMatchmaking::SetModVersion`; changing identity retires the previous matchmaking session.

The listen host confirms members through `memberIds` in its state snapshot. MilanHost
verifies a reservation-specific admission proof in an accepted game connection's URL
before adding its ID, and removes departed/kicked members from the roster. A joiner's
`joined` notice alone grants no invite or migration privileges and does not survive
reservation expiry. Tooling may omit `memberIds` to retain the roster; an explicit `[]`
revokes all members. Upgrade the server and clients together: `/v1/info` now reports
`protocolVersion: 2`, which older ASIs refuse instead of using insecure admission.

### Availability limits

| Setting | Default | Purpose |
| --- | --- | --- |
| `Milan__TotalRequestsPerSecond` | 1000 | Total arrival budget across all source addresses |
| `Milan__RequestBurst` | 2000 | Total token-bucket burst allowance |
| `Milan__MaxConcurrentRequests` | 2048 | Total active HTTP requests, without a queue |
| `Milan__MaxConcurrentRequestsPerAddress` | 64 | Active requests per IPv4 or IPv6 /64 |
| `Milan__MaxConcurrentConnections` | 4096 | Backend Kestrel connection ceiling |
| `Milan__MaxConcurrentLongPolls` | 1024 | Long-poll ceiling, leaving capacity for control requests |
| `Milan__MaxConcurrentLongPollsPerAddress` | 32 | Long polls per IPv4 or IPv6 /64 |
| `Milan__DiscoveryPacketsPerSecond` | 5000 | Total UDP processing budget for structured requests, including rejected tags/tickets |
| `Milan__DiscoveryPacketsPerSecondPerAddress` | 100 | UDP work per address group |

Existing request/create/redeem rate limits still apply. At most one long poll waits per
host, reservation, or member credential; duplicates return `429 poll_busy`. Active HTTP
budget rejection returns `429 request_limit`. Completion and cancellation release permits.
Tune these bounds with measured population, memory, and heartbeat latency on your host.

LAN candidates use RFC1918 or CGNAT space; link-local addresses are rejected. The server
shares LAN candidates only with peers sharing a public address or using private control/
observed addresses. MilanHost further restricts private targets to local subnets or explicit
on-link VPN routes and rejects gateways, broadcasts, and remote loopback. Same-PC loopback
testing remains supported when the matchmaker itself resolves to loopback.

### Statistics

`GET /v1/stats` returns counts only, never anything that identifies a lobby or a player.
It is off until one of these is set:

| Setting | Effect |
|---|---|
| `Milan__Stats__Public` | What anyone can read: `None` (default, answers 404), `Summary`, or `Full` |
| `Milan__Stats__Token` | A secret of 16 characters or more. Sent as `Authorization: Bearer <token>`, it always gets `Full` |
| `Milan__Stats__CacheSeconds` | How long a snapshot is served for (default 5) |
| `Milan__Stats__Presence` | `true` turns presence reports on (default `false`) |
| `Milan__Stats__PresenceSeconds` | How often an idle game reports (default 300) |

- **Summary:** `players`, `searching`, `publicLobbies`, `openLobbies`, and with presence
  `idle` and `online`.
- **Full** adds `details`: lobbies by privacy and phase, capacity, joins and host
  migrations under way, peaks, totals since the server started, and lobbies per game
  version.
- `players` are those in registered lobbies, as their hosts report them. A 1-player lobby
  is never registered, and a player in the menus is in no lobby, so without presence the
  server cannot see them.
- **Presence** fills that gap: a game in no lobby and no search sends one small request
  every `PresenceSeconds`, under a random ID it makes up each run. `/v1/info` advertises
  the interval, and games send nothing unless it does. A game reads `/v1/info` when it
  first hosts, joins or searches, so one that has only sat in the menus is not counted.
  `idle` counts the reporting games and
  `online` is `players + searching + idle`. A game that closes stays counted for up to
  two intervals.
- The token must not ship in a game client or a web page. A website should fetch with the
  token from its own server, or read the public summary.
- Counts reset when the server restarts. Keep a history by polling the endpoint.

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
docker build --pull -t milanserver .
docker compose up -d --build
```

Compose refreshes base images and runs with published ports (`compose.yaml`).
`docker build --pull --target test .` runs the unit tests in the SDK image.
The image listens on TCP 5080 (`ASPNETCORE_HTTP_PORTS`) and UDP 5081 (`Milan__UdpPort`),
runs as the non-root `app` user, and takes any `Milan__<Setting>` environment variable.
It holds the trimmed server on a chiseled Ubuntu base (about 65 MB) with no shell, so
`docker exec` cannot open one; use `docker logs`.
The supported Compose/deploy recipes use a 384 MiB memory cap, 256-PID
limit, read-only root filesystem, dropped capabilities, `no-new-privileges`, a 16 MiB
`/tmp`, and bounded logs (10 MiB x 3). The deploy script caps CPU usage at the smaller
of two CPUs or the remote Docker host's CPU count. Compose defaults to one CPU;
`MILAN_CPU_LIMIT=2 docker compose up -d --build` raises that cap on hosts with at least
two CPUs. Adjust resource caps to measured requirements.
The serviced .NET 10 tags follow published updates; record tested image digests and
rebuild after runtime updates because this executable is self-contained.

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

### Building images for distribution

From WSL or Linux, `build-images.sh` runs the unit tests, then builds the image for both
`linux/amd64` and `linux/arm64`. No emulation is needed: the SDK stage runs natively and
cross-publishes for each architecture.

```
./build-images.sh --version 1.2.0
./build-images.sh --version 1.2.0 --push ghcr.io/<owner>/milanserver --latest
```

- Without `--push`, it writes `dist/milanserver-<version>-linux-<arch>.tar.gz` for each
  architecture and a `.sha256` file. Each tarball loads as `milanserver:<version>`:
  `gunzip -c milanserver-1.2.0-linux-arm64.tar.gz | docker load`.
- With `--push`, it pushes one multi-architecture image (log in to the registry first);
  each server pulls its own architecture. `--latest` also tags it `latest`.
- Without `--version`, the version is the current commit, with `-dirty` if the image's
  sources have uncommitted changes. Images carry the standard OCI version, revision,
  creation-time and source labels. `--skip-tests` skips the tests.

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
  unless stopped, and has a 384 MB memory limit (`MEMORY_LIMIT` in the script). It
  detects the remote Docker host's CPU count before replacing the container and uses
  at most two CPUs (`MAX_CPUS` in the script), so a one-CPU VPS uses `--cpus 1`. It then
  checks `/healthz`. Lobbies are lost on a redeploy; hosts register again on their own.
- Settings go in `/var/milan/milanserver.env` on the server, one `Milan__<Setting>=value`
  per line, and apply on the next deploy.
- The login user must be able to run Docker without sudo (the `docker` group).

To do it by hand, name the tag when saving: `docker save milanserver` saves every tag in
the repository, including `milanserver:test` from `--target test`, which carries the
whole .NET SDK.

With host networking, the server's own firewall applies. A passwordless LAN server can
expose TCP 5080 and UDP 5081 and use `-mmserver=http://<server IPv4 address>:5080`.
Behind Caddy, expose HTTPS 443 and direct UDP 5081, bind backend 5080 to loopback, and use
`-mmserver=https://<server hostname>`. Published ports need Docker-aware firewall rules;
do not publish a TLS proxy's backend publicly. Direct Kestrel TLS is also supported:
mount certificates read-only and configure an HTTPS endpoint (a non-root container can
listen on 8443, published as 443) without a public plaintext fallback for passwords.

## HTTP protocol 2 (`/v1` routes)

JSON is camelCase with string enums. Errors return `{ "code": "...", "message": "..." }`.
A wrong key and a missing object return the same 404.

| Method | Route | Credential | Purpose |
| --- | --- | --- | --- |
| GET | `/v1/info` | none | Protocol version and timings |
| GET | `/v1/stats` | none or stats token | Player and lobby counts; see Statistics |
| PUT | `/v1/presence/{id}` | none | A game in no lobby and no search reports that it is running; `id` is its own random ID |
| DELETE | `/v1/presence/{id}` | none | Withdraw a presence report |
| POST | `/v1/lobbies` | none | Create a lobby; returns `lobbyId`, `hostKey`, `inviteCode` |
| PUT | `/v1/lobbies/{id}/state` | host key | Full-state report. Used for prompt setting changes and the heartbeat |
| DELETE | `/v1/lobbies/{id}` | host key | Close the lobby, or hand it to a member if any remain |
| POST | `/v1/lobbies/{id}/invite` | host key | Replace the invite code |
| POST | `/v1/searches` | none | Start a quick-match search (may match immediately) |
| POST | `/v1/searches/{id}/poll` | search key | Advance the search; may reserve a slot |
| DELETE | `/v1/searches/{id}` | search key | Cancel; also releases an unjoined reservation |
| POST | `/v1/invites/redeem` | invite code in body | Reserve a slot through an invite |
| POST | `/v1/lobbies/{id}/introductions/poll?after=N` | host key | Long-poll: every live reservation with joiner candidates and traversal key |
| PUT | `/v1/reservations/{id}/candidates` | join key | Joiner's game port and LAN addresses, once its socket exists |
| POST | `/v1/reservations/{id}/poll?after=N` | join key | Long-poll: current host candidates |
| POST | `/v1/reservations/{id}/joined` | join key | Provisional join notice; only the host's roster grants membership/releases the slot |
| DELETE | `/v1/reservations/{id}` | join key | Give up a reservation |
| POST | `/v1/members/{id}/host-lost` | join key | A member lost its host; body `{ "canHost": true }` |
| POST | `/v1/members/{id}/poll?after=N` | join key | Long-poll: the member's migration status |
| GET | `/v1/members/{id}/invite` | join key | The lobby's invite code, for a member to invite with; `403 invites_host_only` unless the host's state has `memberInvites` |
| DELETE | `/v1/members/{id}` | join key | A member left the lobby |

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

### Host migration

A joined reservation becomes a lobby member under the same ID and join key. Members send
nothing while the host is fine, so they cost the server only a small record. A host that
pulls its network cable cannot tell anyone it left, so the members report it:

1. A member that loses its host calls `host-lost`. The server wakes the host's pending
   introductions poll and gives it `HostChallengeSeconds` (5) to poll or report again. A
   host that answers keeps the lobby, and the member gets `HostAlive`: its own connection
   failed. A host that closes the lobby (`DELETE`) with members left skips the check.
2. Once the host counts as gone, its key stops working (`host_replaced`, 409) and the
   other members have `MigrationReportSeconds` (8) to report. The server stops waiting
   when all have reported.
3. The new host is the reporter that can host and joined first. It gets `NewHost` with a
   new host key and UDP ticket, the invite code, and the last state and revision. Every
   other reporter gets `JoinNewHost` with a reservation that holds its slot for
   `MigrationJoinSeconds` (90). That reservation has no host candidates until the new host
   reports from its new game socket. Members that did not report are dropped.

While a lobby is changing hosts, searches and invites skip it. If no member reports, the
lobby closes; if the new host never reports, its lease runs out. Member poll states are
`Checking`, `Electing`, `HostAlive`, `NewHost`, and `JoinNewHost`.

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

