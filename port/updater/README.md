# Onslaught CE launcher and updater

Status: active (public builds only)
Last updated: 2026-10-23 (session tape 2: the menus' mouse, typed text; the audio monitor)
Summary: how the public build's launcher finds the game, checks the signed release feed and updates itself.

`ce_launch.c` builds as `onslaught-ce-launch` (Linux, started by the `onslaught-ce` script after `preflight.sh`) and
`onslaught-ce.exe` (Windows), from `build.py --public`. It links SDL3 (its dialogs) and Monocypher (Ed25519, pinned in
`deps.lock`); `sha256.c` is its SHA-256. `ce_report.h` (header only, C and C++) is what the launcher and the game share
for "Report a problem".

## Finding the game

In order: `BEA_GAME_DIR`; `gamedir=` in `ce.ini`; every Steam library (Linux `~/.local/share/Steam`, `~/.steam/steam`,
`~/.steam/root` and the Flatpak's `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`; Windows the `SteamPath` value
of `HKCU\Software\Valve\Steam`), each library's `steamapps/common/Battle Engine Aquila`; then a message box and a folder
picker. A folder counts as a supported install when the game data hashed in `config/game-data.sha256` matches. The
answer is kept in `ce.ini`. The game folder is only read: the writable folder is per user (Linux
`~/.local/share/onslaught-ce`, Windows `%APPDATA%\Onslaught CE`; `BEA_USER_DIR` overrides) and holds `ce.ini`.

## Updates

On start the launcher fetches `latest.json` and `latest.json.sig` from the project's latest GitHub release (system
`curl`, https only). The signature is Ed25519 over the file's exact bytes and is checked against the public key compiled
into the launcher before anything in the file is used; a feed that fails is ignored without a prompt. If the feed names a
newer version (and it was not skipped), the launcher asks: Update, Later, or Skip this version. On Update it downloads the
platform's archive (only from this project's GitHub release downloads, whatever the feed says) beside the install, checks its SHA-256 from the verified feed, unpacks it with the system `tar`, and
renames the install to `<folder>.old-<version>` and the new folder into its place (on Windows a copy of the launcher in
`%TEMP%` does the renames after the launcher exits). Any failure leaves the installed version as it was. The old folder
is removed on the next start. Nothing else is sent: no identifiers, no telemetry. Off: `update_check=0` in `ce.ini` or
`BEA_NO_UPDATE_CHECK=1`. `--update-only` checks (and updates) without starting the game; `--version` prints the version.

Tests use `BEA_UPDATE_FEED` (another feed, which may then be a `file://` URL) and `BEA_UPDATE_ANSWER` (update, later or
skip, instead of the dialog); the signature and hash rules are unchanged.

## The log

The launcher starts the game with its stdout and stderr on one pipe and copies them, with its own `onslaught-ce: ...`
messages, to the terminal and to `<user folder>/onslaught-ce.log`. At start the previous log becomes
`onslaught-ce.old.log`; each file stops at 4 MB (a closing line says so; the terminal still gets everything). The first
lines are the version and build (`build.py` passes the source commit, `+dirty` for a changed tree), the OS, the start
time and the GPU: the game's `onslaught-ce: adapter` line (port/platform/common/d3d9hooks.cpp, printed at CreateDevice),
and on Linux the machine's GPUs from sysfs, since dxvk reports a stand-in name to Direct3D 9 by default. Output that
arrives before the adapter line is held (up to 1 MB) so the GPU stays at the top. The last line is the game's exit
status or the signal that ended it. The log is written to be attached to a public issue, so it never names the
player's account: the home folder is written `~` (Windows `%USERPROFILE%`, `%APPDATA%`, `%LOCALAPPDATA%`) and the account
name, as a path component anywhere else, `<user>`; the launcher's own lines say the same on the terminal. When an update
restarts the launcher (`BEA_CE_LOG_CONTINUE`, set only for that restart), the new launcher continues the same log, so the
run before it stays in `onslaught-ce.old.log`. While `BEA_PORT_TAPE` or `BEA_PORT_RECORD` is set as the launcher starts
(a replay, or the test harness's recording), it writes no log, checks for no update and sets nothing for the CE layer. A Ctrl+C reaches the game; SIGTERM and SIGHUP sent to the launcher are passed on.
On Windows the launcher attaches to the console it was started from, if any, for the terminal copy. On both, the launcher
stops reading once the game has exited and its last output is read, even if a program the game started (a browser) still
holds the pipe.

## Session recording

`--record`, or `record_sessions=1` in `ce.ini` (the main menu's Onslaught CE page writes it), starts the game in record mode (port/platform/common/replay.cpp):
`BEA_PORT_RECORD=<user folder>/tapes/YYYYMMDD-HHMMSS.tape` and, unless set, `BEA_PORT_CLOCK=shim`, so the game runs
at 30 Presents a second and its tape replays exactly. The launcher prints one line saying so. Never while a tape is
replayed (`BEA_PORT_TAPE`) or another recording is set (`BEA_PORT_RECORD`). In record mode the CE layer is off, the
front end and pause menu get keys, typed text (tape 2's `C` lines) and the window mouse (`W` lines; no focus messages) and the battle
gets the DirectInput mouse. The launcher also sets `BEA_AUDIO_MONITOR=1` unless it is set, so the session is heard
(port/dsound/README.md: a write-only monitor of the virtual clock's mix).

Because the CE layer is off while recording, the Onslaught CE page that switched `record_sessions=1` on is hidden in
every recorded session. So while it is set, each start asks first (an SDL message box): **Record** (Enter), **Not this
time** (Escape) or **Stop recording**, which sets `record_sessions=0`. `--no-record` does the same as Stop recording
without asking; `--record` records without asking. Where no box can be shown (no display), the session is recorded, as
set. Tests answer with `BEA_RECORD_ANSWER=record|once|stop`. The launcher and the game change `ce.ini` the same way
(`ce_ini_write`, ce_report.h): the whole file is written to `ce.ini.tmp`, every write and the close are checked, and it
is renamed over `ce.ini`, so a failed write leaves the old file whole.

The tape is replay.cpp's format 2 (`# onslaught tape 2`, then `K`, `M`, `C`, `W_CLIENT` and `W` lines). The port's
parser counts and skips lines it does not know; the rebuild's ([onslaught-godot](https://github.com/dlprentice/onslaught-godot)
`rebuild/OnslaughtRebuild.Parity/RetailTape.cs`) rejects the header and the `C` and `W` lines until it learns them, and
a build that reads format 1 drops them and drifts, so `run.py`, `check_tape.sh` and `replay_session.py` refuse a newer
format. The session's details go in a sidecar `<name>.tape.json`: format (`onslaught-ce session tape 2`), the tape line
kinds, `audio_monitor`, tape name, CE version, build, platform, OS, UTC date, clock, Presents a second,
the game arguments, and `start_files`, the user folder's game files as the session started (options, careers, the
game's own text files; base64; the launcher's files and anything over 1 MB left out; 4 MB in all). Replay a player's
session from a fresh writable folder with its start files:

    python3 port/tools/run.py NAME --shim-clock --tape S.tape --start S.tape.json --probes --every 1 -- GAME_ARGS

## Report a problem

`--report` opens `https://github.com/dlprentice/onslaught-ce/issues/new?template=bug_report.md&title=...&body=...`
in the browser (`SDL_OpenURL`; `BEA_REPORT_NO_OPEN=1` only prints it), with the body in the template's sections, the
version, build and OS, and the paths of the log and the newest tape (home or `%APPDATA%` shortened to `~` or
`%APPDATA%`). It prints the user folder and the address; on Windows without a console a message box names the folder.
The main menu's Onslaught CE page (port/game/ce_frontend.cpp) calls the same page through `extern "C" int CE_OpenReportPage(void)` (port/platform/common/ce.cpp,
public builds only; it does nothing where the CE layer is off). Nothing is sent: the player submits the issue or not.
The issue template is `public/.github/ISSUE_TEMPLATE/bug_report.md`, exported to the repository's `.github/` with
`config.yml` beside it (a link to the security advisory form); the body above repeats the template's sections.

## Releasing

`build.py --target all --public --ce-version X.Y`, `tools/package.py X.Y`, then `tools/sign_release.py X.Y [--tag TAG]` (0.1 ships as `v0.1.0`), which writes
`latest.json` and its signature with the release key. The private key stays outside every repository; only its public half
is in `ce_launch.c`, and `sign_release.py` refuses a key whose public half differs. Upload both packages, the FFmpeg source
tarball, `latest.json` and `latest.json.sig` as the release's assets, tagged as given to `sign_release.py`.
