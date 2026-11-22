#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify expanded terrain textures, toroidal zero-wrap halo math, seam prevention, and device lifecycle.

Compiles the real production ce_terrain_textures_math.h and ce_terrain_textures.cpp against narrow
authored D3D9/device/heightfield stubs (no game data, GPU, or display required), verifies that patch
0354-ce-terrain-textures.patch leaves the non-CE faithful build byte-identical, and asserts that
causal mutants (missing seam halo, oversized toroidal box causing wrap collision, skipped tile
invalidation, leaked GPU texture on device loss, and mirror/suspended-pass cache pollution) fail.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
GAME = PORT / "game"
COMMON = PORT / "platform/common"

sys.path.insert(0, str(PORT / "tools"))
from ce_photo_tests import faithful, patched  # noqa: E402

STUB_TYPES = r"""
#ifndef CE_TERRAIN_TEXTURES_TEST_STUBS_H
#define CE_TERRAIN_TEXTURES_TEST_STUBS_H

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

using SINT = int;
using UINT = unsigned int;
using WORD = uint16_t;
using UWORD = uint16_t;
using UBYTE = unsigned char;
using DWORD = uint32_t;
using BOOL = int;
using HRESULT = int32_t;
using D3DFORMAT = int;
using D3DPOOL = int;

#define S_OK 0
#define E_FAIL (-1)
#define SUCCEEDED(x) ((x) >= 0)
#define FAILED(x) ((x) < 0)
#define TRUE 1
#define FALSE 0

enum {
    D3DFMT_R5G6B5 = 23,
    D3DPOOL_DEFAULT = 0,
    D3DPOOL_SYSTEMMEM = 2
};

struct RECT { int left, top, right, bottom; };
struct D3DLOCKED_RECT { int Pitch; void *pBits; };
struct FVector { float X = 0.f, Y = 0.f, Z = 0.f; };

struct DeviceObject {
    virtual ~DeviceObject() {}
    virtual HRESULT InvalidateDeviceObjects() { return S_OK; }
    virtual HRESULT RestoreDeviceObjects() { return S_OK; }
    virtual HRESULT DeleteDeviceObjects() { return S_OK; }
};

struct IDirect3DTexture9 {
    UINT w = 0, h = 0;
    D3DPOOL pool = D3DPOOL_DEFAULT;
    std::vector<UWORD> pixels;
    int dirtyAdds = 0;
    int locks = 0;
    int refs = 1;
    IDirect3DTexture9(UINT width, UINT height, D3DPOOL p)
        : w(width), h(height), pool(p), pixels(width * height, 0x18e3u) {}
    HRESULT LockRect(UINT, D3DLOCKED_RECT *lr, const RECT *r, DWORD) {
        ++locks;
        lr->Pitch = static_cast<int>(w * sizeof(UWORD));
        const int x0 = r ? r->left : 0;
        const int y0 = r ? r->top : 0;
        lr->pBits = pixels.data() + y0 * w + x0;
        return S_OK;
    }
    HRESULT UnlockRect(UINT) { return S_OK; }
    HRESULT AddDirtyRect(const RECT *) { ++dirtyAdds; return S_OK; }
    void Release();
};

struct FakeDevice {
    int updateTextureCalls = 0;
    HRESULT UpdateTexture(IDirect3DTexture9 *src, IDirect3DTexture9 *dst) {
        ++updateTextureCalls;
        if (src && dst && src->w == dst->w && src->h == dst->h)
            dst->pixels = src->pixels;
        return S_OK;
    }
};

struct FakeCaps {
    UINT MaxTextureWidth = 4096;
    UINT MaxTextureHeight = 4096;
};

struct FakeShell {
    FakeCaps m_d3dCaps;
    FakeDevice dev;
    std::vector<DeviceObject *> objects;
    int liveDefault = 0;
    int liveSysmem = 0;
    int createCalls = 0;
    int failCreateAt = 0;
    FakeDevice *GetDevice() { return &dev; }
    void AddDeviceObject(DeviceObject *o) { objects.push_back(o); }
    void RemoveDeviceObject(DeviceObject *o) {
        for (auto it = objects.begin(); it != objects.end(); ++it)
            if (*it == o) { objects.erase(it); return; }
    }
    HRESULT D3D_CreateTextureNoCheck(UINT w, UINT h, UINT, DWORD, D3DFORMAT, D3DPOOL pool, IDirect3DTexture9 **out) {
        ++createCalls;
        if (failCreateAt && createCalls == failCreateAt) { *out = nullptr; return E_FAIL; }
        *out = new IDirect3DTexture9(w, h, pool);
        if (pool == D3DPOOL_DEFAULT) ++liveDefault;
        else ++liveSysmem;
        return S_OK;
    }
    void InvalidateAll() { for (auto *o : objects) o->InvalidateDeviceObjects(); }
    void RestoreAll() { for (auto *o : objects) o->RestoreDeviceObjects(); }
    void DeleteAll() { for (auto *o : objects) o->DeleteDeviceObjects(); }
};
extern FakeShell LT;

inline void IDirect3DTexture9::Release() {
    if (--refs == 0) {
        if (pool == D3DPOOL_DEFAULT) --LT.liveDefault;
        else --LT.liveSysmem;
        delete this;
    }
}

struct CTweakSLONG { int mValue = 0; };
extern CTweakSLONG LANDSCAPE_LOWRES;

struct CMapTex { int mSize = 128; };
struct CDamageTexture { UBYTE *mData = nullptr; };
struct CDamageMark { UWORD mPrev = 0, mNext = 0; UBYTE mX = 0, mY = 0, mUnknown6 = 0, mSize = 0; };
struct CDamage {
    CDamageTexture *mTexture = nullptr;
    CDamageMark mMarks[16] = {};
    UWORD mCells[4096] = {};
};
extern SINT MIP_OFFSETS[16];

struct CDXLandscape { static CDamage mDamage; };
struct FakeEngine {
    CMapTex mTex[5];
    bool mapTexNull = false;
    CMapTex *GetMapTex() { return mapTexNull ? nullptr : mTex; }
};
extern FakeEngine ENGINE;

struct FakeMap {
    float groundZ = -10.f;
    int renderCalls = 0;
    std::vector<std::pair<int, UWORD>> rendered;
    float GetHeight(int, int) { return groundZ; }
    void Unknown47eff0(SINT shift, CMapTex *, void *dst, SINT pitch, SINT tile, SINT, SINT, SINT, SINT, SINT) {
        ++renderCalls;
        rendered.push_back({shift, static_cast<UWORD>(tile)});
        const int sz = 8 << shift;
        UWORD *p = static_cast<UWORD *>(dst);
        // Encode tile + 1 so rendered texels are never 0x0000 or 0x18e3.
        const UWORD stamp = static_cast<UWORD>((tile + 1u) | 0x8000u);
        for (int y = 0; y < sz; ++y)
            for (int x = 0; x < sz; ++x)
                p[y * pitch + x] = stamp;
    }
};
extern FakeMap MAP;

#endif
"""

DRIVER = r"""
#include <cstdio>
#include <cstdlib>
#include <set>
#include <map>
#include <vector>
#include <string>
#include "ce_terrain_textures_stubs.h"
#include "ce_terrain_textures_math.h"
#include "ce_terrain_textures.h"
#include "terrain_quality.h"
#include "portlog.h"

FakeShell LT;
CTweakSLONG LANDSCAPE_LOWRES{0};
SINT MIP_OFFSETS[16] = {};
CDamage CDXLandscape::mDamage;
FakeEngine ENGINE;
FakeMap MAP;

static bool gExtended = false;
static bool gAllowed = true;
static bool gSuspended = false;
static int gTextureCounts[7] = {};
static int gPatchCounts[4] = {};

bool CeTerrainQualityExtended() { return gExtended; }
bool CeTerrainQualityResourcesAllowed() { return gAllowed; }
void CeTerrainQualityTexture(int level) { if (level >= 0 && level < 7) ++gTextureCounts[level]; }
void CeTerrainQualityPatch(int level) { if (level >= 0 && level < 4) ++gPatchCounts[level]; }
extern "C" bool PortCeWorldViewCaptureSuspended() { return gSuspended; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}

static int gFailures = 0;
static void Check(bool cond, const char *msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++gFailures;
    }
}

static void TestMathAndZeroWrap() {
    Check(CeTerrainMaxAtlasSize(4096, 4096) == 2048u, "4096 caps -> 2048 max atlas");
    Check(CeTerrainMaxAtlasSize(2048, 2048) == 2048u, "2048 caps -> 2048 max atlas");
    Check(CeTerrainMaxAtlasSize(2048, 1024) == 1024u, "2048x1024 -> 1024 max atlas");
    Check(CeTerrainMaxAtlasSize(512, 512) == 0u, "512 -> retail fallback");

    // 2048 atlas dimensions & UV scales.
    Check(CeTerrainLevelAtlasPx(1, 2048u) == 1024u && CeTerrainLevelTilesPerAxis(1, 2048u) == 64, "L1 1024 whole map");
    Check(CeTerrainLevelAtlasPx(2, 2048u) == 2048u && CeTerrainLevelTilesPerAxis(2, 2048u) == 64, "L2 2048 whole map");
    Check(CeTerrainLevelAtlasPx(3, 2048u) == 2048u && CeTerrainLevelTilesPerAxis(3, 2048u) == 32, "L3 2048 32x32");
    Check(CeTerrainLevelAtlasPx(4, 2048u) == 2048u && CeTerrainLevelTilesPerAxis(4, 2048u) == 16, "L4 2048 16x16");
    Check(CeTerrainLevelUvScale(1, 2048u) == 1.0f / 512.0f, "L1 UV scale 1/512");
    Check(CeTerrainLevelUvScale(2, 2048u) == 1.0f / 512.0f, "L2 UV scale 1/512");
    Check(CeTerrainLevelUvScale(3, 2048u) == 1.0f / 256.0f, "L3 UV scale 1/256");
    Check(CeTerrainLevelUvScale(4, 2048u) == 1.0f / 128.0f, "L4 UV scale 1/128");

    // Exhaustive toroidal zero-wrap proof across cameras (Commander overhead + Battle Engine low-angle + corners/edges).
    struct Cam { float x, y, z, dx, dy, dz; };
    const Cam cams[] = {
        {256.f, 256.f, -75.f, 0.4f, 0.4f, 0.82f},    // Commander overhead snow view
        {12.f, 12.f, -65.f, 0.35f, 0.35f, 0.87f},    // Corner overhead
        {500.f, 500.f, -90.f, -0.4f, -0.3f, 0.86f},  // Far corner overhead
        {256.f, 120.f, -14.f, 0.f, 1.f, 0.05f},      // Low-angle Battle Engine north
        {8.f, 8.f, -12.f, 0.707f, 0.707f, 0.f},      // Map corner diagonal
        {480.f, 32.f, -16.f, -0.95f, 0.31f, -0.1f},  // Looking slightly upward
    };
    for (unsigned maxAtlas : {1024u, 2048u}) {
        for (const Cam &c : cams) {
            const CeTerrainViewFocus f = CeTerrainComputeFocus(c.x, c.y, c.z, c.dx, c.dy, c.dz, -10.f, 5, 5, maxAtlas);
            std::map<int, UWORD> usedSlots[5];
            int counts[5] = {};
            for (int ty = 0; ty < 64; ++ty) {
                for (int tx = 0; tx < 64; ++tx) {
                    const int lvl = CeTerrainChooseLevel(f, tx, ty, -10.f);
                    ++counts[lvl];
                    if (lvl < 1) continue;
                    const int n = CeTerrainLevelTilesPerAxis(lvl, maxAtlas);
                    for (int dy = -1; dy <= 1; ++dy) {
                        const int ny = ty + dy;
                        if (ny < 0 || ny >= 64) continue;
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = tx + dx;
                            if (nx < 0 || nx >= 64) continue;
                            const int slot = CeTerrainAtlasSlot(nx, ny, n);
                            const UWORD tile = static_cast<UWORD>((ny << 6) | nx);
                            auto it = usedSlots[lvl].find(slot);
                            if (it != usedSlots[lvl].end() && it->second != tile) {
                                Check(false, "toroidal wrap collision detected between tiles/halos in same frame");
                                return;
                            }
                            usedSlots[lvl][slot] = tile;
                        }
                    }
                }
            }
            if (maxAtlas == 2048u && c.z == -75.f) {
                // Commander overhead at alt 65m must keep high-res L4/L3 tiles (retail collapsed to L1/L0!).
                Check(counts[4] > 20 && counts[3] > 200, "Commander overhead view selects crisp L4 and L3 tiles");
                Check(counts[0] == 0, "Commander overhead view never drops patched tiles to 8px/tile L0");
            }
        }
    }
}

static void TestLifecycleAndHalo() {
    LT = FakeShell{};
    MAP = FakeMap{};
    gExtended = false;
    gAllowed = true;
    gSuspended = false;
    LANDSCAPE_LOWRES.mValue = 0;

    int levels[1] = {5};
    CeTerrainTexturesInit(1, levels);
    // Normal mode allocates ZERO textures.
    Check(!CeTerrainTexturesBeginView(0, {256.f, 256.f, -75.f}, {0.f, 0.5f, 0.86f}, 5, 5, false), "Normal mode stays inactive");
    Check(LT.liveDefault == 0 && LT.liveSysmem == 0 && LT.createCalls == 0, "Normal mode allocates zero textures");

    // Enable Extended: lazy allocates 4 atlases (L1..L4) = 4 default + 4 sysmem.
    gExtended = true;
    Check(CeTerrainTexturesBeginView(0, {256.f, 256.f, -75.f}, {0.f, 0.5f, 0.86f}, 5, 5, false), "Extended mode activates");
    Check(LT.liveDefault == 4 && LT.liveSysmem == 4, "4 levels allocated (L1..L4)");

    // Queue a single tile (32, 32) at L3: verify all 9 tiles (center + 8 halo neighbors) are rendered and uploaded in ONE UpdateTexture!
    const int updatesBefore = LT.dev.updateTextureCalls;
    MAP.rendered.clear();
    CeTerrainTexturesQueueTile(0, 3, static_cast<UWORD>((32 << 6) | 32), 2);
    CeTerrainTexturesFlush(0);
    Check(LT.dev.updateTextureCalls == updatesBefore + 1, "batched single UpdateTexture per dirty atlas");
    std::set<UWORD> got;
    for (const auto &entry : MAP.rendered) {
        Check(entry.first == 3, "rendered at L3");
        got.insert(entry.second);
    }
    Check(got.size() == 9u, "1-tile seam-prevention halo populated center + all 8 neighbors");
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            Check(got.count(static_cast<UWORD>(((32 + dy) << 6) | (32 + dx))) == 1u, "exact halo neighbor populated");

    // Check draw list & UV scale.
    int count = 0;
    const UWORD *tiles = CeTerrainTexturesDrawTiles(0, 3, &count);
    Check(count == 1 && tiles && tiles[0] == static_cast<UWORD>((32 << 6) | 32), "draw tile recorded");
    Check(CeTerrainTexturesUvScale(0, 3) == 1.0f / 256.0f, "runtime L3 UV scale");
    Check(CeTerrainTexturesGetTexture(0, 3) != nullptr, "runtime L3 GPU texture bound");

    // Second frame with same tile: 100% cache hit -> 0 renders, 0 UpdateTexture calls.
    const int rendersBefore = MAP.renderCalls;
    const int upsBefore2 = LT.dev.updateTextureCalls;
    Check(CeTerrainTexturesBeginView(0, {256.f, 256.f, -75.f}, {0.f, 0.5f, 0.86f}, 5, 5, false), "second frame active");
    CeTerrainTexturesQueueTile(0, 3, static_cast<UWORD>((32 << 6) | 32), 2);
    CeTerrainTexturesFlush(0);
    Check(MAP.renderCalls == rendersBefore && LT.dev.updateTextureCalls == upsBefore2, "cache hit performs zero re-renders or uploads");

    // InvalidateTile marks stale -> re-renders ONLY the invalidated tile on next frame!
    CeTerrainTexturesInvalidateTile(static_cast<UWORD>((32 << 6) | 32));
    MAP.rendered.clear();
    Check(CeTerrainTexturesBeginView(0, {256.f, 256.f, -75.f}, {0.f, 0.5f, 0.86f}, 5, 5, false), "post-invalidate frame active");
    CeTerrainTexturesQueueTile(0, 3, static_cast<UWORD>((32 << 6) | 32), 2);
    CeTerrainTexturesFlush(0);
    Check(MAP.rendered.size() == 1u && MAP.rendered[0].second == static_cast<UWORD>((32 << 6) | 32), "invalidation re-renders only stale tile");

    // Mirror pass and suspended cockpit pass stay inactive and never clobber atlases.
    Check(!CeTerrainTexturesBeginView(0, {10.f, 10.f, -10.f}, {1.f, 0.f, 0.f}, 5, 5, true), "mirror pass stays inactive");
    gSuspended = true;
    Check(!CeTerrainTexturesBeginView(0, {10.f, 10.f, -10.f}, {1.f, 0.f, 0.f}, 5, 5, false), "suspended cockpit pass stays inactive");
    gSuspended = false;

    // Aborted Flush (missing mapTex) must clear queuedTile so a later retry uploads every tile + halo.
    {
        Check(CeTerrainTexturesBeginView(0, {256.f, 256.f, -75.f}, {0.f, 0.5f, 0.86f}, 5, 5, false), "active before aborted mapTex flush");
        const UWORD freshTile = static_cast<UWORD>((20 << 6) | 20);
        const int beforeAbort = MAP.renderCalls;
        ENGINE.mapTexNull = true;
        CeTerrainTexturesQueueTile(0, 4, freshTile, 2);
        CeTerrainTexturesFlush(0);
        Check(MAP.renderCalls == beforeAbort, "aborted flush with null mapTex renders nothing");
        ENGINE.mapTexNull = false;
        Check(CeTerrainTexturesBeginView(0, {256.f, 256.f, -75.f}, {0.f, 0.5f, 0.86f}, 5, 5, false), "active for retry after null mapTex");
        CeTerrainTexturesQueueTile(0, 4, freshTile, 2);
        CeTerrainTexturesFlush(0);
        Check(MAP.renderCalls == beforeAbort + 9, "retry after aborted mapTex flush uploads tile + 8-neighbor halo (no wedged slots)");
    }

    // Device loss & restore while dirty slots are queued must clear queuedTile/dirtyCount so pre-loss queues do not leak across reset and post-restore retry uploads cleanly.
    {
        Check(CeTerrainTexturesBeginView(0, {256.f, 256.f, -75.f}, {0.f, 0.5f, 0.86f}, 5, 5, false), "active before device-loss abort");
        const UWORD lossTile = static_cast<UWORD>((25 << 6) | 25);
        const int beforeLoss = MAP.renderCalls;
        CeTerrainTexturesQueueTile(0, 4, lossTile, 2);
        LT.InvalidateAll();
        Check(LT.liveDefault == 0 && LT.liveSysmem == 4, "InvalidateDeviceObjects releases all GPU atlases while keeping sysmem");
        Check(!CeTerrainTexturesActive(0), "view inactive while device lost");
        const int upsBeforeRestore = LT.dev.updateTextureCalls;
        LT.RestoreAll();
        Check(LT.liveDefault == 4 && LT.liveSysmem == 4, "RestoreDeviceObjects recreates all GPU atlases");
        Check(LT.dev.updateTextureCalls == upsBeforeRestore + 4, "RestoreDeviceObjects re-uploads all 4 atlases from sysmem");
        Check(CeTerrainTexturesBeginView(0, {256.f, 256.f, -75.f}, {0.f, 0.5f, 0.86f}, 5, 5, false), "active after device restore");
        CeTerrainTexturesFlush(0);
        Check(MAP.renderCalls == beforeLoss, "pre-loss queued slots cleared on InvalidateDeviceObjects (no stale flush after restore)");
        CeTerrainTexturesQueueTile(0, 4, lossTile, 2);
        CeTerrainTexturesFlush(0);
        Check(MAP.renderCalls == beforeLoss + 9, "retry after device-loss abort uploads tile + 8-neighbor halo (no wedged slots)");
    }

    // Shutdown releases everything cleanly.
    CeTerrainTexturesShutdown();
    Check(LT.liveDefault == 0 && LT.liveSysmem == 0 && LT.objects.empty(), "Shutdown releases all textures and unregisters DeviceObject");

    // Allocation failure rollback: partial creation frees everything and falls back cleanly.
    LT = FakeShell{};
    LT.failCreateAt = 4;
    CeTerrainTexturesInit(1, levels);
    Check(!CeTerrainTexturesBeginView(0, {256.f, 256.f, -75.f}, {0.f, 0.5f, 0.86f}, 5, 5, false), "allocation failure falls back cleanly");
    Check(LT.liveDefault == 0 && LT.liveSysmem == 0, "partial allocation fully rolled back");
    CeTerrainTexturesShutdown();

    // Faithful/fenced mode: ResourcesAllowed == false allocates nothing and stays inactive.
    LT = FakeShell{};
    gAllowed = false;
    CeTerrainTexturesInit(1, levels);
    Check(!CeTerrainTexturesBeginView(0, {256.f, 256.f, -75.f}, {0.f, 0.5f, 0.86f}, 5, 5, false), "faithful/fenced stays inactive");
    Check(LT.liveDefault == 0 && LT.liveSysmem == 0 && LT.createCalls == 0, "faithful/fenced allocates nothing");
    CeTerrainTexturesShutdown();
}

int main() {
    TestMathAndZeroWrap();
    TestLifecycleAndHalo();
    if (gFailures) return 1;
    std::puts("PASS");
    return 0;
}
"""


class TerrainTexturesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("g++") or shutil.which("clang++") or shutil.which("c++")
        if not cls.compiler:
            raise RuntimeError("a C++ compiler is required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="ce-terrain-textures-")
        cls.dir = Path(cls.tmp.name)
        (cls.dir / "platform").mkdir()
        os.symlink(COMMON, cls.dir / "platform/common")
        (cls.dir / "ce_terrain_textures_stubs.h").write_text(STUB_TYPES)
        for hdr in ("common.h", "DX.h", "ltshell.h", "tweak.h", "landscape.h", "DXLandscape.h",
                    "LandscapeTexture.h", "engine.h", "map.h", "maptex.h", "maths.h"):
            (cls.dir / hdr).write_text('#include "ce_terrain_textures_stubs.h"\n')
        (cls.dir / "driver.cpp").write_text(DRIVER)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def build_and_run(self, name: str, math_text: str | None = None, impl_text: str | None = None) -> tuple[bool, int, str]:
        work = self.dir / name
        work.mkdir(exist_ok=True)
        (work / "ce_terrain_textures_math.h").write_text(
            math_text if math_text is not None else (GAME / "ce_terrain_textures_math.h").read_text()
        )
        (work / "ce_terrain_textures.h").write_text((GAME / "ce_terrain_textures.h").read_text())
        impl = work / "ce_terrain_textures.cpp"
        impl.write_text(impl_text if impl_text is not None else (GAME / "ce_terrain_textures.cpp").read_text())
        exe = work / "test_bin"
        built = subprocess.run(
            [self.compiler, "-std=c++14", "-O2", "-Wall", "-Wextra", "-Werror",
             "-I", str(work), "-I", str(self.dir), "-I", str(COMMON),
             str(impl), str(self.dir / "driver.cpp"), "-o", str(exe)],
            capture_output=True, text=True, timeout=45,
        )
        if built.returncode != 0:
            return False, built.returncode, built.stderr
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
        return True, ran.returncode, ran.stdout + ran.stderr

    def test_production_math_halo_and_lifecycle(self):
        ok, rc, out = self.build_and_run("production")
        self.assertTrue(ok and rc == 0, f"production failed: compiled={ok} rc={rc}\n{out}")
        self.assertEqual(out, "PASS\n")

    def test_patch_leaves_faithful_build_unchanged(self):
        with tempfile.TemporaryDirectory(prefix="terrain-tex-patch-") as td:
            base = Path(td)
            with_p = patched(base / "with", ("DXLandscape.cpp",))["DXLandscape.cpp"]
            without_p = patched(base / "without", ("DXLandscape.cpp",), skip=("0354-ce-terrain-textures.patch",))["DXLandscape.cpp"]
        self.assertNotEqual(with_p, without_p)
        self.assertEqual(faithful(with_p), faithful(without_p))

    def test_causal_mutants_are_caught(self):
        math_orig = (GAME / "ce_terrain_textures_math.h").read_text()
        impl_orig = (GAME / "ce_terrain_textures.cpp").read_text()
        mutants = [
            ("oversized-safe-half-wraps", "math", "const int half = (n / 2) - 2;", "const int half = n / 2;"),
            ("commander-l4-blocked", "math", "dist2 <= 90.0f * 90.0f", "dist2 <= 16.0f * 16.0f"),
            ("drop-seam-halo", "impl", "for (int dy = -1; dy <= 1; ++dy)", "for (int dy = 0; dy <= 0; ++dy)"),
            ("drop-invalidate", "impl", "a.cache[slot] |= 0x8000u;", "(void)slot;"),
            ("leak-gpu-on-device-loss", "impl", "ReleaseAtlasGpu(gViews[v].atlases[l]);", "(void)l;"),
            ("active-on-mirror", "impl", "|| mirrorPass ||", "|| (false && mirrorPass) ||"),
            ("active-when-suspended", "impl", "        PortCeWorldViewCaptureSuspended()) {", "        false) {"),
            ("no-rollback-on-alloc-fail", "impl", "if (!ok) {\n        DestroyResources();", "if (!ok) {\n        gPrepared = false;"),
            ("wedge-slot-on-aborted-flush", "impl", "if (!mapTex) {\n        ClearDirtyQueue(a);", "if (!mapTex) {\n        a.dirtyCount = 0;"),
            ("wedge-slot-on-invalidate", "impl", "ClearDirtyQueue(gViews[v].atlases[l]);", "(void)l;"),
        ]
        for label, which, old, new in mutants:
            with self.subTest(mutant=label):
                src = math_orig if which == "math" else impl_orig
                self.assertEqual(src.count(old), 1, f"anchor mismatch for {label}")
                ok, rc, out = self.build_and_run(
                    label,
                    math_text=src.replace(old, new) if which == "math" else None,
                    impl_text=src.replace(old, new) if which == "impl" else None,
                )
                self.assertTrue(ok, f"mutant {label} failed to compile:\n{out}")
                self.assertNotEqual(rc, 0, f"mutant {label} was NOT caught!")


if __name__ == "__main__":
    unittest.main(verbosity=1)
