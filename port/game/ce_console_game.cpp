// SPDX-License-Identifier: GPL-3.0-only
// The CE console's game actions (ce_console.h). Each one calls what the retail console command would have called
// (src/game.cpp's con_* bodies, Atmospherics.cpp ListAtmospherics, ltshell.cpp con_whatami, DXEngine.cpp
// con_setgammabias, fmv.cpp con_fmv_play, SoundManager.cpp con_playsnd), never CConsole::ExecuteCommand, Exec or
// HandleBind, and only after the checks ce_palette.cpp makes.
#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "frontend.h"
#include "console.h"
#include "CLIParams.h"
#include "ltshell.h"
#include "DXEngine.h"
#include "Career.h"
#include "System.h"
#include "fmv.h"
#include "SoundManager.h"
#include "Camera.h"
#include "player.h"
#include "BattleEngine.h"
#include "ce_console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void ListAtmospherics(char *);		// Atmospherics.cpp (004049a0)

namespace
{
int		sPendingLevel = -1;			// the level the front end's loop runs next (patch 0100), -1 for none
CCamera	*sRemote;					// the remote camera this console made, while it is the game's
}

namespace CeConsoleGame
{
bool ConsoleLine(int i, char *out, int n)
{
	if (!out || n <= 0)
		return false;
	out[0] = 0;
	if (i < 0 || i >= CONSOLE_LINES)
		return false;
	// CConsole::Print does not end the line it is still writing, so never read past the row.
	int k = 0;
	for (; k < n - 1 && k < (int)sizeof(CONSOLE.mLines[i]) && CONSOLE.mLines[i][k]; ++k)
		out[k] = CONSOLE.mLines[i][k];
	out[k] = 0;
	return true;
}

int ConsoleLines() { return CONSOLE_LINES; }

void Print(const char *text)
{
	char line[100];
	strncpy(line, text, sizeof(line) - 1);
	line[sizeof(line) - 1] = 0;
	CONSOLE.Print("%s\n", line);
}

VarType Var(const char *name, void **data)
{
	char key[0x20];
	strncpy(key, name, sizeof(key) - 1);
	key[sizeof(key) - 1] = 0;
	CConsoleVar *v = CONSOLE.FindVariable(key);
	if (!v || !v->mData)
		return VAR_NONE;
	*data = v->mData;
	switch (v->mType)
	{
	case CVar_bool: return VAR_BOOL;
	case CVar_int: return VAR_INT;
	case CVar_float: return VAR_FLOAT;
	default: return VAR_NONE;
	}
}

bool CheatOn(int cheat) { return FRONTEND.mFEPSaveGame.IsCheatActive(cheat) != FALSE; }

const char *Version() { return "Onslaught CE V" CE_VERSION; }

void WhatAmI() { LT.ShowGFXCardInfo(); }

void ListAtmospherics() { ::ListAtmospherics(NULL); }

void NavMap(bool on)
{
	ENGINE.LandscapeNavDisplay(on ? TRUE : FALSE);
	ENGINE.UpdateArea(0, 0, 512, 512);
}

bool NavMapOn() { return ENGINE.GetNavDisplayMODE() != FALSE; }

bool LandscapePresent() { return ENGINE.GetLandscape() != NULL; }

bool FreeCameraOn() { return GAME.IsFreeCameraOn(0) != FALSE; }

bool Paused() { return GAME.IsPaused() != FALSE; }

bool ToggleFreeCamera()
{
	const bool was = FreeCameraOn();
	GAME.ReceiveButtonAction(GAME.GetController(0), BUTTON_TOGGLE_FREE_CAMERA, 1.f);
	return FreeCameraOn() != was;
}

bool RemoteCameraActive()
{
	// A script or cutscene that set its own camera replaced ours, and may have deleted it: forget it, never delete it.
	if (sRemote && (GAME.mRemoteCamera != sRemote || GAME.GetCurrentCamera(0) != sRemote))
		sRemote = NULL;
	return sRemote != NULL;
}

int RemoteCameraOn()
{
	if (GAME.mRemoteCamera || FreeCameraOn())
		return -1;
	CPlayer *player = GAME.GetPlayer(0);
	CBattleEngine *be = player ? player->GetBattleEngine() : NULL;
	CThing *what = be ? (CThing *)be->GetUnitOverCrossHair() : NULL;
	if (!what)
		return 0;
	GAME.mOldRemoteCamera = GAME.GetCamera(0);
	GAME.mRemoteCamera = new (MEMTYPE_CAMERA) CThingCamera(what);
	GAME.SetCamera(0, GAME.mRemoteCamera);
	sRemote = GAME.mRemoteCamera;
	return 1;
}

int RemoteCameraOff()
{
	if (!RemoteCameraActive())
		return 0;
	if (!GAME.mOldRemoteCamera || FreeCameraOn())
		return -1;
	GAME.SetCamera(0, GAME.mOldRemoteCamera);
	GAME.mOldRemoteCamera = NULL;
	delete GAME.mRemoteCamera;
	GAME.mRemoteCamera = NULL;
	sRemote = NULL;
	return 1;
}

void SetGammaBias(float value) { ENGINE.SetGammaBias(value); }

int CurrentLevel() { return GAME.GetCurrentLevel(); }

bool CampaignLevel(int level)
{
	CLevelStructure *s = CAREER.GetLevelStructure();
	if (!s)
		return false;
	for (int i = 0; i < NUM_LEVELS; i++)
		if (s->mData[i][0] == level)
			return true;
	return false;
}

void JumpToLevel(int level)
{
	// A -level start runs SYSTEM.mNextLevel itself (CSystem::Run); the front end's loop takes sPendingLevel (patch 0100).
	if (CLIPARAMS.mLevelNo != -1)
		SYSTEM.mNextLevel = level;
	else
		sPendingLevel = level;
	GAME.SetQuit(QT_QUIT_TO_FRONTEND);
}

void Win() { GAME.DeclareLevelWon(); }

void Lose() { GAME.DeclareLevelLost(); }

bool Film(const char *name, char *file, int n, bool *localise)
{
	// The films the game plays itself: the in-game cutscenes by number (the Goodies page's 201-232 use 1-33; data\video\
	// cutscenes, or the _small copy, as MakeCutsceneName chooses), the Goodies page's two extra films, and the boot films.
	static const char *const fixed[] = {"GILL_M_on_a_fork", "UsTheMovie", "ltlogo", "openingfmv"};
	for (const char *f : fixed)
		if (!stricmp(name, f))
		{
			snprintf(file, n, "%s", f);
			*localise = false;
			return true;
		}
	int number = 0, digits = 0;
	for (const char *c = name; *c; ++c, ++digits)
	{
		if (*c < '0' || *c > '9' || digits >= 2)
			return false;
		number = number * 10 + (*c - '0');
	}
	if (!digits || number < 1 || number > 33)
		return false;
	char made[64];
	MakeCutsceneName(made, number);
	snprintf(file, n, "%s", made);
	// FEPGoodies.cpp get_goodie_fmv_localised: these cutscenes have one version for every language.
	*localise = !(number == 9 || number == 12 || number == 13 || number == 14 || number == 15 || number == 16 ||
				  number == 28 || number == 31 || number == 33);
	return true;
}

void PlayFilm(const char *file, bool localise)
{
	char name[64];
	snprintf(name, sizeof(name), "%s", file);
	FMV.PlayFullscreen(name, TRUE, localise ? TRUE : FALSE);
}

bool Sound(const char *name, char *exact, int n)
{
	for (CSample *s = SOUND.GetFirstSample(); s; s = s->mNextSample)
		if (!strnicmp(s->mName, name, sizeof(s->mName)) && strlen(name) < sizeof(s->mName))
		{
			snprintf(exact, n, "%.*s", (int)sizeof(s->mName) - 1, s->mName);
			return true;
		}
	return false;
}

int Sounds()
{
	int n = 0;
	for (CSample *s = SOUND.GetFirstSample(); s && n < 100000; s = s->mNextSample)
		++n;
	return n;
}

void PlaySample(const char *exact)
{
	char name[100];
	snprintf(name, sizeof(name), "%s", exact);
	SOUND.PlayNamedSample(name, NULL);
}

int Players() { return GAME.GetNPlayers(); }

void SetHorizontalSplit(bool horizontal)
{
	// CGame::InitRestartLoop registers cg_horizontalsplitscreen (CGame::mHorizontalSplitscreen) just before this runs.
	void *data = NULL;
	if (Var("cg_horizontalsplitscreen", &data) == VAR_BOOL)
		*(bool *)data = horizontal;
}
}

// The developer information text (patch 0102): CGame::DrawDebugStuff appends the lines its developer build wrote to
// DBT, then this draws them as that build did: FONT_DEBUG at (40, 30), pale yellow 0xffffff80 over a black shadow at
// (41, 31). The buffer is bounded and reset after each draw; nothing here is game state.
namespace
{
char	sDevStats[1024];
int		sDevStatsLength;
}

void PortCeDevStatsOut(const char *fmt, ...)
{
	if (sDevStatsLength >= (int)sizeof(sDevStats) - 1)
		return;
	va_list args;
	va_start(args, fmt);
	int n = vsnprintf(sDevStats + sDevStatsLength, sizeof(sDevStats) - sDevStatsLength, fmt, args);
	va_end(args);
	if (n > 0)
		sDevStatsLength += n;
	if (sDevStatsLength > (int)sizeof(sDevStats) - 1)
		sDevStatsLength = sizeof(sDevStats) - 1;
}

void PortCeDevStatsDraw()
{
	sDevStats[sDevStatsLength] = 0;
	sDevStatsLength = 0;
	CDXBitmapFont *font = PLATFORM.Font(FONT_DEBUG);
	if (!font || !sDevStats[0])
		return;
	// The original drew this at x 40 at its own scale; its first line is wider than a 640-pixel window, so the block
	// shrinks until its widest line fits (the review saw it clipped at 640 x 480).
	float widest = 0.f;
	for (char *line = sDevStats; *line;)
	{
		char *end = strchr(line, '\n');
		char keep = end ? *end : 0;
		if (end) *end = 0;
		SIZE size = {0, 0};
		if (font->GetTextExtent(ToWCHAR(line), &size) && size.cx > widest)
			widest = (float)size.cx;
		if (!end) break;
		*end = keep;
		line = end + 1;
	}
	const float room = (float)PLATFORM.GetWindowWidth() - 48.f;
	const float scale = (widest > room && room > 0.f) ? room / widest : 1.f;
	WCHAR *w = ToWCHAR(sDevStats);
	font->DrawTextScaled(41, 31, 0.0f, scale, scale, 0xff000000, w, 0, NULL);
	font->DrawTextScaled(40, 30, 0.0f, scale, scale, PortCeDevStatsGreen() ? 0xff40ff40 : 0xffffff80, w, 0, NULL);
}

// The front end's loop (patch 0100, System.cpp): the level a console jump asked for, once.
int PortCeConsoleTakeLevel()
{
	int level = sPendingLevel;
	sPendingLevel = -1;
	return level;
}

#endif
