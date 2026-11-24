#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify HD water reflections (0367), HD dynamic unit shadows (0368), hardware caps,
rollback ladders, shader tap offsets, faithful fences, patch neutrality, and causal mutants.
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
COMMON = PORT / "platform/common"

sys.path.insert(0, str(PORT / "tools"))
import public_d3dapp_tests  # noqa: E402
from ce_photo_tests import faithful, patched  # noqa: E402

POLICY_DRIVER = r"""
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "reflection_quality.h"
#include "portlog.h"

static bool gLayer = true;
bool PortCeLayer() { return gLayer; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}
extern "C" uint64_t SDL_GetTicks() { return 1000; }
extern "C" bool SDL_OpenURL(const char *) { return false; }
extern "C" int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }

static int gFailures = 0;
static void Check(bool cond, const char *msg) {
    if (!cond) {
        std::fprintf(stderr, "graphics-r2 assertion: %s\n", msg);
        ++gFailures;
    }
}
static bool Near(float a, float b) { return std::fabs(a - b) < 1e-7f; }

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    if (mode == "layer-off") gLayer = false;

    if (mode == "default") {
        Check(CE_WaterReflectionResChoice() == 512 && CE_WaterReflectionChoice() == 1, "fresh water reflection default is 512");
        Check(CE_DynamicShadowResChoice() == 128 && CE_DynamicShadowChoice() == 1, "fresh dynamic shadow default is 128");
        CeWaterReflectionBeginFrame();
        CeDynamicShadowBeginFrame();
        Check(CeWaterReflectionQualityActive() && CeWaterReflectionTargetRes() == 512u, "water target 512 active");
        Check(CeDynamicShadowQualityActive() && CeDynamicShadowTargetRes() == 128u, "shadow target 128 active");
        Check(CeWaterReflectionActiveRes() == 256u, "unallocated water defaults safely to 256");
        Check(CeDynamicShadowActiveRes() == 64u, "unallocated shadow defaults safely to 64");
        CeWaterReflectionSetAllocatedRes(512u, 512u);
        CeDynamicShadowSetAllocatedRes(128u, 128u);
        Check(CeWaterReflectionActiveRes() == 512u && CE_WaterReflectionAllocatedRes() == 512, "water allocated 512");
        Check(CeDynamicShadowActiveRes() == 128u && CE_DynamicShadowAllocatedRes() == 128, "shadow allocated 128");
        Check(Near(CeWaterReflectionTapOffset(), 1.0f / 256.0f), "512px water tap offset is 2/512");
        Check(Near(CeDynamicShadowTapOffset1(256), 1.0f / 256.0f), "256px shadow tap1 is 1/256");
        Check(Near(CeDynamicShadowTapOffset3(256), 1.0f / 768.0f), "256px shadow tap3 is 1/768");
        Check(Near(CeDynamicShadowTapOffset1(64), 1.0f / 64.0f), "64px shadow tap1 matches retail 1/64");
        Check(Near(CeDynamicShadowTapOffset3(64), 1.0f / 192.0f), "64px shadow tap3 matches retail 1/192");
    } else if (mode == "stored") {
        Check(CE_WaterReflectionResChoice() == 512 && CE_WaterReflectionChoice() == 1, "stored 512 water");
        Check(CE_DynamicShadowResChoice() == 128 && CE_DynamicShadowChoice() == 1, "stored 128 shadow");
        CeWaterReflectionBeginFrame();
        CeDynamicShadowBeginFrame();
        Check(CeWaterReflectionTargetRes() == 512u && CeDynamicShadowTargetRes() == 128u, "stored targets applied");
        CeWaterReflectionSetAllocatedRes(512u, 512u);
        Check(Near(CeWaterReflectionTapOffset(), 1.0f / 256.0f), "512px water tap offset is 2/512");
        Check(Near(CeDynamicShadowTapOffset1(128), 1.0f / 128.0f), "128px shadow tap1 is 1/128");
        Check(Near(CeDynamicShadowTapOffset3(128), 1.0f / 384.0f), "128px shadow tap3 is 1/384");
    } else if (mode == "garbage") {
        Check(CE_WaterReflectionResChoice() == 512, "invalid water setting falls back to 512");
        Check(CE_DynamicShadowResChoice() == 128, "invalid shadow setting falls back to 128");
    } else if (mode == "switch") {
        CE_WaterReflectionRequest(1024);
        CE_DynamicShadowRequest(256);
        CeWaterReflectionBeginFrame();
        CeDynamicShadowBeginFrame();
        CeWaterReflectionSetAllocatedRes(1024u, 1024u);
        CeDynamicShadowSetAllocatedRes(256u, 256u);
        Check(!CeWaterReflectionNeedsRebuild() && !CeDynamicShadowNeedsRebuild(), "no rebuild when target matches attempt");
        Check(CE_WaterReflectionRequest(512) == 1 && CE_DynamicShadowRequest(128) == 1, "accepts valid requests");
        Check(CE_WaterReflectionRequest(768) == 0 && CE_DynamicShadowRequest(96) == 0, "rejects invalid requests");
        // Must remain unchanged until frame boundary!
        Check(CeWaterReflectionTargetRes() == 1024u && CeDynamicShadowTargetRes() == 256u, "immutable mid-frame");
        Check(!CeWaterReflectionNeedsRebuild() && !CeDynamicShadowNeedsRebuild(), "rebuild waits for frame boundary");
        CeWaterReflectionBeginFrame();
        CeDynamicShadowBeginFrame();
        Check(CeWaterReflectionTargetRes() == 512u && CeDynamicShadowTargetRes() == 128u, "switches at frame boundary");
        Check(CeWaterReflectionNeedsRebuild() && CeDynamicShadowNeedsRebuild(), "requests rebuild at frame boundary");
        // Rollback settled: if 512 fails down to 256, must NOT spin rebuilding every frame!
        CeWaterReflectionSetAllocatedRes(256u, 512u);
        CeDynamicShadowSetAllocatedRes(64u, 128u);
        Check(!CeWaterReflectionNeedsRebuild() && !CeDynamicShadowNeedsRebuild(), "settled fallback does not spin");
        Check(CeWaterReflectionActiveRes() == 256u && CeDynamicShadowActiveRes() == 64u, "active res reflects fallback");
        Check(Near(CeWaterReflectionTapOffset(), 1.0f / 128.0f), "256px fallback tap offset matches retail 1/128");
        // Device reset clears allocated state.
        CeWaterReflectionResetAllocated();
        CeDynamicShadowResetAllocated();
        Check(CE_WaterReflectionAllocatedRes() == 0 && CE_DynamicShadowAllocatedRes() == 0, "reset clears allocated res");
    } else if (mode == "caps") {
        CE_WaterReflectionRequest(1024);
        CE_DynamicShadowRequest(256);
        CeRenderTargetQualitySetHardwareCaps(2048u, 512u);
        Check(CeRenderTargetQualityHardwareMaxDim() == 512u, "min(W, H) clamps asymmetric GPU caps");
        CeWaterReflectionBeginFrame();
        CeDynamicShadowBeginFrame();
        Check(CeWaterReflectionTargetRes() == 512u, "water clamps 1024 -> 512 on 512px GPU");
        Check(CeDynamicShadowTargetRes() == 256u, "shadow stays 256 on 512px GPU");
        CeRenderTargetQualitySetHardwareCaps(128u, 128u);
        CeWaterReflectionBeginFrame();
        CeDynamicShadowBeginFrame();
        Check(CeWaterReflectionTargetRes() == 256u, "water clamps to 256 floor");
        Check(CeDynamicShadowTargetRes() == 128u, "shadow clamps 256 -> 128 on 128px GPU");
        Check(CeWaterReflectionNextFallback(1024u) == 512u && CeWaterReflectionNextFallback(512u) == 256u && CeWaterReflectionNextFallback(256u) == 0u, "water fallback ladder");
        Check(CeDynamicShadowNextFallback(256u) == 128u && CeDynamicShadowNextFallback(128u) == 64u && CeDynamicShadowNextFallback(64u) == 0u, "shadow fallback ladder");
    } else if (mode == "fenced" || mode == "layer-off") {
        Check(CE_WaterReflectionResChoice() == 256 && CE_WaterReflectionChoice() == 0, "fenced water stays retail 256");
        Check(CE_DynamicShadowResChoice() == 64 && CE_DynamicShadowChoice() == 0, "fenced shadow stays retail 64");
        Check(CE_WaterReflectionRequest(1024) == 0 && CE_DynamicShadowRequest(256) == 0, "fenced refuses requests");
        CeWaterReflectionBeginFrame();
        CeDynamicShadowBeginFrame();
        Check(!CeWaterReflectionQualityActive() && CeWaterReflectionTargetRes() == 256u && CeWaterReflectionActiveRes() == 256u, "fenced water inactive");
        Check(!CeDynamicShadowQualityActive() && CeDynamicShadowTargetRes() == 64u && CeDynamicShadowActiveRes() == 64u, "fenced shadow inactive");
        Check(Near(CeWaterReflectionTapOffset(), 1.0f / 128.0f), "fenced tap offset equals retail 1/128");
    } else {
        return 2;
    }
    if (gFailures) return 1;
    std::puts("PASS");
    return 0;
}
"""

LANDSCAPE_ROLLBACK_DRIVER = r"""
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "reflection_quality.h"
#include "portlog.h"

bool PortCeLayer() { return true; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}
extern "C" uint64_t SDL_GetTicks() { return 1000; }
extern "C" bool SDL_OpenURL(const char *) { return false; }
extern "C" int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }

struct CTweakSLONG { int mValue; explicit CTweakSLONG(int v = 0) : mValue(v) {} };
CTweakSLONG GEFORCE_FX_POWER(1), EXTRA_GEFORCE_FX_FEATURES(1), R32F_WORKS(1);
CTweakSLONG MY_READABLE_Z_BUFFER(0), OTHER_ZBUFFER_AS_TEXTURE(0);

static bool gPolicyRequested = true;
static int gPolicyOutcome = -1;
bool PortCeGraphicsRequested() { return gPolicyRequested; }
enum { CE_GRAPHICS_LANDSCAPE = 2 };
void PortCeGraphicsResourceOutcome(unsigned, bool ready) { gPolicyOutcome = ready ? 1 : 0; }

using HRESULT = int;
using DWORD = unsigned;
const HRESULT S_OK = 0;
#define SUCCEEDED(x) ((x) >= 0)
#define FAILED(x) ((x) < 0)
using D3DFORMAT = unsigned;
constexpr unsigned D3DFMT_A32B32G32R32F=1, D3DFMT_D16=2, D3DFMT_R32F=3, D3DFMT_R5G6B5=4;
constexpr unsigned D3DMULTISAMPLE_NONE=0, D3DPOOL_DEFAULT=0, D3DUSAGE_DEPTHSTENCIL=1, D3DUSAGE_RENDERTARGET=2;
constexpr bool TRUE = true;
#define D3DPS_VERSION(major, minor) (0xffff0000u | ((major) << 8) | (minor))

static int gLiveTextures = 0, gLiveSurfaces = 0;
static unsigned gFailTexAbove = 99999u, gFailDepthAbove = 99999u;
static unsigned gLastMirrorW = 0, gLastDepthW = 0;

struct Surface {
    unsigned w;
    explicit Surface(unsigned dim) : w(dim) { ++gLiveSurfaces; }
    void Release() { --gLiveSurfaces; delete this; }
};
struct Texture {
    unsigned w;
    bool depthFail;
    Texture(unsigned dim, bool failSurf) : w(dim), depthFail(failSurf) { ++gLiveTextures; }
    void Release() { --gLiveTextures; delete this; }
    HRESULT GetSurfaceLevel(unsigned, Surface **out) {
        if (depthFail) { *out = nullptr; return -1; }
        gLastDepthW = w;
        *out = new Surface(w);
        return S_OK;
    }
};
struct Pixel {} gPixel;
const char *DXGetErrorString9(HRESULT) { return "fault"; }
struct TestGame { bool IsMultiplayer() const { return false; } } GAME;
struct TestHistory { template<class... T> void AddMessage(T...) {} } SETUP_HISTORY;
struct TestDevice {
    struct { unsigned PixelShaderVersion = D3DPS_VERSION(2,0); } m_d3dCaps;
    TestDevice *GetDevice() { return this; }
    HRESULT D3D_CreatePixelShader(const char *, Pixel **out) { *out = &gPixel; return S_OK; }
    HRESULT D3D_CreateTextureNoCheck(unsigned w, unsigned h, unsigned, unsigned usage, D3DFORMAT fmt, unsigned, Texture **out) {
        (void)h; (void)fmt;
        if (usage == D3DUSAGE_DEPTHSTENCIL) {
            if (w > gFailDepthAbove) { *out = nullptr; return -1; }
            *out = new Texture(w, false);
            return S_OK;
        }
        if (usage == D3DUSAGE_RENDERTARGET && fmt == D3DFMT_R5G6B5) {
            if (w > gFailTexAbove) { *out = nullptr; return -1; }
            gLastMirrorW = w;
            *out = new Texture(w, false);
            return S_OK;
        }
        *out = new Texture(w, false);
        return S_OK;
    }
    HRESULT CreateDepthStencilSurface(unsigned w, unsigned h, D3DFORMAT, unsigned, unsigned, bool, Surface **out, void *) {
        (void)h;
        if (w > gFailDepthAbove) { *out = nullptr; return -1; }
        gLastDepthW = w;
        *out = new Surface(w);
        return S_OK;
    }
} LT;

struct CDXLandscape {
    Pixel *mPixelShader = nullptr;
    Texture *mMirrorTexture = nullptr, *mUnknown08 = nullptr, *mDepthTexture = nullptr;
    Surface *mDepthSurface = nullptr;
    HRESULT InitDeviceObjects();
    void Cleanup() {
        if (mDepthSurface) { mDepthSurface->Release(); mDepthSurface = nullptr; }
        if (mDepthTexture) { mDepthTexture->Release(); mDepthTexture = nullptr; }
        if (mMirrorTexture) { mMirrorTexture->Release(); mMirrorTexture = nullptr; }
        if (mUnknown08) { mUnknown08->Release(); mUnknown08 = nullptr; }
        mPixelShader = nullptr;
        CeWaterReflectionResetAllocated();
    }
};
#include "landscape_init_actual.inc"

static int gFailures = 0;
static void Check(bool cond, const char *msg) {
    if (!cond) { std::fprintf(stderr, "rollback assertion: %s\n", msg); ++gFailures; }
}

int main() {
    // 1. Ultra 1024 succeeds directly
    {
        CDXLandscape land;
        gFailTexAbove = 99999u; gFailDepthAbove = 99999u;
        land.InitDeviceObjects();
        Check(gLastMirrorW == 1024u && gLastDepthW == 1024u && CeWaterReflectionActiveRes() == 1024u, "1024 succeeds");
        Check(gPolicyOutcome == 1 && gLiveTextures == 1 && gLiveSurfaces == 1, "1024 no leaks");
        land.Cleanup();
        Check(gLiveTextures == 0 && gLiveSurfaces == 0, "1024 cleanup zero leaks");
    }
    // 2. 1024 depth fails -> releases 1024 mirror texture and rolls back to 512!
    {
        CDXLandscape land;
        gFailTexAbove = 99999u; gFailDepthAbove = 512u;
        land.InitDeviceObjects();
        Check(gLastMirrorW == 512u && gLastDepthW == 512u && CeWaterReflectionActiveRes() == 512u, "1024 depth failure rolls back to 512");
        Check(gPolicyOutcome == 1 && gLiveTextures == 1 && gLiveSurfaces == 1, "rollback released failed 1024 texture");
        Check(!CeWaterReflectionNeedsRebuild(), "settled after rollback to 512");
        land.Cleanup();
        Check(gLiveTextures == 0 && gLiveSurfaces == 0, "512 rollback cleanup zero leaks");
    }
    // 3. Both 1024 and 512 fail -> rolls back cleanly to retail 256!
    {
        CDXLandscape land;
        gFailTexAbove = 256u; gFailDepthAbove = 256u;
        land.InitDeviceObjects();
        Check(gLastMirrorW == 256u && gLastDepthW == 256u && CeWaterReflectionActiveRes() == 256u, "rolls back all the way to 256");
        Check(gPolicyOutcome == 1 && gLiveTextures == 1 && gLiveSurfaces == 1, "256 fallback zero leaks");
        land.Cleanup();
        Check(gLiveTextures == 0 && gLiveSurfaces == 0, "256 cleanup zero leaks");
    }
    // 4. MY_READABLE_Z_BUFFER=1 + OTHER_ZBUFFER_AS_TEXTURE=1 with 1024 -> depth >= max(1024, 512) = 1024
    {
        MY_READABLE_Z_BUFFER.mValue = 1;
        OTHER_ZBUFFER_AS_TEXTURE.mValue = 1;
        gFailTexAbove = 99999u; gFailDepthAbove = 99999u;
        CDXLandscape land;
        land.InitDeviceObjects();
        Check(gLastMirrorW == 1024u && gLastDepthW == 1024u && land.mUnknown08 != nullptr && land.mDepthTexture != nullptr, "readableZ + depthTex at 1024");
        Check(gPolicyOutcome == 1 && gLiveTextures == 3 && gLiveSurfaces == 1, "readableZ resources intact");
        land.Cleanup();
        Check(gLiveTextures == 0 && gLiveSurfaces == 0, "readableZ cleanup zero leaks");
    }
    if (gFailures) return 1;
    std::puts("PASS");
    return 0;
}
"""


LANDSCAPE_RESTORE_DRIVER = r"""
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "reflection_quality.h"
#include "portlog.h"

bool PortCeLayer() { return true; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}
extern "C" uint64_t SDL_GetTicks() { return 1000; }
extern "C" bool SDL_OpenURL(const char *) { return false; }
extern "C" int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }

struct CTweakSLONG { int mValue; explicit CTweakSLONG(int v = 0) : mValue(v) {} };
CTweakSLONG GEFORCE_FX_POWER(1), EXTRA_GEFORCE_FX_FEATURES(1), R32F_WORKS(1);
CTweakSLONG MY_READABLE_Z_BUFFER(0), OTHER_ZBUFFER_AS_TEXTURE(0);

using HRESULT = int;
using DWORD = unsigned;
const HRESULT S_OK = 0;
const HRESULT E_FAIL = -1;
#define SUCCEEDED(x) ((x) >= 0)
#define FAILED(x) ((x) < 0)
#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while (0)
using D3DFORMAT = unsigned;
constexpr unsigned D3DFMT_A32B32G32R32F=1, D3DFMT_D16=2, D3DFMT_R32F=3, D3DFMT_R5G6B5=4;
constexpr unsigned D3DMULTISAMPLE_NONE=0, D3DPOOL_DEFAULT=0, D3DUSAGE_DEPTHSTENCIL=1, D3DUSAGE_RENDERTARGET=2;
constexpr bool TRUE = true;

static int gLiveTextures = 0, gLiveSurfaces = 0;
static unsigned gFailTexAbove = 99999u, gFailDepthAbove = 99999u;
static unsigned gLastMirrorW = 0, gLastDepthW = 0;
static int gPixelShaderReleases = 0, gInitDeviceCalls = 0;

struct IUnknown {};
struct IDirect3DSurface9 {
    unsigned w;
    explicit IDirect3DSurface9(unsigned dim) : w(dim) { ++gLiveSurfaces; }
    void Release() { --gLiveSurfaces; delete this; }
};
struct IDirect3DTexture9 {
    unsigned w;
    explicit IDirect3DTexture9(unsigned dim) : w(dim) { ++gLiveTextures; }
    void Release() { --gLiveTextures; delete this; }
    HRESULT GetSurfaceLevel(unsigned, IDirect3DSurface9 **out) {
        gLastDepthW = w;
        *out = new IDirect3DSurface9(w);
        return S_OK;
    }
};
struct Pixel {} gPixel;
struct DeviceObject { virtual ~DeviceObject() = default; };
const char *DXGetErrorString9(HRESULT) { return "fault"; }
struct TestGame { bool IsMultiplayer() const { return false; } } GAME;
struct TestHistory { template<class... T> void AddMessage(T...) {} } SETUP_HISTORY;
struct TestDevice {
    struct { unsigned MaxTextureWidth = 2048u, MaxTextureHeight = 2048u; } m_d3dCaps;
    TestDevice *GetDevice() { return this; }
    void AddDeviceObject(DeviceObject *) {}
    void RemoveDeviceObject(DeviceObject *) {}
    void D3D_Release(IUnknown **p) {
        if (p && *p) { ++gPixelShaderReleases; *p = nullptr; }
    }
    HRESULT D3D_CreateTextureNoCheck(unsigned w, unsigned h, unsigned, unsigned usage, D3DFORMAT fmt, unsigned, IDirect3DTexture9 **out) {
        (void)h; (void)fmt;
        if (usage == D3DUSAGE_DEPTHSTENCIL) {
            if (w > gFailDepthAbove) { *out = nullptr; return E_FAIL; }
            *out = new IDirect3DTexture9(w);
            return S_OK;
        }
        if (usage == D3DUSAGE_RENDERTARGET && fmt == D3DFMT_R5G6B5) {
            if (w > gFailTexAbove) { *out = nullptr; return E_FAIL; }
            gLastMirrorW = w;
            *out = new IDirect3DTexture9(w);
            return S_OK;
        }
        *out = new IDirect3DTexture9(w);
        return S_OK;
    }
    HRESULT CreateDepthStencilSurface(unsigned w, unsigned h, D3DFORMAT, unsigned, unsigned, bool, IDirect3DSurface9 **out, void *) {
        (void)h;
        if (w > gFailDepthAbove) { *out = nullptr; return E_FAIL; }
        gLastDepthW = w;
        *out = new IDirect3DSurface9(w);
        return S_OK;
    }
} LT;

struct CDXLandscape {
    Pixel *mPixelShader = &gPixel;
    IDirect3DTexture9 *mMirrorTexture = nullptr, *mUnknown08 = nullptr, *mDepthTexture = nullptr;
    IDirect3DSurface9 *mDepthSurface = nullptr;
    HRESULT InitDeviceObjects() { ++gInitDeviceCalls; return S_OK; }
};

#include "landscape_hook_actual.inc"

static int gFailures = 0;
static void Check(bool cond, const char *msg) {
    if (!cond) { std::fprintf(stderr, "restore assertion: %s\n", msg); ++gFailures; }
}

int main() {
    // 1. Invalidate + Restore at 1024 keeps pixel shader intact and never calls InitDeviceObjects().
    {
        CDXLandscape land;
        sCeMirrorDeviceHook.Attach(&land);
        gFailTexAbove = 99999u; gFailDepthAbove = 99999u;
        sCeMirrorDeviceHook.InvalidateDeviceObjects();
        Check(CE_WaterReflectionAllocatedRes() == 0 && gLiveTextures == 0 && gLiveSurfaces == 0, "invalidate clears allocated state");
        sCeMirrorDeviceHook.RestoreDeviceObjects();
        Check(gPixelShaderReleases == 0 && land.mPixelShader == &gPixel, "restore never releases mPixelShader");
        Check(gInitDeviceCalls == 0, "restore never re-runs InitDeviceObjects()");
        Check(land.mMirrorTexture != nullptr && land.mDepthSurface != nullptr, "restore recreates mirror and depth");
        Check(gLastMirrorW == 1024u && gLastDepthW == 1024u && CE_WaterReflectionAllocatedRes() == 1024, "restore recreates at 1024");
        Check(gLiveTextures == 1 && gLiveSurfaces == 1, "restore has zero leaks");
        sCeMirrorDeviceHook.InvalidateDeviceObjects();
        sCeMirrorDeviceHook.Detach(&land);
        Check(gLiveTextures == 0 && gLiveSurfaces == 0, "invalidate cleanup zero leaks");
    }
    // 2. Restore with 1024 depth failure falls back cleanly to 512 with zero leaks.
    {
        CDXLandscape land;
        sCeMirrorDeviceHook.Attach(&land);
        gFailTexAbove = 99999u; gFailDepthAbove = 512u;
        sCeMirrorDeviceHook.RestoreDeviceObjects();
        Check(land.mMirrorTexture != nullptr && land.mDepthSurface != nullptr, "512 fallback succeeds");
        Check(gLastMirrorW == 512u && gLastDepthW == 512u && CE_WaterReflectionAllocatedRes() == 512, "512 fallback active");
        Check(gLiveTextures == 1 && gLiveSurfaces == 1 && gInitDeviceCalls == 0 && gPixelShaderReleases == 0, "512 fallback zero leaks");
        sCeMirrorDeviceHook.InvalidateDeviceObjects();
        sCeMirrorDeviceHook.Detach(&land);
        Check(gLiveTextures == 0 && gLiveSurfaces == 0, "512 cleanup zero leaks");
    }
    // 3. Total failure on restore (even 256 fails) leaves targets NULL, allocated=0, zero leaks.
    {
        CDXLandscape land;
        sCeMirrorDeviceHook.Attach(&land);
        gFailTexAbove = 99999u; gFailDepthAbove = 128u;
        sCeMirrorDeviceHook.RestoreDeviceObjects();
        Check(land.mMirrorTexture == nullptr && land.mDepthSurface == nullptr, "total failure clears mirror and depth");
        Check(CE_WaterReflectionAllocatedRes() == 0, "total failure reports allocated=0");
        Check(gLiveTextures == 0 && gLiveSurfaces == 0, "total failure zero leaks");
        sCeMirrorDeviceHook.Detach(&land);
    }
    // 4. MY_READABLE_Z_BUFFER=1 recreates mUnknown08 + mirror + depth on restore without leaking.
    {
        MY_READABLE_Z_BUFFER.mValue = 1;
        OTHER_ZBUFFER_AS_TEXTURE.mValue = 1;
        CDXLandscape land;
        sCeMirrorDeviceHook.Attach(&land);
        gFailTexAbove = 99999u; gFailDepthAbove = 99999u;
        sCeMirrorDeviceHook.RestoreDeviceObjects();
        Check(land.mUnknown08 != nullptr && land.mMirrorTexture != nullptr && land.mDepthTexture != nullptr && land.mDepthSurface != nullptr, "readableZ restore recreates all targets");
        Check(gLiveTextures == 3 && gLiveSurfaces == 1 && gInitDeviceCalls == 0, "readableZ restore counts exact");
        sCeMirrorDeviceHook.InvalidateDeviceObjects();
        sCeMirrorDeviceHook.Detach(&land);
        Check(gLiveTextures == 0 && gLiveSurfaces == 0, "readableZ invalidate frees all targets");
    }
    if (gFailures) return 1;
    std::puts("PASS");
    return 0;
}
"""

SHADOW_LIFECYCLE_DRIVER = r"""
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "reflection_quality.h"
#include "portlog.h"

bool PortCeLayer() { return true; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}
extern "C" uint64_t SDL_GetTicks() { return 1000; }
extern "C" bool SDL_OpenURL(const char *) { return false; }
extern "C" int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }

using SINT = int;
using UBYTE = unsigned char;
using BOOL = int;
using HRESULT = int;
const HRESULT S_OK = 0;
const HRESULT E_FAIL = -1;
const BOOL TRUE = 1;
const BOOL FALSE = 0;
#define SUCCEEDED(x) ((x) >= 0)
#define FAILED(x) ((x) < 0)
#define SAFE_DELETE(p) do { if (p) { delete (p); (p) = nullptr; } } while (0)
#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while (0)
constexpr unsigned D3DFMT_D16 = 2, D3DMULTISAMPLE_NONE = 0;
enum EMemType { MEMTYPE_TEXTURE = 2 };
inline void *operator new(size_t sz, EMemType) { return ::operator new(sz); }
inline void operator delete(void *p, EMemType) { ::operator delete(p); }

struct CTweakSLONG { int mValue; explicit CTweakSLONG(int v = 0) : mValue(v) {} };
CTweakSLONG DXSHADOWS_INVERSE(0), SRT_ENABLE(1);
bool gMirrorPass = false;

static int gLiveUMTextures = 0, gLiveDefaultTextures = 0, gLiveDepthSurfaces = 0;
static int gDoubleRestoreWithoutInvalidate = 0;
static unsigned gFailShadowTexAbove = 99999u, gFailShadowDepthAbove = 99999u;
static int gFailShadowTexAtIndex = -1;
static int gShadowCreateOrdinal = 0;

struct IDirect3DSurface9 {
    unsigned w;
    explicit IDirect3DSurface9(unsigned dim) : w(dim) { ++gLiveDepthSurfaces; }
    void Release() { --gLiveDepthSurfaces; delete this; }
};
struct IDirect3DTexture9 {
    unsigned w;
    explicit IDirect3DTexture9(unsigned dim) : w(dim) { ++gLiveDefaultTextures; }
    void Release() { --gLiveDefaultTextures; delete this; }
};
struct CUMTexture {
    IDirect3DTexture9 *mTexture = nullptr;
    int mSize = 0;
    CUMTexture() { ++gLiveUMTextures; }
    ~CUMTexture() { InvalidateDeviceObjects(); --gLiveUMTextures; }
    HRESULT Init(int size, int, int) {
        mSize = size;
        return RestoreDeviceObjects();
    }
    HRESULT RestoreDeviceObjects() {
        if (mTexture) {
            ++gDoubleRestoreWithoutInvalidate;
            mTexture->Release();
            mTexture = nullptr;
        }
        const int ord = gShadowCreateOrdinal++;
        if (static_cast<unsigned>(mSize) > gFailShadowTexAbove ||
            (gFailShadowTexAtIndex >= 0 && ord == gFailShadowTexAtIndex && static_cast<unsigned>(mSize) >= 256u)) {
            return E_FAIL;
        }
        mTexture = new IDirect3DTexture9(static_cast<unsigned>(mSize));
        return S_OK;
    }
    HRESULT InvalidateDeviceObjects() {
        SAFE_RELEASE(mTexture);
        return S_OK;
    }
};

struct CUnit {};
struct ActiveReader {
    CUnit *p = nullptr;
    void SetReader(CUnit *u) { p = u; }
    CUnit *ToRead() const { return p; }
};
struct SShadowRequest { ActiveReader mUnit; float mDist = 0.0f; };
struct SShadowTexInfo { ActiveReader mUnit; UBYTE mUnknown08 = 0; };
struct TestCli { bool mHiDetailMode = false; } CLIPARAMS;
struct TestConsole { template<class... T> void RegisterVariable(T...) {} } CONSOLE;
struct TestHistory { template<class... T> void AddMessage(T...) {} } SETUP_HISTORY;
enum { CVar_bool = 0, CVar_float = 1 };
const char *DXGetErrorString9(HRESULT) { return "fault"; }
struct TestDevice {
    struct { unsigned MaxTextureWidth = 2048u, MaxTextureHeight = 2048u; } m_d3dCaps;
    TestDevice *m_pd3dDevice = this;
    void AddDeviceObject(void *) {}
    void RemoveDeviceObject(void *) {}
    HRESULT CreateDepthStencilSurface(unsigned w, unsigned h, unsigned, unsigned, unsigned, int, IDirect3DSurface9 **out, void *) {
        (void)h;
        if (w > gFailShadowDepthAbove) { *out = nullptr; return E_FAIL; }
        *out = new IDirect3DSurface9(w);
        return S_OK;
    }
} LT;

struct CShadows {
    float mBlobShadowFadeDist = 30.0f;
    SShadowRequest mGuardBefore = {{nullptr}, 42.0f};
    SShadowRequest mRequests[32] = {};
    SShadowTexInfo mTexInfo[32] = {};
    bool mShowShadowMap = false;
    SINT mNumTextures = 0;
    SINT mSizes[32] = {};
    CUMTexture *mTextures[32] = {};
    IDirect3DSurface9 *mDepthSurface = nullptr;
    bool mShadows = true;
    bool mShowShadowExtents = false;
    bool mBlobShadows = true;

    void Init();
    HRESULT RestoreDeviceObjects();
    void ResetShads();
    void AddShadow(CUnit *unit, float dist);
#include "shadows_request_actual.inc"
    void Cleanup() {
        for (SINT i = 0; i < 32; ++i) SAFE_DELETE(mTextures[i]);
        SAFE_RELEASE(mDepthSurface);
        mNumTextures = 0;
        CeDynamicShadowResetAllocated();
    }
};

#include "shadows_methods_actual.inc"

static int gFailures = 0;
static void Check(bool cond, const char *msg) {
    if (!cond) { std::fprintf(stderr, "shadow assertion: %s\n", msg); ++gFailures; }
}

int main() {
    // 1. Total allocation failure in Init sets mNumTextures=0 and mShadows=FALSE; ResetShads/RequestShadow stay safe!
    {
        CShadows s;
        gFailShadowTexAbove = 0u; gFailShadowDepthAbove = 99999u; gFailShadowTexAtIndex = -1; gShadowCreateOrdinal = 0;
        s.Init();
        Check(s.mNumTextures == 0, "total init failure sets mNumTextures=0");
        Check(!s.mShadows, "total init failure disables mShadows");
        Check(CE_DynamicShadowAllocatedRes() == 0, "total init failure reports allocated=0");
        Check(gLiveUMTextures == 0 && gLiveDefaultTextures == 0, "total init failure leaves zero textures");
        s.mBlobShadowFadeDist = 30.0f;
        volatile SINT zeroCount = s.mNumTextures;
        s.mNumTextures = zeroCount;
        volatile float *guardDist = &s.mGuardBefore.mDist;
        *guardDist = 42.0f;
        s.ResetShads();
        CUnit dummy;
        s.RequestShadow(&dummy, 5.0f);
        s.AddShadow(&dummy, 5.0f);
        Check(s.mBlobShadowFadeDist == 30.0f && *guardDist == 42.0f,
              "ResetShads/RequestShadow/AddShadow never underflow mRequests[-1] when mNumTextures==0");
        s.Cleanup();
    }
    // 2. RestoreDeviceObjects when CShadows restores BEFORE CUMTextures retries 256 first (NO unnecessary downgrade to 128!).
    {
        CShadows s;
        gFailShadowTexAbove = 99999u; gFailShadowDepthAbove = 99999u; gFailShadowTexAtIndex = -1; gShadowCreateOrdinal = 0;
        gDoubleRestoreWithoutInvalidate = 0;
        s.Init();
        Check(s.mNumTextures == 16 && s.mSizes[0] == 256 && CE_DynamicShadowAllocatedRes() == 256, "init succeeds at 256");
        // Simulate Alt-Tab InvalidateDeviceObjects on all CUMTextures BEFORE CShadows::RestoreDeviceObjects runs.
        for (SINT i = 0; i < s.mNumTextures; ++i) s.mTextures[i]->InvalidateDeviceObjects();
        Check(gLiveDefaultTextures == 0, "all shadow textures invalidated");
        s.RestoreDeviceObjects();
        Check(s.mSizes[0] == 256 && CE_DynamicShadowAllocatedRes() == 256, "restore retries 256 first and does NOT downgrade to 128");
        Check(gLiveDefaultTextures == 16 && gDoubleRestoreWithoutInvalidate == 0, "zero DEFAULT-pool leaks on ordered restore");
        s.Cleanup();
        Check(gLiveUMTextures == 0 && gLiveDefaultTextures == 0 && gLiveDepthSurfaces == 0, "cleanup zero leaks");
    }
    // 3. Partial failure at 256 during restore invalidates already-live textures cleanly and falls back to 128 with ZERO double-restore leaks!
    {
        CShadows s;
        gFailShadowTexAbove = 99999u; gFailShadowDepthAbove = 99999u; gFailShadowTexAtIndex = -1; gShadowCreateOrdinal = 0;
        gDoubleRestoreWithoutInvalidate = 0;
        s.Init();
        // Simulate textures 0..4 restored at 256 by CUMTexture, textures 5..15 still null, and texture 5 failing at 256.
        for (SINT i = 5; i < s.mNumTextures; ++i) s.mTextures[i]->InvalidateDeviceObjects();
        gShadowCreateOrdinal = 0;
        gFailShadowTexAtIndex = 0; // first retry at 256 fails
        gFailShadowTexAbove = 128u;
        s.RestoreDeviceObjects();
        Check(s.mSizes[0] == 128 && s.mSizes[15] == 128 && CE_DynamicShadowAllocatedRes() == 128, "partial 256 restore failure falls back to 128");
        Check(gLiveDefaultTextures == 16, "all 16 textures live at 128");
        Check(gDoubleRestoreWithoutInvalidate == 0, "InvalidateDeviceObjects called before RestoreDeviceObjects on every recreated texture");
        s.Cleanup();
        Check(gLiveUMTextures == 0 && gLiveDefaultTextures == 0, "partial failure cleanup zero leaks");
    }
    // 3b. Live texture with mismatched size must be invalidated before RestoreDeviceObjects (zero DEFAULT-pool leak!).
    {
        CShadows s;
        gFailShadowTexAbove = 99999u; gFailShadowDepthAbove = 99999u; gFailShadowTexAtIndex = -1; gShadowCreateOrdinal = 0;
        gDoubleRestoreWithoutInvalidate = 0;
        s.Init();
        s.mTextures[0]->mSize = 128; // stale size while mTexture != nullptr
        s.mTextures[15]->InvalidateDeviceObjects(); // trigger anyMissing at ceRes=256
        s.RestoreDeviceObjects();
        Check(s.mSizes[0] == 256 && s.mTextures[0]->mSize == 256 && gLiveDefaultTextures == 16, "mismatched texture recreated cleanly at 256");
        Check(gDoubleRestoreWithoutInvalidate == 0, "live mismatched texture invalidated before RestoreDeviceObjects");
        s.Cleanup();
        Check(gLiveUMTextures == 0 && gLiveDefaultTextures == 0, "mismatched cleanup zero leaks");
    }
    // 4. DXSHADOWS_INVERSE=1 depth failure at 256 invalidates all 256 textures and steps down to 128 with zero leaks.
    {
        DXSHADOWS_INVERSE.mValue = 1;
        CShadows s;
        gFailShadowTexAbove = 99999u; gFailShadowDepthAbove = 99999u; gFailShadowTexAtIndex = -1; gShadowCreateOrdinal = 0;
        gDoubleRestoreWithoutInvalidate = 0;
        s.Init();
        Check(s.mSizes[0] == 256 && s.mDepthSurface != nullptr && gLiveDepthSurfaces == 1, "inverse init at 256");
        gFailShadowDepthAbove = 128u;
        s.RestoreDeviceObjects();
        Check(s.mSizes[0] == 128 && s.mDepthSurface != nullptr && s.mDepthSurface->w == 128u, "inverse depth failure falls back to 128");
        Check(gLiveDefaultTextures == 16 && gLiveDepthSurfaces == 1 && gDoubleRestoreWithoutInvalidate == 0, "inverse fallback zero leaks");
        s.Cleanup();
        DXSHADOWS_INVERSE.mValue = 0;
        Check(gLiveUMTextures == 0 && gLiveDefaultTextures == 0 && gLiveDepthSurfaces == 0, "inverse cleanup zero leaks");
    }
    // 5. Total failure on restore (even 64 fails) invalidates all textures, disables mShadows, zero leaks.
    {
        CShadows s;
        gFailShadowTexAbove = 99999u; gFailShadowDepthAbove = 99999u; gFailShadowTexAtIndex = -1; gShadowCreateOrdinal = 0;
        gDoubleRestoreWithoutInvalidate = 0;
        s.Init();
        for (SINT i = 0; i < s.mNumTextures; ++i) s.mTextures[i]->InvalidateDeviceObjects();
        gFailShadowTexAbove = 0u;
        s.RestoreDeviceObjects();
        Check(!s.mShadows && s.mBlobShadows && CE_DynamicShadowAllocatedRes() == 0, "total restore failure falls back to blob shadows");
        Check(gLiveDefaultTextures == 0 && gDoubleRestoreWithoutInvalidate == 0, "total restore failure leaves zero DEFAULT-pool textures");
        s.Cleanup();
    }
    if (gFailures) return 1;
    std::puts("PASS");
    return 0;
}
"""


class GraphicsR2Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("g++") or shutil.which("clang++") or shutil.which("c++")
        if not cls.compiler:
            raise RuntimeError("a C++ compiler is required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="ce-graphics-r2-")
        cls.dir = Path(cls.tmp.name)
        (cls.dir / "SDL3").mkdir()
        (cls.dir / "SDL3/SDL.h").write_text(
            '#pragma once\n#include <stdint.h>\ntypedef uint64_t Uint64;\n'
            'extern "C" Uint64 SDL_GetTicks(void);\nextern "C" bool SDL_OpenURL(const char *);\n'
        )
        (cls.dir / "driver.cpp").write_text(POLICY_DRIVER)
        cls.binaries = {
            public: cls.compile_one("ce" if public else "reference", COMMON / "reflection_quality.cpp", public=public)
            for public in (True, False)
        }

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    @classmethod
    def compile_one(cls, name: str, source: Path, *, public: bool = True, driver: Path | None = None) -> Path:
        exe = cls.dir / name
        ce_obj = cls.dir / f"{name}-ce.o"
        ce_cmd = [
            cls.compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wno-format-truncation",
            "-DPortCeLayer=CeSettingsPortCeLayer",
            "-I", str(cls.dir), "-I", str(COMMON),
            "-c", str(COMMON / "ce.cpp"), "-o", str(ce_obj),
        ]
        if public:
            ce_cmd.insert(1, '-DCE_VERSION="graphics-r2-test"')
        built_ce = subprocess.run(ce_cmd, capture_output=True, text=True, timeout=60)
        if built_ce.returncode != 0:
            raise RuntimeError(f"compile ce.cpp for {name} failed:\n{built_ce.stderr}")
        cmd = [
            cls.compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(cls.dir), "-I", str(COMMON),
            str(source), str(ce_obj), str(driver or (cls.dir / "driver.cpp")),
            "-o", str(exe),
        ]
        if public:
            cmd.insert(1, '-DCE_VERSION="graphics-r2-test"')
        built = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if built.returncode != 0:
            raise RuntimeError(f"compile {name} failed:\n{built.stderr}")
        return exe

    def run_case(self, mode: str, *, public: bool = True, ini: str | None = None, env: dict[str, str] | None = None, binary: Path | None = None):
        home = Path(tempfile.mkdtemp(prefix="r2-prof-", dir=self.dir))
        if ini is not None:
            (home / "ce.ini").write_text(ini)
        run_env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home), "BEA_USER_DIR": str(home)}
        if env:
            run_env.update(env)
        args = [str(binary or self.binaries[public])]
        if mode:
            args.append(mode)
        return subprocess.run(args, env=run_env, capture_output=True, text=True, timeout=15)

    def assert_pass(self, mode: str, **kwargs):
        res = self.run_case(mode, **kwargs)
        self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
        self.assertEqual(res.stdout, "PASS\n")

    def test_defaults_stored_and_invalid(self):
        self.assert_pass("default")
        self.assert_pass("stored", ini="water_reflection_res=512\ndynamic_shadow_res=128\n")
        self.assert_pass("garbage", ini="water_reflection_res=999\ndynamic_shadow_res=bogus\n")

    def test_frame_boundary_caps_and_fallback_settling(self):
        self.assert_pass("switch")
        self.assert_pass("caps")

    def test_faithful_fences_preserve_retail(self):
        self.assert_pass("fenced", public=False)
        self.assert_pass("layer-off")
        for fence in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"):
            with self.subTest(fence=fence):
                self.assert_pass("fenced", ini="water_reflection_res=1024\ndynamic_shadow_res=256\n",
                                 env={fence: "1", "BEA_PORT_CE_LAYER": "1"})

    def test_real_landscape_init_rollback_and_leak_freedom(self):
        sys.path.insert(0, str(PORT))
        import build
        text = build.patched_text(["DXLandscape.cpp"])["DXLandscape.cpp"]
        init_fn = public_d3dapp_tests.function(text, r"^HRESULT\s+CDXLandscape::InitDeviceObjects\s*\(")
        (self.dir / "landscape_init_actual.inc").write_text(init_fn + "\n")
        drv = self.dir / "landscape_rollback_driver.cpp"
        drv.write_text(LANDSCAPE_ROLLBACK_DRIVER)
        exe = self.compile_one("landscape-rollback", COMMON / "reflection_quality.cpp", public=True, driver=drv)
        self.assert_pass("", binary=exe, ini="water_reflection_res=1024\n")

    def test_0367_mirror_restore_zero_res_guards_and_balanced_braces(self):
        sys.path.insert(0, str(PORT))
        import build
        texts = build.patched_text(["DXLandscape.cpp", "DXWater.cpp", "game.cpp"])
        dxl = texts["DXLandscape.cpp"]
        # 1. Brace balance inside CDXLandscape::InitDeviceObjects for both CE and faithful views.
        init_fn = public_d3dapp_tests.function(dxl, r"^HRESULT\s+CDXLandscape::InitDeviceObjects\s*\(")
        self.assertEqual(init_fn.count("{"), init_fn.count("}"), "unbalanced braces in patched InitDeviceObjects")
        self.assertEqual(faithful(init_fn).count("{"), faithful(init_fn).count("}"), "unbalanced braces in faithful InitDeviceObjects")
        # No brace-straddling #ifdef CE_VERSION blocks in InitDeviceObjects.
        depth = 0
        for line in init_fn.splitlines():
            s = line.strip()
            if s.startswith("#ifdef CE_VERSION"):
                mark = depth
            elif s.startswith("#else"):
                self.assertEqual(depth, mark, "brace-straddling #ifdef CE_VERSION block before #else")
            elif s.startswith("#endif"):
                self.assertEqual(depth, mark, "brace-straddling #ifdef CE_VERSION block before #endif")
            elif not s.startswith(("#", "//")):
                depth += line.count("{") - line.count("}")

        # 2. Compile and run actual CeCreateMirrorTargetsAt + CeLandscapeMirrorDeviceHook restore lifecycle + causal mutants.
        start = dxl.index("static bool CeCreateMirrorTargetsAt(")
        end = dxl.index("static CeLandscapeMirrorDeviceHook sCeMirrorDeviceHook;") + len("static CeLandscapeMirrorDeviceHook sCeMirrorDeviceHook;")
        hook_src = dxl[start:end]
        (self.dir / "landscape_hook_actual.inc").write_text(hook_src + "\n")
        drv = self.dir / "landscape_restore_driver.cpp"
        drv.write_text(LANDSCAPE_RESTORE_DRIVER)
        exe = self.compile_one("landscape-restore", COMMON / "reflection_quality.cpp", public=True, driver=drv)
        self.assert_pass("", binary=exe, ini="water_reflection_res=1024\n")

        for label, old, new in (
            ("restore-calls-init",
             "if (CeCreateMirrorTargetsAt(res, readableZ, zAsTex, &newMirror, &newDepthTex, &newDepthSurf))",
             "mOwner->InitDeviceObjects(); if (CeCreateMirrorTargetsAt(res, readableZ, zAsTex, &newMirror, &newDepthTex, &newDepthSurf))"),
            ("restore-releases-pixel-shader",
             "CeWaterReflectionBeginFrame();",
             "LT.D3D_Release((IUnknown **)&mOwner->mPixelShader); CeWaterReflectionBeginFrame();"),
            ("restore-no-fallback",
             "\t\t\t\tres = CeWaterReflectionNextFallback(res);\n\t\t\t}\n\t\t\tCeWaterReflectionSetAllocatedRes(0u, attempt);",
             "\t\t\t\tbreak;\n\t\t\t}\n\t\t\tCeWaterReflectionSetAllocatedRes(0u, attempt);"),
        ):
            with self.subTest(mutant=label):
                self.assertEqual(hook_src.count(old), 1, f"anchor mismatch for {label}")
                mdir = self.dir / f"mut-restore-{label}"
                mdir.mkdir(exist_ok=True)
                (mdir / "landscape_hook_actual.inc").write_text(hook_src.replace(old, new) + "\n")
                mdrv = mdir / "drv.cpp"
                mdrv.write_text(LANDSCAPE_RESTORE_DRIVER)
                cmd = [
                    self.compiler, "-std=c++17", "-O2", "-Wall", "-Wextra",
                    '-DCE_VERSION="graphics-r2-test"',
                    "-I", str(mdir), "-I", str(self.dir), "-I", str(COMMON),
                    str(COMMON / "reflection_quality.cpp"), str(self.dir / "ce-ce.o"), str(mdrv),
                    "-o", str(mdir / "exe"),
                ]
                built = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
                self.assertEqual(built.returncode, 0, built.stderr)
                ran = self.run_case("", binary=mdir / "exe", ini="water_reflection_res=1024\n")
                self.assertEqual(ran.returncode, 1, f"mutant {label} was NOT caught")
                self.assertIn("restore assertion:", ran.stderr)

        # 3. Verify zero-res, missing-depth, HD ripple/blur scaling, lit 3D reflection, and mirror fog guards.
        extra_texts = build.patched_text(["State.cpp", "renderinfo.cpp"])
        game_txt = texts["game.cpp"]
        water_txt = texts["DXWater.cpp"]
        state_txt = extra_texts["State.cpp"]
        rinfo_txt = extra_texts["renderinfo.cpp"]
        self.assertIn("ENGINE.GetLandscape()->mMirrorTexture && ENGINE.GetLandscape()->mDepthSurface", game_txt)
        self.assertIn("CeWaterReflectionActiveRes() > 0u", game_txt)
        self.assertIn("FORCE_OBJECT_IMPOSTERS = (CeWaterReflectionActiveRes() <= 512u);", game_txt)
        self.assertIn("const unsigned ceMirrorDim = CeWaterReflectionActiveRes() > 0u ? CeWaterReflectionActiveRes() : 256u;", game_txt)
        self.assertIn("ceWaterRes > 0u && ceWaterRes != 256u", water_txt)
        self.assertIn("const float ceDistort = 0.08f * (256.0f / static_cast<float>(ceWaterRes));", water_txt)
        self.assertIn("const float ceBlur = ceTap < (1.0f / 256.0f) ? (1.0f / 256.0f) : ceTap;", water_txt)
        self.assertIn("ENGINE.GetLandscape() && ENGINE.GetLandscape()->mMirrorTexture && ENGINE.GetLandscape()->mDepthSurface", water_txt)
        self.assertIn("if (gMirrorPass && CeWaterReflectionActiveRes() <= 256u)", state_txt)
        self.assertIn("if (gMirrorPass && CeWaterReflectionActiveRes() <= 256u)", rinfo_txt)

    def test_0368_shadow_zero_pool_restore_and_line_safety(self):
        sys.path.insert(0, str(PORT))
        import build
        texts = build.patched_text(["shadows.h", "DXShadows.cpp"])
        sh = texts["shadows.h"]
        dxs = texts["DXShadows.cpp"]
        # 1. Verify #line 105 is NOT inside #ifdef CE_VERSION and post-block __LINE__ matches retail.
        patch_368 = (ROOT / "port/patches/0368-ce-dynamic-shadow-res.patch").read_text()
        self.assertNotIn("+#line 105", patch_368)
        self.assertIn('static_assert(sizeof(nextTex) / sizeof(nextTex[0]) >= 32', dxs)
        a = dxs.index("#line 46")
        b = dxs.index("ResetShads();") + len("ResetShads();")
        probe_ce = subprocess.run(
            [self.compiler, "-E", "-P", "-DCE_VERSION=1", "-x", "c++", "-"],
            input=dxs[a:b] + "\nLINE_PROBE = __LINE__;\n", text=True, capture_output=True, timeout=15)
        self.assertEqual(probe_ce.returncode, 0, probe_ce.stderr)
        orig = (ROOT / "src/DXShadows.cpp").read_text()
        oa = orig.index("#line 46")
        ob = orig.index("ResetShads();") + len("ResetShads();")
        probe_orig = subprocess.run(
            [self.compiler, "-E", "-P", "-x", "c++", "-"],
            input=orig[oa:ob] + "\nLINE_PROBE = __LINE__;\n", text=True, capture_output=True, timeout=15)
        self.assertEqual(probe_orig.returncode, 0, probe_orig.stderr)
        ce_line = [l.strip() for l in probe_ce.stdout.splitlines() if "LINE_PROBE" in l]
        orig_line = [l.strip() for l in probe_orig.stdout.splitlines() if "LINE_PROBE" in l]
        self.assertEqual(ce_line, orig_line, f"post-block __LINE__ drifted: CE={ce_line} vs retail={orig_line}")

        # 2. Extract actual CShadows methods and RequestShadow and run lifecycle + causal mutants.
        req_start = sh.index("#ifdef CE_VERSION\n\tvoid\t\tRequestShadow(")
        req_end = sh.index("#endif\n", req_start) + len("#endif\n")
        req_src = sh[req_start:req_end]
        methods_src = "\n".join(
            public_d3dapp_tests.function(dxs, pat) for pat in (
                r"^void\s+CShadows::Init\s*\(",
                r"^HRESULT\s+CShadows::RestoreDeviceObjects\s*\(",
                r"^void\s+CShadows::ResetShads\s*\(",
                r"^void\s+CShadows::AddShadow\s*\(",
            )
        )
        (self.dir / "shadows_request_actual.inc").write_text(req_src)
        (self.dir / "shadows_methods_actual.inc").write_text(methods_src + "\n")
        drv = self.dir / "shadow_lifecycle_driver.cpp"
        drv.write_text(SHADOW_LIFECYCLE_DRIVER)
        exe = self.compile_one("shadow-lifecycle", COMMON / "reflection_quality.cpp", public=True, driver=drv)
        self.assert_pass("", binary=exe, ini="dynamic_shadow_res=256\n")

        for label, old, new in (
            ("init-keeps-stale-num-textures",
             "mNumTextures = 0;\n\t\t\tmShadows = FALSE;",
             "/* mutant: leave mNumTextures stale */"),
            ("restore-skips-ceRes-first",
             "unsigned tryRes = anyMissing ? ceRes : CeDynamicShadowNextFallback(ceRes);",
             "unsigned tryRes = CeDynamicShadowNextFallback(ceRes);"),
            ("restore-omits-invalidate-before-recreate",
             "mTextures[i]->InvalidateDeviceObjects();\n\t\t\t\t\tmSizes[i] = (SINT)tryRes;",
             "mSizes[i] = (SINT)tryRes;"),
            ("resetshads-omits-zero-guard",
             "if (mNumTextures <= 0)\n\t\treturn;\n#endif\n\n\tif (mShadows == TRUE)",
             "if (false)\n\t\treturn;\n#endif\n\n\tif (mShadows == TRUE)"),
        ):
            with self.subTest(mutant=label):
                self.assertEqual(methods_src.count(old), 1, f"anchor mismatch for {label}")
                mdir = self.dir / f"mut-sh-{label}"
                mdir.mkdir(exist_ok=True)
                (mdir / "shadows_request_actual.inc").write_text(req_src)
                (mdir / "shadows_methods_actual.inc").write_text(methods_src.replace(old, new) + "\n")
                mdrv = mdir / "drv.cpp"
                mdrv.write_text(SHADOW_LIFECYCLE_DRIVER)
                cmd = [
                    self.compiler, "-std=c++17", "-O2", "-Wall", "-Wextra",
                    '-DCE_VERSION="graphics-r2-test"',
                    "-I", str(mdir), "-I", str(self.dir), "-I", str(COMMON),
                    str(COMMON / "reflection_quality.cpp"), str(self.dir / "ce-ce.o"), str(mdrv),
                    "-o", str(mdir / "exe"),
                ]
                built = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
                self.assertEqual(built.returncode, 0, built.stderr)
                ran = self.run_case("", binary=mdir / "exe", ini="dynamic_shadow_res=256\n")
                self.assertEqual(ran.returncode, 1, f"mutant {label} was NOT caught")
                self.assertIn("shadow assertion:", ran.stderr)

        # 3. Verify RenderTextures, RenderLandscapeShadows, and TestDraw guard mNumTextures<=0 and mTextures[i]->mTexture.
        for pat in (r"^void\s+CShadows::RenderTextures\s*\(",
                    r"^void\s+CShadows::RenderLandscapeShadows\s*\(",
                    r"^void\s+CShadows::TestDraw\s*\("):
            fn = public_d3dapp_tests.function(dxs, pat)
            self.assertIn("if (mNumTextures <= 0 || mNumTextures > 32)", fn, pat)
            self.assertIn("->mTexture", fn, pat)

    def test_patches_0367_and_0368_leave_faithful_build_unchanged(self):
        hook_sites = tuple(sorted(p.name for p in (ROOT / "port/patches").glob("03[01][0-9]-ce-hook-*.patch")))
        later = ("0381-ce-perf-surf-museum-fixes.patch", *hook_sites)
        with tempfile.TemporaryDirectory(prefix="r2-patch-") as td:
            base = Path(td)
            files_367 = ("DXEngine.cpp", "DXLandscape.cpp", "DXWater.cpp", "State.cpp", "game.cpp", "renderinfo.cpp")
            with_367 = patched(base / "with367", files_367, skip=later)
            without_367 = patched(base / "without367", files_367, skip=("0367-ce-water-reflection-res.patch", *later))
            for name in files_367:
                with self.subTest(patch="0367", file=name):
                    self.assertNotEqual(with_367[name], without_367[name])
                    self.assertEqual(faithful(with_367[name]), faithful(without_367[name]))
            files_368 = ("shadows.h", "DXShadows.cpp", "game.cpp")
            with_368 = patched(base / "with368", files_368, skip=later)
            without_368 = patched(base / "without368", files_368, skip=("0368-ce-dynamic-shadow-res.patch", *later))
            for name in files_368:
                with self.subTest(patch="0368", file=name):
                    self.assertNotEqual(with_368[name], without_368[name])
                    self.assertEqual(faithful(with_368[name]), faithful(without_368[name]))

    def test_causal_mutants_are_caught(self):
        orig = (COMMON / "reflection_quality.cpp").read_text()
        cases = [
            ("mid-frame-water", "waterRequested = res;\n    return 1;", "waterRequested = res; waterActiveTarget = res;\n    return 1;", "switch", None, {}),
            ("mid-frame-shadow", "shadowRequested = res;\n    return 1;", "shadowRequested = res; shadowActiveTarget = res;\n    return 1;", "switch", None, {}),
            ("ignore-fence", "if (value && *value) return false;", "if (false && value && *value) return false;", "fenced", "water_reflection_res=1024\ndynamic_shadow_res=256\n", {"BEA_PORT_CLOCK": "1"}),
            ("ignore-water-cap", "if (target > 512u && cap < 1024u) target = 512u;", "if (false && target > 512u && cap < 1024u) target = 512u;", "caps", None, {}),
            ("ignore-shadow-cap", "if (target > 128u && cap < 256u) target = 128u;", "if (false && target > 128u && cap < 256u) target = 128u;", "caps", None, {}),
            ("spin-after-water-fallback", "return waterActive && waterAllocated != 0u && waterActiveTarget != waterAttemptedTarget;",
             "return waterActive && waterAllocated != 0u && waterActiveTarget != waterAllocated;", "switch", None, {}),
            ("spin-after-shadow-fallback", "return shadowActive && shadowAllocated != 0u && shadowActiveTarget != shadowAttemptedTarget;",
             "return shadowActive && shadowAllocated != 0u && shadowActiveTarget != shadowAllocated;", "switch", None, {}),
            ("fixed-water-tap", "return 2.0f / static_cast<float>(CeWaterReflectionActiveRes());", "return 1.0f / 128.0f;", "default", None, {}),
            ("fixed-shadow-tap1", "return 1.0f / static_cast<float>(res);", "(void)res; return 1.0f / 64.0f;", "default", None, {}),
            ("fixed-shadow-tap3", "return 1.0f / static_cast<float>(res * 3);", "(void)res; return 1.0f / 192.0f;", "default", None, {}),
        ]
        for label, old, new, mode, ini, env in cases:
            with self.subTest(mutant=label):
                self.assertEqual(orig.count(old), 1, f"anchor mismatch for {label}")
                mut = self.dir / f"{label}.cpp"
                mut.write_text(orig.replace(old, new))
                exe = self.compile_one(f"mut-{label}", mut)
                res = self.run_case(mode, ini=ini, env=env, binary=exe)
                self.assertEqual(res.returncode, 1, f"mutant {label} was NOT caught")
                self.assertIn("graphics-r2 assertion:", res.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=1)
