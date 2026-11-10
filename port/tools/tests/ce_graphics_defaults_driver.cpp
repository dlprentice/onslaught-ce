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

CTweakSLONG GEFORCE_FX_POWER(0), EXTRA_GEFORCE_FX_FEATURES(0), R32F_WORKS(0), MY_READABLE_Z_BUFFER(0);
namespace {
bool layer = true, opened, rowsAccepted = true, iniWritable = true;
int openError = ENOENT, reads, writes, logs, failures, displayChanges;
int userFile = 1, userFileAsks;	// the profile's own options file: 1 present, 0 absent, -1 not known
int quality = -1, qualityCalls;
std::map<std::string, int> ini;	// the valid lines of this case's ce.ini: key -> 0 or 1
std::vector<std::string> written;	// "key=value" of every CE_SettingSet, in order
int fullModels = -1, terrainDetail = -1, fullCalls, terrainCalls;	// what the two beyond-retail rows were last asked for (-1: never), and how often
void Check(bool condition, const char *label) { if (!condition) { ++failures; std::cout << "FAIL " << label << '\n'; } }
}
bool PortCeLayer() { return layer; }
extern "C" int CE_SettingGetBool(const char *key, int fallback) { ++reads; auto at = ini.find(key); return at == ini.end() ? fallback : at->second; }
extern "C" int CE_SettingSet(const char *key, int value)
{
    ++writes; written.push_back(std::string(key) + "=" + std::to_string(value));
    if (iniWritable) ini[key] = value;
    return iniWritable ? 1 : 0;
}
// The layer's own refusal (a faithful run) is the only reason a request of the two rows fails; a value other than 0 or 1 is never asked for.
extern "C" int CE_FullModelsRequest(int value) { ++fullCalls; Check(value == 0 || value == 1, "Full models is asked for 0 or 1"); if (!rowsAccepted) return 0; fullModels = value; return 1; }
extern "C" int CE_TerrainDetailRequest(int value) { ++terrainCalls; Check(value == 0 || value == 1, "Terrain detail distance is asked for 0 or 1"); if (!rowsAccepted) return 0; terrainDetail = value; return 1; }
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

// The highest multisample type a device mask lists (bits 5 to 9 stand for 2, 3, 4, 6 and 8 samples), -1 for none: what the
// fresh anti-aliasing default has to reach, worked out here from the mask and not by the native row.
int HighestSamples(unsigned mask)
{
    static const int samples[] = {2, 3, 4, 6, 8};
    int best = -1;
    for (int i = 0; i < 5; ++i) if (mask & (0x20u << i)) best = samples[i];
    return best;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const std::string name = argv[1];
    if (name == "existing" || name == "retail-leftover" || name == "profile-unknown" || name == "retail-leftover-opt-out") opened = true;
    if (name == "retail-leftover" || name == "retail-leftover-opt-out") userFile = 0;	// the read was served by the game folder's file
    if (name == "profile-unknown") userFile = -1;
    if (name == "permission") openError = EACCES;
    if (name == "io-error") openError = EIO;
    if (name == "unclassified") openError = 0;
    if (name == "opt-out" || name == "retail-leftover-opt-out") ini["extra_graphics"] = 0;	// the extras row alone
    if (name == "rows-saved-off") { ini["full_models"] = 0; ini["terrain_detail_distance"] = 0; }
    if (name == "rows-saved-on") { ini["full_models"] = 1; ini["terrain_detail_distance"] = 1; }
    if (name == "rows-one-saved") ini["full_models"] = 0;
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
    PortCeGraphicsBeginDevice(name == "unsupported" ? 0x0104u : 0x0200u, 0x0101u, true, true);
    const bool faithful = name == "private" || name == "layer-off" || name.compare(0, 6, "fence-") == 0;
    const bool fresh = !faithful && name != "existing" && name != "permission" && name != "io-error" &&
                       name != "unclassified" && name != "no-observation" && name != "subsequent-existing" &&
                       name != "profile-unknown";
    if (fresh) {
        CShadowDetail shadows(AuthoredText, 0); CTreeDetail geometry(AuthoredText, 0);
        CLandscapeRes landscape(AuthoredText, 0); CTrilinear filtering(AuthoredText, 0);
        CTextureRes textures(AuthoredText, 0); CEnable32Bit colour(AuthoredText, 0);
        CMultiSample antialiasing(AuthoredText, 0);
        Check(antialiasing.GetNumChoices() > 1 ? (int)LT.device.MultiSampleType == HighestSamples(mode.mUnknown10) &&
                                                     antialiasing.GetValue() == antialiasing.GetNumChoices() - 1
                                               : (int)LT.device.MultiSampleType == -1 && antialiasing.GetValue() == 0,
              "fresh anti-aliasing takes the highest level the device lists, and nothing when it lists none");
        Check(antialiasing.GetNumChoices() == (HighestSamples(mode.mUnknown10) < 0 ? 1 : 1 + __builtin_popcount(mode.mUnknown10 & 0x3e0u)),
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
        // texture and colour always ask the device to reset; anti-aliasing does too when it was changed
        const int changes = antialiasing.GetNumChoices() > 1 ? 3 : 2;
        // The beyond-retail rows: a valid line of ce.ini is a saved choice, requested as it stands and never written again; no line asks
        // for the fresh choice and writes it once (Terrain detail distance: Extended, 1, with the terrain reserve behind it; Full models: Off, 0, one choice below its maximum, the cost of
        // the maximums being almost all Full models); a refused request writes nothing; an unwritable ce.ini still leaves the session request.
        const bool fullSaved = name == "rows-saved-off" || name == "rows-saved-on" || name == "rows-one-saved";
        const bool terrainSaved = name == "rows-saved-off" || name == "rows-saved-on";
        const int wantFull = name == "rows-refused" ? -1 : name == "rows-saved-on" ? 1 : 0;
        const int wantTerrain = name == "rows-refused" ? -1 : name == "rows-saved-off" ? 0 : 1;
        std::vector<std::string> wantWrites;
        if (name != "rows-refused") {
            if (!fullSaved) wantWrites.push_back("full_models=0");
            if (!terrainSaved) wantWrites.push_back("terrain_detail_distance=1");
        }
        Check(fullModels == wantFull && terrainDetail == wantTerrain && fullCalls == 1 && terrainCalls == 1,
              "a fresh profile asks for Full models and Terrain detail distance once, at the saved choice or the fresh one (Full models Off, Terrain detail distance Extended)");
        Check(written == wantWrites && writes == (int)wantWrites.size(),
              "a fresh profile writes only the beyond-retail rows ce.ini has no valid line for, with the fresh choice");
        Check(displayChanges == changes, "native setters retain reset side effects");
        quality = -9;
        PortCeGraphicsBeginDevice(0x0200u, 0x0101u, true, true);
        Check(quality == -9 && qualityCalls == 1 && displayChanges == changes && fullCalls == 1 && terrainCalls == 1 && writes == (int)wantWrites.size(),
              "later device resets do not reapply fresh defaults over a user change");
    } else {
        Check((int)LT.device.MultiSampleType == -1, "existing unreadable unobserved and faithful options keep the device's own anti-aliasing");
        Check(quality == -1 && qualityCalls == 0 && !SHADOWS.mShadows && LANDSCAPE_MAXLEVELS_USER.mValue == 1 &&
              RENDERSTATE_DISALLOW_MIPMAPPING.mValue == 1 && USER_TEXTURE_RES_LOSS_SHIFT.mValue == 2 &&
              USER_TEXTURE_ALLOW_32_BIT.mValue == 0 && displayChanges == 0,
              "existing unreadable unobserved and faithful options retain every authored initial visual choice");
        Check(fullCalls == 0 && terrainCalls == 0 && writes == 0,
              "existing unreadable unobserved and faithful options leave the beyond-retail rows and ce.ini alone");
        if (faithful) Check(reads == 0 && writes == 0 && logs == 0, "faithful mode adds no settings or log activity");
    }
    const bool somethingOpened = opened && name != "no-observation";
    Check(userFileAsks == (faithful || !somethingOpened ? 0 : 1),
          "the profile's own file is asked about once, only after a successful open, and never by faithful runs");
    std::cout << "graphics-defaults " << name << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
