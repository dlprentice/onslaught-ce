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

// The package providing this added level in the running game, or NULL. The startup mount stays fixed until exit.
const char *PortModLevelOwner(int level);

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

// ---- the Mods page's side (port/game/ce_modspage.cpp) -----------------------------------------------------------------
// Why a mod cannot load, as a kind: the loader's refusals (about two hundred texts, mods.cpp) fall into these, and the page
// has one plain sentence for each (ce_modspage_model.h, kCeModWhy). The refusal's own words stay in the log and in
// --check-mods. A new kind needs a sentence there: ce_modspage_tests.py fails a kind without one.
enum PortModWhy
{
	PORT_MOD_WHY_NONE = 0,		// it passes every startup check
	PORT_MOD_WHY_MISSING,		// ce.ini's line names it and the mods folder holds no folder of that name
	PORT_MOD_WHY_LINK,			// its folder is a link
	PORT_MOD_WHY_NAME,			// its folder's name is not a mod id (lowercase letters, digits, '-' and '_', at most 48, no device name)
	PORT_MOD_WHY_PACKED,		// an archive file (.zip, .7z, .rar) lying in the mods folder: a mod that was not unpacked
	PORT_MOD_WHY_INFO,			// mod.json is missing, cannot be read, or is not what schema 1 asks
	PORT_MOD_WHY_RENAMED,		// mod.json's id is not the folder's name (PortModInfo::wanted is the name the folder should have)
	PORT_MOD_WHY_NEWER,			// it needs a later Onslaught CE (ce_min, or a later schema)
	PORT_MOD_WHY_OLDER,			// it was made for an earlier Onslaught CE (ce_max)
	PORT_MOD_WHY_LIST,			// its list of files breaks a rule (a path the loader does not take, a file listed twice)
	PORT_MOD_WHY_NOT_IN_GAME,	// it replaces a file this game does not have
	PORT_MOD_WHY_FILE_MISSING,	// a listed file is not there, or is a link or not a plain file
	PORT_MOD_WHY_DAMAGED,		// a file is not the bytes the manifest lists (size, SHA-256), or cannot be read whole
	PORT_MOD_WHY_FORMAT,		// a packed file, a texture or a level archive the game's readers cannot hold
	PORT_MOD_WHY_TOO_BIG,		// over one of the size bounds
	PORT_MOD_WHY_OTHER,			// a refusal that named no kind (none does today; the page still has a sentence)
	PORT_MOD_WHY_COUNT
};

enum { PORT_MODS_ROWS = 64, PORT_MODS_ON = 16, PORT_MOD_LEVELS = 8 };	// a report's rows; the mods ce.ini may enable; the added levels a row names

// One entry of the mods folder, or one id of ce.ini's line that names no folder there.
struct PortModInfo
{
	char id[49];				// the folder's name when it is a mod id (what ce.ini's line holds); empty when the entry cannot be enabled under its name
	char folder[65];			// the entry's name in printable ASCII, cut to 64: what to call a mod whose mod.json gives no name
	char name[193], version[97], author[193], description[1025];	// mod.json's texts as written (UTF-8); empty where it has none or could not be read
	char wanted[49];			// PORT_MOD_WHY_RENAMED: the name the folder should have
	char detail[256];			// the loader's own words for the refusal (the log's); empty when it passes
	int why;					// PortModWhy
	int place;					// its place in ce.ini's mods= line as the file is now, from 1; 0 when the line does not name it
	int loaded;					// 1 when this process loaded it at startup: the game is given its files now
	int replaced;				// the game's files it replaces
	int worlds;					// the levels whose world it supplies as a file the game lacks (data/worlds/worldNNN.wrd)
	int archives;				// the levels it adds an archive for (850 to 899)
	int level_count, levels[PORT_MOD_LEVELS];	// playable archives this package supplies in this run, lowest first (up to PORT_MOD_LEVELS)
};

// The run the report was made in.
struct PortModsRun
{
	int off;					// 0 mods may load in this process; 1 a fence keeps them off for this run (a recording, a replay, a network game); 2 this build's loader is off
	int folder;					// 1 when the user folder has a mods folder
	int more;					// entries of the mods folder beyond the report's rows
	int refused_at_start;		// the mods ce.ini enabled that this process refused when it loaded them
	int loaded_count;			// the mods this process loaded, in the order it loaded them (a later one wins a file)
	char loaded[PORT_MODS_ON][49];
};

// Whether this build's mod loader is off (mods.cpp, PlatformOff: every Windows build without PORT_MODS_WINDOWS_ON). Nothing is loaded
// or read to answer. A page that changes which mods are enabled is not offered in such a build.
bool PortModsPlatformOff();

// The number of mods ce.ini enabled that this process refused when it loaded them (0 when mods are off for the run or in the build: nothing
// was looked at). The first call loads the enabled mods if nothing has yet.
int PortModsRefusedAtStart();

// Every mod the player has, for a page that lists them: first the ids of ce.ini's mods= line as the file is now, in its
// order (one that names no folder is a row too), then the other entries of the mods folder by name. Each folder is
// checked exactly as the loader checks an enabled mod at startup, alone (the bounds of the whole set are the enabling
// page's to keep), and nothing is served, written or kept open. At most max rows (and PORT_MODS_ROWS); returns their
// number. Call it from the thread that draws the page; the first call loads the enabled mods if nothing has yet.
int PortModsReport(PortModInfo *out, int max, PortModsRun *run);

// What two or more of the last report's mods would do to each other if the mods `on` (ids, in ce.ini's order: a later
// mod wins a file) were the enabled ones. For the mod at on[index]: shared is the number of its files that another of
// them supplies too, others the number of those other mods (other is its id when there is exactly one), lost the number
// of the shared files a later mod wins, and winner the id of the one mod that wins every lost file (empty when there are
// several, or none). False when on[index] is not a mod of the last report that passed its checks.
struct PortModClash
{
	int shared, others, lost;
	char other[49], winner[49];
};
bool PortModsClash(const char *const *on, int count, int index, PortModClash *out);

// Writes ce.ini's mods= line: the ids in order, or an empty value for none (which every reader takes as no line).
// Refuses (false, nothing written) an id that is not a mod id, one named twice, or more than PORT_MODS_ON of them; a
// failed write leaves ce.ini as it was (CE_SettingSetText's write-and-rename). The running game is not affected: the
// loader read the line once, at startup.
bool PortModsWriteLine(const char *const *ids, int count);

// What is checked of a level archive: an archive the game HAS (a replacement) is checked as a packed file alone, its chunks are not walked; only an ADDED
// archive (850 to 899) has its chunks walked (mods.cpp, CheckLevelArchive), because the game's own archives hold models that the walker does not admit yet.

#endif
