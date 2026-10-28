// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/dinput.cpp: the DirectInput 8 objects the game uses (ltshell.cpp, Controller.cpp,
// PCController.cpp), over SDL3, on both targets: the system keyboard and mouse, and game controllers as DIJOYSTATE2.
// Objects live in malloc'd memory (the game's operator new serves C++ allocations; port/include/port.h).
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <SDL3/SDL.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

bool PortCeLayer();

// ---- data formats (the SDK's c_dfDI* tables; only their identity and sizes matter to this implementation) ---------------
extern "C" {
const DIDATAFORMAT c_dfDIKeyboard = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, 256, 0, NULL };
const DIDATAFORMAT c_dfDIMouse = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, sizeof(DIMOUSESTATE), 0, NULL };
const DIDATAFORMAT c_dfDIMouse2 = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, sizeof(DIMOUSESTATE2), 0, NULL };
const DIDATAFORMAT c_dfDIJoystick = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_ABSAXIS, sizeof(DIJOYSTATE), 0, NULL };
const DIDATAFORMAT c_dfDIJoystick2 = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_ABSAXIS, sizeof(DIJOYSTATE2), 0, NULL };
}

namespace
{
HWND gMouseWindow;	// the window of the game's acquired mouse (relative mode is re-asserted for it each frame)
Uint64 gMouseReleasedAt;	// SDL event timestamp cutoff: queued captured events must not become menu input on release

// A physical trigger can be bound through the game's existing joystick-button
// controls. Tapes do not record controller input, so even a forced CE layer must
// leave the faithful, replay, recording and probe input paths unchanged.
bool BindableGamepadTriggers()
{
#if defined(CE_VERSION)
	const char *const offline[] = { "BEA_PORT_TAPE", "BEA_PORT_RECORD", "BEA_PORT_CLOCK", "BEA_PORT_PROBES" };
	for (const char *name : offline)
	{
		const char *value = getenv(name);
		if (value && *value)
			return false;
	}
	return PortCeLayer();
#else
	return false;
#endif
}

// BEA_PORT_FAKE_PAD (a test switch, as window.cpp's BEA_PORT_FAKE_KEYS): an SDL virtual gamepad is attached when
// DirectInput starts, so the game enumerates it as an ordinary pad, and its script changes it once a Present count is
// reached: PRESENT:bBUTTON:d|u presses or releases an SDL gamepad button, PRESENT:aAXIS:VALUE moves an SDL gamepad
// axis (-32768..32767), PRESENT:hHAT:VALUE sets a hat (SDL_HAT_* bits). Entries are comma-separated and in Present
// order. BEA_PORT_FAKE_PAD_KIND=stick attaches a flight stick (4 axes, 8 buttons, 1 hat, not a gamepad) instead.
// For hotplug, PRESENT:detach unplugs the device and PRESENT:attach plugs it in again; BEA_PORT_FAKE_PAD_LATE=1 leaves it
// unplugged until the first attach. BEA_PORT_FAKE_PAD=1 attaches the gamepad with no script. The gamepad logs every
// rumble it is asked for (FakePadRumble).
extern "C" unsigned PortPresentCount(void);	// d3d9hooks.cpp
SDL_Joystick *gFakePad;
SDL_JoystickID gFakePadId;
const char *gFakePadScript;

// A DIPROP_* "GUID" is MAKEDIPROP(n), a reference made from the integer n. Clang assumes a reference names a real
// object, so it folds &prop == &DIPROP_RANGE, and even the address converted to an integer and compared with 4, to
// false: no property ever matched (the controls page showed every pad as not present). The volatile read hides the
// value from that reasoning.
inline uintptr_t PropId(REFGUID prop)
{
	volatile uintptr_t id = (uintptr_t)&prop;
	return id;
}

// The test gamepad reports the rumble it is asked for (low and high motor strengths, and when, on stderr), so a run
// shows the CE rumble (patch 0105) firing without a physical controller.
bool SDLCALL FakePadRumble(void *, Uint16 low, Uint16 high)
{
	fprintf(stderr, PORT_LOG_PREFIX "fake pad rumble low=%u high=%u at %llu ms\n", (unsigned)low, (unsigned)high,
			(unsigned long long)SDL_GetTicks());
	return true;
}

void FakePadPlug()
{
	SDL_VirtualJoystickDesc descriptor;
	SDL_INIT_INTERFACE(&descriptor);
	const char *kind = getenv("BEA_PORT_FAKE_PAD_KIND");
	if (kind && !strcmp(kind, "stick"))
	{
		descriptor.type = SDL_JOYSTICK_TYPE_FLIGHT_STICK;
		descriptor.naxes = 4;
		descriptor.nbuttons = 8;
		descriptor.nhats = 1;
		descriptor.name = "CE test flight stick";
	}
	else
	{
		descriptor.type = SDL_JOYSTICK_TYPE_GAMEPAD;
		descriptor.naxes = SDL_GAMEPAD_AXIS_COUNT;
		descriptor.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
		descriptor.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
		descriptor.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
		descriptor.name = "CE test gamepad";
		descriptor.Rumble = FakePadRumble;
	}
	gFakePadId = SDL_AttachVirtualJoystick(&descriptor);
	gFakePad = gFakePadId ? SDL_OpenJoystick(gFakePadId) : NULL;
	fprintf(stderr, PORT_LOG_PREFIX "test: virtual %s %s\n", kind && !strcmp(kind, "stick") ? "flight stick" : "gamepad",
			gFakePad ? "attached" : SDL_GetError());
}

void FakePadAttach()
{
	const char *script = getenv("BEA_PORT_FAKE_PAD");
	if (!script || !*script || gFakePad || gFakePadScript)
		return;
	gFakePadScript = strcmp(script, "1") ? script : "";	// "1": a pad that sends nothing (rumble runs)
	const char *late = getenv("BEA_PORT_FAKE_PAD_LATE");
	if (!(late && *late == '1'))
		FakePadPlug();
}

void FakePadStep()
{
	while (gFakePadScript && *gFakePadScript)
	{
		unsigned present = 0;
		int which = 0;
		char change = 0;
		float value = 0;
		char word[8] = "";
		if (sscanf(gFakePadScript, "%u:%6[a-z]", &present, word) == 2 && (!strcmp(word, "attach") || !strcmp(word, "detach")))
		{
			if (PortPresentCount() < present)
				return;
			if (!strcmp(word, "attach") && !gFakePad)
				FakePadPlug();
			else if (!strcmp(word, "detach") && gFakePad)
			{
				SDL_CloseJoystick(gFakePad);
				SDL_DetachVirtualJoystick(gFakePadId);
				gFakePad = NULL;
				fprintf(stderr, PORT_LOG_PREFIX "test: virtual device detached\n");
			}
		}
		else if (!gFakePad)
			return;
		else if (sscanf(gFakePadScript, "%u:b%d:%c", &present, &which, &change) == 3)
		{
			if (PortPresentCount() < present)
				return;
			SDL_SetJoystickVirtualButton(gFakePad, which, change == 'd');
		}
		else if (sscanf(gFakePadScript, "%u:a%d:%f", &present, &which, &value) == 3)
		{
			if (PortPresentCount() < present)
				return;
			SDL_SetJoystickVirtualAxis(gFakePad, which, (Sint16)value);
		}
		else if (sscanf(gFakePadScript, "%u:h%d:%f", &present, &which, &value) == 3)
		{
			if (PortPresentCount() < present)
				return;
			SDL_SetJoystickVirtualHat(gFakePad, which, (Uint8)value);
		}
		else
		{
			fprintf(stderr, PORT_LOG_PREFIX "test: BEA_PORT_FAKE_PAD entry not understood: %.40s\n", gFakePadScript);
			gFakePadScript = NULL;
			return;
		}
		SDL_UpdateJoysticks();
		const char *next = strchr(gFakePadScript, ',');
		gFakePadScript = next ? next + 1 : "";
	}
}

template <class T, class... A> T *Make(A... a) { return new (malloc(sizeof(T))) T(a...); }
template <class T> void Destroy(T *p)
{
	p->~T();
	free(p);
}

// SDL scancode -> DirectInput key code (set-1 scan codes; extended keys carry 0x80).
unsigned char DikFor(SDL_Scancode s)
{
	if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z)
	{
		static const unsigned char letters[26] = { 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
												   0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C };
		return letters[s - SDL_SCANCODE_A];
	}
	if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9)
		return (unsigned char)(DIK_1 + (s - SDL_SCANCODE_1));
	if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10)
		return (unsigned char)(DIK_F1 + (s - SDL_SCANCODE_F1));
	switch (s)
	{
	case SDL_SCANCODE_0: return DIK_0;
	case SDL_SCANCODE_RETURN: return DIK_RETURN;
	case SDL_SCANCODE_ESCAPE: return DIK_ESCAPE;
	case SDL_SCANCODE_BACKSPACE: return DIK_BACK;
	case SDL_SCANCODE_TAB: return DIK_TAB;
	case SDL_SCANCODE_SPACE: return DIK_SPACE;
	case SDL_SCANCODE_MINUS: return DIK_MINUS;
	case SDL_SCANCODE_EQUALS: return DIK_EQUALS;
	case SDL_SCANCODE_LEFTBRACKET: return DIK_LBRACKET;
	case SDL_SCANCODE_RIGHTBRACKET: return DIK_RBRACKET;
	case SDL_SCANCODE_BACKSLASH: return DIK_BACKSLASH;
	case SDL_SCANCODE_SEMICOLON: return DIK_SEMICOLON;
	case SDL_SCANCODE_APOSTROPHE: return DIK_APOSTROPHE;
	case SDL_SCANCODE_GRAVE: return DIK_GRAVE;
	case SDL_SCANCODE_COMMA: return DIK_COMMA;
	case SDL_SCANCODE_PERIOD: return DIK_PERIOD;
	case SDL_SCANCODE_SLASH: return DIK_SLASH;
	case SDL_SCANCODE_CAPSLOCK: return DIK_CAPITAL;
	case SDL_SCANCODE_F11: return DIK_F11;
	case SDL_SCANCODE_F12: return DIK_F12;
	case SDL_SCANCODE_SCROLLLOCK: return DIK_SCROLL;
	case SDL_SCANCODE_PAUSE: return DIK_PAUSE;
	case SDL_SCANCODE_INSERT: return DIK_INSERT;
	case SDL_SCANCODE_HOME: return DIK_HOME;
	case SDL_SCANCODE_PAGEUP: return DIK_PRIOR;
	case SDL_SCANCODE_DELETE: return DIK_DELETE;
	case SDL_SCANCODE_END: return DIK_END;
	case SDL_SCANCODE_PAGEDOWN: return DIK_NEXT;
	case SDL_SCANCODE_RIGHT: return DIK_RIGHT;
	case SDL_SCANCODE_LEFT: return DIK_LEFT;
	case SDL_SCANCODE_DOWN: return DIK_DOWN;
	case SDL_SCANCODE_UP: return DIK_UP;
	case SDL_SCANCODE_NUMLOCKCLEAR: return DIK_NUMLOCK;
	case SDL_SCANCODE_KP_DIVIDE: return DIK_DIVIDE;
	case SDL_SCANCODE_KP_MULTIPLY: return DIK_MULTIPLY;
	case SDL_SCANCODE_KP_MINUS: return DIK_SUBTRACT;
	case SDL_SCANCODE_KP_PLUS: return DIK_ADD;
	case SDL_SCANCODE_KP_ENTER: return DIK_NUMPADENTER;
	case SDL_SCANCODE_KP_1: return DIK_NUMPAD1;
	case SDL_SCANCODE_KP_2: return DIK_NUMPAD2;
	case SDL_SCANCODE_KP_3: return DIK_NUMPAD3;
	case SDL_SCANCODE_KP_4: return DIK_NUMPAD4;
	case SDL_SCANCODE_KP_5: return DIK_NUMPAD5;
	case SDL_SCANCODE_KP_6: return DIK_NUMPAD6;
	case SDL_SCANCODE_KP_7: return DIK_NUMPAD7;
	case SDL_SCANCODE_KP_8: return DIK_NUMPAD8;
	case SDL_SCANCODE_KP_9: return DIK_NUMPAD9;
	case SDL_SCANCODE_KP_0: return DIK_NUMPAD0;
	case SDL_SCANCODE_KP_PERIOD: return DIK_DECIMAL;
	case SDL_SCANCODE_LCTRL: return DIK_LCONTROL;
	case SDL_SCANCODE_LSHIFT: return DIK_LSHIFT;
	case SDL_SCANCODE_LALT: return DIK_LMENU;
	case SDL_SCANCODE_RCTRL: return DIK_RCONTROL;
	case SDL_SCANCODE_RSHIFT: return DIK_RSHIFT;
	case SDL_SCANCODE_RALT: return DIK_RMENU;
	case SDL_SCANCODE_LGUI: return DIK_LWIN;
	case SDL_SCANCODE_RGUI: return DIK_RWIN;
	case SDL_SCANCODE_APPLICATION: return DIK_APPS;
	default: return 0;
	}
}

// Mouse wheel motion arrives only as events; an event watch accumulates it for the next GetDeviceState. Watches can
// run off the main thread: keep their tiny state protected, and query SDL windows only on acquire (the main thread).
SDL_SpinLock gWheelLock;
float gWheel;
SDL_WindowID gWheelWindow;
Uint64 gWheelAcquiredAt;
bool gWheelFocused;
unsigned gWheelFocusVersion;

void ResetWheel(SDL_Window *w)
{
	Uint64 acquiredAt = w ? SDL_GetTicksNS() : 0;
	SDL_WindowID id = w ? SDL_GetWindowID(w) : 0;
	SDL_LockSpinlock(&gWheelLock);
	gWheel = 0;
	gWheelWindow = id;
	gWheelAcquiredAt = acquiredAt;
	gWheelFocused = false;
	unsigned version = gWheelFocusVersion;
	SDL_UnlockSpinlock(&gWheelLock);
	if (w)
	{
		bool focused = (SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS) != 0;
		SDL_LockSpinlock(&gWheelLock);
		// A watch can report a newer focus event while the main thread queries SDL. Never overwrite that event.
		if (gWheelWindow == id && gWheelFocusVersion == version)
			gWheelFocused = focused;
		SDL_UnlockSpinlock(&gWheelLock);
	}
}

bool SDLCALL WheelWatch(void *, SDL_Event *e)
{
	SDL_LockSpinlock(&gWheelLock);
	if (gWheelWindow)
	{
		if ((e->type == SDL_EVENT_WINDOW_FOCUS_LOST || e->type == SDL_EVENT_WINDOW_FOCUS_GAINED) &&
			(!e->window.windowID || e->window.windowID == gWheelWindow))
		{
			gWheelFocusVersion++;
			gWheelFocused = e->type == SDL_EVENT_WINDOW_FOCUS_GAINED;
			if (!gWheelFocused) gWheel = 0;
		}
		if (e->type == SDL_EVENT_MOUSE_WHEEL && gWheelFocused && e->wheel.timestamp >= gWheelAcquiredAt &&
			(!e->wheel.windowID || e->wheel.windowID == gWheelWindow))
			gWheel += e->wheel.y;
	}
	SDL_UnlockSpinlock(&gWheelLock);
	return true;
}

enum Kind { KEYBOARD, MOUSE, JOYSTICK };
struct Device;
Device *gPadSlots[8];	// the game's pad devices, for hotplug (Rebind, below)
void Rebind(Device *d);

struct Device : public IDirectInputDevice8A
{
	LONG refs = 1;
	Kind kind;
	SDL_JoystickID joystickId = 0;
	SDL_Gamepad *pad = NULL;
	SDL_Joystick *joy = NULL;	// a joystick SDL does not know as a gamepad (a flight stick): ordinary CE play only
	HWND window = NULL;
	DWORD cooperative = 0;
	bool acquired = false;
	bool triggerDown[2] = { false, false };
	LONG rangeMin = -1000, rangeMax = 1000;

	Device(Kind k, SDL_JoystickID id) : kind(k), joystickId(id) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID *out) override
	{
		*out = this;
		AddRef();
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG r = --refs;
		if (!r)
		{
			Unacquire();
			for (Device *&slot : gPadSlots)
				if (slot == this)
					slot = NULL;
			if (pad)
				SDL_CloseGamepad(pad);
			if (joy)
				SDL_CloseJoystick(joy);
			Destroy(this);
		}
		return r;
	}
	HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS caps) override
	{
		DWORD size = caps->dwSize;
		memset(caps, 0, size);
		caps->dwSize = size;
		caps->dwFlags = DIDC_ATTACHED;
		caps->dwDevType = kind == JOYSTICK ? (joy ? DI8DEVTYPE_JOYSTICK : DI8DEVTYPE_GAMEPAD) : 0;
		caps->dwAxes = kind == JOYSTICK ? 6 : kind == MOUSE ? 3 : 0;
		caps->dwButtons = joy ? (DWORD)SDL_min(SDL_GetNumJoystickButtons(joy), 32) :
			kind == JOYSTICK ? (BindableGamepadTriggers() ? 18 : 16) : kind == MOUSE ? 8 : 0;
		caps->dwPOVs = kind == JOYSTICK ? 1 : 0;
		return DI_OK;
	}
	HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ctx, DWORD flags) override
	{
		if (kind != JOYSTICK || !(flags & DIDFT_AXIS))
			return DI_OK;
		// The game's PCController reads the right stick from Z and Rz, not Rx/Ry. The latter carry the triggers.
		static const GUID *const types[6] = { &GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RxAxis, &GUID_RyAxis, &GUID_RzAxis };
		static const char *const names[6] = { "X Axis", "Y Axis", "Z Axis", "X Rotation", "Y Rotation", "Z Rotation" };
		for (int i = 0; i < 6; i++)
		{
			DIDEVICEOBJECTINSTANCEA o;
			memset(&o, 0, sizeof(o));
			o.dwSize = sizeof(o);
			o.guidType = *types[i];
			o.dwOfs = DIJOFS_X + 4 * i;
			o.dwType = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(i);
			snprintf(o.tszName, sizeof(o.tszName), "%s", names[i]);
			if (cb(&o, ctx) == DIENUM_STOP)
				break;
		}
		return DI_OK;
	}
	HRESULT STDMETHODCALLTYPE GetProperty(REFGUID prop, LPDIPROPHEADER h) override
	{
		if (PropId(prop) == 13 || PropId(prop) == 14)	// DIPROP_INSTANCENAME, DIPROP_PRODUCTNAME
		{
			const char *name = pad ? SDL_GetGamepadName(pad) : joy ? SDL_GetJoystickName(joy) : kind == MOUSE ? "Mouse" : "Keyboard";
			LPDIPROPSTRING s = (LPDIPROPSTRING)h;
			unsigned short *w = (unsigned short *)s->wsz;
			int i = 0;
			// At most 63 characters: the game copies the name into its own text buffers (the Controller Options page's
			// joystick line is 208 characters, after "Joystick 1: " and before ": Enabled").
			for (; name && name[i] && i < 63; i++)
				w[i] = (unsigned char)name[i];
			w[i] = 0;
			return DI_OK;
		}
		if (PropId(prop) == 4)	// DIPROP_RANGE
		{
			LPDIPROPRANGE r = (LPDIPROPRANGE)h;
			r->lMin = rangeMin;
			r->lMax = rangeMax;
			return DI_OK;
		}
		return DIERR_UNSUPPORTED;
	}
	HRESULT STDMETHODCALLTYPE SetProperty(REFGUID prop, LPCDIPROPHEADER h) override
	{
		if (PropId(prop) == 4)	// DIPROP_RANGE
		{
			const DIPROPRANGE *r = (const DIPROPRANGE *)h;
			rangeMin = r->lMin;
			rangeMax = r->lMax;
		}
		return DI_OK;
	}
	HRESULT STDMETHODCALLTYPE Acquire() override
	{
		if (acquired)
			return S_FALSE;
		acquired = true;
		if (getenv("BEA_PORT_INPUT_LOG"))
			fprintf(stderr, PORT_LOG_PREFIX "input: DirectInput %s acquired (cooperative %08lx)\n",
					kind == MOUSE ? "mouse" : kind == KEYBOARD ? "keyboard" : "pad", (unsigned long)cooperative);
		// DirectInput reports a mouse's own movement (mickeys) whether acquired exclusive or not, so turning never stops
		// at the screen's edge. SDL gives that only in relative mode, so an acquired mouse is always relative (-forcewindowed
		// runs acquire it non-exclusive: the cursor used to stop at the edge and with it the battle engine's turning).
		if (kind == MOUSE)
		{
			ResetWheel(PortSdlWindow(window));	// do not inherit menu or previous-acquisition wheel input
			PortSessionLog(PORT_SESSION_WINDOW, false, "mouse acquired cooperative=%08lx relative_mode=requested", (unsigned long)cooperative);
			gMouseWindow = window;
			if (SDL_Window *w = PortSdlWindow(window))
			{
				if (!SDL_SetWindowRelativeMouseMode(w, true))
				{
					PortSessionLog(PORT_SESSION_WINDOW, true, "relative mouse acquire refused: %.200s", SDL_GetError());
					fprintf(stderr, PORT_LOG_PREFIX "input: relative mouse mode refused at acquire: %s\n", SDL_GetError());
				}
				if (PortSessionEnabled()) PortSessionLog(PORT_SESSION_WINDOW, false, "mouse relative_mode=%d input_focus=%d",
					SDL_GetWindowRelativeMouseMode(w) ? 1 : 0, (SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS) ? 1 : 0);
			}
		}
		if (kind == MOUSE)
			SDL_GetRelativeMouseState(NULL, NULL);
		return DI_OK;
	}
	HRESULT STDMETHODCALLTYPE Unacquire() override
	{
		if (!acquired)
			return S_FALSE;
		acquired = false;
		if (kind == JOYSTICK)
			triggerDown[0] = triggerDown[1] = false;
		if (kind == MOUSE)
		{
			gMouseReleasedAt = SDL_GetTicksNS();
			ResetWheel(NULL);
			PortSessionLog(PORT_SESSION_WINDOW, false, "mouse unacquired");
			gMouseWindow = NULL;
			if (SDL_Window *w = PortSdlWindow(window))
				SDL_SetWindowRelativeMouseMode(w, false);
		}
		return DI_OK;
	}
	// A plain joystick's raw axis, scaled as Axis (absent axes rest at the centre).
	LONG RawAxis(int a)
	{
		int v = joy && a < SDL_GetNumJoystickAxes(joy) ? SDL_GetJoystickAxis(joy, a) : 0;
		return rangeMin + (LONG)((long long)(v + 32768) * (rangeMax - rangeMin) / 65535);
	}
	LONG Axis(SDL_GamepadAxis a)
	{
		int v = pad ? SDL_GetGamepadAxis(pad, a) : 0;	// -32768..32767 (triggers 0..32767)
		return rangeMin + (LONG)((long long)(v + 32768) * (rangeMax - rangeMin) / 65535);
	}
	HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size, LPVOID data) override
	{
		if (!acquired)
			return DIERR_NOTACQUIRED;
		memset(data, 0, size);
		PortPumpInput();
		if (kind == JOYSTICK)
		{
			FakePadStep();
			Rebind(this);
		}
		if (kind == KEYBOARD)
		{
			int n = 0;
			const bool *keys = SDL_GetKeyboardState(&n);
			unsigned char *out = (unsigned char *)data;
			for (int s = 0; s < n; s++)
				if (keys[s])
					if (unsigned char d = DikFor((SDL_Scancode)s))
						if (d < size)
							out[d] = 0x80;
			if (getenv("BEA_PORT_INPUT_LOG"))
			{
				static int last = -1;
				int down = 0;
				for (int s = 0; s < n; s++)
					down += keys[s] ? 1 : 0;
				if (down != last)
					fprintf(stderr, PORT_LOG_PREFIX "input: keyboard poll sees %d keys down\n", down);
				last = down;
			}
		}
		else if (kind == MOUSE)
		{
			DIMOUSESTATE2 *m = (DIMOUSESTATE2 *)data;
			long tx, ty, tz;
			int tb;
			if (PortReplayMouse(&tx, &ty, &tz, &tb))
			{
				// A tape plays the mouse (replay.cpp): its movement once, its buttons as a level; SDL's is ignored.
				m->lX = tx;
				m->lY = ty;
				m->lZ = tz;
				for (int i = 0; i < 3; i++)
					m->rgbButtons[i] = (tb >> i) & 1 ? 0x80 : 0;
				return DI_OK;
			}
			float dx = 0, dy = 0;
			SDL_MouseButtonFlags b = SDL_GetRelativeMouseState(&dx, &dy);
			m->lX = (LONG)dx;
			m->lY = (LONG)dy;
			SDL_LockSpinlock(&gWheelLock);
			m->lZ = (LONG)(gWheel * WHEEL_DELTA);
			gWheel = 0;
			SDL_UnlockSpinlock(&gWheelLock);
			m->rgbButtons[0] = (b & SDL_BUTTON_LMASK) ? 0x80 : 0;
			m->rgbButtons[1] = (b & SDL_BUTTON_RMASK) ? 0x80 : 0;
			m->rgbButtons[2] = (b & SDL_BUTTON_MMASK) ? 0x80 : 0;
			if (getenv("BEA_PORT_INPUT_LOG"))
			{
				static SDL_MouseButtonFlags lastb = 0xffffffff;
				if (b != lastb || dx || dy)
					fprintf(stderr, PORT_LOG_PREFIX "input: mouse poll buttons %x move %.0f,%.0f\n", (unsigned)b, dx, dy);
				lastb = b;
			}
			if (size >= sizeof(DIMOUSESTATE2))
			{
				m->rgbButtons[3] = (b & SDL_BUTTON_X1MASK) ? 0x80 : 0;
				m->rgbButtons[4] = (b & SDL_BUTTON_X2MASK) ? 0x80 : 0;
			}
		}
		else if (joy && size >= sizeof(DIJOYSTATE))
		{
			// A flight stick, laid out for the fields the game reads (PCController.cpp): the stick's X and Y as the
			// right stick (Z and Rz: look and steer), the twist (axis 3) as the left stick's X (X: strafe), the hat as
			// the POV. The throttle (axis 2) is not mapped: many rest at one end, which would read as a held stick.
			DIJOYSTATE2 *j = (DIJOYSTATE2 *)data;
			LONG centre = rangeMin + (rangeMax - rangeMin) / 2;
			j->lX = RawAxis(3);
			j->lY = centre;
			j->lZ = RawAxis(0);
			j->lRz = RawAxis(1);
			j->lRx = j->lRy = centre;
			for (int i = 0; i < 4; i++)
				j->rgdwPOV[i] = 0xFFFFFFFF;
			if (SDL_GetNumJoystickHats(joy) > 0)
			{
				Uint8 h = SDL_GetJoystickHat(joy, 0);
				int dx = ((h & SDL_HAT_RIGHT) ? 1 : 0) - ((h & SDL_HAT_LEFT) ? 1 : 0);
				int dy = ((h & SDL_HAT_DOWN) ? 1 : 0) - ((h & SDL_HAT_UP) ? 1 : 0);
				static const DWORD pov[3][3] = { { 31500, 0, 4500 }, { 27000, 0xFFFFFFFF, 9000 }, { 22500, 18000, 13500 } };
				j->rgdwPOV[0] = pov[dy + 1][dx + 1];
			}
			int buttons = SDL_min(SDL_GetNumJoystickButtons(joy), 32);
			for (int i = 0; i < buttons; i++)
				j->rgbButtons[i] = SDL_GetJoystickButton(joy, i) ? 0x80 : 0;
		}
		else if (kind == JOYSTICK && !pad && size >= sizeof(DIJOYSTATE))
		{
			// A slot with no controller (unplugged, or the empty slot): an idle pad, sticks centred and the hat released.
			DIJOYSTATE2 *j = (DIJOYSTATE2 *)data;
			LONG centre = rangeMin + (rangeMax - rangeMin) / 2;
			j->lX = j->lY = j->lZ = j->lRx = j->lRy = j->lRz = centre;
			for (int i = 0; i < 4; i++)
				j->rgdwPOV[i] = 0xFFFFFFFF;
		}
		else if (pad && size >= sizeof(DIJOYSTATE))
		{
			DIJOYSTATE2 *j = (DIJOYSTATE2 *)data;
			j->lX = Axis(SDL_GAMEPAD_AXIS_LEFTX);
			j->lY = Axis(SDL_GAMEPAD_AXIS_LEFTY);
			j->lZ = Axis(SDL_GAMEPAD_AXIS_RIGHTX);
			j->lRx = Axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
			j->lRy = Axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
			j->lRz = Axis(SDL_GAMEPAD_AXIS_RIGHTY);
			for (int i = 0; i < 4; i++)
				j->rgdwPOV[i] = 0xFFFFFFFF;
			bool up = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP), down = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
			bool left = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT), right = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
			if (up || down || left || right)
			{
				int dx = right - left, dy = down - up;
				static const DWORD pov[3][3] = { { 31500, 0, 4500 }, { 27000, 0xFFFFFFFF, 9000 }, { 22500, 18000, 13500 } };
				j->rgdwPOV[0] = pov[dy + 1][dx + 1];
			}
			for (int i = 0; i < 16 && i < SDL_GAMEPAD_BUTTON_DPAD_UP; i++)
				j->rgbButtons[i] = SDL_GetGamepadButton(pad, (SDL_GamepadButton)i) ? 0x80 : 0;
			// Slots 16 and 17 were not emitted by the SDL adapter. Keep existing
			// gamepad button numbers intact and expose actual trigger pressure there.
			// Hysteresis prevents a noisy trigger from repeatedly pressing at rest.
			if (BindableGamepadTriggers() && SDL_GamepadConnected(pad))
			{
				const SDL_GamepadAxis axes[] = { SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER };
				for (int i = 0; i < 2; i++)
				{
					int value = SDL_GetGamepadAxis(pad, axes[i]); // SDL gamepad triggers: 0..32767
					triggerDown[i] = triggerDown[i] ? value > 12288 : value >= 16384;
					j->rgbButtons[16 + i] = triggerDown[i] ? 0x80 : 0;
				}
			}
			else
				triggerDown[0] = triggerDown[1] = false;
		}
		return DI_OK;
	}
	HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD, LPDIDEVICEOBJECTDATA, LPDWORD n, DWORD) override
	{
		if (n)
			*n = 0;
		return DI_OK;
	}
	HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT) override { return DI_OK; }
	HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE) override { return DI_OK; }
	HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND w, DWORD flags) override
	{
		window = w;
		cooperative = flags;
		return DI_OK;
	}
	HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA, DWORD, DWORD) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE RunControlPanel(HWND, DWORD) override { return DI_OK; }
	HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE, DWORD, REFGUID) override { return DI_OK; }
	HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT *out, IUnknown *) override
	{
		*out = NULL;
		return DIERR_UNSUPPORTED;
	}
	HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA, LPVOID, DWORD) override { return DI_OK; }
	HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA, REFGUID) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD) override { return DI_OK; }
	HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE) override { return DIERR_UNSUPPORTED; }
	// UpdateJoystick acquires only after Poll fails. Reporting success before acquisition leaves every pad inert.
	HRESULT STDMETHODCALLTYPE Poll() override { return acquired ? DI_NOEFFECT_OR_OK : DIERR_NOTACQUIRED; }
	HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCSTR, LPDIENUMEFFECTSINFILECALLBACK, LPVOID, DWORD) override { return DI_OK; }
	HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCSTR, DWORD, LPDIFILEEFFECT, DWORD) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATA, LPCSTR, DWORD) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATA, LPCSTR, DWORD) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA) override { return DIERR_UNSUPPORTED; }

	static const HRESULT DI_NOEFFECT_OR_OK = DI_OK;
};

// Device instance GUIDs for controllers carry the SDL joystick id.
// Hotplug in ordinary CE play: the game builds its pad list once, when DirectInput starts, so a slot whose controller
// has gone re-binds to the first connected controller no other slot holds (a gamepad first, then a flight stick), at
// most twice a second; with nothing attached at startup the game is offered one empty slot (kPlaceholder) that binds
// the same way. A slot never changes kind of slot for the game; an unbound slot reads as an idle pad.
const SDL_JoystickID kPlaceholder = 0xffff;

bool Claimed(SDL_JoystickID id)
{
	for (Device *d : gPadSlots)
		if (d && (d->pad || d->joy) && d->joystickId == id)
			return true;
	return false;
}

void Rebind(Device *d)
{
	if (!BindableGamepadTriggers())
		return;
	if ((d->pad && SDL_GamepadConnected(d->pad)) || (d->joy && SDL_JoystickConnected(d->joy)))
		return;
	static Uint64 lastTry;
	Uint64 now = SDL_GetTicks();
	if (lastTry && now - lastTry < 500)
		return;
	lastTry = now;
	if (d->pad)
		SDL_CloseGamepad(d->pad);
	if (d->joy)
		SDL_CloseJoystick(d->joy);
	d->pad = NULL;
	d->joy = NULL;
	d->triggerDown[0] = d->triggerDown[1] = false;
	int count = 0;
	SDL_JoystickID *ids = SDL_GetJoysticks(&count);
	for (int pass = 0; pass < 2 && ids && !d->pad && !d->joy; pass++)
		for (int i = 0; i < count && !d->pad && !d->joy; i++)
			if (SDL_IsGamepad(ids[i]) == (pass == 0) && !Claimed(ids[i]))
			{
				if (pass == 0)
					d->pad = SDL_OpenGamepad(ids[i]);
				else
					d->joy = SDL_OpenJoystick(ids[i]);
				if (d->pad || d->joy)
				{
					d->joystickId = ids[i];
					fprintf(stderr, PORT_LOG_PREFIX "input: controller connected to a game pad slot: %s\n",
							d->pad ? SDL_GetGamepadName(d->pad) : SDL_GetJoystickName(d->joy));
				}
			}
	SDL_free(ids);
}

GUID PadGuid(SDL_JoystickID id)
{
	GUID g = GUID_Joystick;
	g.Data4[7] = (unsigned char)id;
	g.Data4[6] = (unsigned char)(id >> 8);
	return g;
}

struct Input : public IDirectInput8A
{
	LONG refs = 1;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID *out) override
	{
		*out = this;
		AddRef();
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG r = --refs;
		if (!r)
			Destroy(this);
		return r;
	}
	HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID guid, LPDIRECTINPUTDEVICE8A *out, IUnknown *) override
	{
		if (memcmp(&guid, &GUID_SysKeyboard, sizeof(GUID)) == 0)
			*out = Make<Device>(KEYBOARD, (SDL_JoystickID)0);
		else if (memcmp(&guid, &GUID_SysMouse, sizeof(GUID)) == 0)
			*out = Make<Device>(MOUSE, (SDL_JoystickID)0);
		else
		{
			SDL_JoystickID id = (SDL_JoystickID)(guid.Data4[7] | (guid.Data4[6] << 8));
			Device *d = Make<Device>(JOYSTICK, id);
			if (id == kPlaceholder && BindableGamepadTriggers())
				d->joystickId = 0;	// unbound until a controller connects (Rebind)
			else if (SDL_IsGamepad(id))
				d->pad = SDL_OpenGamepad(id);
			else if (BindableGamepadTriggers())
				d->joy = SDL_OpenJoystick(id);
			if (!d->pad && !d->joy && !(id == kPlaceholder && BindableGamepadTriggers()))
			{
				d->Release();
				*out = NULL;
				return DIERR_DEVICENOTREG;
			}
			for (Device *&slot : gPadSlots)
				if (!slot)
				{
					slot = d;
					break;
				}
			*out = d;
		}
		return DI_OK;
	}
	HRESULT STDMETHODCALLTYPE EnumDevices(DWORD devclass, LPDIENUMDEVICESCALLBACKA cb, LPVOID ctx, DWORD) override
	{
		if (devclass != DI8DEVCLASS_GAMECTRL && devclass != DI8DEVCLASS_ALL)
			return DI_OK;
		int count = 0, offered = 0;
		bool stop = false;
		SDL_JoystickID *ids = SDL_GetGamepads(&count);
		for (int i = 0; i < count && !stop; i++)
		{
			DIDEVICEINSTANCEA d;
			memset(&d, 0, sizeof(d));
			d.dwSize = sizeof(d);
			d.guidInstance = PadGuid(ids[i]);
			d.guidProduct = d.guidInstance;
			d.dwDevType = DI8DEVTYPE_GAMEPAD;
			const char *name = SDL_GetGamepadNameForID(ids[i]);
			snprintf(d.tszInstanceName, sizeof(d.tszInstanceName), "%s", name ? name : "Controller");
			snprintf(d.tszProductName, sizeof(d.tszProductName), "%s", name ? name : "Controller");
			stop = cb(&d, ctx) == DIENUM_STOP;
			offered++;
		}
		SDL_free(ids);
		// Ordinary CE play also offers joysticks SDL does not know as gamepads (flight sticks), after the gamepads.
		ids = BindableGamepadTriggers() ? SDL_GetJoysticks(&count) : NULL;
		for (int i = 0; ids && i < count && !stop; i++)
		{
			if (SDL_IsGamepad(ids[i]))
				continue;
			DIDEVICEINSTANCEA d;
			memset(&d, 0, sizeof(d));
			d.dwSize = sizeof(d);
			d.guidInstance = PadGuid(ids[i]);
			d.guidProduct = d.guidInstance;
			d.dwDevType = DI8DEVTYPE_JOYSTICK;
			const char *name = SDL_GetJoystickNameForID(ids[i]);
			snprintf(d.tszInstanceName, sizeof(d.tszInstanceName), "%s", name ? name : "Joystick");
			snprintf(d.tszProductName, sizeof(d.tszProductName), "%s", name ? name : "Joystick");
			stop = cb(&d, ctx) == DIENUM_STOP;
			offered++;
		}
		SDL_free(ids);
		// Ordinary CE play with nothing attached: one empty slot, so a controller plugged in later still works.
		if (!offered && BindableGamepadTriggers())
		{
			DIDEVICEINSTANCEA d;
			memset(&d, 0, sizeof(d));
			d.dwSize = sizeof(d);
			d.guidInstance = PadGuid(kPlaceholder);
			d.guidProduct = d.guidInstance;
			d.dwDevType = DI8DEVTYPE_GAMEPAD;
			snprintf(d.tszInstanceName, sizeof(d.tszInstanceName), "Controller (connect any time)");
			snprintf(d.tszProductName, sizeof(d.tszProductName), "Controller (connect any time)");
			cb(&d, ctx);
		}
		return DI_OK;
	}
	HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID) override { return DI_OK; }
	HRESULT STDMETHODCALLTYPE RunControlPanel(HWND, DWORD) override { return DI_OK; }
	HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE, DWORD) override { return DI_OK; }
	HRESULT STDMETHODCALLTYPE FindDevice(REFGUID, LPCSTR, LPGUID) override { return DIERR_DEVICENOTREG; }
	HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCSTR, LPDIACTIONFORMATA, LPDIENUMDEVICESBYSEMANTICSCBA, LPVOID, DWORD) override
	{
		return DI_OK;
	}
	HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK, LPDICONFIGUREDEVICESPARAMSA, DWORD, LPVOID) override
	{
		return DIERR_UNSUPPORTED;
	}
};
}

// The CE rumble (patch 0105, CPCPlatform::TriggerRumble): one effect of the given motor strengths and length on the
// controller behind a joystick device this shim made. 1 when SDL took it, 0 otherwise (no such pad, or no rumble).
extern "C" int PortRumblePad(void *device, unsigned short low, unsigned short high, unsigned ms)
{
	Device *d = device ? static_cast<Device *>(static_cast<IDirectInputDevice8A *>(device)) : NULL;
	if (!d || d->kind != JOYSTICK || !d->pad)
		return 0;
	return SDL_RumbleGamepad(d->pad, low, high, ms) ? 1 : 0;
}

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD, REFIID, LPVOID *out, IUnknown *)
{
	if (!SDL_WasInit(SDL_INIT_GAMEPAD) && !SDL_InitSubSystem(SDL_INIT_GAMEPAD))
		return DIERR_NOTINITIALIZED;
	FakePadAttach();
	static bool watching;
	if (!watching)
	{
		SDL_AddEventWatch(WheelWatch, NULL);
		watching = true;
	}
	*out = static_cast<IDirectInput8A *>(Make<Input>());
	return DI_OK;
}

bool PortDinputMouseAcquired() { return gMouseWindow != NULL; }

// SDL timestamps events before queueing them. Keep input from a completed capture epoch out of later menu messages;
// checking only current acquisition would deliver an unread gameplay wheel/button after ShutdownMouse releases it.
bool PortDinputOwnsMouseEvent(Uint64 timestamp)
{
	return gMouseWindow != NULL || (timestamp && timestamp <= gMouseReleasedAt);
}

// Each frame (window.cpp's pump): while the game holds the mouse and its window has keyboard focus, relative mode must be on.
// A compositor can refuse it (the window not yet focused at acquire) or drop it (a focus change); without it the pointer
// stops at the screen's edges and the battle engine stops turning or pitching (recorded play, 2026-10-16).
void PortMouseKeepRelative()
{
	if (!gMouseWindow)
		return;
	SDL_Window *w = PortSdlWindow(gMouseWindow);
	if (!w || !(SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS) || SDL_GetWindowRelativeMouseMode(w))
		return;
	static Uint64 lastTry;
	static int logged;
	Uint64 now = SDL_GetTicks();
	if (lastTry && now - lastTry < 1000)
		return;	// at most one attempt a second (a driver without relative mode, such as offscreen, refuses every time)
	lastTry = now;
	bool ok = SDL_SetWindowRelativeMouseMode(w, true);
	if (logged < 5)
		PortSessionLog(PORT_SESSION_WINDOW, !ok, "relative mouse was off; recovery=%s detail=%.200s", ok ? "success" : "failed", ok ? "none" : SDL_GetError());
	if (logged++ < 5 && PortDevLog())
		fprintf(stderr, PORT_LOG_PREFIX "input: relative mouse mode was off; re-enabled: %s\n", ok ? "yes" : SDL_GetError());
}

#ifdef CE_VERSION
// The physical key whose state the game reads as DirectInput key code dik (the first scancode DikFor maps to it), or
// SDL_SCANCODE_UNKNOWN. CE help uses it to name a key binding in the current keyboard layout (ce_tutorial_keys.cpp).
SDL_Scancode PortScancodeForDik(int dik)
{
	if (dik <= 0 || dik > 0xff)
		return SDL_SCANCODE_UNKNOWN;
	for (int s = 1; s < SDL_SCANCODE_COUNT; ++s)
		if (DikFor((SDL_Scancode)s) == dik)
			return (SDL_Scancode)s;
	return SDL_SCANCODE_UNKNOWN;
}
#endif
