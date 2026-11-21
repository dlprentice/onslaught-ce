#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The shore surf is drawn with the coordinate set its buffers were built with, on every water path.

The game lays a surf strip's two texture-coordinate sets out one way or the other by the capability tweak when a level
loads (the wave coordinates first when it is set), and the water pass says which set feeds which texture stage when it
draws. Ordinary CE sets that tweak on every capable device (port/game/ce_graphics_policy.cpp), whether or not the extra
graphical features are on, so both layouts are reachable by every water path: the finest fixed-function method, and the
plain pass that draws the surf when the device took a lower water or surf method.

This compiles the game's actual functions, whole, as the port's patches leave them (the surf's loader, the strip
builder, the surf's draw and the whole water pass), against authored device boundaries, and for each tweak value and
each water and surf method reads back, at every surf draw, which coordinate set the stage holding the waves texture
uses: it must be the strip's wave coordinates, never its world position. The shader path has no stage mapping and is
not exercised. A build without CE_VERSION keeps the original line and is run with the tweak clear only, as the game
leaves it there.

    python3 port/tools/ce_surf_coordinates_tests.py [--causal-controls]

Needs the dependency cache's Direct3D headers (BEA_PORT_DATA). No game data, device or display.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import resource
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
# (tweak, water method, surf method): the device accepted the finest methods (2, 1) or stepped down
METHODS = ((2, 1), (2, 0), (1, 1), (1, 0), (0, 1), (0, 0))

DRIVER = r'''// SPDX-License-Identifier: GPL-3.0-only
// Authored boundaries for the game's actual surf and water functions (ce_surf_coordinates_tests.py writes this file).
#include <windows.h>
#include <d3d9types.h>
#include <d3d9caps.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

typedef int SINT;
#define PI 3.14159265f
#define DIK_F1 0x3B
namespace { int failures; void Check(bool ok, const char *label) { if (!ok) { ++failures; std::cout << "FAIL " << label << '\n'; } } }

// The capability tweak at the game's own initial value; main sets it as the device hook does, before the level loads.
struct CTweakSLONG { int mValue; explicit CTweakSLONG(int value) : mValue(value) {} };
CTweakSLONG GEFORCE_FX_POWER(0);
struct CRenderMethod
{
    int mMethod; BOOL mAccepted;
    CRenderMethod(const char *, int count) : mMethod(count - 1), mAccepted(FALSE) {}
    bool Try(int method) { return mMethod == method; }
    bool Validate() { return true; }
    void Accept() { mAccepted = TRUE; }
    int GetMethod() { return mMethod; }
    void BeginVBufCheck() {}
    bool CheckVBufValid() { return true; }
    void SetMethod(int method) { mMethod = method; }
};
CRenderMethod WATER_METHOD("water_method", 3), SUN_METHOD("sun_method", 2), SURF_METHOD("surf_method", 2);
bool WATER_USE_SHADER, gMirrorPass;

enum { MEMTYPE_VBUFFER = 44, MEMTYPE_LANDSCAPE = 1 };
void *operator new(size_t size, int) { return ::operator new(size); }
void *operator new[](size_t size, int) { return ::operator new[](size); }

struct CVBuffer
{
    std::vector<unsigned char> data;
    static CVBuffer *bound;
    void Create(int count, int size, DWORD) { data.assign(static_cast<size_t>(count) * size, 0); }
    void Lock(void **out) { *out = data.data(); }
    void Unlock() {}
    void SetStreamSource(int) { bound = this; }
    void SetStreamSourceOnly(int) { bound = this; }
};
CVBuffer *CVBuffer::bound;
struct CDXTexture { void *GetTexture() { return this; } };
// The level's surf chunk: four-byte items, as the loader reads them.
struct CChunkReader
{
    std::vector<unsigned> items; size_t at = 0;
    void Int(int value) { items.push_back(static_cast<unsigned>(value)); }
    void Float(float value) { unsigned bits; memcpy(&bits, &value, 4); items.push_back(bits); }
    int GetSize() { return static_cast<int>(items.size() * 4); }
    void GetNext() {}
    void Read(void *out, int size, int count) { Check(size == 4 && count == 1 && at < items.size(), "the loader reads one four-byte item"); memcpy(out, &items[at++], 4); }
};

struct FVector { float X, Y, Z; };
struct FMatrix { FVector GetColumnY() { return FVector{0, 1, 0}; } };
FVector ZERO_FVECTOR; FMatrix ID_FMATRIX;
struct D3DXVECTOR2
{
    float x, y;
    D3DXVECTOR2() : x(0), y(0) {}
    D3DXVECTOR2(float a, float b) : x(a), y(b) {}
    D3DXVECTOR2 operator+(const D3DXVECTOR2 &o) const { return D3DXVECTOR2(x + o.x, y + o.y); }
    D3DXVECTOR2 operator-(const D3DXVECTOR2 &o) const { return D3DXVECTOR2(x - o.x, y - o.y); }
    D3DXVECTOR2 operator*(float f) const { return D3DXVECTOR2(x * f, y * f); }
};
struct AuthoredCamera { FVector GetPos() { return FVector{10, 20, 30}; } FMatrix GetOrientation() { return FMatrix(); } };
struct AuthoredLandscape { void *mMirrorTexture = nullptr; };
struct { AuthoredCamera camera; AuthoredLandscape landscape; int mCurrentViewpoint = 0;
         AuthoredCamera *GetCamera() { return &camera; } AuthoredLandscape *GetLandscape() { return &landscape; } } ENGINE;
struct { bool IsMultiplayer() { return false; } float GetRenderFrameLength() { return 1.f / 30; } } GAME;
struct { bool KeyOn(int) { return false; } } PLATFORM;
struct { struct { float SunPosX = 0, SunPosY = 0; } mHFP;
         DWORD GetWaterColour() { return 0x406080; } float GetFogDensity() { return 0; } DWORD GetFog() { return 0; }
         DWORD GetSunReflectColour() { return 0; } } MAP;
struct { int mFindBadWater = 0, mFindGoodWater = 0; } CLIPARAMS;
struct AuthoredRenderInfo { template<class... A> void SetLightingEnabled(A...) {} template<class... A> void SetWorld(A...) {} template<class... A> void SetDepthBias(A...) {}
         template<class... A> void SetFVF(A...) {} template<class... A> void Apply(A...) {} } RENDERINFO;

// One surf draw: the stage the surf's draw bound the waves texture to, the coordinate set every stage used, the buffer drawn.
struct Draw { int wavesStage; DWORD sets[4]; CVBuffer *buffer; };
std::vector<Draw> draws;
CDXTexture waves, caustic, reflection, sunReflect, sunBlob;
struct AuthoredDevice
{
    template<class... A> HRESULT SetTransform(A...) { return 0; }
    template<class... A> HRESULT SetPixelShader(A...) { return 0; }
    template<class... A> HRESULT SetVertexShaderConstantF(A...) { return 0; }
    template<class... A> HRESULT SetPixelShaderConstantF(A...) { return 0; }
    template<class... A> HRESULT DrawPrimitiveUP(A...) { return 0; }
    HRESULT DrawPrimitive(int, unsigned, unsigned);
};
struct AuthoredShell
{
    D3DCAPS9 m_d3dCaps;
    AuthoredDevice device;
    DWORD sets[8] = {0, 1, 2, 3, 4, 5, 6, 7};	// Direct3D's initial coordinate index of each stage
    int wavesStage = -1;
    AuthoredDevice *GetDevice() { return &device; }
    void STS(int stage, int state, DWORD value) { if (state == D3DTSS_TEXCOORDINDEX) sets[stage] = value; }
    void D3D_SetTexture(int stage, void *texture) { if (texture == &waves) wavesStage = stage; else if (wavesStage == stage) wavesStage = -1; }
    HRESULT D3D_ValidateDevice() { return 0; }
    template<class... A> void SRS(A...) {}
    template<class... A> void ForceRS(A...) {}
    template<class... A> void SSS_NoCache(A...) {}
    template<class... A> void SetFogMode(A...) {}
    template<class... A> void SetFVF(A...) {}
    template<class... A> void SetVertexShader(A...) {}
    template<class... A> void SetAlphaRef(A...) {}
} LT;
HRESULT AuthoredDevice::DrawPrimitive(int type, unsigned, unsigned)
{
    Check(type == D3DPT_TRIANGLESTRIP, "the surf is drawn as triangle strips");
    draws.push_back(Draw{LT.wavesStage, {LT.sets[0], LT.sets[1], LT.sets[2], LT.sets[3]}, CVBuffer::bound});
    return 0;
}
D3DMATRIX gTextureMatrix;
DWORD FtoDW(float value) { DWORD bits; memcpy(&bits, &value, 4); return bits; }
float GlintHeight(float value) { return value; }
void SetMipFilterByGlobalToggle(int) {}
void AuthoredSleep(int) {}
#define Sleep AuthoredSleep
static void PortWaterRenderPath(int) {}
struct AuthoredWaterBuffer { template<class... A> void DrawIndexed(A...) {} template<class... A> void DWATS(A...) {} };
struct AuthoredWaterShader { void SetConstants() {} };
struct CDXWater
{
    CDXTexture *mCaustic = &caustic, *mReflection = &reflection, *mSunReflect = &sunReflect, *mSunBlob = &sunBlob;
    AuthoredWaterBuffer buffer, *mVB = &buffer;
    DWORD mGridColour = 0;
    static void *sPixelShader;
    static AuthoredWaterShader *sShader;
    void BuildGrid(float, DWORD) {}
    void Render(float wlevel);
};
void *CDXWater::sPixelShader;	// no shader objects: the fixed-function stage mapping is what these cases read
AuthoredWaterShader *CDXWater::sShader;

#define DXSURF_CPP
#define DXWATER_CPP
#include "surf.h"
CDXSurf SURF;
#include "ce_surf_actual.inc"

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    const int tweak = atoi(argv[1]), water = atoi(argv[2]), surf = atoi(argv[3]);
    memset(&LT.m_d3dCaps, 0, sizeof LT.m_d3dCaps);
    LT.m_d3dCaps.TextureOpCaps = D3DTEXOPCAPS_MULTIPLYADD;
    GEFORCE_FX_POWER.mValue = tweak;	// the device exists before a level loads
    // A level's surf: two strips of three and two points, every position far from any wave coordinate.
    CChunkReader chunk;
    const int counts[] = {3, 2};
    chunk.Int(2);
    float next = 0;
    for (int count : counts) {
        chunk.Int(count);
        for (int step = 0; step <= count; ++step)
            for (int vertex = 0; vertex < 3; ++vertex) { chunk.Float(1000 + next); chunk.Float(2000 + next); chunk.Float(5); next += 7; }
    }
    SURF.Deserialize(&chunk);
    Check(chunk.at == chunk.items.size() && SURF.mNumStrips == 2, "the actual loader read the whole authored surf");
    SURF.mWaves = &waves;
    WATER_METHOD.mMethod = water; WATER_METHOD.mAccepted = TRUE;
    SURF_METHOD.mMethod = surf; SURF_METHOD.mAccepted = TRUE;
    CDXWater pass;
    pass.Render(4.f);
    Check(draws.size() == 4, "each strip's two buffers are drawn once");
    const bool finest = water == 2 && surf == 1;
    for (const Draw &draw : draws) {
        Check(draw.wavesStage == (finest ? 3 : 0), "the waves texture is on the stage its pass uses");
        if (draw.wavesStage < 0 || !draw.buffer) continue;
        const SSurfVertex *v = reinterpret_cast<const SSurfVertex *>(draw.buffer->data.data());
        const size_t vertices = draw.buffer->data.size() / sizeof(SSurfVertex);
        bool wavesRight = vertices > 0, worldRight = true;
        for (size_t i = 0; i < vertices; ++i) {
            const float sets[2][2] = {{v[i].mU0, v[i].mV0}, {v[i].mU1, v[i].mV1}};
            const DWORD used = draw.sets[draw.wavesStage];
            // the position's own X and Y are the world coordinates; the other set is the wave's
            if (used > 1 || (sets[used][0] == v[i].mX && sets[used][1] == v[i].mY) || sets[1 - used][0] != v[i].mX || sets[1 - used][1] != v[i].mY) wavesRight = false;
            if (finest) for (int stage = 0; stage < 3; ++stage)
                if (draw.sets[stage] > 1 || sets[draw.sets[stage]][0] != v[i].mX || sets[draw.sets[stage]][1] != v[i].mY) worldRight = false;
        }
        Check(wavesRight, "the waves texture is fed the strip's wave coordinates, not its world position");
        Check(worldRight, "the finest method's other three stages are fed the world position");
    }
    Check(LT.sets[0] == 0, "the pass leaves stage 0 on coordinate set 0");
    std::cout << "surf-coordinates tweak=" << tweak << " water=" << water << " surf=" << surf << " draws=" << draws.size() << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
'''


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def run(command, expected=0):
    def bounded():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    env = os.environ.copy()
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
        env.pop(name, None)
    result = subprocess.run([str(x) for x in command], text=True, capture_output=True, env=env, timeout=120, preexec_fn=bounded)
    if result.returncode != expected:
        raise RuntimeError(f"command failed: {command}\n{result.stdout}{result.stderr}")
    return result.stdout


def definition(text, pattern, what):
    found = re.search(pattern, text, flags=re.M | re.S)
    if not found:
        raise AssertionError("the source no longer has " + what)
    return found.group(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--causal-controls", action="store_true")
    args = parser.parse_args()
    build = module("surf_build", PORT / "build.py")
    helpers = module("surf_functions", PORT / "tools/public_d3dapp_tests.py")
    headers = Path(os.environ.get("BEA_PORT_DATA", ROOT / "local-data/port")) / "dxvk-native/usr/include/dxvk"
    if not (headers / "d3d9types.h").is_file():
        raise SystemExit(f"{headers}: no Direct3D headers; set BEA_PORT_DATA to a dependency cache")
    out = ROOT / "local-data/surf-coordinates-tests"
    out.mkdir(parents=True, exist_ok=True)
    texts = build.patched_text(["DXSurf.cpp", "DXWater.cpp", "surf.h"])
    surf, water = texts["DXSurf.cpp"], texts["DXWater.cpp"]
    # Whole definitions, in the order the driver needs them: nothing of a function is left out.
    actual = definition(surf, r"^struct SSurfVertex\n\{.*?^\};\n", "the surf vertex") + "\n"
    actual += definition(surf, r"^#define SURF_FVF[^\n]*\n", "the surf vertex format") + "\n"
    for pattern in (r"^CDXSurf::CDXSurf\s*\(", r"^CDXSurf::~CDXSurf\s*\(", r"^void\s+CDXSurf::Deserialize\s*\(", r"^SSurfStrip::~SSurfStrip\s*\(",
                    r"^void\s+SSurfStrip::Create\s*\(", r"^void\s+CDXSurf::Render\s*\("):
        actual += helpers.function(surf, pattern) + "\n"
    actual += definition(water, r"^#define WATER_ADVANCE\(scroll, angle\)(?:[^\n]*\\\n)+[^\n]*\n", "the water's scroll step") + "\n"
    actual += definition(water, r"^static const float\s+WATER_PS_CONSTANTS\[8\]\[4\] =\n\{.*?^\};\n", "the water shader's constants") + "\n"
    actual += definition(water, r"^struct SSunVertex\n\{.*?^\};\n", "the sun glint's vertex") + "\n"
    actual += helpers.function(water, r"^void\s+CDXWater::Render\s*\(") + "\n"
    # The lines under test must be in what was extracted: the strip's layout by the tweak, and both places the pass maps the sets.
    for needed, count in (("if (GEFORCE_FX_POWER.mValue)", 4), ("if (!WATER_USE_SHADER && GEFORCE_FX_POWER.mValue)", 2), ("SURF.Render(true);", 1), ("SURF.Render(false);", 1)):
        if actual.count(needed) != count:
            raise AssertionError(f"the extracted text has {actual.count(needed)} of {needed!r}, expected {count}")
    (out / "surf.h").write_text(texts["surf.h"], encoding="latin-1")
    (out / "driver.cpp").write_text(DRIVER)
    common = ["clang++", "-std=c++14", "-fno-strict-aliasing", "-Wno-everything", "-I", headers, "-I", out]

    def compile_one(directory, text, public=True):
        directory.mkdir(exist_ok=True)
        (directory / "ce_surf_actual.inc").write_text(text, encoding="latin-1")
        executable = directory / "driver"
        run(common + (["-DCE_VERSION=1"] if public else []) + ["-I", directory, out / "driver.cpp", "-o", executable])
        return executable

    binary = compile_one(out / "public", actual)
    observations = {}
    for tweak in (0, 1):
        for water_method, surf_method in METHODS:
            observations[f"tweak{tweak}-water{water_method}-surf{surf_method}"] = run([binary, tweak, water_method, surf_method])
    private = compile_one(out / "private", actual, public=False)
    for water_method, surf_method in METHODS:
        observations[f"private-tweak0-water{water_method}-surf{surf_method}"] = run([private, 0, water_method, surf_method])
    controls = {}
    if args.causal_controls:
        plain = "LT.STS(0, D3DTSS_TEXCOORDINDEX, GEFORCE_FX_POWER.mValue ? 0 : 1);"
        changes = {
            # the plain pass with the original's fixed set: wrong whenever the tweak is set
            "plain-pass-ignores-the-layout": (plain, "LT.STS(0, D3DTSS_TEXCOORDINDEX, 1);", (1, 2, 0)),
            "plain-pass-ignores-the-layout-lower-water": (plain, "LT.STS(0, D3DTSS_TEXCOORDINDEX, 1);", (1, 1, 1)),
            "plain-pass-takes-the-other-set": (plain, "LT.STS(0, D3DTSS_TEXCOORDINDEX, GEFORCE_FX_POWER.mValue ? 1 : 0);", (0, 0, 0)),
            # the finest method without its own swap, and a strip built the other way round
            "finest-pass-never-swaps": ("if (!WATER_USE_SHADER && GEFORCE_FX_POWER.mValue)", "if (false)", (1, 2, 1)),
            "strip-built-one-way-only": ("if (GEFORCE_FX_POWER.mValue)", "if (false)", (1, 2, 1)),
        }
        for name, (old, new, case) in changes.items():
            if old not in actual:
                raise AssertionError("causal anchor absent: " + name)
            controls[name] = run([compile_one(out / name, actual.replace(old, new))] + list(case), expected=1)
    receipt = {"actual_sha256": hashlib.sha256(actual.encode("latin-1")).hexdigest(),
               "patch_sha256": hashlib.sha256((PORT / "patches/0204-ce-graphics-policy.patch").read_bytes()).hexdigest(),
               "observations": observations, "controls": controls,
               "limits": "Actual loader, strip builder, surf draw and water pass with authored device boundaries; fixed-function stage mapping only, no shader path, no GPU."}
    (out / "receipt.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"surf coordinates: {len(observations)} cases passed; {len(controls)} causal controls detected")


if __name__ == "__main__":
    main()
