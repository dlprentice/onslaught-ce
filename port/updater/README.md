# Onslaught CE launcher and updater

Status: active (public builds only)
Last updated: 2026-10-27
Summary: game discovery and bounded logs, with automatic updating suspended and manual migration guidance.

`ce_launch.c` builds as `onslaught-ce-launch` (Linux, started by the `onslaught-ce` script after game preflight when needed) and
`onslaught-ce.exe` (Windows), from `build.py --public`. It links SDL3 (its dialogs) and Monocypher (Ed25519, pinned in
`deps.lock`, unused while automatic updating is suspended); `sha256.c` is its SHA-256. `ce_report.h` (header only, C
and C++) is what the launcher and the game share for "Report a problem".

## Finding the game

In order: `BEA_GAME_DIR`; `gamedir=` in `ce.ini`; every Steam library (Linux `~/.local/share/Steam`, `~/.steam/steam`,
`~/.steam/root` and the Flatpak's `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`; Windows the `SteamPath` value
of `HKCU\Software\Valve\Steam`), each library's `steamapps/common/Battle Engine Aquila`; then a message box and a folder
picker. A folder counts as a supported install when the game data hashed in `config/game-data.sha256` matches. The
answer is kept in `ce.ini`. The game folder is only read: the writable folder is per user (Linux
`~/.local/share/onslaught-ce`, Windows `%APPDATA%\Onslaught CE`; `BEA_USER_DIR` overrides) and holds `ce.ini`.

## Manual updates

Automatic update checking and installation are temporarily disabled. The launcher does not fetch `latest.json` or
its signature, create an update cache, download/unpack an archive, rename an installation or clean neighboring
folders. `update_check`, `BEA_NO_UPDATE_CHECK`, `BEA_UPDATE_FEED` and `BEA_UPDATE_ANSWER` do not enable those operations.
`--no-update-check` remains accepted for command-line compatibility.

`--update-only` prints manual-download guidance and exits **2**, before opening settings, logs or caches. That status
means updating is disabled; it does **not** mean the installed version is current. The historical `--apply` entry
point also refuses with exit 2 before accessing installation, user or update paths. `--version` still prints the version and exits 0 without
initializing a user folder. None of these commands starts a game or opens a browser.

Download an explicitly corrected release from <https://github.com/dlprentice/onslaught-ce/releases> and extract it
into a **new empty folder**. Preserve the whole previous installation, its neighboring backup/staging folders and
its user data. A relative `BEA_USER_DIR` can put launcher files and game user data in different locations. Identify and
preserve both complete folders and verify independent backups, including careers. Use a separate working copy of the
identified game user-data folder outside the original game and old CE program/staging folders; set `BEA_USER_DIR` to
its absolute path, check careers after migration and keep the originals and backups. Do not merge unclear or conflicting files. See
[the port guide](../README.md) and its linked player steps for the migration warning.

### Older launchers are not repaired by a new download

Source for releases through 0.1.6 contains destructive updater cleanup. Actual shipped Linux 0.1.6 was reproduced
deleting matching neighboring backup folders and contents reached through a matching root symlink, even with
`--no-update-check --update-only`. Native Windows reparse behavior was not tested. Older launchers also used
predictable download/unpack paths and an unchecked rollback. Do not invoke one to carry out the migration: a safer
successor cannot protect work already done by the old process. The published Windows 0.1.5 helper additionally
lacks the normal-token execution manifest. Neither an administrator launch nor a changed signature makes these
paths a safe migration route.

## The log

The launcher starts the game with its stdout and stderr on one pipe and copies them, with its own `onslaught-ce: ...`
messages, to the terminal and to `<user folder>/onslaught-ce.log`. At start the previous log becomes
`onslaught-ce.old.log`; each file stops at 4 MiB (a closing line says so; the terminal still gets everything).
A nonblocking kernel lease belongs to the logging launcher until its file is closed. Concurrent launches in the same
user folder use `onslaught-ce.concurrent-1.log` and `onslaught-ce.concurrent-2.log`, each with its own lease and 4 MiB
limit. Launcher-generated log content in these four files has a combined 16 MiB budget. A fourth simultaneous logger, or a failed/unsupported
lease, keeps terminal output and reports that no persistent log is available; the game still starts. Separate user
folders have independent leases. Only a leased slot may retire a previous closed, regular app-format log, and every
new output file is created exclusively. Links and foreign files are left alone; rotation/open failures use another
safe slot or terminal output. Data and lease handles must each have exactly one hard link. Foreign, linked or
oversized preexisting collision paths are preserved and refused, not retroactively bounded by the log budget.
Lease files remain in the folder and are never deleted; the kernel releases ownership
on process exit. This coordinates cooperating launcher versions on filesystems supporting these locks, without a
game single-instance rule or an updater lock. The first
lines are the version and build (`build.py` passes the source commit, `+dirty` for a changed tree), the OS, the start
time and the GPU: the game's `onslaught-ce: adapter` line (port/platform/common/d3d9hooks.cpp, printed at CreateDevice),
and on Linux the machine's GPUs from sysfs, since dxvk reports a stand-in name to Direct3D 9 by default. Output that
arrives before the adapter line is held (up to 1 MB) so the GPU stays at the top. The last line is the game's exit
status or the signal that ended it. The log is written to be attached to a public issue, so it never names the
player's account: the home folder is written `~` (Windows `%USERPROFILE%`, `%APPDATA%`, `%LOCALAPPDATA%`) and the account
name, as a path component anywhere else, `<user>`; the launcher's own lines say the same on the terminal. For diagnostic compatibility, an inherited legacy restart marker (`BEA_CE_LOG_CONTINUE`) still appends to the primary log
only after acquiring its lease and validating the file. If the primary is busy or unsafe, it starts a fresh fallback
slot instead; it does not append to a previous unrelated fallback session. The report page asks for current, old and
both concurrent logs from the same folder, when present. Logging artifacts are excluded from recording start files.
While `BEA_PORT_TAPE` or `BEA_PORT_RECORD` is set as the launcher starts
(a replay, or the test harness's recording), it writes no log, checks for no update and sets nothing for the CE layer. A Ctrl+C reaches the game; SIGTERM and SIGHUP sent to the launcher are passed on.
On Windows the launcher attaches to the console it was started from, if any, for the terminal copy. Inherited output
pipes, files and `NUL` keep their destinations: attaching must not redirect a tool's `--version` response or errors
to the console instead. On both, the launcher
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

## Windows output regression check

`python3 port/updater/tests/update_restart_tests.py` executes the actual launcher entry point and manual-update
message against authored host boundaries for both platform branches. It requires early refusal without opening
paths for `--update-only` and `--apply`, side-effect-free `--version`, and ordinary game dispatch without updater
calls. It neither starts a game nor establishes native Windows behavior.

`tests/launcher_stdio_driver.c` exercises the actual Windows launcher with redirected pipes, files and a mixed
console/pipe setup, both with and without a parent console. It checks `--version` and the disabled `--update-only`
response while an invalid private settings path remains unopened, so no game, network request or player profile is
needed. Build the driver with the pinned Windows
toolchain and run it beside the candidate's dependencies:

    i686-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror port/updater/tests/launcher_stdio_driver.c -o launcher_stdio_driver.exe
    launcher_stdio_driver.exe path\to\onslaught-ce.exe

Native Windows validation is required; running the same check under Wine is a separate result. The driver briefly
allocates its own console, so automated checks belong in an isolated test desktop or VM.

## Releasing

`build.py --target all --public --ce-version X.Y`, `tools/package.py X.Y`, then `tools/sign_release.py X.Y [--tag TAG]` (0.1 ships as `v0.1.0`), which writes
`latest.json` and its signature with the release key. The private key stays outside every repository; only its public half
is in `ce_launch.c`, and `sign_release.py` refuses a key whose public half differs. Upload both packages, the FFmpeg source
tarball, `latest.json` and `latest.json.sig` as the release's assets, tagged as given to `sign_release.py`.

These packaging/signing tools remain available, but this launcher does not consume the feed. Release and migration
validation must use the manual route above; a historic automatic-update receipt does not validate this suspended updater.
