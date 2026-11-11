// SPDX-License-Identifier: GPL-3.0-only
// The CE rumble on a real SDL virtual gamepad: the production DirectInput shim (dinput.cpp, window.cpp, portlog.cpp),
// the game's own pad opening (InitDirectInput and its callbacks from ltshell.cpp after the port patches), the patched
// CPCPlatform::TriggerRumble, CBattleEngine::AddShockShake and RecoilWeapon (BattleEngine.cpp), and the actual
// ce_rumble.cpp. The virtual pad's rumble callback records what SDL is asked for. One case per process.
// The SDL window/mouse boundary below is the one input_lifetime_driver.cpp uses.
#define INITGUID
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>
#include <dinput.h>
#include <SDL3/SDL.h>

extern "C" int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }	// ce.cpp's cheat gate: no network session in this fixture
static bool focused = true, relativeMode, injectFocusLoss;
static int SDLCALL FocusLossProducer(void *);
static float relativeX, relativeY;
static SDL_MouseButtonFlags relativeButtons;
static std::vector<SDL_JoystickID> virtualIds;
static SDL_Window *testWindow = reinterpret_cast<SDL_Window *>(1);
static SDL_Window *TestMouseFocus() { return focused ? testWindow : nullptr; }
static SDL_MouseButtonFlags TestGlobalMouse(float *x, float *y) { if (x) *x = 2300; if (y) *y = 300; return 0; }
static SDL_MouseButtonFlags TestWindowMouse(float *x, float *y) { if (x) *x = 380; if (y) *y = 300; return 0; }
static SDL_WindowFlags TestWindowFlags(SDL_Window *) {
    SDL_WindowFlags captured = focused ? SDL_WINDOW_INPUT_FOCUS : 0;
    if (injectFocusLoss) {
        injectFocusLoss = false;
        SDL_Thread *thread = SDL_CreateThread(FocusLossProducer, "input-focus-test", nullptr);
        if (!thread) exit(7);
        SDL_WaitThread(thread, nullptr);
    }
    return captured;
}
static bool TestSetRelative(SDL_Window *, bool enabled) { relativeMode = enabled; return true; }
static bool TestGetRelative(SDL_Window *) { return relativeMode; }
static SDL_MouseButtonFlags TestRelativeState(float *x, float *y) {
    if (x) *x = relativeX; if (y) *y = relativeY;
    relativeX = relativeY = 0; return relativeButtons;
}
static SDL_WindowID TestWindowID(SDL_Window *) { return 17; }
static SDL_JoystickID *TestGamepads(int *count) {
    *count = virtualIds.size();
    auto ids = static_cast<SDL_JoystickID *>(SDL_malloc((*count + 1) * sizeof(SDL_JoystickID)));
    for (int i = 0; i < *count; ++i) ids[i] = virtualIds[i];
    ids[*count] = 0; return ids;
}
#define SDL_GetWindowFlags TestWindowFlags
#define SDL_SetWindowRelativeMouseMode TestSetRelative
#define SDL_GetWindowRelativeMouseMode TestGetRelative
#define SDL_GetRelativeMouseState TestRelativeState
#define SDL_GetWindowID TestWindowID
#define SDL_GetGamepads TestGamepads
#define SDL_GetGlobalMouseState TestGlobalMouse
#define SDL_GetMouseState TestWindowMouse
#define SDL_GetMouseFocus TestMouseFocus
// window.cpp only reads LT's raw key array for its optional debug switch. Keep that independent of the caller fixture.
#define LT PortWindowShellBytes
#include "WINDOW_CPP"
#undef LT
#include "DINPUT_CPP"
#include "PORTLOG_CPP"
static int SDLCALL FocusLossProducer(void *) {
    focused = false;
    SDL_Event event{}; event.type = SDL_EVENT_WINDOW_FOCUS_LOST; event.window.windowID = 17;
    return SDL_PushEvent(&event) ? 0 : 1;
}
extern "C" { char PortWindowShellBytes[0x331e4]; unsigned PortPresentCount(void) { return 0; }
int PortMapMouse(float *, float *) { return 0; } }
static bool recording, replay, tapeCursor;
struct RecordedKey { bool down; unsigned vk, scan; };
static std::vector<RecordedKey> recordedKeys;
static unsigned recordedWindowMouse, recordedMotion, recordedWheel, recordedButtons;
bool PortRecording() { return recording; }
bool PortRecordInputSpace(unsigned) { return true; }
void PortRecordKey(bool down, unsigned vk, unsigned scan) { recordedKeys.push_back({down, vk, scan}); }
void PortRecordChar(unsigned, unsigned) {}
void PortRecordWheel(float) { ++recordedWheel; }
void PortRecordMotion(float, float) { ++recordedMotion; }
void PortRecordButton(int, bool) { ++recordedButtons; }
void PortRecordWindowMouse(UINT, LPARAM, int) { ++recordedWindowMouse; }
bool PortTapeCursor(POINT *position) {
    if (!tapeCursor) return false;
    if (position) { position->x = 77; position->y = 88; }
    return true;
}
bool PortReplayMouse(long *dx, long *dy, long *dz, int *buttons) {
    if (!replay) return false;
    *dx = 3; *dy = -4; *dz = 120; *buttons = 1; return true;
}
int PortWideModes() { return 0; }
int PortWideActive() { return 0; }

void PortVideoInit() {}

#define MAX_JOYPADS 4
using UBYTE = unsigned char;
#define ASSERT assert
#define SAFE_RELEASE(pointer) do { if (pointer) { (pointer)->Release(); (pointer) = nullptr; } } while (0)
static HMODULE TestModuleHandle(const char *) { return nullptr; }
#define GetModuleHandle TestModuleHandle
struct { bool mBuildResources = false, mAutoConfigTest = false, mRecordDemo = false, mPlaybackDemo = false, mAttractMode = false;
         int mStressTest = 0; } CLIPARAMS;
BOOL PLAYABLE_DEMO = FALSE;
struct Console { void Print(const char *, ...) {} void AddMessage(const char *, ...) {} } CONSOLE, SETUP_HISTORY;
struct PCLTShell {
    IDirectInputDevice8A *pJoystick[MAX_JOYPADS]{};
    IDirectInput8A *pDI = nullptr;
    int mJoypads = 0, mDeadZone = 0;
    BOOL DodgyJoyX[MAX_JOYPADS]{}, mJoypadHasRz[MAX_JOYPADS]{};
    bool mJoypadIsNewtype[MAX_JOYPADS]{}, mUnknown33474[MAX_JOYPADS]{true, true, true, true};
    DIDEVCAPS diDevCaps{};
    static BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE *, VOID *);
    static BOOL CALLBACK EnumAxesCallback(const DIDEVICEOBJECTINSTANCE *, VOID *);
    HRESULT InitDirectInput(HWND);
} LT;
struct CPCPlatform { void TriggerRumble(int pad); } PLATFORM;
static unsigned randoms;
struct { int Random() { return int(++randoms * 7u); } } GAME;
struct CPlayer { int number = 1; int GetNumber() { return number; } };
struct CCockpit { void AddShockShake(float) {} };
struct CWeapon { float power; float GetPower() { return power; } };
struct CBattleEngine {
    float mYawShake = 0, mPitchShake = 0, mRollShake = 0, mShakeR = 0, mVibration = 0;
    CCockpit *mCockpit = nullptr;
    CPlayer *mPlayer = nullptr;
    void AddShockShake(float amount);
    void RecoilWeapon(CWeapon *inWeapon);
};
static unsigned sKeysRead, spKeysRead;
#include "ce_rumble.h"
#include "CALLERS_INC"
#include "RUMBLE_CPP"

struct Request { unsigned pad, low, high; Uint64 at; };
static std::vector<Request> rumbles;			// what SDL asked each virtual pad, in order
static bool SDLCALL Rumble(void *pad, Uint16 low, Uint16 high) {
    rumbles.push_back({unsigned(uintptr_t(pad)), low, high, SDL_GetTicks()}); return true;
}
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL: %s at %d (rumbles %zu) SDL=%s\n", #expr, __LINE__, rumbles.size(), SDL_GetError()); return 1; } } while (0)
static SDL_JoystickID Attach(bool canRumble) {
    SDL_VirtualJoystickDesc d;
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD; d.naxes = SDL_GAMEPAD_AXIS_COUNT; d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1; d.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    d.name = "Rumble test pad";
    d.userdata = reinterpret_cast<void *>(uintptr_t(virtualIds.size() + 1));
    if (canRumble) d.Rumble = Rumble;
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
    if (id) virtualIds.push_back(id);
    return id;
}
// Runs SDL's joystick update for ms milliseconds (it ends a rumble whose time is up with a (0, 0) request).
static void Pump(Uint64 ms) { Uint64 start = SDL_GetTicks(); while (SDL_GetTicks() - start < ms) { SDL_UpdateJoysticks(); SDL_Delay(2); } }
static bool Stop(const Request &r) { return r.low == 0 && r.high == 0; }
static bool Full(const Request &r, unsigned pad) { return r.pad == pad && r.low == 0xffff && r.high == 0; }
int main(int argc, char **argv) {
    if (argc != 2 || !SDL_Init(SDL_INIT_GAMEPAD)) return 2;
    recording = getenv("TEST_RECORDING") != nullptr;
    gNumWindows = 1; gWindows[0].sdl = testWindow; gWindows[0].handle = reinterpret_cast<HWND>(1);
    const std::string test = argv[1];
    CHECK(Attach(test != "no-motor"));
    if (test == "two-pads") CHECK(Attach(true));
    CHECK(SUCCEEDED(LT.InitDirectInput(reinterpret_cast<HWND>(1))));
    CHECK(LT.mJoypads == (test == "two-pads" ? 2 : 1));
    CPlayer player; CBattleEngine engine; engine.mPlayer = &player;
    if (test == "hit" || test == "fenced") {
        engine.AddShockShake(0.0005f);
        CHECK(rumbles.empty());						// below AddShockShake's 0.001: no shake, no rumble
        engine.AddShockShake(0.1f);					// a hit: CBattleEngine::Damage passes (life lost) / 8
        if (test != "hit") { Pump(50); CHECK(rumbles.empty()); puts("no rumble"); return 0; }
        CHECK(rumbles.size() == 1 && Full(rumbles[0], 1));
        const Uint64 hit = rumbles[0].at;
        Pump(150);
        engine.AddShockShake(0.1f);					// a second hit while it plays restarts the 250 ms
        const Uint64 again = SDL_GetTicks();
        CHECK(rumbles.size() == 1);					// same strengths: SDL only moves the end
        Pump(600);
        CHECK(rumbles.size() == 2 && Stop(rumbles[1]) && rumbles[1].at >= again + 240 && rumbles[1].at <= again + 400);
        CWeapon gun{0.05f};
        engine.RecoilWeapon(&gun);					// firing shakes the view and rumbles too
        CHECK(rumbles.size() == 3 && Full(rumbles[2], 1));
        Pump(600);
        CHECK(rumbles.size() == 4 && Stop(rumbles[3]) && rumbles[3].at - rumbles[2].at >= 240 && rumbles[3].at - rumbles[2].at <= 400);
        printf("hit: rumble low=%u high=%u for %llu ms; again %llu ms after the first hit; recoil rumbles too\n",
               rumbles[2].low, rumbles[2].high, (unsigned long long)(rumbles[3].at - rumbles[2].at),
               (unsigned long long)(again - hit));
        return 0;
    }
    if (test == "two-pads") {
        player.number = 2;							// player 2 rumbles the second pad (Stuart: number - 1)
        engine.AddShockShake(0.2f);
        CHECK(rumbles.size() == 1 && Full(rumbles[0], 2));
        player.number = 5;							// past the pads the game opened: nothing
        engine.AddShockShake(0.2f);
        player.number = 0;
        engine.AddShockShake(0.2f);
        CHECK(rumbles.size() == 1);
        puts("pads mapped");
        return 0;
    }
    if (test == "no-motor") {
        engine.AddShockShake(0.2f);
        Pump(20);
        CHECK(rumbles.empty());
        puts("a pad without motors is left alone");
        return 0;
    }
    return 3;
}
