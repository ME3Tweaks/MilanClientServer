# MilanServer

Server matchmaking component for Project Milan (Legendary Edition Multiplayer). Provides
stats, prioritization, and invites for connecting clients to each other.

## Build and test

Requires the .NET 10 SDK.

```
dotnet build MilanServer.slnx
dotnet test
```

## Run

```
dotnet run --project MilanServer -- --urls http://0.0.0.0:5080
```

Without `--urls` (or `ASPNETCORE_URLS`), the server listens on `localhost:5000` only. UDP
endpoint discovery listens on port 5081 (`UdpPort`). Clients learn that port from the server,
so any firewall or port mapping must keep the same number.

State is held in memory. After a restart, hosts register their lobbies again on their own.

## Configuration

Settings live in the `Milan` section. Sources, from lowest to highest priority:

1. `MilanServer/appsettings.json`
2. `config/milan.json` beside the server (optional, reloads while running)
3. Environment variables: `Milan__<Setting>`, nested with `__`, e.g. `Milan__Stats__Public=Summary`
4. Command-line arguments: `--Milan:<Setting>=value`, e.g. `--Milan:SearchSeconds=90`

Array settings take an index: `Milan__TrustedProxies__0=10.0.0.5`. Invalid values or
combinations will stop the server at startup with an error naming the setting.

### Access and network

| Setting | Default | Purpose |
| --- | --- | --- |
| `AccessKey` | empty | Server password. When set, every `/v1` request except `/v1/stats` must send it in `X-Milan-Access-Key`. Printable ASCII without spaces, at most 256 characters. Requires HTTPS to use |
| `TrustedProxies` | empty | Reverse proxy addresses or CIDR ranges whose forwarding headers are trusted. Loopback is always trusted |
| `UdpEnabled` | true | Run UDP endpoint discovery |
| `UdpPort` | 5081 | UDP discovery port |
| `MaxPlayersCeiling` | 16 | Largest lobby size the server accepts (1-16) |

### Game version and message of the day

These three settings also apply live from `config/milan.json`, with no restart:

| Setting | Default | Purpose |
| --- | --- | --- |
| `ExpectedGameVersion` | empty | Current mod version. Older games are told to update when a player reaches the multiplayer menu |
| `BlockedGameVersions` | empty | Exact versions are refused all matchmaking (`403 version_blocked`) |
| `Notice:Id` | empty | Message of the day ID. Each client shows a notice once per ID, change it to show the message again |
| `Notice:Text:<LANG>` | empty | Message text per game language (`INT`, `DEU`, `FRA`, `ITA`, `ESN`, `POL`, `RUS`, `JPN`), falling back to `INT`. Plain text, at most 1000 characters, `\n` for a new line |

```json
{
  "Milan": {
    "ExpectedGameVersion": "1.4.0",
    "BlockedGameVersions": [ "1.0.34" ],
    "Notice": {
      "Id": "maint-1109",
      "Text": {
        "INT": "Maintenance on Saturday at 18:00 UTC.",
        "DEU": "Wartung am Samstag um 18:00 UTC."
      }
    }
  }
}
```

An edit that does not parse is ignored with a warning, and the last good settings stay in
effect. Any other setting in this file is read only at startup. Environment variables and
command-line arguments override the file.

### Timings

| Setting | Default | Purpose |
| --- | --- | --- |
| `HeartbeatSeconds` | 20 | Interval hosts are asked to report state |
| `LobbyLeaseSeconds` | 60 | A lobby expires this long after its last report. Must exceed `HeartbeatSeconds` |
| `ReservationSeconds` | 30 | How long a joiner's reserved slot is held |
| `SearchSeconds` | 120 | Quick-match search length |
| `MapWidenSeconds` | 30 | Search widens to any map after this |
| `EnemyWidenSeconds` | 60 | Search widens to any enemy after this |
| `InProgressAfterSeconds` | 10 | Delay before matches in progress are offered, when open lobbies exist |
| `PreferredPartySize` | 4 | Quick match fills lobbies up to this many players first (0 turns this off) |
| `PartySizeWidenSeconds` | 15 | Delay before lobbies past `PreferredPartySize` are offered (none with `expandedLobbies: true`) |
| `MaxSearchLobbySize` | 8 | Lobbies with more slots than this are invite-only (0 means no limit) |
| `SearchPollMilliseconds` | 2000 | Search poll interval suggested to clients (at least 100) |
| `LongPollSeconds` | 25 | Longest a long poll waits (1-120) |
| `HostChallengeSeconds` | 5 | Time a host has to answer once a member reports it lost |
| `MigrationReportSeconds` | 8 | Time other members have to report a lost host |
| `MigrationJoinSeconds` | 90 | Time members have to join the new host |
| `DiscoveryMinIntervalMilliseconds` | 200 | Minimum gap between accepted UDP requests per ticket |

Search timings must satisfy `MapWidenSeconds <= EnemyWidenSeconds <= SearchSeconds`.

### Capacity and rate limits

Per-address limits group IPv6 clients by /64. CGNAT puts unrelated players behind one IPv4
address, so keep them generous.

| Setting | Default | Purpose |
| --- | --- | --- |
| `MaxLobbies` | 10000 | Total lobbies |
| `MaxSearches` | 10000 | Total active searches |
| `MaxLobbiesPerAddress` | 8 | Lobbies per address |
| `MaxSearchesPerAddress` | 8 | Searches per address |
| `MaxReservationsPerAddress` | 8 | Reservations per address |
| `RequestsPerMinute` | 600 | Requests per address |
| `CreatesPerMinute` | 30 | Lobby and search creations per address |
| `InviteRedeemsPerMinute` | 20 | Invite redemptions per address |
| `InviteCodeLength` | 10 | Characters in an invite code (3-10). Shorter codes are easier to type but easier to guess, so keep them longer on a public server. `MaxLobbies` may be at most half the possible codes (16384 for 3 characters). Codes of 6 or more are shown split, as `ABCDE-FGHJK` |
| `TotalRequestsPerSecond` | 1000 | Request rate across all addresses |
| `RequestBurst` | 2000 | Burst allowance for `TotalRequestsPerSecond` |
| `MaxConcurrentRequests` | 2048 | Active HTTP requests |
| `MaxConcurrentRequestsPerAddress` | 64 | Active HTTP requests per address |
| `MaxConcurrentConnections` | 4096 | Open connections |
| `MaxConcurrentLongPolls` | 1024 | Active long polls |
| `MaxConcurrentLongPollsPerAddress` | 32 | Active long polls per address |
| `DiscoveryPacketsPerSecond` | 5000 | UDP packets processed across all addresses |
| `DiscoveryPacketsPerSecondPerAddress` | 100 | UDP packets processed per address |

### Unreachable hosts

The server counts failed joins per lobby and offers lobbies players cannot reach last, or
not at all.

| Setting | Default | Purpose |
| --- | --- | --- |
| `Reachability:Enabled` | true | `false` keeps the counts but leaves quick match unchanged |
| `Reachability:SuspectAfterFailures` | 2 | Failed players before a lobby is offered after all others |
| `Reachability:UnreachableAfterFailures` | 4 | Failed players before a lobby is offered only when nothing else fits |
| `Reachability:ProbeSeconds` | 180 | Minimum time between offers to an unreachable lobby |
| `Reachability:FailureSeconds` | 900 | How long a failed join counts (at least 60) |
| `Reachability:JoinerFailureLimit` | 5 | Lobbies a player may fail on before their failures stop counting (player is the problem) |
| `Reachability:UnprovenPendingJoins` | 1 | Joiners still connecting that a lobby nobody has joined yet may have |

### Statistics

`GET /v1/stats` returns counts only, never anything that identifies a lobby or a player.
It is off until `Public` or `Token` is set. Counts reset when the server restarts. To keep lifetime stats,
your server should cache the results of this periodically.

| Setting | Default | Purpose |
| --- | --- | --- |
| `Stats:Public` | `None` | What anyone can read: `None` (404), `Summary`, or `Full` |
| `Stats:Token` | empty | Secret token, at least 16 characters, that always gets `Full`. Keep it out of game clients and web pages |
| `Stats:CacheSeconds` | 5 | How long a snapshot is served (0-3600) |
| `Stats:Presence` | false | Count running games that are in no lobby or search |
| `Stats:PresenceSeconds` | 300 | How often those games report (30-3600) |
| `Stats:MaxPresence` | 100000 | Presence reports kept in total |
| `Stats:MaxPresencePerAddress` | 16 | Presence reports kept per address |

### Restarts

| Setting | Default | Purpose |
| --- | --- | --- |
| `StateFile` | empty | File lobbies are saved to on a normal shutdown and restored from at startup, relative to the server's folder. Empty turns it off |
| `StateMaxAgeSeconds` | 120 | A state file older than this at startup is ignored (1-3600) |

### Other environment variables

| Variable | Purpose |
| --- | --- |
| `ASPNETCORE_URLS` | Listen addresses, e.g. `http://0.0.0.0:5080` |
| `ASPNETCORE_HTTP_PORTS` | HTTP port on all interfaces (the image sets 5080) |
| `Logging__LogLevel__Default` | Log level: `Trace`, `Debug`, `Information`, `Warning`, `Error` |
| `DOTNET_USE_POLLING_FILE_WATCHER` | `true` to poll for `config/milan.json` changes, needed on some mounted volumes |
| `MILAN_CPU_LIMIT` | CPU cap for `docker compose` (default 1.0) |

## Headers

| Header | Sent by | Purpose |
| --- | --- | --- |
| `X-Milan-Access-Key` | Client | The server password, when `AccessKey` is set |
| `Authorization: Bearer <token>` | Stats reader | `Stats:Token`, for full statistics |
| `X-Forwarded-For` | Reverse proxy | Client's real address. Used only from `TrustedProxies` |
| `X-Forwarded-Proto` | Reverse proxy | `https` when the proxy terminated TLS. Used only from `TrustedProxies` |

The server logs a warning naming any untrusted address that sends forwarding headers.

## HTTPS

Lobby keys and the server password are bearer secrets, so serve the API over TLS on the
Internet, through Kestrel certificate configuration or a reverse proxy (Caddy, nginx,
Traefik, HAProxy). Behind a proxy, bind the server to loopback
(`ASPNETCORE_URLS=http://127.0.0.1:5080`), keep TCP 5080 closed to the public, and leave
UDP 5081 direct. A proxy on another address must be listed in `TrustedProxies`; with
published container ports that is the Docker gateway (e.g. `172.17.0.1`).

With `AccessKey` set, plain HTTP `/v1` requests get `426 https_required`, even on a LAN.
Passwordless HTTP works for LAN and private-group setups. `/healthz` is always available
over HTTP.

## Publish

Publishing is trimmed and self-contained (single file, about 22 MB, no runtime needed on
the target):

```
dotnet publish MilanServer -c Release -r linux-x64 --self-contained -p:PublishSingleFile=true -o publish/linux-x64
dotnet publish MilanServer -c Release -r win-x64 --self-contained -p:PublishSingleFile=true -o publish/win-x64
```

Every JSON body type needs a `[JsonSerializable]` entry in `MilanJsonContext`
(`Contracts.cs`), or the trimmed server cannot serialize it.

## Docker

```
docker compose up -d --build
```

The image listens on TCP 5080 and UDP 5081, runs as a non-root user with no shell (use
`docker logs`), and takes any `Milan__<Setting>` environment variable. `compose.yaml`
mounts `./config` for `milan.json` and `./state` for `StateFile`. Create `./state` for the
container's user first: `mkdir -p state && sudo chown 1654:1654 state`.
`docker build --pull --target test .` runs the tests.

The server must see each player's real address and UDP port:

- **Linux:** published ports or `--network host` both work. Publish UDP on the same port
  number on both sides.
- **WSL2 (NAT mode):** only same-PC tests work, against the WSL address (`wsl hostname -I`).
  For LAN tests, set `networkingMode=mirrored` under `[wsl2]` in `%UserProfile%\.wslconfig`,
  run with `--network host`, and allow inbound traffic through the Hyper-V and Windows firewalls.
- **Docker Desktop:** every player appears as the gateway. Do not use it for NAT or LAN tests.

`build-images.sh` builds `linux/amd64` and `linux/arm64` images after running the tests:

```
./build-images.sh --version 1.2.0
./build-images.sh --version 1.2.0 --push ghcr.io/<owner>/milanserver --latest
```

Without `--push` it writes `dist/milanserver-<version>-linux-<arch>.tar.gz` for
`docker load`. Without `--version` the version is the current commit.

## License
MilanServer specifically is licensed as GPLv3.
