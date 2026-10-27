// SPDX-License-Identifier: GPL-3.0-only
// Execute the production palette with the real patched message handler against authored fake game boundaries.
#include "ce_palette_fake.h"
#include <cstdio>

PCLTShell LT; WORD gCharScanCode = 0; bool gKeyPressed = false; Cli CLIPARAMS; BOOL PLAYABLE_DEMO = FALSE;
FakeGame GAME; FakeFrontend FRONTEND; FakeInterface GAMEINTERFACE; FakeEngine ENGINE; FakeHud HUD; FakePlatform PLATFORM;
FakeRenderInfo RENDERINFO; FakeConsole CONSOLE; FakeRect rect; std::vector<FakeDraw> draws;
bool fakeRecording = false;
#ifdef FAKE_NETWORK
extern "C" void SetBootRequested(bool value);
#endif
#include "ce_layer.inc"
#include "msgproc.inc"

static void check(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(2); }
}
static LPARAM lp(unsigned scan, bool repeat = false) { return (LPARAM(scan & 0x7f) << 16) | ((scan & 0x80) ? 0x1000000 : 0) | (repeat ? 0x40000000 : 0); }
static void down(unsigned scan, unsigned vk = 'A', bool repeat = false) { LT.MsgProc(nullptr, WM_KEYDOWN, vk, lp(scan, repeat)); }
static void up(unsigned scan, unsigned vk = 'A') { LT.MsgProc(nullptr, WM_KEYUP, vk, lp(scan)); }
static void character(unsigned scan, unsigned ch) { LT.MsgProc(nullptr, WM_CHAR, ch, lp(scan)); }
#ifdef CE_VERSION
static int otherDown = 0, otherUp = 0, otherChar = 0;
static void OtherTrap(WORD, KeyEventType event) { if (event == KEY_EVENT_DOWN) ++otherDown; if (event == KEY_EVENT_UP) ++otherUp; if (event == KEY_EVENT_CHAR) ++otherChar; }
static void enter() { down(DIK_RETURN, 13); character(DIK_RETURN, 13); up(DIK_RETURN, 13); }
static void type(const char *s) { for (; *s; ++s) { down(0x20, 'A'); character(0x20, (unsigned char)*s); up(0x20, 'A'); } }
static void openPalette() {
    down(DIK_GRAVE, 0xc0);
    character(DIK_GRAVE, '`');
    check(LT.GetKeytrap() != nullptr && LT.GetKeytrap() != OtherTrap, "grave opens palette");
    check(!LT.KeyDown[DIK_GRAVE], "opening grave is not a game keydown");
    draws.clear(); PortCePaletteRender();
    check(draws.size() == 8 && draws[6].text == L"> ", "opening grave character does not enter the line");
    up(DIK_GRAVE, 0xc0);
    check(!LT.KeyDown[DIK_GRAVE] && LT.KeyWasUp[DIK_GRAVE], "opening grave release clears state");
}
static void closePalette() {
    down(DIK_ESCAPE, 27);
    character(DIK_ESCAPE, 27);
    up(DIK_ESCAPE, 27);
    check(!LT.GetKeytrap(), "Escape closes palette");
}
static std::wstring renderedResult() {
    draws.clear(); PortCePaletteRender();
    check(draws.size() == 8, "palette renders all help and result lines");
    return draws.back().text;
}
static void refused() {
    check(!PortCePaletteOpen(DIK_GRAVE, false), "admission refused");
    check(!LT.GetKeytrap(), "refusal leaves trap unowned");
}
static void command(const char *text) { type(text); enter(); }
static void ordinary() {
    // A key held before opening remains a game key until its ordinary release.
    down(0x11, 'W');
    check(LT.KeyDown[0x11], "preexisting held key reaches game");
    down(DIK_GRAVE, 0xc0); character(DIK_GRAVE, '`');
    auto paletteTrap = LT.GetKeytrap();
    draws.clear(); PortCePaletteRender();
    check(paletteTrap && draws.size() == 8 && draws[6].text == L"> ", "opening grave and its character are consumed");
    down(DIK_GRAVE, 0xc0, true); character(DIK_GRAVE, '`');
    check(LT.GetKeytrap() == paletteTrap, "opening key repeat does not close");
    up(DIK_GRAVE, 0xc0); up(0x11, 'W');
    check(!LT.KeyDown[0x11] && LT.KeyWasUp[0x11], "release of preexisting held key reaches game state");
    for (auto size : {std::pair<int,int>(640,480), {1920,1080}, {3840,2160}, {480,640}}) {
        PLATFORM.w = size.first; PLATFORM.h = size.second;
        draws.clear(); PortCePaletteRender();
        check(draws.size() == 8, "help and prompt visible");
        check(draws[1].text.find(L"help") != std::wstring::npos && draws[1].text.find(L"status") != std::wstring::npos, "help shows read-only commands");
        check(draws[2].text.find(L"camera") != std::wstring::npos && draws[4].text.find(L"Escape") != std::wstring::npos, "help shows action and exit");
        check(rect.x >= 0 && rect.y >= 0 && rect.x + rect.w <= PLATFORM.w && rect.y + rect.h <= PLATFORM.h, "background fits screen");
        for (const auto &d : draws)
            check(d.x >= 0 && d.y >= 0 && d.x + float(d.text.size() * 8) * d.sx <= PLATFORM.w && d.y + 16 * d.sy <= PLATFORM.h, "text fits screen");
    }
    PLATFORM.w = 640; PLATFORM.h = 480; PLATFORM.font.height = 80;
    draws.clear(); PortCePaletteRender();
    check(draws.size() == 8 && rect.y + rect.h <= PLATFORM.h, "large font panel fits screen");
    for (const auto &d : draws)
        check(d.y + PLATFORM.font.height * d.sy <= PLATFORM.h, "large font text fits screen");
    PLATFORM.font.height = 16;
    command("help"); check(renderedResult() == L"Commands are listed above.", "help response");
    command("status"); check(renderedResult() == L"Normal camera. Battle running.", "status reports live state");
    GAME.paused = true; command("status"); check(renderedResult() == L"Normal camera. Battle paused.", "status reports pause"); GAME.paused = false;
    command("camera"); check(GAME.calls == 0 && renderedResult() == L"Enable the Free camera cheat first.", "camera respects existing cheat");
    FRONTEND.mFEPSaveGame.cheat = true; ENGINE.landscape = false;
    command("camera"); check(GAME.calls == 0 && renderedResult() == L"Camera is unavailable here.", "camera needs a landscape");
    ENGINE.landscape = true;
    command(" CAMeRa "); check(GAME.calls == 1 && GAME.freeCamera && renderedResult() == L"Free camera on.", "whitelisted camera toggles once");
    command("status"); check(renderedResult() == L"Free camera. Battle running.", "status reflects toggle");
    for (const char *bad : {"camera;Quit", "camera x", "exec", "dumpmem", "camera$", "camera\t", "camera()"}) {
        command(bad); check(GAME.calls == 1, "unknown or extended command cannot invoke game action");
    }
    type("camera"); down(0x20, 'A'); character(0x20, 0x00e9); up(0x20, 'A'); enter();
    check(GAME.calls == 1 && renderedResult() == L"Input unsupported or too long.", "unsupported character cannot be silently removed");
    type("camera"); type("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"); enter();
    check(GAME.calls == 1 && renderedResult() == L"Input unsupported or too long.", "overlength input cannot be silently truncated");
    type("cameraz"); down(DIK_BACK, 8); character(DIK_BACK, 8); up(DIK_BACK, 8); enter();
    check(GAME.calls == 2 && !GAME.freeCamera, "backspace edits input once and toggles back");
    // A character key still held across close has no binding-up or char delivered to another owner.
    down(0x20, 'A'); character(0x20, 'x');
    const auto before = CONSOLE.binds.size();
    closePalette();
    down(0x20, 'A', true); character(0x20, 'x'); up(0x20, 'A');
    check(CONSOLE.binds.size() == before && !LT.KeyDown[0x20] && LT.KeyWasUp[0x20], "palette-owned key release clears state without dispatching a game bind");
    check(!PortCePaletteSuppressChar(0x20), "release clears pending suppression");

    // Competing input owners always win, and held palette events never leak to that owner.
    openPalette(); down(0x20, 'A'); character(0x20, 'x');
    LT.SetKeytrap(OtherTrap);
    down(0x20, 'A', true); character(0x20, 'x'); up(0x20, 'A');
    draws.clear(); PortCePaletteRender();
    check(LT.GetKeytrap() == OtherTrap && draws.empty() && otherDown == 0 && otherChar == 0 && otherUp == 0, "replacement trap retained; stale character and up suppressed");
    down(0x21, 'B'); character(0x21, 'b'); up(0x21, 'B');
    check(otherDown == 1 && otherChar == 1 && otherUp == 1, "new owner's own events are delivered");
    LT.MsgProc(nullptr, WM_ACTIVATEAPP, 0, 0); PortCePaletteClose();
    check(LT.GetKeytrap() == OtherTrap, "focus loss and teardown never clear a foreign trap");
    LT.SetKeytrap(nullptr);

    openPalette(); down(0x20, 'A'); character(0x20, 'x');
    LT.MsgProc(nullptr, WM_ACTIVATEAPP, 0, 0);
    check(!LT.GetKeytrap(), "focus loss closes owned palette");
    up(0x20, 'A'); check(!LT.KeyDown[0x20] && LT.KeyWasUp[0x20], "focus-loss keyup clears state");
    openPalette(); GAME.state = 4; PortCePaletteCheck();
    check(!LT.GetKeytrap(), "leaving play closes palette"); GAME.state = GAME_STATE_PLAYING;
    openPalette(); GAME.pause.mActive = TRUE; PortCePaletteCheck();
    check(!LT.GetKeytrap(), "pause menu taking focus closes palette"); GAME.pause.mActive = FALSE;
    openPalette(); PortCePaletteClose(); check(!LT.GetKeytrap(), "teardown closes owned trap");

    // CE layer remains cached on, so these exercise separate live fences even under CE forced on for a replay.
    for (const char *flag : {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"}) {
        setenv(flag, "1", 1); refused(); unsetenv(flag);
    }
    fakeRecording = true; refused(); fakeRecording = false;
#ifdef FAKE_NETWORK
    SetBootRequested(true); refused(); SetBootRequested(false);
#endif
    GAME.multiplayer = true; refused(); GAME.multiplayer = false;
    GAME.players = 2; refused(); GAME.players = 1;
    GAME.state = 2; refused(); GAME.state = 3;
    GAME.pause.mActive = TRUE; refused(); GAME.pause.mActive = FALSE;
    GAMEINTERFACE.menu = true; refused(); GAMEINTERFACE.menu = false;
    GAMEINTERFACE.mShowJoypad = true; refused(); GAMEINTERFACE.mShowJoypad = false;
    GAME.playerPresent = false; refused(); GAME.playerPresent = true;
    GAME.controllerPresent = false; refused(); GAME.controllerPresent = true;
    GAME.cameraPresent = false; refused(); GAME.cameraPresent = true;
    GAME.pausePresent = false; refused(); GAME.pausePresent = true;
    FRONTEND.running = true; refused(); FRONTEND.running = false;
    PLATFORM.present = false; refused(); PLATFORM.present = true;
    HUD.present = false; refused(); HUD.present = true;
    HUD.white.width = 0; refused(); HUD.white.width = 64;
    PLATFORM.font.height = 0; refused(); PLATFORM.font.height = 16;
    PLATFORM.w = 0; refused(); PLATFORM.w = 640;
    openPalette(); HUD.present = false; PortCePaletteCheck();
    check(!LT.GetKeytrap(), "loss of render resources releases trap"); HUD.present = true;
    CLIPARAMS.mRecordDemo = true; refused(); CLIPARAMS.mRecordDemo = false;
    CLIPARAMS.mPlaybackDemo = true; refused(); CLIPARAMS.mPlaybackDemo = false;
    CLIPARAMS.mAutoConfigTest = true; refused(); CLIPARAMS.mAutoConfigTest = false;
    CLIPARAMS.mAttractMode = true; refused(); CLIPARAMS.mAttractMode = false;
    CLIPARAMS.mStressTest = 1; refused(); CLIPARAMS.mStressTest = 0;
    PLAYABLE_DEMO = true; refused(); PLAYABLE_DEMO = false;
    LT.SetKeytrap(OtherTrap); down(DIK_GRAVE, 0xc0); up(DIK_GRAVE, 0xc0);
    check(LT.GetKeytrap() == OtherTrap && otherDown == 2 && otherUp == 2, "existing owner retains its grave events");
    LT.SetKeytrap(nullptr);
    std::puts("palette admission, whitelist, help, trap ownership and key lifetimes passed");
}
#endif
int main(int argc, char **argv) {
#ifdef CE_VERSION
    if (argc > 1 && !std::strcmp(argv[1], "denied")) { refused(); std::puts("disabled mode passed"); }
    else ordinary();
#else
    (void)argc; (void)argv;
    down(DIK_GRAVE, 0xc0); character(DIK_GRAVE, '`'); up(DIK_GRAVE, 0xc0);
    check(!LT.GetKeytrap() && CONSOLE.binds.size() == 2 && !LT.KeyDown[DIK_GRAVE] && LT.KeyWasUp[DIK_GRAVE], "faithful message path unchanged");
    std::puts("faithful message path passed");
#endif
}
