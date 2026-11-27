// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_policy.cpp: Pure O(1) capability and rule facade (ce_policy.h).
// Keeps all #ifndef CE_NETWORK_OFF checks inside this TU so game patches stay network-neutral.
#include "common.h"
#include "game.h"
#include "DXEngine.h"
#include "world.h"
#include "ce_policy.h"
#include "ce_modes.h"
extern "C" int PortCeRecordFeaturesAllowed(void) __attribute__((weak));

extern bool PortCeLayer();
extern bool PortRecording();
extern bool PortCeMenus();
extern bool PortCeExperimental(const char *mode) __attribute__((weak));
extern bool CeCmdMapOn() __attribute__((weak));
extern bool PortCeOneScreenMatch() __attribute__((weak));
extern bool CeCmdMatch() __attribute__((weak));
extern "C" int PortCeMultiBeActive(void) __attribute__((weak));
extern "C" int PortCeSoloSkirmishActive(void) __attribute__((weak));
extern "C" int PortCeSoloControlsActive(void) __attribute__((weak));
extern "C" int PortCeBattleEngineIsLocalHuman(const void *be) __attribute__((weak));
extern "C" int PortCeMultiBeIsAuxEngine(const void *be) __attribute__((weak));
extern "C" int PortCeCarrierClashBeInStasis(const void *be) __attribute__((weak));
extern "C" int PortCeLivingSandboxBeInStasis(const void *be) __attribute__((weak));
extern "C" int PortCeFlagshipDuelBeInStasis(const void *be) __attribute__((weak));
extern "C" bool PortCeEditorActive(void) __attribute__((weak));
extern "C" bool PortCeMuseumVisitActive(void) __attribute__((weak));
extern "C" bool PortCeCarrierClashActive(void) __attribute__((weak));
extern "C" bool PortCeLivingSandboxActive(void) __attribute__((weak));
extern "C" bool PortCeFlagshipDuelActive(void) __attribute__((weak));

#ifndef CE_NETWORK_OFF
extern "C" int CE_LockstepRequested(void);
extern "C" int CE_LockstepActive(void);
extern "C" int CE_WorldActive(void);
extern "C" int PortCeOnlineMatchActive(void) __attribute__((weak));
#endif

namespace {

struct CeGamePolicyAccess : CGame
{
	static bool FullscreenMultiplayer()
	{
		return static_cast<const CGame &>(GAME).*(&CeGamePolicyAccess::mFullscreenMultiplayer) != FALSE;
	}
};

bool IsOneScreenMatch()
{
	return PortCeOneScreenMatch && PortCeOneScreenMatch();
}

bool IsOnlineSession()
{
#ifndef CE_NETWORK_OFF
	return CeModeActive(CE_MODE_ONLINE_MATCH) ||
	       CE_LockstepRequested() != 0 ||
	       CE_LockstepActive() != 0 ||
	       CE_WorldActive() != 0 ||
	       (PortCeOnlineMatchActive && PortCeOnlineMatchActive() != 0);
#else
	return CeModeActive(CE_MODE_ONLINE_MATCH);
#endif
}

bool IsCustomWorldOwnerActive()
{
	return CeModeActive(CE_MODE_EDITOR) ||
	       CeModeActive(CE_MODE_MUSEUM_VISIT) ||
	       CeModeActive(CE_MODE_BONUS_LEVEL) ||
	       CeModeActive(CE_MODE_CARRIER_CLASH) ||
	       CeModeActive(CE_MODE_LIVING_SANDBOX) ||
	       CeModeActive(CE_MODE_FLAGSHIP_DUEL) ||
	       (PortCeEditorActive && PortCeEditorActive()) ||
	       (PortCeMuseumVisitActive && PortCeMuseumVisitActive()) ||
	       (PortCeCarrierClashActive && PortCeCarrierClashActive()) ||
	       (PortCeLivingSandboxActive && PortCeLivingSandboxActive()) ||
	       (PortCeFlagshipDuelActive && PortCeFlagshipDuelActive());
}

} // namespace

bool CePolicyIsPureSinglePlayerCampaign()
{
	if (!PortCeLayer())
		return !GAME.IsMultiplayer() && GAME.GetNPlayers() <= 1;
	if (GAME.IsMultiplayer() || GAME.GetNPlayers() != 1)
		return false;
	if (IsOnlineSession() || IsOneScreenMatch() || IsCustomWorldOwnerActive())
		return false;
	if (PortCeMultiBeActive && PortCeMultiBeActive())
		return false;
	if (PortCeSoloSkirmishActive && PortCeSoloSkirmishActive())
		return false;
	return true;
}

uint32_t CePolicyActiveToggles()
{
#ifndef CE_VERSION
	return CE_TOGGLE_NONE;
#else
	if (!PortCeLayer())
		return CE_TOGGLE_NONE;
	uint32_t mask = CE_TOGGLE_NONE;
	const bool online = IsOnlineSession();
	const bool solo = (PortCeSoloSkirmishActive && PortCeSoloSkirmishActive() != 0);
	const bool multiBe = (PortCeMultiBeActive && PortCeMultiBeActive() != 0);
	if (solo)
		mask |= CE_TOGGLE_SOLO_SKIRMISH;
	if (multiBe)
		mask |= CE_TOGGLE_MULTI_BE;
	if (solo || multiBe)
		mask |= CE_TOGGLE_AI_PILOTS;
	// Commander & Play-Any-Unit are disallowed under Editor, Museum Visit, recordings, and Online Match v1,
	// and stay strictly gated on their actual feature enablement (0 overhead when OFF).
	const bool recordingFenced = PortRecording() && !(PortCeRecordFeaturesAllowed && PortCeRecordFeaturesAllowed());
	const bool allowTactical = PortCeMenus() && !recordingFenced && !online &&
	                           !CeModeActive(CE_MODE_EDITOR) && !(PortCeEditorActive && PortCeEditorActive()) &&
	                           !CeModeActive(CE_MODE_MUSEUM_VISIT) && !(PortCeMuseumVisitActive && PortCeMuseumVisitActive());
	if (allowTactical)
	{
		const bool singleViewportEligible = !GAME.IsMultiplayer() || IsOneScreenMatch() || solo;
		const bool cmdEnabled = CeCmdMapOn ? CeCmdMapOn() : (PortCeExperimental && PortCeExperimental("commander"));
		const bool playUnitEnabled = PortCeExperimental && PortCeExperimental("play_unit");
		if (CeModeActive(CE_MODE_COMMANDER) || (cmdEnabled && singleViewportEligible))
			mask |= CE_TOGGLE_COMMANDER;
		if (CeModeActive(CE_MODE_PLAY_UNIT) || (playUnitEnabled && singleViewportEligible))
			mask |= CE_TOGGLE_PLAY_UNIT;
	}
	if (!online && (mask & CE_TOGGLE_AI_PILOTS))
		mask |= CE_TOGGLE_AI_STRATEGIST;
	if (!CeModeActive(CE_MODE_MUSEUM_VISIT) && !(PortCeMuseumVisitActive && PortCeMuseumVisitActive()))
		mask |= CE_TOGGLE_FLIGHT;
	return mask;
#endif
}

bool CePolicyIsSplitViewport()
{
#ifndef CE_VERSION
	return GAME.IsMultiplayer() != FALSE;
#else
	if (!PortCeLayer())
		return GAME.IsMultiplayer() != FALSE;
	if (!GAME.IsMultiplayer())
		return false;
	if (IsOneScreenMatch() || IsOnlineSession())
		return false;
	if (PortCeSoloSkirmishActive && PortCeSoloSkirmishActive() != 0)
		return false;
	if (CeCmdMatch && CeCmdMatch() && (!CeCmdMapOn || CeCmdMapOn()))
		return false;
	if (CeGamePolicyAccess::FullscreenMultiplayer())
		return false;
	return true;
#endif
}

int CePolicyCameraSplitAspect()
{
#ifndef CE_VERSION
	return GAME.IsMultiplayer() ? 1 : 0;
#else
	return PortCeCameraSplitAspect();
#endif
}

bool CePolicyUseHalfHeightAspect()
{
	return CePolicyCameraSplitAspect() != 0;
}

bool CePolicyHudScaleEligible()
{
#ifndef CE_VERSION
	return !GAME.IsMultiplayer() && ENGINE.GetNumViewpoints() == 1;
#else
	const bool singleView = !CePolicyIsSplitViewport() && ENGINE.GetNumViewpoints() == 1;
#ifndef CE_NETWORK_OFF
	return CE_LockstepRequested() != 0 || singleView;
#else
	return singleView;
#endif
#endif
}

bool CePolicyFullDetailShading()
{
#ifndef CE_VERSION
	return !GAME.IsMultiplayer();
#else
	if (!PortCeLayer())
		return !GAME.IsMultiplayer();
#ifndef CE_NETWORK_OFF
	// Keep retail multiplayer mirror-pass gating during active lockstep sessions so peer render passes never drift.
	if (CE_LockstepRequested() || CE_LockstepActive())
		return !GAME.IsMultiplayer();
#endif
	return !CePolicyIsSplitViewport();
#endif
}

int CePolicyLandscapeViewCount()
{
#ifndef CE_VERSION
	return GAME.IsMultiplayer() ? 2 : 1;
#else
	if (!PortCeLayer())
		return GAME.IsMultiplayer() ? 2 : 1;
	if (!CePolicyIsSplitViewport())
		return 1;
	int views = GAME.GetNPlayers();
	if (views < 2)
		views = 2;
	if (views > 4)
		views = 4;
	return views;
#endif
}

CeAudioListenerMode CePolicyAudioListenerMode()
{
	return CePolicyIsSplitViewport() ? CE_AUDIO_LISTENER_MULTI_VIEW : CE_AUDIO_LISTENER_SINGLE_LOCAL;
}

bool CePolicyBeIsLocalHuman(const void *be)
{
#ifndef CE_VERSION
	(void)be;
	return true;
#else
	if (!PortCeLayer())
		return true;
	return PortCeBattleEngineIsLocalHuman ? (PortCeBattleEngineIsLocalHuman(be) != 0) : true;
#endif
}

uint8_t CePolicyHumanControllerMask()
{
#ifndef CE_VERSION
	return GAME.IsMultiplayer() ? 0x3u : 0x1u;
#else
	if (!PortCeLayer())
		return GAME.IsMultiplayer() ? 0x3u : 0x1u;
	if (!CePolicyIsSplitViewport())
		return 0x1u;
	int views = ENGINE.GetNumViewpoints();
	if (views < 2)
		views = GAME.GetNPlayers();
	if (views <= 1)
		return 0x1u;
	if (views >= 4)
		return 0xFu;
	return static_cast<uint8_t>((1u << views) - 1u);
#endif
}

bool CePolicyAllowDualBindOnP0()
{
#ifndef CE_VERSION
	return !GAME.IsMultiplayer();
#else
	if (!PortCeLayer())
		return !GAME.IsMultiplayer();
	if (!GAME.IsMultiplayer())
		return true;
	return PortCeSoloControlsActive ? (PortCeSoloControlsActive() != 0) : !CePolicyIsSplitViewport();
#endif
}

bool CePolicyRequireP2Bindings()
{
	if (!PortCeLayer())
		return GAME.IsMultiplayer() != FALSE;
	if (IsOnlineSession() || IsOneScreenMatch() || (PortCeSoloSkirmishActive && PortCeSoloSkirmishActive()))
		return false;
	return GAME.IsMultiplayer() != FALSE;
}

bool CePolicyAllowPause()
{
#ifndef CE_NETWORK_OFF
	if (CE_LockstepActive())
		return false;
#endif
	return true;
}

bool CePolicyAllowRetry()
{
#ifndef CE_NETWORK_OFF
	if (CE_LockstepActive())
		return false;
#endif
	return true;
}

int CePolicyParticleSystemCap()
{
#ifndef CE_VERSION
	return GAME.IsMultiplayer() ? 12 : 8;
#else
	if (!PortCeLayer())
		return GAME.IsMultiplayer() ? 12 : 8;
	if (PortCeMultiBeActive && PortCeMultiBeActive())
		return 18;
	return GAME.IsMultiplayer() ? 12 : 8;
#endif
}

bool CePolicyUseEnhancedMorphAnims()
{
#ifndef CE_VERSION
	return false;
#else
	// Deterministic in CE_MODE_ONLINE_MATCH: never reads local ce.ini preferences.
	return PortCeLayer() && (!PortRecording() || (PortCeRecordFeaturesAllowed && PortCeRecordFeaturesAllowed()));
#endif
}

float CePolicyMorphAnimMultiplier(bool intoWalker, bool airborneTakeoff)
{
	if (!CePolicyUseEnhancedMorphAnims())
		return 1.0f;
	if (intoWalker)
		return 2.0f;
	return airborneTakeoff ? 5.0f : 2.2f;
}

CeDeathAction CePolicyDeathAction(const void *be)
{
#ifndef CE_VERSION
	(void)be;
	return CE_DEATH_RETAIL;
#else
	if (!PortCeLayer() || !be)
		return CE_DEATH_RETAIL;
	if ((PortCeCarrierClashBeInStasis && PortCeCarrierClashBeInStasis(be) != 0) ||
	    (PortCeLivingSandboxBeInStasis && PortCeLivingSandboxBeInStasis(be) != 0) ||
	    (PortCeFlagshipDuelBeInStasis && PortCeFlagshipDuelBeInStasis(be) != 0))
		return CE_DEATH_STASIS_RESCUE;
	if (PortCeMultiBeIsAuxEngine && PortCeMultiBeIsAuxEngine(be) != 0)
		return CE_DEATH_AUX_RESPAWN;
	return CE_DEATH_RETAIL;
#endif
}
