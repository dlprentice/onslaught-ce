// SPDX-License-Identifier: GPL-3.0-only
// Clean-room C++14 Persistent 512m Combined-Arms Battle Director for Living
// Sandbox: 2 Hz host-driven tug-of-war state machine, suppressible/regenerating
// HQ cores, population cap with 15% hysteresis & pressure downshift, stuck-unit
// reaper (>90 s), periodic regroup lull for heap coalescing, Citadel respawn +
// Play-Any-Unit stasis law, timed Naval/Carrier Escalation, and FONT_SMALL-safe
// 9-slice HUD layout.
// Pure C++14 header shared by game builds and standalone ASan/UBSan host soaks.
#ifndef CE_SANDBOX_DIRECTOR_H
#define CE_SANDBOX_DIRECTOR_H

#include "ce_sandbox_world.h"
#include "ce_entity_handles.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum CeSandboxPhase {
	CE_SB_INACTIVE = 0,
	CE_SB_ARMED,
	CE_SB_ACTIVE,
};

enum CeSandboxOwner {
	CE_SB_OWNER_NEUTRAL = 0,
	CE_SB_OWNER_FORSETI = 1,
	CE_SB_OWNER_MUSPELL = 2,
};

enum CeSandboxUnitRole {
	CE_SB_ROLE_LINE = 0,
	CE_SB_ROLE_AIR,
	CE_SB_ROLE_NAVAL,
	CE_SB_ROLE_HQ,
	CE_SB_ROLE_TURRET,
};

struct CeSandboxDirectorConfig {
	int targetUnitCap;           // 180-220 under 16 MiB sandbox heap, 96 under retail heap
	int minUnitCap;              // floor under severe frame/heap pressure
	float hysteresisFraction;    // 0.15f -> resume waves at <= 85% of effective cap
	float directorStepSec;       // 0.5f -> 2 Hz host-driven cadence (zero event timers)
	float captureRadius;         // 28.0f m around each capture node
	float captureRatePerUnit;    // control points per second per net unit
	float hqRebuildSec;          // 60.0f s suppressed HQ regeneration
	float stuckReapSec;          // 90.0f s without >= 2 m travel or combat
	float stuckMoveEps;          // 2.0f m
	float regroupIntervalSec;    // 900.0f s (15 minutes)
	float regroupDurationSec;    // 25.0f s diegetic regroup lull
	float navalIntervalSec;      // 300.0f s (5 minutes)
	float waveIntervalSec;       // 8.0f s reinforcement cadence
	float frameDownshiftMs;      // 25.0f ms frame-time pressure threshold
	float frameRecoverMs;        // 18.0f ms frame-time recovery threshold
	uint32_t minThingHeapFreeBytes; // 768 KiB live mThingHeap safety floor
	float maxEventLoadRatio;     // 0.50f -> 50% CEventManager capacity alarm
};

inline CeSandboxDirectorConfig CeSandboxDefaultConfig(bool expandedSandboxHeap) {
	CeSandboxDirectorConfig c = {};
	c.targetUnitCap = expandedSandboxHeap ? 192 : 96;
	c.minUnitCap = 48;
	c.hysteresisFraction = 0.15f;
	c.directorStepSec = 0.5f;
	c.captureRadius = 28.0f;
	c.captureRatePerUnit = 3.2f;
	c.hqRebuildSec = 60.0f;
	c.stuckReapSec = 90.0f;
	c.stuckMoveEps = 2.0f;
	c.regroupIntervalSec = 900.0f;
	c.regroupDurationSec = 25.0f;
	c.navalIntervalSec = 300.0f;
	c.waveIntervalSec = 8.0f;
	c.frameDownshiftMs = 25.0f;
	c.frameRecoverMs = 18.0f;
	c.minThingHeapFreeBytes = 768u * 1024u;
	c.maxEventLoadRatio = 0.50f;
	return c;
}

struct CeSandboxPressure {
	float frameTimeMs;
	int liveWorldUnits;
	int eventPending;
	int eventCapacity;
	uint32_t thingHeapFreeBytes;
	bool venturerAvailable;
};

struct CeSandboxNodePresence {
	int forsetiWeight[CeSandboxWorld::kCaptureNodeCount];
	int muspellWeight[CeSandboxWorld::kCaptureNodeCount];
};

struct CeSandboxSpawnOrder {
	const char *unitName;
	int allegiance;
	CeSandboxUnitRole role;
	float x, y, z;
	float yaw;
	float targetX, targetY, targetZ;
	bool isHqRebuild;
	bool isEscalation;
};

struct CeSandboxMoveOrder {
	CeEntityHandle handle;
	float x, y, z;
};

struct CeSandboxReapOrder {
	CeEntityHandle handle;
};

struct CeSandboxStepOutput {
	static constexpr int kMaxSpawns = 12;
	static constexpr int kMaxMoves = 24;
	static constexpr int kMaxReaps = 16;

	CeSandboxSpawnOrder spawns[kMaxSpawns];
	int spawnCount;
	CeSandboxMoveOrder moves[kMaxMoves];
	int moveCount;
	CeSandboxReapOrder reaps[kMaxReaps];
	int reapCount;
	bool requestHeapCoalesce;
};

struct CeSandboxTrackedSample {
	CeEntityHandle handle;
	float x, y, z;
	float life;
	bool inCombat;
	bool alive;
};

struct CeSandboxHudState {
	bool active;
	int forsetiNodes;
	int muspellNodes;
	int contestedNodes;
	int liveUnits;
	int effectiveCap;
	bool regroupActive;
	bool navalEscalationActive;
	bool forsetiHqSuppressed;
	bool muspellHqSuppressed;
	bool p0StasisActive;
	bool pressureThrottled;
	float nodeControl[CeSandboxWorld::kCaptureNodeCount]; // [-100..+100]
	CeSandboxOwner nodeOwner[CeSandboxWorld::kCaptureNodeCount];
	int activeFrontNode;
	double elapsedSec;
	char headerLeft[32];
	char headerMid[40];
	char headerRight[32];
	char bannerText[64];
	bool showBanner;
};

// FONT_SMALL lacks ~ \ _ $ @ [ ] ^ { | } — filter strictly so HUD text never
// emits missing-glyph boxes or silent gaps.
inline bool CeSandboxIsFontSmallSafeChar(char c) {
	const unsigned char u = static_cast<unsigned char>(c);
	if (u < 32u || u >= 127u) return false;
	switch (c) {
	case '[': case ']': case '|': case '_': case '~':
	case '\\': case '$': case '@': case '^': case '{': case '}':
		return false;
	default:
		return true;
	}
}

inline bool CeSandboxIsFontSmallSafeText(const char *text) {
	if (!text) return false;
	for (const char *p = text; *p; ++p) {
		if (!CeSandboxIsFontSmallSafeChar(*p)) return false;
	}
	return true;
}

inline void CeSandboxSanitizeHudText(const char *src, char *dst, size_t dstSize) {
	if (!dst || dstSize == 0u) return;
	if (!src) { dst[0] = '\0'; return; }
	size_t out = 0u;
	for (size_t i = 0u; src[i] && out + 1u < dstSize; ++i) {
		dst[out++] = CeSandboxIsFontSmallSafeChar(src[i]) ? src[i] : ' ';
	}
	dst[out] = '\0';
}

struct CeSandboxHudRect {
	float x, y, w, h;
};

struct CeSandboxHudLayout {
	bool valid;
	float scale;
	float cornerRadius;
	float strokeWidth;
	float glowPad;
	CeSandboxHudRect panel;
	CeSandboxHudRect nodePips[CeSandboxWorld::kCaptureNodeCount];
	CeSandboxHudRect bannerBox;
	CeSandboxHudRect cueBox;
	bool showBanner;
	bool showCue;
};

inline CeSandboxHudLayout CeSandboxComputeHudLayout(float viewportW, float viewportH,
                                                    float uiScale, bool showBanner, bool showCue) {
	CeSandboxHudLayout out = {};
	if (!(viewportW >= 320.0f && viewportH >= 240.0f && uiScale > 0.25f)) return out;
	const float s = uiScale;
	const float safeX = floorf(viewportW * 0.035f);
	const float safeTop = floorf(viewportH * 0.032f);
	const float safeBot = floorf(viewportH * 0.045f);
	const float maxPanelW = viewportW - 2.0f * safeX;

	float panelW = floorf(480.0f * s);
	if (panelW > maxPanelW) panelW = maxPanelW;
	if (panelW < 280.0f) panelW = 280.0f;
	const float panelH = floorf(46.0f * s);
	const float panelX = floorf((viewportW - panelW) * 0.5f);
	const float panelY = safeTop > floorf(12.0f * s) ? safeTop : floorf(12.0f * s);

	float r = floorf(13.0f * s);
	if (r > panelH * 0.45f) r = floorf(panelH * 0.45f);
	if (r < 4.0f) r = 4.0f;
	float glow = floorf(1.5f * s + 0.5f);
	if (glow < 1.0f) glow = 1.0f;
	float stroke = floorf(2.0f * s + 0.5f);
	if (stroke < 1.5f) stroke = 1.5f;

	const float padX = floorf(18.0f * s);
	const float pipGap = floorf(8.0f * s);
	const float pipAreaW = panelW - 2.0f * padX;
	const float pipW = floorf((pipAreaW - pipGap * float(CeSandboxWorld::kCaptureNodeCount - 1)) /
	                          float(CeSandboxWorld::kCaptureNodeCount));
	const float pipH = floorf(10.0f * s);
	const float pipY = panelY + panelH - pipH - floorf(7.0f * s);
	const float totalPipsW = pipW * float(CeSandboxWorld::kCaptureNodeCount) +
	                         pipGap * float(CeSandboxWorld::kCaptureNodeCount - 1);
	const float pipStartX = floorf(panelX + (panelW - totalPipsW) * 0.5f);

	out.valid = true;
	out.scale = s;
	out.cornerRadius = r;
	out.strokeWidth = stroke;
	out.glowPad = glow;
	out.panel = {panelX, panelY, panelW, panelH};
	for (unsigned i = 0; i < CeSandboxWorld::kCaptureNodeCount; ++i) {
		out.nodePips[i] = {pipStartX + float(i) * (pipW + pipGap), pipY, pipW, pipH};
	}

	out.showBanner = showBanner;
	if (showBanner) {
		float bw = floorf(390.0f * s);
		if (bw > maxPanelW) bw = maxPanelW;
		const float bh = floorf(30.0f * s);
		out.bannerBox = {floorf((viewportW - bw) * 0.5f), panelY + panelH + floorf(8.0f * s), bw, bh};
	}

	out.showCue = showCue;
	if (showCue) {
		float cw = floorf(420.0f * s);
		if (cw > maxPanelW) cw = maxPanelW;
		const float ch = floorf(56.0f * s);
		out.cueBox = {floorf((viewportW - cw) * 0.5f), viewportH - safeBot - ch, cw, ch};
	}
	return out;
}

// Pure P0 survival & Play-Any-Unit Battle Engine stasis decision rule.
struct CeSandboxPlayerStatus {
	bool alive;
	bool inWater;
	float life;
	float maxLife;
	bool drivingOtherUnit; // PortCePlayUnitShowsBattleline(0) != 0
};

struct CeSandboxPlayerAction {
	bool engageStasisShield;
	bool recallToCitadel;
};

inline CeSandboxPlayerAction CeSandboxEvaluatePlayerProtection(const CeSandboxPlayerStatus &st) {
	CeSandboxPlayerAction act = {};
	if (st.drivingOtherUnit) {
		act.engageStasisShield = true;
		return act;
	}
	const float critThreshold = (st.maxLife > 0.0f) ? (st.maxLife * 0.08f) : 8.0f;
	const float minCrit = critThreshold > 8.0f ? critThreshold : 8.0f;
	if (!st.alive || st.inWater || st.life <= minCrit) {
		act.recallToCitadel = true;
	}
	return act;
}

class CeSandboxDirector {
public:
	static constexpr unsigned kMaxTrackedUnits = 512u;

	struct TrackedUnit {
		CeEntityHandle handle;
		int allegiance;
		CeSandboxUnitRole role;
		float anchorX, anchorY;
		float lastLife;
		float stuckSec;
		float ageSec;
		float orderCooldownSec;
	};

	struct HqCoreState {
		CeEntityHandle handle;
		bool seen;
		bool alive;
		bool suppressed;
		float rebuildTimerSec;
		uint32_t suppressCount;
		uint32_t rebuildCount;
	};

	struct Stats {
		uint64_t directorSteps;
		uint64_t totalSpawns;
		uint64_t totalDeaths;
		uint64_t totalStuckReaps;
		uint64_t totalRegroupLulls;
		uint64_t totalHeapCoalesces;
		uint64_t totalNavalEscalations;
		uint64_t totalNodeCaptures;
		uint64_t totalHqSuppressions;
		uint64_t totalHqRebuilds;
		uint64_t totalPlayerRecalls;
		uint64_t pressureThrottleSteps;
		int peakLiveUnits;
		int peakTrackedUnits;
	};

private:
	CeSandboxPhase phase_;
	CeSandboxDirectorConfig cfg_;
	double elapsedSec_;
	float stepAccumSec_;
	float waveTimerSec_;
	float regroupTimerSec_;
	float regroupRemainSec_;
	bool regroupCoalesced_;
	float navalTimerSec_;
	float bannerRemainSec_;
	char bannerText_[64];

	float nodeControl_[CeSandboxWorld::kCaptureNodeCount];
	CeSandboxOwner nodeOwner_[CeSandboxWorld::kCaptureNodeCount];

	HqCoreState forsetiHq_;
	HqCoreState muspellHq_;
	CeEntityHandle navalForsetiFlagship_;
	CeEntityHandle navalMuspellFlagship_;

	TrackedUnit tracked_[kMaxTrackedUnits];
	unsigned trackedCount_;

	int effectiveCap_;
	int lastLiveUnits_;
	float emaFrameMs_;
	bool hysteresisPaused_;
	bool pressureThrottled_;
	bool p0StasisActive_;
	uint32_t waveSeq_;
	Stats stats_;

	void SetBanner(const char *raw, float durationSec = 5.5f) {
		CeSandboxSanitizeHudText(raw, bannerText_, sizeof(bannerText_));
		bannerRemainSec_ = durationSec;
	}

	int FindTrackedIndex(CeEntityHandle h) const {
		if (!CeEntityHandleIsValid(h)) return -1;
		for (unsigned i = 0; i < trackedCount_; ++i) {
			if (tracked_[i].handle == h) return static_cast<int>(i);
		}
		return -1;
	}

	void RemoveTrackedAt(unsigned idx) {
		if (idx >= trackedCount_) return;
		tracked_[idx] = tracked_[--trackedCount_];
	}

	static float ComputeYaw(float fromX, float fromY, float toX, float toY) {
		const float dx = toX - fromX;
		const float dy = toY - fromY;
		if (dx * dx + dy * dy < 1e-4f) return 0.0f;
		// Engine forward (+Y) rotates to (-sin(yaw), cos(yaw)).
		return atan2f(-dx, dy);
	}

	static bool IsAirDef(const char *name) {
		using namespace CeSandboxWorld;
		return StrEqualsIgnoreCase(name, kUnitVenom) ||
		       StrEqualsIgnoreCase(name, kUnitWildcatJason) ||
		       StrEqualsIgnoreCase(name, kUnitWildcatBilly) ||
		       StrEqualsIgnoreCase(name, kUnitWildcatTara) ||
		       StrEqualsIgnoreCase(name, kUnitMonitorBomber) ||
		       StrEqualsIgnoreCase(name, kUnitMuspellFighter) ||
		       StrEqualsIgnoreCase(name, kUnitMuspellAdvFighter) ||
		       StrEqualsIgnoreCase(name, kUnitEnforcer) ||
		       StrEqualsIgnoreCase(name, kUnitSkyTyrant);
	}

	static bool IsNavalDef(const char *name) {
		using namespace CeSandboxWorld;
		return StrEqualsIgnoreCase(name, kUnitMarshall) ||
		       StrEqualsIgnoreCase(name, kUnitVenturer) ||
		       StrEqualsIgnoreCase(name, kUnitManticore);
	}

	static bool IsTurretDef(const char *name) {
		using namespace CeSandboxWorld;
		return StrEqualsIgnoreCase(name, kUnitFlakTurret) ||
		       StrEqualsIgnoreCase(name, kUnitCannonTurret) ||
		       StrEqualsIgnoreCase(name, kUnitSamSite);
	}

	static bool IsHqDef(const char *name) {
		using namespace CeSandboxWorld;
		return StrEqualsIgnoreCase(name, kUnitForsetiHq) ||
		       StrEqualsIgnoreCase(name, kUnitMuspellHq);
	}

	void ChooseObjectiveFor(int allegiance, float &outX, float &outY, float &outZ) const {
		using namespace CeSandboxWorld;
		const CeSandboxOwner mine = (allegiance == kAllegianceForseti) ? CE_SB_OWNER_FORSETI : CE_SB_OWNER_MUSPELL;
		const float hqX = (allegiance == kAllegianceForseti) ? kForsetiHqX : kMuspellHqX;
		const float hqY = (allegiance == kAllegianceForseti) ? kForsetiHqY : kMuspellHqY;

		int bestNode = -1;
		float bestDist2 = 1e30f;
		for (unsigned i = 0; i < kCaptureNodeCount; ++i) {
			if (nodeOwner_[i] == mine) continue;
			const CaptureNodeSpec &ns = CaptureNode(i);
			const float dx = ns.x - hqX;
			const float dy = ns.y - hqY;
			const float d2 = dx * dx + dy * dy;
			if (d2 < bestDist2) {
				bestDist2 = d2;
				bestNode = static_cast<int>(i);
			}
		}
		if (bestNode >= 0) {
			const CaptureNodeSpec &ns = CaptureNode(static_cast<unsigned>(bestNode));
			outX = ns.x;
			outY = ns.y;
		} else {
			// All 5 nodes held: push against enemy HQ core!
			outX = (allegiance == kAllegianceForseti) ? kMuspellHqX : kForsetiHqX;
			outY = (allegiance == kAllegianceForseti) ? kMuspellHqY : kForsetiHqY;
		}
		ClampOutsideCitadelKeepOut(outX, outY, kDirectorSteerClearRadius);
		outZ = HeightAt(outX, outY);
	}

	void ChooseStagingPoint(int allegiance, unsigned slot, bool air, float &outX, float &outY, float &outZ) const {
		using namespace CeSandboxWorld;
		// Prefer staging near the furthest securely owned rear node, else Main Base.
		float baseX = (allegiance == kAllegianceForseti) ? kForsetiHqX + 24.0f : kMuspellHqX - 24.0f;
		float baseY = (allegiance == kAllegianceForseti) ? kForsetiHqY + 18.0f : kMuspellHqY - 18.0f;
		if (allegiance == kAllegianceForseti && nodeOwner_[0] == CE_SB_OWNER_FORSETI && (slot & 1u)) {
			baseX = CaptureNode(0).x - 12.0f;
			baseY = CaptureNode(0).y - 10.0f;
		} else if (allegiance == kAllegianceMuspell && nodeOwner_[4] == CE_SB_OWNER_MUSPELL && (slot & 1u)) {
			baseX = CaptureNode(4).x + 12.0f;
			baseY = CaptureNode(4).y + 10.0f;
		}
		const float jitterX = float(static_cast<int>(slot % 5u) - 2) * 7.5f;
		const float jitterY = float(static_cast<int>((slot / 5u) % 3u) - 1) * 7.5f;
		outX = baseX + jitterX;
		outY = baseY + jitterY;
		if (outX < 48.0f) outX = 48.0f;
		if (outX > 464.0f) outX = 464.0f;
		if (outY < 48.0f) outY = 48.0f;
		if (outY > 464.0f) outY = 464.0f;
		ClampOutsideCitadelKeepOut(outX, outY, kDirectorSteerClearRadius);
		outZ = air ? -48.0f : HeightAt(outX, outY);
	}

	bool EmitSpawn(CeSandboxStepOutput &out, const CeSandboxSpawnOrder &order) {
		if (out.spawnCount >= CeSandboxStepOutput::kMaxSpawns) return false;
		CeSandboxSpawnOrder safe = order;
		CeSandboxWorld::ClampOutsideCitadelKeepOut(safe.x, safe.y, CeSandboxWorld::kDirectorSteerClearRadius);
		CeSandboxWorld::ClampOutsideCitadelKeepOut(safe.targetX, safe.targetY, CeSandboxWorld::kDirectorSteerClearRadius);
		if (safe.role == CE_SB_ROLE_LINE || safe.role == CE_SB_ROLE_HQ || safe.role == CE_SB_ROLE_TURRET) {
			safe.z = CeSandboxWorld::HeightAt(safe.x, safe.y);
		}
		out.spawns[out.spawnCount++] = safe;
		++stats_.totalSpawns;
		return true;
	}

	void SuppressHq(int allegiance) {
		HqCoreState &hq = (allegiance == CeSandboxWorld::kAllegianceForseti) ? forsetiHq_ : muspellHq_;
		if (hq.suppressed) return;
		hq.alive = false;
		hq.suppressed = true;
		hq.rebuildTimerSec = cfg_.hqRebuildSec;
		++hq.suppressCount;
		++stats_.totalHqSuppressions;

		// Momentum surge along the 5-node tug-of-war front (never triggers game over).
		const float delta = (allegiance == CeSandboxWorld::kAllegianceMuspell) ? 18.0f : -18.0f;
		for (unsigned i = 0; i < CeSandboxWorld::kCaptureNodeCount; ++i) {
			nodeControl_[i] += delta;
			if (nodeControl_[i] > 100.0f) nodeControl_[i] = 100.0f;
			if (nodeControl_[i] < -100.0f) nodeControl_[i] = -100.0f;
		}
		SetBanner(allegiance == CeSandboxWorld::kAllegianceMuspell
		              ? "MUSPELL HQ SUPPRESSED - REBUILDING 60S"
		              : "FORSETI HQ SUPPRESSED - REBUILDING 60S",
		          6.0f);
	}

	void EmitNavalEscalation(CeSandboxStepOutput &out, bool venturerAvailable) {
		using namespace CeSandboxWorld;
		const char *forsetiShip = venturerAvailable ? kUnitVenturer : kUnitMarshall;
		const float forsetiZ = venturerAvailable ? -30.0f : kWaterLevel;
		int spawned = 0;

		if (FindTrackedIndex(navalForsetiFlagship_) < 0) {
			if (EmitSpawn(out, {
				forsetiShip, kAllegianceForseti, CE_SB_ROLE_NAVAL,
				352.0f, 118.0f, forsetiZ, -1.35f,
				418.0f, 136.0f, forsetiZ, false, true
			})) ++spawned;
			if (EmitSpawn(out, {
				kUnitWildcatJason, kAllegianceForseti, CE_SB_ROLE_AIR,
				346.0f, 134.0f, -48.0f, -1.20f,
				432.0f, 148.0f, -48.0f, false, true
			})) ++spawned;
		}
		if (FindTrackedIndex(navalMuspellFlagship_) < 0) {
			if (EmitSpawn(out, {
				kUnitManticore, kAllegianceMuspell, CE_SB_ROLE_NAVAL,
				444.0f, 154.0f, kWaterLevel, 1.85f,
				372.0f, 132.0f, kWaterLevel, false, true
			})) ++spawned;
			if (EmitSpawn(out, {
				kUnitMuspellAdvFighter, kAllegianceMuspell, CE_SB_ROLE_AIR,
				448.0f, 166.0f, -50.0f, 1.90f,
				360.0f, 124.0f, -50.0f, false, true
			})) ++spawned;
		}
		if (spawned > 0) {
			++stats_.totalNavalEscalations;
			SetBanner("NAVAL ESCALATION - COASTAL STRAIT ENGAGED", 6.5f);
		}
	}

	void StepDirector2Hz(float stepDt, const CeSandboxPressure &pressure,
	                     const CeSandboxNodePresence &presence,
	                     const CeSandboxTrackedSample *samples, unsigned sampleCount,
	                     CeSandboxStepOutput &out) {
		using namespace CeSandboxWorld;
		++stats_.directorSteps;
		elapsedSec_ += static_cast<double>(stepDt);
		lastLiveUnits_ = pressure.liveWorldUnits;
		if (pressure.liveWorldUnits > stats_.peakLiveUnits)
			stats_.peakLiveUnits = pressure.liveWorldUnits;

		if (bannerRemainSec_ > 0.0f) {
			bannerRemainSec_ -= stepDt;
			if (bannerRemainSec_ <= 0.0f) {
				bannerRemainSec_ = 0.0f;
				bannerText_[0] = '\0';
			}
		}

		// 1. Frame-time EMA & adaptive effective unit cap downshift/recovery.
		if (pressure.frameTimeMs > 0.0f && pressure.frameTimeMs < 500.0f) {
			emaFrameMs_ = emaFrameMs_ * 0.80f + pressure.frameTimeMs * 0.20f;
		}
		if (emaFrameMs_ > cfg_.frameDownshiftMs) {
			const int reduced = (cfg_.targetUnitCap * 3) / 4;
			effectiveCap_ = reduced < cfg_.minUnitCap ? cfg_.minUnitCap : reduced;
		} else if (emaFrameMs_ < cfg_.frameRecoverMs && effectiveCap_ < cfg_.targetUnitCap) {
			effectiveCap_ += 4;
			if (effectiveCap_ > cfg_.targetUnitCap) effectiveCap_ = cfg_.targetUnitCap;
		}

		const bool eventAlarm = (pressure.eventCapacity > 0) &&
		                        (static_cast<float>(pressure.eventPending) >=
		                         static_cast<float>(pressure.eventCapacity) * cfg_.maxEventLoadRatio);
		const bool heapAlarm = (pressure.thingHeapFreeBytes < cfg_.minThingHeapFreeBytes);
		pressureThrottled_ = eventAlarm || heapAlarm;
		if (pressureThrottled_) ++stats_.pressureThrottleSteps;

		// 2. Hysteresis gate: pause waves at >= effectiveCap, resume at <= 85%.
		const int resumeCap = static_cast<int>(static_cast<float>(effectiveCap_) * (1.0f - cfg_.hysteresisFraction));
		if (pressure.liveWorldUnits >= effectiveCap_) {
			hysteresisPaused_ = true;
		} else if (pressure.liveWorldUnits <= resumeCap) {
			hysteresisPaused_ = false;
		}

		// 3. Update 5-node tug-of-war capture state machine.
		for (unsigned i = 0; i < kCaptureNodeCount; ++i) {
			int diff = presence.forsetiWeight[i] - presence.muspellWeight[i];
			if (diff > 6) diff = 6;
			if (diff < -6) diff = -6;
			if (diff != 0) {
				nodeControl_[i] += static_cast<float>(diff) * cfg_.captureRatePerUnit * stepDt;
				if (nodeControl_[i] > 100.0f) nodeControl_[i] = 100.0f;
				if (nodeControl_[i] < -100.0f) nodeControl_[i] = -100.0f;
			}
			const CeSandboxOwner prev = nodeOwner_[i];
			CeSandboxOwner next = prev;
			if (nodeControl_[i] >= 50.0f) next = CE_SB_OWNER_FORSETI;
			else if (nodeControl_[i] <= -50.0f) next = CE_SB_OWNER_MUSPELL;
			else if (prev == CE_SB_OWNER_FORSETI && nodeControl_[i] < 15.0f) next = CE_SB_OWNER_NEUTRAL;
			else if (prev == CE_SB_OWNER_MUSPELL && nodeControl_[i] > -15.0f) next = CE_SB_OWNER_NEUTRAL;

			if (next != prev) {
				nodeOwner_[i] = next;
				if (next != CE_SB_OWNER_NEUTRAL) {
					++stats_.totalNodeCaptures;
					char msg[64] = {};
					snprintf(msg, sizeof(msg), "%s CAPTURED %s",
					         next == CE_SB_OWNER_FORSETI ? "FORSETI" : "MUSPELL",
					         CaptureNode(i).name);
					SetBanner(msg, 5.0f);
				}
			}
		}

		// 4. HQ suppression & 60 s regeneration timers (never ends the sandbox).
		auto tickHq = [&](HqCoreState &hq, int allegiance, float hx, float hy, float hyaw) {
			if (!hq.suppressed) return;
			hq.rebuildTimerSec -= stepDt;
			if (hq.rebuildTimerSec <= 0.0f && !heapAlarm) {
				if (!EmitSpawn(out, {
					allegiance == kAllegianceForseti ? kUnitForsetiHq : kUnitMuspellHq,
					allegiance, CE_SB_ROLE_HQ,
					hx, hy, HeightAt(hx, hy), hyaw,
					hx, hy, HeightAt(hx, hy), true, false
				})) {
					hq.rebuildTimerSec = 2.0f;
					return;
				}
				hq.suppressed = false;
				hq.rebuildTimerSec = 0.0f;
				++hq.rebuildCount;
				++stats_.totalHqRebuilds;
				SetBanner(allegiance == kAllegianceForseti ? "FORSETI HQ ONLINE" : "MUSPELL HQ ONLINE", 5.0f);
			}
		};
		tickHq(forsetiHq_, kAllegianceForseti, kForsetiHqX, kForsetiHqY, -0.7854f);
		tickHq(muspellHq_, kAllegianceMuspell, kMuspellHqX, kMuspellHqY,  2.3562f);

		// 5. Update tracked unit mobility / stuck-unit reaper (>90 s) and issue front orders.
		float forsetiObjX = 256.0f, forsetiObjY = 256.0f, forsetiObjZ = kValleyFloorZ;
		float muspellObjX = 256.0f, muspellObjY = 256.0f, muspellObjZ = kValleyFloorZ;
		ChooseObjectiveFor(kAllegianceForseti, forsetiObjX, forsetiObjY, forsetiObjZ);
		ChooseObjectiveFor(kAllegianceMuspell, muspellObjX, muspellObjY, muspellObjZ);

		for (unsigned s = 0; s < sampleCount && samples; ++s) {
			const CeSandboxTrackedSample &sm = samples[s];
			const int idx = FindTrackedIndex(sm.handle);
			if (idx < 0) continue;
			TrackedUnit &tu = tracked_[idx];
			if (!sm.alive) {
				RemoveTrackedAt(static_cast<unsigned>(idx));
				continue;
			}
			tu.ageSec += stepDt;
			if (tu.orderCooldownSec > 0.0f) tu.orderCooldownSec -= stepDt;

			const bool exemptFromReap = (tu.role == CE_SB_ROLE_HQ || tu.role == CE_SB_ROLE_TURRET);
			const float dx = sm.x - tu.anchorX;
			const float dy = sm.y - tu.anchorY;
			const bool movedEnough = (dx * dx + dy * dy) >= (cfg_.stuckMoveEps * cfg_.stuckMoveEps);
			const bool tookDamage = (sm.life < tu.lastLife - 0.5f);
			tu.lastLife = sm.life;

			if (movedEnough || sm.inCombat || tookDamage || exemptFromReap) {
				tu.anchorX = sm.x;
				tu.anchorY = sm.y;
				tu.stuckSec = 0.0f;
			} else {
				tu.stuckSec += stepDt;
				// Reap stuck units (>90 s) — or earlier (>45 s) during heap pressure / regroup lull.
				const float reapLimit = (heapAlarm || regroupRemainSec_ > 0.0f)
				                            ? (cfg_.stuckReapSec * 0.5f)
				                            : cfg_.stuckReapSec;
				if (tu.stuckSec >= reapLimit && out.reapCount < CeSandboxStepOutput::kMaxReaps) {
					out.reaps[out.reapCount++] = {tu.handle};
					++stats_.totalStuckReaps;
					RemoveTrackedAt(static_cast<unsigned>(idx));
					continue;
				}
			}

			// Steer units caught near the Sanctuary keep-out zone or needing a frontline waypoint.
			if (!exemptFromReap && tu.orderCooldownSec <= 0.0f && out.moveCount < CeSandboxStepOutput::kMaxMoves) {
				if (IsInsideCitadelKeepOut(sm.x, sm.y, kDirectorKeepOutRadius)) {
					float safeX = sm.x, safeY = sm.y;
					ClampOutsideCitadelKeepOut(safeX, safeY, kDirectorSteerClearRadius + 8.0f);
					out.moves[out.moveCount++] = {
						tu.handle, safeX, safeY,
						tu.role == CE_SB_ROLE_AIR ? -48.0f : HeightAt(safeX, safeY)
					};
					tu.orderCooldownSec = 6.0f;
				} else if (tu.role == CE_SB_ROLE_NAVAL) {
					// Patrol SE coastal naval strait between (355..445, 95..170)
					const float tx = (tu.allegiance == kAllegianceForseti) ? 426.0f : 362.0f;
					const float ty = 132.0f + (((waveSeq_ + s) & 1u) ? 20.0f : -18.0f);
					out.moves[out.moveCount++] = {tu.handle, tx, ty, sm.z};
					tu.orderCooldownSec = 18.0f;
				} else if (!sm.inCombat) {
					float tx = (tu.allegiance == kAllegianceForseti) ? forsetiObjX : muspellObjX;
					float ty = (tu.allegiance == kAllegianceForseti) ? forsetiObjY : muspellObjY;
					const float spreadX = float(static_cast<int>((s + waveSeq_) % 5u) - 2) * 8.0f;
					const float spreadY = float(static_cast<int>(((s + waveSeq_) / 5u) % 5u) - 2) * 8.0f;
					tx += spreadX;
					ty += spreadY;
					ClampOutsideCitadelKeepOut(tx, ty, kDirectorSteerClearRadius);
					out.moves[out.moveCount++] = {
						tu.handle, tx, ty,
						tu.role == CE_SB_ROLE_AIR ? -46.0f : HeightAt(tx, ty)
					};
					tu.orderCooldownSec = 14.0f;
				}
			}
		}

		// 6. Periodic diegetic regroup lull (25 s every 15 minutes) for mThingHeap coalescing.
		if (regroupRemainSec_ > 0.0f) {
			regroupRemainSec_ -= stepDt;
			if (!regroupCoalesced_ && regroupRemainSec_ <= cfg_.regroupDurationSec * 0.5f) {
				regroupCoalesced_ = true;
				out.requestHeapCoalesce = true;
				++stats_.totalHeapCoalesces;
			}
			if (regroupRemainSec_ < 0.0f) regroupRemainSec_ = 0.0f;
		} else {
			regroupTimerSec_ += stepDt;
			if (regroupTimerSec_ >= cfg_.regroupIntervalSec) {
				regroupTimerSec_ = 0.0f;
				regroupRemainSec_ = cfg_.regroupDurationSec;
				regroupCoalesced_ = false;
				++stats_.totalRegroupLulls;
				SetBanner("FRONT REGROUP - REARMING FORCES", 6.0f);
			}
		}

		// 7. Timed 5-minute Naval / Carrier Escalation in the SE coastal strait.
		navalTimerSec_ += stepDt;
		if (navalTimerSec_ >= cfg_.navalIntervalSec &&
		    regroupRemainSec_ <= 0.0f &&
		    !pressureThrottled_ &&
		    !hysteresisPaused_ &&
		    (pressure.liveWorldUnits + 4 <= effectiveCap_)) {
			navalTimerSec_ = 0.0f;
			EmitNavalEscalation(out, pressure.venturerAvailable);
		}

		// 8. Combined-arms reinforcement waves (paused during regroup lull, hysteresis, or pressure).
		waveTimerSec_ += stepDt;
		if (waveTimerSec_ >= cfg_.waveIntervalSec &&
		    regroupRemainSec_ <= 0.0f &&
		    !pressureThrottled_ &&
		    !hysteresisPaused_) {
			waveTimerSec_ = 0.0f;
			int headroom = effectiveCap_ - (pressure.liveWorldUnits + out.spawnCount);
			if (headroom >= 2) {
				static const char *const kForsetiRoster[8] = {
					kUnitBroadsword, kUnitSabre, kUnitBeamTank, kUnitForsetiTrooper,
					kUnitVenom, kUnitPlasmaArtillery, kUnitWildcatBilly, kUnitMonitorBomber
				};
				static const char *const kMuspellRoster[8] = {
					kUnitHeavyGunTank, kUnitHeavyMech, kUnitGunTank, kUnitMuspellGrunt,
					kUnitMuspellFighter, kUnitCrabMech, kUnitMortarTank, kUnitMuspellAdvFighter
				};

				const unsigned baseIdx = waveSeq_++;
				const int pairs = (headroom >= 6) ? 2 : 1;
				for (int p = 0; p < pairs; ++p) {
					const unsigned rIdx = (baseIdx * 2u + static_cast<unsigned>(p)) & 7u;
					if (!forsetiHq_.suppressed && headroom > 0) {
						const char *def = kForsetiRoster[rIdx];
						const bool air = IsAirDef(def);
						float sx = 0.0f, sy = 0.0f, sz = 0.0f;
						ChooseStagingPoint(kAllegianceForseti, baseIdx + static_cast<unsigned>(p), air, sx, sy, sz);
						const float tz = air ? -46.0f : forsetiObjZ;
						EmitSpawn(out, {
							def, kAllegianceForseti, air ? CE_SB_ROLE_AIR : CE_SB_ROLE_LINE,
							sx, sy, sz, ComputeYaw(sx, sy, forsetiObjX, forsetiObjY),
							forsetiObjX, forsetiObjY, tz, false, false
						});
						--headroom;
					}
					if (!muspellHq_.suppressed && headroom > 0) {
						const char *def = kMuspellRoster[rIdx];
						const bool air = IsAirDef(def);
						float sx = 0.0f, sy = 0.0f, sz = 0.0f;
						ChooseStagingPoint(kAllegianceMuspell, baseIdx + static_cast<unsigned>(p), air, sx, sy, sz);
						const float tz = air ? -48.0f : muspellObjZ;
						EmitSpawn(out, {
							def, kAllegianceMuspell, air ? CE_SB_ROLE_AIR : CE_SB_ROLE_LINE,
							sx, sy, sz, ComputeYaw(sx, sy, muspellObjX, muspellObjY),
							muspellObjX, muspellObjY, tz, false, false
						});
						--headroom;
					}
				}
			}
		}
	}

public:
	CeSandboxDirector() { Reset(); }

	void Reset(const CeSandboxDirectorConfig &cfg = CeSandboxDefaultConfig(false)) {
		phase_ = CE_SB_INACTIVE;
		cfg_ = cfg;
		elapsedSec_ = 0.0;
		stepAccumSec_ = 0.0f;
		waveTimerSec_ = 0.0f;
		regroupTimerSec_ = 0.0f;
		regroupRemainSec_ = 0.0f;
		regroupCoalesced_ = false;
		navalTimerSec_ = 0.0f;
		bannerRemainSec_ = 0.0f;
		bannerText_[0] = '\0';

		for (unsigned i = 0; i < CeSandboxWorld::kCaptureNodeCount; ++i) {
			const CeSandboxWorld::CaptureNodeSpec &ns = CeSandboxWorld::CaptureNode(i);
			nodeControl_[i] = ns.initialControl;
			nodeOwner_[i] = ns.initialControl >= 50.0f ? CE_SB_OWNER_FORSETI
			              : ns.initialControl <= -50.0f ? CE_SB_OWNER_MUSPELL
			                                            : CE_SB_OWNER_NEUTRAL;
		}

		forsetiHq_ = {};
		muspellHq_ = {};
		navalForsetiFlagship_ = kCeEntityHandleInvalid;
		navalMuspellFlagship_ = kCeEntityHandleInvalid;
		trackedCount_ = 0u;
		effectiveCap_ = cfg_.targetUnitCap;
		lastLiveUnits_ = 0;
		emaFrameMs_ = 16.67f;
		hysteresisPaused_ = false;
		pressureThrottled_ = false;
		p0StasisActive_ = false;
		waveSeq_ = 0u;
		stats_ = {};
	}

	void Arm(const CeSandboxDirectorConfig &cfg) {
		Reset(cfg);
		phase_ = CE_SB_ARMED;
	}

	void BeginLevel(const CeSandboxDirectorConfig &cfg) {
		Reset(cfg);
		phase_ = CE_SB_ACTIVE;
		SetBanner("LIVING SANDBOX - COMBINED-ARMS FRONT ACTIVE", 5.5f);
	}

	void EndLevel(bool keepArmed) {
		const CeSandboxDirectorConfig kept = cfg_;
		Reset(kept);
		phase_ = keepArmed ? CE_SB_ARMED : CE_SB_INACTIVE;
	}

	CeSandboxPhase Phase() const { return phase_; }
	bool IsActiveOrArmed() const { return phase_ != CE_SB_INACTIVE; }
	bool IsRunning() const { return phase_ == CE_SB_ACTIVE; }
	const CeSandboxDirectorConfig &Config() const { return cfg_; }
	const Stats &GetStats() const { return stats_; }
	unsigned TrackedCount() const { return trackedCount_; }
	int EffectiveCap() const { return effectiveCap_; }
	bool IsHysteresisPaused() const { return hysteresisPaused_; }
	bool IsPressureThrottled() const { return pressureThrottled_; }
	bool IsRegroupActive() const { return regroupRemainSec_ > 0.0f; }
	bool IsForsetiHqSuppressed() const { return forsetiHq_.suppressed; }
	bool IsMuspellHqSuppressed() const { return muspellHq_.suppressed; }
	float NodeControl(unsigned idx) const {
		return idx < CeSandboxWorld::kCaptureNodeCount ? nodeControl_[idx] : 0.0f;
	}
	CeSandboxOwner NodeOwner(unsigned idx) const {
		return idx < CeSandboxWorld::kCaptureNodeCount ? nodeOwner_[idx] : CE_SB_OWNER_NEUTRAL;
	}
	double ElapsedSec() const { return elapsedSec_; }

	void SetP0StasisActive(bool active) { p0StasisActive_ = active; }

	void RecordPlayerRecall() {
		++stats_.totalPlayerRecalls;
		SetBanner("EMERGENCY RECALL - SANCTUARY CITADEL", 5.0f);
	}

	void TriggerNavalEscalationNow(CeSandboxStepOutput &out, bool venturerAvailable) {
		if (!IsRunning()) return;
		navalTimerSec_ = 0.0f;
		EmitNavalEscalation(out, venturerAvailable);
	}

	void OnUnitSpawn(CeEntityHandle handle, const char *unitName, int allegiance,
	                 float x, float y, float life) {
		if (!IsRunning() || !CeEntityHandleIsValid(handle) || !unitName) return;
		if (CeSandboxWorld::IsForbiddenUnit(unitName)) return;
		if (FindTrackedIndex(handle) >= 0) return;

		CeSandboxUnitRole role = CE_SB_ROLE_LINE;
		if (IsHqDef(unitName)) {
			role = CE_SB_ROLE_HQ;
			HqCoreState &hq = (allegiance == CeSandboxWorld::kAllegianceForseti) ? forsetiHq_ : muspellHq_;
			hq.handle = handle;
			hq.seen = true;
			hq.alive = true;
			hq.suppressed = false;
			hq.rebuildTimerSec = 0.0f;
		} else if (IsTurretDef(unitName)) {
			role = CE_SB_ROLE_TURRET;
		} else if (IsNavalDef(unitName)) {
			role = CE_SB_ROLE_NAVAL;
			if (allegiance == CeSandboxWorld::kAllegianceForseti) navalForsetiFlagship_ = handle;
			else if (allegiance == CeSandboxWorld::kAllegianceMuspell) navalMuspellFlagship_ = handle;
		} else if (IsAirDef(unitName)) {
			role = CE_SB_ROLE_AIR;
		}

		if (trackedCount_ < kMaxTrackedUnits) {
			tracked_[trackedCount_++] = {handle, allegiance, role, x, y, life, 0.0f, 0.0f, 2.0f};
			if (static_cast<int>(trackedCount_) > stats_.peakTrackedUnits)
				stats_.peakTrackedUnits = static_cast<int>(trackedCount_);
		}
	}

	void OnUnitDestroyed(CeEntityHandle handle) {
		if (!IsRunning() || !CeEntityHandleIsValid(handle)) return;
		++stats_.totalDeaths;

		if (handle == forsetiHq_.handle && forsetiHq_.seen && forsetiHq_.alive) {
			SuppressHq(CeSandboxWorld::kAllegianceForseti);
		} else if (handle == muspellHq_.handle && muspellHq_.seen && muspellHq_.alive) {
			SuppressHq(CeSandboxWorld::kAllegianceMuspell);
		}
		if (handle == navalForsetiFlagship_) navalForsetiFlagship_ = kCeEntityHandleInvalid;
		if (handle == navalMuspellFlagship_) navalMuspellFlagship_ = kCeEntityHandleInvalid;

		const int idx = FindTrackedIndex(handle);
		if (idx >= 0) RemoveTrackedAt(static_cast<unsigned>(idx));
	}

	void OnHqRebuildSpawnFailed(int allegiance, float retrySec = 5.0f) {
		if (!IsRunning()) return;
		HqCoreState &hq = (allegiance == CeSandboxWorld::kAllegianceForseti) ? forsetiHq_ : muspellHq_;
		hq.alive = false;
		hq.suppressed = true;
		hq.rebuildTimerSec = retrySec > 0.5f ? retrySec : 0.5f;
		if (hq.rebuildCount > 0u) --hq.rebuildCount;
		if (stats_.totalHqRebuilds > 0u) --stats_.totalHqRebuilds;
	}

	// Advances host-driven 2 Hz cadence. Returns true when a 2 Hz step executed.
	bool Tick(float dt, const CeSandboxPressure &pressure,
	          const CeSandboxNodePresence &presence,
	          const CeSandboxTrackedSample *samples, unsigned sampleCount,
	          CeSandboxStepOutput *out) {
		if (out) {
			out->spawnCount = 0;
			out->moveCount = 0;
			out->reapCount = 0;
			out->requestHeapCoalesce = false;
		}
		if (phase_ != CE_SB_ACTIVE || !(dt > 0.0f) || dt > 5.0f || !out) return false;

		stepAccumSec_ += dt;
		if (stepAccumSec_ + 1e-5f < cfg_.directorStepSec) return false;

		const float stepDt = cfg_.directorStepSec;
		stepAccumSec_ -= stepDt;
		if (stepAccumSec_ > stepDt) stepAccumSec_ = 0.0f;
		StepDirector2Hz(stepDt, pressure, presence, samples, sampleCount, *out);
		return true;
	}

	CeSandboxHudState BuildHudState() const {
		CeSandboxHudState h = {};
		h.active = IsRunning();
		h.liveUnits = lastLiveUnits_;
		h.effectiveCap = effectiveCap_;
		h.regroupActive = regroupRemainSec_ > 0.0f;
		h.navalEscalationActive = (FindTrackedIndex(navalForsetiFlagship_) >= 0 ||
		                           FindTrackedIndex(navalMuspellFlagship_) >= 0);
		h.forsetiHqSuppressed = forsetiHq_.suppressed;
		h.muspellHqSuppressed = muspellHq_.suppressed;
		h.p0StasisActive = p0StasisActive_;
		h.pressureThrottled = pressureThrottled_;
		h.elapsedSec = elapsedSec_;
		h.activeFrontNode = 2;

		for (unsigned i = 0; i < CeSandboxWorld::kCaptureNodeCount; ++i) {
			h.nodeControl[i] = nodeControl_[i];
			h.nodeOwner[i] = nodeOwner_[i];
			if (nodeOwner_[i] == CE_SB_OWNER_FORSETI) ++h.forsetiNodes;
			else if (nodeOwner_[i] == CE_SB_OWNER_MUSPELL) ++h.muspellNodes;
			else {
				++h.contestedNodes;
				h.activeFrontNode = static_cast<int>(i);
			}
		}

		char leftRaw[32], midRaw[40], rightRaw[32];
		snprintf(leftRaw, sizeof(leftRaw), "FORSETI %d", h.forsetiNodes);
		if (h.regroupActive) {
			snprintf(midRaw, sizeof(midRaw), "REGROUP - %d UNITS", h.liveUnits);
		} else if (h.p0StasisActive) {
			snprintf(midRaw, sizeof(midRaw), "BE STASIS - %d UNITS", h.liveUnits);
		} else {
			snprintf(midRaw, sizeof(midRaw), "LIVING SANDBOX - %d", h.liveUnits);
		}
		snprintf(rightRaw, sizeof(rightRaw), "%d MUSPELL", h.muspellNodes);

		CeSandboxSanitizeHudText(leftRaw, h.headerLeft, sizeof(h.headerLeft));
		CeSandboxSanitizeHudText(midRaw, h.headerMid, sizeof(h.headerMid));
		CeSandboxSanitizeHudText(rightRaw, h.headerRight, sizeof(h.headerRight));
		CeSandboxSanitizeHudText(bannerText_, h.bannerText, sizeof(h.bannerText));
		h.showBanner = (bannerRemainSec_ > 0.0f && h.bannerText[0] != '\0');
		return h;
	}
};

// Engine C ABI entrypoints (implemented in ce_sandbox_director.cpp & ce_sandbox_world.cpp).
class CPlayer;
class CUnit;
class CInitThing;
class CCareer;

extern "C" {
bool PortCeLivingSandboxBegin(void);
bool PortCeLivingSandboxActive(void);
void PortCeLivingSandboxCancel(void);
const char *PortCeLivingSandboxMessage(void);
bool PortCeLivingSandboxLoadRefused(void);
bool PortCeLivingSandboxTakeReturn(void);
bool PortCeLivingSandboxTakeNotice(const char **sentence);

bool PortCeLivingSandboxPreflightAndBuildWorld(void);
void PortCeLivingSandboxDropWorld(void);
void PortCeLivingSandboxInstallArena(void);
bool PortCeLivingSandboxPreloadMeshes(void);
bool PortCeLivingSandboxVenturerReady(void);
int PortCeLivingSandboxReadWorld(const char *gamePath, const void **bytes, unsigned *length);
void PortCeLivingSandboxMarkRetryReset(void);
void PortCeLivingSandboxSetMessage(const char *message);

bool PortCeLivingSandboxRescuePlayer(int playerNumber);
int PortCeLivingSandboxBeInStasis(const void *be);
float PortCeLivingSandboxHudTopBottom(float viewportW, float viewportH);
void PortCeLivingSandboxLevelBegin(int level);
void PortCeLivingSandboxLevelEnd(int level, int quit);
void PortCeLivingSandboxTickPre(void);
void PortCeLivingSandboxDrawWorld(CPlayer *player, int viewpoint);
void PortCeLivingSandboxDrawHud(CPlayer *player, int viewpoint);
bool PortCeLivingSandboxKey(unsigned char key, bool repeat);
void PortCeLivingSandboxUnitSpawn(CUnit *unit, CInitThing *init);
void PortCeLivingSandboxUnitDestroyed(CUnit *unit);
void PortCeLivingSandboxFilterSave(CCareer *career, void *dest);
}

#endif
