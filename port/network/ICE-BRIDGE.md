# Optional ICE bridge for a CE test session

Status: experimental opt-in game caller; authored local fixtures only
Last updated: 2026-11-03
Summary: an explicit private exchange directory selects relay sessions for the game-facing caller while retaining CE authentication, bounded preparation and credential expiry.

## Boundary

`CeIceBridge` accepts one existing, single-peer `ENetHost`. It derives that host's socket port and forwards incoming ICE datagrams only to that port on `127.0.0.1`. Its own UDP socket also binds to `127.0.0.1` and accepts outbound datagrams only from the supplied ENet host's port. The loopback shuttle has no command-line destination, remote routing table or public listener; libjuice separately owns the configured ICE sockets.

The bridge does not interpret CE packets, replace authentication, admit a player or complete lobby admission. The existing CE transport must still authenticate the peer and validate session/content compatibility. ICE descriptions contain temporary session credentials and must not be logged or published. TURN account keys do not belong in the client; a future provider supplies short-lived client credentials through its separately reviewed session flow.

Relay mode is the default. Local and remote descriptions retain only relay candidates, and forwarding waits until the selected candidate pair includes a relay. The bridge also enables the patched dependency's immutable relay policy before gathering; candidate selection and application egress enforce it inside libjuice as described in [LIBJUICE-RELAY-POLICY.md](LIBJUICE-RELAY-POLICY.md). A direct ICE connection does not establish a relay-mode pass. One remote description is accepted during a bridge lifetime; reconnecting requires a new bridge and a new CE session. Incoming callback storage and each pump are bounded.

When the receive queue is full, [the callback](ice_bridge.cpp#L72-L78) drops the
new datagram without counting that reason. The [existing counters](ice_bridge.h#L36-L38)
count successful sends and deliveries and rejected local datagrams; they do not
count queue overflow or unsuccessful forwarding calls. These are diagnostic gaps
seen in source, not an observed cause of a player's problem. Any future reporting
must avoid packet contents and credentials, and must not make ordinary UDP loss
a fatal error.

All ICE implementation is behind `CE_NETWORK_ICE`. The disabled bridge translation unit is empty and requires no libjuice headers or linkage. Enabling the optional dependency in a game build is a separate build concern. The bridge sets libjuice's process-wide log level to NONE before creating an agent because dependency diagnostics can contain temporary ICE credentials. Other callers must not raise that level while agents exist.

## Dependency and licence pins

| Input | Revision | Licence | Source archive SHA-256 |
| --- | --- | --- | --- |
| [libjuice source](https://codeload.github.com/paullouisageneau/libjuice/tar.gz/b89c792e3612faf2f12cf35bcc56857313a06be3) | v1.7.4, `b89c792e3612faf2f12cf35bcc56857313a06be3` | MPL-2.0 | `713b22174e8cb2e06538fa9bac2650bda21b83c86f5128813ab4dcf8f5bea9ed` |
| [ENet source](https://codeload.github.com/lsalzman/enet/tar.gz/5a9c537fd464b3c6d3c55e1d3bd47588faf71b42) | 1.3.18, `5a9c537fd464b3c6d3c55e1d3bd47588faf71b42` | MIT | `537d42f31b0c0d27f5a9635f6c224f2451c1c1484bfde50e94c884e96181c290` |

The fixture builder verifies both archive hashes and extracts fresh copies under ignored `local-data/`. It does not download anything. The fresh libjuice extraction receives the separately pinned MPL-2.0 relay-policy patch; it is modified dependency source, while the original archive remains unchanged. The inspected upstream headers do not mark them incompatible with secondary licences. Mozilla documents combining MPL-2.0 code with GPL code under [section 3.3 and its related conditions](https://www.mozilla.org/en-US/MPL/2.0/FAQ/#q14-may-i-combine-mpl-licensed-code-and-lgpl-licensed-code-in-the-same-executable-program). Any distribution must preserve libjuice notices, supply its licence and the exact modified source/patch, and satisfy the MPL/GPL combination terms; ENet's MIT notice must also travel with it. Packaging and export acceptance remain separate from these caller fixtures.

The pinned libjuice route is TURN over UDP. TURN over TCP/TLS fallback is outside this prototype. No STUN/TURN service is contacted by the build helper.

## Authored local check

`port/network/tests/ice_bridge_tests.cpp` creates a TURN server bound only to loopback with fixture-only credentials. Two single-peer ENet hosts connect through separate bridges. The check requires actual selected relay candidates, an ENet connection, reliable messages on both channels, and reassembly of a fragmented message. It also checks refusal of a replacement peer, a direct-only description in relay mode, an unrelated local source endpoint, an unsuitable ENet host and a missing endpoint. It uses no game data or game process.

Run the helper with source archives already fetched and verified by the caller:

```sh
python3 port/tools/ice_bridge_build.py \
  --juice-archive local-data/ice-bridge/libjuice-source.tar.gz \
  --enet-archive local-data/ice-bridge/enet-source.tar.gz \
  --output local-data/ice-bridge/native-check --run
```

Use a new output directory for every build. `--target linux32` or `--target windows32` additionally requires `--toolchain-data` pointing at an existing dependency cache from `port/tools/deps.py`. The helper does not modify that cache. Windows execution is deliberately a separate step; `--run` is refused for that target. Run the fixture with an isolated network namespace, private HOME, normal resource limits and the repository's heavy-job coordination.

Validated on the final source: the i686 Linux fixture compiled and linked with the pinned LLVM 22.1.8/toolchain sysroot and passed its authored checks. The i686 Windows fixture compiled and linked with the pinned llvm-mingw 20260922 toolchain (Clang 23.1.2); it was not executed. Both builds also compiled the disabled bridge without any dependency include path. An earlier native x86-64 fixture passed before the final explicit socket-address control was added.

## CE session ownership

`ice_session.h` exposes `CE_NetCreateIce` only when `CE_NETWORK_ICE` is defined. It creates the existing CE transport with an owned bridge and a fresh loopback ENet endpoint. The direct address and port in `CE_NetConfig` are ignored on this explicit route. `CE_NetCreate` retains its direct-IP behavior; nothing selects ICE implicitly or falls back to direct-IP after an ICE failure.

The caller exchanges complete descriptions with `CE_NetIceLocalDescription` and `CE_NetIceSetRemoteDescription`, then uses the ordinary `CE_NetPoll`, `CE_NetClose` and `CE_NetDestroy`. Poll pumps ICE before ENet. The guest starts ENet only after the relay route is ready, and CE's handshake deadline starts there. A separate bounded preparation deadline covers missing signaling and absent relay candidates, including a completed gather that never produces a usable relay description. Zero selects thirty seconds; callers may choose 100 through 180000 milliseconds. A failed route retires its bridge and cannot admit game messages.

`CE_NetCreateIcePending` reserves the CE identity and loopback endpoint without creating a dependency agent. An attached preparation adapter can supply a grant later through `CE_NetIceStart`. Poll does not service ENet while that grant is pending, and starting ICE preserves the original preparation deadline. Starting twice, after cancellation, or after the deadline is refused.

ICE readiness is not admission: only the existing authenticated `CE_NET_ACTIVE` state allows application messages. ENet ingress for an ICE session accepts only its owned shuttle endpoint. Cancellation before connection destroys the bridge immediately; connected closure can flush its queued leave through the shuttle before destruction. The bounded close drain pumps the bridge before every ENet service call, including retransmissions after a lost initial leave. Destroy retires the bridge before the ENet host and ENet's final shutdown. Reconnect requires fresh sessions and descriptions.

`port/network/tests/ice_session_tests.cpp` uses the public CE API with authored data and a loopback TURN server. It covers handshake deferral, a preparation timeout without a relay, invite and content refusal, all four application channels, fragmented messages, an epoch barrier, unrelated loopback ingress, cancellation, authenticated leave, active destruction, reconnect, and ordinary direct-IP sessions. The helper also compiles the disabled path without any libjuice include or link dependency, and runs the existing CE protocol controls in both modes:

```sh
python3 port/tools/ice_session_build.py \
  --juice-archive local-data/ice-bridge/libjuice-source.tar.gz \
  --enet-archive local-data/ice-bridge/enet-source.tar.gz \
  --monocypher-archive local-data/ice-bridge/monocypher-4.0.2.tar.gz \
  --output local-data/ice-session/native-check --run
```

Monocypher is the existing pinned transport dependency from `port/deps.lock`; this helper verifies its source archive before extraction. The bridge helper's target, output-directory, network-isolation and Windows-execution rules apply here too.

The CE session and existing protocol fixtures pass on i686 Linux in both enabled and disabled configurations. The initial session integration preserved the disabled transport object's code; the later bounded close drain also applies to direct sessions. Controls that remove preparation expiry, start ENet before readiness or omit shuttle pumping fail at the intended assertions. The i686 Windows fixtures compile and link in both configurations; they have not been executed on Windows.

## Experimental game caller

`ice_game.h` provides `CE_NetCreateGame`, `CE_NetCreateLobby`, `CE_NetGameIceRequested` and a fixed-text `CE_NetGameProblem`. The engineering session and co-op lockstep creators call the game entry point; the separately composed lobby uses the lobby entry point and propagates creation errors. With neither `BEA_NET_ICE_DIRECTORY` nor `BEA_NET_ICE_GAME_DIRECTORY`, creation remains direct. Either nonempty selection explicitly requests the private directory route; an ICE-disabled build refuses that request. Each peer may select its own private directories, but moving records between peers' directories is not implemented here.

For the menu pilot, create two distinct fresh protected directories and configure both before launch. `BEA_NET_ICE_DIRECTORY` is the lobby preparation/exchange directory; `BEA_NET_ICE_GAME_DIRECTORY` is the game directory after the normal authenticated START handoff and process restart. The caller safely opens both directories through the existing private-directory reader and compares their physical identities before exchange/request publication; a spelling difference is insufficient. Missing, aliased, symlinked or unsafe selections are refused. Both selections must survive the restart. The game factory selects the game directory when set; a direct engineering launch may still use the original single-directory selection. The selector introduces no new join-code copy or identity transfer: the existing CE handoff remains responsible for fresh game identity. The composed process transition needs separate acceptance evidence.

The operator prepares one context in each directory, using the requests generated for that phase. A new retry or campaign context requires a fresh launch with fresh directories; this prototype never removes, replaces or silently reuses old request/grant files. Preparing one game phase does not establish automatic campaign continuation.

The wrapper opens the bounded encrypted `CeIceExchange`, then tries to load a role- and context-bound short-lived grant through `CeIceCredentials` as described in [RELAY-ACCESS.md](RELAY-ACCESS.md). A valid staged grant starts ICE immediately. If the grant file is absent, the wrapper publishes one protected `relay-host.request.json` or `relay-guest.request.json` and returns a pending CE session. That request contains only its schema, role, opaque context and bounded timestamps, never the CE secret, raw compatibility digest, descriptions or credentials. The separate operator helper consumes this request to prepare a matching grant; it is not client authentication or an automatic service connection.

The pending owner retains its original exchange, CE identity and directory. Poll checks for a grant at most every hundred milliseconds. A malformed, mismatched or unsafe grant is terminal, while an absent grant remains pending. The wrapper verifies the grant does not exceed the preparation request's lifetime cap before creating libjuice. The single two-minute monotonic deadline covers the grant wait, private description exchange and usable relay selection; supplying a late grant never resets that deadline. Cancellation or expiry retires the pending owner and a later file cannot restart it. Existing request files are not silently replaced, so a new operator-prepared attempt needs a fresh private directory when previous files remain.

After the grant arrives, the adapter polls remote descriptions before publishing its local description so the answer is bound to the accepted offer. The session owns this adapter and retires it on failure, cancellation or destruction. ICE readiness still precedes the normal authenticated CE admission.

Credentials are copied into the dependency's owned agent configuration, and their grant remains owned by the adapter until it is retired. Expiry during an active match ends the session with a fixed diagnostic; there is no credential refresh or automatic same-context retry. A fresh attempt needs a fresh CE stage secret, exchange context and matching grant. Existing records for the same context are refused instead of reused.

Each lobby or game preparation requests a two-hour credential budget by default. `BEA_NET_ICE_LIFETIME_SECONDS` selects a canonical decimal duration from 120 through 14400 seconds before session creation; it is ignored when ICE is not selected. The request records that exact deadline, and staged or delayed grants may not extend it. Changing the environment later cannot enlarge an existing session's budget. The separately reviewed operator also defaults to two hours, caps issuance at four hours and clamps `--ttl` to the request. To request four hours, both peers must select 14400 and the operator must allow that duration. The two-minute preparation deadline, three-minute maximum request/watch window and five-minute description-exchange validity remain separate short limits.

The game requests its own fresh context after local level loading; grant delivery, waiting for the other peer, admission, pre-run and play consume its remaining budget. Choose a planned play window plus setup and shutdown margin within that budget. A longer bound extends the period in which a disclosed TURN credential could be used and incur charges; the CE role/context checks do not limit provider-side credential use. [Cloudflare documents](https://developers.cloudflare.com/realtime/turn/faq/#what-will-happen-if-turn-credentials-expire-while-the-turn-allocation-is-in-use) that an active allocation disconnects shortly after its credentials expire. Periodic allocation refresh does not renew the credential, and this caller does not implement credential replacement or ICE restart.

`BEA_NET_ICE_FIXTURE=loopback` is an explicit diagnostic mode for authored local fixtures. It permits only the loader's numeric loopback TURN destinations and binds ICE to loopback. Normal mode retains the loader's fixed UDP service endpoint policy. Neither mode accepts a reusable provider token. Credentials and descriptions never belong in arguments, environment variables, join codes, clipboard UI or logs.

The wrapper emits only fixed milestones for selected transport, authenticated `active path=relay`, and bidirectional CE application traffic. Relay classification comes from the selected candidate pair, not the presence of TURN configuration. These milestones establish transport progress; actual mission/tick progress must be observed separately.

The native signaling route adds a private trace of the exchange itself on stderr, so a stall is placed instead of guessed. `onslaught-ce: relay exchange: ROLE step=XX signaling=N gathered=G relay=R state=S after_ms=T` is printed when one of its fields changes (at most 24 times) and as `ROLE beat=N step=...` after five quiet seconds (at most 24 times, one setup window), so a silent wait and a stage that is no longer polled look different. `step` is the hexadecimal sum of 01 the peer's record imported, 02 this player's own grant taken and parsed, 04 the ICE agent started, 08 the peer's description accepted, 10 the local description published to the private directory and 20 the local record handed to the signaling client. `signaling` is the index of `CeIceSignalStatus`; `gathered`, `relay` (how many relay candidates the local description holds so far) and `state` (libjuice's state index, 0 disconnected to 5 failed) come from `CE_NetIceProgress`; `after_ms` is the time since the exchange was created. `gathered=1 relay=0` is gathering that finished without a relay candidate; the dependency's own log stays off, so the trace does not say why. When the exchange stops, one `onslaught-ce: relay exchange: ROLE stopped: SENTENCE` line carries its fixed problem sentence, and the lobby prints `onslaught-ce: network lobby: relay lobby failed: SENTENCE` when its relay session ends in failure, with `onslaught-ce: network lobby: state NAME` for each change of lobby state. Every field is a count, an index or a fixed sentence: never a record, grant, description, address, path or service text. The manual file route is not traced. `port/network/tests/ice_relay_trace_driver.cpp` (run by `port/tools/ice_signaling_consumer_tests.py`) checks the lines, their bounds and the absence of the authored grant, descriptions and directory, with mutants of the trace.

`port/tools/ice_game_tests.py` accepts the same pinned archive, target and new output-directory arguments as the session helper. `--run --causal` runs the real game-facing caller with authored private grants and encrypted files, a loopback TURN server and no game data. The fixture covers long bounded credentials, real selected relay pairs, sustained bidirectional CE application messages, stale-context refusal, a fresh stage, active credential expiry, unavailable relay, missing exchange, and close retransmission. Pending-grant controls observe actual libjuice creation calls, preparation request stability, delayed operator delivery, cancellation, deadline expiry and a mismatched late grant. The explicit lobby/game selectors exercise separate fresh contexts, missing selections, physical directory aliases, symlinks and unsafe permissions. Lifetime checks inspect the actual requests, preserve short preparation deadlines, refuse invalid selections before private publication and advance only the test clock past ten minutes and the final credential expiry. Causal controls omit directory identity checks, the relay policy, request publication, the request's grant cap, active expiry, exchange polling, close pumping, preparation expiry or dependency-log suppression, restore a ten-minute request, ignore explicit lifetime selection, and separately restart the deadline after a late grant. The logging control retains captured canaries only in memory and reports counts, never secret text.

The composed caller, pending preparation, relay policy and directory checks pass on i686 Linux, including sustained bidirectional relay traffic and the causal controls. The i686 Windows fixtures compile and link, with the actual Windows caller refusing ICE before private I/O; no Windows execution is claimed. The original short caller check did not detect a provisional relay pair changing to a direct pair. The sustained control reproduces that failure when the dependency's relay policy is omitted, and passes with the policy enabled. These are authored transport fixtures, not game-process acceptance.

## Remaining acceptance

The caller is an experimental local integration, not a ready two-player service. Trusted grant issuance/distribution, secure cross-machine record transfer, refresh for longer matches, operator preparation across lobby/campaign restarts, the assembled menu/restart path, packages and licence obligations, and real Linux/Windows game-process acceptance remain separate work. Windows ICE requests are refused before any exchange or private-directory I/O: grant ACL checks alone do not establish a safe Windows exchange boundary. Default direct sessions retain their existing path. A shared private directory is sufficient for authored local peers; it is not evidence of cross-machine connectivity. No Cloudflare service, external network, live account credential, player session or native Windows game was exercised by these checks.
