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

namespace {
const int kChars = 96;
const unsigned long long kTitleMs = 5200, kGrowMs = 350;
// The same title asked for again this soon after it was put up keeps its own clock: a visitor stepping on and off a zone's edge
// would otherwise restart it every few frames, and a capsule that keeps restarting is a bar that keeps growing with no words in it.
const unsigned long long kTitleRepeatMs = kTitleMs + 3000;
// The text stage doubles a colour's channels (a channel of 0x80 or more is full white), so the note's grey is the half the notes' own grey is written as.
const DWORD kWhite = 0xffffffff, kGrey = 0xff545454;

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

// One capsule: its text lines centred in the bar, the bar as wide as the longer line needs, grown from the middle by open (0 to 1). cy is the bar's middle.
void Capsule(const Label &label, float cx, float cy, float open)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font || open <= 0.0f)
		return;
	WCHAR name[kChars], line[kChars];
	Drawable(name, label.name, font);
	Drawable(line, label.line, font);
	SIZE a = {0, 0}, b = {0, 0};
	font->GetTextExtent(name, &a);
	if (line[0])
		font->GetTextExtent(line, &b);
	const float widest = float(a.cx > b.cx ? a.cx : b.cx);
	const float rows = line[0] ? 2.0f : 1.0f, pitch = float(font->GetHeight()) + 2.0f;
	const float height = 18.0f + rows * pitch;
	float width = widest + 20.0f;
	const float most = HUD.mViewportWidth - 2.0f * 64.0f;
	if (width > most)
		width = most;
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	RENDERINFO.Apply();
	HUD.RenderObjectiveBar(cx, cy, width * open, height, 1.0f);
	if (open < 0.9f)
		return;										// the text appears once the bar is nearly open, as the radio's does
	RENDERINFO.EnableLighting(false);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.Apply();
	const float top = cy - rows * pitch * 0.5f;
	font->DrawTextWithShadow(cx - float(a.cx) * 0.5f, top, kWhite, name, 0, 1.0f, 1.0f, 1.0f);
	if (line[0])
		font->DrawTextWithShadow(cx - float(b.cx) * 0.5f, top + pitch, kGrey, line, 0, 1.0f, 1.0f, 1.0f);
}

// An exhibit's label: the radio messages' own bar, in the radio messages' own place and shape (CMessageBox::RenderOverlay), its text left-aligned from where the
// radio's text begins, the name white and the line under it grey. The bar keeps the radio's width, so it does not change with every name, and grows
// only for a name too long for it. open is 0 to 1 (the bar grows from the middle).
void ExhibitCapsule(const Label &label, const WCHAR *caption, float open, int number = 0)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font || open <= 0.0f)
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
	SIZE a = {0, 0}, b = {0, 0}, c = {0, 0};
	font->GetTextExtent(name, &a);
	if (line[0])
		font->GetTextExtent(line, &b);
	if (note[0])
		font->GetTextExtent(note, &c);
	if (c.cx > b.cx)
		b.cx = c.cx;
	const float centre = (HUD.mViewportWidth * 0.5f + HUD.mViewportX) + gMessageBoxX + 12.0f;
	const float y = (HUD.mViewportBottom + 16.0f - HUD_OFFSET_Y - 128.0f + 48.0f) + gMessageBoxY;
	float width = 230.0f - HUD.mViewportX * 2 - HUD_OFFSET_X * 2 + gMessageBoxWidth;
	const float widest = float(a.cx > b.cx ? a.cx : b.cx), room = widest + 2.0f * 34.0f;
	if (room > width)
		width = room;
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	RENDERINFO.Apply();
	HUD.RenderObjectiveBar(centre, y, width * open, 60.0f, 1.0f);
	if (open < 0.9f)
		return;										// the text appears once the bar is nearly open, as the radio's does
	RENDERINFO.EnableLighting(false);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.Apply();
	const float rows = 1.0f + (line[0] ? 1.0f : 0.0f) + (note[0] ? 1.0f : 0.0f);
	const float left = centre - width * 0.5f - 12.0f, top = y - gMessageTextY + (3.0f - rows) * 7.5f;
	font->DrawTextWithShadow(left, top, kWhite, name, 0, 1.0f, 1.0f, 1.0f);
	if (line[0])
		font->DrawTextWithShadow(left, top + 15.0f, kGrey, line, 0, 1.0f, 1.0f, 1.0f);
	if (note[0])
		font->DrawTextWithShadow(left, top + (line[0] ? 30.0f : 15.0f), kGrey, note, 0, 1.0f, 1.0f, 1.0f);
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

extern "C" bool PortCeMuseumCinemaCue(char *,unsigned,char *,unsigned);
extern "C" void PortCeMuseumLabelsRender(void)
{
	if (!PortCeMuseumVisitActive())
		return;
	PortCeMuseumVisitFrame();
    CinemaPanelDraw();
	if (!gCount && !gHasTitle)
		return;
	const unsigned long long now = CE_ScreenshotTicks();
	const float cx = HUD.mViewportX + HUD.mViewportWidth * 0.5f;
	if (gHasTitle)
	{
		if (!gTitleSince)
			gTitleSince = now;
		// The title grows open as the radio's bar does, then goes at once when its time is up: a bar shrinking shut has already lost
		// its words (they show only on a nearly open bar), so it would read as an empty capsule.
		const float open = now - gTitleSince >= kTitleMs ? 0.0f : CeMuseumCapsuleOpen(now, gTitleSince, kTitleMs, kGrowMs, false);
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
    char cinemaTitle[48]={},cinemaLine[96]={};
    if (PortCeMuseumCinemaCue(cinemaTitle,sizeof cinemaTitle,cinemaLine,sizeof cinemaLine)) {
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
	const int nearest = atSign ? sign : (level && !exit ? PortCeMuseumLabelsNearest(position.X, position.Y, heading.X, heading.Y) : -1);
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
	}
}
#endif
