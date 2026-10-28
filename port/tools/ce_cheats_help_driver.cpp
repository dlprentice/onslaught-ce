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
        check(std::wcsstr(kCheats[4].mHelp, key) != nullptr, "free-camera keys are discoverable");
    for (const WCHAR *missing : {L"speed", L"up/down", L"Space", L"Shift", L"mouse"})
        check(std::wcsstr(kCheats[4].mHelp, missing) == nullptr, "no free-camera key the game does not have");
    check(std::wcsstr(kCheats[0].mHelp, L"progress is not changed") != nullptr, "level unlock writes nothing");
    check(std::wcsstr(kCheats[1].mHelp, L"Nothing is saved") != nullptr, "goodie unlock writes nothing");
    check(std::wcsstr(kCheats[3].mHelp, L"not in your career") != nullptr, "god mode is kept out of careers");
    check(std::wcsstr(kCheats[2].mHelp, L"available unlock hints") != nullptr,
          "hints do not promise descriptions for every goodie");
    check(std::wcsstr(kCheats[2].mHelp, L"even if Unlock all goodies is on") != nullptr,
          "hint priority is discoverable");
    // MenuOptionsList::Render centers six 20px rows at y=300; Back starts at y=325.
    const float back_y = 325.f;
    const WCHAR *note = L"Cheats apply to all careers and are off when recording. Wins count as usual.";
    for (int width : {7, 8, 9, 10}) {
        PLATFORM.font.advance = width;
        for (int i = 0; i < CE_NUM_CHEATS; ++i) {
            CMenuOptionsList list;
            list.mSelected = i;
            CCeCheatsBack back(&list);
            draws.clear();
            back.Render(320.f, back_y, 0);
            int help = lines(kCheats[i].mHelp), code = lines(kCheats[i].mCode), bottom = lines(note);
            check(int(draws.size()) == help + code + bottom, "all help, code and note lines drawn");
            check(drawn(0, help) == compact(kCheats[i].mHelp), "help survives word wrapping");
            check(drawn(help, code) == compact(kCheats[i].mCode), "code survives word wrapping");
            check(drawn(help + code, bottom) == compact(note), "note survives word wrapping");
            for (const Draw &line : draws) check(line.width < 420, "line fits help width");
            check(draws[help + code - 1].y + 16.f <= draws[help + code].y, "help and code do not overlap note");
            check(draws.back().y + 16.f <= 449.f, "note stays above help bar");
        }
    }
    CMenuOptionsList list;
    CCeCheatsBack back(&list);
    draws.clear();
    FEMESSBOX.open = true;
    back.Render(320.f, back_y, 0);
    check(draws.empty(), "message box suppresses help behind it");
    std::puts("CE cheat help: five selections, wrapping, spacing and message box passed");
}
