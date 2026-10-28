# Onslaught CE

Status: preview — from 0.1.8 on it updates itself; from 0.1.7 or earlier, download the latest release once by hand
Last updated: 2026-10-28
Summary: the native Battle Engine Aquila port, the one-time manual move from 0.1.7 or earlier, signed updates and source build.

A native port of *Battle Engine Aquila*; you need your own copy.

## Getting the latest version: one download by hand, then it updates itself

**Why by hand, this once?** The updater in Onslaught CE 0.1.0–0.1.6 tidied up at startup by deleting any folder next
to the game folder whose name started like it (for example `Onslaught CE.old-backup` beside `Onslaught CE`). We know
of no case where this deleted anything of a player's, but it could, so 0.1.7 switched updating off. 0.1.8 brings
updates back with a new updater that only ever touches its own two folders (`versions` inside the Onslaught CE folder
and `update-v2` in your user folder), never anything beside them; it checks every update's signature before using
it, and can go back to the previous version. Old launchers cannot reach it, so you
download the latest release (0.1.8 or later) yourself once; after that it updates itself.

From any earlier Onslaught CE (0.1.0 to 0.1.7):

1. **Close Onslaught CE.** Do not start the old version again (from 0.1.0–0.1.6, not even to check its version).
2. **Back up your careers and settings:** copy the folder `~/.local/share/onslaught-ce` (Linux) or
   `%APPDATA%\Onslaught CE` (Windows; paste that into the Explorer address bar) somewhere safe.
3. **Download** the latest package for your system (0.1.8 or later) from the
   [Releases page](https://github.com/dlprentice/onslaught-ce/releases).
4. **Extract it into a new, empty folder.** Do not extract it over the old one.
5. **Run** `onslaught-ce` (Linux) or `onslaught-ce.exe` (Windows) from the new folder. It finds your careers and
   settings by itself. Then point your shortcut at the new folder.

Keep the old folder and your backup until you are happy; never run the old launcher. If you set `BEA_USER_DIR` for
an earlier version, set it the same way for the new version. Anything unusual (a relative `BEA_USER_DIR`, data in
several places, folders you are unsure of): follow the
[detailed migration](#manual-migration-from-affected-launchers) instead.

## Quick start

New players start here. If you already have Onslaught CE, follow
[Getting the latest version](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself) instead.

1. Choose the package for your system from the [Releases page](https://github.com/dlprentice/onslaught-ce/releases).
   Extract it into a **new, empty, ordinary folder**, separate from the original game and every old CE folder.
2. Run `onslaught-ce` (Linux) or `onslaught-ce.exe` (Windows).
3. It finds your Steam copy of *Battle Engine Aquila* by itself, or asks once where it is.

Keep the program and its user folder separate from the original game. Careers and settings go to
`$XDG_DATA_HOME/onslaught-ce` (Linux, or `~/.local/share/onslaught-ce` when unset) or
`%APPDATA%\Onslaught CE` (Windows) by default. Keep recovery copies of your careers before changing save locations
or removing any program folder.

Onslaught CE is a native port of *Battle Engine Aquila* (Lost Toys, 2003) for Linux (i686) and Windows (i686), built
from the game's source code.

The source has three parts:

- **Stuart Gillam's GPL source release** of the game ([stuart73/Onslaught](https://github.com/stuart73/Onslaught)).
- **A byte-matching decompilation** that completes that release
  ([onslaught-decomp](https://github.com/dlprentice/onslaught-decomp)), which records its measured matching coverage
  and reconstruction concessions. The native port uses a different compiler and replacement platform code.
- **A platform layer**: SDL3, with dxvk-native on Linux and the system's Direct3D 9 on Windows.

This is an early preview. After installing a corrected successor, its main menu and `onslaught-ce --version`
show its version and build. Use existing logs to identify an affected installation without running its launcher.
See the [latest release notes](https://github.com/dlprentice/onslaught-ce/releases/latest) for the checks performed
and remaining limits; development source may include changes beyond that published package.

## In the game

- **Main menu > Onslaught CE:** About (version, licence and links), Report a problem, Record sessions (see below),
  Updates (whether checking is on, how to roll back, where `ce.ini` is) and Special Thanks.
- **Options > Cheats:** the original game's cheats as On/Off switches; see [Cheats](#cheats).
- **The console:** the original's developer console, back for solo play; press the `` ` `` (grave) key, left of 1 on a US
  keyboard. See [The console](#the-console).
- **Controllers:** gamepads and flight sticks work in the menus and in play without setup, and can be plugged in while
  the game runs; a player who set up their own joystick controls keeps them. See
  [port/controller-input.md](port/controller-input.md).
- **Keyboard:** no key release is lost, typed characters come from the key presses themselves (no desktop input method
  in the way), and the letter and digit keys work by their position on any keyboard layout.
- **A quiet console:** the developers' messages are hidden unless `BEA_PORT_VERBOSE=1` is set.
- **A log file** for reports, `onslaught-ce.log` in your user folder, and **session recording** (both below).

### Cheats

Options > Cheats turns the original game's cheats on and off. They are kept in `ce.ini` in your user folder and apply
to every career. Typing a code into a career's name works too, as in the original. The Cheats page's switches are off
while a session is being recorded and in replays; a code typed into a career's name still works then, as in the
original.

| Cheat | Original code | What it does |
| --- | --- | --- |
| Unlock all levels | TURKEY | Every level shows as open in the level select. Your career's real progress is not changed. In the console it also allows `map`, `win` and `lose`. |
| Unlock all goodies | MALLOY | Every goodie on the Goodies page is open. Nothing is written to your career. |
| Goodie hints | latête | The Goodies page shows the available unlock hints. While it is on, goodies cannot be opened, even with Unlock all goodies. |
| God mode option | Maladim | Adds **God** to Controller Options (in the pause menu, applying at once, and in Options, from the next level). God on: no damage and energy that never runs out. Set this way it is kept in `ce.ini`, not in your career. Turning the cheat off ends god mode. |
| Free camera | Aurore | In a level, **F** detaches the camera so you can fly around the battle. In the console it also allows `camera` and the remote camera. |

Free camera controls:

- **F**: free camera on or off. Off returns to your previous view.
- **W / S**: fly forward and back, in the direction you are looking.
- **A / D**: slide left and right.
- **Arrow keys**: turn left and right, look up and down. There is no up or down key and no speed key: look up or
  down, then fly, at one fixed speed.
- **Esc**: freeze or unfreeze the battle without a menu. The camera still flies while the battle is frozen, so you can
  line up a screenshot.

The battle keeps going while you look around unless you freeze it, and your Battle Engine can be hit.

The cheats themselves write nothing to your career (only the end-of-level statistics read the original's count of
god-mode uses, to show "CHEATER!"). A level you finish with their help, including one ended with the console's `win`,
counts like any other: a win opens the next levels, earns its goodies and records its grade (a level's grade is only
ever raised, never lowered). A code in a career's name works when the name contains it exactly, including latête on Linux
and Windows.

### The console

The original game has a developer console that the PC release never showed. Onslaught CE brings it back for solo play:
press the `` ` `` (grave) key, left of 1 on a US keyboard, during a level. It drops down over the top of the screen, as a
dark panel with the game's own log (what it loaded, and what commands print), and takes a command line. It never runs
the original's command interpreter: it has its own list of commands, and never runs script files or key binds (so an
`autoexec.con` in the game folder is ignored).

- **Keys:** Enter runs the line, Tab completes a command name, Up and Down recall earlier lines, PgUp and PgDn scroll
  the log, Esc or the grave key closes the console. The game does not pause for it. A line holds up to 63 printable
  characters; anything longer, or a character the console cannot take, is refused rather than cut.
- **Always available:** `help` (and `help <command>`), `version`, `status`, `showvars`, `show <variable>`,
  `set <variable> <value>` (views, weather, hit-flash colours and the console's own `cg_consolealpha`, each checked
  against its range), `cg_whatami` (graphics card), `listatmospherics`, `navmapon` and `navmapoff` (the landscape's
  navigation map), `setgammabias <0 to 0.005>` (full screen only), `fmv_play` (lists the game's films: the cutscenes
  1-33, `GILL_M_on_a_fork` and `UsTheMovie` play from the Goodies page, `ltlogo` and `openingfmv` when the game
  starts; none plays during a battle), `playsound <name>` (a sound the level has
  already loaded) and `splitscreen horizontal|vertical` (for the next two-player game).
- **Developer information:** `devstats` (or `devstats on`/`off`) shows the original developers' information text in
  the top left: level, game time, event-list size and events processed, thing count, frame rate, health, energy and
  score (or "GOD mode Score") and the Aug bar, and the game's own DEFAULT heap use. It is the text the developer build
  drew; the PC release never showed it. The TEXDATA and VBDATA heap lines of the original are left out: the PC game has
  no such heaps. `dev_stats=1` in `ce.ini` turns it on in every level; `devstats` changes it until the level ends.
  It is pale yellow as in the original, or green with `console_theme=green`, and only reads the game.
- **Your music:** `music` says what your playlist holds and plays; `music adddir <folder>` adds a folder of your own
  tracks (the rest of the line is the folder, spaces included); `music playtype single|linear|random`; `music next`
  skips to your next track; `music clear` goes back to the game's music. These are the original's playlist commands
  (`m_adddir`, `m_playtype`), which the PC release never registered. They last for the session; `ce.ini` keeps the
  settings (below).
- **With Free camera on:** `camera` (as the F key), `remotecameraon` (views from whatever is under the crosshair) and
  `remotecameraoff`.
- **With Unlock all levels on:** `map <level>` (leaves this level for another campaign level), `win` and `lose`. A win
  from the console counts in your career like any other win.
- **Put back at the end of each level:** every variable `set` changed, the navigation map, the gamma bias and the
  remote camera.
- **Not available:** `exec`, `bind`, `memstats`, `dumpmem`, `dumptextures` and the original's other developer
  commands that write files or run scripts; `quit` points you at the pause menu. The console is closed in multiplayer,
  in network games, in demos, and while a session is recorded or replayed.
- `console_theme=green` in `ce.ini` draws the text in green. This is an Onslaught CE choice: nothing shows that the
  original's console was green.

### Your own music, end-of-level statistics and rumble

Three things the original team built that the PC release left switched off. Each is an Onslaught CE option in `ce.ini`
in your user folder, and none of them is ever on while a session is recorded or replayed, in a network game or in a
demo.

- **Your own music** (`music_dir`, `music_playtype`): `music_dir=` the full path of a folder of `.ogg` tracks plays
  them in place of the level music (the front end and the credits keep the game's own). `music_playtype=linear` (the
  default) plays them in name order and round again, `random` shuffles them, `single` plays one and then stays quiet
  until the next level's music. A track must be Ogg Vorbis, stereo, 44100 Hz (the one format the game's music player
  plays; others are left out and counted in the log), with a full path of at most 255 bytes. Up to 256 tracks are used
  from the folder, which is read, never written; subfolders are not searched. On Linux `~/` is your home folder. The
  game pauses for about three seconds between tracks, as it does between its own. The console's `music` commands
  change it for the session.
- **End-of-level statistics** (`debrief_stats`, on unless set to 0): the victory and defeat screens list your mission
  time, time in jet and walker mode, rounds fired, accuracy (once you have fired) and damage taken, in the original's
  own words and layout, and "CHEATER!" in red when god mode was on. The game kept counting these; the PC release left
  the panel commented out.
- **Gamepad rumble** (`rumble`, on unless set to 0): your controller rumbles when your Battle Engine is hit or shaken
  by its own weapons, with the effect Stuart's PC source code played (the released game's rumble call was empty): full
  strength for a quarter of a second. Gamepads only; flight sticks do not rumble. Player 1's
  rumble goes to the first controller and player 2's to the second, the controllers the CE gamepad controls give them;
  a player who bound another controller in Controller Options feels the rumble on that player's numbered one. It needs a controller with rumble motors; nobody has tested how it feels on one yet.

Onslaught CE is an unofficial fan project. It is not affiliated with or endorsed by the game's developer, publisher or
rights holders; *Battle Engine Aquila* is a trademark of its owners.

## You need your own copy of the game

No game files are included. You need your own copy of *Battle Engine Aquila* for PC, for example from Steam.

- Onslaught CE runs from its own folder and reads the game's data from your installation. Its default user folder
  keeps careers and settings separate from that installation.
- The `onslaught-ce` launcher finds Steam installs automatically. On Linux it looks in Steam library folders and in
  native, `~/.steam` and Flatpak Steam installs. On Windows it uses Steam's library folders and the registry. If it
  can't find an install, it asks you once to pick the game folder. To choose the folder yourself, set `BEA_GAME_DIR`.
- Saves and settings are stored in:
  - Linux: `$XDG_DATA_HOME/onslaught-ce`, or `~/.local/share/onslaught-ce` when unset
  - Windows: `%APPDATA%\Onslaught CE`

`BEA_USER_DIR` overrides that location. Use a separate ordinary directory, not the game folder or a link into it.
The settings writer refuses linked parent paths; the Linux save writer also refuses linked and hard-linked write
targets. These checks do not make arbitrary redirected folders safe on every platform. Original-install careers
remain available in the load menu, with a same-named CE career taking precedence.

**Supported edition.** The launcher accepts an install whose two mission data files match the SHA-256 hashes in
`config/game-data.sha256`, which come from the Steam edition. Other editions can have different files and may be
refused with "not a supported Battle Engine Aquila install" (the launcher says which file and why). If yours is
refused, please [open an issue](https://github.com/dlprentice/onslaught-ce/issues) naming the edition and that message.

## Download and install

Download 0.1.8 or later from this repository's [Releases](https://github.com/dlprentice/onslaught-ce/releases) page.
If you have an earlier Onslaught CE, read [Getting the latest version](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself)
first.

### Verify a download (optional)

Download `ASSETS.txt` from the same release into the folder containing your package. It lists each file's SHA-256,
byte size and name; releases before 0.1.2 omit the size. It is not directly a `sha256sum --check` file.
In Bash on Linux, this command handles either form and checks just the package you name (change `package` to your
download's filename). A successful check prints that filename followed by `OK` and exits zero. A missing entry,
missing file, malformed list or changed download fails the check.

```bash
package=onslaught-ce-0.1.9-linux-i686.tar.gz
set -o pipefail
awk -v package="$package" '
  /^[[:space:]]*$/ || /^Onslaught CE / || /^Source: / { next }
  length($1) == 64 && $1 ~ /^[0-9a-f]+$/ && (NF == 2 || (NF == 3 && $2 ~ /^[0-9]+$/)) {
    if (seen[$NF]++) exit 1
    if ($NF == package) { found++; print $1 "  " $NF }
    next
  }
  { exit 1 }
  END { if (found != 1) exit 1 }
' ASSETS.txt | sha256sum --check --strict -
```

### Linux

1. Extract the tarball and run the `onslaught-ce` launcher.
2. The tarball bundles dxvk-native, SDL3, libxkbcommon and the FFmpeg shared libraries.
3. It needs **glibc 2.29 or newer** (`ldd --version` shows yours) and a **32-bit Vulkan 1.3 driver** that offers what
   its graphics layer (dxvk 3.1) requires: among other extensions `VK_KHR_load_store_op_none`, `VK_KHR_maintenance5`
   and `VK_KHR_maintenance6`. It also requires 256 bytes of push constants when DXVK's descriptor-heap path is not
   enabled; extension presence alone does not establish that path is usable. On older distributions the graphics
   driver, not glibc, is what stops it. Found on test systems: the Mesa drivers of Ubuntu 22.04 (23.2) and Debian 12 (22.3) do not have
   those extensions (Debian 12 has Mesa 25.0 in `bookworm-backports`), and Fedora 40's Intel driver (Mesa 24.1) offers
   only 128 bytes of push constants and is refused, while its software driver (lavapipe, the same Mesa) is accepted.
   AMD (RADV) and NVIDIA drivers have not been tried on those distributions. The preflight check below says when your
   glibc is too old or your 32-bit driver lacks the Vulkan version or those extensions (it cannot see the push-constant
   size).
4. Your system must provide these 32-bit libraries:
   - glibc, libgcc and libstdc++
   - zlib
   - the Vulkan loader and a 32-bit Vulkan driver (above)
   - the X11 or Wayland client libraries
   - `curl` (any architecture), which the launcher runs to check for and download updates; without it no update is
     found and the game still starts
5. A preflight check lists any missing 32-bit system libraries, with their pacman and apt package names. Set
   `BEA_SKIP_PREFLIGHT=1` to start without it.

The 32-bit libraries by distribution family (plus your GPU's 32-bit Vulkan driver):

- **Arch and derivatives** (enable the `multilib` repository in `/etc/pacman.conf` first; tested):

  ```sh
  sudo pacman -S --needed lib32-glibc lib32-gcc-libs lib32-zlib lib32-vulkan-icd-loader lib32-libx11 lib32-libxext lib32-wayland
  sudo pacman -S --needed lib32-vulkan-radeon     # or lib32-vulkan-intel, or lib32-nvidia-utils
  ```

- **Debian and Ubuntu** (Ubuntu 24.04, or Debian 12 with Mesa from backports:
  `sudo apt install -t bookworm-backports mesa-vulkan-drivers:i386`; the Mesa of Ubuntu 22.04 and stock Debian 12 is
  too old):

  ```sh
  sudo dpkg --add-architecture i386 && sudo apt update
  sudo apt install libc6:i386 libstdc++6:i386 libgcc-s1:i386 zlib1g:i386 libvulkan1:i386 libx11-6:i386 libxext6:i386 \
      libwayland-client0:i386 libwayland-cursor0:i386 mesa-vulkan-drivers:i386   # NVIDIA: the driver's i386 libraries
  ```

- **Fedora** (Fedora 40's Intel driver, Mesa 24.1, is refused for its push-constant size; other Fedora releases and
  drivers have not been tried):

  ```sh
  sudo dnf install glibc.i686 libstdc++.i686 libgcc.i686 zlib-ng-compat.i686 vulkan-loader.i686 libX11.i686 libXext.i686 \
      libwayland-client.i686 libwayland-cursor.i686 mesa-vulkan-drivers.i686     # NVIDIA: the driver's i686 libraries
  ```

- **Steam Deck and SteamOS** (untested): add `onslaught-ce` to Steam as a non-Steam game and set its compatibility tool
  to "Steam Linux Runtime 3.0 (sniper)"; that container supplies the 32-bit libraries and the Deck's driver. The game
  ran in that container on a desktop PC, started directly from the container's own launch script. Starting it as a
  Steam shortcut with that compatibility tool has not been tried, on a Deck or anywhere else.

### Windows

The Windows build has been checked under Wine on Linux and on a Windows 11 virtual machine using Microsoft's
software renderer. It has not yet been tested on a Windows PC with a real graphics card and its driver. Please
report any problems; see Known limitations and the release notes for the routes checked.

1. Extract the zip and run the `onslaught-ce.exe` launcher. The zip also contains `bea.exe` and the SDL3, FFmpeg and
   zlib DLLs.
2. You need Windows 10 or newer, for its built-in UCRT. User and game folders whose names have
   characters outside your Windows language's character set work from Windows 10 version 1903 on (the UTF-8 setting
   the programs use is ignored by earlier versions).
3. The executable is unsigned, so SmartScreen may show a warning. Choose **More info**, then **Run anyway**.

## Update, move and uninstall

- **Update:** from 0.1.8 on, the launcher updates itself ([Updates and privacy](#updates-and-privacy)). From 0.1.0–0.1.7,
  install the latest release by hand once ([Getting the latest version](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself)); never
  start an old launcher to reach its updater.
- **Move:** the program folder can live anywhere; move it as a whole (with its `versions` folder) while the game is
  closed. The launcher remembers your game folder in
  `ce.ini` (`gamedir`); if the game moves, it logs why the remembered folder no longer works and searches again, or
  set `BEA_GAME_DIR` to the new folder.
- **Uninstall:** first make and verify a recovery copy of the user folder, including any custom `BEA_USER_DIR`.
  It may be inside a program folder if you chose that location. Then delete the program folder. Before deleting an
  old 0.1.0–0.1.6 folder, look at any neighboring `.old-*`, `.download-*` and `.unpack-*` folders yourself: they may
  hold things of yours; never use the old launcher's cleanup.

<a id="windows-upgrading-from-015"></a>

### Manual migration from affected launchers

The detailed version of [Getting the latest version](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself), for unusual setups.

1. Close CE and its launcher. **Do not run the affected version again.** Do not rename or delete its program folder
   or any neighboring `<program folder>.old-*`, `.download-*` or `.unpack-*` paths. They may contain unrelated files
   or the only remaining copy of your data.
2. Locate **both launcher and game user-data folders** without starting the launcher. `BEA_USER_DIR` takes priority;
   otherwise the default is `$XDG_DATA_HOME/onslaught-ce` on Linux (`~/.local/share/onslaught-ce` when unset), or
   `%APPDATA%\Onslaught CE` on Windows. A relative `BEA_USER_DIR` can put launcher files and game user data in
   different locations. For example, the 0.1.6 Linux source indicates that a launch from a folder with
   `BEA_USER_DIR=user` would resolve launcher files in `user` there and game user data under `user/user`; Windows
   behavior has not been verified. Identify the actual locations from the old launch working directory and saved
   files; do not assume they are the same.
3. Copy **each whole data folder** to an independent backup outside the original game and all CE program and staging
   folders. Verify the copies and identify the locations containing your careers (`savegames`), settings (`ce.ini`)
   and recordings when present. Keep all originals and backups; if locations or conflicting files are unclear, do not
   merge or overwrite them, and seek help before choosing a working folder.
4. Choose the user folder the new version will use. If your data is in the default folder above (no `BEA_USER_DIR`) and step 3's
   backup is verified, the new version can use that folder as it is: leave `BEA_USER_DIR` unset. Otherwise (a custom or relative
   `BEA_USER_DIR`, or locations you are unsure of), prepare a separate working copy of the identified game user-data
   folder outside the original game and all old CE program and staging folders, and set `BEA_USER_DIR` to its absolute
   path for the successor. Preserve any separately located launcher settings and logs as well; do not assume copying
   the game folder restores them. Do not put saves into the new program folder.
5. Download the **0.1.8** (or later) platform package and `ASSETS.txt` from that specific release, and [verify the package](#verify-a-download-optional).
   Do not use 0.1.6 as an intermediate upgrade, and do not use an old launcher's automatic update path.
6. Extract the successor into a **new, empty folder writable by your account**, with no directory links/junctions
   in its path, separate from the original game and all old CE folders. Do not merge old executables or DLLs into it.
7. Run only the successor's `onslaught-ce` (Linux) or `onslaught-ce.exe` (Windows), using the same account and the
   user-folder choice from step 4. Check **Onslaught CE > About**, careers and settings, then update your shortcut.
   Keep your backup and uncertain old/staging paths. No administrator execution or disabled UAC is required.

Windows 0.1.5 also has a separate update-helper startup problem. The same manual procedure avoids invoking that
old helper. The embedded execution-level declaration in 0.1.6 does **not** fix the cleanup hazard above.

## Updates and privacy

This source's launcher (from 0.1.8) checks for updates when it starts: it downloads the public files
`ce-update-v2.json` and its signature from the latest GitHub release, checks the signature against the key built into
the launcher, and asks before installing. **Update** downloads the package, checks its size and SHA-256 against the
signed feed, and unpacks it into `versions/` inside the Onslaught CE folder; that folder's own files and everything
beside it are left alone, and the version you had stays for `--rollback`. Careers and settings are not changed.
`--update-only` checks now without starting the game; `BEA_NO_UPDATE_CHECK=1`, `update_check=0` in `ce.ini` or
`--no-update-check` turn checking off; **Onslaught CE > Updates** in the game says whether `update_check` in `ce.ini` has turned checking off, and where
`ce.ini` is.
On Linux the launcher downloads with the system `curl` (https only); without `curl` the check finds nothing and says so
only in the log. Releases never carry the old `latest.json` feed that launchers 0.1.0–0.1.6 fetch, and the legacy
`--apply` command is refused.

**Going back:** if an update gives you trouble, close the game and run the launcher in the Onslaught CE folder with
`--rollback`: `./onslaught-ce --rollback` (Linux), or `onslaught-ce.exe --rollback` from a Command Prompt in that folder
(Windows). The version before the update becomes the active one again, and that update is not offered again; a newer
one will be.

There is no telemetry. **Report a problem** is a user-initiated action: it opens a GitHub page in your browser with
your version and OS in its address; nothing is posted until you submit it.

## Screen modes and widescreen

Onslaught CE starts at your desktop's resolution, and Video Options lists your monitor's sizes, widescreen ones
included. On a widescreen monitor the 3D view shows more to the sides (the original's vertical view is kept); the menus,
loading pictures and films keep their original 4:3 shape, centred with black bars, and the in-game HUD sits where it
would on a 4:3 screen. To get the original's 4:3-only list and 640 x 480 start back, set `BEA_PORT_WIDESCREEN=0`.

Above 640 x 480 the single-player HUD (radar, portrait, messages, compass and reticle) is drawn larger in
proportion to the screen, so it stays readable at 1440p and 4K. Set
`BEA_PORT_HUD_SCALE=0` for the original's fixed-size HUD.

For testers: to try a screen size your monitor does not offer, set `BEA_PORT_WIDE_TEST=WIDTHxHEIGHT` and start with
the same size, for example `BEA_PORT_WIDE_TEST=2560x1440 onslaught-ce -- -res 2560 1440`. The game then treats it as
an available widescreen mode.

Replays and recordings always use the original 4:3 behaviour. The original game shares one random number source
between visual effects and play, so a wider view can change the details of a fight; that is expected.

## Report a problem

1. In 0.1.7 or later, choose **Onslaught CE > Report a problem** in the main menu, or run `onslaught-ce --report`
   (Windows: `onslaught-ce.exe --report`, from a Command Prompt in the program folder or a shortcut with `--report`
   added). Do not start a 0.1.0–0.1.6 launcher to report a problem, by either route; use the
   [Issues](https://github.com/dlprentice/onslaught-ce/issues) page instead and give the version you downloaded.
   Report a problem opens a new issue on this repository in your browser, already filled in with your version and
   system (which GitHub sees in the page's address), and prints the folder that holds your log. Nothing is posted
   until you submit the issue. You can also open an issue on the Issues page yourself.
2. Say what happened and how to make it happen again.
3. Attach `onslaught-ce.log` from your user folder (`~/.local/share/onslaught-ce` on Linux, `%APPDATA%\Onslaught CE`
   on Windows). In ordinary, unrecorded play it includes the launcher and game identities, the game executable's
   SHA-256, graphics and audio configuration, every display (size, refresh rate, scaling, which one the game used),
   the keyboards, mice, controllers and audio playback devices by the names your system gives them, the Onslaught CE
   settings in use, level and player-mode changes, window focus changes, music/film requests and results, and counted
   audio submission errors. Events carry elapsed milliseconds and a Present
   number. These details help distinguish a mixed installation, a device failure and the order of playback requests;
   successful requests do not prove what reached your speakers. Screenshots and recordings are still useful.
   `onslaught-ce.old.log` is the run before. Each stops at 4 MB, and repeated diagnostic events have their own limits.
   Home paths and account-name path components are shortened; look the file over before sharing it. It needs no
   administrator access, records no microphone audio and is never uploaded automatically. You can also share the
   file directly with someone helping you. Replays and session recordings omit the detailed ordinary-play events.
4. If you recorded the session (below), attach its tape too: it lets us replay exactly what happened.

## Help make Onslaught CE more faithful

The goal is a game that behaves exactly like the original. We check that by replaying recorded input
in both and comparing them, and recordings of real play find differences our own test runs miss.

- Record one session with `onslaught-ce --record`, or every session with **Onslaught CE > Record sessions** in the main
  menu. That starts the next time you launch Onslaught CE (quit the game, then start it again); it puts
  `record_sessions=1` in `ce.ini` in your user folder. While it is on, each start asks whether to record: **Stop
  recording** there (or starting with `onslaught-ce --no-record`) turns it off again.
- While recording, the game runs at 30 frames a second (the timing replays use), its menus take the keyboard and the
  mouse, and you can type a career's name, the screen is the original 4:3 (no widescreen), and the menus are the
  original's: the version text, no Onslaught CE menu and no Cheats page, and only cheats typed into a career's name
  work. These Onslaught CE additions are off too: the larger HUD, the command panel, the tutorial's dodge and zoom
  cues, and the saved Display mode (Windowed or Fullscreen) from Video Options. The game's sound plays while recording.
- Each completed session is saved in the `tapes` folder of your user folder as
  `YYYYMMDD-HHMMSS-PID-SERIAL.tape` and a `.tape.json` beside it. The final tape appears when the recording closes
  successfully. A failed or interrupted capture may leave an `.incomplete` file; that is not a replayable tape, and
  the launcher reports that the recording failed. Earlier recordings are never replaced.
  The tape holds only your key presses, typed characters and mouse movement and clicks, frame by frame. The `.json` names the version, your OS,
  the date and the game's arguments, and holds a copy of your options and saved careers as the session started, so a
  replay starts where you did. No screen capture, sound recording or microphone audio is included.
- To share a session, put the `.tape` and its `.tape.json` in a zip and attach it to an issue, saying what to look at
  (what differed from the original, and roughly when). Sessions where something looked or behaved
  differently from the original help most.

## How faithful is it?

The port is compared with the original game using recorded replays under a virtual clock: the game's state is checked
every 50 frames on the standard levels, and on every frame for the recorded play sessions.

**Standard test levels.** The available sampled state agrees with the retained original-game references for
levels 100, 200, 400, 521 and 611 through 10,000 frames, and level 860 through 3,250 frames. Missing player
state after death is unavailable evidence, not a match. Beyond frame 3,250, level 860's original-game recordings
disagree with each other. The earlier level-200 health and level-860 energy differences are corrected in the
0.1.5 candidate; this does not prove all gameplay, pictures or sound are identical.

**60-level sweep.** This sweep played the first ~3,000 frames (about 1 min 40 s) of 60 levels with one generic input
recording. 30 of those levels have original-game recordings to compare against:

- 20 match exactly.
- 2 have known small differences that are being fixed: levels 322 and 431.
- 8 have original-game recordings that disagree with each other, so they can't decide a match.

Those 30 levels were replayed again on the 0.1 build itself. Replays run in faithful mode (4:3, no CE menus or cheat
switches).

**Played through on the port:** the full front end, creating, loading and saving careers, options and videos, and most
of the tutorial (through its third zone; the final drone fight is still being completed).

## Known limitations

- Released launchers 0.1.0–0.1.6 contain the [startup cleanup hazard](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself); do not run them.
  Install the latest release by hand once ([Getting the latest version](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself)).
  Windows 0.1.5 also has an update-helper startup failure. Neither disabling updates nor installing 0.1.6 solves the cleanup risk.
- Windows checks cover Wine and one Windows 11 virtual machine using Microsoft's software renderer, not a PC with
  a real graphics card. During the 0.1.3 release checks, that VM updated 0.1.2 to 0.1.3, restarted, and the installed
  files matched the published package. An earlier attempt fetched no feed; a later retry succeeded. The
  [release notes](https://github.com/dlprentice/onslaught-ce/releases/latest) describe the tested routes and known
  limitations. The ordinary-play idle-title hang with disabled films is fixed in 0.1.4; faithful replay retains
  retail behavior. The 0.1.5 candidate also corrects a default-fullscreen mouse hit-area mismatch and recovery
  after minimize/focus changes, reproduced in that VM. The player's AMD graphics setup remains untested.
  Please report problems with the log.
- The Windows executable is unsigned, so SmartScreen may warn you (see above).
- On Linux you need glibc 2.29 or newer and a 32-bit Vulkan 1.3 graphics driver with dxvk's required extensions (see
  Linux above). It was run in Ubuntu 24.04 and Debian 12 (with backported Mesa) file systems and in the Steam Linux
  Runtime 3.0 container, all with test drivers and no window; nobody has played it on those systems or on a Steam
  Deck, and the Steam shortcut route for the Deck (above) has not been tried. Those runs used a 0.1.3 candidate build,
  not the released packages. The 0.1.5 candidate corrects an asset-decompression buffer overrun and an audio-file
  cleanup defect reproduced in a Fedora 40 file system with the software driver (lavapipe). Bounded level-entry
  and shutdown checks pass there; this is not a full-playthrough or hardware-driver validation.
- One effect depends on leftover memory rather than on the game: a walking unit's foot-landing flags are never given
  a starting value by the original game's code, so whether its first landing makes a stomp explosion depends on what
  that memory held before. This can differ between builds and between machines: level 521's replay shuts down one
  more explosion on 0.1.3 than on 0.1.2, and nothing else the replay compares (positions, health, random numbers,
  scores) differs. A recording that reaches such a landing may play back slightly differently on another version or
  machine.
- Recording and replay were compared using an isolated native PipeWire null sink as well as silent audio;
  the tested tape, mixed audio and saved bytes agree. Ordinary-clock music transitions and clean shutdown were
  also exercised. These checks do not validate a physical audio device, listening quality or every race schedule.
  The 0.1.5 candidate corrects music starts that could survive a stop request; the reported Windows overlapping
  music has not yet been reproduced and attributed to this defect.
- The tutorial is playable through its third zone; its final drone fight is still being completed.
- Gamepads and flight sticks work without setup ([how](port/controller-input.md)), but have been tested only with
  virtual devices so far.
- Widescreen play is not compared with the original (the original has no widescreen); replays and recordings use 4:3.
- Planned, not in this version yet: multiplayer, mods and HUD options.

## Building from source

The build runs on Linux (x86-64) and makes both programs from a Git checkout of this repository. It needs Python 3,
Git, Ninja and the usual build tools, and your installed game (the build checks the few game files it compiles in by
their hashes, `config/game-data.sha256`; set `BEA_GAME_DIR` if the game is not in Steam's default folder).

```sh
python3 port/tools/deps.py build                   # the pinned toolchains, libraries and Linux sysroot, checked by SHA-256
python3 port/build.py --public --target linux --ce-version dev
python3 port/build.py --public --target windows --ce-version dev
```

`--ce-version` sets the version the menu and `--version` show; without it the build calls itself
"vdev". It also sets the Windows launcher's file and product version strings. Their numeric fields use one to four
release-number parts, each from 0 to 65535; `-prerelease` and `+build` suffixes stay in the strings. Named development
builds such as `dev` use numeric 0.0.0.0. The game's `bea.exe` keeps its separate retail 1.0.0.0 resource.

`python3 port/tools/build_tests.py` checks launcher version parsing and resource linking without building the game.

After completing the Linux build, `python3 port/tools/audio_eof_tests.py` checks the actual Ogg reader and music
worker with generated audio and controlled buffer endpoints. It also needs a host `ffmpeg` command with the
`libvorbis` encoder; the pinned decoder-only FFmpeg libraries do not provide that command. This check does not
launch the game or open an audio device.

- `port/deps.lock` pins every downloaded input by URL and SHA-256.
- One input comes prebuilt: libvorbis, built with the game's original compiler. It is fetched from this repository's
  release assets, with its source and build script.
- `port/README.md` describes the build in detail: the compiler flags, the platform layer and the patches the port
  applies to `src/`.

## Licence

Onslaught CE is licensed under **GPL-3.0-only** (see [COPYING](COPYING), [NOTICE](NOTICE) and
[REUSE.toml](REUSE.toml)). Stuart's release ships the GPLv3 text without a version statement; we treat it, and license
our work, as GPL-3.0-only. Third-party components keep their own licences; see [THIRD-PARTY.md](THIRD-PARTY.md). The
FFmpeg libraries ship as replaceable shared libraries, and their source tarball is attached to each release.

The public graphics host is the GPL-3.0-only implementation in `port/platform/public/d3dapp.cpp` and `.h`.
The inherited SDK sample framework is excluded from this source tree and from the public build.

The game's data is not part of this project and is not covered by its licence.

## Contributing and security

See [CONTRIBUTING.md](CONTRIBUTING.md) (contributions are GPL-3.0-only; never include game files) and
[SECURITY.md](SECURITY.md) (report problems privately).

## Credits and special thanks

- **Lost Toys**, for the original game
- **Stuart Gillam** (**desimbr** on Discord), for his help, insight and guidance
- **Jeppi** and **BermudaMaster** on Discord, and every other early tester, for testing and helpful reports
- **The Onslaught CE contributors**, including everyone who contributed work made with their own AI coding agents
- **The Battle Engine Aquila community and preservation projects**, including everyone testing, reporting issues
  and sharing fixes

The **Onslaught CE → Special Thanks** menu carries these acknowledgements in the game. The original game's
**Credits** remain separate.

## Related projects

- [onslaught-decomp](https://github.com/dlprentice/onslaught-decomp): the byte-matching decompilation of the game's PC
  code, and the tools that judge the match.
- [onslaught-godot](https://github.com/dlprentice/onslaught-godot): a rebuild of the game in Godot (a proof of concept).
- [onslaught-preservation](https://github.com/dlprentice/onslaught-preservation): documentation of the game, its
  formats, behaviours and history.
- [Onslaught-Career-Editor](https://github.com/dlprentice/Onslaught-Career-Editor): companion app for saves, settings
  and lore; to be renamed onslaught-companion.
- [stuart73/Onslaught](https://github.com/stuart73/Onslaught): Stuart Gillam's release of the game's source.
