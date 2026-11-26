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
//   hud_focus       RENDER  OVERRIDE_CHAIN     Hud.cpp CHud::RenderOverlayForViewpoint, after player->GetBattleEngine(), before mBattleEngine
//   unit_spawn      SIM     BROADCAST          Unit.cpp CUnit::Init, its end, after WORLD allegiance-set registration and next-frame event
//   unit_destroyed  SIM     BROADCAST          Unit.cpp CUnit::StartDieProcess and BattleEngine.cpp CBattleEngine::StartDieProcess, before return TRUE
//   spawner_trigger SIM     FILTER             SpawnerThng.cpp CSpawner::DoSpawn and CSpawnerThing::Unknown4e3370, before spawning a wave or unit
//   weapon_fire     SIM     BROADCAST          weapon.cpp CWeapon::SpawnRound, after round->Init(&init)
//   render_pass     RENDER  BROADCAST          DXEngine.cpp CDXEngine::Render, pre-world (after SetViewport) and post-world (after draw_fairly_late_stuff)
//   world_source    SIM     FIRST_MATCH        world.cpp CWorld::Load & ResourceAccumulator.cpp CResourceAccumulator::ReadResources
//   projectile_intercept SIM FILTER            Round.cpp CRound::Move, CRound::HandleEvent, CRound::DeclareOnGround
//   unit_damage     SIM     BROADCAST          Unit.cpp CUnit::Damage (after weakpoint, before mSegments) & BattleEngine.cpp CBattleEngine::Damage (saved rawAmount)
//   player_act      SIM     BROADCAST          game.cpp CGame::Update (unpaused GAME_STATE_PLAYING, after human flush, before EVENT_MANAGER.Flush)
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

#include <stdint.h>

// Hook API version contract: major bumps on breaking signature, enum, or dispatch changes; minor bumps on
// backwards-compatible additions (new hook points or appended enum values). Checked at compile time in ce_hooks.cpp
// and verified by port/tools/ce_hooks_tests.py.
#define CE_HOOKS_API_VERSION_MAJOR 3u
#define CE_HOOKS_API_VERSION_MINOR 0u
#define CE_HOOKS_API_VERSION ((CE_HOOKS_API_VERSION_MAJOR << 8) | CE_HOOKS_API_VERSION_MINOR)
#define CE_HOOKS_POINT_COUNT 25u

class CPlayer;
class CController;
class CCareer;
class CFrontEnd;
class CPauseMenu;
class CMenuOptionsList;
class CCamera;
class CBattleEngine;
class CThing;
class CUnit;
class CInitThing;
class CSpawner;
class CSpawnerThing;
class CWeapon;
class CRound;

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
// CE_SAVE_OPTIONS_FILE (dest is the whole file about to be written). Slot 10 (`career_snapshot_save`) enforces
// CeHookSaveCareerValid (non-NULL pointers and `size >= (int)sizeof(CCareer)`) and dispatches exclusively to the
// single active CeCareerSnapshotOwner() (ce_modes.h), eliminating multi-filter whole-copy overwrite hazards.
// Higher slots (>= 20) are reserved for incremental field filters that compose freely in table order.
void			ce_hook_save_gate(CeHookSaveTarget target, CCareer *career, char *dest, int size);
void			ce_hook_audio_clock(CeHookAudioEvent event, unsigned bytes);
// render_camera (provisional contract): a handler returns the camera this viewpoint draws from, or the camera it was
// given. The engine keeps the original camera for the original's own world pass and puts it back itself after a CE
// pass. Tagged SIM because the drawing camera decides which component transforms the simulation refreshes; what the
// simulation sees while a CE camera is active (for example CE passes flagged as the mirror pass is, so the refresh
// skips them) is a later change of the simulation, not part of this point.
CCamera			*ce_hook_render_camera(int viewpoint, CCamera *camera, CPlayer *player);
// hud_focus: the Battle Engine whose state the HUD shows for this player (defaults to player->GetBattleEngine()).
// Wired in Hud.cpp CHud::RenderOverlayForViewpoint; returning `be` preserves exact retail behavior. A non-NULL engine
// returned without an attached player (`GetPlayer() == NULL`) is rejected and falls back cleanly to incoming `be`
// (or NULL if `be` itself has no player).
CBattleEngine	*ce_hook_hud_focus(CPlayer *player, CBattleEngine *be);

// spawner_trigger: FILTER before CSpawner::DoSpawn (monitor/factory wave) or CSpawnerThing::Unknown4e3370 (map actor).
// `allow` starts true; handlers run in table order and any handler may clear `allow` to veto this spawn cleanly.
enum CeSpawnerKind { CE_SPAWNER_MONITOR = 0, CE_SPAWNER_ACTOR = 1 };
struct CeSpawnerTrigger
{
	CeSpawnerKind	kind;
	CSpawner		*spawner;		// set at CE_SPAWNER_MONITOR, else NULL
	CSpawnerThing	*actor;			// set at CE_SPAWNER_ACTOR, else NULL
	CUnit			*owner;			// factory/carrier unit at CE_SPAWNER_MONITOR, else NULL
	const char		*unit_name;		// unit class name about to spawn
	int				squad_size;		// wave squad size
	int				allegiance;		// EAllegiance value
	bool			allow;			// starts true; set false to veto this spawn trigger
};

// render_pass: structured pre-world and post-world scene pass hook inside CDXEngine::Render for visual overlays & mods.
// Tagged RENDER: handlers must never mutate simulation state.
enum CeRenderPassStage { CE_RENDER_PASS_PRE_WORLD = 0, CE_RENDER_PASS_POST_WORLD = 1 };
struct CeRenderPass
{
	CeRenderPassStage	stage;
	int					viewpoint;
	CPlayer				*player;
	CCamera				*camera;
	bool				mirror_pass;
};

// unit_spawn: SIM BROADCAST at the end of CUnit::Init (fires for every spawned CUnit, including CBattleEngine).
void			ce_hook_unit_spawn(CUnit *unit, CInitThing *init);
// unit_destroyed: SIM BROADCAST when a unit commits to dying in CUnit::StartDieProcess or CBattleEngine::StartDieProcess.
void			ce_hook_unit_destroyed(CUnit *unit);
void			ce_hook_spawner_trigger(CeSpawnerTrigger *trigger);
// weapon_fire: SIM BROADCAST in CWeapon::SpawnRound immediately after round->Init(&init).
void			ce_hook_weapon_fire(CWeapon *weapon, CUnit *owner, CRound *round);
void			ce_hook_render_pass(const CeRenderPass *pass);

// world_source (v3.0): SIM FIRST_MATCH in ResourceAccumulator.cpp & world.cpp for donor archives, chunk policies,
// and custom world byte streams (retiring per-mode cascade patches).
enum CeWorldSourceStage
{
	CE_WORLD_SRC_DONOR_ARCHIVE = 0,
	CE_WORLD_SRC_CHUNK_POLICY  = 1,
	CE_WORLD_SRC_READ_BYTES    = 2
};
struct CeWorldSourceReq
{
	CeWorldSourceStage	stage;
	int					level;			// CE_WORLD_SRC_DONOR_ARCHIVE
	uint32_t			fourcc;			// CE_WORLD_SRC_CHUNK_POLICY
	const char			*wrd_name;		// CE_WORLD_SRC_READ_BYTES
	const char			*archive_path;	// out: CE_WORLD_SRC_DONOR_ARCHIVE
	int					chunk_policy;	// out: CeWorldChunkPolicy value
	void				(*post_eres)();	// out: optional post-ERES installer
	int					read_status;	// out: <0 fail, 0 retail, >0 owned
	const void			*bytes;			// out: CE_WORLD_SRC_READ_BYTES
	unsigned			length;			// out: CE_WORLD_SRC_READ_BYTES
};

// projectile_intercept (v3.0): SIM FILTER in Round.cpp CRound::Move, HandleEvent (4000), and DeclareOnGround.
enum CeProjectileInterceptStage
{
	CE_PROJ_PRE_MOVE   = 0,
	CE_PROJ_POST_MOVE  = 1,
	CE_PROJ_FLAK_CHECK = 2,
	CE_PROJ_BEAM_CLAMP = 3,
	CE_PROJ_ON_GROUND  = 4
};
struct CeProjectileIntercept
{
	CeProjectileInterceptStage	stage;
	CRound						*round;
	float						prev_x, prev_y, prev_z;
	float						cur_x, cur_y, cur_z;
	float						hit_x, hit_y, hit_z;
	float						vel_x, vel_y, vel_z;
	bool						dying;
	bool						intercepted;
};

// unit_damage (v3.0): SIM ORDERED_BROADCAST in CUnit::Damage (after weakpoint mult, before mSegments)
// and CBattleEngine::Damage (with pre-shield rawAmount). CRocket::Damage does not call CUnit::Damage and is excluded.
enum CeUnitDamageFlags : uint32_t
{
	CE_DMG_NONE          = 0u,
	CE_DMG_SHIELD_TARGET = 1u << 0,
	CE_DMG_SEGMENT_HIT   = 1u << 1,
	CE_DMG_BATTLE_ENGINE = 1u << 2
};
struct CeUnitDamage
{
	CUnit		*victim;		// always non-NULL
	CThing		*source;		// nullable (CRound*, CExplosion*, CUnit*, etc.)
	float		rawAmount;		// positive pre-shield damage (after weakpoint mult)
	int			meshPart;		// mesh_part_no (-1 if none)
	uint32_t	flags;			// CeUnitDamageFlags
};

bool			ce_hook_world_source(CeWorldSourceReq *req);
void			ce_hook_projectile_intercept(CeProjectileIntercept *ev);
void			ce_hook_unit_damage(const CeUnitDamage *ev);
// Monotonic gameplay simulation tick counter (reset on level_begin/level_end; incremented only when unpaused GAME_STATE_PLAYING).
uint32_t		CeHooksStepGameplayTick(void);
// player_act (v3.0): SIM ORDERED_BROADCAST inside unpaused GAME_STATE_PLAYING after human controller flush and before EVENT_MANAGER.Flush.
void			ce_hook_player_act(uint32_t sim_tick);

#endif
