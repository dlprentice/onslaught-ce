// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_gamepad.cpp: the CE layer's gamepad support (patch 0106 at CController::DoMappings and the CPCController
// joystick reads). In ordinary CE single-player play only:
//
// - Press and release edges per controller update. Retail compares a pad with its state one Present earlier
//   (LT.JoyButtonOnce), but the game reads its controllers once per game update, so at more Presents than updates a
//   press or release that does not land on an update's own Present is never seen, for any joystick binding. Here each
//   controller update snapshots its pad's buttons and an edge is a change since the previous update's snapshot.
// - An SDL gamepad (Xbox layout) or a flight stick works without setup. Retail binds menus to keys only, and its joystick preset
//   (FEPControls' sJoyBindings) goes only into an input a keyboard preset leaves free, which the shipped presets never
//   do, and names buttons SDL's gamepad layout does not have. So:
//   - menus (always): D-pad or left stick to move, A to select (and skip a cutscene or a level's opening pan), B to
//     go back, Start to pause;
//   - the sixteen game controls, only while none of them has any joystick input: the sticks as retail's joystick
//     preset uses them (left moves, right looks), right trigger charges and fires the gun pod, left trigger the landing
//     jets, A transforms, X changes weapon, Y cloaks, RB zooms in and LB out.
//   A flight stick (kStick): stick to look and steer, twist to strafe, hat to walk, trigger to charge and fire, thumb
//   button to change weapon, the next two to transform and cloak, then landing jets and zoom in and out.
//   These are extra inputs read beside the bindings: nothing is written to CONTROL_BINDINGS, so no binding is
//   overwritten and careers save exactly what the player chose. Binding any game control to a joystick input in the
//   controls editor turns the game-control defaults off (the menu ones stay).
//
// Faithful builds, tapes, recordings, probes, the virtual clock and multiplayer keep retail's behaviour.
#ifdef CE_VERSION

#include "common.h"
#include "Controller.h"
#include "game.h"
#include "ltshell.h"

#include <stdlib.h>
#include <string.h>

bool PortCeLayer();

namespace
{
// Game button numbers for an SDL gamepad. port/platform/common/dinput.cpp reports SDL's order (A 0, B 1, X 2, Y 3,
// Start 6, LB 9, RB 10; the D-pad as the hat, the triggers as 16 and 17) and a Z axis, and retail's UpdateJoystick
// rotates the first four buttons of any pad with a Z axis "into 'joybox' format" (ltshell.cpp): game 0 is B, 1 X,
// 2 Y, 3 A. The controls editor shows and stores these game numbers.
enum { PAD_B = 0, PAD_X = 1, PAD_Y = 2, PAD_A = 3, PAD_START = 6, PAD_LB = 9, PAD_RB = 10, PAD_LT = 16, PAD_RT = 17 };
const int kButtons = 32;	// DIJOYSTATE's first 32 buttons: every one the adapter reports

struct Default
{
	int id, type;
	short key;
};

// Menus: retail's keys, and these.
const Default kMenu[] = {
	{BUTTON_FRONTEND_MENU_UP, ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_POV_Y},
	{BUTTON_FRONTEND_MENU_UP, ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_Y1},
	{BUTTON_FRONTEND_MENU_DOWN, ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_POV_Y},
	{BUTTON_FRONTEND_MENU_DOWN, ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_Y1},
	{BUTTON_FRONTEND_MENU_LEFT, ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_POV_X},
	{BUTTON_FRONTEND_MENU_LEFT, ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_X1},
	{BUTTON_FRONTEND_MENU_RIGHT, ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_POV_X},
	{BUTTON_FRONTEND_MENU_RIGHT, ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_X1},
	{BUTTON_PAUSE_MENU_UP, ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_POV_Y},
	{BUTTON_PAUSE_MENU_UP, ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_Y1},
	{BUTTON_PAUSE_MENU_DOWN, ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_POV_Y},
	{BUTTON_PAUSE_MENU_DOWN, ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT, ANALOGUE_Y1},
	{BUTTON_FRONTEND_MENU_SELECT, BUTTON_ONCE, PAD_A},
	{BUTTON_PAUSE_MENU_SELECT, BUTTON_ONCE, PAD_A},
	{BUTTON_SKIP_CUTSCENE, BUTTON_ONCE, PAD_A},
	{BUTTON_SKIP_PANNING, BUTTON_ONCE, PAD_A},
	{BUTTON_FRONTEND_MENU_BACK, BUTTON_ONCE, PAD_B},
	{BUTTON_PAUSE, BUTTON_ONCE, PAD_START},
};

// The sixteen game controls: the sticks as retail's sJoyBindings, the buttons for the Xbox layout.
const Default kGame[] = {
	{BUTTON_MECH_YAW_LEFT, ANALOGUE_MINUS, ANALOGUE_X2},
	{BUTTON_MECH_YAW_RIGHT, ANALOGUE_PLUS, ANALOGUE_X2},
	{BUTTON_MECH_PITCH_UP, ANALOGUE_PLUS, ANALOGUE_Y2},
	{BUTTON_MECH_PITCH_DOWN, ANALOGUE_MINUS, ANALOGUE_Y2},
	{BUTTON_MECH_STRAFE_LEFT, ANALOGUE_MINUS, ANALOGUE_X1},
	{BUTTON_MECH_STRAFE_RIGHT, ANALOGUE_PLUS, ANALOGUE_X1},
	{BUTTON_MECH_FORWARD, ANALOGUE_MINUS, ANALOGUE_Y1},
	{BUTTON_MECH_BACKWARD, ANALOGUE_PLUS, ANALOGUE_Y1},
	{BUTTON_MECH_CHARGE_GUN_POD, BUTTON_ON, PAD_RT},
	{BUTTON_MECH_FIRE_GUN_POD, BUTTON_RELEASE, PAD_RT},
	{BUTTON_MECH_LANDING_JETS, BUTTON_ON, PAD_LT},
	{BUTTON_MECH_MORPH, BUTTON_ONCE, PAD_A},
	{BUTTON_MECH_CHANGE_WEAPON, BUTTON_ONCE, PAD_X},
	{BUTTON_MECH_CLOAK, BUTTON_ONCE, PAD_Y},
	{BUTTON_MECH_CHANGE_ZOOM_IN, BUTTON_ONCE, PAD_RB},
	{BUTTON_MECH_CHANGE_ZOOM_OUT, BUTTON_ONCE, PAD_LB},
};

// A flight stick (a joystick SDL does not know as a gamepad; dinput.cpp): its stick X and Y arrive as the right stick,
// its twist as the left stick's X and its hat as the POV, and its raw buttons go through the same rotation, so game 3 is
// the trigger, 0 the thumb button, 1 and 2 the next two; 4 and up are unrotated. Menus use kMenu as for a gamepad
// (hat to move, trigger to select, thumb button back).
enum { STICK_THUMB = 0, STICK_2 = 1, STICK_3 = 2, STICK_TRIGGER = 3, STICK_4 = 4, STICK_5 = 5, STICK_6 = 6 };
const Default kStick[] = {
	{BUTTON_MECH_YAW_LEFT, ANALOGUE_MINUS, ANALOGUE_X2},
	{BUTTON_MECH_YAW_RIGHT, ANALOGUE_PLUS, ANALOGUE_X2},
	{BUTTON_MECH_PITCH_UP, ANALOGUE_PLUS, ANALOGUE_Y2},
	{BUTTON_MECH_PITCH_DOWN, ANALOGUE_MINUS, ANALOGUE_Y2},
	{BUTTON_MECH_STRAFE_LEFT, ANALOGUE_MINUS, ANALOGUE_X1},
	{BUTTON_MECH_STRAFE_RIGHT, ANALOGUE_PLUS, ANALOGUE_X1},
	{BUTTON_MECH_FORWARD, ANALOGUE_MINUS, ANALOGUE_POV_Y},
	{BUTTON_MECH_BACKWARD, ANALOGUE_PLUS, ANALOGUE_POV_Y},
	{BUTTON_MECH_CHARGE_GUN_POD, BUTTON_ON, STICK_TRIGGER},
	{BUTTON_MECH_FIRE_GUN_POD, BUTTON_RELEASE, STICK_TRIGGER},
	{BUTTON_MECH_CHANGE_WEAPON, BUTTON_ONCE, STICK_THUMB},
	{BUTTON_MECH_MORPH, BUTTON_ONCE, STICK_2},
	{BUTTON_MECH_CLOAK, BUTTON_ONCE, STICK_3},
	{BUTTON_MECH_LANDING_JETS, BUTTON_ON, STICK_4},
	{BUTTON_MECH_CHANGE_ZOOM_IN, BUTTON_ONCE, STICK_5},
	{BUTTON_MECH_CHANGE_ZOOM_OUT, BUTTON_ONCE, STICK_6},
};

// True when the game's pad is a flight stick rather than a gamepad: the adapter's device type, asked every time, since
// hotplug can put a flight stick in a slot that held a gamepad or nothing (the adapter answers from the device in it now).
bool IsStick(int pad)
{
	DIDEVCAPS caps;
	memset(&caps, 0, sizeof(caps));
	caps.dwSize = sizeof(caps);
	return LT.pJoystick[pad] && SUCCEEDED(LT.pJoystick[pad]->GetCapabilities(&caps)) &&
		(caps.dwDevType & 0xff) == DI8DEVTYPE_JOYSTICK;
}

// Each pad's buttons at the previous and the current controller update.
unsigned char sPrevious[MAX_JOYPADS][kButtons], sCurrent[MAX_JOYPADS][kButtons];

bool Ordinary()
{
	static int ordinary = -1;
	if (ordinary < 0)
	{
		ordinary = 1;
		const char *const faithful[] = {"BEA_PORT_TAPE", "BEA_PORT_RECORD", "BEA_PORT_CLOCK", "BEA_PORT_PROBES"};
		for (const char *name : faithful)
		{
			const char *value = getenv(name);
			if (value && *value)
				ordinary = 0;
		}
	}
	return ordinary && PortCeLayer();
}

bool Allowed(int pad)
{
	return Ordinary() && !GAME.IsMultiplayer() && pad >= 0 && pad < LT.mJoypads && pad < MAX_JOYPADS &&
		   LT.mUnknown33474[pad];
}

// True while no configurable control reads a joystick (the joystick push types come before KEY_ONCE).
bool NoJoystickBindings()
{
	for (SControlBinding *b = CONTROL_BINDINGS; b->mId != -1; b++)
		if (b->mConfigurable)
			for (int i = 0; i < 2; i++)
				if (b->mInput[i].mPad != -1 && b->mInput[i].mType < KEY_ONCE)
					return false;
	return true;
}
}

// At the start of a controller update: this pad's buttons become the current snapshot, the last one the previous.
void PortCeGamepadUpdate(int pad)
{
	if (!Allowed(pad))
		return;
	memcpy(sPrevious[pad], sCurrent[pad], kButtons);
	const DIJOYSTATE2 *state = LT.JoyState(pad);
	for (int b = 0; b < kButtons; b++)
		sCurrent[pad][b] = state && state->rgbButtons[b] ? 1 : 0;
}

// A joystick button's press (release false) or release edge since the previous controller update. Returns -1 when
// retail's own per-Present edge applies instead (outside ordinary CE single-player play, or an unknown button).
int PortCeGamepadEdge(int pad, int button, bool release)
{
	if (!Allowed(pad) || button < 0 || button >= kButtons)
		return -1;
	return release ? (sPrevious[pad][button] && !sCurrent[pad][button]) : (!sPrevious[pad][button] && sCurrent[pad][button]);
}

// The CE gamepad inputs for this control and pad (at most 2), written to out; 0 when none apply.
int PortCeGamepadInputs(int id, int pad, SControlInput *out)
{
	if (!Allowed(pad))
		return 0;
	int n = 0;
	for (const Default &d : kMenu)
		if (d.id == id && n < 2)
			out[n++] = SControlInput{pad, d.type, d.key, 0};
	if (!n && NoJoystickBindings())
	{
		const Default *table = IsStick(pad) ? kStick : kGame;
		int size = IsStick(pad) ? (int)(sizeof(kStick) / sizeof(kStick[0])) : (int)(sizeof(kGame) / sizeof(kGame[0]));
		for (int t = 0; t < size; t++)
			if (table[t].id == id && n < 2)
				out[n++] = SControlInput{pad, table[t].type, table[t].key, 0};
	}
	return n;
}

#endif
