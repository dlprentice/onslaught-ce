# Onslaught CE updater v2

Status: active — released in 0.1.8 (2026-10-27); the v2 feed is published on the latest release
Last updated: 2026-10-27
Summary: how 0.1.8 brings automatic updates back without the 0.1.0–0.1.6 hazard: a new signed feed that old
launchers never fetch, a versioned install layout switched by one atomic state-file replacement, and deletion only
of what the updater itself created and recorded.

## The hazard this replaces

Launchers 0.1.0–0.1.6 delete every neighbour named `<install>.old-*` at startup, following a directory link into its
target, and fetch `https://github.com/dlprentice/onslaught-ce/releases/latest/download/latest.json`. Publishing a feed at
that URL would make an old launcher offer its unsafe in-place swap. 0.1.7 therefore removed updating entirely. v2 keeps
every one of those launchers away from it:

- **A new feed name.** v2 fetches `ce-update-v2.json` and `ce-update-v2.json.sig` from
  `releases/latest/download/`. Every released launcher's source (this source repository's tags `ce-v0.1.0`, `ce-v0.1.2`–`ce-v0.1.6`
  and the public onslaught-ce tags `v0.1.0`–`v0.1.6`, read by `tests/updater_v2_tests.py`) has exactly one feed define,
  `latest.json` at that URL, and the sig URL is that URL plus `.sig`; none names the v2 feed. `latest.json` is never
  published again: `tools/sign_release.py` is retired and `tools/sign_update_v2.py` refuses a folder holding one.
- **Domain separation.** The v2 signature is Ed25519 over `onslaught-ce update feed v2\n` followed by the feed's exact
  bytes. A v2 feed and signature uploaded by mistake as `latest.json` would not verify in an old launcher, and an old
  `latest.json` pair does not verify in v2 (tested).
- **Nothing beside the install.** v2 never creates, renames or deletes anything next to the install folder, so it
  creates nothing an old launcher's cleanup could later match.

The signed `latest.json` and `latest.json.sig` assets were removed from the v0.1.0–v0.1.6 release pages on
2026-10-27: every `releases/download/v0.1.*/latest.json(.sig)` and the `releases/latest/download/` route answer 404, so
no route serves an installable old feed, whichever release is marked latest. Never attach either name again
(`tools/release_rc.py` and `sign_update_v2.py` refuse them).

## The feed

```json
{
 "format": "onslaught-ce-update-v2",
 "version": "0.1.8.1",
 "packages": {
  "linux-i686": {"url": "https://github.com/dlprentice/onslaught-ce/releases/download/v0.1.8.1/onslaught-ce-0.1.8.1-linux-i686.tar.gz",
                 "sha256": "<64 lowercase hex>", "size": 6725296},
  "windows-i686": {"url": "...windows-i686.zip", "sha256": "...", "size": 3702551}
 }
}
```

The launcher:

1. downloads the feed (at most 16 KiB) and the signature (at most 256 bytes) into its own stage folder;
2. checks the signature (exactly 128 lowercase hex digits) against the pinned release key, over the domain prefix and
   the exact bytes, **before parsing**;
3. parses strictly: objects, escape-free printable strings and non-negative integers; duplicate known keys, trailing
   bytes and a `format` other than `onslaught-ce-update-v2` are refused; the version is one to four numeric parts;
4. accepts only this repository's release-asset URL for the exact versioned file name (`ce_update_url_allowed`), a
   package of at most 512 MiB, and a version newer than every installed one: the running version, the active one and
   every recorded entry (never a downgrade; after `--rollback` the newer entry is not offered again, `--rollback`
   switches back to it);
5. after the player chooses **Update**, downloads the package with the signed size as a hard byte cap, then requires
   the size to equal the signed size and the SHA-256 to match, before anything is unpacked.

Downloads are https only, redirects included: Linux runs `curl -q` (no `~/.curlrc`) with `--proto =https
--proto-redir =https`, writing to a pipe the launcher reads, so the byte cap holds whatever curl does; Windows uses
WinHTTP (no `curl.exe` or `tar.exe` is needed any more). Development builds (a non-numeric version) never update.

## The install layout

```
<install>/                      the extracted package: the "base" version; its own files are never modified
<install>/versions/             created by the updater
<install>/versions/lock         lock and journal (below); created once, never deleted
<install>/versions/state        which version is active (the pointer file) and every entry the updater made
<install>/versions/v0.1.8.1-<16 hex>/onslaught-ce-0.1.8.1-linux-i686/   an installed version
```

`state` (written whole, replaced atomically):

```
onslaught-ce-updater-state 2
dir <ino>                       the versions folder's identity
active v0.1.8.1-1f2e…           an entry, or "base"
previous base                   an entry, "base" or "none" (what --rollback returns to)
entry v0.1.8.1-1f2e… <ino> 0.1.8.1 ready
```

Only inodes (Windows file indexes) are recorded. Device numbers are not stable across boots (btrfs gives each
subvolume an anonymous device number, for one), so every identity is compared on the versions folder's device as it
is numbered now. Entry names must be `v<their version>-<16 hex>`; anything else makes the state foreign. When the
state cannot be matched to the folder (it was copied, restored from a backup or edited), the base runs, nothing is
changed and no request is made, and the launcher says how to recover: quit, move the `versions` folder out of the
Onslaught CE folder (keeping it until a new update works), or extract a new release into a new empty folder.

**Startup handoff.** The base folder's launcher reads `state` first. When it names a ready entry newer than itself,
whose folder still has the recorded identity and is not a link or junction, it starts that version's launcher with the
same arguments: Linux `exec`s the entry's `onslaught-ce` wrapper, so the entry's own preflight and libraries are used;
Windows starts the entry's `onslaught-ce.exe`, waits for it and returns its exit status. An entry's launcher recognises
itself (its folder is `versions/<entry>/onslaught-ce-<its version>-<platform>` and `state` records that entry with
that identity) and never hands off again. A launcher inside a `versions` folder that its state does not record
disables updating for that run. Anything doubtful means "run the base": `BEA_CE_NO_HANDOFF=1` forces that, and a
base forced this way never checks for or installs an update (it could otherwise install an older version over the
active one). An entry's running launcher holds its folder (Linux: a shared `flock` on the folder, inherited by the
game; Windows: a handle that denies deletion), so retention never removes a running version.

## Installing an update

The player is asked first, with **Update** (Enter), **Later** (Escape) or **Skip this version** (`skip_version` in
`ce.ini`). With no display (Linux without `DISPLAY` or `WAYLAND_DISPLAY`) nobody is asked and nothing is installed.
`BEA_NO_UPDATE_CHECK=1`, `update_check=0` in `ce.ini` and `--no-update-check` turn checking off: no request is made.
An unreachable feed is silent (one log line). A replay or harness recording never checks.

1. **Stage.** In `<user folder>/update-v2/` (mode 0700; refused if it is a link), under that folder's lock, the
   launcher journals a new name `stage-<16 hex>`, creates it exclusively, journals its identity, and downloads
   `feed.json`, `feed.json.sig` and `package` into it with exclusively created files.
2. **Verify** as above.
3. **Lock the install.** `versions/` is created if absent (refused if it is a link). `versions/lock` is taken without
   waiting; a busy lock means another launcher is updating, and nothing changes. A `versions` folder without `state`
   is adopted only while it is empty (the lock aside) or holds only a journaled leftover; otherwise nothing changes.
4. **Record the entry.** The name `v<version>-<16 random hex>` is journaled, the folder created exclusively, and
   `state` replaced with the entry added as `unpacking`, before anything is written into the folder.
5. **Unpack** with the launcher's own readers (zlib): a GNU tar inside gzip on Linux, a zip on Windows. Each member
   must lie under `onslaught-ce-<version>-<platform>/` with plain name components (letters, digits, `. _ - +`; no `..`,
   no trailing dot, no absolute path, no Windows device names). Only regular files and directories are accepted;
   symlinks, hard links, devices and pax headers are refused, as are zip entries that are encrypted, zip64 or marked
   as links. Files are created exclusively and flushed to disk; CRC-32 and sizes are checked for zip members; totals
   are bounded (4,096 entries, 1 GiB). The unpacked folder must contain the launcher and the game.
6. **Switch.** One state replacement makes the entry `ready` and `active`, with the running version as `previous`.
   The new state is written to `state.new-<16 hex>` (journaled first), flushed, then:
   - Linux: `renameat2(RENAME_EXCHANGE)` with `state`; the file displaced to the temporary name must be the state file
     this run read (identity check), otherwise the exchange is undone. Where the file system lacks `RENAME_EXCHANGE`
     (EINVAL/ENOSYS), the identity is checked and `rename(2)` replaces it atomically, and the log says so. A first
     state uses `RENAME_NOREPLACE` (or `link(2)` where that is missing).
   - Windows: the current `state`'s identity is checked, then `MoveFileEx(MOVEFILE_REPLACE_EXISTING |
     MOVEFILE_WRITE_THROUGH)`; a first state uses `MoveFileEx` without replacement. Nothing outside `<install>` is
     touched, so no directory swap and no helper copy in `%TEMP%` is needed.
7. **Start the new version** (normal start) with the same arguments; it continues the log and does not check again.
   `--update-only` reports and exits instead.
8. **Retention.** Entries other than `active` and `previous` are removed (below). The base is never removed.
   `--rollback` swaps `active` and `previous` in one state replacement and removes nothing.

Every failure before step 6 leaves the active version as it was; the player sees "Nothing was changed: this version
starts now", and the log says which step failed.

## Deleting only what the updater made

There is no name-pattern cleanup anywhere. The updater deletes only:

- what this run created: a partial download, a temporary state file, a new entry whose unpacking failed;
- what its own records name: entries in `state`, and the names in each folder's journal.

**Journal.** Each updater folder's `lock` file, while locked, holds `tmp`, `dir` and `stage` lines naming a temporary
name *before* the run creates it (with the stage's identity once created). After a crash the next run, holding the
lock, removes exactly those names. Each must have the exact form the updater creates, or the line is ignored:
`tmp state.new-<16 hex>` (removed only if it is a regular file starting with the state header; never `state` itself),
`dir v<version>-<16 hex>` (a folder `state` does not record, removed only while it is empty, with `rmdir`) and
`stage stage-<16 hex> <ino>` (removed only if it still has the journaled identity, by unlinking the three file names
the updater writes there and then `rmdir`; anything else inside keeps it). A journal that cannot be read names
nothing.

**Removing an entry.** The folder must be a directory (not a link or junction) with the recorded identity, and no
running launcher may hold it. A scan first walks the whole tree without following anything (Linux
`openat(O_NOFOLLOW | O_DIRECTORY)`, `fstatat(AT_SYMLINK_NOFOLLOW)`, each directory re-checked by identity after
opening; Windows refuses any reparse point, which includes mounted volumes). On Linux every item must also be on the
versions folder's own mount: `statx` reports each item's mount ID and whether it is a mount root, so a bind mount from
the same device is refused too; where `statx` cannot report them (Linux before 5.8) old versions are never removed.
The removal goes ahead only if every item is a regular file with one link or a directory; a symlink, mount point,
hard-linked file, FIFO, device or socket keeps the whole entry. Then it removes bottom-up with
`unlinkat`/`DeleteFile` and `RemoveDirectory`. The log says what happened: removed; in use (kept and removed later);
refused, with the reason (nothing in it was removed; remove it by hand once checked); already gone (forgotten); or no
longer the updater's folder (left untouched and forgotten).

## Logging

Each step writes one bounded line to `onslaught-ce.log` through the launcher's existing scrubbed log (home written
`~`, the account name `<user>`, 4 MiB per file): check started, feed and signature verified (with sizes) or why they
were refused, the offer and the answer, the download's size and hash result, files and bytes unpacked, the switch,
removed or kept entries, recovery of leftovers. No URL query, token or private path beyond the scrubbed user folder.

## Commands and exit status

`--update-only` checks now, asks, installs, and never starts the game. `--rollback` makes the previous version active
again. `--apply` (the 0.1.0–0.1.6 helper's entry point) is still refused with 2 before any path is touched.

| Status | `--update-only` |
| --- | --- |
| 0 | installed and active (rollback: switched) |
| 10 | already current or already installed (rollback: nothing to roll back) |
| 11 | not now: Later, Skip, a skipped version, or nobody could be asked |
| 12 | checking is off (opt-out, `--no-update-check`, `BEA_CE_NO_HANDOFF`, or a development build); no request |
| 13 | no feed reached (offline, or no feed published) |
| 14 | feed refused (signature, format, version, URL) |
| 15 | package refused (download, size or hash) |
| 16 | install failed; nothing changed |
| 17 | another launcher holds the lock |

## Tests

`port/updater/tests/updater_v2_tests.py` builds updater test launchers (`-DCE_UPDATER_TESTING` with a generated
test key; `--version` prints `[CE-UPDATER-TEST-BUILD]` and `package.py` refuses such a launcher) for 0.1.8, 0.1.8.1,
0.1.8.2 and 0.1.8.3 on both platforms, packages them like releases, and runs every case inside bwrap (loopback only,
read-only system, private HOME and `/tmp`, the real home hidden); Windows runs under Wine with a private prefix and no
display. Test builds alone accept `BEA_UPDATE_V2_FEED`, `http://127.0.0.1:PORT/` package URLs with the exact file
name, `BEA_UPDATE_ANSWER`, `BEA_UPDATE_TEST_CRASH_AT=step[:N]` (killed with SIGKILL / `TerminateProcess` at that
step) and `BEA_UPDATE_TEST_WRITE_LIMIT` (a simulated full disk). Each case checks that every neighbour
(`<install>.old-*` folders, files, symlinks to folders and files, `.download-*`, `.unpack-*`, under Wine a junction),
every link target, the old `update/latest.json` cache and a foreign `update-v2/stage-*` folder are byte-for-byte
unchanged afterwards.

## Publishing

The v2 feed is published: since 0.1.8 every release marked latest carries `ce-update-v2.json` and
`ce-update-v2.json.sig`, so every 0.1.8+ player is offered the newest release. Each release writes them with
`python3 port/tools/sign_update_v2.py VERSION` (reads the release key in place; never copy or print it) as
`docs/release.md` describes, and never attaches `latest.json`. On Linux the launcher downloads with the system `curl`;
without it no feed is reached (status 13, one log line). A broken new version is undone with `--rollback`, or by
starting the base with `BEA_CE_NO_HANDOFF=1`.

## Open limits

- A concurrent attacker running as the same user could race path-based checks inside the updater's own new folders
  (unpacking, Windows tree removal); the Linux removal itself is descriptor-based. Same-user code can already replace
  the launcher, so this is not a new boundary.
- The release binaries' prompt and https paths are exercised only by inspection: the automated suite uses test builds
  (test key, localhost http, scripted answers). Native Windows, a real GitHub redirect and a desktop dialog have not
  been run.
- The Windows manifest does not declare `longPathAware`: it is shared with `bea.exe`, whose own fixed path buffers
  have not been reviewed for longer paths. Paths stay within MAX_PATH on Windows.
- Under Wine 11 the unchanged 0.1.7 settings-folder check refuses a user folder on `Z:\` or below any mount point
  (Wine appears to report those directories as reparse points); the Wine cases therefore keep the user folder on `C:\`,
  and the full-user-folder case runs on Linux only (Wine covers a failing download with the simulated full disk).
  Whether native Windows refuses mounted-volume folders the same way is unverified.
