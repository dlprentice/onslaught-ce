# Onslaught CE

Onslaught CE is a native port of *Battle Engine Aquila* (Lost Toys, 2003) for Linux (i686) and Windows (i686), built
from the game's source code.

The source has three parts:

- **Stuart Gillam's GPL-3.0-only source release** of the game ([stuart73/Onslaught](https://github.com/stuart73/Onslaught)).
- **A byte-matching decompilation** that completes that release. All 5,482 game functions compile to the retail
  executable's bytes when built with the original compiler.
- **A platform layer**: SDL3, with dxvk-native on Linux and the system's Direct3D 9 on Windows.

This is **version 0.1, a preview** (tag `v0.1.0`). In the main menu, the version text in the bottom-left corner
(originally "V1.00") reads "Onslaught CE v0.1".

Onslaught CE is an unofficial fan project. It is not affiliated with or endorsed by the game's developer, publisher or
rights holders; *Battle Engine Aquila* is a trademark of its owners.

## You need your own copy of the game

No game files are included. You need your own copy of *Battle Engine Aquila* for PC, for example from Steam.

- Onslaught CE runs from its own folder and reads the game's data from your installation. It writes nothing into your
  installation.
- The `onslaught-ce` launcher finds Steam installs automatically. On Linux it looks in Steam library folders and in
  native, `~/.steam` and Flatpak Steam installs. On Windows it uses Steam's library folders and the registry. If it
  can't find an install, it asks you once to pick the game folder. To choose the folder yourself, set `BEA_GAME_DIR`.
- Saves and settings are stored in:
  - Linux: `~/.local/share/onslaught-ce`
  - Windows: `%APPDATA%\Onslaught CE`

## Download and install

Download a build from this repository's [Releases](https://github.com/dlprentice/onslaught-ce/releases) page.

### Linux

1. Extract the tarball and run the `onslaught-ce` launcher.
2. The tarball bundles dxvk-native, SDL3, libxkbcommon and the FFmpeg shared libraries.
3. Your system must provide these 32-bit libraries:
   - glibc and libstdc++
   - the Vulkan loader and a 32-bit Vulkan driver
   - the X11 or Wayland client libraries
4. A preflight check lists any missing 32-bit system libraries, with their pacman and apt package names.

### Windows

So far the Windows build has been tested only under Wine on Linux, not on real Windows (see Known limitations). Please
report any problems.

1. Extract the zip and run the `onslaught-ce.exe` launcher. The zip also contains `bea.exe` and the SDL3, FFmpeg and
   zlib DLLs.
2. You need Windows 10 or newer, for its built-in UCRT, curl and tar.
3. The executable is unsigned, so SmartScreen may show a warning. Choose **More info**, then **Run anyway**.

## Updates and privacy

- **No telemetry:** nothing about you or your machine is sent. The only network requests are the launcher's update
  check, which reads this repository's public, signed release feed, and the update download if you agree to it.
- **Update check:** each time it starts, the launcher checks the release feed (a signed `latest.json` on this
  repository's Releases). If a newer version exists, it asks **Update / Later / Skip this version**.
- **Verification:** each update is checked against its Ed25519 signature and its SHA-256 hash before anything is
  written, and is downloaded only from this repository's releases. A failed or tampered update changes nothing.
- **Opting out:** set `BEA_NO_UPDATE_CHECK=1`, or put `update_check=0` in `ce.ini`.

## How faithful is it?

The port is compared with the original game frame by frame, at every Present, using recorded replays under a virtual
clock.

**Standard test levels.** All six standard test levels were compared with the original for their recorded length:

- Levels 100, 400, 521 and 611 match exactly for 10,000 frames.
- Levels 200 and 860 each have one known tiny difference in a health value.

**60-level sweep.** This sweep played the first ~3,000 frames (about 1 min 40 s) of 60 levels with one generic input
recording. 30 of those levels have original-game recordings to compare against:

- 17 match exactly.
- 5 have known small differences that are being fixed: levels 110, 322, 431, 500 and 511.
- 8 have original-game recordings that disagree with each other, so they can't decide a match.

The sweep ran on the build just before patch 0036, the 0.1 build's last gameplay change.

**Played through on the port:** the full front end, creating, loading and saving careers, options and videos, and most
of the tutorial (through its third zone; the final drone fight is still being completed).

## Known limitations in 0.1

- The Windows build has been tested only under Wine on Linux, not on real Windows. The Windows self-update path in
  particular is untested on real Windows. Please report any problems.
- The Windows executable is unsigned, so SmartScreen may warn you (see above).
- On Linux you need 32-bit graphics drivers (Vulkan).
- With NVIDIA's driver on Linux the program can occasionally crash while closing, after the game has ended; your saves
  are already written by then.
- The tutorial is playable through its third zone; its final drone fight is still being completed.
- Joystick and controller support is limited to what the original game supported.
- Planned but not in 0.1: multiplayer, mods, widescreen and HUD options, and the in-game "check for updates" menu item.

## Building from source

The build runs on Linux (x86-64) and makes both programs from a Git checkout of this repository. It needs Python 3,
Git, Ninja and the usual build tools, and your installed game (the build checks the few game files it compiles in by
their hashes, `config/game-data.sha256`; set `BEA_GAME_DIR` if the game is not in Steam's default folder).

```sh
python3 port/tools/deps.py build                   # the pinned toolchains and libraries, checked by SHA-256
python3 port/build.py --public --target linux
python3 port/build.py --public --target windows
```

- `port/deps.lock` pins every downloaded input by URL and SHA-256.
- One input comes prebuilt: libvorbis, built with the game's original compiler. It is fetched from this repository's
  release assets, with its source and build script.
- `port/README.md` describes the build in detail: the compiler flags, the platform layer and the patches the port
  applies to `src/`.

## Licence

Onslaught CE is licensed under **GPL-3.0-only** (see [COPYING](COPYING), [NOTICE](NOTICE) and
[REUSE.toml](REUSE.toml)). Third-party components keep their own licences; see [THIRD-PARTY.md](THIRD-PARTY.md). The
FFmpeg libraries ship as replaceable shared libraries, and their source tarball is attached to each release.

The game's data is not part of this project and is not covered by its licence.

## Contributing and security

See [CONTRIBUTING.md](CONTRIBUTING.md) (contributions are GPL-3.0-only; never include game files) and
[SECURITY.md](SECURITY.md) (report problems privately).

## Credits

- **Lost Toys**, for the original game
- **Stuart Gillam**, for the source release
- **The Battle Engine Aquila community**

## Related projects

- [stuart73/Onslaught](https://github.com/stuart73/Onslaught): Stuart Gillam's release of the game's source.
- [Onslaught-Career-Editor](https://github.com/dlprentice/Onslaught-Career-Editor): a companion app, with lore and
  documentation about the game.
