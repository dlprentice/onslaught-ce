// SPDX-License-Identifier: GPL-3.0-only
// The production palette's CmdPhoto coupled to the production photo control, with only its game boundary faked.
#include "ce_palette_fake.h"
#include "ce_photo.h"
#include <cstdarg>
#include <cstdio>

PCLTShell LT; WORD gCharScanCode = 0; bool gKeyPressed = false; Cli CLIPARAMS; BOOL PLAYABLE_DEMO = FALSE;
FakeGame GAME; FakeFrontend FRONTEND; FakeInterface GAMEINTERFACE; FakeHud HUD; FakePlatform PLATFORM;
FakeRenderInfo RENDERINFO; FakeConsole CONSOLE; std::vector<FakeRect> sprites; std::vector<FakeDraw> draws;
bool PortCeLayer() { return true; }
bool fakeRecording = false;
extern "C" unsigned long long CE_ScreenshotTicks() { return 1000; }
extern "C" unsigned PortPresentCount() { return 1; }

typedef char Arg[64];
static std::string line;
static bool closed;
static bool ParseBool(const char *s, bool *out) {
    if (!std::strcmp(s, "on")) { *out = true; return true; }
    if (!std::strcmp(s, "off")) { *out = false; return true; }
    return false;
}
static void Out(const char *fmt, ...) {
    char text[256]; va_list args; va_start(args, fmt); std::vsnprintf(text, sizeof text, fmt, args); va_end(args);
    line = text;
}
static void Close() { closed = true; }
#include "photo_command.inc"

static void check(bool yes, const char *what) { if (!yes) { std::fprintf(stderr, "%s\n", what); std::exit(2); } }
int main() {
    Arg args[2] = {"photo", "on"};
    CmdPhoto(2, args);
    check(line == "Photo mode is temporarily unavailable. F12 saves a screenshot." && !closed && !PortCePhotoOn() && !CLIPARAMS.mKillHUD,
          "actual console-on is refused without suppressing gameplay rendering");
    std::strcpy(args[1], "off"); CmdPhoto(2, args);
    check(line == "Photo mode off." && !closed && !PortCePhotoOn(), "console-off is harmless");
    CmdPhoto(1, args);
    check(line == "Photo mode is temporarily unavailable. F12 saves a screenshot." && !PortCePhotoOn(), "bare photo cannot activate mode");
    check(PortCePhotoKey(DIK_F12, false) && PortCePhotoShotWanted() && !PortCePhotoShotWanted(),
          "F12 remains available independently of console refusal");
    return 0;
}
