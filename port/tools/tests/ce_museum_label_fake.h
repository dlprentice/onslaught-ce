// SPDX-License-Identifier: GPL-3.0-only
// The game boundary ce_museum_label.cpp draws through, faked for a host test (ce_museum_label_visit_driver.cpp): the visitor's engine (where it
// stands and faces), the HUD's framed bar and the small face, each recorded so a test can read what one frame drew. Nothing else of the game.
#pragma once
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

typedef unsigned long DWORD;
typedef wchar_t WCHAR;
typedef long HRESULT;
typedef int BOOL;
#define FAILED(x) ((x) < 0)
#define FONT_SMALL 0
#define HUD_OFFSET_X 0.0f
#define HUD_OFFSET_Y 0.0f

struct SIZE { long cx, cy; };
struct D3DMATRIX { float m[16]; };
struct D3DVIEWPORT9 { DWORD X, Y, Width, Height; float MinZ, MaxZ; };
struct FakeDevice { HRESULT GetViewport(D3DVIEWPORT9 *v) { *v = D3DVIEWPORT9{0, 0, 1280, 720, 0.0f, 1.0f}; return 0; } };
struct FakeShell { FakeDevice device; FakeDevice *GetDevice() { return &device; } };

struct FakeText { float x, y; DWORD colour; std::wstring text; };
extern std::vector<FakeText> gFakeTexts;
struct FakeBar { float cx, cy, width, height; };
extern std::vector<FakeBar> gFakeBars;

struct CDXBitmapFont
{
	bool CanDisplay(WCHAR) { return true; }
	void GetTextExtent(const WCHAR *text, SIZE *size) { size->cx = long(std::wcslen(text)) * 7; size->cy = 12; }
	int GetHeight() { return 12; }
	void DrawTextWithShadow(float x, float y, DWORD colour, const WCHAR *text, int, float, float, float) { gFakeTexts.push_back(FakeText{x, y, colour, text}); }
};
struct FakePlatform { CDXBitmapFont font; CDXBitmapFont *Font(int) { return &font; } };
struct FakeHud
{
	float mViewportX = 0, mViewportY = 0, mViewportWidth = 1280, mViewportHeight = 720, mViewportBottom = 720;
	void RenderObjectiveBar(float cx, float cy, float width, float height, float) { gFakeBars.push_back(FakeBar{cx, cy, width, height}); }
};
struct FakeRenderInfo
{
	void SetFogEnabled(bool) {}
	void SetLightingEnabled(bool) {}
	void EnableLighting(bool) {}
	void Apply() {}
	void GetView(D3DMATRIX *m) { std::memset(m, 0, sizeof *m); m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f; }
	void GetProjection(D3DMATRIX *m) { GetView(m); }
};

struct FVector { float X, Y, Z; };
struct FMatrix { FVector forward; FVector GetColumnY() const { return forward; } };
struct CBattleEngine
{
	FVector position{0, 0, 0};
	FMatrix orientation{{0, 1, 0}};
	FVector GetPos() { return position; }
	FMatrix &GetOrientation() { return orientation; }
};
struct CPlayer { CBattleEngine engine; CBattleEngine *GetBattleEngine() { return &engine; } };
struct FakeGame { CPlayer player; bool IsMultiplayer() { return false; } CPlayer *GetPlayer(int) { return &player; } };
struct FakeEngine { int GetNumViewpoints() { return 1; } };

extern FakeShell LT;
extern FakeGame GAME;
extern FakeEngine ENGINE;
extern FakeHud HUD;
extern FakePlatform PLATFORM;
extern FakeRenderInfo RENDERINFO;
