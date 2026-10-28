// SPDX-License-Identifier: GPL-3.0-only
// Minimal authored game boundary used to execute the production console (port/game/ce_palette.cpp) and the patched
// message handler. The console's game actions (port/game/ce_console.h) are recorded by the driver's fakes.
#ifndef CE_PALETTE_FAKE_H
#define CE_PALETTE_FAKE_H
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
using WORD = uint16_t; using BYTE = uint8_t; using DWORD = uint32_t; using WCHAR = wchar_t;
using UINT = unsigned; using WPARAM = uintptr_t; using LPARAM = intptr_t; using LRESULT = intptr_t;
using HWND = void *; using BOOL = int;
#define FALSE 0
#define TRUE 1
#define DIK_GRAVE 0x29
#define DIK_ESCAPE 0x01
#define DIK_RETURN 0x1c
#define DIK_NUMPADENTER 0x9c
#define DIK_BACK 0x0e
#define DIK_TAB 0x0f
#define DIK_UP 0xc8
#define DIK_DOWN 0xd0
#define DIK_PRIOR 0xc9
#define DIK_NEXT 0xd1
#define WM_COMMAND 0x0111
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_ACTIVATEAPP 0x001c
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSEWHEEL 0x020a
#define IDM_ABOUT 0x0010
#define LOWORD(x) ((x) & 0xffff)
#define HIWORD(x) (((x) >> 16) & 0xffff)
#define FONT_SMALL 0
#define BUTTON_TOGGLE_FREE_CAMERA 1
#define D3DRS_SRCBLEND 1
#define D3DRS_DESTBLEND 2
#define D3DBLEND_SRCALPHA 3
#define D3DBLEND_INVSRCALPHA 4
#define D3DTSS_COLORARG1 5
#define D3DTSS_COLORARG2 6
#define D3DTSS_COLOROP 7
#define D3DTA_TEXTURE 8
#define D3DTA_DIFFUSE 9
#define D3DTOP_MODULATE 10
#define D3DRS_ALPHATESTENABLE 11
#define D3DRS_ALPHAREF 12
#define D3DRS_ALPHABLENDENABLE 13
#define D3DRS_ZWRITEENABLE 14
#define D3DRS_ZENABLE 15
#define D3DRS_ZFUNC 16
#define D3DCMP_ALWAYS 17
#define D3DCMP_LESSEQUAL 18
struct SIZE { int cx, cy; };
enum KeyEventType { KEY_EVENT_DOWN, KEY_EVENT_UP, KEY_EVENT_CHAR, KEY_EVENT_MOUSE, KEY_EVENT_UNKNOWN4 };
using pKeyTrapper = void (*)(WORD, KeyEventType);
struct BaseShell { LRESULT MsgProc(HWND, UINT, WPARAM, LPARAM) { return 0; } };
#define PCLTShellSUPERTYPE BaseShell
struct PCLTShell : BaseShell {
    pKeyTrapper mCurrentKeytrap = nullptr;
    BYTE KeyDown[256] = {}, KeyWasDown[256] = {}, KeyWasUp[256] = {};
    WPARAM mMouseButtons = 0; int mMouseX = 0, mMouseY = 0;
    pKeyTrapper GetKeytrap() { return mCurrentKeytrap; }
    void SetKeytrap(pKeyTrapper t) { mCurrentKeytrap = t; }
    void SRS(int, int) {}
    void STS(int, int, int) {}
    LRESULT MsgProc(HWND, UINT, WPARAM, LPARAM);
};
extern PCLTShell LT;
extern WORD gCharScanCode;
extern bool gKeyPressed;
inline LRESULT HandleMouseMessage(UINT, WPARAM, LPARAM) { return 0; }
struct Cli { bool mAutoConfigTest = false, mRecordDemo = false, mPlaybackDemo = false, mAttractMode = false; int mStressTest = 0; };
extern Cli CLIPARAMS;
extern BOOL PLAYABLE_DEMO;
struct FakeController {};
struct FakePlayer {};
struct FakeCamera {};
struct FakePause { BOOL mActive = FALSE; };
struct FakeGame {
    int state = 3, players = 1; bool multiplayer = false;
    bool playerPresent = true, controllerPresent = true, cameraPresent = true, pausePresent = true;
    FakePlayer player; FakeController controller; FakeCamera camera; FakePause pause;
    int GetGameState() { return state; }
    bool IsMultiplayer() { return multiplayer; }
    int GetNPlayers() { return players; }
    FakePlayer *GetPlayer(int) { return playerPresent ? &player : nullptr; }
    FakeController *GetController(int) { return controllerPresent ? &controller : nullptr; }
    FakeCamera *GetCurrentCamera(int) { return cameraPresent ? &camera : nullptr; }
    FakePause *GetPauseMenu() { return pausePresent ? &pause : nullptr; }
};
#define GAME_STATE_PLAYING 3
extern FakeGame GAME;
struct FakeFrontend { bool running = false; bool IsRunning() { return running; } };
extern FakeFrontend FRONTEND;
struct FakeInterface { bool menu = false, mShowJoypad = false; bool IsMenuUp() { return menu; } };
extern FakeInterface GAMEINTERFACE;
struct CDXTexture { int width = 64, height = 64; int GetRealWidth() { return width; } int GetRealHeight() { return height; } };
struct FakeHud { bool present = true; CDXTexture white; CDXTexture *GetWhiteTexture() { return present ? &white : nullptr; } };
extern FakeHud HUD;
struct FakeDraw { float x, y, sx, sy; DWORD colour; std::wstring text; };
extern std::vector<FakeDraw> draws;
struct CDXBitmapFont {
    int height = 16;
    int GetHeight() { return height; }
    // As the game's font: no cell for "$@[\]^_{|}~" (src/Font.cpp FONT_UNMAPPED_CHARACTERS) or anything above 0xff.
    bool CanDisplay(WCHAR c) { return c >= 32 && c <= 0xff && !std::wcschr(L"$@[\\]^_{|}~", c); }
    void GetTextExtent(WCHAR *s, SIZE *size) { size->cx = int(std::wcslen(s)) * 8; size->cy = 16; }
    void DrawTextWithShadow(float x, float y, DWORD colour, WCHAR *s, int, float, float sx, float sy) {
        for (WCHAR *c = s; *c; ++c) if (!CanDisplay(*c)) { std::fprintf(stderr, "FAIL: undrawable character %x\n", unsigned(*c)); std::exit(2); }
        draws.push_back({x, y, sx, sy, colour, s});
    }
};
struct FakePlatform { int w = 640, h = 480; bool present = true; CDXBitmapFont font; CDXBitmapFont *Font(int) { return present ? &font : nullptr; } int GetWindowWidth() { return w; } int GetWindowHeight() { return h; } };
extern FakePlatform PLATFORM;
struct FakeRenderInfo { void SetFogEnabled(bool) {} void SetLightingEnabled(bool) {} void SRS(int, int) {} void Apply() {} };
extern FakeRenderInfo RENDERINFO;
struct FakeRect { float x = 0, y = 0, w = 0, h = 0; DWORD colour = 0; };
extern std::vector<FakeRect> sprites;
struct CSPRITERENDERER {
    static void DrawColouredSprite(float x, float y, float, CDXTexture *t, DWORD colour, float sx, float sy) { sprites.push_back({x, y, sx * t->width, sy * t->height, colour}); }
};
struct FakeConsole { std::vector<std::pair<unsigned, int>> binds; void HandleBind(unsigned key, int event) { binds.emplace_back(key, event); } };
extern FakeConsole CONSOLE;
extern bool fakeRecording;
inline bool PortRecording() { return fakeRecording; }
#ifdef CE_VERSION
void PortCePaletteCheck();
void PortCePaletteClose();
void PortCePaletteRender();
bool PortCePaletteOpen(unsigned key, bool repeat);
bool PortCePaletteSuppressDown(unsigned key);
bool PortCePaletteRelease(unsigned key);
bool PortCePaletteSuppressChar(unsigned key);
#endif
#endif
