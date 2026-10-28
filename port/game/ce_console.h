// SPDX-License-Identifier: GPL-3.0-only
// The CE console's game actions (port/game/ce_console_game.cpp). The console itself (ce_palette.cpp: its panel, line
// editor, parser and fences) reaches the game only through these, so its tests run against authored fakes. None of them
// goes through the retail console's executor, script runner or bind table; each calls the game function the retail
// command would have called, after the checks the console makes first.
#ifndef PORT_CE_CONSOLE_H
#define PORT_CE_CONSOLE_H

namespace CeConsoleGame
{
enum VarType { VAR_NONE, VAR_BOOL, VAR_INT, VAR_FLOAT };

// The retail console's scrollback (CConsole::mLines, newest first; 0 is the line being written): line i copied into out
// (at most n - 1 bytes, always terminated). False when there is no such line.
bool	ConsoleLine(int i, char *out, int n);
int		ConsoleLines();
// Prints text as one line of the retail scrollback (CConsole::Print("%s\n")); text is already bounded by the caller.
void	Print(const char *text);

// A registered console variable by name (CConsole::FindVariable): its type and storage, or VAR_NONE.
VarType	Var(const char *name, void **data);

bool	CheatOn(int cheat);							// CFEPSaveGame::IsCheatActive
const char *Version();							// "Onslaught CE V..."
void	WhatAmI();								// PCLTShell::ShowGFXCardInfo (cg_whatami)
void	ListAtmospherics();						// ListAtmospherics (Atmospherics.cpp)
void	NavMap(bool on);						// con_navmapon / con_navmapoff
bool	NavMapOn();
bool	LandscapePresent();
bool	FreeCameraOn();
bool	Paused();
bool	ToggleFreeCamera();						// BUTTON_TOGGLE_FREE_CAMERA from player 1's controller; true when it changed
// Remote camera (con_remotecameraon/off): 1 done, 0 nothing to do (nothing under the crosshair, or none active),
// -1 refused here. RemoteCameraActive says whether the camera the game shows is still the one the console made.
int		RemoteCameraOn();
int		RemoteCameraOff();
bool	RemoteCameraActive();
void	SetGammaBias(float value);				// CDXEngine::SetGammaBias
int		CurrentLevel();
bool	CampaignLevel(int level);				// a level in the career's level structure (the level select's levels)
void	JumpToLevel(int level);					// leave this level for that one (con_map, and the front end's loop)
void	Win();									// CGame::DeclareLevelWon
void	Lose();									// CGame::DeclareLevelLost
// A film the game itself plays: its file name for CFMV::PlayFullscreen and whether that is localised; false when unknown.
bool	Film(const char *name, char *file, int n, bool *localise);
void	PlayFilm(const char *file, bool localise);	// CFMV::PlayFullscreen, skippable
// A sound the game has already loaded (CSoundManager's sample list, compared ignoring case): its exact name in exact.
// Never loads a file by a typed name.
bool	Sound(const char *name, char *exact, int n);
int		Sounds();
void	PlaySample(const char *exact);			// CSoundManager::PlayNamedSample
int		Players();
void	SetHorizontalSplit(bool horizontal);	// cg_horizontalsplitscreen (CGame::mHorizontalSplitscreen)
}

// The console's hooks in the game (patch 0100): each level's end, between frames, and a level's start.
void	PortCeConsoleLevelEnd();
void	PortCeConsoleFrame();
void	PortCeConsoleLevelStart();
int		PortCeConsoleTakeLevel();
// The developer information overlay (patch 0102): whether to draw it, its colour, and its text (ce_console_game.cpp).
bool	PortCeDevStatsOn();
bool	PortCeDevStatsGreen();
void	PortCeDevStatsOut(const char *fmt, ...);
void	PortCeDevStatsDraw();

#endif
