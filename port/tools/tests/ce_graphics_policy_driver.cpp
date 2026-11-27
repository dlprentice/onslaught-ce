// SPDX-License-Identifier: GPL-3.0-only
// Compile the production policy and actual selected menu/water functions; substitute only native boundaries.
#include "ce_graphics_policy_support.h"
#include "ce_graphics_policy.h"
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>

// The game's own initial values (ce_graphics_policy_tests.py compares each with the game's definition).
CTweakSLONG GEFORCE_FX_POWER(0), EXTRA_GEFORCE_FX_FEATURES(1), R32F_WORKS(0);
CTweakSLONG MY_READABLE_Z_BUFFER(0), OTHER_ZBUFFER_AS_TEXTURE(0);
namespace
{
bool layer = true, writeSucceeds = true, pixelSucceeds = true, vertexSucceeds = true, vertexNativeSucceeds = true;
bool allowDetailPreset = false;
unsigned maxAnisoHw = 16u, maxRtDimHw = 1024u;
int stored = -1, reads, writes, notices, displayChanges, logCount, failures;
int detailUnit = -1, detailTree = -1, detailTerrain = -1, detailAniso = -1, detailWater = -1, detailShadow = -1;
int detailCalls, detailStagedCalls;
CeDetailPreset lastStaged = {-1, -1, -1, -1, -1, -1};
bool textureSucceeds = true, textureOutputSucceeds = true, depthSucceeds = true;
int textureCalls, textureFailAt;
void Check(bool value, const char *label) { if (!value) { ++failures; std::cout << "FAIL " << label << '\n'; } }
void RecordDetailStage(const CeDetailPreset &preset) { ++detailStagedCalls; lastStaged = preset; }
}
bool PortCeLayer() { return layer; }
extern "C" int CE_SettingGetBool(const char *name, int fallback)
{
    Check(std::string(name) == "extra_graphics", "only the graphics preference is read");
    ++reads; return stored < 0 ? fallback : stored;
}
extern "C" int CE_SettingSet(const char *name, int value)
{
    if (!allowDetailPreset)
        Check(std::string(name) == "extra_graphics", "only the graphics preference is written");
    ++writes; if (writeSucceeds && std::string(name) == "extra_graphics") stored = value; return writeSucceeds;
}
void PortSessionLog(PortSessionCategory, bool, const char *, ...) { ++logCount; }
void PortCeSaveFailedNotice() { ++notices; }
int PortUserFileState(const char *) { return 1; }	// these cases never open the options file
// The hook that takes the fresh defaults is a stub below, so nothing here may ask for the beyond-retail quality rows
// except the explicit detail-preset cases.
extern "C" int CE_SettingGetText(const char *, char *, int) { return 0; }
extern "C" unsigned CE_AnisoHardwareMax() { return maxAnisoHw; }
extern "C" unsigned CeRenderTargetQualityHardwareMaxDim() { return maxRtDimHw; }
extern "C" int CE_FullUnitModelsRequest(int v) { if (!allowDetailPreset) Check(false, "the beyond-retail rows are not requested by these cases"); ++detailCalls; detailUnit = v; return allowDetailPreset ? 1 : 0; }
extern "C" int CE_FullTreeModelsRequest(int v) { if (!allowDetailPreset) Check(false, "the beyond-retail rows are not requested by these cases"); ++detailCalls; detailTree = v; return allowDetailPreset ? 1 : 0; }
extern "C" int CE_TerrainDetailRequest(int v) { if (!allowDetailPreset) Check(false, "the beyond-retail rows are not requested by these cases"); ++detailCalls; detailTerrain = v; return allowDetailPreset ? 1 : 0; }
extern "C" int CE_AnisoRequest(int v) { if (!allowDetailPreset) Check(false, "the beyond-retail rows are not requested by these cases"); ++detailCalls; detailAniso = v; return allowDetailPreset ? 1 : 0; }
extern "C" int CE_WaterReflectionRequest(int v) { if (!allowDetailPreset) Check(false, "the beyond-retail rows are not requested by these cases"); ++detailCalls; detailWater = v; return allowDetailPreset ? 1 : 0; }
extern "C" int CE_DynamicShadowRequest(int v) { if (!allowDetailPreset) Check(false, "the beyond-retail rows are not requested by these cases"); ++detailCalls; detailShadow = v; return allowDetailPreset ? 1 : 0; }
void CeWaterReflectionBeginFrame() {}
unsigned CeWaterReflectionTargetRes() { return 256u; }
unsigned CeWaterReflectionNextFallback(unsigned) { return 0u; }
void CeWaterReflectionSetAllocatedRes(unsigned, unsigned) {}
void DisplaySettingChanged() { ++displayChanges; }
#ifdef CE_VERSION
void PortCeGraphicsApplyFreshDefaults() {}
#endif

using HRESULT = int;
using DWORD = unsigned;
const HRESULT S_OK = 0;
#define SUCCEEDED(x) ((x) >= 0)
#define FAILED(x) ((x) < 0)
using D3DFORMAT = unsigned;
constexpr unsigned D3DFMT_A32B32G32R32F=1, D3DFMT_D16=2, D3DFMT_R32F=3, D3DFMT_R5G6B5=4;
constexpr unsigned D3DMULTISAMPLE_NONE=0, D3DPOOL_DEFAULT=0, D3DUSAGE_DEPTHSTENCIL=1, D3DUSAGE_RENDERTARGET=2;
struct Surface { void Release() {} } surface;
struct Texture { void Release() {} HRESULT GetSurfaceLevel(unsigned, Surface **out) { *out=depthSucceeds ? &surface : nullptr; return depthSucceeds ? S_OK : -1; } } texture;
template<class T> void AssignOutput(T) {}
void AssignOutput(Texture **out) { *out=textureOutputSucceeds ? &texture : nullptr; }
void AssignOutput(Surface **out) { *out=depthSucceeds ? &surface : nullptr; }
void AssignOutputs() {}
template<class T,class... Rest> void AssignOutputs(T first,Rest... rest) { AssignOutput(first); AssignOutputs(rest...); }
template<class... T> void AddMessage(T...) {}
const char *DXGetErrorString9(HRESULT) { return "authored failure"; }
struct TestGame { bool IsMultiplayer() const { return false; } } GAME;
struct TestHistory { template<class... T> void AddMessage(T...) {} } SETUP_HISTORY;
constexpr bool TRUE = true;
#define D3DPS_VERSION(major, minor) (0xffff0000u | ((major) << 8) | (minor))
struct Pixel { void Release() {} } pixel;
struct CVertexShader
{
    void *mShader;
    int mRefCount = 1;
    void Release() { --mRefCount; }
    static CVertexShader *Create(const char *, DWORD *, int, void *, int, int)
    {
        if (!vertexSucceeds) return nullptr;
        auto *result = new CVertexShader;
        result->mShader = vertexNativeSucceeds ? result : nullptr;
        return result;
    }
};
struct TestDevice
{
    struct { unsigned PixelShaderVersion = D3DPS_VERSION(2,0); } m_d3dCaps;
    TestDevice *GetDevice() { return this; }
    template<class... T> HRESULT D3D_CreateTextureNoCheck(T... values) { textureOutputSucceeds = textureSucceeds && ++textureCalls != textureFailAt; AssignOutputs(values...); return textureOutputSucceeds ? S_OK : -1; }
    template<class... T> HRESULT CreateDepthStencilSurface(T... values) { AssignOutputs(values...); return depthSucceeds ? S_OK : -1; }
    template<class... T> void AddMessage(T...) {}
    HRESULT D3D_CreatePixelShader(const char *, Pixel **out)
    { *out = pixelSucceeds ? &pixel : nullptr; return pixelSucceeds ? S_OK : -1; }
} LT;
const char WATER_PS_TEXT[] = "authored shader boundary";
const char WATER_VS_TEXT[] = "authored shader boundary";
struct CDXWater
{
    DWORD mGridColour = 0;
    static CVertexShader *sShader;
    static Pixel *sPixelShader;
    HRESULT InitDeviceObjects();
};
CVertexShader *CDXWater::sShader;
Pixel *CDXWater::sPixelShader;
struct CDXLandscape
{
    Pixel *mPixelShader = nullptr;
    Texture *mMirrorTexture = nullptr, *mUnknown08 = nullptr, *mDepthTexture = nullptr;
    Surface *mDepthSurface = nullptr;
    HRESULT InitDeviceObjects();
};
// The authored language stand-ins identify which stock choice the real row selects.
using WCHAR = wchar_t;
static WCHAR *Localization_GetStringById(int) { static WCHAR unavailable[] = L"Unavailable"; return unavailable; }
struct CBOOLMenuItem
{
    WCHAR *GetChoiceText(int value)
    {
        static WCHAR on[] = L"Yes", off[] = L"No";
        return value ? on : off;
    }
};
struct CnVidia : CBOOLMenuItem
{
    int mSelection = 1, mValue = 1;
    int GetNumChoices();
    WCHAR *GetChoiceText(int);
    int GetValue();
    void SetValue(int);
};
inline bool CePolicyFullDetailShading() { return !GAME.IsMultiplayer(); }
inline void CeEnsureLandscapeVertexShader(void *) {}
#include "ce_graphics_actual.inc"

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const std::string name = argv[1];
    if (name.compare(0, 7, "detail-") == 0)
    {
        PortCeGraphicsRegisterDetailPresetStager(RecordDetailStage);
        if (name == "detail-stage-clamped")
        {
            maxAnisoHw = 4u; maxRtDimHw = 256u;
            PortCeGraphicsStageDetailPreset(3);
            Check(writes == 0 && detailCalls == 0, "staging a Detail preset writes nothing to ce.ini and requests no live change");
            Check(detailStagedCalls == 1 && lastStaged.unitModels == 1 && lastStaged.treeModels == 0 &&
                  lastStaged.terrainDetail == 1 && lastStaged.aniso == 4 && lastStaged.waterRes == 256 && lastStaged.shadowRes == 128,
                  "High preset stages hardware-clamped aniso and water resolution");
            maxAnisoHw = 1u; maxRtDimHw = 64u;
            PortCeGraphicsStageDetailPreset(2);
            Check(writes == 0 && detailCalls == 0 && detailStagedCalls == 2 &&
                  lastStaged.aniso == 0 && lastStaged.waterRes == 256 && lastStaged.shadowRes == 64,
                  "Medium preset clamps aniso to 0 and shadow resolution to 64 when hardware caps are minimal");
            PortCeGraphicsStageDetailPreset(0);
            Check(detailStagedCalls == 2, "Custom detail level stages nothing");
        }
        else if (name == "detail-apply-clamped")
        {
            allowDetailPreset = true;
            maxAnisoHw = 8u; maxRtDimHw = 256u;
            PortCeGraphicsApplyDetailPreset(3);
            Check(detailCalls == 6 && writes == 6 && detailUnit == 1 && detailTree == 0 && detailTerrain == 1 &&
                  detailAniso == 8 && detailWater == 256 && detailShadow == 128,
                  "applying High preset clamps aniso and water resolution to hardware caps before requesting and saving");
        }
        else if (name == "detail-apply-uncapped")
        {
            allowDetailPreset = true;
            maxAnisoHw = 16u; maxRtDimHw = 1024u;
            PortCeGraphicsApplyDetailPreset(3);
            Check(detailCalls == 6 && writes == 6 && detailUnit == 1 && detailTree == 0 && detailTerrain == 1 &&
                  detailAniso == 16 && detailWater == 512 && detailShadow == 128,
                  "applying High preset keeps full preset values when hardware supports them");
        }
        else if (name == "detail-stage-faithful")
        {
            layer = false;
            PortCeGraphicsStageDetailPreset(3);
            PortCeGraphicsApplyDetailPreset(3);
            Check(writes == 0 && detailCalls == 0 && detailStagedCalls == 0,
                  "faithful mode neither stages nor applies CE detail presets");
        }
        std::cout << "graphics-policy " << name << " failures=" << failures << '\n';
        return failures ? 1 : 0;
    }
    const bool faithful = name == "private" || name == "layer-off" || name.compare(0, 6, "fence-") == 0;
    if (name == "layer-off") layer = false;
    if (name.compare(0, 6, "fence-") == 0)
    {
        const std::string variable = "BEA_PORT_" + name.substr(6);
        setenv(variable.c_str(), "1", 1);
    }
    // Who asks for the extra features: a saved line of ce.ini (most cases here hold a saved Yes, so that the resources are
    // exercised), else only a new profile. "upgraded" and "legacy-disabled" are profiles with options of their own and no line (the
    // game's tweak at its initial value, and cleared); "new-profile" has no options file at all.
    const bool noLine = name == "upgraded" || name == "legacy-disabled" || name == "new-profile";
    stored = (name == "opt-out" || name == "row-opt-out") ? 0 : noLine ? -1 : 1;
    if (name == "legacy-disabled") EXTRA_GEFORCE_FX_FEATURES.mValue = 0;
    if (name == "new-profile") PortCeGraphicsOptionsOpened(false, ENOENT);
    unsigned pixelVersion = (name == "pixel-old" || name == "row-low-caps") ? D3DPS_VERSION(1,4) : D3DPS_VERSION(2,0);
    unsigned vertexVersion = name == "vertex-old" ? 0x0100u : 0x0101u;
    PortCeGraphicsBeginDevice(pixelVersion, vertexVersion, name != "formats-missing", name != "float-fallback");
    const bool unsupported = name == "pixel-old" || name == "vertex-old" || name == "formats-missing";
    if (name.compare(0, 4, "row-") == 0)
    {
        LT.m_d3dCaps.PixelShaderVersion = pixelVersion;
        CnVidia row;
        if (name == "row-low-caps")
        {
            Check(row.GetNumChoices() == 1 && std::wstring(row.GetChoiceText(0)) == L"Unavailable",
                  "low-capability device retains the stock unavailable row");
        }
        else if (name == "row-opt-out")
        {
            Check(row.GetNumChoices() == 2 && row.GetValue() == 0 && std::wstring(row.GetChoiceText(0)) == L"No",
                  "saved opt-out is a plain No");
        }
        else
        {
            Check(row.GetNumChoices() == 2 && row.GetValue() == 1 && std::wstring(row.GetChoiceText(1)) == L"Yes",
                  "working requested feature is a plain Yes");
            pixelSucceeds = false;
            CDXWater water;
            Check(water.InitDeviceObjects() == S_OK && !EXTRA_GEFORCE_FX_FEATURES.mValue &&
                  GEFORCE_FX_POWER.mValue && PortCeGraphicsRequested() && stored == 1,
                  "rejected optional shader falls back without changing capability or the saved request");
            if (name == "row-recovered")
            {
                PortCeGraphicsBeginDevice(D3DPS_VERSION(2,0), 0x0101u, true, true);
                pixelSucceeds = true;
                Check(water.InitDeviceObjects() == S_OK && EXTRA_GEFORCE_FX_FEATURES.mValue &&
                      row.GetValue() == 1 && std::wstring(row.GetChoiceText(1)) == L"Yes",
                      "working device recreation clears the unavailable label");
            }
            else
            {
                Check(row.GetValue() == 1 && std::wstring(row.GetChoiceText(row.GetValue())) == L"Yes (not active)" &&
                      std::wstring(row.GetChoiceText(0)) == L"No" && row.GetNumChoices() == 2,
                      "refused feature shows the request and that it is not active, while No stays selectable");
                row.SetValue(0);
                Check(stored == 0 && row.GetValue() == 0 && std::wstring(row.GetChoiceText(0)) == L"No",
                      "the player can turn the refused effect off");
            }
        }
        return failures ? 1 : 0;
    }
    if (faithful)
    {
        PortCeGraphicsResourceOutcome(CE_GRAPHICS_WATER, false);
        PortCeGraphicsFinishDevice(false);
        Check(!PortCeGraphicsRequestFailed(), "ordinary comparison and private runs keep the stock graphics row");
        Check(PortCeGraphicsStoreRequest(0), "faithful setter proceeds through native path");
        Check(GEFORCE_FX_POWER.mValue == 0 && EXTRA_GEFORCE_FX_FEATURES.mValue == 1 && R32F_WORKS.mValue == 0,
              "faithful policy preserves every native tweak");
        Check(reads == 0 && writes == 0 && logCount == 0, "faithful policy does no settings or diagnostic work");
    }
    else if (unsupported)
        Check(!GEFORCE_FX_POWER.mValue && !EXTRA_GEFORCE_FX_FEATURES.mValue, "required capability absence refuses extras");
    else
    {
        Check(GEFORCE_FX_POWER.mValue == 1, "supported device is admitted without a legacy name match");
        Check(R32F_WORKS.mValue == (name != "float-fallback"), "actual available floating target selects its path");
        Check(reads == 1, "one preference read per process");
        if (name == "new-profile")
        {
            Check(EXTRA_GEFORCE_FX_FEATURES.mValue == 1 && stored == 1 && writes == 1, "a new profile gets the extra features and its choice is written once");
            PortCeGraphicsBeginDevice(D3DPS_VERSION(2,0), 0x0101u, true, true);
            Check(EXTRA_GEFORCE_FX_FEATURES.mValue == 1 && writes == 1 && reads == 1, "a device reset neither reads nor writes the choice again");
        }
        else if (name == "opt-out" || noLine)
        {
            Check(!EXTRA_GEFORCE_FX_FEATURES.mValue, name == "opt-out" ? "a saved No is retained"
                  : "a profile with options of its own and no saved choice keeps the extra features off, whatever the game's tweak holds");
            Check(writes == 0 && stored == (name == "opt-out" ? 0 : -1), "nothing is written for a profile that is not new");
            PortCeGraphicsResourceOutcome(CE_GRAPHICS_WATER, false);
            Check(GEFORCE_FX_POWER.mValue == 1, "unrequested resources do not mark capable hardware unavailable");
            PortCeGraphicsBeginDevice(D3DPS_VERSION(2,0), 0x0101u, true, true);
            Check(!EXTRA_GEFORCE_FX_FEATURES.mValue && GEFORCE_FX_POWER.mValue == 1, "a device reset keeps the row offered and off");
        }
        else if (name.compare(0, 10, "landscape-") == 0 && name != "landscape-failed")
        {
            MY_READABLE_Z_BUFFER.mValue = name == "landscape-readable-surface" || name == "landscape-readable-texture" || name == "landscape-float-failed";
            OTHER_ZBUFFER_AS_TEXTURE.mValue = name == "landscape-readable-texture";
            pixelSucceeds = name != "landscape-pixel-failed";
            textureSucceeds = name != "landscape-texture-failed";
            depthSucceeds = name != "landscape-depth-failed";
            textureFailAt = name == "landscape-float-failed" ? 2 : 0;
            CDXLandscape landscape;
            Check(landscape.InitDeviceObjects() == S_OK, "actual landscape optional-resource refusal is a normal fallback");
            const bool expected = pixelSucceeds && textureSucceeds && depthSucceeds && name != "landscape-float-failed";
            Check((EXTRA_GEFORCE_FX_FEATURES.mValue != 0) == expected,
                  "actual selected landscape path controls activation without requiring unused resources");
            Check(GEFORCE_FX_POWER.mValue == 1, "a refused landscape resource leaves the capability tweak as the device set it");
        }
        else if (name == "save-refused" || name == "save-off")
        {
            writeSucceeds = name != "save-refused";
            CnVidia menu; menu.SetValue(0);
            if (writeSucceeds)
                Check(stored == 0 && !EXTRA_GEFORCE_FX_FEATURES.mValue && displayChanges == 1 && notices == 0,
                      "actual native setter applies and retains the stored request");
            else
                Check(stored == 1 && writes == 1 && !EXTRA_GEFORCE_FX_FEATURES.mValue && displayChanges == 1 && notices == 1,
                      "with ce.ini refusing the write the actual native setter still applies the choice for the session and reports that it was not saved");
            // The device reset that Apply causes takes the session's choice, saved or not; a later Yes is taken the same way.
            PortCeGraphicsBeginDevice(D3DPS_VERSION(2,0), 0x0101u, true, true);
            Check(!EXTRA_GEFORCE_FX_FEATURES.mValue && GEFORCE_FX_POWER.mValue == 1 && reads == 1, "the choice holds across the device reset that applies it");
            menu.SetValue(1);
            PortCeGraphicsBeginDevice(D3DPS_VERSION(2,0), 0x0101u, true, true);
            Check(EXTRA_GEFORCE_FX_FEATURES.mValue == 1 && displayChanges == 2 && notices == (writeSucceeds ? 0 : 2),
                  "choosing Yes again applies for the session too");
        }
        else
        {
            pixelSucceeds = name != "pixel-create-failed";
            vertexSucceeds = name != "vertex-create-failed";
            vertexNativeSucceeds = name != "vertex-native-failed";
            CDXWater water;
            Check(water.InitDeviceObjects() == S_OK, "optional native resource failure remains a normal fallback");
            bool failed = !pixelSucceeds || !vertexSucceeds || !vertexNativeSucceeds;
            if (name == "landscape-failed") { PortCeGraphicsResourceOutcome(CE_GRAPHICS_LANDSCAPE, false); failed = true; }
            if (name == "particles-failed") { PortCeGraphicsResourceOutcome(CE_GRAPHICS_PARTICLES, false); failed = true; }
            if (name == "device-failed") { PortCeGraphicsFinishDevice(false); failed = true; }
            Check((EXTRA_GEFORCE_FX_FEATURES.mValue != 0) == !failed, "observed resource result controls activation");
            // The game lays the shore surf out by the capability tweak when a level loads and reads it again at every water draw:
            // a refused resource switches the features off and leaves that tweak as the device set it.
            Check(GEFORCE_FX_POWER.mValue == 1, "a refused resource leaves the capability tweak as the device set it");
            if (failed)
            {
                PortCeGraphicsResourceOutcome(CE_GRAPHICS_WATER, true);
                Check(!EXTRA_GEFORCE_FX_FEATURES.mValue && GEFORCE_FX_POWER.mValue == 1, "late success cannot revive an incomplete resource generation");
                PortCeGraphicsBeginDevice(D3DPS_VERSION(2,0), 0x0101u, true, true);
                Check(EXTRA_GEFORCE_FX_FEATURES.mValue == 1 && reads == 1, "a new generation retries without losing user intent");
            }
        }
    }
    std::cout << "graphics-policy " << name << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
