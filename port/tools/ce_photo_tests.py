#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CE photo mode (port/game/ce_photo.cpp, patches 0108 and 0133) and its screenshot writer (port/platform/common/ce_screenshot.cpp).

- The patches: where their hooks sit in the patched DXEngine.cpp, Hud.cpp, ltshell.cpp and game.cpp (the HUD overlay, the battle
  line and the cockpit routines each run inside a draw scope, and nothing skips them), and that the faithful build's view of
  those files (CE_VERSION and DEV_VERSION undefined) is unchanged by them. The filter that scope switches is
  photo_draw_filter_tests.py.
- Photo mode itself with the real patched message handler against the console's authored fakes (ce_photo_driver.cpp):
  F11 and F12 and who owns them, the draw filter switched with photo mode and the scope that opens it, the developer
  -killhud field left alone, the note kept out of a saved frame, the fences, level end and the proof switch.
- The writer on the pinned SDL library (ce_screenshot_driver.cpp), one case per process: the pixels of the PNG it wrote,
  decoded here, for each back buffer format; never replacing a file or following a link; the folder bounds; a write
  refused part way leaves nothing.
Causal mutants of each must be caught. What a real back buffer reads back as is covered by the offscreen captures, not
here.
"""
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build  # noqa: E402
import deps  # noqa: E402
from ce_palette_tests import function, no_core, sections, SANITIZE  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
PATCHES = ROOT / "port/patches"
PATCH_NAMES = ("0108-ce-photo-mode.patch", "0133-ce-photo-draw-suppression.patch")
# Patches whose hunks are written against photo mode's (the save error notice is drawn after its note): left out of the run without it.
BUILT_ON_PHOTO = ("0137-ce-save-failed-notice.patch", "0162-commander-map-hooks.patch")
PHOTO = ROOT / "port/game/ce_photo.cpp"
PHOTO_H = ROOT / "port/game/ce_photo.h"
WRITER = ROOT / "port/platform/common/ce_screenshot.cpp"
FAKE = ROOT / "port/tools/ce_palette_fake.h"
LOGIC = ROOT / "port/tools/tests/ce_photo_driver.cpp"
PALETTE_LOGIC = ROOT / "port/tools/tests/ce_photo_palette_driver.cpp"
WRITER_DRIVER = ROOT / "port/tools/tests/ce_screenshot_driver.cpp"
FILES = ("DXEngine.cpp", "Hud.cpp", "ltshell.cpp", "game.cpp")
FLAGS = ("BEA_PORT_CE_LAYER", "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
         "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN", "BEA_PORT_PHOTO_PROOF")
NAME = re.compile(r"^onslaught-ce-\d{8}-\d{6}(-\d+)?\.png$")
STATUS = {"SAVED": 0, "FORMAT": -1, "SIZE": -2, "FOLDER": -3, "FULL": -4, "ENCODE": -5, "WRITE": -6, "NAME": -7}


def patched(out, names, skip=()):
    """src/ copies of names in out/src, normalised as the build's mirror is, with every port patch's section for them
    applied in order (skip: patches left out)."""
    mirror = out / "src"
    mirror.mkdir(parents=True, exist_ok=True)
    build.SYSTEM_NAMES.update(build.system_header_names())
    index = build.CaseIndex([out / path.relative_to(ROOT) for path in build.tracked_sources()])
    for name in names:
        source = (ROOT / "src" / name).read_bytes().decode("latin-1")
        (mirror / name).write_bytes(build.normalise(source, mirror, index, [mirror, mirror / "MissionScript"]).encode("latin-1"))
    for patch in sorted(PATCHES.glob("*.patch")):
        if patch.name in skip:
            continue
        parts = sections(patch.read_text(encoding="latin1"))
        for name in names:
            if name in parts:
                (mirror / "section.patch").write_text(parts[name], encoding="latin1")
                applied = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", "section.patch"], cwd=mirror,
                                         capture_output=True, text=True, timeout=10, preexec_fn=no_core)
                if applied.returncode or "fuzz" in applied.stdout.lower():
                    raise AssertionError(f"{patch.name} {name}: {applied.stdout}{applied.stderr}")
    return {name: (mirror / name).read_text(encoding="latin1") for name in names}


def faithful(text):
    """The text the faithful build compiles: CE_VERSION and DEV_VERSION undefined. Conditionals on them are resolved and
    their directives dropped; any other conditional is kept as it is."""
    out, stack = [], []				# each entry: None (not ours) or [active, branch taken]
    for line in text.split("\n"):
        d = line.strip()
        ours = None
        if re.match(r"#\s*ifdef\s+(CE_VERSION|DEV_VERSION)\b", d) or \
                re.match(r"#\s*if\s+defined\s*\(\s*(CE|DEV)_VERSION\s*\)(\s*\|\|\s*defined\s*\(\s*(CE|DEV)_VERSION\s*\))*\s*$", d):
            ours = False
        elif re.match(r"#\s*ifndef\s+(CE_VERSION|DEV_VERSION)\b", d):
            ours = True
        if ours is not None:
            stack.append([ours])
            continue
        if re.match(r"#\s*if", d):
            stack.append(None)
        elif re.match(r"#\s*else\b", d) and stack and stack[-1] is not None:
            stack[-1][0] = not stack[-1][0]
            continue
        elif re.match(r"#\s*elif\b", d) and stack and stack[-1] is not None:
            raise AssertionError("an #elif on CE_VERSION is not resolved here")
        elif re.match(r"#\s*endif\b", d) and stack:
            if stack.pop() is not None:
                continue
        if all(s is None or s[0] for s in stack):
            out.append(line)
    return "\n".join(out)


def check_structure(files):
    """Where patch 0108 puts photo mode in the patched game files (raises AssertionError)."""
    try:
        placed(files)
    except ValueError as missing:
        raise AssertionError(f"a gate is missing: {missing}") from None


def placed(files):
    shell = function(files["ltshell.cpp"], "LRESULT PCLTShell::MsgProc(")
    keydown = shell[shell.index("if( WM_KEYDOWN == uMsg )"):shell.index("WM_KEYUP == uMsg")]
    assert keydown.index("if (PortCePhotoKey(key, (lParam & 0x40000000) != 0)) {}") < keydown.index("PortCePaletteSuppressDown(key)"), \
        "F11 and F12 are offered to photo mode before the console"
    engine = files["DXEngine.cpp"]
    late = function(engine, "static void draw_fairly_late_stuff(")
    assert "#ifdef DEV_VERSION\n\tif(!CLIPARAMS.mKillHUD)\n#endif\n" in late, "the cockpit keeps the developer build's own -killhud test, as src/ has it"
    assert "CE_VERSION)" not in late.split("be->GetCockpit()")[0].split("Render Cockpit")[1], "photo mode does not open the cockpit's -killhud test"
    cockpit = re.search(r"\(player->ShouldRenderInternalCockpit\(\) == TRUE\)\)\n(.*?)be->GetCockpit\(\)->Render\(\) ;\n\t+RENDERQUEUE\.Render\(\);", late, re.S)
    assert cockpit and re.search(r"RENDERINFO\.SetLightingEnabled\(TRUE\);\s*\n#ifdef CE_VERSION\n\t+CePhotoDrawScope photoDraw;[^\n]*\n#endif\n\t+$",
                                 cockpit.group(1)), "the cockpit and its queue run inside a draw scope, not behind a test"
    post = function(engine, "HRESULT\tCDXEngine::PostRender(")
    assert "CLIPARAMS" not in post, "PostRender no longer reads -killhud"
    assert re.search(r"GetShowHUD\(\)\)\)\n\t+render=TRUE;", post), "the battle line goes where it always did"
    assert re.search(r"if \(render\)\n#ifdef CE_VERSION\n\t\t\{\n\t\t\tCePhotoDrawScope photoDraw;[^\n]*\n#endif\n\t\t\tHUD\.RenderBattleline\(viewport\);"
                     r"\n#ifdef CE_VERSION\n\t\t\}\n#endif", post), "the battle line runs inside a draw scope"
    for kept in ("HUD.Render();", "GAME.GetMessageLog()->Render(viewport)", "GAME.GetLevelBriefingLog()->Render(viewport)",
                 "GAME.GetPauseMenu()->Render()", "GAMEINTERFACE.Render();", "HUD.RenderOverlay();"):
        assert kept in post, kept
    mirror = post[post.index("if (!gMirrorPass)\t\t// retail PC (0053efc4)"):]
    assert re.search(r"GAMEINTERFACE\.Render\(\);\n#ifdef CE_VERSION\n\t\tPortCePaletteRender\(\);\n\t\tPortCePhotoRender\(\);\n(\t\tPortCeNoticeRender\(\);\n)?#endif",
                     mirror), "the note is drawn after the console, outside the mirror pass (only the save error notice, patch 0137, may follow it)"
    hud = function(files["Hud.cpp"], "void\tCHud::Render()")
    assert "CLIPARAMS" not in hud, "the HUD no longer reads -killhud"
    assert re.search(r"GetShowHUD\(\)\)\n#ifdef CE_VERSION\n\t\t\{\n\t\t\tCePhotoDrawScope photoDraw;[^\n]*\n#endif\n\t\t\tRenderOverlayForViewpoint\(player, i\);"
                     r"\n#ifdef CE_VERSION\n\t\t\}\n#endif", hud), "the HUD overlay runs inside a draw scope"
    assert hud.index("mFlash =") < hud.index("RenderOverlayForViewpoint"), "the HUD's own bookkeeping still runs"
    for name in ("Hud.cpp", "DXEngine.cpp"):
        assert '#ifdef CE_VERSION\n#include "../game/ce_photo.h"' in files[name], f"{name} includes the scope's header only in CE builds"
    game = files["game.cpp"]
    assert re.search(r"PortCeConsoleLevelEnd\(\);[^\n]*\n\tPortCePhotoLevelEnd\(\);", game), "photo mode ends with the level"
    assert re.search(r"PortCeConsoleFrame\(\);[^\n]*\n\tPortCePhotoFrame\(\);", game), "between frames"


def png_pixels(data):
    """(width, height, rows of RGBA tuples) of an 8-bit RGBA, non-interlaced PNG."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "a PNG signature"
    at, idat, head = 8, b"", None
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        assert zlib.crc32(kind + body) == struct.unpack(">I", data[at + 8 + n:at + 12 + n])[0], "chunk CRC"
        if kind == b"IHDR":
            head = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        at += 12 + n
    w, h, depth, colour, _, _, interlace = head
    assert (depth, colour, interlace) == (8, 6, 0), head
    raw, stride, rows, prev = zlib.decompress(idat), w * 4, [], bytearray(w * 4)
    for y in range(h):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b, c = prev[i], prev[i - 4] if i >= 4 else 0
            if kind == 1: line[i] = (line[i] + a) & 255
            elif kind == 2: line[i] = (line[i] + b) & 255
            elif kind == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x * 4:x * 4 + 4]) for x in range(w)])
        prev = line
    return w, h, rows


def expected(fmt):
    """The test picture (ce_screenshot_driver.cpp) as the PNG must hold it: bit-replicated widening for 16-bit formats."""
    def widen(v, bits):
        v >>= 8 - bits
        return (v << (8 - bits)) | (v >> (2 * bits - 8))
    rows = []
    for y in range(5):
        row = []
        for x in range(7):
            r, g, b = x * 36 + 1, y * 50 + 2, (x + y) * 20 + 3
            if fmt == "rgb565": r, g, b = widen(r, 5), widen(g, 6), widen(b, 5)
            if fmt == "xrgb1555": r, g, b = widen(r, 5), widen(g, 5), widen(b, 5)
            row.append((r, g, b, 255))
        rows.append(row)
    return rows


class PhotoTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "local-data/port/ce-photo-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        self.out = Path(self.tmp.name)

    def tearDown(self):
        for p in self.out.rglob("*"):
            if p.is_dir() and not p.is_symlink():
                p.chmod(0o700)
        self.tmp.cleanup()

    # ---- the patch ------------------------------------------------------------------------------------------------
    def test_patch_places_and_faithful_view(self):
        files = patched(self.out / "with", FILES)
        check_structure(files)
        without = patched(self.out / "without", FILES, skip=PATCH_NAMES + BUILT_ON_PHOTO)
        for name in FILES:
            with self.subTest(file=name):
                self.assertNotEqual(files[name], without[name])
                self.assertEqual(faithful(files[name]), faithful(without[name]), f"patches 0108 and 0133 change the faithful {name}")

    def test_structure_mutants_are_caught(self):
        files = patched(self.out / "with", FILES)
        mutants = [
            ("key-after-console", "ltshell.cpp", "\t\t\tif (PortCePhotoKey(key, (lParam & 0x40000000) != 0)) {}	// patch 0108: F11 photo mode, F12 screenshot\n\t\t\telse if (PortCePaletteSuppressDown(key)) {}",
             "\t\t\tif (PortCePaletteSuppressDown(key)) {}\n\t\t\telse if (PortCePhotoKey(key, (lParam & 0x40000000) != 0)) {}"),
            ("cockpit-scope-removed", "DXEngine.cpp", "\t\t\t\tCePhotoDrawScope photoDraw;\t// patch 0133: photo mode drops the cockpit's draw calls; its routines run in full\n", ""),
            ("cockpit-behind-killhud", "DXEngine.cpp", "#ifdef DEV_VERSION\n\tif(!CLIPARAMS.mKillHUD)\n#endif", "#if defined(DEV_VERSION) || defined(CE_VERSION)\n\tif(!CLIPARAMS.mKillHUD)\n#endif"),
            ("battle-line-behind-killhud", "DXEngine.cpp", "\t\t\t\tif ((GAME.GetCamera(i)->GetShowHUD()))\n\t\t\t\t\trender=TRUE;", "\t\t\t\tif ((GAME.GetCamera(i)->GetShowHUD()))\n\t\t\t\tif (!CLIPARAMS.mKillHUD)\n\t\t\t\t\trender=TRUE;"),
            ("battle-line-scope-removed", "DXEngine.cpp", "\t\t\tCePhotoDrawScope photoDraw;\t// patch 0133: photo mode drops the battle line's draw calls; its routine runs in full\n", ""),
            ("pause-menu-hidden", "DXEngine.cpp", "\t\tGAME.GetPauseMenu()->Render() ;", "\t\tif (!CLIPARAMS.mKillHUD) GAME.GetPauseMenu()->Render() ;"),
            ("note-under-console", "DXEngine.cpp", "\t\tPortCePaletteRender();\n\t\tPortCePhotoRender();", "\t\tPortCePhotoRender();\n\t\tPortCePaletteRender();"),
            ("hud-scope-removed", "Hud.cpp", "\t\t\tCePhotoDrawScope photoDraw;\t// patch 0133: photo mode drops the overlay's draw calls; its routines run in full\n", ""),
            ("hud-skipped", "Hud.cpp", "\t\t\tRenderOverlayForViewpoint(player, i);\n#ifdef CE_VERSION", "\t\t\tif (false) RenderOverlayForViewpoint(player, i);\n#ifdef CE_VERSION"),
            ("hud-behind-photo", "Hud.cpp", "\t\tif (player && GAME.GetCamera(i)->GetShowHUD())\n#ifdef CE_VERSION", "\t\tif (player && GAME.GetCamera(i)->GetShowHUD() && !PortCePhotoOn())\n#ifdef CE_VERSION"),
            ("no-level-end", "game.cpp", "\tPortCePhotoLevelEnd();", "\t;"),
            ("no-frame", "game.cpp", "\tPortCePhotoFrame();", "\t;"),
        ]
        for name, where, old, new in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(files[where].count(old), 1, name)
                broken = dict(files)
                broken[where] = files[where].replace(old, new)
                with self.assertRaises(AssertionError, msg=f"mutant {name} survived"):
                    check_structure(broken)

    # ---- photo mode's logic ---------------------------------------------------------------------------------------------
    def logic(self, name, photo=None, network=False, ce=True, driver=LOGIC):
        folder = self.out / name
        folder.mkdir(parents=True)
        shell = patched(folder, ["ltshell.cpp"])["ltshell.cpp"]
        (folder / "msgproc.inc").write_text(function(shell, "LRESULT PCLTShell::MsgProc("))
        (folder / "ce_layer.inc").write_text(function((ROOT / "port/platform/common/ce.cpp").read_text(), "bool PortCeLayer()"))
        if driver == PALETTE_LOGIC:
            (folder / "photo_command.inc").write_text(function((ROOT / "port/game/ce_palette.cpp").read_text(), "void CmdPhoto("))
        (folder / "ce_photo.cpp").write_text(photo if photo is not None else PHOTO.read_text())
        shutil.copy(PHOTO_H, folder)
        shutil.copy(FAKE, folder)
        for header in ("common.h", "game.h", "frontend.h", "CLIParams.h", "ltshell.h", "Hud.h", "SpriteRenderer.h",
                       "renderinfo.h", "Texture.h"):
            (folder / header).write_text('#include "ce_palette_fake.h"\n')
        extra = []
        if network:
            (folder / "network_boot.cpp").write_text(
                'namespace { int requested; }\nextern "C" int CE_BootRequested(void) { return requested; }\n'
                'extern "C" void SetBootRequested(bool v) { requested = v ? 1 : 0; }\n')
            extra = ["-DFAKE_NETWORK=1", str(folder / "network_boot.cpp")]
        exe = folder / "driver"
        built = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-g", "-Wall", "-Wextra", "-Werror",
                                *(SANITIZE if ce else []), *(["-DCE_VERSION=1"] if ce else []), "-I", str(folder),
                                *extra, str(driver), str(folder / "ce_photo.cpp"), "-o", str(exe)],
                               capture_output=True, text=True, timeout=120, preexec_fn=no_core)
        self.assertEqual(built.returncode, 0, built.stderr)
        return exe

    def run_logic(self, exe, mode="ordinary", env=None):
        e = {k: v for k, v in os.environ.items() if k not in FLAGS}
        e.update(env or {})
        e.update(ASAN_OPTIONS="detect_leaks=0:abort_on_error=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        return subprocess.run([str(exe), mode], env=e, capture_output=True, text=True, timeout=30, preexec_fn=no_core)

    def test_photo_mode(self):
        exe = self.logic("actual")
        ran = self.run_logic(exe)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("photo mode keys, owners, level end, fences and proof passed", ran.stdout)
        ran = self.run_logic(exe, "denied", {"BEA_PORT_CE_LAYER": "0"})
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        for flag in FLAGS[1:-1]:
            with self.subTest(flag=flag):
                # The CE layer's screenshot override never opens photo mode under these, except the proof switch.
                ran = self.run_logic(exe, "denied", {"BEA_PORT_CE_LAYER": "1", flag: "1"})
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        net = self.logic("network", network=True)
        ran = self.run_logic(net)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        plain = self.logic("faithful", ce=False)
        ran = self.run_logic(plain)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("faithful message path passed", ran.stdout)

    def test_real_console_command_with_real_photo_controls(self):
        exe = self.logic("palette-coupled", driver=PALETTE_LOGIC)
        ran = self.run_logic(exe)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_photo_mutants_are_caught(self):
        source = PHOTO.read_text()
        mutants = [
            ("no-tape-fence", '"BEA_PORT_CLOCK", "BEA_PORT_TAPE",', '"BEA_PORT_CLOCK", "BEA_UNUSED",', "ordinary", None),
            ("no-recording-fence", "\t\tif (PortRecording()) return false;\n", "", "ordinary", None),
            ("proof-without-layer", 'if (!value || !Present("BEA_PORT_CE_LAYER")) return 0;', "if (!value) return 0;", "ordinary", None),
            ("proof-off-by-one", "PortPresentCount() % every == 0", "(PortPresentCount() + 1) % every == 0", "ordinary", None),
            ("keys-over-owner", "|| LT.GetKeytrap() || !Allowed()", "|| !Allowed()", "ordinary", None),
            ("repeat-toggles", "\tif (repeat) return true;\n", "\tif (repeat && false) return true;\n", "ordinary", None),
            ("key-not-taken", "\telse shotPending = true;\n\treturn true;", "\telse shotPending = true;\n\treturn false;", "ordinary", None),
            ("note-in-shot", "if (SavedNext() || !note[0]", "if ((SavedNext() && false) || !note[0]", "ordinary", None),
            ("note-in-proof-shot", "if (SavedNext() || !note[0]", "if (shotPending || (SavedNext() && false) || !note[0]", "ordinary", None),
            ("proof-shot-off-by-one", "return every && (PortPresentCount() + 1) % every == 0;", "return every && PortPresentCount() % every == 0;", "ordinary", None),
            ("shot-repeats", "\tshotPending = false;\n\treturn wanted;", "\treturn wanted;", "ordinary", None),
            ("f11-never-leaves", "if (key == kPhotoKey) PortCePhotoSet(!photo);", "if (key == kPhotoKey) PortCePhotoSet(true);", "ordinary", None),
            ("filter-never-on", "\tphoto = on;\n\tPortCeDrawHooks(on);", "\tphoto = on;", "ordinary", None),
            ("filter-never-off", "\tphoto = on;\n\tPortCeDrawHooks(on);", "\tphoto = on;\n\tif (on) PortCeDrawHooks(on);", "ordinary", None),
            ("proof-without-photo", "Switch(photo || ProofEvery());", "Switch(photo);", "ordinary", None),
            ("frame-no-reapply", "Switch(photo || ProofEvery());", "if (ProofEvery()) Switch(true);", "ordinary", None),
            ("sets-killhud", "\tphoto = on;\n\tPortCeDrawHooks(on);", "\tphoto = on;\n\tCLIPARAMS.mKillHUD = on;\n\tPortCeDrawHooks(on);", "ordinary", None),
            ("level-end-keeps-photo", "void PortCePhotoLevelEnd()\n{\n\tSwitch(false);", "void PortCePhotoLevelEnd()\n{", "ordinary", None),
            ("level-end-keeps-shot", "\tshotPending = false;\n\tnoteUntil = 0;", "\tnoteUntil = 0;", "ordinary", None),
            ("fence-keeps-photo", "if (!Allowed()) { Switch(false); shotPending = false; return; }", "if (!Allowed()) { shotPending = false; return; }", "ordinary", None),
            ("fence-keeps-shot", "if (!Allowed()) { Switch(false); shotPending = false; return; }", "if (!Allowed()) { Switch(false); return; }", "ordinary", None),
            ("set-ignores-fence", "if (!(fromMenu ? AllowedInLevel() : Allowed())) { Switch(false); return false; }", "", "ordinary", None),
            ("menu-item-ignores-fence", "bool PortCePhotoAvailable() { return AllowedInLevel(); }", "bool PortCePhotoAvailable() { return true; }", "ordinary", None),
            ("menu-note-is-f11s", "fromMenu ? \"Photo mode: HUD hidden. Pause menu or F11 returns; F12 saves a screenshot.\"",
             "!fromMenu ? \"Photo mode: HUD hidden. Pause menu or F11 returns; F12 saves a screenshot.\"", "ordinary", None),
            ("menu-item-never-on", "bool PortCePhotoMenuSet(bool on) { return SetPhoto(on, true); }", "bool PortCePhotoMenuSet(bool on) { return SetPhoto(on && false, true); }", "ordinary", None),
            ("note-forever", "CE_ScreenshotTicks() >= noteUntil || ", "", "ordinary", None),
            ("multiplayer", "return !GAME.IsMultiplayer() && GAME.GetNPlayers() == 1", "return GAME.GetNPlayers() >= 1", "ordinary", None),
            ("front-end", "return !FRONTEND.IsRunning() && AllowedInLevel();", "return AllowedInLevel();", "ordinary", None),
            ("menu-item-asks-the-front-end", "bool PortCePhotoAvailable() { return AllowedInLevel(); }", "bool PortCePhotoAvailable() { return Allowed(); }", "ordinary", None),
            ("menu-choice-asks-the-front-end", "if (!(fromMenu ? AllowedInLevel() : Allowed()))", "if (!Allowed())", "ordinary", None),
            ("network-boot", "\tif (CE_BootRequested && CE_BootRequested()) return false;\n", "", "network", None),
        ]
        caught = []
        for name, old, new, mode, env in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1, name)
                exe = self.logic(name, source.replace(old, new), network=(mode == "network"))
                ran = self.run_logic(exe, "ordinary", env)
                self.assertNotEqual(ran.returncode, 0, f"mutant {name} survived: {ran.stdout}")
                caught.append(name)
        self.assertEqual(len(caught), len(mutants))

    # ---- the writer -----------------------------------------------------------------------------------------------------
    def writer(self, name, source=None):
        folder = self.out / name
        (folder / "platform/common").mkdir(parents=True)
        (folder / "updater").mkdir()
        (folder / "game").mkdir()
        (folder / "platform/common/ce_screenshot.cpp").write_text(source if source is not None else WRITER.read_text())
        shutil.copy(ROOT / "port/updater/ce_report.h", folder / "updater")
        shutil.copy(PHOTO_H, folder / "game")
        exe = folder / "driver"
        cmd = [str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17", "-w", "-O1",
               '-DCE_VERSION="photo-test"', f"-I{deps.PREFIX / 'include'}", f"-I{folder / 'game'}", str(WRITER_DRIVER),
               str(folder / "platform/common/ce_screenshot.cpp"), f"-L{deps.PREFIX / 'lib'}", "-lSDL3",
               f"-Wl,-rpath,{deps.PREFIX / 'lib'}", "-o", str(exe)]
        built = subprocess.run(cmd, capture_output=True, text=True, timeout=120, env=dict(os.environ, TMPDIR=str(folder)))
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        return exe

    def save(self, exe, case, user=None):
        """Runs one case in a fresh user folder beside exe (or user); its (status, name) lines, the screenshots folder."""
        if user is None:
            user = exe.parent / f"user-{len(list(exe.parent.glob('user-*')))}" / "onslaught-ce"
        env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "SDL_"))}
        env["BEA_USER_DIR"] = str(user)
        ran = subprocess.run([str(exe), case], env=env, capture_output=True, text=True, timeout=60, preexec_fn=no_core)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        lines = [re.match(r"status=(-?\d+) name=(.*)$", line) for line in ran.stdout.splitlines()]
        self.assertTrue(lines and all(lines), ran.stdout)
        return [(int(m.group(1)), m.group(2)) for m in lines], user / "screenshots"

    def check_saved(self, results, folder, fmt="xrgb"):
        for status, name in results:
            self.assertEqual(status, STATUS["SAVED"], results)
            self.assertRegex(name, NAME)
            w, h, rows = png_pixels((folder / name).read_bytes())
            self.assertEqual((w, h), (7, 5))
            self.assertEqual(rows, expected(fmt), f"{fmt} pixels")

    def writer_cases(self, exe):
        """Every writer case; raises AssertionError at the first that fails."""
        root = exe.parent
        for fmt in ("xrgb", "argb", "rgb565", "xrgb1555"):
            results, folder = self.save(exe, fmt)
            self.check_saved(results, folder, "xrgb" if fmt == "argb" else fmt)
            self.assertEqual(folder.stat().st_mode & 0o777, 0o700, "a new screenshots folder is private")
        # Several in one second: distinct names, each a whole picture.
        results, folder = self.save(exe, "many")
        self.check_saved(results, folder)
        self.assertEqual(len({n for _, n in results}), 5)
        self.assertEqual(sorted(p.name for p in folder.iterdir()), sorted(n for _, n in results))
        # Names already taken (files of the second it is saved in): the next free name; the files are untouched.
        user = root / "taken" / "onslaught-ce"
        (user / "screenshots").mkdir(parents=True)
        results, folder = self.save(exe, "taken", user)
        self.check_saved(results, folder)
        self.assertTrue(results[0][1].endswith("-2.png"), results)
        sentinels = [p for p in folder.iterdir() if not p.name.endswith("-2.png")]
        self.assertEqual(len(sentinels), 4)
        self.assertTrue(all(p.read_bytes() == b"sentinel" for p in sentinels), "an existing file is never replaced")
        # A linked screenshots folder is refused, and nothing is written where it points.
        user = root / "linked" / "onslaught-ce"
        (user / "elsewhere").mkdir(parents=True)
        (user / "screenshots").symlink_to(user / "elsewhere")
        results, _ = self.save(exe, "one", user)
        self.assertEqual(results, [(STATUS["FOLDER"], "")])
        self.assertEqual(list((user / "elsewhere").iterdir()), [])
        # A full folder: CE_SHOT_MAX_FILES entries refuse; one fewer still saves.
        user = root / "full" / "onslaught-ce"
        (user / "screenshots").mkdir(parents=True)
        for i in range(1999):
            (user / "screenshots" / f"old-{i:04}.png").write_bytes(b"")
        results, folder = self.save(exe, "one", user)
        self.check_saved(results, folder)
        results, _ = self.save(exe, "one", user)
        self.assertEqual(results, [(STATUS["FULL"], "")])
        self.assertEqual(len(list(folder.iterdir())), 2000)
        # Bounds: an unread format, no picture, a side over the limit, a short pitch, no pixels.
        results, folder = self.save(exe, "bounds")
        self.assertEqual([s for s, _ in results], [STATUS["FORMAT"]] + [STATUS["SIZE"]] * 6, results)
        self.assertTrue(all(n == "" for _, n in results))
        self.assertFalse(folder.exists() and list(folder.iterdir()))
        # A folder it may not write to, and a file refused part way: nothing is left behind.
        user = root / "readonly" / "onslaught-ce"
        (user / "screenshots").mkdir(parents=True)
        (user / "screenshots").chmod(0o500)
        results, folder = self.save(exe, "one", user)
        self.assertEqual(results, [(STATUS["WRITE"], "")])
        folder.chmod(0o700)
        self.assertEqual(list(folder.iterdir()), [])
        results, folder = self.save(exe, "fsize")
        self.assertEqual(results, [(STATUS["WRITE"], "")])
        self.assertEqual(list(folder.iterdir()), [], "a partial file is removed")

    def test_writer(self):
        self.writer_cases(self.writer("actual"))

    def test_writer_mutants_are_caught(self):
        source = WRITER.read_text()
        mutants = [
            ("replaces", "O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC", "O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC"),
            ("first-name-only", "for (int i = 1; i <= 99 && status == CE_SHOT_NAME; ++i)", "for (int i = 1; i <= 1 && status == CE_SHOT_NAME; ++i)"),
            ("count-off-by-one", "if (Entries(&dir, CE_SHOT_MAX_FILES) >= CE_SHOT_MAX_FILES)", "if (Entries(&dir, CE_SHOT_MAX_FILES) > CE_SHOT_MAX_FILES)"),
            ("keeps-partial", "\tif (!ok) unlinkat(dir->fd, name, 0);\n", ""),
            ("no-side-bound", "width > CE_SHOT_MAX_SIDE || ", ""),
            ("alpha-kept", "\tcase kA8R8G8B8:\t\t// the back buffer's alpha is not the picture's: saved opaque\n",
             "\tcase kA8R8G8B8: return SDL_PIXELFORMAT_ARGB8888;\n"),
            ("565-swapped", "case kR5G6B5: return SDL_PIXELFORMAT_RGB565;", "case kR5G6B5: return SDL_PIXELFORMAT_BGR565;"),
            ("1555-as-565", "\tcase kX1R5G5B5: return SDL_PIXELFORMAT_XRGB1555;", "\tcase kX1R5G5B5: return SDL_PIXELFORMAT_RGB565;"),
            ("byte-cap-off", "size <= CE_SHOT_MAX_BYTES", "size <= 100"),
        ]
        caught = []
        for name, old, new in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1, name)
                exe = self.writer(name, source.replace(old, new))
                with self.assertRaises(AssertionError, msg=f"mutant {name} survived"):
                    self.writer_cases(exe)
                caught.append(name)
        self.assertEqual(len(caught), len(mutants))


if __name__ == "__main__":
    unittest.main(verbosity=2)
