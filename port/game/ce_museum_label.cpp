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

namespace {
const int kChars = 96;
const unsigned long long kTitleMs = 5200, kGrowMs = 350;
// The same title asked for again this soon after it was put up keeps its own clock: a visitor stepping on and off a zone's edge
// would otherwise restart it every few frames, and a capsule that keeps restarting is a bar that keeps growing with no words in it.
const unsigned long long kTitleRepeatMs = kTitleMs + 3000;
// HUD.RenderObjectiveBar leaves stage 0 at LT.SetModulate(0) (D3DTOP_MODULATE2X when USE_MODULATE_2X is on, else DXFont::StageColour doubles channels),
// so channels >= 0x80 saturate to pure white. 0xff6c6c6c doubles to 0xd8 (216), giving a crisp readable light grey on translucent radio capsules.
const DWORD kWhite = 0xffffffff, kGrey = 0xff6c6c6c;

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

// One capsule: its text lines centred in the bar, the bar as wide as the longer line needs, grown from the middle by open (0 to 1). cy is the bar's middle.
void Capsule(const Label &label, float cx, float cy, float open)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font || open < 0.45f)
		return;
	WCHAR name[kChars], line[kChars];
	Drawable(name, label.name, font);
	Drawable(line, label.line, font);
	if (!name[0] && !line[0])
		return;
	SIZE a = {0, 0}, b = {0, 0};
	font->GetTextExtent(name, &a);
	if (line[0])
		font->GetTextExtent(line, &b);
	const float widest = float(a.cx > b.cx ? a.cx : b.cx);
	const float rows = line[0] ? 2.0f : 1.0f, pitch = float(font->GetHeight()) + 2.0f;
	const float height = 18.0f + rows * pitch;
	float width = widest + 48.0f;
	const float most = HUD.mViewportWidth - 2.0f * 64.0f;
	if (width > most)
		width = most;
	const float t = open >= 1.0f ? 1.0f : (open - 0.45f) / 0.55f;
	const float minBarW = widest + 24.0f;
	float barW = minBarW + (width - minBarW) * t;
	if (barW > width)
		barW = width;
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	RENDERINFO.Apply();
	HUD.RenderObjectiveBar(cx, cy, barW, height, 1.0f);
	RENDERINFO.EnableLighting(false);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.Apply();
	const DWORD white = TextFadeColour(kWhite, open);
	const DWORD grey = TextFadeColour(kGrey, open);
	const float top = cy - rows * pitch * 0.5f;
	font->DrawTextWithShadow(cx - float(a.cx) * 0.5f, top, white, name, 0, 1.0f, 1.0f, 1.0f);
	if (line[0])
		font->DrawTextWithShadow(cx - float(b.cx) * 0.5f, top + pitch, grey, line, 0, 1.0f, 1.0f, 1.0f);
}

// An exhibit's label: the radio messages' own bar, in the radio messages' own place and shape (CMessageBox::RenderOverlay), its text left-aligned from where the
// radio's text begins, the name white and the line under it grey. The bar keeps the radio's width, so it does not change with every name, and grows
// only for a name too long for it. open is 0 to 1 (the bar grows from the middle).
void ExhibitCapsule(const Label &label, const WCHAR *caption, float open, int number = 0)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font || open < 0.45f)
		return;
	WCHAR name[kChars], line[kChars], note[kChars];
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
	Drawable(line, caption, font);
	Drawable(note, label.note, font);
	if (!name[0] && !line[0] && !note[0])
		return;
	SIZE a = {0, 0}, b = {0, 0}, c = {0, 0};
	font->GetTextExtent(name, &a);
	if (line[0])
		font->GetTextExtent(line, &b);
	if (note[0])
		font->GetTextExtent(note, &c);
	if (c.cx > b.cx)
		b.cx = c.cx;
	const float viewLeft = HUD.mViewportX;
	const float viewW = HUD.mViewportWidth;
	const float maxBarW = viewW > 80.0f ? (viewW - 36.0f) : viewW;
	const float centre = viewLeft + viewW * 0.5f;
	const float y = (HUD.mViewportBottom + 16.0f - HUD_OFFSET_Y - 128.0f + 48.0f) + gMessageBoxY;
	float width = 230.0f - HUD.mViewportX * 2 - HUD_OFFSET_X * 2 + gMessageBoxWidth;
	const float widest = float(a.cx > b.cx ? a.cx : b.cx), padX = 24.0f, room = widest + 2.0f * padX;
	if (room > width)
		width = room;
	if (width > maxBarW)
		width = maxBarW;
	const float t = open >= 1.0f ? 1.0f : (open - 0.45f) / 0.55f;
	float minBarW = widest + 16.0f;
	if (minBarW > width)
		minBarW = width;
	float barW = minBarW + (width - minBarW) * t;
	if (barW > width)
		barW = width;
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	RENDERINFO.Apply();
	HUD.RenderObjectiveBar(centre, y, barW, 60.0f, 1.0f);
	RENDERINFO.EnableLighting(false);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.Apply();
	const DWORD white = TextFadeColour(kWhite, open);
	const DWORD grey = TextFadeColour(kGrey, open);
	const float rows = 1.0f + (line[0] ? 1.0f : 0.0f) + (note[0] ? 1.0f : 0.0f);
	const float availW = width > 2.0f * padX + 40.0f ? (width - 2.0f * padX) : width;
	float scaleX = (widest > availW && widest > 1.0f) ? (availW / widest) : 1.0f;
	if (scaleX < 0.72f)
		scaleX = 0.72f;
	const float left = centre - width * 0.5f + padX, top = y - gMessageTextY + (3.0f - rows) * 7.5f;
	font->DrawTextWithShadow(left, top, white, name, 0, scaleX, 1.0f, 1.0f);
	if (line[0])
		font->DrawTextWithShadow(left, top + 15.0f, grey, line, 0, scaleX, 1.0f, 1.0f);
	if (note[0])
		font->DrawTextWithShadow(left, top + (line[0] ? 30.0f : 15.0f), grey, note, 0, scaleX, 1.0f, 1.0f);
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
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font)
		return;
	WCHAR arrow[] = L"v";
	SIZE extent;
	font->GetTextExtent(arrow, &extent);
	const float x = HUD.mViewportX + u * HUD.mViewportWidth - 0.5f * float(extent.cx);
	const float y = HUD.mViewportY + v * HUD.mViewportHeight - float(extent.cy) - 2.0f;
	font->DrawTextWithShadow(x, y, 0xffffd477, arrow, 0, 1.0f, 1.0f, 1.0f);
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
        ExhibitCapsule(card,card.line,1.0f); return;
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
		ExhibitCapsule(exitLabel, exitLabel.line, CeMuseumCapsuleOpen(now, gExitSince, 0, kGrowMs, false));
		return;
	}
	if (gShown >= 0)
	{
		const Label &label = gLabels[gShown];
		gInspecting = atSign || CeMuseumInspectionNear(position.X - gAt[gShown][0], position.Y - gAt[gShown][1], label.radius, gInspecting);
		ExhibitCapsule(label, gInspecting ? label.detail : label.line, CeMuseumCapsuleOpen(now, gShownSince, 0, kGrowMs, false), atSign ? number : 0);
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
			Copy(card.name, ititle);
			Copy(card.line, iline);
			Copy(card.note, inote);
			ExhibitCapsule(card, card.line, 1.0f);
		}
	}
}
#endif
