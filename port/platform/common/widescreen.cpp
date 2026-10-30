// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/widescreen.cpp: widescreen and native resolutions (patches 0040-0046, port/PARITY.md).
//
// The game knows one shape, 4:3. These answers let its patched code draw any back buffer shape without stretching:
// - the 3D view widens horizontally (Hor+: the vertical field of view stays the game's), or extends vertically on a
//   screen narrower than 4:3 (Vert+), by PortWideX and PortWideY; the projection and the visibility gamut widen together;
// - the 640 x 480 virtual screen of the front end and the in-game menus is drawn into the largest centred 4:3 box
//   (PortUiScale, PortUiLeft, PortUiTop), and the in-game HUD is moved into that box's columns (PortHudInset), except in
//   ordinary single-player play, where its corner groups sit in the screen's corners (PortHudAnchored, ce_hud_anchor.cpp);
// - the display mode list offers the desktop's sizes and the default mode is the desktop's (d3d9hooks.cpp, patch 0046);
// - with ce.ini's sharp_ui=1 (off by default), bitmap text at twice its size or more can use a sharper texture (DXFont.cpp).
//
// Ordinarily PortWideModes is false for replays or recordings (PortCeLayer), offscreen and BEA_PORT_WIDESCREEN=0.
// A valid BEA_PORT_WIDE_TEST=WxH forces modes on before those checks and chooses the default size;
// BEA_PORT_CE_LAYER=1 can bypass the replay/recording gate. PortWideActive still requires a non-4:3 back buffer.
// Each patch site keeps the game's own statements in its else arm.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

#include <SDL3/SDL.h>

bool PortCeLayer();		// ce.cpp
int PortHudAnchored();	// ce_hud_anchor.cpp

namespace
{
int gModes = -1;
unsigned gTestW, gTestH;
bool gActive;
unsigned gW = 640, gH = 480;
float gX = 1.f, gY = 1.f, gScale = 1.f, gLeft = 0.f, gTop = 0.f;

void ReadTest()
{
	static bool read;
	if (read)
		return;
	read = true;
	const char *t = getenv("BEA_PORT_WIDE_TEST");
	unsigned w = 0, h = 0;
	if (t && sscanf(t, "%ux%u", &w, &h) == 2 && w >= 640 && h >= 480 && w <= 16384 && h <= 16384)
	{
		gTestW = w;
		gTestH = h;
	}
}
}

// SDL's video subsystem, started once (window.cpp and the mode list both need it; BuildDeviceList asks for modes before
// the window exists). On Windows SDL 3 declares per-monitor (v2) DPI awareness itself when it starts, so the window,
// its client rectangle and the mouse are in real pixels, as the game assumes.
void PortVideoInit()
{
	if (SDL_WasInit(SDL_INIT_VIDEO))
		return;
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		fprintf(stderr, PORT_LOG_PREFIX "SDL video: %s\n", SDL_GetError());
}

int PortWideModes()
{
	if (gModes >= 0)
		return gModes;
	ReadTest();
	if (gTestW)
		return gModes = 1;
	if (!PortCeLayer())
		return gModes = 0;
	const char *off = getenv("BEA_PORT_WIDESCREEN");
	if (off && *off == '0')
		return gModes = 0;
	const char *want = getenv("SDL_VIDEO_DRIVER");
	if (want && !strcmp(want, "offscreen"))
		return gModes = 0;
	PortVideoInit();
	const char *driver = SDL_GetCurrentVideoDriver();
	if (!driver || !strcmp(driver, "offscreen"))
		return gModes = 0;
	gModes = 1;
	if (PortDevLog())
		fprintf(stderr, PORT_LOG_PREFIX "widescreen modes on (video driver %s)\n", driver);
	return gModes;
}

// The size the game starts at when nothing else was chosen: the test size, else the desktop's in pixels.
int PortWideDefaultSize(unsigned *w, unsigned *h)
{
	if (!PortWideModes())
		return 0;
	if (gTestW)
	{
		*w = gTestW;
		*h = gTestH;
		return 1;
	}
	return PortWideDesktopSize(w, h);
}

// The size a saved mode starts at with widescreen modes on, when the saved mode is not the player's own choice: 1 and that
// size, else 0 (the saved mode stands). No saved mode is no choice: the default size. A 32-bit 640 x 480 never decides
// alone: it is what every file held before the port listed other sizes, and what a session with widescreen modes off (a
// recording, BEA_PORT_WIDESCREEN=0) saves whenever the file's size is not in its 4:3 list (d3dapp falls back to mode 0).
// ce.ini's wide_mode_w and wide_mode_h, written whenever a session with widescreen modes on saves (PortWideOptionsSaved),
// keep the size the player last had there: a 640 x 480 stands only when that size is 640 x 480 too, another one replaces
// it, and with none the default size does. Test sizes always take the default.
extern "C" int CE_SettingGet(const char *key, int def);	// ce.cpp
extern "C" int CE_SettingSet(const char *key, int value);
#define PORT_WIDE_KEY_W	"wide_mode_w"
#define PORT_WIDE_KEY_H	"wide_mode_h"

static int WideStartSize(unsigned savedW, unsigned savedH, int saved32, unsigned desktopW, unsigned desktopH,
					 unsigned *w, unsigned *h)
{
	if (!PortWideModes())
		return 0;
	if (savedW && !(savedW == 640 && savedH == 480 && saved32))
		return 0;
	if (savedW && !gTestW)
	{
		int rw = CE_SettingGet(PORT_WIDE_KEY_W, 0), rh = CE_SettingGet(PORT_WIDE_KEY_H, 0);
		if (rw == 640 && rh == 480)
			return 0;
		if (rw >= 640 && rh >= 480 && rw <= 16384 && rh <= 16384)
		{
			*w = (unsigned)rw;
			*h = (unsigned)rh;
			return 1;
		}
	}
	// A caller selecting a specific adapter can supply that adapter's desktop. Before the main window exists,
	// SDL's default display is the primary, which may be a different monitor. The explicit test size still wins.
	if (!gTestW && desktopW && desktopH)
	{
		*w = desktopW;
		*h = desktopH;
		return 1;
	}
	return PortWideDefaultSize(w, h);
}

int PortWideStartSize(unsigned savedW, unsigned savedH, int saved32, unsigned *w, unsigned *h)
{
	return WideStartSize(savedW, savedH, saved32, 0, 0, w, h);
}

int PortWideStartSizeOnDesktop(unsigned savedW, unsigned savedH, int saved32, unsigned desktopW, unsigned desktopH,
							   unsigned *w, unsigned *h)
{
	return WideStartSize(savedW, savedH, saved32, desktopW, desktopH, w, h);
}

// CCareer::Save (patch 0046) with the mode it writes: a session with widescreen modes on keeps that size in ce.ini.
void PortWideOptionsSaved(unsigned w, unsigned h)
{
	if (!PortWideModes() || gTestW || !w || !h)
		return;
	if (CE_SettingGet(PORT_WIDE_KEY_W, 0) != (int)w)
		CE_SettingSet(PORT_WIDE_KEY_W, (int)w);
	if (CE_SettingGet(PORT_WIDE_KEY_H, 0) != (int)h)
		CE_SettingSet(PORT_WIDE_KEY_H, (int)h);
}

int PortWideDesktopSize(unsigned *w, unsigned *h)
{
	ReadTest();
	if (gTestW)
	{
		*w = gTestW;
		*h = gTestH;
		return 1;
	}
	PortVideoInit();
	SDL_DisplayID display = 0;
	if (SDL_Window *win = PortMainSdlWindow())
		display = SDL_GetDisplayForWindow(win);
	if (!display)
		display = SDL_GetPrimaryDisplay();
	const SDL_DisplayMode *mode = display ? SDL_GetDesktopDisplayMode(display) : NULL;
	if (!mode || mode->w <= 0 || mode->h <= 0)
		return 0;
	float density = mode->pixel_density > 0 ? mode->pixel_density : 1.f;
	*w = (unsigned)(mode->w * density + 0.5f);
	*h = (unsigned)(mode->h * density + 0.5f);
	return 1;
}

// The display's own fullscreen sizes in pixels (Linux's mode list, d3d9hooks.cpp); returns how many were written.
int PortWideDisplaySizes(unsigned (*out)[2], int max)
{
	PortVideoInit();
	SDL_DisplayID display = 0;
	if (SDL_Window *win = PortMainSdlWindow())
		display = SDL_GetDisplayForWindow(win);
	if (!display)
		display = SDL_GetPrimaryDisplay();
	if (!display)
		return 0;
	int count = 0, n = 0;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &count);
	for (int i = 0; modes && i < count && n < max; i++)
	{
		float density = modes[i]->pixel_density > 0 ? modes[i]->pixel_density : 1.f;
		out[n][0] = (unsigned)(modes[i]->w * density + 0.5f);
		out[n][1] = (unsigned)(modes[i]->h * density + 0.5f);
		n++;
	}
	SDL_free(modes);
	return n;
}

// The back buffer the device was created or reset with (d3d9hooks.cpp): sets every answer below for it.
void PortWideBackBuffer(unsigned w, unsigned h)
{
	if (!w || !h)
		return;
	gW = w;
	gH = h;
	gActive = PortWideModes() && w * 3 != h * 4;
	if (!gActive)
	{
		gX = gY = gScale = 1.f;
		gLeft = gTop = 0.f;
		return;
	}
	float r = ((float)w / (float)h) * 0.75f;
	gX = r > 1.f ? r : 1.f;
	gY = r < 1.f ? 1.f / r : 1.f;
	if (w * 3 > h * 4)
	{
		gScale = (float)h / 480.f;
		gLeft = ((float)w - 640.f * gScale) * 0.5f;
		gTop = 0.f;
	}
	else
	{
		gScale = (float)w / 640.f;
		gLeft = 0.f;
		gTop = ((float)h - 480.f * gScale) * 0.5f;
	}
	if (PortDevLog())
		fprintf(stderr, PORT_LOG_PREFIX "widescreen: back buffer %ux%u, 3D x %.4f y %.4f, 4:3 box at %.1f,%.1f scale %.4f\n", w, h, (double)gX, (double)gY,
				(double)gLeft,
				(double)gTop, (double)gScale);
}

int PortWideActive() { return gActive; }
float PortWideX() { return gX; }
float PortWideY() { return gY; }
float PortUiScale() { return gScale; }
float PortUiLeft() { return gLeft; }
float PortUiTop() { return gTop; }

// The sharper UI: ce.ini sharp_ui=1 is off by default. A public bitmap font can use a sharper copy when the wide path
// is active and the larger drawing scale is at least 2 (DXFont.cpp, CrispWanted). A 4:3 back buffer is never active.
// Replays, recordings, offscreen and BEA_PORT_WIDESCREEN=0 ordinarily disable wide modes; BEA_PORT_WIDE_TEST can
// override those checks, and BEA_PORT_CE_LAYER=1 can bypass the replay/recording gate. Each eligible caller asks
// last, so ce.ini is read once on an eligible draw; the session log says the answer, and changes take effect next start.
extern "C" int CE_SettingGetBool(const char *key, int def);	// ce.cpp
int PortSharpUi()
{
	if (!gActive)
		return 0;
	static int ini = -1;
	if (ini < 0)
	{
		ini = CE_SettingGetBool("sharp_ui", 0) ? 1 : 0;
		PortSessionLog(PORT_SESSION_VIDEO, false, "sharp_ui=%d (ce.ini): %s", ini,
					   ini ? "text at twice its size or more is drawn from sharper copies of the fonts" : "the game's soft text");
	}
	return ini;
}

// The HUD's shift into the 4:3 columns of a viewport: the part of its width the widening added, halved. PortHudBegin
// returns the shifted HUD_OFFSET_X for the HUD's drawing and keeps the game's own value, which the centred message box
// reads back (PortHudOriginal: it narrows itself by HUD_OFFSET_X on both sides). The game lays its HUD out against the
// window's edges (HUD_OFFSET_X is 0), so where PortHudAnchored says so (ordinary single-player play) the shift is none: the
// corner groups sit in the screen's corners, the compass, reticle and radio box stay where they were, and everything the
// HUD scale anchors follows because its anchors are made from the same HUD_OFFSET_X. PortHudBegin decides once per pass:
// the objective panel's own PortHudInset terms are drawn inside it.
static bool gHudAnchored;
static int gHudLogged = -1;	// the answer last told to the developer log (-1: none yet)
float PortHudInset(float viewportWidth) { return gActive && !gHudAnchored ? viewportWidth * (1.f - 1.f / gX) * 0.5f : 0.f; }
static float gHudOriginal;
float PortHudBegin(float offset, float viewportWidth)
{
	gHudOriginal = offset;
	gHudAnchored = PortHudAnchored() != 0;
	float shifted = offset + PortHudInset(viewportWidth);
	if (gHudLogged != (int)gHudAnchored)
	{
		gHudLogged = gHudAnchored;
		if (PortDevLog())
			fprintf(stderr, PORT_LOG_PREFIX "widescreen: the HUD's corner groups %s (HUD_OFFSET_X %.0f)\n",
					gHudAnchored ? "sit in the screen's corners" : "sit in the 4:3 columns", (double)shifted);
	}
	return shifted;
}
float PortHudOriginal() { return gHudOriginal; }

// An in-game menu (the pause menu, the message and briefing logs, the end-of-level menus) draws in the 640 x 480 virtual screen as the front end does, but the game scales the pointer to it only while
// the front end runs: there the pointer stayed in window pixels, which is right at 640 x 480 only, so in a bigger window (or a wide one, where the screen is a 4:3 box) the arrow was drawn off its place
// or not at all and no menu item felt the mouse. While the game's own mouse is released (it is, in a menu), the layer is on (not in a replay or a recording) and the commander map, which reads
// window pixels, is not open, the pointer of those menus is mapped as the front end's (patch 0042's mouse hunks ask).
#ifdef CE_VERSION
bool PortCeCmdMapOpen();	// ce_cmdmap.cpp
#endif
void PortUiMouse(int *x, int *y);
int PortUiMouseMenu(int mouseInputActive)
{
#ifdef CE_VERSION
	return !mouseInputActive && PortCeLayer() && !PortCeCmdMapOpen() ? 1 : 0;
#else
	(void)mouseInputActive;
	return 0;
#endif
}

// The same point as the virtual screen's, for the pointer those menus read: through the 4:3 box where the wide paths are on, else scaled by the back buffer's size as the front end's.
void PortUiMouseMenuPoint(int *x, int *y)
{
	if (gActive)
	{
		PortUiMouse(x, y);
		return;
	}
	if (gW && gH)
	{
		*x = (int)((long long)*x * 640 / (long long)gW);
		*y = (int)((long long)*y * 480 / (long long)gH);
	}
}

// A window position (back buffer pixels) as the 640 x 480 virtual screen's.
void PortUiMouse(int *x, int *y)
{
	int vx = (int)(((float)*x - gLeft) / gScale);
	int vy = (int)(((float)*y - gTop) / gScale);
	*x = vx < 0 ? 0 : vx > 639 ? 639 : vx;
	*y = vy < 0 ? 0 : vy > 479 ? 479 : vy;
}
