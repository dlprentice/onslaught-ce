// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/mods.h: Onslaught CE mods (experimental). A mod is a folder <user folder>/mods/<id>/ holding
// mod.json and replacement copies of game files at their game-relative paths (data/...). ce.ini's line
// mods=<id>,<id>,... enables mods in order; when two enabled mods replace the same file, the later one wins. When the
// game opens a game file for reading, an enabled mod's verified copy is read instead; the user folder's own copy of a
// file still wins over a mod. A mod may also add two kinds of file the game lacks: a world body, data/worlds/worldNNN.wrd
// (CWorld::Load reads it in place of the level archive's copy), and the archive of a multiplayer level the game has none
// for, data/resources/NNN_res_PC.aya with NNN from 850 to 899 (CResourceAccumulator::ReadResources opens it). Mods are
// never written, the game folder is never written, and every mod file is checked at startup (mods.cpp: paths, links,
// sizes, SHA-256, packed-file structure, and for an added level archive the structure of its chunks). Consumers receive
// an immutable verified snapshot at every open: a sealed memfd on Linux; on Windows a private delete-on-close file made
// from a handle-relative open chain (NtCreateFile against the held parent, links refused, names compared with the ones the
// volume holds), hashed against the manifest as it is copied, returned read-only. The Windows loader runs only in a build
// made with PORT_MODS_WINDOWS_ON (port/build.py --mods-windows-test); every other Windows build has mods off until the
// checks that only NTFS can show have passed. Mods are off for replays, recordings, probes, virtual clocks, pilots and
// network sessions.
#ifndef PORT_MODS_H
#define PORT_MODS_H
#include <stddef.h>

// bea --check-mods [id ...]: the loader's startup checks and nothing else, the one validator for the editors and the Companion. It checks the named mods, or without a
// name the ones ce.ini's mods= line enables, in that order, as a game would load them, under BEA_USER_DIR and BEA_GAME_DIR (a program calls the game itself with those two set; the
// launcher would write its log and might check for an update). Standard output is the loader's own lines, with no limit on their number: each refusal as a line
// "refused <id>: <reason>" (a mod with several refused files has one line for each, where the game stops at the first), then what the game would log for the mods that passed, then
// "result: <n> passed, <m> refused" (mods, or ids that name none; an id named twice is checked once, its first place counts). Status: 0 nothing refused, 1 something refused, 3 this build's loader is off
// (the one line says why; no check ran). A caller takes a 3 as that answer only with its one line on standard output: audioguard.cpp ends a run with 3 too, before main and with nothing on standard
// output, when BEA_PORT_REQUIRE_DUMMY_AUDIO is set and SDL_AUDIO_DRIVER names another driver (test variables). Nothing is written, nothing is sent, no window or device opens, the user folder is
// not even created. It runs before anything else of the game (linux/main.cpp, on Windows the end of mods.cpp). Only a public build has the command (port/build.py --public, which always
// defines CE_VERSION: "dev" unless --ce-version names one): in any other build --check-mods is an ordinary game argument and the game starts, so a caller points it at a public build (a
// release package's bea or bea.exe). The caller has set up the two folders' names (PortPathsNamesOnly).
// Two differences between the platforms' answers for one mod: a reason is in the platform's own words (a folder where mod.json should be, for one, is "not a regular file ..." on
// Linux and "a file that cannot be opened" on Windows), so a caller shows the reason and does not parse it; and names are exact on Windows (NameIsExact: every component as the volume
// spells it) but on Linux only where the file system is case-sensitive, so in a case-folding folder (ext4 or tmpfs casefold, exFAT, FAT) Linux passes a mod whose folders or files are
// spelled in another case than `mods`, its id and its manifest, which Windows refuses.
int PortModsCheck(int count, char *const *ids);
// The platform's half: the game and user folders' names as the first file access would set them, with nothing created (Linux paths.cpp, Windows files.cpp).
void PortPathsNamesOnly();

// Loads and checks the enabled mods once (later calls return at once; the first read of a game file calls it).
void PortModsInit();
// The number of active mods (0 when none is enabled, all were refused, or a fence is set).
int PortModsActive();
// The fence that keeps mods off for this run ("BEA_PORT_TAPE", ...), or NULL when mods may load.
const char *PortModsFence();
// Whether an active mod supplies game_path right now (a replacement, or a world file or level archive the game lacks).
// The file is opened below its mod's folder and its identity compared with the one checked at startup, but nothing is
// read or hashed, so a caller can decide before it reads: a file that changed since startup is not claimed (logged
// once), and a caller that skips its own copy on a yes is not left without one. False when mods are off, fenced or
// refused. A change between this answer and the read that follows still ends in that read's refusal.
bool PortModHas(const char *game_path);

#ifdef _WIN32
// A read-only handle (a HANDLE) to a private verified snapshot of the active mods' copy of game_path, or NULL when no active
// mod replaces it. A copy that no longer matches what was checked at startup is refused (logged once) and NULL is returned,
// so the caller reads the game's file instead. Closing the handle removes the snapshot.
void *PortModOpenRead(const char *game_path);
#else
// A read-only descriptor of the active mods' verified copy of game_path, or -1 (as the Windows form above).
int PortModOpenRead(const char *game_path);
#endif

// The platform's half (Linux paths.cpp, Windows files.cpp): whether the game folder itself holds the file at this
// game-relative path ('/'-separated, matched as the game matches names). A mod may only replace a file the game has
// (a world body, data/worlds/worldNNN.wrd, and the archive of a multiplayer level, data/resources/NNN_res_PC.aya, may
// also be added).
bool PortGameHasFile(const char *rel);

// What is checked of a level archive: an archive the game HAS (a replacement) is checked as a packed file alone, its chunks are not walked; only an ADDED
// archive (850 to 899) has its chunks walked (mods.cpp, CheckLevelArchive), because the game's own archives hold models that the walker does not admit yet.

#endif
