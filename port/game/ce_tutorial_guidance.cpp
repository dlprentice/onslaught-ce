// SPDX-License-Identifier: GPL-3.0-only
// CE-only Level 100 guidance. The original script's messages, timing and inputs are unchanged.
#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "Controller.h"
#include "HelpText.h"
#include "Platform.h"
#include "CLIParams.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool PortCeLayer();
extern "C" int PortCeTutorialPrintableKey(int key, char *out, int size);

namespace {
// Script IDs, not message text. The retail AddHelpMessage ignores both zoom help IDs; only the first creates a
// cue. The dodge cue follows the radio message that explains dodging and starts when that message starts playing.
const int kHelpZoomIn = 8268984;
const int kTutorialDodge = 667656903;
// The movement and zoom facts these cues state, from the game's own code:
// - CBattleEngineWalkerPart::StrafeLeft/StrafeRight: a dash needs the engine on the ground and the opposite direction
//   pressed fully after more than half and less than all of g_dash_time (default 0.2 game seconds); a right then left
//   flick dashes left, left then right dashes right; repeating one direction does nothing.
// - CBattleEngine::ZoomIn: zoom only changes when the current weapon has the normal zoom mode.
const char kDodgeFallback[] = "Dodge on the ground: one strafe key, then the other 0.1-0.2 s later.";
const char kZoomFallback[] = "Zoom: see Zoom In and Zoom Out in Controller Configuration. Not every weapon zooms.";
// The native WordWrap writes into 100-WCHAR lines; keep even an unwrapped cue within one line buffer.
const int kBufferSize = 100;
WCHAR sText[MAX_HELP_TEXT_MESSAGES][kBufferSize];
int sKind[MAX_HELP_TEXT_MESSAGES];

bool Enabled()
{
	if (!PortCeLayer() || PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo ||
		CLIPARAMS.mPlaybackDemo || GAME.IsMultiplayer() || GAME.GetCurrentlyRunningLevelNum() != 100 ||
		TEXT.GetLanguage() != 0)
		return false;
	// Explicitly guard even if BEA_PORT_CE_LAYER forces the CE layer on during a tape or test.
	const char *names[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
		"BEA_NET_HOST", "BEA_NET_JOIN"};
	for (const char *name : names)
	{
		const char *value = getenv(name);
		if (value && *value)
			return false;
	}
	return true;
}

bool Printable(CDXBitmapFont *font, const char *text)
{
	for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
		if (*p < 32 || *p > 126 || !font->CanDisplay((WCHAR)*p))
			return false;
	return true;
}

// Named physical keys are layout-independent; printable keys use SDL's current keyboard layout.
bool KeyName(int key, char *out, int size)
{
	if (key == 0x2a || key == 0x36 || key == 0x1d || key == 0x9d || key == 0x38 || key == 0xb8)
	{
		const char *names[] = {"Left Shift", "Right Shift", "Left Ctrl", "Right Ctrl", "Left Alt", "Right Alt"};
		const int keys[] = {0x2a, 0x36, 0x1d, 0x9d, 0x38, 0xb8};
		for (int i = 0; i < 6; ++i)
			if (key == keys[i])
			{
				snprintf(out, size, "%s", names[i]);
				return true;
			}
	}
	const int specialKeys[] = {0x0f, 0x1c, 0x39, 0xc8, 0xcb, 0xcd, 0xd0};
	const char *specialNames[] = {"Tab", "Enter", "Space", "Up arrow", "Left arrow", "Right arrow", "Down arrow"};
	for (int i = 0; i < 7; ++i)
		if (key == specialKeys[i])
		{
			snprintf(out, size, "%s", specialNames[i]);
			return true;
		}
	return PortCeTutorialPrintableKey(key, out, size) != 0;
}

bool BindingName(int id, bool strafe, CDXBitmapFont *font, char *out, int size)
{
	SControlBinding *binding = FindControlBinding(id);
	if (!binding)
		return false;
	for (int i = 0; i < 2; ++i)
	{
		const SControlInput &input = binding->mInput[i];
		if (input.mPad == -1)
			continue;
		char name[32] = {};
		bool known = false;
		if ((strafe && input.mType == KEY_ON) || (!strafe &&
			(input.mType == KEY_ON || input.mType == KEY_ONCE || input.mType == KEY_RELEASE)))
			known = KeyName(input.mKey, name, sizeof(name));
		else if (!strafe && (input.mType == MOUSE_BUTTON_ON || input.mType == MOUSE_BUTTON_ONCE ||
			input.mType == MOUSE_BUTTON_RELEASE))
		{
			const char *mouse[] = {"Left mouse button", "Right mouse button", "Middle mouse button",
				"Mouse wheel up", "Mouse wheel down"};
			if (input.mKey >= 0 && input.mKey <= 4 && (input.mKey <= 2 || input.mType != MOUSE_BUTTON_ON))
			{
				snprintf(name, sizeof(name), "%s", mouse[input.mKey]);
				known = true;
			}
		}
		if (known && strlen(name) <= 24 && Printable(font, name))
		{
			snprintf(out, size, "%s", name);
			return true;
		}
	}
	return false;
}

bool Build(int kind, WCHAR *out)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_NORMAL);
	if (!font || !font->IsValid())
		return false;
	char left[32], right[32], line[192];
	if (kind == kHelpZoomIn)
	{
		if (BindingName(BUTTON_MECH_CHANGE_ZOOM_IN, false, font, left, sizeof(left)) &&
			BindingName(BUTTON_MECH_CHANGE_ZOOM_OUT, false, font, right, sizeof(right)) && strcmp(left, right))
			snprintf(line, sizeof(line), "Zoom in: %s. Zoom out: %s. Not every weapon zooms.", left, right);
		else
			snprintf(line, sizeof(line), "%s", kZoomFallback);
	}
	else if (kind == kTutorialDodge)
	{
		if (BindingName(BUTTON_MECH_STRAFE_LEFT, true, font, left, sizeof(left)) &&
			BindingName(BUTTON_MECH_STRAFE_RIGHT, true, font, right, sizeof(right)) && strcmp(left, right))
			snprintf(line, sizeof(line), "Dodge right: %s, then %s 0.1-0.2 s later. Reverse for left.", left, right);
		else
			snprintf(line, sizeof(line), "%s", kDodgeFallback);
	}
	else
		return false;
	// The native WordWrap copies unbroken text into 100-WCHAR lines: a cue that could reach that length, or that the
	// font cannot draw, uses the generic text, and nothing is shown if even that does not fit.
	if (strlen(line) >= kBufferSize || !Printable(font, line))
		snprintf(line, sizeof(line), "%s", kind == kTutorialDodge ? kDodgeFallback : kZoomFallback);
	if (strlen(line) >= kBufferSize || !Printable(font, line))
		return false;
	int i = 0;
	for (; line[i] && i < kBufferSize - 1; ++i)
		out[i] = (WCHAR)(unsigned char)line[i];
	out[i] = 0;
	return true;
}

int Owned(WCHAR *text)
{
	for (int i = 0; i < MAX_HELP_TEXT_MESSAGES; ++i)
		if (text == sText[i])
			return i;
	return -1;
}
}

// Refresh before the existing draw, so opening Controller Configuration and remapping cannot leave a stale label.
void PortCeTutorialRefresh(CHelpTextDisplay *help)
{
	for (int i = 0; i < MAX_HELP_TEXT_MESSAGES; ++i)
	{
		if (!help->mMessages[i].mActive)
			continue;
		int owned = Owned(help->mMessages[i].mText);
		if (owned >= 0 && (!Enabled() || !Build(sKind[owned], sText[owned])))
			help->mMessages[i].mActive = FALSE;
	}
}

namespace {
// One CE cue at a time: a new cue retires the other CE cue, never the game's own help.
void Cue(int kind)
{
	if (!Enabled())
		return;
	CHelpTextDisplay *help = GAME.GetHelpTextDisplay();
	if (!help)
		return;
	bool freeSlot = false;
	int retire = -1;
	for (int i = 0; i < MAX_HELP_TEXT_MESSAGES; ++i)
	{
		if (!help->mMessages[i].mActive)
			freeSlot = true;
		else
		{
			int owned = Owned(help->mMessages[i].mText);
			if (owned >= 0)
			{
				if (sKind[owned] == kind)
					return;		// already showing
				retire = i;
			}
		}
	}
	if (!freeSlot && retire < 0)
		return;		// never displace the game's own help
	WCHAR text[kBufferSize];
	if (!Build(kind, text))
		return;
	if (retire >= 0)
		help->mMessages[retire].mActive = FALSE;
	// No CE cue is showing now, so every buffer is free.
	memcpy(sText[0], text, sizeof(text));
	sKind[0] = kind;
	help->AddMessage(sText[0]);
}
}

// IScript::AddHelpMessage, after the original handling: the first zoom help ID (which retail ignores) adds the zoom cue.
void PortCeTutorialHelpCue(int id)
{
	if (id == kHelpZoomIn)
		Cue(kHelpZoomIn);
}

// CMessageBox::StartMessage, once the message is known to play (its speaker is alive): the radio message that explains
// dodging adds the dodge cue now, not when the script queued it, so the cue's 20 seconds run with the message. A message
// dropped from the queue, or whose speaker died before it started, never reaches here.
void PortCeTutorialRadioStart(int id)
{
	if (id == kTutorialDodge)
		Cue(kTutorialDodge);
}

// The original script help has priority if CE hints occupy both available slots.
void PortCeTutorialYield(CHelpTextDisplay *help)
{
	for (int i = 0; i < MAX_HELP_TEXT_MESSAGES; ++i)
		if (!help->mMessages[i].mActive)
			return;
	for (int i = 0; i < MAX_HELP_TEXT_MESSAGES; ++i)
		if (Owned(help->mMessages[i].mText) >= 0)
		{
			help->mMessages[i].mActive = FALSE;
			return;
		}
}
#endif
