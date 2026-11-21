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
bool PortCeLayer();
WCHAR *PortCeSaveErrorText();								// FrontEndText.cpp, patch 0137: TEXT_DB.GetString(FETX_SAVEERROR)

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
