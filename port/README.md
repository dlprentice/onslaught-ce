# Native port (Linux and Windows)

Status: active (both targets build, link and run; measured scope and limits are summarized in the public CE README's Known limitations; native Windows acceptance uses a software-rendered VM, not physical-GPU validation)
Last updated: 2026-11-10 (the dependency cache identity note; new-profile video defaults and the dodge_button key named; earlier, 2026-11-07: the unfinished features listed under [Experimental] in ce.ini; earlier, 2026-11-06: maintainer release steps marked, environment variables listed, files named but not in this tree; compile contract and migration wording corrected)
Summary: a second build of the decompilation's sources as native i686 Linux and Windows programs; layout, ownership,
commands and rules.

The port compiles the same `src/` the matching build compiles, with clang for i686 Linux and i686 Windows, against
Direct3D 9 (dxvk-native on Linux, the system's on Windows) and one SDL3 platform layer for the window, input and
audio. Retail data is read from the player's install; nothing retail is committed. The VC6 matching build is separate.
The [public decompilation](https://github.com/dlprentice/onslaught-decomp#reproducing-the-match) documents its pinned
inputs, measured matching coverage, compiled-object concession and remaining exclusions. The public CE README owns
the port's [known limitations](https://github.com/dlprentice/onslaught-ce#known-limitations) and
[public setup](https://github.com/dlprentice/onslaught-ce#building-from-source).

    python3 port/tools/deps.py build     # pinned inputs, both targets, into local-data/port
    python3 port/build.py --public [--target linux|windows] [-k]   # the game: local-data/port/build/<target>/bea[.exe]

`-k` keeps the build going after errors (passed to ninja as `-k 0`) and prints a census of the error sites by message and
file; the full log is `local-data/port/build/<target>/last.log`.

Files this tree names but does not hold: comments and guides under `port/` and `src/` sometimes name `PLAN.md`,
`PARITY.md`, `tools/ce_frontend_tests.py` (a maintainer-only check that reads font source this tree leaves out), `tools/decomp.py`, `docs/release.md`, `tools/release_rc.py` and folders such as `tests/` and `probe/`. They
belong to the maintainers' working source, which this repository is generated from, and are not in this tree; the
[public decompilation](https://github.com/dlprentice/onslaught-decomp) is the public home of the matching tools.

Licence: GPL-3.0 (`SPDX-License-Identifier: GPL-3.0-only`), matching the upstream Onslaught source (GPL version 3,
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

The default `--public` build also defines `CE_NETWORK_OFF`: the network (Host / Join, the world and session layers, the lockstep co-op prototype) is compiled out until enabled explicitly with `--with-network`. build.py compiles none of its modules in the default flavour (`network_module`: `port/network/`, `game/ce_network_*.cpp`, `game/ce_netpages.cpp`, `platform/common/ce_netlobby.cpp` and `ce_netcode.cpp`, monocypher) and links no ENet; the shared SHA-256 used by mod checks is still compiled. The hooks the network added to shared files of the port are inside the same guards. Every hunk of every patch listed in `network/PATCHES.txt` sits inside `CE_NETWORK_OFF` guards (an insertion in `#ifndef`, a rewrite in `#ifdef` original `#else` new `#endif`), so a default public build patches `src/` to exactly what main's patches alone produce: `tools/network_public_source_tests.py` builds both trees and requires them equal once the guards are resolved. `game/ce_network_boot.h`'s `CE_NETWORK_ENABLED` also hides the Network game entry and the `BEA_NET_*` roles in the modules a private build compiles (`tools/network_public_off_tests.py`). Private builds and `--test-automation` builds are unaffected.

`--public --with-network` is the same public build with the network compiled in (the experimental co-op build): it does not define `CE_NETWORK_OFF`, compiles the network modules, links ENet (`-lenet`, and `-lws2_32` on Windows) and writes `flavour.txt` (`public-network`) beside the objects, so that `tools/package.py` carries ENet's licence file and notice row and refuses a package whose build folder and program disagree (ENet in the default flavour, none in this one). It is refused without `--public` (a private build has the network already) and with `--test-automation`. Its objects differ from the default public build's: build it in a separate worktree. The default `--public` build is unchanged by it.

The default public build (no `--with-network`) is intended to compile the same source as the selected CE hub. Byte-identical output on a newly composed head still requires a clean build comparison; source checks alone do not establish it. Every difference the network made to a file of main is inside a `CE_NETWORK_OFF` guard, and the default build compiles main's text itself, not the guarded file: the preprocessor would skip the network's lines but still count them, which moves every `__LINE__` (the game's `new` passes one to the allocator), every line table, and with them the PDB GUID that the linker derives from the unstripped image and that the stripped `bea.exe` still carries. So `build.py` resolves the guards of the mirror (`resolve_network_off`) and compiles a copy of the port's code with them resolved (`build_port_view`, under `build/resolved/port`, mapped to `.` in the debug names and `__FILE__`), and the network's directory and the tools are not in that copy. `tools/network_default_text_tests.py` pins the result: `network/MAIN-TEXT.sha256` holds the sha-256 of main's text of every port file a default build can compile or include, `network/MAIN-SOURCES.sha256` that of every file of `src/` and `linklib/` and every patch of main's queue (the network's own patches are the ones `network/PATCHES.txt` lists); the suite fails if any resolved file differs, if a file that main lacks is compiled or included by the default build, or if a patch of main's queue is edited. After merging the selected CE hub, refresh the pins with `python3 port/tools/network_default_text_tests.py --pin <hub ref>`; a changed pin names a file whose guards need another look. To show it on a build, make the default flavour of this tree and of main in two worktrees with the same `--ce-version` and compare them object by object and program by program (`llvm-strip --strip-all` is what the packager does to `bea`); golden tapes replayed with probes at every Present remain the check that nothing else moved.

### Released launcher safety hold and manual migration

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
**Onslaught CE > Network Game** (`game/ce_netpages.cpp`): Host a game, or Join a game, for two-player co-op on the building defence (level 856) or the
tutorial (100). The original ships the building defence twice, as levels 856 and 858, with the same map and script (their level files differ in one byte of the world data and some
resource-table indices; frames of the two are identical), so the lobby offers it once; the gates admit 858 as well. Never in replays, recordings, probe or pilot runs (even with `BEA_PORT_CE_LAYER=1`), `-autoconfigtest` or the demo, where the CE page keeps
its earlier rows.

The instructions below describe direct-IP Host / Join, including LAN play and router forwarding. An optional build with `--with-ice` can select a TURN relay path; its setup and endpoints are described in [ICE-BRIDGE.md](network/ICE-BRIDGE.md) and [RELAY-ACCESS.md](network/RELAY-ACCESS.md). Direct-IP instructions do not establish relay availability or successful online gameplay.

What the direct-IP page tells the players. The packaged README.txt of this flavour says the same (`tools/package_readme_tests.py` pins the wording against this page's
text), and a release that offers the page repeats it in its notes:

- Experimental: new and unfinished, expect problems.
- Both players need the same Onslaught CE version and their own copy of the game; a different build is refused with both version labels.
- If the two games ever disagree, the game ends with a message (`co-op session stopped: the two games went out of step at tick N ...`); nothing is
  resynchronised. A network game does not change your careers: its process may open only the files its admission lists (`CE_BootLocalFileAllowed`: the options
  file `defaultoptions.bea`, `cardid.txt`, `setuphistory.txt` and `OnslaughtException.txt`), and a career is not among them. Its HUD scales and sits in the
  corners as in single-player play (patch 0157; `BEA_PORT_HUD_SCALE=0` turns the scaling off).
- There is no pause in a network game and Escape does nothing (while a session runs the lockstep controller replaces the game's button dispatch, and the shared
  input has no pause field); to leave, close the window. That ends this process without returning to the front end (`the window was closed: not returning to the
  front end`) and the other player is told "the other player left the co-op game".
- There is no matchmaking server: play on one network, or the host forwards its UDP port (`net_port` in ce.ini, default 27652) on its router and gives the
  other player its public address. The page "How to connect over the Internet" (a row of the Network game page) walks the host through the three steps: the port forward at the
  router, the Windows firewall prompt, and the public address (with how to tell a private address, and the shared range 100.64 to 100.127 that no port forward can work in).
  The notices that cannot know the cause say so: a guest that gets no answer is told "The game cannot tell why" and shown the causes as "may" clauses (wrong code or address, a
  host that is not waiting, a router or firewall blocking the port); a host that has waited 45 seconds with nobody reaching it is told the same.
- Not tested: play through routers over the real internet, more than two players, Windows on real hardware (the Windows build has been run against Linux
  under Wine; under Wine or Proton keep the game folder and the user folder on drive C: the settings reader refuses a folder reached through a link, which Wine's Z:
  drive is, and admission refuses a content file that has no volume serial number, which Wine reports for Z: paths on some host-mounted folders, as observed during one test), so a game or user folder reached through Z: can be refused at admission).
- What it contacts: only the other player's address (UDP). No server, no telemetry. To list this PC's addresses the Host page reads the network
  interfaces (Linux: `getifaddrs`, local only; Windows: `getaddrinfo` on `gethostname()`, which asks the system resolver for this PC's own name and may be
  answered by the local network). The code and the secrets are never logged.

- **Host**: this PC's IPv4 addresses, the UDP port (`net_port` in ce.ini, default 27652), the join code with Copy code,
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
  which admits the engineering session (852) or a co-op map (100, 856, 858) and its own content contract. After the match, unless
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

On a fresh profile CE writes `full_models=0` and `terrain_detail_distance=1` to `ce.ini` once; a valid line already there is a saved choice and is kept. `extra_graphics` is only read (a fresh profile asks for it On where the device passes the capability test; `extra_graphics=0` keeps it off) and CE does not write it on a fresh profile. The CE Settings row Dodge Button is the `dodge_button` key (on by default; `dodge_button=0` turns it off).

## Unfinished features (`ce.ini` `[Experimental]`)

`ce.ini` lists every unfinished feature, one line each, switched off, in a block that CE writes when the file lacks it (a new profile, or the first start of 0.1.14):

```
[Experimental]
; Unfinished features, off in this release. They may change or break. Set a line to 1 to try one.
map_editor=0
museum=0
commander=0
play_unit=0
```

They are for people who test those features, not a supported part of the game. Each line switches only its own feature (it is read once, the first time anything asks and at the
latest as the first main menu is built; a later edit applies at the next start), and a line that is missing, has any value but a lone `1`, appears twice, or sits in a `ce.ini` that cannot be read whole is off:

| Line | Feature |
| --- | --- |
| `map_editor` | the native map and mission editor: the Map Editor row of the Onslaught CE page and its map pages |
| `museum` | the native museum visit (Museum Prototype) and the Goodies Gallery (Museum Preview) row |
| `commander` | Commander: its CE Settings row (Commander Map), the M key, the live view and the orders |
| `play_unit` | playing a unit (a tank) |

With a feature's line off, its rows are not on the Onslaught CE page, a forced action of its rows does nothing, nothing it adds runs, and the Help pages do not describe it as present: without
`commander`, Controls & Tips drops the commander map's line and the Godot and Commander Ideas entry of What's Coming says it is in development; without `map_editor`, the Map and Mission Tools
entry says the same. With all four at 0 nothing unfinished is offered, and the faithful and replay paths and every ordinary career are as without them; a recording, a replay or a probe run
never opens `ce.ini` for these lines (Commander and `play_unit` ask only after their own fence has passed, and the block is written only where the CE menus are on).

**Older values.** `commander` and `play_unit` are the lines earlier builds already used: a `commander=1` written by 0.1.13 (the CE Settings row Commander Map: Yes) keeps the Commander map on, and
`play_unit=tank` (the older spelling; `play_unit=1` means the same, the tank being the one unit) keeps playing a tank on. The block does not repeat a line the file already has and
changes no value; those two lines stay where they were and the block adds only the lines that are missing.

**The block.** CE adds it once: the file's lines come first, byte for byte (a missing final newline is added), then a blank line and the block. A file that already has the line `[Experimental]`
is left as it is, including a line the player removed or changed, so the block is never written twice, and is not even opened for writing (the start keeps its inode, time and mode). When a write is needed it keeps the file's permission bits (a new file is private to its owner); a `ce.ini` the player made read-only, and a result longer than 1 MiB (the size every reader accepts), are refused: the start logs that the file is unchanged, once. A key that CE Settings or the Cheats page writes later goes above the header, and a
key that is there changes in place. Every reader of `ce.ini` takes lines that begin with `key=`, so the header and the `;` line change nothing: the game's readers (`ce.cpp`), the launcher
(`ce_launch.c`, `ini_get`) and `ce_ini_write`. (The Companion's launcher-settings editor was read, not run, against a file with the block: it matches lines by `key=` and keeps the others.)

Sharper Text stays a CE Settings row: it is off by default and labelled experimental, and not behind these lines. Network play is not behind them either: a default `--public` build does not contain
the network, so no `ce.ini` line can turn it on (`CE_NETWORK_OFF`; the build with `--with-network` is a source build for developers). `BEA_CE_PROJECT` (the project-launch prototype of
`game/ce_project.cpp`) is an explicit developer environment switch with its own required absolute roots, reachable from no menu, key or `ce.ini` line, and is unchanged.
Checks: `tools/ce_experimental_modes_tests.py` (the lines and the block over the actual `ce.cpp` reader and writer on scratch folders, and the entry points' wording),
`tools/ce_frontend_tests.py` (the pages with each line on alone, and with all off), `updater/tests/ce_ini_tests.py` (the writer), `tools/ce_settings_tests.py` (the launcher's and the game's readers over files with the header) and `tools/ce_commander_tests.py`.

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
