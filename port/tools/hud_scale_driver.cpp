// SPDX-License-Identifier: GPL-3.0-only
// Authored renderer boundaries calling the actual patched font/sprite code.
#include "hud_scale.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using DWORD = uint32_t;
using SINT = int;
using WCHAR = wchar_t;
bool layer = true;
int wideModes = 1;
bool PortCeLayer() { return layer; }
int PortWideModes() { return wideModes; }

static void check(bool value, const char *message)
{
	if (!value) { fprintf(stderr, "FAIL: %s\n", message); exit(2); }
}
static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

struct Draw { float x, y, sx, sy; };
std::vector<Draw> draws;
struct CDXBitmapFont
{
	void DrawTextDynamic(float, float, float, float, float, DWORD, WCHAR *, float, int, int);
	void DrawTextWithShadow(float, float, DWORD, WCHAR *, int, float, float, float);
	void DrawTextScaled(float x, float y, float, float sx, float sy, DWORD, WCHAR *, int, DWORD *)
	{ draws.push_back({x, y, sx, sy}); }
};
#include "font_methods.inc"

struct Point { float x, y; };
struct Sprite { Point p; };
struct FrontEnd { bool running = false; bool IsRunning() const { return running; } } FRONTEND;
struct Platform { int w = 1920, h = 1080; int GetWindowWidth() { return w; } int GetWindowHeight() { return h; } } PLATFORM;
struct Game { bool multiplayer = false; bool IsMultiplayer() { return multiplayer; } } GAME;
struct RenderInfo
{
	float width, height;
	void SetProjection(float, float, float w, float h) { width = w; height = h; }
} RENDERINFO;
static void render_compass_projection()
{
#include "compass_projection.inc"
}
bool wideActive = true;
int PortWideActive() { return wideActive; }
float PortUiScale() { return 2.25f; }
float PortUiLeft() { return 240.f; }
float PortUiTop() { return 0.f; }
static void render_sprite(Sprite *verts)
{
	int i;
#include "sprite_transform.inc"
}

struct CViewport { int X, Y, Width, Height; };
static CViewport target_viewport(CViewport vp)
{
#include "target_viewport.inc"
	return vp;
}

static void clear_env()
{
	for (const char *name : {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_HUD_SCALE"})
		unsetenv(name);
}

int main()
{
	clear_env();
	WCHAR message[] = L"HUD";
	CDXBitmapFont font;
	{
		CeHudScope scope(1, 640.f, 480.f, 0.f, 480.f);
		check(!CeHudScaleActive() && near(CeHudScaleFactor(), 1.f), "640x480 baseline");
		PLATFORM.w = 640; PLATFORM.h = 480;
		render_compass_projection();
		check(near(RENDERINFO.width, .5f) && near(RENDERINFO.height, .375f), "baseline ring projection");
		font.DrawTextDynamic(165.f, 445.f, .008f, .6f, .6f, 0xffffffff, message, 1.f, 0, 0);
		check(draws.size() == 2 && near(draws[0].x, 166.f) && near(draws[1].x, 165.f) &&
			near(draws[1].y, 445.f) && near(draws[1].sx, .6f), "baseline actual font calls");
	}
	draws.clear();
	{
		CeHudScope scope(1, 3840.f, 2160.f, 480.f, 2160.f);
		check(CeHudScaleActive() && near(CeHudScaleFactor(), 4.5f), "4K scale");
		font.DrawTextDynamic(645.f, 2125.f, .008f, .6f, .6f, 0xffffffff, message, 1.f, 0, 0);
		check(draws.size() == 2 && near(draws[1].x, 1222.5f) && near(draws[1].y, 2002.5f) &&
			near(draws[1].sx, 2.7f) && near(draws[1].sy, 2.7f), "4K actual font position and glyph size");
		check(near(draws[0].x - draws[1].x, 4.5f) && near(draws[0].y - draws[1].y, 4.5f), "scaled shadow");
		Sprite vertices[4] = {{{497.f, 2144.f}}, {{529.f, 2144.f}}, {{497.f, 2112.f}}, {{529.f, 2112.f}}};
		render_sprite(vertices);
		check(near(vertices[0].p.x, 556.5f) && near(vertices[0].p.y, 2088.f) &&
			near(vertices[1].p.x - vertices[0].p.x, 144.f), "4K actual sprite vertices");
		{
			CeHudScope target(1, 3840.f, 2160.f, 480.f, 0.f);
			CViewport vp = target_viewport({490, 10, 64, 64});
			check(vp.X == 525 && vp.Y == 45 && vp.Width == 288 && vp.Height == 288, "target panel viewport");
		}
		{
			CeHudScope reticle(1, 3840.f, 2160.f, 2050.f, 800.f);
			Sprite mark[4] = {{{2040.f, 790.f}}, {{2060.f, 790.f}},
			                  {{2040.f, 810.f}}, {{2060.f, 810.f}}};
			render_sprite(mark);
			check(near((mark[0].p.x + mark[1].p.x) * .5f, 2050.f) &&
			      near((mark[0].p.y + mark[2].p.y) * .5f, 800.f) &&
			      near(mark[1].p.x - mark[0].p.x, 90.f), "reticle grows about projected center");
		}
		{
			CeHudScope compass(1, 3840.f, 2160.f, 1920.f, 1080.f);
			float x = 2030.f, y = 1080.f;
			CeHudScalePoint(&x, &y);
			check(near(x, 2415.f) && near(y, 1080.f), "compass radius grows about screen center");
			PLATFORM.w = 3840; PLATFORM.h = 2160;
			render_compass_projection();
			check(near(RENDERINFO.width, 3.f / 4.5f) && near(RENDERINFO.height, 1.6875f / 4.5f),
			      "4K actual ring projection matches sprite scale");
		}
		check(near(CeHudScaleFactor(), 4.5f), "nested scope restored");
		scope.SetAnchor(1920.f, 2160.f);
		draws.clear();
		font.DrawTextWithShadow(1890.f, 2040.f, 0xffffffff, message, 0, .01f, 1.f, 1.f);
		check(draws.size() == 2 && near(draws[1].x, 1785.f) && near(draws[1].y, 1620.f) &&
			near(draws[1].sx, 4.5f), "centred warning actual font path");
	}
	check(!CeHudScaleActive(), "scope leaves no global transform");
	{
		CeHudScope scope(1, 1920.f, 1080.f, 240.f, 1080.f);
		check(near(CeHudScaleFactor(), 2.25f), "1080p scale");
	}
	{
		CeHudScope scope(1, 1280.f, 960.f, 0.f, 960.f);
		check(near(CeHudScaleFactor(), 2.f), "4:3 scale");
	}
	{
		CeHudScope scope(1, 1280.f, 1024.f, 0.f, 1024.f);
		check(near(CeHudScaleFactor(), 2.f), "narrow aspect fits width");
	}
	{
		CeHudScope scope(0, 3840.f, 2160.f, 0.f, 0.f);
		check(!CeHudScaleActive(), "multiple players retain existing HUD");
		GAME.multiplayer = true;
		render_compass_projection();
		check(near(RENDERINFO.width, 3840.f * .00093750004f) && near(RENDERINFO.height, 2160.f * .00046666668f),
		      "multiplayer ring projection unchanged");
		GAME.multiplayer = false;
	}
	for (const char *name : {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"})
	{
		setenv(name, "1", 1); // CE may be forced on; the replay fence must still win.
		CeHudScope scope(1, 3840.f, 2160.f, 0.f, 0.f);
		check(!CeHudScaleActive(), name);
		unsetenv(name);
	}
	setenv("BEA_PORT_HUD_SCALE", "0", 1);
	{ CeHudScope scope(1, 3840.f, 2160.f, 0.f, 0.f); check(!CeHudScaleActive(), "player opt-out"); }
	clear_env();
	layer = false;
	{ CeHudScope scope(1, 3840.f, 2160.f, 0.f, 0.f); check(!CeHudScaleActive(), "faithful layer"); }
	layer = true; wideModes = 0;
	{ CeHudScope scope(1, 3840.f, 2160.f, 0.f, 0.f); check(!CeHudScaleActive(), "widescreen disabled"); }
	wideModes = 1;
	FRONTEND.running = true;
	Sprite menu[4] = {{{10.f, 20.f}}, {{30.f, 20.f}}, {{10.f, 40.f}}, {{30.f, 40.f}}};
	render_sprite(menu);
	check(near(menu[0].p.x, 262.5f) && near(menu[0].p.y, 45.f), "existing menu transform unchanged");
	puts("HUD scale authored source controls: PASS");
	return 0;
}
