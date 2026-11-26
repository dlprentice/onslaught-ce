<!-- SPDX-License-Identifier: GPL-3.0-only -->
# CE Engine Hook API Reference

This document is the contributor reference for the Community Edition (CE) **compiled C++ engine hook layer** (`port/game/ce_hooks.h`, `port/game/ce_hooks.cpp`, `port/game/ce_hooks_dispatch.h`, `port/game/ce_hooks_check.cpp`).

> **Scope — compiled contributors vs. runtime mods:** `ce_hooks` is a **compile-time C++ extension surface** for contributors building Onslaught CE from source. Runtime schema-1 mods loaded from the player's `mods/` folder are **data-only** (asset, level, and text overrides); they cannot load native code or register hook handlers.

## 1. Versioning & Compatibility Contract

`port/game/ce_hooks.h` exposes explicit compile-time version macros:

```cpp
#define CE_HOOKS_API_VERSION_MAJOR 3u
#define CE_HOOKS_API_VERSION_MINOR 0u
#define CE_HOOKS_API_VERSION       ((CE_HOOKS_API_VERSION_MAJOR << 8) | CE_HOOKS_API_VERSION_MINOR)
#define CE_HOOKS_POINT_COUNT       25u
```

- **Major version (`CE_HOOKS_API_VERSION_MAJOR`)**: increments on breaking signature, struct layout, enum value, or dispatch-contract changes.
- **Minor version (`CE_HOOKS_API_VERSION_MINOR`)**: increments on backwards-compatible additions (new hook points or appended enum values).
- **Compile-time verification**: `port/game/ce_hooks.cpp` asserts `CE_HOOKS_API_VERSION` and `CE_HOOKS_POINT_COUNT`, and `port/tools/ce_hooks_tests.py` verifies exact parity across the header, table, host stubs, weak test stubs, and patch sites.
- **Zero-overhead private builds**: when `CE_VERSION` is not defined, every `ce_hook_*` compiles to an empty/passthrough function (`ce_hook_key_capture` returns `false`, `ce_hook_command_push` returns `false`, `ce_hook_render_camera` returns `camera`, `ce_hook_hud_focus` returns `be`, `ce_hook_spawner_trigger` leaves `trigger->allow == true`).

## 2. Architecture & Invariants

Every engine touchpoint goes through **one numbered patch per hook point** (`port/patches/03xx-ce-hook-<name>.patch`), calling a single `ce_hook_<name>(...)` entrypoint. Handler wiring lives in **one compiled static table** in `port/game/ce_hooks.cpp` — no runtime registration, no static initializer ordering bugs, and no link-order surprises.

### 2.1 `SIM` vs `RENDER` Write-Fence

Every hook point and every handler row carries a tag (`CE_HOOK_SIM` or `CE_HOOK_RENDER`):

- **`CE_HOOK_SIM`**: runs on the deterministic simulation or lifecycle cadence (identical across replays and lockstep peers). Handlers may read and write game state.
- **`CE_HOOK_RENDER`**: runs at frame rate or presentation boundaries. Handlers may read game state and draw or answer UI input, and **must never mutate simulation state**.

Enforcement is two-layered:
1. **Compile time**: `CE_HOOK_CHECK` statically rejects any row whose `SIM`/`RENDER` tag disagrees with its point's tag, any duplicate or non-ascending slot number, or any missing `CE_HOOK_END` sentinel.
2. **Test-build runtime guard (`CE_TEST_BUILD`)**: `port/game/ce_hooks_check.cpp` hashes a non-mutating simulation digest (`EVENT_MANAGER.GetTime()`, CRT RNG state/call count, `WORLD.GetThingNB().Size()`, `WORLD.GetUnitNB().Size()`, and every player's Battle Engine position) immediately before and after every `CE_HOOK_RENDER` handler, exiting with status `70` on any drift. Public non-test builds compile the guard away completely.

> **Worker-thread exception (`audio_clock`)**: `audio_clock` may execute on the music streaming worker thread. `static_assert(CeHookWorkerSafe(kAudioClock))` enforces `CE_GATE_VERSION` and `CE_MODE_NONE` only, and skips the simulation-digest guard.

### 2.2 Gates and Mode Scoping

Each table row declares:
- **Slot (`int`)**: strictly ascending within a point (leave gaps like `10, 20, 30` so future handlers can insert cleanly without renumbering).
- **Gate (`CeHookGate`)**:
  - `CE_GATE_VERSION`: active in every `CE_VERSION` build.
  - `CE_GATE_LAYER`: active only while `PortCeLayer()` is true — automatically disabled during faithful replays, recordings, probes, and determinism sweeps (`BEA_PORT_CLOCK`, `BEA_PORT_TAPE`, `BEA_PORT_PROBES`, `BEA_PORT_RECORD`). **Use `CE_GATE_LAYER` for all gameplay/feature/mod hooks that must never perturb faithful replay parity.**
- **Mode (`CeMode`, `port/game/ce_modes.h`)**: `CE_MODE_NONE` (always open) or a specific exclusive mode (`CE_MODE_EDITOR`, `CE_MODE_MUSEUM_VISIT`, `CE_MODE_BONUS_LEVEL`, `CE_MODE_COMMANDER`, `CE_MODE_PLAY_UNIT`, `CE_MODE_PROJECT`, `CE_MODE_CARRIER_CLASH`, `CE_MODE_LIVING_SANDBOX`, `CE_MODE_FLAGSHIP_DUEL`, `CE_MODE_ONLINE_MATCH`).

### 2.3 Dispatch Modes (`port/game/ce_hooks_dispatch.h`)

| Dispatch | Behavior |
|---|---|
| `ORDERED_BROADCAST` | Runs every open row in ascending slot order. |
| `FIRST_MATCH` | Runs open rows in order and stops at the first handler returning `true`. |
| `POSITIONAL_INSERT` | Runs open rows in order; each row supplies a target index resolved via `CeHookPlace(count, position)` (`-1` or past end appends). |
| `FILTER` | Runs open rows in order over shared mutable state (`char *dest` or `CeSpawnerTrigger *`); each handler sees earlier handlers' modifications. |
| `COMMAND_APPLY` | Drains `CeHookCommandQueue` (capacity 64, queued via `ce_hook_command_push`, `kind != 0`), oldest first, broadcasting each command to open rows and clearing the queue. |
| `OVERRIDE_CHAIN` | Runs every open row in **ascending slot order**, passing each returned value into the next handler (`value = fn(value, ...)`). Returning the input argument is a clean passthrough; when multiple handlers override the value, **last-writer-wins** applies unless the later handler explicitly yields on prior overrides (for example, on `render_camera`, slot `30` `multibe_camera` runs after slot `20` `playunit_camera` and slot `10` `commander_camera` and passes through whenever an earlier camera override, Commander, Play-Any-Unit, or FreeCam owns the view). |

## 3. Complete Hook Point Reference (25 Points)

| # | Point | Tag | Dispatch | Patch | Engine Site(s) | Signature |
|---|---|---|---|---|---|---|
| 1 | `boot` | `SIM` | `ORDERED_BROADCAST` | `0301` | `ltshell.cpp` `WinMain` (after `MEM_MANAGER.Init`, before options read) | `void ce_hook_boot()` |
| 2 | `exit` | `SIM` | `ORDERED_BROADCAST` | `0302` | `ltshell.cpp` `PCLTShell::MainLoop` (`CE_EXIT_SHELL`) & `ExitCode` (`CE_EXIT_ATEXIT`) | `void ce_hook_exit(CeHookExitSite site)` |
| 3 | `level_begin` | `SIM` | `ORDERED_BROADCAST` | `0303` | `game.cpp` `CGame::RestartLoopRunLevel` (after demo wait, before pre-run ticks) | `void ce_hook_level_begin(int level)` |
| 4 | `level_end` | `SIM` | `ORDERED_BROADCAST` | `0304` | `game.cpp` `CGame::ShutdownRestartLoop` (before resources are freed; may run twice on load failure) | `void ce_hook_level_end(int level, int quit)` |
| 5 | `tick_pre` | `SIM` | `ORDERED_BROADCAST` | `0305` | `game.cpp` `CGame::Update` (after early returns; includes paused & pre-run ticks) | `void ce_hook_tick_pre()` |
| 6 | `tick_post` | `SIM` | `COMMAND_APPLY` | `0306` | `game.cpp` `CGame::Update` (end of update; drains `ce_hook_command_push` queue) | `void ce_hook_tick_post()` |
| 7 | `input_actions` | `SIM` | `ORDERED_BROADCAST` | `0307` | `Controller.cpp` `CController::DoMappings` (after bindings, before recording) | `void ce_hook_input_actions(CController *controller, int pad)` |
| 8 | `key_capture` | `RENDER` | `FIRST_MATCH` | `0308` | `ltshell.cpp` `PCLTShell::MsgProc` (`WM_KEYDOWN`, before keytrap/binds) | `bool ce_hook_key_capture(unsigned char key, bool repeat)` |
| 9 | `draw_world` | `RENDER` | `ORDERED_BROADCAST` | `0309` | `DXEngine.cpp` `draw_fairly_late_stuff` (after world matrices saved, before cockpit/screen FX) | `void ce_hook_draw_world(CPlayer *player, int viewpoint)` |
| 10 | `draw_hud` | `RENDER` | `ORDERED_BROADCAST` | `0310` | `Hud.cpp` `CHud::RenderOverlayForViewpoint` (end, over retail HUD) | `void ce_hook_draw_hud(CPlayer *player, int viewpoint)` |
| 11 | `draw_overlay` | `RENDER` | `ORDERED_BROADCAST` | `0311` | `DXEngine.cpp` `CDXEngine::PostRender` (after CE overlays, before `EndScene`) | `void ce_hook_draw_overlay()` |
| 12 | `menu_rows` | `RENDER` | `POSITIONAL_INSERT` | `0312` | `FrontEnd.cpp` `CFrontEnd::Init`, `PauseMenu.cpp` `CPauseMenu::CPauseMenu` (x2) | `void ce_hook_menu_rows(CeMenuEvent event, CeMenuPage *page)` |
| 13 | `save_gate` | `SIM` | `FILTER` | `0313` | `Career.cpp` `CCareer::Save`, `FEPOptions.cpp` `CFEPOptions::WriteDefaultOptionsFile` | `void ce_hook_save_gate(CeHookSaveTarget target, CCareer *career, char *dest, int size)` |
| 14 | `audio_clock` | `RENDER` | `ORDERED_BROADCAST` | `0314` | `PCMusicStream.cpp` `MusicStreamHandle` & `GetMusicStreamFinished` | `void ce_hook_audio_clock(CeHookAudioEvent event, unsigned bytes)` |
| 15 | `render_camera` | `SIM` | `OVERRIDE_CHAIN` | `0315` | `engine.cpp` `CEngine::SetViewpoint` (before viewport copy) | `CCamera *ce_hook_render_camera(int viewpoint, CCamera *camera, CPlayer *player)` |
| 16 | `hud_focus` | `RENDER` | `OVERRIDE_CHAIN` | `0360` | `Hud.cpp` `CHud::RenderOverlayForViewpoint` (after `player->GetBattleEngine()`, before `mBattleEngine = be`) | `CBattleEngine *ce_hook_hud_focus(CPlayer *player, CBattleEngine *be)` |
| 17 | `unit_spawn` | `SIM` | `ORDERED_BROADCAST` | `0361` | `Unit.cpp` `CUnit::Init` (end, after `WORLD.mSet20`/`mSet30` insertion & `UNIT_EVENT_4003`) | `void ce_hook_unit_spawn(CUnit *unit, CInitThing *init)` |
| 18 | `unit_destroyed` | `SIM` | `ORDERED_BROADCAST` | `0362` | `Unit.cpp` `CUnit::StartDieProcess` & `BattleEngine.cpp` `CBattleEngine::StartDieProcess` (before `return TRUE`) | `void ce_hook_unit_destroyed(CUnit *unit)` |
| 19 | `spawner_trigger` | `SIM` | `FILTER` | `0363` | `SpawnerThng.cpp` `CSpawnerThing::Unknown4e3370` & `CSpawner::DoSpawn` (before spawn) | `void ce_hook_spawner_trigger(CeSpawnerTrigger *trigger)` |
| 20 | `weapon_fire` | `SIM` | `ORDERED_BROADCAST` | `0364` | `weapon.cpp` `CWeapon::SpawnRound` (immediately after `round->Init(&init)`) | `void ce_hook_weapon_fire(CWeapon *weapon, CUnit *owner, CRound *round)` |
| 21 | `render_pass` | `RENDER` | `ORDERED_BROADCAST` | `0365` | `DXEngine.cpp` `CDXEngine::Render` (`CE_RENDER_PASS_PRE_WORLD` after `SetViewport`; `CE_RENDER_PASS_POST_WORLD` after `draw_fairly_late_stuff`) | `void ce_hook_render_pass(const CeRenderPass *pass)` |
| 22 | `world_source` | `SIM` | `FIRST_MATCH` | `0393` | `ResourceAccumulator.cpp` `CResourceAccumulator::ReadResources` & `world.cpp` `CWorld::Load` | `bool ce_hook_world_source(CeWorldSourceReq *req)` |
| 23 | `projectile_intercept` | `SIM` | `FILTER` | `0394` | `Round.cpp` `CRound::Move` (x2), `CRound::HandleEvent` (x2), `CRound::DeclareOnGround` | `void ce_hook_projectile_intercept(CeProjectileIntercept *ev)` |
| 24 | `unit_damage` | `SIM` | `ORDERED_BROADCAST` | `0395` | `Unit.cpp` `CUnit::Damage` (before `mSegments`) & `BattleEngine.cpp` `CBattleEngine::Damage` (pre-shield `rawAmount`) | `void ce_hook_unit_damage(const CeUnitDamage *ev)` |
| 25 | `player_act` | `SIM` | `ORDERED_BROADCAST` | `0396` | `game.cpp` `CGame::Update` (unpaused `GAME_STATE_PLAYING`, after human controller flush, before `EVENT_MANAGER.Flush`) | `void ce_hook_player_act(uint32_t sim_tick)` |

### 3.1 Registered Built-in Handlers (25 Hook Points)

The compiled dispatch table in `port/game/ce_hooks.cpp` wires the following built-in CE handlers (55 rows across 18 active hook points; 7 points are currently open for extension):

| Point | Slot | Handler Name | Tag | Gate | Mode | Owner Module |
|---|---|---|---|---|---|---|
| `boot` | — | *(open — no built-in rows)* | `SIM` | — | — | — |
| `exit` | `10` | `ai_strategist_shutdown` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ai/ce_ai_strategist_l3.cpp` |
| `level_begin` | `10` | `multibe_level_begin` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_multibe.cpp` |
| `level_begin` | `20` | `carrier_clash_level_begin` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_carrier_clash.cpp` |
| `level_begin` | `30` | `living_sandbox_level_begin` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_sandbox_director.cpp` |
| `level_begin` | `40` | `flagship_duel_level_begin` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_flagship_duel.cpp` |
| `level_end` | `10` | `palette_close` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_palette.cpp` |
| `level_end` | `20` | `console_level_end` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_palette.cpp` |
| `level_end` | `30` | `photo_level_end` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_photo.cpp` |
| `level_end` | `40` | `commander_level_end` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_cmdmap.cpp` |
| `level_end` | `50` | `multibe_level_end` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_multibe.cpp` |
| `level_end` | `70` | `carrier_clash_level_end` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_carrier_clash.cpp` |
| `level_end` | `80` | `living_sandbox_level_end` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_sandbox_director.cpp` |
| `level_end` | `85` | `flagship_duel_level_end` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_flagship_duel.cpp` |
| `level_end` | `90` | `muspell_cockpit_level_end` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_muspell_cockpit.cpp` |
| `level_end` | `92` | `player_hooks_level_end` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_player_hooks.cpp` |
| `level_end` | `95` | `entity_handles_level_end` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_entity_handles.cpp` |
| `tick_pre` | `10` | `gamepad_cycle` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_gamepad.cpp` |
| `tick_pre` | `20` | `multibe_tick_pre` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_multibe.cpp` |
| `tick_pre` | `30` | `carrier_clash_tick_pre` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_carrier_clash.cpp` |
| `tick_pre` | `40` | `living_sandbox_tick_pre` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_sandbox_director.cpp` |
| `tick_pre` | `50` | `flagship_duel_tick_pre` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_flagship_duel.cpp` |
| `tick_post` | — | *(open — drains `ce_hook_command_push` queue)* | `SIM` | — | — | — |
| `input_actions` | — | *(open — no built-in rows)* | `SIM` | — | — | — |
| `key_capture` | `10` | `photo_key` | `RENDER` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_photo.cpp` |
| `key_capture` | `20` | `palette_suppress_down` | `RENDER` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_palette.cpp` |
| `key_capture` | `30` | `palette_open` | `RENDER` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_palette.cpp` |
| `key_capture` | `40` | `commander_key` | `RENDER` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_cmdmap.cpp` |
| `key_capture` | `50` | `living_sandbox_key` | `RENDER` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_sandbox_director.cpp` |
| `draw_world` | `10` | `playunit_draw_world` | `RENDER` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_playunit.cpp` |
| `draw_world` | `20` | `living_sandbox_draw_world` | `RENDER` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_sandbox_director.cpp` |
| `draw_hud` | `10` | `multibe_draw_hud` | `RENDER` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_multibe.cpp` |
| `draw_hud` | `20` | `carrier_clash_draw_hud` | `RENDER` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_carrier_clash.cpp` |
| `draw_hud` | `30` | `living_sandbox_draw_hud` | `RENDER` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_sandbox_director.cpp` |
| `draw_hud` | `40` | `flagship_duel_draw_hud` | `RENDER` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_flagship_duel.cpp` |
| `draw_overlay` | `10` | `editor_overlay` | `RENDER` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_editor.cpp` |
| `draw_overlay` | `20` | `carrier_clash_draw_overlay` | `RENDER` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_carrier_clash.cpp` |
| `draw_overlay` | `30` | `flagship_duel_draw_overlay` | `RENDER` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_flagship_duel.cpp` |
| `menu_rows` | `10` | `multibe_menu_rows` | `RENDER` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_multibe.cpp` |
| `save_gate` | `10` | `career_snapshot_save` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_modes.cpp` / `port/game/ce_hooks.cpp` |
| `audio_clock` | — | *(open — worker-thread-safe rows only)* | `RENDER` | — | — | — |
| `render_camera` | `10` | `commander_camera` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_cmdmap.cpp` |
| `render_camera` | `20` | `playunit_camera` | `SIM` | `CE_GATE_VERSION` | `CE_MODE_NONE` | `port/game/ce_playunit.cpp` |
| `render_camera` | `30` | `multibe_camera` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_multibe.cpp` |
| `hud_focus` | `10` | `multibe_hud_focus` | `RENDER` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_multibe.cpp` |
| `unit_spawn` | `5` | `entity_handles_unit_spawn` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_entity_handles.cpp` |
| `unit_spawn` | `10` | `multibe_unit_spawn` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_multibe.cpp` |
| `unit_spawn` | `30` | `carrier_clash_unit_spawn` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_carrier_clash.cpp` |
| `unit_spawn` | `40` | `living_sandbox_unit_spawn` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_sandbox_director.cpp` |
| `unit_spawn` | `50` | `flagship_duel_unit_spawn` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_flagship_duel.cpp` |
| `unit_destroyed` | `10` | `multibe_unit_destroyed` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_multibe.cpp` |
| `unit_destroyed` | `30` | `carrier_clash_unit_destroyed` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_carrier_clash.cpp` |
| `unit_destroyed` | `40` | `living_sandbox_unit_destroyed` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_sandbox_director.cpp` |
| `unit_destroyed` | `50` | `flagship_duel_unit_destroyed` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_flagship_duel.cpp` |
| `unit_destroyed` | `90` | `entity_handles_unit_destroyed` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_entity_handles.cpp` |
| `spawner_trigger` | — | *(open — no built-in rows)* | `SIM` | — | — | — |
| `weapon_fire` | `10` | `multibe_weapon_fire` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_multibe.cpp` |
| `render_pass` | — | *(open — no built-in rows)* | `RENDER` | — | — | — |
| `world_source` | `10` | `world_provider_source` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_world_provider.cpp` |
| `projectile_intercept` | `10` | `sanctuary_projectile_intercept` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_sanctuary_citadel.cpp` |
| `unit_damage` | — | *(open — no built-in rows)* | `SIM` | — | — | — |
| `player_act` | `10` | `player_hooks_act` | `SIM` | `CE_GATE_LAYER` | `CE_MODE_NONE` | `port/game/ce_player_hooks.cpp` |

> **Why certain legacy CE call sites intentionally remain direct:** Some older CE seams sit at distinct render-stage, write-fence, or control-flow boundaries that do not match the 25 hook sites (for example: `PortCeEditorKey` in `ltshell.cpp` plays UI cues via `CFrontEnd::PlaySound` (`rand()`) and applies editor mutations such as `actor->MoveTo` / `GAME.SetQuit`, which intentionally mutate state and therefore cannot sit behind `key_capture`'s `CE_HOOK_RENDER` simulation-digest guard in `CE_TEST_BUILD`; `PortCeMuseumHallDraw` in `CDXEngine::Render` draws opaque hall geometry before `mWater->Render()` and `WORLD.Render()` rather than in `draw_world`; `PortCeMuseumViewCapture` / `PortCeWorldViewCapture` capture projection matrices after `RENDERINFO.SetProjection` rather than at `CE_RENDER_PASS_PRE_WORLD`; `PortCeMuseumLabelsRender` draws inside `CHud::RenderBattleline` while `0366` suppresses combat overlays before `draw_hud`; `CePlayUnitHudOverlay` executes in the `GetShowHUD() == FALSE` branch of `CHud::Render`; and `PortCeEditorLevelEnd` / `PortCeMuseumVisitLevelEnd` tear down mode state before `CE_SessionBeginTeardown`). Those sites are preserved directly to guarantee exact write-fence, Z-ordering, matrix restoration, and mode invariants.

### 3.2 New Data Payloads (v2.0)

#### `CeSpawnerTrigger` (`spawner_trigger`)
```cpp
enum CeSpawnerKind { CE_SPAWNER_MONITOR = 0, CE_SPAWNER_ACTOR = 1 };
struct CeSpawnerTrigger
{
    CeSpawnerKind  kind;
    CSpawner      *spawner;     // non-NULL for CE_SPAWNER_MONITOR (factory/unit spawner)
    CSpawnerThing *actor;       // non-NULL for CE_SPAWNER_ACTOR (map-placed spawner)
    CUnit         *owner;       // factory/carrier unit for CE_SPAWNER_MONITOR, else NULL
    const char    *unit_name;   // unit class name about to spawn
    int            squad_size;  // squad wave size
    int            allegiance;  // EAllegiance value
    bool           allow;       // initialized true; clear to veto cleanly
};
```
- When a handler sets `trigger->allow = false`:
  - `CSpawner::DoSpawn` returns `FALSE` without creating a squad or consuming wave count; the monitor retries on its normal schedule.
  - `CSpawnerThing::Unknown4e3370` skips `SpawnThingByName` while still rescheduling event `3000` (`+0.1s`), so lifting a veto later cleanly resumes spawning.

#### `CeRenderPass` (`render_pass`)
```cpp
enum CeRenderPassStage { CE_RENDER_PASS_PRE_WORLD = 0, CE_RENDER_PASS_POST_WORLD = 1 };
struct CeRenderPass
{
    CeRenderPassStage stage;
    int               viewpoint;
    CPlayer          *player;
    CCamera          *camera;
    bool              mirror_pass;
};
```
- Fires twice per `CDXEngine::Render(viewpoint)` call (including mirror reflection passes, distinguishable via `pass->mirror_pass`, and any secondary render invocation such as Commander live view's `PortCeCmdRenderWorld`, which runs `Render(0)` an extra time).
- `CE_RENDER_PASS_PRE_WORLD` runs immediately after `PLATFORM.SetViewport(&mViewport[mCurrentViewpoint])`, before lights, sky, landscape, and things.
- `CE_RENDER_PASS_POST_WORLD` runs immediately after `draw_fairly_late_stuff` (and therefore after `SCREENFX.Render()`, which alters D3D9 matrices/states), before `PLATFORM.EndScene()`. Handlers drawing 3D world geometry in `POST_WORLD` must restore world/view/projection matrices from `ENGINE.mWorldMatrices[pass->viewpoint]`, `ENGINE.mViewMatrices[pass->viewpoint]`, and `ENGINE.mProjectionMatrices[pass->viewpoint]` (or use `draw_world` instead, which runs right after those matrices are backed up and before cockpit/screen FX).

#### `hud_focus` Details
- Wired immediately after `CBattleEngine *be = player->GetBattleEngine();` inside `CHud::RenderOverlayForViewpoint`.
- Returning `be` unchanged preserves exact retail HUD rendering. Returning a different non-dying `CBattleEngine *` redirects the entire HUD pass (`mBattleEngine`, weapons, gauges, radar, target markers) to display that Battle Engine's state without modifying `CPlayer::mBattleEngine`.
- Returning `NULL` cleanly suppresses the HUD pass for that viewpoint (`if ((!be) || be->IsDying()) return;`).
- **Preconditions & split-screen caveat:**
  - Any non-`NULL` `CBattleEngine *` returned by a handler **must** have a non-`NULL` `be->GetPlayer()`, because retail HUD helpers dereference `mBattleEngine->GetPlayer()`. As a defensive backstop, `ce_hook_hud_focus` checks `focused->GetPlayer()` in game builds and falls back cleanly to the incoming `be` (or `NULL` if `be` itself has no player) whenever a handler returns a headless engine.
  - In split-screen (`GAME.GetNPlayers() > 1`), retail `CHud` and `CCompass` anchor widgets and buffers off `mBattleEngine->GetPlayer()->GetNumber() == 1`. Redirecting a split-screen viewpoint to a Battle Engine whose player number differs from `player->GetNumber()` shifts compass/gauge coordinates into the opposite split half; split-screen handlers should only redirect to Battle Engines with a matching `GetPlayer()->GetNumber()`.
  - Because `hud_focus` is tagged `CE_HOOK_RENDER`, handlers must remain strictly read-only — in particular, never iterate `WORLD.GetUnitNB()` via `.First()/.Next()` (which mutates its shared intrusive cursor `mIterator`); always use stack-local `ListIterator<CUnit>`.

## 4. `save_gate` Composition & Mode-Exclusivity Contract

`save_gate` uses `CE_DISPATCH_FILTER`, running open handlers in table order over the same serialized buffer `dest`:

- **Incremental mutations** (modifying individual fields in `dest`) compose freely across multiple active handlers.
- **Whole-copy snapshot replacements** (`memcpy(dest, &snapshot, sizeof(CCareer))`) overwrite prior handlers' changes; only one whole-copy owner may be active during a save.
- **Single whole-copy owner (`kSaveGate` slot `10`, `career_snapshot_save`)**: `port/game/ce_modes.cpp` holds the single `CeCareerSnapshotOwner` slot (`CeCareerSnapshotAcquire` / `Read` / `Release`). Every career-fenced world-owner mode (`CE_MODE_EDITOR`, `CE_MODE_MUSEUM_VISIT`, `CE_MODE_BONUS_LEVEL`, `CE_MODE_CARRIER_CLASH`, `CE_MODE_LIVING_SANDBOX`, `CE_MODE_FLAGSHIP_DUEL`, `CE_MODE_ONLINE_MATCH`) is strictly pairwise incompatible in `CeModesCompatible`, acquires its snapshot only while active, and releases it automatically in `CeModeEnd`. Slot `10` validates `target == CE_SAVE_CAREER`, non-NULL `career`/`dest`, and `size >= (int)sizeof(CCareer)`, then dispatches exclusively to the single active owner (`SaveHazardTests` in `port/tools/ce_hooks_tests.py` verifies single-owner exclusivity).
- Higher `kSaveGate` slots (`>= 20`) are strictly for incremental field filters; never register a second whole-copy replacement handler.

## 5. Practical Examples

### 5.1 Gating Factory Waves (`spawner_trigger`)
```cpp
static void CapEnemySquads(CeSpawnerTrigger *trigger)
{
    if (!trigger->allow)
        return; // already vetoed by an earlier handler
    if (trigger->allegiance == kMuspellAllegiance && WORLD.GetUnitNB().Size() >= 250)
        trigger->allow = false;
}
```
Table row (in `kSpawnerTrigger`, `port/game/ce_hooks.cpp`):
```cpp
{10, "cap_enemy_squads", CE_HOOK_SIM, CE_GATE_LAYER, CE_MODE_NONE, CapEnemySquads},
```

### 5.2 Tracking Spawns, Weapon Fire & Losses (`unit_spawn`, `weapon_fire`, `unit_destroyed`)
```cpp
static void OnUnitSpawn(CUnit *unit, CInitThing *)
{
    if (unit && unit->IsA(THING_TYPE_BATTLE_ENGINE))
        TrackBattleEngine(static_cast<CBattleEngine *>(unit));
}
static void OnWeaponFire(CWeapon *, CUnit *owner, CRound *)
{
    if (owner && owner->IsA(THING_TYPE_BATTLE_ENGINE))
        IncrementShotsFired(owner);
}
static void OnUnitDestroyed(CUnit *unit)
{
    UnregisterTrackedUnit(unit);
}
```

### 5.3 Redirecting HUD Telemetry (`hud_focus`) & Drawing Scene Overlays (`render_pass`)
```cpp
static CBattleEngine *FocusSpectatedBe(CBattleEngine *be, CPlayer *player)
{
    CBattleEngine *spectated = GetSpectatedBattleEngine(player);
    return (spectated && !spectated->IsDying()) ? spectated : be;
}
static void DrawTacticalPass(const CeRenderPass *pass)
{
    if (pass->mirror_pass || pass->stage != CE_RENDER_PASS_POST_WORLD)
        return;
    DrawWorldGridOverlay(pass->viewpoint, pass->camera);
}
```

## 6. Contributor Checklist: Adding a Hook Point or Handler

1. **Adding a handler to an existing point**:
   - Declare the handler in `port/game/ce_hooks.cpp` (use `__attribute__((weak))` with a null check only if standalone host drivers compile `ce_hooks.cpp` without your module, and confirm your strong definition links on Windows via `port/tools/windows_weak_link_tests.py`).
   - Add a row in the point's `k<Point>[]` array with an ascending slot, matching `CE_HOOK_SIM`/`CE_HOOK_RENDER` tag, and `CE_GATE_LAYER` unless the feature must run in faithful replays.
   - Update slot/order expectations in `port/tools/ce_hooks_tests.py`.
2. **Adding a new hook point**:
   - Bump `CE_HOOKS_API_VERSION_MINOR` (or `MAJOR` if breaking) and `CE_HOOKS_POINT_COUNT` in `port/game/ce_hooks.h`.
   - Add the declaration to `port/game/ce_hooks.h`, both CE and private definitions + table + `CE_HOOK_CHECK` to `port/game/ce_hooks.cpp`, inert host stub to `port/game/ce_hooks_host_stubs.h`, and weak test stub to `port/tools/tests/ce_weak_stubs.cpp`.
   - Create one clean patch `port/patches/03XX-ce-hook-<name>.patch` using symmetric unchanged context (`lead <= trail`, zero overlap with non-`03` patches so `IndependenceTests` passes without adding `DEPENDS`).
   - Register the point in `POINTS`, `SITES`, `DISPATCH`, `TABLE_MUTANTS`, and runtime tests in `port/tools/ce_hooks_tests.py`.
   - Run:
     ```bash
     python3 -B port/tools/ce_hooks_tests.py -v
     python3 -B port/tools/patch_shape_tests.py
     python3 -B port/tools/patch_control_body_tests.py
     ```
