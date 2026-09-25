# LMP matchmaking and NAT traversal investigation handoff

Last updated: 2026-09-25

Status: handoff step 1 (lobby state machine and HTTP control API) is implemented and unit tested on branch `MilanServerBlaze`. UDP endpoint discovery, traversal, host introductions, and all game/client integration are not implemented. Nothing has been tested with LMP.

## Current implementation handoff (2026-09-25, second agent)

### Branch and repository decisions

- The user chose a new branch, **`MilanServerBlaze`** (cut from `main` at `9fa4dbc`), for this work. **Do not use `origin/server`.** The user says it is old. That branch has an unrelated `MilanServer/` .NET solution (Milan.Core/Milan.GameServer, framed TCP protocol, EF Core and account models) by other contributors. The README on `main` still calls `server` the primary branch; that note is stale for this work.
- As of this update, nothing is committed. The new files are untracked on `MilanServerBlaze`. The unrelated working-tree changes are untouched: `Patching/_index.list`, untracked `Patching/*.diff`, and the `ThirdParty/LExSDKv2` submodule. Do not stage or reset them. `git` may need `-c safe.directory='B:/UserProfile/source/repos/ME3Tweaks/MilanClientServer'`.
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

### Known limits and open design points

- **The host is never told about joiners.** Introductions (host learns the joiner's candidates and reservation ID for probing) belong with UDP discovery in step 2. A long-poll or WebSocket host channel is the likely shape.
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

1. **Done (unit-tested only):** lobby state machine and v1 HTTP API, described above. Before building on it, ask the user whether to commit it on `MilanServerBlaze`. The contracts are still provisional; bump `Api.ProtocolVersion` on breaking changes once a client exists.
2. Add a host introduction channel (pending reservations with joiner candidates) and bounded UDP endpoint discovery with authentication and anti-reflection limits, then verify its packets can originate from the **actual UE3 gameplay socket** before claiming NAT traversal. The earlier Ghidra pass identified the socket lifecycle but not a safe client receive/interception point. Do not use a separate preliminary UDP socket as proof that the game socket mapping works.
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
