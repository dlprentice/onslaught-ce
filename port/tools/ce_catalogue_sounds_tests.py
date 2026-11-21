#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic host checks for session Catalogue sounds and owned playback."""
from __future__ import annotations
import argparse
import json
import hashlib
from pathlib import Path
import re
import subprocess
import struct
import zlib
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def write_synthetic_texture(game: Path) -> None:
    # Same authored two-pixel fixture construction as the asset-catalogue suite.
    def member(data):
                z = zlib.compress(data)
                return struct.pack("<I", len(z)) + z
    def header(w=4, h=4):
                v = bytearray(128); v[:4] = b"DDS "
                struct.pack_into("<7I", v, 4, 124, 0x1007, h, w, 0, 0, 0)
                struct.pack_into("<II4s", v, 76, 32, 4, b"DXT1")
                struct.pack_into("<I", v, 108, 0x1000)
                return bytes(v)
    a, b = bytes(range(8)), bytes(reversed(range(8)))
    payload = member(header()) + member(a)
    target = game / "data/resources/dxtntextures/authored-preview.aya"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(payload)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    parent = ROOT / "local-data" / "catalogue-sounds-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = args.out.resolve() if args.out else Path(tempfile.mkdtemp(prefix="check-", dir=parent))
    out.mkdir(parents=True, exist_ok=True)
    game = out / "synthetic-game"
    game.mkdir(exist_ok=True)
    write_synthetic_texture(game)
    # Two source-bound adverse controls: a sound row must never read a texture,
    # and stopping Catalogue playback must not stop another system's events.
    menu_source = (ROOT / "port/game/ce_catalogue_menu.cpp").read_text()
    start = menu_source.index("bool PortCeCatalogueReadTexture(")
    end = menu_source.index("\n}", start) + 2
    bad_body, changed = re.subn(r"category\s*!=\s*2\s*&&\s*", "", menu_source[start:end], count=1)
    if changed != 1:
        raise RuntimeError("sound-to-texture control no longer targets the current guard")
    menu_control = out / "sound-as-texture.cpp"
    menu_control.write_text(menu_source[:start] + bad_body + menu_source[end:])
    engine_source = (ROOT / "port/game/ce_catalogue_sound_engine.inc").read_text()
    needle = "SOUND.KillSamplesForThing(&sCatalogueSoundOwner);"
    if engine_source.count(needle) != 1:
        raise RuntimeError("foreign-owner control no longer targets the current stop")
    engine_control = out / "foreign-owner-stop.inc"
    engine_control.write_text(engine_source.replace(needle, "SOUND.KillSamplesForThing(0);", 1))
    driver_source = (ROOT / "port/tools/tests/ce_catalogue_sound_engine_driver.cpp").read_text()
    driver_source = driver_source.replace('"../../game/ce_catalogue_sounds.h"',
        '"' + str(ROOT / "port/game/ce_catalogue_sounds.h") + '"')
    driver_source = driver_source.replace('"../../game/ce_catalogue_sound_engine.inc"',
        '"' + str(engine_control) + '"')
    engine_driver_control = out / "foreign-owner-driver.cpp"
    engine_driver_control.write_text(driver_source)
    list_source = (ROOT / "port/game/ce_catalogue_list.cpp").read_text()
    list_needle = "\tPortCeCatalogueSoundsStop();\n\tPaneClose();"
    if list_source.count(list_needle) != 1:
        raise SystemExit("the sound list's Close is not where this check expects it")
    list_control = out / "list-leave-keeps-playing.cpp"
    list_control.write_text(list_source.replace(list_needle, "\t(void)0;\n\tPaneClose();", 1))
    rows = []
    cases = [
        ("controller-menu", ["port/game/ce_asset_catalog.cpp", "port/game/ce_catalogue_menu.cpp",
            "port/game/ce_catalogue_sounds.cpp", "port/game/ce_catalogue_list.cpp", "port/tools/tests/ce_catalogue_sounds_driver.cpp"], [str(game)], False),
        ("native-owner", ["port/tools/tests/ce_catalogue_sound_engine_driver.cpp"], [], False),
        ("sound-as-texture", ["port/game/ce_asset_catalog.cpp", str(menu_control), "port/game/ce_catalogue_sounds.cpp", "port/game/ce_catalogue_list.cpp",
            "port/tools/tests/ce_catalogue_sounds_driver.cpp"], [str(game)], True),
        # the list box keeps playing after it is left: its Close no longer stops the sound
        ("list-leave-keeps-playing", ["port/game/ce_asset_catalog.cpp", "port/game/ce_catalogue_menu.cpp", "port/game/ce_catalogue_sounds.cpp",
            str(list_control), "port/tools/tests/ce_catalogue_sounds_driver.cpp"], [str(game)], True),
        ("foreign-owner-stop", [str(engine_driver_control)], [], True),
    ]
    for name, sources, command_args, adverse in cases:
        binary = out / name
        log = out / (name + ".private.log")
        command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1", "-DCE_VERSION", "-DWCHAR=wchar_t", "-I", str(ROOT / "port/game")]
        command += [str(ROOT / source) for source in sources]
        command += ["-lz", "-o", str(binary)]
        with log.open("wb") as stream:
            compile_result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
            run_result = None
            if compile_result.returncode == 0:
                run_result = subprocess.run([str(binary), *command_args], stdout=stream, stderr=subprocess.STDOUT)
        text = log.read_text(errors="replace")
        counts = re.findall(r"(?:^|\s)(\d+) (?:checks|assertions)(?: passed)?", text)
        counts += [passed for passed, total in re.findall(r"(\d+)/(\d+) loaded-sound[^\n]*checks passed", text) if passed == total]
        row = {"case": name, "compile_rc": compile_result.returncode,
               "run_rc": run_result.returncode if run_result else None,
               "assertion_counts": [int(value) for value in counts], "log": str(log), "adverse_control": adverse,
               "passed": compile_result.returncode == 0 and run_result is not None and
                   ((run_result.returncode != 0 and ("FAIL:" in text or "native sound adapter check" in text)) if adverse else run_result.returncode == 0)}
        rows.append(row)
    ok = all(row["passed"] for row in rows)
    receipt = {"passed": ok, "cases": rows, "skipped": 0, "retail_inputs": False,
               "source_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                   for path in [ROOT / "port/game/ce_catalogue_menu.cpp", ROOT / "port/game/ce_catalogue_sounds.cpp", ROOT / "port/game/ce_catalogue_list.cpp", ROOT / "port/game/ce_catalogue_sounds.h", ROOT / "port/game/ce_catalogue_sound_engine.inc"]}}
    (out / "receipt.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": ok, "cases": len(rows), "skipped": 0,
                      "assertion_counts": [count for row in rows if not row["adverse_control"] for count in row["assertion_counts"]],
                      "adverse_controls_killed": sum(row["passed"] and row["adverse_control"] for row in rows),
                      "receipt": str(out / "receipt.private.json")}))
    return 0 if ok else 1

if __name__ == "__main__":
    raise SystemExit(main())
