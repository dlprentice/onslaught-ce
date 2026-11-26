// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_hooks_host_stubs.h: the CE hook points for HOST test drivers that compile an extracted piece of the original
// (or a whole mirror file) without the game: the real declarations (ce_hooks.h) plus a definition of every hook point that
// does what the game does with no handler registered: nothing (a key is never consumed, a camera and a Battle Engine come
// back unchanged, a command is never queued). Include it ONCE per driver, before the extracted code, with port/game on the
// include path. Never compiled into the game. ce_hooks_tests.py fails when a hook point exists without its stub here.
#ifndef PORT_CE_HOOKS_HOST_STUBS_H
#define PORT_CE_HOOKS_HOST_STUBS_H
#include "ce_hooks.h"

void			ce_hook_boot() {}
void			ce_hook_exit(CeHookExitSite) {}
void			ce_hook_level_begin(int) {}
void			ce_hook_level_end(int, int) {}
void			ce_hook_tick_pre() {}
void			ce_hook_tick_post() {}
bool			ce_hook_command_push(const CeHookCommand &) { return false; }
void			ce_hook_input_actions(CController *, int) {}
bool			ce_hook_key_capture(unsigned char, bool) { return false; }
void			ce_hook_draw_world(CPlayer *, int) {}
void			ce_hook_draw_hud(CPlayer *, int) {}
void			ce_hook_draw_overlay() {}
void			ce_hook_menu_rows(CeMenuEvent, CeMenuPage *) {}
void			ce_hook_save_gate(CeHookSaveTarget, CCareer *, char *, int) {}
void			ce_hook_audio_clock(CeHookAudioEvent, unsigned) {}
CCamera			*ce_hook_render_camera(int, CCamera *camera, CPlayer *) { return camera; }
CBattleEngine	*ce_hook_hud_focus(CPlayer *, CBattleEngine *be) { return be; }
void			ce_hook_unit_spawn(CUnit *, CInitThing *) {}
void			ce_hook_unit_destroyed(CUnit *) {}
void			ce_hook_spawner_trigger(CeSpawnerTrigger *) {}
void			ce_hook_weapon_fire(CWeapon *, CUnit *, CRound *) {}
void			ce_hook_render_pass(const CeRenderPass *) {}
bool			ce_hook_world_source(CeWorldSourceReq *) { return false; }
void			ce_hook_projectile_intercept(CeProjectileIntercept *) {}
void			ce_hook_unit_damage(const CeUnitDamage *) {}
uint32_t		CeHooksStepGameplayTick(void) { return 0u; }
void			ce_hook_player_act(uint32_t) {}

#endif
