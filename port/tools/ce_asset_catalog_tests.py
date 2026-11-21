#!/usr/bin/env python3
"""Exercise the native catalogue with authored installations; no game data is used."""
# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import hashlib
import json
import os
import re
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

PORT = Path(__file__).resolve().parents[1]
ROOT = PORT.parent
OUTPUT = ROOT / "local-data" / "ce-asset-catalog-tests"
FOLDERS = (
    "data/resources/meshes",
    "data/resources/dxtntextures",
    "data/resources/textures",
    "data/sounds",
)


def disable_core_dumps() -> None:
    if os.name == "posix":
        import resource
        _, hard = resource.getrlimit(resource.RLIMIT_CORE)
        resource.setrlimit(resource.RLIMIT_CORE, (0, hard))


def bank(records: list[tuple[bytes, int]], *, trailer: bytes = b"") -> bytes:
    """Small independent PC XAP writer for parser controls, never retail payloads."""
    result = bytearray(b"PCXP" + struct.pack("<I", len(records)))
    for name, decoded_size in records:
        if len(name) > 64:
            raise ValueError("fixture name exceeds its field")
        result += b"PSMP" + name.ljust(64, b"\0") + struct.pack("<I", decoded_size)
        result += b"\x33" * ((decoded_size + 3) // 4)
    return bytes(result) + trailer


def compile_driver(output: Path = OUTPUT, *, sanitize: bool = False) -> Path:
    output.mkdir(parents=True, exist_ok=True)
    binary = output / ("driver-sanitize" if sanitize else "driver")
    compiler = os.environ.get("CXX", "clang++")
    command = [compiler, "-std=c++17", "-O1" if sanitize else "-O2", "-g",
               "-Wall", "-Wextra", "-Werror", "-pedantic", "-DCE_VERSION", "-I", str(PORT / "game"),
               str(PORT / "game/ce_asset_catalog.cpp"), str(PORT / "game/ce_catalogue_menu.cpp"), str(PORT / "game/ce_catalogue_sounds.cpp"), str(PORT / "tools/tests/ce_catalogue_sounds_none.cpp"),
               str(PORT / "tools/tests/ce_asset_catalog_driver.cpp"), "-lz", "-o", str(binary)]
    if sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    result = subprocess.run(command, capture_output=True, text=True)
    (output / (binary.name + "-compile.log")).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"native catalogue compilation failed; see {output}")
    return binary


class CatalogueTests(unittest.TestCase):
    binary: Path

    @classmethod
    def setUpClass(cls) -> None:
        cls.binary = compile_driver(sanitize="--sanitize" in sys.argv)

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="authored-", dir=OUTPUT)
        self.base = Path(self.temporary.name)
        self.install = self.base / "Synthetic installation"
        for folder in FOLDERS:
            (self.install / folder).mkdir(parents=True)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def file(self, folder: int, name: str, data: bytes = b"authored fixture") -> Path:
        path = self.install / FOLDERS[folder] / name
        path.write_bytes(data)
        return path

    def scan(self, query: str = "", mask: int = 7, texture: str = "", *, root: Path | str | None = None) -> dict:
        result = subprocess.run([str(self.binary), "scan", str(self.install if root is None else root),
                                 query, str(mask), texture], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, "native driver failed; authored fixture only")
        value = json.loads(result.stdout)
        self.assertTrue(value["bounds_safe"])
        return value

    def test_empty_folders_are_a_complete_empty_scan(self) -> None:
        result = self.scan()
        self.assertTrue(result["opened"])
        self.assertEqual(result["assets"], [])
        self.assertEqual(result["diagnostics"], [])

    def test_null_and_out_of_range_interfaces_are_safe(self) -> None:
        result = subprocess.run([str(self.binary), "defaults"], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stdout), True)

    def test_invalid_root_has_a_bounded_error(self) -> None:
        for root in ("", self.base / "missing", self.base / "not-a-folder"):
            if str(root).endswith("not-a-folder"):
                Path(root).write_bytes(b"fixture")
            result = self.scan(root=root)
            self.assertFalse(result["opened"])
            self.assertTrue(result["error"])
            self.assertLess(len(result["error"]), 256)

    def test_models_textures_and_sound_samples_are_distinct(self) -> None:
        self.file(0, "m_Scout.msh.aya", b"model")
        self.file(1, "mustbe_Hull.tga(0).aya", b"texture")
        self.file(2, "Reticle.tga(0).aya", b"interface")
        self.file(3, "sounds_effects_pc.xap", bank([(b"weapons\\Pulse.wav", 8), (b"UI\\Select.wav", 0)]))
        result = self.scan()
        self.assertEqual([a["kind"] for a in result["assets"]], [1, 2, 2, 4, 4])
        self.assertEqual(result["assets"][0]["title"], "Scout")
        self.assertEqual(result["assets"][1]["title"], "Hull")
        sounds = [a for a in result["assets"] if a["kind"] == 4]
        self.assertEqual([a["title"] for a in sounds], ["weapons/Pulse.wav", "UI/Select.wav"])
        self.assertEqual([a["sample"] for a in sounds], [0, 1])
        self.assertEqual([a["size"] for a in sounds], [8, 0])
        self.assertTrue(all(a["group"] == "Effects" for a in sounds))
        self.assertEqual(result["diagnostics"], [])

    def browse(self, group: int, page: int, slot: int, root: Path | None = None) -> dict:
        result = subprocess.run([str(self.binary), "browse", str(root or self.install),
                                 str(group), str(page), str(slot)], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0)
        return json.loads(result.stdout.decode("latin1"))

    def test_selected_texture_uses_stored_dds_header_across_members(self) -> None:
        h = bytearray(128)
        h[:4] = b"DDS "
        struct.pack_into("<7I", h, 4, 124, 0x21007, 32, 64, 128, 0, 4)
        struct.pack_into("<II4s", h, 76, 32, 4, b"DXT5")
        struct.pack_into("<I", h, 108, 0x1000)
        def member(data: bytes) -> bytes:
            packed = zlib.compress(data)
            return struct.pack("<I", len(packed)) + packed
        for i in range(12):
            self.file(1, "mustbe_Anchor%02d.tga(0).aya" % i)
        source = self.file(1, "mustbe_Coral_Plate.tga(0).aya", member(h[:57]) + member(h[57:]))
        view = self.browse(1, 2, 0)
        self.assertEqual((view["name"], view["page"], view["pages"]), ("Textures: Coral Plate", 2, 2))
        self.assertIn("64 x 32 pixels", view["detail"])
        self.assertIn("64 x 32 pixels, with transparency", view["detail"])
        self.assertNotIn(source.name, view["detail"])
        self.assertEqual(view, self.browse(1, 2, 0))
        exact = bytes(h)
        struct.pack_into("<I", h, 8, 0x1007)
        struct.pack_into("<7I", h, 80, 0x41, 0, 32, 0xff0000, 0xff00, 0xff, 0xff000000)
        source.write_bytes(member(h))
        self.assertIn("with transparency", self.browse(1, 2, 0)["detail"])
        short = bytearray(exact); struct.pack_into("<I", short, 28, 99)
        for bad in [member(b"not a DDS"), member(short), b"\xff\xff\xff\x7f" + b"bad", member(b"x" * (2**20 + 1)), member(exact)[:-2]]:
            source.write_bytes(bad)
            v = self.browse(1, 2, 0)
            self.assertEqual(v["name"], "Textures: Coral Plate")
            self.assertNotIn(source.name, v["detail"])
            self.assertNotIn("mip level", v["detail"])
            self.assertIn("Its size could not be read.", v["detail"])

    def test_native_list_identifies_texture_source_and_names_from_the_install(self) -> None:
        self.file(1, "mustbe_Solar_Hull.tga(0).aya")
        self.file(2, "Solar_Hull.tga(0).aya")
        game, interface = (self.browse(1, 1, slot) for slot in (0, 1))
        self.assertEqual([game["name"], interface["name"]], ["Textures: Solar Hull", "Interface: Solar Hull"])
        # a player reads a name, its group and place, and the size: never the file, its folder or its encoding
        self.assertTrue(game["detail"].startswith("Solar Hull\nTextures, item 1 of 2."))
        self.assertTrue(interface["detail"].startswith("Solar Hull\nInterface, item 2 of 2."))
        for text in (game["detail"], interface["detail"]):
            for machine in ("Source", "resources", ".aya", "(0)", "mustbe", "%", "underscore", "DDS:"):
                self.assertNotIn(machine, text)
        self.assertIn("Its size could not be read.", game["detail"])
        self.assertNotIn(str(self.install), game["detail"] + interface["detail"])

    def test_picture_reads_only_the_selected_file_and_passes_its_pixels_to_the_loader(self) -> None:
        def member(data):
            z = zlib.compress(data)
            return struct.pack("<I", len(z)) + z
        def header(w=4, h=4):
            v = bytearray(128); v[:4] = b"DDS "
            struct.pack_into("<7I", v, 4, 124, 0x1007, h, w, 0, 0, 0)
            struct.pack_into("<II4s", v, 76, 32, 4, b"DXT1")
            struct.pack_into("<I", v, 108, 0x1000)
            return bytes(v)
        def picture(slot, allow=True, group=1):
            out = subprocess.run([str(self.binary),"picture",str(self.install),str(group),"1",str(slot),str(int(allow))],
                                 capture_output=True, timeout=10, check=True)
            return json.loads(out.stdout)
        def signature(pixels):
            n = 2166136261
            for c in pixels: n = ((n ^ c) * 16777619) & 0xffffffff
            return n
        a, b = bytes(range(8)), bytes(reversed(range(8)))
        self.file(1, "A.aya", member(header()) + member(a))
        self.file(1, "B.aya", member(header()+b))
        self.file(1, "C.aya", member(header()) + b"bad")
        self.file(1, "D.aya", member(header()))
        self.file(1, "E.aya", member(header(8192)) + member(a))
        self.file(0, "A.aya", member(header()+a))
        self.assertEqual(picture(0), [1, 1, 136, signature(a)])
        self.assertEqual(picture(1), [1, 1, 136, signature(b)])
        self.assertEqual(picture(0,False), [0, 1, 136, signature(a)])
        for slot,group in [(2,1),(3,1),(4,1),(11,1),(0,0)]:
            self.assertEqual(picture(slot,group=group), [0,0,0,0])
        self.assertIn("4 x 4 pixels", self.browse(1,1,2)["detail"])  # a broken payload can still have a readable header

    def test_details_name_the_item_not_its_file(self) -> None:
        # a texture's first line is its name as words: no file name, extension, format or encoding
        for source, shown in [("B_Panel_2.aya", "B Panel 2"), ("D%2A-normal.aya", "2A Normal"),
                              ("C" * 30 + "_x.aya", "C" * 30 + " X")]:
            created = self.file(1, source)
            detail = self.browse(1, 1, 0)["detail"]
            self.assertEqual(detail.splitlines()[0], shown)
            self.assertNotIn(".aya", detail)
            self.assertNotIn("underscore", detail)
            created.unlink()

    def test_native_paging_and_full_identifying_name(self) -> None:
        for item in range(13):
            self.file(0, f"m_Flight_{item:02}.msh.aya")
        self.file(0, "m_Zed_" + "x" * 38 + ".msh.aya")
        last = self.browse(0, 2, 1)
        self.assertEqual((last["page"], last["pages"], last["visible"]), (2, 2, 2))
        self.assertEqual(last["name"], ("Models: Zed X" + "x" * 37)[:34])
        self.assertIn("Zed X" + "x" * 37, last["detail"])
        self.assertIn("Models, item 14 of 14.", last["detail"])
        self.assertEqual(self.browse(0, 3, 1)["name"], "Models: Flight 01")
        self.assertEqual(self.browse(0, 2, 2)["detail"], "")

    def test_excluded_sound_banks_are_not_opened(self) -> None:
        self.file(0, "m_Scout.msh.aya")
        self.file(3, "broken.xap", b"not a bank")
        self.assertTrue(self.scan()["diagnostics"])
        command = [str(self.binary), "scan-kinds", str(self.install), "", "7", "", "3"]
        filtered = json.loads(subprocess.check_output(command, timeout=10))
        self.assertEqual(filtered["diagnostics"], [])
        self.assertEqual([item["title"] for item in filtered["assets"]], ["Scout"])

    def test_native_list_handles_absent_and_empty_game_folders(self) -> None:
        for root in (self.install, self.base / "missing"):
            result = self.browse(0, 1, 0, root)
            self.assertEqual((result["visible"], result["name"], result["detail"]), (0, "", ""))

    def test_case_insensitive_extensions_and_stable_filename_order(self) -> None:
        for name in ("m_zed.MSH.AYA", "M_Alpha.MsH.AyA", "m_beta.msh.aya"):
            self.file(0, name)
        result = self.scan()
        self.assertEqual([a["title"] for a in result["assets"]], ["Alpha", "beta", "zed"])
        self.assertEqual(result, self.scan())

    def test_non_asset_files_and_nested_trees_are_not_ingested(self) -> None:
        self.file(0, "notes.txt")
        self.file(0, "mesh.aya.backup")
        nested = self.install / FOLDERS[0] / "nested"
        nested.mkdir()
        (nested / "m_hidden.msh.aya").write_bytes(b"nested fixture")
        self.assertEqual(self.scan()["assets"], [])

    def test_query_matches_title_path_and_group_with_kind_filter(self) -> None:
        self.file(0, "m_Scout.msh.aya")
        self.file(1, "mustbe_Scout.tga(0).aya")
        self.file(3, "sounds_radio_pc.xap", bank([(b"voice\\Scout.wav", 4)]))
        self.assertEqual(len(self.scan("SCOUT")["matches"]), 3)
        self.assertEqual(len(self.scan("SCOUT", 1)["matches"]), 1)
        self.assertEqual(len(self.scan("dxtntextures")["matches"]), 1)
        self.assertEqual(len(self.scan("radio")["matches"]), 1)
        self.assertEqual(self.scan("not present")["matches"], [])
        self.assertEqual(self.scan("", 0)["matches"], [])
        self.assertEqual(len(self.scan("", 1 | 4)["matches"]), 2)

    def test_texture_lookup_is_exact_case_insensitive_and_deterministic(self) -> None:
        self.file(1, "mustbe_Hull.tga(0).aya")
        self.file(1, "mustbe_Hull.tga(1).aya")
        self.file(2, "mustbe_Hull.tga(0).aya")
        result = self.scan(texture="MUSTBE_HULL.TGA")
        self.assertIsNotNone(result["resolve"])
        self.assertEqual(result["assets"][result["resolve"]]["path"], FOLDERS[1] + "/mustbe_Hull.tga(0).aya")
        for wrong in ("Hull", "mustbe_Hull", "mustbe_Hull.tga.extra", "../mustbe_Hull.tga", ""):
            self.assertIsNone(self.scan(texture=wrong)["resolve"])

    def test_utf8_paths_and_latin1_sample_names_are_lossless(self) -> None:
        self.file(0, "m_Caf\u00e9.msh.aya")
        self.file(3, "sounds_test_pc.xap", bank([(b"caf\xe9.wav", 4)]))
        result = self.scan("Caf\u00e9")
        self.assertEqual(len(result["matches"]), 2)
        self.assertEqual(result["assets"][0]["title"], "Caf\u00e9")
        self.assertEqual(result["assets"][1]["title"], "caf\u00e9.wav")

    @unittest.skipUnless(os.name == "posix", "POSIX byte names")
    def test_invalid_utf8_filename_is_reported_not_returned(self) -> None:
        path = os.fsencode(self.install / FOLDERS[0]) + b"/m_\xff.msh.aya"
        with open(path, "wb") as stream:
            stream.write(b"invalid-name fixture")
        result = self.scan()
        self.assertEqual(result["assets"], [])
        self.assertEqual(len(result["diagnostics"]), 1)
        self.assertIn("UTF-8", result["diagnostics"][0]["message"])

    def test_missing_folder_is_reported_without_hiding_other_kinds(self) -> None:
        (self.install / FOLDERS[2]).rmdir()
        self.file(0, "m_Visible.msh.aya")
        result = self.scan()
        self.assertEqual(len(result["assets"]), 1)
        self.assertEqual(len(result["diagnostics"]), 1)

    @unittest.skipUnless(hasattr(os, "symlink"), "requires symlink support")
    def test_root_and_child_directory_links_are_refused(self) -> None:
        root_link = self.base / "root-link"
        root_link.symlink_to(self.install, target_is_directory=True)
        self.assertFalse(self.scan(root=root_link)["opened"])
        self.assertFalse(self.scan(root=str(root_link) + "/")["opened"])
        folder = self.install / FOLDERS[0]
        folder.rmdir()
        outside = self.base / "outside"
        outside.mkdir()
        (outside / "m_Private.msh.aya").write_bytes(b"outside fixture")
        folder.symlink_to(outside, target_is_directory=True)
        result = self.scan()
        self.assertEqual(result["assets"], [])
        self.assertEqual(len(result["diagnostics"]), 1)

    @unittest.skipUnless(hasattr(os, "symlink"), "requires symlink support")
    def test_intermediate_directory_link_cannot_escape_root(self) -> None:
        for folder in FOLDERS:
            (self.install / folder).rmdir()
        (self.install / "data/resources").rmdir()
        (self.install / "data").rmdir()
        outside = self.base / "outside-data"
        for folder in FOLDERS:
            (outside / Path(folder).relative_to("data")).mkdir(parents=True, exist_ok=True)
        (outside / "resources/meshes/m_Private.msh.aya").write_bytes(b"outside fixture")
        (self.install / "data").symlink_to(outside, target_is_directory=True)
        result = self.scan()
        self.assertEqual(result["assets"], [])
        self.assertEqual(len(result["diagnostics"]), 4)

    @unittest.skipUnless(hasattr(os, "symlink"), "requires symlink support")
    def test_asset_link_and_fifo_never_read_their_target(self) -> None:
        outside = self.base / "outside-bank"
        outside.write_bytes(bank([(b"Outside", 4)]))
        (self.install / FOLDERS[3] / "link.xap").symlink_to(outside)
        if hasattr(os, "mkfifo"):
            os.mkfifo(self.install / FOLDERS[3] / "pipe.xap")
        result = self.scan()
        self.assertEqual(result["assets"], [])
        self.assertEqual(len(result["diagnostics"]), 2 if hasattr(os, "mkfifo") else 1)

    def test_malformed_bank_is_atomic_and_other_banks_survive(self) -> None:
        self.file(3, "a_bad.xap", bank([(b"first", 8), (b"second", 8)])[:-1])
        self.file(3, "b_good.xap", bank([(b"accepted", 4)]))
        result = self.scan()
        self.assertEqual([a["title"] for a in result["assets"]], ["accepted"])
        self.assertEqual(len(result["diagnostics"]), 1)

    def test_bank_header_and_record_validation(self) -> None:
        good = bank([(b"test", 4)])
        variants = [b"", good[:7], b"WRNG" + good[4:],
                    good[:8] + b"WRNG" + good[12:],
                    b"PCXP" + struct.pack("<I", 0xFFFFFFFF),
                    bank([(b"test", 3)]), bank([(b"", 4)]),
                    bank([(b"bad\x1bname", 4)]),
                    good[:76] + struct.pack("<I", 0x80000000)]
        for i, value in enumerate(variants):
            with self.subTest(case=i):
                path = self.file(3, "malformed.xap", value)
                result = self.scan()
                self.assertEqual(result["assets"], [])
                self.assertEqual(len(result["diagnostics"]), 1)
                path.unlink()

    def test_zero_record_bank_is_valid(self) -> None:
        self.file(3, "empty.xap", bank([]))
        result = self.scan()
        self.assertEqual(result["assets"], [])
        self.assertEqual(result["diagnostics"], [])

    def test_diagnostic_count_is_bounded(self) -> None:
        for i in range(1100):
            self.file(3, f"bad-{i:04}.xap", b"bad fixture")
        result = self.scan()
        self.assertEqual(len(result["diagnostics"]), 1025)
        self.assertNotEqual(result["diagnostics"][-1]["message"], result["diagnostics"][-2]["message"])

    def test_scan_preserves_contents_and_modes(self) -> None:
        self.file(0, "m_ReadOnly.msh.aya")
        self.file(3, "readonly.xap", bank([(b"fixture", 4)]))
        sentinel = self.base / "profile-sentinel"
        sentinel.write_bytes(b"ordinary profile is outside the selected install")
        paths = [p for p in self.base.rglob("*") if p.is_file()]
        for p in paths:
            p.chmod(0o444)
        before = {str(p): (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_mode) for p in paths}
        self.assertEqual(len(self.scan()["assets"]), 2)
        after = {str(p): (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_mode) for p in paths}
        self.assertEqual(before, after)


if __name__ == "__main__":
    disable_core_dumps()
    unittest.main(argv=[x for x in sys.argv if x != "--sanitize"])
