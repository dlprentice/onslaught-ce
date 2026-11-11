// SPDX-License-Identifier: GPL-3.0-only
// Run the game's actual Overall Detail level, landscape and anti-aliasing rows with the production policy; substitute only the
// other rows, the device and the tweaks. Every line printed is a selection or a level the menu shows, nothing else.
#include "ce_graphics_policy_support.h"
#include "ce_graphics_policy.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

CTweakSLONG GEFORCE_FX_POWER(0), EXTRA_GEFORCE_FX_FEATURES(0), R32F_WORKS(0), MY_READABLE_Z_BUFFER(0);
namespace { bool layer = true; int failures; }
bool PortCeLayer() { return layer; }
extern "C" int CE_SettingGetBool(const char *, int fallback) { return fallback; }
extern "C" int CE_SettingSet(const char *, int) { return 1; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}
int PortUserFileState(const char *) { return 1; }
void PortCeGraphicsApplyFreshDefaults() {}
extern "C" int CE_FullModelsRequest(int) { ++failures; return 0; }	// the fresh defaults are never taken here
extern "C" int CE_TerrainDetailRequest(int) { ++failures; return 0; }

typedef wchar_t WCHAR;
typedef int EPCText;
enum D3DMULTISAMPLE_TYPE { D3DMULTISAMPLE_NONE = 0 };
struct Tweak { int mValue; void Set(float v) { mValue = (int)v; } Tweak &operator=(int v) { mValue = v; return *this; } operator int() const { return mValue; } };
Tweak LANDSCAPE_MAXLEVELS{5}, LANDSCAPE_MAXLEVELS_USER{-1}, FORCE_VSYNC{1}, TEXTURE_RES_LOSS_SHIFT{0}, SUGGEST_MULTISAMPLE_TYPE{0};
struct Device { unsigned dwLowDetailMode = 0, dwMediumDetailMode = 1, dwHighDetailMode = 2; D3DMULTISAMPLE_TYPE MultiSampleType = (D3DMULTISAMPLE_TYPE)-1; };
typedef Device D3DDeviceInfo;
struct { Device device; Device *GetCurrentDevice() { return &device; } } LT;
struct D3DModeInfo { unsigned mUnknown10 = 0x04; } gMode;
D3DModeInfo *CurrentMode() { return &gMode; }
void DisplaySettingChanged() {}
static WCHAR textBuffer[8];
WCHAR *Localization_GetStringById(int) { textBuffer[0] = 0; return textBuffer; }

struct CMenuMenuItem
{
    int mSelection = 0, mValue = 0, choices = 2, preferred = 0;
    CMenuMenuItem() {}
    CMenuMenuItem(EPCText, int, bool) {}
    virtual ~CMenuMenuItem() {}
    virtual int GetSelection() { return mSelection; }
    virtual void SetSelection(int v) { mSelection = v; }
    virtual int GetNumChoices() { return choices; }
    virtual int GetDefault() { return preferred; }
    virtual void SetValue(int v) { mSelection = mValue = v; }
    virtual int GetValue() { return mSelection; }
    virtual WCHAR *GetChoiceText(int) { return textBuffer; }
};
struct CThreeWay : CMenuMenuItem {};
#define private public
#define protected public
#include "ce_detail_level_actual.inc"
#undef private
#undef protected
WCHAR *CVideoDetailLevel::GetChoiceText(int) { return textBuffer; }

// What the menu holds after the native setters of the fresh defaults ran: each row's selection is what its own getter reads.
struct Menu
{
    CMenuMenuItem resolution, shadow, geometry, trilinear, vsync, texture;
    CThreeWay colour;
    CLandscapeRes landscape{0, 0};
    CMultiSample antialiasing{0, 0};
    CVideoDetailLevel overall{0, 0};
    Menu(unsigned maxLevels, unsigned aaMask)
    {
        LANDSCAPE_MAXLEVELS.mValue = (int)maxLevels; LANDSCAPE_MAXLEVELS_USER.mValue = -1;
        gMode.mUnknown10 = 0x04 | aaMask; LT.device.MultiSampleType = (D3DMULTISAMPLE_TYPE)-1;
        new (&overall) CVideoDetailLevel(0, 0);                     // the presets come from the device's landscape level
        resolution.choices = 3; shadow.choices = geometry.choices = 3; trilinear.choices = vsync.choices = 2; texture.choices = 3;
        colour.choices = 3; colour.preferred = 2;
        overall.mResolution = &resolution; overall.mShadowDetail = &shadow; overall.mTreeDetail = &geometry; overall.mTrilinear = &trilinear;
        overall.mVSync = &vsync; overall.mLandscapeRes = &landscape; overall.mTextureRes = &texture; overall.m32Bit = &colour;
        overall.mMultiSample = &antialiasing;
    }
    // The state of a fresh profile on the desktop: every quality row at its top choice (patch 0204's defaults), the highest
    // anti-aliasing level the device lists (patch 0214; the game's own High preset is its first level), VSync on, the desktop's size selected.
    void FreshDefaults()
    {
        resolution.mSelection = (int)LT.device.dwHighDetailMode;
        shadow.mSelection = 2; geometry.mSelection = 2; trilinear.mSelection = 1; vsync.mSelection = 1;
        landscape.SetValue(landscape.GetNumChoices() - 1); landscape.mSelection = landscape.GetValue();
        texture.mSelection = TEXTURE_RES_LOSS_SHIFT.mValue; colour.mSelection = colour.preferred;
        if (antialiasing.GetNumChoices() > 1) antialiasing.SetValue(antialiasing.GetNumChoices() - 1);
        antialiasing.mSelection = antialiasing.GetValue();
    }
};

static void Check(bool ok, const std::string &label) { if (!ok) { ++failures; std::printf("FAIL %s\n", label.c_str()); } }
enum { Custom = 0, Low = 1, Medium = 2, High = 3 };

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    const std::string mode = argv[1];
    if (mode == "layer-off" || (argc > 2 && !std::strcmp(argv[2], "off"))) layer = false;
    // maxLevels: the device's landscape level (the choice count is 1, 2, 3 or 4 rows deep); aaMask: bits of the supported types
    const unsigned maxLevels[] = {1, 2, 3, 4, 5, 6};
    const unsigned aaMasks[] = {0x00, 0x20, 0x20 | 0x80 | 0x200, 0x3e0};
    const bool wide = mode == "grid";
    (void)wide;
    if (mode == "grid")
    {
        // Every combination of the rows' selections, printed as the level the Overall row reads: the same table must come out of the
        // game's own function (faithful runs) and, for the rows this lane does not touch, of the patched one.
        for (unsigned max : maxLevels) for (unsigned aa : aaMasks)
        {
            Menu menu(max, aa);
            for (int rs = 0; rs < 3; ++rs) for (int sh = 0; sh < 3; ++sh) for (int ge = 0; ge < 3; ++ge) for (int tr = 0; tr < 2; ++tr)
            for (int vs = 0; vs < 2; ++vs) for (int ld = 0; ld < menu.landscape.GetNumChoices(); ++ld) for (int tx = 0; tx < 3; ++tx)
            for (int cb = 0; cb < 3; ++cb) for (int a = 0; a < menu.antialiasing.GetNumChoices(); ++a)
            {
                menu.resolution.mSelection = rs; menu.shadow.mSelection = sh; menu.geometry.mSelection = ge; menu.trilinear.mSelection = tr;
                menu.vsync.mSelection = vs; menu.landscape.mSelection = ld; menu.texture.mSelection = tx; menu.colour.mSelection = cb;
                menu.antialiasing.mSelection = a;
                std::printf("%u %x %d%d%d%d%d%d%d%d%d %d\n", max, aa, rs, sh, ge, tr, vs, ld, tx, cb, a, menu.overall.GetValue());
            }
        }
        return 0;
    }
    for (unsigned max : maxLevels) for (unsigned aa : aaMasks)
    {
        Menu menu(max, aa);
        char label[96];
        std::snprintf(label, sizeof label, "landscape level %u, anti-aliasing bits %x: ", max, aa);
        menu.FreshDefaults();
        const int level = menu.overall.GetValue();
        if (mode == "fresh")
            Check(level == High, std::string(label) + "a fresh profile at the desktop's size reads High");
        if (mode == "fresh-without-anti-aliasing")
        {
            // The coupling the fresh defaults rely on: with no anti-aliasing the High preset is out of reach on a device that offers it.
            menu.antialiasing.mSelection = 0;
            Check((menu.overall.GetValue() == High) == (menu.antialiasing.GetNumChoices() < 2),
                  std::string(label) + "without anti-aliasing the reading is High only where the device offers none");
        }
        if (mode == "layer-off")
        {
            // The game's own reading: its landscape preset is the row's last choice only on a card whose landscape level is 1 or 2, and its High
            // preset names the first anti-aliasing level only, so the fresh profile's highest level reads Custom wherever the device lists more than one.
            Check(!layer, "the layer is off");
            Check((level == High) == (max <= 2 && menu.antialiasing.GetNumChoices() < 3),
                  std::string(label) + "the game's own reading is unchanged outside the ordinary CE menus");
            menu.antialiasing.mSelection = menu.antialiasing.GetNumChoices() > 1 ? 1 : 0;
            Check((menu.overall.GetValue() == High) == (max <= 2), std::string(label) + "the game's own reading with its first anti-aliasing level");
        }
        if (mode == "choose")
        {
            // Choosing High on the Overall row, then the rows as the menu shows them after Apply: High again, with the top landscape choice.
            menu.landscape.mSelection = 0; menu.antialiasing.mSelection = 0; menu.shadow.mSelection = 0;
            menu.overall.SetValue(High);
            Check(menu.landscape.mSelection == menu.landscape.GetNumChoices() - 1, std::string(label) + "High selects the landscape row's last choice");
            menu.landscape.SetValue(menu.landscape.mSelection); menu.landscape.mSelection = menu.landscape.GetValue();
            const int antialiasing = menu.antialiasing.GetNumChoices() > 1 ? 1 : 0;
            Check(menu.antialiasing.mSelection == antialiasing, std::string(label) + "High selects the first anti-aliasing level when there is one");
            menu.antialiasing.mSelection = antialiasing;
            Check(menu.overall.GetValue() == High, std::string(label) + "after choosing High the row still reads High");
            menu.overall.SetValue(Medium);
            menu.landscape.SetValue(menu.landscape.mSelection); menu.landscape.mSelection = menu.landscape.GetValue();
            Check(menu.overall.GetValue() == Medium, std::string(label) + "choosing Medium reads Medium");
        }
        if (mode == "landscape-presets")
        {
            // The device's landscape level decides how many choices the row has and which selections the game's presets name (the numbers are the
            // ones the game's own rows give for levels 1 to 6): High is the row's last choice on every card; Medium names selection 0 up to level 4
            // and one beyond the row's last choice from level 5, where the row's setter leaves its last choice.
            const int choices[] = {1, 1, 2, 3, 4, 4}, afterHigh[] = {0, 0, 1, 2, 3, 3}, afterMedium[] = {0, 0, 0, 0, 3, 3};
            const unsigned index = max - 1;
            Check(menu.landscape.GetNumChoices() == choices[index], std::string(label) + "the landscape row's choices follow the device's level");
            menu.landscape.mSelection = -1;
            menu.overall.SetValue(High);
            Check(menu.landscape.mSelection == afterHigh[index], std::string(label) + "High selects the landscape row's last choice");
            menu.landscape.mSelection = -1;
            menu.overall.SetValue(Medium);
            Check(menu.landscape.mSelection == afterMedium[index], std::string(label) + "Medium selects its own landscape level, or the last choice where it names one beyond the row");
        }
        if (mode == "finer-anti-aliasing")
        {
            const int choices = menu.antialiasing.GetNumChoices();
            for (int a = 1; a < choices; ++a)
            {
                menu.antialiasing.mSelection = a;
                Check(menu.overall.GetValue() == High, std::string(label) + "a finer anti-aliasing level is still High");
            }
            menu.antialiasing.mSelection = 0;
            Check(choices < 2 || menu.overall.GetValue() == Custom, std::string(label) + "no anti-aliasing is not High when the device offers it");
        }
        if (mode == "each-row")
        {
            // One row below its top choice and the reading is not High: no row is ignored.
            struct { CMenuMenuItem *row; int value; const char *name; } below[] = {
                {&menu.shadow, 1, "shadow"}, {&menu.geometry, 1, "geometry"}, {&menu.trilinear, 0, "trilinear"}, {&menu.vsync, 0, "vsync"},
                {&menu.landscape, 0, "landscape"}, {&menu.resolution, 0, "resolution"}, {&menu.texture, 1, "texture"}, {&menu.colour, 1, "colour"}};
            for (auto &entry : below)
            {
                if (entry.row == &menu.landscape && menu.landscape.GetNumChoices() < 2) continue;
                const int kept = entry.row->mSelection;
                entry.row->mSelection = entry.value;
                Check(menu.overall.GetValue() != High, std::string(label) + entry.name + " below its top choice is not High");
                entry.row->mSelection = kept;
            }
        }
    }
    std::printf("detail-level %s failures=%d\n", mode.c_str(), failures);
    return failures ? 1 : 0;
}
