// SPDX-License-Identifier: GPL-3.0-only
// Production shims and complete source callers, with bounded SDL mouse/window boundaries and real virtual gamepads.
#define INITGUID
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include <windows.h>
#include <dinput.h>
#include <SDL3/SDL.h>
#define PI 3.14159265358979f
#define ANALOGUE_X1 -1
#define ANALOGUE_Y1 -2
#define ANALOGUE_X2 -3
#define ANALOGUE_Y2 -4
#define ANALOGUE_POV_X -5
#define ANALOGUE_POV_Y -6
#define ANALOGUE_DEAD_ZONE 0.15f
#define ANALOGUE_DEAD_ZONE_SCALE ((float)(1.0 / (1.0 - 0.15)))

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
// Rebind enumerates joysticks, not gamepads. Restrict that path too to this fixture's still-connected virtual IDs.
static SDL_JoystickID *TestJoysticks(int *count) {
    std::vector<SDL_JoystickID> connected;
    for (SDL_JoystickID id : virtualIds) {
        SDL_Joystick *joy = SDL_GetJoystickFromID(id);
        if (joy && SDL_JoystickConnected(joy)) connected.push_back(id);
    }
    *count = connected.size();
    auto ids = static_cast<SDL_JoystickID *>(SDL_malloc((*count + 1) * sizeof(SDL_JoystickID)));
    if (!ids) { *count = 0; return nullptr; }
    for (int i = 0; i < *count; ++i) ids[i] = connected[i];
    ids[*count] = 0; return ids;
}
#define SDL_GetWindowFlags TestWindowFlags
#define SDL_SetWindowRelativeMouseMode TestSetRelative
#define SDL_GetWindowRelativeMouseMode TestGetRelative
#define SDL_GetRelativeMouseState TestRelativeState
#define SDL_GetWindowID TestWindowID
#define SDL_GetGamepads TestGamepads
#define SDL_GetJoysticks TestJoysticks
#define SDL_GetGlobalMouseState TestGlobalMouse
#define SDL_GetMouseState TestWindowMouse
#define SDL_GetMouseFocus TestMouseFocus
// window.cpp only reads LT's raw key array for its optional debug switch. Keep that independent of the caller fixture.
#define LT PortWindowShellBytes
#include "WINDOW_CPP"
#undef LT
#include "DINPUT_CPP"
#include "PORTLOG_CPP"
#include "INVENTORY_CPP"
static int SDLCALL FocusLossProducer(void *) {
    focused = false;
    SDL_Event event{}; event.type = SDL_EVENT_WINDOW_FOCUS_LOST; event.window.windowID = 17;
    return SDL_PushEvent(&event) ? 0 : 1;
}
extern "C" { char PortWindowShellBytes[0x331e4]; unsigned PortPresentCount(void) { return 0; }
int PortMapMouse(float *, float *) { return 0; } }
static bool recording, replay, tapeCursor, ceLayer = true;
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
bool PortCeLayer() { return ceLayer; }
void PortVideoInit() {}
bool PortDevLog() { return false; }

#define MAX_JOYPADS 4
using UBYTE = unsigned char;
#define ASSERT assert
#define SAFE_RELEASE(pointer) do { if (pointer) { (pointer)->Release(); (pointer) = nullptr; } } while (0)
static unsigned sKeysRead, spKeysRead;
static HMODULE TestModuleHandle(const char *) { return nullptr; }
#define GetModuleHandle TestModuleHandle
struct { bool mBuildResources = false, mAutoConfigTest = false; } CLIPARAMS;
struct Console { void Print(const char *, ...) {} void AddMessage(const char *, ...) {} } CONSOLE, SETUP_HISTORY, LOG;
struct PCLTShell {
    IDirectInputDevice8A *pJoystick[MAX_JOYPADS]{};
    uintptr_t canary = 0x12345678;
    IDirectInput8A *pDI = nullptr;
    int mJoypads = 0, mDeadZone = 0;
    BOOL DodgyJoyX[MAX_JOYPADS]{}, mJoypadHasRz[MAX_JOYPADS]{};
    bool mJoypadIsNewtype[MAX_JOYPADS]{}, mUnknown33474[MAX_JOYPADS]{true, true, true, true};
    DIDEVCAPS diDevCaps{};
    DIJOYSTATE2 states[MAX_JOYPADS]{}, oldStates[MAX_JOYPADS]{};
    DIJOYSTATE2 *mJoyState[MAX_JOYPADS], *mOldJoyState[MAX_JOYPADS];
    PCLTShell() { for (int i = 0; i < MAX_JOYPADS; ++i) { mJoyState[i] = &states[i]; mOldJoyState[i] = &oldStates[i]; } }
    DIJOYSTATE2 *JoyState(int pad) { return mJoyState[pad]; }
    DIJOYSTATE2 *OldJoyState(int pad) { return mOldJoyState[pad]; }
    JOY_BUTTON_ACCESSORS
    static BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE *, VOID *);
    static BOOL CALLBACK EnumAxesCallback(const DIDEVICEOBJECTINSTANCE *, VOID *);
    HRESULT InitDirectInput(HWND);
    HRESULT UpdateJoystick(int, bool);
} LT;
#if defined(CE_EDGE_TESTS)
enum { BUTTON_ON, BUTTON_ONCE, BUTTON_RELEASE, BUTTON_REPEAT, ANALOGUE_PLUS, ANALOGUE_MINUS,
       ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT, KEY_ONCE, KEY_ON, KEY_RELEASE,
       MOUSE_AXIS_PLUS_ANY, MOUSE_AXIS_MINUS_ANY, MOUSE_AXIS_PLUS, MOUSE_AXIS_MINUS, MOUSE_BUTTON_ON,
       MOUSE_BUTTON_ONCE, MOUSE_BUTTON_RELEASE };
enum { BUTTON_FRONTEND_CHEAT = 45 };
constexpr float ANALOGUE_ACT_AS_DIGITAL_THRESHOLD = .9f;
static bool testKeyOn;
struct SControlInput { int mPad, mType; short mKey, mVKey; };
struct SControlBinding { bool mConfigurable; int mId; SControlInput mInput[2]; } CONTROL_BINDINGS[4];
struct FixtureDemoBuffer {
    unsigned words[3]{}, cursor = 0;
    void Read(void *out, size_t n) { assert(cursor < 3 && n == sizeof(unsigned)); memcpy(out, &words[cursor++], n); }
    bool EndOfFile() { return false; }
    void Close() {}
};
struct CController {
    int mPadNumber = 0, mNumMappings = 0;
    bool mPlaying = false, mRecording = false, mPausedBeforeMap = false;
    float mLastRepeatTime = 0, mNextDelayTime = 0;
    unsigned mButtons1 = 0, mButtons2 = 0, mButtons3 = 0, mButtons1Old = 0, mButtons2Old = 0, mButtons3Old = 0;
    std::vector<int> actions;
    std::vector<float> actionValues;
    virtual int GetJoyButtonOnce(int, int) = 0;
    virtual int GetJoyButtonOn(int, int) = 0;
    virtual int GetJoyButtonRelease(int, int) = 0;
    virtual float GetJoyAnalogueLeftX(int) = 0;
    virtual float GetJoyAnalogueLeftY(int) = 0;
    virtual float GetJoyAnalogueRightX(int) = 0;
    virtual float GetJoyAnalogueRightY(int) = 0;
    virtual float GetJoyPovX(int) = 0;
    virtual float GetJoyPovY(int) = 0;
    void Flush();
    void DoMappings();
    void CalcNumMappings() {}
    virtual void ReadControllerState() = 0;
    void RecordControllerState() {}
    bool RepeatOccured(BOOL, SControlBinding *) { return false; }
    float GetMappedInputValue(int, int);
    bool GetKeyOnce(int) { return false; }
    bool GetKeyOn(int) { return testKeyOn; }
    bool GetKeyRelease(int) { return false; }
    bool IsButtonSet(int id) { return id >= 0 && id < 32 && (mButtons1 & (1u << id)); }
    void UnsetButton(int id) { if (id >= 0 && id < 32) mButtons1 &= ~(1u << id); }
    void SendButtonAction(int id, float value) {
        if (id >= 0 && id < 32) mButtons1 |= 1u << id; actions.push_back(id); actionValues.push_back(value);
    }
};
struct CPCController : CController {
    FixtureDemoBuffer mDataFile;
    void ReadControllerState() override;
    int GetJoyButtonOnce(int, int) override;
    int GetJoyButtonOn(int, int) override;
    int GetJoyButtonRelease(int, int) override;
#else
struct CPCController {
#endif
    float GetJoyAnalogueLeftX(int), GetJoyAnalogueLeftY(int), GetJoyAnalogueRightX(int), GetJoyAnalogueRightY(int);
    float GetJoyPovX(int), GetJoyPovY(int);
};
static IDirectInputDevice8A *sMouse;
static DIMOUSESTATE2 sMouseState;
static int MOUSE_X, MOUSE_Y, MOUSE_WHEEL;
static bool MOUSE_HELD[3], gKeyPressed, sMouseInputActive, sMouseMoved;
static int sLastMoveX, sLastMoveY;
static float sMouseFracX, sMouseFracY;
struct MouseButton { BOOL mDown, mReleased; int mX, mY; float mFracX, mFracY; } MOUSE_BUTTONS[3];
enum { MOUSE_EVENT_UP = 3, MOUSE_EVENT_DOWN = 4 };
using MouseTrapper = bool (*)(WORD, int);
#define MOUSE_TRAP static_cast<MouseTrapper>(nullptr)
struct { bool IsRunning() { return false; } } FRONTEND;
struct { int GetWindowWidth() { return 640; } int GetWindowHeight() { return 480; }
    float GetSysTimeFloat() { return 0; } bool KeyOn(int) { return false; } } PLATFORM;
static void SetMouseInputActive(BOOL value) { sMouseInputActive = value; }
static void ResetMouseTransientState() { MOUSE_WHEEL = 0; }
static int PortUiMouse(int *, int *) { return 0; }
#define GetCursorPos PortGetCursorPos
#include "CALLERS_INC"

#if defined(CE_EDGE_TESTS)
static bool testMultiplayer, testBonus;
struct InputTestGame { bool IsMultiplayer() { return testMultiplayer; } bool IsPaused() { return false; } } GAME;
bool PortCeBonusCoopRunning() { return testBonus; }
#if defined(CE_VERSION)
GAMEPAD_EDGE_FUNCTIONS
#endif
static bool gMouseAxisLocked[MAX_JOYPADS], sSendingMouseRest, sMouseAxisRead;
static float gMouseSensitivity = 1;
#if defined(CE_VERSION)
static int sCeMouseRestId = -1;
#endif
static float DigitalValueForButton(int) { return 1; }
int PortCeGamepadInputs(int, int, SControlInput *) { return 0; } // isolate the actual saved-binding mapping path
GAMEPAD_MAPPING_FUNCTIONS
#endif

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s at %d SDL=%s\n", #expr, __LINE__, SDL_GetError()); return 1; } } while (0)
static unsigned windowMouse, directDown, directUp, focusNotifications;
static LRESULT WINAPI Proc(HWND, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSEWHEEL) { ++windowMouse; HandleMouseMessage(msg, wp, lp); }
    if (msg == WM_KEYDOWN) ++directDown;
    if (msg == WM_KEYUP) ++directUp;
    if (msg == WM_ACTIVATE || msg == WM_ACTIVATEAPP) ++focusNotifications;
    return 0;
}
static void Drain() {
    MSG message;
    unsigned steps = 0;
    while (PortPeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
        PortDispatchMessageA(&message);
        if (++steps > 4096) exit(7);
    }
}
static void Wheel(float value, SDL_WindowID id = 17) {
    SDL_Event event{}; event.type = SDL_EVENT_MOUSE_WHEEL; event.wheel.windowID = id; event.wheel.y = value;
    if (!SDL_PushEvent(&event)) exit(7);
}
static void MouseEvent(Uint32 type) {
    SDL_Event event{}; event.type = type; event.button.windowID = 17;
    event.button.button = SDL_BUTTON_LEFT; event.button.x = 10; event.button.y = 20;
    if (!SDL_PushEvent(&event)) exit(7);
}
static SDL_Joystick *VirtualPad() {
    SDL_VirtualJoystickDesc descriptor;
    SDL_INIT_INTERFACE(&descriptor);
    descriptor.type = SDL_JOYSTICK_TYPE_GAMEPAD; descriptor.naxes = SDL_GAMEPAD_AXIS_COUNT;
    descriptor.nbuttons = SDL_GAMEPAD_BUTTON_COUNT; descriptor.name = "Input lifetime test";
    descriptor.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    descriptor.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&descriptor);
    if (!id) return nullptr;
    virtualIds.push_back(id);
    return SDL_OpenJoystick(id);
}
int main(int argc, char **argv) {
    if (argc != 2 || !SDL_Init(SDL_INIT_GAMEPAD)) return 2;
    gNumWindows = 1; gWindows[0].sdl = testWindow; gWindows[0].handle = reinterpret_cast<HWND>(1); gWindows[0].proc = Proc;
    // Initialize the production DirectInput factory/watch; the actual InitDirectInput caller creates its own object.
    void *factory = nullptr;
    CHECK(SUCCEEDED(DirectInput8Create(nullptr, 0x800, IID_IDirectInput8A, &factory, nullptr)));
    static_cast<IDirectInput8A *>(factory)->Release();
    const char *test = argv[1];
    if (!strcmp(test, "mouse-fraction-tape")) setenv("BEA_PORT_TAPE", "fixture", 1);
    if (!strcmp(test, "mouse-fraction-record")) setenv("BEA_PORT_RECORD", "fixture", 1);
    if (!strcmp(test, "mouse-fraction-clock")) setenv("BEA_PORT_CLOCK", "fixture", 1);
    if (!strcmp(test, "mouse-fraction-probes")) setenv("BEA_PORT_PROBES", "fixture", 1);
    if (!strcmp(test, "mouse-fraction-disabled")) ceLayer = false;
    if (!strncmp(test, "pad-", 4)) {
        if (!strcmp(test, "pad-edge-tape") || !strncmp(test, "pad-edge-tape-saved", 19) || !strncmp(test, "pad-neutral-tape", 16)) setenv("BEA_PORT_TAPE", "fixture", 1);
        if (!strcmp(test, "pad-edge-record") || !strncmp(test, "pad-edge-record-saved", 21) || !strncmp(test, "pad-neutral-record", 18) || !strcmp(test, "pad-neutral-faithful")) setenv("BEA_PORT_RECORD", "fixture", 1);
        if (!strncmp(test, "pad-edge-pov-record-", 20) || !strncmp(test, "pad-edge-demo-record-", 21) || !strcmp(test, "pad-edge-record-keys")) setenv("BEA_PORT_RECORD", "fixture", 1);
        if (!strncmp(test, "pad-edge-pov-tape-", 18) || !strncmp(test, "pad-edge-demo-tape-", 19) || !strcmp(test, "pad-edge-tape-keys")) setenv("BEA_PORT_TAPE", "fixture", 1);
        if (!strcmp(test, "pad-edge-pov-faithful-disabled")) setenv("BEA_PORT_RECORD", "fixture", 1);
        if (!strcmp(test, "pad-neutral-empty-record")) setenv("BEA_PORT_RECORD", "", 1);
        if (!strcmp(test, "pad-neutral-empty-tape")) setenv("BEA_PORT_TAPE", "", 1);
        if (!strcmp(test, "pad-neutral-clock")) setenv("BEA_PORT_CLOCK", "fixture", 1);
        if (!strcmp(test, "pad-neutral-probes")) setenv("BEA_PORT_PROBES", "fixture", 1);
        if (!strcmp(test, "pad-edge-clock")) setenv("BEA_PORT_CLOCK", "fixture", 1);
        if (!strcmp(test, "pad-edge-probes")) setenv("BEA_PORT_PROBES", "fixture", 1);
        if (!strcmp(test, "pad-trigger-disabled")) ceLayer = false;
        if (!strcmp(test, "pad-trigger-tape")) setenv("BEA_PORT_TAPE", "fixture", 1);
        if (!strcmp(test, "pad-trigger-record")) setenv("BEA_PORT_RECORD", "fixture", 1);
        if (!strcmp(test, "pad-trigger-clock")) setenv("BEA_PORT_CLOCK", "fixture", 1);
        if (!strcmp(test, "pad-trigger-probes")) setenv("BEA_PORT_PROBES", "fixture", 1);
        // The UI override may be forced during a tape; it must not force live controller actions.
        if (strncmp(test, "pad-trigger-", 12) == 0) setenv("BEA_PORT_CE_LAYER", "1", 1);
        std::vector<SDL_Joystick *> pads;
        bool two = !strcmp(test, "pad-edge-two") || !strcmp(test, "pad-edge-cross-binding") ||
                   !strcmp(test, "pad-edge-two-consumers") || !strcmp(test, "pad-edge-repeat-consumer") ||
                   !strcmp(test, "pad-edge-cross-overflow") || !strcmp(test, "pad-edge-bonus-two") ||
                   !strcmp(test, "pad-edge-hotplug-two") || !strcmp(test, "pad-neutral-record-two") ||
                   !strcmp(test, "pad-neutral-tape-two") || !strcmp(test, "pad-edge-record-saved-two") ||
                   !strcmp(test, "pad-edge-tape-saved-two");
        const bool noPad = (!strncmp(test, "pad-edge-pov-", 13) || !strncmp(test, "pad-edge-demo-", 14)) && strstr(test, "-absent");
        for (int i = 0; i < (noPad ? 0 : !strcmp(test, "pad-limit") ? 5 : two ? 2 : 1); ++i)
            { pads.push_back(VirtualPad()); CHECK(pads.back()); }
        if (!strcmp(test, "pad-edge-duplicate-binding")) {
            SDL_GUID guid = SDL_GetJoystickGUID(pads[0]); char s[33]{}, mapping[256]{};
            SDL_GUIDToString(guid, s, sizeof(s));
            snprintf(mapping, sizeof(mapping), "%s,Authored duplicate,a:b0,a:b1,b:b2,start:b6,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,", s);
            CHECK(SDL_AddGamepadMapping(mapping) >= 0);
        }
        if (strncmp(test, "pad-trigger-", 12) == 0) {
            CHECK(SDL_SetJoystickVirtualAxis(pads[0], SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768));
            CHECK(SDL_SetJoystickVirtualAxis(pads[0], SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768));
            SDL_UpdateJoysticks();
        }
        CHECK(SUCCEEDED(LT.InitDirectInput(reinterpret_cast<HWND>(1))));
        CHECK(LT.canary == 0x12345678 && LT.mJoypads == (noPad ? 0 : !strcmp(test, "pad-limit") ? 4 : two ? 2 : 1));
        if (!strncmp(test, "pad-neutral-", 12)) {
            bool blocked = !strncmp(test, "pad-neutral-record", 18) || !strncmp(test, "pad-neutral-tape", 16);
            for (int pad = 0; pad < (two ? 2 : 1); ++pad) {
                CHECK(SUCCEEDED(LT.pJoystick[pad]->Acquire()));
                CHECK(SDL_SetJoystickVirtualAxis(pads[pad], SDL_GAMEPAD_AXIS_LEFTX, 32767));
                CHECK(SDL_SetJoystickVirtualAxis(pads[pad], SDL_GAMEPAD_AXIS_LEFTY, -32768));
                CHECK(SDL_SetJoystickVirtualAxis(pads[pad], SDL_GAMEPAD_AXIS_RIGHTX, 32767));
                CHECK(SDL_SetJoystickVirtualAxis(pads[pad], SDL_GAMEPAD_AXIS_RIGHTY, -32768));
                CHECK(SDL_SetJoystickVirtualButton(pads[pad], SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true));
                CHECK(SDL_SetJoystickVirtualButton(pads[pad], SDL_GAMEPAD_BUTTON_SOUTH, true));
                SDL_UpdateJoysticks();
                CHECK(SUCCEEDED(LT.UpdateJoystick(pad, true)));
                DIJOYSTATE2 *state = LT.JoyState(pad);
                if (blocked) {
                    CHECK(state->lX == 0 && state->lY == 0 && state->lZ == 0 && state->lRz == 0);
                    CHECK(state->lRx == 0 && state->lRy == 0);
                    for (int i = 0; i < 4; ++i) CHECK(state->rgdwPOV[i] == 0xFFFFFFFF);
                    for (int i = 0; i < 32; ++i) CHECK(state->rgbButtons[i] == 0);
                    CHECK(SDL_SetJoystickVirtualButton(pads[pad], SDL_GAMEPAD_BUTTON_SOUTH, false)); SDL_UpdateJoysticks();
                    CHECK(SUCCEEDED(LT.UpdateJoystick(pad, true)) && !LT.JoyButtonRelease(pad, 3));
                } else {
                    CHECK(state->lX == 1000 && state->lY == -1000 && state->lZ == 1000 && state->lRz == -1000);
                    CHECK(state->rgdwPOV[0] == 9000 && LT.JoyButtonOn(pad, 3));
                }
            }
            // A configured non-default range still reports its own center, rather than a hard-coded zero.
            DIPROPRANGE range{}; range.diph.dwSize = sizeof(range); range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            range.diph.dwHow = DIPH_DEVICE; range.lMin = 100; range.lMax = 900;
            CHECK(SUCCEEDED(LT.pJoystick[0]->SetProperty(DIPROP_RANGE, &range.diph)));
            DIJOYSTATE2 raw{}; CHECK(SUCCEEDED(LT.pJoystick[0]->GetDeviceState(sizeof(raw), &raw)));
            CHECK(raw.lX == (blocked ? 500 : 900));
            if (blocked) {
                CHECK(raw.lY == 500 && raw.lZ == 500 && raw.lRx == 500 && raw.lRy == 500 && raw.lRz == 500);
                for (unsigned char button : raw.rgbButtons) CHECK(button == 0);
            }
            DIJOYSTATE rawLegacy{}; CHECK(SUCCEEDED(LT.pJoystick[0]->GetDeviceState(sizeof(rawLegacy), &rawLegacy)));
            CHECK(rawLegacy.lX == (blocked ? 500 : 900));
            if (blocked) for (int i = 0; i < 4; ++i) CHECK(rawLegacy.rgdwPOV[i] == 0xFFFFFFFF);
        }
#if defined(CE_EDGE_TESTS)
        if (!strcmp(test, "pad-edge-pov-faithful-disabled")) {
            CHECK(SUCCEEDED(LT.pJoystick[0]->Acquire())); LT.mUnknown33474[0] = false;
            CHECK(SUCCEEDED(LT.UpdateJoystick(0, true)));
            CONTROL_BINDINGS[0] = {true, 16, {{0, ANALOGUE_MINUS, ANALOGUE_POV_Y, 0}, {-1, 0, 0, 0}}};
            CPCController player; player.mNumMappings = 1; player.Flush();
            CHECK(player.GetMappedInputValue(0, ANALOGUE_POV_Y) == -1.0f);
            CHECK(player.actions.size() == 1 && player.actions[0] == 16 && player.actionValues[0] == -1.0f);
        } else
#endif
#if defined(CE_VERSION)
        if (!strncmp(test, "pad-edge-pov-", 13) || !strncmp(test, "pad-edge-demo-", 14)) {
#if defined(CE_EDGE_TESTS)
            const bool demo = !strncmp(test, "pad-edge-demo-", 14);
            const bool blocked = strstr(test, "-record-") || strstr(test, "-tape-");
            const bool disabled = strstr(test, "-disabled");
            const bool keyboard = strstr(test, "-keyboard"), mouse = strstr(test, "-mouse");
            if (!noPad) CHECK(SUCCEEDED(LT.pJoystick[0]->Acquire()));
            if (disabled) LT.mUnknown33474[0] = false;
            if (keyboard || mouse) {
                CHECK(SDL_SetJoystickVirtualButton(pads[0], SDL_GAMEPAD_BUTTON_SOUTH, true)); SDL_UpdateJoysticks();
            }
            CHECK(SUCCEEDED(LT.UpdateJoystick(0, true)));
            CONTROL_BINDINGS[0] = {true, 16, {{0, ANALOGUE_MINUS, ANALOGUE_POV_Y, 0}, {-1, 0, 0, 0}}};
            if (keyboard || mouse) CONTROL_BINDINGS[0].mInput[0] = {0, keyboard ? KEY_ON : MOUSE_BUTTON_ON, 3, 0};
            CPCController player; player.mNumMappings = 1;
            if (demo) { player.mPlaying = true; player.mDataFile.words[0] = 1u << 16; }
            PortCeGamepadBeginCycle(); player.Flush();
            const float value = player.GetMappedInputValue(0, ANALOGUE_POV_Y);
            CHECK(value == (blocked || !disabled ? 0.0f : -1.0f));
            if (demo) {
                CHECK(player.mPlaying && player.mDataFile.cursor == 3 && player.actions.size() == 1);
                CHECK(player.actions[0] == 16 && player.actionValues[0] == value);
            } else if (blocked || !disabled) CHECK(player.actions.empty());
            else CHECK(player.actions.size() == 1 && player.actions[0] == 16 && player.actionValues[0] == -1.0f);
#else
            return 2;
#endif
        } else if (!strcmp(test, "pad-edge-record-keys") || !strcmp(test, "pad-edge-tape-keys")) {
#if defined(CE_EDGE_TESTS)
            CHECK(SUCCEEDED(LT.pJoystick[0]->Acquire()));
            CHECK(SDL_SetJoystickVirtualButton(pads[0], SDL_GAMEPAD_BUTTON_SOUTH, true)); SDL_UpdateJoysticks();
            CHECK(SUCCEEDED(LT.UpdateJoystick(0, true)));
            CONTROL_BINDINGS[0] = {true, 16, {{0, ANALOGUE_MINUS, ANALOGUE_POV_Y, 0}, {0, KEY_ON, 1, 0}}};
            CONTROL_BINDINGS[1] = {true, 17, {{0, MOUSE_BUTTON_ON, 0, 0}, {-1, 0, 0, 0}}};
            testKeyOn = true; MOUSE_HELD[0] = true;
            CPCController player; player.mNumMappings = 2;
            PortCeGamepadBeginCycle(); player.Flush();
            CHECK(player.actions.size() == 2 && player.actions[0] == 16 && player.actions[1] == 17);
#else
            return 2;
#endif
        } else if (!strcmp(test, "pad-tutorial-info") || !strcmp(test, "pad-tutorial-unplug")) {
            CeTutorialPadInfo info{};
            CHECK(PortCeTutorialPadInfo(LT.pJoystick[0], &info));
            CHECK(info.kind == CE_TUTORIAL_GAMEPAD);
            CHECK((info.axes & (CE_PAD_X1 | CE_PAD_Y1 | CE_PAD_X2 | CE_PAD_Y2)) ==
                  (CE_PAD_X1 | CE_PAD_Y1 | CE_PAD_X2 | CE_PAD_Y2));
            CHECK((info.buttons & (1u << SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) &&
                  (info.buttons & (1u << SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)));
            CHECK(!PortCeTutorialPadInfo(nullptr, &info) && !PortCeTutorialPadInfo(LT.pJoystick[0], nullptr));
            if (!strcmp(test, "pad-tutorial-unplug")) {
                CHECK(SDL_DetachVirtualJoystick(virtualIds[0]));
                SDL_UpdateJoysticks();
                CHECK(!PortCeTutorialPadInfo(LT.pJoystick[0], &info));
            }
        } else
#endif
        if (!strcmp(test, "pad-properties")) {
            // The game's Controller Options page names each pad by DIPROP_INSTANCENAME, and InitDirectInput sets the
            // axis range with DIPROP_RANGE. Both are MAKEDIPROP(n) references; a comparison an optimizing compiler can
            // fold made every property miss (every pad "Not present").
            DIPROPSTRING name{}; name.diph.dwSize = sizeof(name); name.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            name.diph.dwHow = DIPH_DEVICE;
            CHECK(SUCCEEDED(LT.pJoystick[0]->GetProperty(DIPROP_INSTANCENAME, &name.diph)));
            CHECK(name.wsz[0] == 'I' && name.wsz[1] == 'n');
            DIPROPRANGE range{}; range.diph.dwSize = sizeof(range); range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            range.diph.dwHow = DIPH_DEVICE; range.lMin = -200; range.lMax = 200;
            CHECK(SUCCEEDED(LT.pJoystick[0]->SetProperty(DIPROP_RANGE, &range.diph)));
            DIPROPRANGE back{}; back.diph.dwSize = sizeof(back); back.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            back.diph.dwHow = DIPH_DEVICE;
            CHECK(SUCCEEDED(LT.pJoystick[0]->GetProperty(DIPROP_RANGE, &back.diph)) && back.lMin == -200 && back.lMax == 200);
            CHECK(SDL_SetJoystickVirtualAxis(pads[0], SDL_GAMEPAD_AXIS_LEFTX, 32767));
            SDL_UpdateJoysticks();
            LT.pJoystick[0]->Acquire();
            CHECK(SUCCEEDED(LT.UpdateJoystick(0, false)));
            CHECK(LT.JoyState(0)->lX == 200);
        } else if (!strcmp(test, "pad-acquire") || !strcmp(test, "pad-reacquire")) {
            CHECK(SDL_SetJoystickVirtualButton(pads[0], SDL_GAMEPAD_BUTTON_SOUTH, true));
            SDL_UpdateJoysticks();
            CHECK(SUCCEEDED(LT.UpdateJoystick(0, true)));
            CHECK(SUCCEEDED(LT.UpdateJoystick(0, false)));
            // Actual UpdateJoystick rotates SDL's first four button slots for the game's legacy pad order.
            CHECK(LT.JoyState(0)->rgbButtons[3] == 0x80);
            if (!strcmp(test, "pad-reacquire")) {
                CHECK(SUCCEEDED(LT.pJoystick[0]->Unacquire()));
                CHECK(SUCCEEDED(LT.UpdateJoystick(0, false)));
                CHECK(SUCCEEDED(LT.UpdateJoystick(0, false)));
                CHECK(LT.JoyState(0)->rgbButtons[3] == 0x80);
            }
        } else if (strncmp(test, "pad-trigger-", 12) == 0) {
            bool enabled = !strcmp(test, "pad-trigger-edges") || !strcmp(test, "pad-trigger-unplug");
#if defined(CE_VERSION)
            enabled = enabled || !strcmp(test, "pad-trigger-build-mode");
#else
            CHECK(!enabled); // The focused faithful controls do not request CE-only edge cases.
#endif
            CHECK(SUCCEEDED(LT.pJoystick[0]->Acquire()));
            auto step = [&](int left, int right) {
                if (!SDL_SetJoystickVirtualAxis(pads[0], SDL_GAMEPAD_AXIS_LEFT_TRIGGER, left) ||
                    !SDL_SetJoystickVirtualAxis(pads[0], SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, right)) return false;
                SDL_UpdateJoysticks();
                return SUCCEEDED(LT.UpdateJoystick(0, true));
            };
            CHECK(step(-32768, -32768));
            CHECK(!LT.JoyButtonOn(0, 16) && !LT.JoyButtonOn(0, 17));
            DIDEVCAPS caps{}; caps.dwSize = sizeof(caps);
            CHECK(SUCCEEDED(LT.pJoystick[0]->GetCapabilities(&caps)));
            CHECK(step(20000, -32768));
            CHECK(LT.JoyButtonOn(0, 16) == enabled && LT.JoyButtonOnce(0, 16) == enabled);
            CHECK(!LT.JoyButtonOn(0, 17));
            CHECK(caps.dwButtons == (enabled ? 18u : 16u));
            CHECK(step(-5000, 20000));
            CHECK(LT.JoyButtonOn(0, 16) == enabled && !LT.JoyButtonOnce(0, 16));
            CHECK(LT.JoyButtonOn(0, 17) == enabled && LT.JoyButtonOnce(0, 17) == enabled);
            CHECK(step(-20000, -5000));
            CHECK(!LT.JoyButtonOn(0, 16) && LT.JoyButtonRelease(0, 16) == enabled);
            CHECK(LT.JoyButtonOn(0, 17) == enabled && !LT.JoyButtonOnce(0, 17));
            CHECK(step(-5000, -20000));
            CHECK(!LT.JoyButtonOn(0, 16) && !LT.JoyButtonOn(0, 17));
            CHECK(LT.JoyButtonRelease(0, 17) == enabled);
            // A held trigger must not latch after loss of acquisition.
            CHECK(step(20000, -32768));
            CHECK(SUCCEEDED(LT.pJoystick[0]->Unacquire()));
            CHECK(SUCCEEDED(LT.pJoystick[0]->Acquire()));
            CHECK(step(-5000, -32768));
            CHECK(!LT.JoyButtonOn(0, 16));
            if (!strcmp(test, "pad-trigger-unplug")) {
                CHECK(step(20000, 20000));
                CHECK(LT.JoyButtonOn(0, 16) && LT.JoyButtonOn(0, 17));
                CHECK(SDL_DetachVirtualJoystick(virtualIds[0]));
                SDL_UpdateJoysticks();
                CHECK(SUCCEEDED(LT.UpdateJoystick(0, true)));
                CHECK(!LT.JoyButtonOn(0, 16) && !LT.JoyButtonOn(0, 17));
                CHECK(LT.JoyButtonRelease(0, 16) && LT.JoyButtonRelease(0, 17));
                CHECK(SUCCEEDED(LT.UpdateJoystick(0, true)));
                CHECK(!LT.JoyButtonOnce(0, 16) && !LT.JoyButtonOnce(0, 17));
            }
        } else if (!strcmp(test, "pad-quick-tap") || !strncmp(test, "pad-edge-", 9)) {
#if defined(CE_VERSION) && defined(CE_EDGE_TESTS)
            if (!strcmp(test, "pad-edge-stale-focus")) {
                SDL_Event stale{}; stale.type = SDL_EVENT_WINDOW_FOCUS_LOST; stale.window.windowID = 17; stale.window.timestamp = 1;
                CHECK(SDL_PushEvent(&stale));
            }
            CHECK(SUCCEEDED(LT.pJoystick[0]->Acquire()));
            CHECK(SUCCEEDED(LT.UpdateJoystick(0, true))); PortCeGamepadUpdate(0);
            auto button = [&](bool down) {
                if (!SDL_SetJoystickVirtualButton(pads[0], SDL_GAMEPAD_BUTTON_SOUTH, down)) return false;
                SDL_UpdateJoysticks();
                return true;
            };
            auto update = [&] { CHECK(SUCCEEDED(LT.UpdateJoystick(0, true))); PortCeGamepadUpdate(0); return 0; };
            if (!strcmp(test, "pad-edge-bonus-two")) {
                CHECK(SUCCEEDED(LT.pJoystick[1]->Acquire())); CHECK(SUCCEEDED(LT.UpdateJoystick(1, true)));
                CONTROL_BINDINGS[0] = {true, 16, {{0, BUTTON_ONCE, 3, 0}, {1, BUTTON_ONCE, 3, 0}}};
                CONTROL_BINDINGS[1] = {true, 17, {{0, BUTTON_RELEASE, 3, 0}, {1, BUTTON_RELEASE, 3, 0}}};
                CPCController p0, p1; p0.mNumMappings = p1.mNumMappings = 2; p1.mPadNumber = 1;
                testMultiplayer = true; testBonus = true;
                PortCeGamepadBeginCycle(); p0.Flush(); p1.Flush();
                for (int pad = 0; pad < 2; ++pad) {
                    CHECK(SDL_SetJoystickVirtualButton(pads[pad], SDL_GAMEPAD_BUTTON_SOUTH, true)); SDL_UpdateJoysticks();
                    CHECK(SDL_SetJoystickVirtualButton(pads[pad], SDL_GAMEPAD_BUTTON_SOUTH, false)); SDL_UpdateJoysticks();
                }
                PortCeGamepadBeginCycle(); p0.Flush(); p1.Flush();
                CHECK(p0.actions.size() == 1 && p0.actions[0] == 16 && p1.actions.size() == 1 && p1.actions[0] == 16);
                PortCeGamepadBeginCycle(); p0.Flush(); p1.Flush();
                CHECK(p0.actions.size() == 2 && p0.actions[1] == 17 && p1.actions.size() == 2 && p1.actions[1] == 17);
                PortCeGamepadBeginCycle(); p0.Flush(); p1.Flush(); CHECK(p0.actions.size() == 2 && p1.actions.size() == 2);
            } else if (!strcmp(test, "pad-edge-cross-binding") || !strcmp(test, "pad-edge-two-consumers") ||
                !strcmp(test, "pad-edge-repeat-consumer")) {
                CHECK(SUCCEEDED(LT.pJoystick[1]->Acquire())); CHECK(SUCCEEDED(LT.UpdateJoystick(1, true)));
                CONTROL_BINDINGS[0] = {true, 16, {{-1, 0, 0, 0}, {1, BUTTON_ONCE, 3, 0}}};
                CONTROL_BINDINGS[1] = {true, 17, {{-1, 0, 0, 0}, {1, BUTTON_RELEASE, 3, 0}}};
                CPCController p0, p1; p0.mNumMappings = p1.mNumMappings = 2; p1.mPadNumber = 1;
                PortCeGamepadBeginCycle(); p0.Flush();
                if (strcmp(test, "pad-edge-cross-binding")) p1.Flush();
                CHECK(p0.actions.empty() && p1.actions.empty());
                CHECK(SDL_SetJoystickVirtualButton(pads[1], SDL_GAMEPAD_BUTTON_SOUTH, true)); SDL_UpdateJoysticks();
                CHECK(SDL_SetJoystickVirtualButton(pads[1], SDL_GAMEPAD_BUTTON_SOUTH, false)); SDL_UpdateJoysticks();
                CHECK(SUCCEEDED(LT.UpdateJoystick(0, true))); CHECK(SUCCEEDED(LT.UpdateJoystick(1, true)));
                PortCeGamepadBeginCycle(); p0.Flush();
                CHECK(p0.actions.size() == 1 && p0.actions[0] == 16 && PortCeGamepadOn(1, 3));
                if (strcmp(test, "pad-edge-cross-binding")) {
                    p1.Flush(); CHECK(p1.actions.size() == 1 && p1.actions[0] == 16 && PortCeGamepadOn(1, 3));
                }
                if (strcmp(test, "pad-edge-repeat-consumer")) PortCeGamepadBeginCycle();
                p0.Flush(); CHECK(p0.actions.size() == 2 && p0.actions[1] == 17 && !PortCeGamepadOn(1, 3));
                if (strcmp(test, "pad-edge-cross-binding")) {
                    p1.Flush(); CHECK(p1.actions.size() == 2 && p1.actions[1] == 17);
                }
                PortCeGamepadBeginCycle(); p0.Flush(); CHECK(p0.actions.size() == 2);
            } else if (!strcmp(test, "pad-edge-no-poll-held")) {
                CHECK(button(true)); PortCeGamepadUpdate(0);
                CHECK(PortCeGamepadOn(0, 3) && PortCeGamepadEdge(0, 3, false));
                PortCeGamepadUpdate(0);
                CHECK(PortCeGamepadOn(0, 3) && !PortCeGamepadEdge(0, 3, true));
                CHECK(button(false)); PortCeGamepadUpdate(0);
                CHECK(!PortCeGamepadOn(0, 3) && PortCeGamepadEdge(0, 3, true));
            } else if (!strcmp(test, "pad-edge-duplicate-binding")) {
                CHECK(button(true)); CHECK(update() == 0); CHECK(PortCeGamepadOn(0, 3));
                CHECK(SDL_SetJoystickVirtualButton(pads[0], SDL_GAMEPAD_BUTTON_EAST, true)); SDL_UpdateJoysticks();
                CHECK(update() == 0); CHECK(PortCeGamepadOn(0, 3));
                CHECK(button(false)); CHECK(SUCCEEDED(LT.UpdateJoystick(0, true))); CHECK(LT.JoyButtonOn(0, 3));
                CHECK(update() == 0); CHECK(PortCeGamepadOn(0, 3) && !PortCeGamepadEdge(0, 3, true));
                // B remains an unambiguous mapping, so its complete tap must still be retained.
                CHECK(SDL_SetJoystickVirtualButton(pads[0], SDL_GAMEPAD_BUTTON_WEST, true)); SDL_UpdateJoysticks();
                CHECK(SDL_SetJoystickVirtualButton(pads[0], SDL_GAMEPAD_BUTTON_WEST, false)); SDL_UpdateJoysticks();
                CHECK(update() == 0); CHECK(PortCeGamepadOn(0, 3) && PortCeGamepadEdge(0, 0, false));
                CHECK(update() == 0); CHECK(PortCeGamepadOn(0, 3) && PortCeGamepadEdge(0, 0, true));
                CHECK(SDL_SetJoystickVirtualButton(pads[0], SDL_GAMEPAD_BUTTON_EAST, false)); SDL_UpdateJoysticks();
                CHECK(update() == 0); CHECK(!PortCeGamepadOn(0, 3) && PortCeGamepadEdge(0, 3, true));
            } else if (!strcmp(test, "pad-edge-no-watch")) {
                SDL_RemoveEventWatch(PadWatch, nullptr); CHECK(button(true)); CHECK(update() == 0);
                CHECK(PortCeGamepadOn(0, 3) && PortCeGamepadEdge(0, 3, false));
                CHECK(button(false)); CHECK(update() == 0);
                CHECK(!PortCeGamepadOn(0, 3) && PortCeGamepadEdge(0, 3, true));
            } else if (!strcmp(test, "pad-edge-missing-focus-event")) {
                CHECK(button(true)); CHECK(update() == 0); CHECK(PortCeGamepadOn(0, 3));
                focused = false; CHECK(update() == 0); CHECK(!PortCeGamepadOn(0, 3));
                CHECK(button(false)); focused = true; CHECK(update() == 0);
                CHECK(!PortCeGamepadOn(0, 3) && !PortCeGamepadEdge(0, 3, true));
                CHECK(button(true)); CHECK(update() == 0); CHECK(PortCeGamepadEdge(0, 3, false));
            } else if (!strcmp(test, "pad-edge-cross-overflow")) {
                CHECK(SUCCEEDED(LT.pJoystick[1]->Acquire())); CHECK(SUCCEEDED(LT.UpdateJoystick(1, true))); PortCeGamepadUpdate(1);
                CHECK(SDL_SetJoystickVirtualButton(pads[1], SDL_GAMEPAD_BUTTON_SOUTH, true)); SDL_UpdateJoysticks();
                CHECK(SDL_SetJoystickVirtualButton(pads[1], SDL_GAMEPAD_BUTTON_SOUTH, false)); SDL_UpdateJoysticks();
                PortCeGamepadUpdate(1); CHECK(PortCeGamepadEdge(1, 3, false)); // release stays queued on pad 1
                for (int i = 0; i < 140; ++i) CHECK(button((i & 1) == 0));
                PortCeGamepadUpdate(1); CHECK(!PortCeGamepadEdge(1, 3, false) && !PortCeGamepadEdge(1, 3, true));
            } else if (!strcmp(test, "pad-edge-overflow-focus")) {
                for (int i = 0; i < 139; ++i) CHECK(button((i & 1) == 0));
                focused = false; SDL_Event event{}; event.type = SDL_EVENT_WINDOW_FOCUS_LOST; event.window.windowID = 17;
                CHECK(SDL_PushEvent(&event)); CHECK(update() == 0); CHECK(!PortCeGamepadOn(0, 3));
                focused = true; event.type = SDL_EVENT_WINDOW_FOCUS_GAINED; event.window.timestamp = 0; CHECK(SDL_PushEvent(&event));
                CHECK(button(false)); CHECK(update() == 0); CHECK(button(true)); CHECK(update() == 0);
                CHECK(PortCeGamepadEdge(0, 3, false) == 1);
            } else if (!strcmp(test, "pad-edge-overflow") || !strcmp(test, "pad-edge-event-overflow")) {
                for (int i = 0; i < (!strcmp(test, "pad-edge-overflow") ? 70 : 140); ++i) CHECK(button((i & 1) == 0));
                CHECK(update() == 0); CHECK(PortCeGamepadEdge(0, 3, false) == 0 && PortCeGamepadEdge(0, 3, true) == 0);
                CHECK(button(true)); CHECK(update() == 0); CHECK(PortCeGamepadEdge(0, 3, false) == 1);
            } else if (!strcmp(test, "pad-edge-stale-focus")) {
                CHECK(button(true)); CHECK(update() == 0); CHECK(PortCeGamepadEdge(0, 3, false) == 1);
            } else if (!strcmp(test, "pad-edge-focus")) {
                CHECK(button(true));
                focused = false; SDL_Event event{}; event.type = SDL_EVENT_WINDOW_FOCUS_LOST; event.window.windowID = 17;
                CHECK(SDL_PushEvent(&event)); CHECK(button(false)); CHECK(update() == 0);
                CHECK(!PortCeGamepadOn(0, 3) && !PortCeGamepadEdge(0, 3, true));
                focused = true; event.type = SDL_EVENT_WINDOW_FOCUS_GAINED; event.window.timestamp = 0; CHECK(SDL_PushEvent(&event));
                CHECK(update() == 0); CHECK(button(true)); CHECK(update() == 0);
                CHECK(PortCeGamepadEdge(0, 3, false) == 1);
            } else if (!strcmp(test, "pad-edge-chord") || !strcmp(test, "pad-edge-chord-pulses")) {
                CHECK(button(true));
                CHECK(SDL_SetJoystickVirtualButton(pads[0], SDL_GAMEPAD_BUTTON_EAST, true)); SDL_UpdateJoysticks();
                if (!strcmp(test, "pad-edge-chord-pulses")) {
                    CHECK(button(false));
                    CHECK(SDL_SetJoystickVirtualButton(pads[0], SDL_GAMEPAD_BUTTON_EAST, false)); SDL_UpdateJoysticks();
                }
                CHECK(update() == 0);
                CHECK(PortCeGamepadEdge(0, 3, false) == 1 && PortCeGamepadEdge(0, 0, false) == 1);
                CHECK(PortCeGamepadOn(0, 3) == 1 && PortCeGamepadOn(0, 0) == 1);
                if (!strcmp(test, "pad-edge-chord")) {
                    CHECK(button(false));
                    CHECK(SDL_SetJoystickVirtualButton(pads[0], SDL_GAMEPAD_BUTTON_EAST, false)); SDL_UpdateJoysticks();
                }
                CHECK(update() == 0);
                CHECK(PortCeGamepadEdge(0, 3, true) == 1 && PortCeGamepadEdge(0, 0, true) == 1);
                CHECK(PortCeGamepadOn(0, 3) == 0 && PortCeGamepadOn(0, 0) == 0);
            } else if (!strcmp(test, "pad-edge-two")) {
                CHECK(SUCCEEDED(LT.pJoystick[1]->Acquire())); CHECK(SUCCEEDED(LT.UpdateJoystick(1, true))); PortCeGamepadUpdate(1);
                CHECK(SDL_SetJoystickVirtualButton(pads[1], SDL_GAMEPAD_BUTTON_SOUTH, true)); SDL_UpdateJoysticks();
                CHECK(SDL_SetJoystickVirtualButton(pads[1], SDL_GAMEPAD_BUTTON_SOUTH, false)); SDL_UpdateJoysticks();
                CHECK(update() == 0); CHECK(!PortCeGamepadEdge(0, 3, false));
                CHECK(SUCCEEDED(LT.UpdateJoystick(1, true))); PortCeGamepadUpdate(1); CHECK(PortCeGamepadEdge(1, 3, false) == 1);
                CHECK(SUCCEEDED(LT.UpdateJoystick(1, true))); PortCeGamepadUpdate(1); CHECK(PortCeGamepadEdge(1, 3, true) == 1);
            } else if (!strcmp(test, "pad-edge-trigger")) {
                CHECK(SDL_SetJoystickVirtualAxis(pads[0], SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768)); SDL_UpdateJoysticks(); CHECK(update() == 0);
                CHECK(SDL_SetJoystickVirtualAxis(pads[0], SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767)); SDL_UpdateJoysticks();
                CHECK(SDL_SetJoystickVirtualAxis(pads[0], SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768)); SDL_UpdateJoysticks();
                CHECK(update() == 0); CHECK(PortCeGamepadEdge(0, 17, false) == 1 && PortCeGamepadOn(0, 17) == 1);
                CHECK(update() == 0); CHECK(PortCeGamepadEdge(0, 17, true) == 1 && PortCeGamepadOn(0, 17) == 0);
            } else if (!strcmp(test, "pad-edge-hotplug-two")) {
                CHECK(SUCCEEDED(LT.pJoystick[1]->Acquire())); CHECK(SUCCEEDED(LT.UpdateJoystick(1, true)));
                CONTROL_BINDINGS[0] = {true, 16, {{0, BUTTON_ONCE, 3, 0}, {1, BUTTON_ONCE, 3, 0}}};
                CONTROL_BINDINGS[1] = {true, 17, {{0, BUTTON_RELEASE, 3, 0}, {1, BUTTON_RELEASE, 3, 0}}};
                CPCController p0, p1; p0.mNumMappings = p1.mNumMappings = 2; p1.mPadNumber = 1;
                testMultiplayer = true; testBonus = true;
                auto cycle = [&] {
                    PortCeGamepadBeginCycle(); p0.Flush(); p1.Flush();
                };
                cycle(); CHECK(p0.actions.empty() && p1.actions.empty());
                // Losing player one's pad must discard its pending press without discarding player two's quick tap.
                CHECK(button(true)); CHECK(SDL_DetachVirtualJoystick(virtualIds[0])); SDL_UpdateJoysticks();
                CHECK(SDL_SetJoystickVirtualButton(pads[1], SDL_GAMEPAD_BUTTON_SOUTH, true)); SDL_UpdateJoysticks();
                CHECK(SDL_SetJoystickVirtualButton(pads[1], SDL_GAMEPAD_BUTTON_SOUTH, false)); SDL_UpdateJoysticks();
                cycle(); CHECK(p0.actions.empty() && p1.actions.size() == 1 && p1.actions[0] == 16);
                cycle(); CHECK(p0.actions.empty() && p1.actions.size() == 2 && p1.actions[1] == 17);
                cycle(); CHECK(p0.actions.empty() && p1.actions.size() == 2);
                // The replacement arrives already held: it is the new baseline, not a synthetic press.
                pads.push_back(VirtualPad()); CHECK(pads.back());
                CHECK(SDL_SetJoystickVirtualButton(pads.back(), SDL_GAMEPAD_BUTTON_SOUTH, true)); SDL_UpdateJoysticks();
                SDL_Delay(550); cycle();
                CHECK(PortCeGamepadOn(0, 3) && p0.actions.empty() && p1.actions.size() == 2);
                CHECK(SDL_SetJoystickVirtualButton(pads.back(), SDL_GAMEPAD_BUTTON_SOUTH, false)); SDL_UpdateJoysticks();
                cycle(); CHECK(p0.actions.size() == 1 && p0.actions[0] == 17 && p1.actions.size() == 2);
                CHECK(SDL_SetJoystickVirtualButton(pads.back(), SDL_GAMEPAD_BUTTON_SOUTH, true)); SDL_UpdateJoysticks();
                cycle(); CHECK(p0.actions.size() == 2 && p0.actions[1] == 16 && p1.actions.size() == 2);
                CHECK(SDL_SetJoystickVirtualButton(pads.back(), SDL_GAMEPAD_BUTTON_SOUTH, false)); SDL_UpdateJoysticks();
                cycle(); CHECK(p0.actions.size() == 3 && p0.actions[2] == 17 && p1.actions.size() == 2);
            } else if (!strcmp(test, "pad-edge-hotplug")) {
                CHECK(button(true)); CHECK(SDL_DetachVirtualJoystick(virtualIds[0])); SDL_UpdateJoysticks();
                CHECK(update() == 0); CHECK(!PortCeGamepadOn(0, 3) && !PortCeGamepadEdge(0, 3, true));
                pads.push_back(VirtualPad()); CHECK(pads.back()); SDL_Delay(550);
                CHECK(update() == 0); CHECK(!PortCeGamepadEdge(0, 3, false));
                CHECK(SDL_SetJoystickVirtualButton(pads.back(), SDL_GAMEPAD_BUTTON_SOUTH, true)); SDL_UpdateJoysticks();
                CHECK(update() == 0); CHECK(PortCeGamepadEdge(0, 3, false) == 1);
            } else if (!strcmp(test, "pad-edge-multiplayer")) {
                testMultiplayer = true; CHECK(button(true)); CHECK(update() == 0);
                CHECK(PortCeGamepadOn(0, 3) == -1 && PortCeGamepadEdge(0, 3, false) == -1);
                CHECK(button(false)); testMultiplayer = false; CHECK(update() == 0);
                CHECK(button(true)); CHECK(update() == 0); CHECK(PortCeGamepadEdge(0, 3, false) == 1);
            } else if (!strncmp(test, "pad-edge-record-saved", 21) || !strncmp(test, "pad-edge-tape-saved", 19)) {
                // A saved button binding must not dispatch a live pad action into a keyboard/mouse tape.
                CHECK(button(true)); CHECK(update() == 0);
                CHECK(PortCeGamepadOn(0, 3) == -1 && !LT.JoyButtonOn(0, 3));
                if (two) {
                    CHECK(SUCCEEDED(LT.pJoystick[1]->Acquire()));
                    CHECK(SDL_SetJoystickVirtualButton(pads[1], SDL_GAMEPAD_BUTTON_SOUTH, true)); SDL_UpdateJoysticks();
                    CHECK(SUCCEEDED(LT.UpdateJoystick(1, true)) && !LT.JoyButtonOn(1, 3));
                }
                CONTROL_BINDINGS[0] = {true, 16, {{two ? 1 : 0, BUTTON_ON, 3, 0}, {-1, 0, 0, 0}}};
                CPCController p0; p0.mNumMappings = 1;
                PortCeGamepadBeginCycle(); p0.Flush();
                CHECK(p0.actions.empty());
            } else if (!strcmp(test, "pad-edge-tape") || !strcmp(test, "pad-edge-record") ||
                       !strcmp(test, "pad-edge-clock") || !strcmp(test, "pad-edge-probes")) {
                CHECK(button(true)); CHECK(update() == 0);
                CHECK(PortCeGamepadOn(0, 3) == -1 && PortCeGamepadEdge(0, 3, false) == -1);
            } else {
                CHECK(button(true));
                if (!strcmp(test, "pad-edge-order")) CHECK(SUCCEEDED(LT.UpdateJoystick(0, true)));
                CHECK(button(false));
                if (!strcmp(test, "pad-edge-order")) CHECK(SUCCEEDED(LT.UpdateJoystick(0, true)));
                CHECK(update() == 0); CHECK(PortCeGamepadEdge(0, 3, false) == 1 && PortCeGamepadOn(0, 3) == 1);
                CHECK(update() == 0); CHECK(PortCeGamepadEdge(0, 3, true) == 1 && PortCeGamepadOn(0, 3) == 0);
                CHECK(update() == 0); CHECK(!PortCeGamepadEdge(0, 3, false) && !PortCeGamepadEdge(0, 3, true));
                CHECK(button(true)); CHECK(update() == 0); CHECK(PortCeGamepadEdge(0, 3, false) == 1);
                CHECK(update() == 0); CHECK(PortCeGamepadOn(0, 3) == 1 && !PortCeGamepadEdge(0, 3, false));
                CHECK(button(false)); CHECK(update() == 0); CHECK(PortCeGamepadEdge(0, 3, true) == 1);
            }
#else
            return 2;
#endif
        } else if (!strcmp(test, "pad-axes")) {
            CHECK(SUCCEEDED(LT.pJoystick[0]->Acquire()));
            CPCController controller;
            auto axis = [&](int which, Sint16 value) {
                assert(SDL_SetJoystickVirtualAxis(pads[0], which, value)); SDL_UpdateJoysticks();
                assert(SUCCEEDED(LT.UpdateJoystick(0, false)));
            };
            CHECK(std::abs(controller.GetJoyAnalogueRightY(0)) < .01f);
            axis(SDL_GAMEPAD_AXIS_RIGHTX, -32768); CHECK(controller.GetJoyAnalogueRightX(0) == -1.f);
            axis(SDL_GAMEPAD_AXIS_RIGHTX, 32767); CHECK(controller.GetJoyAnalogueRightX(0) == 1.f);
            axis(SDL_GAMEPAD_AXIS_RIGHTX, 0); CHECK(std::abs(controller.GetJoyAnalogueRightX(0)) < .01f);
            axis(SDL_GAMEPAD_AXIS_RIGHTY, -32768); CHECK(controller.GetJoyAnalogueRightY(0) == -1.f);
            axis(SDL_GAMEPAD_AXIS_RIGHTY, 32767); CHECK(controller.GetJoyAnalogueRightY(0) == 1.f);
            axis(SDL_GAMEPAD_AXIS_RIGHTY, 0); CHECK(std::abs(controller.GetJoyAnalogueRightY(0)) < .01f);
            axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767); axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
            CHECK(std::abs(controller.GetJoyAnalogueRightX(0)) < .01f && std::abs(controller.GetJoyAnalogueRightY(0)) < .01f);
            axis(SDL_GAMEPAD_AXIS_LEFTX, -32768); CHECK(controller.GetJoyAnalogueLeftX(0) == -1.f);
            axis(SDL_GAMEPAD_AXIS_LEFTY, 32767); CHECK(controller.GetJoyAnalogueLeftY(0) == 1.f);
        }
        for (int i = 0; i < LT.mJoypads; ++i) LT.pJoystick[i]->Release();
        LT.pDI->Release();
        for (auto pad : pads) SDL_CloseJoystick(pad);
        for (auto id : virtualIds) SDL_DetachVirtualJoystick(id);
    } else if (!strcmp(test, "wheel-menu")) {
        Device *mouse = Make<Device>(MOUSE, 0);
        mouse->SetCooperativeLevel(MainWindow(), DISCL_EXCLUSIVE | DISCL_FOREGROUND);
        CHECK(SUCCEEDED(mouse->Acquire())); sMouse = mouse;
        Wheel(1); MouseEvent(SDL_EVENT_MOUSE_BUTTON_DOWN); MouseEvent(SDL_EVENT_MOUSE_BUTTON_UP);
        CHECK(SUCCEEDED(ShutdownMouse()));
        Drain(); CHECK(MOUSE_WHEEL == 0 && windowMouse == 0);
        Wheel(2); Drain(); CHECK(MOUSE_WHEEL == 240 && windowMouse == 1);
    } else if (!strncmp(test, "pause-", 6)) {
        Device *mouse = Make<Device>(MOUSE, 0);
        mouse->SetCooperativeLevel(MainWindow(), DISCL_EXCLUSIVE | DISCL_FOREGROUND);
        CHECK(SUCCEEDED(mouse->Acquire())); sMouse = mouse;
        tapeCursor = !strcmp(test, "pause-tape");
        focused = strcmp(test, "pause-no-focus") != 0;
        MOUSE_X = 320; MOUSE_Y = 240;
        CHECK(SUCCEEDED(ShutdownMouse()));
        if (tapeCursor) CHECK(MOUSE_X == 77 && MOUSE_Y == 88);
        else if (!focused) CHECK(MOUSE_X == 320 && MOUSE_Y == 240);
        else CHECK(MOUSE_X == 380 && MOUSE_Y == 300);
    } else if (!strcmp(test, "release-overflow") || !strcmp(test, "release-repress") || !strcmp(test, "record-release-overflow")) {
        recording = !strcmp(test, "record-release-overflow");
        for (int i = 0; i < 20; ++i) {
            SDL_KeyboardEvent key{}; key.type = SDL_EVENT_KEY_DOWN; key.scancode = static_cast<SDL_Scancode>(SDL_SCANCODE_A + i);
            key.key = static_cast<SDL_Keycode>('a' + i); key.down = true;
            gFakeHeld[key.scancode] = true; KeyEvent(key, MainWindow());
        }
        CHECK(gHeld == 20); Drain();
        while (Free() > 17) Post(MainWindow(), WM_USER, 0, 0);
        SDL_Event event{}; event.type = SDL_EVENT_WINDOW_FOCUS_LOST; event.window.windowID = 17;
        CHECK(SDL_PushEvent(&event));
        bool repress = !strcmp(test, "release-repress");
        if (repress) {
            event.type = SDL_EVENT_WINDOW_FOCUS_GAINED; CHECK(SDL_PushEvent(&event));
            event = SDL_Event{}; event.type = SDL_EVENT_KEY_DOWN;
            event.key.scancode = SDL_SCANCODE_W; event.key.key = SDLK_W; event.key.down = true;
            gFakeHeld[SDL_SCANCODE_W] = true; CHECK(SDL_PushEvent(&event));
        }
        Drain(); CHECK(gHeld == (repress ? 1 : 0));
        CHECK(recording ? recordedKeys.size() == 40 && directUp == 0 : directUp == 20);
        CHECK(focusNotifications == (recording ? 0u : repress ? 4u : 2u));
        for (int i = 0; i < 20; ++i) {
            SDL_KeyboardEvent key{}; key.type = SDL_EVENT_KEY_UP; key.scancode = static_cast<SDL_Scancode>(SDL_SCANCODE_A + i);
            KeyEvent(key, MainWindow());
        }
        Drain(); CHECK(recording ? recordedKeys.size() == 40 : directUp == 20);
        if (repress) {
            SDL_KeyboardEvent key{}; key.type = SDL_EVENT_KEY_UP; key.scancode = SDL_SCANCODE_W; key.key = SDLK_W;
            gFakeHeld[SDL_SCANCODE_W] = false; KeyEvent(key, MainWindow()); Drain();
            CHECK(gHeld == 0 && directUp == 21);
        }
    } else if (!strcmp(test, "replay-key")) {
        PortPostMessageA(MainWindow(), WM_KEYDOWN, 'W', KeyLParam(0x11, false, false)); Drain();
        CHECK(directDown == 1 && gHeld == 0);
        ReleaseAll("test focus loss"); Drain(); CHECK(directUp == 0);
    } else {
        Device mouse(MOUSE, 0); mouse.SetCooperativeLevel(MainWindow(), DISCL_EXCLUSIVE | DISCL_FOREGROUND);
        if (!strcmp(test, "mouse-fraction-no-focus")) focused = false;
        if (!strcmp(test, "wheel-stale")) { Wheel(2); Drain(); MOUSE_WHEEL = 0; }
        injectFocusLoss = !strcmp(test, "wheel-race");
        CHECK(SUCCEEDED(mouse.Acquire())); sMouse = &mouse;
        if (!strcmp(test, "mouse-exclusive") || !strcmp(test, "record-mouse")) {
            recording = !strcmp(test, "record-mouse");
            MouseEvent(SDL_EVENT_MOUSE_MOTION); MouseEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
            MouseEvent(SDL_EVENT_MOUSE_BUTTON_UP); Wheel(1); Drain();
            CHECK(windowMouse == 0);
            if (recording) CHECK(recordedMotion == 1 && recordedButtons == 2 && recordedWheel == 1 && recordedWindowMouse == 4);
        } else if (!strcmp(test, "wheel-race")) {
            CHECK(!focused); Wheel(1); Drain();
            DIMOUSESTATE2 state{}; CHECK(SUCCEEDED(mouse.GetDeviceState(sizeof(state), &state))); CHECK(state.lZ == 0);
        } else if (!strcmp(test, "wheel-before-acquire")) {
            // A native producer can enqueue a hardware event after acquisition whose timestamp predates it.
            SDL_Event event{}; event.type = SDL_EVENT_MOUSE_WHEEL; event.wheel.windowID = 17;
            event.wheel.timestamp = 1; event.wheel.y = 1; CHECK(SDL_PushEvent(&event)); Drain();
            DIMOUSESTATE2 state{}; CHECK(SUCCEEDED(mouse.GetDeviceState(sizeof(state), &state))); CHECK(state.lZ == 0);
        } else if (!strcmp(test, "wheel-focus")) {
            focused = false;
            SDL_Event event{}; event.type = SDL_EVENT_WINDOW_FOCUS_LOST; event.window.windowID = 17;
            CHECK(SDL_PushEvent(&event)); Wheel(2); Drain();
            focused = true; event.type = SDL_EVENT_WINDOW_FOCUS_GAINED; CHECK(SDL_PushEvent(&event)); Drain();
            DIMOUSESTATE2 state{}; CHECK(SUCCEEDED(mouse.GetDeviceState(sizeof(state), &state))); CHECK(state.lZ == 0);
            Wheel(1); Drain(); CHECK(SUCCEEDED(mouse.GetDeviceState(sizeof(state), &state))); CHECK(state.lZ == 120);
        } else if (!strcmp(test, "mouse-fraction-mixed")) {
            MOUSE_X = MOUSE_Y = 0;
            relativeX = .25f; relativeY = -.25f; CHECK(SUCCEEDED(PollMouseState())); CHECK(MOUSE_X == 0 && MOUSE_Y == 0);
            relativeX = .25f; relativeY = -.25f; CHECK(SUCCEEDED(PollMouseMotion())); CHECK(MOUSE_X == 0 && MOUSE_Y == 0);
            relativeX = .25f; relativeY = -.25f; CHECK(SUCCEEDED(PollMouseState())); CHECK(MOUSE_X == 0 && MOUSE_Y == 0);
            relativeX = .25f; relativeY = -.25f; CHECK(SUCCEEDED(PollMouseMotion())); CHECK(MOUSE_X == 1 && MOUSE_Y == -1);
            CHECK(SUCCEEDED(PollMouseState())); CHECK(MOUSE_X == 1 && MOUSE_Y == -1); // no duplicate delivery
        } else if (!strcmp(test, "mouse-fraction-invalid")) {
            MOUSE_X = MOUSE_Y = 0;
            relativeX = .75f; relativeY = -.75f; CHECK(SUCCEEDED(PollMouseState()));
            relativeX = std::numeric_limits<float>::quiet_NaN(); relativeY = std::numeric_limits<float>::infinity();
            CHECK(SUCCEEDED(PollMouseMotion())); CHECK(MOUSE_X == 0 && MOUSE_Y == 0);
            for (int i = 0; i < 4; ++i) { relativeX = .25f; relativeY = -.25f; CHECK(SUCCEEDED(PollMouseState())); }
            CHECK(MOUSE_X == 1 && MOUSE_Y == -1);
            MOUSE_X = MOUSE_Y = 0;
            relativeX = (std::numeric_limits<float>::max)(); relativeY = -(std::numeric_limits<float>::max)();
            CHECK(SUCCEEDED(PollMouseState())); CHECK(MOUSE_X == 0 && MOUSE_Y == 0);
            relativeX = 1.f; relativeY = -1.f; CHECK(SUCCEEDED(PollMouseState())); CHECK(MOUSE_X == 1 && MOUSE_Y == -1);
        } else if (!strcmp(test, "mouse-fraction-pending-focus")) {
            MOUSE_X = MOUSE_Y = 0;
            relativeX = 1.5f; relativeY = -1.5f; // SDL still holds this from the old focus epoch.
            focused = false; SDL_Event event{}; event.type = SDL_EVENT_WINDOW_FOCUS_LOST; event.window.windowID = 17;
            CHECK(SDL_PushEvent(&event)); Drain();
            focused = true; event.type = SDL_EVENT_WINDOW_FOCUS_GAINED; event.window.timestamp = 0; CHECK(SDL_PushEvent(&event)); Drain();
            CHECK(SUCCEEDED(PollMouseMotion())); CHECK(MOUSE_X == 0 && MOUSE_Y == 0);
            relativeX = .5f; relativeY = -.5f; CHECK(SUCCEEDED(PollMouseState())); CHECK(MOUSE_X == 0 && MOUSE_Y == 0);
            relativeX = .5f; relativeY = -.5f; CHECK(SUCCEEDED(PollMouseState())); CHECK(MOUSE_X == 1 && MOUSE_Y == -1);
        } else if (!strcmp(test, "mouse-fraction-no-focus")) {
            MOUSE_X = MOUSE_Y = 0;
            relativeX = 2.f; relativeY = -2.f; CHECK(SUCCEEDED(PollMouseState()));
            CHECK(MOUSE_X == 0 && MOUSE_Y == 0);
            relativeX = 2.f; relativeY = -2.f; CHECK(SUCCEEDED(PollMouseState()));
            CHECK(MOUSE_X == 0 && MOUSE_Y == 0);
        } else if (!strncmp(test, "mouse-fraction", 14)) {
            bool fenced = strcmp(test, "mouse-fraction") && strcmp(test, "mouse-fraction-reset") && strcmp(test, "mouse-fraction-focus");
            MOUSE_X = MOUSE_Y = 0;
            for (int i = 0; i < 4; ++i) {
                relativeX = .25f; relativeY = -.25f; CHECK(SUCCEEDED(PollMouseState()));
            }
            CHECK(MOUSE_X == (fenced ? 0 : 1) && MOUSE_Y == (fenced ? 0 : -1));
            MOUSE_X = MOUSE_Y = 0;
            if (!fenced) {
                relativeX = .75f; relativeY = -.75f; CHECK(SUCCEEDED(PollMouseState()));
                if (!strcmp(test, "mouse-fraction-reset")) {
                    CHECK(SUCCEEDED(mouse.Unacquire())); CHECK(SUCCEEDED(mouse.Acquire()));
                } else if (!strcmp(test, "mouse-fraction-focus")) {
                    focused = false; SDL_Event event{}; event.type = SDL_EVENT_WINDOW_FOCUS_LOST; event.window.windowID = 17;
                    CHECK(SDL_PushEvent(&event)); Drain();
                    focused = true; event.type = SDL_EVENT_WINDOW_FOCUS_GAINED; event.window.timestamp = 0; CHECK(SDL_PushEvent(&event)); Drain();
                    CHECK(SUCCEEDED(PollMouseState())); // discard the ambiguous first focus-epoch poll
                } else {
                    relativeX = -.75f; relativeY = .75f; CHECK(SUCCEEDED(PollMouseState()));
                }
                relativeX = .5f; relativeY = -.5f; CHECK(SUCCEEDED(PollMouseState())); CHECK(MOUSE_X == 0 && MOUSE_Y == 0);
                relativeX = .5f; relativeY = -.5f; CHECK(SUCCEEDED(PollMouseState())); CHECK(MOUSE_X == 1 && MOUSE_Y == -1);
            }
        } else if (!strcmp(test, "relative-focus")) {
            relativeMode = false; focused = false; PortMouseKeepRelative(); CHECK(!relativeMode);
            focused = true; PortMouseKeepRelative(); CHECK(relativeMode);
            CHECK(SUCCEEDED(mouse.Unacquire())); CHECK(!relativeMode && !PortDinputMouseAcquired());
            relativeX = 100; relativeY = 200;
            CHECK(SUCCEEDED(mouse.Acquire())); CHECK(relativeMode);
            DIMOUSESTATE2 state{}; CHECK(SUCCEEDED(mouse.GetDeviceState(sizeof(state), &state)));
            CHECK(state.lX == 0 && state.lY == 0 && state.lZ == 0);
        } else if (!strcmp(test, "wheel-focus-pending")) {
            Wheel(1);
            SDL_Event event{}; event.type = SDL_EVENT_WINDOW_FOCUS_LOST; event.window.windowID = 17;
            CHECK(SDL_PushEvent(&event)); Drain();
            DIMOUSESTATE2 state{}; CHECK(SUCCEEDED(mouse.GetDeviceState(sizeof(state), &state))); CHECK(state.lZ == 0);
        } else if (!strcmp(test, "wheel-window")) {
            Wheel(2, 99); Drain();
            DIMOUSESTATE2 state{}; CHECK(SUCCEEDED(mouse.GetDeviceState(sizeof(state), &state))); CHECK(state.lZ == 0);
        } else if (!strcmp(test, "wheel-stale")) {
            DIMOUSESTATE2 state{}; CHECK(SUCCEEDED(mouse.GetDeviceState(sizeof(state), &state))); CHECK(state.lZ == 0);
        } else if (!strcmp(test, "wheel-once")) {
            Wheel(1); Drain(); CHECK(SUCCEEDED(PollMouseState())); CHECK(MOUSE_WHEEL == 120);
            CHECK(SUCCEEDED(PollMouseState())); CHECK(MOUSE_WHEEL == 120);
        } else if (!strcmp(test, "replay-mouse")) {
            replay = true; relativeX = 100; relativeY = 200; Wheel(9); Drain();
            CHECK(SUCCEEDED(PollMouseState())); CHECK(MOUSE_X == 3 && MOUSE_Y == -4 && MOUSE_WHEEL == 120);
        } else return 2;
        sMouse = nullptr; mouse.Unacquire();
    }
    gNumWindows = 0; SDL_Quit();
    puts("checked actual input shim/caller path");
    return 0;
}
