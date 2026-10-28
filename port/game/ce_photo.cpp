// SPDX-License-Identifier: GPL-3.0-only
// F11's HUD-hiding photo mode is temporarily disabled: suppressing the normal HUD/cockpit render calls changes later
// simulation state. The replacement render-only suppression is still under review. F12's screenshot key remains.
//
// F11 or the console's photo command explains the temporary unavailability; F12 saves a PNG in the user's screenshots
// folder (ce_screenshot.cpp, read back at the Present, d3d9hooks.cpp), and a brief note says so. The frame saved is
// drawn without that note. Level end cancels any pending screenshot.
//
// Never in faithful play: off in a replay, a recording, under probes or the virtual clocks, for a test pilot or a network
// game, in multiplayer, demos and the front end, or where the CE layer is off. BEA_PORT_PHOTO_PROOF=N with
// BEA_PORT_CE_LAYER=1 is a screenshot test switch: a screenshot every N Presents, without enabling photo mode.
#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "frontend.h"
#include "CLIParams.h"
#include "ltshell.h"
#include "Hud.h"
#include "SpriteRenderer.h"
#include "renderinfo.h"
#include "Texture.h"
#include "ce_photo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__clang__) && !defined(__GNUC__)
#error CE photo mode requires weak symbol support for the network boot boundary
#endif
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" unsigned PortPresentCount(void);				// d3d9hooks.cpp
bool PortCeLayer();
bool PortRecording();

namespace {
const unsigned kPhotoKey = DIK_F11, kShotKey = DIK_F12;
const unsigned long long kNoteMs = 2500;		// CE_ScreenshotTicks: SDL's milliseconds, never the game's clock
const char *const kFenced[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
							   "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};
bool shotPending;				// the next Present is saved; the note is not drawn in that frame
char note[96];
unsigned long long noteUntil;
bool holdNotice;				// place the short F11 notice away from the radio subtitles

bool Present(const char *name)
{
	const char *value = getenv(name);
	return value && *value;
}

// BEA_PORT_PHOTO_PROOF=N (N > 0) with the CE layer forced on: a screenshot every N Presents; no photo mode.
unsigned ProofEvery()
{
	const char *value = getenv("BEA_PORT_PHOTO_PROOF");
	if (!value || !Present("BEA_PORT_CE_LAYER")) return 0;
	const long n = strtol(value, NULL, 10);
	return n > 0 && n < 1000000 ? unsigned(n) : 0;
}

bool Allowed()
{
	if (!PortCeLayer()) return false;
	if (!ProofEvery())
	{
		if (PortRecording()) return false;
		for (const char *name : kFenced) if (Present(name)) return false;
	}
	if (CE_BootRequested && CE_BootRequested()) return false;
	if (PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo ||
		CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode || FRONTEND.IsRunning()) return false;
	return !GAME.IsMultiplayer() && GAME.GetNPlayers() == 1 && GAME.GetPlayer(0);
}

void Note(const char *text, bool hold = false)
{
	snprintf(note, sizeof(note), "%s", text);
	noteUntil = CE_ScreenshotTicks() + kNoteMs;
	holdNotice = hold;
}

}

bool PortCePhotoOn() { return false; }

// Photo mode on or off (the console's photo, F11); false when it is not available here (it is then off).
bool PortCePhotoSet(bool on)
{
	if (!Allowed()) return false;
	if (on) { Note("Photo unavailable (F12: screenshot).", true); return false; }
	noteUntil = 0;
	return true;
}

// F11 reports photo mode unavailable and F12 saves a screenshot, in an allowed level when no keytrap has the keyboard
// (ltshell.cpp, patch 0108, before the console's own keys). True when the key was taken; a held key's repeats are too.
bool PortCePhotoKey(unsigned key, bool repeat)
{
	if ((key != kPhotoKey && key != kShotKey) || LT.GetKeytrap() || !Allowed()) return false;
	if (repeat) return true;
	if (key == kPhotoKey) PortCePhotoSet(true);
	else shotPending = true;
	return true;
}

// Between frames (CGame::MainLoop, after the console's): discard a screenshot that is no longer allowed.
void PortCePhotoFrame()
{
	if (!Allowed()) shotPending = false;
}

// The level is ending (CGame::ShutdownRestartLoop, beside the console's): discard any unsaved screenshot.
void PortCePhotoLevelEnd()
{
	shotPending = false;
	noteUntil = 0;
}

// At the Present (d3d9hooks.cpp, after it counted this one): whether this frame is to be saved, as F12 asked or every
// N Presents under the proof switch. Asked once per Present.
bool PortCePhotoShotWanted()
{
	const unsigned every = ProofEvery();
	const bool wanted = (shotPending || (every && PortPresentCount() % every == 0)) && Allowed();
	shotPending = false;
	return wanted;
}

// The screenshot's outcome (ce_screenshot.cpp's CE_SHOT_* status) for the note.
void PortCePhotoShotDone(int status, const char *name)
{
	char text[96];
	switch (status)
	{
	case CE_SHOT_SAVED: snprintf(text, sizeof(text), "Saved screenshot %s", name ? name : ""); break;
	case CE_SHOT_FULL: snprintf(text, sizeof(text), "Screenshot not saved: the screenshots folder is full."); break;
	case CE_SHOT_FOLDER: snprintf(text, sizeof(text), "Screenshot not saved: no screenshots folder."); break;
	default: snprintf(text, sizeof(text), "Screenshot not saved (error %d).", -status); break;
	}
	Note(text);
}

// After the console (CDXEngine::PostRender): F11's short hold notice at top left, screenshot status at bottom centre;
// neither is drawn in a frame being saved. Other HUD elements may still share the notice's screen area.
void PortCePhotoRender()
{
	if (shotPending || !note[0] || CE_ScreenshotTicks() >= noteUntil || !Allowed()) return;
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	CDXTexture *white = HUD.GetWhiteTexture();
	if (!font || !white || white->GetRealWidth() <= 0 || white->GetRealHeight() <= 0 || font->GetHeight() <= 0) return;
	const float w = float(PLATFORM.GetWindowWidth()), h = float(PLATFORM.GetWindowHeight());
	if (w <= 0.f || h <= 0.f) return;
	const float scale = (w / 640.f < h / 480.f) ? w / 640.f : h / 480.f;
	WCHAR text[96];
	unsigned n = 0;
	for (const char *c = note; *c && n < 95; ++c)
		text[n++] = font->CanDisplay(WCHAR((unsigned char)*c)) ? WCHAR((unsigned char)*c) : L'-';
	text[n] = 0;
	SIZE extent;
	font->GetTextExtent(text, &extent);
	float textScale = holdNotice ? 0.9f * scale : scale;
	if (float(extent.cx) * textScale > w - 32.f * scale && extent.cx > 0) textScale = (w - 32.f * scale) / float(extent.cx);
	const float tw = float(extent.cx) * textScale, th = float(font->GetHeight()) * textScale;
	const float pad = 6.f * scale;
	const float x = holdNotice ? 16.f * scale : (w - tw) * 0.5f;
	const float y = holdNotice ? 48.f * scale : h - 40.f * scale - th;
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
