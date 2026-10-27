// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/window.cpp: the game's window and message loop on SDL3, on both targets. The game registers a
// class and creates its window (d3dapp.cpp); here that is an SDL window, and SDL's events become the messages the
// game's window procedure (CD3DApplication::MsgProc via WndProc) handles, through the game's own Peek/Get/Dispatch
// loop. The HWND the game and Direct3D see is the SDL_Window itself on Linux (dxvk-native's convention) and the SDL
// window's Win32 handle on Windows. port.h renames the game's window, message and cursor calls to these Port* functions
// on both targets; on Windows, calls on windows the port does not own (the sample framework's device dialog and its
// controls) go to the real API.
#include <windows.h>

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
extern "C" unsigned PortPresentCount(void);	// d3d9hooks.cpp
bool PortDinputOwnsMouseEvent(Uint64 timestamp);	// dinput.cpp: current capture and queued events from completed captures
bool PortRecordInputSpace(unsigned slots);	// replay.cpp: fail capture before deferring an unrepresentable input

#if defined(_WIN32)
#define WIN32_PASSTHROUGH(call) if (!Find(h)) return call;
#else
#define WIN32_PASSTHROUGH(call)
#endif

// ---- window classes and windows -------------------------------------------------------------------------------------
namespace
{
struct WindowClass
{
	char name[64];
	WNDPROC proc;
	HCURSOR cursor;
};

struct Window
{
	SDL_Window *sdl;
	HWND handle;			// what the game and Direct3D see: the SDL_Window on Linux, its Win32 handle on Windows
	WNDPROC proc;
	LONG style;
	LONG exstyle;
	LONG_PTR userdata;
	DWORD classCursor;
};

WindowClass gClasses[8];
int gNumClasses;
Window gWindows[8];
int gNumWindows;

SDL_Window *Sdl(HWND h);

Window *Find(HWND h)
{
	for (int i = 0; i < gNumWindows; i++)
		if (gWindows[i].handle == h)
			return &gWindows[i];
	return NULL;
}

SDL_Window *Sdl(HWND h)
{
	Window *w = Find(h);
	return w ? w->sdl : NULL;
}

// Messages posted for the game (SDL events translated, PostMessage, WM_QUIT).
MSG gQueue[256];
int gHead, gTail;
bool gReleasePending;	// release repair or a focus-loss release batch is waiting for space in the ring

// BEA_PORT_INPUT_LOG: keys down and up (scan code, keycode, virtual key), drops, synthetic releases, focus.
bool InputLog()
{
	static int on = -1;
	if (on < 0)
		on = getenv("BEA_PORT_INPUT_LOG") ? 1 : 0;
	return on == 1;
}

int Free() { return 255 - (gTail - gHead + 256) % 256; }

bool Post(HWND h, UINT msg, WPARAM w, LPARAM l)
{
	int next = (gTail + 1) % 256;
	if (next == gHead)
	{
		// full: drop, as a saturated Windows queue would; a dropped key-up must not leave a key held (gReleasePending)
		static unsigned drops;
		if (InputLog() || drops < 8)
			fprintf(stderr, PORT_LOG_PREFIX "input: queue full, message %04x (%08lx, %08lx) dropped\n", msg, (unsigned long)w, (unsigned long)l);
		drops++;
		gReleasePending = true;
		return false;
	}
	MSG &m = gQueue[gTail];
	m.hwnd = h;
	m.message = msg;
	m.wParam = w;
	m.lParam = l;
	m.time = (DWORD)SDL_GetTicks();
	m.pt.x = m.pt.y = 0;
	gTail = next;
	return true;
}

// The player's mouse movement, coalesced as Windows does (WM_MOUSEMOVE is made from the latest position when the queue
// is read, never one per hardware report): a move that follows an unread move replaces it.
void PostMouseMove(HWND h, WPARAM w, LPARAM l)
{
	if (gHead != gTail)
	{
		MSG &last = gQueue[(gTail + 255) % 256];
		if (last.message == WM_MOUSEMOVE && last.hwnd == h && last.wParam == w)
		{
			last.lParam = l;
			last.time = (DWORD)SDL_GetTicks();
			return;
		}
	}
	Post(h, WM_MOUSEMOVE, w, l);
}

bool Pop(MSG *out, bool remove)
{
	if (gHead == gTail)
		return false;
	*out = gQueue[gHead];
	if (remove)
		gHead = (gHead + 1) % 256;
	return true;
}

// SDL scancode (USB HID usage) -> the PC set-1 scan code Windows puts in a key message's lParam (bits 16-23), with
// 0x100 for the extended flag (bit 24). The game records keys by that code (ltshell.cpp MsgProc: extended keys above
// 0x80), so live input must carry Windows' codes, not the host's raw ones (Wayland evdev, X11 keycodes).
unsigned PcScan(SDL_Scancode s)
{
	static const unsigned char letters[26] = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
		0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
	if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z)
		return letters[s - SDL_SCANCODE_A];
	if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0)
		return 0x02 + (s - SDL_SCANCODE_1);		// 1..9 = 0x02..0x0a, 0 = 0x0b
	if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10)
		return 0x3b + (s - SDL_SCANCODE_F1);
	switch (s)
	{
	case SDL_SCANCODE_RETURN: return 0x1c;
	case SDL_SCANCODE_ESCAPE: return 0x01;
	case SDL_SCANCODE_BACKSPACE: return 0x0e;
	case SDL_SCANCODE_TAB: return 0x0f;
	case SDL_SCANCODE_SPACE: return 0x39;
	case SDL_SCANCODE_MINUS: return 0x0c;
	case SDL_SCANCODE_EQUALS: return 0x0d;
	case SDL_SCANCODE_LEFTBRACKET: return 0x1a;
	case SDL_SCANCODE_RIGHTBRACKET: return 0x1b;
	case SDL_SCANCODE_BACKSLASH: return 0x2b;
	case SDL_SCANCODE_SEMICOLON: return 0x27;
	case SDL_SCANCODE_APOSTROPHE: return 0x28;
	case SDL_SCANCODE_GRAVE: return 0x29;
	case SDL_SCANCODE_COMMA: return 0x33;
	case SDL_SCANCODE_PERIOD: return 0x34;
	case SDL_SCANCODE_SLASH: return 0x35;
	case SDL_SCANCODE_CAPSLOCK: return 0x3a;
	case SDL_SCANCODE_F11: return 0x57;
	case SDL_SCANCODE_F12: return 0x58;
	case SDL_SCANCODE_SCROLLLOCK: return 0x46;
	case SDL_SCANCODE_NUMLOCKCLEAR: return 0x145;
	case SDL_SCANCODE_PAUSE: return 0x45;
	case SDL_SCANCODE_LCTRL: return 0x1d;
	case SDL_SCANCODE_LSHIFT: return 0x2a;
	case SDL_SCANCODE_LALT: return 0x38;
	case SDL_SCANCODE_RSHIFT: return 0x36;
	case SDL_SCANCODE_RCTRL: return 0x11d;
	case SDL_SCANCODE_RALT: return 0x138;
	case SDL_SCANCODE_LGUI: return 0x15b;
	case SDL_SCANCODE_RGUI: return 0x15c;
	case SDL_SCANCODE_APPLICATION: return 0x15d;
	case SDL_SCANCODE_PRINTSCREEN: return 0x137;
	case SDL_SCANCODE_INSERT: return 0x152;
	case SDL_SCANCODE_DELETE: return 0x153;
	case SDL_SCANCODE_HOME: return 0x147;
	case SDL_SCANCODE_END: return 0x14f;
	case SDL_SCANCODE_PAGEUP: return 0x149;
	case SDL_SCANCODE_PAGEDOWN: return 0x151;
	case SDL_SCANCODE_UP: return 0x148;
	case SDL_SCANCODE_DOWN: return 0x150;
	case SDL_SCANCODE_LEFT: return 0x14b;
	case SDL_SCANCODE_RIGHT: return 0x14d;
	case SDL_SCANCODE_KP_DIVIDE: return 0x135;
	case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
	case SDL_SCANCODE_KP_MINUS: return 0x4a;
	case SDL_SCANCODE_KP_PLUS: return 0x4e;
	case SDL_SCANCODE_KP_ENTER: return 0x11c;
	case SDL_SCANCODE_KP_PERIOD: return 0x53;
	case SDL_SCANCODE_KP_7: return 0x47;
	case SDL_SCANCODE_KP_8: return 0x48;
	case SDL_SCANCODE_KP_9: return 0x49;
	case SDL_SCANCODE_KP_4: return 0x4b;
	case SDL_SCANCODE_KP_5: return 0x4c;
	case SDL_SCANCODE_KP_6: return 0x4d;
	case SDL_SCANCODE_KP_1: return 0x4f;
	case SDL_SCANCODE_KP_2: return 0x50;
	case SDL_SCANCODE_KP_3: return 0x51;
	case SDL_SCANCODE_KP_0: return 0x52;
	case SDL_SCANCODE_NONUSBACKSLASH: return 0x56;
	default: return 0;
	}
}

// SDL scancode -> Windows virtual key (the keys the game, its console and front end read through messages).
UINT VirtualKey(SDL_Keycode k, SDL_Scancode s)
{
	if (k >= 'a' && k <= 'z')
		return (UINT)(k - 'a' + 'A');
	if (k >= '0' && k <= '9')
		return (UINT)k;
	// A layout (or a keymap change between a press and its release) whose keycode is not a letter or digit: the key's
	// position decides, so a letter key always has a virtual key and its release is never lost.
	if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z)
		return (UINT)('A' + (s - SDL_SCANCODE_A));
	if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9)
		return (UINT)('1' + (s - SDL_SCANCODE_1));
	if (s == SDL_SCANCODE_0)
		return (UINT)'0';
	switch (s)
	{
	case SDL_SCANCODE_RETURN: return VK_RETURN;
	case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
	case SDL_SCANCODE_BACKSPACE: return VK_BACK;
	case SDL_SCANCODE_TAB: return VK_TAB;
	case SDL_SCANCODE_SPACE: return VK_SPACE;
	case SDL_SCANCODE_LEFT: return VK_LEFT;
	case SDL_SCANCODE_RIGHT: return VK_RIGHT;
	case SDL_SCANCODE_UP: return VK_UP;
	case SDL_SCANCODE_DOWN: return VK_DOWN;
	case SDL_SCANCODE_HOME: return VK_HOME;
	case SDL_SCANCODE_END: return VK_END;
	case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
	case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
	case SDL_SCANCODE_INSERT: return VK_INSERT;
	case SDL_SCANCODE_DELETE: return VK_DELETE;
	case SDL_SCANCODE_LSHIFT:
	case SDL_SCANCODE_RSHIFT: return VK_SHIFT;
	case SDL_SCANCODE_LCTRL:
	case SDL_SCANCODE_RCTRL: return VK_CONTROL;
	case SDL_SCANCODE_LALT:
	case SDL_SCANCODE_RALT: return VK_MENU;
	case SDL_SCANCODE_PAUSE: return VK_PAUSE;
	case SDL_SCANCODE_CAPSLOCK: return VK_CAPITAL;
	case SDL_SCANCODE_NUMLOCKCLEAR: return VK_NUMLOCK;
	case SDL_SCANCODE_SCROLLLOCK: return VK_SCROLL;
	case SDL_SCANCODE_KP_MULTIPLY: return VK_MULTIPLY;
	case SDL_SCANCODE_KP_PLUS: return VK_ADD;
	case SDL_SCANCODE_KP_MINUS: return VK_SUBTRACT;
	case SDL_SCANCODE_KP_DIVIDE: return VK_DIVIDE;
	case SDL_SCANCODE_KP_PERIOD: return VK_DECIMAL;
	case SDL_SCANCODE_GRAVE: return 0xC0;		// VK_OEM_3
	// The punctuation keys (US positions), which WM_CHAR now comes from (KeyChar), as on Windows
	case SDL_SCANCODE_MINUS: return 0xBD;		// VK_OEM_MINUS
	case SDL_SCANCODE_EQUALS: return 0xBB;		// VK_OEM_PLUS
	case SDL_SCANCODE_LEFTBRACKET: return 0xDB;	// VK_OEM_4
	case SDL_SCANCODE_RIGHTBRACKET: return 0xDD;	// VK_OEM_6
	case SDL_SCANCODE_BACKSLASH: return 0xDC;	// VK_OEM_5
	case SDL_SCANCODE_SEMICOLON: return 0xBA;	// VK_OEM_1
	case SDL_SCANCODE_APOSTROPHE: return 0xDE;	// VK_OEM_7
	case SDL_SCANCODE_COMMA: return 0xBC;		// VK_OEM_COMMA
	case SDL_SCANCODE_PERIOD: return 0xBE;		// VK_OEM_PERIOD
	case SDL_SCANCODE_SLASH: return 0xBF;		// VK_OEM_2
	case SDL_SCANCODE_NONUSBACKSLASH: return 0xE2;	// VK_OEM_102
	case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
	case SDL_SCANCODE_LGUI: return 0x5B;		// VK_LWIN
	case SDL_SCANCODE_RGUI: return 0x5C;		// VK_RWIN
	case SDL_SCANCODE_APPLICATION: return 0x5D;	// VK_APPS
	default: break;
	}
	if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12)
		return VK_F1 + (s - SDL_SCANCODE_F1);
	if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9)
		return VK_NUMPAD0 + 1 + (s - SDL_SCANCODE_KP_1);
	if (s == SDL_SCANCODE_KP_0)
		return VK_NUMPAD0;
	return 0;
}

extern "C" int PortMapMouse(float *x, float *y);	// d3d9hooks.cpp: window to game coordinates in desktop fullscreen

// SDL's mouse positions are in points; the client rectangle (PortGetClientRect) is in pixels. With widescreen modes on,
// the window has pixel density (HiDPI), so outside desktop fullscreen a position is scaled by the window's density.
LPARAM MouseLParam(float x, float y)
{
	if (!PortMapMouse(&x, &y) && PortWideModes() && gNumWindows)
	{
		float density = SDL_GetWindowPixelDensity(gWindows[0].sdl);
		if (density > 0 && density != 1.f)
		{
			x *= density;
			y *= density;
		}
	}
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	return MAKELPARAM((int)x, (int)y);
}

WPARAM MouseButtons()
{
	SDL_MouseButtonFlags b = SDL_GetMouseState(NULL, NULL);
	WPARAM w = 0;
	if (b & SDL_BUTTON_LMASK) w |= MK_LBUTTON;
	if (b & SDL_BUTTON_RMASK) w |= MK_RBUTTON;
	if (b & SDL_BUTTON_MMASK) w |= MK_MBUTTON;
	return w;
}

// ---- the player's keys ----------------------------------------------------------------------------------------------
// The game's only key state is LT.KeyDown[scan], set by WM_KEYDOWN and cleared only by WM_KEYUP (ltshell.cpp MsgProc),
// so a lost key-up leaves a key held until it is pressed again. The port remembers every key it posted down (by the PC
// scan code the message carries) and makes sure each gets its key-up: the real one, or a synthetic one when SDL reports
// the key up without an event the port saw (Reconcile), when the window loses focus, or after a dropped message.
// Only the player's keys are tracked: a replay's keys are posted by replay.cpp. In record mode both real and synthetic
// releases go through the recorder, so the captured tape and the game receive the same release at the next Present.
struct HeldKey
{
	bool down;
	bool released;		// released by the port (synthetic key-up): the real key-up that may still come is not posted again
	UINT vk;
	SDL_Scancode sdl;
};
HeldKey gKeys[0x200];	// by PC scan code, 0x100 = extended
int gHeld;

// The test switch's own key state (BEA_PORT_FAKE_KEYS): pushed events do not change SDL's keyboard state, so
// Reconcile treats a fake key as down from its 'd' to its 'u' or 'x' (x: the state goes up but the event is lost).
bool gFakeHeld[SDL_SCANCODE_COUNT];

LPARAM KeyLParam(unsigned scan, bool up, bool repeat)
{
	LPARAM l = 1 | ((LPARAM)(scan & 0x1ff) << 16);	// 0x100 lands on bit 24, Windows' extended flag
	if (up)
		l |= (LPARAM)3 << 30;
	else if (repeat)
		l |= (LPARAM)1 << 30;
	return l;
}

HWND MainWindow() { return gNumWindows ? gWindows[0].handle : NULL; }

void SyntheticKeyUp(unsigned scan, const char *why)
{
	HeldKey &k = gKeys[scan];
	if (!k.down)
		return;
	if (InputLog())
		fprintf(stderr, PORT_LOG_PREFIX "input: synthetic key up scan %03x vk %02x (%s) (%s)\n", scan, k.vk,
				SDL_GetScancodeName(k.sdl), why);
	if (PortRecording())
	{
		if (!PortRecordInputSpace(1))
		{
			gReleasePending = true;
			return;	// the recorder has not accepted this release; retain physical-key ownership for the next Present
		}
		PortRecordKey(false, k.vk, scan);
	}
	else if (!Post(MainWindow(), WM_KEYUP, k.vk, KeyLParam(scan, true, false)))
		return;	// keep ownership until a later pump can enqueue this release; Post requested that retry
	k.down = false;
	k.released = true;
	gHeld--;
}

void ReleaseAll(const char *why)
{
	for (unsigned s = 1; s < 0x200 && gHeld > 0; s++)
		if (gKeys[s].down)
		{
			// Leave room for the focus notifications after these releases. A later pump drains the rest.
			if (!PortRecording() && Free() <= 2)
			{
				gReleasePending = true;
				break;
			}
			SyntheticKeyUp(s, why);
		}
}

// Every key the port holds down that SDL reports up gets its key-up now.
void Reconcile()
{
	if (gHeld <= 0)
		return;
	int n = 0;
	const bool *state = SDL_GetKeyboardState(&n);
	for (unsigned s = 1; s < 0x200 && gHeld > 0; s++)
	{
		HeldKey &k = gKeys[s];
		if (k.down && !(state && (int)k.sdl < n && state[k.sdl]) && !gFakeHeld[k.sdl])
			SyntheticKeyUp(s, "SDL reports it up");
	}
}

// The character a key-down makes, as TranslateMessage would: control characters for return, backspace, escape and tab,
// and printable ASCII from the key's position, the layout and Shift/Caps Lock (no text input, no input method: game keys
// never go through one). 0 for none.
unsigned KeyChar(UINT vk, SDL_Scancode sc, SDL_Keymod mod)
{
	if (vk == VK_RETURN) return '\r';
	if (vk == VK_BACK) return '\b';
	if (vk == VK_ESCAPE) return 27;
	if (vk == VK_TAB) return '\t';
	if (mod & (SDL_KMOD_CTRL | SDL_KMOD_LALT | SDL_KMOD_GUI))
		return 0;
	SDL_Keycode c = SDL_GetKeyFromScancode(sc, mod, false);
	return c >= 0x20 && c < 0x7f ? (unsigned)c : 0;
}

void KeyEvent(const SDL_KeyboardEvent &e, HWND h)
{
	bool down = e.type == SDL_EVENT_KEY_DOWN;
	UINT vk = VirtualKey(e.key, e.scancode);
	unsigned scan = PcScan(e.scancode) & 0x1ff;
	if (InputLog())
		fprintf(stderr, PORT_LOG_PREFIX "input: SDL key %s%s %s scancode %d keycode %08x mod %04x -> vk %02x scan %03x\n",
				down ? "down" : "up", e.repeat ? " (repeat)" : "", SDL_GetScancodeName(e.scancode), (int)e.scancode,
				(unsigned)e.key, (unsigned)e.mod, vk, scan);
	bool recording = PortRecording();
	if (recording && e.repeat)
		return; // Tapes contain transitions, not the operating system's key repeat.
	HeldKey *k = scan ? &gKeys[scan] : NULL;
	if (down)
	{
		if (!vk)
		{
			if (InputLog())
				fprintf(stderr, PORT_LOG_PREFIX "input: key down %s dropped (no virtual key)\n", SDL_GetScancodeName(e.scancode));
			return;
		}
		LPARAM l = KeyLParam(scan, false, e.repeat);
		if (recording)
			PortRecordKey(true, vk, scan);
		else
			Post(h, WM_KEYDOWN, vk, l);
		if (k)
		{
			if (!k->down)
				gHeld++;
			k->down = true;
			k->released = false;
			k->vk = vk;
			k->sdl = e.scancode;
		}
		if (unsigned c = KeyChar(vk, e.scancode, e.mod))
		{
			if (recording)
				PortRecordChar(c, scan);
			else
				Post(h, WM_CHAR, c, l);
		}
		return;
	}
	// A key-up: always for a key the port posted down (with the virtual key it went down with), never twice.
	if (k && k->down)
	{
		if (recording)
			PortRecordKey(false, k->vk, scan);
		else
			Post(h, WM_KEYUP, k->vk, KeyLParam(scan, true, false));
		k->down = false;
		gHeld--;
	}
	else if (k && k->released)
	{
		if (InputLog())
			fprintf(stderr, PORT_LOG_PREFIX "input: key up %s not posted (already released)\n", SDL_GetScancodeName(e.scancode));
	}
	else if (vk)
	{
		// A key that went down before the port saw it.
		if (recording)
			PortRecordKey(false, vk, scan);
		else
			Post(h, WM_KEYUP, vk, KeyLParam(scan, true, false));
	}
	else if (InputLog())
		fprintf(stderr, PORT_LOG_PREFIX "input: key up %s dropped (no virtual key, not held)\n", SDL_GetScancodeName(e.scancode));
	if (k)
		k->released = false;
}

extern "C" char LT[];	// PCLTShell (ltshell.cpp): KeyDown[256] at +0x330e4, as retail's (gdb on the Linux build)

// Turns pending SDL events into messages.
void Pump()
{
	// BEA_PORT_FAKE_CLICK=SECONDS:X,Y (a test switch): after SECONDS, one left click at window point X,Y, through the
	// same event path as the player's mouse.
	static int fakeState = 0;
	if (fakeState == 0)
		fakeState = getenv("BEA_PORT_FAKE_CLICK") ? 1 : 2;
	if (fakeState == 1)
	{
		float secs = 0, fx = 0, fy = 0;
		if (sscanf(getenv("BEA_PORT_FAKE_CLICK"), "%f:%f,%f", &secs, &fx, &fy) == 3 && SDL_GetTicks() >= (Uint64)(secs * 1000))
		{
			SDL_Event f{};
			f.type = SDL_EVENT_MOUSE_MOTION; f.motion.x = fx; f.motion.y = fy; SDL_PushEvent(&f);
			f = SDL_Event{}; f.type = SDL_EVENT_MOUSE_BUTTON_DOWN; f.button.button = SDL_BUTTON_LEFT; f.button.down = true;
			f.button.x = fx; f.button.y = fy; SDL_PushEvent(&f);
			f = SDL_Event{}; f.type = SDL_EVENT_MOUSE_BUTTON_UP; f.button.button = SDL_BUTTON_LEFT; f.button.x = fx; f.button.y = fy;
			SDL_PushEvent(&f);
			fprintf(stderr, PORT_LOG_PREFIX "test click at %.0f,%.0f\n", fx, fy);
			fakeState = 2;
		}
	}
	// BEA_PORT_FAKE_KEYS=PRESENT:SDLSCAN:RAWSCAN:d|u|x[,...] (a test switch, for record mode's and the input path's
	// checks): once PRESENT Presents have completed, an SDL key event through the same path as the player's keyboard
		// (x: the key goes up in SDL's state but its event is lost); kPRESENT:SDLSCAN:MOD a key-down with modifier state MOD
	// (hex SDL_Keymod) and its key-up; mPRESENT:DX:DY a relative mouse movement; aPRESENT:X:Y a mouse movement to window
	// point X,Y; bPRESENT:BUTTON:d|u[:X:Y] a mouse button (at window point X,Y);
	// sPRESENT:N:SDLSCAN a key-down, N mouse movements and the key-up, all queued before one pump (a backlog; wPRESENT:N:SDLSCAN
	// the same with N zero wheel steps, which are not coalesced);
	// cPRESENT:SCAN logs the game's KeyDown[SCAN] (hex PC scan code).
	static const char *fakeKeys = getenv("BEA_PORT_FAKE_KEYS");
	if (fakeKeys && *fakeKeys)
	{
		unsigned present = 0, sc = 0, raw = 0, n = 0;
		char ud = 0;
		int mdx = 0, mdy = 0;
		bool used = false;
		float ax = 0, ay = 0;
		if (sscanf(fakeKeys, "a%u:%f:%f", &present, &ax, &ay) == 3 && PortPresentCount() >= present)
		{
			SDL_Event f{};	// an absolute mouse movement (the window mouse: menus)
			f.type = SDL_EVENT_MOUSE_MOTION;
			f.motion.x = ax;
			f.motion.y = ay;
			SDL_PushEvent(&f);
			used = true;
		}
		else if (sscanf(fakeKeys, "m%u:%d:%d", &present, &mdx, &mdy) == 3 && PortPresentCount() >= present)
		{
			SDL_Event f{};	// a relative mouse movement (record mode's checks)
			f.type = SDL_EVENT_MOUSE_MOTION;
			f.motion.xrel = (float)mdx;
			f.motion.yrel = (float)mdy;
			SDL_PushEvent(&f);
			used = true;
		}
		else if (sscanf(fakeKeys, "b%u:%u:%c", &present, &sc, &ud) == 3 && PortPresentCount() >= present)
		{
			SDL_Event f{};	// a mouse button (1 left, 3 right)
			f.type = ud == 'd' ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
			f.button.button = (Uint8)sc;
			f.button.down = ud == 'd';
			unsigned p2 = 0, s2 = 0;
			char u2 = 0;
			if (sscanf(fakeKeys, "b%u:%u:%c:%f:%f", &p2, &s2, &u2, &ax, &ay) == 5)
			{
				f.button.x = ax;
				f.button.y = ay;
			}
			SDL_PushEvent(&f);
			used = true;
		}
		else if (sscanf(fakeKeys, "k%u:%u:%x", &present, &sc, &raw) == 3 && PortPresentCount() >= present)
		{
			for (int i = 0; i < 2; i++)
			{
				SDL_Event f{};	// a typed key with modifiers (Shift: 3, Caps Lock: 2000)
				f.type = i == 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
				f.key.scancode = (SDL_Scancode)sc;
				f.key.key = SDL_GetKeyFromScancode((SDL_Scancode)sc, SDL_KMOD_NONE, false);
				f.key.mod = (SDL_Keymod)raw;
				f.key.down = i == 0;
				SDL_PushEvent(&f);
			}
			used = true;
		}
		else if ((sscanf(fakeKeys, "s%u:%u:%u", &present, &n, &sc) == 3 || sscanf(fakeKeys, "w%u:%u:%u", &present, &n, &sc) == 3)
				 && PortPresentCount() >= present)
		{
			bool wheel = fakeKeys[0] == 'w';	// zero wheel steps: one message each, never coalesced (fills the ring)
			SDL_Event f{};
			f.type = SDL_EVENT_KEY_DOWN;
			f.key.scancode = (SDL_Scancode)sc;
			f.key.key = SDL_GetKeyFromScancode((SDL_Scancode)sc, SDL_KMOD_NONE, false);
			f.key.down = true;
			SDL_PushEvent(&f);
			for (unsigned i = 0; i < n; i++)
			{
				SDL_Event m{};
				if (wheel)
					m.type = SDL_EVENT_MOUSE_WHEEL;
				else
				{
					m.type = SDL_EVENT_MOUSE_MOTION;
					m.motion.x = (float)(i % 640);
					m.motion.y = (float)(i % 480);
					m.motion.xrel = 1;
				}
				SDL_PushEvent(&m);
			}
			f.type = SDL_EVENT_KEY_UP;
			f.key.down = false;
			SDL_PushEvent(&f);
			fprintf(stderr, PORT_LOG_PREFIX "test: backlog of a key-down, %u mouse %s and the key-up\n", n, wheel ? "wheel events" : "movements");
			used = true;
		}
		else if (sscanf(fakeKeys, "c%u:%x", &present, &sc) == 2 && PortPresentCount() >= present)
		{
			fprintf(stderr, PORT_LOG_PREFIX "test: at Present %u KeyDown[%02x] = %d\n", PortPresentCount(), sc & 0xff,
					(int)(unsigned char)LT[0x330e4 + (sc & 0xff)]);
			used = true;
		}
		else if (sscanf(fakeKeys, "%u:%u:%x:%c", &present, &sc, &raw, &ud) == 4 && PortPresentCount() >= present)
		{
			if (sc < SDL_SCANCODE_COUNT)
				gFakeHeld[sc] = ud == 'd';
			if (ud != 'x')
			{
				SDL_Event f{};
				f.type = ud == 'd' ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
				f.key.scancode = (SDL_Scancode)sc;
				f.key.key = SDL_GetKeyFromScancode((SDL_Scancode)sc, SDL_KMOD_NONE, false);
				f.key.raw = (Uint16)raw;
				f.key.down = ud == 'd';
				SDL_PushEvent(&f);
			}
			used = true;
		}
		if (used)
		{
			const char *next = strchr(fakeKeys, ',');
			fakeKeys = next ? next + 1 : "";
		}
	}
	// The input and audio inventories (inventory.cpp), once each, as soon as their SDL subsystems are up.
	if (PortSessionEnabled())
	{
		static bool inputListed, audioListed;
		if (!inputListed && SDL_WasInit(SDL_INIT_GAMEPAD))
		{
			inputListed = true;
			PortSessionInputInventory();
		}
		if (!audioListed && SDL_WasInit(SDL_INIT_AUDIO))
		{
			audioListed = true;
			PortSessionAudioInventory();
		}
	}
	PortMouseKeepRelative();
	// Keys left held by a dropped message go up once the game has read the ring.
	if (gReleasePending && Free() > 64)
	{
		gReleasePending = false;
		ReleaseAll("after a dropped message");
	}
	if (gReleasePending)
		return;	// finish the older key lifetimes before admitting a new press after focus recovery
	SDL_Event e;
	static SDL_Event deferred;
	static bool haveDeferred;
	// SDL's own queue holds the rest of a backlog (tens of thousands of events): stop short of filling the ring, so the
	// player's events are delayed to the next pump, never dropped.
	while (Free() > 16)
	{
		if (haveDeferred)
			e = deferred;
		else if (!SDL_PollEvent(&e))
			break;
		// Hold at most one dequeued event and leave the SDL tail untouched. Normal input keeps the existing pump;
		// after capture failure, the same bounded record-mode delivery resumes when the next Present frees space.
		unsigned slots = e.type == SDL_EVENT_KEY_UP ? 1 :
			e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_MOUSE_MOTION ||
			e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP ||
			e.type == SDL_EVENT_MOUSE_WHEEL ? 2 : 0;
		if (slots && PortRecording() && !PortRecordInputSpace(slots))
		{
			deferred = e;
			haveDeferred = true;
			break;
		}
		haveDeferred = false;
		HWND h = MainWindow();
		switch (e.type)
		{
		case SDL_EVENT_QUIT:
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			PortSessionLog(PORT_SESSION_WINDOW, false, "close requested event=%u", (unsigned)e.type);
			Post(h, WM_CLOSE, 0, 0);
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			PortSessionLog(PORT_SESSION_WINDOW, false, "focus gained");
			if (InputLog()) fprintf(stderr, PORT_LOG_PREFIX "input: focus gained\n");
			Post(h, WM_ACTIVATEAPP, TRUE, 0);
			Post(h, WM_ACTIVATE, 1, 0);
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			PortSessionLog(PORT_SESSION_WINDOW, false, "focus lost");
			if (InputLog()) fprintf(stderr, PORT_LOG_PREFIX "input: focus lost\n");
			ReleaseAll("focus lost");
			if (PortRecording())
				break;	// a tape has no focus changes
			Post(h, WM_ACTIVATEAPP, FALSE, 0);
			Post(h, WM_ACTIVATE, 0, 0);
			break;
		case SDL_EVENT_WINDOW_RESIZED:
			PortSessionLog(PORT_SESSION_WINDOW, false, "logical size=%dx%d", e.window.data1, e.window.data2);
			if (!PortWideModes())	// HiDPI windows report their size in pixels below, as the client rectangle is
				Post(h, WM_SIZE, SIZE_RESTORED, MAKELPARAM(e.window.data1, e.window.data2));
			break;
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			PortSessionLog(PORT_SESSION_WINDOW, false, "pixel size=%dx%d", e.window.data1, e.window.data2);
			if (PortWideModes())
				Post(h, WM_SIZE, SIZE_RESTORED, MAKELPARAM(e.window.data1, e.window.data2));
			break;
		case SDL_EVENT_WINDOW_MINIMIZED:
			PortSessionLog(PORT_SESSION_WINDOW, false, "minimized");
			Post(h, WM_SIZE, SIZE_MINIMIZED, 0);
			break;
		case SDL_EVENT_WINDOW_RESTORED:
		case SDL_EVENT_WINDOW_MAXIMIZED:
		{
			// A same-size restore need not produce a resize event. The game's WM_SIZE handler is what makes it
			// active again after minimization; focus messages alone leave its message loop waiting forever.
			int width = 0, height = 0;
			if (SDL_Window *w = Sdl(h))
				SDL_GetWindowSizeInPixels(w, &width, &height);
			PortSessionLog(PORT_SESSION_WINDOW, false, "%s pixel size=%dx%d",
				e.type == SDL_EVENT_WINDOW_MAXIMIZED ? "maximized" : "restored", width, height);
			Post(h, WM_SIZE, e.type == SDL_EVENT_WINDOW_MAXIMIZED ? SIZE_MAXIMIZED : SIZE_RESTORED,
				 MAKELPARAM(width, height));
			break;
		}
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			KeyEvent(e.key, h);
			break;
		case SDL_EVENT_MOUSE_MOTION:
			if (PortRecording())
			{
				// record mode: DirectInput-style movement and the window message, one of which the tape keeps
				PortRecordMotion(e.motion.xrel, e.motion.yrel);
				PortRecordWindowMouse(WM_MOUSEMOVE, MouseLParam(e.motion.x, e.motion.y), 0);
				break;
			}
			if (PortDinputOwnsMouseEvent(e.common.timestamp))
				break;	// exclusive DirectInput owns game mouse input; recording makes the same choice at Present
			PostMouseMove(h, MouseButtons(), MouseLParam(e.motion.x, e.motion.y));
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
		{
			if (PortRecording())
			{
				int bit = e.button.button == SDL_BUTTON_LEFT ? 0 : e.button.button == SDL_BUTTON_RIGHT ? 1 : e.button.button == SDL_BUTTON_MIDDLE ? 2 : -1;
				bool dn = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
				if (bit >= 0)
				{
					static const UINT msgs[3][2] = {{WM_LBUTTONUP, WM_LBUTTONDOWN}, {WM_RBUTTONUP, WM_RBUTTONDOWN}, {WM_MBUTTONUP, WM_MBUTTONDOWN}};
					PortRecordButton(bit, dn);
					PortRecordWindowMouse(msgs[bit][dn], MouseLParam(e.button.x, e.button.y), 0);
				}
				break;
			}
			if (PortDinputOwnsMouseEvent(e.common.timestamp))
				break;
			bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
			UINT msg = 0;
			if (e.button.button == SDL_BUTTON_LEFT) msg = down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
			else if (e.button.button == SDL_BUTTON_RIGHT) msg = down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
			else if (e.button.button == SDL_BUTTON_MIDDLE) msg = down ? WM_MBUTTONDOWN : WM_MBUTTONUP;
			if (msg)
				Post(h, msg, MouseButtons(), MouseLParam(e.button.x, e.button.y));
			break;
		}
		case SDL_EVENT_MOUSE_WHEEL:
			if (PortRecording())
			{
				PortRecordWheel(e.wheel.y);
				PortRecordWindowMouse(WM_MOUSEWHEEL, MouseLParam(e.wheel.mouse_x, e.wheel.mouse_y), (short)(e.wheel.y * WHEEL_DELTA));
				break;
			}
			if (PortDinputOwnsMouseEvent(e.common.timestamp))
				break;	// otherwise the window message and DirectInput both add this wheel delta
			Post(h, WM_MOUSEWHEEL, MAKEWPARAM(MouseButtons(), (short)(e.wheel.y * WHEEL_DELTA)), 0);
			break;
		default:
			break;
		}
	}
	Reconcile();
}
}

extern "C" ATOM WINAPI PortRegisterClassA(const WNDCLASSA *wc)
{
	if (gNumClasses >= 8)
		return 0;
	WindowClass &c = gClasses[gNumClasses++];
	snprintf(c.name, sizeof(c.name), "%s", wc->lpszClassName ? wc->lpszClassName : "");
	c.proc = wc->lpfnWndProc;
	c.cursor = wc->hCursor;
	return (ATOM)gNumClasses;
}

extern "C" BOOL WINAPI PortUnregisterClassA(LPCSTR, HINSTANCE) { return TRUE; }

extern "C" HWND WINAPI PortCreateWindowExA(DWORD exstyle, LPCSTR cls, LPCSTR title, DWORD style, int, int, int w, int h, HWND,
									   HMENU, HINSTANCE, LPVOID)
{
	if (gNumWindows >= 8)
		return NULL;
	PortVideoInit();
	if (!SDL_WasInit(SDL_INIT_VIDEO))
		return NULL;
	// Test runs set BEA_PORT_REQUIRE_OFFSCREEN so the game can never open a window on the desktop (port/tools/run.py).
	const char *driver = SDL_GetCurrentVideoDriver();
	fprintf(stderr, PORT_LOG_PREFIX "SDL video driver %s\n", driver ? driver : "(none)");
	const char *require = getenv("BEA_PORT_REQUIRE_OFFSCREEN");
	if (require && *require && *require != '0' && (!driver || strcmp(driver, "offscreen") != 0))
	{
		fprintf(stderr, PORT_LOG_PREFIX "refusing a window on video driver %s (BEA_PORT_REQUIRE_OFFSCREEN)\n", driver ? driver : "(none)");
		exit(3);
	}
	WNDPROC proc = NULL;
	for (int i = 0; i < gNumClasses; i++)
		if (strcasecmp(gClasses[i].name, cls) == 0)
			proc = gClasses[i].proc;
	if (w <= 0 || w == CW_USEDEFAULT)
		w = 640;
	if (h <= 0 || h == CW_USEDEFAULT)
		h = 480;
#if defined(_WIN32)
	SDL_WindowFlags flags = 0;			// Direct3D 9 draws on the window's Win32 handle
#else
	SDL_WindowFlags flags = SDL_WINDOW_VULKAN;	// dxvk-native draws through Vulkan
#endif
	if (!(style & WS_VISIBLE))
		flags |= SDL_WINDOW_HIDDEN;
	if (PortWideModes())
		flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;	// a HiDPI desktop's real pixels (never offscreen, in replays or recordings)
	SDL_Window *sdl = SDL_CreateWindow(title ? title : "Battle Engine Aquila", w, h, flags);
	if (!sdl)
	{
		fprintf(stderr, PORT_LOG_PREFIX "SDL_CreateWindow: %s\n", SDL_GetError());
		return NULL;
	}
	// No SDL text input: it would route every game key through the desktop's input method (an IME's keyboard grab can
	// lose or delay key-ups); WM_CHAR is made from key-downs instead (KeyChar), as TranslateMessage does.
	Window &win = gWindows[gNumWindows++];
	memset(&win, 0, sizeof(win));
	win.sdl = sdl;
#if defined(_WIN32)
	win.handle = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(sdl), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#else
	win.handle = (HWND)sdl;
#endif
	win.proc = proc;
	win.style = (LONG)style;
	win.exstyle = (LONG)exstyle;
	if (PortSessionEnabled())
	{
		int count = 0, logicalW = 0, logicalH = 0, pixelW = 0, pixelH = 0;
		SDL_DisplayID *displays = SDL_GetDisplays(&count);
		SDL_free(displays);
		SDL_DisplayID display = SDL_GetDisplayForWindow(sdl);
		SDL_Rect bounds{};
		bool haveBounds = SDL_GetDisplayBounds(display, &bounds);
		bool haveLogical = SDL_GetWindowSize(sdl, &logicalW, &logicalH);
		bool havePixels = SDL_GetWindowSizeInPixels(sdl, &pixelW, &pixelH);
		float scale = SDL_GetWindowDisplayScale(sdl);
		unsigned scaleBits;
		memcpy(&scaleBits, &scale, sizeof scaleBits);
		PortSessionLog(PORT_SESSION_WINDOW, false,
			"displays=%d selected_display=%u bounds_available=%d bounds=%d,%d,%d,%d "
			"logical_available=%d logical_size=%dx%d pixels_available=%d pixel_size=%dx%d display_scale_ieee754=%08x",
			count, (unsigned)display, haveBounds, bounds.x, bounds.y, bounds.w, bounds.h,
			haveLogical, logicalW, logicalH, havePixels, pixelW, pixelH, scaleBits);
		static bool inventoried;
		if (!inventoried)
		{
			inventoried = true;
			PortSessionDisplayInventory(sdl);
			PortSessionSettingsInventory();
		}
	}
	return win.handle;
}

extern "C" BOOL WINAPI PortDestroyWindow(HWND h)
{
	WIN32_PASSTHROUGH(DestroyWindow(h))
	Window *w = Find(h);
	if (!w)
		return FALSE;
	if (w->proc)
		w->proc(h, WM_DESTROY, 0, 0);
	SDL_DestroyWindow(w->sdl);
	*w = gWindows[--gNumWindows];
	return TRUE;
}

extern "C" BOOL WINAPI PortShowWindow(HWND h, int cmd)
{
	WIN32_PASSTHROUGH(ShowWindow(h, cmd))
	SDL_Window *w = Sdl(h);
	if (!Find(h))
		return FALSE;
	if (cmd == SW_HIDE)
		SDL_HideWindow(w);
	else if (cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED)
		SDL_MinimizeWindow(w);
	else
		SDL_ShowWindow(w);
	return TRUE;
}

extern "C" BOOL WINAPI PortUpdateWindow(HWND) { return TRUE; }
extern "C" HWND WINAPI PortSetActiveWindow(HWND h) { return h; }
extern "C" HWND WINAPI PortSetFocus(HWND h) { return h; }
extern "C" HWND WINAPI PortGetFocus(void) { return gNumWindows ? gWindows[0].handle : NULL; }
extern "C" HWND WINAPI PortGetForegroundWindow(void) { return PortGetFocus(); }
extern "C" BOOL WINAPI PortSetForegroundWindow(HWND) { return TRUE; }
extern "C" BOOL WINAPI PortIsIconic(HWND h)
{
	return Find(h) && (SDL_GetWindowFlags(Sdl(h)) & SDL_WINDOW_MINIMIZED) != 0;
}
extern "C" BOOL WINAPI PortIsZoomed(HWND h)
{
	return Find(h) && (SDL_GetWindowFlags(Sdl(h)) & SDL_WINDOW_MAXIMIZED) != 0;
}
extern "C" BOOL WINAPI PortIsWindowVisible(HWND h)
{
	return Find(h) && !(SDL_GetWindowFlags(Sdl(h)) & SDL_WINDOW_HIDDEN);
}

extern "C" LRESULT WINAPI PortDefWindowProcA(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	WIN32_PASSTHROUGH(DefWindowProcA(h, msg, wp, lp))
	(void)wp;
	(void)lp;
	if (msg == WM_CLOSE)
		PortDestroyWindow(h);
	return 0;
}

extern "C" LRESULT WINAPI PortCallWindowProcA(WNDPROC proc, HWND h, UINT m, WPARAM w, LPARAM l) { return proc(h, m, w, l); }

extern "C" BOOL WINAPI PortPeekMessageA(LPMSG msg, HWND, UINT, UINT, UINT remove)
{
	Pump();
	return Pop(msg, (remove & PM_REMOVE) != 0);
}

extern "C" BOOL WINAPI PortGetMessageA(LPMSG msg, HWND, UINT, UINT)
{
	for (;;)
	{
		Pump();
		if (Pop(msg, true))
			return msg->message != WM_QUIT;
		SDL_WaitEventTimeout(NULL, 10);
	}
}

extern "C" BOOL WINAPI PortTranslateMessage(const MSG *) { return FALSE; }

extern "C" LRESULT WINAPI PortDispatchMessageA(const MSG *msg)
{
	Window *w = Find(msg->hwnd);
	if (w && w->proc)
		return w->proc(msg->hwnd, msg->message, msg->wParam, msg->lParam);
	return 0;
}

extern "C" BOOL WINAPI PortPostMessageA(HWND h, UINT m, WPARAM w, LPARAM l)
{
	Post(h, m, w, l);
	return TRUE;
}

extern "C" BOOL WINAPI PortTryPostMessageA(HWND h, UINT m, WPARAM w, LPARAM l)
{
	// Recorder FIFO admission must not drop a message or request physical-key repair when the ring is full.
	return Free() > 0 && Post(h, m, w, l) ? TRUE : FALSE;
}

extern "C" LRESULT WINAPI PortSendMessageA(HWND h, UINT m, WPARAM w, LPARAM l)
{
	WIN32_PASSTHROUGH(SendMessageA(h, m, w, l))
	Window *win = Find(h);
	// Dialog controls (the device dialog's combo boxes) do not exist; their queries answer "none".
	if (!win)
		return m == CB_GETCURSEL || m == CB_ADDSTRING ? CB_ERR : 0;
	return win->proc ? win->proc(h, m, w, l) : 0;
}

extern "C" void WINAPI PortPostQuitMessage(int code) { Post(NULL, WM_QUIT, (WPARAM)code, 0); }

extern "C" BOOL WINAPI PortGetClientRect(HWND h, LPRECT r)
{
	WIN32_PASSTHROUGH(GetClientRect(h, r))
	int w = 0, ht = 0;
	if (Find(h))
		SDL_GetWindowSizeInPixels(Sdl(h), &w, &ht);
	r->left = r->top = 0;
	r->right = w;
	r->bottom = ht;
	return TRUE;
}

extern "C" BOOL WINAPI PortGetWindowRect(HWND h, LPRECT r)
{
	WIN32_PASSTHROUGH(GetWindowRect(h, r))
	int x = 0, y = 0, w = 0, ht = 0;
	if (Find(h))
	{
		SDL_GetWindowPosition(Sdl(h), &x, &y);
		SDL_GetWindowSize(Sdl(h), &w, &ht);
	}
	r->left = x;
	r->top = y;
	r->right = x + w;
	r->bottom = y + ht;
	return TRUE;
}

extern "C" BOOL WINAPI PortAdjustWindowRect(LPRECT, DWORD, BOOL) { return TRUE; }
extern "C" BOOL WINAPI PortAdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD) { return TRUE; }

extern "C" BOOL WINAPI PortSetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags)
{
	WIN32_PASSTHROUGH(SetWindowPos(h, after, x, y, cx, cy, flags))
	(void)after;
	if (!Find(h))
		return FALSE;
	if (!(flags & SWP_NOMOVE))
		SDL_SetWindowPosition(Sdl(h), x, y);
	if (!(flags & SWP_NOSIZE) && cx > 0 && cy > 0)
		SDL_SetWindowSize(Sdl(h), cx, cy);
	if (flags & SWP_SHOWWINDOW)
		SDL_ShowWindow(Sdl(h));
	if (flags & SWP_HIDEWINDOW)
		SDL_HideWindow(Sdl(h));
	return TRUE;
}

extern "C" BOOL WINAPI PortMoveWindow(HWND h, int x, int y, int w, int ht, BOOL)
{
	return PortSetWindowPos(h, NULL, x, y, w, ht, 0);
}

extern "C" LONG WINAPI PortGetWindowLongA(HWND h, int index)
{
	WIN32_PASSTHROUGH(GetWindowLongA(h, index))
	Window *w = Find(h);
	if (!w)
		return 0;
	switch (index)
	{
	case GWL_STYLE: return w->style;
	case GWL_EXSTYLE: return w->exstyle;
	case GWL_USERDATA: return (LONG)w->userdata;
	case GWL_WNDPROC: return (LONG)(intptr_t)w->proc;
	default: return 0;
	}
}

extern "C" LONG WINAPI PortSetWindowLongA(HWND h, int index, LONG value)
{
	WIN32_PASSTHROUGH(SetWindowLongA(h, index, value))
	Window *w = Find(h);
	if (!w)
		return 0;
	LONG old = PortGetWindowLongA(h, index);
	switch (index)
	{
	case GWL_STYLE:
		w->style = value;
		SDL_SetWindowBordered(w->sdl, (value & WS_CAPTION) != 0);
		break;
	case GWL_EXSTYLE: w->exstyle = value; break;
	case GWL_USERDATA: w->userdata = value; break;
	case GWL_WNDPROC: w->proc = (WNDPROC)(intptr_t)value; break;
	}
	return old;
}

extern "C" DWORD WINAPI PortGetClassLongA(HWND, int) { return 0; }
extern "C" DWORD WINAPI PortSetClassLongA(HWND, int, LONG) { return 0; }
extern "C" BOOL WINAPI PortSetWindowTextA(HWND h, LPCSTR s)
{
	if (Find(h))
		SDL_SetWindowTitle(Sdl(h), s);
	return TRUE;
}

// ---- cursor, keyboard ---------------------------------------------------------------------------------------------
static int sCursorCount = 0;

extern "C" HCURSOR WINAPI PortSetCursor(HCURSOR c) { return c; }
extern "C" int WINAPI PortShowCursor(BOOL show)
{
	sCursorCount += show ? 1 : -1;
	if (SDL_WasInit(SDL_INIT_VIDEO))
	{
		if (sCursorCount >= 0)
			SDL_ShowCursor();
		else
			SDL_HideCursor();
	}
	return sCursorCount;
}

extern "C" BOOL WINAPI PortGetCursorPos(LPPOINT p)
{
	if (PortTapeCursor(p))
		return TRUE;	// a tape-2 replay or a recording: the tape's cursor, never the desktop's
	float x = 0, y = 0;
	if (SDL_WasInit(SDL_INIT_VIDEO))
		SDL_GetGlobalMouseState(&x, &y);
	p->x = (LONG)x;
	p->y = (LONG)y;
	return TRUE;
}

// ShutdownMouse needs the cursor in the game's space after releasing relative mode, not a desktop position whose
// origin can be another monitor. Use the same mapping as mouse messages; a tape already supplies game coordinates.
extern "C" BOOL PortGetMouseClientPosition(LPPOINT p)
{
	if (PortTapeCursor(p))
		return TRUE;
	if (!gNumWindows || SDL_GetMouseFocus() != gWindows[0].sdl)
		return FALSE;
	float x = 0, y = 0;
	SDL_GetMouseState(&x, &y);
	LPARAM mapped = MouseLParam(x, y);
	p->x = LOWORD(mapped);
	p->y = HIWORD(mapped);
	return TRUE;
}

extern "C" BOOL WINAPI PortSetCursorPos(int x, int y)
{
	if (SDL_WasInit(SDL_INIT_VIDEO))
		SDL_WarpMouseGlobal((float)x, (float)y);
	return TRUE;
}

extern "C" BOOL WINAPI PortClipCursor(const RECT *) { return TRUE; }

extern "C" BOOL WINAPI PortScreenToClient(HWND h, LPPOINT p)
{
	WIN32_PASSTHROUGH(ScreenToClient(h, p))
	int x = 0, y = 0;
	if (Find(h) && !PortTapeCursor(NULL))
		SDL_GetWindowPosition(Sdl(h), &x, &y);
	p->x -= x;
	p->y -= y;
	return TRUE;
}

extern "C" BOOL WINAPI PortClientToScreen(HWND h, LPPOINT p)
{
	WIN32_PASSTHROUGH(ClientToScreen(h, p))
	int x = 0, y = 0;
	if (Find(h) && !PortTapeCursor(NULL))
		SDL_GetWindowPosition(Sdl(h), &x, &y);
	p->x += x;
	p->y += y;
	return TRUE;
}

// ---- the platform layer's view of the windows ---------------------------------------------------------------------------
SDL_Window *PortSdlWindow(HWND hwnd) { return Sdl(hwnd); }
SDL_Window *PortMainSdlWindow() { return gNumWindows ? gWindows[0].sdl : NULL; }
void PortPumpInput() { SDL_PumpEvents(); }
