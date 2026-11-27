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
#include "ce_multibe_ai.h"
#include "ce_player_hooks.h"
#include "ai/ce_ai_pilot_l1.h"

#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "frontend.h"
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
#include "weapon.h"
#include "InfluenceMap.h"
#include "eventmanager.h"
#include "Hud.h"
#include "hud_scale.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "GameInterface.h"
#include "PauseMenu.h"
#include "MenuItem.h"
#include "MenuOptionsList.h"
#include "DXEngine.h"
#include "CLIParams.h"
#include "data/MissionScripts/text/text.stf"
#include "ce_photo.h"
#include "ce_cmdmatch.h"
#ifndef CE_NETWORK_OFF
#include "BattleEngineConfigurations.h"
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
extern "C" int PortCeRecordFeaturesAllowed(void) __attribute__((weak));
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
extern "C" bool PortCeMuseumVisitActive(void) __attribute__((weak));
extern "C" bool PortCeLivingSandboxActive(void) __attribute__((weak));
extern "C" bool PortCeCarrierClashActive(void) __attribute__((weak));
extern "C" bool PortCeFlagshipDuelActive(void) __attribute__((weak));
extern "C" bool PortCeEditorActive(void) __attribute__((weak));
bool PortCeCmdMapOpen() __attribute__((weak));
extern "C" int PortCePlayUnitOwnsPilot(int player) __attribute__((weak));
bool PortCeNetActive() __attribute__((weak));
#ifndef CE_NETWORK_OFF
extern "C" uint32_t CE_LockstepLocalPlayer(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchActive(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchSlotCount(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchLocalSlot(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchSlotIsHuman(int slot) __attribute__((weak));
extern "C" int PortCeOnlineMatchSlotTeam(int slot) __attribute__((weak));
extern "C" const char *PortCeOnlineMatchSlotBeCfg(int slot) __attribute__((weak));
extern "C" uint32_t PortCeOnlineMatchSeed(void) __attribute__((weak));
#endif

namespace CeMultiBePlayerAccess
{

// Standard C++ explicit-instantiation member accessor for private CPlayer::mBattleEngine so
// PortCeMultiBeUnitDestroyed() can safely detach a dying auxiliary Battle Engine via SetReader(NULL)
// without triggering CPlayer::AssignBattleEngine(NULL)'s unconditional mBattleEngine->SetPlayer() deref.
template <typename Tag>
struct Slot { static typename Tag::Ptr ptr; };
template <typename Tag>
typename Tag::Ptr Slot<Tag>::ptr = NULL;

template <typename Tag, typename Tag::Ptr P>
struct Bind
{
	struct Init { Init() { Slot<Tag>::ptr = P; } };
	static Init sInit;
};
template <typename Tag, typename Tag::Ptr P>
typename Bind<Tag, P>::Init Bind<Tag, P>::sInit;

struct BattleEngineTag { typedef CActiveReader<CBattleEngine> CPlayer::*Ptr; };
template struct Bind<BattleEngineTag, &CPlayer::mBattleEngine>;

inline void ClearPlayerBattleEngine(CPlayer *p)
{
	if (p && Slot<BattleEngineTag>::ptr)
		(p->*Slot<BattleEngineTag>::ptr).SetReader(NULL);
}

} // namespace CeMultiBePlayerAccess

namespace
{

static constexpr EMemoryType kMultiBeMenuMem = MEMTYPE_UNKNOWN;

bool NetworkOrLockstepActive()
{
	return (CE_BootRequested && CE_BootRequested()) ||
	       (CE_SessionActive && CE_SessionActive()) ||
	       (CE_LockstepRequested && CE_LockstepRequested()) ||
	       (CE_LockstepSharing && CE_LockstepSharing()) ||
	       (PortCeNetActive && PortCeNetActive());
}
#ifndef CE_NETWORK_OFF

bool OnlineMatchActive()
{
	return PortCeOnlineMatchActive && PortCeOnlineMatchActive() != 0;
}

int OnlineResolveBeConfigId(int slot)
{
	if (!OnlineMatchActive() || !PortCeOnlineMatchSlotBeCfg)
		return 0;
	const char *want = PortCeOnlineMatchSlotBeCfg(slot);
	if (!want || !want[0])
		return 0;
	const SLONG count = UBattleEngineConfigurations::CountConfigurations();
	for (int i = 0; i < (int)count && i < kMaxConfigurations; ++i)
	{
		CBattleEngineData *cfg = UBattleEngineConfigurations::GetConfiguration(i);
		if (cfg && cfg->mConfigurationName && CeMultiBeStrEqCase(cfg->mConfigurationName, want))
			return i;
	}
	return 0;
}

#endif

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
	const bool recordingFenced = PortRecording() && !(PortCeRecordFeaturesAllowed && PortCeRecordFeaturesAllowed());
	if (!PortCeLayer() || recordingFenced || NetworkOrLockstepActive())
		return true;
	if (PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo ||
	    CLIPARAMS.mPlaybackDemo || CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode)
		return true;
	if ((PortCeMuseumVisitActive && PortCeMuseumVisitActive()) ||
	    (PortCeLivingSandboxActive && PortCeLivingSandboxActive()) ||
	    (PortCeCarrierClashActive && PortCeCarrierClashActive()) ||
	    (PortCeFlagshipDuelActive && PortCeFlagshipDuelActive()) ||
	    (PortCeEditorActive && PortCeEditorActive()))
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
int						gAiWeaponPicks = 0;
int						gAiEvades = 0;
int						gAiRetreats = 0;
int						gAiDashes = 0;
int						gAiLandings = 0;
float					gMaxAiLandAlt = 0.0f;

CBattleEngine			*gEngines[CE_MULTIBE_MAX] = {NULL, NULL, NULL, NULL};
bool					gIsAux[CE_MULTIBE_MAX] = {false, false, false, false};
bool					gIsAiControlled[CE_MULTIBE_MAX] = {false, false, false, false};
CPlayer					*gAuxPlayers[CE_MULTIBE_MAX] = {NULL, NULL, NULL, NULL};
CCamera					*gAuxCameras[CE_MULTIBE_MAX] = {NULL, NULL, NULL, NULL};
bool					gAuxDeathCam[CE_MULTIBE_MAX] = {false, false, false, false};
FVector					gStartPos[CE_MULTIBE_MAX];
float					gStartYaw[CE_MULTIBE_MAX] = {0.0f, 0.0f, 0.0f, 0.0f};
bool					gMoved[CE_MULTIBE_MAX] = {false, false, false, false};
float					gAnchorFwdX = 0.0f;
float					gAnchorFwdY = 1.0f;
bool					gHasOpposingAnchor = false;
FVector					gOpposingAnchorPos;
FMatrix					gOpposingAnchorOri;
float					gOpposingAnchorYaw = 0.0f;
bool					gRelocatedOnlineP1 = false;
int						gWinningTeam = -1;
int						gAuxCoopLossTicks = 0;
int						gAuxCoopLossReason = 0;
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
	CCamera *cam0 = GAME.GetCamera(0);
	const float camAspect = cam0 ? cam0->GetAspectRatio() : 0.0f;
	const int hudAnchored = PortHudAnchored();
	const float hudInset = PortHudInset(float(PLATFORM.GetWindowWidth()));
	fprintf(stderr,
	        "ce-multibe: phase=%s level=%d target=%d active=%d spawned=%d ticks=%d nan=%d shots=%d aux_shots=%d ai_targeted=%d hud_focus=%d moved=%d morphs=%d respawns=%d forseti_kills=%d muspell_kills=%d side=%s solo=%d onescreen=%d viewpoints=%d p2_shots=%d p2_moved=%d spawns=%d destroyed=%d weapon_picks=%d evades=%d retreats=%d dashes=%d cam_aspect=%.2f hud_anchored=%d hud_inset=%.1f max_ai_land_alt=%.1f ai_landings=%d\n",
	        phase, gLevel, gConfig.target_count, CountLiveEngines(), gAuxSpawned,
	        gSimTicks, gNanCount, gTotalShots, gAuxShots, gAiTargetedCount,
	        gHudFocusCalls, CountMovedEngines(), gMatchState.totalMorphs, gMatchState.totalRespawns,
	        gMatchState.forsetiKills, gMatchState.muspellKills,
	        CeMultiBePlaySideIniValue(gPlayerSide), gSoloSkirmish ? 1 : 0,
	        PortCeOneScreenMatch() ? 1 : 0, ENGINE.GetNumViewpoints(),
	        gP2Shots, gP2Moved ? 1 : 0, gUnitSpawnEvents, gUnitDestroyedEvents,
	        gAiWeaponPicks, gAiEvades, gAiRetreats, gAiDashes,
	        camAspect, hudAnchored, hudInset, gMaxAiLandAlt, gAiLandings);
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
		gAuxDeathCam[i] = false;
		gMoved[i] = false;
		gStartYaw[i] = 0.0f;
	}
	CeAiPilotL1Reset(-1);
	PortCePlayerHooksLevelReset();
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
	gAuxCoopLossTicks = 0;
	gAuxCoopLossReason = 0;
	gTotalShots = 0;
	gAuxShots = 0;
	gP2Shots = 0;
	gP2Moved = false;
	gAiTargetedCount = 0;
	gHudFocusCalls = 0;
	gUnitSpawnEvents = 0;
	gUnitDestroyedEvents = 0;
	gAiWeaponPicks = 0;
	gAiEvades = 0;
	gAiRetreats = 0;
	gAiDashes = 0;
	gAiLandings = 0;
	gMaxAiLandAlt = 0.0f;
	gAnchorFwdX = 0.0f;
	gAnchorFwdY = 1.0f;
	gHasOpposingAnchor = false;
	gRelocatedOnlineP1 = false;
	gWinningTeam = -1;
	gOverviewCam.Reset();
}

#ifndef CE_NETWORK_OFF
void CheckOnlineVersusTeamElimination(int deadSlot)
{
	if (!OnlineMatchActive() || WORLD.GetType() != CWorld::kVersusMultiplayer)
		return;
	if (deadSlot < 0 || deadSlot >= gEngineCount)
		return;
	const int deadTeam = gMatchState.slotTeam[deadSlot];
	bool teamHasLife = false;
	for (int s = 0; s < gEngineCount; ++s)
	{
		if (s == deadSlot || gMatchState.slotTeam[s] != deadTeam)
			continue;
		const bool aliveNow = gEngines[s] && UnitExistsInWorld(gEngines[s]) && !gEngines[s]->IsDying();
		if (aliveNow || GAME.GetPlayerLives(s + 1) > 0 || (gIsAux[s] && gMatchState.respawnTimer[s] > 0))
		{
			teamHasLife = true;
			break;
		}
	}
	if (!teamHasLife)
	{
		const int winTeam = (deadTeam == CE_MULTIBE_SIDE_MUSPELL)
			? CE_MULTIBE_SIDE_FORSETI : CE_MULTIBE_SIDE_MUSPELL;
		gWinningTeam = winTeam;
		const int p0Team = gMatchState.slotTeam[0];
		GAME.MPDeclarePlayerWon(winTeam == p0Team ? 1 : 2);
	}
}
#endif

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

	for (int si = 0; si < 4; si++)
	{
		for (int ai = 0; ai < 8; ai++)
		{
			float r = 0.0f, f = 0.0f;
			CeMultiBeDrySearchOffset(si, ai, offRight, offFwd, &r, &f);
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
// Uses GotoFPView() (CThingCamera) so weapon aim adjustment & direct lock raycasts have zero 3rd-person parallax.
CCamera *CreateAuxCameraForSlot(CPlayer *auxPlayer, int camSlot)
{
	if (!auxPlayer || camSlot < 0 || camSlot >= MAX_PLAYERS)
		return NULL;
	CCamera *savedCam = MultiBeGameAccess::CameraSlot(camSlot);
	const BOOL savedFreeCam = MultiBeGameAccess::FreeCameraSlot(camSlot);
	MultiBeGameAccess::FreeCameraSlot(camSlot) = FALSE;
	MultiBeGameAccess::CameraSlot(camSlot) = NULL;
	auxPlayer->GotoFPView();
	CCamera *auxCam = MultiBeGameAccess::CameraSlot(camSlot);
	MultiBeGameAccess::CameraSlot(camSlot) = savedCam;
	MultiBeGameAccess::FreeCameraSlot(camSlot) = savedFreeCam;
	return auxCam;
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
#ifndef CE_NETWORK_OFF
	if (OnlineMatchActive())
		init.mConfigurationId = OnlineResolveBeConfigId(idx);
#endif
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
	gAuxDeathCam[idx] = false;
	gAuxCameras[idx] = CreateAuxCameraForSlot(gAuxPlayers[idx], idx);

	gEngines[idx] = be;
	gIsAux[idx] = true;
#ifdef CE_NETWORK_OFF
	gIsAiControlled[idx] = true;
#else
	gIsAiControlled[idx] = OnlineMatchActive()
		? !(PortCeOnlineMatchSlotIsHuman && PortCeOnlineMatchSlotIsHuman(idx))
		: true;
#endif
	gStartPos[idx] = be->GetPos();
	gStartYaw[idx] = yaw;
#ifndef CE_NETWORK_OFF
	CeAiPilotL1Reset(idx, be->GetLife(),
	                 (OnlineMatchActive() && PortCeOnlineMatchSeed) ? PortCeOnlineMatchSeed() : 0u,
	                 OnlineMatchActive());
#else
	CeAiPilotL1Reset(idx, be->GetLife(), 0u, false);
#endif
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
#ifndef CE_NETWORK_OFF
		const bool onlineSlotAi = OnlineMatchActive() && !(PortCeOnlineMatchSlotIsHuman && PortCeOnlineMatchSlotIsHuman(slot));
		if (OnlineMatchActive())
			gIsAiControlled[slot] = onlineSlotAi;
#endif
		if (be)
		{
#ifndef CE_NETWORK_OFF
			if (OnlineMatchActive() && PortCeOnlineMatchSlotTeam)
			{
				const int ticketTeam = (PortCeOnlineMatchSlotTeam(slot) == CE_MULTIBE_SIDE_MUSPELL)
					? CE_MULTIBE_SIDE_MUSPELL : CE_MULTIBE_SIDE_FORSETI;
				be->mAllegiance = (ticketTeam == CE_MULTIBE_SIDE_MUSPELL) ? kMuspellAllegiance : kForsetiAllegiance;
			}
#endif
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
			if (soloP2)
			{
				CeAiPilotL1Reset(slot, be->GetLife(), 0u, false);
			}
#ifndef CE_NETWORK_OFF
			if (onlineSlotAi)
			{
				if (pl && slot < MAX_PLAYERS && (!pl->GetIsInFPV() || !GAME.GetCurrentCamera(slot)))
					pl->GotoFPView();
				const uint32_t matchSeed = PortCeOnlineMatchSeed ? PortCeOnlineMatchSeed() : 0u;
				CeAiPilotL1Reset(slot, be->GetLife(), matchSeed, true);
			}
#endif
		}
		else
		{
#ifndef CE_NETWORK_OFF
			const int fallbackTeam = OnlineMatchActive()
				? ((anchor->GetAllegiance() == kMuspellAllegiance) ? CE_MULTIBE_SIDE_FORSETI : CE_MULTIBE_SIDE_MUSPELL)
				: ((gPlayerSide == CE_MULTIBE_SIDE_MUSPELL) ? CE_MULTIBE_SIDE_FORSETI : CE_MULTIBE_SIDE_MUSPELL);
#else
			const int fallbackTeam = (gPlayerSide == CE_MULTIBE_SIDE_MUSPELL) ? CE_MULTIBE_SIDE_FORSETI : CE_MULTIBE_SIDE_MUSPELL;
#endif
			gMatchState.RegisterSlotTeam(slot, fallbackTeam);
			if (fallbackTeam == CE_MULTIBE_SIDE_MUSPELL)
				muspellCount++;
			else
				forsetiCount++;
		}
	}

	gPlayerSide = (anchor->GetAllegiance() == kMuspellAllegiance) ? CE_MULTIBE_SIDE_MUSPELL : CE_MULTIBE_SIDE_FORSETI;
#ifndef CE_NETWORK_OFF
	if (OnlineMatchActive() && PortCeOnlineMatchSlotTeam)
	{
		const int localSlot = PortCeOnlineMatchLocalSlot ? PortCeOnlineMatchLocalSlot() : 0;
		gPlayerSide = (PortCeOnlineMatchSlotTeam(localSlot) == CE_MULTIBE_SIDE_MUSPELL)
			? CE_MULTIBE_SIDE_MUSPELL : CE_MULTIBE_SIDE_FORSETI;
	}
#endif

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
	const bool versusLevel = (WORLD.GetType() == CWorld::kVersusMultiplayer);

	// Preserve retail P1's opposing base anchor before relocating same-team online P1 in 2v2 Humans-vs-AI.
	if (gEngineCount >= 2 && gEngines[1] && UnitExistsInWorld(gEngines[1]))
	{
		gHasOpposingAnchor = true;
		gOpposingAnchorPos = gEngines[1]->GetPos();
		gOpposingAnchorOri = gEngines[1]->GetOrientation();
		gOpposingAnchorYaw = gEngines[1]->GetCurrentEulerOrientation().mYaw;
#ifndef CE_NETWORK_OFF
		if (OnlineMatchActive() && versusLevel && gEngines[0] &&
		    gEngines[1]->GetAllegiance() == gEngines[0]->GetAllegiance())
		{
			FVector allyPos = anchorPos;
			bool relocated = FindDrySpawnPosition(anchorPos, right, fwd, 12.0f, 0.0f, &allyPos) ||
			                 FindDrySpawnPosition(anchorPos, right, fwd, -12.0f, 0.0f, &allyPos);
			bool airborneFallback = false;
			if (!relocated)
			{
				const float waterZ = MAP.GetWaterLevel();
				allyPos = anchorPos + right * 12.0f;
				const float groundZ = MAP.Collide(allyPos);
				const float surfaceZ = (groundZ < waterZ) ? groundZ : waterZ;
				allyPos.Z = (anchorPos.Z < surfaceZ - 18.0f) ? anchorPos.Z : (surfaceZ - 22.0f);
				if (CeMultiBeIsFinitePos(allyPos.X, allyPos.Y, allyPos.Z))
				{
					relocated = true;
					airborneFallback = true;
				}
			}
			if (relocated)
			{
				gEngines[1]->Teleport(allyPos);
				gEngines[1]->TeleportOrientation(anchorOri);
				if (airborneFallback && gEngines[1]->GetState() == BATTLE_ENGINE_STATE_WALKER)
					gEngines[1]->Morph();
				gStartPos[1] = allyPos;
				gStartYaw[1] = anchorYaw;
				gRelocatedOnlineP1 = true;
			}
		}
#endif
	}

	// Spawn auxiliary Battle Engines up to target_count (3 or 4), strictly on dry terrain above water level.
	int formationSlot = 1;
	int opposingAuxIdx = 0;
	while (gEngineCount < gConfig.target_count && gEngineCount < CE_MULTIBE_MAX)
	{
		// On 2v2 Versus maps, keep teams balanced (2 Forseti vs 2 Muspell) regardless of Play side.
		// On Campaign & Co-op MP levels, spawn allied Forseti BEs (f_be* mesh is guaranteed loaded).
		EAllegiance side = kForsetiAllegiance;
#ifndef CE_NETWORK_OFF
		if (OnlineMatchActive() && PortCeOnlineMatchSlotTeam)
		{
			side = (PortCeOnlineMatchSlotTeam(gEngineCount) == CE_MULTIBE_SIDE_MUSPELL)
				? kMuspellAllegiance : kForsetiAllegiance;
			if (side == kMuspellAllegiance)
				muspellCount++;
			else
				forsetiCount++;
		}
		else
#endif
		if (versusLevel)
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
		// - In normal Versus play, opposing AI BEs deploy beside their own team's base anchor and fly in.
		// - In panoramic showcase mode, deploy opposing AI BEs on the skirmish perimeter ahead of P1 for clear combat framing.
		FVector baseAnchor = anchorPos;
		FVector baseRight = right;
		FVector baseFwd = fwd;
		float spawnYaw = anchorYaw;
		float offRight = 0.0f, offFwd = 0.0f;

#ifndef CE_NETWORK_OFF
		const bool sameSideAsP0 = OnlineMatchActive()
			? (side == anchor->GetAllegiance())
			: ((side == kMuspellAllegiance) == (gPlayerSide == CE_MULTIBE_SIDE_MUSPELL));
#else
		const bool sameSideAsP0 = ((side == kMuspellAllegiance) == (gPlayerSide == CE_MULTIBE_SIDE_MUSPELL));
#endif
		if (versusLevel && !sameSideAsP0 && !gConfig.panoramic_cam && gEngineCount >= 2 && gHasOpposingAnchor)
		{
			baseAnchor = gOpposingAnchorPos;
			baseFwd = gOpposingAnchorOri * FVector(0.0f, 1.0f, 0.0f);
			baseFwd.Z = 0.0f;
			if (baseFwd.MagnitudeSq() < 1e-4f)
				baseFwd = fwd * -1.0f;
			else
				baseFwd.Normalise();
			baseRight = FVector(baseFwd.Y, -baseFwd.X, 0.0f);
			spawnYaw = gOpposingAnchorYaw;
			offRight = (opposingAuxIdx == 0) ? 12.0f : -12.0f;
			offFwd = 6.0f;
			opposingAuxIdx++;
		}
		else if (versusLevel && !sameSideAsP0 && gConfig.panoramic_cam)
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
	const float waterZ = MAP.GetWaterLevel();
	const float startGroundZ = MAP.Collide(spawnPos);
	bool foundDry = false;
	if (CeMultiBeIsAboveWater(startGroundZ, waterZ, 0.8f))
	{
		spawnPos.Z = startGroundZ - 0.5f;
		foundDry = CeMultiBeIsFinitePos(spawnPos.X, spawnPos.Y, spawnPos.Z);
	}
	if (!foundDry && !FindDrySpawnPosition(gStartPos[idx], right, fwd, 12.0f, 0.0f, &spawnPos))
	{
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

void SyncHumanPlayerEngines()
{
	const int nPlayers = GAME.GetNPlayers();
	for (int slot = 0; slot < gEngineCount && slot < nPlayers && slot < MAX_PLAYERS; ++slot)
	{
		if (gIsAux[slot] || (gSoloSkirmish && slot == 1))
			continue;
		CPlayer *pl = GAME.GetPlayer(slot);
		CBattleEngine *live = pl ? pl->GetBattleEngine() : NULL;
		if (live && UnitExistsInWorld(live) && !live->IsDying())
		{
			if (gEngines[slot] != live)
			{
				gEngines[slot] = live;
#ifndef CE_NETWORK_OFF
				if (OnlineMatchActive() && PortCeOnlineMatchSlotTeam)
				{
					const int ticketTeam = (PortCeOnlineMatchSlotTeam(slot) == CE_MULTIBE_SIDE_MUSPELL)
						? CE_MULTIBE_SIDE_MUSPELL : CE_MULTIBE_SIDE_FORSETI;
					live->mAllegiance = (ticketTeam == CE_MULTIBE_SIDE_MUSPELL) ? kMuspellAllegiance : kForsetiAllegiance;
				}
				if (slot == 1 && gRelocatedOnlineP1 && (live->GetPos() - gStartPos[1]).MagnitudeSq() > 900.0f)
				{
					live->Teleport(gStartPos[1]);
					FMatrix ori;
					ori.MakeRotationYawF(gStartYaw[1]);
					live->TeleportOrientation(ori);
					const float waterZ = MAP.GetWaterLevel();
					const float groundZ = MAP.Collide(gStartPos[1]);
					if (!CeMultiBeIsAboveWater(groundZ, waterZ, 0.8f) &&
					    live->GetState() == BATTLE_ENGINE_STATE_WALKER)
						live->Morph();
				}
				else if (!(slot == 1 && gRelocatedOnlineP1))
#endif
				{
					gStartPos[slot] = live->GetPos();
					gStartYaw[slot] = live->GetCurrentEulerOrientation().mYaw;
				}
				const int team = (live->GetAllegiance() == kMuspellAllegiance)
					? CE_MULTIBE_SIDE_MUSPELL : CE_MULTIBE_SIDE_FORSETI;
				gMatchState.RegisterSlotTeam(slot, team);
				if (gIsAiControlled[slot])
				{
#ifndef CE_NETWORK_OFF
					CeAiPilotL1Reset(slot, live->GetLife(),
					                 (OnlineMatchActive() && PortCeOnlineMatchSeed) ? PortCeOnlineMatchSeed() : 0u,
					                 OnlineMatchActive());
#else
					CeAiPilotL1Reset(slot, live->GetLife(), 0u, false);
#endif
				}
			}
			if (gIsAiControlled[slot] && (!pl->GetIsInFPV() || !GAME.GetCurrentCamera(slot)))
				pl->GotoFPView();
		}
		else
		{
			gEngines[slot] = NULL;
		}
	}
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
			CeAiPilotL1Reset(1, live->GetLife(), 0u, false);
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
	// Defensively restore any leaked one-screen fullscreen switch from an abnormal prior level exit.
	CeCmdMatchLevelEnd();
	ResetState();
	gInLevel = true;
	gLatchedSoloSkirmish = false;
#ifndef CE_NETWORK_OFF
	if (OnlineMatchActive())
	{
		const int onlineSlots = PortCeOnlineMatchSlotCount ? PortCeOnlineMatchSlotCount() : 0;
		if (onlineSlots >= 2 && onlineSlots <= CE_MULTIBE_MAX)
		{
			const int localSlot = PortCeOnlineMatchLocalSlot ? PortCeOnlineMatchLocalSlot() : 0;
			gConfig.target_count = onlineSlots;
			gConfig.panoramic_cam = false;
			gConfig.hud_focus_slot = (localSlot >= 0 && localSlot < onlineSlots) ? localSlot : 0;
			gLevel = level;
			gPlayerSide = (PortCeOnlineMatchSlotTeam && PortCeOnlineMatchSlotTeam(gConfig.hud_focus_slot) == CE_MULTIBE_SIDE_MUSPELL)
				? CE_MULTIBE_SIDE_MUSPELL : CE_MULTIBE_SIDE_FORSETI;
			gSoloSkirmish = false;
			gActive = true;
		}
		return;
	}
#endif
	// Never activate inside an online network/lockstep session, recording, demo, attract, or pilot run.
	if (MultiBeFenced())
		return;
	RefreshCachedSettings();
	char buf[32] = {0};
	const int count = CE_SettingGetText("multi_be", buf, (int)sizeof(buf)) ? CeMultiBeParseCount(buf) : 0;
	const bool solo = (ReadSoloSkirmishSetting() == CE_SOLO_SKIRMISH_SOLO_AI);
	const bool allowCampaign = CE_SettingIs("multi_be_campaign", "1") != 0;
	const bool soloThisLevel = solo && (level >= 850 && level <= 899);
	gLatchedSoloSkirmish = soloThisLevel;
	const int effectiveTarget = CeMultiBeEffectiveTargetCount(count, solo, level, allowCampaign);
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

#ifndef CE_NETWORK_OFF
	// Synchronize online human -> AI disconnect backfill BEFORE SyncHumanPlayerEngines(), regardless of
	// whether gEngines[i] is currently alive or mid-respawn, so every false -> true transition resets
	// Layer-1 state with the match seed and snaps core player slots into FP view.
	if (OnlineMatchActive())
	{
		const uint32_t matchSeed = PortCeOnlineMatchSeed ? PortCeOnlineMatchSeed() : 0u;
		const int nPlayers = GAME.GetNPlayers();
		for (int i = 0; i < gEngineCount && i < CE_MULTIBE_MAX; i++)
		{
			if (!gIsAiControlled[i] && !(PortCeOnlineMatchSlotIsHuman && PortCeOnlineMatchSlotIsHuman(i)))
			{
				gIsAiControlled[i] = true;
				CBattleEngine *be = gEngines[i];
				const bool liveBe = (be != NULL && UnitExistsInWorld(be) && !be->IsDying());
				CeAiPilotL1Reset(i, liveBe ? be->GetLife() : 0.0f, matchSeed, true);
				if (!gIsAux[i] && i < nPlayers && i < MAX_PLAYERS)
				{
					CPlayer *pl = GAME.GetPlayer(i);
					if (pl && (!pl->GetIsInFPV() || !GAME.GetCurrentCamera(i)))
						pl->GotoFPView();
				}
			}
		}
	}
#endif

	// Keep human and Solo Skirmish Player 2 pointers synchronized across retail deaths & respawns (never dangle!).
	SyncHumanPlayerEngines();
	if (gSoloSkirmish)
		SyncSoloP2Engine();

	gSimTicks++;

	if (gAuxCoopLossTicks > 0)
	{
		gAuxCoopLossTicks--;
		if (gAuxCoopLossTicks == 0)
		{
			const int reason = gAuxCoopLossReason;
			gAuxCoopLossReason = 0;
			if (GAME.GetGameState() == GAME_STATE_PLAYING)
				GAME.DeclareLevelLost(reason, TRUE);
		}
	}

	// Advance auxiliary BE respawn timers and redeploy any slot whose 5-second cooldown finished.
	const uint8_t readyMask = gMatchState.TickRespawnTimers();
	if (readyMask)
	{
		for (int i = 0; i < gEngineCount; i++)
			if ((readyMask & (uint8_t)(1u << i)) && gIsAux[i])
				RespawnAuxiliaryBattleEngine(i);
	}

	// Validate positions & movement telemetry (AI pilot execution runs in 0396 player_act via ce_player_hooks.cpp).
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
	if (viewpoint != 0)
		return camera;
	if ((PortCeCmdMapOpen && PortCeCmdMapOpen()) ||
	    (PortCePlayUnitOwnsPilot && PortCePlayUnitOwnsPilot(0)) ||
	    GAME.IsFreeCameraOn(0) ||
	    camera != GAME.GetCamera(0))
		return camera;
#ifndef CE_NETWORK_OFF
	if (gActive && OnlineMatchActive())
	{
		if (dynamic_cast<CPanCamera *>(camera) || dynamic_cast<CPanCamera *>(GAME.GetCamera(0)))
			return camera;
		const int localSlot = PortCeOnlineMatchLocalSlot ? PortCeOnlineMatchLocalSlot() : 0;
		if (localSlot >= 2 && localSlot < gEngineCount && gAuxCameras[localSlot])
			return gAuxCameras[localSlot];
		return camera;
	}
#endif
	if (!gActive || !gConfig.panoramic_cam || !gOverviewCam.Ready())
		return camera;
	return &gOverviewCam;
}

extern "C" CBattleEngine *PortCeMultiBeHudFocus(CBattleEngine *be, CPlayer *player)
{
	if (!gActive)
		return be;
	gHudFocusCalls++;
#ifndef CE_NETWORK_OFF
	if (OnlineMatchActive())
	{
		const int slot = gConfig.hud_focus_slot;
		if (slot < 0 || slot >= gEngineCount)
			return (slot >= GAME.GetNPlayers()) ? NULL : be;
		if (gIsAux[slot] && gAuxDeathCam[slot])
			return NULL;
		CBattleEngine *focused = gEngines[slot];
		if (focused && UnitExistsInWorld(focused) && !focused->IsDying() && focused->GetPlayer())
			return focused;
		return NULL;
	}
#endif
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
	const bool pauseMenuOpen = (GAME.GetPauseMenu() && GAME.GetPauseMenu()->mActive);
	if (PortCePhotoOn() || PortCePhotoSavedNext() || GAME.IsPaused() || GAMEINTERFACE.IsMenuUp() ||
	    pauseMenuOpen || (PortCeCmdMapOpen && PortCeCmdMapOpen()) ||
	    GAME.GetGameState() != GAME_STATE_PLAYING)
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

	CeHudScope topScope((!PortCeIsSplitScreen() || PortCeOneScreenMatch()) && ENGINE.GetNumViewpoints() == 1,
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
		if (gIsAux[idx])
		{
			if (gAuxPlayers[idx])
				CeMultiBePlayerAccess::ClearPlayerBattleEngine(gAuxPlayers[idx]);
			if (gAuxCameras[idx] && !gAuxDeathCam[idx])
			{
				delete gAuxCameras[idx];
				gAuxCameras[idx] = NULL;
			}
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
#ifndef CE_NETWORK_OFF
	if (OnlineMatchActive() && playerNumber)
	{
		const bool isVersus = (WORLD.GetType() == CWorld::kVersusMultiplayer);
		const bool swapSides = isVersus && PortCeOnlineMatchSlotTeam &&
			PortCeOnlineMatchSlotTeam(0) == CE_MULTIBE_SIDE_MUSPELL;
		if (swapSides)
			*playerNumber = CeMultiBeTransformStartSlot(*playerNumber, retailInvert != 0, false, true, true);
		return;
	}
#endif
	if (!playerNumber || MultiBeFenced())
		return;
	const bool isVersus = (WORLD.GetType() == CWorld::kVersusMultiplayer);
	const bool cli0359 = (PortCePlayUnitInvertSides && PortCePlayUnitInvertSides()) != 0;
	const bool menuMuspell = (ReadPlaySideSetting() == CE_MULTIBE_SIDE_MUSPELL);
	const bool featureActive = gActive || menuMuspell || cli0359 || (*playerNumber >= 3);
	if (!featureActive)
		return;
	*playerNumber = CeMultiBeTransformStartSlot(*playerNumber, retailInvert != 0, cli0359, menuMuspell, isVersus);
}

extern "C" int PortCeMultiBeResolveStartAllegiance(int playerNumber, int defaultAllegiance)
{
#ifndef CE_NETWORK_OFF
	if (OnlineMatchActive() && PortCeOnlineMatchSlotTeam &&
	    WORLD.GetType() == CWorld::kVersusMultiplayer)
	{
		const int slot = playerNumber - 1;
		if (slot >= 0 && slot < CE_MULTIBE_MAX)
		{
			return (PortCeOnlineMatchSlotTeam(slot) == CE_MULTIBE_SIDE_MUSPELL)
				? (int)kMuspellAllegiance : (int)kForsetiAllegiance;
		}
	}
#else
	(void)playerNumber;
#endif
	return defaultAllegiance;
}

extern "C" void PortCeMultiBeAdjustRespawnPos(int playerNumber, float *x, float *y, float *z, float *yaw, int *planeMode)
{
#ifndef CE_NETWORK_OFF
	if (!gActive || !OnlineMatchActive() || playerNumber != 2 || !gRelocatedOnlineP1)
		return;
	if (!x || !y || !z || !yaw || !planeMode)
		return;
	FVector spawnPos = gStartPos[1];
	FVector fwd(gAnchorFwdX, gAnchorFwdY, 0.0f);
	if (fwd.MagnitudeSq() < 1e-4f)
		fwd = FVector(0.0f, 1.0f, 0.0f);
	else
		fwd.Normalise();
	FVector right(fwd.Y, -fwd.X, 0.0f);
	const float waterZ = MAP.GetWaterLevel();
	const float startGroundZ = MAP.Collide(spawnPos);
	bool foundDry = false;
	if (CeMultiBeIsAboveWater(startGroundZ, waterZ, 0.8f))
	{
		spawnPos.Z = startGroundZ - 0.5f;
		foundDry = CeMultiBeIsFinitePos(spawnPos.X, spawnPos.Y, spawnPos.Z);
	}
	bool airborne = false;
	if (!foundDry && !FindDrySpawnPosition(gStartPos[1], right, fwd, 12.0f, 0.0f, &spawnPos))
	{
		spawnPos = gStartPos[1];
		const float groundZ = MAP.Collide(spawnPos);
		const float surfaceZ = (groundZ < waterZ) ? groundZ : waterZ;
		spawnPos.Z = surfaceZ - 22.0f;
		if (!CeMultiBeIsFinitePos(spawnPos.X, spawnPos.Y, spawnPos.Z))
			return;
		airborne = true;
	}
	*x = spawnPos.X;
	*y = spawnPos.Y;
	*z = spawnPos.Z;
	*yaw = gStartYaw[1];
	*planeMode = airborne ? 1 : 0;
#else
	(void)playerNumber; (void)x; (void)y; (void)z; (void)yaw; (void)planeMode;
#endif
}

extern "C" int PortCeMultiBeAllowTeamKills(void)
{
#ifndef CE_NETWORK_OFF
	if (OnlineMatchActive())
		return 1;
#endif
	if (MultiBeFenced())
		return 0;
	const bool isVersus = (WORLD.GetType() == CWorld::kVersusMultiplayer);
	const bool cli0359 = (PortCePlayUnitInvertSides && PortCePlayUnitInvertSides()) != 0;
	const bool menuMuspell = isVersus && (ReadPlaySideSetting() == CE_MULTIBE_SIDE_MUSPELL);
	return (gActive || menuMuspell || cli0359) ? 1 : 0;
}

extern "C" int PortCeMultiBeSelectStartConfig(int playerNumber, int p1Config, int p2Config)
{
#ifndef CE_NETWORK_OFF
	if (OnlineMatchActive())
	{
		const int slot = playerNumber - 1;
		if (slot >= 0 && slot < CE_MULTIBE_MAX)
			return OnlineResolveBeConfigId(slot);
	}
#endif
	return CeMultiBeSelectConfigId(playerNumber, p1Config, p2Config);
}
#ifndef CE_NETWORK_OFF

extern "C" CPlayer *PortCeMultiBeGetSlotPlayer(int slot)
{
	if (!gActive || slot < 0 || slot >= gEngineCount)
		return NULL;
	if (gIsAux[slot])
		return gAuxPlayers[slot];
	return (slot < GAME.GetNPlayers()) ? GAME.GetPlayer(slot) : NULL;
}

extern "C" CBattleEngine *PortCeMultiBeGetSlotEngine(int slot)
{
	if (!gActive || slot < 0 || slot >= gEngineCount)
		return NULL;
	CBattleEngine *be = gEngines[slot];
	return (be && UnitExistsInWorld(be)) ? be : NULL;
}
#endif

extern "C" CCamera *PortCeMultiBeAuxCamera(int slot)
{
	if (!gActive || slot < 0 || slot >= gEngineCount || slot >= CE_MULTIBE_MAX || !gIsAux[slot])
		return NULL;
	return gAuxCameras[slot];
}

extern "C" void PortCeMultiBeSetAuxCamera(int slot, CCamera *cam)
{
	if (!gActive || slot < 0 || slot >= gEngineCount || slot >= CE_MULTIBE_MAX || !gIsAux[slot])
		return;
	gAuxCameras[slot] = cam;
	gAuxDeathCam[slot] = false;
}

extern "C" void PortCeMultiBeRefreshSettings(void)
{
	RefreshCachedSettings();
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
		const FVector deathPos = be->GetPos();
		bool shouldRespawn = true;
#ifndef CE_NETWORK_OFF
		if (OnlineMatchActive())
		{
			const SINT lives = GAME.GetPlayerLives(idx + 1);
			if (lives > 0)
			{
				GAME.SetPlayerLives(idx + 1, lives - 1);
			}
			else
			{
				shouldRespawn = false;
				if (WORLD.GetType() == CWorld::kCooperativeMultiplayer)
				{
					if (gAuxCoopLossTicks <= 0)
					{
						gAuxCoopLossTicks = CeMultiBeMatchState::kRespawnTicks;
						gAuxCoopLossReason = (idx == 2) ? P1_OUT_OF_LIVES : P2_OUT_OF_LIVES;
					}
				}
				else if (WORLD.GetType() == CWorld::kVersusMultiplayer)
				{
					CheckOnlineVersusTeamElimination(idx);
				}
			}
		}
#endif
		if (shouldRespawn)
		{
			gMatchState.OnAuxDestroyed(idx);
		}
		else
		{
			if (gMatchState.slotTeam[idx] == CE_MULTIBE_SIDE_MUSPELL)
				gMatchState.forsetiKills++;
			else
				gMatchState.muspellKills++;
		}
		if (gAuxCameras[idx])
		{
			delete gAuxCameras[idx];
			gAuxCameras[idx] = NULL;
		}
		gAuxDeathCam[idx] = false;
#ifndef CE_NETWORK_OFF
		if (OnlineMatchActive())
		{
			const int localSlot = PortCeOnlineMatchLocalSlot ? PortCeOnlineMatchLocalSlot() : 0;
			if (localSlot == idx)
			{
				gAuxCameras[idx] = new (MEMTYPE_CAMERA) CViewPointCamera(deathPos, 0.5f, 0.0f, 6.0f, 3.0f);
				gAuxDeathCam[idx] = (gAuxCameras[idx] != NULL);
			}
		}
#endif
		return 1;	// Suppress retail GAME.DeclarePlayerDead() for auxiliary BEs; respawn cleanly after 5s.
	}
	return 0;
}

extern "C" int PortCeMultiBeIsAuxEngine(const void *battleEnginePtr)
{
	if (!gActive || !battleEnginePtr)
		return 0;
	const int idx = IndexOfEngine(static_cast<const CUnit *>(battleEnginePtr));
	return (idx >= 0 && gIsAux[idx]) ? 1 : 0;
}

extern "C" int PortCeMultiBeActive(void)
{
	return gActive ? 1 : 0;
}

extern "C" int PortCeMultiBeIsAuxDeathCam(int slot)
{
	if (!gActive || slot < 0 || slot >= gEngineCount || slot >= CE_MULTIBE_MAX || !gIsAux[slot])
		return 0;
	return gAuxDeathCam[slot] ? 1 : 0;
}

extern "C" int PortCeMultiBeWinningTeam(void)
{
	return gActive ? gWinningTeam : -1;
}

extern "C" int PortCeMultiBeVersusCheckOutOfLives(int slot)
{
#ifndef CE_NETWORK_OFF
	if (gActive && OnlineMatchActive())
	{
		CheckOnlineVersusTeamElimination(slot);
		return 1;
	}
#else
	(void)slot;
#endif
	return 0;
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
	if (ReadSoloSkirmishSetting() != CE_SOLO_SKIRMISH_SOLO_AI)
		return 0;
	if (FRONTEND.IsRunning())
	{
		const EFrontEndPage cur = FRONTEND.GetCurrentPage();
		const EFrontEndPage dst = FRONTEND.GetTransitionTo();
		const EFrontEndPage src = FRONTEND.GetTransitionFrom();
		const bool onMpPage =
			cur == FEP_MULTIPLAYER || cur == FEP_MULTIPLAYER_START ||
			(cur == FEP_TRANSITION && (dst == FEP_MULTIPLAYER || dst == FEP_MULTIPLAYER_START ||
			                           src == FEP_MULTIPLAYER || src == FEP_MULTIPLAYER_START));
		return onMpPage ? 1 : 0;
	}
	const int curLevel = GAME.GetCurrentLevel();
	return (GAME.IsMultiplayer() && curLevel >= 850 && curLevel <= 899) ? 1 : 0;
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

extern "C" int PortCeSoloControlsActive(void)
{
	return CeMultiBeShouldUseSoloControls(MultiBeFenced(), PortCeSoloSkirmishActive() != 0, PortCeOneScreenMatch()) ? 1 : 0;
}

extern "C" int PortCeBattleEngineIsLocalHuman(const void *be)
{
	const bool faithfulFence = !be || !PortCeLayer() || (PortRecording() && !(PortCeRecordFeaturesAllowed && PortCeRecordFeaturesAllowed())) || PLAYABLE_DEMO ||
	                           CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo ||
	                           CLIPARAMS.mPlaybackDemo || CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode;
	if (faithfulFence)
		return 1;
	const int idx = gActive ? IndexOfEngine(static_cast<const CUnit *>(be)) : -1;
	const bool slotIsAi = (idx >= 0) ? gIsAiControlled[idx] : false;
	const bool soloOrOne = gSoloSkirmish || PortCeOneScreenMatch();
	CPlayer *p0 = GAME.GetPlayer(0);
	const bool matchesSlot0 = (p0 != NULL && p0->GetBattleEngine() != NULL && be == p0->GetBattleEngine());
#ifndef CE_NETWORK_OFF
	const bool onlineActive = OnlineMatchActive() || (CE_SessionActive && CE_SessionActive());
	const int localSlot = (OnlineMatchActive() && PortCeOnlineMatchLocalSlot)
		? PortCeOnlineMatchLocalSlot()
		: (CE_LockstepLocalPlayer ? static_cast<int>(CE_LockstepLocalPlayer()) : 0);
	CPlayer *lp = NULL;
	if (onlineActive && localSlot >= 0 && localSlot < CE_MULTIBE_MAX)
	{
		if (gActive && localSlot < gEngineCount && gIsAux[localSlot])
			lp = gAuxPlayers[localSlot];
		else if (localSlot < GAME.GetNPlayers() && localSlot < MAX_PLAYERS)
			lp = GAME.GetPlayer(localSlot);
	}
	CBattleEngine *localOnlineBe = NULL;
	if (onlineActive && gActive && localSlot >= 0 && localSlot < gEngineCount && gIsAux[localSlot])
		localOnlineBe = gEngines[localSlot];
	else if (lp)
		localOnlineBe = lp->GetBattleEngine();
	else if (onlineActive && gActive && localSlot >= 0 && localSlot < gEngineCount)
		localOnlineBe = gEngines[localSlot];
	const bool matchesLocalOnline = (localOnlineBe != NULL && be == localOnlineBe);
#else
	const bool onlineActive = false;
	const int localSlot = 0;
	const bool matchesLocalOnline = true;
#endif
	return CeMultiBeShouldUseLocalAudio(false, gActive, idx, slotIsAi, onlineActive, localSlot,
	                                    soloOrOne, matchesSlot0, matchesLocalOnline) ? 1 : 0;
}


// ---- Layer-1 AI pilot host bridge (port/game/ai/ce_ai_pilot_l1.h) ----

bool CeMultiBeAiHostActive() { return gActive; }
int CeMultiBeAiHostSlotCount() { return gActive ? gEngineCount : 0; }
CBattleEngine *CeMultiBeAiHostSlotEngine(int slot)
{
	return (gActive && slot >= 0 && slot < gEngineCount && slot < CE_MULTIBE_MAX) ? gEngines[slot] : NULL;
}
bool CeMultiBeAiHostSlotIsAi(int slot)
{
	return gActive && slot >= 0 && slot < gEngineCount && slot < CE_MULTIBE_MAX && gIsAiControlled[slot];
}
bool CeMultiBeAiHostSlotIsAux(int slot)
{
	return gActive && slot >= 0 && slot < gEngineCount && slot < CE_MULTIBE_MAX && gIsAux[slot];
}
CCamera *CeMultiBeAiHostSlotAuxCamera(int slot)
{
	return (gActive && slot >= 0 && slot < gEngineCount && slot < CE_MULTIBE_MAX && gIsAux[slot]) ? gAuxCameras[slot] : NULL;
}
bool CeMultiBeAiHostSlotStartPos(int slot, float out[3])
{
	if (!out || !gActive || slot < 0 || slot >= gEngineCount || slot >= CE_MULTIBE_MAX)
		return false;
	out[0] = gStartPos[slot].X;
	out[1] = gStartPos[slot].Y;
	out[2] = gStartPos[slot].Z;
	return true;
}
bool CeMultiBeAiHostIsSoloSkirmish() { return gActive && gSoloSkirmish; }
bool CeMultiBeAiHostIsPanoramicCam() { return gActive && gConfig.panoramic_cam; }
bool CeMultiBeAiHostIsOnlineMatch()
{
#ifndef CE_NETWORK_OFF
	return gActive && OnlineMatchActive();
#else
	return false;
#endif
}
uint32_t CeMultiBeAiHostMatchSeed()
{
#ifndef CE_NETWORK_OFF
	return (gActive && OnlineMatchActive() && PortCeOnlineMatchSeed) ? PortCeOnlineMatchSeed() : 0u;
#else
	return 0u;
#endif
}
int CeMultiBeAiHostSimTicks() { return gSimTicks; }
void CeMultiBeAiHostNoteWeaponPick() { gAiWeaponPicks++; }
void CeMultiBeAiHostNoteEvade() { gAiEvades++; }
void CeMultiBeAiHostNoteRetreat() { gAiRetreats++; }
void CeMultiBeAiHostNoteDash() { gAiDashes++; }
void CeMultiBeAiHostNoteMorph(bool wasJet, float altitudeAboveGround)
{
	if (wasJet)
	{
		gAiLandings++;
		if (altitudeAboveGround > gMaxAiLandAlt)
			gMaxAiLandAlt = altitudeAboveGround;
	}
	gMatchState.totalMorphs++;
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
extern "C" int PortCeMultiBeResolveStartAllegiance(int, int defaultAllegiance) { return defaultAllegiance; }
extern "C" void PortCeMultiBeAdjustRespawnPos(int, float *, float *, float *, float *, int *) {}
extern "C" int PortCeMultiBeAllowTeamKills(void) { return 0; }
extern "C" int PortCeMultiBeSelectStartConfig(int, int p1Config, int) { return p1Config; }
extern "C" CCamera *PortCeMultiBeAuxCamera(int) { return NULL; }
extern "C" void PortCeMultiBeSetAuxCamera(int, CCamera *) {}
extern "C" void PortCeMultiBeRefreshSettings(void) {}
extern "C" int PortCeMultiBeCareerPrefGet(int, int) { return 0; }
extern "C" void PortCeMultiBeCareerPrefSet(int, int, int) {}
extern "C" void PortCeMultiBeOnConfirmedKill(void *, void *) {}
extern "C" int PortCeMultiBeHandleDeath(void *) { return 0; }
extern "C" int PortCeMultiBeIsAuxEngine(const void *) { return 0; }
extern "C" int PortCeMultiBeActive(void) { return 0; }
extern "C" int PortCeMultiBeIsAuxDeathCam(int) { return 0; }
extern "C" int PortCeMultiBeWinningTeam(void) { return -1; }
extern "C" int PortCeMultiBeVersusCheckOutOfLives(int) { return 0; }
extern "C" int PortCeMultiBePlaySideIsMuspell(void) { return 0; }
extern "C" int PortCeSoloSkirmishActive(void) { return 0; }
extern "C" void PortCeMultiBeDrawMpStartStatus(float) {}
extern "C" int PortCeSoloControlsActive(void) { return 0; }
extern "C" int PortCeBattleEngineIsLocalHuman(const void *) { return 1; }


bool CeMultiBeAiHostActive() { return false; }
int CeMultiBeAiHostSlotCount() { return 0; }
CBattleEngine *CeMultiBeAiHostSlotEngine(int) { return NULL; }
bool CeMultiBeAiHostSlotIsAi(int) { return false; }
bool CeMultiBeAiHostSlotIsAux(int) { return false; }
CCamera *CeMultiBeAiHostSlotAuxCamera(int) { return NULL; }
bool CeMultiBeAiHostSlotStartPos(int, float *) { return false; }
bool CeMultiBeAiHostIsSoloSkirmish() { return false; }
bool CeMultiBeAiHostIsPanoramicCam() { return false; }
bool CeMultiBeAiHostIsOnlineMatch() { return false; }
uint32_t CeMultiBeAiHostMatchSeed() { return 0u; }
int CeMultiBeAiHostSimTicks() { return 0; }
void CeMultiBeAiHostNoteWeaponPick() {}
void CeMultiBeAiHostNoteEvade() {}
void CeMultiBeAiHostNoteRetreat() {}
void CeMultiBeAiHostNoteDash() {}
void CeMultiBeAiHostNoteMorph(bool, float) {}

#endif
