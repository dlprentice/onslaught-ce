// SPDX-License-Identifier: GPL-3.0-only
// The production palette's CmdPhoto coupled to the production photo control, with only its game boundary faked
// (and the draw filter, which d3d9hooks.cpp owns, recorded).
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
static bool filtered;
void PortCeDrawHooks(bool on) { filtered = on; }
void PortCeDrawsDropped(int) {}

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
    check(line == "Photo mode on: HUD hidden. F11 or photo returns; F12 saves a screenshot." && closed && PortCePhotoOn() && filtered &&
          !CLIPARAMS.mKillHUD, "console-on turns photo mode on, closes the panel and leaves -killhud alone");
    closed = false;
    std::strcpy(args[1], "off"); CmdPhoto(2, args);
    check(line == "Photo mode off." && !closed && !PortCePhotoOn() && !filtered, "console-off turns it off and keeps the panel");
    CmdPhoto(1, args);
    check(PortCePhotoOn() && closed, "bare photo switches it on");
    closed = false;
    CmdPhoto(1, args);
    check(line == "Photo mode off." && !PortCePhotoOn() && !closed, "and the next bare photo off");
    check(PortCePhotoKey(DIK_F12, false) && PortCePhotoShotWanted() && !PortCePhotoShotWanted(),
          "F12 saves one frame beside the console command");
    // Where photo mode is fenced the command says so and nothing changes.
    setenv("BEA_PORT_TAPE", "1", 1);
    std::strcpy(args[1], "on"); CmdPhoto(2, args);
    check(line == "Photo mode is unavailable here." && !PortCePhotoOn() && !filtered && !closed, "a fenced level refuses the command");
    unsetenv("BEA_PORT_TAPE");
    return 0;
}
