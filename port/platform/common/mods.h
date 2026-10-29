// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/mods.h: Onslaught CE mods (experimental). A mod is a folder <user folder>/mods/<id>/ holding
// mod.json and replacement copies of game files at their game-relative paths (data/...). ce.ini's line
// mods=<id>,<id>,... enables mods in order; when two enabled mods replace the same file, the later one wins. When the
// game opens a game file for reading, an enabled mod's verified copy is read instead; the user folder's own copy of a
// file still wins over a mod. A mod may also add the one kind of file the game reads loose: a world body,
// data/worlds/worldNNN.wrd (CWorld::Load reads it in place of the level archive's copy). Mods are never written, the
// game folder is never written, and every mod file is checked at startup (mods.cpp: paths, links, sizes, SHA-256,
// packed-file structure). Linux consumers receive an immutable verified snapshot. Mods are disabled on Windows until
// path-safe opens and path-only readers are implemented, and are off for replays, recordings, probes, virtual clocks,
// pilots and network sessions.
#ifndef PORT_MODS_H
#define PORT_MODS_H
#include <stddef.h>

// Loads and checks the enabled mods once (later calls return at once; the first read of a game file calls it).
void PortModsInit();
// The number of active mods (0 when none is enabled, all were refused, or a fence is set).
int PortModsActive();
// The fence that keeps mods off for this run ("BEA_PORT_TAPE", ...), or NULL when mods may load.
const char *PortModsFence();
// Whether an active mod supplies game_path right now (a replacement, or a world file the game lacks). The file is opened
// below its mod's folder and its identity compared with the one checked at startup, but nothing is read or hashed, so a
// caller can decide before it reads: a file that changed since startup is not claimed (logged once), and a caller that
// skips its own copy on a yes is not left without one. False when mods are off, fenced or refused. A change between this
// answer and the read that follows still ends in that read's refusal.
bool PortModHas(const char *game_path);

#ifdef _WIN32
// A read-only handle (a HANDLE) to the active mods' verified copy of game_path, or NULL when no active mod replaces it.
// A copy that no longer matches what was checked at startup is refused (logged once) and NULL is returned, so the
// caller reads the game's file instead.
void *PortModOpenRead(const char *game_path);
// The host path of that verified copy, for the one reader that takes only a path (mmioOpenA): it is checked, then
// reopened by name, so a change in between is not caught there.
bool PortModPath(const char *game_path, char *out, size_t n);
#else
// A read-only descriptor of the active mods' verified copy of game_path, or -1 (as the Windows form above).
int PortModOpenRead(const char *game_path);
#endif

// The platform's half (Linux paths.cpp, Windows files.cpp): whether the game folder itself holds the file at this
// game-relative path ('/'-separated, matched as the game matches names). A mod may only replace a file the game has
// (a world body, data/worlds/worldNNN.wrd, may also be added).
bool PortGameHasFile(const char *rel);

#endif
