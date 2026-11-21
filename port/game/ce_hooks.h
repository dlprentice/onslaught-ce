// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_hooks.h: the named points where the CE layer joins the game's own code. Each point is one function,
// ce_hook_<name>, called from the game's files by the 03xx patches (one patch per point). Which handlers run at a point,
// and in what order, is one compiled table in ce_hooks.cpp: no registration at run time, no static initialisers, no link
// order. A build without CE_VERSION compiles every point as an empty function, so the game's call is inert there.
//
// Each point and each handler is tagged with what the handler itself may write: SIM (game state; it runs as part of the
// simulation's step or the game's lifecycle, the same way in every run of a tape) or RENDER (presentation only: it reads
// game state and draws or answers input). A handler carries its point's tag (port/tools/ce_hooks_tests.py and the
// static_asserts in ce_hooks.cpp refuse a mismatch either way); a feature that needs both registers two handlers on two
// points. Test builds do not trust the tag: a guard runs before and after every handler (ce_hooks_dispatch.h). Each point
// has one dispatch, named in its table (ce_hooks_dispatch.h): BROADCAST, FIRST_MATCH, POSITIONAL_INSERT, FILTER,
// COMMAND_APPLY or OVERRIDE_CHAIN.
//
//   point           tag     dispatch           site (the game's file and function)
//   boot            SIM     BROADCAST          ltshell.cpp WinMain, after the memory manager starts, before the options file is read
//   exit            SIM     BROADCAST          ltshell.cpp PCLTShell::MainLoop end (CE_EXIT_SHELL) and ExitCode (CE_EXIT_ATEXIT, every exit())
//   level_begin     SIM     BROADCAST          game.cpp CGame::RestartLoopRunLevel, after the demo wait, before the pre-run ticks (per attempt)
//   level_end       SIM     BROADCAST          game.cpp CGame::ShutdownRestartLoop, before anything is freed (runs twice on a load failure)
//   tick_pre        SIM     BROADCAST          game.cpp CGame::Update, after its early returns (a tick that runs; also paused and pre-run ticks)
//   tick_post       SIM     COMMAND_APPLY      game.cpp CGame::Update, its end
//   input_actions   SIM     BROADCAST          Controller.cpp CController::DoMappings, after every action of this flush, before recording
//   key_capture     RENDER  FIRST_MATCH        ltshell.cpp PCLTShell::MsgProc, WM_KEYDOWN, in the first-match chain after today's CE keys
//   draw_world      RENDER  BROADCAST          DXEngine.cpp draw_fairly_late_stuff, world matrices backed up, before the cockpit and screen FX
//   draw_hud        RENDER  BROADCAST          Hud.cpp CHud::RenderOverlayForViewpoint, its end, over the original HUD
//   draw_overlay    RENDER  BROADCAST          DXEngine.cpp CDXEngine::PostRender, after the CE overlays, before EndScene
//   menu_rows       RENDER  POSITIONAL_INSERT  FrontEnd.cpp CFrontEnd::Init (page slots), PauseMenu.cpp CPauseMenu::CPauseMenu (pause list,
//                                              options registration)
//   save_gate       SIM     FILTER             Career.cpp CCareer::Save, after the career filters, before the god flags are put back;
//                                              FEPOptions.cpp CFEPOptions::WriteDefaultOptionsFile, before the options file is replaced
//   audio_clock     RENDER  BROADCAST          PCMusicStream.cpp MusicStreamHandle, a refill (worker thread under a real clock, main thread
//                                              under the virtual one, inside the stream guard); GetMusicStreamFinished, the finished poll
//   render_camera   SIM     OVERRIDE_CHAIN     engine.cpp CEngine::SetViewpoint, after today's camera overrides
//   hud_focus       RENDER  OVERRIDE_CHAIN     no site yet (the HUD's reads of the player's Battle Engine are not redirected in this tree)
//
// A handler row also names a gate (CE_GATE_VERSION: whenever CE_VERSION is compiled in; CE_GATE_LAYER: only while
// PortCeLayer() is true, so never in a replay, a recording or a probe run) and a mode (ce_modes.h: CE_MODE_NONE runs
// always, any other mode only while that mode is active). audio_clock handlers take CE_GATE_VERSION and CE_MODE_NONE only:
// they may run on the music worker thread, where neither PortCeLayer's first call nor the mode registry may be read.
//
// HOST TEST DRIVERS: a driver that compiles an extracted piece of a patched original file (or a whole mirror file) meets
// these calls. Include port/game/ce_hooks_host_stubs.h once, before the extracted code, with port/game on the include
// path; never write a fake of a hook by hand. Adding a point here without its stub fails ce_hooks_tests.py.
#ifndef PORT_CE_HOOKS_H
#define PORT_CE_HOOKS_H

class CPlayer;
class CController;
class CCareer;
class CFrontEnd;
class CPauseMenu;
class CMenuOptionsList;
class CCamera;
class CBattleEngine;

enum CeHookExitSite { CE_EXIT_SHELL = 0, CE_EXIT_ATEXIT = 1 };
enum CeHookSaveTarget { CE_SAVE_CAREER = 0, CE_SAVE_OPTIONS_FILE = 1 };
enum CeHookAudioEvent { CE_AUDIO_REFILL = 0, CE_AUDIO_FINISHED_POLL = 1 };

// menu_rows is page registration. The game calls it with a page and an event; a handler adds rows by kind at a position
// (rows have positions the player sees), follows focus, and learns when the page opens, closes and starts to accept
// input (after its fade or its select guard). Only CE_MENU_BUILD has sites in this tree; the other events are named
// here so the signature is settled, and get their sites with their first handler.
enum CeMenuSite { CE_MENU_FRONTEND_PAGES = 0, CE_MENU_PAUSE_LIST = 1, CE_MENU_PAUSE_OPTIONS = 2 };
enum CeMenuEvent { CE_MENU_BUILD = 0, CE_MENU_OPEN = 1, CE_MENU_CLOSE = 2, CE_MENU_FOCUS = 3, CE_MENU_ACCEPTS_INPUT = 4 };
enum CeMenuRowKind { CE_ROW_ACTION = 0, CE_ROW_TOGGLE = 1, CE_ROW_CHOICE = 2, CE_ROW_SLIDER = 3, CE_ROW_PAGE = 4, CE_ROW_BACK = 5 };
struct CeMenuRow
{
	unsigned		id;			// stable across builds and languages: a reader finds a row by it, not by its text
	CeMenuRowKind	kind;
	const char		*text;
	bool			enabled;
	int				position;	// the index the row takes in its list; -1 appends
};
struct CeMenuPage
{
	CeMenuSite			site;
	int					page;		// the front end's page number, or -1 on the pause menu
	CFrontEnd			*frontend;	// set at CE_MENU_FRONTEND_PAGES
	CPauseMenu			*pause;		// set at the pause menu's sites
	CMenuOptionsList	*list;		// the list being built (the pause list), else NULL
	int					focus;		// the focused row's index, or -1
};

void			ce_hook_boot();
void			ce_hook_exit(CeHookExitSite site);
void			ce_hook_level_begin(int level);
void			ce_hook_level_end(int level, int quit);
void			ce_hook_tick_pre();
void			ce_hook_tick_post();									// applies the queued commands, oldest first
struct CeHookCommand;
bool			ce_hook_command_push(const CeHookCommand &command);	// main thread; false when the queue is full
void			ce_hook_input_actions(CController *controller, int pad);
bool			ce_hook_key_capture(unsigned char key, bool repeat);		// true: consumed (no keytrap, no bind)
void			ce_hook_draw_world(CPlayer *player, int viewpoint);
void			ce_hook_draw_hud(CPlayer *player, int viewpoint);
void			ce_hook_draw_overlay();
void			ce_hook_menu_rows(CeMenuEvent event, CeMenuPage *page);
// save_gate: `career` is the live career at CE_SAVE_CAREER (dest holds its copy, `size` bytes) and NULL at
// CE_SAVE_OPTIONS_FILE (dest is the whole file about to be written). Handlers run in table order and each may rewrite
// dest; today's three career filters (editor, bonus level, museum visit) each replace the whole copy, so with two active
// the later one's snapshot wins (port/tools/ce_hooks_tests.py keeps that hazard as an expected failure).
void			ce_hook_save_gate(CeHookSaveTarget target, CCareer *career, char *dest, int size);
void			ce_hook_audio_clock(CeHookAudioEvent event, unsigned bytes);
// render_camera (provisional contract): a handler returns the camera this viewpoint draws from, or the camera it was
// given. The engine keeps the original camera for the original's own world pass and puts it back itself after a CE
// pass. Tagged SIM because the drawing camera decides which component transforms the simulation refreshes; what the
// simulation sees while a CE camera is active (for example CE passes flagged as the mirror pass is, so the refresh
// skips them) is a later change of the simulation, not part of this point.
CCamera			*ce_hook_render_camera(int viewpoint, CCamera *camera, CPlayer *player);
// hud_focus: the Battle Engine whose state the HUD shows for this player (today always the player's own). No site yet.
CBattleEngine	*ce_hook_hud_focus(CPlayer *player, CBattleEngine *be);

#endif
