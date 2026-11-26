// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_policy.h: Pure O(1) capability and rule facade for Onslaught CE.
// Safe to call in Boot, FrontEnd, LoadLevel, Sim, and Render. Zero common.h, zero CE_NETWORK_OFF, zero network headers.
// Rule 0: whenever !CE_VERSION or !PortCeLayer(), every predicate returns the exact retail expression.
#ifndef PORT_CE_POLICY_H
#define PORT_CE_POLICY_H

#include <stdint.h>

enum CeMatchToggleMask : uint32_t
{
	CE_TOGGLE_NONE          = 0u,
	CE_TOGGLE_SOLO_SKIRMISH = 1u << 0,	// 1P human vs AI on MP map (single full-screen viewport)
	CE_TOGGLE_MULTI_BE      = 1u << 1,	// 3-4 Battle Engine roster active
	CE_TOGGLE_COMMANDER     = 1u << 2,	// Squad ordering allowed (humans & Layer-2 AI)
	CE_TOGGLE_PLAY_UNIT     = 1u << 3,	// Unit possession allowed (humans & Layer-2 AI)
	CE_TOGGLE_AI_PILOTS     = 1u << 4,	// Layer-1 deterministic AI pilots active on AI slots
	CE_TOGGLE_AI_STRATEGIST = 1u << 5,	// Optional Layer-3 async strategist (offline only in v3.0)
	CE_TOGGLE_FLIGHT        = 1u << 6,	// Walker <-> Jet morphing allowed on current map
};

enum CeAudioListenerMode
{
	CE_AUDIO_LISTENER_SINGLE_LOCAL = 0,
	CE_AUDIO_LISTENER_MULTI_VIEW   = 1,
};

enum CeDeathAction
{
	CE_DEATH_RETAIL        = 0,
	CE_DEATH_AUX_RESPAWN   = 1,
	CE_DEATH_STASIS_RESCUE = 2,
};

// Pure, lock-free, O(1) capability queries. Handles Level 100 Co-op Lockstep
// (NPlayers == 2 && !GAME.IsMultiplayer() && NumViewpoints == 1) explicitly.
bool                 CePolicyIsPureSinglePlayerCampaign();
uint32_t             CePolicyActiveToggles();
bool                 CePolicyIsSplitViewport();
int                  CePolicyCameraSplitAspect();			// 0 = normal (0.75f), 1 = 2P horiz (0.5f), 2 = 2P vert (1.5f)
bool                 CePolicyUseHalfHeightAspect();			// true ONLY on 2P split; false on 1P/3P/4P (governs DXEngine.cpp:689)
bool                 CePolicyHudScaleEligible();			// Replaces 0190's #ifdef CE_NETWORK_OFF leak in Hud.cpp
bool                 CePolicyFullDetailShading();			// true on 1P & local single-viewport MP; false on split-screen
int                  CePolicyLandscapeViewCount();			// active view count (1..4); retail 1/2 when !PortCeLayer()
CeAudioListenerMode  CePolicyAudioListenerMode();			// SINGLE_LOCAL on 1P/Solo/Online; MULTI_VIEW on split-screen
bool                 CePolicyBeIsLocalHuman(const void *be);
uint8_t              CePolicyHumanControllerMask();			// 0x1 on 1P/Solo/Online; 0x3..0xF on split-screen
bool                 CePolicyAllowDualBindOnP0();
bool                 CePolicyRequireP2Bindings();
bool                 CePolicyAllowPause();
bool                 CePolicyAllowRetry();
int                  CePolicyParticleSystemCap();			// 8 (SP), 12 (2P), 18 (Multi-BE)
bool                 CePolicyUseEnhancedMorphAnims();		// Covers all 4 hunks of 0388; deterministic in CE_MODE_ONLINE_MATCH
float                CePolicyMorphAnimMultiplier(bool intoWalker, bool airborneTakeoff);
CeDeathAction        CePolicyDeathAction(const void *be);

#endif
