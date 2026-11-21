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

#include <string.h>

extern "C" unsigned long long CE_ScreenshotTicks(void);		// ce_screenshot.cpp: SDL's milliseconds

namespace
{
const unsigned long long kShownMs = 4000;	// how long a notice stays (the house style's in-mission notices: four to six seconds)
const unsigned long long kShownLongMs = 6000;	// a take's notice, which also says the way back: the upper bound
const unsigned long long kCapsMs = 150;		// the caps turning in, then the box with its text, then the same backwards at the end
const unsigned long long kGrowMs = 200;		// how long a replaced notice may count as still opening (CePlayUnitNoticeAgeOnReplace)
const unsigned long long kStepMs = 100;		// the most one drawn frame adds to a notice's age (a frame after a pause adds no more than this)
const int kChars = 96;
// Where the capsule sits on the HUD's 640x480 frame: where the radio's capsule is (bottom centre) while the unit's view has no HUD, and at the top centre, clear of
// the radio, the HUD's alert words, its corner groups, the objective counter at the top right and the reticle's ring, while the Battle Engine's HUD is up; the
// widest the text may be, and the narrowest a line may be squeezed before it is cut (the menus' rule, kCeValueMinScale).
const float kRadioY = 435.f, kPromptY = 450.f, kTopY = 98.f, kMaxWidth = 420.f, kMinSqueeze = 0.75f;
// The colours: content white, a note grey (lighter than the menus' note grey: it sits on the sky), the HUD's alert red.
const unsigned kWhite = 0xffffffff, kNote = 0xffc0c0c0, kAlert = 0xffdc3c3c;
// The Battle Engine's own weapon-state words (CHud::Render00485d50, FONT_NORMAL under the view's middle): red for a fault (overheated, low armour), green for a
// state that passes (low energy recharging). A driven unit's gun says its state in the same face and colours.
const unsigned kWeaponFault = 0xffff3020, kWeaponWait = 0xff30ff20;
// The prompt's pairs as the editor's prompt line draws them (ce_editor_ui.cpp, PromptPair): the key white, the action grey, a small gap between, a wider one
// between pairs; the unit's name first, white, in the HUD's normal face.
const unsigned kPromptKey = 0xfff4f4f4, kPromptAction = 0xffa8a8a8;
const float kKeyGap = 8.f, kPairGap = 22.f;
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
	return (total * 0.6f + 40.f + total * 0.5f + 8.f) * s;
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
float Fit(CDXBitmapFont *font, WCHAR *text, float *squeeze, float maxWidth = kMaxWidth)
{
	SIZE e = {0, 0};
	font->GetTextExtent(text, &e);
	*squeeze = 1.f;
	if (float(e.cx) <= maxWidth) return float(e.cx);
	if (float(e.cx) * kMinSqueeze <= maxWidth)
	{
		*squeeze = maxWidth / float(e.cx);
		return maxWidth;
	}
	*squeeze = kMinSqueeze;
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
		if (float(e.cx) * kMinSqueeze <= maxWidth)
		{
			for (int i = 0; i <= m + 3; ++i) text[i] = trial[i];
			return float(e.cx) * kMinSqueeze;
		}
	}
	return float(e.cx) * kMinSqueeze;
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

// The held fire at the middle of the view, where the shot goes: the HUD's own reticle (its primary ring and its dot, CHud::RenderOverlayForViewpoint's textures, at the
// HUD's scale), white while ready as the HUD draws it over open ground, grey while the gun arms or recovers, the HUD's alert red when the shot cannot be made; then, only
// when it is not ready, the state as one of the HUD's alert words under it (ARMING, RECOVERING, OUT OF RANGE, BLOCKED), with the shadow and no box. Ready says nothing, as
// the original says nothing when all is well. The Battle Engine's HUD itself is not drawn in the unit's view, so this is the reticle while a unit is driven.
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
	OverlayStates();
	if (HUD.mCrosshairPrimary)
		CSPRITERENDERER::DrawSprite(cx, cy, 0.011f, HUD.mCrosshairPrimary, SA_CENTRE, SPT_NONE, 1.f, 0.f, tint, s, s, 0.f, 1.f, 0.f, 1.f);
	if (HUD.mCrosshairDot)
		CSPRITERENDERER::DrawSprite(cx, cy, 0.011f, HUD.mCrosshairDot, SA_CENTRE, SPT_NONE, 1.f, 0.f, tint, s, s, 0.f, 1.f, 0.f, 1.f);
	if (word)
	{
		WCHAR text[24];
		int n = 0;
		for (const char *c = word; *c && n < 23; ++c) text[n++] = font->CanDisplay(WCHAR((unsigned char)*c)) ? WCHAR((unsigned char)*c) : L'-';
		text[n] = 0;
		SIZE extent;
		font->GetTextExtent(text, &extent);
		font->DrawTextWithShadow(cx - float(extent.cx) * s * 0.5f, cy + 30.f * s, colour, text, 0, 0.f, s, s);
	}
	RestoreStates();
}

// One capsule: lines (one or two) at (cx, cy) on the screen, opened or closing by phase (ms from its start or its end); the first line in firstColour, a second as a note.
void Capsule(CDXBitmapFont *font, const WCHAR *first, const WCHAR *second, unsigned firstColour, float cx, float cy, float height, float s, unsigned long long phase)
{
	WCHAR text[2][kChars];
	Displayable(font, first, text[0]);
	if (second) Displayable(font, second, text[1]);
	else text[1][0] = 0;
	const int lines = text[1][0] ? 2 : 1;
	float squeeze[2] = {1.f, 1.f};
	// at the top the capsule keeps clear of the objective box (CapsuleRoom); at the radio's place below it has the whole width
	const float top = CapsuleRoom(cx * 2.f, s);
	const float most = cy < 200.f * s && top < kMaxWidth ? top : kMaxWidth;
	const float a = Fit(font, text[0], &squeeze[0], most);
	const float b = lines == 2 ? Fit(font, text[1], &squeeze[1], most) : 0.f;
	float textWidth = a > b ? a : b;
	if (textWidth < 150.f) textWidth = 150.f;
	const float lineGap = 15.f;							// the radio's line spacing
	const float full = (textWidth + 8.f) * s;
	OverlayStates();
	// the caps turn in (no box yet), then the box and its text together: a box is never drawn without its text
	if (phase < kCapsMs)
		HUD.RenderObjectiveBar(cx, cy, full, height, 0.5f + 0.49f * float(phase) / float(kCapsMs));
	else
	{
		HUD.RenderObjectiveBar(cx, cy, full, height, 1.f);
		OverlayStates();
		// left-aligned inside the body, as the radio's text is: from the body's left end, the lines centred on the capsule's middle
		const float x = cx - full * 0.5f;
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

// The prompt line "name | key: action | key: action": the name in the normal face, then each pair, key white and action grey, as the editor's prompt line. A
// piece without a key (no ": ") is drawn as an action.
void Prompt(CDXBitmapFont *font, float w, float h, bool atTop)
{
	Advance(&gPromptAge, &gPromptLast);
	const float s = HudScale(w, h);
	CDXBitmapFont *large = PLATFORM.Font(FONT_NORMAL);
	if (!large || large->GetHeight() <= 0) large = font;
	// split the line as written ("|" is not in the HUD's face), then make each piece drawable
	WCHAR piece[4][2][kChars];
	int pieces = 0;
	for (const WCHAR *c = gPrompt; *c && pieces < 4; ++pieces)
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
			Displayable(j == 1 && i == 0 ? large : font, piece[i][j], shown);
			memcpy(piece[i][j], shown, sizeof(shown));
		}
	// the driven unit's armour after its name: the word as a key, the share in the Battle Engine's weapon-state colours (green, red when low)
	CePlayUnitHudFocus focus;
	const bool health = CePlayUnitHudFocusOf(&focus) && focus.fullLife > 0.f;
	WCHAR armourKey[16], armourValue[16];
	unsigned armourColour = kWeaponWait;
	if (health)
	{
		float life = focus.life / focus.fullLife;
		if (life < 0.f) life = 0.f;
		if (life > 1.f) life = 1.f;
		char text[16];
		snprintf(text, sizeof(text), "%d%%", int(life * 100.f + 0.5f));
		WCHAR raw[16];
		int n = 0;
		for (const char *c = text; *c && n < 15; ++c) raw[n++] = WCHAR((unsigned char)*c);
		raw[n] = 0;
		Displayable(font, raw, armourValue);
		const char *word = "Armour";
		n = 0;
		for (const char *c = word; *c && n < 15; ++c) raw[n++] = WCHAR((unsigned char)*c);
		raw[n] = 0;
		Displayable(font, raw, armourKey);
		if (life < 0.3f) armourColour = kWeaponFault;
	}
	const float barW = health ? Width(font, armourKey) + kKeyGap + Width(font, armourValue) : 0.f;
	float width = Width(large, piece[0][1]) + (health ? kPairGap + barW : 0.f);
	for (int i = 1; i < pieces; ++i)
		width += kPairGap + (piece[i][0][0] ? Width(font, piece[i][0]) + kKeyGap : 0.f) + Width(font, piece[i][1]);
	// the line keeps the unit's name and armour; key pairs go from the end while it does not fit beside the objective box, then what is left is squeezed
	const float room = CapsuleRoom(w, HudScale(w, h)) < kMaxWidth + 120.f ? CapsuleRoom(w, HudScale(w, h)) : kMaxWidth + 120.f;
	while (pieces > 1 && width > room)
	{
		--pieces;
		width -= kPairGap + (piece[pieces][0][0] ? Width(font, piece[pieces][0]) + kKeyGap : 0.f) + Width(font, piece[pieces][1]);
	}
	const float lh = float(large->GetHeight()), sh = float(font->GetHeight());
	const float height = (lh + 14.f > 31.f ? lh + 14.f : 31.f) * s;
	float cy = atTop ? kTopY * s : h - (480.f - kPromptY) * s;
	float squeeze = width > room ? room / width : 1.f;
	// too tight beside the objective box (the name and armour would be squeezed): the capsule goes down under the box instead, where the width is free
	const float below = ObjectiveBottom(s);
	if (atTop && squeeze < 0.9f && below > 0.f)
	{
		cy = below + height * 0.5f + 12.f * s;
		float wide = w / s - 60.f;
		if (wide > kMaxWidth + 120.f) wide = kMaxWidth + 120.f;
		squeeze = width > wide ? wide / width : 1.f;
	}
	const float full = (width * squeeze + 16.f) * s;
	const float cx = w * 0.5f;
	OverlayStates();
	const unsigned long long phase = gPromptAge;
	if (phase < kCapsMs) { HUD.RenderObjectiveBar(cx, cy, full, height, 0.5f + 0.49f * float(phase) / float(kCapsMs)); RestoreStates(); return; }
	HUD.RenderObjectiveBar(cx, cy, full, height, 1.f);
	OverlayStates();
	float x = cx - full * 0.5f + 8.f * s;
	large->DrawTextWithShadow(x, cy - lh * 0.5f * s, kWhite, piece[0][1], 0, 0.f, s * squeeze, s);
	x += Width(large, piece[0][1]) * s * squeeze;
	const float y = cy - sh * 0.5f * s;
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
}

void CePlayUnitPrompt(const char *line)
{
	Copy(gPrompt, line);
	gPromptAge = 0;
	gPromptLast = CE_ScreenshotTicks();
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
	if (notice) Notice(font, w, h, hudShown);
	else if (prompt) Prompt(font, w, h, hudShown);
	// while a notice is up the prompt waits, and opens afresh after it
	if (notice) { gPromptAge = 0; gPromptLast = CE_ScreenshotTicks(); }
}

#endif
