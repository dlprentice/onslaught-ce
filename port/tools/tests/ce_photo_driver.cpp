// SPDX-License-Identifier: GPL-3.0-only
// Execute the production CE photo mode (port/game/ce_photo.cpp) with the real patched message handler against the console's
// authored fake game boundary (port/tools/ce_palette_fake.h): the keys and who owns them, the draw filter it switches and the
// scope that opens it, its fences and proof switch, the screenshot request and the note that must never be in the frame being
// saved. Photo mode never touches the developer -killhud field: the HUD routines run, only their draw calls are dropped.
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
// d3d9hooks.cpp's draw filter, recorded: whether it is in the device's draw entries and how many scopes are open.
static bool filtered = false;
static int dropped = 0, filterCalls = 0;
void PortCeDrawHooks(bool on) { filtered = on; ++filterCalls; if (!on) dropped = 0; }
void PortCeDrawsDropped(int delta) { dropped += delta; if (dropped < 0) dropped = 0; }
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
    // F11 switches photo mode and the draw filter with it; the key is taken from the game (no key state, no bind); repeats are
    // taken and ignored; the developer field is never touched.
    clearKeys();
    down(DIK_F11);
    check(PortCePhotoOn() && filtered && CLIPARAMS.mKillHUD == FALSE, "F11 turns photo mode and the draw filter on, and leaves -killhud alone");
    check(!LT.KeyDown[DIK_F11] && !LT.KeyWasDown[DIK_F11] && CONSOLE.binds.empty(), "F11 is not a game key down");
    down(DIK_F11, true);
    check(PortCePhotoOn() && !LT.KeyDown[DIK_F11], "a held F11 repeats nothing and stays taken");
    up(DIK_F11);
    check(!LT.KeyDown[DIK_F11], "F11's release leaves no key down");
    render();
    check(drew(L"Photo mode: HUD hidden. F11 returns, F12 saves a screenshot."), "photo mode says how to leave it");
    for (const auto &d : draws) check(d.x >= 0 && d.y >= 0 && d.x + float(d.text.size() * 8) * d.sx <= PLATFORM.w + 0.01f, "the note fits");
    press(DIK_F11);
    check(!PortCePhotoOn() && !filtered && dropped == 0 && CLIPARAMS.mKillHUD == FALSE, "F11 again ends photo mode and the draw filter");
    render();
    check(draws.empty(), "the note goes with photo mode");
    check(PortCePhotoSet(true) && PortCePhotoOn() && filtered, "the console turns it on");
    check(PortCePhotoSet(false) && !PortCePhotoOn() && !filtered, "and off");
    // Other keys stay the game's.
    clearKeys();
    down(0x21);
    check(LT.KeyDown[0x21] && CONSOLE.binds.empty(), "F (the free camera) is still the game's key");
    up(0x21);
    // F12 asks for the next Present; the note is never drawn in that frame; one request is one picture.
    clearKeys();
    press(DIK_F11);
    render();
    check(drew(L"Photo mode: HUD hidden."), "the photo mode note is showing");
    press(DIK_F12);
    check(!LT.KeyDown[DIK_F12] && !LT.KeyWasDown[DIK_F12], "F12 is not a game key down");
    render();
    check(draws.empty() && sprites.empty(), "nothing of the note is in the frame being saved");
    check(PortCePhotoShotWanted(), "the Present after F12 is saved");
    check(!PortCePhotoShotWanted(), "one F12, one screenshot");
    render();
    check(drew(L"Photo mode: HUD hidden."), "the note is back in the next frame");
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
    // Screenshots without photo mode too: the frame as the player sees it.
    press(DIK_F11);
    check(!PortCePhotoOn(), "photo mode off");
    press(DIK_F12); check(frame(), "F12 works without photo mode");
    check(!frame(), "and only once");
}

static void scope() {
    // The scope the patched HUD, battle line and cockpit routines open: it drops draw calls only while photo mode is on, nests,
    // and always closes.
    PortCePhotoSet(false);
    { CePhotoDrawScope s; check(dropped == 0, "a scope with photo mode off drops nothing"); }
    PortCePhotoSet(true);
    check(dropped == 0, "photo mode alone drops nothing outside a scope");
    {
        CePhotoDrawScope outer;
        check(dropped == 1, "a scope drops while photo mode is on");
        { CePhotoDrawScope inner; check(dropped == 2, "scopes nest"); }
        check(dropped == 1, "an inner scope closes only itself");
    }
    check(dropped == 0, "the draw calls after the routine are drawn");
    // Photo mode turned off with a scope open (not a thing a frame does) leaves the filter off and the count zero.
    { CePhotoDrawScope s; PortCePhotoSet(false); check(dropped == 0 && !filtered, "photo mode off clears the count"); }
    check(dropped == 0, "and the scope's close never takes it below zero");
    // A scope opened before photo mode was on does not close one it never opened.
    PortCePhotoSet(false);
    { CePhotoDrawScope s; PortCePhotoSet(true); check(dropped == 0, "a scope opened with photo mode off stays shut"); }
    check(dropped == 0, "and closing it changes nothing");
    PortCePhotoSet(false);
}

// A routine that leaves a block in the middle: the scope is a destructor's, so an early return, a continue and a break close it.
static int leaving(bool early) { CePhotoDrawScope s; if (early) return dropped; return dropped * 10; }

static void leaks() {
    // A draw made outside any scope while photo mode is on is drawn (the count is zero there); a scope never stays open.
    PortCePhotoSet(true);
    check(dropped == 0, "outside any scope nothing is dropped while photo mode is on");
    check(leaving(true) == 1 && dropped == 0, "an early return closes the scope");
    check(leaving(false) == 10 && dropped == 0, "so does the end of the routine");
    for (int i = 0; i < 4; ++i) {
        CePhotoDrawScope s;
        if (i == 1) continue;
        check(dropped == 1, "a loop's scope is open in its body");
        if (i == 2) break;
    }
    check(dropped == 0, "a continue and a break close it");
    // Photo mode ended between two frames: a scope opened the frame before has closed, the next one is inert.
    { CePhotoDrawScope s; }
    PortCePhotoSet(false);
    check(leaving(true) == 0 && dropped == 0 && !filtered, "scopes are inert when photo mode is off");
}

static void devices() {
    // A device made anew has its own draw entries: the between-frames hook puts the filter back while photo mode is on.
    PortCePhotoSet(true);
    filtered = false;
    PortCePhotoFrame();
    check(PortCePhotoOn() && filtered, "photo mode re-applies the filter to a device made anew");
    PortCePhotoSet(false);
    const int calls = filterCalls;
    PortCePhotoFrame();
    check(!PortCePhotoOn() && !filtered && filterCalls >= calls, "off stays off");
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
    // Level end: photo mode and the filter off, no screenshot carried into the next level.
    CLIPARAMS.mKillHUD = FALSE;
    press(DIK_F11);
    check(PortCePhotoOn() && filtered, "photo mode on before the level ends");
    press(DIK_F12);
    PortCePhotoLevelEnd();
    check(!PortCePhotoOn() && !filtered && dropped == 0 && CLIPARAMS.mKillHUD == FALSE, "level end ends photo mode");
    check(!PortCePhotoShotWanted(), "level end drops a screenshot not yet taken");
    // A developer -killhud set by someone else is theirs: photo mode neither sets nor clears it.
    CLIPARAMS.mKillHUD = TRUE;
    check(PortCePhotoSet(true) && CLIPARAMS.mKillHUD == TRUE, "photo on over a set field");
    PortCePhotoLevelEnd();
    check(CLIPARAMS.mKillHUD == TRUE, "the field is as it was");
    CLIPARAMS.mKillHUD = FALSE;
    PortCePhotoLevelEnd();
    check(CLIPARAMS.mKillHUD == FALSE, "level end without photo mode changes nothing");
}

static void refusedHere(const char *why) {
    clearKeys();
    down(DIK_F11); up(DIK_F11); down(DIK_F12); up(DIK_F12);
    check(!PortCePhotoOn() && !filtered && LT.KeyWasDown[DIK_F11] && LT.KeyWasDown[DIK_F12], why);
    check(!PortCePhotoSet(true) && !PortCePhotoOn() && !filtered && CLIPARAMS.mKillHUD == FALSE, why);
    check(!PortCePhotoShotWanted(), why);
}

static void fences() {
    // Every replay, recording, probe, clock, pilot and network switch; recording; multiplayer, demos and the front end.
    for (const char *flag : kFlags) {
        check(PortCePhotoSet(true), "photo mode before the fence");
        press(DIK_F12);
        setenv(flag, "1", 1);
        PortCePhotoFrame();
        unsetenv(flag);
        check(!PortCePhotoOn() && !filtered, "a fence ends photo mode and the filter");
        check(!PortCePhotoShotWanted(), "a fence permanently discards a pending screenshot");
        setenv(flag, "1", 1);
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
    check(PortCePhotoSet(true) && PortCePhotoSet(false), "allowed again");
}

static void proof() {
    // BEA_PORT_PHOTO_PROOF=N under a tape: nothing without the CE layer forced; with it, photo mode in the level and a
    // screenshot at every Present that is a multiple of N, exactly.
    setenv("BEA_PORT_TAPE", "1", 1);
    setenv("BEA_PORT_PHOTO_PROOF", "4", 1);
    presents = 0;
    for (int i = 0; i < 8; ++i) check(!frame() && !PortCePhotoOn(), "the proof switch alone opens nothing");
    setenv("BEA_PORT_CE_LAYER", "1", 1);
    presents = 0;
    std::string saved;
    for (int i = 0; i < 12; ++i) if (frame()) saved += std::to_string(presents) + " ";
    check(PortCePhotoOn() && filtered && CLIPARAMS.mKillHUD == FALSE, "the proof switch keeps photo mode on and -killhud alone");
    check(saved == "4 8 12 ", "the proof screenshots are every 4th Present exactly");
    setenv("BEA_PORT_PHOTO_PROOF", "0", 1);
    PortCePhotoFrame();
    check(!PortCePhotoOn() && !filtered, "without the proof the tape fence ends it");
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

static void proofNote() {
    // The proof switch's frames are saved without the note too, as F12's are: the note of the picture before is drawn in the frames
    // between, never in one that is to be saved.
    setenv("BEA_PORT_TAPE", "1", 1);
    setenv("BEA_PORT_PHOTO_PROOF", "4", 1);
    setenv("BEA_PORT_CE_LAYER", "1", 1);
    PortCePhotoLevelEnd();
    presents = 0;
    int noted = 0, quiet = 0;
    for (int i = 1; i <= 16; ++i) {
        PortCePhotoFrame();
        render();
        const bool savedNow = (presents + 1) % 4 == 0;
        if (savedNow) check(draws.empty() && sprites.empty(), "nothing of a note is in a frame the proof switch saves");
        else if (i > 4) { check(drew(L"Saved screenshot proof.png"), "the note of the picture before is in the frames between"); ++noted; }
        else ++quiet;
        ++presents;
        if (PortCePhotoShotWanted()) PortCePhotoShotDone(CE_SHOT_SAVED, "proof.png");
    }
    check(noted == 9 && quiet == 3, "the frames between the pictures were checked");
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
        check(!frame() && !filtered && CLIPARAMS.mKillHUD == FALSE && draws.empty(), "a disabled layer saves and draws nothing");
        std::puts("disabled mode passed");
        return 0;
    }
    keys();
    scope();
    leaks();
    devices();
    owners();
    levelEnd();
    fences();
    proof();
    proofNote();
    std::puts("photo mode keys, owners, level end, fences and proof passed");
#else
    (void)argc; (void)argv;
    down(DIK_F11); up(DIK_F11); down(DIK_F12); up(DIK_F12);
    check(LT.KeyWasDown[DIK_F11] && LT.KeyWasDown[DIK_F12] && CONSOLE.binds.size() == 4 && CLIPARAMS.mKillHUD == FALSE,
          "faithful message path unchanged");
    std::puts("faithful message path passed");
#endif
}
