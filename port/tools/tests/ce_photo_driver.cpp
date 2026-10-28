// SPDX-License-Identifier: GPL-3.0-only
// Execute the production screenshot and temporarily disabled photo controls with the real patched message handler.
// Check ownership, non-activation through F11/console/proof, fences, F12, and the note kept out of the saved frame.
#include "ce_palette_fake.h"
#include "ce_photo.h"
#include <cstdio>
#include <string>

PCLTShell LT; WORD gCharScanCode = 0; bool gKeyPressed = false; Cli CLIPARAMS; BOOL PLAYABLE_DEMO = FALSE;
FakeGame GAME; FakeFrontend FRONTEND; FakeInterface GAMEINTERFACE; FakeHud HUD; FakePlatform PLATFORM;
FakeRenderInfo RENDERINFO; FakeConsole CONSOLE; std::vector<FakeRect> sprites; std::vector<FakeDraw> draws;
bool fakeRecording = false;
#ifdef FAKE_NETWORK
extern "C" void SetBootRequested(bool value);
#endif
#include "ce_layer.inc"

#ifdef CE_VERSION
// The console's key hooks are not under test here: a closed console that owns no key.
void PortCePaletteCheck() {}
void PortCePaletteClose() {}
bool PortCePaletteOpen(unsigned, bool) { return false; }
bool PortCePaletteSuppressDown(unsigned) { return false; }
bool PortCePaletteRelease(unsigned) { return false; }
bool PortCePaletteSuppressChar(unsigned) { return false; }
static unsigned long long ticks = 1000;
static unsigned presents = 0;
extern "C" unsigned long long CE_ScreenshotTicks(void) { return ticks; }
extern "C" unsigned PortPresentCount(void) { return presents; }
#endif
#include "msgproc.inc"

static void check(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(2); }
}

static LPARAM lp(unsigned scan, bool repeat = false) { return (LPARAM(scan & 0x7f) << 16) | ((scan & 0x80) ? 0x1000000 : 0) | (repeat ? 0x40000000 : 0); }
static void down(unsigned scan, bool repeat = false) { LT.MsgProc(nullptr, WM_KEYDOWN, 0x7a, lp(scan, repeat)); }
static void up(unsigned scan) { LT.MsgProc(nullptr, WM_KEYUP, 0x7a, lp(scan)); }

#ifdef CE_VERSION
static void press(unsigned scan) { down(scan); up(scan); }
static void clearKeys() { for (int i = 0; i < 256; ++i) LT.KeyDown[i] = LT.KeyWasDown[i] = LT.KeyWasUp[i] = 0; CONSOLE.binds.clear(); }
static int otherDown = 0;
static void OtherTrap(WORD, KeyEventType event) { if (event == KEY_EVENT_DOWN) ++otherDown; }
static void render() { draws.clear(); sprites.clear(); PortCePhotoRender(); }
static bool drew(const wchar_t *text) {
    for (const auto &d : draws) if (d.text.find(text) != std::wstring::npos) return true;
    return false;
}
// What a frame does: the between-frames hook, the drawing, then the Present asks whether to save it.
static bool frame() { PortCePhotoFrame(); render(); ++presents; return PortCePhotoShotWanted(); }
static const char *const kFlags[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
                                     "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};

static void keys() {
    // F11 is consumed, explains the temporary hold, and does not change the developer HUD flag.
    clearKeys();
    down(DIK_F11);
    check(!PortCePhotoOn() && CLIPARAMS.mKillHUD == FALSE, "F11 cannot activate photo mode");
    check(!LT.KeyDown[DIK_F11] && !LT.KeyWasDown[DIK_F11] && CONSOLE.binds.empty(), "F11 is not a game key down");
    down(DIK_F11, true);
    check(!PortCePhotoOn() && !LT.KeyDown[DIK_F11], "a held F11 repeats nothing and stays taken");
    up(DIK_F11);
    render();
    check(drew(L"Photo unavailable (F12: screenshot)."), "F11 explains the hold and F12");
    check(!draws.empty() && draws[0].x == 16 && draws[0].y == 48 && draws[0].sx == 0.9f,
          "the F11 notice is compact and placed above radio subtitles");
    for (const auto &d : draws) check(d.x >= 0 && d.y >= 0 && d.x + float(d.text.size() * 8) * d.sx <= PLATFORM.w + 0.01f, "the note fits");
    check(!PortCePhotoSet(true) && !PortCePhotoOn() && CLIPARAMS.mKillHUD == FALSE, "console enable is refused");
    check(PortCePhotoSet(false), "console off is accepted");
    render();
    check(draws.empty(), "off removes the note");
    clearKeys();
    down(0x21);
    check(LT.KeyDown[0x21] && CONSOLE.binds.empty(), "F (the free camera) is still the game's key");
    up(0x21);
    // F12 still saves exactly one frame, without the note in that frame.
    press(DIK_F11);
    render();
    check(drew(L"Photo unavailable (F12: screenshot)."), "the unavailability note is showing");
    press(DIK_F12);
    check(!LT.KeyDown[DIK_F12] && !LT.KeyWasDown[DIK_F12], "F12 is not a game key down");
    render();
    check(draws.empty() && sprites.empty(), "nothing of the note is in the frame being saved");
    check(PortCePhotoShotWanted(), "the Present after F12 is saved");
    check(!PortCePhotoShotWanted(), "one F12, one screenshot");
    render();
    check(drew(L"Photo unavailable (F12: screenshot)."), "the note is back in the next frame");
    down(DIK_F12, true);
    check(!PortCePhotoShotWanted(), "a held F12 takes one screenshot");
    up(DIK_F12);
    PortCePhotoShotDone(CE_SHOT_SAVED, "onslaught-ce-20261029-120000.png");
    render();
    check(drew(L"Saved screenshot onslaught-ce-20261029-120000.png"), "the note names the file");
    check(sprites.size() == 1 && sprites[0].x >= 0 && sprites[0].y + sprites[0].h <= PLATFORM.h, "the note sits inside the window");
    ticks += 2400; render(); check(!draws.empty(), "the note stays a moment");
    ticks += 200; render(); check(draws.empty(), "and then goes");
    PortCePhotoShotDone(CE_SHOT_FULL, ""); render();
    check(drew(L"Screenshot not saved: the screenshots folder is full."), "a full folder is said so");
    PortCePhotoShotDone(CE_SHOT_WRITE, ""); render();
    check(drew(L"Screenshot not saved (error 6)."), "a failed write is said so");
    ticks += 10000;
    press(DIK_F12); check(frame(), "F12 works while photo mode is unavailable");
    check(!frame(), "and only once");
}

static void owners() {
    // A menu, remap screen or the console owns the keyboard: F11 and F12 are theirs.
    LT.SetKeytrap(OtherTrap);
    const int before = otherDown;
    down(DIK_F11); up(DIK_F11); down(DIK_F12); up(DIK_F12);
    check(otherDown == before + 2 && !PortCePhotoOn() && !PortCePhotoShotWanted(), "keys go to the keyboard's owner");
    LT.SetKeytrap(nullptr);
}

static void levelEnd() {
    CLIPARAMS.mKillHUD = FALSE;
    press(DIK_F11); press(DIK_F12);
    PortCePhotoLevelEnd();
    check(!PortCePhotoOn() && CLIPARAMS.mKillHUD == FALSE, "level end leaves photo unavailable");
    check(!PortCePhotoShotWanted(), "level end drops a screenshot not yet taken");
    CLIPARAMS.mKillHUD = TRUE;
    check(!PortCePhotoSet(true) && CLIPARAMS.mKillHUD == TRUE, "refused enable preserves another HUD setting");
    PortCePhotoLevelEnd();
    check(CLIPARAMS.mKillHUD == TRUE, "level end does not overwrite another HUD setting");
    CLIPARAMS.mKillHUD = FALSE;
}

static void refusedHere(const char *why) {
    clearKeys();
    down(DIK_F11); up(DIK_F11); down(DIK_F12); up(DIK_F12);
    check(!PortCePhotoOn() && LT.KeyWasDown[DIK_F11] && LT.KeyWasDown[DIK_F12], why);
    check(!PortCePhotoSet(true) && CLIPARAMS.mKillHUD == FALSE, why);
    check(!PortCePhotoSet(false), why);
    check(!PortCePhotoShotWanted(), why);
}

static void fences() {
    // Every replay, recording, probe, clock, pilot and network switch; recording; multiplayer, demos and the front end.
    for (const char *flag : kFlags) {
        press(DIK_F12);
        setenv(flag, "1", 1);
        PortCePhotoFrame();
        unsetenv(flag);
        check(!PortCePhotoShotWanted(), "a fence permanently discards a pending screenshot");
        setenv(flag, "1", 1);
        check(!PortCePhotoOn() && CLIPARAMS.mKillHUD == FALSE, "a fence leaves photo mode unavailable");
        check(!PortCePhotoShotWanted(), "a fence drops the screenshot");
        refusedHere("a fence refuses photo mode and its keys");
        unsetenv(flag);
    }
    fakeRecording = true; refusedHere("recording refuses it"); fakeRecording = false;
    GAME.multiplayer = true; refusedHere("multiplayer refuses it"); GAME.multiplayer = false;
    GAME.players = 2; refusedHere("two players refuse it"); GAME.players = 1;
    GAME.playerPresent = false; refusedHere("no player refuses it"); GAME.playerPresent = true;
    FRONTEND.running = true; refusedHere("the front end refuses it"); FRONTEND.running = false;
    PLAYABLE_DEMO = TRUE; refusedHere("the demo refuses it"); PLAYABLE_DEMO = FALSE;
    CLIPARAMS.mAttractMode = true; refusedHere("attract mode refuses it"); CLIPARAMS.mAttractMode = false;
    CLIPARAMS.mPlaybackDemo = true; refusedHere("demo playback refuses it"); CLIPARAMS.mPlaybackDemo = false;
#ifdef FAKE_NETWORK
    SetBootRequested(true); refusedHere("a network game refuses it"); SetBootRequested(false);
#endif
    check(!PortCePhotoSet(true) && PortCePhotoSet(false), "photo remains unavailable after fences");
}

static void proof() {
    // BEA_PORT_PHOTO_PROOF=N under a tape: without a forced CE layer nothing; with it, screenshots but never photo mode.
    setenv("BEA_PORT_TAPE", "1", 1);
    setenv("BEA_PORT_PHOTO_PROOF", "4", 1);
    presents = 0;
    for (int i = 0; i < 8; ++i) check(!frame() && !PortCePhotoOn(), "the proof switch alone opens nothing");
    setenv("BEA_PORT_CE_LAYER", "1", 1);
    presents = 0;
    std::string saved;
    for (int i = 0; i < 12; ++i) if (frame()) saved += std::to_string(presents) + " ";
    check(!PortCePhotoOn() && CLIPARAMS.mKillHUD == FALSE, "the proof switch cannot enable photo mode");
    check(saved == "4 8 12 ", "the proof screenshots are every 4th Present exactly");
    setenv("BEA_PORT_PHOTO_PROOF", "0", 1);
    PortCePhotoFrame();
    check(!PortCePhotoOn() && CLIPARAMS.mKillHUD == FALSE, "without the proof the tape fence ends it");
    setenv("BEA_PORT_PHOTO_PROOF", "4", 1);
#ifdef FAKE_NETWORK
    SetBootRequested(true);
    presents = 0;
    for (int i = 0; i < 8; ++i) check(!frame() && !PortCePhotoOn(), "never in a network game, even with the proof");
    SetBootRequested(false);
#endif
    PortCePhotoLevelEnd();
    unsetenv("BEA_PORT_TAPE"); unsetenv("BEA_PORT_PHOTO_PROOF"); unsetenv("BEA_PORT_CE_LAYER");
}
#endif

int main(int argc, char **argv) {
#ifdef CE_VERSION
    if (argc > 1 && !std::strcmp(argv[1], "denied")) {
        // The CE layer off (a replay, a recording, or forced off): nothing at all.
        refusedHere("a disabled layer takes no key");
        PortCePhotoFrame();
        check(!frame() && CLIPARAMS.mKillHUD == FALSE && draws.empty(), "a disabled layer saves and draws nothing");
        std::puts("disabled mode passed");
        return 0;
    }
    keys();
    owners();
    levelEnd();
    fences();
    proof();
    std::puts("photo mode keys, owners, level end, fences and proof passed");
#else
    (void)argc; (void)argv;
    down(DIK_F11); up(DIK_F11); down(DIK_F12); up(DIK_F12);
    check(LT.KeyWasDown[DIK_F11] && LT.KeyWasDown[DIK_F12] && CONSOLE.binds.size() == 4 && CLIPARAMS.mKillHUD == FALSE,
          "faithful message path unchanged");
    std::puts("faithful message path passed");
#endif
}
