// SPDX-License-Identifier: GPL-3.0-only
// Run the actual options-open hook and native visual setters against authored storage/device boundaries.
#include "ce_graphics_policy_support.h"
#include "ce_graphics_policy.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// The four tweaks the policy reads start at the game's own initial values (the feature tweak starts set and no options file holds
// it); ce_graphics_defaults_tests.py compares every stand-in on this line with the game's definition.
CTweakSLONG GEFORCE_FX_POWER(0), EXTRA_GEFORCE_FX_FEATURES(1), R32F_WORKS(0), MY_READABLE_Z_BUFFER(0);
namespace {
bool layer = true, opened, rowsAccepted = true, iniWritable = true;
int openError = ENOENT, reads, writes, logs, failures, displayChanges;
int userFile = 1, userFileAsks;	// the profile's own options file: 1 present, 0 absent, -1 not known
int quality = -1, qualityCalls;
std::map<std::string, int> ini;	// the valid lines of this case's ce.ini: key -> value
std::vector<std::string> written;	// "key=value" of every CE_SettingSet, in order
int unitModels = -1, treeModels = -1, terrainDetail = -1, anisoLevel = -1, waterRes = -1, shadowRes = -1;
int unitCalls, treeCalls, terrainCalls, anisoCalls, waterCalls, shadowCalls;
void Check(bool condition, const char *label) { if (!condition) { ++failures; std::cout << "FAIL " << label << '\n'; } }
}
bool PortCeLayer() { return layer; }
extern "C" int CE_SettingGetBool(const char *key, int fallback) { ++reads; auto at = ini.find(key); return at == ini.end() ? fallback : at->second; }
extern "C" int CE_SettingGetText(const char *key, char *out, int n)
{
    ++reads;
    auto at = ini.find(key);
    if (at == ini.end() || !out || n <= 0) return 0;
    const std::string text = std::to_string(at->second);
    if (static_cast<int>(text.size()) >= n) return 0;
    std::snprintf(out, static_cast<size_t>(n), "%s", text.c_str());
    return 1;
}
extern "C" int CE_SettingSet(const char *key, int value)
{
    ++writes; written.push_back(std::string(key) + "=" + std::to_string(value));
    if (iniWritable) ini[key] = value;
    return iniWritable ? 1 : 0;
}
// The layer's own refusal (a faithful run) is the only reason a request of the beyond-retail rows fails.
extern "C" int CE_FullUnitModelsRequest(int value) { ++unitCalls; Check(value == 0 || value == 1, "High-detail units is asked for 0 or 1"); if (!rowsAccepted) return 0; unitModels = value; return 1; }
extern "C" int CE_FullTreeModelsRequest(int value) { ++treeCalls; Check(value == 0 || value == 1, "3D distant trees is asked for 0 or 1"); if (!rowsAccepted) return 0; treeModels = value; return 1; }
extern "C" int CE_TerrainDetailRequest(int value) { ++terrainCalls; Check(value == 0 || value == 1, "Terrain detail distance is asked for 0 or 1"); if (!rowsAccepted) return 0; terrainDetail = value; return 1; }
extern "C" int CE_AnisoRequest(int value) { ++anisoCalls; Check(value == 0 || value == 4 || value == 8 || value == 16, "Anisotropic filtering is asked for 0, 4, 8 or 16"); if (!rowsAccepted) return 0; anisoLevel = value; return 1; }
extern "C" unsigned CE_AnisoHardwareMax() { return 16u; }
extern "C" int CE_WaterReflectionRequest(int value) { ++waterCalls; Check(value == 256 || value == 512 || value == 1024, "Water reflections is asked for 256, 512 or 1024"); if (!rowsAccepted) return 0; waterRes = value; return 1; }
extern "C" int CE_DynamicShadowRequest(int value) { ++shadowCalls; Check(value == 64 || value == 128 || value == 256, "Dynamic shadows is asked for 64, 128 or 256"); if (!rowsAccepted) return 0; shadowRes = value; return 1; }
extern "C" unsigned CeRenderTargetQualityHardwareMaxDim() { return 1024u; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) { ++logs; }
void DisplaySettingChanged() { ++displayChanges; }
int PortUserFileState(const char *name) { ++userFileAsks; Check(std::string(name) == "defaultoptions.bea", "the profile's own options file is asked for by the game's name"); return userFile; }
FILE *AuthoredOpenOptions(const char *, const char *) { errno = openError; return opened ? reinterpret_cast<FILE *>(1) : nullptr; }
const char *DEFAULT_OPTIONS_NAME = "authored-options";
constexpr int CE_BOOT_OPTIONS = 0;
FILE *CE_BootOpenLocal(int, const char *name, const char *access) { return AuthoredOpenOptions(name, access); }

enum EPCText { AuthoredText };
struct NativeTweak {
    int mValue; bool set = true;
    explicit NativeTweak(int value) : mValue(value) {}
    void Set(float value) { mValue = static_cast<int>(value); set = true; }
    bool IsSet() const { return set; }
};
NativeTweak LANDSCAPE_MAXLEVELS(3), LANDSCAPE_MAXLEVELS_USER(1), RENDERSTATE_DISALLOW_MIPMAPPING(1);
NativeTweak TEXTURE_RES_LOSS_SHIFT(1), USER_TEXTURE_RES_LOSS_SHIFT(2);
NativeTweak TEXTURE_ALLOW_32_BIT(1), USER_TEXTURE_ALLOW_32_BIT(0);
struct { bool mShadows = false, mBlobShadows = true; } SHADOWS;
struct NativeMode { int mUnknown10 = 32; } mode;
NativeMode *CurrentMode() { return &mode; }
typedef NativeMode D3DModeInfo;
enum D3DMULTISAMPLE_TYPE { D3DMULTISAMPLE_NONE = 0 };
struct { int mValue = 0; } SUGGEST_MULTISAMPLE_TYPE;
struct NativeDevice { D3DMULTISAMPLE_TYPE MultiSampleType = (D3DMULTISAMPLE_TYPE)-1; };
typedef NativeDevice D3DDeviceInfo;
struct { NativeDevice device; NativeDevice *GetCurrentDevice() { return &device; } } LT;
namespace CRTMesh {
void SetQualityLevel(int value) { quality = value; ++qualityCalls; }
int GetQualityLevel() { return quality; }
}
#define DECLARE_VISUAL(name) struct name { name(EPCText, int) {} int GetNumChoices(); int GetValue(); void SetValue(int); }
DECLARE_VISUAL(CShadowDetail);
DECLARE_VISUAL(CTreeDetail);
DECLARE_VISUAL(CLandscapeRes);
DECLARE_VISUAL(CTrilinear);
DECLARE_VISUAL(CTextureRes);
DECLARE_VISUAL(CEnable32Bit);
DECLARE_VISUAL(CMultiSample);
#undef DECLARE_VISUAL
#define fopen AuthoredOpenOptions
#include "ce_graphics_defaults_actual.inc"
#undef fopen

// The highest multisample type up to 4x a device mask lists (bits 5 to 7 stand for 2, 3 and 4 samples), -1 for none:
// fresh anti-aliasing caps at 4x MSAA so fresh installs do not default to 8x MSAA.
int HighestSamples(unsigned mask)
{
    static const int samples[] = {2, 3, 4};
    int best = -1;
    for (int i = 0; i < 3; ++i) if (mask & (0x20u << i)) best = samples[i];
    return best;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    const std::string name = argv[1];
    // Further arguments are valid lines this start finds in ce.ini (key=value): what an earlier start of the same profile wrote.
    for (int i = 2; i < argc; ++i) {
        const std::string line = argv[i];
        const std::string::size_type cut = line.find('=');
        if (cut == std::string::npos || cut + 1 >= line.size()) return 2;
        char *end = nullptr;
        const long parsed = std::strtol(line.c_str() + cut + 1, &end, 10);
        if (!end || *end) return 2;
        ini[line.substr(0, cut)] = static_cast<int>(parsed);
    }
    if (name == "existing" || name == "existing-saved-on" || name == "existing-saved-off" || name == "retail-leftover" || name == "profile-unknown" ||
        name == "retail-leftover-opt-out") opened = true;
    if (name == "existing-saved-on") ini["extra_graphics"] = 1;	// the player chose Yes in an earlier session
    if (name == "existing-saved-off") ini["extra_graphics"] = 0;
    if (name == "retail-leftover" || name == "retail-leftover-opt-out") userFile = 0;	// the read was served by the game folder's file
    if (name == "profile-unknown") userFile = -1;
    if (name == "permission") openError = EACCES;
    if (name == "io-error") openError = EIO;
    if (name == "unclassified") openError = 0;
    if (name == "opt-out" || name == "retail-leftover-opt-out") ini["extra_graphics"] = 0;	// the extras row alone
    if (name == "rows-saved-off") {
        ini["full_unit_models"] = 0; ini["full_tree_models"] = 0; ini["terrain_detail_distance"] = 0;
        ini["anisotropic_filtering"] = 0; ini["water_reflection_res"] = 256; ini["dynamic_shadow_res"] = 64;
    }
    if (name == "rows-saved-on") {
        ini["full_unit_models"] = 1; ini["full_tree_models"] = 1; ini["terrain_detail_distance"] = 1;
        ini["anisotropic_filtering"] = 8; ini["water_reflection_res"] = 1024; ini["dynamic_shadow_res"] = 256;
    }
    if (name == "rows-legacy-on") ini["full_models"] = 1;
    if (name == "rows-one-saved") ini["full_unit_models"] = 0;
    if (name == "rows-unwritable") iniWritable = false;
    if (name == "rows-refused") rowsAccepted = false;
    if (name == "antialiasing-levels") mode.mUnknown10 = 0x20 | 0x80 | 0x200;	// 2x, 4x and 8x
    if (name == "antialiasing-all-levels") mode.mUnknown10 = 0x3e0;	// 2, 3, 4, 6 and 8 samples
    if (name == "colour16") mode.mUnknown10 = 16;
    if (name == "no-antialiasing") mode.mUnknown10 = 0x04;	// no multisample type supported
    if (name == "landscape-limited") LANDSCAPE_MAXLEVELS.mValue = 1;
    if (name == "layer-off") layer = false;
    if (name.compare(0, 6, "fence-") == 0) setenv(("BEA_PORT_" + name.substr(6)).c_str(), "1", 1);
    if (name != "no-observation") OpenNativeOptions();
    if (name == "subsequent-existing") { opened = true; OpenNativeOptions(); }
    // The Extra graphical features choice this start finds in ce.ini (-1: no valid line), read before the policy can write one.
    const int extrasSaved = ini.count("extra_graphics") ? ini["extra_graphics"] : -1;
    const bool capableDevice = name != "unsupported";
    PortCeGraphicsBeginDevice(capableDevice ? 0x0200u : 0x0104u, 0x0101u, true, true);
    const bool faithful = name == "private" || name == "layer-off" || name.compare(0, 6, "fence-") == 0;
    const bool fresh = !faithful && name != "existing" && name != "existing-saved-on" && name != "existing-saved-off" && name != "permission" &&
                       name != "io-error" && name != "unclassified" && name != "no-observation" && name != "subsequent-existing" &&
                       name != "profile-unknown";
    if (faithful)
        Check(GEFORCE_FX_POWER.mValue == 0 && EXTRA_GEFORCE_FX_FEATURES.mValue == 1, "a faithful run leaves both tweaks at the game's initial values");
    else {
        const bool wanted = extrasSaved >= 0 ? extrasSaved == 1 : fresh;
        Check(GEFORCE_FX_POWER.mValue == (capableDevice ? 1 : 0), "the capability tweak follows the device, whatever the profile asks for");
        Check(EXTRA_GEFORCE_FX_FEATURES.mValue == (capableDevice && wanted ? 1 : 0),
              "the extra features follow the saved choice, and with none are on for a new profile only: the game's own tweak decides nothing");
    }
    if (fresh) {
        CShadowDetail shadows(AuthoredText, 0); CTreeDetail geometry(AuthoredText, 0);
        CLandscapeRes landscape(AuthoredText, 0); CTrilinear filtering(AuthoredText, 0);
        CTextureRes textures(AuthoredText, 0); CEnable32Bit colour(AuthoredText, 0);
        CMultiSample antialiasing(AuthoredText, 0);
        Check(HighestSamples(mode.mUnknown10) > 0 ? (int)LT.device.MultiSampleType == HighestSamples(mode.mUnknown10) &&
                                                     antialiasing.GetValue() > 0
                                               : (int)LT.device.MultiSampleType == -1 && antialiasing.GetValue() == 0,
              "fresh anti-aliasing takes the highest level up to 4x the device lists, and nothing when it lists none");
        Check(antialiasing.GetNumChoices() == ((mode.mUnknown10 & 0x3e0u) == 0u ? 1 : 1 + __builtin_popcount(mode.mUnknown10 & 0x3e0u)),
              "the row lists None and one level for every multisample type the device offers");
        Check(qualityCalls == 1 && geometry.GetValue() == geometry.GetNumChoices() - 1,
              "fresh geometry uses the actual native quality setter at its highest choice");
        Check(shadows.GetValue() == shadows.GetNumChoices() - 1 && SHADOWS.mShadows,
              "fresh shadow choice enables native full shadows");
        Check(landscape.GetValue() == landscape.GetNumChoices() - 1, "fresh landscape respects native capability-dependent choice count");
        Check(filtering.GetValue() == filtering.GetNumChoices() - 1 && !RENDERSTATE_DISALLOW_MIPMAPPING.mValue,
              "fresh filtering enables native trilinear mipmapping");
        Check(textures.GetValue() == 0 && !USER_TEXTURE_RES_LOSS_SHIFT.mValue,
              "fresh texture selection retains full native texture resolution");
        Check(colour.GetValue() == colour.GetNumChoices() - 1,
              "fresh colour selection respects the current native mode support");
        Check(EXTRA_GEFORCE_FX_FEATURES.mValue == (name != "opt-out" && name != "retail-leftover-opt-out" && name != "unsupported"),
              "fresh supported extras default on while saved opt-out and capability refusal win");
        Check((extrasSaved < 0) == (name != "opt-out" && name != "retail-leftover-opt-out"), "only the opt-out cases start with an extra_graphics line");
        const int changes = antialiasing.GetNumChoices() > 1 ? 3 : 2;
        const bool legacyOn = name == "rows-legacy-on";
        const bool unitSaved = legacyOn || name == "rows-saved-off" || name == "rows-saved-on" || name == "rows-one-saved";
        const bool treeSaved = legacyOn || name == "rows-saved-off" || name == "rows-saved-on";
        const bool terrainSaved = name == "rows-saved-off" || name == "rows-saved-on";
        const bool anisoSaved = name == "rows-saved-off" || name == "rows-saved-on";
        const bool waterSaved = name == "rows-saved-off" || name == "rows-saved-on";
        const bool shadowSaved = name == "rows-saved-off" || name == "rows-saved-on";
        const int wantUnit = name == "rows-refused" ? -1 : (name == "rows-saved-off" || name == "rows-one-saved") ? 0 : 1;
        const int wantTree = name == "rows-refused" ? -1 : (legacyOn || name == "rows-saved-on") ? 1 : 0;
        const int wantTerrain = name == "rows-refused" ? -1 : name == "rows-saved-off" ? 0 : 1;
        const int wantAniso = name == "rows-refused" ? -1 : name == "rows-saved-off" ? 0 : name == "rows-saved-on" ? 8 : 16;
        const int wantWater = name == "rows-refused" ? -1 : name == "rows-saved-off" ? 256 : name == "rows-saved-on" ? 1024 : 512;
        const int wantShadow = name == "rows-refused" ? -1 : name == "rows-saved-off" ? 64 : name == "rows-saved-on" ? 256 : 128;
        std::vector<std::string> wantWrites;
        if (extrasSaved < 0) wantWrites.push_back("extra_graphics=1");
        if (name != "rows-refused") {
            if (!unitSaved) wantWrites.push_back("full_unit_models=1");
            if (!treeSaved) wantWrites.push_back("full_tree_models=0");
            if (!terrainSaved) wantWrites.push_back("terrain_detail_distance=1");
            if (!anisoSaved) wantWrites.push_back("anisotropic_filtering=16");
            if (!waterSaved) wantWrites.push_back("water_reflection_res=512");
            if (!shadowSaved) wantWrites.push_back("dynamic_shadow_res=128");
        }
        Check(unitModels == wantUnit && treeModels == wantTree && terrainDetail == wantTerrain &&
              anisoLevel == wantAniso && waterRes == wantWater && shadowRes == wantShadow &&
              unitCalls == 1 && treeCalls == 1 && terrainCalls == 1 && anisoCalls == 1 && waterCalls == 1 && shadowCalls == 1,
              "a fresh profile asks for every beyond-retail quality row once, at the saved or fresh choice");
        Check(written == wantWrites && writes == (int)wantWrites.size(),
              "a fresh profile writes only the rows ce.ini has no valid line for, with the fresh choice");
        Check(displayChanges == changes, "native setters retain reset side effects");
        quality = -9;
        PortCeGraphicsBeginDevice(0x0200u, 0x0101u, true, true);
        Check(quality == -9 && qualityCalls == 1 && displayChanges == changes &&
              unitCalls == 1 && treeCalls == 1 && terrainCalls == 1 && anisoCalls == 1 && waterCalls == 1 && shadowCalls == 1 &&
              writes == (int)wantWrites.size(),
              "later device resets do not reapply fresh defaults over a user change");
    } else {
        Check((int)LT.device.MultiSampleType == -1, "existing unreadable unobserved and faithful options keep the device's own anti-aliasing");
        Check(quality == -1 && qualityCalls == 0 && !SHADOWS.mShadows && LANDSCAPE_MAXLEVELS_USER.mValue == 1 &&
              RENDERSTATE_DISALLOW_MIPMAPPING.mValue == 1 && USER_TEXTURE_RES_LOSS_SHIFT.mValue == 2 &&
              USER_TEXTURE_ALLOW_32_BIT.mValue == 0 && displayChanges == 0,
              "existing unreadable unobserved and faithful options retain every authored initial visual choice");
        Check(unitCalls == 0 && treeCalls == 0 && terrainCalls == 0 && anisoCalls == 0 && waterCalls == 0 && shadowCalls == 0 && writes == 0,
              "existing unreadable unobserved and faithful options leave the beyond-retail rows and ce.ini alone");
        if (faithful) Check(reads == 0 && writes == 0 && logs == 0, "faithful mode adds no settings or log activity");
    }
    const bool somethingOpened = opened && name != "no-observation";
    Check(userFileAsks == (faithful || !somethingOpened ? 0 : 1),
          "the profile's own file is asked about once, only after a successful open, and never by faithful runs");
    for (const std::string &line : written) std::cout << "wrote " << line << '\n';
    std::cout << "state capable=" << GEFORCE_FX_POWER.mValue << " extras=" << EXTRA_GEFORCE_FX_FEATURES.mValue << '\n';
    std::cout << "graphics-defaults " << name << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
