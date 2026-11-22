// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_multibe.cpp: Playable Multi-Battle-Engine (2v2 Versus & 3-4 BE Skirmish/Co-op), clickable
// Front-End Options rows (Play side & Battle Engines), HUD squad/2v2 banner, and Phase 1 4-slot callbacks.
//
// Activation:
//   - ce.ini `multi_be=3` or `multi_be=4` (off/"2" by default; configurable in Front-End -> Options -> Battle Engines)
//   - ce.ini `play_side=forseti` or `play_side=muspell` (default "forseti"; configurable in Front-End -> Options -> Play side)
//   - Optional showcase helpers: `multi_be_cam=1` (elevated panoramic camera) and `multi_be_hud_slot=0..3`
//
// Strict neutrality & safety:
//   - Gated behind CE_GATE_LAYER / PortCeLayer() and NetworkOrLockstepActive() so faithful replays, recordings,
//     probes, perturbation_sweep.py, and 2P network lockstep sessions are completely untouched.
//   - Phase 1 4-slot safety (patches 0369..0373) allows auxiliary Battle Engines to bind dedicated CPlayer(slot+1)
//     slots (mNumber 2..4) with CE-side P3/P4 career preferences (zero bytes changed in CCareer .bes saves),
//     real combat vulnerability, full explosions, team kill tracking, and 5-second dry-land respawns without
//     ever triggering retail 1P/2P DeclarePlayerDead loss conditions.
#include "ce_hooks.h"
#include "ce_multibe.h"

#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "world.h"
#include "map.h"
#include "player.h"
#include "BattleEngine.h"
#include "BattleEngineWalkerPart.h"
#include "BattleEngineJetPart.h"
#include "Camera.h"
#include "InitThing.h"
#include "ThingType.h"
#include "unit.h"
#include "UnitAI.h"
#include "Hud.h"
#include "hud_scale.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "PauseMenu.h"
#include "MenuItem.h"
#include "MenuOptionsList.h"
#include "DXEngine.h"
#include "CLIParams.h"
#include "ce_photo.h"
#include "ce_cmdmatch.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool PortCeLayer();
extern bool PortCeMenus();
extern bool PortRecording();
extern "C" int CE_SettingGetText(const char *key, char *out, int n);
extern "C" int CE_SettingSetText(const char *key, const char *value);
extern "C" int CE_SettingIs(const char *key, const char *value);
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" int CE_SessionActive(void) __attribute__((weak));
extern "C" int CE_LockstepRequested(void) __attribute__((weak));
extern "C" int CE_LockstepSharing(void) __attribute__((weak));
extern "C" int PortCePlayUnitInvertSides(void) __attribute__((weak));
bool PortCeNetActive() __attribute__((weak));

namespace
{

static constexpr EMemoryType kMultiBeMenuMem = (EMemoryType)0x80;

bool NetworkOrLockstepActive()
{
	return (CE_BootRequested && CE_BootRequested()) ||
	       (CE_SessionActive && CE_SessionActive()) ||
	       (CE_LockstepRequested && CE_LockstepRequested()) ||
	       (CE_LockstepSharing && CE_LockstepSharing()) ||
	       (PortCeNetActive && PortCeNetActive());
}

bool PilotEnvPresent()
{
	static int cached = -1;
	if (cached < 0)
	{
		const char *pilot = getenv("BEA_PORT_PILOT");
		const char *autopilot = getenv("BEA_PORT_AUTOPILOT");
		cached = ((pilot && pilot[0]) || (autopilot && autopilot[0])) ? 1 : 0;
	}
	return cached != 0;
}

bool MultiBeFenced()
{
	if (!PortCeLayer() || PortRecording() || NetworkOrLockstepActive())
		return true;
	if (PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo ||
	    CLIPARAMS.mPlaybackDemo || CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode)
		return true;
	if (PilotEnvPresent())
		return true;
	return false;
}

// Cached ce.ini settings (-1 = uninitialized) so per-frame Front-End / gameplay checks perform zero disk I/O.
// Refreshed on Options build and level begin, and updated synchronously when toggled in Front-End Options.
int gCachedPlaySide = -1;
int gCachedSoloSkirmish = -1;
int gCachedMultiBeChoice = -1;
bool gInLevel = false;
bool gLatchedSoloSkirmish = false;

int LoadPlaySideFromIni()
{
	char buf[32] = {0};
	if (!CE_SettingGetText("play_side", buf, (int)sizeof(buf)))
		return CE_MULTIBE_SIDE_FORSETI;
	return CeMultiBeParsePlaySide(buf);
}

int LoadMultiBeChoiceFromIni()
{
	char buf[32] = {0};
	if (!CE_SettingGetText("multi_be", buf, (int)sizeof(buf)))
		return CE_MULTIBE_CHOICE_STANDARD;
	return CeMultiBeModeFromIni(buf);
}

int LoadSoloSkirmishFromIni()
{
	char buf[32] = {0};
	if (!CE_SettingGetText("solo_skirmish", buf, (int)sizeof(buf)))
		return CE_SOLO_SKIRMISH_SPLITSCREEN;
	return CeMultiBeParseSoloSkirmish(buf);
}

void RefreshCachedSettings()
{
	gCachedPlaySide = LoadPlaySideFromIni();
	gCachedSoloSkirmish = LoadSoloSkirmishFromIni();
	gCachedMultiBeChoice = LoadMultiBeChoiceFromIni();
}

int ReadPlaySideSetting()
{
	if (gCachedPlaySide < 0)
		gCachedPlaySide = LoadPlaySideFromIni();
	return gCachedPlaySide;
}

int ReadMultiBeChoiceSetting()
{
	if (gCachedMultiBeChoice < 0)
		gCachedMultiBeChoice = LoadMultiBeChoiceFromIni();
	return gCachedMultiBeChoice;
}

int ReadSoloSkirmishSetting()
{
	if (gCachedSoloSkirmish < 0)
		gCachedSoloSkirmish = LoadSoloSkirmishFromIni();
	return gCachedSoloSkirmish;
}

// Native clickable Front-End Options rows (auto-fitted by ce_menu_fit at 640x480 & 1280x720).
// Uses (EPCText)78 so CPauseMenu::ButtonPressed ignores it cleanly while CMenuMenuItem handles
// mouse hover/click, dropdown expansion, keyboard arrows/Enter, and gamepad selection.
class CCePlaySideItem : public CMenuMenuItem
{
public:
	CCePlaySideItem() : CMenuMenuItem((EPCText)78, 3, false)
	{
		Refresh();
	}

	WCHAR *GetText() override
	{
		return const_cast<WCHAR *>(L"Play side: ");
	}

	int GetNumChoices() override
	{
		return 2;
	}

	WCHAR *GetChoiceText(int n) override
	{
		return const_cast<WCHAR *>(n == CE_MULTIBE_SIDE_MUSPELL ? L"Muspell" : L"Forseti");
	}

	int GetValue() override
	{
		return ReadPlaySideSetting();
	}

	void SetValue(int value) override
	{
		const int side = (value == CE_MULTIBE_SIDE_MUSPELL) ? CE_MULTIBE_SIDE_MUSPELL : CE_MULTIBE_SIDE_FORSETI;
		gCachedPlaySide = side;
		if (PortCeMenus())
			CE_SettingSetText("play_side", CeMultiBePlaySideIniValue(side));
	}
};

class CCeSoloSkirmishItem : public CMenuMenuItem
{
public:
	CCeSoloSkirmishItem() : CMenuMenuItem((EPCText)78, 3, false)
	{
		Refresh();
	}

	WCHAR *GetText() override
	{
		return const_cast<WCHAR *>(L"Skirmish mode: ");
	}

	int GetNumChoices() override
	{
		return 2;
	}

	WCHAR *GetChoiceText(int n) override
	{
		return const_cast<WCHAR *>(n == CE_SOLO_SKIRMISH_SOLO_AI ? L"Solo vs AI" : L"Splitscreen");
	}

	int GetValue() override
	{
		return ReadSoloSkirmishSetting();
	}

	void SetValue(int value) override
	{
		const int choice = (value == CE_SOLO_SKIRMISH_SOLO_AI)
			? CE_SOLO_SKIRMISH_SOLO_AI : CE_SOLO_SKIRMISH_SPLITSCREEN;
		gCachedSoloSkirmish = choice;
		if (PortCeMenus())
			CE_SettingSetText("solo_skirmish", CeMultiBeSoloSkirmishIniValue(choice));
	}
};

class CCeBattleEnginesItem : public CMenuMenuItem
{
public:
	CCeBattleEnginesItem() : CMenuMenuItem((EPCText)78, 3, false)
	{
		Refresh();
	}

	WCHAR *GetText() override
	{
		return const_cast<WCHAR *>(L"Battle Engines: ");
	}

	int GetNumChoices() override
	{
		return 3;
	}

	WCHAR *GetChoiceText(int n) override
	{
		if (n == CE_MULTIBE_CHOICE_THREE)
			return const_cast<WCHAR *>(L"3 BEs");
		if (n == CE_MULTIBE_CHOICE_FOUR)
			return const_cast<WCHAR *>(L"4 BEs (2v2)");
		return const_cast<WCHAR *>(L"Standard (2)");
	}

	int GetValue() override
	{
		return ReadMultiBeChoiceSetting();
	}

	void SetValue(int value) override
	{
		const int choice = (value >= CE_MULTIBE_CHOICE_STANDARD && value <= CE_MULTIBE_CHOICE_FOUR)
			? value : CE_MULTIBE_CHOICE_STANDARD;
		gCachedMultiBeChoice = choice;
		if (PortCeMenus())
			CE_SettingSetText("multi_be", CeMultiBeModeToIni(choice));
	}
};

struct MultiBeGameAccess : CGame
{
	static CCamera *&CameraSlot(int index) { return (GAME.*(&MultiBeGameAccess::mCurrentCamera))[index]; }
	static BOOL &FreeCameraSlot(int index) { return (GAME.*(&MultiBeGameAccess::mFreeCameraOn))[index]; }
};

class CeMultiBeOverviewCamera : public CCamera
{
public:
	CeMultiBeOverviewCamera() : mPos(0.0f, 0.0f, 0.0f), mOldPos(0.0f, 0.0f, 0.0f),
	                            mOrientation(ID_FMATRIX), mOldOrientation(ID_FMATRIX),
	                            mInitialised(false) {}

	virtual FVector GetPos() { return mPos; }
	virtual FMatrix GetOrientation() { return mOrientation; }
	virtual FVector GetOldPos() { return mOldPos; }
	virtual FMatrix GetOldOrientation() { return mOldOrientation; }
	virtual const float GetZoom() { return 1.0f; }
	virtual bool GetShowHUD() { return true; }

	void Reset() { mInitialised = false; }

	void UpdatePose(const FVector &pos, const FVector &lookAt)
	{
		FVector dir = lookAt - pos;
		FMatrix ori = mOrientation;
		if (dir.MagnitudeSq() > 1e-4f)
			ori = FMatrix(dir.Azimuth(), dir.Elevation(), 0.0f);
		if (!mInitialised)
		{
			mPos = pos;
			mOldPos = pos;
			mOrientation = ori;
			mOldOrientation = ori;
			mInitialised = true;
			return;
		}
		mOldPos = mPos;
		mOldOrientation = mOrientation;
		mPos = mPos + (pos - mPos) * 0.25f;
		mOrientation = ori;
	}

	bool Ready() const { return mInitialised; }

private:
	FVector	mPos;
	FVector	mOldPos;
	FMatrix	mOrientation;
	FMatrix	mOldOrientation;
	bool	mInitialised;
};

CeMultiBeExtraSlotPrefs	gExtraSlotPrefs = {};
CeMultiBeMatchState		gMatchState = {};
CeMultiBeConfig			gConfig = {0, false, -1};
bool					gActive = false;
bool					gSoloSkirmish = false;
bool					gSpawned = false;
bool					gReportedMilestone = false;
int						gLevel = 0;
int						gPlayerSide = CE_MULTIBE_SIDE_FORSETI;
int						gEngineCount = 0;
int						gAuxSpawned = 0;
int						gSimTicks = 0;
int						gNanCount = 0;
int						gTotalShots = 0;
int						gAuxShots = 0;
int						gP2Shots = 0;
bool					gP2Moved = false;
int						gAiTargetedCount = 0;
int						gHudFocusCalls = 0;
int						gUnitSpawnEvents = 0;
int						gUnitDestroyedEvents = 0;

CBattleEngine			*gEngines[CE_MULTIBE_MAX] = {NULL, NULL, NULL, NULL};
bool					gIsAux[CE_MULTIBE_MAX] = {false, false, false, false};
bool					gIsAiControlled[CE_MULTIBE_MAX] = {false, false, false, false};
CPlayer					*gAuxPlayers[CE_MULTIBE_MAX] = {NULL, NULL, NULL, NULL};
CCamera					*gAuxCameras[CE_MULTIBE_MAX] = {NULL, NULL, NULL, NULL};
FVector					gStartPos[CE_MULTIBE_MAX];
float					gStartYaw[CE_MULTIBE_MAX] = {0.0f, 0.0f, 0.0f, 0.0f};
bool					gMoved[CE_MULTIBE_MAX] = {false, false, false, false};
int						gTicksSinceMorph[CE_MULTIBE_MAX] = {0, 0, 0, 0};
int						gTicksSinceWeaponSwitch[CE_MULTIBE_MAX] = {0, 0, 0, 0};
float					gAnchorFwdX = 0.0f;
float					gAnchorFwdY = 1.0f;
CeMultiBeOverviewCamera	gOverviewCam;

int IndexOfEngine(const CUnit *unit)
{
	if (!unit)
		return -1;
	for (int i = 0; i < gEngineCount; i++)
		if (gEngines[i] == unit)
			return i;
	return -1;
}

bool UnitExistsInWorld(const CUnit *needle)
{
	if (!needle)
		return false;
	// Stack-local iterator: strictly read-only, never mutates WORLD.GetUnitNB()'s shared intrusive mIterator.
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
		if (u == needle)
			return true;
	return false;
}

int CountLiveEngines()
{
	int live = 0;
	for (int i = 0; i < gEngineCount; i++)
		if (gEngines[i] && UnitExistsInWorld(gEngines[i]) && !gEngines[i]->IsDying())
			live++;
	return live;
}

int CountMovedEngines()
{
	int moved = 0;
	for (int i = 0; i < gEngineCount; i++)
		if (gMoved[i])
			moved++;
	return moved;
}

void RefreshMatchStateCounts()
{
	gMatchState.forsetiAlive = 0;
	gMatchState.muspellAlive = 0;
	gMatchState.forsetiTotal = 0;
	gMatchState.muspellTotal = 0;
	gMatchState.walkerCount = 0;
	gMatchState.jetCount = 0;

	for (int i = 0; i < gEngineCount; i++)
	{
		if (gMatchState.slotTeam[i] == CE_MULTIBE_SIDE_MUSPELL)
			gMatchState.muspellTotal++;
		else
			gMatchState.forsetiTotal++;

		CBattleEngine *be = gEngines[i];
		if (be && UnitExistsInWorld(be) && !be->IsDying())
		{
			if (gMatchState.slotTeam[i] == CE_MULTIBE_SIDE_MUSPELL)
				gMatchState.muspellAlive++;
			else
				gMatchState.forsetiAlive++;

			const EBattleEngineState st = be->GetState();
			if (st == BATTLE_ENGINE_STATE_JET || st == BATTLE_ENGINE_STATE_MORPHING_INTO_JET)
				gMatchState.jetCount++;
			else
				gMatchState.walkerCount++;
		}
	}
}

void EmitTelemetry(const char *phase)
{
	RefreshMatchStateCounts();
	fprintf(stderr,
	        "ce-multibe: phase=%s level=%d target=%d active=%d spawned=%d ticks=%d nan=%d shots=%d aux_shots=%d ai_targeted=%d hud_focus=%d moved=%d morphs=%d respawns=%d forseti_kills=%d muspell_kills=%d side=%s solo=%d onescreen=%d viewpoints=%d p2_shots=%d p2_moved=%d spawns=%d destroyed=%d\n",
	        phase, gLevel, gConfig.target_count, CountLiveEngines(), gAuxSpawned,
	        gSimTicks, gNanCount, gTotalShots, gAuxShots, gAiTargetedCount,
	        gHudFocusCalls, CountMovedEngines(), gMatchState.totalMorphs, gMatchState.totalRespawns,
	        gMatchState.forsetiKills, gMatchState.muspellKills,
	        CeMultiBePlaySideIniValue(gPlayerSide), gSoloSkirmish ? 1 : 0,
	        PortCeOneScreenMatch() ? 1 : 0, ENGINE.GetNumViewpoints(),
	        gP2Shots, gP2Moved ? 1 : 0, gUnitSpawnEvents, gUnitDestroyedEvents);
	fflush(stderr);
}

void ResetState()
{
	for (int i = 0; i < CE_MULTIBE_MAX; i++)
	{
		if (gAuxCameras[i])
		{
			delete gAuxCameras[i];
			gAuxCameras[i] = NULL;
		}
		if (gAuxPlayers[i])
		{
			delete gAuxPlayers[i];
			gAuxPlayers[i] = NULL;
		}
		gEngines[i] = NULL;
		gIsAux[i] = false;
		gIsAiControlled[i] = false;
		gMoved[i] = false;
		gStartYaw[i] = 0.0f;
		gTicksSinceMorph[i] = 0;
		gTicksSinceWeaponSwitch[i] = 0;
	}
	gMatchState.Reset();
	gActive = false;
	gSoloSkirmish = false;
	gSpawned = false;
	gReportedMilestone = false;
	gLevel = 0;
	gPlayerSide = CE_MULTIBE_SIDE_FORSETI;
	gEngineCount = 0;
	gAuxSpawned = 0;
	gSimTicks = 0;
	gNanCount = 0;
	gTotalShots = 0;
	gAuxShots = 0;
	gP2Shots = 0;
	gP2Moved = false;
	gAiTargetedCount = 0;
	gHudFocusCalls = 0;
	gUnitSpawnEvents = 0;
	gUnitDestroyedEvents = 0;
	gOverviewCam.Reset();
}

bool FindDrySpawnPosition(const FVector &anchorPos, const FVector &right, const FVector &fwd,
                          float offRight, float offFwd, FVector *outPos)
{
	if (!outPos)
		return false;
	const float waterZ = MAP.GetWaterLevel();
	const float dryMargin = 0.8f;

	FVector spawnPos = anchorPos + right * offRight + fwd * offFwd;
	float groundZ = MAP.Collide(spawnPos);
	if (CeMultiBeIsAboveWater(groundZ, waterZ, dryMargin))
	{
		spawnPos.Z = groundZ - 0.5f;
		if (CeMultiBeIsFinitePos(spawnPos.X, spawnPos.Y, spawnPos.Z))
		{
			*outPos = spawnPos;
			return true;
		}
	}

	const float scales[4] = { 0.70f, 0.50f, 0.35f, 0.20f };
	const float angles[8] = { 0.0f, 0.7854f, -0.7854f, 1.5708f, -1.5708f, 2.3562f, -2.3562f, 3.14159f };
	for (int si = 0; si < 4; si++)
	{
		for (int ai = 0; ai < 8; ai++)
		{
			const float ca = cosf(angles[ai]), sa = sinf(angles[ai]);
			const float r = (offRight * ca - offFwd * sa) * scales[si];
			const float f = (offRight * sa + offFwd * ca) * scales[si];
			FVector cand = anchorPos + right * r + fwd * f;
			const float candGround = MAP.Collide(cand);
			if (CeMultiBeIsAboveWater(candGround, waterZ, dryMargin))
			{
				cand.Z = candGround - 0.5f;
				if (CeMultiBeIsFinitePos(cand.X, cand.Y, cand.Z))
				{
					*outPos = cand;
					return true;
				}
			}
		}
	}
	return false;
}

// Creates a dedicated CCamera for an auxiliary BE without disturbing or double-owning GAME.mCurrentCamera[slot].
CCamera *CreateAuxCameraForSlot(CPlayer *auxPlayer, int camSlot)
{
	if (!auxPlayer || camSlot < 0 || camSlot >= MAX_PLAYERS)
		return NULL;
	CCamera *savedCam = MultiBeGameAccess::CameraSlot(camSlot);
	const BOOL savedFreeCam = MultiBeGameAccess::FreeCameraSlot(camSlot);
	MultiBeGameAccess::FreeCameraSlot(camSlot) = FALSE;
	MultiBeGameAccess::CameraSlot(camSlot) = NULL;
	auxPlayer->Goto3rdPersonView();
	CCamera *auxCam = MultiBeGameAccess::CameraSlot(camSlot);
	MultiBeGameAccess::CameraSlot(camSlot) = savedCam;
	MultiBeGameAccess::FreeCameraSlot(camSlot) = savedFreeCam;
	return auxCam;
}

CUnit *FindNearestHostile(CBattleEngine *self)
{
	if (!self)
		return NULL;
	const FVector myPos = self->GetPos();
	CUnit *best = NULL;
	float bestScoreSq = 640.0f * 640.0f;
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
	{
		if (!u || u == self || u->IsDying() || !u->Active())
			continue;
		if (!self->IsTargetAlligence(u->GetAllegiance()))
			continue;
		const FVector delta = u->GetPos() - myPos;
		const float dSq = delta.MagnitudeSq();
		if (dSq <= 4.0f)
			continue;
		// Slightly prioritize opposing Battle Engines so 2v2 duels stay lively and engaging.
		const float weight = u->IsA(THING_TYPE_BATTLE_ENGINE) ? 0.55f : 1.0f;
		const float scoreSq = dSq * weight;
		if (scoreSq < bestScoreSq)
		{
			bestScoreSq = scoreSq;
			best = u;
		}
	}
	return best;
}

void UpdateOverviewCamera()
{
	if (!gConfig.panoramic_cam || gEngineCount <= 0)
		return;
	const bool hasAnchor = gEngines[0] && UnitExistsInWorld(gEngines[0]);
	const FVector anchorPos = hasAnchor ? gEngines[0]->GetPos() : FVector(0.0f, 0.0f, 0.0f);
	float positions[CE_MULTIBE_MAX][3];
	int count = 0;
	for (int i = 0; i < gEngineCount; i++)
	{
		if (!gEngines[i] || !UnitExistsInWorld(gEngines[i]) || gEngines[i]->IsDying())
			continue;
		const FVector p = gEngines[i]->GetPos();
		// Keep panoramic framing centered on the active skirmish cluster around slot 0.
		if (hasAnchor && i > 0 && (p - anchorPos).MagnitudeSq() > 75.0f * 75.0f)
			continue;
		positions[count][0] = p.X;
		positions[count][1] = p.Y;
		positions[count][2] = p.Z;
		count++;
	}
	if (count == 0)
		return;
	float camArr[3], lookArr[3];
	if (!CeMultiBeComputeOverviewCam(positions, count, gAnchorFwdX, gAnchorFwdY, 28.0f, 9.5f, camArr, lookArr))
		return;
	FVector camPos(camArr[0], camArr[1], camArr[2]);
	const float groundAtCam = MAP.Collide(camPos);
	if (camPos.Z > groundAtCam - 4.0f)
		camPos.Z = groundAtCam - 4.0f;
	gOverviewCam.UpdatePose(camPos, FVector(lookArr[0], lookArr[1], lookArr[2]));
}

bool SpawnAuxEngineInSlot(int idx, const FVector &spawnPos, float yaw, EAllegiance side, bool startAsJet = false)
{
	if (idx < 0 || idx >= CE_MULTIBE_MAX)
		return false;

	CBattleEngine *be = (CBattleEngine *)SpawnThing(OID_CBattleEngine);
	if (!be)
		return false;

	const int playerNum = idx + 1;	// 1-based slot 2..4 (safe under Phase 1 MAX_PLAYERS=4 patches)
	CBattleEngineInitThing init;
	init.mPos = spawnPos;
	init.mYaw = yaw;
	init.mPitch = 0.0f;
	init.mRoll = 0.0f;
	init.mAllegiance = side;
	init.mConfigurationId = CeMultiBeSelectConfigId(
		playerNum,
		GAME.GetFrontEndSettings()->mPlayer1Configuration,
		GAME.GetFrontEndSettings()->mPlayer2Configuration);
	init.mPlaneMode = startAsJet ? TRUE : FALSE;
	be->Init(&init);

	if (!gAuxPlayers[idx])
		gAuxPlayers[idx] = new(MEMTYPE_PLAYER) CPlayer(playerNum);
	gAuxPlayers[idx]->AssignBattleEngine(be);
	// Real combat vulnerability: CBattleEngine::StartDieProcess() routes through PortCeMultiBeHandleDeath()
	// so auxiliary BE deaths play full explosions, award team kills, and trigger a clean 5-second respawn.
	be->SetVulnerable(TRUE);

	if (gAuxCameras[idx])
	{
		delete gAuxCameras[idx];
		gAuxCameras[idx] = NULL;
	}
	gAuxCameras[idx] = CreateAuxCameraForSlot(gAuxPlayers[idx], idx);

	gEngines[idx] = be;
	gIsAux[idx] = true;
	gIsAiControlled[idx] = true;
	gStartPos[idx] = be->GetPos();
	gStartYaw[idx] = yaw;
	gTicksSinceMorph[idx] = 0;
	gTicksSinceWeaponSwitch[idx] = 0;
	gMatchState.RegisterSlotTeam(idx, side == kMuspellAllegiance ? CE_MULTIBE_SIDE_MUSPELL : CE_MULTIBE_SIDE_FORSETI);
	return true;
}

void SpawnAuxiliaryBattleEngines()
{
	CPlayer *p0 = GAME.GetPlayer(0);
	CBattleEngine *anchor = p0 ? p0->GetBattleEngine() : NULL;
	if (!anchor || anchor->IsDying())
		return;

	if (gConfig.panoramic_cam && p0->GetIsInFPV())
		p0->Goto3rdPersonView();

	// Register existing real-player Battle Engines first.
	const int nPlayers = GAME.GetNPlayers();
	int forsetiCount = 0;
	int muspellCount = 0;
	for (int i = 0; i < nPlayers && i < MAX_PLAYERS && gEngineCount < gConfig.target_count; i++)
	{
		CPlayer *pl = GAME.GetPlayer(i);
		CBattleEngine *be = pl ? pl->GetBattleEngine() : NULL;
		if (!be && !(gSoloSkirmish && i == 1))
			continue;
		const int slot = gEngineCount++;
		const bool soloP2 = (gSoloSkirmish && slot == 1);
		gEngines[slot] = be;
		gIsAux[slot] = false;
		gIsAiControlled[slot] = soloP2;
		if (be)
		{
			const int team = (be->GetAllegiance() == kMuspellAllegiance) ? CE_MULTIBE_SIDE_MUSPELL : CE_MULTIBE_SIDE_FORSETI;
			gStartPos[slot] = be->GetPos();
			gStartYaw[slot] = be->GetCurrentEulerOrientation().mYaw;
			gMatchState.RegisterSlotTeam(slot, team);
			if (team == CE_MULTIBE_SIDE_MUSPELL)
				muspellCount++;
			else
				forsetiCount++;
			// Ensure Solo Skirmish AI-piloted P2 owns a live first-person aiming camera tracking its Battle Engine.
			if (soloP2 && pl && (!pl->GetIsInFPV() || !GAME.GetCurrentCamera(1)))
				pl->GotoFPView();
		}
		else
		{
			const int fallbackTeam = (gPlayerSide == CE_MULTIBE_SIDE_MUSPELL) ? CE_MULTIBE_SIDE_FORSETI : CE_MULTIBE_SIDE_MUSPELL;
			gMatchState.RegisterSlotTeam(slot, fallbackTeam);
			if (fallbackTeam == CE_MULTIBE_SIDE_MUSPELL)
				muspellCount++;
			else
				forsetiCount++;
		}
	}

	gPlayerSide = (anchor->GetAllegiance() == kMuspellAllegiance) ? CE_MULTIBE_SIDE_MUSPELL : CE_MULTIBE_SIDE_FORSETI;

	const FVector anchorPos = anchor->GetPos();
	const FMatrix anchorOri = anchor->GetOrientation();
	FVector fwd = anchorOri * FVector(0.0f, 1.0f, 0.0f);
	fwd.Z = 0.0f;
	if (fwd.MagnitudeSq() < 1e-4f)
		fwd = FVector(0.0f, 1.0f, 0.0f);
	else
		fwd.Normalise();
	gAnchorFwdX = fwd.X;
	gAnchorFwdY = fwd.Y;
	FVector right(fwd.Y, -fwd.X, 0.0f);
	const float anchorYaw = anchor->GetCurrentEulerOrientation().mYaw;
	const bool mpLevel = GAME.IsMultiplayer() != FALSE;

	// Spawn auxiliary Battle Engines up to target_count (3 or 4), strictly on dry terrain above water level.
	int formationSlot = 1;
	while (gEngineCount < gConfig.target_count && gEngineCount < CE_MULTIBE_MAX)
	{
		// On 2v2 Versus (levels 850..899), keep teams balanced (2 Forseti vs 2 Muspell) regardless of Play side.
		// On Campaign/Co-op levels (100..799), spawn allied Forseti BEs (f_be* mesh is guaranteed loaded).
		EAllegiance side = kForsetiAllegiance;
		if (mpLevel)
		{
			side = (forsetiCount <= muspellCount) ? kForsetiAllegiance : kMuspellAllegiance;
			if (side == kMuspellAllegiance)
				muspellCount++;
			else
				forsetiCount++;
		}
		else
		{
			forsetiCount++;
		}

		// Pick team-appropriate anchor:
		// - Allied wingmen deploy beside P1 (slot 0).
		// - In normal Versus play, opposing AI BEs deploy beside their own team's anchor (slot 1) and fly in.
		// - In panoramic showcase mode, deploy opposing AI BEs on the skirmish perimeter ahead of P1 for clear combat framing.
		FVector baseAnchor = anchorPos;
		FVector baseRight = right;
		FVector baseFwd = fwd;
		float spawnYaw = anchorYaw;
		float offRight = 0.0f, offFwd = 0.0f;

		const bool sameSideAsP0 = ((side == kMuspellAllegiance) == (gPlayerSide == CE_MULTIBE_SIDE_MUSPELL));
		if (mpLevel && !sameSideAsP0 && !gConfig.panoramic_cam && gEngineCount >= 2 && gEngines[1] && UnitExistsInWorld(gEngines[1]))
		{
			baseAnchor = gEngines[1]->GetPos();
			const FMatrix p1Ori = gEngines[1]->GetOrientation();
			baseFwd = p1Ori * FVector(0.0f, 1.0f, 0.0f);
			baseFwd.Z = 0.0f;
			if (baseFwd.MagnitudeSq() < 1e-4f)
				baseFwd = fwd * -1.0f;
			else
				baseFwd.Normalise();
			baseRight = FVector(baseFwd.Y, -baseFwd.X, 0.0f);
			spawnYaw = gEngines[1]->GetCurrentEulerOrientation().mYaw;
			offRight = 12.0f;
			offFwd = 6.0f;
		}
		else if (mpLevel && !sameSideAsP0 && gConfig.panoramic_cam)
		{
			// Skirmish vanguard ahead of P1 facing P1's team so 2v2 firefights happen immediately in frame.
			offRight = (formationSlot & 1) ? 10.0f : -10.0f;
			offFwd = 22.0f;
			spawnYaw = CeMultiBeWrapAngle(anchorYaw + 3.14159265f);
		}
		else
		{
			CeMultiBeFormationOffset(formationSlot, gConfig.target_count, 11.5f, &offRight, &offFwd);
		}
		formationSlot++;

		FVector spawnPos;
		bool startAsJet = false;
		if (!FindDrySpawnPosition(baseAnchor, baseRight, baseFwd, offRight, offFwd, &spawnPos))
		{
			// Oceanic / airborne maps (e.g. Level 860) start Battle Engines in Jet mode over water:
			// spawn auxiliary BEs safely airborne in Jet mode well above water and terrain.
			const float waterZ = MAP.GetWaterLevel();
			spawnPos = baseAnchor + baseRight * offRight + baseFwd * offFwd;
			const float groundZ = MAP.Collide(spawnPos);
			const float surfaceZ = (groundZ < waterZ) ? groundZ : waterZ;
			spawnPos.Z = (baseAnchor.Z < surfaceZ - 18.0f) ? baseAnchor.Z : (surfaceZ - 22.0f);
			if (!CeMultiBeIsFinitePos(spawnPos.X, spawnPos.Y, spawnPos.Z))
				break;
			startAsJet = true;
		}

		const int idx = gEngineCount;
		if (!SpawnAuxEngineInSlot(idx, spawnPos, spawnYaw, side, startAsJet))
			break;
		gEngineCount++;
		gAuxSpawned++;
	}

	gSpawned = true;
	RefreshMatchStateCounts();
	UpdateOverviewCamera();
	EmitTelemetry("spawn");
}

void RespawnAuxiliaryBattleEngine(int idx)
{
	if (idx < 0 || idx >= gEngineCount || !gIsAux[idx])
		return;
	if (gEngines[idx] && UnitExistsInWorld(gEngines[idx]) && !gEngines[idx]->IsDying())
		return;

	const EAllegiance side = (gMatchState.slotTeam[idx] == CE_MULTIBE_SIDE_MUSPELL)
		? kMuspellAllegiance : kForsetiAllegiance;
	FVector spawnPos = gStartPos[idx];
	FVector fwd(gAnchorFwdX, gAnchorFwdY, 0.0f);
	if (fwd.MagnitudeSq() < 1e-4f)
		fwd = FVector(0.0f, 1.0f, 0.0f);
	else
		fwd.Normalise();
	FVector right(fwd.Y, -fwd.X, 0.0f);
	bool startAsJet = false;
	if (!FindDrySpawnPosition(gStartPos[idx], right, fwd, 0.0f, 0.0f, &spawnPos))
	{
		const float waterZ = MAP.GetWaterLevel();
		spawnPos = gStartPos[idx];
		const float groundZ = MAP.Collide(spawnPos);
		const float surfaceZ = (groundZ < waterZ) ? groundZ : waterZ;
		spawnPos.Z = surfaceZ - 22.0f;
		if (!CeMultiBeIsFinitePos(spawnPos.X, spawnPos.Y, spawnPos.Z))
			return;
		startAsJet = true;
	}

	if (SpawnAuxEngineInSlot(idx, spawnPos, gStartYaw[idx], side, startAsJet))
		gMatchState.totalRespawns++;
}

void SyncSoloP2Engine()
{
	if (!gSoloSkirmish || gEngineCount < 2)
		return;
	CPlayer *p1 = GAME.GetPlayer(1);
	CBattleEngine *live = p1 ? p1->GetBattleEngine() : NULL;
	if (live && UnitExistsInWorld(live) && !live->IsDying())
	{
		if (gEngines[1] != live)
		{
			gEngines[1] = live;
			gIsAux[1] = false;
			gIsAiControlled[1] = true;
			gStartPos[1] = live->GetPos();
			gStartYaw[1] = live->GetCurrentEulerOrientation().mYaw;
			gTicksSinceMorph[1] = 0;
			gTicksSinceWeaponSwitch[1] = 0;
			const int team = (live->GetAllegiance() == kMuspellAllegiance)
				? CE_MULTIBE_SIDE_MUSPELL : CE_MULTIBE_SIDE_FORSETI;
			gMatchState.RegisterSlotTeam(1, team);
		}
		if (!p1->GetIsInFPV() || !GAME.GetCurrentCamera(1))
			p1->GotoFPView();
	}
	else
	{
		gEngines[1] = NULL;
	}
}

void FireAiWeapon(int idx, CBattleEngine *be)
{
	if (!be || idx < 0 || idx >= MAX_PLAYERS)
		return;
	if (gIsAux[idx])
	{
		if (!gAuxCameras[idx])
			return;
		CCamera *savedCam = MultiBeGameAccess::CameraSlot(idx);
		MultiBeGameAccess::CameraSlot(idx) = gAuxCameras[idx];
		be->FireWeapon();
		MultiBeGameAccess::CameraSlot(idx) = savedCam;
		return;
	}
	// Real Player 2 slot in Solo Skirmish: GAME owns mCurrentCamera[1] (CThingCamera via GotoFPView),
	// so never stash or delete it in gAuxCameras[1].
	if (idx == 1 && gSoloSkirmish)
	{
		CPlayer *p1 = GAME.GetPlayer(1);
		if (!p1 || p1->GetBattleEngine() != be)
			return;
		if (!p1->GetIsInFPV() || !GAME.GetCurrentCamera(1))
			p1->GotoFPView();
		if (!GAME.GetCurrentCamera(1))
			return;
		be->FireWeapon();
	}
}

void DriveAiEngine(int idx)
{
	CBattleEngine *be = gEngines[idx];
	if (!be || !UnitExistsInWorld(be) || be->IsDying())
		return;

	gTicksSinceMorph[idx]++;
	gTicksSinceWeaponSwitch[idx]++;

	const EBattleEngineState state = be->GetState();
	if (state == BATTLE_ENGINE_STATE_MORPHING_INTO_JET || state == BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER)
		return;

	const float waterZ = MAP.GetWaterLevel();
	const float waterMargin = 0.8f;
	const FVector myPos = be->GetPos();
	const float groundZ = MAP.Collide(myPos);
	const bool groundIsDry = CeMultiBeIsAboveWater(groundZ, waterZ, 1.1f);
	const bool nearWater = !CeMultiBeIsAboveWater(groundZ, waterZ, waterMargin) ||
	                       !CeMultiBeIsAboveWater(myPos.Z, waterZ, waterMargin);

	CUnit *target = FindNearestHostile(be);
	FVector goalPos = target ? target->GetPos() : gStartPos[idx];

	// Keep active Battle Engines well inside BEA's 512x512 map bounds (avoid boundary drag traps).
	const bool outOfBounds = (myPos.X < 36.0f || myPos.X > 476.0f || myPos.Y < 36.0f || myPos.Y > 476.0f);
	if (outOfBounds)
		goalPos = FVector(256.0f, 256.0f, groundZ - 20.0f);

	const FVector toGoal = goalPos - myPos;
	const float horizDist = sqrtf(toGoal.X * toGoal.X + toGoal.Y * toGoal.Y);
	const float targetGroundZ = target ? MAP.Collide(target->GetPos()) : groundZ;
	const float targetAltAboveGround = target ? (targetGroundZ - target->GetPos().Z) : 0.0f;

	const float maxEnergy = be->GetMaxEnergy();
	const float energyRatio = (maxEnergy > 0.01f) ? CeMultiBeClamp(be->GetEnergy() / maxEnergy, 0.0f, 1.0f) : 1.0f;

	// Combined-arms Walker <-> Jet morphing with strict never-land-in-water invariant.
	CeMultiBeMorphInput morphIn;
	morphIn.isJet = (state == BATTLE_ENGINE_STATE_JET);
	morphIn.flightAllowed = true;
	morphIn.groundIsDry = groundIsDry;
	morphIn.nearWaterHazard = nearWater;
	morphIn.energyRatio = energyRatio;
	morphIn.targetDist = target ? horizDist : -1.0f;
	morphIn.targetAltitude = targetAltAboveGround;
	morphIn.ticksSinceMorph = gTicksSinceMorph[idx];
	morphIn.slotIndex = idx;
	morphIn.simTicks = gSimTicks;

	if (CeMultiBeShouldMorph(morphIn))
	{
		const EBattleEngineState before = be->GetState();
		be->Morph();
		if (be->GetState() != before)
		{
			gTicksSinceMorph[idx] = 0;
			gMatchState.totalMorphs++;
			return;
		}
	}

	// Periodically cycle primary/secondary weapons so AI wingmen & opponents use their full loadout.
	if (gTicksSinceWeaponSwitch[idx] >= 150)
	{
		be->ChangeWeapon();
		gTicksSinceWeaponSwitch[idx] = 0;
	}

	if (state == BATTLE_ENGINE_STATE_JET)
	{
		CBattleEngineJetPart *jet = be->GetJetPart();
		if (!jet)
			return;
		const CEulerAngles euler = be->GetCurrentEulerOrientation();
		const CeMultiBeJetCommand cmd = CeMultiBeComputeJetControl(
			euler.mYaw, euler.mPitch, myPos.Z, groundZ, waterZ,
			toGoal.X, toGoal.Y, toGoal.Z, target != NULL && !outOfBounds, 24.0f);

		jet->Turn(cmd.turn);
		// CBattleEngineJetPart::Pitch(vy) subtracts vy/117 from mPitchvel; positive cmd.pitch means nose UP (+mPitch).
		jet->Pitch(-cmd.pitch);
		// CBattleEngineJetPart::Thrust(vy) sets mThrusterValue = 0.5 - vy/2; clamp away from <-0.9/>0.8 loop trigger.
		const float thrustVy = CeMultiBeClamp(1.0f - 2.0f * cmd.thrust, -0.82f, 0.45f);
		jet->Thrust(thrustVy);

		if (cmd.fire && (gSimTicks + idx * 5) % 8 == 0)
			FireAiWeapon(idx, be);
		return;
	}

	// Walker mode combat & formation maneuvering.
	CBattleEngineWalkerPart *walker = be->GetWalkerPart();
	if (!walker)
		return;

	const FVector toHome = gStartPos[idx] - myPos;
	const float homeDistSq = toHome.X * toHome.X + toHome.Y * toHome.Y;
	const float maxWander = gConfig.panoramic_cam ? 26.0f : 140.0f;
	const bool returnHome = nearWater || outOfBounds || (!target && homeDistSq > maxWander * maxWander);

	if (returnHome && homeDistSq > 1.0f)
	{
		const FVector steer = outOfBounds ? toGoal : toHome;
		const float desiredYaw = steer.Azimuth();
		const float curYaw = be->GetCurrentEulerOrientation().mYaw;
		const float diff = CeMultiBeWrapAngle(desiredYaw - curYaw);
		if (fabsf(diff) > 0.08f)
			walker->Rotate(diff > 0.0f ? -0.75f : 0.75f);
		if (fabsf(diff) < 0.85f)
			walker->Forward(0.50f);
	}
	else
	{
		if (target && horizDist > 1.0f)
		{
			const CEulerAngles curEuler = be->GetCurrentEulerOrientation();
			const float desiredYaw = toGoal.Azimuth();
			const float diff = CeMultiBeWrapAngle(desiredYaw - curEuler.mYaw);
			if (fabsf(diff) > 0.07f)
				walker->Rotate(diff > 0.0f ? -0.68f : 0.68f);
			const float pitchInput = CeMultiBeComputeWalkerPitchInput(curEuler.mPitch, toGoal.Elevation());
			if (pitchInput != 0.0f)
				walker->Pitch(pitchInput);
		}
		else
		{
			walker->Rotate(((gSimTicks / 45 + idx) & 1) ? 0.18f : -0.18f);
		}

		// Probe terrain 8m ahead in the intended movement direction so walkers stay strictly on dry ground.
		const FMatrix ori = be->GetOrientation();
		FVector fwd = ori * FVector(0.0f, 1.0f, 0.0f);
		FVector right = ori * FVector(1.0f, 0.0f, 0.0f);
		fwd.Z = 0.0f;
		right.Z = 0.0f;
		if (fwd.MagnitudeSq() > 1e-4f) fwd.Normalise();
		if (right.MagnitudeSq() > 1e-4f) right.Normalise();

		const int phase = (gSimTicks + idx * 17) % 120;
		FVector stepDir(0.0f, 0.0f, 0.0f);
		int moveKind = 0;	// 0=fwd, 1=left, 2=right, 3=back
		if (target && horizDist > 42.0f)
		{
			stepDir = fwd;
			moveKind = 0;
		}
		else if (phase < 40)
		{
			stepDir = fwd;
			moveKind = 0;
		}
		else if (phase < 70)
		{
			stepDir = right * -1.0f;
			moveKind = 1;
		}
		else if (phase < 100)
		{
			stepDir = right;
			moveKind = 2;
		}
		else
		{
			stepDir = fwd * -1.0f;
			moveKind = 3;
		}

		const FVector probe = myPos + stepDir * 8.0f;
		if (CeMultiBeIsAboveWater(MAP.Collide(probe), waterZ, waterMargin))
		{
			if (moveKind == 0) walker->Forward(0.45f);
			else if (moveKind == 1) walker->StrafeLeft(0.32f);
			else if (moveKind == 2) walker->StrafeRight(0.32f);
			else walker->Backward(0.28f);
		}
	}

	if ((gSimTicks + idx * 7) % 12 == 0)
		FireAiWeapon(idx, be);
}

void ProbeAiTargeting()
{
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
	{
		if (!u || u->IsDying())
			continue;
		CUnitAI *ai = u->GetAI();
		if (!ai)
			continue;
		CUnit *tgt = ai->mTarget.ToRead();
		const int hitIdx = IndexOfEngine(tgt);
		if (hitIdx >= 0 && gIsAux[hitIdx])
			gAiTargetedCount++;
	}
}

void CopyDrawableWide(WCHAR *out, size_t cap, const char *ascii, CDXBitmapFont *font)
{
	if (!out || cap == 0)
		return;
	size_t n = 0;
	for (; ascii && ascii[n] && n + 1 < cap; ++n)
	{
		const WCHAR ch = static_cast<WCHAR>(static_cast<unsigned char>(ascii[n]));
		out[n] = (font && font->CanDisplay(ch)) ? ch : L'-';
	}
	out[n] = 0;
}

} // namespace

// ---- Hook handlers (registered in port/game/ce_hooks.cpp) ----

extern "C" void PortCeMultiBeLevelBegin(int level)
{
	ResetState();
	gInLevel = true;
	gLatchedSoloSkirmish = false;
	// Never activate inside an online network/lockstep session, recording, demo, attract, or pilot run.
	if (MultiBeFenced())
		return;
	RefreshCachedSettings();
	char buf[32] = {0};
	const int count = CE_SettingGetText("multi_be", buf, (int)sizeof(buf)) ? CeMultiBeParseCount(buf) : 0;
	const bool solo = (ReadSoloSkirmishSetting() == CE_SOLO_SKIRMISH_SOLO_AI);
	const bool soloThisLevel = solo && (level >= 850 && level <= 899);
	gLatchedSoloSkirmish = soloThisLevel;
	const int effectiveTarget = CeMultiBeEffectiveTargetCount(count, solo, level);
	if (effectiveTarget <= 0)
		return;
	gConfig.target_count = effectiveTarget;
	gConfig.panoramic_cam = CE_SettingIs("multi_be_cam", "1") != 0;
	char hudBuf[16] = {0};
	gConfig.hud_focus_slot = CE_SettingGetText("multi_be_hud_slot", hudBuf, (int)sizeof(hudBuf))
		? CeMultiBeParseHudSlot(hudBuf)
		: -1;
	gLevel = level;
	gPlayerSide = ReadPlaySideSetting();
	gSoloSkirmish = soloThisLevel;
	gActive = true;
	// Enable single-screen fullscreen immediately (before opening pan renders), even when commander=0.
	if (gSoloSkirmish)
		CeCmdMatchUpdate();
}

extern "C" void PortCeMultiBeLevelEnd(int, int)
{
	gInLevel = false;
	gLatchedSoloSkirmish = false;
	if (!gActive)
		return;
	if (gSoloSkirmish)
		CeCmdMatchLevelEnd();
	EmitTelemetry("end");
	ResetState();
}

extern "C" void PortCeMultiBeTickPre()
{
	if (!gActive)
		return;

	if (gSoloSkirmish && !PortCeOneScreenMatch())
		CeCmdMatchUpdate();

	const EGameState state = GAME.GetGameState();
	if (state == GAME_STATE_PANNING && gConfig.panoramic_cam)
	{
		bool ready = false;
		const int nPlayers = GAME.GetNPlayers();
		for (int i = 0; i < nPlayers && i < MAX_PLAYERS; i++)
		{
			CPlayer *pl = GAME.GetPlayer(i);
			if (pl && pl->GetBattleEngine())
			{
				if (i == 0)
					pl->Goto3rdPersonView();
				else
					pl->GotoControlView();
				ready = true;
			}
		}
		if (ready)
			GAME.StartPlayingState();
	}
	if (GAME.GetGameState() != GAME_STATE_PLAYING)
		return;

	if (!gSpawned)
	{
		SpawnAuxiliaryBattleEngines();
		if (!gSpawned)
			return;
	}

	// Keep Solo Skirmish Player 2 pointer synchronized across retail deaths & respawns (never dangle!).
	if (gSoloSkirmish)
		SyncSoloP2Engine();

	gSimTicks++;

	// Advance auxiliary BE respawn timers and redeploy any slot whose 5-second cooldown finished.
	const uint8_t readyMask = gMatchState.TickRespawnTimers();
	if (readyMask)
	{
		for (int i = 0; i < gEngineCount; i++)
			if ((readyMask & (uint8_t)(1u << i)) && gIsAux[i])
				RespawnAuxiliaryBattleEngine(i);
	}

	// Validate positions & drive AI-controlled Battle Engines (Solo Skirmish P2 + auxiliary BEs).
	for (int i = 0; i < gEngineCount; i++)
	{
		CBattleEngine *be = gEngines[i];
		if (!be || !UnitExistsInWorld(be))
			continue;
		const FVector pos = be->GetPos();
		if (!CeMultiBeIsFinitePos(pos.X, pos.Y, pos.Z))
			gNanCount++;
		else if ((pos - gStartPos[i]).MagnitudeSq() > 0.25f)
		{
			gMoved[i] = true;
			if (i == 1)
				gP2Moved = true;
		}

		if (gIsAiControlled[i])
			DriveAiEngine(i);
	}

	if (gSimTicks % 10 == 0)
		ProbeAiTargeting();

	RefreshMatchStateCounts();
	UpdateOverviewCamera();

	if (!gReportedMilestone && gSimTicks >= 200)
	{
		gReportedMilestone = true;
		EmitTelemetry("tick200");
	}
}

extern "C" CCamera *PortCeMultiBeRenderCamera(CCamera *camera, int viewpoint, CPlayer *)
{
	if (!gActive || !gConfig.panoramic_cam || viewpoint != 0 || !gOverviewCam.Ready())
		return camera;
	return &gOverviewCam;
}

extern "C" CBattleEngine *PortCeMultiBeHudFocus(CBattleEngine *be, CPlayer *player)
{
	if (!gActive)
		return be;
	gHudFocusCalls++;
	if (gConfig.hud_focus_slot >= 0 && gConfig.hud_focus_slot < gEngineCount)
	{
		CBattleEngine *focused = gEngines[gConfig.hud_focus_slot];
		if (focused && UnitExistsInWorld(focused) && !focused->IsDying() && focused->GetPlayer())
		{
			if (GAME.GetNPlayers() <= 1 || !player || focused->GetPlayer()->GetNumber() == player->GetNumber())
				return focused;
		}
	}
	return be;
}

extern "C" void PortCeMultiBeDrawHud(CPlayer *, int viewpoint)
{
	if (!gActive || !gSpawned || viewpoint != 0 || gConfig.target_count < 3)
		return;
	if (PortCePhotoOn() || PortCePhotoSavedNext() || GAME.GetGameState() != GAME_STATE_PLAYING)
		return;

	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font)
		return;

	RefreshMatchStateCounts();
	char ascii[128] = {0};
	const bool isVersus = (gMatchState.forsetiTotal > 0 && gMatchState.muspellTotal > 0);
	gMatchState.FormatHudBanner(isVersus, gPlayerSide, ascii, sizeof(ascii));

	WCHAR wide[128] = {0};
	CopyDrawableWide(wide, 128, ascii, font);
	SIZE extent = {0, 0};
	font->GetTextExtent(wide, &extent);
	if (extent.cx <= 0)
		return;

	const float cx = HUD.mViewportX + HUD.mViewportWidth * 0.5f;
	const float cy = HUD.mViewportY + HUD_OFFSET_Y + 22.0f;
	float width = float(extent.cx) + 28.0f;
	float textScaleX = 1.0f;
	const float maxWidth = HUD.mViewportWidth - 88.0f;
	if (maxWidth > 80.0f && width > maxWidth)
	{
		width = maxWidth;
		const float availText = width - 24.0f;
		if (availText > 16.0f && float(extent.cx) > availText)
			textScaleX = availText / float(extent.cx);
	}
	const float height = float(font->GetHeight()) + 14.0f;

	CeHudScope topScope((!GAME.IsMultiplayer() || PortCeOneScreenMatch()) && ENGINE.GetNumViewpoints() == 1,
	                    HUD.mViewportWidth, HUD.mViewportHeight, cx, HUD.mViewportY + HUD_OFFSET_Y);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	RENDERINFO.Apply();
	HUD.RenderObjectiveBar(cx, cy, width, height, 1.0f);
	RENDERINFO.EnableLighting(false);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.Apply();

	const float textX = cx - float(extent.cx) * textScaleX * 0.5f;
	const float textY = cy - float(font->GetHeight()) * 0.5f;
	font->DrawTextWithShadow(textX, textY, 0xffffffff, wide, 0, 1.0f, textScaleX, 1.0f);
}

extern "C" void PortCeMultiBeMenuRows(CeMenuEvent event, CeMenuPage *page)
{
	if (event != CE_MENU_BUILD || !page || page->site != CE_MENU_PAUSE_LIST || !page->list)
		return;
	if (!CPauseMenu::mOptionsPage || !PortCeMenus() || MultiBeFenced())
		return;

	RefreshCachedSettings();
	CMenuOptionsList *list = page->list;
	// Insert Play side, Skirmish mode & Battle Engines cleanly right before Credits (the only mId==1 item on the Options root list).
	CMenuItem *credits = NULL;
	if (list->mItems.Size() > 0)
	{
		CMenuItem *tail = list->mItems.Last();
		if (tail && tail->mText != 0 && tail->mId == 1)
		{
			list->mItems.Remove(tail);
			credits = tail;
		}
	}
	list->AddItem(new (kMultiBeMenuMem) CCePlaySideItem());
	list->AddItem(new (kMultiBeMenuMem) CCeSoloSkirmishItem());
	list->AddItem(new (kMultiBeMenuMem) CCeBattleEnginesItem());
	if (credits)
		list->AddItem(credits);
}

extern "C" void PortCeMultiBeUnitSpawn(CUnit *, CInitThing *)
{
	if (gActive)
		gUnitSpawnEvents++;
}

extern "C" void PortCeMultiBeUnitDestroyed(CUnit *unit)
{
	if (!gActive)
		return;
	gUnitDestroyedEvents++;
	const int idx = IndexOfEngine(unit);
	if (idx >= 0)
	{
		gEngines[idx] = NULL;
		if (gIsAux[idx] && gAuxCameras[idx])
		{
			delete gAuxCameras[idx];
			gAuxCameras[idx] = NULL;
		}
	}
}

extern "C" void PortCeMultiBeWeaponFire(CWeapon *, CUnit *owner, CRound *)
{
	if (!gActive || !owner)
		return;
	const int idx = IndexOfEngine(owner);
	if (idx >= 0)
	{
		gTotalShots++;
		if (gIsAux[idx])
			gAuxShots++;
		if (idx == 1 && gIsAiControlled[1])
			gP2Shots++;
	}
}

// ---- Phase 1 & Front-End patch callbacks (patches 0371..0374) ----

extern "C" void PortCeMultiBeApplyStartSlot(int *playerNumber, int retailInvert)
{
	if (!playerNumber || MultiBeFenced())
		return;
	const int level = GAME.GetCurrentLevel();
	const bool isVersus = (level >= 850 && level <= 899);
	const bool cli0359 = (PortCePlayUnitInvertSides && PortCePlayUnitInvertSides()) != 0;
	const bool menuMuspell = (ReadPlaySideSetting() == CE_MULTIBE_SIDE_MUSPELL);
	*playerNumber = CeMultiBeTransformStartSlot(*playerNumber, retailInvert != 0, cli0359, menuMuspell, isVersus);
}

extern "C" int PortCeMultiBeAllowTeamKills(void)
{
	return MultiBeFenced() ? 0 : 1;
}

extern "C" int PortCeMultiBeSelectStartConfig(int playerNumber, int p1Config, int p2Config)
{
	return CeMultiBeSelectConfigId(playerNumber, p1Config, p2Config);
}

extern "C" int PortCeMultiBeCareerPrefGet(int slot, int prefKind)
{
	return gExtraSlotPrefs.Get(slot, prefKind);
}

extern "C" void PortCeMultiBeCareerPrefSet(int slot, int prefKind, int value)
{
	gExtraSlotPrefs.Set(slot, prefKind, value);
}

extern "C" void PortCeMultiBeOnConfirmedKill(void *killerPtr, void *killedPtr)
{
	if (!gActive || !killerPtr || !killedPtr)
		return;
	CBattleEngine *killer = static_cast<CBattleEngine *>(killerPtr);
	CUnit *killed = static_cast<CUnit *>(killedPtr);
	// Track opposing unit kills by side (auxiliary BE deaths are additionally tracked in OnAuxDestroyed).
	if (killed->IsA(THING_TYPE_BATTLE_ENGINE) && IndexOfEngine(killed) >= 0 && gIsAux[IndexOfEngine(killed)])
		return;
	if (killer->GetAllegiance() == kMuspellAllegiance)
		gMatchState.muspellKills++;
	else if (killer->GetAllegiance() == kForsetiAllegiance)
		gMatchState.forsetiKills++;
}

extern "C" int PortCeMultiBeHandleDeath(void *battleEnginePtr)
{
	if (!gActive || !battleEnginePtr)
		return 0;
	CBattleEngine *be = static_cast<CBattleEngine *>(battleEnginePtr);
	const int idx = IndexOfEngine(be);
	if (idx >= 0 && gIsAux[idx])
	{
		gMatchState.OnAuxDestroyed(idx);
		if (gAuxCameras[idx])
		{
			delete gAuxCameras[idx];
			gAuxCameras[idx] = NULL;
		}
		return 1;	// Suppress retail GAME.DeclarePlayerDead() for auxiliary BEs; respawn cleanly after 5s.
	}
	return 0;
}

extern "C" int PortCeMultiBeActive(void)
{
	return gActive ? 1 : 0;
}

extern "C" int PortCeMultiBePlaySideIsMuspell(void)
{
	if (MultiBeFenced())
		return 0;
	return (ReadPlaySideSetting() == CE_MULTIBE_SIDE_MUSPELL) ? 1 : 0;
}

extern "C" int PortCeSoloSkirmishActive(void)
{
	if (MultiBeFenced())
		return 0;
	if (gInLevel)
		return gLatchedSoloSkirmish ? 1 : 0;
	return (ReadSoloSkirmishSetting() == CE_SOLO_SKIRMISH_SOLO_AI) ? 1 : 0;
}

extern "C" void PortCeMultiBeDrawMpStartStatus(float transition)
{
	if (!PortCeMenus() || MultiBeFenced())
		return;
	const bool solo = (ReadSoloSkirmishSetting() == CE_SOLO_SKIRMISH_SOLO_AI);
	const int side = ReadPlaySideSetting();
	const int mode = ReadMultiBeChoiceSetting();

	char ascii[128] = {0};
	if (!CeMultiBeFormatMpStartStatus(solo, side, mode, ascii, sizeof(ascii)))
		return;

	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font)
		return;
	const float vis = CeMultiBeClamp(transition, 0.0f, 1.0f);
	if (vis < 0.1f)
		return;

	WCHAR wide[128] = {0};
	CopyDrawableWide(wide, 128, ascii, font);
	SIZE extent = {0, 0};
	font->GetTextExtent(wide, &extent);
	if (extent.cx <= 0)
		return;

	float scaleX = 1.0f;
	const float maxLogicalWidth = 580.0f;
	if (float(extent.cx) > maxLogicalWidth)
		scaleX = maxLogicalWidth / float(extent.cx);

	const DWORD alpha = static_cast<DWORD>(vis * 235.0f) & 0xffu;
	const DWORD col = (alpha << 24) | 0x00d8e8ffu;
	font->DrawTextWithShadow(320.0f - float(extent.cx) * scaleX * 0.5f, 418.0f, col, wide, 0, 0, scaleX, 1.0f);
}

#else	// private build: completely inert

extern "C" void PortCeMultiBeLevelBegin(int) {}
extern "C" void PortCeMultiBeLevelEnd(int, int) {}
extern "C" void PortCeMultiBeTickPre() {}
extern "C" CCamera *PortCeMultiBeRenderCamera(CCamera *camera, int, CPlayer *) { return camera; }
extern "C" CBattleEngine *PortCeMultiBeHudFocus(CBattleEngine *be, CPlayer *) { return be; }
extern "C" void PortCeMultiBeDrawHud(CPlayer *, int) {}
extern "C" void PortCeMultiBeMenuRows(CeMenuEvent, CeMenuPage *) {}
extern "C" void PortCeMultiBeUnitSpawn(CUnit *, CInitThing *) {}
extern "C" void PortCeMultiBeUnitDestroyed(CUnit *) {}
extern "C" void PortCeMultiBeWeaponFire(CWeapon *, CUnit *, CRound *) {}
extern "C" void PortCeMultiBeApplyStartSlot(int *, int) {}
extern "C" int PortCeMultiBeAllowTeamKills(void) { return 0; }
extern "C" int PortCeMultiBeSelectStartConfig(int, int p1Config, int) { return p1Config; }
extern "C" int PortCeMultiBeCareerPrefGet(int, int) { return 0; }
extern "C" void PortCeMultiBeCareerPrefSet(int, int, int) {}
extern "C" void PortCeMultiBeOnConfirmedKill(void *, void *) {}
extern "C" int PortCeMultiBeHandleDeath(void *) { return 0; }
extern "C" int PortCeMultiBeActive(void) { return 0; }
extern "C" int PortCeMultiBePlaySideIsMuspell(void) { return 0; }
extern "C" int PortCeSoloSkirmishActive(void) { return 0; }
extern "C" void PortCeMultiBeDrawMpStartStatus(float) {}

#endif
