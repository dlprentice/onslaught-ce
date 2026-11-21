// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_hooks.cpp: the CE hook points (ce_hooks.h) and their one handler table. Every build compiles this file;
// without CE_VERSION each point is an empty function. With CE_VERSION each point runs the rows of its table by its
// dispatch (ce_hooks_dispatch.h): a row's slot orders it, a row runs only while its gate is open and its mode active, and
// the table is checked while compiling (CE_HOOK_CHECK) and by port/tools/ce_hooks_tests.py. No handler is registered
// yet: every point returns at once and the game runs as it did before the points existed.
#include "ce_hooks.h"
#include "ce_hooks_dispatch.h"

#ifdef CE_VERSION

typedef void (*CeHookVoidFn)();
typedef void (*CeHookExitFn)(CeHookExitSite site);
typedef void (*CeHookLevelBeginFn)(int level);
typedef void (*CeHookLevelEndFn)(int level, int quit);
typedef void (*CeHookCommandFn)(const CeHookCommand *command);
typedef void (*CeHookInputFn)(CController *controller, int pad);
typedef bool (*CeHookKeyFn)(unsigned char key, bool repeat);
typedef void (*CeHookViewFn)(CPlayer *player, int viewpoint);
typedef void (*CeHookMenuFn)(CeMenuEvent event, CeMenuPage *page);
typedef void (*CeHookSaveFn)(CeHookSaveTarget target, CCareer *career, char *dest, int size);
typedef void (*CeHookAudioFn)(CeHookAudioEvent event, unsigned bytes);
typedef CCamera *(*CeHookCameraFn)(CCamera *camera, int viewpoint, CPlayer *player);
typedef CBattleEngine *(*CeHookFocusFn)(CBattleEngine *be, CPlayer *player);

// ---- the table: per point, its handlers in the order they run ----
// A row: {slot, "name", CE_HOOK_SIM or CE_HOOK_RENDER, CE_GATE_VERSION or CE_GATE_LAYER, CE_MODE_..., function},
// slots ascending (leave gaps, 10 20 30, so a later handler can go between two).
static constexpr CeHookRow<CeHookVoidFn> kBoot[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kBoot, CE_HOOK_SIM, CE_DISPATCH_ORDERED_BROADCAST, "boot");
static constexpr CeHookRow<CeHookExitFn> kExit[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kExit, CE_HOOK_SIM, CE_DISPATCH_ORDERED_BROADCAST, "exit");
static constexpr CeHookRow<CeHookLevelBeginFn> kLevelBegin[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kLevelBegin, CE_HOOK_SIM, CE_DISPATCH_ORDERED_BROADCAST, "level_begin");
static constexpr CeHookRow<CeHookLevelEndFn> kLevelEnd[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kLevelEnd, CE_HOOK_SIM, CE_DISPATCH_ORDERED_BROADCAST, "level_end");
static constexpr CeHookRow<CeHookVoidFn> kTickPre[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kTickPre, CE_HOOK_SIM, CE_DISPATCH_ORDERED_BROADCAST, "tick_pre");
static constexpr CeHookRow<CeHookCommandFn> kTickPost[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kTickPost, CE_HOOK_SIM, CE_DISPATCH_COMMAND_APPLY, "tick_post");
// input_actions broadcasts: each handler adds its own actions to the controller in table order, after the bindings
// (today: the gamepad's extras, then the Dodge taps); none stops another, and an action already set stays set.
static constexpr CeHookRow<CeHookInputFn> kInputActions[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kInputActions, CE_HOOK_SIM, CE_DISPATCH_ORDERED_BROADCAST, "input_actions");
static constexpr CeHookRow<CeHookKeyFn> kKeyCapture[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kKeyCapture, CE_HOOK_RENDER, CE_DISPATCH_FIRST_MATCH, "key_capture");
static constexpr CeHookRow<CeHookViewFn> kDrawWorld[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kDrawWorld, CE_HOOK_RENDER, CE_DISPATCH_ORDERED_BROADCAST, "draw_world");
static constexpr CeHookRow<CeHookViewFn> kDrawHud[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kDrawHud, CE_HOOK_RENDER, CE_DISPATCH_ORDERED_BROADCAST, "draw_hud");
static constexpr CeHookRow<CeHookVoidFn> kDrawOverlay[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kDrawOverlay, CE_HOOK_RENDER, CE_DISPATCH_ORDERED_BROADCAST, "draw_overlay");
static constexpr CeHookRow<CeHookMenuFn> kMenuRows[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kMenuRows, CE_HOOK_RENDER, CE_DISPATCH_POSITIONAL_INSERT, "menu_rows");
// save_gate, in today's order when its handlers move here: editor (patch 0222), bonus level (0181), museum visit (0279).
static constexpr CeHookRow<CeHookSaveFn> kSaveGate[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kSaveGate, CE_HOOK_SIM, CE_DISPATCH_FILTER, "save_gate");
static constexpr CeHookRow<CeHookAudioFn> kAudioClock[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kAudioClock, CE_HOOK_RENDER, CE_DISPATCH_ORDERED_BROADCAST, "audio_clock");
static_assert(CeHookWorkerSafe(kAudioClock), "audio_clock: a handler may run on the music worker thread: CE_GATE_VERSION and CE_MODE_NONE only");
// render_camera, in today's order when its handlers move here: commander (patch 0200), then play-unit.
static constexpr CeHookRow<CeHookCameraFn> kRenderCamera[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kRenderCamera, CE_HOOK_SIM, CE_DISPATCH_OVERRIDE_CHAIN, "render_camera");
// hud_focus chains like render_camera: each handler may answer another Battle Engine for the HUD to read, and the last
// open one decides (the HUD shows one machine; a FIRST_MATCH would need a "no answer" value the game does not have).
static constexpr CeHookRow<CeHookFocusFn> kHudFocus[] = {
	CE_HOOK_END
};
CE_HOOK_CHECK(kHudFocus, CE_HOOK_RENDER, CE_DISPATCH_OVERRIDE_CHAIN, "hud_focus");
// ---- end of the table ----

static CeHookCommandQueue gCommands;

void ce_hook_boot() { CeHookBroadcast("boot", kBoot); }
void ce_hook_exit(CeHookExitSite site) { CeHookBroadcast("exit", kExit, site); }
void ce_hook_level_begin(int level) { CeHookBroadcast("level_begin", kLevelBegin, level); }
void ce_hook_level_end(int level, int quit) { CeHookBroadcast("level_end", kLevelEnd, level, quit); }
void ce_hook_tick_pre() { CeHookBroadcast("tick_pre", kTickPre); }
void ce_hook_tick_post() { CeHookApply("tick_post", kTickPost, &gCommands); }
bool ce_hook_command_push(const CeHookCommand &command) { return gCommands.Push(command); }
void ce_hook_input_actions(CController *controller, int pad) { CeHookBroadcast("input_actions", kInputActions, controller, pad); }
bool ce_hook_key_capture(unsigned char key, bool repeat) { return CeHookFirstMatch("key_capture", kKeyCapture, key, repeat); }
void ce_hook_draw_world(CPlayer *player, int viewpoint) { CeHookBroadcast("draw_world", kDrawWorld, player, viewpoint); }
void ce_hook_draw_hud(CPlayer *player, int viewpoint) { CeHookBroadcast("draw_hud", kDrawHud, player, viewpoint); }
void ce_hook_draw_overlay() { CeHookBroadcast("draw_overlay", kDrawOverlay); }
void ce_hook_menu_rows(CeMenuEvent event, CeMenuPage *page) { CeHookBroadcast("menu_rows", kMenuRows, event, page); }
void ce_hook_save_gate(CeHookSaveTarget target, CCareer *career, char *dest, int size) { CeHookFilter("save_gate", kSaveGate, target, career, dest, size); }
void ce_hook_audio_clock(CeHookAudioEvent event, unsigned bytes)
{
	// no gate, no mode and no guard: audio_clock rows are CE_GATE_VERSION and CE_MODE_NONE (CeHookWorkerSafe), and the
	// handler may be on the music worker thread, where the guard's digest of the simulation must not be read
	for (const auto *r = kAudioClock; r->name; ++r)
		r->fn(event, bytes);
}
CCamera *ce_hook_render_camera(int viewpoint, CCamera *camera, CPlayer *player) { return CeHookOverride("render_camera", kRenderCamera, camera, viewpoint, player); }
CBattleEngine *ce_hook_hud_focus(CPlayer *player, CBattleEngine *be) { return CeHookOverride("hud_focus", kHudFocus, be, player); }

#else	// no CE_VERSION: a private build has no CE layer; every point is inert

void ce_hook_boot() {}
void ce_hook_exit(CeHookExitSite) {}
void ce_hook_level_begin(int) {}
void ce_hook_level_end(int, int) {}
void ce_hook_tick_pre() {}
void ce_hook_tick_post() {}
bool ce_hook_command_push(const CeHookCommand &) { return false; }
void ce_hook_input_actions(CController *, int) {}
bool ce_hook_key_capture(unsigned char, bool) { return false; }
void ce_hook_draw_world(CPlayer *, int) {}
void ce_hook_draw_hud(CPlayer *, int) {}
void ce_hook_draw_overlay() {}
void ce_hook_menu_rows(CeMenuEvent, CeMenuPage *) {}
void ce_hook_save_gate(CeHookSaveTarget, CCareer *, char *, int) {}
void ce_hook_audio_clock(CeHookAudioEvent, unsigned) {}
CCamera *ce_hook_render_camera(int, CCamera *camera, CPlayer *) { return camera; }
CBattleEngine *ce_hook_hud_focus(CPlayer *, CBattleEngine *be) { return be; }

#endif
