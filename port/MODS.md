# Making a mod for Onslaught CE

Status: active (guide for mod authors; mods are experimental, on Linux only, and the Mods page is behind `mods_menu=1` in `ce.ini`)
Last updated: 2026-11-13
Summary: the mod format (schema 1), what a mod may replace or add, the limits the game checks, when mods are off, and the two commands that
make and check one (`mods.py new`, `mods.py check`).

A mod is a folder. The game reads files from it instead of its own, after checking every one. Nothing in the game folder is ever
changed, and a mod that fails any check is not loaded at all.

## The shortest way to a first mod

Run these in the source folder, with the game closed (the sample is made from your own copy of the game and is never shared):

    python3 port/tools/mods.py new my-first-mod --name "My First Mod" --author you
    python3 port/tools/mods.py check my-first-mod

`new` makes `mods/my-first-mod/` in your user folder (`~/.local/share/onslaught-ce` on Linux, or `BEA_USER_DIR`): the Battle Engine's
texture with its colours rotated, and a `mod.json` that lists it. `check` runs the game's own startup checks on the mods you name (on
the mods `ce.ini` enables, with no name) and prints what the game would log. Turn the mod on in Onslaught CE > Mods (where that page is offered), or add its folder's
name to the `mods=` line of `ce.ini` with the game closed. A change applies the next time the game starts.

## The folder

    <user folder>/mods/<id>/
        mod.json
        data/...            the files, at the paths the game itself uses

- `<id>` is the folder's name: 1 to 48 lowercase letters, digits, `-` or `_`, and not a Windows device name (`con`, `prn`, `aux`,
  `nul`, `com0` to `com9`, `lpt0` to `lpt9`). A mod must be able to load on every system.
- A mod is a folder, never an archive: a `.zip` in the mods folder is listed as "Can't Load" until it is unpacked.
- No file or folder of a mod may be a link.

## mod.json (schema 1)

    {"schema": 1, "id": "my-first-mod", "name": "My First Mod", "version": "1.0", "author": "you",
     "description": "What it does, in a sentence or two.",
     "ce_min": "0.2", "ce_max": "0.9",
     "files": [{"path": "data/...", "size": 146214, "sha256": "<64 hexadecimal digits>"}]}

- `schema`, `id`, `name`, `version` and `files` are required. `author`, `description`, `ce_min` and `ce_max` are optional. No other key is
  allowed. `id` must be the folder's name.
- Every file under `data/` is listed once, with its exact size and SHA-256 (`mods.py manifest MODDIR --name N --version V` writes the
  list from the folder, and refuses to overwrite a file).
- `ce_min` and `ce_max` are Onslaught CE versions (numbers separated by dots). A game outside the range refuses the mod; an older
  game that does not know a later schema or key refuses it too. Give a mod that needs a newer game a `ce_min`.
- The Mods page shows the name (about 25 characters fit a row), the version, the author and the first lines of the description, and all
  of the description in Details. It shows only the letters the game's small face has: accents are kept, other scripts become `?`, so
  give a mod a name in the Latin alphabet. Keep the description to a few sentences.
- `mod.json` is at most 256 KiB.

## What a mod may do

1. **Replace a file the game has.** The path is the game's own (for example a texture in `data/resources/dxtntextures/`). A path the
   game does not have is refused ("changes a file your game does not have"). A packed `.aya` file must unpack with the game's own
   reader (1 MiB blocks); a texture must unpack to under 5 MiB. A larger picture than the original is allowed (twice the size has been
   tested).
2. **Supply a level's world.** `data/worlds/worldNNN.wrd` (three digits, the level number), at most 4 MiB. The game reads it instead of
   the one inside the level's archive. A level's base world is a separate file; careers remember which buildings of a base world still
   stand, so add things at its end rather than moving or removing the ones it has.
3. **Add a multiplayer level.** `data/resources/NNN_res_PC.aya` with NNN from 850 to 899 and no archive of the game's own. It is
   checked chunk by chunk when the game starts. Give such a mod a `ce_min`. When the mod is on, Play lists the levels this mod supplies in the running game. If two
   mods add the same level, only the one supplying its archive offers Play for it. Changing the order takes effect at the next start.
   With several playable levels, Play Level 1 to Play Level 3 are the lowest three numbers. Play opens the
   game's two-player start page, plays outside every career (nothing it does is kept) and returns to the Mods page with the mod's row
   highlighted. `-level 888` on the command line still starts it too.

When two mods replace the same file, the one lower in the list wins. The Mods page says so on both rows. A file you put in your user
folder yourself wins over a mod.

## The limits

- 16 mods on at once; at most 4,096 files in a mod and 8,192 across the mods that are on; at most 1 GiB of files in all.
- One file is at most 64 MiB unpacked; an added level's archive is at most 8 MiB packed and 32 MiB unpacked.
- Paths are at most 240 characters, 12 folders deep, with no part over 128 characters.

## When mods are off

- On every Windows build in this version: a `mods=` line is ignored and the log says so.
- In recordings, replays, probes, a virtual clock, pilots and autopilot, and in network games: so that those stay the same for
  everyone. The Mods page says so, and loads none.
- A mod that breaks any rule is refused whole; the game never loads part of a mod. The log names the first problem, and `mods.py check`
  names every problem of the mod.

## Checking a mod

`mods.py check [id ...]` runs `bea --check-mods` (set `--bea` to the program, or run it in the Onslaught CE folder). Each problem is a
line `refused <id>: <reason>`; the last line is `result: <n> passed, <m> refused`. The status is 0 when nothing was refused, 1 when
something was, and 3 when the program has no mod checks (a build with mods off). Nothing is written, not even the user folder.

## Rules that are not checked

- The checks cover where a file is and what is in it at the byte level, not what a texture, archive or world does in the game. Install
  mods only from people you trust.
- A unit's "Died" script handler that arrives while another script is running (a script's own Damage call kills the unit) is dropped, in a
  level a mod supplies, for good: the game would otherwise start the handler inside the one that is running and corrupt it, and
  waiting a frame is not possible because the unit's script is deleted the moment it dies. The "started dying" handler of such a unit runs a
  frame later (as far as the code can be read: no run has watched it). Put what must happen when a unit dies in the "started dying" handler, or in a script that watches
  the unit.
- A mod's world file is checked for size only, and a replaced level archive only as a packed file; a broken one can still crash the
  game.


## Finding installed models and textures

Add `catalogue=1` under `[Experimental]` in `ce.ini`, then restart CE. Open **Onslaught CE → Catalogue**
and select **Models** or **Textures**. The number beside each category is how many items CE found in your game
folder. Select an item to read its name and see whether it is a model, a game texture or an interface texture.
**Next page** shows the next twelve items; **First page** returns to the start. Dismiss the details to keep
browsing from the same item, or use **Back** to return to its category. If CE finds no matching files, selecting the empty category explains that and lets
you choose another.
