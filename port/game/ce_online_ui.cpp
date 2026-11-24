// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_online_ui.cpp: Native BEA front-end game-engine adapter (qcxx, ZERO host STL!)
// bridging D3D9 panel/font drawing, input events, and Multiplayer entry bar (patch 0380)
// to the host-STL model & live service in port/game/ce_online_ui_bridge.cpp via plain C callbacks.
#ifdef CE_VERSION
#ifndef CE_NETWORK_OFF

#include "ce_online_ui.h"
#include "ce_online_ui_host.h"

#include "common.h"
#include "frontend.h"
#include "FEMessBox.h"
#include "FEPBEConfig.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "text.h"
#include "TransitionHelpers.h"
#include "keytrap.h"
#include "mouse.h"
#include "../platform/common/ce_netlobby.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FEP_CE FEP_LANGUAGE_TEST

bool PortCeNetOffered();
void PortCeNetShutdown() __attribute__((weak));
extern "C" int CE_SettingGetBool(const char *key, int def);
extern "C" void PortCeNetOpenDirectFromOnline(void) __attribute__((weak));

namespace {

const DWORD kWhite     = 0xffffffffu;
const DWORD kWarn      = 0xffffcc00u;
const DWORD kCyan      = 0xff66ccffu;
const DWORD kCursorCol = 0xff00c0ffu;

bool     sOpsInited = false;
bool     sPendingMultiplayerEntry = false;
bool     sNextOpenFromMultiplayer = false;
bool     sOpenedFromMultiplayer = false;
bool     sOpenedDirectLan = false;
bool     sExitingToMultiplayer = false;
bool     sTrapActive = false;
bool     sCtrlHeld = false;
uint16_t sSuppressCharCode = 0;
float    sOpenTime = -10.f;
float    sLastSelectTime = -10.f;
float    sLaunchStartedAt = -1.f;
bool     sLaunchQuitPosted = false;
int      sLastMouseX = -1;
int      sLastMouseY = -1;

bool OnlineOffered() {
    return PortCeNetOffered() && (CE_SettingGetBool("network_game", 1) == 1);
}

bool FontCharSafe(unsigned char c) {
    if (c == ' ' || c == '-' || c == '.' || c == ':' || c == '/' ||
        c == '#' || c == '(' || c == ')' || c == '[' || c == ']' ||
        c == '+' || c == '!' || c == '?' || c == ',' || c == '*' ||
        c == '\'' || c == '<' || c == '>' || c == '%' || c == '=')
        return true;
    if (c >= '0' && c <= '9') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= 'a' && c <= 'z') return true;
    return false;
}

// 2-byte WCHAR conversion safe under -fshort-wchar (never calls glibc wcslen/wcscpy).
void WidenSafe(WCHAR *out, int cap, const char *in) {
    if (!out || cap <= 0) return;
    if (!in) {
        out[0] = 0;
        return;
    }
    CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
    int i = 0;
    for (; in[i] && i < cap - 1; ++i) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (!FontCharSafe(c)) {
            c = (c == '_') ? '-' : ' ';
        }
        WCHAR w = static_cast<WCHAR>(c);
        if (font && !font->CanDisplay(w)) w = ' ';
        out[i] = w;
    }
    out[i] = 0;
}

void ApplyTextStates() {
    LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    RENDERINFO.SetFogEnabled(false);
    RENDERINFO.SetUnknown2ed(false);
    RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
    RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
    LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
    LT.SRS(D3DRS_ALPHAREF, 8);
    LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
    LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
    LT.SRS(D3DRS_ZENABLE, FALSE);
    RENDERINFO.Apply();
}

float TextWidth(const WCHAR *w) {
    CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
    if (!font) return 0.f;
    SIZE sz = { 0, 0 };
    font->GetTextExtent(const_cast<WCHAR *>(w), &sz);
    return float(sz.cx);
}

// Fits 2-byte WCHAR text strictly inside maxW at scale >= 0.65f, ellipsizing with "..."
// whenever min-scale would overflow maxW (preventing text from bleeding across column borders).
// Safe under -fshort-wchar: never calls glibc wcslen/wcscpy.
void FitWideTextToWidth(WCHAR *wbuf, int cap, float maxW, float *outScale, float *outDrawW, bool keepTail) {
    const float kMinScale = 0.65f;
    if (!wbuf || cap <= 1 || maxW <= 0.f || wbuf[0] == 0) {
        if (wbuf && cap > 0) wbuf[0] = 0;
        if (outScale) *outScale = 1.f;
        if (outDrawW) *outDrawW = 0.f;
        return;
    }

    float w = TextWidth(wbuf);
    if (w <= maxW) {
        if (outScale) *outScale = 1.f;
        if (outDrawW) *outDrawW = w;
        return;
    }
    if (w * kMinScale <= maxW && w > 1.f) {
        const float sc = maxW / w;
        if (outScale) *outScale = sc;
        if (outDrawW) *outDrawW = w * sc;
        return;
    }

    int len = 0;
    while (len < cap - 1 && wbuf[len] != 0) ++len;

    WCHAR orig[200];
    if (len >= 200) len = 199;
    for (int i = 0; i < len; ++i) orig[i] = wbuf[i];
    orig[len] = 0;

    bool fitted = false;
    for (int keep = len - 1; keep >= 0; --keep) {
        int outLen = 0;
        if (keepTail) {
            if (outLen + 3 < cap) {
                wbuf[outLen++] = '.';
                wbuf[outLen++] = '.';
                wbuf[outLen++] = '.';
            }
            const int start = len - keep;
            for (int i = start; i < len && outLen < cap - 1; ++i) {
                wbuf[outLen++] = orig[i];
            }
        } else {
            for (int i = 0; i < keep && outLen < cap - 4; ++i) {
                wbuf[outLen++] = orig[i];
            }
            if (outLen + 3 < cap) {
                wbuf[outLen++] = '.';
                wbuf[outLen++] = '.';
                wbuf[outLen++] = '.';
            }
        }
        wbuf[outLen] = 0;
        const float cw = TextWidth(wbuf);
        if (cw * kMinScale <= maxW) {
            w = cw;
            fitted = true;
            break;
        }
    }

    if (!fitted) {
        // Extremely narrow slot: try "..", ".", or empty so nothing ever overflows maxW.
        for (int dots = 2; dots >= 0; --dots) {
            int i = 0;
            for (; i < dots && i < cap - 1; ++i) wbuf[i] = '.';
            wbuf[i] = 0;
            const float cw = TextWidth(wbuf);
            if (cw * kMinScale <= maxW || dots == 0) {
                w = cw;
                break;
            }
        }
    }

    float sc = 1.f;
    if (w > maxW && w > 1.f) {
        sc = maxW / w;
        if (sc < kMinScale) sc = kMinScale;
    }
    if (outScale) *outScale = sc;
    if (outDrawW) *outDrawW = w * sc;
}

void OpDrawPanel(float x0, float y0, float x1, float y1, uint32_t fill, uint32_t border) {
    FRONTEND.DrawPanel(x0, y0, x1, y1, 0.55f, static_cast<DWORD>(fill));
    FRONTEND.DrawBox(x0, y0, x1, y1, static_cast<DWORD>(border), 1.5f, 0.50f);
    ApplyTextStates();
}

void OpDrawTextLeft(float x, float y, float maxW, uint32_t col, const char *text, int withCursor) {
    CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
    if (!font) return;
    WCHAR wbuf[200];
    WidenSafe(wbuf, 200, text);
    const float availW = (withCursor && maxW > 8.f) ? (maxW - 6.f) : maxW;
    float scale = 1.f;
    float drawW = 0.f;
    FitWideTextToWidth(wbuf, 200, availW, &scale, &drawW, withCursor != 0);
    if (wbuf[0] != 0) {
        font->DrawTextWithShadow(x, y, static_cast<DWORD>(col), wbuf, 0, 0, scale, 1.f);
    }
    if (withCursor && (fmod(PLATFORM.GetSysTimeFloat(), 1.0) < 0.5)) {
        static WCHAR kCursor[] = L"I";
        float cx = x + drawW + 1.f;
        if (cx < x + maxW + 2.f) {
            font->DrawTextWithShadow(cx, y, kCursorCol, kCursor, 0, 0, 1.f, 1.f);
        }
    }
}

void OpDrawTextCenter(float x0, float y0, float x1, float y1, uint32_t col, const char *text) {
    CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
    if (!font) return;
    WCHAR wbuf[200];
    WidenSafe(wbuf, 200, text);
    const float boxW = x1 - x0;
    const float maxW = (boxW > 8.f) ? (boxW - 8.f) : boxW;
    float scale = 1.f;
    float drawW = 0.f;
    FitWideTextToWidth(wbuf, 200, maxW, &scale, &drawW, false);
    if (wbuf[0] == 0) return;
    const float fh = float(font->GetHeight());
    const float tx = x0 + (boxW - drawW) * 0.5f;
    const float ty = y0 + ((y1 - y0) - fh) * 0.5f;
    font->DrawTextWithShadow(tx, ty, static_cast<DWORD>(col), wbuf, 0, 0, scale, 1.f);
}

void OpDrawTextRight(float rightX, float y, float maxW, uint32_t col, const char *text) {
    CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
    if (!font) return;
    WCHAR wbuf[200];
    WidenSafe(wbuf, 200, text);
    float scale = 1.f;
    float drawW = 0.f;
    FitWideTextToWidth(wbuf, 200, maxW, &scale, &drawW, false);
    if (wbuf[0] == 0) return;
    font->DrawTextWithShadow(rightX - drawW, y, static_cast<DWORD>(col), wbuf, 0, 0, scale, 1.f);
}

int OpMouseClicked(float x0, float y0, float x1, float y1) {
    if (FEMESSBOX.BeingDisplayed()) return 0;
    if (PLATFORM.GetSysTimeFloat() - sOpenTime <= 0.25f) return 0;
    return MouseClickedInRect(x0, y0, x1, y1) ? 1 : 0;
}

void OpPlaySound(int kind) {
    if (kind == CE_OUI_SND_MOVE) CFrontEnd::PlaySound(FES_MOVE);
    else if (kind == CE_OUI_SND_BACK) CFrontEnd::PlaySound(FES_BACK);
    else CFrontEnd::PlaySound(FES_SELECT);
}

int OpCopyClip(const char *text) {
    return CE_LobbyCopy(text) != 0 ? 1 : 0;
}

int OpPasteClip(char *out, int cap) {
    if (!out || cap <= 1) return 0;
    return CE_LobbyPaste(out, static_cast<size_t>(cap)) != 0 ? 1 : 0;
}

int OpGetBeConfig(int level, int index, char *out, int cap) {
    if (!out || cap <= 1 || index < 0) return 0;
    out[0] = 0;
    for (SBEConfigLevel *cfg = FRONTEND.mFEPBEConfig.mLevels.First();
         cfg; cfg = FRONTEND.mFEPBEConfig.mLevels.Next()) {
        if (cfg->mLevel == level) {
            int cur = 0;
            for (char **name = cfg->mConfigs.First(); name; name = cfg->mConfigs.Next()) {
                if (*name && **name) {
                    if (cur == index) {
                        snprintf(out, static_cast<size_t>(cap), "%s", *name);
                        return 1;
                    }
                    ++cur;
                }
            }
            break;
        }
    }
    return 0;
}

bool OnlineKeyTrap(WORD key, KeyEventType event);
pKeyTrapper OnlineTrapFn() { return (pKeyTrapper)(void (*)(void))OnlineKeyTrap; }

void SetKeyTrap(bool on) {
    if (on && !sTrapActive) {
        LT.SetKeytrap(OnlineTrapFn());
        sTrapActive = true;
    } else if (!on && sTrapActive) {
        if (LT.GetKeytrap() == OnlineTrapFn()) {
            LT.SetKeytrap(NULL);
        }
        sTrapActive = false;
        sCtrlHeld = false;
        sSuppressCharCode = 0;
    }
}

void OpExitAction(int kind) {
    SetKeyTrap(false);
    if (kind == CE_OUI_EXIT_DIRECT_LAN) {
        sExitingToMultiplayer = false;
        sOpenedDirectLan = true;
        // Preserve sOpenedFromMultiplayer so re-entering Play Online keeps Multiplayer return context.
        if (PortCeNetOpenDirectFromOnline) {
            PortCeNetOpenDirectFromOnline();
        }
    } else if (kind == CE_OUI_EXIT_CLOSE_MP) {
        if (sOpenedDirectLan && PortCeNetShutdown) {
            sOpenedDirectLan = false;
            PortCeNetShutdown();
        }
        sExitingToMultiplayer = true;
        sOpenedFromMultiplayer = false;
        FRONTEND.SetPage(FEP_MULTIPLAYER, 40);
    } else if (kind == CE_OUI_EXIT_CLOSE_NET) {
        sExitingToMultiplayer = false;
        sOpenedFromMultiplayer = false;
    }
}

void EnsureHostOps() {
    if (sOpsInited) return;
    sOpsInited = true;
    static const CeOnlineUiHostOps kOps = {
        OpDrawPanel,
        OpDrawTextLeft,
        OpDrawTextCenter,
        OpDrawTextRight,
        OpMouseClicked,
        OpPlaySound,
        OpCopyClip,
        OpPasteClip,
        OpGetBeConfig,
        OpExitAction
    };
    PortCeOnlineHostInitOps(&kOps);
}

bool OnlineKeyTrap(WORD key, KeyEventType event) {
    if (!PortCeOnlineHostActive() || sExitingToMultiplayer || FEMESSBOX.BeingDisplayed()) return false;
    (void)PortCeOnlineUiKey(key, static_cast<int>(event));
    return false;
}

} // namespace

extern "C" {

void PortCeOnlineUiOpen(void) {
    if (!OnlineOffered()) return;
    EnsureHostOps();
    const bool enteredFromMpPage = (FRONTEND.GetTransitionFrom() == FEP_MULTIPLAYER);
    if (!sNextOpenFromMultiplayer && !enteredFromMpPage) {
        sOpenedFromMultiplayer = false;
    }
    const int fromMp = (sNextOpenFromMultiplayer || sOpenedFromMultiplayer || enteredFromMpPage) ? 1 : 0;
    sNextOpenFromMultiplayer = false;
    sOpenedFromMultiplayer = (fromMp != 0);
    sExitingToMultiplayer = false;
    sOpenTime = PLATFORM.GetSysTimeFloat();
    sLastSelectTime = sOpenTime;
    sLaunchStartedAt = -1.f;
    sLaunchQuitPosted = false;
    sLastMouseX = MOUSE_X;
    sLastMouseY = MOUSE_Y;
    PortCeOnlineHostOpen(fromMp);
    SetKeyTrap(true);
}

void PortCeOnlineUiOpenFromMultiplayer(void) {
    if (!OnlineOffered()) return;
    EnsureHostOps();
    sPendingMultiplayerEntry = true;
    sNextOpenFromMultiplayer = true;
    sOpenedFromMultiplayer = true;
    sOpenedDirectLan = false;
    sExitingToMultiplayer = false;
    CFrontEnd::PlaySound(FES_SELECT);
    FRONTEND.SetPage(FEP_CE, 50);
}

int PortCeOnlineUiTakePendingEntry(void) {
    if (!sPendingMultiplayerEntry) return 0;
    sPendingMultiplayerEntry = false;
    return 1;
}

int PortCeOnlineUiActive(void) {
    if (sExitingToMultiplayer) {
        const bool stillExiting =
            (FRONTEND.GetCurrentPage() == FEP_TRANSITION) &&
            (FRONTEND.GetTransitionFrom() == FEP_CE) &&
            (FRONTEND.GetTransitionTo() == FEP_MULTIPLAYER);
        if (!stillExiting) {
            sExitingToMultiplayer = false;
        }
    }
    return (PortCeOnlineHostActive() || sExitingToMultiplayer) ? 1 : 0;
}

const char *PortCeOnlineUiTitleUtf8(void) {
    return PortCeOnlineHostTitleUtf8();
}

void PortCeOnlineUiUpdate(void) {
    if (sExitingToMultiplayer) {
        const bool stillExiting =
            (FRONTEND.GetCurrentPage() == FEP_TRANSITION) &&
            (FRONTEND.GetTransitionFrom() == FEP_CE) &&
            (FRONTEND.GetTransitionTo() == FEP_MULTIPLAYER);
        if (!stillExiting) {
            sExitingToMultiplayer = false;
        }
        return;
    }
    if (!PortCeOnlineHostActive()) return;
    EnsureHostOps();
    const uint64_t nowMs = static_cast<uint64_t>(PLATFORM.GetSysTimeFloat() * 1000.0);
    PortCeOnlineHostUpdate(nowMs);
    if (CE_LobbyRestartDue()) {
        if (sLaunchStartedAt < 0.f) {
            sLaunchStartedAt = PLATFORM.GetSysTimeFloat();
        }
        if (!sLaunchQuitPosted && PLATFORM.GetSysTimeFloat() - sLaunchStartedAt > 0.35f) {
            sLaunchQuitPosted = true;
            SetKeyTrap(false);
            PostMessage(LT.GetHWnd(), WM_CLOSE, 0, 0);
        }
        return;
    }
    SetKeyTrap(!FEMESSBOX.BeingDisplayed());
}

void PortCeOnlineUiRender(void) {
    if (sExitingToMultiplayer || !PortCeOnlineHostActive() || FEMESSBOX.BeingDisplayed()) return;
    EnsureHostOps();
    ApplyTextStates();

    const int mouseMoved = (MOUSE_X != sLastMouseX || MOUSE_Y != sLastMouseY) ? 1 : 0;
    sLastMouseX = MOUSE_X;
    sLastMouseY = MOUSE_Y;
    int wheel = MOUSE_WHEEL;
    const int canClick = (!FEMESSBOX.BeingDisplayed() && (PLATFORM.GetSysTimeFloat() - sOpenTime > 0.25f)) ? 1 : 0;

    PortCeOnlineHostRender(float(MOUSE_X), float(MOUSE_Y), mouseMoved, &wheel, canClick);
    MOUSE_WHEEL = wheel;

    LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
    LT.SRS(D3DRS_ZENABLE, TRUE);
}

void PortCeOnlineUiButton(int button, float val) {
    (void)val;
    if (!PortCeOnlineHostActive() || sExitingToMultiplayer || FEMESSBOX.BeingDisplayed()) return;
    const float now = PLATFORM.GetSysTimeFloat();
    if (now - sOpenTime < 0.35f) return;

    if (button == BUTTON_FRONTEND_MENU_BACK) {
        PortCeOnlineHostButton(CE_OUI_BTN_BACK);
        return;
    }
    if (button == BUTTON_FRONTEND_MENU_SELECT) {
        if (now - sLastSelectTime < 0.15f) return;
        button = BUTTON_PAUSE_MENU_SELECT;
    }
    if (button == BUTTON_PAUSE_MENU_SELECT) {
        if (now - sLastSelectTime < 0.15f) return;
        sLastSelectTime = now;
        PortCeOnlineHostButton(CE_OUI_BTN_SELECT);
        return;
    }
    if (button == BUTTON_FRONTEND_MENU_UP)    { PortCeOnlineHostButton(CE_OUI_BTN_UP); return; }
    if (button == BUTTON_FRONTEND_MENU_DOWN)  { PortCeOnlineHostButton(CE_OUI_BTN_DOWN); return; }
    if (button == BUTTON_FRONTEND_MENU_LEFT)  { PortCeOnlineHostButton(CE_OUI_BTN_LEFT); return; }
    if (button == BUTTON_FRONTEND_MENU_RIGHT) { PortCeOnlineHostButton(CE_OUI_BTN_RIGHT); return; }
}

int PortCeOnlineUiKey(uint16_t key, int eventType) {
    if (!PortCeOnlineHostActive() || sExitingToMultiplayer || FEMESSBOX.BeingDisplayed()) return 0;
    if (key == DIK_LCONTROL || key == DIK_RCONTROL) {
        if (eventType == KEY_EVENT_DOWN) sCtrlHeld = true;
        else if (eventType == KEY_EVENT_UP) sCtrlHeld = false;
        return 1;
    }
    const bool ctrl = sCtrlHeld || (CE_LobbyCtrlHeld() != 0);
    if (eventType == KEY_EVENT_DOWN) {
        sSuppressCharCode = 0;
        if (key == DIK_ESCAPE)                        return PortCeOnlineHostKey(CE_OUI_KEY_ESC, 0);
        if (key == DIK_V && ctrl)                     return PortCeOnlineHostKey(CE_OUI_KEY_PASTE, 0);
        if (key == DIK_BACK)                          return PortCeOnlineHostKey(CE_OUI_KEY_BACKSPACE, 0);
        if (key == DIK_RETURN || key == DIK_NUMPADENTER) return PortCeOnlineHostKey(CE_OUI_KEY_ENTER, 0);
        if (key == DIK_SPACE) {
            if (PortCeOnlineHostKey(CE_OUI_KEY_CHAR, '\0')) {
                sSuppressCharCode = ' ';
                return 1;
            }
            return 0;
        }
        if (key == DIK_UP)                            return PortCeOnlineHostKey(CE_OUI_KEY_UP, 0);
        if (key == DIK_DOWN || key == DIK_TAB)        return PortCeOnlineHostKey(CE_OUI_KEY_DOWN, 0);
        if (key == DIK_LEFT)                          return PortCeOnlineHostKey(CE_OUI_KEY_LEFT, 0);
        if (key == DIK_RIGHT)                         return PortCeOnlineHostKey(CE_OUI_KEY_RIGHT, 0);
        if (key == DIK_PRIOR)                         return PortCeOnlineHostKey(CE_OUI_KEY_PGUP, 0);
        if (key == DIK_NEXT)                          return PortCeOnlineHostKey(CE_OUI_KEY_PGDN, 0);
        if (key == DIK_T) {
            if (PortCeOnlineHostKey(CE_OUI_KEY_CHAT, 0)) {
                sSuppressCharCode = 't';
                return 1;
            }
            return 0;
        }
        return 0;
    }
    if (eventType == KEY_EVENT_CHAR && !ctrl && key >= 0x20 && key < 0x7f) {
        if (sSuppressCharCode != 0) {
            const uint16_t suppressed = sSuppressCharCode;
            sSuppressCharCode = 0;
            if (key == suppressed || (suppressed == 't' && key == 'T')) {
                return 1;
            }
        }
        return PortCeOnlineHostKey(CE_OUI_KEY_CHAR, static_cast<char>(key));
    }
    return 0;
}

void PortCeOnlineUiClose(void) {
    SetKeyTrap(false);
    if (CE_LobbyRestartDue()) {
        PortCeOnlineHostClosePreservingLaunch();
    } else {
        PortCeOnlineHostClose();
    }
    sPendingMultiplayerEntry = false;
    sNextOpenFromMultiplayer = false;
    sOpenedFromMultiplayer = false;
    sOpenedDirectLan = false;
    sExitingToMultiplayer = false;
    sSuppressCharCode = 0;
    sLaunchStartedAt = -1.f;
    sLaunchQuitPosted = false;
}

// ---------------------------------------------------------------------------
// Main Menu -> Multiplayer (FEP_MULTIPLAYER) Play Online Entry Bar (patch 0380)
// Pure game-heap / zero-STL rendering!
// ---------------------------------------------------------------------------
void PortCeOnlineUiMultiplayerProcess(int active) {
    if (!active || !OnlineOffered() || FEMESSBOX.BeingDisplayed()) return;
    sExitingToMultiplayer = false;
    sOpenedDirectLan = false;
    bool padOpen = false;
    for (int pad = 0; pad < 2; ++pad) {
        if (LT.JoyState(pad) && LT.OldJoyState(pad) && LT.JoyButtonOnce(pad, 3)) {
            padOpen = true;
            break;
        }
    }
    if (PLATFORM.KeyOnce(DIK_O) || padOpen) {
        PortCeOnlineUiOpenFromMultiplayer();
    }
}

void PortCeOnlineUiMultiplayerRender(float transition) {
    if (!OnlineOffered()) return;
    const float fade = RangeTransition(transition, 0.75f, 1.f);
    if (fade <= 0.01f) return;

    const float x0 = 115.f, y0 = 412.f, x1 = 525.f, y1 = 442.f;
    const bool ready = (transition >= 0.99f) && !FEMESSBOX.BeingDisplayed();
    const bool hover = ready && FRONTEND.GetCursorStateInRect(x0, y0, x1, y1);
    const SINT alpha = SINT(fade * 255.f);

    const DWORD bgCol = BlendAlpha2(hover ? 0xd8203858u : 0xc00c1828u, alpha);
    const DWORD brdCol = BlendAlpha2(hover ? kWarn : kCyan, alpha);
    const DWORD txtCol = BlendAlpha2(hover ? kWarn : kWhite, alpha);

    FRONTEND.DrawPanel(x0, y0, x1, y1, 0.30f, bgCol);
    FRONTEND.DrawBox(x0, y0, x1, y1, brdCol, 1.5f, 0.29f);
    ApplyTextStates();
    OpDrawTextCenter(x0, y0, x1, y1, txtCol, "PLAY ONLINE (2-4 PLAYERS) - O / Y BUTTON OR CLICK");
    LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
    LT.SRS(D3DRS_ZENABLE, TRUE);

    if (ready && FRONTEND.MouseClickedInRect(x0, y0, x1, y1)) {
        PortCeOnlineUiOpenFromMultiplayer();
    }
}

} // extern "C"

#endif // !CE_NETWORK_OFF
#endif // CE_VERSION
