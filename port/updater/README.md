# Onslaught CE launcher and updater

Status: active (public builds only)
Last updated: 2026-10-22
Summary: how the public build's launcher finds the game, checks the signed release feed and updates itself.

`ce_launch.c` builds as `onslaught-ce-launch` (Linux, started by the `onslaught-ce` script after `preflight.sh`) and
`onslaught-ce.exe` (Windows), from `build.py --public`. It links SDL3 (its dialogs) and Monocypher (Ed25519, pinned in
`deps.lock`); `sha256.c` is its SHA-256.

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

## Releasing

`build.py --target all --public --ce-version X.Y`, `tools/package.py X.Y`, then `tools/sign_release.py X.Y [--tag TAG]` (0.1 ships as `v0.1.0`), which writes
`latest.json` and its signature with the release key. The private key stays outside every repository; only its public half
is in `ce_launch.c`, and `sign_release.py` refuses a key whose public half differs. Upload both packages, the FFmpeg source
tarball, `latest.json` and `latest.json.sig` as the release's assets, tagged as given to `sign_release.py`.
