# Native port (Linux and Windows)

Status: active (both targets build, link and run; measured scope and limits are summarized in the public CE README's Known limitations; native Windows acceptance uses a software-rendered VM, not physical-GPU validation)
Last updated: 2026-11-18
Summary: a second build of the decompilation's sources as native i686 Linux and Windows programs; layout, ownership,
commands and rules.

The port compiles the same `src/` the matching build compiles, with clang for i686 Linux and i686 Windows, against
Direct3D 9 (dxvk-native on Linux, the system's on Windows) and one SDL3 platform layer for the window, input and
audio. Retail data is read from the player's install; nothing retail is committed. The VC6 matching build is separate.
The [public decompilation](https://github.com/dlprentice/onslaught-decomp#reproducing-the-match) documents its pinned
inputs, measured matching coverage and stated reconstruction limits. The public CE README owns
the port's [known limitations](https://github.com/dlprentice/onslaught-ce#known-limitations) and
[public setup](https://github.com/dlprentice/onslaught-ce#building-from-source).

This guide follows the source branch. The published package is 0.1.13; newer native menus and opt-in prototypes
described here are not a new release. Build support, isolated execution and ordinary player experience are
different checks. In particular, a Windows compile or Wine fixture does not demonstrate a native Windows UI,
and a Linux audio initialization check does not establish audible output.

    python3 port/tools/deps.py build     # pinned inputs, both targets, into local-data/port
    python3 port/build.py --public [--target linux|windows] [-k]   # the game: local-data/port/build/<target>/bea[.exe]

`-k` keeps the build going after errors (passed to ninja as `-k 0`) and prints a census of the error sites by message and
file; the full log is `local-data/port/build/<target>/last.log`.

Files this tree names but does not hold: comments and guides under `port/` and `src/` sometimes name `PLAN.md`,
`PARITY.md`, `tools/ce_frontend_tests.py` (a maintainer-only check that reads font source this tree leaves out), `tools/decomp.py`, `docs/release.md`, `tools/release_rc.py` and folders such as `tests/` and `probe/`. They
belong to the maintainers' working source, which this repository is generated from, and are not in this tree; the
[public decompilation](https://github.com/dlprentice/onslaught-decomp) is the public home of the matching tools.

## Guides in this folder

- This page: the build, the layout, the rules, the build and development variables and `ce.ini`'s unfinished-feature lines.
- [controller-input.md](controller-input.md): gamepads, flight sticks, hotplug, the Dodge Button and adjustable CE keyboard actions.
- [updater/README.md](updater/README.md): the launcher (finding the game, logs, Report a Problem);
  [updater/UPDATER-V2.md](updater/UPDATER-V2.md): the signed updater.
- The replacement libraries: [d3dx](d3dx/README.md), [bink](bink/README.md), [dsound](dsound/README.md) and
  [vorbis](vorbis/README.md).
- [PROJECTS.md](PROJECTS.md): linked mission projects (experimental).
- [recording-design.md](recording-design.md): a proposal for recording CE features; it is not implemented.
- The experimental ICE and relay work, none of it in a default build: [ICE-BUILD.md](ICE-BUILD.md),
  [network/ICE-BRIDGE.md](network/ICE-BRIDGE.md), [network/LIBJUICE-RELAY-POLICY.md](network/LIBJUICE-RELAY-POLICY.md),
  [network/RELAY-ACCESS.md](network/RELAY-ACCESS.md), [network/SIGNALING-SERVICE.md](network/SIGNALING-SERVICE.md),
  [network/ICE-PILOT-COURIER.md](network/ICE-PILOT-COURIER.md),
  [network/ICE-FLOW-INTEGRATION.md](network/ICE-FLOW-INTEGRATION.md) and
  [services/closed_signaling/README.md](services/closed_signaling/README.md).

Licence: GPL-3.0 (SPDX `GPL-3.0-only`), matching the upstream Onslaught source (GPL version 3,
no "or later" grant). The public NOTICE retains the source attribution; third-party inputs keep their own licences
(`deps.lock`).

## Public release

The port is released as [Onslaught CE](https://github.com/dlprentice/onslaught-ce), from an export of this source. Its
licence text is [COPYING](../COPYING) (GPL version 3).

The two commands below are the maintainers' release steps, not part of contributing. A contributor builds a test
package with the development steps in the README's
[Building from source](https://github.com/dlprentice/onslaught-ce#building-from-source) and does not publish one.

    python3 port/build.py --target all --public --ce-version X.Y   # the shippable build: no NvTriStrip, bundled libraries
    BEA_PRIVATE_PATHS=/first/folder:/second/folder python3 port/tools/package.py X.Y   # Linux tarball and Windows zip

The signed update feed comes from `port/tools/sign_update_v2.py`.

`--ce-version` names the version the game and launcher show ("Onslaught CE VX.Y"); without it the build is
"vdev". `package.py` scans every packaged binary for the builder's home folder, the checkout path and the account name
always, and for the colon-separated folders in `BEA_PRIVATE_PATHS` too. Left unset, folders outside the home and the
checkout (scratch space, external drives) are not scanned, so list them in `BEA_PRIVATE_PATHS` when building a release.

`--public` compiles `platform/public/`'s replacements in place of the files the public build must not ship and reports
"not shippable" for any still missing; `package.py` refuses such a build and any binary that still names a private
path. The launcher and updater, and what the signed feed holds, are in [updater/README.md](updater/README.md). No game
files are packaged: the launcher reads the player's own install. The launcher's updater is v2 ([updater/UPDATER-V2.md](updater/UPDATER-V2.md)): a
signed `ce-update-v2.json` feed, never `latest.json`, which launchers 0.1.0–0.1.6 fetch and which is never published
again (`tools/sign_release.py` is retired: running it only prints a refusal and exits). The v2 feed has been published with every release since 0.1.8. There is no telemetry.

The default `--public` build also defines `CE_NETWORK_OFF`: the network (Host / Join, the world and session layers, the lockstep Co-Op prototype) is compiled out until enabled explicitly with `--with-network`. build.py compiles none of its modules in the default flavour (`network_module`: `port/network/`, `game/ce_network_*.cpp`, `game/ce_netpages.cpp`, `platform/common/ce_netlobby.cpp` and `ce_netcode.cpp`, monocypher) and links no ENet; the shared SHA-256 used by mod checks is still compiled. The hooks the network added to shared files of the port are inside the same guards. Every hunk of every patch listed in `network/PATCHES.txt` sits inside `CE_NETWORK_OFF` guards (an insertion in `#ifndef`, a rewrite in `#ifdef` original `#else` new `#endif`), so a default public build patches `src/` to exactly what main's patches alone produce: `tools/network_public_source_tests.py` builds both trees and requires them equal once the guards are resolved. `game/ce_network_boot.h`'s `CE_NETWORK_ENABLED` also hides the Network game entry and the `BEA_NET_*` roles in the modules a private build compiles (`tools/network_public_off_tests.py`). Private builds and `--test-automation` builds are unaffected.

`--public --with-network` is the same public build with the network compiled in (the experimental Co-Op build): it does not define `CE_NETWORK_OFF`, compiles the network modules, links ENet (`-lenet`, and `-lws2_32` on Windows) and writes `flavour.txt` (`public-network`) beside the objects, so that `tools/package.py` carries ENet's licence file and notice row and refuses a package whose build folder and program disagree (ENet in the default flavour, none in this one). It is refused without `--public` (a private build has the network already) and with `--test-automation`. Its objects differ from the default public build's: build it in a separate worktree. The default `--public` build is unchanged by it.

The default public build (no `--with-network`) is intended to compile the same source as the selected CE hub. Byte-identical output on a newly composed head still requires a clean build comparison; source checks alone do not establish it. Every difference the network made to a file of main is inside a `CE_NETWORK_OFF` guard, and the default build compiles main's text itself, not the guarded file: the preprocessor would skip the network's lines but still count them, which moves every `__LINE__` (the game's `new` passes one to the allocator), every line table, and with them the PDB GUID that the linker derives from the unstripped image and that the stripped `bea.exe` still carries. So `build.py` resolves the guards of the mirror (`resolve_network_off`) and compiles a copy of the port's code with them resolved (`build_port_view`, under `build/resolved/port`, mapped to `.` in the debug names and `__FILE__`), and the network's directory and the tools are not in that copy. `tools/network_default_text_tests.py` pins the result: `network/MAIN-TEXT.sha256` holds the sha-256 of main's text of every port file a default build can compile or include, `network/MAIN-SOURCES.sha256` that of every file of `src/` and `linklib/` and every patch of main's queue (the network's own patches are the ones `network/PATCHES.txt` lists); the suite fails if any resolved file differs, if a file that main lacks is compiled or included by the default build, or if a patch of main's queue is edited. After merging the selected CE hub, refresh the pins with `python3 port/tools/network_default_text_tests.py --pin <hub ref>`; a changed pin names a file whose guards need another look. To show it on a build, make the default flavour of this tree and of main in two worktrees with the same `--ce-version` and compare them object by object and program by program (`llvm-strip --strip-all` is what the packager does to `bea`); golden tapes replayed with probes at every Present remain the check that nothing else moved.

### Upgrading from 0.1.0 to 0.1.6

Do not run released launchers 0.1.0 through 0.1.6 on either platform. Their source contains name-based startup
deletion of neighboring `<program folder>.old-*` paths; the shipped Linux 0.1.6 package has reproduced data deletion
and traversal through a matching directory link. Windows reparse behavior remains unmeasured. Disabling updates
does not disable startup cleanup. Predictable download/unpack names and unverified rollback paths are additional
reasons to avoid invoking an affected updater. The corrected successor is the latest release on the
[GitHub releases page](https://github.com/dlprentice/onslaught-ce/releases); 0.1.7 switched the old updater off, and 0.1.8 brought the v2 updater.

Migration must not start the old launcher. Preserve its entire program folder and uncertain neighboring
`.old-*`, `.download-*` and `.unpack-*` paths. A relative `BEA_USER_DIR` can put launcher and game user data in different
locations; identify and preserve both complete folders and verify independent backups, including careers. Use a
separate working copy of the identified game user-data folder outside the original game and CE program/staging folders
and set `BEA_USER_DIR` to its absolute path for the successor.
Keep originals and backups, check careers, and do not merge unclear or conflicting files.
Only then verify the latest release's download and extract it into a new,
empty, ordinary writable folder. Run that successor only; retain backups and uncertain old paths.

The complete steps live in the [player README](https://github.com/dlprentice/onslaught-ce#manual-migration-from-affected-launchers).
Windows 0.1.5's helper additionally hits installer detection in the measured ordinary session. Although 0.1.6 embeds
`asInvoker`, that declaration does not repair the cleanup hazard. Do not use administrator execution or disabled UAC
as migration workarounds, and do not use 0.1.6 as an intermediate release.

## Network game (Host / Join)

**Experimental.** The public flavour made with `--public --with-network` offers the CE network menus.
The default public build leaves the network out, as the section on `CE_NETWORK_OFF` above says, and offers no such page.
The network-enabled public build offers
**Onslaught CE > Network Game** (`game/ce_netpages.cpp`): Host Game or Join Game, for two-player Co-Op on the building defence (level 856) or the
tutorial (100). The original ships the building defence twice, as levels 856 and 858, with the same map and script (their level files differ in one byte of the world data and some
resource-table indices; frames of the two are identical), so the lobby offers it once; the gates admit 858 as well. Never in replays, recordings, probe or pilot runs (even with `BEA_PORT_CE_LAYER=1`), `-autoconfigtest` or the demo, where the CE page keeps
its earlier rows.

The first page offers **Host Game**, **Join Game** and **Connection Help**, and both use the connection the launch was prepared for (direct, or the relay pilot when the launch selected it). Host Game hosts at once on this PC's first address (a private LAN address when there is one) and shows the join code; on a PC with several addresses the host's page has an Address row, and changing it hosts again there with a new code. Join Game opens the code page (Code, Address, Connect, Paste Code, Paste Address, Clear); while a join is trying the page shows only the code and Cancel. Each page is titled by its name (Network Game, Host Game, Join Game, Connection Help). Under each row a hint says what it does. A failure, a lobby notice (a friend who left, nobody joining) and the end of a network game open the game's message box, so no sentence is dropped for room. The pointer works on every page, the Join page included (its key trap answers the game's mouse question with false). The lobby shows Player 1 and Player 2 as You and Your Friend, Ready in green, and the mission by name; the host has Mission, Start and Leave, the guest sees the mission and Leave only. Campaign co-op is not offered to players: its lobby row appears only with `BEA_NET_LOBBY_CAMPAIGN=1`, for testers. A `--with-ice` build adds **Import Online Invitation** (an offline import with a public preview, then an explicit Use invitation), and its Host or Join opens a network disclosure before anything contacts the signaling service, then asks for pair consent. A build without the relay refuses a launch prepared for it and starts nothing. `tools/network_player_menu_tests.py` compiles these pages both ways and checks those decisions; it runs no game, socket or service. `tools/coop_pair_run.py` plays one co-op game through these menus with two offscreen games on one machine, to the mission's end and back, or with `--ending leave` until the guest closes its window during the mission (see its own help); in both, each player's careers and options must be byte-equal afterwards.

What has been seen of play on this line, and its limits: on one Linux machine, two native Linux processes joined through these ordinary menus, both players moved and fired in a lockstep game, and the guest's window close took the host back to its front end (an independent run on 2026-11-08 at a3c83dc8, 501 shared observations). On the 0.2 line, `tools/coop_pair_run.py` repeated it through the menus to the end of level 856 (both peers agreed on the end tick and state hash; the mission was lost, not won). Nothing has been run with a Windows host, between two machines, over the Internet or a relay with a real partner, or in campaign co-op, and no person has played it.

The instructions below describe direct-IP Host / Join, including LAN play and router forwarding. An optional build with `--with-ice` can select a TURN relay path; its setup and endpoints are described in [ICE-BRIDGE.md](network/ICE-BRIDGE.md) and [RELAY-ACCESS.md](network/RELAY-ACCESS.md). Direct-IP instructions do not establish relay availability or successful online gameplay.

What this flavour tells the players, in its packaged README.txt and, where a player needs it, on the pages (`tools/package_readme_tests.py` pins the shared
sentences against the page's text); a release that offers the page repeats it in its notes:

- Experimental: new and unfinished, expect problems.
- Both players need the same Onslaught CE version and their own copy of the game; a different build is refused with both version labels.
- If the two games ever disagree, the game ends with a message (`co-op session stopped: the two games went out of step at tick N ...`); nothing is
  resynchronised. A network game does not change your careers: its process may open only the files its admission lists (`CE_BootLocalFileAllowed`: the options
  file `defaultoptions.bea`, `cardid.txt`, `setuphistory.txt` and `OnslaughtException.txt`), and a career is not among them. Its HUD scales and sits in the
  corners as in single-player levels (patch 0157; `BEA_PORT_HUD_SCALE=0` turns the scaling off).
- A network game does not pause: one player cannot stop the shared battle. In the mission Escape (or a pad's Start) opens the pause menu's ring with
  Continue and Leave Game and the line "The battle goes on." (port/game/ce_network_ring.cpp, patch 0231); while it is open that player's controls are held
  still on both games, so they stay in step. Leave Game asks the pause menu's "Are you sure?" with the focus on No; Yes returns that player to the front end,
  where the message box says "You left the game.", and the other player's game logs "the other player left the co-op game" and returns to its front end,
  where the message box says "Your friend left the game." Closing the window still works and ends that process without returning
  (`the window was closed: not returning to the front end`). A lost network game's end screen offers Quit only: Retry would restart one player alone
  (patch 0230). Other stops read "The two games went out of step, so the game stopped. You can host or join again." or "The network game stopped. You
  can host or join again."; the reason is in the log.
- There is no matchmaking server: play on one network, or the host forwards its UDP port (`net_port` in ce.ini, default 27652) on its router and gives the
  other player its public address. Connection Help (a row of the Network Game page) walks the host through the steps in four short paragraphs: the
  port forward at the router, the public address and the Windows firewall prompt. A public address starting 10., 192.168., 172.16 to 172.31 or 100.64 to
  100.127 means another router is in front of yours, so forwarding is not enough. Use a VPN you both already use, or play on one network.
  The notices say what to do in one or two sentences. A guest whose join fails reads, in the game's message box, "No game accepted this code. Check it
  with your friend, then choose Connect again." The host's page keeps the code in view and says what happened in one line under it: a host that has
  waited 45 seconds with nobody reaching it reads "Nobody has joined yet. If your friend cannot connect, see Connection Help.", and one whose friend
  could not join reads "Your friend could not join. Check that they have this code." The game cannot tell a wrong code from a host that is not waiting
  or a blocked port, so the address, port, transport error and the number of failed attempts go to the log only.
- Not tested: play through routers over the real internet, more than two players, Windows on real hardware (the Windows build has been run against Linux
  under Wine; under Wine or Proton keep the game folder and the user folder on drive C: the settings reader refuses a folder reached through a link, which Wine's Z:
  drive is, and admission refuses a content file that has no volume serial number, which Wine reports for Z: paths on some host-mounted folders, as observed during one test), so a game or user folder reached through Z: can be refused at admission).
- What it contacts: only the other player's address (UDP). No server, no telemetry. To list this PC's addresses the Host page reads the network
  interfaces (Linux: `getifaddrs`, local only; Windows: `getaddrinfo` on `gethostname()`, which asks the system resolver for this PC's own name and may be
  answered by the local network). The code and the secrets are never logged.

- **Host**: this PC's IPv4 addresses, the UDP port (`net_port` in ce.ini, default 27652), the join code with Copy Code,
  and the waiting state. It contacts nothing to learn a public address; over the internet the host forwards the port and
  gives its public address too.
- **Join**: a code field (typed, or pasted with Ctrl+V) and an optional address (`a.b.c.d` or `a.b.c.d:port`) that
  overrides the code's.
- **Code** (`platform/common/ce_netcode.h`): 60-bit random key, IPv4 and port in 22 Crockford base-32 symbols and a
  GF(32) check symbol, shown `XXXX-XXXX-XXXX-XXXX-XXXX-XXX`. Case, dashes and spaces are ignored, I and L read as 1,
  O as 0; every single wrong symbol and every swap of two different symbols is caught before connecting. The lobby's
  secret is SHA-256("onslaught-ce join v1" || key); the code and secrets are never logged. The host makes a new code
  after five failed admissions. Limit: 60 bits resist online guessing, but an on-path observer who records a handshake
  could search the keys offline at real cost.
- **Lobby** (`platform/common/ce_netlobby.cpp`): the existing authenticated transport with the lobby secret and a
  digest of the build's network identity and version, so a different build is refused with both version labels.
  Both versions, the compatibility line, the map (the host chooses the building defence or the tutorial and the guest
  follows), Start (host only) and Leave. Game data is compared only by the game process's own admission, which
  reports a difference on return.
- **Start**: the host sends START (map, a fresh start number) and the guest acknowledges it (ACK, retried while the transport pushes back, for at most 5 s). Only when the acknowledgement has
  reached the host does the host write its start file, queue GO and leave gracefully (so that GO is delivered); the guest starts on GO and on nothing else: not on its own acknowledgement,
  not on silence, not on the host's leave without GO (a failure), and a GO that arrives before the loss of the connection is judged still counts. Each side's wait has a deadline (the host's 3 s,
  the guest's 5 s), after which the lobby is as it was with a notice and Start can be tried again, and a late ACK or GO of an expired attempt is ignored. A reliable transport still cannot make the
  last message certain: if GO is lost after the host has started, the host's game waits for a guest who does not come and says so when its wait ends. Each process then writes `<user folder>/net-launch`
  (mode 0600, read once and deleted as it is read, refused after 60 s) with its role, endpoint, a per-Start secret SHA-256("onslaught-ce start v1" || lobby secret || nonce),
  the level and its own arguments, closes its window ("Starting..."), and starts itself again with
  `-level N -skipfmv`, N the chosen map (Linux: `execv`, same process; Windows: a child with the same standard
  handles, waited for). The fresh process feeds the handoff to the unchanged fresh-process admission (`CE_BootStart`),
  which admits the engineering session (852) or a Co-Op map (100, 856, 858) and its own content contract. After the match, unless
  the player closed the window, the game process starts the front end again with `<user folder>/net-outcome`, which
  opens the Network game page with what happened (patches 0140 to 0142) and the log says what the player was told. The
  window closing and opening is the v1 cost of keeping admission in a fresh process. LAN discovery is not part of v1.
- **Session** (`game/ce_network_lockstep.cpp`, patches 0129 and 0152 to 0156): both games run the same simulation
  from the same inputs (20 ticks a second, an input delay of 3 ticks), exchange a state hash every 20 ticks and stop
  with the message above when the hashes differ. A session that fails or whose other player leaves ends the level and returns to the front end, where the Network game page shows the
  reason (a game the lobby started leaves for the front end; a test-automation pair has no lobby and quits to the system). While a session is requested the game's own allocations start as
  zeros (`PARITY.md`, "Co-op sessions"). The controller takes its role and secret from the handoff and its content
  digest from the admission; every `BEA_LOCKSTEP_*` switch, the dump, stalls, skews and probes exist only in a
  `--test-automation` build (`tools/lockstep_production_shape_tests.py` pins that split).
- On Windows each restart leaves the previous process waiting for its child (it has already shut the game down), so
  the launcher keeps one process to wait for and the log pipe stays open; a long chain of matches keeps one such
  process per restart.
- Switches for tests and unusual setups: `BEA_NET_LOBBY_BIND` (an IPv4 address: the host listens there and puts it in
  the code, for example 127.0.0.1); `BEA_NET_LOBBY_CODE_OUT` (a new private file the host writes its code to, honoured
  only on SDL's offscreen video driver with `BEA_PORT_REQUIRE_OFFSCREEN=1`, so a second test process can type it). A
  restart drops test input (`BEA_PORT_FAKE_KEYS`, `BEA_PORT_FAKE_CLICK`, `BEA_PORT_SHOT`, `BEA_PORT_QUIT`) and any
  `BEA_NET_*` endpoint or secret from the environment it passes on.
- Tests: `tools/ce_netcode_tests.py` (code, secrets, handoff files; mutants), `tools/ce_frontend_tests.py` (the Coming
  soon wording in both flavours), `tools/network_hostjoin_lockstep_tests.py` (the lobby's restart scrub), `tools/network_lobby_restart_handoff_tests.py` (the start file the lobby writes is the one the game process reads), `tools/network_lobby_start_tests.py` (the handshake against a scripted peer: refused, delayed and lost acknowledgements, the host leaving, the deadlines; mutants and the lobby before GO), `tools/network_lobby_notice_tests.py` (the lobby page's exact words for a refused or failed session: another version label or build on both pages, a host that does not answer, failed attempts and the new code after five, a host that nobody has reached for 45 seconds, a player who leaves, a taken port, an unanswered start, the Join page's code and address problems; mutants), `tools/network_page_words_tests.py` (the help page's words and the host page's two optional lines, word for word, the numbers in them against the ipaddress module, one label for every pointer to the help; mutants), `tools/lockstep_production_shape_tests.py`.

## Layout and owners

| Path | What |
|---|---|
| `deps.lock`, `tools/` | pinned third-party inputs and their builds into `local-data/port/`; isolated run and check tools |
| `build.py` | the port build (ninja): flags, include overlay, mirror + patch queue, link |
| `include/` | Win32, DirectSound, DirectInput, mmsystem and VC6 CRT shims over dxvk-native's headers |
| `platform/` | `common/` (both targets: the SDL3 window and message pump, DirectInput 8, the Direct3D 9 hooks, clocks, registry, NvTriStrip bridge), `linux/` (Win32 over POSIX and SDL3, paths, entry point), `windows/` (file overlay, clocks) |
| `aliases.tsv` | linklib/aliases.tsv's second names in the Itanium ABI, the C++ name-mangling scheme of the Linux and MinGW toolchains (the original compiler mangled names differently); applied with `--defsym` on Linux and `/alternatename` on Windows |
| `game/` | CE gameplay additions and integration code |
| `platform/public/` | public replacements for excluded SDK/framework components |
| `patches/` | port-only source edits that cannot be byte-neutral in `src/`, applied to a generated mirror |
| `d3dx/` | `include/` (d3dx9*.h, dxerr9.h) and `src/`: maths, zlib-wrapped DDS loader, vs/ps 1.x assembler, DXGetErrorString9 (its tests are maintainer-only) |

## Rules

- `src/` line counts never change (`__LINE__` strings and `.bss` creation order depend on them). Propose source changes
  through [CE contribution guide](https://github.com/dlprentice/onslaught-ce/blob/main/CONTRIBUTING.md); maintainers run the matching check in their working source. That tool
  is not part of the CE export. The separate [public decompilation](https://github.com/dlprentice/onslaught-decomp)
  documents its own function-check setup. CE behavior changes normally belong in `game/`, platform code or `patches/`.
- Pinned dependency builds go to the ignored `local-data/port/` of your clone's main working tree, which every Git
  worktree of that clone shares (or to the folder `BEA_PORT_DATA` names). The first setup
  downloads roughly 3.2 GiB before extraction and builds (an estimate: `deps.lock` records no sizes). What derives
  from a checkout's own `src/` (the mirror, objects, probe output) goes to that checkout's ignored `local-data/port/`.
- Dependency cache identity: the SDL receipt in a dependency cache records the SHA-256 of the whole of `tools/deps.py`
  (`sdl_expected`), so editing that file in any way, a docstring or comment included, makes an existing cache refuse
  every build until it is rebuilt. `tools/deps_tests.py` pins the hash (`RECIPE_FILE_SHA256`); change the file only
  together with a deliberate cache rebuild, then replace the constant. The same whole-file hash is in
  `tools/ice_deps.py` (the optional ICE library receipt) and `build.py` (`build_recipe_sha256` in the optional ICE link
  receipt, which is rewritten by every such build). Planned hardening for the next deliberate cache rebuild: hash the
  recipe without prose (the file parsed and its docstrings and comments dropped, or the recipe's functions and
  constants only), so that a prose edit stops invalidating caches; that change invalidates every cache once.

## Environment variables

The variables a player sets are in the [README](https://github.com/dlprentice/onslaught-ce#readme) (`BEA_GAME_DIR`,
`BEA_USER_DIR` and the settings it lists). Build and development variables:

| Variable | Used by | Meaning |
| --- | --- | --- |
| `BEA_PORT_DATA` | `tools/deps.py`, `build.py` | where the pinned dependencies are built and cached (default `local-data/port` in the clone's main working tree) |
| `BEA_PORT_SYSROOT` | `tools/deps.py`, `build.py` | `sniper` (the default, an old-glibc sysroot for the Linux build) or `host` |
| `BEA_GAME_DIR` | the build and many checks | the game installation whose two build inputs `config/game-data.sha256` pins |
| `BEA_PRIVATE_PATHS` | `tools/package.py` | colon-separated folders to scan packaged binaries for, besides the home folder and the clone; a maintainer's release step |
| `BEA_VC6` | `vorbis/build.py` | the pinned Visual C++ 6 toolchain, only to regenerate the prebuilt libvorbis, which needs Wine and a toolchain this repository does not supply |
| `BEA_PORT_TAPE`, `BEA_PORT_RECORD`, `BEA_PORT_CLOCK`, `BEA_AUDIO_MONITOR` | the launcher and the checks | replay, recording, virtual clock and audio-monitor controls ([updater/README.md](updater/README.md), [dsound/README.md](dsound/README.md)); developer switches, not player settings |

Other `BEA_PORT_*` names appear in individual checks and patches; the file that reads one is the place that describes it.

## New-profile video defaults (`ce.ini`)

On a fresh profile CE writes `full_models=0`, `terrain_detail_distance=1` and `extra_graphics=1` to `ce.ini` once; a valid line already there is a saved choice and is kept. `extra_graphics=1` asks for the extra graphical features, which come on only where the device passes the capability test; `extra_graphics=0` keeps them off. The CE Settings row Dodge Button is the `dodge_button` key (on by default; `dodge_button=0` turns it off).

## Unfinished features (`ce.ini` `[Experimental]`)

`ce.ini` lists every unfinished feature, one line each, switched off, in a block that CE writes when the file lacks it (a new profile, or an existing `ce.ini` that has no such block yet):

```
[Experimental]
; Unfinished features, off in this release. They may change or break. Set a line to 1 to try one.
map_editor=0
museum=0
commander=0
play_unit=0
```

They are for people who test those features, not a supported part of the game. Each line switches only its own feature (it is read once, the first time anything asks and at the
latest as the first Main Menu is built; a later edit applies at the next start), and a line that is missing, has any value but a lone `1`, appears twice, or sits in a `ce.ini` that cannot be read whole is off:

| Line | Feature |
| --- | --- |
| `map_editor` | the native map and mission editor: the Map Editor row of the Onslaught CE page and its map pages |
| `museum` | the native museum: the Museum row, its Units hall and its Art and Movies wall |
| `commander` | Commander: its CE Settings row (Commander Map), the M key, the live view and the orders |
| `play_unit` | take a friendly tank or an eligible friendly soldier moving independently; use Morph to return to the Battle Engine |

The current prototypes have these limits:

- Commander provides squad selection and orders. A native-model RTS view and complete individual-unit, group and
  structure workflows are still being developed; the existing live-view isolation checks below describe the
  current read-only picture, not proof of those later workflows.
- Taking a soldier is not a finished first-person infantry mode. Camera and weapon presentation remain work in
  progress; use the bound Morph action to return, as described in the [controls guide](controller-input.md#adjustable-keyboard-actions-development-build).
- The Museum's walkable room and film screen are usable prototypes. Close-up exhibits can disappear, and hall
  composition and effects still need correction; expanded exhibition and multi-film work is not completed here.
- The editor's existing-object placement, saved/draft state, F5 preview and F6 reset already work in the exercised
  Linux path. A player-start workflow and reported interaction lag remain unresolved. It does not create new units.

With a feature's line off, its rows are not on the Onslaught CE page, a forced action of its rows does nothing, nothing it adds runs, and the Help pages do not describe it as present: without
`commander`, Controls & Tips drops the commander view's line and the Other Projects and Ideas entry of What's Coming says it is in development; without `map_editor`, the Map and Mission Tools
entry says the same. With all four at 0 nothing unfinished is offered, and the faithful and replay paths and every ordinary career are as without them; a recording, a replay or a probe run
never opens `ce.ini` for these lines (Commander and `play_unit` ask only after their own fence has passed, and the block is written only where the CE menus are on).

With `map_editor=1`, open **Onslaught CE → Map Editor**, choose a map and select an existing named unit. **Change Side** (T)
cycles its Side card through Forseti, Muspell and Neutral. Ctrl+Z undoes the change; Ctrl+Y restores it.
Choose **Preview Draft** or press F5 to try your current edits without saving. Flight controls belong to the pilot during
preview. F6 resets the preview to its start; F5 returns to your camera, selection and undo history. **Save** (Ctrl+S)
writes a project; Ctrl+O reopens that save on the same map. Position-only projects stay compatible with version 2; a
side override is saved in a version 3 CE project.

Choose **Hold Area** (H), then click visible ground. In its inspector, click **Duration** or **Radius** to cycle the values, then **Done**. A Hold Area can stand alone or share a two-stop route with a Reach Point. The HUD counts your continuous time inside the sphere; leaving resets it. Projects using a Hold Area save as version 6; older Reach Point projects keep their format.

To make a mission, choose **Reach Point** (R) and click visible ground. To add another destination, choose **Reach Point →
Add Stop 2** and click its ground position. The numbered diamonds show the order. The same menu lets you move or remove
either stop or **Reverse Order**; Undo restores the previous route. Preview the draft and follow the HUD to within
18 metres of each point in order. Visiting Stop 2 first does not count. If the stops overlap and you enter both
together, leave Stop 2 and return. A single point stays in CE project version 4; only two ordered points use version 5.
Previewing a project leaves the ordinary career and campaign unchanged.

With both Commander and unit control enabled, **Commander** (M by default) opens the strategic view from your Battle
Engine, and **Take Unit** (E by default) in the cockpit can take a friendly unit in your sights. While commanding,
the map keeps its controls. Close it before taking a unit. While controlling a unit, use your bound **Morph** action
to return to the Battle Engine before opening Commander. The [controls guide](controller-input.md#adjustable-keyboard-actions-development-build)
describes rebinding these actions and resolving conflicts.

Opening the battle view changes what the game draws, and in this game what is drawn can change how a battle unfolds, as turning
your view does in the original (drawing a unit refreshes its turret and weapon positions). The same inputs, with the same camera
moves in the view, give the same battle again; a different camera path may give a different one. Opening the view leaves nothing
of its own in the battle once it is closed, and a game where Commander is never opened plays exactly as it does without CE's Commander.

**Older values.** `commander` and `play_unit` are the lines earlier builds already used: a `commander=1` from 0.1.13 (which had no CE Settings row for it: its Controls & Tips named the line and the
player typed it) still turns Commander on, but what the Commander action does has changed. In 0.1.13 M opened the paused map. Now Commander opens the live view: the battle keeps running while pilot control is suspended
and the flight HUD is hidden. The paused map on V also requires `commander_tactical_map=1`; Escape or Commander closes the view. And
`play_unit=tank` (the older spelling; `play_unit=1` means the same, the tank being the one unit) keeps playing a tank on. The block does not repeat a line the file already has and
changes no value; those two lines stay where they were and the block adds only the lines that are missing.

**The block.** CE adds it once: the file's lines come first, byte for byte (a missing final newline is added), then a blank line and the block. A file that already has the line `[Experimental]`
is left as it is, including a line the player removed or changed, so the block is never written twice, and is not even opened for writing (the start keeps its inode, time and mode). When a write is needed it keeps the file's permission bits (a new file is private to its owner); a `ce.ini` the player made read-only, and a result longer than 1 MiB (the size every reader accepts), are refused: the start logs that the file is unchanged, once. A key that CE Settings or the Cheats page writes later goes above the header, and a
key that is there changes in place. Every reader of `ce.ini` takes lines that begin with `key=`, so the header and the `;` line change nothing: the game's readers (`ce.cpp`), the launcher
(`ce_launch.c`, `ini_get`) and `ce_ini_write`. (The Companion's launcher-settings editor was read, not run, against a file with the block: it matches lines by `key=` and keeps the others.)

**A museum visit and the save notice.** With `museum=1`, a Museum Prototype visit keeps the career out of reach (the career gate of `game/ce_project.cpp` refuses every save while it is active), and the
Pause Menu saves a named career on every resume, so the game's save error line is drawn each time the visitor leaves the Pause Menu. Nothing is lost: the career is copied when the visit begins and copied
back when it ends. The player-facing READMEs say so; the notice itself is not suppressed.


**Test-only switch.** `BEA_PORT_MUSEUM_REFUSE=<name>` makes a museum visit that is loading refuse at one named step (exhibits, site, binding, generate, changed, world, afterload), so a route test can see the refusal sentence, the player's log line and the return to the Museum page. Only `ce_museum_run_tests.py` and `ce_museum_visit_tests.py` set it; it is read only while a visit is loading and changes nothing a player sees in an ordinary run. No other museum switch exists: the hall's facing, its level and the hall's edge are fixed by the game's data and by CE's code.

Sharper Text stays a CE Settings row: it is off by default and labelled experimental, and not behind these lines. Network play is not behind them either: a default `--public` build does not contain
the network, so no `ce.ini` line can turn it on (`CE_NETWORK_OFF`; the build with `--with-network` is a source build for developers). `BEA_CE_PROJECT` (the project-launch prototype of
`game/ce_project.cpp`) is an explicit developer environment switch with its own required absolute roots, reachable from no menu, key or `ce.ini` line, and is unchanged.
Checks: `tools/ce_experimental_modes_tests.py` (the lines and the block over the actual `ce.cpp` reader and writer on scratch folders, and the entry points' wording),
`tools/ce_frontend_tests.py` (the pages with each line on alone, and with all off), `updater/tests/ce_ini_tests.py` (the writer), `tools/ce_settings_tests.py` (the launcher's and the game's readers over files with the header) and `tools/ce_commander_tests.py`.


**Extras (`extras=1`, hand-added).** The Companion's player features inside the game, still being built. A line `extras=1` under `[Experimental]` adds an Extras row to the Onslaught CE page (the block
CE writes does not carry it yet). Its pages, in the game's own menu style: the Service Record (every career listed without loading it, with each mission, its state and rank letter, the kills by class and
the Goodies found; Saved Settings shows the stored sound and music levels and whether each player has invincibility;
viewing the record leaves the current settings and career file unchanged) the Goodies Catalogue (the same Goodies with how each is earned, in words made by `tools/gen_goodie_rules.py` from the Companion's published rules, each with how far the evidence for it goes), and the Music Player
(the soundtrack and the tracks of Your Music in a list; Select plays one, Select on the playing one stops it, a track the game cannot start says so and is passed over, and a Play Order row sets what follows a
track for that visit only; leaving puts the menu music back), and the Lore (six articles of the lore library, made by `tools/gen_lore_pack.py` from the Toolkit's lore, as a list and then a reading panel in the small face with the list box's scroll bar. Select or click Contents to jump to a heading, or go Back to keep reading at the same place; looking changes nothing and writes no file). A career with values the game never writes is listed as one that could not be read. CE reads this line wherever it stands in `ce.ini`, as it reads its other unfinished-feature lines (the `[Experimental]` block is where it belongs); with it missing or set to anything but `1`, no row and no page exists. The pages exist only where the CE menus do:
never in a replay, a recording, a probe run or the test harness. Windows has not been seen.

Choose **Back Up Career** in the selected Service Record. Pick an existing folder outside the game,
its package and your CE careers, then choose **Back Up Now**. The page initially highlights **Back**. It shows
the full location after verifying a copy; scroll the path if needed. Each set contains just the selected
CE career and appears in the standalone Companion's backup list. Repeating the action creates another set.
On Windows, use a local drive without mapped or linked folders. Game-folder careers, linked files and careers that have changed since you
opened the record cannot be backed up here. Your original career and existing backups stay in place.

## Frame-feel measurement

`BEA_PORT_FEEL_LOG=PATH` makes the game write a timestamped log of how it paced, ticked, moved its camera and answered input
(`platform/common/feel.h` lists every switch; `tools/feel_report.py LOG` reads it). It is off unless that variable is set, and then only
where the CE layer is admitted: never in a replay, a recording, probes or a virtual clock. It observes: the game's state is not
changed, the clock the game reads is not read, and the only thing it can do to timing is the optional display model
(`BEA_PORT_FEEL_PACE_HZ=N` makes Present return on a grid of N per second, as a vertical sync does, for machines whose offscreen route
has no display). A run on a player's own machine with the log on gives the numbers a person cannot see: the interval between Presents,
how many game updates fall in each, how the camera moved, and how long the main thread worked, with the Present call's own duration
showing whether the driver was holding the game back. `BEA_PORT_FEEL_SCRIPT=FILE` adds authored input on a wall-clock timeline (a
steady mouse stream, keys, a quit, and runs of consecutive frames saved as pictures) so that the time from an input to the frame that
shows it can be measured on a run nobody touches; the report gives, for each held key, how long the camera took to answer it, how
evenly it moved and how long it ran on after the key was let go, and for the paused Commander map the same for its pan. The numbers
describe the game up to Present; the display, the compositor and the driver's queue add the same after it for any variant compared.
Tests: `tools/feel_tests.py` (the plain arithmetic on the host, the report on logs of known pacing, update cadence, camera motion
and input lag, the switch's own text compiled with counted stand-ins, `window.cpp`'s key lookup compiled against SDL's scancode header with a settable
keyboard state, and the hooks' wiring). On Windows nothing has been run, and the log's last flush is wired into the Linux exit paths only: a run that ends
through `ExitProcess` can lose the last `BEA_PORT_FEEL_FLUSH_S` seconds (default 30), so set it to a few seconds for a run that must keep its tail. What the measurement found in the Commander view, and the changes it supports, are in
PARITY.md ("Frame feel"); `BEA_PORT_COMMANDER_VIEW=ticks` (the camera moves with game updates and reads the key trap's record),
`=messages` (it moves with frames but reads the key trap's record) and `BEA_PORT_COMMANDER_TERRAIN=cells` (the paused map's terrain as
sprites) bring the earlier behaviours back on one build for a comparison. While a unit is driven (`play_unit=tank`) the report adds the
view's heading, pitch and roll from frame to frame: the change per frame, its second difference, and the share of all the motion that fell on frames
holding an update, which a camera that steps with the updates puts near 100% and a smooth one leaves at the share of such frames;
`BEA_PORT_TANK_VIEW=rigid` (the camera fixed to the unit's displayed pose, as the first render hook had it) and `=stock` (the game's own camera) bring the
earlier views back on one build.

## Commander live view: isolation runs

This section describes the current source branch's read-only Commander picture. It is not acceptance of the
separate native-world renderer or the intended RTS construction and production interface.

The Commander's live view never renders the world: the game draws the pilot's frame as it does with the view closed, and the view's own picture
(`game/ce_cmdview.cpp`) is drawn over it from read-only queries, through the device alone (PARITY.md, "Commander live view"). The rule it must keep
is that a battle with the view open advances exactly as one in which it was never opened. Three things hold it to that:

- `tools/ce_commander_tests.py` (no game): the picture's source names only the game facilities on its list of reads and none of the game's drawing,
  allocation, random or clock routines; the map's part of the live frame asks for none of the game's drawing state; the engine's presentation hook
  hands back the camera it is given; and the patches give the Commander no entry into the game beyond the five it has. Each with mutants.
- `tools/ce_commander_camera_tests.py` (no game): the map's own text compiled against authored engine doubles, among its controls that the engine is
  never given the strategic camera and that the draw filter is never taken.
- `tools/ce_commander_isolation_run.py` (the game, a test build): for each level it runs the level with the view never opened (twice, to learn the
  level's own noise), open for the whole run with the camera turning, and opening and closing every 90 Presents, and requires the update dumps of the
  last two to equal the first, update by update; it runs a control that must differ, and the footprint audit and its own control. Its judging is
  checked without a game by `tools/ce_commander_isolation_run_tests.py`.

      python3 port/build.py --target linux --public --commander-test --ce-version 0.2.0-rc0 -j 8
      python3 port/tools/ce_commander_isolation_run.py --exe local-data/port/build/linux/bea --out local-data/commander-isolation --level 110:330 --level 201:400

A test build's switches for these runs (none is compiled into a build without `--commander-test`): `BEA_PORT_COMMANDER_AUDIT=1` copies the game's
heaps and the executable's data before each frame of the picture and compares them after it, leaving out the Commander's own records, and the map's
close note in the probe lists what changed (`view audit ...`); `BEA_PORT_COMMANDER_AUDIT_LEAK=1` flips one byte of the game's data inside each
audited frame, which the audit must report; `BEA_PORT_COMMANDER_ENGINE_CAMERA=1` gives the engine the strategic camera again, as the view's first
form did, which the dump comparison must see. The close note also gives the picture's counts (`view picture ...`) and the frame periods with the
view open and closed (`view periods ...`: read them from a run made with `BEA_PORT_REPLAY_SLEEP_US=0`). The view opens only where the player has a
squad that takes orders; a level without one cannot be used for these runs, and the runner fails an arm whose picture was not drawn.

## Commander: a match on a versus level, and its orders

With `commander=1` under `[Experimental]` in `ce.ini`, a versus level started straight from the command line (`-level 851`, any level from 850 to
899) is a Commander match: one person, player 1, commands player 1's side on one full screen, and the second Battle Engine stands in the world for
the other side. The game's own "player 1 fullscreen" switch gives the one screen; patch 0235 makes the HUD and the end screen take their
one-player layout while a match shows it (the radar, the aiming ring, the battle-line dial, the radio and one Victory or Defeat), and changes
nothing anywhere else. A versus level chosen in the game's Multiplayer menu is two people's game and never a match (`game/ce_cmdrules.h`).

In the battlefield view, click a friendly unit or its squad marker to select the whole squad. Drag across units from
several squads to select them together; several units from the same squad still count as one. The squad card identifies
the selection. If you pan away, press C to bring the selected squad back to the centre of the view. You can pan, turn or
zoom again immediately. Right-click terrain to Advance, or use Fall Back to send the selection home. Esc cancels an armed order;
with no order armed, it returns you to the cockpit.

In the live view (M, and M or Esc back to the cockpit): a click picks a squad, a drag picks the squads inside its box, Shift adds or takes away,
Ctrl and A picks every squad that takes orders, Ctrl and a digit keeps the picked squads as a group and the digit picks them again (Shift and the
digit adds them). A right click orders an Advance, Ctrl and a right click a Move, H a Hold, B a Fall back, Delete gives the squads back to their
own orders; the four buttons under the squad card give the same orders. Fall back sends a squad home: to a rally in front of the nearest building of
its side on land (or, where the side has none, a battle-line node it holds), never to the map's edge. Several squads sent to one place each get a
place of their own round it, Fall back included; a Move or a Fall back becomes a Hold where it arrives. Esc calls off an order that waits for its
click before it takes you back to the cockpit. In a match the view warns of a lost building, a base under attack and a hurt Battle Engine; when the
Battle Engine goes down the view closes, the cockpit says so, and the view comes back with the engine (M while it is down stops or asks for that). The card names the lead squad and its order (Moving, Advancing,
Fighting, Arrived, Stuck, Holding, Falling back, None). The paused tactical map (V) is a tester's page, reached only with
`commander_tactical_map=1` in `ce.ini`; it orders the same selection with the same keys.

`tools/ce_commander_match_run.py` plays a test build (`--commander-test`) from a plan of keys, pointer events and order-tape lines and judges the
order layer's probe (its plan format and checks are in its header); `tools/ce_commander_match_run_tests.py` checks every judgement on an authored
probe, and `tools/ce_cmdorders_tests.py` holds the pure rules (`ce_cmdsel.h`, `ce_cmdoutcome.h`, `ce_cmdgroup.h`, `ce_cmdrules.h`) with mutants.

      python3 port/tools/ce_commander_match_run.py --exe local-data/port/build/linux/bea --out local-data/commander-match --plan PLAN.json

## Compile contract

`build.py` applies the flags below to both targets: Linux `--target=i686-pc-linux-gnu` and Windows
`--target=i686-w64-mingw32` (llvm-mingw), both clang. Every game translation unit (`src/`, compiled from its generated
mirror, and the hooks in `port/game/`) gets `-fms-extensions -fno-strict-aliasing -fwrapv -malign-double -mms-bitfields
-fshort-wchar`, and then one of two flag classes. Five files in `port/game/` (the standalone host-buffer units that `build.py` lists in `HOST_BUFFER_GAME_MODULES`:
`ce_network_boot_authority.cpp`, `ce_network_invitation.cpp`, `ce_project.cpp`, `ce_museum_visit.cpp` and
`ce_museum_visit_generate.cpp`) are the exception: `build.py` compiles them as port code, below. The two network ones are
compiled only with `--with-network` in the public build; `ce_museum_visit_generate.cpp` also takes `-iquote` for the mirror,
which answers its quoted includes and nothing else.

- **x87 class** (game sources, the generated mirror, and port code whose results must match the VC6 build, such as
  d3dx's `maths_x87.cpp`): `-std=gnu++14 -Wno-c++11-narrowing -Wno-reserved-user-defined-literal` (C: `-std=gnu99`; port
  code uses `-std=gnu++17`, below) `-mno-sse -mfpmath=387`. clang 22's i686 default does float maths in SSE, and
  `-mfpmath=387` is refused while SSE is on (measured). These files never include SDL headers. C++11 or later is forced
  by dxvk-native's headers (`constexpr` GUID helpers in `windows_base.h`); the two warning switches keep VC6-era forms
  compiling (HRESULT `case` labels above 0x7fffffff, `"text"MACRO`), measured on a probe TU that also showed x87-only
  code.
- **platform class** (`platform/`, anything that includes SDL): `-std=gnu++17`, the target's default SSE.

Port code (everything outside the game hooks in `port/game/`, plus the five files named above) compiles as
`-std=gnu++17` with the list above minus `-malign-double`. Its per-file flags come from the `flags.txt` files
(`file: -flags`) of `port/d3dx/`, `port/bink/` and `port/network/`, appended after the class flags (`build.py` also
reads one for `port/dsound/`, which has none): `maths.cpp` and `texture.cpp` take `-msse -mfpmath=sse`, where retail
d3dx9's SSE path is what players got, and `bink.cpp` and `native_posix_stat.cpp` take `-mno-ms-bitfields`
(`native_posix_stat.cpp` writes the native Linux stat layout). Other d3dx files keep the x87 switches, and the rest of
`dsound`, `bink`, `network` and `platform/` take the target's default SSE. The plain C files that `build.py` compiles
as port code (`vorbis/vc6crt.c`, `updater/sha256.c`, the pinned Monocypher source and the marker sources of the
optional ICE and native-signaling builds) use `-std=gnu99 -O2 -w` and none of the switches above.

The include order is `port/d3dx/include`, `port/include`, then dxvk-native's `usr/include/dxvk` (Linux) or llvm-mingw's
MinGW headers (Windows), so the port's d3dx and shim headers win. dxvk-native (Linux only) defines `WINAPI`,
`STDMETHODCALLTYPE` and `__stdcall` empty and `LONG`/`HRESULT` as `int32_t`. D3DX types keep the SDK layout
(`D3DXMATRIX` derives from `D3DMATRIX`; game code reads `.m` and `_11`). `-fshort-wchar` gives a 2-byte `WCHAR` on
Linux, as on Windows; no D3D9 entry point the game calls takes one.
