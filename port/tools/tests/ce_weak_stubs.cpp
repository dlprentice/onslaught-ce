// SPDX-License-Identifier: GPL-3.0-only
// port/tools/tests/ce_weak_stubs.cpp: weak default definitions of every CE hook point and
// common port-layer query for standalone host C++ test drivers built via port/tools/test_cc.py.
//
// Linking against the cached libbea_test_stubs.a archive lets any host test driver call patched
// game or CE code without re-declaring boilerplate stubs, while still allowing a driver to define
// strong overrides of any symbol it wants to observe or fake. Never compiled into the game.
#include "ce_hooks.h"

#define CE_WEAK __attribute__((weak))

CE_WEAK void			ce_hook_boot() {}
CE_WEAK void			ce_hook_exit(CeHookExitSite) {}
CE_WEAK void			ce_hook_level_begin(int) {}
CE_WEAK void			ce_hook_level_end(int, int) {}
CE_WEAK void			ce_hook_tick_pre() {}
CE_WEAK void			ce_hook_tick_post() {}
CE_WEAK bool			ce_hook_command_push(const CeHookCommand &) { return false; }
CE_WEAK void			ce_hook_input_actions(CController *, int) {}
CE_WEAK bool			ce_hook_key_capture(unsigned char, bool) { return false; }
CE_WEAK void			ce_hook_draw_world(CPlayer *, int) {}
CE_WEAK void			ce_hook_draw_hud(CPlayer *, int) {}
CE_WEAK void			ce_hook_draw_overlay() {}
CE_WEAK void			ce_hook_menu_rows(CeMenuEvent, CeMenuPage *) {}
CE_WEAK void			ce_hook_save_gate(CeHookSaveTarget, CCareer *, char *, int) {}
CE_WEAK void			ce_hook_audio_clock(CeHookAudioEvent, unsigned) {}
CE_WEAK CCamera			*ce_hook_render_camera(int, CCamera *camera, CPlayer *) { return camera; }
CE_WEAK CBattleEngine	*ce_hook_hud_focus(CPlayer *, CBattleEngine *be) { return be; }
CE_WEAK void			ce_hook_unit_spawn(CUnit *, CInitThing *) {}
CE_WEAK void			ce_hook_unit_destroyed(CUnit *) {}
CE_WEAK void			ce_hook_spawner_trigger(CeSpawnerTrigger *) {}
CE_WEAK void			ce_hook_weapon_fire(CWeapon *, CUnit *, CRound *) {}
CE_WEAK void			ce_hook_render_pass(const CeRenderPass *) {}

// Common port-layer query defaults (override with strong definitions in any driver that tests them).
CE_WEAK bool			PortCeLayer() { return true; }
CE_WEAK bool			PortRecording() { return false; }
CE_WEAK bool			PortCeMenus() { return true; }
CE_WEAK bool			PortCeExperimental(const char *) { return false; }
CE_WEAK bool			PortCeIniBool(const char *, const char *, bool fallback) { return fallback; }
CE_WEAK bool			PortWideModes() { return false; }
