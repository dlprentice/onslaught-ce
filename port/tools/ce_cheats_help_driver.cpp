// SPDX-License-Identifier: GPL-3.0-only
// Exercise the actual CE cheat help renderer and retail WordWrap with an authored font and menu.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
struct MessageBox {
    bool open = false;
    bool BeingDisplayed() { return open; }
} FEMESSBOX;
struct CMenuOptionsList { int mSelected = 0; };
struct CMenuItem {
    CMenuItem(DWORD, int) {}
    virtual ~CMenuItem() = default;
    virtual void Render(float, float, BOOL) {}
};
#include "cheats_help.inc"

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

static void check(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(2); }
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
static int lines(const WCHAR *text) {
    SWordWrapFill fill{};
    WCHAR copy[400];
    std::wcsncpy(copy, text, 399);
    copy[399] = 0;
    return PLATFORM.font.WordWrap(&fill, copy, 420.f);
}

int main() {
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
    for (const WCHAR *key : {L"F:", L"W/S fly", L"A/D slide", L"arrows look", L"Esc freezes", L"F returns", L"still be hit"})
        check(std::wcsstr(kCheats[4].mHint.mHelp, key) != nullptr, "free-camera keys are discoverable");
    for (const WCHAR *missing : {L"speed", L"up/down", L"Space", L"Shift", L"mouse"})
        check(std::wcsstr(kCheats[4].mHint.mHelp, missing) == nullptr, "no free-camera key the game does not have");
    check(std::wcsstr(kCheats[0].mHint.mHelp, L"progress is not changed") != nullptr, "level unlock writes nothing");
    check(std::wcsstr(kCheats[1].mHint.mHelp, L"Nothing is saved") != nullptr, "goodie unlock writes nothing");
    check(std::wcsstr(kCheats[3].mHint.mHelp, L"not in your career") != nullptr, "god mode is kept out of careers");
    check(std::wcsstr(kCheats[2].mHint.mHelp, L"available unlock hints") != nullptr,
          "hints do not promise descriptions for every goodie");
    // the hint names the switch as its row does
    check(std::wcsstr(kCheats[2].mHint.mHelp, L"even if Unlock All Goodies is on") != nullptr
              && std::wcscmp(kCheats[1].mLabel, L"Unlock All Goodies: ") == 0,
          "hint priority is discoverable, and names the switch as its row does");
    const WCHAR *codes[] = {L"Original code: TURKEY", L"Original code: MALLOY", nullptr, L"Original code: Maladim", L"Original code: Aurore"};
    for (int i = 0; i < CE_NUM_CHEATS; ++i) {
        check(kCheats[i].mHint.mExtra != nullptr && std::wcsstr(kCheats[i].mHint.mExtra, L"Original code: ") == kCheats[i].mHint.mExtra,
              "each cheat's hint carries its original code");
        if (codes[i]) check(std::wcscmp(kCheats[i].mHint.mExtra, codes[i]) == 0, "the original codes");
    }
    check(std::wcscmp(kCheatsNote, L"Cheats apply to all careers and are off when recording. Wins count as usual.") == 0,
          "the Cheats page's standing note");
    for (int page = 0; page < CEP_COUNT; ++page) check(std::wcslen(kCeTitles[page]) > 0 && std::wcslen(kCeTitles[page]) < 24, "page titles");

    // MenuOptionsList::Render centres N rows of 20 px at y=300 (the page has no title row): the first row is at 285-10N, so
    // Back, the last, is at 265+10N. Every page's hints, for every row, must fit between Back and the help bar.
    struct Page { const char *name; const SCeHint *hints; int stride; int count; const WCHAR *note; };
    const Page pages[] = {
        {"hub", &kHubRows[0].mHint, int(sizeof(SCeRow)), int(sizeof(kHubRows) / sizeof(kHubRows[0])), nullptr},
        {"bonus levels", &kBonusRows[0].mHint, int(sizeof(SCeRow)), CE_NUM_BONUS, nullptr},
        {"settings", kSettingsHints, int(sizeof(SCeHint)), CE_NUM_SETTINGS, nullptr},
        {"cheats", &kCheats[0].mHint, int(sizeof(SCeCheat)), CE_NUM_CHEATS, kCheatsNote},
        {"help", &kHelpRows[0].mHint, int(sizeof(SCeRow)), int(sizeof(kHelpRows) / sizeof(kHelpRows[0])), nullptr},
        {"plans", kPlanHints, int(sizeof(SCeHint)), CE_NUM_PLANS, nullptr},
    };
    check(pages[0].count == 5 && pages[1].count == 7 && pages[2].count == 1 && pages[4].count == 5 && pages[5].count == 9,
          "every page's rows");
    for (const Page &page : pages) {
        const float back_y = 265.f + 10.f * float(page.count + 1);
        check(back_y + 20.f < 449.f, "Back is above the help bar");
        for (int width : {7, 8, 9, 10}) {
            PLATFORM.font.advance = width;
            for (int i = 0; i <= page.count; ++i) {
                // the last selection is Back itself, which has no hint
                CMenuOptionsList list;
                list.mSelected = i;
                CCeBack back(&list, page.hints, page.stride, page.count, page.note);
                draws.clear();
                back.Render(320.f, back_y, 0);
                const SCeHint *hint = i < page.count ? (const SCeHint *)((const char *)page.hints + i * page.stride) : nullptr;
                int help = hint ? lines(hint->mHelp) : 0, code = hint && hint->mExtra ? lines(hint->mExtra) : 0;
                int bottom = page.note ? lines(page.note) : 0;
                check(int(draws.size()) == help + code + bottom, "all help, code and note lines drawn, and none for Back");
                if (hint) {
                    check(drawn(0, help) == compact(hint->mHelp), "help survives word wrapping");
                    if (hint->mExtra) check(drawn(help, code) == compact(hint->mExtra), "code survives word wrapping");
                    check(draws[0].y >= back_y + 20.f, "the hint starts below Back");
                    check(draws[0].colour == 0xffffffff, "the hint is white");
                }
                if (page.note) check(drawn(help + code, bottom) == compact(page.note), "note survives word wrapping");
                for (const Draw &line : draws) check(line.width < 420, "line fits help width");
                if (help + code && bottom) check(draws[help + code - 1].y + 16.f <= draws[help + code].y, "help and code do not overlap note");
                if (help + code) check(draws[help + code - 1].y + 16.f <= 449.f, "help stays above the help bar");
                if (bottom) check(draws.back().y + 16.f <= 449.f, "note stays above help bar");
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
    std::puts("CE cheat help: every page's hints, wrapping, spacing and message box passed");
}
