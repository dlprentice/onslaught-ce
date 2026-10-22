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

// Mouse wheel motion arrives only as events; an event watch accumulates it for the next GetDeviceState.
float gWheel;
bool SDLCALL WheelWatch(void *, SDL_Event *e)
{
	if (e->type == SDL_EVENT_MOUSE_WHEEL)
		gWheel += e->wheel.y;
	return true;
}

enum Kind { KEYBOARD, MOUSE, JOYSTICK };

struct Device : public IDirectInputDevice8A
{
	LONG refs = 1;
	Kind kind;
	SDL_JoystickID joystickId = 0;
	SDL_Gamepad *pad = NULL;
	HWND window = NULL;
	DWORD cooperative = 0;
	bool acquired = false;
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
			if (pad)
				SDL_CloseGamepad(pad);
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
		caps->dwDevType = kind == JOYSTICK ? DI8DEVTYPE_GAMEPAD : 0;
		caps->dwAxes = kind == JOYSTICK ? 6 : kind == MOUSE ? 3 : 0;
		caps->dwButtons = kind == JOYSTICK ? 16 : kind == MOUSE ? 8 : 0;
		caps->dwPOVs = kind == JOYSTICK ? 1 : 0;
		return DI_OK;
	}
	HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ctx, DWORD flags) override
	{
		if (kind != JOYSTICK || !(flags & DIDFT_AXIS))
			return DI_OK;
		// The pad's axes, in DIJOYSTATE2 order: X, Y (left stick), Z (triggers), Rx, Ry (right stick), Rz.
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
		if (&prop == &DIPROP_INSTANCENAME || &prop == &DIPROP_PRODUCTNAME)
		{
			const char *name = pad ? SDL_GetGamepadName(pad) : kind == MOUSE ? "Mouse" : "Keyboard";
			LPDIPROPSTRING s = (LPDIPROPSTRING)h;
			unsigned short *w = (unsigned short *)s->wsz;
			int i = 0;
			for (; name && name[i] && i < MAX_PATH - 1; i++)
				w[i] = (unsigned char)name[i];
			w[i] = 0;
			return DI_OK;
		}
		if (&prop == &DIPROP_RANGE)
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
		if (&prop == &DIPROP_RANGE)
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
			gMouseWindow = window;
			if (SDL_Window *w = PortSdlWindow(window))
				if (!SDL_SetWindowRelativeMouseMode(w, true))
					fprintf(stderr, PORT_LOG_PREFIX "input: relative mouse mode refused at acquire: %s\n", SDL_GetError());
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
		if (kind == MOUSE)
		{
			gMouseWindow = NULL;
			if (SDL_Window *w = PortSdlWindow(window))
				SDL_SetWindowRelativeMouseMode(w, false);
		}
		return DI_OK;
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
			m->lZ = (LONG)(gWheel * WHEEL_DELTA);
			gWheel = 0;
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
		else if (pad && size >= sizeof(DIJOYSTATE))
		{
			DIJOYSTATE2 *j = (DIJOYSTATE2 *)data;
			j->lX = Axis(SDL_GAMEPAD_AXIS_LEFTX);
			j->lY = Axis(SDL_GAMEPAD_AXIS_LEFTY);
			j->lZ = Axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
			j->lRx = Axis(SDL_GAMEPAD_AXIS_RIGHTX);
			j->lRy = Axis(SDL_GAMEPAD_AXIS_RIGHTY);
			j->lRz = Axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
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
	HRESULT STDMETHODCALLTYPE Poll() override { return DI_NOEFFECT_OR_OK; }
	HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCSTR, LPDIENUMEFFECTSINFILECALLBACK, LPVOID, DWORD) override { return DI_OK; }
	HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCSTR, DWORD, LPDIFILEEFFECT, DWORD) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATA, LPCSTR, DWORD) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATA, LPCSTR, DWORD) override { return DIERR_UNSUPPORTED; }
	HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA) override { return DIERR_UNSUPPORTED; }

	static const HRESULT DI_NOEFFECT_OR_OK = DI_OK;
};

// Device instance GUIDs for controllers carry the SDL joystick id.
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
			d->pad = SDL_OpenGamepad(id);
			if (!d->pad)
			{
				d->Release();
				*out = NULL;
				return DIERR_DEVICENOTREG;
			}
			*out = d;
		}
		return DI_OK;
	}
	HRESULT STDMETHODCALLTYPE EnumDevices(DWORD devclass, LPDIENUMDEVICESCALLBACKA cb, LPVOID ctx, DWORD) override
	{
		if (devclass != DI8DEVCLASS_GAMECTRL && devclass != DI8DEVCLASS_ALL)
			return DI_OK;
		int count = 0;
		SDL_JoystickID *ids = SDL_GetGamepads(&count);
		for (int i = 0; i < count; i++)
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
			if (cb(&d, ctx) == DIENUM_STOP)
				break;
		}
		SDL_free(ids);
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

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD, REFIID, LPVOID *out, IUnknown *)
{
	if (!SDL_WasInit(SDL_INIT_GAMEPAD) && !SDL_InitSubSystem(SDL_INIT_GAMEPAD))
		return DIERR_NOTINITIALIZED;
	static bool watching;
	if (!watching)
	{
		SDL_AddEventWatch(WheelWatch, NULL);
		watching = true;
	}
	*out = static_cast<IDirectInput8A *>(Make<Input>());
	return DI_OK;
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
	if (logged++ < 5 && PortDevLog())
		fprintf(stderr, PORT_LOG_PREFIX "input: relative mouse mode was off; re-enabled: %s\n", ok ? "yes" : SDL_GetError());
}
