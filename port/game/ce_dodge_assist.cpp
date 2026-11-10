// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_dodge_assist.cpp: the Dodge Button (patch 0220 at CController::DoMappings and CController::SendButtonAction).
//
// The game's ground dodge is a double tap: two full strafe pushes in opposite directions, the second between half and the whole
// of g_dash_time (0.2 s) after the first (README, Dodging; ce_dodge_assist_rules.h has the game's code in words). Players find the
// window hard. The Dodge Button gives them one press for it: hold Left Ctrl (a gamepad: B) while pushing left or right, or push
// left or right while holding it, and the controller update sends those two strafe actions itself, through the game's own path,
// as keys would. StrafeLeft and StrafeRight then dash with their own code: the dodge is the original one, the same speed, the same
// length, the same cost; only the typing of it is done for the player. The A then D (D then A) double tap keeps working as before.
//
// Ordinary CE play only. The assist is off, and the controller update is exactly the original's, in a replay, a recording, under
// probes or the virtual clocks, for a test pilot, in a demo or an attract loop, in a network game (the lockstep samples this
// peer's controls and its copy of the other peer would have to make the same two pushes at the same game times, which nothing here
// has been run to show), where the CE layer is off, and when ce.ini says dodge_button=0 (the CE Settings row Dodge Button). It
// adds an input and takes none away: a key or button the player has bound to a control of their own is not a Dodge Button, and
// without a press of it the player's strafe actions pass exactly as they did.
#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "player.h"
#include "PCController.h"
#include "eventmanager.h"
#include "console.h"
#include "CLIParams.h"
#include "ltshell.h"
#include "BattleEngine.h"
#include "BattleEngineWalkerPart.h"
#include "ce_dodge_assist.h"
#include "../platform/common/portlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__clang__) && !defined(__GNUC__)
#error The Dodge Button requires weak symbol support for the network boundary
#endif
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" int CE_SessionActive(void) __attribute__((weak));
extern "C" int CE_SettingGetBool(const char *key, int def);
bool PortCeLayer();
bool PortRecording();
int PortCeGamepadOn(int pad, int button);		// ce_gamepad.cpp
bool PortCeGamepadIsStick(int pad);

namespace {
const int kPads = 4;										// MAX_JOYPADS
const char *const kFenced[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
							   "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};

struct State
{
	CeDodge::Assist	mAssist;
	CeDodge::Walker	mWalker;
	const void		*mOf;									// the walker the assist's copy of the walker's memory is of
	bool			mActive;								// the last update found the assist eligible for this pad
	bool			mHaveWalker;
	float			mFirstAt;								// for the session log
	int				mSide;
};
State gState[kPads];
int gSetting = -1;											// ce.ini dodge_button, read when the game is next in a level
bool gSettingStale = true;

bool Present(const char *name)
{
	const char *value = getenv(name);
	return value && *value;
}

// The faithful and replay fences, the demos and the network: none of them may see the assist.
bool OrdinaryPlay()
{
	if (!PortCeLayer() || PortRecording()) return false;
	for (const char *name : kFenced) if (Present(name)) return false;
	if (CE_BootRequested && CE_BootRequested()) return false;
	if (CE_SessionActive && CE_SessionActive()) return false;
	if (PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo ||
		CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode) return false;
	return true;
}

// The ce.ini line, read when the game was last out of a level (the CE Settings page is only in the front end), not every frame.
bool SettingOn()
{
	if (gSettingStale)
	{
		gSetting = CE_SettingGetBool("dodge_button", 1) ? 1 : 0;
		gSettingStale = false;
	}
	return gSetting != 0;
}

// The Battle Engine the controller is steering, when it is steering a player's and the game is running (not paused). (A demo's
// playback or recording is out of OrdinaryPlay() above.)
CBattleEngine *Driven(CController *controller)
{
	if (!controller) return NULL;
	if (GAME.GetGameState() != GAME_STATE_PLAYING || GAME.IsPaused()) return NULL;
	for (int i = 0; i < GAME.GetNPlayers(); ++i)
	{
		CPlayer *player = GAME.GetPlayer(i);
		if (player && controller->GetToControl() == player)
			return player->GetBattleEngine();
	}
	return NULL;
}

CBattleEngineWalkerPart *WalkerOf(CBattleEngine *engine)
{
	return engine && engine->GetState() == BATTLE_ENGINE_STATE_WALKER ? engine->GetWalkerPart() : NULL;
}

// What StrafeLeft and StrafeRight take: they return at once in the air and during a dash.
bool Accepts(CBattleEngine *engine)
{
	CBattleEngineWalkerPart *walker = WalkerOf(engine);
	return walker && engine->IsOnGround() && walker->GetDashCount() == 0;
}

// A walker that can dodge now: alive, powered up, on the ground and not in the middle of a dash.
bool CanDodge(CBattleEngine *engine)
{
	return engine && !engine->IsDying() && engine->GetIsPoweredUp() && Accepts(engine);
}

// The assist's copy of the walker's memory is of one walker, and a walker remembers nothing it was not given since it was made. A Battle Engine that is dying, or
// a different one, ends the copy: in a co-op or versus game the game spawns a new engine, with a new walker, when a player is lost, and its clock runs on, so
// nothing else tells the assist that the walker it was following is gone. False while the engine is dying: there is no copy to keep.
bool OfThisWalker(State &s, CBattleEngine *engine)
{
	if (engine->IsDying())
	{
		s.mAssist.Reset();
		s.mOf = NULL;
		return false;
	}
	const void *walker = engine->GetWalkerPart();
	if (walker != s.mOf)
	{
		if (s.mOf) s.mAssist.Reset();
		s.mOf = walker;
	}
	return true;
}

bool Float(const char *name, float low, float high, float *out)
{
	char key[24];
	snprintf(key, sizeof(key), "%s", name);
	CConsoleVar *var = CONSOLE.FindVariable(key);
	if (!var || var->mType != CVar_float || !var->mData) return false;
	float value = *static_cast<float *>(var->mData);
	if (!(value >= low && value <= high)) return false;		// also rejects NaN
	*out = value;
	return true;
}

// The walker's registered numbers; retail's when the game has not registered them (no walker yet) or they are unusable.
void ReadWalker(CeDodge::Walker *walker)
{
	CeDodge::Walker w = {0.2f, 0.9f, 0.8f};
	Float("g_dash_time", 0.004f, 10.f, &w.mTime);
	Float("g_dash_start", 0.01f, 1.f, &w.mStart);
	Float("g_dash_end", 0.01f, 1.f, &w.mEnd);
	*walker = w;
}

bool KeyBound(int key)
{
	for (SControlBinding *b = CONTROL_BINDINGS; b->mId != -1; b++)
		for (int i = 0; i < 2; i++)
			if (b->mInput[i].mPad != -1 && b->mInput[i].mKey == key &&
				(b->mInput[i].mType == KEY_ON || b->mInput[i].mType == KEY_ONCE || b->mInput[i].mType == KEY_RELEASE))
				return true;
	return false;
}

bool PadButtonBound(int pad, int button)
{
	for (SControlBinding *b = CONTROL_BINDINGS; b->mId != -1; b++)
		for (int i = 0; i < 2; i++)
			if (b->mInput[i].mPad == pad && b->mInput[i].mKey == button &&
				(b->mInput[i].mType == BUTTON_ON || b->mInput[i].mType == BUTTON_ONCE || b->mInput[i].mType == BUTTON_REPEAT ||
				 b->mInput[i].mType == BUTTON_RELEASE))
				return true;
	return false;
}

bool PadDodgeDown(int pad)
{
	return PortCeDodgeAssistPadFree(pad) && PortCeGamepadOn(pad, CE_DODGE_PAD_BUTTON) > 0;
}
}

bool PortCeDodgeAssistKeyFree() { return !KeyBound(DIK_LCONTROL); }

bool PortCeDodgeAssistPadFree(int pad)
{
	return pad >= 0 && pad < kPads && !PortCeGamepadIsStick(pad) && !PadButtonBound(pad, CE_DODGE_PAD_BUTTON);
}

bool PortCeDodgeAssistOn()
{
	// The cue and the help ask every frame they are drawn: they take the value the assist is using, and ask ce.ini only if it never has.
	if (!OrdinaryPlay()) return false;
	return (gSetting >= 0 ? gSetting != 0 : SettingOn());
}

int PortCeDodgeAssistKey(int pad)
{
	// Only controller 0 reads the keyboard bindings (CController::DoMappings), so only it has the key; and none while text is typed.
	if (pad != 0 || !gState[0].mActive || LT.GetKeytrap()) return 0;
	return PortCeDodgeAssistKeyFree() ? DIK_LCONTROL : 0;
}

int PortCeDodgeAssistFilter(CController *controller, int pad, int button, float value)
{
	if ((button != BUTTON_MECH_STRAFE_LEFT && button != BUTTON_MECH_STRAFE_RIGHT) || pad < 0 || pad >= kPads) return 0;
	State &s = gState[pad];
	const bool active = s.mActive;
	CBattleEngine *engine = NULL;
	if (active)
		engine = Driven(controller);
	else
	{
		// Idle (the last update found the assist ineligible, or this is a level's first): the walker takes the player's pushes all the same, and
		// the assist keeps a copy of what the walker remembers, so it follows them wherever it would be working: ordinary play, the setting on,
		// the player driving a Battle Engine. The front end and the pause menu send strafe actions too and have no such engine: they cost nothing,
		// and ce.ini is asked once on the way into a level, as the end hook does, never for each action.
		engine = OrdinaryPlay() ? Driven(controller) : NULL;
		if (!engine || !SettingOn()) return 0;
	}
	if (engine && !OfThisWalker(s, engine)) return 0;
	if (!s.mHaveWalker)
	{
		ReadWalker(&s.mWalker);
		s.mHaveWalker = true;
	}
	const int side = button == BUTTON_MECH_STRAFE_LEFT ? -1 : 1;
	if (!active)
	{
		s.mAssist.Follow(side, value, EVENT_MANAGER.GetTime(), s.mWalker, CanDodge(engine));
		return 0;
	}
	return s.mAssist.Real(side, value, EVENT_MANAGER.GetTime(), s.mWalker, CanDodge(engine)) ? 1 : 0;
}

int PortCeDodgeAssistEnd(CController *controller, int pad, int keyDown, int *ids, int *hard)
{
	if (pad < 0 || pad >= kPads) return 0;
	State &s = gState[pad];
	CeDodge::Update u = {};
	const bool ordinary = OrdinaryPlay();
	CBattleEngine *engine = ordinary ? Driven(controller) : NULL;
	if (engine) OfThisWalker(s, engine);
	if (!engine) gSettingStale = true;								// out of a level: the next one asks ce.ini again
	// Out of the running game altogether (the front end, loading, the end screens; a pause is not that) the walker is gone and the next level's is
	// new: the assist's copy of what it remembered goes too, not only the clock running backwards, which a quick level start could miss.
	if (ordinary && GAME.GetGameState() != GAME_STATE_PLAYING)
	{
		s.mAssist.Reset();
		s.mOf = NULL;
	}
	u.mEligible = engine && SettingOn();
	s.mActive = u.mEligible;
	CeDodge::Tap taps[CeDodge::kMaxTaps];
	if (!u.mEligible)
	{
		s.mAssist.Step(u, taps);									// cancels what was under way
		return 0;
	}
	u.mNow = EVENT_MANAGER.GetTime();
	u.mDodgeDown = keyDown != 0 || PadDodgeDown(pad);
	u.mCanDodge = CanDodge(engine);
	u.mAccepts = CanDodge(engine);
	if (!s.mHaveWalker || u.mDodgeDown || s.mAssist.Waiting())
	{
		ReadWalker(&s.mWalker);
		s.mHaveWalker = true;
	}
	u.mWalker = s.mWalker;
	const int count = s.mAssist.Step(u, taps);
	for (int k = 0; k < count; ++k)
	{
		ids[k] = taps[k].mSide < 0 ? BUTTON_MECH_STRAFE_LEFT : BUTTON_MECH_STRAFE_RIGHT;
		hard[k] = taps[k].mHard ? 1 : 0;
	}
	if (count && s.mAssist.Waiting())
	{
		s.mFirstAt = u.mNow;											// the first push of a double tap (again, after a slow frame)
		s.mSide = s.mAssist.Side();
	}
	else if (count)
		PortSessionLog(PORT_SESSION_GAME, false, "dodge button: dodge %s, %d ms between the two pushes", s.mSide < 0 ? "left" : "right",
			(int)((u.mNow - s.mFirstAt) * 1000.f + 0.5f));
	return count;
}

#endif
