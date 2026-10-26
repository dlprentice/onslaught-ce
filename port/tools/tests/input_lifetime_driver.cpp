// SPDX-License-Identifier: GPL-3.0-only
// Production shims and complete source callers, with bounded SDL mouse/window boundaries and real virtual gamepads.
#define INITGUID
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>
#include <windows.h>
#include <dinput.h>
#include <SDL3/SDL.h>

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
bool PortCeLayer() { return true; }
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
struct Console { void Print(const char *, ...) {} void AddMessage(const char *, ...) {} } CONSOLE, SETUP_HISTORY;
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
    static BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE *, VOID *);
    static BOOL CALLBACK EnumAxesCallback(const DIDEVICEOBJECTINSTANCE *, VOID *);
    HRESULT InitDirectInput(HWND);
    HRESULT UpdateJoystick(int, bool);
} LT;
struct CPCController {
    float GetJoyAnalogueLeftX(int), GetJoyAnalogueLeftY(int), GetJoyAnalogueRightX(int), GetJoyAnalogueRightY(int);
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
struct { int GetWindowWidth() { return 640; } int GetWindowHeight() { return 480; } } PLATFORM;
static void SetMouseInputActive(BOOL value) { sMouseInputActive = value; }
static void ResetMouseTransientState() { MOUSE_WHEEL = 0; }
static int PortUiMouse(int *, int *) { return 0; }
#define GetCursorPos PortGetCursorPos
#include "CALLERS_INC"

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
    if (!strncmp(test, "pad-", 4)) {
        std::vector<SDL_Joystick *> pads;
        for (int i = 0; i < (!strcmp(test, "pad-limit") ? 5 : 1); ++i) { pads.push_back(VirtualPad()); CHECK(pads.back()); }
        CHECK(SUCCEEDED(LT.InitDirectInput(reinterpret_cast<HWND>(1))));
        CHECK(LT.canary == 0x12345678 && LT.mJoypads == (!strcmp(test, "pad-limit") ? 4 : 1));
        if (!strcmp(test, "pad-acquire") || !strcmp(test, "pad-reacquire")) {
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
