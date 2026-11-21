// SPDX-License-Identifier: GPL-3.0-only
// The timed notes CE draws over a level (the photo and screenshot notes of ce_photo.cpp, the save error notice of ce_notice.cpp)
// in the look of the original's message box (CFEMessBox::Render): its translucent panel (0xaf000000), its two-pixel outline in
// 0xff7f7f7f and white text with a shadow, 8 pixels of padding, the whole scaled with the window, and a fade in and out. The box
// has no button: it goes by itself. The outline is four strips of the HUD's white texture, as the console's rule is drawn, since
// the front end's panel texture is not loaded in a level. Drawing only; nothing here is game state.
#ifndef PORT_CE_OVERLAY_BOX_H
#define PORT_CE_OVERLAY_BOX_H
#ifdef CE_VERSION

#include "common.h"
#include "ltshell.h"
#include "Hud.h"
#include "SpriteRenderer.h"
#include "renderinfo.h"
#include "Texture.h"

// The fade's length in SDL milliseconds: the message box's ten frames at 50 a second.
const unsigned long long kCeNoteFadeMs = 200;

// 0 to 1: how much of the note shows at now, for a note that came at from and goes at until.
inline float PortCeNoteFade(unsigned long long now, unsigned long long from, unsigned long long until)
{
	if (now >= until || now < from) return 0.f;
	float fade = 1.f;
	if (now - from < kCeNoteFadeMs) fade = float(now - from) / float(kCeNoteFadeMs);
	if (until - now < kCeNoteFadeMs && float(until - now) / float(kCeNoteFadeMs) < fade) fade = float(until - now) / float(kCeNoteFadeMs);
	return fade;
}

// A colour with its alpha byte scaled by fade (0 to 1). The scale is a whole number of 256ths, clamped to 0..256, and the alpha is
// worked in integers, so that a float-to-integer conversion that rounds (the game runs the x87 unit in round-to-nearest, 0x007f: a
// full-strength 255 + 0.5 came out as 256, which wrapped the alpha byte to 0 and made the whole note vanish) can move the result by
// one 256th at most and never past the colour's own alpha.
inline DWORD PortCeNoteColour(DWORD colour, float fade)
{
	int scale = int(fade * 256.f + 0.5f);
	if (scale > 256) scale = 256;
	if (scale < 0) scale = 0;
	const unsigned alpha = ((colour >> 24) * unsigned(scale)) >> 8;
	return (colour & 0x00ffffff) | (alpha << 24);
}

// Draws a centred note above the message bar when the HUD is visible, or nearer the bottom of a photo.
// A long line wraps at the space nearest its middle and shrinks only if it still does not fit.
inline void PortCeNoteDraw(const WCHAR *text, float fade, bool hudVisible)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	CDXTexture *white = HUD.GetWhiteTexture();
	if (!font || !white || white->GetRealWidth() <= 0 || white->GetRealHeight() <= 0 || font->GetHeight() <= 0 || fade <= 0.f) return;
	const float w = float(PLATFORM.GetWindowWidth()), h = float(PLATFORM.GetWindowHeight());
	if (w <= 0.f || h <= 0.f) return;
	const float scale = (w / 640.f < h / 480.f) ? w / 640.f : h / 480.f;
	const float bottomUnits = hudVisible ? 89.f : 40.f;
	const float pad = 8.f * scale;
	// Leave each lower instrument its own strip when the HUD is up. The space between them grows on wide screens.
	const float sideMargin = (hudVisible ? 120.f : 16.f) * scale;
	const float room = w - 2.f * (sideMargin + pad);
	WCHAR lines[2][200];
	int n = 0;
	for (; text[n] && n < 199; ++n) lines[0][n] = text[n];
	lines[0][n] = 0;
	lines[1][0] = 0;
	SIZE extent = {0, 0};
	font->GetTextExtent(lines[0], &extent);
	if (float(extent.cx) * scale > room && n > 1)
	{
		int split = -1;
		for (int i = 1; i < n - 1; ++i)
			if (lines[0][i] == L' ' && (split < 0 || (i - n / 2 < 0 ? n / 2 - i : i - n / 2) < (split - n / 2 < 0 ? n / 2 - split : split - n / 2)))
				split = i;
		if (split > 0)
		{
			int m = 0;
			for (int i = split + 1; i < n; ++i) lines[1][m++] = lines[0][i];
			lines[1][m] = 0;
			lines[0][split] = 0;
		}
	}
	const int count = lines[1][0] ? 2 : 1;
	float widest = 0.f;
	SIZE sizes[2] = {{0, 0}, {0, 0}};
	for (int i = 0; i < count; ++i)
	{
		font->GetTextExtent(lines[i], &sizes[i]);
		if (float(sizes[i].cx) > widest) widest = float(sizes[i].cx);
	}
	float textScale = scale;
	if (widest * textScale > room && widest > 0.f) textScale = room / widest;
	const float lineHeight = float(font->GetHeight()) * textScale;
	const float edge = (2.f * scale > 1.f) ? 2.f * scale : 1.f;
	const float tw = widest * textScale, th = lineHeight * float(count);
	const float left = (w - tw) * 0.5f - pad, top = h - bottomUnits * scale - th - pad, boxW = tw + 2.f * pad, boxH = th + 2.f * pad;
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
	const float rw = float(white->GetRealWidth()), rh = float(white->GetRealHeight());
	const DWORD panel = PortCeNoteColour(0xaf000000, fade), rule = PortCeNoteColour(0xff7f7f7f, fade);
	CSPRITERENDERER::DrawColouredSprite(left, top, 0.001f, white, panel, boxW / rw, boxH / rh);
	CSPRITERENDERER::DrawColouredSprite(left, top, 0.001f, white, rule, boxW / rw, edge / rh);
	CSPRITERENDERER::DrawColouredSprite(left, top + boxH - edge, 0.001f, white, rule, boxW / rw, edge / rh);
	CSPRITERENDERER::DrawColouredSprite(left, top + edge, 0.001f, white, rule, edge / rw, (boxH - 2.f * edge) / rh);
	CSPRITERENDERER::DrawColouredSprite(left + boxW - edge, top + edge, 0.001f, white, rule, edge / rw, (boxH - 2.f * edge) / rh);
	for (int i = 0; i < count; ++i)
		font->DrawTextWithShadow((w - float(sizes[i].cx) * textScale) * 0.5f, top + pad + lineHeight * float(i), PortCeNoteColour(0xffffffff, fade),
								 lines[i], 0, 0.f, textScale, textScale);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.Apply();
}

#endif
#endif
