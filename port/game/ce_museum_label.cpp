// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_label.cpp: the museum's words in the HUD's own capsule (ce_museum_label.h).
//
// The capsule is the game's: CHud::RenderObjectiveBar draws the framed bar the radio messages use (the ends, then the dark middle tiled), and the text is the
// small face the radio messages use, white for the name and the notes' grey for the line under it. The place is the radio messages' own, bottom centre, inside the
// HUD's scope (patch 0207) so a wide screen anchors it as it anchors them. The clock is SDL's milliseconds (as the other CE notes use), never the game's, so a
// paused game does not stall it.
#ifdef CE_VERSION
#include "common.h"
#include "game.h"
#include "player.h"
#include "BattleEngine.h"
#include "ltshell.h"
#include "Hud.h"
#include "renderinfo.h"
#include "Texture.h"
#include "DXEngine.h"
#include "hud_scale.h"
#include "ce_museum_hall.h"
#include "ce_museum_cinema.h"
#include "ce_museum_label.h"
#include "ce_museum_label_rules.h"
#include "ce_view_projection.h"
#include <cstring>

extern "C" unsigned PortPresentCount();
extern bool gMirrorPass;

extern float gMessageBoxX, gMessageBoxY, gMessageBoxWidth, gMessageTextY;	// Hud.cpp: where the radio messages' bar and text sit
extern "C" unsigned long long CE_ScreenshotTicks(void);		// ce_screenshot.cpp: SDL's milliseconds
extern "C" bool PortCeMuseumVisitActive(void);				// ce_museum_visit.cpp
extern "C" void PortCeMuseumVisitFrame(void);				// ce_museum_visit_engine.cpp: the visitor's safety, once a frame
extern "C" bool PortCeMuseumCinemaCue(char *, unsigned, char *, unsigned);	// ce_museum_hall.cpp: the screening room's card, while the visitor is in it or its doorway
extern "C" bool PortCeMuseumInteractiveAim(void) __attribute__((weak));		// ce_museum_hall.cpp: true only when aiming at a projector, turntable stud, kiosk or directory
extern "C" bool PortCeMuseumInteractiveCard(float, float, float, float, char *, unsigned, char *, unsigned, char *, unsigned) __attribute__((weak));
extern "C" void PortCeMuseumDrawAimDot(float cx, float cy) __attribute__((weak));
extern "C" void PortCeMuseumDrawExhibitMarker(float cx, float cy, float size) __attribute__((weak));	// ce_museum_hall.cpp: the brass diamond over the named exhibit

namespace {
const int kChars = 96;
const unsigned long long kTitleMs = 5200, kGrowMs = 350;
// The same title asked for again this soon after it was put up keeps its own clock: a visitor stepping on and off a zone's edge
// would otherwise restart it every few frames, and a capsule that keeps restarting is a bar that keeps growing with no words in it.
const unsigned long long kTitleRepeatMs = kTitleMs + 3000;
// HUD.RenderObjectiveBar leaves stage 0 at LT.SetModulate(0) (D3DTOP_MODULATE2X when USE_MODULATE_2X is on, else DXFont::StageColour doubles channels),
// so channels >= 0x80 saturate to pure white. 0xff565656 doubles to 0xac (172), keeping body lines clearly subordinate to pure-white titles.
const DWORD kWhite = 0xffffffff, kGrey = 0xff565656;

struct Label
{
	WCHAR name[kChars], line[kChars], detail[kChars], note[kChars];	// note: an enlarged exhibit's own line, under the others
	float top, radius;
};
Label gLabels[kCeMuseumLabelsMax];
float gAt[kCeMuseumLabelsMax][2];
unsigned gCount;
Label gTitle;
bool gHasTitle;
bool gWayfinding;
bool gInPlace = true;								// the visitor stands in the place the wayfinding title names (the visit's own zone test)
bool gSigns;										// the hall has numbered displays: a card is only ever a sign's own
bool gExitShown;
unsigned long long gExitSince;
bool gInspecting;
bool gViewValid;
unsigned gViewPresent;
float gView[16], gProjection[16];
CeViewViewport gViewport;

unsigned long long gTitleSince;						// 0 until the first frame of the visit draws
int gShown = -1;
unsigned long long gShownSince;

// The text as wide characters; one the small face lacks is drawn as a minus sign (at draw time, when the face exists).
bool Copy(WCHAR *out, const char *text)
{
	int n = 0;
	for (; text && text[n] && n < kChars - 1; ++n)
		out[n] = static_cast<WCHAR>(static_cast<unsigned char>(text[n]));
	out[n] = 0;
	return !text || !text[n];
}

// What the face can draw of a text, into out (kChars long).
void Drawable(WCHAR *out, const WCHAR *text, CDXBitmapFont *font)
{
	int n = 0;
	for (; text[n] && n < kChars - 1; ++n)
		out[n] = font->CanDisplay(text[n]) ? text[n] : L'-';
	out[n] = 0;
}

// C++14 SFINAE pause probe: calls real CGame::IsPaused() in game builds while staying compatible with minimal fake test boundaries.
template <typename G>
auto MuseumGamePaused(G &g, int) -> decltype(g.IsPaused(), bool()) { return g.IsPaused() != 0; }
template <typename G>
bool MuseumGamePaused(G &, long) { return false; }

// Scale a 0xAARRGGBB colour's alpha channel smoothly once text becomes visible (open >= 0.45),
// starting at 55% opacity so text is always readable on the very first frame any bar appears.
// Clamps strictly to [0, 255] so /QIfist round-to-nearest-even can never wrap 255.5 -> 256 -> 0x00!
DWORD TextFadeColour(DWORD base, float open)
{
	if (open >= 1.0f)
		return base;
	const float t = open <= 0.45f ? 0.0f : (open - 0.45f) / 0.55f;
	const float alpha = 0.55f + 0.45f * t;
	int ia = static_cast<int>(alpha * 255.0f);
	if (ia < 0) ia = 0;
	if (ia > 255) ia = 255;
	return (static_cast<DWORD>(ia) << 24) | (base & 0x00ffffffu);
}

// Extracts an exhibit's category/class prefix from an interactive cue's note string when the
// visitor is viewing the exhibit without aiming at its control stud (stripping any " - aim at..."
// or "- archival plate..." suffix and ignoring pure control prompts).
void ExtractExhibitClass(WCHAR *out, const char *note)
{
	out[0] = 0;
	if (!note || !note[0])
		return;
	if (std::strncmp(note, "Press Fire", 10) == 0 ||
	    std::strncmp(note, "Aim at ", 7) == 0 ||
	    std::strncmp(note, "Turntable ", 10) == 0 ||
	    std::strncmp(note, "Battle ", 7) == 0 ||
	    std::strstr(note, "3D display model unavailable") != NULL)
		return;
	const char *cut = std::strstr(note, " - aim at ");
	if (!cut)
		cut = std::strstr(note, " - archival plate");
	int len = cut ? static_cast<int>(cut - note) : static_cast<int>(std::strlen(note));
	while (len > 0 && note[len - 1] == ' ')
		--len;
	if (len <= 0)
		return;
	if (len >= kChars)
		len = kChars - 1;
	for (int i = 0; i < len; ++i)
		out[i] = static_cast<WCHAR>(static_cast<unsigned char>(note[i]));
	out[len] = 0;
}

// Draws a crisp chamfered dark translucent backing quad (0xb0000000) tucked inside the curved
// cyan end-caps BEFORE HUD.RenderObjectiveBar so caption and plaque text never loses contrast
// over bright concept art sheets, skylights, or plinth light strips.
#ifdef D3DFVF_XYZRHW
void MuseumDrawCapsuleBacking(IDirect3DDevice9 *dev, float cx, float cy, float barWidth, float height, float open)
{
	if (!dev || !(barWidth > 0.0f) || !(height > 0.0f) || open < 0.45f)
		return;
	const float t = open >= 1.0f ? 1.0f : (open - 0.45f) / 0.55f;
	int ia = static_cast<int>((0.60f + 0.40f * t) * float(0xb0) + 0.5f);
	if (ia < 0) ia = 0;
	if (ia > 0xb0) ia = 0xb0;
	const DWORD col = static_cast<DWORD>(ia) << 24;

	const float halfW = barWidth * 0.5f;
	const float shoulderX = halfW + height * 0.14f;
	const float tipX = halfW + height * 0.36f;
	const float halfH = height * 0.43f;
	const float tipH = height * 0.20f;

	float px[10] = {
		cx,
		cx - shoulderX, cx + shoulderX,
		cx + tipX,      cx + tipX,
		cx + shoulderX, cx - shoulderX,
		cx - tipX,      cx - tipX,
		cx - shoulderX,
	};
	float py[10] = {
		cy,
		cy - halfH, cy - halfH,
		cy - tipH,  cy + tipH,
		cy + halfH, cy + halfH,
		cy + tipH,  cy - tipH,
		cy - halfH,
	};
	for (int i = 0; i < 10; ++i)
		CeHudScalePoint(&px[i], &py[i]);

	struct BackingVertex { float x, y, z, rhw; DWORD colour; };
	BackingVertex tris[8 * 3];
	for (int k = 0; k < 8; ++k)
	{
		tris[k * 3 + 0] = {px[0],     py[0],     0.0095f, 1.0f, col};
		tris[k * 3 + 1] = {px[k + 1], py[k + 1], 0.0095f, 1.0f, col};
		tris[k * 3 + 2] = {px[k + 2], py[k + 2], 0.0095f, 1.0f, col};
	}

	RENDERINFO.SetShader(NULL);
	RENDERINFO.SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	RENDERINFO.Apply();
	dev->SetPixelShader(NULL);
	LT.D3D_SetTexture(0, NULL);
	LT.D3D_SetTexture(1, NULL);
	LT.SRS(D3DRS_ZENABLE, FALSE);
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	LT.SRS(D3DRS_CULLMODE, D3DCULL_NONE);
	LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 8, tris, sizeof(BackingVertex));
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.SetModulate(0);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	LT.SRS(D3DRS_CULLMODE, D3DCULL_CCW);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
}
#else
inline void MuseumDrawCapsuleBacking(const void *, float, float, float, float, float) {}
#endif

// Breaks text in two at the space nearest its middle when it is wider than room: first gets the part before the space, second the rest.
// Returns false (first = text, second empty) when it fits or cannot be broken.
bool WrapLine(CDXBitmapFont *font, const WCHAR *text, float room, WCHAR *first, WCHAR *second)
{
	int n = 0;
	for (; text[n] && n < kChars - 1; ++n)
		first[n] = text[n];
	first[n] = 0;
	second[0] = 0;
	SIZE whole = {0, 0};
	if (!text[0])
		return false;
	font->GetTextExtent(first, &whole);
	if (float(whole.cx) <= room)
		return false;
	const int at = CeMuseumCaptionSplit(first);
	if (at < 0)
		return false;
	int m = 0;
	for (int i = at + 1; first[i] && m < kChars - 1; ++i)
		second[m++] = first[i];
	second[m] = 0;
	first[at] = 0;
	return true;
}

float LineWidth(CDXBitmapFont *font, const WCHAR *text)
{
	SIZE size = {0, 0};
	if (text[0])
		font->GetTextExtent(const_cast<WCHAR *>(text), &size);
	return float(size.cx);
}

// One capsule: its text lines centred in the bar, the bar as wide as its text needs under the Museum's one caption rule (CeMuseumCaptionLayout),
// grown from the middle by open (0 to 1). cy is the bar's middle. A second line too wide for the bar wraps onto a third.
void Capsule(const Label &label, float cx, float cy, float open)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font || open < 0.45f)
		return;
	WCHAR name[kChars], line[kChars], lineA[kChars], lineB[kChars];
	Drawable(name, label.name, font);
	Drawable(line, label.line, font);
	if (!name[0] && !line[0])
		return;
	const float pitch = float(font->GetHeight()) + 2.0f;
	const float hudScale = CeHudScaleFactor();
	// First as two rows; if the second row overflows the room the rule allows, it is wrapped and the bar measured again with three.
	lineB[0] = 0;
	float rows = line[0] ? 2.0f : 1.0f;
	float widest = LineWidth(font, name) > LineWidth(font, line) ? LineWidth(font, name) : LineWidth(font, line);
	CeMuseumCaptionFit fit = CeMuseumCaptionLayout(widest, 18.0f + rows * pitch, HUD.mViewportWidth, hudScale, open, 0.0f);
	if (line[0] && fit.overflow && WrapLine(font, line, fit.textRoom, lineA, lineB))
	{
		rows = 3.0f;
		widest = LineWidth(font, name);
		if (LineWidth(font, lineA) > widest) widest = LineWidth(font, lineA);
		if (LineWidth(font, lineB) > widest) widest = LineWidth(font, lineB);
		fit = CeMuseumCaptionLayout(widest, 18.0f + rows * pitch, HUD.mViewportWidth, hudScale, open, 0.0f);
	}
	else
	{
		int n = 0;
		for (; line[n]; ++n) lineA[n] = line[n];
		lineA[n] = 0;
	}
	const float height = 18.0f + rows * pitch;
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	RENDERINFO.Apply();
	MuseumDrawCapsuleBacking(LT.GetDevice(), cx, cy, fit.barWidth, height, open);
	HUD.RenderObjectiveBar(cx, cy, fit.barWidth, height, 1.0f);
	RENDERINFO.EnableLighting(false);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.Apply();
	const DWORD white = TextFadeColour(kWhite, open);
	const DWORD grey = TextFadeColour(kGrey, open);
	const float top = cy - rows * pitch * 0.5f;
	const float sx = fit.scaleX;
	font->DrawTextWithShadow(cx - LineWidth(font, name) * sx * 0.5f, top, white, name, 0, 0.0f, sx, 1.0f);
	if (lineA[0])
		font->DrawTextWithShadow(cx - LineWidth(font, lineA) * sx * 0.5f, top + pitch, grey, lineA, 0, 0.0f, sx, 1.0f);
	if (lineB[0])
		font->DrawTextWithShadow(cx - LineWidth(font, lineB) * sx * 0.5f, top + 2.0f * pitch, grey, lineB, 0, 0.0f, sx, 1.0f);
}

// An exhibit's plaque: the radio messages' own bar, in the radio messages' own place (CMessageBox::RenderOverlay), its text left-aligned from
// inside the left bracket. Its rows are kept apart and each says one thing: the name in white, then in grey its class, one fact about it
// (when the visitor is close enough to read one), and a note. The bar keeps the radio's width for short text and grows for longer text only
// as far as the caption rule allows; a fact too long for it wraps onto a second line. open is 0 to 1 (the bar grows from the middle).
void ExhibitCapsule(const Label &label, const WCHAR *classLine, const WCHAR *fact, float open, int number = 0)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font || open < 0.45f)
		return;
	WCHAR name[kChars], row[4][kChars], spare[kChars];
	Drawable(name, label.name, font);
	if (number >= 1 && number <= 9)
	{
		int size = 0;
		while (name[size] && size < kChars - 4) ++size;
		name[size + 3] = 0;
		while (size > 0) { --size; name[size + 3] = name[size]; }
		name[0] = char('0' + number);
		name[1] = ':'; name[2] = ' ';
	}
	// Up to four grey rows under the name: class, fact (one or two lines), note.
	int rowCount = 0;
	const WCHAR *const sources[3] = {classLine, fact, label.note};
	for (int i = 0; i < 3; ++i)
	{
		if (!sources[i] || !sources[i][0])
			continue;
		bool same = false;								// a fact that only repeats the class line is not said twice
		for (int k = 0; k < rowCount && !same; ++k)
		{
			int c = 0;
			while (row[k][c] && sources[i][c] && row[k][c] == sources[i][c]) ++c;
			same = !row[k][c] && !sources[i][c];
		}
		if (!same && rowCount < 4)
			Drawable(row[rowCount++], sources[i], font);
	}
	if (!name[0] && !rowCount)
		return;
	const float viewLeft = HUD.mViewportX;
	const float viewW = HUD.mViewportWidth;
	const float centre = viewLeft + viewW * 0.5f;
	const float radioWidth = 230.0f - HUD.mViewportX * 2 - HUD_OFFSET_X * 2 + gMessageBoxWidth;
	const float hudScale = CeHudScaleFactor();
	auto widestRow = [&]() {
		float w = LineWidth(font, name);
		for (int k = 0; k < rowCount; ++k)
			if (LineWidth(font, row[k]) > w) w = LineWidth(font, row[k]);
		return w;
	};
	float height = 60.0f;
	CeMuseumCaptionFit fit = CeMuseumCaptionLayout(widestRow(), height, viewW, hudScale, open, radioWidth);
	// The longest grey row wraps once if it overflows and there is a row to spare.
	if (fit.overflow && rowCount > 0 && rowCount < 4)
	{
		int longest = 0;
		for (int k = 1; k < rowCount; ++k)
			if (LineWidth(font, row[k]) > LineWidth(font, row[longest])) longest = k;
		if (LineWidth(font, row[longest]) >= LineWidth(font, name) && WrapLine(font, row[longest], fit.textRoom, row[longest], spare))
		{
			for (int k = rowCount; k > longest + 1; --k)
				for (int c = 0; c < kChars; ++c) row[k][c] = row[k - 1][c];
			for (int c = 0; c < kChars; ++c) row[longest + 1][c] = spare[c];
			++rowCount;
		}
	}
	const float rows = 1.0f + float(rowCount);
	if (rows > 3.0f)
		height = 60.0f + (rows - 3.0f) * 15.0f;
	fit = CeMuseumCaptionLayout(widestRow(), height, viewW, hudScale, open, radioWidth);
	const float y = (HUD.mViewportBottom + 16.0f - HUD_OFFSET_Y - 128.0f + 48.0f) + gMessageBoxY - (height - 60.0f) * 0.5f - 12.0f;
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	RENDERINFO.Apply();
	MuseumDrawCapsuleBacking(LT.GetDevice(), centre, y, fit.barWidth, height, open);
	HUD.RenderObjectiveBar(centre, y, fit.barWidth, height, 1.0f);
	RENDERINFO.EnableLighting(false);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.Apply();
	const DWORD white = TextFadeColour(kWhite, open);
	const DWORD grey = TextFadeColour(kGrey, open);
	const float left = centre - fit.fullWidth * 0.5f + CeMuseumCaptionCapInset(height);
	// The radio's own first-line offset (gMessageTextY) for a three-row card, and half a row either way for fewer or more.
	const float top = y - gMessageTextY + (3.0f - rows) * 7.5f;
	font->DrawTextWithShadow(left, top, white, name, 0, 0.0f, fit.scaleX, 1.0f);
	for (int k = 0; k < rowCount; ++k)
		font->DrawTextWithShadow(left, top + 15.0f * float(k + 1), grey, row[k], 0, 0.0f, fit.scaleX, 1.0f);
}

void ExhibitMarker(unsigned index)
{
	if (gMirrorPass || !gViewValid || gViewPresent != PortPresentCount())
		return;
	CeViewPoint pixel;
	const CeViewPoint top = {gAt[index][0], gAt[index][1], gLabels[index].top};
	if (!CeViewProject(gView, gProjection, gViewport, top, &pixel))
		return;
	const float u = (pixel.x - gViewport.x) / gViewport.width;
	const float v = (pixel.y - gViewport.y) / gViewport.height;
	if (!(u > 0.02f && u < 0.98f && v > 0.06f && v < 0.94f))
		return;
	// A small brass diamond hangs just above the named exhibit: drawn as two vertex-coloured triangles at the projected point itself (device
	// pixels, so it sits on the exhibit at every HUD size), where a font glyph used to stand in for a marker.
	if (PortCeMuseumDrawExhibitMarker)
	{
		const float size = gViewport.height > 0.0f ? 6.0f * gViewport.height / 480.0f : 6.0f;
		PortCeMuseumDrawExhibitMarker(pixel.x, pixel.y - size * 1.6f, size);
	}
}

}

// Capture the world camera before the interface replaces it. The marker may
// consume only this frame's matrix and only while its own exhibit list exists.
#include "ce_museum_cinema_panel.inc"

extern "C" void PortCeMuseumViewCapture()
{
	if (gMirrorPass)
		return; // Reflections do not replace the visitor's camera.
	gViewValid = false;
	if (!gCount || !PortCeMuseumVisitActive())
		return;
	D3DVIEWPORT9 viewport;
	if (!LT.GetDevice() || FAILED(LT.GetDevice()->GetViewport(&viewport)))
		return;
	gViewport = {float(viewport.X), float(viewport.Y), float(viewport.Width), float(viewport.Height), viewport.MinZ, viewport.MaxZ};
	D3DMATRIX view, projection;
	RENDERINFO.GetView(&view);
	RENDERINFO.GetProjection(&projection);
	static_assert(sizeof(view) == sizeof(gView) && sizeof(projection) == sizeof(gProjection), "Expected D3D matrices");
	std::memcpy(gView, &view, sizeof(view));
    PortCeMuseumCinemaView(gView, PortPresentCount());
	std::memcpy(gProjection, &projection, sizeof(projection));
	gViewPresent = PortPresentCount();
	gViewValid = true;
}

extern "C" void PortCeMuseumLabelsClear(void)
{
	gCount = 0;
	gSigns = false;
	gInPlace = true;
	gExitShown = false;
	gExitSince = 0;
	gInspecting = false;
	gViewValid = false;
	gHasTitle = false;
	gTitleSince = 0;
	gShown = -1;
	gShownSince = 0;
}

// Whether a wide text equals a narrow one as Copy would write it.
static bool Same(const WCHAR *wide, const char *text)
{
	int n = 0;
	for (; text && text[n] && n < kChars - 1; ++n)
		if (wide[n] != static_cast<WCHAR>(static_cast<unsigned char>(text[n])))
			return false;
	return wide[n] == 0;
}

extern "C" void PortCeMuseumLabelsTitle(const char *title, const char *line)
{
	if (gHasTitle && title && title[0] && Same(gTitle.name, title) && Same(gTitle.line, line) &&
	    (!gTitleSince || CE_ScreenshotTicks() - gTitleSince < kTitleRepeatMs))
	{
		gWayfinding = false;
		return;
	}
	gHasTitle = false;
	gWayfinding = false;
	if (!title || !title[0])
		return;
	if (!Copy(gTitle.name, title) || !Copy(gTitle.line, line))
		return;
	gHasTitle = true;
	gTitleSince = 0;
}

extern "C" void PortCeMuseumLabelsWayfinding(const char *title, const char *guide)
{
	PortCeMuseumLabelsTitle(title, guide);
	gWayfinding = true; // The route at the top must not hide a machine's inspection card below.
}

extern "C" bool PortCeMuseumLabelsAdd(const char *name, const char *line, const char *detail, float x, float y, float top, float radius)
{
	if (gCount >= kCeMuseumLabelsMax || !name || !name[0])
		return false;
	Label &label = gLabels[gCount];
	if (!Copy(label.name, name) || !Copy(label.line, line) || !Copy(label.detail, detail))
		return false;
	label.note[0] = 0;
	label.top = top;
	label.radius = radius;
	gAt[gCount][0] = x;
	gAt[gCount][1] = y;
	++gCount;
	return true;
}

extern "C" void PortCeMuseumLabelsPlace(bool inPlace)
{
	gInPlace = inPlace;
}

extern "C" void PortCeMuseumLabelsSigns(bool signs)
{
	gSigns = signs;
}

extern "C" bool PortCeMuseumLabelsNote(unsigned index, const char *note)
{
	return index < gCount && Copy(gLabels[index].note, note);
}

extern "C" unsigned PortCeMuseumLabelsCount(void)
{
	return gCount;
}

extern "C" int PortCeMuseumLabelsNearest(float x, float y, float forwardX, float forwardY)
{
	return CeMuseumNearestLabel(gAt, gCount, x, y, forwardX, forwardY, gShown);
}

extern "C" void PortCeMuseumLabelsRender(void)
{
	if (!PortCeMuseumVisitActive() || MuseumGamePaused(GAME, 0))
		return;
	PortCeMuseumVisitFrame();
    CinemaPanelDraw();
	if (PortCeMuseumInteractiveAim && PortCeMuseumInteractiveAim() && PortCeMuseumDrawAimDot)
		PortCeMuseumDrawAimDot(HUD.mViewportX + HUD.mViewportWidth * 0.5f, HUD.mViewportY + HUD.mViewportHeight * 0.5f);
	if (!gCount && !gHasTitle)
		return;
	const unsigned long long now = CE_ScreenshotTicks();
	const float cx = HUD.mViewportX + HUD.mViewportWidth * 0.5f;
	// In the screening room or its doorway the cinema's own card is the one shown, and its console panel has the top of the view.
	char cinemaTitle[48] = {}, cinemaLine[96] = {};
	const bool cinema = PortCeMuseumCinemaCue(cinemaTitle, sizeof cinemaTitle, cinemaLine, sizeof cinemaLine);
	if (gHasTitle)
	{
		if (!gTitleSince)
			gTitleSince = now;
		// The title grows open as the radio's bar does, then goes at once when its time is up, when the visitor walks out of the place it names, or into
		// the cinema (CeMuseumTitleOpen). Its clock is SDL's, a real five seconds whatever the frame rate; leaving the place ends it whatever is left.
		const bool elsewhere = CeMuseumTitleElsewhere(cinema, gWayfinding, gInPlace);
		const float open = CeMuseumTitleOpen(now, gTitleSince, kTitleMs, kGrowMs, elsewhere);
		if (elsewhere)
			gHasTitle = false;							// gone for good: the hall names a place again when the visitor walks into it
		if (open > 0.0f)
		{
			// The scope the exhibit labels draw in is anchored at the bottom of the view; the title hangs from the top, so it opens its own, as the HUD's
			// top groups do, and a wide screen scales it about the top edge.
			CeHudScope top(!GAME.IsMultiplayer() && ENGINE.GetNumViewpoints() == 1, HUD.mViewportWidth, HUD.mViewportHeight, cx, HUD.mViewportY + HUD_OFFSET_Y);
			Capsule(gTitle, cx, HUD.mViewportY + HUD_OFFSET_Y + 48.0f, open);
			if (!gWayfinding)
				return;									// while the hall's name is up, no exhibit label draws over the same view
		}
	}
	CPlayer *player = GAME.GetPlayer(0);
	CBattleEngine *engine = player ? player->GetBattleEngine() : NULL;
	if (!engine)
		return;
    if (cinema) {
        Label card={}; Copy(card.name,cinemaTitle); Copy(card.line,cinemaLine);
        ExhibitCapsule(card,card.line,NULL,1.0f); return;
    }
	const FVector position = engine->GetPos();
	const FVector heading = engine->GetOrientation().GetColumnY();
	// With the nose well up or down (jet mode over the hill or the sea) the machines on the ground are not what the visitor looks at: no name is held over that view.
	const bool level = CeMuseumLookingLevel(heading.X, heading.Y, heading.Z);
	int number = 0;
	const int sign = level ? CeMuseumHallSignLabel(heading.X, heading.Y, gShown, &number) : -1;
	const bool atSign = sign >= 0 && unsigned(sign) < gCount;
	const bool exit = level && !atSign &&
	                  CeMuseumHallFacesExit(position.X, position.Y, heading.X, heading.Y, gExitShown);
	if (exit && !gExitShown) gExitSince = now;
	gExitShown = exit;
	// In a hall of numbered displays only a sign names a machine: the in-view rule would put a far display's card up from the entrance.
	const int nearest = CeMuseumCardLabel(gSigns, atSign ? sign : -1,
	                                      level && !exit ? PortCeMuseumLabelsNearest(position.X, position.Y, heading.X, heading.Y) : -1);
	if (nearest != gShown)
	{
		gInspecting = false;
		gShown = nearest;
		gShownSince = now;
	}
	if (gExitShown)
	{
		Label exitLabel = {};
		Copy(exitLabel.name, "Exit gallery");
		Copy(exitLabel.line, "Pause, then choose Leave Museum.");
		ExhibitCapsule(exitLabel, exitLabel.line, NULL, CeMuseumCapsuleOpen(now, gExitSince, 0, kGrowMs, false));
		return;
	}
	if (gShown >= 0)
	{
		const Label &label = gLabels[gShown];
		gInspecting = atSign || CeMuseumInspectionNear(position.X - gAt[gShown][0], position.Y - gAt[gShown][1], label.radius, gInspecting);
		// Name and class always; the one fact only once the visitor is close enough to be reading the plaque.
		ExhibitCapsule(label, label.line, gInspecting ? label.detail : NULL, CeMuseumCapsuleOpen(now, gShownSince, 0, kGrowMs, false), atSign ? number : 0);
		ExhibitMarker(static_cast<unsigned>(gShown));
		return;
	}
	if (level && PortCeMuseumInteractiveCard)
	{
		char ititle[kChars] = {}, iline[kChars] = {}, inote[kChars] = {};
		if (PortCeMuseumInteractiveCard(position.X, position.Y, heading.X, heading.Y,
		                                ititle, sizeof ititle, iline, sizeof iline, inote, sizeof inote))
		{
			Label card = {};
			WCHAR iclass[kChars] = {};
			Copy(card.name, ititle);
			Copy(card.line, iline);
			// How to work a fixture is said only while the visitor is aiming at its control: walking past, the card names it and no more.
			if (PortCeMuseumInteractiveAim && PortCeMuseumInteractiveAim())
				Copy(card.note, inote);
			else
				ExtractExhibitClass(iclass, inote);
			ExhibitCapsule(card, iclass[0] ? iclass : card.line, iclass[0] ? card.line : NULL, 1.0f);
		}
	}
}
#endif
