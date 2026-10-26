# Onslaught CE

Status: active preview
Last updated: 2026-10-26
Summary: the native Battle Engine Aquila port, its installation requirements, download verification and source build.

A native port of *Battle Engine Aquila*; you need your own copy.

## Quick start

1. Download the package for your system from the [latest release](https://github.com/dlprentice/onslaught-ce/releases/latest)
   and extract it into any folder, for example your Downloads folder, not your game folder. You can move the folder
   anywhere later.
2. Run `onslaught-ce` (Linux) or `onslaught-ce.exe` (Windows).
3. It finds your Steam copy of *Battle Engine Aquila* by itself, or asks once where it is.

Keep the program and its user folder separate from the original game. Careers and settings go to
`$XDG_DATA_HOME/onslaught-ce` (Linux, or `~/.local/share/onslaught-ce` when unset) or
`%APPDATA%\Onslaught CE` (Windows) by default; delete the program folder to uninstall. Keep recovery copies of your careers before changing save locations.

Onslaught CE is a native port of *Battle Engine Aquila* (Lost Toys, 2003) for Linux (i686) and Windows (i686), built
from the game's source code.

The source has three parts:

- **Stuart Gillam's GPL source release** of the game ([stuart73/Onslaught](https://github.com/stuart73/Onslaught)).
- **A byte-matching decompilation** that completes that release
  ([onslaught-decomp](https://github.com/dlprentice/onslaught-decomp)), which records its measured matching coverage
  and reconstruction concessions. The native port uses a different compiler and replacement platform code.
- **A platform layer**: SDL3, with dxvk-native on Linux and the system's Direct3D 9 on Windows.

This is an early preview. The main menu and `onslaught-ce --version` show the installed version and build.
See the [latest release notes](https://github.com/dlprentice/onslaught-ce/releases/latest) for the checks performed
and remaining limits; development source may include changes beyond that published package.

## In the game

- **Main menu > Onslaught CE:** About (version, licence and links), Special Thanks, Check for updates (explains the check the launcher
  makes at every start, how to be asked again about a skipped version, and how to turn checks back on), Record sessions
  (see below) and Report a problem.
- **Options > Cheats:** the game's own cheats as On/Off switches, kept in `ce.ini` so they apply to every career; the
  line under the list says what the highlighted one does:
  - **Unlock all levels** (original code TURKEY): every level opens in the level select.
  - **Unlock all goodies** (MALLOY): every goodie opens on the Goodies page.
  - **Goodie hints** (latête): the Goodies page shows how to unlock each goodie; while it is on, goodies cannot be
    opened.
  - **God mode option** (Maladim): adds a God switch to Controller Options, in the pause menu (applies at once) and in
    Options (applies from the next level), for no damage and endless energy; turning the cheat off ends god mode. God
    mode set this way is kept in `ce.ini`, not in your careers, so a recorded session never carries it.
  - **Free camera** (F key, Aurore): in a level, F lets you fly the camera; F again returns it.

  They are off while a session is being recorded. Typing a code into a career's name still works as in the original.
- **Keyboard:** no key release is lost, typed characters come from the key presses themselves (no desktop input method
  in the way), and the letter and digit keys work by their position on any keyboard layout.
- **A quiet console:** the developers' messages are hidden unless `BEA_PORT_VERBOSE=1` is set.
- **A log file** for reports, `onslaught-ce.log` in your user folder, and **session recording** (both below).

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

Download a build from this repository's [Releases](https://github.com/dlprentice/onslaught-ce/releases) page.

### Verify a download (optional)

Download `ASSETS.txt` from the same release into the folder containing your package. It lists each file's SHA-256,
byte size and name; releases before 0.1.2 omit the size. It is not directly a `sha256sum --check` file.
In Bash on Linux, this command handles either form and checks just the package you name (change `package` to your
download's filename). A successful check prints that filename followed by `OK` and exits zero. A missing entry,
missing file, malformed list or changed download fails the check.

```bash
package=onslaught-ce-0.1.4-linux-i686.tar.gz
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
2. You need Windows 10 or newer, for its built-in UCRT, curl and tar. User and game folders whose names have
   characters outside your Windows language's character set work from Windows 10 version 1903 on (the UTF-8 setting
   the programs use is ignored by earlier versions).
3. The executable is unsigned, so SmartScreen may show a warning. Choose **More info**, then **Run anyway**.

## Update, move and uninstall

- **Update:** the launcher offers each new release when it starts (below). You can also download a new package and
  extract it in place of the old folder: your careers and settings are in your user folder, not the program folder.
- **Move:** the program folder can live anywhere; move it as a whole. The launcher remembers your game folder in
  `ce.ini` (`gamedir`); if the game moves, it logs why the remembered folder no longer works and searches again, or
  set `BEA_GAME_DIR` to the new folder.
- **Uninstall:** delete the program folder. To remove your careers and settings too, delete your user folder
  (`~/.local/share/onslaught-ce` or `%APPDATA%\Onslaught CE`). Keep any careers you want before deleting that folder.

## Updates and privacy

- **No telemetry:** nothing about you or your machine is sent. The only network requests the program makes by itself
  are the launcher's update check, which reads this repository's public, signed release feed, and the update download
  if you agree to it.
- **Report a problem** is the one network action you start yourself: it opens a GitHub page in your browser with your
  version and OS in its address; nothing is posted until you submit it.
- **Update check:** each time it starts, the launcher checks the release feed (a signed `latest.json` on this
  repository's Releases). If a newer version exists, it asks **Update / Later / Skip this version**.
- **Verification:** each update is checked against its Ed25519 signature and its SHA-256 hash before anything is
  written, and is downloaded only from this repository's releases. A failed or tampered update changes nothing.
- **Opting out:** set `BEA_NO_UPDATE_CHECK=1`, or put `update_check=0` in `ce.ini`.

## Screen modes and widescreen

Onslaught CE starts at your desktop's resolution, and Video Options lists your monitor's sizes, widescreen ones
included. On a widescreen monitor the 3D view shows more to the sides (the original's vertical view is kept); the menus,
loading pictures and films keep their original 4:3 shape, centred with black bars, and the in-game HUD sits where it
would on a 4:3 screen. To get the original's 4:3-only list and 640 x 480 start back, set `BEA_PORT_WIDESCREEN=0`.

Replays and recordings always use the original 4:3 behaviour. The original game shares one random number source
between visual effects and play, so a wider view can change the details of a fight; that is expected.

## Report a problem

1. Choose **Onslaught CE > Report a problem** in the main menu, or run `onslaught-ce --report` (Windows: `onslaught-ce.exe --report`, from a Command Prompt in the program folder or
   a shortcut with `--report` added). It opens a new issue on this repository in your browser, already filled in with
   your version and system (which GitHub sees in the page's address), and prints the folder that holds your log.
   Nothing is posted until you submit the issue.
   You can also open an issue on the [Issues](https://github.com/dlprentice/onslaught-ce/issues) page yourself.
2. Say what happened and how to make it happen again.
3. Attach `onslaught-ce.log` from your user folder (`~/.local/share/onslaught-ce` on Linux, `%APPDATA%\Onslaught CE`
   on Windows). In ordinary, unrecorded play it includes the launcher and game identities, the game executable's
   SHA-256, graphics and audio configuration, level and player-mode changes, window focus changes, music/film
   requests and results, and counted audio submission errors. Events carry elapsed milliseconds and a Present
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
  work. The game's sound plays while recording.
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
- Joystick and controller support is limited to what the original game supported, and controllers have not been tested
  yet.
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

`--ce-version` sets the version the menu, `--version` and the update check show; without it the build calls itself
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
- **Jeppi** and **BermudaMaster** on Discord, for early testing and helpful reports
- **The Onslaught CE contributors**
- **The Battle Engine Aquila community and preservation projects**, including everyone testing, reporting issues
  and sharing fixes, and the people guiding and reviewing work with coding agents

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
