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
#include "ce_overlay_box.h"

extern "C" unsigned long long CE_ScreenshotTicks(void);		// ce_screenshot.cpp: SDL's milliseconds
extern "C" int PortCeBugClipStatus(char *out, int n, int *outRecording, float *outFade) __attribute__((weak));
extern "C" int PortCeEditorAllowsBugClipHotkey(void) __attribute__((weak));
bool PortCeLayer();
WCHAR *PortCeSaveErrorText();								// FrontEndText.cpp, patch 0137: TEXT_DB.GetString(FETX_SAVEERROR)

extern "C" int PortCeKeytrapActive(void)
{
	if (PortCeEditorAllowsBugClipHotkey && PortCeEditorAllowsBugClipHotkey())
		return 0;
	return LT.GetKeytrap() != NULL ? 1 : 0;
}

namespace {
const unsigned long long kShownMs = 6000;
const int kChars = 160;
WCHAR note[kChars];
unsigned long long noteFrom, noteUntil;
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
	noteFrom = CE_ScreenshotTicks();
	noteUntil = noteFrom + kShownMs;
}

// After the console (CDXEngine::PostRender), as the photo note: bottom centre, inside the window, scaled down to fit.
void CePlayUnitRenderState() __attribute__((weak));		// ce_playunit.cpp: the held fire's state while a unit is driven (nothing otherwise)

// After the console (CDXEngine::PostRender), as the photo note: in the message box's look (ce_overlay_box.h), above the HUD's
// message bar (the HUD is drawn in a level, except in photo mode, where the low place is kept), shrunk to fit. The note's fade
// (ce_overlay_box.h) ends it: after noteUntil it draws nothing.
void PortCeBugClipRender()
{
	if (!PortCeLayer() || !PortCeBugClipStatus || PortCePhotoSavedNext()) return;
	char raw[128] = "";
	int recording = 0;
	float fade = 0.0f;
	if (!PortCeBugClipStatus(raw, (int)sizeof(raw), &recording, &fade) || !raw[0] || fade <= 0.0f)
		return;
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font) return;
	WCHAR text[128];
	int n = 0;
	for (const char *c = raw; *c && n < 127; ++c)
		text[n++] = font->CanDisplay((WCHAR)(unsigned char)*c) ? (WCHAR)(unsigned char)*c : L'-';
	text[n] = 0;
	if (recording)
	{
		// Compact top-right REC indicator (never blocks central gameplay or menu lists).
		if (FRONTEND.IsRunning())
		{
			SIZE s = {};
			font->GetTextExtent(text, &s);
			const float pad = 6.0f;
			const float w = (float)s.cx + pad * 2.0f;
			const float h = (float)s.cy + pad * 2.0f;
			const float x = 640.0f - w - 10.0f;
			const float y = 10.0f;
			RENDERINFO.SetFogEnabled(false);
			FRONTEND.EnableModulateAlpha();
			LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
			LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
			LT.SRS(D3DRS_ZENABLE, FALSE);
			LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
			RENDERINFO.Apply();
			FRONTEND.DrawPanel(x, y, x + w, y + h, 0.002f, 0xc0200808);
			FRONTEND.DrawBox(x, y, x + w, y + h, 0xffe04040, 2.0f, 0.002f);
			font->DrawTextDynamic(x + pad, y + pad, 0.001f, 1.0f, 1.0f, 0xffffe8e8, text, 1000.0f, 0, 0);
			LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
			LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
			LT.SRS(D3DRS_ZENABLE, TRUE);
			RENDERINFO.Apply();
		}
		else
		{
			const float winW = (float)PLATFORM.GetWindowWidth();
			const float winH = (float)PLATFORM.GetWindowHeight();
			CDXTexture *white = HUD.GetWhiteTexture();
			if (winW <= 0.0f || winH <= 0.0f || !white || white->GetRealWidth() <= 0 || white->GetRealHeight() <= 0)
				return;
			const float scale = (winW / 640.0f < winH / 480.0f) ? (winW / 640.0f) : (winH / 480.0f);
			SIZE s = {};
			font->GetTextExtent(text, &s);
			const float pad = 6.0f * scale;
			const float edge = (2.0f * scale > 1.0f) ? (2.0f * scale) : 1.0f;
			const float w = (float)s.cx * scale + pad * 2.0f;
			const float h = (float)s.cy * scale + pad * 2.0f;
			const float x = winW - w - 10.0f * scale;
			const float y = 10.0f * scale;
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
			const float sw = (float)white->GetRealWidth(), sh = (float)white->GetRealHeight();
			CSPRITERENDERER::DrawColouredSprite(x, y, 0.002f, white, 0xc0200808, w / sw, h / sh, SA_TOP_LEFT);
			CSPRITERENDERER::DrawColouredSprite(x, y, 0.002f, white, 0xffe04040, w / sw, edge / sh, SA_TOP_LEFT);
			CSPRITERENDERER::DrawColouredSprite(x, y + h - edge, 0.002f, white, 0xffe04040, w / sw, edge / sh, SA_TOP_LEFT);
			CSPRITERENDERER::DrawColouredSprite(x, y + edge, 0.002f, white, 0xffe04040, edge / sw, (h - 2.0f * edge) / sh, SA_TOP_LEFT);
			CSPRITERENDERER::DrawColouredSprite(x + w - edge, y + edge, 0.002f, white, 0xffe04040, edge / sw, (h - 2.0f * edge) / sh, SA_TOP_LEFT);
			font->DrawTextWithShadow(x + pad, y + pad, 0xffffe8e8, text, 0, 0.0f, scale, scale);
			LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
			LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
			LT.SRS(D3DRS_ZENABLE, TRUE);
			RENDERINFO.Apply();
		}
	}
	else if (FRONTEND.IsRunning())
	{
		SIZE s = {};
		font->GetTextExtent(text, &s);
		const float scale = (s.cx > 580) ? (580.0f / (float)s.cx) : 1.0f;
		const float pad = 6.0f;
		const float w = (float)s.cx * scale + pad * 2.0f;
		const float h = (float)s.cy * scale + pad * 2.0f;
		const float x = (640.0f - w) * 0.5f;
		const float y = 466.0f - h;
		RENDERINFO.SetFogEnabled(false);
		FRONTEND.EnableModulateAlpha();
		LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
		LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
		LT.SRS(D3DRS_ZENABLE, FALSE);
		LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		RENDERINFO.Apply();
		FRONTEND.DrawPanel(x, y, x + w, y + h, 0.002f, PortCeNoteColour(0xaf000000, fade));
		FRONTEND.DrawBox(x, y, x + w, y + h, PortCeNoteColour(0xff7f7f7f, fade), 2.0f, 0.002f);
		font->DrawTextDynamic(x + pad, y + pad, 0.001f, scale, scale, PortCeNoteColour(0xffffffff, fade), text, 1000.0f, 0, 0);
		LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
		LT.SRS(D3DRS_ZENABLE, TRUE);
		RENDERINFO.Apply();
	}
	else
	{
		PortCeNoteDraw(text, fade, !PortCePhotoOn());
	}
}

void PortCeNoticeRender()
{
	if (CePlayUnitRenderState) CePlayUnitRenderState();
	if (!note[0] || PortCePhotoSavedNext()) return;
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font) return;
	WCHAR text[kChars];
	int n = 0;
	for (const WCHAR *c = note; *c && n < kChars - 1; ++c)
		text[n++] = font->CanDisplay(*c) ? *c : L'-';
	text[n] = 0;
	PortCeNoteDraw(text, PortCeNoteFade(CE_ScreenshotTicks(), noteFrom, noteUntil), !PortCePhotoOn());
}

#endif
