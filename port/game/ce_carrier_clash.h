// SPDX-License-Identifier: GPL-3.0-only
// Clean-room C++ Carrier Clash (F.S. Venturer vs. Fenrir) mission director,
// 32-bit CeEntityHandle flagship tracking, escort wave budgeter, and 9-slice
// HUD layout/glyph contract. Pure C++14 header shared by game builds and host
// ASan/UBSan + mutant test suites.
#ifndef CE_CARRIER_CLASH_H
#define CE_CARRIER_CLASH_H

#include "ce_carrier_clash_world.h"
#include "ce_entity_handles.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum CeCarrierClashPhase {
	CE_CC_INACTIVE = 0,
	CE_CC_ARMED,
	CE_CC_ACTIVE,
	CE_CC_VICTORY,
	CE_CC_DEFEAT,
};

struct CeCarrierClashWaveCaps {
	int maxWorldUnits;       // strict mThingHeap & CEventManager safety ceiling
	int maxForsetiFighters;
	int maxMuspellFighters;
	float waveIntervalSec;
	float minSpawnCooldownSec;
	float patrolStepSec;
};

inline CeCarrierClashWaveCaps CeCarrierClashDefaultCaps() {
	CeCarrierClashWaveCaps caps = {};
	caps.maxWorldUnits = 96;
	caps.maxForsetiFighters = 6;
	caps.maxMuspellFighters = 8;
	caps.waveIntervalSec = 24.0f;
	caps.minSpawnCooldownSec = 2.0f;
	caps.patrolStepSec = 14.0f;
	return caps;
}

struct CeCarrierClashSpawnOrder {
	const char *unitName;
	int allegiance;
	float x, y, z;
	float yaw;
};

struct CeCarrierClashPatrolOrder {
	bool valid;
	float x, y, z;
};

struct CeCarrierClashHudState {
	bool active;
	CeCarrierClashPhase phase;
	int venturerPct;       // 0..100
	int fenrirPct;         // 0..100
	int forsetiEscorts;
	int muspellEscorts;
	int waveNumber;
	float elapsedSec;
	char bannerText[64];   // strictly FONT_SMALL-safe ASCII
	char venturerText[48]; // strictly FONT_SMALL-safe ASCII
	char fenrirText[48];   // strictly FONT_SMALL-safe ASCII
};

// FONT_SMALL lacks ~ \ _ $ @ [ ] ^ { | } — reject them strictly so HUD text
// never renders missing-glyph boxes or silent gaps.
inline bool CeCarrierClashIsFontSmallSafeChar(char c) {
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

inline bool CeCarrierClashIsFontSmallSafeText(const char *text) {
	if (!text) return false;
	for (const char *p = text; *p; ++p) {
		if (!CeCarrierClashIsFontSmallSafeChar(*p)) return false;
	}
	return true;
}

inline void CeCarrierClashSanitizeHudText(const char *src, char *dst, size_t dstSize) {
	if (!dst || dstSize == 0u) return;
	if (!src) { dst[0] = '\0'; return; }
	size_t out = 0u;
	for (size_t i = 0u; src[i] && out + 1u < dstSize; ++i) {
		dst[out++] = CeCarrierClashIsFontSmallSafeChar(src[i]) ? src[i] : ' ';
	}
	dst[out] = '\0';
}

// Pure 9-slice top-center status strip metrics (exact fixed circular corners,
// mirrored left/right symmetry, >= 3.2% screen safe margins).
struct CeCarrierClashHudRect {
	float x, y, w, h;
};

struct CeCarrierClashHudLayout {
	bool valid;
	float scale;
	float cornerRadius;
	float strokeWidth;
	float glowPad;
	CeCarrierClashHudRect panel;
	CeCarrierClashHudRect venturerBar;
	CeCarrierClashHudRect fenrirBar;
	CeCarrierClashHudRect bannerBox;
	bool showBanner;
};

inline CeCarrierClashHudLayout CeCarrierClashComputeHudLayout(float viewportW, float viewportH,
                                                              float uiScale, bool showBanner) {
	CeCarrierClashHudLayout out = {};
	if (!(viewportW >= 320.0f && viewportH >= 240.0f && uiScale > 0.25f)) return out;
	const float s = uiScale;
	const float safeX = floorf(viewportW * 0.035f);
	const float safeTop = floorf(viewportH * 0.032f);
	const float maxPanelW = viewportW - 2.0f * safeX;
	float panelW = floorf(460.0f * s);
	if (panelW > maxPanelW) panelW = maxPanelW;
	if (panelW < 280.0f) panelW = 280.0f;
	const float panelH = floorf(42.0f * s);
	const float panelX = floorf((viewportW - panelW) * 0.5f);
	const float panelY = safeTop > floorf(12.0f * s) ? safeTop : floorf(12.0f * s);

	float r = floorf(13.0f * s);
	if (r > panelH * 0.45f) r = floorf(panelH * 0.45f);
	if (r < 4.0f) r = 4.0f;
	float glow = floorf(1.5f * s + 0.5f);
	if (glow < 1.0f) glow = 1.0f;
	float stroke = floorf(2.0f * s + 0.5f);
	if (stroke < 1.5f) stroke = 1.5f;

	const float padX = floorf(16.0f * s);
	const float gapMid = floorf(18.0f * s);
	const float barW = floorf((panelW - 2.0f * padX - gapMid) * 0.5f);
	const float barH = floorf(9.0f * s);
	const float barY = panelY + panelH - barH - floorf(7.0f * s);

	out.valid = true;
	out.scale = s;
	out.cornerRadius = r;
	out.strokeWidth = stroke;
	out.glowPad = glow;
	out.panel = {panelX, panelY, panelW, panelH};
	out.venturerBar = {panelX + padX, barY, barW, barH};
	out.fenrirBar = {panelX + panelW - padX - barW, barY, barW, barH};
	out.showBanner = showBanner;
	if (showBanner) {
		float bw = floorf(340.0f * s);
		if (bw > maxPanelW) bw = maxPanelW;
		const float bh = floorf(32.0f * s);
		out.bannerBox = {floorf((viewportW - bw) * 0.5f), panelY + panelH + floorf(8.0f * s), bw, bh};
	}
	return out;
}

inline int CeCarrierClashComputePercent(float life, float maxLife, bool alive) {
	if (!alive || !(life > 0.0f)) return 0;
	if (!(maxLife > 0.0f)) return 100;
	const float ratio = life / maxLife;
	if (ratio <= 0.0f) return 0;
	if (ratio >= 1.0f) return 100;
	int pct = static_cast<int>(ratio * 100.0f + 0.5f);
	if (pct == 0) pct = 1; // alive with >0 HP still shows 1% until destroyed
	if (pct > 100) pct = 100;
	return pct;
}

class CeCarrierClashDirector {
	static constexpr unsigned kMaxTrackedEscorts = 64u;
	struct EscortSlot {
		CeEntityHandle handle;
		int allegiance;
		bool isFighter;
	};

	CeCarrierClashPhase phase_;
	CeCarrierClashWaveCaps caps_;
	CeEntityHandle venturerHandle_;
	CeEntityHandle fenrirHandle_;
	bool venturerSeen_;
	bool fenrirSeen_;
	bool venturerAlive_;
	bool fenrirAlive_;
	float venturerLife_;
	float venturerMaxLife_;
	float fenrirLife_;
	float fenrirMaxLife_;
	float elapsedSec_;
	float waveTimerSec_;
	float spawnCooldownSec_;
	float patrolTimerSec_;
	int waveNumber_;
	int patrolWaypointIdx_;
	int totalEscortsSpawned_;
	bool outcomeDeclared_;
	EscortSlot escorts_[kMaxTrackedEscorts];
	unsigned escortCount_;

	static bool IsWildcatOrForsetiFighter(const char *name) {
		return CeCarrierClashWorld::StrEqualsIgnoreCase(name, CeCarrierClashWorld::kWildcatJasonUnit) ||
		       CeCarrierClashWorld::StrEqualsIgnoreCase(name, CeCarrierClashWorld::kWildcatBillyUnit) ||
		       CeCarrierClashWorld::StrEqualsIgnoreCase(name, CeCarrierClashWorld::kWildcatTaraUnit) ||
		       CeCarrierClashWorld::StrEqualsIgnoreCase(name, "Air Fighter");
	}

	static bool IsMuspellFighter(const char *name) {
		return CeCarrierClashWorld::StrEqualsIgnoreCase(name, CeCarrierClashWorld::kMuspellFighterUnit);
	}

	static bool IsForsetiCruiser(const char *name) {
		return CeCarrierClashWorld::StrEqualsIgnoreCase(name, CeCarrierClashWorld::kEscortCruiserUnit);
	}

	void TrackEscort(CeEntityHandle handle, int allegiance, bool isFighter) {
		if (!CeEntityHandleIsValid(handle)) return;
		for (unsigned i = 0; i < escortCount_; ++i) {
			if (escorts_[i].handle == handle) return;
		}
		if (escortCount_ < kMaxTrackedEscorts) {
			escorts_[escortCount_++] = {handle, allegiance, isFighter};
		}
	}

	bool UntrackEscort(CeEntityHandle handle) {
		if (!CeEntityHandleIsValid(handle)) return false;
		for (unsigned i = 0; i < escortCount_; ++i) {
			if (escorts_[i].handle == handle) {
				escorts_[i] = escorts_[--escortCount_];
				return true;
			}
		}
		return false;
	}

	void ResolveOutcomeIfNeeded() {
		if (phase_ != CE_CC_ACTIVE) return;
		if (fenrirSeen_ && !fenrirAlive_) {
			phase_ = CE_CC_VICTORY;
			return;
		}
		if (venturerSeen_ && !venturerAlive_) {
			phase_ = CE_CC_DEFEAT;
			return;
		}
	}

public:
	CeCarrierClashDirector() { Reset(); }

	void Reset(const CeCarrierClashWaveCaps &caps = CeCarrierClashDefaultCaps()) {
		phase_ = CE_CC_INACTIVE;
		caps_ = caps;
		venturerHandle_ = kCeEntityHandleInvalid;
		fenrirHandle_ = kCeEntityHandleInvalid;
		venturerSeen_ = false;
		fenrirSeen_ = false;
		venturerAlive_ = false;
		fenrirAlive_ = false;
		venturerLife_ = 0.0f;
		venturerMaxLife_ = 0.0f;
		fenrirLife_ = 0.0f;
		fenrirMaxLife_ = 0.0f;
		elapsedSec_ = 0.0f;
		waveTimerSec_ = 0.0f;
		spawnCooldownSec_ = 0.0f;
		patrolTimerSec_ = 0.0f;
		waveNumber_ = 0;
		patrolWaypointIdx_ = 0;
		totalEscortsSpawned_ = 0;
		outcomeDeclared_ = false;
		escortCount_ = 0u;
	}

	void Arm() {
		Reset(caps_);
		phase_ = CE_CC_ARMED;
	}

	void BeginLevel() {
		const CeCarrierClashWaveCaps kept = caps_;
		Reset(kept);
		phase_ = CE_CC_ACTIVE;
		patrolTimerSec_ = caps_.patrolStepSec; // issue initial patrol immediately on tick 1
	}

	void EndLevel(bool keepArmed) {
		const CeCarrierClashWaveCaps kept = caps_;
		Reset(kept);
		phase_ = keepArmed ? CE_CC_ARMED : CE_CC_INACTIVE;
	}

	CeCarrierClashPhase Phase() const { return phase_; }
	bool IsActiveOrArmed() const { return phase_ != CE_CC_INACTIVE; }
	bool IsRunning() const {
		return phase_ == CE_CC_ACTIVE || phase_ == CE_CC_VICTORY || phase_ == CE_CC_DEFEAT;
	}
	CeEntityHandle VenturerHandle() const { return venturerHandle_; }
	CeEntityHandle FenrirHandle() const { return fenrirHandle_; }
	int WaveNumber() const { return waveNumber_; }
	int TotalEscortsSpawned() const { return totalEscortsSpawned_; }
	bool OutcomeDeclared() const { return outcomeDeclared_; }
	void MarkOutcomeDeclared() { outcomeDeclared_ = true; }

	int CountEscorts(int allegiance, bool fightersOnly) const {
		int n = 0;
		for (unsigned i = 0; i < escortCount_; ++i) {
			if (escorts_[i].allegiance == allegiance && (!fightersOnly || escorts_[i].isFighter))
				++n;
		}
		return n;
	}

	void OnUnitSpawn(CeEntityHandle handle, const char *unitName, int allegiance,
	                 float life, float maxLife) {
		if (!IsRunning() || !CeEntityHandleIsValid(handle) || !unitName) return;
		if (CeCarrierClashWorld::IsForbiddenUnit(unitName)) return;

		if (CeCarrierClashWorld::StrEqualsIgnoreCase(unitName, CeCarrierClashWorld::kVenturerUnit) &&
		    allegiance == CeCarrierClashWorld::kAllegianceForseti) {
			if (!venturerSeen_) {
				venturerHandle_ = handle;
				venturerSeen_ = true;
				venturerAlive_ = life > 0.0f;
				venturerLife_ = life > 0.0f ? life : 0.0f;
				venturerMaxLife_ = maxLife > 0.0f ? maxLife : venturerLife_;
			}
			return;
		}
		if ((CeCarrierClashWorld::StrEqualsIgnoreCase(unitName, CeCarrierClashWorld::kFenrirUnit) ||
		     CeCarrierClashWorld::StrEqualsIgnoreCase(unitName, CeCarrierClashWorld::kFenrirAltUnit)) &&
		    allegiance == CeCarrierClashWorld::kAllegianceMuspell) {
			if (!fenrirSeen_) {
				fenrirHandle_ = handle;
				fenrirSeen_ = true;
				fenrirAlive_ = life > 0.0f;
				fenrirLife_ = life > 0.0f ? life : 0.0f;
				fenrirMaxLife_ = maxLife > 0.0f ? maxLife : fenrirLife_;
			}
			return;
		}
		if (IsWildcatOrForsetiFighter(unitName)) {
			TrackEscort(handle, CeCarrierClashWorld::kAllegianceForseti, true);
		} else if (IsMuspellFighter(unitName)) {
			TrackEscort(handle, CeCarrierClashWorld::kAllegianceMuspell, true);
		} else if (IsForsetiCruiser(unitName)) {
			TrackEscort(handle, CeCarrierClashWorld::kAllegianceForseti, false);
		}
	}

	void OnUnitDestroyed(CeEntityHandle handle) {
		if (!IsRunning() || !CeEntityHandleIsValid(handle)) return;
		if (handle == fenrirHandle_) {
			fenrirAlive_ = false;
			fenrirLife_ = 0.0f;
			ResolveOutcomeIfNeeded();
			return;
		}
		if (handle == venturerHandle_) {
			venturerAlive_ = false;
			venturerLife_ = 0.0f;
			ResolveOutcomeIfNeeded();
			return;
		}
		UntrackEscort(handle);
	}

	void OnPlayerLost() {
		if (phase_ == CE_CC_ACTIVE) {
			phase_ = CE_CC_DEFEAT;
		}
	}

	void UpdateFlagshipTelemetry(float venturerLife, float venturerMaxLife, bool venturerAlive,
	                             float fenrirLife, float fenrirMaxLife, bool fenrirAlive) {
		if (!IsRunning()) return;
		if (venturerSeen_) {
			venturerAlive_ = venturerAlive && (venturerLife > 0.0f);
			venturerLife_ = venturerAlive_ ? venturerLife : 0.0f;
			if (venturerMaxLife > venturerMaxLife_) venturerMaxLife_ = venturerMaxLife;
		}
		if (fenrirSeen_) {
			fenrirAlive_ = fenrirAlive && (fenrirLife > 0.0f);
			fenrirLife_ = fenrirAlive_ ? fenrirLife : 0.0f;
			if (fenrirMaxLife > fenrirMaxLife_) fenrirMaxLife_ = fenrirMaxLife;
		}
		ResolveOutcomeIfNeeded();
	}

	// Advances the simulation by `dt` seconds. Emits at most `maxOutSpawns`
	// strictly-capped escort spawns and optional flagship patrol waypoints.
	void Tick(float dt, int liveWorldUnits,
	          CeCarrierClashSpawnOrder *outSpawns, int maxOutSpawns, int *outSpawnCount,
	          CeCarrierClashPatrolOrder *outVenturerPatrol,
	          CeCarrierClashPatrolOrder *outFenrirPatrol) {
		if (outSpawnCount) *outSpawnCount = 0;
		if (outVenturerPatrol) *outVenturerPatrol = {false, 0.0f, 0.0f, 0.0f};
		if (outFenrirPatrol) *outFenrirPatrol = {false, 0.0f, 0.0f, 0.0f};
		if (phase_ != CE_CC_ACTIVE || !(dt > 0.0f) || dt > 5.0f) return;

		elapsedSec_ += dt;
		waveTimerSec_ += dt;
		patrolTimerSec_ += dt;
		if (spawnCooldownSec_ > 0.0f) {
			spawnCooldownSec_ -= dt;
			if (spawnCooldownSec_ < 0.0f) spawnCooldownSec_ = 0.0f;
		}

		// Opposite broadside patrol loop keeping both flagships cleanly separated
		// inside the deep central ocean strait (x in [198..320], y in [220..286]).
		if (patrolTimerSec_ >= caps_.patrolStepSec && venturerAlive_ && fenrirAlive_) {
			patrolTimerSec_ = 0.0f;
			static const float kVenturerPts[4][3] = {
				{216.0f, 236.0f, -32.0f},
				{204.0f, 268.0f, -32.0f},
				{238.0f, 282.0f, -32.0f},
				{240.0f, 224.0f, -32.0f},
			};
			static const float kFenrirPts[4][3] = {
				{304.0f, 240.0f, -36.0f},
				{314.0f, 274.0f, -36.0f},
				{282.0f, 286.0f, -36.0f},
				{280.0f, 226.0f, -36.0f},
			};
			const int idx = patrolWaypointIdx_ & 3;
			++patrolWaypointIdx_;
			if (outVenturerPatrol) {
				*outVenturerPatrol = {true, kVenturerPts[idx][0], kVenturerPts[idx][1], kVenturerPts[idx][2]};
			}
			if (outFenrirPatrol) {
				*outFenrirPatrol = {true, kFenrirPts[idx][0], kFenrirPts[idx][1], kFenrirPts[idx][2]};
			}
		}

		// Capped dynamic escort wave spawner: never exceeds mThingHeap safety cap.
		if (waveTimerSec_ >= caps_.waveIntervalSec &&
		    spawnCooldownSec_ <= 0.0f &&
		    venturerAlive_ && fenrirAlive_ &&
		    outSpawns && maxOutSpawns > 0 && outSpawnCount) {
			waveTimerSec_ = 0.0f;
			const int headroom = caps_.maxWorldUnits - liveWorldUnits;
			if (headroom >= 2) {
				int emitted = 0;
				const int forsetiAir = CountEscorts(CeCarrierClashWorld::kAllegianceForseti, true);
				const int muspellAir = CountEscorts(CeCarrierClashWorld::kAllegianceMuspell, true);
				if (forsetiAir < caps_.maxForsetiFighters && emitted < maxOutSpawns && emitted < headroom) {
					static const char *const kWildcats[3] = {
						CeCarrierClashWorld::kWildcatJasonUnit,
						CeCarrierClashWorld::kWildcatBillyUnit,
						CeCarrierClashWorld::kWildcatTaraUnit,
					};
					const char *def = kWildcats[waveNumber_ % 3];
					outSpawns[emitted++] = {
						def,
						CeCarrierClashWorld::kAllegianceForseti,
						218.0f + float((waveNumber_ % 3) * 14),
						214.0f + float(((waveNumber_ + 1) % 3) * 12),
						-58.0f,
						-0.78539816f,
					};
				}
				if (muspellAir < caps_.maxMuspellFighters && emitted < maxOutSpawns && emitted < headroom) {
					outSpawns[emitted++] = {
						CeCarrierClashWorld::kMuspellFighterUnit,
						CeCarrierClashWorld::kAllegianceMuspell,
						318.0f - float((waveNumber_ % 3) * 14),
						320.0f - float(((waveNumber_ + 1) % 3) * 12),
						-62.0f,
						2.35619449f,
					};
				}
				if (emitted > 0) {
					++waveNumber_;
					totalEscortsSpawned_ += emitted;
					spawnCooldownSec_ = caps_.minSpawnCooldownSec;
					*outSpawnCount = emitted;
				}
			}
		}
	}

	CeCarrierClashHudState BuildHudState() const {
		CeCarrierClashHudState hud = {};
		hud.active = IsRunning();
		hud.phase = phase_;
		hud.venturerPct = venturerSeen_
			? CeCarrierClashComputePercent(venturerLife_, venturerMaxLife_, venturerAlive_)
			: 100;
		hud.fenrirPct = fenrirSeen_
			? CeCarrierClashComputePercent(fenrirLife_, fenrirMaxLife_, fenrirAlive_)
			: 100;
		hud.forsetiEscorts = CountEscorts(CeCarrierClashWorld::kAllegianceForseti, false);
		hud.muspellEscorts = CountEscorts(CeCarrierClashWorld::kAllegianceMuspell, false);
		hud.waveNumber = waveNumber_;
		hud.elapsedSec = elapsedSec_;

		char rawV[48], rawF[48];
		snprintf(rawV, sizeof(rawV), "F.S. VENTURER %d%%", hud.venturerPct);
		snprintf(rawF, sizeof(rawF), "FENRIR %d%%", hud.fenrirPct);
		CeCarrierClashSanitizeHudText(rawV, hud.venturerText, sizeof(hud.venturerText));
		CeCarrierClashSanitizeHudText(rawF, hud.fenrirText, sizeof(hud.fenrirText));

		const char *banner = "CARRIER CLASH";
		if (phase_ == CE_CC_VICTORY) {
			banner = "VICTORY - FENRIR DESTROYED";
		} else if (phase_ == CE_CC_DEFEAT) {
			banner = venturerSeen_ && !venturerAlive_
				? "DEFEAT - F.S. VENTURER LOST"
				: "DEFEAT - BATTLE ENGINE LOST";
		}
		CeCarrierClashSanitizeHudText(banner, hud.bannerText, sizeof(hud.bannerText));
		return hud;
	}
};

// Engine C ABI entrypoints (implemented in ce_carrier_clash.cpp & ce_carrier_clash_world.cpp).
class CPlayer;
class CUnit;
class CInitThing;
class CCareer;

extern "C" {
bool PortCeCarrierClashBegin(void);
bool PortCeCarrierClashActive(void);
void PortCeCarrierClashCancel(void);
const char *PortCeCarrierClashMessage(void);
bool PortCeCarrierClashLoadRefused(void);
bool PortCeCarrierClashTakeReturn(void);
bool PortCeCarrierClashTakeNotice(const char **sentence);

bool PortCeCarrierClashPreflightAndBuildWorld(void);
void PortCeCarrierClashDropWorld(void);
void PortCeCarrierClashInstallArena(void);
bool PortCeCarrierClashPreloadMeshes(void);
int PortCeCarrierClashReadWorld(const char *gamePath, const void **bytes, unsigned *length);
void PortCeCarrierClashMarkRetryReset(void);
void PortCeCarrierClashSetMessage(const char *message);

void PortCeCarrierClashLevelBegin(int level);
void PortCeCarrierClashLevelEnd(int level, int quit);
void PortCeCarrierClashTickPre(void);
void PortCeCarrierClashDrawHud(CPlayer *player, int viewpoint);
void PortCeCarrierClashDrawOverlay(void);
int PortCeCarrierClashBeInStasis(const void *be);
float PortCeCarrierClashHudTopBottom(float viewportW, float viewportH);
void PortCeCarrierClashUnitSpawn(CUnit *unit, CInitThing *init);
void PortCeCarrierClashUnitDestroyed(CUnit *unit);
void PortCeCarrierClashFilterSave(CCareer *career, void *dest);
}

#endif
