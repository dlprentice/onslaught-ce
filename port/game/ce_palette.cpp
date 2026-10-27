// SPDX-License-Identifier: GPL-3.0-only
// A small CE-only command palette. It is independent of the retail console and its command executor.
#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "frontend.h"
#include "PauseMenu.h"
#include "GameInterface.h"
#include "CLIParams.h"
#include "ltshell.h"
#include "DXEngine.h"
#include "Hud.h"
#include "SpriteRenderer.h"
#include "renderinfo.h"
#include "Texture.h"

#include <stdlib.h>
#include <string.h>

// The optional network composite supplies this latched boot-intent owner. A weak reference is resolved at link time,
// so adding networking cannot leave an incrementally reused offline object without the fence. Both port toolchains
// are Clang; unknown compilers fail rather than silently building an unguarded network palette.
#if !defined(__clang__) && !defined(__GNUC__)
#error The CE palette requires weak symbol support for the network boot boundary
#endif
extern "C" int CE_BootRequested(void) __attribute__((weak));

bool PortCeLayer();
bool PortRecording();
extern WORD gCharScanCode;

namespace {
const unsigned kInputLimit = 31;
char input[kInputLimit + 1];
unsigned length;
bool overflow, paletteOpen;
bool consumed[256];
const WCHAR *result = L"Type help for commands.";

void Trap(WORD key, KeyEventType event);

bool Present(const char *name)
{
    const char *value = getenv(name);
    return value && *value;
}

bool Allowed()
{
    // The usual CE switch has a deliberate screenshot override. Never honor that override for interactive commands.
    const char *blocked[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
                             "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};
    if (!PortCeLayer() || PortRecording()) return false;
    for (const char *name : blocked) if (Present(name)) return false;
    if (CE_BootRequested && CE_BootRequested()) return false;
    if (PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo ||
        CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode || FRONTEND.IsRunning()) return false;
    if (GAME.GetGameState() != GAME_STATE_PLAYING || GAME.IsMultiplayer() || GAME.GetNPlayers() != 1 ||
        GAMEINTERFACE.IsMenuUp() || GAMEINTERFACE.mShowJoypad) return false;
    if (!GAME.GetPlayer(0) || !GAME.GetController(0) || !GAME.GetCurrentCamera(0) ||
        !GAME.GetPauseMenu() || GAME.GetPauseMenu()->mActive) return false;
    // Do not capture input if the overlay cannot even be laid out.
    CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
    CDXTexture *white = HUD.GetWhiteTexture();
    return font && font->GetHeight() > 0 && white && white->GetRealWidth() > 0 && white->GetRealHeight() > 0 &&
           PLATFORM.GetWindowWidth() > 0 && PLATFORM.GetWindowHeight() > 0;
}

void Close()
{
    // A remap, menu or other owner may have replaced our trap; never clear its trap.
    if (LT.GetKeytrap() == Trap) LT.SetKeytrap(NULL);
    paletteOpen = false;
    length = 0;
    input[0] = 0;
    overflow = false;
}

void Submit()
{
    if (!Allowed()) { Close(); return; }
    if (overflow)
        result = L"Input unsupported or too long.";
    else {
        // ASCII case folding only. The entire line must match one command: no arguments or command separators.
        char command[kInputLimit + 1];
        unsigned first = 0, last = length;
        while (first < last && input[first] == ' ') ++first;
        while (last > first && input[last - 1] == ' ') --last;
        unsigned n = 0;
        for (unsigned i = first; i < last; ++i) {
            char c = input[i];
            command[n++] = (c >= 'A' && c <= 'Z') ? char(c + ('a' - 'A')) : c;
        }
        command[n] = 0;
        if (!strcmp(command, "help"))
            result = L"Commands are listed above.";
        else if (!strcmp(command, "status")) {
            if (GAME.IsFreeCameraOn(0))
                result = GAME.IsPaused() ? L"Free camera. Battle paused." : L"Free camera. Battle running.";
            else
                result = GAME.IsPaused() ? L"Normal camera. Battle paused." : L"Normal camera. Battle running.";
        }
        else if (!strcmp(command, "camera")) {
            if (!FRONTEND.mFEPSaveGame.IsCheatActive(4))
                result = L"Enable the Free camera cheat first.";
            else if (!ENGINE.GetLandscape())
                result = L"Camera is unavailable here.";
            else {
                const bool wasFree = GAME.IsFreeCameraOn(0) != FALSE;
                GAME.ReceiveButtonAction(GAME.GetController(0), BUTTON_TOGGLE_FREE_CAMERA, 1.f);
                if ((GAME.IsFreeCameraOn(0) != FALSE) == wasFree)
                    result = L"Camera is unavailable here.";
                else
                    result = GAME.IsFreeCameraOn(0) ? L"Free camera on." : L"Free camera off.";
            }
        }
        else if (*command)
            result = L"Unknown command. Type help.";
        else
            result = L"Type help for commands.";
    }
    length = 0;
    input[0] = 0;
    overflow = false;
}

void Trap(WORD key, KeyEventType event)
{
    if (!paletteOpen || LT.GetKeytrap() != Trap) return;
    if (event == KEY_EVENT_DOWN) {
        if (key >= 256) return;
        const bool wasDown = consumed[key];
        consumed[key] = true;
        if ((key == DIK_GRAVE && !wasDown) || key == DIK_ESCAPE) Close();
        else if (key == DIK_RETURN || key == DIK_NUMPADENTER) Submit();
        else if (key == DIK_BACK && length) input[--length] = 0;
    }
    else if (event == KEY_EVENT_CHAR && gCharScanCode != DIK_GRAVE && (key >= 32 || key == 9)) {
        if (key >= 32 && key <= 126 && length < kInputLimit && !overflow) {
            input[length++] = char(key);
            input[length] = 0;
        } else overflow = true; // never execute truncated or unsupported input
    }
}

void DrawLine(CDXBitmapFont *font, float left, float y, float scale, const WCHAR *text)
{
    WCHAR visible[80];
    unsigned i = 0;
    for (; text[i] && i < 79; ++i)
        visible[i] = font->CanDisplay(text[i]) ? text[i] : L'?';
    visible[i] = 0;
    SIZE extent;
    font->GetTextExtent(visible, &extent);
    float textScale = scale;
    if (extent.cx > 608) textScale *= 608.f / float(extent.cx);
    font->DrawTextWithShadow(left, y, 0xffffffff, visible, 0, 0.f, textScale, scale);
}
}

void PortCePaletteCheck()
{
    if (paletteOpen && (LT.GetKeytrap() != Trap || !Allowed())) Close();
}

bool PortCePaletteOpen(unsigned key, bool repeat)
{
    if (key != DIK_GRAVE || repeat || key >= 256 || consumed[key] || LT.GetKeytrap() || !Allowed()) return false;
    paletteOpen = true;
    length = 0;
    input[0] = 0;
    overflow = false;
    result = L"Type help for commands.";
    consumed[key] = true;
    LT.SetKeytrap(Trap);
    return true;
}

bool PortCePaletteSuppressDown(unsigned key)
{
    // A repeat after close/replacement belongs to the old owner until the physical release.
    return key < 256 && consumed[key] && LT.GetKeytrap() != Trap;
}

bool PortCePaletteRelease(unsigned key)
{
    if (key >= 256 || !consumed[key]) return false;
    consumed[key] = false;
    return true;
}

bool PortCePaletteSuppressChar(unsigned key)
{
    return key < 256 && consumed[key] && LT.GetKeytrap() != Trap;
}

void PortCePaletteClose() { Close(); }

void PortCePaletteRender()
{
    PortCePaletteCheck();
    if (!paletteOpen) return;
    CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
    CDXTexture *white = HUD.GetWhiteTexture();
    if (!font || !white || white->GetRealWidth() <= 0 || white->GetRealHeight() <= 0) return;
    const float w = float(PLATFORM.GetWindowWidth()), h = float(PLATFORM.GetWindowHeight());
    if (w <= 0.f || h <= 0.f) return;
    const float neededHeight = (float(font->GetHeight()) + 3.f) * 8.f + 22.f;
    const float canvasHeight = neededHeight > 480.f ? neededHeight : 480.f;
    const float scale = (w / 640.f < h / canvasHeight) ? w / 640.f : h / canvasHeight;
    const float left = (w - 640.f * scale) * 0.5f + 16.f * scale;
    const float top = (h - canvasHeight * scale) * 0.5f + 16.f * scale;
    const float step = (float(font->GetHeight()) + 3.f) * scale;
    // Match the game's options-style text state; later passes get depth testing back.
    RENDERINFO.SetFogEnabled(false);
    RENDERINFO.SetLightingEnabled(false);
    LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
    LT.SRS(D3DRS_ALPHAREF, 8);
    LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
    LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
    LT.SRS(D3DRS_ZENABLE, FALSE);
    LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    RENDERINFO.Apply();
    CSPRITERENDERER::DrawColouredSprite(left - 8.f * scale, top - 6.f * scale, 0.001f, white, 0xd0000000,
                                       624.f * scale / white->GetRealWidth(), (step * 8.f + 12.f * scale) / white->GetRealHeight());
    DrawLine(font, left, top + step * 0, scale, L"Onslaught CE commands");
    DrawLine(font, left, top + step * 1, scale, L"help: show commands   status: show state");
    DrawLine(font, left, top + step * 2, scale, L"camera: free camera on/off (cheat must be on)");
    DrawLine(font, left, top + step * 3, scale, L"F also toggles. WASD move, arrows look.");
    DrawLine(font, left, top + step * 4, scale, L"Enter runs. Escape or grave key closes.");
    DrawLine(font, left, top + step * 5, scale, L"The game does not pause for this.");
    WCHAR line[kInputLimit + 3];
    line[0] = L'>'; line[1] = L' ';
    for (unsigned i = 0; i < length; ++i) line[i + 2] = (unsigned char)input[i];
    line[length + 2] = 0;
    DrawLine(font, left, top + step * 6, scale, line);
    DrawLine(font, left, top + step * 7, scale, result);
    LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
    LT.SRS(D3DRS_ZENABLE, TRUE);
    RENDERINFO.Apply();
}
#endif
