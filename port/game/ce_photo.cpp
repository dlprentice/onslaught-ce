// SPDX-License-Identifier: GPL-3.0-only
// CE photo mode: F11 or the console's photo command hides the HUD, the battle line and the cockpit, and F12 saves the
// frame as a PNG. Retail keeps the CLIPARAMS.mKillHUD field (CLIParams.h, +0x29c) and the cockpit's test of it (DXEngine.cpp,
// under DEV_VERSION), but nothing sets it, and photo mode does not either: it drops the draw calls the HUD overlay, the
// battle line and the cockpit routines make (patch 0133, CePhotoDrawScope, d3d9hooks.cpp) and lets the routines run in
// full. What they compute feeds the simulation: the HUD's warning panel calls CBattleEngine::TestDanger, which starts the
// incoming-warhead warning that draws from rand, so skipping the routines changed every later random draw (patch 0108's
// first design). Photo mode now only hides drawing and saves pictures: it moves no camera, pauses nothing and changes no
// game state. Menus, the message and briefing logs, the pause menu and the end of level screens are still drawn: they take
// input. The free camera is untouched: F and the camera command still need the Free camera cheat, and so does its Esc
// photo pause; photo mode needs none.
//
// F11 or the console's photo command switches it; F12 saves the frame as a PNG in the per-user folder's screenshots
// folder (ce_screenshot.cpp, read back at the Present, d3d9hooks.cpp), and a brief note says so. The frame saved is
// drawn without that note. Photo mode ends when the level ends.
//
// Never in faithful play: off in a replay, a recording, under probes or the virtual clocks, for a test pilot or a network
// game, in multiplayer, demos and the front end, or where the CE layer is off. BEA_PORT_PHOTO_PROOF=N with
// BEA_PORT_CE_LAYER=1 is a test switch: photo mode on in every level under a tape, and a screenshot every N Presents,
// so a replay can show that neither changes the game.
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
bool photo;						// photo mode on (the draw calls of the HUD, battle line and cockpit routines are dropped)
bool shotPending;				// F12 asked: the next Present is saved; the note is not drawn in that frame
char note[96];
unsigned long long noteUntil;

bool Present(const char *name)
{
	const char *value = getenv(name);
	return value && *value;
}

// BEA_PORT_PHOTO_PROOF=N (N > 0) with the CE layer forced on: photo mode in every level, a screenshot every N Presents.
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

// Whether the Present that follows the frame being drawn saves it: F12 asked, or the proof switch's next multiple of N (the
// Present counts itself before it asks, PortCePhotoShotWanted, so the frame in hand is the one after the Presents counted).
bool SavedNext()
{
	if (shotPending) return true;
	const unsigned every = ProofEvery();
	return every && (PortPresentCount() + 1) % every == 0;
}

void Note(const char *text)
{
	snprintf(note, sizeof(note), "%s", text);
	noteUntil = CE_ScreenshotTicks() + kNoteMs;
}

// Photo mode on or off, and the device's draw entries filtered or as the device came; asking again re-applies them, which
// a device made anew needs.
void Switch(bool on)
{
	photo = on;
	PortCeDrawHooks(on);
}

}

bool PortCePhotoOn() { return photo; }

// Whether the frame being drawn is the one the Present after it saves as a screenshot (the notices of ce_notice.cpp stay out of it).
bool PortCePhotoSavedNext() { return SavedNext(); }

// Photo mode on or off (the console's photo, F11); false when it is not available here (it is then off).
bool PortCePhotoSet(bool on)
{
	if (!Allowed()) { Switch(false); return false; }
	Switch(on);
	if (on) Note("Photo mode: HUD hidden. F11 returns, F12 saves a screenshot.");
	else noteUntil = 0;
	return true;
}

// F11 switches photo mode and F12 saves a screenshot, in a level where no menu or console has the keyboard
// (ltshell.cpp, patch 0108, before the console's own keys). True when the key was taken; a held key's repeats are too.
bool PortCePhotoKey(unsigned key, bool repeat)
{
	if ((key != kPhotoKey && key != kShotKey) || LT.GetKeytrap() || !Allowed()) return false;
	if (repeat) return true;
	if (key == kPhotoKey) PortCePhotoSet(!photo);
	else shotPending = true;
	return true;
}

// Between frames (CGame::MainLoop, after the console's): photo mode ends where it is no longer allowed, and the proof
// switch keeps it on.
void PortCePhotoFrame()
{
	if (!Allowed()) { Switch(false); shotPending = false; return; }
	Switch(photo || ProofEvery());
}

// The level is ending (CGame::ShutdownRestartLoop, beside the console's): photo mode off.
void PortCePhotoLevelEnd()
{
	Switch(false);
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

// After the console (CDXEngine::PostRender): the note, bottom centre, never in a frame being saved.
void PortCePhotoRender()
{
	if (SavedNext() || !note[0] || CE_ScreenshotTicks() >= noteUntil || !Allowed()) return;
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
