// SPDX-License-Identifier: GPL-3.0-only
// Play as a unit: what the player sees of it (ce_playunit_hud.h). ce_playunit.cpp decides what to show and calls these from the render hook of ce_notice.cpp (after the
// console, CDXEngine::PostRender); this file only draws, with the HUD's own textures and the game's small face, and keeps the notice's text and clock. Nothing here
// reads or changes the simulation, the controls or a save, and nothing is drawn while no unit is taken and no notice is up.
//
// The notice is the HUD's message capsule, the one the mission radio's text sits in (CHud::RenderObjectiveBar, the textures CMessageBox draws with): its caps turn in,
// the body grows to the text, the text shows, and at the end it closes the same way. Its clock is SDL's milliseconds, never the game's, counted only while it is drawn (a
// pause, a menu or photo mode stops it, so a notice is never lost behind one). It is laid out on the HUD's
// 640x480 frame, placed from the top or bottom edge as the HUD places its groups, and scaled as the HUD is (the HUD Size setting: CeHudScaleFactor), so that it is the radio capsule's size at every resolution.
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "ltshell.h"
#include "Hud.h"
#include "SpriteRenderer.h"
#include "renderinfo.h"
#include "Texture.h"
#include "hud_scale.h"
#include "ce_photo.h"
#include "ce_playunit_hud.h"
#include "ce_playunit.h"
#include "ce_playunit_rules.h"
#include "ce_playunit_tactics.h"

#include <math.h>
#include <string.h>

extern "C" unsigned long long CE_ScreenshotTicks(void);		// ce_screenshot.cpp: SDL's milliseconds

namespace
{
const unsigned long long kShownMs = 4000;	// how long a notice stays (the house style's in-mission notices: four to six seconds)
const unsigned long long kShownLongMs = 6000;	// a take's notice, which also says the way back: the upper bound
const unsigned long long kCapsMs = 150;		// the caps turning in, then the box with its text, then the same backwards at the end
const unsigned long long kGrowMs = 200;		// how long a replaced notice may count as still opening (CePlayUnitNoticeAgeOnReplace)
const unsigned long long kStepMs = 100;		// the most one drawn frame adds to a notice's age (a frame after a pause adds no more than this)
const int kChars = 160;
// Where the capsule sits on the HUD's 640x480 frame: where the radio's capsule is (bottom centre) while the unit's view has no HUD, and at the top centre, clear of
// the radio, the HUD's alert words, its corner groups, the objective counter at the top right and the reticle's ring, while the Battle Engine's HUD is up; the
// widest the text may be, and the narrowest a line may be squeezed before it is cut (the menus' rule, kCeValueMinScale).
const float kRadioY = 435.f, kPromptY = 450.f, kTopY = 72.f, kMaxWidth = 540.f, kMinSqueeze = 0.72f;
// The colours: content white, a note grey (lighter than the menus' note grey: it sits on the sky), the HUD's alert red.
const unsigned kWhite = 0xffffffff, kNote = 0xffc0c0c0, kAlert = 0xffdc3c3c;
// The Battle Engine's own weapon-state words (CHud::Render00485d50, FONT_NORMAL under the view's middle): red for a fault (overheated, low armour), green for a
// state that passes (low energy recharging). A driven unit's gun says its state in the same face and colours.
const unsigned kWeaponFault = 0xffff3020, kWeaponWait = 0xff30ff20;
// The prompt's pairs as the editor's prompt line draws them (ce_editor_ui.cpp, PromptPair): the key white, the action grey, a small gap between, a wider one
// between pairs; the unit's name first, white, in the HUD's small face so name, armour, and control hints stay balanced and visible together.
const unsigned kPromptKey = 0xfff4f4f4, kPromptAction = 0xffd4d4d4;
const float kKeyGap = 5.f, kPairGap = 12.f, kPromptMaxWidth = 600.f;
WCHAR gLine[2][kChars];
unsigned long long gShown, gAge, gLast;		// the notice's length, how long it has been drawn, the clock at its last frame
int gDrawnAtTop = -1;						// where the notice was last drawn (1 the top, 0 the bottom, -1 not yet): a notice that moves opens afresh where it now is
WCHAR gPrompt[kChars];						// the one prompt line while a unit is driven (the way back), shown when no notice is
unsigned long long gPromptAge, gPromptLast;

void Copy(WCHAR *to, const char *from)
{
	int n = 0;
	for (const char *c = from; c && *c && n < kChars - 1; ++c) to[n++] = WCHAR((unsigned char)*c);
	to[n] = 0;
}

// The HUD's scale (1 at 640x480 and for the Original HUD size), as the HUD's own passes use it.
// The widest a capsule's text may be at the top of the screen, in HUD units, so that the capsule (centred, with its caps) never reaches the mission's objective
// box in the top right corner (CHud::Render00483530: centred at the window's width less 90, its width the panel's, anchored to the right edge and scaled with the
// HUD) nor, mirrored, the left edge. kCapsuleMargin covers the two capsules' caps and a gap between them.
const float kCapsuleMargin = 80.f;
// The bottom of the objective box on the screen (0 when it is shut): CHud::Render00483530 centres it at 0.6 of its height plus 40 down, its height the faded-in
// lines of the small face plus 5.
float ObjectiveBottom(float s)
{
	if (HUD.mObjectivePanelOpening == 0.f) return 0.f;
	CDXBitmapFont *small = PLATFORM.Font(FONT_SMALL);
	float total = 5.f;
	for (int i = 0; i < 4; ++i) total += (small ? float(small->GetHeight()) : 10.f) * HUD.mUnknown064[i].mFadeIn;
	return (total * 0.6f + 40.f + total * 0.5f + 14.f) * s;
}

float CapsuleRoom(float w, float s)
{
	const float panel = HUD.mObjectivePanelOpening != 0.f ? HUD.mObjectivePanelWidth : 0.f;
	const float rightEdge = panel > 0.f ? w - (90.f + panel) * s : w;		// the screen x where the objective box begins (or the screen's edge)
	const float half = rightEdge - w * 0.5f;
	const float room = 2.f * half / s - 16.f - kCapsuleMargin;
	return room > 60.f ? room : 60.f;
}

float HudScale(float w, float h)
{
	CeHudScope scope(1, w, h, 0.f, 0.f);
	const float s = CeHudScaleActive() ? CeHudScaleFactor() : 1.f;
	return s > 0.f ? s : 1.f;
}

// The text as the face can draw it: a character it lacks is shown as a dash.
void Displayable(CDXBitmapFont *font, const WCHAR *in, WCHAR *out)
{
	int n = 0;
	for (const WCHAR *c = in; *c && n < kChars - 1; ++c) out[n++] = font->CanDisplay(*c) ? *c : L'-';
	out[n] = 0;
}

// A line fitted to the capsule's widest text (on the HUD's 640 frame): as it is when it fits, squeezed across down to kMinSqueeze when it does not, and cut with "..."
// when even that is too wide. *squeeze is the horizontal scale to draw it with; the width returned is what it then takes.
float Fit(CDXBitmapFont *font, WCHAR *text, float *squeeze, float maxWidth = kMaxWidth, float minSqueeze = kMinSqueeze)
{
	SIZE e = {0, 0};
	font->GetTextExtent(text, &e);
	*squeeze = 1.f;
	if (float(e.cx) <= maxWidth) return float(e.cx);
	if (float(e.cx) * minSqueeze <= maxWidth)
	{
		*squeeze = maxWidth / float(e.cx);
		return maxWidth;
	}
	*squeeze = minSqueeze;
	int n = 0;
	while (text[n]) ++n;
	const WCHAR dot = font->CanDisplay(L'.') ? L'.' : L'-';
	while (n > 3)
	{
		--n;
		text[n] = 0;
		WCHAR trial[kChars];
		int m = 0;
		for (; m < n && m < kChars - 4; ++m) trial[m] = text[m];
		while (m > 0 && trial[m - 1] == L' ') --m;
		trial[m] = trial[m + 1] = trial[m + 2] = dot;
		trial[m + 3] = 0;
		font->GetTextExtent(trial, &e);
		if (float(e.cx) * minSqueeze <= maxWidth)
		{
			for (int i = 0; i <= m + 3; ++i) text[i] = trial[i];
			return float(e.cx) * minSqueeze;
		}
	}
	return float(e.cx) * minSqueeze;
}

// The HUD's own 2D states (alpha blend, no fog or lighting, clamped and filtered textures: without the clamp the capsule's textures show a hairline at their
// edges), with the depth test off.
void OverlayStates()
{
	HUD.SetOverlayRenderStates();
	LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.Apply();
}

void RestoreStates()
{
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.Apply();
}

struct CeHudQuadVertex
{
	float x, y, z, rhw;
	DWORD color;
};

void HudFillRect(float x, float y, float w, float h, unsigned colour)
{
	if (w <= 0.f || h <= 0.f) return;
	LPDIRECT3DDEVICE9 dev = LT.GetDevice();
	if (!dev) return;
	const CeHudQuadVertex v[4] = {
		{x,     y,     0.005f, 1.f, colour},
		{x + w, y,     0.005f, 1.f, colour},
		{x,     y + h, 0.005f, 1.f, colour},
		{x + w, y + h, 0.005f, 1.f, colour},
	};
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	LT.SRS(D3DRS_LIGHTING, FALSE);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SRS(D3DRS_ZENABLE, FALSE);
	LT.SRS(D3DRS_CULLMODE, D3DCULL_NONE);
	LT.SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(CeHudQuadVertex));
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

void DrawHudLineText(CDXBitmapFont *font, float x, float y, unsigned colour, const char *ascii, float s, float maxW)
{
	if (!font || !ascii || !ascii[0]) return;
	WCHAR raw[kChars], shown[kChars];
	Copy(raw, ascii);
	Displayable(font, raw, shown);
	float squeeze = 1.f;
	if (maxW > 10.f) Fit(font, shown, &squeeze, maxW / s, 0.56f);
	OverlayStates();
	font->DrawTextWithShadow(x, y, colour, shown, 0, 0.f, s * squeeze, s);
}

unsigned LerpArgb(unsigned c0, unsigned c1, float t)
{
	if (t <= 0.f) return c0;
	if (t >= 1.f) return c1;
	const auto ch = [t](unsigned a, unsigned b) -> unsigned {
		const float v = float(a) + (float(b) - float(a)) * t;
		return v <= 0.f ? 0u : (v >= 255.f ? 255u : unsigned(v + 0.5f));
	};
	return (ch((c0 >> 24) & 0xffu, (c1 >> 24) & 0xffu) << 24)
	     | (ch((c0 >> 16) & 0xffu, (c1 >> 16) & 0xffu) << 16)
	     | (ch((c0 >> 8) & 0xffu, (c1 >> 8) & 0xffu) << 8)
	     | ch(c0 & 0xffu, c1 & 0xffu);
}

void BeginHudUntextured()
{
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	LT.SRS(D3DRS_LIGHTING, FALSE);
	LT.SRS(D3DRS_FOGENABLE, FALSE);
	LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SRS(D3DRS_ZENABLE, FALSE);
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	LT.SRS(D3DRS_CULLMODE, D3DCULL_NONE);
	LT.SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}

void EndHudUntextured()
{
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

void DrawNineSliceGlassFill(LPDIRECT3DDEVICE9 dev, const CePlayUnitNineSlice &ns,
                            unsigned topCol, unsigned botCol)
{
	const int kSegs = 8;
	const float inset = ns.glowPad + ns.strokeWidth * 0.45f;
	const float rFill = ns.radius - inset > 1.f ? (ns.radius - inset) : 1.f;
	const float cx = ns.x + ns.w * 0.5f;
	const float cy = ns.y + ns.h * 0.5f;
	const float invH = ns.h > 1.f ? (1.f / ns.h) : 0.f;

	float tlx[kSegs + 1], tly[kSegs + 1];
	for (int k = 0; k <= kSegs; ++k)
	{
		const float th = 3.14159265f + (1.57079633f * float(k) / float(kSegs));
		tlx[k] = ns.leftCenterX + rFill * cosf(th);
		tly[k] = ns.topCenterY + rFill * sinf(th);
	}

	CeHudQuadVertex fan[1 + (kSegs + 1) * 4 + 1];
	int n = 0;
	fan[n++] = {cx, cy, 0.009f, 1.f, LerpArgb(topCol, botCol, 0.5f)};
	auto emit = [&](float px, float py) {
		fan[n++] = {px, py, 0.009f, 1.f, LerpArgb(topCol, botCol, (py - ns.y) * invH)};
	};
	for (int k = 0; k <= kSegs; ++k) emit(tlx[k], tly[k]);
	for (int k = kSegs; k >= 0; --k) emit(2.f * cx - tlx[k], tly[k]);
	for (int k = 0; k <= kSegs; ++k) emit(2.f * cx - tlx[k], 2.f * cy - tly[k]);
	for (int k = kSegs; k >= 0; --k) emit(tlx[k], 2.f * cy - tly[k]);
	fan[n++] = fan[1];

	dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, n - 2, fan, sizeof(CeHudQuadVertex));
}

void DrawNineSliceBracketSide(LPDIRECT3DDEVICE9 dev, const CePlayUnitNineSlice &ns,
                              bool mirrorRight, float halfW, unsigned col)
{
	const int kSegs = 8;
	const float cx = ns.x + ns.w * 0.5f;
	const float cy = ns.y + ns.h * 0.5f;
	const float coreHalf = ns.strokeWidth * 0.5f;
	const float rMid = ns.radius - ns.glowPad - coreHalf > 1.f ? (ns.radius - ns.glowPad - coreHalf) : 1.f;
	const float lipLen = ns.bodySpanW * 0.14f < ns.radius * 0.60f ? (ns.bodySpanW * 0.14f) : (ns.radius * 0.60f);

	struct SpinePt { float x, y, nx, ny, sw; };
	SpinePt pts[2 + (kSegs + 1) * 2];
	int count = 0;
	if (lipLen > 1.f)
		pts[count++] = {ns.leftCenterX + lipLen, ns.topCenterY - rMid, 0.f, -1.f, 0.35f};
	for (int k = kSegs; k >= 0; --k)
	{
		const float frac = float(k) / float(kSegs);
		const float th = 3.14159265f + 1.57079633f * frac;
		const float c = cosf(th), s = sinf(th);
		const float sw = 1.0f + 0.22f * sinf(frac * 3.14159265f);
		pts[count++] = {ns.leftCenterX + rMid * c, ns.topCenterY + rMid * s, c, s, sw};
	}
	for (int k = 0; k <= kSegs; ++k)
	{
		const float frac = float(k) / float(kSegs);
		const float th = 3.14159265f + 1.57079633f * frac;
		const float c = cosf(th), s = sinf(th);
		const float sw = 1.0f + 0.22f * sinf(frac * 3.14159265f);
		pts[count++] = {ns.leftCenterX + rMid * c, 2.f * cy - (ns.topCenterY + rMid * s), c, -s, sw};
	}
	if (lipLen > 1.f)
		pts[count++] = {ns.leftCenterX + lipLen, 2.f * cy - (ns.topCenterY - rMid), 0.f, 1.f, 0.35f};

	CeHudQuadVertex strip[(2 + (kSegs + 1) * 2) * 2];
	const float minX = ns.x, maxX = ns.x + ns.w;
	const float minY = ns.y, maxY = ns.y + ns.h;
	auto clampBox = [&](float &px, float &py) {
		if (px < minX) px = minX;
		if (px > maxX) px = maxX;
		if (py < minY) py = minY;
		if (py > maxY) py = maxY;
	};
	for (int i = 0; i < count; ++i)
	{
		float px = pts[i].x, py = pts[i].y;
		float nx = pts[i].nx, ny = pts[i].ny;
		if (mirrorRight) { px = 2.f * cx - px; nx = -nx; }
		const float hw = halfW * pts[i].sw;
		float ox = px + nx * hw, oy = py + ny * hw;
		float ix = px - nx * hw, iy = py - ny * hw;
		clampBox(ox, oy);
		clampBox(ix, iy);
		strip[i * 2 + 0] = {ox, oy, 0.008f, 1.f, col};
		strip[i * 2 + 1] = {ix, iy, 0.008f, 1.f, col};
	}
	dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, (count - 1) * 2, strip, sizeof(CeHudQuadVertex));
}

// True 9-slice mirrored holographic capsule bracket drawer: fixed-radius circular corner arcs
// that NEVER stretch vertically, clean straight vertical rails for tall panels, exact mirrored
// left/right symmetry, and strict bounds containment inside [x0, x0 + totalW] x [y0, y0 + height].
void DrawNineSliceCapsuleArt(float x0, float y0, float totalW, float height, float progress,
                             unsigned glassFill, bool isForseti, float s, float desiredR = 0.f)
{
	if (height < 10.f) height = 10.f;
	if (totalW < 12.f) totalW = 12.f;
	LPDIRECT3DDEVICE9 dev = LT.GetDevice();
	if (!dev) return;

	float open = progress > 1.f ? 1.f : (progress < 0.f ? 0.f : progress);
	const float cx = x0 + totalW * 0.5f;
	const float drawW = open >= 1.f ? totalW : (totalW * (0.20f + 0.80f * open));
	const float drawX = cx - drawW * 0.5f;
	const float rReq = desiredR > 0.f ? desiredR : floorf(15.f * s);
	const CePlayUnitNineSlice ns = CePlayUnitComputeNineSlice(drawX, y0, drawW, height, rReq, s);
	if (!ns.valid) return;

	auto fadeA = [open](unsigned col) -> unsigned {
		unsigned a = unsigned(float((col >> 24) & 0xffu) * open + 0.5f);
		if (a > 255u) a = 255u;
		return (a << 24) | (col & 0x00ffffffu);
	};

	OverlayStates();
	BeginHudUntextured();
	if (open >= 0.99f)
	{
		const unsigned accent = isForseti ? 0x003ce4ffu : 0x00f04e1au;
		const unsigned topCol = LerpArgb(glassFill, (glassFill & 0xff000000u) | accent, 0.08f);
		DrawNineSliceGlassFill(dev, ns, topCol, glassFill);
	}
	const unsigned haloCol = fadeA((isForseti ? 0x4c28d4ffu : 0x4cff4414u));
	const unsigned bodyCol = fadeA((isForseti ? 0xf23ce4ffu : 0xf2f04e1au));
	const unsigned coreCol = fadeA((isForseti ? 0xffccf8ffu : 0xffffc464u));
	const float coreHalf = ns.strokeWidth * 0.5f;
	DrawNineSliceBracketSide(dev, ns, false, coreHalf + ns.glowPad, haloCol);
	DrawNineSliceBracketSide(dev, ns, true,  coreHalf + ns.glowPad, haloCol);
	DrawNineSliceBracketSide(dev, ns, false, coreHalf, bodyCol);
	DrawNineSliceBracketSide(dev, ns, true,  coreHalf, bodyCol);
	DrawNineSliceBracketSide(dev, ns, false, coreHalf * 0.44f, coreCol);
	DrawNineSliceBracketSide(dev, ns, true,  coreHalf * 0.44f, coreCol);
	EndHudUntextured();
	OverlayStates();
}

float MeasureAsciiWidth(CDXBitmapFont *font, const char *ascii)
{
	if (!font || !ascii || !ascii[0]) return 0.f;
	WCHAR raw[kChars], shown[kChars];
	Copy(raw, ascii);
	Displayable(font, raw, shown);
	SIZE e = {0, 0};
	font->GetTextExtent(shown, &e);
	return float(e.cx);
}

// Compact BEA-native curved telemetry pod anchored on the upper-left margin (clear of the top prompt capsule,
// bottom-left radar, bottom-right radio portrait / battle-line orb, and center compass), themed dynamically
// for Forseti (glowing cyan crescents + deep slate-navy glass) or Muspell (glowing fiery amber crescents + dark iron glass).
void ClassPanel(CDXBitmapFont *font, float w, float h)
{
	CePlayUnitHudTelemetry tel;
	memset(&tel, 0, sizeof(tel));
	if (!CePlayUnitHudTelemetryOf(&tel) || !tel.valid) return;

	char header[48], line1[64], line2[64], action[48];
	CePlayUnitFormatClassHud(tel, header, line1, line2, action);
	const bool muspell = tel.allegiance == 1;
	const CePlayUnitHudPalette pal = CePlayUnitHudPaletteFor(tel.allegiance);
	const float s = HudScale(w, h);
	const float t = s > 1.f ? floorf(s) : 1.f;
	const float sh = float(font->GetHeight()) * s;
	const bool isAir = tel.classId == CE_PLAYUNIT_CLASS_AIRCRAFT;
	const bool isArty = tel.classId == CE_PLAYUNIT_CLASS_ARTILLERY;
	const bool hasSideGauge = isAir || (isArty && tel.arcOn);
	const bool hasAction = action[0] != 0;

	const char *factionTag = muspell ? "MUSPELL" : "FORSETI";
	const float hdrW = MeasureAsciiWidth(font, header) * (s * 0.92f) + 14.f * s + MeasureAsciiWidth(font, factionTag) * (s * 0.78f);
	const float l1W = MeasureAsciiWidth(font, line1) * (s * 0.84f);
	const float l2W = MeasureAsciiWidth(font, line2) * (s * 0.82f);
	const float actW = hasAction ? MeasureAsciiWidth(font, action) * (s * 0.82f) + 8.f * s : 0.f;
	float textNeedW = hdrW;
	if (l1W > textNeedW) textNeedW = l1W;
	if (l2W > textNeedW) textNeedW = l2W;
	if (actW > textNeedW) textNeedW = actW;
	CePlayUnitClassCardLayout card;
	if (!CePlayUnitComputeClassCardLayout(w, h, s, textNeedW, hasSideGauge, &card)) return;

	const float rowGap = floorf(3.f * s);
	const float padX = card.padX;
	const float padY = floorf(7.f * s);
	const float barH = floorf(4.f * s > 3.f ? 4.f * s : 3.f);
	const float bodyW = card.bodyW;
	const float panelH = floorf(padY * 2.f + sh * (hasAction ? 4.f : 3.f) + rowGap * (hasAction ? 4.f : 3.f) + barH + 4.f * s);
	const float totalW = card.totalW;
	// Anchor cleanly on the upper-left safe-area margin, completely clear of the top prompt/notice capsule,
	// bottom-left radar, bottom-right radio portrait / battle-line orb, and strictly left of the center reticle.
	const float objBottom = ObjectiveBottom(s);
	float yHud = 166.f;
	if (objBottom > 0.f)
	{
		const float needY = objBottom / s + 86.f;
		if (needY > yHud) yHud = needY;
		if (yHud > 218.f) yHud = 218.f;
	}
	const float x0 = card.x0;
	const float safeBot = floorf(h * 0.035f);
	float y0 = floorf(yHud * s);
	if (y0 + panelH > h - safeBot) y0 = floorf(h - safeBot - panelH);
	if (x0 + totalW > w - x0 || y0 < 8.f) return;

	const float bodyLeft = card.bodyLeft;
	const float cardR = floorf(14.f * s);

	OverlayStates();
	// 1. Draw true 9-slice mirrored holographic capsule pod (fixed corner radius, straight rails, zero clipping).
	{
		const unsigned glassFill = muspell ? 0xd41a0b08u : 0xcc051220u;
		DrawNineSliceCapsuleArt(x0, y0, totalW, panelH, 1.f, glassFill, !muspell, s, cardR);
		// Subtle horizontal translucent rim connecting the 9-slice corner arcs top and bottom
		const float rimX = x0 + cardR + 2.f * s;
		const float rimW = totalW - 2.f * (cardR + 2.f * s);
		if (rimW > 6.f)
		{
			const unsigned topRim = (pal.bevelAccent & 0x00ffffffu) | 0x58000000u;
			const unsigned botRim = (pal.panelBorder & 0x00ffffffu) | 0x48000000u;
			HudFillRect(rimX, y0 + t * 1.5f, rimW, t, topRim);
			HudFillRect(rimX, y0 + panelH - t * 2.5f, rimW, t, botRim);
		}
	}

	const float textX = card.textX;
	const float textMaxW = card.textMaxW;

	if (isAir)
	{
		const float gx = bodyLeft + bodyW - padX - floorf(18.f * s);
		const float gy = y0 + padY + sh + rowGap;
		const float gh = panelH - padY * 2.f - sh - rowGap - 2.f * s;
		const float gw = floorf(5.f * s > 4.f ? 5.f * s : 4.f);
		if (gh > 12.f)
		{
			// Sleek altitude bar (0..80m)
			HudFillRect(gx, gy, gw, gh, 0x9804080e);
			HudFillRect(gx, gy, gw, t, (pal.accentDim & 0x00ffffffu) | 0x94000000u);
			HudFillRect(gx, gy + gh - t, gw, t, (pal.accentDim & 0x00ffffffu) | 0x94000000u);
			float altRatio = isfinite(tel.altitude) ? (tel.altitude / 80.f) : 0.f;
			if (altRatio < 0.f) altRatio = 0.f;
			if (altRatio > 1.f) altRatio = 1.f;
			const float fillH = (gh - 2.f * t) * altRatio;
			if (fillH > 0.5f)
				HudFillRect(gx + t, gy + gh - t - fillH, gw - 2.f * t, fillH, pal.accentBright);

			// Horizon pitch tick beside altitude bar
			const float hx = gx + gw + floorf(3.f * s);
			const float hw = floorf(9.f * s);
			const float hmid = gy + gh * 0.5f;
			HudFillRect(hx, hmid - t * 0.5f, hw, t, pal.accentDim);
			float pNorm = isfinite(tel.pitchDeg) ? (tel.pitchDeg / 45.f) : 0.f;
			if (pNorm < -1.f) pNorm = -1.f;
			if (pNorm > 1.f) pNorm = 1.f;
			const float py = hmid - pNorm * (gh * 0.42f);
			HudFillRect(hx + t, py - t, hw - 2.f * t, t * 2.f, pal.highlight);
		}
	}
	else if (isArty && tel.arcOn)
	{
		// Dotted/segmented parabolic trajectory profile with elevation ticks so ARC ON reads clearly
		// as a ballistic curve even when the Spotter camera frames the battery from behind.
		const float gw = floorf(24.f * s);
		const float gx = bodyLeft + bodyW - padX - gw;
		const float gy = y0 + padY + sh + rowGap;
		const float gh = panelH - padY * 2.f - sh - rowGap - 2.f * s;
		if (gh > 14.f && gw > 14.f)
		{
			HudFillRect(gx, gy, gw, gh, 0xa8050b14);
			HudFillRect(gx, gy, gw, t, (pal.accentDim & 0x00ffffffu) | 0x94000000u);
			// Ground baseline & vertical elevation tick ruler
			const float baseY = gy + gh - 4.f * s;
			HudFillRect(gx + 2.f * s, baseY, gw - 4.f * s, t, (pal.accentBright & 0x00ffffffu) | 0xc8000000u);
			for (int tick = 1; tick <= 3; ++tick)
			{
				const float ty = baseY - (gh - 8.f * s) * (float(tick) / 3.f);
				HudFillRect(gx + 2.f * s, ty, 3.5f * s, t, (pal.textBright & 0x00ffffffu) | 0xa8000000u);
			}
			float rawEl = isfinite(tel.elDeg) ? ((tel.elDeg + 10.f) / 65.f) : 0.5f;
			if (rawEl < 0.f) rawEl = 0.f;
			if (rawEl > 1.f) rawEl = 1.f;
			const float elNorm = 0.42f + 0.50f * rawEl;
			const float apexH = (gh - 9.f * s) * elNorm;
			const float dotS = floorf(2.2f * s) > 2.f ? floorf(2.2f * s) : 2.f;
			const int kDots = 9;
			const unsigned arcCol = (tel.fireState == CE_PLAYUNIT_FIRE_OUT_OF_RANGE) ? 0xffff6644u : pal.arcMain;
			for (int d = 0; d < kDots; ++d)
			{
				const float u = float(d) / float(kDots - 1);
				const float px = gx + 3.5f * s + (gw - 7.f * s) * u;
				const float py = baseY - 4.f * u * (1.f - u) * apexH;
				const bool isEnd = (d == kDots - 1);
				const float sz = isEnd ? (dotS + t) : dotS;
				const unsigned col = isEnd ? pal.arcImpact : arcCol;
				HudFillRect(floorf(px - sz * 0.5f), floorf(py - sz * 0.5f), sz, sz, col);
			}
		}
	}

	const float row0Y = y0 + padY;
	const float hullY = row0Y + sh + rowGap;
	const float row1Y = hullY + barH + rowGap + 1.f * s;
	const float row2Y = row1Y + sh + rowGap;
	const float row3Y = row2Y + sh + rowGap;

	// Sleek recessed hull integrity bar (soft track + clean colour fill, no harsh boxy frame)
	const float hullW = textMaxW;
	HudFillRect(textX, hullY, hullW, barH, 0x9c04080e);
	HudFillRect(textX, hullY, hullW, t > 1.f ? 1.f : t, (pal.panelBorder & 0x00ffffffu) | 0x80000000u);
	float hullRatio = float(tel.hullPct) / 100.f;
	if (hullRatio < 0.f) hullRatio = 0.f;
	if (hullRatio > 1.f) hullRatio = 1.f;
	const unsigned hullCol = hullRatio < 0.30f ? pal.crit : (hullRatio < 0.60f ? pal.warn : pal.ok);
	const float hullFill = (hullW - 2.f * t) * hullRatio;
	if (hullFill > 0.5f)
		HudFillRect(textX + t, hullY + t * 0.5f, hullFill, barH - t, hullCol);

	const bool urgent = hasAction && (tel.unloadStatus == CE_NAVAL_READY_UNLOAD || tel.spotterMode != CE_SPOTTER_OFF);
	if (urgent)
	{
		const unsigned pillFill = (pal.accentDim & 0x00ffffffu) | 0x88000000u;
		const unsigned pillTrim = (pal.highlight & 0x00ffffffu) | 0xc8000000u;
		HudFillRect(textX - 2.f * s, row3Y - 1.f * s, textMaxW + 4.f * s, sh + 2.f * s, pillFill);
		HudFillRect(textX - 2.f * s, row3Y - 1.f * s, textMaxW + 4.f * s, t, pillTrim);
	}

	// 2. Draw ALL bitmap-font text after geometry so texture state stays pristine across every line.
	WCHAR fwRaw[16], fwShown[16];
	Copy(fwRaw, factionTag);
	Displayable(font, fwRaw, fwShown);
	float tagSqueeze = 1.f;
	const float tagScale = s * 0.78f;
	const float tagMaxW = textMaxW * 0.34f;
	const float tagW = Fit(font, fwShown, &tagSqueeze, tagMaxW / tagScale, 0.65f) * tagScale;
	const float tagX = textX + textMaxW - tagW;
	const float headerMaxW = (tagX - textX - 6.f * s) > 24.f ? (tagX - textX - 6.f * s) : (textMaxW * 0.60f);
	DrawHudLineText(font, textX, row0Y, pal.accentBright, header, s * 0.92f, headerMaxW);
	OverlayStates();
	font->DrawTextWithShadow(tagX, row0Y + sh * 0.08f, pal.highlight, fwShown, 0, 0.f, tagScale * tagSqueeze, tagScale);
	DrawHudLineText(font, textX, row1Y, pal.textBright, line1, s * 0.84f, textMaxW);
	DrawHudLineText(font, textX, row2Y, pal.textMuted, line2, s * 0.82f, textMaxW);
	if (hasAction)
		DrawHudLineText(font, textX + (urgent ? 2.f * s : 0.f), row3Y, urgent ? pal.highlight : pal.accentBright, action, s * 0.82f, textMaxW - 2.f * s);
	RestoreStates();
}

// The held fire at the middle of the view, where the shot goes: the HUD's own reticle (its primary ring and its dot, CHud::RenderOverlayForViewpoint's textures, at the
// HUD's scale), white while ready as the HUD draws it over open ground, grey while the gun arms or recovers, the HUD's alert red when the shot cannot be made; then, only
// when it is not ready, the state as one of the HUD's alert words under it (ARMING, RECOVERING, OUT OF RANGE, BLOCKED), with the shadow and no box. Ready says nothing, as
// the original says nothing when all is well. The Battle Engine's HUD itself is not drawn in the unit's view, so this is the reticle while a unit is driven.
void DrawReticleBar(float x, float y, float w, float h, unsigned colour)
{
	HudFillRect(x + 1.f, y + 1.f, w, h, 0x88000000u);
	HudFillRect(x, y, w, h, colour);
}

void Reticle(int fireState, CDXBitmapFont *font, float w, float h)
{
	if (CDXBitmapFont *normal = PLATFORM.Font(FONT_NORMAL)) if (normal->GetHeight() > 0) font = normal;
	const char *word = NULL;
	unsigned colour = kWhite;
	switch (fireState)
	{
	case CE_PLAYUNIT_FIRE_ARMING: word = "ARMING"; colour = kWeaponWait; break;
	case CE_PLAYUNIT_FIRE_RECOVERING: word = "RECOVERING"; colour = kWeaponWait; break;
	case CE_PLAYUNIT_FIRE_OUT_OF_RANGE: word = "OUT OF RANGE"; colour = kWeaponFault; break;
	case CE_PLAYUNIT_FIRE_BLOCKED: word = "BLOCKED"; colour = kWeaponFault; break;
	default: break;
	}
	const float s = HudScale(w, h), cx = w * 0.5f, cy = h * 0.5f;
	const unsigned tint = (colour & 0x00ffffff) | 0xaf000000;		// the HUD's reticle alpha
	const float t = s > 1.f ? floorf(s * 1.15f) : 1.5f;
	CePlayUnitHudTelemetry tel;
	memset(&tel, 0, sizeof(tel));
	const bool haveTel = CePlayUnitHudTelemetryOf(&tel) && tel.valid;
	const CePlayUnitReticleKind kind = haveTel ? CePlayUnitCompactReticleKind(tel.classId) : CE_PLAYUNIT_RETICLE_VEHICLE;
	const CePlayUnitHudPalette pal = CePlayUnitHudPaletteFor(haveTel ? tel.allegiance : 0);
	const unsigned accentTint = (fireState == CE_PLAYUNIT_FIRE_READY || fireState == CE_PLAYUNIT_FIRE_NONE)
		? ((pal.accentBright & 0x00ffffffu) | 0xd8000000u)
		: ((colour & 0x00ffffffu) | 0xe4000000u);
	const unsigned crispTint = (colour & 0x00ffffffu) | 0xe8000000u;

	OverlayStates();
	if (kind == CE_PLAYUNIT_RETICLE_INFANTRY)
	{
		// Compact 4-tick infantry crosshair well clear of the viewmodel and center target.
		const float gap = floorf(6.f * s);
		const float len = floorf(8.f * s);
		const float halfT = t * 0.5f;
		DrawReticleBar(cx - gap - len, cy - halfT, len, t, crispTint);
		DrawReticleBar(cx + gap,       cy - halfT, len, t, crispTint);
		DrawReticleBar(cx - halfT, cy - gap - len, t, len, crispTint);
		DrawReticleBar(cx - halfT, cy + gap,       t, len, crispTint);
	}
	else if (kind == CE_PLAYUNIT_RETICLE_AIRCRAFT)
	{
		// Forward-aim gun pipper + split pitch-responsive artificial-horizon wings (stays clear of aircraft fuselage below).
		const float wingGap = floorf(18.f * s);
		const float wingLen = floorf(16.f * s);
		const float tickH = floorf(5.f * s);
		const float halfT = t * 0.5f;
		float pShift = (haveTel && isfinite(tel.pitchDeg)) ? (tel.pitchDeg * 0.45f * s) : 0.f;
		if (pShift < -18.f * s) pShift = -18.f * s;
		if (pShift >  18.f * s) pShift =  18.f * s;
		const float hy = floorf(cy + pShift);
		// Left & right horizon wing bars with inner drop ticks
		DrawReticleBar(cx - wingGap - wingLen, hy - halfT, wingLen, t, accentTint);
		DrawReticleBar(cx - wingGap,           hy - halfT, t, tickH, accentTint);
		DrawReticleBar(cx + wingGap,           hy - halfT, wingLen, t, accentTint);
		DrawReticleBar(cx + wingGap - t,       hy - halfT, t, tickH, accentTint);
		// Crisp compact aim brackets flanking center pipper
		const float aimGap = floorf(7.f * s);
		const float aimLen = floorf(5.f * s);
		DrawReticleBar(cx - aimGap - aimLen, cy - halfT, aimLen, t, crispTint);
		DrawReticleBar(cx + aimGap,          cy - halfT, aimLen, t, crispTint);
	}
	else if (kind == CE_PLAYUNIT_RETICLE_ARTILLERY)
	{
		// Precision ballistic range/elevation mil-reticle with vertical stadia ticks and side deflection brackets.
		const float hGap = floorf(10.f * s);
		const float hLen = floorf(11.f * s);
		const float halfT = t * 0.5f;
		DrawReticleBar(cx - hGap - hLen, cy - halfT, hLen, t, crispTint);
		DrawReticleBar(cx + hGap,        cy - halfT, hLen, t, crispTint);
		// Vertical elevation stadia ladder below/above center
		for (int k = 1; k <= 3; ++k)
		{
			const float dy = floorf(float(k) * 7.f * s);
			const float tw = floorf((10.f - float(k) * 2.f) * s);
			DrawReticleBar(cx - tw * 0.5f, cy + dy - halfT, tw, t, accentTint);
			if (k <= 2)
				DrawReticleBar(cx - tw * 0.5f, cy - dy - halfT, tw, t, accentTint);
		}
		// Subtle range-arc brackets left & right
		const float brX = floorf(24.f * s);
		const float brH = floorf(14.f * s);
		DrawReticleBar(cx - brX, cy - brH * 0.5f, t, brH, accentTint);
		DrawReticleBar(cx + brX, cy - brH * 0.5f, t, brH, accentTint);
	}
	else if (HUD.mCrosshairPrimary)
	{
		CSPRITERENDERER::DrawSprite(cx, cy, 0.011f, HUD.mCrosshairPrimary, SA_CENTRE, SPT_NONE, 1.f, 0.f, tint, s * 0.78f, s * 0.78f, 0.f, 1.f, 0.f, 1.f);
	}
	OverlayStates();
	if (HUD.mCrosshairDot)
		CSPRITERENDERER::DrawSprite(cx, cy, 0.011f, HUD.mCrosshairDot, SA_CENTRE, SPT_NONE, 1.f, 0.f, tint, s, s, 0.f, 1.f, 0.f, 1.f);
	if (word)
	{
		WCHAR text[48];
		char line[48];
		if (fireState == CE_PLAYUNIT_FIRE_OUT_OF_RANGE && haveTel && tel.classId == CE_PLAYUNIT_CLASS_ARTILLERY && tel.maxRangeMeters > 0.f)
			snprintf(line, sizeof(line), "%s (MAX %dM)", word, CePlayUnitSafeRoundInt(tel.maxRangeMeters, 1, 99999));
		else
			snprintf(line, sizeof(line), "%s", word);
		int n = 0;
		for (const char *c = line; *c && n < 47; ++c) text[n++] = font->CanDisplay(WCHAR((unsigned char)*c)) ? WCHAR((unsigned char)*c) : L'-';
		text[n] = 0;
		SIZE extent;
		font->GetTextExtent(text, &extent);
		font->DrawTextWithShadow(cx - float(extent.cx) * s * 0.5f, cy + 30.f * s, colour, text, 0, 0.f, s, s);
	}
	RestoreStates();
}

bool IsMuspellHud()
{
	CePlayUnitHudFocus focus;
	return CePlayUnitHudFocusOf(&focus) && focus.allegiance == 1;
}

void DrawFactionCapsule(float cx, float cy, float full, float height, float progress, float s)
{
	const float r = floorf((height * 0.46f < 15.f * s ? height * 0.46f : 15.f * s));
	const float totalW = full + 2.f * r;
	const float x0 = floorf(cx - totalW * 0.5f);
	const float y0 = floorf(cy - height * 0.5f);
	if (!IsMuspellHud())
	{
		DrawNineSliceCapsuleArt(x0, y0, totalW, height, progress, 0xcc051220u, true, s, r);
		return;
	}
	DrawNineSliceCapsuleArt(x0, y0, totalW, height, progress, 0xd41a0b08u, false, s, r);
}

int CountVisibleChars(const WCHAR *text)
{
	int count = 0;
	if (!text) return 0;
	for (const WCHAR *c = text; *c; ++c)
		if (*c != L' ' && *c != L'\t') ++count;
	return count;
}

// One capsule: lines (one or two) at (cx, cy) on the screen, opened or closing by phase (ms from its start or its end); the first line in firstColour, a second as a note.
void Capsule(CDXBitmapFont *font, const WCHAR *first, const WCHAR *second, unsigned firstColour, float cx, float cy, float height, float s, unsigned long long phase)
{
	WCHAR text[2][kChars];
	Displayable(font, first, text[0]);
	if (second) Displayable(font, second, text[1]);
	else text[1][0] = 0;
	// Never draw a large empty capsule around empty or tiny (<=2 char) typewriter fragments.
	if (CountVisibleChars(text[0]) + CountVisibleChars(text[1]) <= 2) return;
	const int lines = text[1][0] ? 2 : 1;
	float squeeze[2] = {1.f, 1.f};
	// Measure natural line widths first: if beside the top-right objective box and the text needs more room,
	// drop the notice cleanly beneath the objective box so full hints render without "..." truncation.
	SIZE e0 = {0, 0}, e1 = {0, 0};
	font->GetTextExtent(text[0], &e0);
	if (lines == 2) font->GetTextExtent(text[1], &e1);
	const float naturalW = float(e0.cx > e1.cx ? e0.cx : e1.cx);
	const float screenWide = (cx * 2.f) / s - 96.f > 160.f ? ((cx * 2.f) / s - 96.f) : 160.f;
	const float fullWide = screenWide < kMaxWidth ? screenWide : kMaxWidth;
	const float below = ObjectiveBottom(s);
	const bool besideObjective = cy < 200.f * s && below > 0.f && (cy - height * 0.5f) < below;
	const float top = CapsuleRoom(cx * 2.f, s);
	float most = besideObjective && top < fullWide ? top : fullWide;
	if (besideObjective && naturalW > most && fullWide > most)
	{
		cy = below + height * 0.5f + 6.f * s;
		most = fullWide;
	}
	const float a = Fit(font, text[0], &squeeze[0], most);
	const float b = lines == 2 ? Fit(font, text[1], &squeeze[1], most) : 0.f;
	float textWidth = a > b ? a : b;
	if (textWidth < 64.f) textWidth = 64.f;
	const float lineGap = 15.f;							// the radio's line spacing
	const float full = (textWidth + 16.f) * s;
	OverlayStates();
	// the caps turn in (no box yet), then the box and its text together: a box is never drawn without its text
	if (phase < kCapsMs)
		DrawFactionCapsule(cx, cy, full, height, 0.5f + 0.49f * float(phase) / float(kCapsMs), s);
	else if (IsMuspellHud())
	{
		DrawFactionCapsule(cx, cy, full, height, 1.f, s);
		OverlayStates();
		const float x = cx - full * 0.5f + 4.f * s;
		const float fh = float(font->GetHeight());
		float y = cy - (fh + (lines - 1) * lineGap) * 0.5f * s;
		font->DrawTextWithShadow(x, y, firstColour, text[0], 0, 0.f, s * squeeze[0], s);
		if (lines == 2)
		{
			y += lineGap * s;
			font->DrawTextWithShadow(x, y, kNote, text[1], 0, 0.f, s * squeeze[1], s);
		}
	}
	else
	{
		HUD.RenderObjectiveBar(cx, cy, full, height, 1.f);
		OverlayStates();
		// left-aligned inside the body, as the radio's text is: from the body's left end, the lines centred on the capsule's middle
		const float x = cx - full * 0.5f + 2.f * s;
		const float fh = float(font->GetHeight());
		float y = cy - (fh + (lines - 1) * lineGap) * 0.5f * s;
		font->DrawTextWithShadow(x, y, firstColour, text[0], 0, 0.f, s * squeeze[0], s);
		if (lines == 2)
		{
			y += lineGap * s;
			font->DrawTextWithShadow(x, y, kNote, text[1], 0, 0.f, s * squeeze[1], s);
		}
	}
	RestoreStates();
}

// The clock of a capsule: how long it has been drawn, counting only frames that draw it (a step is at most kStepMs).
void Advance(unsigned long long *age, unsigned long long *last)
{
	const unsigned long long now = CE_ScreenshotTicks();
	unsigned long long step = now >= *last ? now - *last : 0;
	if (step > kStepMs) step = kStepMs;
	*age += step;
	*last = now;
}

// The notice in the message capsule, centred on the HUD's 640x480 frame at kRadioY or kTopY. At the top, beside the objective counter, it is as tall as its lines; at the
// bottom it is the radio's three-line capsule.
void Notice(CDXBitmapFont *font, float w, float h, bool hudShown)
{
	Advance(&gAge, &gLast);
	if (gAge >= gShown) return;
	if (gDrawnAtTop >= 0 && gDrawnAtTop != (hudShown ? 1 : 0)) gAge = 0;
	gDrawnAtTop = hudShown ? 1 : 0;
	const unsigned long long left = gShown - gAge;
	const unsigned long long phase = gAge < left ? gAge : left;		// the opening and the closing are the same steps
	const float s = HudScale(w, h);
	const int lines = gLine[1][0] ? 2 : 1;
	// the HUD keeps its corner groups at the screen's edges: the top capsule is placed from the top edge, the radio's from the bottom edge
	const float cx = w * 0.5f, cy = hudShown ? kTopY * s : h - (480.f - kRadioY) * s;
	const float height = hudShown ? (16.f + 15.f * float(lines)) * s : 60.f * s;
	Capsule(font, gLine[0], gLine[1][0] ? gLine[1] : NULL, kWhite, cx, cy, height, s, phase);
}

// The prompt line while a unit is driven and no notice is up: one line in a one-line capsule, opening as a notice opens, for as long as the unit is held; at the top
// where the notice sits when the radio is on screen (atTop), at the bottom otherwise.
float Width(CDXBitmapFont *font, WCHAR *text)
{
	SIZE e = {0, 0};
	font->GetTextExtent(text, &e);
	return float(e.cx);
}

// The prompt line "name | key: action | key: action": all drawn in the HUD's small face so the unit name, armour, and control pairs
// fit cleanly together without stripping hints. A piece without a key (no ": ") is drawn as an action.
void Prompt(CDXBitmapFont *font, float w, float h, bool atTop)
{
	Advance(&gPromptAge, &gPromptLast);
	const float s = HudScale(w, h);
	// split the line as written ("|" is not in the HUD's face), then make each piece drawable
	WCHAR piece[kCePlayUnitPromptMaxPieces][2][kChars];
	int pieces = 0;
	for (const WCHAR *c = gPrompt; *c && pieces < kCePlayUnitPromptMaxPieces; ++pieces)
	{
		const WCHAR *end = c;
		while (*end && !(end[0] == L' ' && end[1] == L'|' && end[2] == L' ')) ++end;
		const WCHAR *colon = c;
		while (colon < end && !(colon[0] == L':' && colon[1] == L' ')) ++colon;
		const bool pair = pieces > 0 && colon < end;
		int n = 0;
		for (const WCHAR *k = c; k < (pair ? colon : end) && n < kChars - 1; ++k) piece[pieces][pair ? 0 : 1][n++] = *k;
		piece[pieces][pair ? 0 : 1][n] = 0;
		if (pair)
		{
			n = 0;
			for (const WCHAR *k = colon + 2; k < end && n < kChars - 1; ++k) piece[pieces][1][n++] = *k;
			piece[pieces][1][n] = 0;
		}
		else piece[pieces][0][0] = 0;
		c = *end ? end + 3 : end;
	}
	if (!pieces) return;
	for (int i = 0; i < pieces; ++i)
		for (int j = 0; j < 2; ++j)
		{
			WCHAR shown[kChars];
			Displayable(font, piece[i][j], shown);
			memcpy(piece[i][j], shown, sizeof(shown));
		}
	// the driven unit's armour after its name: the word as a key, the share in the Battle Engine's weapon-state colours (green, red when low)
	CePlayUnitHudFocus focus;
	const bool health = CePlayUnitHudFocusOf(&focus) && focus.fullLife > 0.f;
	WCHAR armourKey[16], armourValue[16];
	unsigned armourColour = kWeaponWait;
	if (health)
	{
		float life = (isfinite(focus.life) && isfinite(focus.fullLife) && focus.fullLife > 0.f) ? (focus.life / focus.fullLife) : 0.f;
		if (!isfinite(life) || life < 0.f) life = 0.f;
		if (life > 1.f) life = 1.f;
		char text[16];
		snprintf(text, sizeof(text), "%d%%", int(life * 100.f + 0.5f));
		WCHAR raw[16];
		int n = 0;
		for (const char *c = text; *c && n < 15; ++c) raw[n++] = WCHAR((unsigned char)*c);
		raw[n] = 0;
		Displayable(font, raw, armourValue);
		CePlayUnitHudTelemetry telPrompt;
		memset(&telPrompt, 0, sizeof(telPrompt));
		const bool isFoot = CePlayUnitHudTelemetryOf(&telPrompt) && telPrompt.valid && telPrompt.classId == CE_PLAYUNIT_CLASS_INFANTRY;
		const char *word = isFoot ? "Health" : "Armour";
		n = 0;
		for (const char *c = word; *c && n < 15; ++c) raw[n++] = WCHAR((unsigned char)*c);
		raw[n] = 0;
		Displayable(font, raw, armourKey);
		if (life < 0.3f) armourColour = kWeaponFault;
	}
	const float barW = health ? Width(font, armourKey) + kKeyGap + Width(font, armourValue) : 0.f;
	float width = Width(font, piece[0][1]) + (health ? kPairGap + barW : 0.f);
	for (int i = 1; i < pieces; ++i)
		width += kPairGap + (piece[i][0][0] ? Width(font, piece[i][0]) + kKeyGap : 0.f) + Width(font, piece[i][1]);
	const float sh = float(font->GetHeight());
	const float height = (sh + 14.f > 30.f ? sh + 14.f : 30.f) * s;
	float cy = atTop ? kTopY * s : h - (480.f - kPromptY) * s;
	const float below = ObjectiveBottom(s);
	const bool besideObjective = atTop && below > 0.f && (cy - height * 0.5f) < below;
	const float screenWide = w / s - 64.f > 160.f ? w / s - 64.f : 160.f;
	const float fullWide = screenWide < kPromptMaxWidth ? screenWide : kPromptMaxWidth;
	float room = besideObjective ? CapsuleRoom(w, s) : fullWide;
	// If the prompt overlaps the objective box vertically and needs more room for its hints, drop cleanly under the box instead of stripping controls.
	if (besideObjective && width > room && fullWide > room)
	{
		cy = below + height * 0.5f + 4.f * s;
		room = fullWide;
	}
	// Keep unit name, armour, and key hints; allow horizontal squeeze down to kMinSqueeze before dropping trailing hints.
	while (pieces > 1 && width * kMinSqueeze > room)
	{
		--pieces;
		width -= kPairGap + (piece[pieces][0][0] ? Width(font, piece[pieces][0]) + kKeyGap : 0.f) + Width(font, piece[pieces][1]);
	}
	const float squeeze = width > room ? room / width : 1.f;
	const float full = (width * squeeze + 16.f) * s;
	const float cx = w * 0.5f;
	OverlayStates();
	const unsigned long long phase = gPromptAge;
	if (phase < kCapsMs) { DrawFactionCapsule(cx, cy, full, height, 0.5f + 0.49f * float(phase) / float(kCapsMs), s); RestoreStates(); return; }
	DrawFactionCapsule(cx, cy, full, height, 1.f, s);
	OverlayStates();
	float x = cx - full * 0.5f + 8.f * s;
	const float y = cy - sh * 0.5f * s;
	font->DrawTextWithShadow(x, y, kWhite, piece[0][1], 0, 0.f, s * squeeze, s);
	x += Width(font, piece[0][1]) * s * squeeze;
	if (health)
	{
		x += kPairGap * s * squeeze;
		font->DrawTextWithShadow(x, y, kPromptKey, armourKey, 0, 0.f, s * squeeze, s);
		x += (Width(font, armourKey) + kKeyGap) * s * squeeze;
		font->DrawTextWithShadow(x, y, armourColour, armourValue, 0, 0.f, s * squeeze, s);
		x += Width(font, armourValue) * s * squeeze;
	}
	for (int i = 1; i < pieces; ++i)
	{
		x += kPairGap * s * squeeze;
		if (piece[i][0][0])
		{
			font->DrawTextWithShadow(x, y, kPromptKey, piece[i][0], 0, 0.f, s * squeeze, s);
			x += (Width(font, piece[i][0]) + kKeyGap) * s * squeeze;
		}
		font->DrawTextWithShadow(x, y, kPromptAction, piece[i][1], 0, 0.f, s * squeeze, s);
		x += Width(font, piece[i][1]) * s * squeeze;
	}
	RestoreStates();
}
}

void CePlayUnitNotice(const char *first, const char *second, bool longer)
{
	// a capsule already up keeps where its opening is (CePlayUnitNoticeAgeOnReplace): a press again and again never restarts it empty
	const unsigned long long age = gLine[0][0] ? CePlayUnitNoticeAgeOnReplace(gAge, gShown, kCapsMs + kGrowMs) : 0;
	Copy(gLine[0], first);
	Copy(gLine[1], second);
	gShown = longer ? kShownLongMs : kShownMs;
	gAge = age;
	gLast = CE_ScreenshotTicks();
}

void CePlayUnitNoticeClear()
{
	gLine[0][0] = gLine[1][0] = 0;
	gShown = gAge = 0;
	gDrawnAtTop = -1;
	gPrompt[0] = 0;
	gPromptAge = 0;
}

void CePlayUnitPrompt(const char *line)
{
	const bool hadPrompt = gPrompt[0] != 0;
	Copy(gPrompt, line);
	if (!gPrompt[0] || !hadPrompt)
	{
		gPromptAge = 0;
		gPromptLast = CE_ScreenshotTicks();
	}
}

void CePlayUnitHudRender(bool reticle, int fireState, bool hudShown)
{
	const bool notice = gLine[0][0] && gAge < gShown;
	const bool prompt = reticle && !notice && gPrompt[0];
	if (!reticle && !notice) return;
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font || font->GetHeight() <= 0) return;
	const float w = float(PLATFORM.GetWindowWidth()), h = float(PLATFORM.GetWindowHeight());
	if (w <= 0.f || h <= 0.f) return;
	if (reticle && fireState != CE_PLAYUNIT_FIRE_NONE) Reticle(fireState, font, w, h);
	if (reticle) ClassPanel(font, w, h);
	if (notice) Notice(font, w, h, hudShown);
	else if (prompt) Prompt(font, w, h, hudShown);
	// while a notice is up the prompt waits, and opens afresh after it
	if (notice) { gPromptAge = 0; gPromptLast = CE_ScreenshotTicks(); }
}

#endif
