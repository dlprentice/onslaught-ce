<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Onslaught CE Mod Author Quickstart

This guide covers the complete mod-authoring workflow for **Onslaught CE**: packaging asset and world mods in the schema-1 folder format, discovering valid target paths with the Onslaught Modding & Content Toolkit, validating mods with `bea --check-mods`, enabling them in-game, and building native C++ engine extensions on `port/game/ce_hooks.h` v2.0.

> **Read-only game folder rule:** Never modify your installed retail game files or commit retail assets into Git. Mods live strictly in your CE user folder (`<user-dir>/mods/<mod-id>/`) and overlay files at runtime without touching the original installation.

## 1. Schema-1 Mod Folder Format

Onslaught CE's mod loader (`port/platform/common/mods.cpp`; full specification in [`port/MODS.md`](../port/MODS.md)) loads mods from `<user-dir>/mods/<mod-id>/`:

```text
<user-dir>/mods/ce-example-starter/
  mod.json
  data/
    resources/
      dxtntextures/
        meshtex%BE_texA.tga(0)A1R5G5B5.aya
```

`mod.json` is strict printable-ASCII JSON (`schema: 1`, max 256 KiB):

```json
{
  "schema": 1,
  "id": "ce-example-starter",
  "name": "CE Example Starter",
  "version": "1.0.0",
  "author": "Community Example",
  "description": "Example starter mod verified with Onslaught CE.",
  "files": [
    {
      "path": "data/resources/dxtntextures/meshtex%BE_texA.tga(0)A1R5G5B5.aya",
      "size": 123456,
      "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    }
  ]
}
```

Key loader rules enforced at start-up:

- **`id` / folder name:** 1–48 lowercase ASCII letters, digits, `-`, or `_` (must match the folder name and cannot be a reserved Windows device name such as `con`, `nul`, `com1`, `lpt1`).
- **Metadata limits:** `name` 1–64 chars, `version` 1–32 chars, optional `author` 0–64 chars, optional `description` 0–1024 chars, optional `ce_min` / `ce_max` dotted version bounds.
- **File paths:** game-relative `data/...` paths (max 240 chars, max 12 components, forward slashes, no `..`, no symlinks or hardlinks).
- **Admitted content:**
  - Replacements for existing files in your game folder (max 64 MiB; `.aya` files must pass 1 MiB zlib-block unpacking; texture `.aya` files must unpack under 5 MiB with a valid DDS header).
  - Added or replaced loose world bodies at `data/worlds/worldNNN.wrd` (max 4 MiB).
  - Added multiplayer level archives in the reserved custom slot range (`data/resources/850_res_PC.aya` through `899_res_PC.aya`).
- **Integrity:** every listed file must exist as a regular file whose exact byte `size` and lowercase `sha256` match `mod.json`. Any 1-byte mismatch causes the entire mod to be refused cleanly.

## 2. Discovering Targets & Scaffolding a Mod

### Using the Onslaught Modding & Content Toolkit (`Onslaught-Career-Editor`)

The companion [Onslaught Modding & Content Toolkit](https://github.com/dlprentice/Onslaught-Career-Editor) surveys your local PC installation into a JSON Content Catalogue and scaffolds validated schema-1 starter mods:

```bash
# 1. Build a local JSON Content Catalogue from your own game install (read-only)
python3 -B tools/content_catalog.py --game /path/to/game --out local-data/content-catalog

# 2. Discover valid replaceable data/... paths and level IDs
python3 -B tools/mod_starter.py --catalog local-data/content-catalog --list-targets --type texture --filter be_texa
python3 -B tools/mod_starter.py --catalog local-data/content-catalog --list-targets --type world

# 3. Scaffold a schema-1 starter mod and validate it directly with Onslaught CE
python3 -B tools/mod_starter.py \
  --out /path/to/ce-user-dir/mods \
  --id ce-example-starter \
  --name "CE Example Starter" \
  --game-dir /path/to/game \
  --target data/language/english.dat \
  --validate-with /path/to/bea
```

### Using Onslaught CE's Built-in Mod Helper (`port/tools/mods.py`)

This repository also ships `port/tools/mods.py` for `.aya` packing/unpacking, manifest generation, and Battle Engine texture starters:

```bash
# Scaffold a sample Battle Engine recolor mod from your own install
python3 -B port/tools/mods.py new ce-example-starter \
  --name "CE Example Starter" \
  --user /path/to/ce-user-dir \
  --game /path/to/game

# Unpack/pack .aya texture files and regenerate mod.json after edits
python3 -B port/tools/mods.py unpack IN.aya OUT.dds
python3 -B port/tools/mods.py pack EDITED.dds OUT.aya
python3 -B port/tools/mods.py manifest /path/to/ce-user-dir/mods/ce-example-starter \
  --name "CE Example Starter" --version 1.0.0
```

## 3. Validating with `bea --check-mods`

Run the game binary's built-in mod validator before launching:

```bash
BEA_USER_DIR=/path/to/ce-user-dir BEA_GAME_DIR=/path/to/game ./bea --check-mods ce-example-starter
```

A valid mod exits with code `0` and prints:

```text
result: 1 passed, 0 refused
```

A corrupted file, missing entry, invalid path, or mismatched SHA-256 exits with code `1` and reports the exact refusal reason (`refused ce-example-starter: ...`).

## 4. Enabling Mods In-Game (`Mods` Menu & `ce.ini`)

You can enable mods either in the in-game UI or directly in `<user-dir>/ce.ini`:

1. **In-game Mods page:** Add `mods_menu=1` to `<user-dir>/ce.ini`, start Onslaught CE, and open **Onslaught CE > Mods**. Toggle any detected mod **On** or **Off**, adjust load order (later mods override earlier mods on shared paths), and inspect Details. Changes are written to `ce.ini` and take effect on the next start.
2. **Direct `ce.ini` configuration:**
   ```ini
   mods_menu=1
   mods=ce-example-starter
   ```
   Separate multiple mod IDs with commas (`mods=first-mod,second-mod`).

> **Faithful-mode fence:** Onslaught CE automatically disables mods during deterministic replays, session recordings, probes, virtual-clock runs, and network sessions so faithful replay parity is never perturbed.

## 5. Writing C++ Engine Extensions on `ce_hooks.h` v2.0

When you need new engine behavior beyond file/world replacement — such as custom HUD overlays, camera overrides, unit spawn/destruction tracking, weapon-fire reactions, spawner wave filters, or custom render passes — register handlers on Onslaught CE's 21-point static hook table instead of scattering patches across `src/`.

See [`docs/ce-hooks-api.md`](ce-hooks-api.md) for the complete 21-point reference, built-in handler table, and payload structures.

### Minimal Handler Checklist

1. **Choose the right point & write-fence tag:**
   - `CE_HOOK_SIM` points (`level_begin`, `level_end`, `tick_pre`, `tick_post`, `input_actions`, `save_gate`, `render_camera`, `unit_spawn`, `unit_destroyed`, `spawner_trigger`, `weapon_fire`) run on the deterministic simulation cadence and may mutate game state.
   - `CE_HOOK_RENDER` points (`key_capture`, `draw_world`, `draw_hud`, `draw_overlay`, `menu_rows`, `audio_clock`, `hud_focus`, `render_pass`) run at presentation boundaries and **must never mutate simulation state** (enforced at compile time by `CE_HOOK_CHECK` and at runtime in `CE_TEST_BUILD` via `port/game/ce_hooks_check.cpp`).
2. **Gate gameplay/mod behavior with `CE_GATE_LAYER`:**
   - Use `CE_GATE_LAYER` on every gameplay or presentation extension so it stays completely inert during faithful replays, recordings, and perturbation sweeps.
3. **Wire one static row in `port/game/ce_hooks.cpp`:**
   ```cpp
   // Example in port/game/ce_hooks.cpp (inside kUnitSpawn):
   {20, "my_spawn_tracker", CE_HOOK_SIM, CE_GATE_LAYER, CE_MODE_NONE, CeHookMySpawnTracker},
   ```
   Use `__attribute__((weak))` with a null check only when standalone host test drivers compile `ce_hooks.cpp` without linking your new `.cpp` file, and verify strong Windows linkage with `port/tools/windows_weak_link_tests.py`.
4. **Verify your extension:**
   ```bash
   python3 -B port/tools/ce_hooks_tests.py -v
   python3 -B port/tools/patch_shape_tests.py
   python3 -B port/tools/patch_control_body_tests.py
   python3 -B port/tools/ce_mod_starter_e2e_tests.py -v
   ```
