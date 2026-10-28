// SPDX-License-Identifier: GPL-3.0-only
// The CE console: the original game's "~" console brought back for solo play, as a drop-down panel over the battle. It
// shows the retail console's own scrollback (CConsole::mLines: the load log, and what the commands print) and takes
// commands from its own table, never from the retail parser: no CConsole::ExecuteCommand, Exec, Bind or HandleBind, no
// script files and no key binds. Every command is checked here first: information and view commands are open; the level
// and camera commands need the matching Cheats switch (or career name), as the original cheats do. What a command
// changes is put back when the level ends. The game actions themselves are in ce_console_game.cpp (ce_console.h).
#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "frontend.h"
#include "PauseMenu.h"
#include "GameInterface.h"
#include "CLIParams.h"
#include "ltshell.h"
#include "DXEngine.h"
#include "Hud.h"
#include "SpriteRenderer.h"
#include "renderinfo.h"
#include "Texture.h"
#include "ce_console.h"
#include "ce_music.h"
#include "ce_photo.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The optional network composite supplies this latched boot-intent owner. A weak reference is resolved at link time,
// so adding networking cannot leave an incrementally reused offline object without the fence. Both port toolchains
// are Clang; unknown compilers fail rather than silently building an unguarded network palette.
#if !defined(__clang__) && !defined(__GNUC__)
#error The CE palette requires weak symbol support for the network boot boundary
#endif
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" int CE_SettingIs(const char *key, const char *value);
extern "C" int CE_SettingGetBool(const char *key, int def);

bool PortCeLayer();
bool PortRecording();
extern WORD gCharScanCode;

using namespace CeConsoleGame;

namespace {
const unsigned kInputLimit = 63;		// characters a command line may hold; longer input is refused, never cut
const unsigned kHistory = 8;
const unsigned kArgs = 4;				// a command and up to three arguments
const int kCheatLevels = 1;				// TURKEY: Unlock all levels
const int kCheatCamera = 4;				// Aurore: Free camera
char input[kInputLimit + 1];
unsigned length;
bool overflow, paletteOpen, green;
char history[kHistory][kInputLimit + 1];
unsigned historyCount;
int historyAt = -1;						// the history line shown while browsing, -1 for the line being typed
int scroll;								// scrollback lines hidden below the panel (PgUp/PgDn)
bool consumed[256];

// What this level's commands changed, put back by PortCeConsoleLevelEnd.
struct Change { char name[32]; void *data; VarType type; unsigned char saved[4], set[4]; };
Change changes[24];
unsigned changeCount;
bool navMapChanged, navMapWas, gammaChanged;
char pendingFilm[64];
bool pendingLocalise;
int pendingSplit = -1;					// the next two-player game's split: 1 horizontal (the game's), 0 vertical
int devStats = -1;						// devstats this level: 1 on, 0 off, -1 as ce.ini's dev_stats says
int devStatsIni = -1;					// dev_stats from ce.ini, read once
const char *rawLine = "";				// the line being run: music adddir takes the rest of it, spaces and all

void Trap(WORD key, KeyEventType event);

// Replays, recordings, probes, the virtual clocks, test pilots and network games.
const char *const kFenced[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
							   "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};

bool Present(const char *name)
{
	const char *value = getenv(name);
	return value && *value;
}

bool Allowed()
{
	// The usual CE switch has a deliberate screenshot override. Never honor that override for interactive commands.
	if (!PortCeLayer() || PortRecording()) return false;
	for (const char *name : kFenced) if (Present(name)) return false;
	if (CE_BootRequested && CE_BootRequested()) return false;
	if (PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo ||
		CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode || FRONTEND.IsRunning()) return false;
	if (GAME.GetGameState() != GAME_STATE_PLAYING || GAME.IsMultiplayer() || GAME.GetNPlayers() != 1 ||
		GAMEINTERFACE.IsMenuUp() || GAMEINTERFACE.mShowJoypad) return false;
	if (!GAME.GetPlayer(0) || !GAME.GetController(0) || !GAME.GetCurrentCamera(0) ||
		!GAME.GetPauseMenu() || GAME.GetPauseMenu()->mActive) return false;
	// Do not capture input if the overlay cannot even be laid out.
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	CDXTexture *white = HUD.GetWhiteTexture();
	return font && font->GetHeight() > 0 && white && white->GetRealWidth() > 0 && white->GetRealHeight() > 0 &&
		   PLATFORM.GetWindowWidth() > 0 && PLATFORM.GetWindowHeight() > 0;
}

// The developer information overlay (CGame::DrawDebugStuff's text, patch 0102): the console's fences, without the
// console's own needs (an open pause menu or a missing font only hide it). BEA_PORT_DEVSTATS_PROOF=1 with
// BEA_PORT_CE_LAYER=1 is a test switch that draws it under a tape, so a replay can show it changes nothing.
bool DevStatsAllowed()
{
	if (!PortCeLayer()) return false;
	const bool proof = Present("BEA_PORT_DEVSTATS_PROOF") && Present("BEA_PORT_CE_LAYER");
	if (!proof)
	{
		if (PortRecording()) return false;
		for (const char *name : kFenced) if (Present(name)) return false;
	}
	if (CE_BootRequested && CE_BootRequested()) return false;
	if (PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo ||
		CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode || FRONTEND.IsRunning()) return false;
	return !GAME.IsMultiplayer() && GAME.GetNPlayers() == 1 && GAME.GetPlayer(0);
}

void Out(const char *fmt, ...)
{
	char line[96];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	Print(line);
}

void Close()
{
	// A remap, menu or other owner may have replaced our trap; never clear its trap.
	if (LT.GetKeytrap() == Trap) LT.SetKeytrap(NULL);
	paletteOpen = false;
	length = 0;
	input[0] = 0;
	overflow = false;
	historyAt = -1;
	scroll = 0;
}

bool Same(const char *a, const char *b)	// ASCII case folding only
{
	for (;; ++a, ++b)
	{
		char x = (*a >= 'A' && *a <= 'Z') ? char(*a + 32) : *a, y = (*b >= 'A' && *b <= 'Z') ? char(*b + 32) : *b;
		if (x != y) return false;
		if (!x) return true;
	}
}

// ---- values -----------------------------------------------------------------------------------------------------------
// Whole-word numbers only: no signs where none is needed, no exponents, no trailing text.
bool ParseInt(const char *s, int *out)
{
	int v = 0, n = 0;
	for (; *s; ++s, ++n)
	{
		if (*s < '0' || *s > '9' || n >= 6) return false;
		v = v * 10 + (*s - '0');
	}
	if (!n) return false;
	*out = v;
	return true;
}

bool ParseFloat(const char *s, float *out)
{
	float v = 0.f, place = 0.f;
	int digits = 0;
	for (; *s; ++s)
	{
		if (*s == '.' && place == 0.f) place = 1.f;
		else if (*s >= '0' && *s <= '9' && digits < 9)
		{
			++digits;
			if (place == 0.f) v = v * 10.f + float(*s - '0');
			else v += float(*s - '0') * (place *= 0.1f);
		}
		else return false;
	}
	if (!digits) return false;
	*out = v;
	return true;
}

bool ParseBool(const char *s, bool *out)
{
	if (Same(s, "1") || Same(s, "on") || Same(s, "true")) { *out = true; return true; }
	if (Same(s, "0") || Same(s, "off") || Same(s, "false")) { *out = false; return true; }
	return false;
}

// The retail variables a player may read and set: views, weather and colours. Never the simulation's tick length, the
// variables that point into one Battle Engine or walker, sound freezing, or the renderer's internal switches.
struct VarRule { const char *name; VarType type; float low, high; const char *what; };
const VarRule kVars[] = {
	{"cg_consolealpha", VAR_INT, 64, 255, "console background (64-255)"},
	{"cg_Shadows", VAR_BOOL, 0, 1, "shadows"},
	{"cg_BlobShadows", VAR_BOOL, 0, 1, "blob shadows"},
	{"cg_ShowShadowMap", VAR_BOOL, 0, 1, "show the shadow map"},
	{"cg_ShowShadowExtents", VAR_BOOL, 0, 1, "show the shadow extents"},
	{"cg_renderlandscape", VAR_BOOL, 0, 1, "draw the landscape"},
	{"cg_renderreflections", VAR_BOOL, 0, 1, "water reflections"},
	{"cg_drawpolybuckets", VAR_BOOL, 0, 1, "show polybucket volumes"},
	{"cg_snow", VAR_BOOL, 0, 1, "snow"},
	{"cg_zoomeffectenabled", VAR_BOOL, 0, 1, "zoom effect"},
	{"cg_showgamut", VAR_BOOL, 0, 1, "show the gamut"},
	{"cg_forceobjectimposters", VAR_BOOL, 0, 1, "force object imposters"},
	{"cg_meshlodbias", VAR_FLOAT, 0.25f, 4.f, "mesh detail bias (0.25-4, lower is finer)"},
	{"cg_hiteffectfactorr", VAR_INT, 0, 400, "hit flash red (0-400, /100)"},
	{"cg_hiteffectfactorg", VAR_INT, 0, 400, "hit flash green (0-400, /100)"},
	{"cg_hiteffectfactorb", VAR_INT, 0, 400, "hit flash blue (0-400, /100)"},
	{"atm_raindensity", VAR_FLOAT, 0, 1, "rain (0-1)"},
	{"atm_snowdensity", VAR_FLOAT, 0, 1, "snowfall (0-1)"},
	{"atm_lightningdensity", VAR_FLOAT, 0, 1, "lightning (0-1)"},
	{"snd_visible", VAR_BOOL, 0, 1, "show sound sources"},
};

const VarRule *Rule(const char *name)
{
	for (const VarRule &r : kVars) if (Same(r.name, name)) return &r;
	return NULL;
}

// The registered variable behind a rule, only when it has the type the rule expects.
void *Data(const VarRule *r)
{
	void *data = NULL;
	return Var(r->name, &data) == r->type ? data : NULL;
}

void Format(const VarRule *r, void *data, char *out, int n)
{
	if (r->type == VAR_BOOL) snprintf(out, n, "%s", *(bool *)data ? "1" : "0");
	else if (r->type == VAR_INT) snprintf(out, n, "%d", *(int *)data);
	else snprintf(out, n, "%.3f", double(*(float *)data));
}

Change *Remember(const VarRule *r, void *data)
{
	for (unsigned i = 0; i < changeCount; ++i) if (!strcmp(changes[i].name, r->name)) return &changes[i];
	if (changeCount >= sizeof(changes) / sizeof(changes[0])) return NULL;	// every rule fits; never reached
	Change &c = changes[changeCount++];
	snprintf(c.name, sizeof(c.name), "%s", r->name);
	c.data = data;
	c.type = r->type;
	memcpy(c.saved, data, r->type == VAR_BOOL ? 1 : 4);
	return &c;
}

// ---- commands ---------------------------------------------------------------------------------------------------------
typedef char Arg[kInputLimit + 1];
struct Command
{
	const char *name, *usage, *help;
	int cheat;					// the Cheats switch it needs (IsCheatActive's number), -1 for none
	void (*run)(int argc, Arg *argv);
};
extern const Command kCommands[];
extern const unsigned kNumCommands;

void CmdHelp(int argc, Arg *argv)
{
	if (argc > 1)
	{
		for (unsigned i = 0; i < kNumCommands; ++i)
			if (Same(kCommands[i].name, argv[1]))
			{
				const Command &c = kCommands[i];
				Out("%s %s", c.name, c.usage);
				Out("  %s", c.help);
				if (c.cheat == kCheatLevels) Out("  Needs the Unlock all levels cheat (Options > Cheats).");
				if (c.cheat == kCheatCamera) Out("  Needs the Free camera cheat (Options > Cheats).");
				return;
			}
		Out("No command %s. Type help.", argv[1]);
		return;
	}
	Out("Commands (help <command> says more):");
	char line[96];
	int at = 0;
	line[0] = 0;
	for (unsigned i = 0; i < kNumCommands; ++i)
	{
		const char *mark = kCommands[i].cheat >= 0 ? "*" : "";
		int need = int(strlen(kCommands[i].name) + strlen(mark)) + 2;
		if (at && at + need > 64) { Out("  %s", line); at = 0; line[0] = 0; }
		int wrote = snprintf(line + at, sizeof(line) - at, "%s%s  ", kCommands[i].name, mark);
		at = (wrote > 0 && at + wrote < int(sizeof(line))) ? at + wrote : int(sizeof(line)) - 1;
	}
	if (at) Out("  %s", line);
	Out("* needs its cheat switched on in Options > Cheats.");
}

void CmdVersion(int, Arg *) { Out("%s", CeConsoleGame::Version()); }

void CmdStatus(int, Arg *)
{
	Out("Level %d. %s camera. Battle %s.", CurrentLevel(), FreeCameraOn() ? "Free" : RemoteCameraActive() ? "Remote" : "Normal",
		Paused() ? "paused" : "running");
}

void CmdShowVars(int, Arg *)
{
	Out("Variables (set <name> <value>; put back when the level ends):");
	for (const VarRule &r : kVars)
	{
		void *data = Data(&r);
		char value[24];
		if (data) Format(&r, data, value, sizeof(value));
		else snprintf(value, sizeof(value), "(not here)");
		Out("  %s = %s: %s", r.name, value, r.what);
	}
}

void CmdShow(int argc, Arg *argv)
{
	if (argc != 2) { Out("Usage: show <variable>. showvars lists them."); return; }
	const VarRule *r = Rule(argv[1]);
	void *data = r ? Data(r) : NULL;
	if (!r) { Out("No variable %s. showvars lists them.", argv[1]); return; }
	if (!data) { Out("%s is not available here.", r->name); return; }
	char value[24];
	Format(r, data, value, sizeof(value));
	Out("%s = %s", r->name, value);
}

void CmdSet(int argc, Arg *argv)
{
	if (argc != 3) { Out("Usage: set <variable> <value>. showvars lists them."); return; }
	const VarRule *r = Rule(argv[1]);
	if (!r) { Out("No variable %s. showvars lists them.", argv[1]); return; }
	void *data = Data(r);
	if (!data) { Out("%s is not available here.", r->name); return; }
	bool b = false;
	int i = 0;
	float f = 0.f;
	if ((r->type == VAR_BOOL && !ParseBool(argv[2], &b)) || (r->type == VAR_INT && !ParseInt(argv[2], &i)) ||
		(r->type == VAR_FLOAT && !ParseFloat(argv[2], &f)) ||
		(r->type == VAR_INT && (float(i) < r->low || float(i) > r->high)) ||
		(r->type == VAR_FLOAT && (f < r->low || f > r->high)))
	{
		Out("%s takes %s.", r->name, r->type == VAR_BOOL ? "0 or 1" : r->what);
		return;
	}
	Change *c = Remember(r, data);
	if (r->type == VAR_BOOL) *(bool *)data = b;
	else if (r->type == VAR_INT) *(int *)data = i;
	else *(float *)data = f;
	if (c) memcpy(c->set, data, r->type == VAR_BOOL ? 1 : 4);
	char value[24];
	Format(r, data, value, sizeof(value));
	Out("%s = %s", r->name, value);
}

void CmdWhatAmI(int, Arg *) { CeConsoleGame::WhatAmI(); }
void CmdAtmospherics(int, Arg *) { ListAtmospherics(); }

void NavMapSwitch(bool on)
{
	if (!LandscapePresent()) { Out("There is no landscape here."); return; }
	if (!navMapChanged) { navMapChanged = true; navMapWas = NavMapOn(); }
	NavMap(on);
	Out(on ? "Navigation map on." : "Navigation map off.");
}
void CmdNavMapOn(int, Arg *) { NavMapSwitch(true); }
void CmdNavMapOff(int, Arg *) { NavMapSwitch(false); }

void CmdCamera(int, Arg *)
{
	if (!LandscapePresent() || RemoteCameraActive()) { Out("The free camera is unavailable here."); return; }
	if (!ToggleFreeCamera()) { Out("The free camera is unavailable here."); return; }
	Out(FreeCameraOn() ? "Free camera on. F or camera turns it off." : "Free camera off.");
}

void CmdRemoteOn(int, Arg *)
{
	int r = RemoteCameraOn();
	if (r < 0) Out("Turn the free or remote camera off first.");
	else if (!r) Out("There is nothing under the crosshair.");
	else Out("Remote camera on. remotecameraoff returns.");
}

void CmdRemoteOff(int, Arg *)
{
	int r = RemoteCameraOff();
	if (r < 0) Out("Turn the free camera off first.");
	else if (!r) Out("The remote camera is not on.");
	else Out("Remote camera off.");
}

void CmdMap(int argc, Arg *argv)
{
	int level = 0;
	if (argc != 2 || !ParseInt(argv[1], &level)) { Out("Usage: map <level>, for example map 200."); return; }
	if (!CampaignLevel(level)) { Out("%s is not a campaign level.", argv[1]); return; }
	Out("Leaving for level %d.", level);
	Close();
	JumpToLevel(level);
}

void CmdWin(int, Arg *) { Out("Level won."); Close(); Win(); }
void CmdLose(int, Arg *) { Out("Level lost (the original's lose command gives no reason)."); Close(); Lose(); }

void CmdGamma(int argc, Arg *argv)
{
	float v = 0.f;
	if (argc != 2 || !ParseFloat(argv[1], &v)) { Out("Usage: setgammabias <0 to 0.005>, 0 is normal."); return; }
	if (v > 0.005f) v = 0.005f;
	gammaChanged = gammaChanged || v != 0.f;
	SetGammaBias(v);
	Out("Gamma bias %.4f (full screen only).", double(v));
}

void CmdFilm(int argc, Arg *argv)
{
	char file[64];
	bool localise = false;
	if (argc != 2 || !Film(argv[1], file, sizeof(file), &localise))
	{
		Out("Films: a cutscene number 1-33, GILL_M_on_a_fork, UsTheMovie, ltlogo, openingfmv.");
		Out("  Goodies plays the cutscenes and the two extras; the boot films play at start.");
		return;
	}
	// Not during a battle: whether the battle catches up on the time a film took has not been measured, and the
	// console only opens in a battle. The Goodies page plays the cutscenes and the two extras; the boot films play at start.
	(void)localise;
	Out("Films are not played during a battle; fmv_play alone lists them.");
}

void CmdSound(int argc, Arg *argv)
{
	char exact[100];
	if (argc != 2) { Out("Usage: playsound <name>: one of the %d sounds this level has loaded.", Sounds()); return; }
	if (!Sound(argv[1], exact, sizeof(exact))) { Out("No loaded sound %s.", argv[1]); return; }
	PlaySample(exact);
	Out("Playing %s.", exact);
}

void CmdSplit(int argc, Arg *argv)
{
	if (argc == 2 && Same(argv[1], "horizontal")) pendingSplit = 1;
	else if (argc == 2 && Same(argv[1], "vertical")) pendingSplit = 0;
	else { Out("Usage: splitscreen horizontal or splitscreen vertical."); return; }
	Out(pendingSplit ? "The next two-player game splits top and bottom (as normal)."
					 : "The next two-player game splits side by side.");
}

// The retail playlist commands (m_adddir, m_playtype, ...) for the player's own music (ce_music.cpp).
const char *Leaf(const char *path)
{
	const char *leaf = path;
	for (const char *c = path; *c; ++c) if (*c == '/' || *c == '\\') leaf = c + 1;
	return leaf;
}

void MusicUsage()
{
	Out("Usage: music, music adddir <folder>, music playtype single|linear|random,");
	Out("  music next or music clear.");
}

void CmdMusic(int argc, Arg *argv)
{
	static const char *const kTypes[] = {"single", "linear", "random"};
	if (argc == 1)
	{
		const int n = CeMusic::Tracks();
		Out("Your music: %d track%s, play type %s.", n, n == 1 ? "" : "s", kTypes[CeMusic::Type()]);
		if (CeMusic::Folder()[0]) Out("  Folder %s", CeMusic::Folder());
		if (const char *now = CeMusic::Now()) Out("  Playing %s", Leaf(now));
		else if (!n) Out("  The game's music plays. music adddir <folder> adds yours.");
		else Out("  Your tracks play from the next level music on.");
		return;
	}
	if (Same(argv[1], "adddir"))
	{
		// The folder is the rest of the line after "adddir", so a folder name may have spaces.
		const char *rest = rawLine;
		while (*rest == ' ') ++rest;
		rest += 5;							// "music"
		while (*rest == ' ') ++rest;
		rest += 6;							// "adddir"
		while (*rest == ' ') ++rest;
		char dir[kInputLimit + 1];
		snprintf(dir, sizeof(dir), "%s", rest);
		for (size_t n = strlen(dir); n && dir[n - 1] == ' '; --n) dir[n - 1] = 0;
		if (!dir[0]) { Out("Usage: music adddir <folder>: the full path of a folder of .ogg files."); return; }
		int skipped = 0, truncated = 0;
		const int added = CeMusic::AddFolder(dir, &skipped, &truncated);
		if (added < 0) { Out("Cannot read the folder %s. Give its full path.", dir); return; }
		Out("Added %d track%s; %d .ogg file%s left out.", added, added == 1 ? "" : "s", skipped, skipped == 1 ? "" : "s");
		if (skipped) Out("  Tracks must be Ogg Vorbis, stereo, 44100 Hz, with a path of 255 bytes at most.");
		if (truncated) Out("  Only the first 4096 names in that folder were read.");
		if (CeMusic::Tracks() >= 256) Out("  The playlist holds 256 tracks at most.");
		return;
	}
	if (Same(argv[1], "playtype"))
	{
		for (int t = CE_MUSIC_SINGLE; argc == 3 && t <= CE_MUSIC_RANDOM; ++t)
			if (Same(argv[2], kTypes[t]))
			{
				CeMusic::SetType(t);
				Out(t == CE_MUSIC_SINGLE ? "Play type single: one track, then quiet until the next level music."
					: t == CE_MUSIC_LINEAR ? "Play type linear: your tracks in name order, round again."
										   : "Play type random: your tracks shuffled.");
				return;
			}
		Out("Usage: music playtype single|linear|random.");
		return;
	}
	if (Same(argv[1], "next") && argc == 2)
	{
		if (!CeMusic::Next()) { Out(CeMusic::Tracks() ? "Your music plays with a level's music." : "Your playlist is empty."); return; }
		Out("Playing %s", Leaf(CeMusic::Now()));
		return;
	}
	if (Same(argv[1], "clear") && argc == 2)
	{
		CeMusic::Clear();
		Out("Playlist cleared: the game's music plays.");
		return;
	}
	MusicUsage();
}

// Photo mode (ce_photo.cpp) is temporarily unavailable; its control also reports that to the player.
void CmdPhoto(int argc, Arg *argv)
{
	bool on = !PortCePhotoOn();
	if (argc > 2 || (argc == 2 && !ParseBool(argv[1], &on))) { Out("Usage: photo [on|off]."); return; }
	if (!PortCePhotoSet(on)) { Out("Photo mode is temporarily unavailable. F12 saves a screenshot."); return; }
	Out(on ? "Photo mode on: HUD hidden. F11 or photo returns; F12 saves a screenshot." : "Photo mode off.");
	if (on) Close();
}

void CmdDevStats(int argc, Arg *argv)
{
	if (argc == 1) devStats = PortCeDevStatsOn() ? 0 : 1;
	else if (argc == 2 && (Same(argv[1], "on") || Same(argv[1], "1"))) devStats = 1;
	else if (argc == 2 && (Same(argv[1], "off") || Same(argv[1], "0"))) devStats = 0;
	else { Out("Usage: devstats [on|off]."); return; }
	Out(devStats ? "Developer information on (the original's debug text)." : "Developer information off.");
}

const Command kCommands[] = {
	{"help", "[command]", "Lists the commands, or says what one does.", -1, CmdHelp},
	{"version", "", "Shows the Onslaught CE version.", -1, CmdVersion},
	{"status", "", "Shows the level, the camera and whether the battle is paused.", -1, CmdStatus},
	{"showvars", "", "Lists the variables set can change, with their values.", -1, CmdShowVars},
	{"show", "<variable>", "Shows one variable's value.", -1, CmdShow},
	{"set", "<variable> <value>", "Changes a view variable until the level ends.", -1, CmdSet},
	{"cg_whatami", "", "Shows the graphics card and driver.", -1, CmdWhatAmI},
	{"listatmospherics", "", "Lists the weather effects.", -1, CmdAtmospherics},
	{"navmapon", "", "Shows the navigation map on the landscape, until the level ends.", -1, CmdNavMapOn},
	{"navmapoff", "", "Hides the navigation map.", -1, CmdNavMapOff},
	{"setgammabias", "<0 to 0.005>", "Darkens the gamma ramp (full screen only); reset when the level ends.", -1, CmdGamma},
	{"fmv_play", "<film>", "Lists the game's films (none plays during a battle).", -1, CmdFilm},
	{"playsound", "<name>", "Plays a sound this level has loaded.", -1, CmdSound},
	{"music", "[adddir|playtype|next|clear]", "Your own .ogg folder in place of the level music; music alone says more.", -1, CmdMusic},
	{"photo", "[on|off]", "Photo mode is temporarily unavailable. F12 saves a screenshot in your screenshots folder.", -1, CmdPhoto},
	{"devstats", "[on|off]", "The original developers' information text: level, time, events, FPS, health.", -1, CmdDevStats},
	{"splitscreen", "horizontal|vertical", "Sets how the next two-player game splits the screen.", -1, CmdSplit},
	{"camera", "", "Free camera on or off, as the F key.", kCheatCamera, CmdCamera},
	{"remotecameraon", "", "Views from what is under the crosshair.", kCheatCamera, CmdRemoteOn},
	{"remotecameraoff", "", "Returns from the remote camera.", kCheatCamera, CmdRemoteOff},
	{"map", "<level>", "Leaves this level and starts another campaign level.", kCheatLevels, CmdMap},
	{"win", "", "Wins this level. It counts in your career like any win.", kCheatLevels, CmdWin},
	{"lose", "", "Loses this level.", kCheatLevels, CmdLose},
};
const unsigned kNumCommands = sizeof(kCommands) / sizeof(kCommands[0]);

// Retail commands left out, with the reason a player who knows them is given.
const char *const kRefused[] = {"exec", "bind", "listbinds", "memstats", "dumpmem", "dumptextures", "menu", "echo",
								"useconfiguration", "buildlandscapecache", "buildstaticshadows", "resetmemsizes",
								"dumptimerecords", "exit", "quit"};

// Splits the line at spaces into at most kArgs words; false for more words than that.
int Words(const char *line, Arg *argv)
{
	int argc = 0;
	for (const char *c = line; *c;)
	{
		while (*c == ' ') ++c;
		if (!*c) break;
		if (argc == int(kArgs)) return -1;
		unsigned n = 0;
		while (*c && *c != ' ' && n < kInputLimit) argv[argc][n++] = *c++;
		if (*c && *c != ' ') return -1;
		argv[argc++][n] = 0;
	}
	return argc;
}

void Run(const char *line)
{
	Arg argv[kArgs];
	rawLine = line;
	int argc = Words(line, argv);
	if (argc == 0) return;
	if (argc < 0)
	{
		// A music folder may have spaces: music adddir takes the rest of the line.
		Arg head[kArgs];
		char start[16];
		snprintf(start, sizeof(start), "%.15s", line);
		if (Words(start, head) >= 2 && Same(head[0], "music") && Same(head[1], "adddir")) { CmdMusic(2, head); return; }
		Out("Too many words. Type help.");
		return;
	}
	for (unsigned i = 0; i < kNumCommands; ++i)
	{
		const Command &c = kCommands[i];
		if (!Same(c.name, argv[0])) continue;
		if (c.cheat >= 0 && !CheatOn(c.cheat))
		{
			if (c.cheat == kCheatCamera) Out("%s needs the Free camera cheat: Options > Cheats.", c.name);
			else Out("%s needs the Unlock all levels cheat: Options > Cheats.", c.name);
			return;
		}
		c.run(argc, argv);
		return;
	}
	if (Same(argv[0], "showcmds")) { CmdHelp(1, argv); return; }
	for (const char *r : kRefused)
		if (Same(r, argv[0]))
		{
			if (Same(r, "exit") || Same(r, "quit")) Out("Use the pause menu (Esc) to quit.");
			else Out("%s is not available in Onslaught CE.", r);
			return;
		}
	Out("Unknown command %s. Type help.", argv[0]);
}

void Submit()
{
	if (!Allowed()) { Close(); return; }
	if (overflow)
		Out("Input refused: printable characters only, %u at most.", kInputLimit);
	else if (length)
	{
		Out("> %s", input);
		if (!historyCount || strcmp(history[0], input))
		{
			for (unsigned i = kHistory - 1; i > 0; --i) memcpy(history[i], history[i - 1], sizeof(history[i]));
			memcpy(history[0], input, sizeof(history[0]));
			if (historyCount < kHistory) ++historyCount;
		}
		char line[kInputLimit + 1];
		memcpy(line, input, sizeof(line));
		Run(line);
	}
	length = 0;
	input[0] = 0;
	overflow = false;
	historyAt = -1;
	scroll = 0;
}

void Load(const char *text)
{
	length = 0;
	while (text[length] && length < kInputLimit) { input[length] = text[length]; ++length; }
	input[length] = 0;
	overflow = false;
}

void Browse(int step)
{
	int at = historyAt + step;
	if (at < -1 || at >= int(historyCount)) return;
	historyAt = at;
	Load(at < 0 ? "" : history[at]);
}

void Complete()
{
	if (overflow || !length || strchr(input, ' ')) return;
	const Command *found = NULL;
	for (unsigned i = 0; i < kNumCommands; ++i)
	{
		bool prefix = true;
		for (unsigned k = 0; k < length && prefix; ++k)
		{
			char a = input[k], b = kCommands[i].name[k];
			if (a >= 'A' && a <= 'Z') a = char(a + 32);
			prefix = a == b;
		}
		if (!prefix) continue;
		if (found) return;	// more than one: leave the line as it is
		found = &kCommands[i];
	}
	if (found && strlen(found->name) + 1 <= kInputLimit) { Load(found->name); input[length++] = ' '; input[length] = 0; }
}

void Trap(WORD key, KeyEventType event)
{
	if (!paletteOpen || LT.GetKeytrap() != Trap) return;
	if (event == KEY_EVENT_DOWN) {
		if (key >= 256) return;
		const bool wasDown = consumed[key];
		consumed[key] = true;
		if ((key == DIK_GRAVE && !wasDown) || key == DIK_ESCAPE) Close();
		else if (key == DIK_RETURN || key == DIK_NUMPADENTER) Submit();
		else if (key == DIK_BACK && length && !overflow) input[--length] = 0;
		else if (key == DIK_BACK && overflow) { length = 0; input[0] = 0; overflow = false; }
		else if (key == DIK_UP) Browse(1);
		else if (key == DIK_DOWN) Browse(-1);
		else if (key == DIK_PRIOR) scroll += 4;
		else if (key == DIK_NEXT) scroll = scroll > 4 ? scroll - 4 : 0;
		else if (key == DIK_TAB) Complete();
	}
	else if (event == KEY_EVENT_CHAR && gCharScanCode != DIK_GRAVE && key >= 32) {
		if (key <= 126 && length < kInputLimit && !overflow) {
			input[length++] = char(key);
			input[length] = 0;
		} else overflow = true; // never execute truncated or unsupported input
	}
}

// ---- drawing ----------------------------------------------------------------------------------------------------------
struct Look { DWORD text, hint, panel; };

Look Colours()
{
	Look look = {0xffffffff, 0xffa8a8a8, 0xc8000000};
	if (green) { look.text = 0xff40ff40; look.hint = 0xff20a020; }
	// cg_consolealpha (the retail console's background, 200 by default), when this level registered it.
	const VarRule *r = Rule("cg_consolealpha");
	void *data = r ? Data(r) : NULL;
	if (data && *(int *)data >= 64 && *(int *)data <= 255) look.panel = DWORD(*(int *)data) << 24;
	return look;
}

// One line in the small font. The font has no cell for "_" or the brackets: "_" is drawn as a bar under a space,
// brackets become parentheses, and anything else it cannot show becomes "?". A line wider than the panel is narrowed.
void DrawLine(CDXBitmapFont *font, CDXTexture *white, float left, float y, float width, float scale, DWORD colour,
			  const char *text)
{
	WCHAR visible[100];
	bool bar[100];
	unsigned n = 0;
	for (unsigned i = 0, spaces = 0; text[i] && n < 99; ++i)
	{
		unsigned char c = (unsigned char)text[i];
		spaces = c == ' ' ? spaces + 1 : 0;
		if (spaces > 2 && n > 4) continue;	// the load log pads its status lines to 80 columns
		WCHAR w = c;
		bar[n] = c == '_';
		if (c == '_') w = L' ';
		else if (!font->CanDisplay(w))
			w = (c == '[' || c == '{') ? L'(' : (c == ']' || c == '}') ? L')' : (c == '\\') ? L'/' : L'?';
		visible[n++] = w;
	}
	visible[n] = 0;
	if (!n) return;
	SIZE extent;
	font->GetTextExtent(visible, &extent);
	float textScale = scale;
	if (float(extent.cx) * scale > width && extent.cx > 0) textScale = width / float(extent.cx);
	font->DrawTextWithShadow(left, y, colour, visible, 0, 0.f, textScale, scale);
	for (unsigned i = 0; i < n; ++i)
	{
		if (!bar[i]) continue;
		WCHAR prefix[100];
		memcpy(prefix, visible, i * sizeof(WCHAR));
		prefix[i] = 0;
		SIZE before, space;
		font->GetTextExtent(prefix, &before);
		WCHAR one[2] = {L' ', 0};
		font->GetTextExtent(one, &space);
		if (!i) before.cx = 0;
		CSPRITERENDERER::DrawColouredSprite(left + float(before.cx) * textScale, y + (float(font->GetHeight()) - 2.f) * scale,
										   0.001f, white, colour, float(space.cx) * textScale / white->GetRealWidth(),
										   scale / white->GetRealHeight());
	}
}
}

void PortCePaletteCheck()
{
	if (paletteOpen && (LT.GetKeytrap() != Trap || !Allowed())) Close();
}

bool PortCePaletteOpen(unsigned key, bool repeat)
{
	if (key != DIK_GRAVE || repeat || key >= 256 || consumed[key] || LT.GetKeytrap() || !Allowed()) return false;
	paletteOpen = true;
	length = 0;
	input[0] = 0;
	overflow = false;
	historyAt = -1;
	scroll = 0;
	green = CE_SettingIs("console_theme", "green") != 0;
	consumed[key] = true;
	LT.SetKeytrap(Trap);
	return true;
}

bool PortCePaletteSuppressDown(unsigned key)
{
	// A repeat after close/replacement belongs to the old owner until the physical release.
	return key < 256 && consumed[key] && LT.GetKeytrap() != Trap;
}

bool PortCePaletteRelease(unsigned key)
{
	if (key >= 256 || !consumed[key]) return false;
	consumed[key] = false;
	return true;
}

bool PortCePaletteSuppressChar(unsigned key)
{
	return key < 256 && consumed[key] && LT.GetKeytrap() != Trap;
}

void PortCePaletteClose() { Close(); }

// The level is ending (CGame::ShutdownRestartLoop, before anything is freed): put back what the commands changed.
void PortCeConsoleLevelEnd()
{
	Close();
	if (RemoteCameraActive()) RemoteCameraOff();
	for (unsigned i = changeCount; i-- > 0;)
	{
		void *data = NULL;
		const VarRule *r = Rule(changes[i].name);
		const size_t n = changes[i].type == VAR_BOOL ? 1 : 4;
		// Only a value still as the console left it: the game's own settings (detail level, weather scripts) win.
		if (r && Var(r->name, &data) == changes[i].type && data == changes[i].data && !memcmp(data, changes[i].set, n))
			memcpy(data, changes[i].saved, n);
	}
	changeCount = 0;
	if (navMapChanged && NavMapOn() != navMapWas) NavMap(navMapWas);
	navMapChanged = false;
	if (gammaChanged) SetGammaBias(0.f);
	gammaChanged = false;
	pendingFilm[0] = 0;
	devStats = -1;
}

// Whether CGame::DrawDebugStuff draws its developer text this frame (patch 0102): devstats, else ce.ini's dev_stats=1.
// Never in photo mode.
bool PortCeDevStatsOn()
{
	if (!DevStatsAllowed() || PortCePhotoOn()) return false;
	if (devStats >= 0) return devStats != 0;
	if (devStatsIni < 0) devStatsIni = CE_SettingGetBool("dev_stats", 0) ? 1 : 0;
	return devStatsIni != 0;
}

// The overlay's colour: the original's pale yellow, or the console's green theme.
bool PortCeDevStatsGreen()
{
	static int green = -1;
	if (green < 0) green = CE_SettingIs("console_theme", "green") ? 1 : 0;
	return green != 0;
}

// Between frames (CGame::MainLoop, after the platform's messages): a film asked for in the console plays here, never
// inside the message handler that ran the command.
void PortCeConsoleFrame()
{
	if (!pendingFilm[0]) return;
	char file[64];
	memcpy(file, pendingFilm, sizeof(file));
	pendingFilm[0] = 0;
	if (Allowed()) PlayFilm(file, pendingLocalise);
}

// A level is starting (CGame::InitRestartLoop, after the game's own split-screen default): a split chosen with
// splitscreen applies to the next two-player game, once.
void PortCeConsoleLevelStart()
{
	if (pendingSplit < 0 || !PortCeLayer() || Players() != 2) return;
	SetHorizontalSplit(pendingSplit != 0);
	pendingSplit = -1;
}

void PortCePaletteRender()
{
	PortCePaletteCheck();
	if (!paletteOpen) return;
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	CDXTexture *white = HUD.GetWhiteTexture();
	if (!font || !white || white->GetRealWidth() <= 0 || white->GetRealHeight() <= 0 || font->GetHeight() <= 0) return;
	const float w = float(PLATFORM.GetWindowWidth()), h = float(PLATFORM.GetWindowHeight());
	if (w <= 0.f || h <= 0.f) return;
	// The 640 x 480 layout, scaled to the window; the panel drops over the top of the screen, as the original's did.
	float scale = (w / 640.f < h / 480.f) ? w / 640.f : h / 480.f;
	float step = (float(font->GetHeight()) + 2.f) * scale;
	const float margin = 6.f * scale;
	int rows = int((h * 0.55f - 2.f * margin) / step);
	if (rows < 3)
	{
		// A font too tall for the window: shrink the text until three rows fit.
		scale *= (h * 0.55f - 2.f * margin) / (3.f * step);
		step = (float(font->GetHeight()) + 2.f) * scale;
		rows = 3;
	}
	if (rows > 16) rows = 16;
	const int lines = rows - 2;		// scrollback above the input line and the key hint
	const float left = 8.f * scale, width = w - 16.f * scale;
	const Look look = Colours();
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SRS(D3DRS_ALPHAREF, 8);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	LT.SRS(D3DRS_ZENABLE, FALSE);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.Apply();
	const float panel = step * float(rows) + 2.f * margin;
	CSPRITERENDERER::DrawColouredSprite(0.f, 0.f, 0.001f, white, look.panel, w / white->GetRealWidth(),
									   panel / white->GetRealHeight());
	CSPRITERENDERER::DrawColouredSprite(0.f, panel - scale, 0.001f, white, look.hint, w / white->GetRealWidth(),
									   scale / white->GetRealHeight());
	// The retail scrollback, oldest at the top; line 0 is the one being written, shown only while it has text.
	char text[0x80];
	int newest = (ConsoleLine(0, text, sizeof(text)) && text[0]) ? 0 : 1;
	int stored = 0;
	for (int i = ConsoleLines() - 1; i >= newest; --i)
		if (ConsoleLine(i, text, sizeof(text)) && text[0]) { stored = i - newest + 1; break; }
	if (scroll > stored - lines) scroll = stored - lines > 0 ? stored - lines : 0;
	for (int row = 0; row < lines; ++row)
	{
		const int index = newest + scroll + (lines - 1 - row);
		if (index < ConsoleLines() && ConsoleLine(index, text, sizeof(text)))
			DrawLine(font, white, left, margin + step * float(row), width, scale, look.text, text);
	}
	char line[kInputLimit + 40];
	snprintf(line, sizeof(line), "> %s%s", input, overflow ? " (refused: Backspace clears)" : "_");
	DrawLine(font, white, left, margin + step * float(lines), width, scale, look.text, line);
	DrawLine(font, white, left, margin + step * float(lines + 1), width, scale, look.hint,
			 scroll ? "Scrolled back: PgDn returns. Enter runs. Esc closes."
					: "help lists commands. Enter runs, Tab completes, Up/Down recall, PgUp/PgDn scroll, Esc closes.");
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.Apply();
}
#endif
