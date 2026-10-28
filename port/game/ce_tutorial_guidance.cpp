// SPDX-License-Identifier: GPL-3.0-only
// CE-only Level 100 guidance. The original script's messages, timing and inputs are unchanged.
#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "Controller.h"
#include "PCController.h"
#include "console.h"
#include "HelpText.h"
#include "Platform.h"
#include "CLIParams.h"
#include "text.h"
#include "ltshell.h"
#include "ce_tutorial_pad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool PortCeLayer();
int PortCeGamepadInputs(int id, int pad, SControlInput *out);
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

// In solo play controller 0 reads both binding slots, but controller 1 reads only slot 1 (CController::DoMappings).
// The front end can select either controller for player 1; do not advertise inputs that that controller never reads.
int PlayerPad()
{
	CPCController *controller = GAME.GetController(0);
	if (!controller) return -1;
	int pad = controller->GetPadNumber();
	return pad == 0 || pad == 1 ? pad : -1;
}

// While the mouse is acquired an earlier axis binding sends an action even at zero; that sets the action and can
// prevent the second saved input from being read. The locked variants can be available in some contexts, but do not
// advertise them as a reliable second binding. This does not suppress CE's separate centred-mouse default path.
bool SavedSlotUsable(const SControlBinding *binding, int playerPad, int slot)
{
	if (playerPad != 0 || slot != 1) return true;
	const SControlInput &first = binding->mInput[0];
	if (first.mPad == -1) return true;
	return first.mType != MOUSE_AXIS_PLUS_ANY && first.mType != MOUSE_AXIS_MINUS_ANY &&
		first.mType != MOUSE_AXIS_PLUS && first.mType != MOUSE_AXIS_MINUS;
}

bool BindingName(int id, bool strafe, CDXBitmapFont *font, char *out, int size)
{
	SControlBinding *binding = FindControlBinding(id);
	int playerPad = PlayerPad();
	if (!binding || playerPad < 0)
		return false;
	for (int i = playerPad; i < 2; ++i)
	{
		if (!SavedSlotUsable(binding, playerPad, i)) continue;
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

// Describe only controls that the active SDL adapter can read. Buttons 0-3 may have been rotated by retail's
// UpdateJoystick; the extra CE defaults and the player's saved bindings both use those game-side numbers.
bool PadInputName(const SControlInput &input, int id, const CeTutorialPadInfo &info, bool rotated,
	CDXBitmapFont *font, char *out, int size)
{
	bool strafe = id == BUTTON_MECH_STRAFE_LEFT || id == BUTTON_MECH_STRAFE_RIGHT;
	char name[32] = {};
	if (input.mType == ANALOGUE_MINUS || input.mType == ANALOGUE_PLUS)
	{
		bool plus = input.mType == ANALOGUE_PLUS;
		// A strafe action must receive the correct sign; a misconfigured opposite axis cannot perform that strafe.
		if (strafe && (plus != (id == BUTTON_MECH_STRAFE_RIGHT)))
			return false;
		unsigned bit = 0;
		const char *minus = NULL, *positive = NULL;
		switch (input.mKey)
		{
		case ANALOGUE_X1: bit = CE_PAD_X1; minus = info.kind == CE_TUTORIAL_STICK ? "twist left" : "left stick left";
			positive = info.kind == CE_TUTORIAL_STICK ? "twist right" : "left stick right"; break;
		case ANALOGUE_Y1: bit = CE_PAD_Y1; minus = "left stick up"; positive = "left stick down"; break;
		case ANALOGUE_X2: bit = CE_PAD_X2; minus = info.kind == CE_TUTORIAL_STICK ? "stick left" : "right stick left";
			positive = info.kind == CE_TUTORIAL_STICK ? "stick right" : "right stick right"; break;
		case ANALOGUE_Y2: bit = CE_PAD_Y2; minus = info.kind == CE_TUTORIAL_STICK ? "stick up" : "right stick up";
			positive = info.kind == CE_TUTORIAL_STICK ? "stick down" : "right stick down"; break;
		case ANALOGUE_POV_X: bit = plus ? CE_PAD_HAT_RIGHT : CE_PAD_HAT_LEFT;
			minus = info.kind == CE_TUTORIAL_STICK ? "hat left" : "D-pad left";
			positive = info.kind == CE_TUTORIAL_STICK ? "hat right" : "D-pad right"; break;
		case ANALOGUE_POV_Y: bit = plus ? CE_PAD_HAT_DOWN : CE_PAD_HAT_UP;
			minus = info.kind == CE_TUTORIAL_STICK ? "hat up" : "D-pad up";
			positive = info.kind == CE_TUTORIAL_STICK ? "hat down" : "D-pad down"; break;
		}
		if (!bit || !(info.axes & bit)) return false;
		snprintf(name, sizeof(name), "%s", plus ? positive : minus);
	}
	else if ((strafe && input.mType == BUTTON_ON) || (!strafe &&
		(input.mType == BUTTON_ON || input.mType == BUTTON_ONCE)))
	{
		int button = input.mKey;
		if (button < 0 || button >= 32) return false;
		int raw = rotated && button < 4 ? (button + 1) % 4 : button;
		if (!(info.buttons & (1u << raw))) return false;
		if (info.kind == CE_TUTORIAL_STICK)
			snprintf(name, sizeof(name), "stick button %d", raw + 1);
		else
		{
			// Positional names also work for controllers whose printed labels are not Xbox A/B/X/Y.
			const char *names[] = {"lower button", "right button", "left button", "upper button", "back button",
				"guide button", "start button", "left stick button", "right stick button", "left bumper", "right bumper"};
			if (raw < 11) snprintf(name, sizeof(name), "%s", names[raw]);
			else if (raw == 16 || raw == 17) snprintf(name, sizeof(name), "%s trigger", raw == 16 ? "left" : "right");
			else return false;
		}
	}
	else
		return false;
	if (strlen(name) > 24 || !Printable(font, name)) return false;
	snprintf(out, size, "%s", name);
	return true;
}

bool PadBindingName(int id, int pad, int playerPad, const CeTutorialPadInfo &info, CDXBitmapFont *font, char *out, int size)
{
	SControlBinding *binding = FindControlBinding(id);
	if (!binding) return false;
	for (int i = playerPad; i < 2; ++i)
		if (SavedSlotUsable(binding, playerPad, i) && binding->mInput[i].mPad == pad && PadInputName(binding->mInput[i], id, info,
			LT.mJoypadIsNewtype[pad], font, out, size)) return true;
	// The gameplay controller uses its own pad for CE defaults. The provider itself decides whether
	// defaults are active (custom joystick bindings switch them off), so the cue cannot advertise an inactive preset.
	if (pad == playerPad)
	{
		SControlInput extra[2];
		int count = PortCeGamepadInputs(id, pad, extra);
		for (int i = 0; i < count; ++i)
			if (PadInputName(extra[i], id, info, LT.mJoypadIsNewtype[pad], font, out, size)) return true;
	}
	return false;
}

bool PadPair(int leftId, int rightId, CDXBitmapFont *font, char *left, char *right)
{
	int playerPad = PlayerPad();
	if (playerPad < 0) return false;
	for (int pad = 0; pad < LT.mJoypads && pad < MAX_JOYPADS; ++pad)
	{
		CeTutorialPadInfo info = {};
		if (!LT.mUnknown33474[pad] || !PortCeTutorialPadInfo(LT.pJoystick[pad], &info)) continue;
		if (PadBindingName(leftId, pad, playerPad, info, font, left, 32) &&
			PadBindingName(rightId, pad, playerPad, info, font, right, 32) && strcmp(left, right)) return true;
	}
	return false;
}

// The walker's registered duration is private to the game. Give an interior interval in whole milliseconds, rather
// than the strict endpoints: at the default 0.2 seconds, 101-199ms is inside the actual (0.1, 0.2) window. If the
// variable is unavailable or unusable, avoid inventing a timing. Refresh reads it again before each draw.
bool DodgeTiming(char *out, int size)
{
	char name[] = "g_dash_time";
	CConsoleVar *var = CONSOLE.FindVariable(name);
	if (!var || var->mType != CVar_float || !var->mData) return false;
	float duration = *static_cast<float *>(var->mData);
	if (!(duration >= 0.004f && duration <= 10.f)) return false; // also rejects NaN and infinities
	int first = (int)(duration * 500.f) + 1;
	int last = (int)(duration * 1000.f) - 1;
	if (first > last) return false;
	if (first == last) snprintf(out, size, "%dms", first);
	else snprintf(out, size, "%d-%dms", first, last);
	return true;
}

// Do not repeat the stick or button name when both directions share it ("left stick left then right").
bool DirectionOrNumber(const char *word)
{
	if (!strcmp(word, "left") || !strcmp(word, "right") || !strcmp(word, "up") || !strcmp(word, "down")) return true;
	return *word && strspn(word, "0123456789") == strlen(word);
}

void PadSequence(const char *left, const char *right, char *out, int size)
{
	const char *l = strrchr(left, ' '), *r = strrchr(right, ' ');
	if (l && r && DirectionOrNumber(l + 1) && DirectionOrNumber(r + 1) &&
		l - left == r - right && !strncmp(left, right, (size_t)(l - left)))
		snprintf(out, size, "%s then %s", left, r + 1);
	else
		snprintf(out, size, "%s then %s", left, right);
}

void DodgeFallback(char *out, int size, bool timingKnown, const char *timing)
{
	if (timingKnown) snprintf(out, size, "Ground dodge: fully strafe one way, then the other %s apart.", timing);
	else snprintf(out, size, "Ground dodge: fully strafe one way, then the other. Timing varies.");
}

bool Build(int kind, WCHAR *out)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_NORMAL);
	if (!font || !font->IsValid())
		return false;
	char left[32], right[32], padLeft[32], padRight[32], line[192], timing[24] = {};
	bool timingKnown = kind == kTutorialDodge && DodgeTiming(timing, sizeof(timing));
	if (kind == kHelpZoomIn)
	{
		bool keys = BindingName(BUTTON_MECH_CHANGE_ZOOM_IN, false, font, left, sizeof(left)) &&
			BindingName(BUTTON_MECH_CHANGE_ZOOM_OUT, false, font, right, sizeof(right)) && strcmp(left, right);
		bool pad = PadPair(BUTTON_MECH_CHANGE_ZOOM_IN, BUTTON_MECH_CHANGE_ZOOM_OUT, font, padLeft, padRight);
		if (pad && keys)
		{
			snprintf(line, sizeof(line), "Zoom: %s/%s or %s/%s (in/out). Some weapons only.", left, right, padLeft, padRight);
			if (strlen(line) >= kBufferSize || !Printable(font, line))
				snprintf(line, sizeof(line), "Zoom in: %s. Zoom out: %s. Not every weapon zooms.", padLeft, padRight);
		}
		else if (pad)
			snprintf(line, sizeof(line), "Zoom in: %s. Zoom out: %s. Not every weapon zooms.", padLeft, padRight);
		else if (keys)
			snprintf(line, sizeof(line), "Zoom in: %s. Zoom out: %s. Not every weapon zooms.", left, right);
		else
			snprintf(line, sizeof(line), "%s", kZoomFallback);
	}
	else if (kind == kTutorialDodge)
	{
		bool keys = BindingName(BUTTON_MECH_STRAFE_LEFT, true, font, left, sizeof(left)) &&
			BindingName(BUTTON_MECH_STRAFE_RIGHT, true, font, right, sizeof(right)) && strcmp(left, right);
		bool pad = PadPair(BUTTON_MECH_STRAFE_LEFT, BUTTON_MECH_STRAFE_RIGHT, font, padLeft, padRight);
		char sequence[64] = {};
		if (pad) PadSequence(padLeft, padRight, sequence, sizeof(sequence));
		if (pad && keys)
		{
			if (timingKnown)
				snprintf(line, sizeof(line), "Ground dodge: %s then %s or fully push %s %s apart. Reverse for left.", left, right, sequence, timing);
			else
				snprintf(line, sizeof(line), "Ground dodge: %s then %s or fully push %s. Timing varies. Reverse for left.", left, right, sequence);
			if (strlen(line) >= kBufferSize || !Printable(font, line))
			{
				if (timingKnown) snprintf(line, sizeof(line), "Ground dodge right: fully push %s %s apart. Reverse for left.", sequence, timing);
				else snprintf(line, sizeof(line), "Ground dodge right: fully push %s. Timing varies. Reverse for left.", sequence);
			}
		}
		else if (pad)
		{
			if (timingKnown) snprintf(line, sizeof(line), "Ground dodge right: fully push %s %s apart. Reverse for left.", sequence, timing);
			else snprintf(line, sizeof(line), "Ground dodge right: fully push %s. Timing varies. Reverse for left.", sequence);
		}
		else if (keys)
		{
			if (timingKnown) snprintf(line, sizeof(line), "Ground dodge right: %s, then %s %s apart. Reverse for left.", left, right, timing);
			else snprintf(line, sizeof(line), "Ground dodge right: %s, then %s. Timing varies. Reverse for left.", left, right);
		}
		else
			DodgeFallback(line, sizeof(line), timingKnown, timing);
	}
	else
		return false;
	// The native WordWrap copies unbroken text into 100-WCHAR lines: a cue that could reach that length, or that the
	// font cannot draw, uses the generic text, and nothing is shown if even that does not fit.
	if (strlen(line) >= kBufferSize || !Printable(font, line))
	{
		if (kind == kTutorialDodge) DodgeFallback(line, sizeof(line), timingKnown, timing);
		else snprintf(line, sizeof(line), "%s", kZoomFallback);
	}
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
