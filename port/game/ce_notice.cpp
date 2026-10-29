// SPDX-License-Identifier: GPL-3.0-only
// CE save error notice (patch 0137). The retail PC code saves the career and the options file in two places without looking at
// the result: the pause menu's Continue (CPauseMenu::Resume) and the language change (CFEPMain::Process). Since patch 0117 a
// failed write leaves the old file whole, and this tells the player it happened, with the front end's own save error line (the
// text its save page shows, in the language the player chose): in a level as a note at the bottom of the screen for a few seconds
// (this file), in the front end as that page's message box (patch 0137's hunk in FEPMain.cpp, which asks for the text here).
//
// Never in faithful play: nothing is kept, asked or drawn where the CE layer is off (a tape, a recording, probes or the virtual
// clocks), so a replay sees the retail code's silence. The note's clock is SDL's milliseconds, never the game's, and the note
// is not drawn into a frame that is being saved as a screenshot.
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "ltshell.h"
#include "Hud.h"
#include "SpriteRenderer.h"
#include "renderinfo.h"
#include "Texture.h"
#include "ce_photo.h"
#include "ce_notice.h"

extern "C" unsigned long long CE_ScreenshotTicks(void);		// ce_screenshot.cpp: SDL's milliseconds
bool PortCeLayer();
WCHAR *PortCeSaveErrorText();								// FrontEndText.cpp, patch 0137: TEXT_DB.GetString(FETX_SAVEERROR)

namespace {
const unsigned long long kShownMs = 6000;
const int kChars = 160;
WCHAR note[kChars];
unsigned long long noteUntil;
bool optionsFailed;
}

void PortCeOptionsWriteFailed() { optionsFailed = true; }

bool PortCeTakeOptionsFailure()
{
	const bool failed = optionsFailed;
	optionsFailed = false;
	return failed;
}

bool PortCeSaveErrorMessage(WCHAR *out, int n)
{
	if (!out || n <= 0) return false;
	out[0] = 0;
	if (!PortCeLayer()) return false;
	const WCHAR *text = PortCeSaveErrorText();
	if (!text || !text[0]) return false;
	int i = 0;
	for (; text[i] && i < n - 1; ++i) out[i] = text[i];
	out[i] = 0;
	return true;
}

void PortCeSaveFailedNotice()
{
	if (!PortCeSaveErrorMessage(note, kChars)) return;
	noteUntil = CE_ScreenshotTicks() + kShownMs;
}

// After the console (CDXEngine::PostRender), as the photo note: bottom centre, inside the window, scaled down to fit.
void PortCeNoticeRender()
{
	if (!note[0] || CE_ScreenshotTicks() >= noteUntil || PortCePhotoSavedNext()) return;
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	CDXTexture *white = HUD.GetWhiteTexture();
	if (!font || !white || white->GetRealWidth() <= 0 || white->GetRealHeight() <= 0 || font->GetHeight() <= 0) return;
	const float w = float(PLATFORM.GetWindowWidth()), h = float(PLATFORM.GetWindowHeight());
	if (w <= 0.f || h <= 0.f) return;
	const float scale = (w / 640.f < h / 480.f) ? w / 640.f : h / 480.f;
	WCHAR text[kChars];
	int n = 0;
	for (const WCHAR *c = note; *c && n < kChars - 1; ++c)
		text[n++] = font->CanDisplay(*c) ? *c : L'-';
	text[n] = 0;
	SIZE extent;
	font->GetTextExtent(text, &extent);
	float textScale = scale;
	if (float(extent.cx) * textScale > w - 32.f * scale && extent.cx > 0) textScale = (w - 32.f * scale) / float(extent.cx);
	const float tw = float(extent.cx) * textScale, th = float(font->GetHeight()) * textScale;
	const float pad = 6.f * scale, x = (w - tw) * 0.5f, y = h - 40.f * scale - th;
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
	CSPRITERENDERER::DrawColouredSprite(x - pad, y - pad, 0.001f, white, 0xb4000000, (tw + 2.f * pad) / white->GetRealWidth(),
									   (th + 2.f * pad) / white->GetRealHeight());
	font->DrawTextWithShadow(x, y, 0xffffffff, text, 0, 0.f, textScale, textScale);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.Apply();
}

#endif
