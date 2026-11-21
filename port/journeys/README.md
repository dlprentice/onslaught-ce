# Journeys: features driven by named steps on one clock

Status: active (the test kit, step 1 of the CE core proposal)
Last updated: 2026-11-17
Summary: how a feature's journeys are written and run, what the test kit's verbs are, which clock a run uses, and the gap
between a test-kit build and the binary a player gets.

## What a journey is

A journey is a folder `port/journeys/<feature>/<name>/` with:

- `steps.txt`: the steps, one per line, run inside the game by the test kit (below). `#` starts a comment.
- `seed.json`: the profile the game starts from, as `{"files": {PATH: {"text": ...} | {"fixture": NAME, "sha256": HEX}}}`.
  A fixture is a file from the folder given by `--fixtures` (or `BEA_JOURNEY_FIXTURES`), pinned by its hash; no save is
  tracked here and none is made up.
- `expect.json`: the clock and its step (`"clock"`, `"dt"`), the game's arguments, the run's time limit, the files whose
  hashes the result records (`"files"`) and the hashes they must have (`"hashes"`).

`@RUN@` (the run's folder) and `@BACKUPS@` (a fresh folder in it) may appear in steps and in seed text.

Run them with the shared runner:

    python3 port/tools/journey.py --exe <test-kit build> --candidate <exact binary> --all
    python3 port/tools/journey.py --exe <test-kit build> --journey restore/restore-as-new
    python3 port/tools/journey.py --list

Each run has a new UTC-stamped name and writes one `result.json` (pass or fail, every step's reply, the dumps, the file
hashes, the clock and step, both binaries' hashes and the commit). The runner finds the build's libraries from its own link
receipt.

## How to write a journey, and add a verb, page or state key

This is the page to read before a feature's first journey. The two worked examples are `restore/restore-as-new` and
`catalogue/speech-english`; both pass on the public and network test-kit builds.

### 1. Build the two binaries

    python3 port/build.py --target linux --public --test-kit ...    # the test-kit twin: journeys run here
    python3 port/build.py --target linux --public ...               # the exact candidate: key-only smoke routes run here

Same commit, same flags, apart from `--test-kit`. Copy each `bea` (with `onslaught-ce-launch` and `.bea-sdl3-link.json`
beside it) somewhere of your own before building the other.

### 2. Write the journey folder

`port/journeys/<feature>/<name>/` holds `steps.txt`, `seed.json` and `expect.json`.

**Start from a real profile, never an invented one.** `restore/restore-as-new/seed.json` starts with the tracked career,
pinned by hash, and a `ce.ini` that turns on what the journey needs:

    {"files": {
      "ce.ini": {"text": "career_backup_folder=@BACKUPS@\nextras=1\n"},
      "savegames/Gold.bes": {"fixture": "gold_career_save.bin", "sha256": "0c17e47d..."}}}

**Say the clock.** `expect.json` names the fixed clock and its step (`"clock": "fixed", "dt": "0.016667"`), the game's
arguments, a time limit, and the files whose hashes the result must show.

**Steps reach things by name, and wait on state, not on time.** `restore/restore-as-new/steps.txt`:

    wait ticks 600                                   # the title screen
    key RETURN
    wait page fe:main within 900
    go "Onslaught CE" > Extras > "Service Record" > Gold > "Back Up Career"
    wait page xp:backup
    select row "Back Up Now"
    wait state backup.state=ok within 900            # the feature's own state key says the backup finished
    ...
    select row "Restore as New"
    wait state restore.outcome=complete within 900
    assert file "savegames/Gold restored.bes" sha256=0c17e47d...
    ...
    go "Load Game"
    select row "Gold restored"
    wait page fe:level-select within 900            # the restored career really loads

`catalogue/speech-english` is shorter: open the Speech list, `select row #1`, `wait page box`, `assert catalogue.playing=yes`,
`select row Continue`, the same for `#2`, then `back` and `assert catalogue.playing=no`.

Rules that save a run:
- Never count rows. `go` and `select row` find the row by its text, so a row added above it (the Network Game row on a network
  build) does not break the route. `#N` is for lists whose text is data (a list of sound lines).
- Wait for the page or the state, with `within N` when it is slow. A step that runs out of ticks fails with a dump of the
  screen; read that dump first.
- Use INPUT verbs for the outcome you are proving. A TESTWRITE verb (a pose, `test-dirty`) marks the run, and acceptance needs
  the same outcome reached with INPUT verbs alone.
- `dump LABEL` at each point a reviewer will want to see; `shot LABEL` for a frame.
- A journey only some builds can take begins with `require KEY=VALUE` (`require build.network=yes`); elsewhere it ends as
  skipped.

### 3. Run it

    python3 port/tools/journey.py --exe <twin>/bea --candidate <candidate>/bea --journey <feature>/<name> \
        --fixtures <folder with gold_career_save.bin>

It prints `PASS`, `FAIL` or `SKIP` and the path of `result.json`. `verbs.jsonl` beside it holds every step's reply and every
dump. On a failure, the last dump shows the page, its rows, the focused row and every state key.

### 4. Add what your feature needs, in a kit file beside it

Everything goes in `port/game/ce_<feature>_testkit.cpp`, which only `build.py --test-kit` compiles. It reads your feature
through its ordinary header; if the header lacks a read the kit needs, add a plain accessor there (always compiled, changing
nothing). Never put kit code in the feature's own file: the twin must stay the same compile as the player's binary. There is
no shared list to edit.

**A state key** (the Catalogue's, `port/game/ce_catalogue_testkit.cpp`):

    #include "ce_catalogue_sounds.h"
    #include "../platform/common/ce_testkit.h"
    #include <stdio.h>
    namespace {
    bool StatePlaying(char *out, size_t size) {
        snprintf(out, size, "%s", PortCeCatalogueSoundsPlaying() ? "yes" : "no");
        return true;              // return false when the key does not apply now (dump then leaves it out)
    }
    }
    CE_TEST_STATE("catalogue.playing", StatePlaying);

Name keys `<feature>.<thing>`, with plain lower-case values a journey can compare.

**A page**, so `go`, `select row`, `wait page` and `dump` see your screen. Its functions read your page's own model (never
pixels), and have exactly these types:

    bool Active();                              // your page is the one on screen now
    void Id(char *out, size_t size);            // "<family>:<page>", such as "xp:sounds"
    void Title(char *out, size_t size);         // its title, or "" (write out[0] = 0)
    int  Rows(CeTestRow *out, int max);         // fill out[i].text (UTF-8) and out[i].enabled; return the count
    int  Focused();                             // the highlighted row, or -1
    void Box(char *out, size_t size);           // the text a `wait notice` reads (a message, a notice), or ""
    CE_TEST_PAGE("family", PRIORITY, Active, Id, Title, Rows, Focused, Box);

`CE_TEST_PAGE` takes five more optional numbers for a list that pages by a key: rows in view, then the SDL scancode and PC
scan code of the page-down key and of the page-up key (the Extras host: `10, 79, 0x14d, 80, 0x14b`). When two pages say
they are active, the higher priority is the one shown. The built-in pages:

| family | priority | pages |
| --- | --- | --- |
| `box` | 100 | a message or details box, over anything |
| `ed` | 50 | the map editor |
| `xp` | 40 | the Extras host |
| `ce` | 30 | the Onslaught CE page |
| `pause` | 20 | the pause menu in a level |
| `fe` | 10 | the original front end, including its title screen (`fe:intro`) |

A page drawn over the front end needs a priority above 10, and one drawn over a box above 100. See the Extras host's in
`port/game/ce_extras_testkit.cpp` (it reads the host through `ce_extras.h`) and `port/game/ce_testkit_pages.cpp`.

**A verb**: `CE_TEST_VERB(name, usage, CE_TEST_INPUT or CE_TEST_TESTWRITE, fn)`, where

    CeTestResult fn(CeTestArgs &a, CeTestReply &r);   // return CE_TEST_DONE, CE_TEST_PENDING or CE_TEST_FAILED

runs once per tick until it returns `CE_TEST_DONE` or `CE_TEST_FAILED`. `a.word[0..a.count-1]` are the step's words
(`a.word[0]` is the verb), `a.tick` and `a.started` the tick now and when the step began, `a.scratch[0..15]` its own state
(zeroed when the step starts). Return `CE_TEST_PENDING` to wait (the step's budget still applies) and put a short reply in
`r` with `CeTestReplyf(r, "...", ...)`. Tag a verb TESTWRITE when it writes game state directly instead of going through the
player's input (`editor-pose` in `port/game/ce_editor_testkit.cpp`). All of these come from
`port/platform/common/ce_testkit.h`.

### 5. Before you hand it over

- The host test still passes: `python3 port/tools/test_kit_tests.py`.
- A plain public build carries none of the kit:
  `python3 port/tools/test_kit_tests.py --kit-bin <twin>/bea --plain-bin <candidate>/bea`.
- Your feature's isolation spec passes, if it is an optional mode (`port/tools/isolation.py`, below).

## The exact-binary gap

A journey runs on a **test-kit twin**: `build.py --test-kit` with the same commit and the same other flags as the candidate.
That binary is not the one a player gets: it carries the verb channel (`CE_TEST_KIT`), and `package.py` refuses to package
it. The gap is closed two ways:

1. The runner records the twin's hash, the candidate's hash (`--candidate`) and the commit in every result.
2. Every feature keeps at least one **key-driven smoke route**: a journey with `keys.txt` in place of `steps.txt`
   ("PRESENT KEY" lines through the player's own input path). It runs on the candidate itself and passes on its file hashes
   and the log lines its `expect.json` names. Acceptance "seen on the exact binary" means these routes.

## Steps (the test kit's verbs)

Every verb is tagged **INPUT** (it only produces input a player could produce, or reads state) or **TESTWRITE** (it changes
game state directly). A journey that uses a TESTWRITE verb is marked in its result, and a feature's acceptance must also
reach the same outcome by INPUT verbs alone at least once.

| Step | Tag | Does |
|---|---|---|
| `go A > B > C` | INPUT | selects each row by its text, waiting for the page to change after each but the last |
| `select row "TEXT"` / `select row #N` | INPUT | moves the cursor there with real keys, then Return |
| `focus row "TEXT"` / `#N` | INPUT | moves the cursor only |
| `back` | INPUT | Esc |
| `key NAME[+NAME] [hold N]` | INPUT | one raw key or a chord (`LCTRL+Z`): RETURN, ESC, arrows, PGUP, PGDN, TAB, GRAVE, F1 to F12, 0 to 9, LCTRL, LSHIFT, LALT, A to Z, ... |
| `wait page ID` / `wait notice "TEXT"` / `wait state KEY=VALUE` / `wait ticks N` | INPUT | holds until true |
| `assert KEY=VALUE` / `assert file PATH sha256=HEX` / `assert file PATH exists` / `assert page ID` | INPUT | fails the journey if not true |
| `dump [LABEL]` | INPUT | page id, title, rows, focused row, box text, tick and every state key that applies |
| `shot LABEL` | INPUT | one frame (PNG in the run folder) |
| `press NAME[+NAME]` / `release NAME[+NAME]` | INPUT | hold keys down across later steps (drive while turning); a journey's end lets them all go |
| `mouse-move DX DY [over N]` | INPUT | a relative movement (the mouse in a level), spread over N ticks so the parts add up exactly |
| `mouse-down` / `mouse-up` | INPUT | the left button where the pointer is (held fire) |
| `click X Y` / `rclick X Y` / `hover X Y` / `drag X1 Y1 X2 Y2 [over N]` | INPUT | pointer events at window points (left/right click, hover, drag), through the player's mouse path |
| `test-dirty PATH` | TESTWRITE | appends a line to a profile file: only the isolation proof's control uses it |
| `editor-pose X Y [DISTANCE [YAW [PITCH]]]` | TESTWRITE | puts the map editor's view over a map point (registered in `ce_editor.cpp`) |
| `until KEY=VALUE every N do STEP` | INPUT | repeats a step every N ticks until the state test holds (a take: press E while turning) |
| `repeat COUNT every N do STEP` | INPUT | runs a step COUNT times, N ticks apart (a control's same presses) |
| `mouse-move ... background` / `mouse-stop` | INPUT | a movement that runs on under later steps, and its end |
| `probe-line X1 Y1 Z1 X2 Y2 Z2` | INPUT | what a line hits first, read-only: `world.line` = type, distance, thing number, class, hit height (z grows down; over water the line stops at the surface and still says ground) |
| `probe-ground X Y` | INPUT | the map's height and the water level there, read-only: `world.ground` |
| `require KEY=VALUE` | INPUT | ends the journey as skipped (not failed) on a build where the state differs, such as `build.network=yes` |

State tests (`assert`, `wait state`, `until`) take `KEY=VALUE`, `KEY=*` (the key applies at all), one field of a
comma-separated value (`playunit.pose[3]`), and `<`, `>`, `<=`, `>=` on numbers. Before the first step runs, every line is
checked: a line naming no verb refuses the whole journey. `expect.json` and `seed.json` are read strictly (a key given twice,
or one the runner does not read, refuses the journey). `expect.json` may name a `control` journey and the profile files that
must be byte-equal to it after both runs: `{"journey": "feature/name", "equal": ["savegames/*", "ce.ini"]}`.

Rows are read from the page's own model, never from pixels; the cursor still moves by real key events, and the step checks
that it moved. Text matches a whole row first, then a unique start of one. Any step may end with `within N` (ticks; 600
otherwise). A step that runs out of ticks, an unknown row or an ambiguous one fails the journey with a dump.

Page ids: `fe:main`, `fe:directory` (Load Game), `fe:level-select`, ... (the original front end); `ce:hub`, `ce:catalogue`,
... (the Onslaught CE page); `xp:hub`, `xp:careers`, `xp:record`, `xp:backup`, `xp:backup-preview`, `xp:sounds`, ... (the
Extras host); `pause:N` (the pause menu); `ed:map` (the map editor, whose box is its current notice); `box` (a message or
details box, over any of them).

### The same compile as the player's binary

A test-kit build is the candidate's own compile plus the kit's files: every file named `*testkit*` (`port/platform/common/
ce_testkit*.cpp`, `port/game/ce_<feature>_testkit.cpp`) is compiled only by `build.py --test-kit`, and no other file
changes. The platform reaches the kit through `port/platform/common/testhooks.h`, a hook pointer that is null in every other
build; features are read through plain accessors in their own headers. `build.py` writes `.bea-objects.json` beside each
program (every object's hash), and `journey.py` refuses a twin whose objects, the kit's own aside, differ from the
candidate's (`--candidate`). Without a candidate a run must say `--unchecked-twin`, and its result says so. `result.json`
records `same_compile` and `twin_objects_equal`.

Today's state keys: `game.state`, `game.level`, `frontend.page`, `build.network` (yes in a `--with-network` build),
`editor.phase`, `editor.view`, `editor.selected`, `backup.state`, `preview.verified`, `restore.phase`, `restore.outcome`,
`restore.target`, `catalogue.playing`.

## Clocks

`BEA_PORT_CLOCK` chooses the clock the game reads (`port/platform/common/clock.cpp`). Journeys run on **fixed**, with the
step named in each `expect.json`; the default step, 0.016667 s, is the one the recorded original-game tapes use. Input is addressed by game tick
(a Present on that clock). The session log stays on under every test clock: its stamp is SDL's host clock, which no game
clock counts.

| clock | what it is for |
|---|---|
| real (unset) | ordinary play; frame-feel measurement |
| present (`run.py --virtual`) | screenshots that must repeat; older routes |
| shim | parity with retail's golden traces (REPORT-5/6) |
| fixed (journeys' default) | every feature journey; the recorded original-game tapes |
| feel pace (`BEA_PORT_FEEL_PACE_HZ`, real time paced to a rate) | audio measured against keys at a set rate |

### The hub's Network Game row

On a network build the Onslaught CE hub has a Network Game row, which moves every row below it down by one, so a route that
counts rows reaches the wrong one. The row is offered by the run mode (`port/platform/common/runmode.h`, decided once at
start): in ordinary play and in a test-kit journey on any clock, and in no recording, replay, engineering session, probe
run or other test run. `kit/network-row` routes past it by name on the fixed clock.

## Builds with ICE (temporary)

ICE is not yet a canonical dependency (that is decided with the co-op merge). Until then a `--with-ice
--with-native-signaling` twin is built with `BEA_PORT_DATA` set to the older dependency root that has the ICE relay, and
run with `--lib` pointing to that root's runtime libraries (for libcurl-gnutls). Both paths depend on the machine, so
they are given on the command line, not written here.

## What stops the kit on Windows (not fixed yet)

- `build.py` refuses `--test-kit` for any target but Linux.
- `ce_testkit.cpp` uses `strcasecmp` from `<strings.h>` and `strtok_r`, which the Windows toolchain lacks (`_stricmp`,
  `strtok_s`).
- `journey.py` finds libraries from the Linux link receipt and `LD_LIBRARY_PATH`, and starts the game through `run.py`'s
  offscreen Linux session; a Windows run needs Wine or the Windows VM, and neither has a journey driver.
- The kit's shot, the end-of-journey close and the key path are wired in the shared SDL window and D3D9 hooks. Whether the
  Windows build takes the same paths has not been checked.

## The isolation proof

`port/tools/isolation.py --exe <test-kit build> --feature F` runs the standard proof for an optional mode from
`port/journeys/F/isolation.json` (`base`, `never_opened`, `opened`, `allowed`, `control_file`). Each run's change set is the
profile files whose hash differs from its seed.
1. Never opened equals base: the feature switched on but never opened changes the same files as the feature switched off.
2. Opened: the feature, used and left, changes nothing outside the base's change set and the allowed globs.
3. Control: the opened journey plus `test-dirty CONTROL_FILE` must be caught by leg 2's comparison; if it is not, the tool is
   broken and the proof fails.
4. Audit: `isolation.json` in the output folder records every run, change set, binary hash, the commit and the verdict.
`catalogue/isolation.json` is the first spec.

## Known traps

Each of these has cost a run. A journey that walks into one should say so in a comment.

- **A direct level start is never the control for a menu route.** `-level N` writes a different options file than starting the
  same level from the menus, so a comparison of their profiles differs for a reason the feature did not cause. A journey's
  `control` is itself a journey, with the same route and input and the feature unused.
- **The Commander is fenced off under any test clock.** Its gate (`kFenced`, `port/game/ce_commander.cpp`) shuts it whenever
  `BEA_PORT_CLOCK` (any value, the fixed clock included), a tape, probes, recording, a pilot or autopilot, or a network role is
  set; only a `--commander-test` build with `BEA_PORT_COMMANDER_PROOF=1` gets past it. So no journey reaches the Commander
  until that gate asks the run mode (`runmode.h`), as the network pages now do. That change is the Commander's owner's.
- **Level 851 under the virtual or shim clock plays split-screen.** Seen with `-level 851` under `--virtual` and the shim
  clock, not on the real clock; the cause is not known and the fixed clock has not been tried. Do not use a direct 851 start
  under a test clock as evidence about the level.
- **Pages ignore early presses.** A page fading in, or the Extras host in its first 0.4 s, drops a select. The kit waits for a
  page to settle (30 ticks, and 0.5 s of wall time on the real clock) before pressing; a key route written by hand must too.
- **The real clock is not a comparison.** Two runs on the real clock differ for timing reasons alone. Compare runs on the
  fixed (or virtual) clock; a real-clock difference is not a finding.
- **Recording turns the session log off.** `BEA_PORT_RECORD` (and tapes and probes) turn the log off, and a judge that reads
  the log then sees nothing. The kit refuses tapes and recording; keep the two apart.
- **Run folders must be new.** A judge that finds an old run folder reads an old verdict. `journey.py` names every run afresh
  and a judge should read only the run it is given.
- **The network pages are offered by the run mode.** Host and Join show only in ordinary play and in test-kit journeys
  (`port/platform/common/runmode.h`); a hand-made run with a probe, tape or recording switch will not see them.
- **The mouse in a level is relative.** Turning and aiming in a level read relative movement (`mouse-move`); `click` and
  `drag` move the pointer to window points, which the menus and the editor read.

## Agent play & visual inspection gallery

`port/tools/agent_play.py` drives Onslaught CE completely offscreen (`SDL_VIDEO_DRIVER=offscreen`,
`SDL_AUDIO_DRIVER=dummy`, desktop/session environment variables stripped, `xdg-open` shimmed, isolated per-run user
folder, and optional `flock` GPU serialization via `--gpu-lock` or `GODOT_GPU_LOCK`). It never opens a desktop window
or touches a player profile.

Run an ad-hoc step script (on a `--test-kit` twin) with inline steps or a file, custom `ce.ini` switches, PNG captures,
and extracted `telemetry.json`:

    python3 port/tools/agent_play.py --exe <twin>/bea --out local-data/play-out \
        --ce-ini commander=1 --level 110 --clock real \
        --step "wait state game.state=3 within 2400" --step "wait ticks 350" \
        --step "key M hold 15" --step "wait ticks 160" --step "shot commander"

Run an exact-binary key route (no test kit) using `--keys` / `--fake-keys` and `--shot-at` / `--quit-at`:

    python3 port/tools/agent_play.py --exe <candidate>/bea --out local-data/play-exact \
        --keys route.txt --shot-at 900 --quit-at 960

Capture the full repeatable 1280x720 visual inspection gallery (Main Menu, CE Hub, CE Settings, Video Options, Extras,
Asset Catalogue, Mods Menu, Level 100/110 Walker & Jet HUD, Photo Mode, Commander Live View + unit panel, Play Any Unit,
Museum Hall & Cinema, and Map Editor) plus `manifest.json` and per-scene `telemetry/*.json`:

    python3 port/tools/agent_play.py --gallery --exe <twin>/bea \
        --fixtures <folder with gold_career_save.bin> --out local-data/gallery
    python3 port/tools/agent_play.py --list-gallery

**Clock rule for live-play modes:** `BEA_PORT_CLOCK` fences Photo Mode (`F11`), Commander Live View (`M`), Play Any Unit
(`E`), Mods loading, and CE Video Options rows. Pass `--clock fixed` (default, 60 Hz) for deterministic menu/catalogue/
flight/editor journeys, and `--clock real` (leaves `BEA_PORT_CLOCK` unset) whenever driving fenced CE modes. Because
offscreen `--clock real` runs without vsync at 150-400 FPS while gameplay polls at 20 Hz, hold gameplay keys for 10-15
ticks on `--clock real`. Widescreen offscreen captures set `BEA_PORT_WIDE_TEST=WxH` automatically from `--res`
(default `1280x720`).

## A base's known failures

`port/tools/known_failing.py --exe <test-kit build> --out FILE` runs every suite and every journey at the checked-out commit
and records each outcome, so an integration diffs its own run against the base's instead of rediscovering old failures.
Outcomes: pass, fail, timeout, skipped (`--skip`, or a journey whose `require` this build does not meet), needs-arguments
(a suite run by its own driver) and needs-windows (`--windows-suite`: a suite that starts Wine or the VM; recorded, never
started).
