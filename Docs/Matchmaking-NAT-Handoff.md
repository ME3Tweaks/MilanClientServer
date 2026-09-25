# LMP matchmaking and NAT traversal investigation handoff

Last updated: 2026-09-25

Status: on branch `MilanServerBlaze`, step 1 (lobby state machine and HTTP API) is committed by the user. Step 2 server and MilanHost client work is staged but uncommitted. Quick Match menu integration is edited in MilanHost and ProjectMilan source. MilanHost DEBUG builds, MilanServer's 89 tests pass, and the two edited UnrealScript functions compiled into a scratch package. That package and the ASI are deployed to LMP. **The first same-PC two-instance Quick Match test succeeded:** the user reached the host's lobby, and server/ASI logs confirm search, reservation, direct-IP travel, authenticated probes, and joined reporting. This is local loopback proof, not public Internet NAT traversal.

## Current implementation handoff (2026-09-25, second agent)

### Quick Match menu follow-up (2026-09-25, current agent)

- Goal: run MilanServer and two LMP instances on one Windows PC; host normally in instance A, register it with `mm.host`, then use the **Quick Match menu option** in instance B. This tests server search, reservation, direct-IP travel, and the UE3 socket discovery/probe path on one machine. It does not establish Internet NAT traversal.
- `MilanHost/Components/CMatchmaking.*` now has an asynchronous `/v1/searches` flow. It sends map/enemy/difficulty filters from `USFXOnlineGameSettings`, polls while searching, consumes the returned reservation, and exposes the chosen `ip:port` through `GetResolvedConnectString`. Search cancellation invalidates pending callbacks, and a loopback discovery candidate is preferred when both instances use the local server. Invite-code joining still uses the same reservation parser.
- `MilanHost/Online/SFXOnlineComponentBlazeGame.cpp` now starts that search from `QuickMatch_Hook`, handles the optional settings pointer safely, dispatches the completion delegate on the game thread, returns the chosen address from `GetResolvedConnectString_Hook`, and cancels a pending search from `CancelFindOnlineGames_Hook`. The existing `JoinOnlineGame_Hook` still returns failure; the patched script uses the proven direct-IP `open` path instead.
- ProjectMilan source was edited separately at `B:\UserProfile\source\repos\ME3Tweaks\ProjectMilan\ProjectMilan\Pipelines\Shared\Compiling\ClassOverride\SFXGameInfoMP_Lobby.uc`: default Quick Match supplies map `-1`, enemy `127`, difficulty `127`; result 2 runs `open <resolved address>`; failure shows a matchmaking error instead of starting a new listen lobby. Changes use `MILAN CHANGE` markers. This source edit is **not in this repository**, and must be reviewed/committed in ProjectMilan separately.
- Verification: `dotnet test` from `MilanServer/` passes 89 tests. `Build/VisualStudio/DEBUG/MilanHost.asi` rebuilt successfully with the Visual Studio CMake tool. An LECCLI `compile-class` attempt against the old installed LMP `BIOP_MP_COMMON.pcc` parsed the whole script but stopped on six existing missing `GUI_SF_*` resource references at source lines 752-760; it reported no modified exports and saved nothing. A temporary patcher under ignored `Build/QuickMatchPatcher/` then used LegendaryExplorerCore `CompileFunction` on exactly `OnQuickMatchComplete` and `StartMPMatch`, saved a **scratch** package, and asserted those were the only changed exports. LECCLI decompiled both functions from that scratch package and confirmed the new code. This is package validation, not game runtime proof.
- The user authorized deployment to LMP and required removing ASIs whose **filenames contain `MilanHost`** before copying the new one. The LMP `Win64/ASI` folder contained `MilanHost.asi` and `LE3TextureOverride-v4.asi`; product metadata confirmed the latter is Texture Override. Removed the single name-matched ASI, copied the DEBUG build, and verified exactly one name-matched ASI remains and its SHA-256 equals the build (`EE5E2D59E1CC8EA7E655BECB86296D6A6733F1C8716A2E138897195AA618C741`).
- Backed up the original installed `BIOP_MP_COMMON.pcc` to ignored `Build/QuickMatchPatcher/BIOP_MP_COMMON.before-quickmatch.pcc` (SHA-256 `D045F10EFC236DA539E0ADEE438D2DABC54D9A4FDCBA9B9A74D246AA9D2498E8`). Copied the scratch package over the installed LMP package and verified its SHA-256 is `06E266E59EDBEEBF4EA25F98ECA331F1A99A7AD1222EEA7921BA0B16EDEE90B8`. This is a narrow two-function patch, **not a full ProjectMilan pipeline rebuild**.
- MilanServer is running as `dotnet` PID 4488 from `MilanServer/`, bound to `http://127.0.0.1:5080` and UDP 5081. `/healthz` returned `ok`; `/v1/info` returned protocol 1 and UDP port 5081. Logs are in ignored `Build/quickmatch-server.stdout.log` and `.stderr.log`. LMP instances were launched visibly as PIDs 64840 and 37992; both later exited normally (`BioRequestExit: bForce = FALSE, ExitCode = 0`).
- **Runtime result:** the user reported a successful join. Server log: created public lobby `KqukhjnHiISt2GGFW5JLDg`, then Quick Match matched it at stage `Exact`. Host log: UE3 game socket port 3659, UDP discovery observed `127.0.0.1:3659` on that same socket, one reservation, `NotifyControlMessage (Login)` from the joiner, and authenticated probes/path verification. Joiner log: Quick Match filters `map -1, enemy 127, difficulty 127`; selected `127.0.0.1:3659 (PublicObserved, same public address: 1)`; `GetResolvedConnectString` was called; UE3 game socket port 57979 was observed unchanged by discovery; probes were verified; `joined the host's game; releasing the reservation` was logged. Actor channels including `SFXGRIMP_Lobby` were created, which supports actual game replication rather than just a control-plane match.
- The user noticed a slight pause when “searching for game” appeared and suggested disabled movies. The joiner log shows the Quick Match request at 49.859s and chosen target at 49.927s (about 68 ms). The pause is **not explained by server search latency** in this run; no UI/render cause has been established. Do not claim a fix without a targeted repeat if it remains noticeable.
- Both ASI logs reported a nonfatal `failed to read config: File could not be opened for reading`: there is no `Win64/MilanHost.toml`, so matchmaking used its compiled localhost URL default. The test still succeeded. The server later logged expiration of the now-closed host lobby after missed heartbeats (60-second lease). The server remains running for a repeat test; check PID/health before starting another copy.

### Branch and repository decisions

- The user chose a new branch, **`MilanServerBlaze`** (cut from `main` at `9fa4dbc`), for this work. **Do not use `origin/server`.** The user says it is old. That branch has an unrelated `MilanServer/` .NET solution (Milan.Core/Milan.GameServer, framed TCP protocol, EF Core and account models) by other contributors. The README on `main` still calls `server` the primary branch; that note is stale for this work.
- The user reviews and commits changes themselves; agents only stage files and suggest a commit message. Step 1 is committed; the step 2 server-side changes are not. The unrelated working-tree changes are untouched: `Patching/_index.list`, untracked `Patching/*.diff`, and the `ThirdParty/LExSDKv2` submodule. Do not stage or reset them. `git` may need `-c safe.directory='B:/UserProfile/source/repos/ME3Tweaks/MilanClientServer'`.
- The user confirmed that map, enemy, and difficulty IDs are **not fixed**: new ones may be added without a server update, and they do not change the logic. The server therefore treats IDs as opaque integers. Only the search sentinels (map `-1`, enemy/difficulty `127`) are special, and hosted states may not use them. Matching compares selected IDs by equality. That alone makes a Random map lobby (ID 0) fail a specific-map search, and makes Random Faction/Coalition/Unified match by mode ID.

### What exists (source evidence plus unit tests only)

`MilanServer/` layout: `MilanServer.slnx`, `global.json` (opts `dotnet test` into Microsoft.Testing.Platform, which the .NET 10 SDK requires for xunit v3), `MilanServer/` (ASP.NET Core minimal API, `net10.0`, warnings as errors), `MilanServer.Tests/` (xunit v3, `Microsoft.AspNetCore.Mvc.Testing`, `FakeTimeProvider`), and `README.md` with run, publish, and API details.

- `MatchmakingStore.cs`: all lobby, search, and reservation state behind **one lock**. That makes host reports, search decisions, and capacity reservations atomic. Implemented behavior:
  - Create, report, and delete lobbies, plus invite rotation.
  - Full-snapshot reports with a host-chosen monotonic revision. An older revision returns `stale: true` and is ignored. The same revision with the same content is a heartbeat; with different content it is `revision_conflict`.
  - Lease expiry: 60 s after the last authenticated report (lazy checks plus a 5 s sweeper).
  - Quick match: `Any` is a wildcard; exact → any map at 30 s → any enemy at 60 s; `NoMatch` at 120 s. Difficulty is held throughout, `widen:false` disables widening, and no lobby is ever auto-created.
  - Search exclusions: private lobbies, extraction/complete phases, non-joinable lobbies, full lobbies, other `GameVersion`s, and client-supplied `ExcludeLobbyIds` (for retrying after a failed join).
  - Invite redemption works for private lobbies and during extraction.
  - Capacity reservations: 30 s, cancellable. A joiner's `joined` report keeps the slot until the host's next non-stale report.
  - Per-address caps: IPv6 is grouped by /64.
- Ranking is one tuple in `Rank()`: lobby phase before in-progress; then more of the player's original filters matched (only matters after widening); earlier wave; more players; older lobby. The handoff did not settle filter-closeness versus wave order. Confirm it with the user if it matters.
- Capabilities: host, search, and join keys are separate 256-bit bearer secrets, and the server stores only SHA-256 hashes. Invite codes are 10 Crockford base32 characters, displayed as `XXXXX-XXXXX` and accepted case-insensitively with O→0 and I/L→1. The server never logs keys or invite codes. A wrong key and a missing object return the same 404.
- Endpoint candidates in a reservation:
  - Host-reported LAN addresses: at most 4, and only 10/8, 172.16/12, 192.168/16, 169.254/16, or 100.64/10, so a host cannot aim joiners' probes at public third parties.
  - `PublicReported`: the host's HTTP source IPv4 plus its reported `GamePort`. This works only with a manual or router-mapped forward of that port.
  - `SamePublicAddress`: a locality hint only.

  There is **no observed public endpoint yet**. If the host reaches the server over IPv6, no public candidate is produced, because the UE3 socket is IPv4.
- `Program.cs` and `Api.cs`:
  - Per-address fixed-window rate limits: global, create, and invite redeem.
  - 16 KB body limit; malformed JSON returns a `ServerError` 400.
  - Forwarded headers only from configured `TrustedProxies`.
  - `/healthz` and `/v1/info`.
  - Option validation at startup.
- Verification: `dotnet test` passes 69 tests. Two deliberate mutations were caught: removing the private-lobby filter and removing the joined-reservation release. A manual Kestrel smoke run checked create/redeem, 413 on oversize bodies, and real remote-address capture. A self-contained `linux-x64` single-file publish succeeded. No Docker image was built.

### Step 2 server side: discovery and introductions (source plus tests only)

- `Discovery.cs` defines UDP discovery v1. A binding request is 64 bytes: magic `MLNR`, version, type, a 16-byte ticket ID, a u64 sequence, zero padding, and a 16-byte truncated HMAC-SHA256 tag. The 56-byte response echoes the ticket and sequence and gives the observed IPv4 address and port, with its own tag. Responses are smaller than requests.
- The server replies only to authenticated requests with a sequence above the last accepted one, at most once per 200 ms per ticket, and only while the ticket's lobby or reservation is live. Everything else is dropped silently, and nothing is ever forwarded. The listener is IPv4-only, on `Milan:UdpPort` (default 5081, reported in `/v1/info`), and binds before startup completes.
- Tickets: the lobby gets a host ticket at creation, and each reservation gets a joiner ticket plus a `traversalKey` for peer probes. The host receives the `traversalKey` in its introduction; the server never sees peer probes.
- Host candidates for joiners are ordered `PublicObserved`, then `PublicReported` (only if different), then `Lan`. Joiner candidates for the host are `PublicObserved` from the joiner's binding, plus `Lan` from `PUT /v1/reservations/{id}/candidates`. The joiner can only report LAN candidates once its game socket and port exist. `SamePublicAddress` prefers observed addresses.
- Long-polls:
  - `POST /v1/lobbies/{id}/introductions/poll?after=N` (host key) returns every live reservation.
  - `POST /v1/reservations/{id}/poll?after=N` (join key) returns current host candidates.
  - Both wait up to 25 s on a per-lobby event version. It is bumped by reservation add/remove/joined, candidate or observed-endpoint changes, host address changes, and state content changes. Same-content heartbeats and lobby removal (which wakes waiters into a 404) are handled.
- Verification: 89 tests pass, including a real loopback UDP round trip through the hosted service and an HTTP long-poll woken by an invite redemption. Deliberately removing replay protection, and separately removing the reservation wake-up, each failed the tests.

**Proposed connection sequence for the client work (not implemented or validated):**
1. The host creates a lobby.
2. The host sends a binding request from the **UE3 game socket** about every 20 s, and long-polls introductions.
3. The joiner redeems an invite or matches a search.
4. Once the joiner's game socket exists, it sends its own binding request and reports its port and LAN addresses.
5. Both sides send authenticated probes with the `traversalKey` to each other's candidates for a few seconds: LAN first when `SamePublicAddress`, observed public otherwise.
6. On the first verified probe response, the joiner starts the UE3 connection to that endpoint, then reports `joined` after admission.
7. The host clears finished introductions when their reservations disappear.

The peer probe packet format is still undesigned. Reuse the discovery layout style: a distinct magic, fixed length, and an HMAC keyed by `traversalKey` that binds the reservation ID and direction. The open problems in "Same-NAT topology" and "Working hypothesis" below still decide whether steps 2 and 4 are possible on the real game socket.

### MilanHost client, first cut (builds; not yet run in the game)

Console-driven, so the socket boundary can be proven before any menu integration. Files:
- `MilanHost/Components/CMatchmaking.*`: the state machine, two worker threads, and the `mm.*` commands.
- `MilanHost/Online/Rendezvous.*`: MLNR discovery and MLNP probe packets.
- `MilanHost/Online/MatchmakingNet.*`: WinHTTP, BCrypt HMAC, GetAdaptersAddresses, and getsockname. It is compiled with `SKIP_PRECOMPILE_HEADERS` because the SDK PCH's `<Windows.h>` pulls in Winsock 1.
- `ThirdParty/nlohmann/json.hpp`: nlohmann/json 3.12.0, vendored at the user's choice.

Config key `milan.matchmaker.url` is optional, default `http://127.0.0.1:5080` (see `Docs/DefaultMilanHost.toml`).

- **Socket lookup (static offsets, validated at runtime by getsockname):** the pending level's net driver, else `GWorld->NetDriver`. It must be `IsA(UTcpNetDriver)`. The `FSocket*` is at driver `+0x1F8`, and the `SOCKET` at `FSocket+0x1C`. `mm.status` prints the driver class and whether the socket was found. If the live driver is not a `UTcpNetDriver` subclass (see the earlier `SFXOnlineNetDriver` question), this is the first thing to check.
- **Send path:** discovery requests and probes go through the original `FSocketWin::SendTo`, on the game thread (from the `UGameEngine::Tick` hook).
- **Receive path:** the `FSocketWin::RecvFrom` hook now loops. It pulls out any datagram that `Rendezvous::Classify` recognizes (exact length plus magic/version/type), then reads the next one, so UE3 sees the final call's result and Winsock error unchanged. After 256 consumed datagrams it returns WSAEWOULDBLOCK for the frame. State is only touched on the game thread. `FSocketWin::SendTo` and `RecvFrom` are now installed in all builds; NETPROF calls inside them remain non-final only. Both hooks are uninstalled in `UninstallSomeHooks` before the component is torn down.
- **Host (`mm.host` after hosting from the menu):** `/v1/info`, then create the lobby, then discovery (1 s retries, then every 20 s). It sends full state when it changes (checked every 250 ms) or as the heartbeat. It long-polls introductions and probes each joiner's candidates for 15 s after they change. **The lobby state is a placeholder:** map, enemy, and difficulty are 0, max players is 4, and players are counted as 1 + `ChildConnections`.
- **Joiner (`mm.join <code>`):**
  1. Redeems the invite and picks one target: LAN if `samePublicAddress`, otherwise `PublicObserved`, then `PublicReported`.
  2. Runs `open ip:port` through the first local player's `ConsoleCommand`.
  3. Once UE3's socket exists, sends discovery and reports its port and LAN addresses, then probes the host.
  4. Reports `joined` when the pending level is gone and `GWorld->NetDriver->ServerConnection` exists.

  UE3 connects to one address only. If a probe verifies a different endpoint than the one `open` used, the likely fix is to rewrite the pending `UTcpipConnection::Address` (`+0x606C`) on the game thread. That is not implemented.
- **Verification so far:**
  - DEBUG builds warning-free.
  - FINAL fails only on the pre-existing `CConstants.cpp(101)` C4100.
  - A throwaway harness (scratchpad, not committed) compiled `Rendezvous.cpp` and `MatchmakingNet.cpp` at `/W4 /WX` and passed against a live MilanServer: WinHTTP and JSON, discovery HMAC and observed port, tamper rejection, introductions, and probe authentication.
  - **No in-game run yet.**

**Local two-instance Quick Match test (completed successfully on 2026-09-25; repeatable procedure):**
1. Run MilanServer from `MilanServer/` with `dotnet run --project MilanServer -- --urls http://127.0.0.1:5080` (UDP 5081). Check `http://127.0.0.1:5080/healthz` and `/v1/info`.
2. On instance A: host a **public** match from the menu, then run `mm.status` and `mm.host`. Expect a `UTcpNetDriver` socket at local port N, `lobby registered`, and `discovery: server saw 127.0.0.1:N ... (same port)`. Keep this instance in the lobby.
3. On instance B: choose **Quick Match** from the menu (not `mm.join`). Expect `quick match search: map -1, enemy 127, difficulty 127`, a server match/reservation, a `host candidate 127.0.0.1:N`, then `joining via 127.0.0.1:N (PublicObserved, same public address: 1)`. `GetResolvedConnectString` should return that address and the script should issue `open 127.0.0.1:N`.
4. Confirm B appears in A's lobby. Run `mm.status` in each instance and capture the MilanHost logs. Look for new game socket/discovery, authenticated `probe received` and `path verified`, and `joined the host's game`. Check UE3 has no spurious connections from the traversal datagrams.
5. If Quick Match reports no lobby, check A's `mm.status`, the lobby registration and discovery logs, public/private state, exact `gameVersion`, and server search logs. If B starts its own lobby, inspect the **installed** `BIOP_MP_COMMON.pcc` bytecode for the updated `OnQuickMatchComplete`; source alone is insufficient.

This proves the socket boundary and loopback path on one machine, not NAT traversal. The older `mm.join <code>` test remains useful as a control if Quick Match fails.

### LMP script changes that affect matchmaking (2026-09-25)

**Sources:**
- ProjectMilan source: `Pipelines/Shared/Compiling/ClassOverride/SFXGameInfoMP_Lobby.uc`, `SFXLobbyFlow.uc`, `Mod/BaseDelta/SFXOnlineFoundation/Classes/SFXOnlineSubsystem.uc`, and `SFXOnlineGameSettings.uc`.
- Checked against the **installed** package with LECCLI: `LMP/BioGame/DLC/DLC_MOD_ProjectMilan/CookedPCConsole/BIOP_MP_COMMON.pcc` (written 2026-09-23). Its classes live under `SFXGameMPContent.*`. The decompiled `OnMPGameStarted`, `OnQuickMatchComplete`, and `CreateOnlineGame` match the source.
- **Compared with original ME3 (2012 PC) using LECCLI.** `list-game-paths` gives the install as `B:\SteamLibrary\steamapps\common\Mass Effect 3`. The comparison used `BIOGame/CookedPCConsole/BIOP_MP_COMMON.pcc` and `SFXOnlineFoundation.pcc` from there, against LMP's pre-Quick-Match package backup (`Build/QuickMatchPatcher/BIOP_MP_COMMON.before-quickmatch.pcc`) and LMP's base `SFXOnlineFoundation.pcc`. Whole classes were decompiled and compared function by function, ignoring whitespace:
  - Lobby and online classes checked: `SFXGameInfoMP_Lobby`, `SFXLobbyFlow`, `SFXGRIMP_Lobby`, `SFXGameInfoMP`, `SFXPlayerControllerMP`, `SFXOnlineComponentBlazeGame`, `SFXOnlineSubsystem`, `SFXOnlineGameSettings`, `SFXOnlineComponentGameFlow`, `ISFXOnlineComponentGame`.
  - Only LMP's lobby changes touch the join path.
  - The `SFXOnlineFoundation` differences are harmless for matchmaking:
    - extra logging and `if (TRUE)` debug blocks, since LMP's base is a later build (it has Dingo/Orbis components);
    - the privacy enum;
    - 127 "any" sentinels.
  - `SFXLobbyFlow` changes are the privacy enum, the kicked-out message, MPSaveManager initialization, and the challenge screens.

**Findings:**
- **Original ME3 routed both hosting and joining through `GetResolvedConnectString`.** `OnMPGameStarted` called it and appended `?origin=MultiplayerMenu?private=..?map=..?enemy=..?difficulty=..`. Blaze returned `MPLobby?listen` for a new game or the host address for a joined one. Original `OnQuickMatchComplete` was the same for every result:
  - 2 (start game) read the address, then called `OnMPGameStarted(TRUE)`, which joined through that same resolved string.
  - 3 retried.
  - Any other result called `OnMPGameStarted(Result != 4)`, so a search that ended in a created game made the player the host.
- **Hosting ignores the online subsystem** (this is what makes solo and local play work, and it should stay):
  - `CreateOnlineGame` short-circuits with `if (TRUE) { OnCreateGameComplete('Game', TRUE); }`.
  - `OnMPGameStarted` always runs `open mplobby?listen?origin=MultiplayerMenu?privacy=..?map=..?enemy=..?difficulty=..`. That is the only reason the unpatched Quick Match hosted instead of joining.
  - `StartMPMatch` drops the `CanPlayOnline()` check and sets `BlazeGame.GameSettings` itself before `CreateOnlineGame`.
  - The matchmaker can register the lobby when that listen server comes up. The URL options, then the live settings, can supply the real lobby state that `CMatchmaking` currently fakes.
- **Quick Match (the narrow two-function patch, now deployed):**
  - `OnQuickMatchComplete(2)` runs `open <GetResolvedConnectString>` directly, leaving `OnMPGameStarted` host-only.
  - Every result other than 2 and 3 shows `srMatchMakingError`. ME3 would have hosted instead. MilanHost only ever sends 2 or 4, so a player without a reachable matchmaker gets an error, and hosting is untouched.
  - `StartMPMatch` with no settings now searches with map -1, enemy 127, difficulty 127.
  - `StartCustomMatch`, the filtered search, already sends public settings with `mCreateNewMatch = FALSE`, so it reaches the same `QuickMatch_Hook` with the chosen filters.
  - `JoinOnlineGame_Hook` still returns failure, but nothing on this path calls it.
- **Privacy is now three-way:** `EMatchPrivacySetting` is `PRIVACY_PUBLIC` (0), `PRIVACY_PRIVATE` (1), and `PRIVACY_FRIENDS` (2). It replaces ME3's `bool`, and `GRI.PrivacySetting` is an `int`. `SetPrivacy` sets `NumPublicConnections` to 0 only for `PRIVACY_PRIVATE`. The accountless server has no friends concept, so FRIENDS probably maps to an unlisted, invite-code-only lobby.
- **Joinable signal:** `CommitSettings` (lobby countdown, before `servertravel`) calls `ChangeMatchSettings(..., bAllowMatchmaking = FALSE)`, and `UnCommitSettings` passes TRUE. MilanHost's `AllowMatchmaking_Hook` already stores this in `BlazeGame.bAllowMatchmaking`. `ChangeMatchSettings` also calls the hijacked `UpdateOnlineGame('Game', NewSettings)` with the new map, enemy, difficulty, and privacy, so both hooks can drive prompt lobby-state reports without reading the `SFXGRIMP_Lobby` GRI. That class is LMP script and is not in the LE3 SDK.
- **The private and local flow is unaffected** as long as matchmaking stays opt-in. `mm.join` / Discord invites use a plain `open ip:port`, which the user reports works, and it bypasses these delegates entirely.
- **Useful hooks for host updates:**
  - `ChangeMatchSettings` already calls `GetComponentGame().AllowMatchmaking(bAllow, FALSE)`, which MilanHost hijacks. That is a natural trigger for prompt state reports.
  - `SFXOnlineGameSettings.SetPrivacy` sets `NumPublicConnections` to 0 when private.
  - `SFXOnlineSubsystem` builds `GameProtocolVersion = m_IsolatedMatchMakingCode $ GetBuildUniqueId() $ GetBioStaticBuildVersion()`. This is a better `gameVersion` for the matchmaker than the ASI version, because it matches content builds.

### Known limits and open design points

- The host learns about joiners only by long-polling. Consider a WebSocket if polling proves too slow or heavy in practice.
- Reservation accounting is best effort. A heartbeat that was in flight before an admission can release a `joined` slot slightly early. The game still enforces its own cap.
- Accountless griefing from many addresses (holding public slots through repeated searches) is limited only by per-address and rate limits.
- Invite codes change when a host re-creates its lobby after a server restart. Allowing clients to choose codes would let other clients claim codes that belong to someone else.
- 100.64/10 is accepted as a LAN candidate to support overlay VPNs such as Tailscale. It is also carrier CGNAT space. Revisit if that is a concern.
- Search is a linear scan under the lock. That is fine for expected LMP scale; index it if lobby counts grow large.

### Agreed matchmaking behavior

- A player creates and hosts a lobby. Public lobbies and in-progress matches with room are searchable; private lobbies are excluded from public search. An invite code grants a join attempt for either type, including during extraction, subject to capacity and joinability. PC Friends privacy is ignored.
- The server tracks at least selected map, selected enemy mode, difficulty, privacy, current human player count, configured capacity, phase, and wave. Capacity defaults to 4 and the server ceiling is 16. This ceiling is **not** evidence that a 16-player LMP game works; game-side caps, spawn points, map presence, and UI still need work and validation.
- Host setting changes must be reported promptly, independent of the roughly 20-second heartbeat. The proposed starting point is a full state snapshot on change, coalescing rapid noncritical edits over about 200 ms, while privacy/capacity/joinability changes go immediately. Use a monotonic revision so older updates cannot overwrite newer state. The heartbeat should also reconcile full state. Exact transport and timings are design proposals, not tested behavior.
- Quick match uses the game's map, enemy, and difficulty filters. `Any` on each filter means a wildcard immediately. For a specific selection, hold difficulty fixed; search exact map and enemy for 30 seconds, then any map with the selected enemy for 30 seconds, then any map and enemy for 60 seconds. Stop after two minutes with **no match found**; do not automatically create a lobby. A future setting will let the player disable widening.
- Search should prefer lobbies and earlier waves over later waves. Exclude extraction from automatic matchmaking, while allowing invite-code joins during extraction. Private matches never appear in search.
- A specific enemy search matches the selected enemy **mode ID**, not the currently active faction in a Coalition wave. A Random map lobby does not satisfy a specific map search. Preserve selected random mode separately from its resolved map/faction after match start.
- The first release may expire a lobby after host loss. Host migration and secure transfer of lobby ownership to the new host are critical follow-up requirements; do not claim that LMP already handles this. A 20-second heartbeat and roughly 60-second expiry were suggested starting values.

### Game-setting evidence to retain

ProjectMilan source currently uses `SFXGRIMP_Lobby.EnemySetting` / `SFXOnlineGameSettings.EnemyType` as a numeric `MPEnemyInfo.Id`. In `ProjectMilan/Pipelines/Levels/MPLevels.cs`, ID 0 is Random Faction, 1 is Coalition (random faction per wave), 2 is Unified (mixed enemies), and 3-6 are specific factions. `MPEnemyInfo.WaveClass` maps a specific ID to a wave class. Search `Any` sentinels in `SFXOnlineGameSettings.EnsureMatchSettingsAreValid` are map `-1`, enemy `127`, and challenge/difficulty `127`; hosted Random map/enemy settings use `0`. `SFXGameInfoMP_Lobby.CommitSettings` resolves Random Faction to a concrete ID and leaves `bRandomEnemy` set. These are source observations, not installed-package or live proof. Verify the installed build and actual menu values before cementing the wire protocol.

The current development `SFXGameInfoMP_Lobby.CreateOnlineGame` bypasses a real online session, `OnMPGameStarted` builds a local `mplobby?listen` travel command, and the MilanHost Blaze `JoinOnlineGame` hook reports failure. Quick match and invite travel therefore require client/game integration, not just a server.

### Same-NAT topology required in validation

Test two clients in one household joining a remote host as **two distinct players**. They share a public IP but should have distinct game-socket endpoint mappings and join tickets; never key players or slots by public IP alone. Also test one household member hosting while the other joins over the LAN and two remote players join through the public endpoint. Exchange and probe local and public candidates with authenticated packets; prefer a proven LAN path for the local housemate so router hairpin support is not required. Same public IP is only a hint of locality because CGNAT can make unrelated households share one address. Verify in Ghidra and live LMP that the host distinguishes two connections with the same IP and different source ports. `UTcpipConnection.Address` is a `SOCKADDR_IN` including port in the SDK, but that alone is not runtime proof of receive-path routing.

### Next agent's immediate steps

1. **Done and committed by the user (unit-tested only):** lobby state machine and v1 HTTP API, described above. The user commits changes themselves; stage only and suggest a message. The contracts are still provisional; bump `Api.ProtocolVersion` on breaking changes once a client exists.
2. **Same-PC Quick Match passed with deployed MilanHost and a narrowly patched ProjectMilan package.** The equal observed/local UDP ports prove discovery originated from each live UE3 game socket on this loopback test. Peer probes were authenticated and the game join completed. Next, verify distinct-machine LAN and real Internet NAT paths, including whether UE3's first connection packets reach the host before/after the punch. Do not use a separate preliminary UDP socket as proof that the game socket mapping works.
3. Add Windows/Linux packaging and container documentation. A single .NET server instance with ephemeral state is a reasonable first deployment; account for host re-registration after a restart. AWS UDP ingress likely needs a Network Load Balancer if using ECS Fargate. Keep independent hosting possible.
4. Integrate MilanHost and the Discord ASI only after the socket and menu-flow boundaries are proven. The Discord ASI source is outside this repository at `B:\UserProfile\source\repos\LExASIs\DiscordIntegration`; it was inspected but not changed. Preserve the ability to run LMP without Discord for direct/invite-code use.
5. Run real LMP tests: one LAN host and join, two distinct consumer NATs, both same-house topologies above, failed hairpin, rapid setting changes during a search, full lobby, extraction, host crash/expiry, and game travel/socket recreation. Label source/static evidence separately from runtime proof.

Update this section after each substantive implementation or runtime milestone. The user explicitly requested a durable running handoff for multiple agents.

## Objective and scope

Make player-hosted LMP matches joinable over the Internet without asking users to forward ports manually. Keep UE3's existing multiplayer protocol and replication. A small, independently hostable matchmaking server should provide rendezvous; Discord's Game/Social SDK should initially carry an invite secret that the client redeems with that server. No central user accounts are required. Public matchmaking search is now required, alongside private code-only lobbies. The server should normally leave the gameplay path after introduction. Evaluate direct connectivity, router port mapping, UDP hole punching, and eventually a relay fallback.

The original user handoff is at `C:\Users\mgame\.codex\attachments\8168f8d7-b116-4bbb-8ec0-0f143b3f6b41\Pasted text.txt`. Implementation was authorized on 2026-09-25, then explicitly paused for agent handoff. Ask targeted questions where desired behavior remains undecided, and preserve the distinction between static evidence and runtime proof.

Requirements carried forward from that handoff:

- The matchmaker should be small, primarily Linux hosted, convenient as a single binary, and also runnable by independent communities or private groups with the same protocol. It should need little or no persistent state and no central account provider.
- Separate the matchmaker's control connection from UE3 gameplay. A normal TLS control transport is acceptable; the matchmaker should not need to understand or relay ordinary game packets when a direct path works.
- Evaluate existing/manual forwards, UPnP IGD, NAT-PMP, PCP, public endpoint discovery, coordinated UDP hole punching, and relay fallback. Traversal packets should ideally use the exact UE3 gameplay socket and local port.
- Use bounded, unambiguous, authenticated traversal messages. Accountless lobby/invite capabilities should resist replay, spoofing, enumeration, spam, reflection, and resource exhaustion. A self-hosted matchmaker must not become an open arbitrary UDP relay.
- Account for disabled port mapping, CGNAT, difficult NAT pairs, IPv6, a manually forwarded host, server restart/outage, peer network changes, socket recreation, and a successful probe followed by failed UE3 traffic.
- The first useful prototype should answer whether two player-hosted LMP peers behind ordinary separate consumer NAT routers can complete a native UE3 session without manual forwarding. The original handoff requests a socket lifecycle diagram, concrete connection sequence, security analysis, and staged implementation recommendation after the research supports them.

The user reports that LMP's `MassEffectMultiplayer.exe` is byte-for-byte identical to LE3 `MassEffect3.exe` 11.3.2 on disk and is modified by MilanHost at runtime. This has not been independently hash-verified in this investigation. The Ghidra program used here is `/Legendary/MassEffect3.exe-11.3.2` in the open project. Addresses below are virtual addresses in that image, not RVAs. Check the actual module base before using any address in an ASI.

## What already works, according to the user

- A player can start hosting in LMP through the normal in-game menu.
- Another LMP instance can join that host on the same LAN with console `open <IP>`. This is a reported working game session, not a connection produced by the proposed matchmaker.
- The user has an older debugger screenshot showing `TcpNetDriver` as `UWorld->NetDriver` on the host. There is no fresh socket log yet. The user does not launch with `-tcpip`.
- The eventual implementation targets LMP. The initial native analysis uses the byte-identical-on-disk LE3 11.3.2 executable, but MilanHost's runtime effects still have to be checked.

## Evidence ledger

| Finding | Evidence | Limit |
| --- | --- | --- |
| Host and client both choose a net driver through `SelectNetDriverClassForTravel` at `0x7ff74b21c550`. | Ghidra callers: `UWorld::Listen` at `0x7ff74b153970` and `UNetPendingLevel` constructor at `0x7ff74aee7dd8`. | The active component and class selected in a running Milan host have not been freshly observed. |
| The selector loads `SFXOnlineFoundation.SFXOnlineNetDriver` when an online game interface is active, `-tcpip` is absent, and its API name is `Blaze`; otherwise it loads `engine-ini:Engine.Engine.NetworkDevice`. | Ghidra decompilation at `0x7ff74b21c550`. | The user's lack of `-tcpip` alone does not establish which branch runs. |
| LMP `SFXOnlineComponentBlazeGame.GetAPIName` is native; the LE3 Blaze component implementation returns the name `Blaze`. | Read-only LECCLI decompilation of installed LMP `SFXOnlineFoundation.pcc`, function export 1402; LE3 Ghidra `USFXOnlineComponentBlaze::VirtFunc_88` at `0x7ff74b1dc7b0`. | Package declaration plus static native code do not prove the component is active at the moment a match starts. |
| In the unmodified LE3 image, `SFXOnlineNetDriver` vtable slot 92 calls common setup and returns `0`; the host's `UWorld::Listen` treats a zero return as initialization failure. Its slot 94 is not the UDP receive loop. | Ghidra `0x7ff74b21d3f0`, `0x7ff74b223a20`, `0x7ff74b153970`. | A working LMP menu host suggests the live path uses a different driver or runtime changes; do not treat this as a demonstrated LMP hosting failure. |
| The plain `UTcpNetDriver` path creates an IPv4 UDP socket (`AF_INET`, `SOCK_DGRAM`, `IPPROTO_UDP`), wraps it as `FSocketWin`, configures it nonblocking, and binds it. Host uses its URL port; client uses port `0`. | Ghidra `WinSock_CreateIPv4UdpSocket` at `0x7ff74b190150` and `UTcpNetDriver_InitUdpSocketAndBind` at `0x7ff74b193120`. | The actual port and whether any socket is recreated are runtime questions. This specific path is IPv4. |
| Default URL port comes from command-line `Port=` or `[URL] Port` in `GEngineIni`; explicit URL ports can override it. | Ghidra `FURL::StaticInit` at `0x7ff74b136130` and URL construction. | No deployed value was measured in this pass. |
| `FSocketWin::SendTo` and `RecvFrom` directly call Winsock `sendto`/`recvfrom` on the handle at wrapper offset `+0x1c`. `UTcpNetDriver::TickDispatch` drains that socket into a `0x240`-byte receive buffer, then routes or accepts packets. | Ghidra `0x7ff74b199f30`, `0x7ff74b198520`, `0x7ff74b19cc30`. | Static call path only. A separate worker that receives from this socket could steal UE3 packets. |
| `FSocketWin` destruction closes the handle. Driver cleanup releases its socket wrapper at driver offset `+0x1f8`. | Ghidra `0x7ff74b18d2c0`, `0x7ff74b1948a0`. | Observe travel and teardown before caching a handle long term. |
| The binary contains UPnP discovery code and a `-noupnp` switch. Its embedded UPnP initialization creates a separate datagram socket. | Ghidra strings and `0x7ff74b6369a0`, `0x7ff74b651db0`. | No proof yet that it requests a mapping for the UE3 gameplay port, or that MilanHost uses it. |

## Relevant project state

- [MilanHost/Online/SFXOnlineComponentBlazeGame.cpp](../MilanHost/Online/SFXOnlineComponentBlazeGame.cpp) currently has a `CreateOnlineGame_Hook` that reports success locally, a `JoinOnlineGame_Hook` that returns failure, and a `GetResolvedConnectString_Hook` that returns `MPLobby?listen`. These source hooks do not account for the reported working console direct-IP join; they describe the online lobby path, not all travel paths. Check the deployed ASI before drawing runtime conclusions from source.
- [MilanHost/Components/CGlobals.cpp](../MilanHost/Components/CGlobals.cpp) installs `FSocketWin::SendTo`, `Send`, `RecvFrom`, and `Recv` hooks in non-final builds. [MilanHost/Engine/Hooks.cpp](../MilanHost/Engine/Hooks.cpp) has the handlers; verbose endpoint logs are commented out, while network-profiler capture remains. This is an existing narrow diagnostic foothold for a later authorized instrumentation pass.
- The Discord ASI source is `B:\UserProfile\source\repos\LExASIs\DiscordIntegration`. Its `Discord.cpp` currently publishes Rich Presence without party/join secrets or invite acceptance wiring. The vendored `discordpp.h` provides `ActivityParty`, `ActivitySecrets::Join`, invite callbacks, and activity join callbacks. Inspect the actual vendored version when implementing; do not assume a current online SDK version matches it.
- Installed package inspected with compiled LECCLI: `S:\SteamLibrary\steamapps\common\Mass Effect Legendary Edition\Game\LMP\BioGame\CookedPCConsole\SFXOnlineFoundation.pcc`. The LECCLI session was ended; file ID 1 from that session is no longer valid. Do not save package data.
- The repository already had unrelated dirty files when this investigation began. No repository source file was edited in the first research pass. The first implementation turn added a provisional `MilanServer` skeleton. The second agent moved it to `MilanServer/MilanServer/` and implemented step 1 on branch `MilanServerBlaze` (see above). No MilanHost or Discord ASI source has been changed.

## Ghidra annotations from the first pass

The open LE3 program received four names (`SelectNetDriverClassForTravel`, `UTcpNetDriver_InitUdpSocketAndBind`, `WinSock_CreateIPv4UdpSocket`, `FSocketWin_Bind`) and thirteen comments at the locations in the evidence ledger. At the end of the pass, `currentProgram.isChanged()` was `true`: **the annotations were still unsaved**. A bridge script's `currentProgram.save(...)` attempt failed with `Unable to lock due to active transaction`. Save the LE3 program in the Ghidra UI before closing it, then verify the names remain. Do not claim they were persisted merely because the MCP mutation calls succeeded.

## Working hypothesis, not yet a design commitment

```text
Discord invite secret
        |
        v
selected independent matchmaker  <---- TLS/control ---->  MilanHost on each peer
        |                                                  |
        | endpoint introduction                            | same UE3 UDP socket
        v                                                  v
 host and joining peer  <--------- UE3 native UDP game traffic --------->
```

The invite secret should identify a short-lived matchmaker session. The matchmaker need not carry normal game packets when a direct route works. A relay is a separate fallback question. The Discord ASI and MilanHost integration boundary, server identity for self-hosted instances, token authorization policy, and transport are undecided.

There is a sequencing problem to solve before promising same-socket punching: on the direct-IP path, the client game socket is created as client travel begins. The rendezvous flow must coordinate client socket creation, public endpoint observation, host probes, and UE3's first Hello packet. Creating an unrelated UDP socket ahead of time would not establish that UE3 uses the same NAT mapping. The host's receive path must distinguish traversal datagrams before UE3 accepts/parses them. Prefer inspection at the identified UE3 socket/dispatch boundary over a broad Winsock hook until runtime evidence says otherwise; preserve nonblocking receive behavior and avoid concurrent readers on the gameplay socket.

## Next research steps

1. Capture one working LMP LAN host and console join. Record the selected `UWorld->NetDriver` class on both peers, all relevant socket handles, local IP/port, remote IP/port, bind/send/receive/close order, and whether travel or map changes replace the socket. Use timestamps and thread IDs. Avoid full payload dumps initially.
2. Trace the menu-host path under MilanHost to explain why the working host uses `TcpNetDriver` despite the static Blaze branch and the native `SFXOnlineNetDriver` slot-92 stub. Check active game component/interface state and deployed ASI behavior. The old screenshot is a useful lead, not fresh proof.
3. Trace precisely when the client UDP socket becomes available during `open <IP>` and when the first UE3 Hello is sent. Determine a safe pause/introduction point for synchronized probes.
4. Confirm whether the embedded UPnP module maps the gameplay port. If it does, inspect lease, refresh, and cleanup behavior; if not, evaluate external UPnP IGD, NAT-PMP, and PCP only after the socket/port lifecycle is known.
5. Test two separate consumer NATs with the native UDP session. Measure success for direct/manual mapping, automated mapping, and coordinated hole punching; record failures before deciding how soon a relay is needed. Same-LAN success does not prove Internet traversal.
6. Only after the transport evidence is stable, specify the accountless matchmaker protocol, invite capability, rate limits, anti-amplification measures, self-hosting/TLS behavior, and relay boundary from the original handoff.

## Remaining design choices

- Should accepting a Discord invite launch the proven direct-IP travel path automatically, or enter the game's multiplayer menu flow?
- Possession of a valid invite code grants a join attempt without separate host approval; capacity and joinability still apply. Design how a code maps to the self-hosted matchmaker's identity and how it is passed through Discord.
- What should the first prototype guarantee: an ordinary two-router direct connection when feasible, or connectivity through difficult NAT/CGNAT from day one with a relay?

Update this document after each substantive Ghidra or runtime finding. Mark every claim as static binary evidence, source/package observation, user-reported behavior, live instrumentation, or two-peer runtime proof.
