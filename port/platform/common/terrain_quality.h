// SPDX-License-Identifier: GPL-3.0-only
#ifndef PORT_TERRAIN_QUALITY_H
#define PORT_TERRAIN_QUALITY_H

// Main thread only. The caller owns persistence and any session-only feedback.
// Normal is zero; Extended is one. Requests apply to the next complete render,
// so all views use the same policy. The policy allocates no device resources.
extern "C" int CE_TerrainDetailChoice();
extern "C" int CE_TerrainDetailRequest(int value);
enum CeTerrainDetailStatus {
    CE_TERRAIN_DETAIL_PENDING = 0,
    CE_TERRAIN_DETAIL_ACTIVE = 1,
    CE_TERRAIN_DETAIL_RESOURCE_FALLBACK = 2
};
// Describes the requested choice, sampled at the last complete-frame boundary.
// Pending covers a queued change or a level whose reserve has not been tried.
// Fallback means a completed resource check refused Extended and uses Normal.
// This getter never probes resources or changes the current frame's policy.
extern "C" int CE_TerrainDetailStatus();
void CeTerrainQualityBeginFrame();
void CeTerrainQualityEndFrame();
float CeTerrainGeometryDistanceSquared(float distanceSquared);
bool CeTerrainQualityExtended();
bool CeTerrainQualityResourcesAllowed();
// Pool initialization installs this check. The reserve it asks about is prepared
// once, the first time a frame samples the check while Extended is requested, so a
// session that stays on Normal creates nothing; a refused preparation stays refused
// until the next initialization. Shutdown unregisters the check. A missing check is
// pending; a refusing check is an observed fallback. Both keep the requested choice
// and draw Normal.
void CeTerrainQualityResourceCheck(bool (*usable)());

// Measures the CPU render scope only when the bounded diagnostic is enabled.
struct CeTerrainRenderFrameScope {
    CeTerrainRenderFrameScope() { CeTerrainQualityBeginFrame(); }
    ~CeTerrainRenderFrameScope() { CeTerrainQualityEndFrame(); }
};

// True only while a frame boundary samples a request that changed since the last
// sample: the player chose Extended again, or the check was just installed. A
// readiness check may use it to earn one new attempt after a refusal.
bool CeTerrainQualityFreshRequest();

// Optional bounded diagnostics; these observe CPU decisions, not GPU success.
// True only in an admitted CE session that set BEA_PORT_TERRAIN_QUALITY_LOG=1.
bool CeTerrainQualityDiagnostics();
// Counts a request to the extra tier pools that was granted or refused (diagnostics only).
void CeTerrainQualityReserveUse(int level, bool granted);
void CeTerrainQualityPatch(int level);
void CeTerrainQualityFarCell();
void CeTerrainQualityAllocationAttempt(int level);
void CeTerrainQualityAllocationFailure(int level = -1);
void CeTerrainQualityTexture(int level);

#endif
