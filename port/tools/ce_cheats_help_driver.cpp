// SPDX-License-Identifier: GPL-3.0-only
// Exercise the actual CE cheat help renderer and retail WordWrap with an authored font and menu.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <cwchar>
#include <string>
#include <vector>

using WCHAR = wchar_t;
using DWORD = uint32_t;
using BOOL = int;
const int FONT_SMALL = 0, GI_BACK = 99;
#define PORT_CE_GOD_CHEAT 3

struct SIZE { int cx, cy; };
struct SWordWrapFill { WCHAR c[40][100]; };
struct Draw { float y; DWORD colour; std::wstring text; int width; };
static std::vector<Draw> draws;

struct CDXBitmapFont {
    int advance = 8;
    bool CanDisplay(WCHAR) { return true; }
    int GetHeight() { return 16; }
    void GetTextExtent(WCHAR *text, SIZE *size) { size->cx = int(wcslen(text)) * advance; size->cy = GetHeight(); }
    int WordWrap(SWordWrapFill *fill, WCHAR *text, float width);
    void DrawTextWithShadow(float, float y, DWORD colour, WCHAR *text, int, float, float, float) {
        SIZE size;
        GetTextExtent(text, &size);
        draws.push_back({y, colour, text, size.cx});
    }
};
static WCHAR *ToWCHAR(const char *text) {
    static WCHAR result[256];
    size_t i = 0;
    while (text[i] && i < 255) { result[i] = (unsigned char)text[i]; ++i; }
    result[i] = 0;
    return result;
}
#include "word_wrap.inc"

struct Platform {
    CDXBitmapFont font;
    CDXBitmapFont *Font(int) { return &font; }
} PLATFORM;
constexpr int FONT_NORMAL = 1, FEP_NONE = 0, FEMB_OPTION_A_CONTINUE = 0, FALSE = 0;
#define FO(value) (value)
#define PORT_LOG_PREFIX ""
struct MessageBox {
    bool open = false;
    SWordWrapFill mLines{};
    int mLineHeight = 0, mNumLines = 0;
    float mHeight = 0, mY = 0;
    bool BeingDisplayed() { return open; }
    void Kill() { open = false; }
    void Create(float, float y, float, int, WCHAR*, CDXBitmapFont* font, int, int, BOOL, int, int) {
        open = true; mY = y; mLineHeight = font->GetHeight();
    }
    void SetOnDismiss(int, int) {}
} FEMESSBOX;
static bool sCeBox = false;
#include "message.inc"

struct CMenuOptionsList { int mSelected = 0; };
struct CMenuItem {
    CMenuItem(DWORD, int) {}
    virtual ~CMenuItem() = default;
    virtual void Render(float, float, BOOL) {}
};
// ce.cpp's switches for the unfinished features: the page tables that follow hold their rows too (the help text is measured for all of them).
static bool experimentalOn = true;
static bool optionalPages = false;
bool PortCeExperimental(const char*) { return experimentalOn; }
// Controls & Tips (in the extracted tables) asks the Dodge Button's conditions; this driver measures the note's lines, not those
// conditions (ce_frontend_tests.py does), so the stand-ins give no pad and a free key.
#define MAX_JOYPADS 4
enum { CE_TUTORIAL_GAMEPAD = 1, CE_TUTORIAL_STICK = 2 };
struct CeTutorialPadInfo { int kind; unsigned axes, buttons; };
struct { int mJoypads = 0; bool mUnknown33474[MAX_JOYPADS] = {}; void *pJoystick[MAX_JOYPADS] = {}; } LT;
extern "C" int CE_SettingGetBool(const char* key, int def)
{
    return !std::strcmp(key, "catalogue") ? optionalPages : def;
}
extern "C" int PortCeTutorialPadInfo(void *, CeTutorialPadInfo *) { return 0; }
bool PortCeDodgeAssistKeyFree() { return true; }
bool PortCeDodgeAssistPadFree(int) { return true; }
#include "../game/ce_catalogue_menu.h"
#include "../game/ce_dodge_binding.h"

bool PortCeMenus() { return true; }
bool PortCeExtrasOffered() { return optionalPages; }
bool PortCeModsPageOffered() { return optionalPages; }
#include "binding_keys.inc"
// Test profile storage and the physical keyboard layout are the external boundaries.
static std::string savedCommander;
extern "C" int CE_SettingGetText(const char* key, char* out, int n) {
    if (std::strcmp(key, "commander_key") || savedCommander.empty()) return 0;
    std::snprintf(out, n, "%s", savedCommander.c_str());
    return 1;
}
extern "C" int CE_SettingSetText(const char* key, const char* value) {
    if (std::strcmp(key, "commander_key")) return 0;
    savedCommander = value;
    return 1;
}
extern "C" int PortCeTutorialPrintableKey(int key, char* out, int n) {
    const char* text = key == DIK_M ? "M" : key == DIK_T ? "T" : key == DIK_E ? "E" : nullptr;
    if (!text || n < 2) return 0;
    std::snprintf(out, n, "%s", text);
    return 1;
}
enum { KEY_ON = 1, KEY_ONCE, KEY_RELEASE };
struct SControlInput { int mPad, mKey, mType; };
struct SControlBinding { int mId; SControlInput mInput[2]; };
// One ordinary control can claim a saved key; the production table ends with id -1.
static SControlBinding CONTROL_BINDINGS[] = {{BUTTON_MECH_MORPH, {}}, {-1, {}}};
#include "binding_conflict.inc"
void PortCeDodgeStrafeNames(char* left, int leftSize, char* right, int rightSize)
{
    std::snprintf(left, leftSize, "%s", "A");
    std::snprintf(right, rightSize, "%s", "D");
}
#include "cheats_help.inc"
// Use the same offered-row policy as the hub. A disabled row need not be at the end.
struct HubRows {
    SCeRow rows[sizeof(kHubRows) / sizeof(kHubRows[0])]{};
    int count = 0;
};
static HubRows visibleHub(bool network, bool experimental)
{
    HubRows hub;
    experimentalOn = experimental;
    for (const SCeRow& row : kHubRows) {
#ifndef CE_NETWORK_OFF
        if (row.mAction == CEA_NETWORK && !network) continue;
#endif
        if (ExperimentalOffered(row.mAction)) hub.rows[hub.count++] = row;
    }
    return hub;
}

// Run the real camera input handler with an identity transform and a fake pause boundary.
// This checks key actions, not the game's camera maths or rendering.
#include "camera_buttons.inc"
struct CController {};
struct FVector {
    float x, y, z;
    FVector(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}
    FVector &operator+=(const FVector &v) { x += v.x; y += v.y; z += v.z; return *this; }
};
struct FMatrix {
    float yaw, pitch, roll;
    FMatrix(float yaw = 0, float pitch = 0, float roll = 0) : yaw(yaw), pitch(pitch), roll(roll) {}
    FMatrix operator*(const FMatrix &m) const { return FMatrix(yaw + m.yaw, pitch + m.pitch, roll + m.roll); }
    FVector operator*(const FVector &v) const { return v; }
};
struct FakeGame {
    bool paused = false;
    int pauses = 0, resumes = 0;
    bool IsPaused() { return paused; }
    void Pause() { paused = true; ++pauses; }
    void UnPause() { paused = false; ++resumes; }
} GAME;
struct CControllableCamera {
    FMatrix mTempOrientation;
    FVector mTempPos;
    void ReceiveButtonAction(CController *from_controller, int button, float val);
};
#include "camera_actions.inc"

static const char *current_page = "";		// the page the help-fit loop is measuring, named in its failures
static void check(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s%s%s%s\n", message, *current_page ? " (page: " : "", current_page, *current_page ? ")" : ""); std::exit(2); }
}
static std::wstring compact(std::wstring text) {
    text.erase(std::remove(text.begin(), text.end(), L' '), text.end());
    return text;
}
static std::wstring drawn(int first, int count) {
    std::wstring text;
    for (int i = first; i < first + count; ++i) text += draws[i].text;
    return compact(text);
}
static int lines(const WCHAR *text, float width = 420.f) {
    SWordWrapFill fill{};
    WCHAR copy[400];
    std::wcsncpy(copy, text, 399);
    copy[399] = 0;
    return PLATFORM.font.WordWrap(&fill, copy, width);
}

static std::wstring displayedTips(bool commander)
{
    experimentalOn = commander;
    constexpr int CEBT_TIPS = 1;
    int mBoxTitle = 0;
    sCeBox = false;
    switch (CEA_TIPS) {
#include "tips_action.inc"
    default: break;
    }
    check(sCeBox && FEMESSBOX.open && mBoxTitle == CEBT_TIPS, "Controls & Tips opens its message");
    std::wstring result;
    for (int i = 0; i < FEMESSBOX.mNumLines; ++i) {
        if (!result.empty()) result += L" ";
        result += FEMESSBOX.mLines.c[i];
    }
    FEMESSBOX.Kill();
    return result;
}

static void checkCommanderHelp()
{
    const auto action = CeControlAction::Commander;
    const auto absent = [](const std::wstring& text) {
        return text.find(L"Zoom:") != std::wstring::npos && text.find(L"commander view") == std::wstring::npos;
    };
    const auto describes = [](const std::wstring& text, const wchar_t* key) {
        return text.find(std::wstring(key) + L": commander view, the battle runs on.") != std::wstring::npos;
    };
    savedCommander.clear();
    PortCeDodgeBindingReload();
    check(describes(displayedTips(true), L"M"), "default Commander key reaches the message box");
    check(absent(displayedTips(false)), "disabled Commander has no key instructions");
    check(PortCeActionStore(action, DIK_T), "the normal action stores a different key");
    PortCeDodgeBindingReload();
    const auto rebound = displayedTips(true);
    check(describes(rebound, L"T") && !describes(rebound, L"M"), "the saved replacement reaches the message box");
    check(absent(displayedTips(false)), "disabled rebound Commander has no key instructions");
    CONTROL_BINDINGS[0].mInput[0] = {0, DIK_T, KEY_ON};
    check(absent(displayedTips(true)), "an occupied saved key must not be advertised");
    CONTROL_BINDINGS[0].mInput[0] = {};
    check(describes(displayedTips(true), L"T"), "releasing the key restores the saved instructions");
}

int main(int argc, char** argv) {
    if (argc == 2 && !std::strcmp(argv[1], "commander-bindings")) {
        checkCommanderHelp();
        std::puts("Commander bindings: six displayed help states passed");
        return 0;
    }
    check(argc == 2 && (!std::strcmp(argv[1], "optional-on") ||
                        !std::strcmp(argv[1], "optional-off")), "select the optional page policy");
    optionalPages = !std::strcmp(argv[1], "optional-on");
    check(PlanText(0) != nullptr, "the first plan still has a description");
    const HubRows all = visibleHub(true, true), plain = visibleHub(true, false);
#ifndef CE_NETWORK_OFF
    const HubRows noNetwork = visibleHub(false, true);
    const int net = 1;
#else
    const int net = 0;
#endif
    experimentalOn = true;
    check(plain.count == 6 + net + (optionalPages ? 3 : 0) &&
          all.count == plain.count + 2, "hub shows only enabled pages");
#ifndef CE_NETWORK_OFF
    check(noNetwork.count == all.count - 1, "network availability hides its row");
#endif

    CControllableCamera camera;
    camera.ReceiveButtonAction(nullptr, BUTTON_CAMERA_MOVE_FORAWRD, -1.f);
    check(camera.mTempPos.y == 1.f, "forward key moves camera forward in the fixture");
    camera.ReceiveButtonAction(nullptr, BUTTON_CAMERA_MOVE_BACKWARDS, 1.f);
    check(camera.mTempPos.y == 0.f, "back key reverses camera movement");
    camera.ReceiveButtonAction(nullptr, BUTTON_CAMERA_STRAFE_LEFT, -1.f);
    check(camera.mTempPos.x == -1.f, "left key strafes left in the fixture");
    camera.ReceiveButtonAction(nullptr, BUTTON_CAMERA_STRAFE_RIGHT, 1.f);
    check(camera.mTempPos.x == 0.f, "right key reverses camera strafe");
    camera.ReceiveButtonAction(nullptr, BUTTON_CAMERA_YAW_LEFT, -1.f);
    check(std::fabs(camera.mTempOrientation.yaw - 1.f / 11.f) < 0.00001f, "left arrow turns camera");
    camera.ReceiveButtonAction(nullptr, BUTTON_CAMERA_YAW_RIGHT, 1.f);
    check(std::fabs(camera.mTempOrientation.yaw) < 0.00001f, "right arrow reverses camera turn");
    camera.ReceiveButtonAction(nullptr, BUTTON_CAMERA_PITCH_UP, 1.f);
    check(std::fabs(camera.mTempOrientation.pitch + 1.f / 11.f) < 0.00001f, "up arrow pitches camera");
    camera.ReceiveButtonAction(nullptr, BUTTON_CAMERA_PITCH_DOWN, -1.f);
    check(std::fabs(camera.mTempOrientation.pitch) < 0.00001f, "down arrow reverses camera pitch");
    camera.ReceiveButtonAction(nullptr, BUTTON_PAUSE, 1.f);
    camera.ReceiveButtonAction(nullptr, BUTTON_PAUSE, 1.f);
    check(!GAME.paused && GAME.pauses == 1 && GAME.resumes == 1, "Escape alternates pause and resume");
    check(CE_NUM_CHEATS == 5, "five supported player-facing cheats");
    const int ids[] = {1, 0, 5, 3, 4};
    for (int i = 0; i < CE_NUM_CHEATS; ++i) check(kCheats[i].mCheat == ids[i], "cheat identity and order");
    for (const WCHAR *key : {L"F:", L"W/S fly", L"A/D slide", L"arrows look", L"F returns", L"still be hit"})
        check(std::wcsstr(kCheats[4].mHint.mHelp, key) != nullptr, "free-camera keys are discoverable");
    for (const WCHAR *missing : {L"speed", L"up/down", L"Space", L"Shift", L"mouse"})
        check(std::wcsstr(kCheats[4].mHint.mHelp, missing) == nullptr, "no free-camera key the game does not have");
    check(std::wcsstr(kCheats[0].mHint.mHelp, L"Select Level screen") != nullptr && std::wcsstr(kCheats[0].mHint.mHelp, L"progress is kept") != nullptr,
          "level unlock names the screen it opens and writes nothing");
    check(std::wcsstr(kCheats[1].mHint.mHelp, L"Nothing is saved") != nullptr, "goodie unlock writes nothing");
    check(std::wcsstr(kCheats[3].mHint.mHelp, L"God row") != nullptr && std::wcsstr(kCheats[3].mHint.mHelp, L"God ON: no damage and endless energy") != nullptr,
          "god mode says what it adds and what God ON does, as the game's own God ON / God OFF read");
    check(std::wcsstr(kCheats[2].mHint.mHelp, L"unlock hints") != nullptr && std::wcsstr(kCheats[2].mHint.mHelp, L"each goodie") == nullptr,
          "hints do not promise a hint for every goodie");
    // the hint says what the cheat does to the other, and the row's label ends in the colon the list draws the value after
    check(std::wcsstr(kCheats[2].mHint.mHelp, L"While on, goodies cannot be opened.") != nullptr
              && std::wcscmp(kCheats[1].mLabel, L"Unlock All Goodies:") == 0,
          "hint priority is discoverable, and the labels end in a colon with no space after it");
    for (int i = 0; i < CE_NUM_CHEATS; ++i)
        check(std::wcslen(kCheats[i].mLabel) > 2 && kCheats[i].mLabel[std::wcslen(kCheats[i].mLabel) - 1] == L':', "a cheat's label ends in a colon, and no space");
    const WCHAR *codes[] = {L"Original code: TURKEY", L"Original code: MALLOY", nullptr, L"Original code: Maladim", L"Original code: Aurore"};
    for (int i = 0; i < CE_NUM_CHEATS; ++i) {
        check(kCheats[i].mHint.mExtra != nullptr && std::wcsstr(kCheats[i].mHint.mExtra, L"Original code: ") == kCheats[i].mHint.mExtra,
              "each cheat's hint carries its original code");
        if (codes[i]) check(std::wcscmp(kCheats[i].mHint.mExtra, codes[i]) == 0, "the original codes");
    }
    check(std::wcscmp(kCheatsNote, L"Cheats apply to every career and are off while recording. Wins still count.") == 0,
          "the Cheats page's standing note");
    for (int page = 0; page < CEP_COUNT; ++page) check(std::wcslen(kCeTitles[page]) > 0 && std::wcslen(kCeTitles[page]) < 24, "page titles");
    for (int box = 0; box < 4; ++box) check(std::wcslen(kBoxTitles[box]) > 0 && std::wcslen(kBoxTitles[box]) < 24, "the titles Help & Info's boxes give the title bar");
    check(std::wcscmp(kCeTitles[CEP_HUB], L"Onslaught CE") == 0, "the hub's title uses consistent mixed case matching sibling CE pages");

    // MenuOptionsList::Render centres N rows of 20 px (Back is the last) on the page's y (the page has no title row): the first row
    // is at y-10N-15 and Back at y+10N-35. Most pages are centred on 300; CE Settings, with thirteen rows, is centred on 260. Every
    // page's hints, for every row, must fit between Back and the help bar, and every first row must be below the title bar.
    struct Page { const char *name; const SCeHint *hints; int stride; int count; const WCHAR *note; float centre; int extra; };
    // The Help and plan hints as the page shows them without the experimental modes (the hints that replace the ones naming the commander map
    // and the Map Editor), and the settings without the Commander Map row: all measured here as well.
    static SCeRow plainHelp[sizeof(kHelpRows) / sizeof(kHelpRows[0])];
    for (size_t i = 0; i < sizeof(kHelpRows) / sizeof(kHelpRows[0]); ++i) plainHelp[i] = kHelpRows[i];
    plainHelp[0].mHint.mHelp = kControlsHintPlain;
    static SCeHint plainPlans[CE_NUM_PLANS];
    for (int i = 0; i < CE_NUM_PLANS; ++i) plainPlans[i] = kPlanHints[i];
    for (const SCePlanPlain &plain : kPlanPlain) plainPlans[plain.mIndex].mHelp = plain.mHint;
    static SCeSetting plainSettings[CE_NUM_SETTINGS];
    int plainCount = 0;
    for (const SCeSetting &setting : kSettings) if (std::strcmp(setting.mKey, "commander")) plainSettings[plainCount++] = setting;
    const Page pages[] = {
        {"hub", &all.rows[0].mHint, int(sizeof(SCeRow)), all.count, nullptr, kCeHubY, CCeBack::EXTRA_FOLLOW},
        {"bonus levels", &kBonusRows[0].mHint, int(sizeof(SCeRow)), CE_NUM_BONUS, kBonusNote, kCeListY, CCeBack::EXTRA_FOLLOW},
        {"settings", &kSettings[0].mHint, int(sizeof(SCeSetting)), CE_NUM_SETTINGS, nullptr, kCeSettingsY, CCeBack::EXTRA_FOOT},
        {"cheats", &kCheats[0].mHint, int(sizeof(SCeCheat)), CE_NUM_CHEATS, kCheatsNote, kCeListY, CCeBack::EXTRA_FIXED},
        {"help", &kHelpRows[0].mHint, int(sizeof(SCeRow)), int(sizeof(kHelpRows) / sizeof(kHelpRows[0])), nullptr, kCeListY, CCeBack::EXTRA_FOLLOW},
        {"plans", kPlanHints, int(sizeof(SCeHint)), CE_NUM_PLANS, nullptr, kCeListY, CCeBack::EXTRA_FOLLOW},
        {"help without the experimental modes", &plainHelp[0].mHint, int(sizeof(SCeRow)), int(sizeof(kHelpRows) / sizeof(kHelpRows[0])), nullptr, kCeListY, CCeBack::EXTRA_FOLLOW},
        {"plans without the experimental modes", plainPlans, int(sizeof(SCeHint)), CE_NUM_PLANS, nullptr, kCeListY, CCeBack::EXTRA_FOLLOW},
        {"settings without the Commander Map row", &plainSettings[0].mHint, int(sizeof(SCeSetting)), plainCount, nullptr, kCeSettingsY, CCeBack::EXTRA_FOOT},
        {"hub without the experimental rows", &plain.rows[0].mHint, int(sizeof(SCeRow)), plain.count, nullptr, kCeHubY, CCeBack::EXTRA_FOLLOW},
        {"experimental features", &kLabs[0].mHint, int(sizeof(SCeLab)), CE_NUM_LABS, kLabsNote, kCeListY, CCeBack::EXTRA_FOLLOW},
#ifndef CE_NETWORK_OFF
        {"hub with network unavailable", &noNetwork.rows[0].mHint, int(sizeof(SCeRow)), noNetwork.count, nullptr, kCeHubY, CCeBack::EXTRA_FOLLOW},
#endif
    };
    check(pages[1].count == 9 && pages[2].count == 13 && pages[4].count == 5 &&
          pages[5].count == 9 && pages[8].count == 12 && pages[7].count == 9 &&
          pages[6].count == 5 && pages[10].count == 5, "every page's rows");
    for (const Page &page : pages) {
        current_page = page.name;
        const float first_y = page.centre - 10.f * float(page.count + 1) - 15.f;
        const float back_y = page.centre + 10.f * float(page.count + 1) - 35.f;
        check(first_y >= 104.f, "the first row is below the title bar");
        check(back_y + 20.f < 449.f, "Back is above the help bar");
        for (int width : {7, 8, 9, 10}) {
            PLATFORM.font.advance = width;
            for (int i = 0; i <= page.count; ++i) {
                char where[160];
                std::snprintf(where, sizeof where, "%s, row %d, glyph advance %d", page.name, i, width);
                current_page = where;
                // the last selection is Back itself, which has no hint
                CMenuOptionsList list;
                list.mSelected = i;
                CCeBack back(&list, page.hints, page.stride, page.count, page.note, page.extra);
                draws.clear();
                back.Render(320.f, back_y, 0);
                const SCeHint *hint = i < page.count ? (const SCeHint *)((const char *)page.hints + i * page.stride) : nullptr;
                int help = hint ? lines(hint->mHelp) : 0, code = hint && hint->mExtra ? lines(hint->mExtra) : 0;
                int bottom = page.note ? lines(page.note, 500.f) : 0;
                check(int(draws.size()) == help + code + bottom, "all help, code and note lines drawn, and none for Back");
                if (hint) {
                    check(drawn(0, help) == compact(hint->mHelp), "help survives word wrapping");
                    if (hint->mExtra) check(drawn(help, code) == compact(hint->mExtra), "code survives word wrapping");
                    check(draws[0].y >= back_y + 20.f, "the hint starts below Back");
                    check(draws[0].colour == CCeBack::kCream && draws[0].colour != 0xffffffff, "the hint is cream, not the white of the rows");
                    for (int l = 1; l < help; ++l) check(draws[l].colour == CCeBack::kCream, "every line of the hint is cream");
                    if (hint->mExtra) for (int l = help; l < help + code; ++l) check(draws[l].colour == CCeBack::kGrey, "the second line is grey");
                    // no hint ends on a single word, in the game's widths
#ifdef CE_NETWORK_OFF
                    const bool exempt = false;
#else
                    const bool exempt = !std::strncmp(page.name, "plans", 5);		// the network flavour's own wordings are not the default build's
#endif
                    if (width == 8 && !exempt) {
                        const std::wstring last = draws[help - 1].text;
                        check(last.find(L' ') != std::wstring::npos, "a hint does not end on a single word");
                        if (hint->mExtra) check(draws[help + code - 1].text.find(L' ') != std::wstring::npos, "a second line does not end on a single word");
                    }
                }
                if (page.note) check(drawn(help + code, bottom) == compact(page.note), "note survives word wrapping");
                for (size_t idx = 0; idx < draws.size(); ++idx) {
                    const int limit = int(idx) >= help + code ? 500 : 420;
                    check(draws[idx].width < limit, "line fits help or note width");
                }
                if (page.note == kLabsNote && width == 8) check(bottom == 1, "Labs restart caveat fits cleanly on one line");
                if (help + code && bottom) check(draws[help + code - 1].y + 16.f <= draws[help + code].y, "help and code do not overlap note");
                if (help + code) {
                    std::snprintf(where, sizeof where, "%s, row %d, glyph advance %d, %d help and %d code lines, last at y %.0f", page.name, i, width, help, code, double(draws[help + code - 1].y));
                    check(draws[help + code - 1].y + 16.f <= 449.f, "help stays above the help bar");
                }
                if (bottom) check(draws.back().y + 16.f <= 449.f, "note stays above help bar");
                if (hint && hint->mExtra && page.extra == CCeBack::EXTRA_FIXED && width <= 9) {
                    // the Cheats page: two lines of hint at the game's widths, the original code two lines below the first line, at one place on every row
                    check(help == 2, "a cheat's hint is two lines");
                    check(draws[help].y == draws[0].y + 2 * 16.f, "the original code is two lines below the hint's first line, on every row");
                    check(draws[help + code - 1].y + 16.f < draws[help + code].y, "a clear line stands between the code and the note");
                }
                if (hint && hint->mExtra && page.extra == CCeBack::EXTRA_FOOT && width <= 9 && help <= 2)
                    check(draws[help + code - 1].y + 16.f == kCeFootEnd, "the settings' second line ends at one place above the help bar");
                check(help + code <= 5, "a hint is five lines at most");
            }
        }
    }
    PLATFORM.font.advance = 8;
    CMenuOptionsList list;
    CCeBack back(&list, &kCheats[0].mHint, int(sizeof(SCeCheat)), CE_NUM_CHEATS, kCheatsNote);
    draws.clear();
    FEMESSBOX.open = true;
    back.Render(320.f, 325.f, 0);
    check(draws.empty(), "message box suppresses help behind it");
    FEMESSBOX.open = false;
    // A multi-line note with explicit '\n' (Catalogue Find) renders every line above y=449 without overlapping.
    CCeBack findBack(&list, nullptr, 0, 0, L"Type a name. Enter finds, Esc cancels.\nSpaces also match underscores.");
    draws.clear();
    findBack.Render(320.f, 325.f, 0);
    check(draws.size() == 2 && draws[0].y + 16.f <= draws[1].y && draws[1].y + 16.f <= 449.f &&
          draws[0].text == L"Type a name. Enter finds, Esc cancels." && draws[1].text == L"Spaces also match underscores.",
          "multi-line note splits on newline and stays above the help bar");
    std::puts("CE cheat help: every page's hints, wrapping, spacing and message box passed");
}
