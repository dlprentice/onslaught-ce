#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authentic Retail Muspell First-Person Cockpit test suite.

    BEA_GAME_DIR=/path/to/game python3 -I port/tools/ce_muspell_cockpit_tests.py [-v]

Verifies:
  1. Host helper logic & 12 causal mutants under ASan/UBSan (port/game/ce_muspell_cockpit.h):
     - case- and slash-insensitive texture-name matching (cockpit.tga, cockpit_M.tga, A8_bluegunlight_LIT.tga);
     - deterministic procedural warm crimson-amber gunlight gradient (R > G > B everywhere, monotonic U ramp,
       strong mLit alpha, out-of-bounds clamping, zero-dimension guard);
     - strict allegiance & faithful/demo/recording/pilot fence gating (Muspell only; every fence tested);
     - fixed-capacity scoped swap & LIFO restore-all invariant, including shared CVBufTexture* across duplicate
       CMeshTexture slots (cockpit2b.msh), null/missing fallback, capacity saturation, and idempotent restore.
  2. Static source & patch invariants (0376-ce-muspell-cockpit.patch, ce_muspell_cockpit.h/.cpp):
     - SPDX GPL-3.0-only headers, zero unallowlisted BEA_* env vars, zero CE_NETWORK_OFF refs in 0376;
     - pure #ifdef CE_VERSION guard on DXCockpit.cpp (compiling identically under !CE_VERSION);
     - zero FindTexture/GetTextureByName calls (no disk I/O, no AddRef leaks, no FatalFileError);
     - persistent procedural gunlight unlinked from CTexture::mFirst with D3DPOOL_MANAGED DeviceObject overrides.
  3. Live offscreen engine proof (real compiled Linux `bea` at 1280x720, zero desktop windows):
     - Level 851 Muspell 1P vs Forseti 1P: telemetry + direct left gun-pod RGB & crimson gunlight pixel proof;
     - Level 851 Muspell Jet morph: clean takeoff morph with Muspell cockpit active, 0 NaNs;
     - Level 851 2P splitscreen (P1 Muspell top + P2 Forseti bottom in the same frame): proves scoped Begin/End
       restoration keeps P2 Forseti blue while P1 Muspell renders dark obsidian armor + crimson gunlight;
     - Replay fence (BEA_PORT_CE_LAYER unset) and campaign level 110 (missing cockpit_M.tga): zero activation, clean exit.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
SCRATCH = ROOT / "local-data/port/ce-muspell-cockpit-tests"
DEFAULT_EXE = ROOT / "local-data/port/build/linux/bea"

HOST_DRIVER = r"""
#include "ce_muspell_cockpit.h"
#include <cstdio>

static int gFail = 0;
static void Check(bool ok, const char *what) {
    if (!ok) { std::printf("FAIL: %s\n", what); gFail++; }
}

struct FakeTex { int id; };
struct FakeVBuf { FakeTex *mTexture; };

int main() {
    // 1. Texture-name matching (case-insensitive, / and \ normalized, exact match only)
    Check(CeMuspellCockpitIsBaseTextureName("meshtex\\cockpit.tga"), "base exact");
    Check(CeMuspellCockpitIsBaseTextureName("MESHTEX\\COCKPIT.TGA"), "base uppercase");
    Check(CeMuspellCockpitIsBaseTextureName("meshtex/cockpit.tga"), "base forward slash");
    Check(CeMuspellCockpitIsBaseTextureName("MeshTex/CockPit.TGA"), "base mixed case & slash");
    Check(!CeMuspellCockpitIsBaseTextureName(nullptr), "base rejects nullptr");
    Check(!CeMuspellCockpitIsBaseTextureName(""), "base rejects empty");
    Check(!CeMuspellCockpitIsBaseTextureName("meshtex\\cockpit_M.tga"), "base rejects muspell");
    Check(!CeMuspellCockpitIsBaseTextureName("meshtex\\Chrome3.tga"), "base rejects chrome");
    Check(!CeMuspellCockpitIsBaseTextureName("meshtex\\A8_bluegunlight_LIT.tga"), "base rejects gunlight");
    Check(!CeMuspellCockpitIsBaseTextureName("cockpit.tga"), "base rejects bare filename");
    Check(!CeMuspellCockpitIsBaseTextureName("meshtex\\cockpit.tga.bak"), "base rejects suffix");

    Check(CeMuspellCockpitIsMuspellTextureName("meshtex\\cockpit_M.tga"), "muspell exact");
    Check(CeMuspellCockpitIsMuspellTextureName("meshtex/cockpit_m.tga"), "muspell lowercase slash");
    Check(CeMuspellCockpitIsMuspellTextureName("MESHTEX\\COCKPIT_M.TGA"), "muspell uppercase");
    Check(!CeMuspellCockpitIsMuspellTextureName(nullptr), "muspell rejects nullptr");
    Check(!CeMuspellCockpitIsMuspellTextureName("meshtex\\cockpit.tga"), "muspell rejects base");
    Check(!CeMuspellCockpitIsMuspellTextureName("meshtex\\cockpit_M2.tga"), "muspell rejects extra chars");

    Check(CeMuspellCockpitIsGunlightTextureName("meshtex\\A8_bluegunlight_LIT.tga"), "gunlight exact");
    Check(CeMuspellCockpitIsGunlightTextureName("meshtex/a8_bluegunlight_lit.tga"), "gunlight lowercase slash");
    Check(!CeMuspellCockpitIsGunlightTextureName(nullptr), "gunlight rejects nullptr");
    Check(!CeMuspellCockpitIsGunlightTextureName("meshtex\\cockpit.tga"), "gunlight rejects base");
    Check(!CeMuspellCockpitIsGunlightTextureName("meshtex\\A8_bluegunlight.tga"), "gunlight rejects non-lit");

    // 2. Procedural warm crimson-amber gunlight pixel generator
    Check(CeMuspellCockpitGunlightPixel(0, 0, 0, 64) == 0u, "zero width -> 0");
    Check(CeMuspellCockpitGunlightPixel(0, 0, 64, 0) == 0u, "zero height -> 0");
    Check(CeMuspellCockpitGunlightPixel(-5, -5, 64, 64) == CeMuspellCockpitGunlightPixel(0, 0, 64, 64), "neg coords clamp");
    Check(CeMuspellCockpitGunlightPixel(99, 99, 64, 64) == CeMuspellCockpitGunlightPixel(63, 63, 64, 64), "hi coords clamp");
    for (int y = 0; y < CE_MUSPELL_GUNLIGHT_SIZE; ++y) {
        unsigned prevR = 0;
        for (int x = 0; x < CE_MUSPELL_GUNLIGHT_SIZE; ++x) {
            unsigned px = CeMuspellCockpitGunlightPixel(x, y, CE_MUSPELL_GUNLIGHT_SIZE, CE_MUSPELL_GUNLIGHT_SIZE);
            unsigned a = (px >> 24) & 0xffu;
            unsigned r = (px >> 16) & 0xffu;
            unsigned g = (px >> 8) & 0xffu;
            unsigned b = px & 0xffu;
            Check(r > g && g > b, "warm crimson-amber palette R > G > B everywhere");
            Check(r >= 180u && a >= 120u, "strong emissive red & alpha");
            Check(r >= prevR, "red channel monotonic along U");
            prevR = r;
        }
    }
    unsigned centerPx = CeMuspellCockpitGunlightPixel(32, 32, 64, 64);
    unsigned edgePx = CeMuspellCockpitGunlightPixel(32, 2, 64, 64);
    Check(((centerPx >> 16) & 0xffu) > ((edgePx >> 16) & 0xffu), "center band brighter than outer edge");

    // 2b. Warm crimson-charcoal lighting boost prevents dark night maps (L851) from washing cockpit navy-blue
    {
        CeMuspellCockpitLightBoost lb = CeMuspellCockpitComputeLightBoost(0xff142048u, 0.25f, 0.35f, 0.65f);
        unsigned ar = (lb.ambient >> 16) & 0xffu;
        unsigned ag = (lb.ambient >> 8) & 0xffu;
        unsigned ab = lb.ambient & 0xffu;
        Check(ar >= 68u && ar > ag + 35u && ab <= 16u, "boosted ambient is warm crimson-obsidian with blue clamped");
        Check(lb.keyR >= 0.75f && lb.keyR > lb.keyG * 2.0f && lb.keyG > lb.keyB, "boosted key light is warm crimson-amber");
        Check(lb.rimR >= 0.55f && lb.rimR > lb.rimG * 2.0f && lb.rimG > lb.rimB, "boosted rim light is warm crimson");
        Check(lb.matEmisR >= 0.08f && lb.matEmisR > lb.matEmisB * 4.0f, "subtle crimson emissive material boost");
    }

    // 3. Strict allegiance & fence gating
    CeMuspellCockpitFenceInput good = {};
    good.ceLayer = true;
    good.allegiance = CE_MUSPELL_ALLEGIANCE;
    Check(CeMuspellCockpitShouldSwap(good), "active for Muspell under CE layer with all fences clear");

    CeMuspellCockpitFenceInput t = good; t.allegiance = 0;
    Check(!CeMuspellCockpitShouldSwap(t), "inactive for Forseti (0)");
    t = good; t.allegiance = 2;
    Check(!CeMuspellCockpitShouldSwap(t), "inactive for Independent (2)");
    t = good; t.allegiance = -1;
    Check(!CeMuspellCockpitShouldSwap(t), "inactive for negative allegiance");
    t = good; t.ceLayer = false;
    Check(!CeMuspellCockpitShouldSwap(t), "fenced when !ceLayer");
    t = good; t.recording = true;
    Check(!CeMuspellCockpitShouldSwap(t), "fenced during recording");
    t = good; t.playableDemo = true;
    Check(!CeMuspellCockpitShouldSwap(t), "fenced during PLAYABLE_DEMO");
    t = good; t.autoConfigTest = true;
    Check(!CeMuspellCockpitShouldSwap(t), "fenced during AutoConfigTest");
    t = good; t.recordDemo = true;
    Check(!CeMuspellCockpitShouldSwap(t), "fenced during RecordDemo");
    t = good; t.playbackDemo = true;
    Check(!CeMuspellCockpitShouldSwap(t), "fenced during PlaybackDemo");
    t = good; t.stressTest = true;
    Check(!CeMuspellCockpitShouldSwap(t), "fenced during StressTest");
    t = good; t.attractMode = true;
    Check(!CeMuspellCockpitShouldSwap(t), "fenced during AttractMode");
    t = good; t.pilotEnv = true;
    Check(!CeMuspellCockpitShouldSwap(t), "fenced during pilot/autopilot");

    // 4. Scoped swap & LIFO restore-all invariant (including shared CVBufTexture across duplicate entries)
    FakeTex baseTex{1}, muspellTex{2}, blueLight{3}, redLight{4}, chromeTex{5};
    FakeVBuf sharedVBuf{&baseTex}, lightVBuf{&blueLight}, chromeVBuf{&chromeTex};
    FakeTex *slot0 = &baseTex;
    FakeTex *slot1 = &blueLight;
    FakeTex *slot2 = &chromeTex;
    FakeTex *slot3 = &baseTex; // duplicate cockpit.tga entry sharing sharedVBuf (cockpit2b.msh pattern)

    CeMuspellCockpitRestoreStack<FakeTex *, FakeVBuf *, 4> stack;
    auto getVBufSlot = [](FakeVBuf *vb) -> FakeTex ** { return vb ? &vb->mTexture : nullptr; };

    // Null & missing fallback safety
    Check(!stack.RecordAndSwap(nullptr, &sharedVBuf, &sharedVBuf.mTexture, &muspellTex), "null mesh slot rejected");
    Check(!stack.RecordAndSwap(&slot0, &sharedVBuf, &sharedVBuf.mTexture, nullptr), "null replacement rejected");
    Check(stack.count == 0 && slot0 == &baseTex && sharedVBuf.mTexture == &baseTex, "failed swaps mutate nothing");

    // Record swaps: slot0 + sharedVBuf, slot1 + lightVBuf, slot3 + SAME sharedVBuf, and a null-vbuf slot
    FakeTex *nullVbSlot = &baseTex;
    Check(stack.RecordAndSwap(&slot0, &sharedVBuf, &sharedVBuf.mTexture, &muspellTex), "swap slot0");
    Check(stack.RecordAndSwap(&slot1, &lightVBuf, &lightVBuf.mTexture, &redLight), "swap slot1");
    Check(stack.RecordAndSwap(&slot3, &sharedVBuf, &sharedVBuf.mTexture, &muspellTex), "swap duplicate slot3");
    Check(stack.RecordAndSwap(&nullVbSlot, nullptr, nullptr, &muspellTex), "swap null-vbuf slot");
    Check(stack.count == 4, "4 swaps recorded");

    // Capacity saturation fails closed without OOB writes (ASan checked)
    FakeTex *overflowSlot = &baseTex;
    Check(!stack.RecordAndSwap(&overflowSlot, nullptr, nullptr, &muspellTex), "capacity overflow rejected");
    Check(overflowSlot == &baseTex && stack.count == 4, "overflow slot untouched");

    // Verify swapped state while active
    Check(slot0 == &muspellTex && slot3 == &muspellTex && sharedVBuf.mTexture == &muspellTex, "armor swapped on both mesh & vbuf");
    Check(slot1 == &redLight && lightVBuf.mTexture == &redLight, "gunlight swapped on both mesh & vbuf");
    Check(slot2 == &chromeTex && chromeVBuf.mTexture == &chromeTex, "chrome untouched");
    Check(nullVbSlot == &muspellTex, "null-vbuf slot swapped");

    // RestoreAll must restore every slot AND sharedVBuf back to baseTex (LIFO invariant!)
    Check(stack.RestoreAll(getVBufSlot) == 4, "restored 4 entries");
    Check(stack.count == 0, "stack empty after restore");
    Check(slot0 == &baseTex && slot3 == &baseTex, "mesh armor slots restored");
    Check(sharedVBuf.mTexture == &baseTex, "shared CVBufTexture restored to original baseTex (LIFO)");
    Check(slot1 == &blueLight && lightVBuf.mTexture == &blueLight, "gunlight restored");
    Check(nullVbSlot == &baseTex, "null-vbuf slot restored");
    Check(slot2 == &chromeTex && chromeVBuf.mTexture == &chromeTex, "chrome still untouched");
    Check(stack.RestoreAll(getVBufSlot) == 0, "second RestoreAll is idempotent no-op");

    if (!gFail) std::puts("muspell-cockpit-host-ok");
    return gFail != 0;
}
"""


def read_ppm_rgb(path: Path):
    """Pure-stdlib P6 PPM reader (returns (width, height, raw_rgb_bytes)) so tests work under python3 -I."""
    data = path.read_bytes()
    idx = 0
    tokens = []
    while len(tokens) < 4:
        while idx < len(data) and data[idx] in b" \t\r\n":
            idx += 1
        if data[idx:idx + 1] == b"#":
            while idx < len(data) and data[idx] != ord("\n"):
                idx += 1
            continue
        start = idx
        while idx < len(data) and data[idx] not in b" \t\r\n":
            idx += 1
        tokens.append(data[start:idx])
    if tokens[0] != b"P6":
        raise ValueError(f"expected P6 PPM, got {tokens[0]!r}")
    w, h, maxv = int(tokens[1]), int(tokens[2]), int(tokens[3])
    if maxv != 255:
        raise ValueError(f"expected maxval 255, got {maxv}")
    # Exactly one whitespace byte separates header from binary pixel payload.
    pixels = data[idx + 1:]
    if len(pixels) < w * h * 3:
        raise ValueError(f"truncated PPM: {len(pixels)} < {w * h * 3}")
    return w, h, pixels


def region_color_stats(w, h, pixels, x0, y0, x1, y1):
    """Returns (avg_r, avg_g, avg_b, warm_crimson_pixels) inside [x0, x1) x [y0, y1)."""
    sr = sg = sb = count = warm = 0
    for y in range(max(0, y0), min(h, y1)):
        row = y * w * 3
        for x in range(max(0, x0), min(w, x1)):
            off = row + x * 3
            r, g, b = pixels[off], pixels[off + 1], pixels[off + 2]
            sr += r
            sg += g
            sb += b
            count += 1
            if r >= 140 and r - b >= 60 and r > g:
                warm += 1
    assert count > 0
    return sr / count, sg / count, sb / count, warm


def resolve_runtime_libs():
    roots = []
    if os.environ.get("BEA_PORT_DATA"):
        roots.append(Path(os.environ["BEA_PORT_DATA"]))
    roots.append(ROOT / "local-data/port-deps-canonical")
    git = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--git-common-dir"], capture_output=True, text=True)
    if git.returncode == 0 and git.stdout.strip():
        roots.append((ROOT / git.stdout.strip()).resolve().parent / "local-data/port-deps-canonical")
    for root in roots:
        if (root / "dxvk-native/usr/lib32").is_dir():
            dirs = [root / "prefix-sniper/lib", root / "dxvk-native/usr/lib32", root / "ffmpeg-shared-sniper/linux/usr/lib"]
            return [str(d) for d in dirs if d.is_dir()]
    return []


def run_bea_offscreen(exe, game_dir, run_dir, level, quit_frames, ce_ini_text="", ce_layer=True, shot_at=None, fake_keys=None):
    user_dir = run_dir / "user"
    for sub in (user_dir, run_dir / "cache", run_dir / "runtime"):
        sub.mkdir(parents=True, exist_ok=True)
    if ce_ini_text is not None:
        (user_dir / "ce.ini").write_text(ce_ini_text, encoding="utf-8")
    env = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "HOME": str(user_dir),
        "SDL_VIDEO_DRIVER": "offscreen",
        "BEA_PORT_REQUIRE_OFFSCREEN": "1",
        "SDL_AUDIO_DRIVER": "dummy",
        "BEA_PORT_REQUIRE_DUMMY_AUDIO": "1",
        "BEA_REPORT_NO_OPEN": "1",
        "BROWSER": "/bin/true",
        "DXVK_WSI_DRIVER": "SDL3",
        "DXVK_LOG_LEVEL": "none",
        "XDG_CACHE_HOME": str(run_dir / "cache"),
        "XDG_RUNTIME_DIR": str(run_dir / "runtime"),
        "BEA_USER_DIR": str(user_dir),
        "BEA_GAME_DIR": str(game_dir),
        "BEA_NO_UPDATE_CHECK": "1",
        "BEA_PORT_CLOCK": "fixed",
        "BEA_PORT_CLOCK_DT": "0.05",
        "BEA_PORT_QUIT": str(quit_frames),
    }
    if ce_layer:
        env["BEA_PORT_CE_LAYER"] = "1"
    if shot_at is not None:
        env["BEA_PORT_SHOT"] = f"{shot_at[0]}:{shot_at[1]}"
    if fake_keys is not None:
        env["BEA_PORT_FAKE_KEYS"] = fake_keys
    libs = resolve_runtime_libs()
    if libs:
        env["LD_LIBRARY_PATH"] = ":".join(libs)
    cmd = [str(exe), "-level", str(level), "-skipfmv", "-forcewindowed", "-res", "1280", "720"]
    return subprocess.run(cmd, cwd=str(game_dir), env=env, capture_output=True, text=True, timeout=180)


def cockpit_telemetry_lines(stderr_text):
    return [line.strip() for line in stderr_text.splitlines() if "ce-muspell-cockpit:" in line]


class HostHelperTests(unittest.TestCase):
    def compile_and_run(self, header_text, tag, sanitize=True):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        safe_tag = tag.replace(" ", "-").replace("/", "-")
        base = Path(tempfile.mkdtemp(prefix=f"muspell-{safe_tag}-", dir=SCRATCH))
        try:
            (base / "ce_muspell_cockpit.h").write_text(header_text, encoding="utf-8")
            driver = base / "driver.cpp"
            driver.write_text(HOST_DRIVER, encoding="utf-8")
            exe = base / "driver"
            cmd = [shutil.which("g++") or "c++", "-std=gnu++14", "-Wall", "-Werror", "-I", str(base), str(driver), "-o", str(exe)]
            if sanitize:
                cmd.insert(2, "-fsanitize=address,undefined")
                cmd.insert(3, "-fno-omit-frame-pointer")
            cc = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            if cc.returncode != 0:
                return cc.returncode, cc.stdout + cc.stderr
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            return run.returncode, run.stdout + run.stderr
        finally:
            shutil.rmtree(base, ignore_errors=True)

    def test_host_helpers_pass_asan_ubsan(self):
        src = (PORT / "game/ce_muspell_cockpit.h").read_text(encoding="utf-8")
        rc, out = self.compile_and_run(src, "clean", sanitize=True)
        self.assertEqual(rc, 0, out)
        self.assertEqual(out.strip(), "muspell-cockpit-host-ok")

    def test_causal_mutants_are_caught(self):
        src = (PORT / "game/ce_muspell_cockpit.h").read_text(encoding="utf-8")
        mutants = {
            "case-sensitive match": (
                "if (c >= 'A' && c <= 'Z')\n\t\treturn (char)(c - 'A' + 'a');",
                "if (false)\n\t\treturn c;",
            ),
            "slash normalization dropped": (
                "if (c == '/')\n\t\treturn '\\\\';",
                "if (false)\n\t\treturn '\\\\';",
            ),
            "base matches muspell texture": (
                'return CeMuspellCockpitStrEqPathCase(name, "meshtex\\\\cockpit.tga");',
                'return CeMuspellCockpitStrEqPathCase(name, "meshtex\\\\cockpit.tga") || CeMuspellCockpitStrEqPathCase(name, "meshtex\\\\cockpit_M.tga");',
            ),
            "allows Forseti allegiance": (
                "return in.allegiance == CE_MUSPELL_ALLEGIANCE;",
                "return in.allegiance >= 0;",
            ),
            "ignores ceLayer fence": (
                "if (!in.ceLayer || in.recording)",
                "if (in.recording)",
            ),
            "ignores recording fence": (
                "if (!in.ceLayer || in.recording)",
                "if (!in.ceLayer)",
            ),
            "ignores playableDemo fence": (
                "if (in.playableDemo || in.autoConfigTest || in.recordDemo ||",
                "if (in.autoConfigTest || in.recordDemo ||",
            ),
            "ignores demo record/playback fence": (
                "in.playbackDemo || in.stressTest || in.attractMode || in.pilotEnv)",
                "in.stressTest || in.attractMode || in.pilotEnv)",
            ),
            "ignores pilotEnv fence": (
                "in.playbackDemo || in.stressTest || in.attractMode || in.pilotEnv)",
                "in.playbackDemo || in.stressTest || in.attractMode)",
            ),
            "FIFO restore breaks shared CVBufTexture": (
                "for (int i = count - 1; i >= 0; --i)",
                "for (int i = 0; i < count; ++i)",
            ),
            "missing vbufTexSlot swap": (
                "if (vbufTexSlot)\n\t\t\t*vbufTexSlot = replacement;",
                "if (false && vbufTexSlot)\n\t\t\t*vbufTexSlot = replacement;",
            ),
            "gunlight RGB channels swapped to blue": (
                "return (a << 24) | (r << 16) | (g << 8) | b;",
                "return (a << 24) | (b << 16) | (g << 8) | r;",
            ),
        }
        for name, (old, new) in mutants.items():
            with self.subTest(mutant=name):
                self.assertIn(old, src, f"mutant anchor missing for {name}")
                rc, _ = self.compile_and_run(src.replace(old, new, 1), name, sanitize=False)
                self.assertNotEqual(rc, 0, f"causal mutant {name} was NOT caught")


class StaticSourceInvariantTests(unittest.TestCase):
    def test_spdx_headers_and_env_hygiene(self):
        for rel in ("game/ce_muspell_cockpit.h", "game/ce_muspell_cockpit.cpp", "tools/ce_muspell_cockpit_tests.py"):
            text = (PORT / rel).read_text(encoding="utf-8")
            self.assertIn("SPDX-License-Identifier: GPL-3.0-only", text[:200], rel)
        h_text = (PORT / "game/ce_muspell_cockpit.h").read_text(encoding="utf-8")
        cpp_text = (PORT / "game/ce_muspell_cockpit.cpp").read_text(encoding="utf-8")
        self.assertEqual(set(re.findall(r"BEA_[A-Z0-9_]+", h_text)), set())
        self.assertEqual(set(re.findall(r"BEA_[A-Z0-9_]+", cpp_text)), {"BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT"})

    def test_patch_0376_is_pure_ce_version_and_network_free(self):
        patch = (PORT / "patches/0376-ce-muspell-cockpit.patch").read_text(encoding="latin-1")
        self.assertEqual(re.findall(r"^--- a/(.+)$", patch, re.M), ["DXCockpit.cpp"])
        self.assertNotIn("CE_NETWORK_OFF", patch, "0376 must contain zero CE_NETWORK_OFF references")
        self.assertIn("PortCeMuspellCockpitBegin(mBattleEngine, mRenderThing);", patch)
        self.assertIn("PortCeMuspellCockpitEnd();", patch)
        # Every added non-blank line must lie inside an added #ifdef CE_VERSION .. #endif block.
        depth = 0
        for line in patch.splitlines():
            if not line.startswith("+") or line.startswith("+++"):
                continue
            body = line[1:].strip()
            if body == "#ifdef CE_VERSION":
                depth += 1
            elif body == "#endif":
                self.assertGreater(depth, 0)
                depth -= 1
            elif body:
                self.assertGreater(depth, 0, f"added line outside #ifdef CE_VERSION: {body!r}")
        self.assertEqual(depth, 0, "unbalanced #ifdef CE_VERSION in 0376")

    def test_cpp_invariants_zero_findtexture_and_device_reset_safety(self):
        cpp = (PORT / "game/ce_muspell_cockpit.cpp").read_text(encoding="utf-8")
        # Must NEVER call FindTexture or GetTextureByName (avoids disk I/O, AddRef leaks, FatalFileError).
        self.assertNotIn("->FindTexture(", cpp)
        self.assertNotIn("::FindTexture(", cpp)
        self.assertNotIn("GetTextureByName(", cpp)
        self.assertIn("CTexture::mFirst", cpp)
        self.assertIn("static_assert((int)kMuspellAllegiance == CE_MUSPELL_ALLEGIANCE", cpp)
        for fence in ("PortCeLayer()", "PortRecording()", "PLAYABLE_DEMO", "CLIPARAMS.mAutoConfigTest",
                      "CLIPARAMS.mRecordDemo", "CLIPARAMS.mPlaybackDemo", "CLIPARAMS.mStressTest",
                      "CLIPARAMS.mAttractMode", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT"):
            self.assertIn(fence, cpp, f"missing required fence {fence}")
        for method in ("InitDeviceObjects()", "RestoreDeviceObjects()", "InvalidateDeviceObjects()", "DeleteDeviceObjects()"):
            self.assertIn(method, cpp, f"missing DeviceObject override {method}")
        self.assertIn("D3DPOOL_MANAGED", cpp)
        self.assertGreaterEqual(cpp.count('extern "C" void PortCeMuspellCockpitBegin('), 2)
        self.assertGreaterEqual(cpp.count('extern "C" void PortCeMuspellCockpitEnd('), 2)


class LiveEngineCockpitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = Path(os.environ.get("BEA_EXE", str(DEFAULT_EXE)))
        game_dir_env = os.environ.get("BEA_GAME_DIR", "").strip()
        cls.game_dir = Path(game_dir_env) if game_dir_env else None
        if not cls.exe.is_file():
            raise AssertionError(f"built Linux binary not found at {cls.exe} (build with port/build.py --target linux --public)")
        if cls.game_dir is None or not cls.game_dir.is_dir():
            raise AssertionError("BEA_GAME_DIR is not set to a valid game directory")

    def test_level_851_muspell_vs_forseti_1p_cockpit_and_gunlight_live(self):
        tmp = Path(tempfile.mkdtemp(prefix="muspell-l851-cmp-", dir=SCRATCH))
        try:
            m_ppm = tmp / "muspell.ppm"
            f_ppm = tmp / "forseti.ppm"
            m_proc = run_bea_offscreen(
                self.exe, self.game_dir, tmp / "m", level=851, quit_frames=440,
                ce_ini_text="[Experimental]\nsolo_skirmish=1\nmulti_be=2\nplay_side=muspell\n",
                shot_at=(420, m_ppm),
            )
            self.assertEqual(m_proc.returncode, 0, m_proc.stderr[-1500:])
            self.assertTrue(m_ppm.is_file(), "missing Muspell screenshot")
            m_lines = cockpit_telemetry_lines(m_proc.stderr)
            self.assertEqual(len(m_lines), 1, f"expected single per-level activation log, got {m_lines}")
            self.assertIn("level=851 swapped=2 gunlight=1", m_lines[0])
            self.assertNotIn("leaked : refcount", m_proc.stderr)

            f_proc = run_bea_offscreen(
                self.exe, self.game_dir, tmp / "f", level=851, quit_frames=440,
                ce_ini_text="[Experimental]\nsolo_skirmish=1\nmulti_be=2\nplay_side=forseti\n",
                shot_at=(420, f_ppm),
            )
            self.assertEqual(f_proc.returncode, 0, f_proc.stderr[-1500:])
            self.assertTrue(f_ppm.is_file(), "missing Forseti screenshot")
            self.assertEqual(cockpit_telemetry_lines(f_proc.stderr), [], "Forseti must never trigger Muspell cockpit swap")

            mw, mh, mpx = read_ppm_rgb(m_ppm)
            fw, fh, fpx = read_ppm_rgb(f_ppm)
            self.assertEqual((mw, mh), (1280, 720))
            self.assertEqual((fw, fh), (1280, 720))

            mr, mg, mb, m_warm = region_color_stats(mw, mh, mpx, 0, 220, 140, 440)
            fr, fg, fb, f_warm = region_color_stats(fw, fh, fpx, 0, 220, 140, 440)
            # Forseti left pod is distinctly Forseti blue; Muspell left pod is dark obsidian + warm crimson gunlight.
            self.assertGreater(fb - fr, 30.0, f"Forseti pod should remain strongly blue: RGB=({fr:.1f},{fg:.1f},{fb:.1f})")
            self.assertLess(mb - mr, (fb - fr) - 12.0,
                            f"Muspell pod should be much less blue than Forseti: M=({mr:.1f},{mg:.1f},{mb:.1f}) F=({fr:.1f},{fg:.1f},{fb:.1f})")
            self.assertGreater(m_warm, 50, f"expected warm crimson-amber gunlight pixels on Muspell left pod, got {m_warm}")
            self.assertEqual(f_warm, 0, f"Forseti left pod must have zero crimson gunlight pixels, got {f_warm}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_level_851_muspell_jet_morph_live(self):
        tmp = Path(tempfile.mkdtemp(prefix="muspell-l851-jet-", dir=SCRATCH))
        try:
            ppm = tmp / "jet.ppm"
            proc = run_bea_offscreen(
                self.exe, self.game_dir, tmp, level=851, quit_frames=480,
                ce_ini_text="[Experimental]\nsolo_skirmish=1\nmulti_be=2\nplay_side=muspell\n",
                shot_at=(458, ppm),
                fake_keys="450:98:52:d,453:98:52:u",
            )
            self.assertEqual(proc.returncode, 0, proc.stderr[-1500:])
            self.assertTrue(ppm.is_file())
            w, h, _ = read_ppm_rgb(ppm)
            self.assertEqual((w, h), (1280, 720))
            m_lines = cockpit_telemetry_lines(proc.stderr)
            self.assertEqual(len(m_lines), 1, m_lines)
            self.assertIn("level=851 swapped=2 gunlight=1", m_lines[0])
            self.assertIn("morphs=1", proc.stderr, "expected Jet morph during capture")
            self.assertIn("nan=0", proc.stderr)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_level_851_splitscreen_p1_muspell_and_p2_forseti_restore_isolation(self):
        """In 2P splitscreen (P1 Muspell top half, P2 Forseti bottom half), both Battle Engines share cockpit2.msh
        in the same frame: P1 gets Muspell armor + crimson gunlight while P2 MUST restore cleanly to Forseti blue."""
        tmp = Path(tempfile.mkdtemp(prefix="muspell-l851-split-", dir=SCRATCH))
        try:
            ppm = tmp / "split.ppm"
            proc = run_bea_offscreen(
                self.exe, self.game_dir, tmp, level=851, quit_frames=440,
                ce_ini_text="[Experimental]\nsolo_skirmish=0\nmulti_be=2\nplay_side=muspell\n",
                shot_at=(420, ppm),
            )
            self.assertEqual(proc.returncode, 0, proc.stderr[-1500:])
            self.assertTrue(ppm.is_file())
            m_lines = cockpit_telemetry_lines(proc.stderr)
            self.assertEqual(len(m_lines), 1, m_lines)
            self.assertIn("level=851 swapped=2 gunlight=1", m_lines[0])

            w, h, px = read_ppm_rgb(ppm)
            self.assertEqual((w, h), (1280, 720))
            # Top half (P1 Muspell): left gun-pod region y=110..230; Bottom half (P2 Forseti): y=470..590
            tr, tg, tb, t_warm = region_color_stats(w, h, px, 0, 110, 130, 230)
            br, bg, bb, b_warm = region_color_stats(w, h, px, 0, 470, 130, 590)
            self.assertGreater(bb - br, 20.0, f"P2 Forseti splitscreen pod leaked Muspell skin: bottom RGB=({br:.1f},{bg:.1f},{bb:.1f})")
            self.assertGreater(t_warm, 10, f"P1 Muspell splitscreen pod missing warm crimson gunlight: warm={t_warm} RGB=({tr:.1f},{tg:.1f},{tb:.1f})")
            self.assertEqual(b_warm, 0, f"P2 Forseti splitscreen pod leaked crimson gunlight: b_warm={b_warm}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_replay_fence_and_campaign_fallback_remain_inert(self):
        tmp = Path(tempfile.mkdtemp(prefix="muspell-fence-", dir=SCRATCH))
        try:
            # 1. Faithful replay fence (BEA_PORT_CE_LAYER unset) on Level 851
            r_proc = run_bea_offscreen(
                self.exe, self.game_dir, tmp / "replay", level=851, quit_frames=440,
                ce_ini_text="[Experimental]\nsolo_skirmish=1\nmulti_be=2\nplay_side=muspell\n",
                ce_layer=False,
            )
            self.assertEqual(r_proc.returncode, 0, r_proc.stderr[-1500:])
            self.assertEqual(cockpit_telemetry_lines(r_proc.stderr), [], "must stay completely inert when !PortCeLayer()")

            # 2. Campaign Level 110 (lacks cockpit_M.tga in 110_res_PC.aya)
            c_proc = run_bea_offscreen(
                self.exe, self.game_dir, tmp / "camp110", level=110, quit_frames=440,
                ce_ini_text="[Experimental]\nplay_side=muspell\n",
                ce_layer=True,
            )
            self.assertEqual(c_proc.returncode, 0, c_proc.stderr[-1500:])
            self.assertEqual(cockpit_telemetry_lines(c_proc.stderr), [], "campaign Forseti level must not activate Muspell cockpit")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    SCRATCH.mkdir(parents=True, exist_ok=True)
    unittest.main()
