// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/inventory.cpp: one-off ordinary-session inventories for problem reports (port/platform/common/
// portlog.h): every display, the input devices and the audio playback devices SDL sees, and the CE settings that
// change how the game runs. Each is written once, through PortSessionLog, so it inherits the session log's limits and
// stays off in faithful, replay and recording runs. Names are what the system reports for the hardware: printable
// ASCII only, at most 48 characters, and the account name is written <user> wherever it appears.
#include "portlog.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Weak, so host test harnesses that compile window.cpp without ce.cpp still link; the game always defines it.
extern "C" int CE_SettingGet(const char *key, int def) __attribute__((weak));

namespace
{
const int kMaxListed = 8;

const char *Account()
{
#ifdef _WIN32
	const char *name = getenv("USERNAME");
#else
	const char *name = getenv("USER");
	if (!name || !*name)
		name = getenv("LOGNAME");
#endif
	return name && strlen(name) > 1 ? name : NULL;
}

bool SameNoCase(const char *a, const char *b, size_t n)
{
	for (size_t i = 0; i < n; ++i)
		if (!a[i] || tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
			return false;
	return true;
}

// A device or display name fit for a public report.
void Clean(const char *in, char *out, size_t n)
{
	const char *account = Account();
	size_t al = account ? strlen(account) : 0, o = 0;
	for (const char *p = in ? in : ""; *p && o + 1 < n;)
	{
		if (al && SameNoCase(p, account, al))
		{
			const char *tag = "<user>";
			for (const char *t = tag; *t && o + 1 < n; ++t)
				out[o++] = *t;
			p += al;
			continue;
		}
		unsigned char c = (unsigned char)*p++;
		out[o++] = c >= 0x20 && c < 0x7f ? (char)c : '?';
	}
	out[o] = 0;
	if (!o)
		strcpy(out, "-");
}
}

// Every display: its bounds, current mode and refresh, content scale, and which one is primary and which the game's
// window is on. Called once, from window creation (window.cpp).
void PortSessionDisplayInventory(SDL_Window *window)
{
	if (!PortSessionEnabled())
		return;
	int count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&count);
	SDL_DisplayID primary = SDL_GetPrimaryDisplay(), used = window ? SDL_GetDisplayForWindow(window) : 0;
	for (int i = 0; displays && i < count && i < kMaxListed; ++i)
	{
		char name[49];
		Clean(SDL_GetDisplayName(displays[i]), name, sizeof name);
		SDL_Rect bounds{}, usable{};
		bool haveBounds = SDL_GetDisplayBounds(displays[i], &bounds);
		bool haveUsable = SDL_GetDisplayUsableBounds(displays[i], &usable);
		const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(displays[i]);
		int modes = 0;
		SDL_DisplayMode **list = SDL_GetFullscreenDisplayModes(displays[i], &modes);
		SDL_free(list);
		PortSessionLog(PORT_SESSION_WINDOW, false,
			"display %d/%d id=%u name=\"%s\" primary=%d game_window=%d bounds=%s%d,%d,%d,%d usable=%s%d,%d,%d,%d "
			"mode=%dx%d@%.2fHz pixel_density=%.2f content_scale=%.2f fullscreen_modes=%d",
			i + 1, count, (unsigned)displays[i], name, displays[i] == primary, displays[i] == used,
			haveBounds ? "" : "unavailable:", bounds.x, bounds.y, bounds.w, bounds.h,
			haveUsable ? "" : "unavailable:", usable.x, usable.y, usable.w, usable.h,
			mode ? mode->w : 0, mode ? mode->h : 0, mode ? (double)mode->refresh_rate : 0.0,
			mode ? (double)mode->pixel_density : 0.0, (double)SDL_GetDisplayContentScale(displays[i]), modes);
	}
	if (count > kMaxListed)
		PortSessionLog(PORT_SESSION_WINDOW, false, "display list truncated after %d of %d", kMaxListed, count);
	SDL_free(displays);
}

// The keyboards, mice and game controllers SDL sees. Called once, after the gamepad subsystem starts (dinput.cpp).
void PortSessionInputInventory()
{
	if (!PortSessionEnabled())
		return;
	int keyboards = 0, mice = 0, joysticks = 0;
	SDL_KeyboardID *k = SDL_GetKeyboards(&keyboards);
	SDL_MouseID *m = SDL_GetMice(&mice);
	SDL_JoystickID *j = SDL_GetJoysticks(&joysticks);
	PortSessionLog(PORT_SESSION_WINDOW, false, "input keyboards=%d mice=%d controllers=%d", keyboards, mice, joysticks);
	char name[49];
	for (int i = 0; k && i < keyboards && i < kMaxListed; ++i)
	{
		Clean(SDL_GetKeyboardNameForID(k[i]), name, sizeof name);
		PortSessionLog(PORT_SESSION_WINDOW, false, "input keyboard %d name=\"%s\"", i + 1, name);
	}
	for (int i = 0; m && i < mice && i < kMaxListed; ++i)
	{
		Clean(SDL_GetMouseNameForID(m[i]), name, sizeof name);
		PortSessionLog(PORT_SESSION_WINDOW, false, "input mouse %d name=\"%s\"", i + 1, name);
	}
	for (int i = 0; j && i < joysticks && i < kMaxListed; ++i)
	{
		Clean(SDL_GetJoystickNameForID(j[i]), name, sizeof name);
		PortSessionLog(PORT_SESSION_WINDOW, false, "input controller %d name=\"%s\" gamepad=%d vendor=%04x product=%04x",
			i + 1, name, SDL_IsGamepad(j[i]), SDL_GetJoystickVendorForID(j[i]), SDL_GetJoystickProductForID(j[i]));
	}
	SDL_free(k);
	SDL_free(m);
	SDL_free(j);
}

// Every audio playback device the system offers, and the default. Called once, when the game's audio opens (dsound.cpp).
void PortSessionAudioInventory()
{
	if (!PortSessionEnabled())
		return;
	int count = 0;
	SDL_AudioDeviceID *devices = SDL_GetAudioPlaybackDevices(&count);
	PortSessionLog(PORT_SESSION_AUDIO, false, "playback devices offered=%d driver=%s", count,
		SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "-");
	char name[49];
	for (int i = 0; devices && i < count && i < kMaxListed; ++i)
	{
		SDL_AudioSpec spec{};
		int frames = 0;
		bool haveFormat = SDL_GetAudioDeviceFormat(devices[i], &spec, &frames);
		Clean(SDL_GetAudioDeviceName(devices[i]), name, sizeof name);
		PortSessionLog(PORT_SESSION_AUDIO, false, "playback device %d name=\"%s\" format=%s%d Hz %d channels",
			i + 1, name, haveFormat ? "" : "unavailable:", spec.freq, spec.channels);
	}
	SDL_free(devices);
}

// The CE settings (ce.ini) and environment switches that change how the game runs, as numbers only.
void PortSessionSettingsInventory()
{
	if (!PortSessionEnabled() || !CE_SettingGet)
		return;
	// -1: the variable is not set (the default applies); otherwise its number.
	const char *wide = getenv("BEA_PORT_WIDESCREEN"), *hud = getenv("BEA_PORT_HUD_SCALE"), *user = getenv("BEA_USER_DIR");
	PortSessionLog(PORT_SESSION_GAME, false,
		"settings windowed=%d wide_mode=%dx%d record_sessions=%d god_mode=%d widescreen_env=%d hud_scale_env=%d "
		"custom_user_folder=%d",
		CE_SettingGet("windowed", -1), CE_SettingGet("wide_mode_w", 0), CE_SettingGet("wide_mode_h", 0),
		CE_SettingGet("record_sessions", 0), CE_SettingGet("god_mode", 0), wide && *wide ? atoi(wide) : -1,
		hud && *hud ? atoi(hud) : -1, user && *user ? 1 : 0);
}
