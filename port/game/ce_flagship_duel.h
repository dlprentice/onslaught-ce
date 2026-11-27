// SPDX-License-Identifier: GPL-3.0-only
// Clean-Room Level 888 ("Flagship Duel"): pure encounter director, North-vs-South
// flagship separation guard, 1P & 2P split-screen outcome state machine, and
// 9-slice HUD layout / FONT_SMALL glyph validator.
#pragma once

#include "ce_flagship_duel_world.h"
#include "ce_entity_handles.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

enum class CeFlagshipDuelPhase : uint8_t {
	kInactive = 0,
	kRunning = 1,
	kVictoryHold = 2,
	kDefeatHold = 3,
	kFinishedVictory = 4, // 1P: Fenrir destroyed -> DeclareLevelWon
	kFinishedDefeat = 5,  // 1P: Venturer destroyed -> DeclareLevelLost
	kFinishedP1Won = 6,   // 2P: Fenrir destroyed or P2 lives exhausted -> MPDeclarePlayerWon(1)
	kFinishedP2Won = 7,   // 2P: Venturer destroyed or P1 lives exhausted -> MPDeclarePlayerWon(2)
};

enum class CeFlagshipDuelAct : uint8_t {
	kActI_OpeningSalvo = 0,
	kActII_BroadsideExchange = 1,
	kActIII_FinalStand = 2,
};

struct CeFlagshipDuelConfig {
	CeFlagshipDuelMode mode = CeFlagshipDuelMode::kSolo1P;
	int initialLives2P = 3;
	float escortWaveIntervalSec = 18.0f;
	float soloVictoryHoldSec = 4.0f;
	float soloDefeatHoldSec = 2.5f;
	float splitOutcomeHoldSec = 3.5f;
	uint32_t maxAliveEscortsPerSide = 4u;
	uint32_t maxTotalUnitsInWorld = 24u;
};

struct CeFdPatrolPoint {
	float x;
	float y;
	float z;
};

// Authentic Level 888 North-vs-South broadside patrol stations:
// F.S. Venturer holds the North sector (Y = 368..428, centered around Y = 420)
// while Fenrir holds the South sector (Y = 92..152, centered around Y = 100),
// keeping ~270..330 m horizontal separation and staggered altitudes across legs.
static constexpr CeFdPatrolPoint kCeFdVenturerPatrol[] = {
	{256.0f, 420.0f, -60.0f},
	{168.0f, 396.0f, -58.0f},
	{256.0f, 372.0f, -64.0f},
	{344.0f, 396.0f, -60.0f},
};
static constexpr size_t kCeFdVenturerPatrolCount =
	sizeof(kCeFdVenturerPatrol) / sizeof(kCeFdVenturerPatrol[0]);

static constexpr CeFdPatrolPoint kCeFdFenrirPatrol[] = {
	{256.0f, 100.0f, -60.0f},
	{344.0f, 124.0f, -66.0f},
	{256.0f, 148.0f, -56.0f},
	{168.0f, 124.0f, -62.0f},
};
static constexpr size_t kCeFdFenrirPatrolCount =
	sizeof(kCeFdFenrirPatrol) / sizeof(kCeFdFenrirPatrol[0]);

static constexpr float kCeFdSeparationGuardDist = 175.0f;
static constexpr float kCeFdSeparationCooldownSec = 3.0f;
static constexpr float kCeFdHardSeparationDist = 135.0f;
static constexpr float kCeFdMaxPushStepPerTick = 2.5f;
static constexpr float kCeFdArenaMinX = 72.0f;
static constexpr float kCeFdArenaMaxX = 440.0f;
static constexpr float kCeFdArenaMinY = 64.0f;
static constexpr float kCeFdArenaMaxY = 456.0f;

static constexpr float kCeFdActIIThreshold = 0.65f;
static constexpr float kCeFdActIIIThreshold = 0.30f;
static constexpr float kCeFdHullFlashThreshold = 0.02f;
static constexpr float kCeFdHullFlashHoldSec = 0.55f;
static constexpr float kCeFdActCalloutHoldSec = 4.2f;
static constexpr float kCeFdDangerHullThreshold = 0.30f;

struct CeFdSeparationPush {
	bool active;
	float distance2D;
	float venturerNewX;
	float venturerNewY;
	float fenrirNewX;
	float fenrirNewY;
	float nx; // Unit normal pointing from Fenrir toward Venturer
	float ny;
};

inline float CeFlagshipDuelClampF(float v, float lo, float hi) {
	return (v < lo) ? lo : ((v > hi) ? hi : v);
}

// Pure deterministic hull-overlap barrier: if AI pursuit ever drives the two
// capital ships inside `minSep`, pushes both apart symmetrically (clamped to
// `maxStepPerTick` so motion stays smooth) along their horizontal separation axis.
inline CeFdSeparationPush CeFlagshipDuelComputeSeparationPush(
	float vx, float vy, float fx, float fy,
	float minSep = kCeFdHardSeparationDist,
	float maxStepPerTick = kCeFdMaxPushStepPerTick) {
	CeFdSeparationPush out{false, 0.0f, vx, vy, fx, fy, 0.0f, 1.0f};
	const float dx = vx - fx;
	const float dy = vy - fy;
	const float distSq = dx * dx + dy * dy;
	const float dist = std::sqrt(distSq);
	out.distance2D = dist;
	if (dist >= minSep) return out;

	out.active = true;
	if (dist > 1e-3f) {
		out.nx = dx / dist;
		out.ny = dy / dist;
	} else {
		// Degenerate exact-center tie-breaker: push Venturer North (+Y) and Fenrir South (-Y).
		out.nx = 0.0f;
		out.ny = 1.0f;
	}

	float halfDeficit = 0.5f * (minSep - dist);
	if (halfDeficit > maxStepPerTick)
		halfDeficit = maxStepPerTick;

	out.venturerNewX = CeFlagshipDuelClampF(vx + out.nx * halfDeficit, kCeFdArenaMinX, kCeFdArenaMaxX);
	out.venturerNewY = CeFlagshipDuelClampF(vy + out.ny * halfDeficit, kCeFdArenaMinY, kCeFdArenaMaxY);
	out.fenrirNewX = CeFlagshipDuelClampF(fx - out.nx * halfDeficit, kCeFdArenaMinX, kCeFdArenaMaxX);
	out.fenrirNewY = CeFlagshipDuelClampF(fy - out.ny * halfDeficit, kCeFdArenaMinY, kCeFdArenaMaxY);
	return out;
}

class CeFlagshipDuelDirector {
public:
	static constexpr size_t kMaxEscortSlots = 6u;

	CeFlagshipDuelDirector() { Reset(CeFlagshipDuelConfig{}); }

	void Reset(const CeFlagshipDuelConfig &cfg) {
		mCfg = cfg;
		mPhase = CeFlagshipDuelPhase::kInactive;
		mElapsedSec = 0.0f;
		mHoldRemainingSec = 0.0f;
		mNextEscortWaveSec = cfg.escortWaveIntervalSec;
		mNextPatrolUpdateSec = 0.5f;
		mVenturerPatrolIdx = 0u;
		mFenrirPatrolIdx = 0u;
		mVenturer = kCeEntityHandleInvalid;
		mFenrir = kCeEntityHandleInvalid;
		mVenturerHealthFrac = 1.0f;
		mFenrirHealthFrac = 1.0f;
		mVenturerAlive = false;
		mFenrirAlive = false;
		for (size_t i = 0; i < kMaxEscortSlots; ++i) {
			mForsetiEscorts[i] = kCeEntityHandleInvalid;
			mMuspellEscorts[i] = kCeEntityHandleInvalid;
		}
		mForsetiEscortKills = 0u;
		mMuspellEscortKills = 0u;
		mWavesSpawned = 0u;
		mPlayer1Lives = (cfg.mode == CeFlagshipDuelMode::kSplitScreen2P) ? cfg.initialLives2P : 1;
		mPlayer2Lives = (cfg.mode == CeFlagshipDuelMode::kSplitScreen2P) ? cfg.initialLives2P : 0;
		mAct = CeFlagshipDuelAct::kActI_OpeningSalvo;
		mActBannerRemainingSec = kCeFdActCalloutHoldSec;
		mVenturerPrevHealthFrac = 1.0f;
		mFenrirPrevHealthFrac = 1.0f;
		mVenturerFlashSec = 0.0f;
		mFenrirFlashSec = 0.0f;
		mSeparationRedirectPending = false;
		mSeparationCooldownSec = 0.0f;
		mVenturerRedirectPt = kCeFdVenturerPatrol[0];
		mFenrirRedirectPt = kCeFdFenrirPatrol[0];
		mMinObservedSeparation = 9999.0f;
		mSeparationGuardTriggers = 0u;
	}

	void BeginMatch(CeEntityHandle venturer, CeEntityHandle fenrir) {
		mVenturer = venturer;
		mFenrir = fenrir;
		mVenturerAlive = (venturer != kCeEntityHandleInvalid);
		mFenrirAlive = (fenrir != kCeEntityHandleInvalid);
		mVenturerHealthFrac = mVenturerAlive ? 1.0f : 0.0f;
		mFenrirHealthFrac = mFenrirAlive ? 1.0f : 0.0f;
		mVenturerPrevHealthFrac = mVenturerHealthFrac;
		mFenrirPrevHealthFrac = mFenrirHealthFrac;
		mVenturerFlashSec = 0.0f;
		mFenrirFlashSec = 0.0f;
		mAct = CeFlagshipDuelAct::kActI_OpeningSalvo;
		mActBannerRemainingSec = kCeFdActCalloutHoldSec;
		mSeparationRedirectPending = false;
		mSeparationCooldownSec = 0.0f;
		mPhase = CeFlagshipDuelPhase::kRunning;
		mElapsedSec = 0.0f;
		mHoldRemainingSec = 0.0f;
		mNextEscortWaveSec = mCfg.escortWaveIntervalSec;
		mNextPatrolUpdateSec = 0.5f;
	}

	bool RegisterEscort(int allegiance, CeEntityHandle h) {
		if (h == kCeEntityHandleInvalid) return false;
		CeEntityHandle *slots = (allegiance == 0) ? mForsetiEscorts : mMuspellEscorts;
		for (size_t i = 0; i < kMaxEscortSlots; ++i) {
			if (slots[i] == h) return true;
		}
		for (size_t i = 0; i < kMaxEscortSlots; ++i) {
			if (slots[i] == kCeEntityHandleInvalid) {
				slots[i] = h;
				return true;
			}
		}
		return false;
	}

	void OnEntityDestroyed(CeEntityHandle h) {
		if (h == kCeEntityHandleInvalid || mPhase == CeFlagshipDuelPhase::kInactive)
			return;

		// Fenrir destroyed: P1 / Forseti wins!
		if (h == mFenrir) {
			mFenrirAlive = false;
			mFenrirHealthFrac = 0.0f;
			if (mPhase == CeFlagshipDuelPhase::kRunning) {
				mPhase = CeFlagshipDuelPhase::kVictoryHold;
				mHoldRemainingSec = IsSplitScreen2P()
					? mCfg.splitOutcomeHoldSec
					: mCfg.soloVictoryHoldSec;
			}
			return;
		}

		// F.S. Venturer destroyed: Defeat in 1P, Player 2 Wins in 2P!
		// Once VictoryHold has latched (Fenrir died first), Venturer loss cannot overwrite it.
		if (h == mVenturer) {
			mVenturerAlive = false;
			mVenturerHealthFrac = 0.0f;
			if (mPhase == CeFlagshipDuelPhase::kRunning) {
				mPhase = CeFlagshipDuelPhase::kDefeatHold;
				mHoldRemainingSec = IsSplitScreen2P()
					? mCfg.splitOutcomeHoldSec
					: mCfg.soloDefeatHoldSec;
			}
			return;
		}

		for (size_t i = 0; i < kMaxEscortSlots; ++i) {
			if (mForsetiEscorts[i] == h) {
				mForsetiEscorts[i] = kCeEntityHandleInvalid;
				++mForsetiEscortKills;
				return;
			}
			if (mMuspellEscorts[i] == h) {
				mMuspellEscorts[i] = kCeEntityHandleInvalid;
				++mMuspellEscortKills;
				return;
			}
		}
	}

	void PruneStaleHandles(bool (*isAliveFn)(CeEntityHandle, void *), void *ctx) {
		if (!isAliveFn) return;
		if (mVenturer != kCeEntityHandleInvalid && !isAliveFn(mVenturer, ctx))
			OnEntityDestroyed(mVenturer);
		if (mFenrir != kCeEntityHandleInvalid && !isAliveFn(mFenrir, ctx))
			OnEntityDestroyed(mFenrir);
		for (size_t i = 0; i < kMaxEscortSlots; ++i) {
			if (mForsetiEscorts[i] != kCeEntityHandleInvalid && !isAliveFn(mForsetiEscorts[i], ctx)) {
				mForsetiEscorts[i] = kCeEntityHandleInvalid;
				++mForsetiEscortKills;
			}
			if (mMuspellEscorts[i] != kCeEntityHandleInvalid && !isAliveFn(mMuspellEscorts[i], ctx)) {
				mMuspellEscorts[i] = kCeEntityHandleInvalid;
				++mMuspellEscortKills;
			}
		}
	}

	void UpdatePlayerLives(int p1Lives, int p2Lives) {
		mPlayer1Lives = (p1Lives >= 0) ? p1Lives : 0;
		mPlayer2Lives = (p2Lives >= 0) ? p2Lives : 0;
	}

	// Syncs outcome if 2P split-screen ends via 3-lives exhaustion in CGame::RespawnPlayer.
	// Once a flagship kill latches kVictoryHold (P1 won) or kDefeatHold (P2 won),
	// an opposite external outcome cannot reverse the latched winner.
	void SyncExternalMultiplayerOutcome(bool p1WonState, bool p2WonState) {
		if (!IsSplitScreen2P() || mPhase == CeFlagshipDuelPhase::kInactive)
			return;
		if (p1WonState &&
		    mPhase != CeFlagshipDuelPhase::kFinishedP2Won &&
		    mPhase != CeFlagshipDuelPhase::kDefeatHold) {
			mPhase = CeFlagshipDuelPhase::kFinishedP1Won;
			mHoldRemainingSec = 0.0f;
		} else if (p2WonState &&
		           mPhase != CeFlagshipDuelPhase::kFinishedP1Won &&
		           mPhase != CeFlagshipDuelPhase::kVictoryHold) {
			mPhase = CeFlagshipDuelPhase::kFinishedP2Won;
			mHoldRemainingSec = 0.0f;
		}
	}

	// Handles 1P solo Battle Engine destruction (GAME.DeclarePlayerDead() transitions
	// to GAME_STATE_LEVEL_LOST while Venturer may still be afloat).
	void OnSoloPlayerLost() {
		if (IsSplitScreen2P() || mPhase != CeFlagshipDuelPhase::kRunning)
			return;
		mPhase = CeFlagshipDuelPhase::kFinishedDefeat;
		mHoldRemainingSec = 0.0f;
	}

	void UpdateFlagshipHealth(float venturerFrac, float fenrirFrac) {
		const float vClamped = mVenturerAlive ? Clamp01(venturerFrac) : 0.0f;
		const float fClamped = mFenrirAlive ? Clamp01(fenrirFrac) : 0.0f;

		if (mVenturerPrevHealthFrac - vClamped >= kCeFdHullFlashThreshold)
			mVenturerFlashSec = kCeFdHullFlashHoldSec;
		if (mFenrirPrevHealthFrac - fClamped >= kCeFdHullFlashThreshold)
			mFenrirFlashSec = kCeFdHullFlashHoldSec;

		mVenturerHealthFrac = vClamped;
		mFenrirHealthFrac = fClamped;
		mVenturerPrevHealthFrac = vClamped;
		mFenrirPrevHealthFrac = fClamped;

		if (mPhase == CeFlagshipDuelPhase::kRunning && mVenturerAlive && mFenrirAlive) {
			const float minHull = (vClamped < fClamped) ? vClamped : fClamped;
			CeFlagshipDuelAct targetAct = mAct;
			if (minHull <= kCeFdActIIIThreshold)
				targetAct = CeFlagshipDuelAct::kActIII_FinalStand;
			else if (minHull <= kCeFdActIIThreshold &&
			         uint8_t(mAct) < uint8_t(CeFlagshipDuelAct::kActII_BroadsideExchange))
				targetAct = CeFlagshipDuelAct::kActII_BroadsideExchange;

			if (uint8_t(targetAct) > uint8_t(mAct)) {
				mAct = targetAct;
				mActBannerRemainingSec = kCeFdActCalloutHoldSec;
			}
		}
	}

	// North-vs-South separation guard: keeps Venturer in the North sector and
	// Fenrir in the South sector if AI pursuit closes inside 175 m.
	bool UpdateFlagshipPositions(float vx, float vy, float vz,
	                             float fx, float fy, float fz) {
		(void)vz;
		(void)fz;
		if (mPhase != CeFlagshipDuelPhase::kRunning || !mVenturerAlive || !mFenrirAlive)
			return false;
		const float dx = vx - fx;
		const float dy = vy - fy;
		const float dist = std::sqrt(dx * dx + dy * dy);
		if (dist < mMinObservedSeparation)
			mMinObservedSeparation = dist;
		if (dist < kCeFdSeparationGuardDist && mSeparationCooldownSec <= 0.0f) {
			if (vy >= fy - 8.0f) {
				const float vTargetY = CeFlagshipDuelClampF(
					(vy + 72.0f > 396.0f) ? (vy + 72.0f) : 396.0f, 360.0f, kCeFdArenaMaxY);
				const float fTargetY = CeFlagshipDuelClampF(
					(fy - 72.0f < 124.0f) ? (fy - 72.0f) : 124.0f, kCeFdArenaMinY, 160.0f);
				mVenturerRedirectPt = {CeFlagshipDuelClampF(vx, kCeFdArenaMinX, kCeFdArenaMaxX), vTargetY, -60.0f};
				mFenrirRedirectPt = {CeFlagshipDuelClampF(fx, kCeFdArenaMinX, kCeFdArenaMaxX), fTargetY, -64.0f};
			} else {
				const float inv = (dist > 1e-3f) ? (1.0f / dist) : 0.0f;
				const float ux = (dist > 1e-3f) ? (dx * inv) : 0.0f;
				const float uy = (dist > 1e-3f) ? (dy * inv) : 1.0f;
				mVenturerRedirectPt = {
					CeFlagshipDuelClampF(vx + ux * 95.0f, kCeFdArenaMinX, kCeFdArenaMaxX),
					CeFlagshipDuelClampF(vy + uy * 95.0f, kCeFdArenaMinY, kCeFdArenaMaxY),
					-60.0f
				};
				mFenrirRedirectPt = {
					CeFlagshipDuelClampF(fx - ux * 95.0f, kCeFdArenaMinX, kCeFdArenaMaxX),
					CeFlagshipDuelClampF(fy - uy * 95.0f, kCeFdArenaMinY, kCeFdArenaMaxY),
					-64.0f
				};
			}
			mSeparationRedirectPending = true;
			mSeparationCooldownSec = kCeFdSeparationCooldownSec;
			++mSeparationGuardTriggers;
			return true;
		}
		return false;
	}

	bool ConsumeSeparationRedirect(CeFdPatrolPoint *outVenturer, CeFdPatrolPoint *outFenrir) {
		if (!mSeparationRedirectPending) return false;
		mSeparationRedirectPending = false;
		if (outVenturer) *outVenturer = mVenturerRedirectPt;
		if (outFenrir) *outFenrir = mFenrirRedirectPt;
		return true;
	}

	struct TickDecision {
		bool advancePatrol;
		CeFdPatrolPoint venturerGoal;
		CeFdPatrolPoint fenrirGoal;
		uint32_t spawnForsetiEscorts;
		uint32_t spawnMuspellEscorts;
		bool declareSoloWon;
		bool declareSoloLost;
		bool declareMpPlayer1Won;
		bool declareMpPlayer2Won;
	};

	TickDecision Tick(float dtSec, uint32_t currentWorldUnitCount) {
		TickDecision d{};
		if (dtSec < 0.0f) dtSec = 0.0f;
		if (dtSec > 0.25f) dtSec = 0.25f;

		if (mPhase == CeFlagshipDuelPhase::kInactive || IsFinished())
			return d;

		mElapsedSec += dtSec;
		if (mActBannerRemainingSec > 0.0f) {
			mActBannerRemainingSec -= dtSec;
			if (mActBannerRemainingSec < 0.0f) mActBannerRemainingSec = 0.0f;
		}
		if (mVenturerFlashSec > 0.0f) {
			mVenturerFlashSec -= dtSec;
			if (mVenturerFlashSec < 0.0f) mVenturerFlashSec = 0.0f;
		}
		if (mFenrirFlashSec > 0.0f) {
			mFenrirFlashSec -= dtSec;
			if (mFenrirFlashSec < 0.0f) mFenrirFlashSec = 0.0f;
		}
		if (mSeparationCooldownSec > 0.0f) {
			mSeparationCooldownSec -= dtSec;
			if (mSeparationCooldownSec < 0.0f) mSeparationCooldownSec = 0.0f;
		}

		if (mPhase == CeFlagshipDuelPhase::kVictoryHold) {
			mHoldRemainingSec -= dtSec;
			if (mHoldRemainingSec <= 0.0f) {
				mHoldRemainingSec = 0.0f;
				if (IsSplitScreen2P()) {
					mPhase = CeFlagshipDuelPhase::kFinishedP1Won;
					d.declareMpPlayer1Won = true;
				} else {
					mPhase = CeFlagshipDuelPhase::kFinishedVictory;
					d.declareSoloWon = true;
				}
			}
			return d;
		}

		if (mPhase == CeFlagshipDuelPhase::kDefeatHold) {
			mHoldRemainingSec -= dtSec;
			if (mHoldRemainingSec <= 0.0f) {
				mHoldRemainingSec = 0.0f;
				if (IsSplitScreen2P()) {
					mPhase = CeFlagshipDuelPhase::kFinishedP2Won;
					d.declareMpPlayer2Won = true;
				} else {
					mPhase = CeFlagshipDuelPhase::kFinishedDefeat;
					d.declareSoloLost = true;
				}
			}
			return d;
		}

		if (!mFenrirAlive) {
			mPhase = CeFlagshipDuelPhase::kVictoryHold;
			mHoldRemainingSec = IsSplitScreen2P() ? mCfg.splitOutcomeHoldSec : mCfg.soloVictoryHoldSec;
			return d;
		}
		if (!mVenturerAlive) {
			mPhase = CeFlagshipDuelPhase::kDefeatHold;
			mHoldRemainingSec = IsSplitScreen2P() ? mCfg.splitOutcomeHoldSec : mCfg.soloDefeatHoldSec;
			return d;
		}

		if (mElapsedSec >= mNextPatrolUpdateSec) {
			d.advancePatrol = true;
			d.venturerGoal = kCeFdVenturerPatrol[mVenturerPatrolIdx % kCeFdVenturerPatrolCount];
			d.fenrirGoal = kCeFdFenrirPatrol[mFenrirPatrolIdx % kCeFdFenrirPatrolCount];
			mVenturerPatrolIdx = (mVenturerPatrolIdx + 1u) % kCeFdVenturerPatrolCount;
			mFenrirPatrolIdx = (mFenrirPatrolIdx + 1u) % kCeFdFenrirPatrolCount;
			mNextPatrolUpdateSec = mElapsedSec + 22.0f;
		}

		if (mElapsedSec >= mNextEscortWaveSec) {
			mNextEscortWaveSec = mElapsedSec + ActEscortWaveIntervalSec();
			const uint32_t aliveF = AliveEscortCount(0);
			const uint32_t aliveM = AliveEscortCount(1);
			uint32_t budget = (currentWorldUnitCount < mCfg.maxTotalUnitsInWorld)
				? (mCfg.maxTotalUnitsInWorld - currentWorldUnitCount)
				: 0u;
			const uint32_t perSideCap = ActEscortCap();

			if (mVenturerAlive && aliveF < perSideCap && budget > 0u) {
				const uint32_t want = (perSideCap - aliveF >= 2u) ? 2u : 1u;
				d.spawnForsetiEscorts = (want <= budget) ? want : budget;
				budget -= d.spawnForsetiEscorts;
			}
			if (mFenrirAlive && aliveM < perSideCap && budget > 0u) {
				const uint32_t want = (perSideCap - aliveM >= 2u) ? 2u : 1u;
				d.spawnMuspellEscorts = (want <= budget) ? want : budget;
				budget -= d.spawnMuspellEscorts;
			}
			if (d.spawnForsetiEscorts > 0u || d.spawnMuspellEscorts > 0u)
				++mWavesSpawned;
		}

		return d;
	}

	uint32_t AliveEscortCount(int allegiance) const {
		const CeEntityHandle *slots = (allegiance == 0) ? mForsetiEscorts : mMuspellEscorts;
		uint32_t n = 0u;
		for (size_t i = 0; i < kMaxEscortSlots; ++i) {
			if (slots[i] != kCeEntityHandleInvalid) ++n;
		}
		return n;
	}

	uint32_t ActEscortCap() const {
		const uint32_t base = (mCfg.maxAliveEscortsPerSide <= kMaxEscortSlots)
			? mCfg.maxAliveEscortsPerSide
			: uint32_t(kMaxEscortSlots);
		if (base <= 1u) return base;
		if (mAct == CeFlagshipDuelAct::kActI_OpeningSalvo)
			return (base > 2u) ? (base - 1u) : base;
		return base;
	}

	float ActEscortWaveIntervalSec() const {
		if (mAct == CeFlagshipDuelAct::kActIII_FinalStand)
			return mCfg.escortWaveIntervalSec * 0.72f;
		if (mAct == CeFlagshipDuelAct::kActII_BroadsideExchange)
			return mCfg.escortWaveIntervalSec * 0.86f;
		return mCfg.escortWaveIntervalSec;
	}

	CeFlagshipDuelMode Mode() const { return mCfg.mode; }
	bool IsSplitScreen2P() const { return mCfg.mode == CeFlagshipDuelMode::kSplitScreen2P; }
	CeFlagshipDuelPhase Phase() const { return mPhase; }
	bool IsFinished() const {
		return mPhase == CeFlagshipDuelPhase::kFinishedVictory ||
		       mPhase == CeFlagshipDuelPhase::kFinishedDefeat ||
		       mPhase == CeFlagshipDuelPhase::kFinishedP1Won ||
		       mPhase == CeFlagshipDuelPhase::kFinishedP2Won;
	}
	bool IsRunning() const { return mPhase != CeFlagshipDuelPhase::kInactive; }
	bool ShouldShieldWinningFlagships() const {
		return mPhase == CeFlagshipDuelPhase::kVictoryHold ||
		       mPhase == CeFlagshipDuelPhase::kDefeatHold ||
		       IsFinished();
	}
	float ElapsedSec() const { return mElapsedSec; }
	float HoldRemainingSec() const { return mHoldRemainingSec; }
	CeEntityHandle VenturerHandle() const { return mVenturer; }
	CeEntityHandle FenrirHandle() const { return mFenrir; }
	float VenturerHealthFrac() const { return mVenturerHealthFrac; }
	float FenrirHealthFrac() const { return mFenrirHealthFrac; }
	bool VenturerAlive() const { return mVenturerAlive; }
	bool FenrirAlive() const { return mFenrirAlive; }
	int Player1Lives() const { return mPlayer1Lives; }
	int Player2Lives() const { return mPlayer2Lives; }
	uint32_t ForsetiEscortKills() const { return mForsetiEscortKills; }
	uint32_t MuspellEscortKills() const { return mMuspellEscortKills; }
	uint32_t WavesSpawned() const { return mWavesSpawned; }
	CeFlagshipDuelAct CurrentAct() const { return mAct; }
	float ActBannerRemainingSec() const { return mActBannerRemainingSec; }
	float VenturerFlashSec() const { return mVenturerFlashSec; }
	float FenrirFlashSec() const { return mFenrirFlashSec; }
	bool IsVenturerInDanger() const {
		return mVenturerAlive && (mVenturerHealthFrac <= kCeFdDangerHullThreshold);
	}
	bool IsFenrirInDanger() const {
		return mFenrirAlive && (mFenrirHealthFrac <= kCeFdDangerHullThreshold);
	}
	float MinObservedSeparation() const { return mMinObservedSeparation; }
	uint32_t SeparationGuardTriggers() const { return mSeparationGuardTriggers; }

private:
	static float Clamp01(float v) {
		if (v < 0.0f) return 0.0f;
		if (v > 1.0f) return 1.0f;
		return v;
	}

	CeFlagshipDuelConfig mCfg;
	CeFlagshipDuelPhase mPhase;
	float mElapsedSec;
	float mHoldRemainingSec;
	float mNextEscortWaveSec;
	float mNextPatrolUpdateSec;
	uint32_t mVenturerPatrolIdx;
	uint32_t mFenrirPatrolIdx;
	CeEntityHandle mVenturer;
	CeEntityHandle mFenrir;
	float mVenturerHealthFrac;
	float mFenrirHealthFrac;
	bool mVenturerAlive;
	bool mFenrirAlive;
	CeEntityHandle mForsetiEscorts[kMaxEscortSlots];
	CeEntityHandle mMuspellEscorts[kMaxEscortSlots];
	uint32_t mForsetiEscortKills;
	uint32_t mMuspellEscortKills;
	uint32_t mWavesSpawned;
	int mPlayer1Lives;
	int mPlayer2Lives;
	CeFlagshipDuelAct mAct;
	float mActBannerRemainingSec;
	float mVenturerPrevHealthFrac;
	float mFenrirPrevHealthFrac;
	float mVenturerFlashSec;
	float mFenrirFlashSec;
	bool mSeparationRedirectPending;
	float mSeparationCooldownSec;
	CeFdPatrolPoint mVenturerRedirectPt;
	CeFdPatrolPoint mFenrirRedirectPt;
	float mMinObservedSeparation;
	uint32_t mSeparationGuardTriggers;
};

// ============================================================================
// Pure HUD Geometry & FONT_SMALL Glyph Safety Validator (1P + 2P Split-Screen)
// ============================================================================

inline bool CeFlagshipDuelIsFontSmallSafeChar(char c) {
	const unsigned char u = static_cast<unsigned char>(c);
	if (u < 0x20u || u > 0x7Eu) return false;
	switch (c) {
	case '~':
	case '\\':
	case '_':
	case '$':
	case '@':
	case '[':
	case ']':
	case '^':
	case '{':
	case '|':
	case '}':
	case '`':
	case ';':
		return false;
	default:
		return true;
	}
}

inline bool CeFlagshipDuelIsFontSmallSafeText(const char *s) {
	if (!s) return false;
	for (const char *p = s; *p; ++p) {
		if (!CeFlagshipDuelIsFontSmallSafeChar(*p))
			return false;
	}
	return true;
}

struct CeFdHudRect {
	float x0;
	float y0;
	float x1;
	float y1;
};

struct CeFdHudLayout {
	float uiScale;
	CeFdHudRect stripPanel;
	CeFdHudRect venturerBarOuter;
	CeFdHudRect venturerBarFill;
	CeFdHudRect fenrirBarOuter;
	CeFdHudRect fenrirBarFill;
	CeFdHudRect centerDivider;
	CeFdHudRect bannerPanel;
	float stripBottomY;
};

// 1P top-center HUD strip layout.
inline CeFdHudLayout CeFlagshipDuelComputeHudLayout(
	float viewportW, float viewportH,
	float venturerFrac, float fenrirFrac,
	bool hasSubBanner = false) {
	CeFdHudLayout l{};
	const float rawScale = viewportH / 480.0f;
	l.uiScale = (rawScale < 0.85f) ? 0.85f : ((rawScale > 4.5f) ? 4.5f : rawScale);
	const float s = l.uiScale;

	const float cx = viewportW * 0.5f;
	const float panelW = 396.0f * s;
	const float panelH = 38.0f * s;
	const float topY = 8.0f * s;
	l.stripPanel = {cx - panelW * 0.5f, topY, cx + panelW * 0.5f, topY + panelH};

	const float padX = 12.0f * s;
	const float centerGap = 24.0f * s;
	const float barW = (panelW * 0.5f) - padX - centerGap;
	const float barH = 9.0f * s;
	const float barY0 = topY + 22.0f * s;
	const float barY1 = barY0 + barH;

	const float vClamped = (venturerFrac < 0.0f) ? 0.0f : ((venturerFrac > 1.0f) ? 1.0f : venturerFrac);
	const float fClamped = (fenrirFrac < 0.0f) ? 0.0f : ((fenrirFrac > 1.0f) ? 1.0f : fenrirFrac);

	l.venturerBarOuter = {l.stripPanel.x0 + padX, barY0, l.stripPanel.x0 + padX + barW, barY1};
	l.venturerBarFill = {
		l.venturerBarOuter.x0 + 1.0f * s,
		barY0 + 1.0f * s,
		l.venturerBarOuter.x0 + 1.0f * s + (barW - 2.0f * s) * vClamped,
		barY1 - 1.0f * s
	};

	l.fenrirBarOuter = {l.stripPanel.x1 - padX - barW, barY0, l.stripPanel.x1 - padX, barY1};
	l.fenrirBarFill = {
		l.fenrirBarOuter.x1 - 1.0f * s - (barW - 2.0f * s) * fClamped,
		barY0 + 1.0f * s,
		l.fenrirBarOuter.x1 - 1.0f * s,
		barY1 - 1.0f * s
	};

	l.centerDivider = {cx - 1.0f * s, barY0 - 2.0f * s, cx + 1.0f * s, barY1 + 2.0f * s};

	const float bannerW = 368.0f * s;
	const float bannerH = 44.0f * s;
	const float bannerY = l.stripPanel.y1 + 8.0f * s;
	l.bannerPanel = {cx - bannerW * 0.5f, bannerY, cx + bannerW * 0.5f, bannerY + bannerH};

	const float subBannerExtra = hasSubBanner ? (28.0f * s) : 0.0f;
	l.stripBottomY = l.stripPanel.y1 + subBannerExtra + 6.0f * s;
	return l;
}

// 2P split-screen tactical strip: in horizontal split-screen, centered on the
// horizontal split divider (Y = windowH * 0.5f) between side HUD panels; in
// vertical split-screen, anchored across the top of the vertical seam so neither
// player's reticle (at Y = windowH * 0.5f) is covered.
inline CeFdHudLayout CeFlagshipDuelComputeSplitHudLayout(
	float windowW, float windowH,
	float venturerFrac, float fenrirFrac,
	bool horizontalSplit = true) {
	CeFdHudLayout l{};
	const float rawScale = windowH / 480.0f;
	l.uiScale = (rawScale < 0.85f) ? 0.85f : ((rawScale > 4.5f) ? 4.5f : rawScale);
	const float s = l.uiScale;

	const float cx = windowW * 0.5f;
	const float seamY = windowH * 0.5f;
	const float maxSafeW = windowW - 300.0f * s;
	const float desiredW = 336.0f * s;
	const float panelW = (maxSafeW < desiredW && maxSafeW > 240.0f * s) ? maxSafeW : desiredW;
	const float panelH = 28.0f * s;
	const float topY = horizontalSplit ? (seamY - panelH * 0.5f) : std::floor(10.0f * s);

	l.stripPanel = {cx - panelW * 0.5f, topY, cx + panelW * 0.5f, topY + panelH};

	const float padX = 10.0f * s;
	const float centerGap = 24.0f * s;
	const float barW = (panelW * 0.5f) - padX - centerGap;
	const float barH = 7.0f * s;
	const float barY0 = topY + 16.0f * s;
	const float barY1 = barY0 + barH;

	const float vClamped = (venturerFrac < 0.0f) ? 0.0f : ((venturerFrac > 1.0f) ? 1.0f : venturerFrac);
	const float fClamped = (fenrirFrac < 0.0f) ? 0.0f : ((fenrirFrac > 1.0f) ? 1.0f : fenrirFrac);

	l.venturerBarOuter = {l.stripPanel.x0 + padX, barY0, l.stripPanel.x0 + padX + barW, barY1};
	l.venturerBarFill = {
		l.venturerBarOuter.x0 + 1.0f * s,
		barY0 + 1.0f * s,
		l.venturerBarOuter.x0 + 1.0f * s + (barW - 2.0f * s) * vClamped,
		barY1 - 1.0f * s
	};

	l.fenrirBarOuter = {l.stripPanel.x1 - padX - barW, barY0, l.stripPanel.x1 - padX, barY1};
	l.fenrirBarFill = {
		l.fenrirBarOuter.x1 - 1.0f * s - (barW - 2.0f * s) * fClamped,
		barY0 + 1.0f * s,
		l.fenrirBarOuter.x1 - 1.0f * s,
		barY1 - 1.0f * s
	};

	l.centerDivider = {cx - 1.0f * s, barY0 - 1.0f * s, cx + 1.0f * s, barY1 + 1.0f * s};

	const float bannerW = 352.0f * s;
	const float bannerH = 42.0f * s;
	const float bannerY = l.stripPanel.y1 + 6.0f * s;
	l.bannerPanel = {cx - bannerW * 0.5f, bannerY, cx + bannerW * 0.5f, bannerY + bannerH};
	l.stripBottomY = 0.0f;
	return l;
}

inline const char *CeFlagshipDuelActRoman(CeFlagshipDuelAct act) {
	switch (act) {
	case CeFlagshipDuelAct::kActI_OpeningSalvo:      return "I";
	case CeFlagshipDuelAct::kActII_BroadsideExchange: return "II";
	case CeFlagshipDuelAct::kActIII_FinalStand:      return "III";
	default:                                         return "I";
	}
}

inline const char *CeFlagshipDuelActTitle(CeFlagshipDuelAct act) {
	switch (act) {
	case CeFlagshipDuelAct::kActI_OpeningSalvo:
		return "ACT I - FLAGSHIP DUEL ENGAGED";
	case CeFlagshipDuelAct::kActII_BroadsideExchange:
		return "ACT II - BROADSIDE ESCALATION";
	case CeFlagshipDuelAct::kActIII_FinalStand:
		return "ACT III - CRITICAL HULL DUEL";
	default:
		return "";
	}
}

inline const char *CeFlagshipDuelBannerTitle(
	CeFlagshipDuelPhase phase, bool split2P, bool flagshipDestroyed = true) {
	switch (phase) {
	case CeFlagshipDuelPhase::kVictoryHold:
	case CeFlagshipDuelPhase::kFinishedVictory:
	case CeFlagshipDuelPhase::kFinishedP1Won:
		if (split2P) {
			return flagshipDestroyed
				? "PLAYER 1 WINS - FENRIR DESTROYED"
				: "PLAYER 1 WINS - P2 OUT OF LIVES";
		}
		return "VICTORY - FENRIR DESTROYED";
	case CeFlagshipDuelPhase::kDefeatHold:
	case CeFlagshipDuelPhase::kFinishedDefeat:
	case CeFlagshipDuelPhase::kFinishedP2Won:
		if (split2P) {
			return flagshipDestroyed
				? "PLAYER 2 WINS - VENTURER LOST"
				: "PLAYER 2 WINS - P1 OUT OF LIVES";
		}
		return flagshipDestroyed
			? "DEFEAT - F.S. VENTURER LOST"
			: "DEFEAT - BATTLE ENGINE LOST";
	default:
		return "";
	}
}

inline const char *CeFlagshipDuelBannerSubtitle(
	CeFlagshipDuelPhase phase, bool split2P, bool flagshipDestroyed = true) {
	switch (phase) {
	case CeFlagshipDuelPhase::kVictoryHold:
	case CeFlagshipDuelPhase::kFinishedVictory:
		return "F.S. Venturer holds the 888 archipelago.";
	case CeFlagshipDuelPhase::kFinishedP1Won:
		if (!split2P) return "";
		return flagshipDestroyed
			? "Forseti flagship Venturer holds the strait."
			: "Player 2 ran out of Battle Engine lives.";
	case CeFlagshipDuelPhase::kDefeatHold:
	case CeFlagshipDuelPhase::kFinishedDefeat:
		return flagshipDestroyed
			? "Muspell flagship Fenrir broke the Forseti line."
			: "Your Battle Engine was destroyed in action.";
	case CeFlagshipDuelPhase::kFinishedP2Won:
		if (!split2P) return "";
		return flagshipDestroyed
			? "Muspell flagship Fenrir commands the sea."
			: "Player 1 ran out of Battle Engine lives.";
	default:
		return "";
	}
}

// ============================================================================
// Engine C ABI Entrypoints (implemented in ce_flagship_duel*.cpp)
// ============================================================================
#ifdef __cplusplus
extern "C" {
#endif

class CUnit;
class CInitThing;
class CPlayer;
class CCareer;

bool PortCeFlagshipDuelBegin1P(void);
bool PortCeFlagshipDuelBegin2P(void);
bool PortCeFlagshipDuelActive(void);
bool PortCeFlagshipDuelIsSplitScreen2P(void);
int PortCeFlagshipDuelActiveLevel(void);
void PortCeFlagshipDuelCancel(void);
const char *PortCeFlagshipDuelMessage(void);
bool PortCeFlagshipDuelLoadRefused(void);
bool PortCeFlagshipDuelTakeReturn(void);
bool PortCeFlagshipDuelTakeNotice(const char **sentence, int *outLevel);

bool PortCeFlagshipDuelPreflightAndBuildWorld(int modeRaw);
void PortCeFlagshipDuelDropWorld(void);
const char *PortCeFlagshipDuelDonorArchivePath(void);
bool PortCeFlagshipDuelPreloadMeshes(void);
int PortCeFlagshipDuelReadWorld(const char *gamePath, const void **bytes, unsigned *length);
void PortCeFlagshipDuelMarkRetryReset(void);
void PortCeFlagshipDuelSetMessage(const char *message);

void PortCeFlagshipDuelLevelBegin(int level);
void PortCeFlagshipDuelLevelEnd(int level, int quit);
void PortCeFlagshipDuelTickPre(void);
void PortCeFlagshipDuelDrawHud(CPlayer *player, int viewpoint);
void PortCeFlagshipDuelDrawOverlay(void);
int PortCeFlagshipDuelBeInStasis(const void *be);
float PortCeFlagshipDuelHudTopBottom(float viewportW, float viewportH);
void PortCeFlagshipDuelUnitSpawn(CUnit *unit, CInitThing *init);
void PortCeFlagshipDuelUnitDestroyed(CUnit *unit);
void PortCeFlagshipDuelFilterSave(CCareer *career, void *dest);

#ifdef __cplusplus
}
#endif
