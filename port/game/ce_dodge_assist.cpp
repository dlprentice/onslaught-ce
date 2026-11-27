// Optional keyboard and gamepad shortcut for the walker's four ground
// dodges. Movement comes from the player's actual bindings. The shortcut
// sends ordinary actions through the controller; the walker decides to dash.
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
#include "ce_dodge_binding.h"
#include <initializer_list>
#include "../platform/common/ce_record_mode.h"
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
bool PortCeExperimental(const char *mode);
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
	CeDodge::PlanarAssist mAssist;
	CeDodge::Walker	mWalker;
	const void		*mOf;									// the walker the assist's copy of the walker's memory is of
	bool			mActive;								// the last update found the assist eligible for this pad
	bool			mHaveWalker;
	float			mFirstAt;								// for the session log
	int				mSide;
};
State gState[kPads];
int gSetting = -1;											// ce.ini dodge_button, as last read (-1: never)
// Each pad asks ce.ini when it next drives a Battle Engine after an update in which it drove none: on the way into a level, as the CE Settings page is only in
// the front end. The flag is the pad's own. In split screen one player's controller drives nothing while that player waits to come back and the other's is in the
// game: a flag the four pads shared was raised by the first and answered by the second in every controller update of that wait, each answer a read of the whole
// of ce.ini. gSettingStale is the flag of the pad whose controller update is running; both hooks say which before anything asks.
bool gStale[kPads] = {true, true, true, true};
int gPad = 0;
#define gSettingStale gStale[gPad]

bool Present(const char *name)
{
	const char *value = getenv(name);
	return value && *value;
}

bool RecordCeAllowed()
{
#ifdef PORT_CE_RECORD_MODE_H
	return ce_record_ce_allowed() != 0;
#else
	const char *ceRec = getenv("BEA_PORT_RECORD_CE");
	if (!ceRec || ceRec[0] != '1' || ceRec[1] != '\0') return false;
	if (!Present("BEA_PORT_RECORD") && !Present("BEA_PORT_TAPE")) return false;
	if (Present("BEA_PORT_PILOT") || Present("BEA_PORT_AUTOPILOT") ||
	    Present("BEA_NET_HOST") || Present("BEA_NET_JOIN") ||
	    Present("BEA_LOCKSTEP_HOST") || Present("BEA_LOCKSTEP_JOIN")) return false;
	return true;
#endif
}

// The faithful and replay fences, the demos and the network: none of them may see the assist.
bool OrdinaryPlay()
{
	if (!RecordCeAllowed())
	{
		if (!PortCeLayer() || PortRecording()) return false;
		for (const char *name : kFenced) if (Present(name)) return false;
	}
	else if (!PortCeLayer()) return false;
	if (CE_BootRequested && CE_BootRequested()) return false;
	if (CE_SessionActive && CE_SessionActive()) return false;
	if (PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo ||
		CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode) return false;
	return true;
}

// The ce.ini line, read when this pad was last out of a level (the CE Settings page is only in the front end), not every frame.
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

bool PortCeDodgeBindingAvailable() { return OrdinaryPlay(); }

bool PortCeActionEnabled(CeControlAction action)
{
	if (action == CeControlAction::TakeUnit) return PortCeExperimental("play_unit");
	if (action == CeControlAction::Commander) return PortCeExperimental("commander");
	return CE_SettingGetBool("dodge_button", 1) != 0;
}

const char *PortCeDodgeBindingConflict(int key) { return PortCeActionConflict(CeControlAction::Dodge, key); }

const char *PortCeActionConflict(CeControlAction action, int key)
{
	for (auto other : {CeControlAction::Dodge, CeControlAction::TakeUnit, CeControlAction::Commander}) {
		if (other == action) continue;
		// Disabled optional modes do not occupy their default key.
		if (PortCeActionKey(other) == key && PortCeActionEnabled(other))
			return other == CeControlAction::TakeUnit ? "Take Unit" : other == CeControlAction::Commander ? "Commander view" : "Dodge modifier";
	}
	// Keep Commander navigation and orders usable after a custom assignment.
	if (action == CeControlAction::Commander) {
		switch (key) {
		case DIK_TAB: case DIK_A: case DIK_C: case DIK_W: case DIK_S: case DIK_D:
		case DIK_Q: case DIK_E: case DIK_R: case DIK_F: case DIK_V: case DIK_SPACE:
		case DIK_B: case DIK_H: case DIK_X: case DIK_HOME: case DIK_DELETE:
		case DIK_UP: case DIK_LEFT: case DIK_RIGHT: case DIK_DOWN:
		case DIK_LSHIFT: case DIK_RSHIFT: case DIK_LCONTROL: case DIK_RCONTROL:
			return "Commander controls";
		}
		if (key >= DIK_1 && key <= DIK_0) return "Commander groups";
	}
	if (key == 0x29) return "Console";
	if (!KeyBound(key)) return nullptr;
    for (SControlBinding *b=CONTROL_BINDINGS;b->mId!=-1;++b)
        for (int i=0;i<2;++i) {
            const SControlInput &in=b->mInput[i];
            if(in.mPad==-1 || in.mKey!=key || (in.mType!=KEY_ON && in.mType!=KEY_ONCE && in.mType!=KEY_RELEASE))continue;
            switch(b->mId) {
            case BUTTON_MECH_STRAFE_LEFT: return "Strafe Left";
            case BUTTON_MECH_STRAFE_RIGHT: return "Strafe Right";
            case BUTTON_MECH_FORWARD: return "Move Forward";
            case BUTTON_MECH_BACKWARD: return "Move Backward";
            case BUTTON_MECH_FIRE_GUN_POD: case BUTTON_MECH_CHARGE_GUN_POD: return "Fire";
            case BUTTON_MECH_MORPH: return "Morph";
            case BUTTON_MECH_CHANGE_WEAPON: return "Change Weapon";
            default: break;
            }
        }
    return "another control";
}
void PortCeDodgeStrafeNames(char *left,int leftSize,char *right,int rightSize) {
    const int id[]={BUTTON_MECH_STRAFE_LEFT,BUTTON_MECH_STRAFE_RIGHT};
    char *out[]={left,right}; int cap[]={leftSize,rightSize};
    for(int side=0;side<2;++side) {
        if(!out[side]||cap[side]<2)continue;
        snprintf(out[side],cap[side],"%s",side?"strafe right":"strafe left");
        SControlBinding *b=FindControlBinding(id[side]);
        if(!b)continue;
        for(int i=0;i<2;++i) {
            const auto &in=b->mInput[i];char name[32]{};
            if(in.mPad!=-1 && (in.mType==KEY_ON||in.mType==KEY_ONCE||in.mType==KEY_RELEASE) &&
                PortCeDodgeBindingName(in.mKey,name,sizeof(name))) { snprintf(out[side],cap[side],"%s",name);break; }
        }
    }
}
bool PortCeDodgeAssistKeyFree() { return !PortCeDodgeBindingConflict(PortCeDodgeBindingKey()); }

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

extern "C" void PortCeDodgeAssistRefresh(void)
{
	gSetting = -1;
	for (int i = 0; i < kPads; ++i) gStale[i] = true;
}

int PortCeDodgeAssistKey(int pad)
{
	// Only controller 0 reads the keyboard bindings (CController::DoMappings), so only it has the key; and none while text is typed.
	if (pad != 0 || !gState[0].mActive || LT.GetKeytrap()) return 0;
	return PortCeDodgeAssistKeyFree() ? PortCeDodgeBindingKey() : 0;
}

int PortCeDodgeAssistFilter(CController *controller, int pad, int button, float value)
{
	if ((button != BUTTON_MECH_STRAFE_LEFT && button != BUTTON_MECH_STRAFE_RIGHT &&
	     button != BUTTON_MECH_FORWARD && button != BUTTON_MECH_BACKWARD) || pad < 0 || pad >= kPads) return 0;
	State &s = gState[pad];
	gPad = pad;
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
	const int side = button == BUTTON_MECH_FORWARD ? -2 : button == BUTTON_MECH_BACKWARD ? 2 :
		button == BUTTON_MECH_STRAFE_LEFT ? -1 : 1;
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
	gPad = pad;
	const bool ordinary = OrdinaryPlay();
	CBattleEngine *engine = ordinary ? Driven(controller) : NULL;
	if (engine) OfThisWalker(s, engine);
	if (!engine) gSettingStale = true;								// this pad is out of a level (or its player is waiting to come back): it asks ce.ini again when it next drives
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
		ids[k] = taps[k].mSide == -2 ? BUTTON_MECH_FORWARD : taps[k].mSide == 2 ? BUTTON_MECH_BACKWARD :
			taps[k].mSide < 0 ? BUTTON_MECH_STRAFE_LEFT : BUTTON_MECH_STRAFE_RIGHT;
		hard[k] = taps[k].mHard ? 1 : 0;
	}
	if (count && s.mAssist.Waiting())
	{
		s.mFirstAt = u.mNow;											// the first push of a double tap (again, after a slow frame)
		s.mSide = s.mAssist.Side();
	}
	else if (count)
		PortSessionLog(PORT_SESSION_GAME, false, "dodge button: dodge %s, %d ms between the two pushes", s.mSide == -2 ? "forward" : s.mSide == 2 ? "backward" : s.mSide < 0 ? "left" : "right",
			(int)((u.mNow - s.mFirstAt) * 1000.f + 0.5f));
	return count;
}

#endif
