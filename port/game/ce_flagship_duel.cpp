// SPDX-License-Identifier: GPL-3.0-only
// Clean-Room Level 888 ("Flagship Duel"): live engine integration for both
// 1P Solo (level 788) and 2P Split-Screen (level 888), flagship separation
// enforcement, dynamic escort wave spawning, 9-slice HUD + split-seam tactical
// strip, and career snapshot fence.
#ifdef CE_VERSION

#include "common.h"
#include "BattleEngine.h"
#include "Career.h"
#include "engine.h"
#include "eventmanager.h"
#include "frontend.h"
#include "game.h"
#include "GameInterface.h"
#include "InitThing.h"
#include "ltshell.h"
#include "map.h"
#include "PauseMenu.h"
#include "Platform.h"
#include "player.h"
#include "renderinfo.h"
#include "squad.h"
#include "unit.h"
#include "WorldPhysicsManager.h"
#include "world.h"
#include "../platform/common/portlog.h"

#include "ce_entity_handles.h"
#include "ce_flagship_duel.h"
#include "ce_modes.h"
#include "ce_photo.h"

#include <cmath>
#include <cstdio>
#include <cstring>

bool PortCeMenus();
bool PortCeLayer();
void PortCeBonusClearDebriefing();
bool PortCeCmdMapOpen() __attribute__((weak));

extern int gControlPreset;
void ApplyControlPreset(int preset);
bool AllControlBindingsSet(int index);

extern "C" int PortCePlayUnitShowsBattleline(int viewpoint) __attribute__((weak));
extern "C" int PortCePlayUnitOwnsPilot(int player) __attribute__((weak));

namespace {

// Localized retail text.db IDs (english.dat):
//   534802956 (_231_LOSE_VENTURER): "The Venturer had to withdraw"
//   54406750  (GAME_OVER_DEATH):    "Battle Engine was destroyed."
constexpr int kVenturerLostTextId = 534802956;
constexpr int kBattleEngineLostTextId = 54406750;

CeFlagshipDuelDirector gDirector;
CeFlagshipDuelMode gMode = CeFlagshipDuelMode::kSolo1P;
bool gArmed = false;
bool gLoadRefused = false;
bool gPendingReturn = false;
bool gPendingNotice = false;
int gNoticeLevel = CeFlagshipDuelWorld::kSoloLevel;
const char *gMessage = "Flagship Duel could not be started. Your career is unchanged.";
const char *gNotice = NULL;

unsigned char gCareerSnapshot[sizeof(CCareer)];
bool gCareerSnapshotHeld = false;
bool gP0StasisApplied = false;
uint32_t gWaveSerial = 0u;

struct CeFdQuadVertex {
	float x, y, z, rhw;
	unsigned color;
};

unsigned LerpArgb(unsigned c0, unsigned c1, float t) {
	if (t <= 0.0f) return c0;
	if (t >= 1.0f) return c1;
	auto ch = [t](unsigned a, unsigned b) -> unsigned {
		const float v = float(a) + (float(b) - float(a)) * t;
		return v <= 0.0f ? 0u : (v >= 255.0f ? 255u : unsigned(v + 0.5f));
	};
	return (ch((c0 >> 24) & 0xffu, (c1 >> 24) & 0xffu) << 24) |
	       (ch((c0 >> 16) & 0xffu, (c1 >> 16) & 0xffu) << 16) |
	       (ch((c0 >> 8) & 0xffu, (c1 >> 8) & 0xffu) << 8) |
	       ch(c0 & 0xffu, c1 & 0xffu);
}

void BeginHudUntextured() {
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	LT.SRS(D3DRS_LIGHTING, FALSE);
	LT.SRS(D3DRS_FOGENABLE, FALSE);
	LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SRS(D3DRS_ZENABLE, FALSE);
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	LT.SRS(D3DRS_CULLMODE, D3DCULL_NONE);
	LT.SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}

void EndHudUntextured() {
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	LT.SRS(D3DRS_CULLMODE, D3DCULL_CCW);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
}

void RestoreHudDeviceState() {
	LT.SRS(D3DRS_CULLMODE, D3DCULL_CCW);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SRS(D3DRS_FOGENABLE, FALSE);
	LT.SRS(D3DRS_LIGHTING, FALSE);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.SetModulate(0);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

struct CeFdHudBatch {
	static constexpr int kMaxVerts = 1536;
	CeFdQuadVertex verts[kMaxVerts];
	int count;

	void Reset() { count = 0; }

	void Flush(LPDIRECT3DDEVICE9 dev) {
		if (dev && count >= 3) {
			dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(count / 3),
			                     verts, sizeof(CeFdQuadVertex));
		}
		count = 0;
	}

	void PushTri(LPDIRECT3DDEVICE9 dev, const CeFdQuadVertex &a,
	             const CeFdQuadVertex &b, const CeFdQuadVertex &c) {
		if (count + 3 > kMaxVerts) Flush(dev);
		verts[count++] = a;
		verts[count++] = b;
		verts[count++] = c;
	}

	void EmitRect(LPDIRECT3DDEVICE9 dev, float x0, float y0, float x1, float y1, unsigned col) {
		if (x1 <= x0 || y1 <= y0) return;
		const CeFdQuadVertex v0 = {x0, y0, 0.006f, 1.0f, col};
		const CeFdQuadVertex v1 = {x1, y0, 0.006f, 1.0f, col};
		const CeFdQuadVertex v2 = {x0, y1, 0.006f, 1.0f, col};
		const CeFdQuadVertex v3 = {x1, y1, 0.006f, 1.0f, col};
		PushTri(dev, v0, v1, v2);
		PushTri(dev, v1, v3, v2);
	}

	// Clean 9-slice chamfered tactical glass panel + left/right faction brackets.
	void EmitNineSlicePanel(LPDIRECT3DDEVICE9 dev, const CeFdHudRect &r, float s,
	                        unsigned topBg, unsigned botBg,
	                        unsigned leftBracketCol, unsigned rightBracketCol) {
		const float cham = 6.0f * s;
		const float x0 = r.x0, y0 = r.y0, x1 = r.x1, y1 = r.y1;
		if (x1 - x0 <= cham * 2.0f || y1 - y0 <= cham * 2.0f) {
			EmitRect(dev, x0, y0, x1, y1, topBg);
			return;
		}
		const unsigned midBg = LerpArgb(topBg, botBg, 0.5f);
		// Center + top + bottom + left + right 9-slice body
		EmitRect(dev, x0 + cham, y0, x1 - cham, y0 + cham, topBg);
		EmitRect(dev, x0, y0 + cham, x1, y1 - cham, midBg);
		EmitRect(dev, x0 + cham, y1 - cham, x1 - cham, y1, botBg);
		// 4 chamfered corner triangles
		PushTri(dev, {x0 + cham, y0, 0.008f, 1.0f, topBg},
		             {x0 + cham, y0 + cham, 0.008f, 1.0f, midBg},
		             {x0, y0 + cham, 0.008f, 1.0f, midBg});
		PushTri(dev, {x1 - cham, y0, 0.008f, 1.0f, topBg},
		             {x1, y0 + cham, 0.008f, 1.0f, midBg},
		             {x1 - cham, y0 + cham, 0.008f, 1.0f, midBg});
		PushTri(dev, {x0, y1 - cham, 0.008f, 1.0f, midBg},
		             {x0 + cham, y1 - cham, 0.008f, 1.0f, midBg},
		             {x0 + cham, y1, 0.008f, 1.0f, botBg});
		PushTri(dev, {x1 - cham, y1 - cham, 0.008f, 1.0f, midBg},
		             {x1, y1 - cham, 0.008f, 1.0f, midBg},
		             {x1 - cham, y1, 0.008f, 1.0f, botBg});

		// Left & right glowing tactical brackets
		const float bw = (2.0f * s < 1.5f) ? 1.5f : 2.0f * s;
		const float lip = 18.0f * s;
		EmitRect(dev, x0, y0 + cham, x0 + bw, y1 - cham, leftBracketCol);
		EmitRect(dev, x0 + cham, y0, x0 + cham + lip, y0 + bw, leftBracketCol);
		EmitRect(dev, x0 + cham, y1 - bw, x0 + cham + lip, y1, leftBracketCol);

		EmitRect(dev, x1 - bw, y0 + cham, x1, y1 - cham, rightBracketCol);
		EmitRect(dev, x1 - cham - lip, y0, x1 - cham, y0 + bw, rightBracketCol);
		EmitRect(dev, x1 - cham - lip, y1 - bw, x1 - cham, y1, rightBracketCol);
	}
};

CeFdHudBatch gHudBatch;

bool IsP0DrivingOtherUnit() {
	return (PortCePlayUnitShowsBattleline && PortCePlayUnitShowsBattleline(0) != 0) ||
	       (PortCePlayUnitOwnsPilot && PortCePlayUnitOwnsPilot(0) != 0);
}

void CaptureCareerSnapshot() {
	std::memcpy((void *)gCareerSnapshot, (const void *)&CAREER, sizeof(CCareer));
	gCareerSnapshotHeld = true;
}

void ClearCareerSnapshot() {
	gCareerSnapshotHeld = false;
}

void SetRefusal(const char *msg) {
	gMessage = (msg && msg[0])
		? msg
		: "Flagship Duel could not be started. Your career is unchanged.";
}

void AsciiToSafeWChar(const char *src, WCHAR *dst, size_t cap) {
	if (!dst || cap == 0u) return;
	size_t o = 0u;
	if (src) {
		for (size_t i = 0u; src[i] != '\0' && o + 1u < cap; ++i) {
			const char c = src[i];
			dst[o++] = CeFlagshipDuelIsFontSmallSafeChar(c)
				? static_cast<WCHAR>(static_cast<unsigned char>(c))
				: static_cast<WCHAR>(' ');
		}
	}
	dst[o] = 0;
}

void DrawAsciiSmallFit(CDXBitmapFont *font, float x, float y, float scale,
                       unsigned color, const char *text, float maxW = 0.0f) {
	if (!font || !text || !text[0]) return;
	WCHAR wbuf[128] = {};
	AsciiToSafeWChar(text, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));
	SIZE ext = {0, 0};
	font->GetTextExtent(wbuf, &ext);
	float sx = scale;
	if (maxW > 8.0f && ext.cx > 0 && float(ext.cx) * sx > maxW) {
		sx = maxW / float(ext.cx);
		if (sx < scale * 0.55f) sx = scale * 0.55f;
	}
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, FALSE);
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	font->DrawTextWithShadow(x, y, color, wbuf, 0, 0.0f, sx, scale);
}

void DrawAsciiSmallCentered(CDXBitmapFont *font, float cx, float y, float scale,
                            unsigned color, const char *text, float maxW) {
	if (!font || !text || !text[0]) return;
	WCHAR wbuf[128] = {};
	AsciiToSafeWChar(text, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));
	SIZE ext = {0, 0};
	font->GetTextExtent(wbuf, &ext);
	float sx = scale;
	if (maxW > 8.0f && ext.cx > 0 && float(ext.cx) * sx > maxW) {
		sx = maxW / float(ext.cx);
		if (sx < scale * 0.55f) sx = scale * 0.55f;
	}
	const float drawW = float(ext.cx) * sx;
	DrawAsciiSmallFit(font, std::floor(cx - drawW * 0.5f), y, scale, color, text, maxW);
}

void DrawAsciiSmallRight(CDXBitmapFont *font, float rightX, float y, float scale,
                         unsigned color, const char *text, float maxW) {
	if (!font || !text || !text[0]) return;
	WCHAR wbuf[128] = {};
	AsciiToSafeWChar(text, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));
	SIZE ext = {0, 0};
	font->GetTextExtent(wbuf, &ext);
	float sx = scale;
	if (maxW > 8.0f && ext.cx > 0 && float(ext.cx) * sx > maxW) {
		sx = maxW / float(ext.cx);
		if (sx < scale * 0.55f) sx = scale * 0.55f;
	}
	const float drawW = float(ext.cx) * sx;
	DrawAsciiSmallFit(font, std::floor(rightX - drawW), y, scale, color, text, maxW);
}

bool IsHandleAliveCallback(CeEntityHandle h, void *) {
	CUnit *u = CeEntityHandleResolveUnit(h);
	return u != NULL && !u->IsDying() && !u->IsShuttingDown() && u->GetLife() > 0.0f;
}

float ComputeUnitHealthFraction(CUnit *u) {
	if (!u || u->IsDying() || u->IsShuttingDown() || u->GetLife() <= 0.0f) return 0.0f;
	const float maxLife = u->GetMaxLife();
	if (maxLife <= 0.001f) return 1.0f;
	const float cur = u->GetLife();
	if (cur >= maxLife) return 1.0f;
	return cur / maxLife;
}

uint32_t CountLiveUnitsInWorld() {
	uint32_t count = 0u;
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next()) {
		if (u && !u->IsDying() && !u->IsShuttingDown() && !u->IsA(THING_TYPE_COMPONENT))
			++count;
	}
	return count;
}

bool SpawnEscortFighter(int allegiance, uint32_t slotIndex, CUnit *carrier) {
	static const char *const kForsetiDefs[3] = {
		"Jason Fighter",
		"Billy Fighter",
		"Tara Fighter",
	};
	const char *defName = (allegiance == 0)
		? kForsetiDefs[slotIndex % 3u]
		: "Muspell Advanced Fighter";
	if (CeFlagshipDuelWorld::IsForbiddenUnit(defName))
		return false;

	CUnitData *stats = UPhysicsManager::GetUnitData(const_cast<char *>(defName));
	if (!stats) return false;

	FVector basePos = (allegiance == 0)
		? FVector(CeFlagshipDuelWorld::kVenturerStartX, CeFlagshipDuelWorld::kVenturerStartY, -60.0f)
		: FVector(CeFlagshipDuelWorld::kFenrirStartX, CeFlagshipDuelWorld::kFenrirStartY, -60.0f);
	if (carrier && !carrier->IsDying())
		basePos = carrier->GetPos();

	const float sideSign = (slotIndex & 1u) ? 1.0f : -1.0f;
	const float spreadX = sideSign * (32.0f + float((slotIndex % 3u) * 14u));
	const float forwardY = (allegiance == 0) ? -34.0f : 34.0f;
	const float spawnX = CeFlagshipDuelClampF(basePos.X + spreadX, kCeFdArenaMinX, kCeFdArenaMaxX);
	const float spawnY = CeFlagshipDuelClampF(basePos.Y + forwardY, kCeFdArenaMinY, kCeFdArenaMaxY);
	const float spawnZ = (basePos.Z < -56.0f) ? (basePos.Z - 16.0f) : -76.0f;

	CUnitInitThing init;
	init.mPos = FVector(spawnX, spawnY, spawnZ);
	init.mYaw = (allegiance == 0) ? CeFlagshipDuelWorld::kPi : 0.0f;
	init.mPitch = 0.0f;
	init.mRoll = 0.0f;
	init.mAllegiance = static_cast<EAllegiance>(allegiance);
	init.mActive = TRUE;
	init.mStats = stats;

	CSquad *squad = UPhysicsManager::SpawnSquad(stats->mBehaviour);
	if (squad) {
		CSquadInitThing sinit;
		sinit.Copy(&init);
		std::snprintf(sinit.mUnitName, sizeof(sinit.mUnitName), "%s", defName);
		sinit.mAmount = 1;
		squad->mUnknown84 = TRUE;
		squad->Init(&sinit);
		return true;
	}

	const SINT unitIdx = UPhysicsManager::GetUnitIndex(const_cast<char *>(defName));
	if (unitIdx < 0) return false;
	CThing *thing = UPhysicsManager::SpawnThing(unitIdx);
	if (!thing) return false;
	thing->Init(&init);
	return true;
}

void UpdateP0PlayUnitStasis() {
	if (gDirector.IsSplitScreen2P()) return;
	CPlayer *p0 = GAME.GetPlayer(0);
	CBattleEngine *be = p0 ? p0->GetBattleEngine() : NULL;
	if (!be || be->IsDying()) {
		gP0StasisApplied = false;
		return;
	}

	const bool drivingOther = IsP0DrivingOtherUnit();
	if (drivingOther) {
		const float maxLife = be->GetMaxLife();
		if (maxLife > 0.0f && be->mLife < maxLife) be->mLife = maxLife;
		if (be->mMaxShields > 0.0f && be->mShields < be->mMaxShields) be->mShields = be->mMaxShields;
		const float maxEnergy = be->GetMaxEnergy();
		if (maxEnergy > 0.0f && be->mEnergy < maxEnergy) be->mEnergy = maxEnergy;
		be->SetVelocity(FVector(0.0f, 0.0f, 0.0f));
		if (!gP0StasisApplied) {
			be->SetVulnerable(FALSE);
			gP0StasisApplied = true;
		}
		return;
	}

	const bool outcomeShield = gDirector.Phase() == CeFlagshipDuelPhase::kVictoryHold ||
	                           gDirector.Phase() == CeFlagshipDuelPhase::kFinishedVictory;
	if (outcomeShield) {
		be->SetVulnerable(FALSE);
	} else if (gP0StasisApplied) {
		be->SetVulnerable(TRUE);
		gP0StasisApplied = false;
	}
}

void EnforceFlagshipSeparation(CUnit *venturer, CUnit *fenrir) {
	if (!venturer || !fenrir || venturer->IsDying() || fenrir->IsDying())
		return;

	const FVector vp = venturer->GetPos();
	const FVector fp = fenrir->GetPos();

	if (gDirector.UpdateFlagshipPositions(vp.X, vp.Y, vp.Z, fp.X, fp.Y, fp.Z)) {
		CeFdPatrolPoint vg{}, fg{};
		if (gDirector.ConsumeSeparationRedirect(&vg, &fg)) {
			venturer->GoToPoint(FVector(vg.x, vg.y, vg.z), FALSE);
			fenrir->GoToPoint(FVector(fg.x, fg.y, fg.z), FALSE);
		}
	}

	const CeFdSeparationPush push = CeFlagshipDuelComputeSeparationPush(
		vp.X, vp.Y, fp.X, fp.Y, kCeFdHardSeparationDist, 0.75f);
	if (push.active) {
		venturer->MoveTo(FVector(push.venturerNewX, push.venturerNewY, vp.Z));
		fenrir->MoveTo(FVector(push.fenrirNewX, push.fenrirNewY, fp.Z));

		FVector vv = venturer->GetVelocity();
		const float vProj = vv.X * push.nx + vv.Y * push.ny; // < 0 means Venturer moving toward Fenrir
		if (vProj < 0.0f) {
			vv.X -= vProj * push.nx;
			vv.Y -= vProj * push.ny;
			venturer->SetVelocity(vv);
		}

		FVector fv = fenrir->GetVelocity();
		const float fProj = fv.X * push.nx + fv.Y * push.ny; // > 0 means Fenrir moving toward Venturer
		if (fProj > 0.0f) {
			fv.X -= fProj * push.nx;
			fv.Y -= fProj * push.ny;
			fenrir->SetVelocity(fv);
		}
	}
}

bool BeginModeCommon(CeFlagshipDuelMode mode) {
	if (gArmed) return false;
	gLoadRefused = false;
	gPendingReturn = false;
	gPendingNotice = false;
	gNotice = NULL;
	gMode = mode;
	gNoticeLevel = CeFlagshipDuelWorld::LevelForMode(mode);

	if (!PortCeFlagshipDuelPreflightAndBuildWorld(static_cast<int>(mode))) {
		gArmed = false;
		return false;
	}

	// In 2-player local split-screen, automatically promote the default 1P
	// keyboard preset (preset 1) to the 2P split-keyboard preset (preset 2) if
	// Player 2 has no bindings yet, matching FEPMultiplayer.cpp (patch 0382).
	// If Player 2 still lacks bindings (e.g. custom P1 layout), refuse cleanly
	// instead of launching an uncontrollable P2 into split-screen.
	if (mode == CeFlagshipDuelMode::kSplitScreen2P && !AllControlBindingsSet(1)) {
		if (gControlPreset == 1) {
			ApplyControlPreset(2);
		}
		if (!AllControlBindingsSet(1)) {
			PortCeFlagshipDuelDropWorld();
			gArmed = false;
			SetRefusal("Flagship Duel (2P) needs Player 2 controls bound. Configure them in Options > Controls.");
			return false;
		}
	}

	if (CeModeBegin(CE_MODE_FLAGSHIP_DUEL) != CE_MODE_BEGUN) {
		PortCeFlagshipDuelDropWorld();
		gArmed = false;
		SetRefusal("Close other special modes before starting Flagship Duel.");
		return false;
	}
	CaptureCareerSnapshot();
	CeCareerSnapshotAcquire(CE_MODE_FLAGSHIP_DUEL, gCareerSnapshot, sizeof(CCareer));
	gArmed = true;
	gWaveSerial = 0u;
	CeFlagshipDuelConfig cfg{};
	cfg.mode = mode;
	cfg.maxAliveEscortsPerSide = (mode == CeFlagshipDuelMode::kSplitScreen2P) ? 3u : 4u;
	gDirector.Reset(cfg);
	return true;
}

bool ShouldSuppressGameplayHud(bool endState) {
	const bool pauseMenuOpen = (GAME.GetPauseMenu() && GAME.GetPauseMenu()->mActive);
	if (PortCePhotoOn() || GAMEINTERFACE.IsMenuUp() || pauseMenuOpen) return true;
	if (PortCeCmdMapOpen && PortCeCmdMapOpen()) return true;
	if (!endState && GAME.IsPaused()) return true;
	if (!endState && !(ENGINE.GetDrawDebugStuff() & DRAW_COCKPIT)) return true;
	return false;
}

void EmitHullBarsToBatch(LPDIRECT3DDEVICE9 dev, const CeFdHudLayout &layout,
                         bool dangerPulse) {
	const float s = layout.uiScale;
	unsigned vFillCol = gDirector.IsVenturerInDanger()
		? (dangerPulse ? 0xFFF59E0Bu : 0xFF7CB8E8u)
		: 0xF032D8FFu;
	if (gDirector.VenturerFlashSec() > 0.0f)
		vFillCol = 0xFFFFFFFFu;

	unsigned fFillCol = gDirector.IsFenrirInDanger()
		? (dangerPulse ? 0xFFFF8844u : 0xFFE84848u)
		: 0xF0FF4C28u;
	if (gDirector.FenrirFlashSec() > 0.0f)
		fFillCol = 0xFFFFFFFFu;

	gHudBatch.EmitRect(dev, layout.venturerBarOuter.x0, layout.venturerBarOuter.y0,
	                   layout.venturerBarOuter.x1, layout.venturerBarOuter.y1, 0xC8081622u);
	gHudBatch.EmitRect(dev, layout.venturerBarFill.x0, layout.venturerBarFill.y0,
	                   layout.venturerBarFill.x1, layout.venturerBarFill.y1, vFillCol);

	gHudBatch.EmitRect(dev, layout.fenrirBarOuter.x0, layout.fenrirBarOuter.y0,
	                   layout.fenrirBarOuter.x1, layout.fenrirBarOuter.y1, 0xC8220A08u);
	gHudBatch.EmitRect(dev, layout.fenrirBarFill.x0, layout.fenrirBarFill.y0,
	                   layout.fenrirBarFill.x1, layout.fenrirBarFill.y1, fFillCol);

	gHudBatch.EmitRect(dev, layout.centerDivider.x0, layout.centerDivider.y0,
	                   layout.centerDivider.x1, layout.centerDivider.y1, 0x886A8296u);
	(void)s;
}

// Read-only display phase for CE_HOOK_RENDER: bridges the single render frame
// between an external CGame state transition during WORLD.Move and the next
// PortCeFlagshipDuelTickPre without ever mutating gDirector from render.
CeFlagshipDuelPhase EffectiveDisplayPhase(EGameState gs) {
	const CeFlagshipDuelPhase phase = gDirector.Phase();
	if (gDirector.IsSplitScreen2P()) {
		if (gs == GAME_STATE_PLAYER_1_WON && phase != CeFlagshipDuelPhase::kFinishedP2Won)
			return CeFlagshipDuelPhase::kFinishedP1Won;
		if (gs == GAME_STATE_PLAYER_2_WON && phase != CeFlagshipDuelPhase::kFinishedP1Won)
			return CeFlagshipDuelPhase::kFinishedP2Won;
	} else if (gs == GAME_STATE_LEVEL_LOST && phase == CeFlagshipDuelPhase::kRunning) {
		return CeFlagshipDuelPhase::kFinishedDefeat;
	}
	return phase;
}

void RenderOutcomeBanner(LPDIRECT3DDEVICE9 dev, CDXBitmapFont *font,
                         const CeFdHudLayout &layout, CeFlagshipDuelPhase phase) {
	const bool split2P = gDirector.IsSplitScreen2P();
	const bool p1SideWon = (phase == CeFlagshipDuelPhase::kVictoryHold ||
	                        phase == CeFlagshipDuelPhase::kFinishedVictory ||
	                        phase == CeFlagshipDuelPhase::kFinishedP1Won);
	const bool flagshipDestroyed = p1SideWon ? !gDirector.FenrirAlive() : !gDirector.VenturerAlive();
	const char *title = CeFlagshipDuelBannerTitle(phase, split2P, flagshipDestroyed);
	const char *sub = CeFlagshipDuelBannerSubtitle(phase, split2P, flagshipDestroyed);
	if (!title || !title[0]) return;

	const float s = layout.uiScale;
	const unsigned topBg = p1SideWon ? 0xE60B2638u : 0xE62C0E0Cu;
	const unsigned botBg = p1SideWon ? 0xEE061420u : 0xEE180606u;
	const unsigned accent = p1SideWon ? 0xF43CE4FFu : 0xF4F04E1Au;

	BeginHudUntextured();
	gHudBatch.Reset();
	gHudBatch.EmitNineSlicePanel(dev, layout.bannerPanel, s, topBg, botBg, accent, accent);
	gHudBatch.Flush(dev);
	EndHudUntextured();

	const float cx = 0.5f * (layout.bannerPanel.x0 + layout.bannerPanel.x1);
	const float maxTextW = (layout.bannerPanel.x1 - layout.bannerPanel.x0) - 20.0f * s;
	DrawAsciiSmallCentered(font, cx, layout.bannerPanel.y0 + 7.0f * s,
	                       0.90f * s, p1SideWon ? 0xFF7CF4FFu : 0xFFFF8860u, title, maxTextW);
	if (sub && sub[0]) {
		DrawAsciiSmallCentered(font, cx, layout.bannerPanel.y0 + 23.0f * s,
		                       0.76f * s, 0xFFDCE8F2u, sub, maxTextW);
	}
}

} // namespace

extern "C" void PortCeFlagshipDuelSetMessage(const char *message) {
	SetRefusal(message);
}

extern "C" bool PortCeFlagshipDuelBegin1P(void) {
	return BeginModeCommon(CeFlagshipDuelMode::kSolo1P);
}

extern "C" bool PortCeFlagshipDuelBegin2P(void) {
	return BeginModeCommon(CeFlagshipDuelMode::kSplitScreen2P);
}

extern "C" bool PortCeFlagshipDuelActive(void) {
	return gArmed && PortCeMenus() && PortCeLayer();
}

extern "C" bool PortCeFlagshipDuelIsSplitScreen2P(void) {
	return PortCeFlagshipDuelActive() && gMode == CeFlagshipDuelMode::kSplitScreen2P;
}

extern "C" int PortCeFlagshipDuelActiveLevel(void) {
	return CeFlagshipDuelWorld::LevelForMode(gMode);
}

extern "C" void PortCeFlagshipDuelCancel(void) {
	gArmed = false;
	gLoadRefused = false;
	gPendingReturn = false;
	gP0StasisApplied = false;
	ClearCareerSnapshot();
	CeModeEnd(CE_MODE_FLAGSHIP_DUEL);
	gDirector.Reset(CeFlagshipDuelConfig{});
	PortCeFlagshipDuelDropWorld();
}

extern "C" const char *PortCeFlagshipDuelMessage(void) {
	return gMessage;
}

extern "C" bool PortCeFlagshipDuelLoadRefused(void) {
	if (!gArmed) return false;
	gLoadRefused = true;
	gPendingReturn = true;
	gPendingNotice = true;
	gNoticeLevel = CeFlagshipDuelWorld::LevelForMode(gMode);
	gNotice = gMessage;
	PortSessionLog(PORT_SESSION_GAME, true, "flagship_duel: load refused cleanly: %s", gMessage);
	return true;
}

extern "C" bool PortCeFlagshipDuelTakeReturn(void) {
	if (!gPendingReturn) return false;
	const bool refused = gLoadRefused;
	const char *savedNotice = gNotice;
	const bool hadNotice = gPendingNotice && savedNotice != NULL;
	const int savedLevel = gNoticeLevel;
	gPendingReturn = false;
	PortCeFlagshipDuelCancel();
	if (hadNotice) {
		gNotice = savedNotice;
		gPendingNotice = true;
		gNoticeLevel = savedLevel;
	}
	if (refused)
		PortCeBonusClearDebriefing();
	return true;
}

extern "C" bool PortCeFlagshipDuelTakeNotice(const char **sentence, int *outLevel) {
	if (sentence) *sentence = NULL;
	if (outLevel) *outLevel = gNoticeLevel;
	if (!gPendingNotice) return false;
	gPendingNotice = false;
	if (sentence) *sentence = gNotice;
	gNotice = NULL;
	return true;
}

extern "C" void PortCeFlagshipDuelLevelBegin(int level) {
	if (!PortCeFlagshipDuelActive() || level != PortCeFlagshipDuelActiveLevel())
		return;

	gP0StasisApplied = false;

	// Apply crisp maritime visibility and symmetric perpendicular West sun so
	// both flagships silhouette cleanly at spawn and neither 2P player faces sun glare.
	MAP.mHFP.FogDensity = CeFlagshipDuelWorld::kDuelFogDensity;
	MAP.mHFP.SunPosX = CeFlagshipDuelWorld::kDuelSunDirX;
	MAP.mHFP.SunPosY = CeFlagshipDuelWorld::kDuelSunDirY;
	MAP.mHFP.SunPosZ = CeFlagshipDuelWorld::kDuelSunDirZ;
	ENGINE.SetupLights();

	CeFlagshipDuelConfig cfg{};
	cfg.mode = gMode;
	cfg.maxAliveEscortsPerSide = (gMode == CeFlagshipDuelMode::kSplitScreen2P) ? 3u : 4u;
	gDirector.Reset(cfg);

	if (gMode == CeFlagshipDuelMode::kSplitScreen2P) {
		GAME.SetPlayerLives(1, cfg.initialLives2P);
		GAME.SetPlayerLives(2, cfg.initialLives2P);
		gDirector.UpdatePlayerLives(cfg.initialLives2P, cfg.initialLives2P);
	}

	CeEntityHandle venturerH = kCeEntityHandleInvalid;
	CeEntityHandle fenrirH = kCeEntityHandleInvalid;

	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next()) {
		if (!u || u->IsDying() || u->IsA(THING_TYPE_COMPONENT)) continue;
		CUnitData *ud = u->GetUnitData();
		const char *defName = ud ? ud->mName : NULL;
		const CeEntityHandle h = CeEntityHandleFor(u);
		if (h == kCeEntityHandleInvalid) continue;
		if (defName && CeFlagshipDuelWorld::StrEqualsIgnoreCase(defName, "Aircarrier") &&
		    venturerH == kCeEntityHandleInvalid) {
			venturerH = h;
		} else if (defName && CeFlagshipDuelWorld::StrEqualsIgnoreCase(defName, "Fenrir") &&
		           fenrirH == kCeEntityHandleInvalid) {
			fenrirH = h;
		} else if (!u->IsA(THING_TYPE_BATTLE_ENGINE)) {
			(void)gDirector.RegisterEscort(static_cast<int>(u->GetAllegiance()), h);
		}
	}

	gDirector.BeginMatch(venturerH, fenrirH);
	PortSessionLog(PORT_SESSION_GAME, true,
	               "flagship_duel: match begun (mode=%s, level=%d, venturer=0x%08x, fenrir=0x%08x)",
	               (gMode == CeFlagshipDuelMode::kSplitScreen2P) ? "2P" : "1P",
	               level, unsigned(venturerH), unsigned(fenrirH));
}

extern "C" void PortCeFlagshipDuelLevelEnd(int level, int quit) {
	if (!gArmed || level != PortCeFlagshipDuelActiveLevel()) return;
	gP0StasisApplied = false;
	gDirector.Reset(CeFlagshipDuelConfig{});
	if (quit == QT_RESTART_LEVEL && !gLoadRefused) {
		PortCeFlagshipDuelMarkRetryReset();
	} else if (gMode == CeFlagshipDuelMode::kSplitScreen2P) {
		gPendingReturn = true;
	} else {
		PortCeFlagshipDuelCancel();
	}
}

extern "C" void PortCeFlagshipDuelUnitSpawn(CUnit *unit, CInitThing *init) {
	(void)init;
	if (!PortCeFlagshipDuelActive() || !gDirector.IsRunning() || !unit) return;
	if (unit->IsA(THING_TYPE_COMPONENT) || unit->IsA(THING_TYPE_BATTLE_ENGINE)) return;
	const CeEntityHandle h = CeEntityHandleFor(unit);
	if (h == kCeEntityHandleInvalid) return;
	if (h == gDirector.VenturerHandle() || h == gDirector.FenrirHandle()) return;
	(void)gDirector.RegisterEscort(static_cast<int>(unit->GetAllegiance()), h);
}

extern "C" void PortCeFlagshipDuelUnitDestroyed(CUnit *unit) {
	if (!PortCeFlagshipDuelActive() || !gDirector.IsRunning() || !unit) return;
	if (unit->IsA(THING_TYPE_COMPONENT)) return;

	if (!gDirector.IsSplitScreen2P() && unit->IsA(THING_TYPE_BATTLE_ENGINE)) {
		const bool drivingOther = gP0StasisApplied || IsP0DrivingOtherUnit();
		CPlayer *p0 = GAME.GetPlayer(0);
		if (!drivingOther && p0 && p0->GetBattleEngine() == unit) {
			gDirector.OnSoloPlayerLost();
		}
		return;
	}

	const CeEntityHandle h = CeEntityHandleLookup(unit);
	if (h != kCeEntityHandleInvalid) {
		gDirector.OnEntityDestroyed(h);
		return;
	}

	CUnitData *ud = unit->GetUnitData();
	const char *defName = ud ? ud->mName : NULL;
	if (defName && CeFlagshipDuelWorld::StrEqualsIgnoreCase(defName, "Fenrir"))
		gDirector.OnEntityDestroyed(gDirector.FenrirHandle());
	else if (defName && CeFlagshipDuelWorld::StrEqualsIgnoreCase(defName, "Aircarrier"))
		gDirector.OnEntityDestroyed(gDirector.VenturerHandle());
}

extern "C" int PortCeFlagshipDuelBeInStasis(const void *be) {
	if (!PortCeFlagshipDuelActive() || !gDirector.IsRunning() || !be) return 0;
	if (gDirector.IsSplitScreen2P()) return 0;
	CPlayer *p0 = GAME.GetPlayer(0);
	if (!p0 || static_cast<const void *>(p0->GetBattleEngine()) != be) return 0;
	return (gP0StasisApplied || IsP0DrivingOtherUnit()) ? 1 : 0;
}

extern "C" void PortCeFlagshipDuelTickPre(void) {
	if (!PortCeFlagshipDuelActive() || !gDirector.IsRunning()) return;

	const EGameState gs = GAME.GetGameState();
	if (gDirector.IsSplitScreen2P()) {
		gDirector.SyncExternalMultiplayerOutcome(
			gs == GAME_STATE_PLAYER_1_WON,
			gs == GAME_STATE_PLAYER_2_WON);
	} else if (gs == GAME_STATE_LEVEL_LOST && gDirector.Phase() == CeFlagshipDuelPhase::kRunning) {
		gDirector.OnSoloPlayerLost();
	}

	if (GAME.IsPaused() || gs != GAME_STATE_PLAYING) return;

	UpdateP0PlayUnitStasis();

	if (gDirector.IsSplitScreen2P()) {
		gDirector.UpdatePlayerLives(GAME.GetPlayerLives(1), GAME.GetPlayerLives(2));
	}

	gDirector.PruneStaleHandles(&IsHandleAliveCallback, NULL);

	CUnit *venturer = CeEntityHandleResolveUnit(gDirector.VenturerHandle());
	CUnit *fenrir = CeEntityHandleResolveUnit(gDirector.FenrirHandle());
	gDirector.UpdateFlagshipHealth(
		ComputeUnitHealthFraction(venturer),
		ComputeUnitHealthFraction(fenrir));

	if (gDirector.ShouldShieldWinningFlagships()) {
		if (venturer && !venturer->IsDying())
			venturer->SetVulnerable(FALSE);
		if (fenrir && !fenrir->IsDying())
			fenrir->SetVulnerable(FALSE);
	}

	EnforceFlagshipSeparation(venturer, fenrir);

	const CeFlagshipDuelDirector::TickDecision d =
		gDirector.Tick(CLOCK_TICK, CountLiveUnitsInWorld());

	if (d.advancePatrol) {
		if (venturer && !venturer->IsDying())
			venturer->GoToPoint(FVector(d.venturerGoal.x, d.venturerGoal.y, d.venturerGoal.z), FALSE);
		if (fenrir && !fenrir->IsDying())
			fenrir->GoToPoint(FVector(d.fenrirGoal.x, d.fenrirGoal.y, d.fenrirGoal.z), FALSE);
	}

	for (uint32_t i = 0u; i < d.spawnForsetiEscorts; ++i)
		(void)SpawnEscortFighter(0, ++gWaveSerial, venturer);
	for (uint32_t i = 0u; i < d.spawnMuspellEscorts; ++i)
		(void)SpawnEscortFighter(1, ++gWaveSerial, fenrir);

	if (d.declareSoloWon) {
		GAME.DeclareLevelWon();
	} else if (d.declareSoloLost) {
		const int reasonId = gDirector.VenturerAlive() ? kBattleEngineLostTextId : kVenturerLostTextId;
		GAME.DeclareLevelLost(reasonId, FALSE);
	} else if (d.declareMpPlayer1Won) {
		GAME.MPDeclarePlayerWon(1);
	} else if (d.declareMpPlayer2Won) {
		GAME.MPDeclarePlayerWon(2);
	}
}

extern "C" float PortCeFlagshipDuelHudTopBottom(float viewportW, float viewportH) {
	if (!PortCeFlagshipDuelActive() || !gDirector.IsRunning() || gDirector.IsSplitScreen2P())
		return 0.0f;
	if (viewportW <= 64.0f || viewportH <= 64.0f) return 0.0f;
	if (ShouldSuppressGameplayHud(false)) return 0.0f;
	const bool hasSubBanner = (gDirector.Phase() == CeFlagshipDuelPhase::kRunning) &&
		(gDirector.IsVenturerInDanger() || gDirector.IsFenrirInDanger() ||
		 gDirector.ActBannerRemainingSec() > 0.0f);
	const CeFdHudLayout layout = CeFlagshipDuelComputeHudLayout(
		viewportW, viewportH,
		gDirector.VenturerHealthFrac(),
		gDirector.FenrirHealthFrac(),
		hasSubBanner);
	return layout.stripBottomY;
}

extern "C" void PortCeFlagshipDuelDrawHud(CPlayer *player, int viewpoint) {
	(void)player;
	if (viewpoint != 0) return;
	if (!PortCeFlagshipDuelActive() || !gDirector.IsRunning() || gDirector.IsSplitScreen2P())
		return;
	if (GAME.GetGameState() != GAME_STATE_PLAYING) return;
	if (ShouldSuppressGameplayHud(false)) return;

	LPDIRECT3DDEVICE9 dev = LT.GetDevice();
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!dev || !font) return;

	const float vw = float(PLATFORM.GetWindowWidth());
	const float vh = float(PLATFORM.GetWindowHeight());
	if (vw <= 64.0f || vh <= 64.0f) return;

	const CeFlagshipDuelPhase phase = gDirector.Phase();
	const bool hasSubBanner = (phase == CeFlagshipDuelPhase::kRunning) &&
		(gDirector.IsVenturerInDanger() || gDirector.IsFenrirInDanger() ||
		 gDirector.ActBannerRemainingSec() > 0.0f);
	const CeFdHudLayout layout = CeFlagshipDuelComputeHudLayout(
		vw, vh,
		gDirector.VenturerHealthFrac(),
		gDirector.FenrirHealthFrac(),
		hasSubBanner);
	const float s = layout.uiScale;
	const bool dangerPulse = (int(EVENT_MANAGER.GetTime() * 4.0f) & 1) != 0;

	BeginHudUntextured();
	gHudBatch.Reset();
	gHudBatch.EmitNineSlicePanel(dev, layout.stripPanel, s,
	                             0xD80B1C2Eu, 0xE406101Au,
	                             0xF23CE4FFu, 0xF2F04E1Au);
	EmitHullBarsToBatch(dev, layout, dangerPulse);
	gHudBatch.Flush(dev);
	EndHudUntextured();

	const float barW = layout.venturerBarOuter.x1 - layout.venturerBarOuter.x0;
	const float labelScale = 0.80f * s;
	const float textY = layout.stripPanel.y0 + std::floor(6.0f * s);
	const float cx = 0.5f * (layout.stripPanel.x0 + layout.stripPanel.x1);

	char leftBuf[64];
	const int vPct = int(gDirector.VenturerHealthFrac() * 100.0f + 0.5f);
	std::snprintf(leftBuf, sizeof(leftBuf), "F.S. VENTURER  %d%%", vPct);
	DrawAsciiSmallFit(font, layout.venturerBarOuter.x0, textY, labelScale, 0xFFCCEEFFu, leftBuf, barW);

	DrawAsciiSmallCentered(font, cx, textY + 1.0f * s, 0.74f * s, 0xFFE8EEF4u,
	                       "VS", 36.0f * s);

	char rightBuf[64];
	const int fPct = int(gDirector.FenrirHealthFrac() * 100.0f + 0.5f);
	std::snprintf(rightBuf, sizeof(rightBuf), "%d%%  FENRIR", fPct);
	DrawAsciiSmallRight(font, layout.fenrirBarOuter.x1, textY, labelScale, 0xFFFFD4C0u, rightBuf, barW);

	if (phase == CeFlagshipDuelPhase::kRunning) {
		const char *subText = NULL;
		unsigned subBorder = 0xCC4C7294u;
		unsigned subColor = 0xFFDCE8F2u;

		if (gDirector.IsVenturerInDanger()) {
			subText = "WARNING - F.S. VENTURER HULL CRITICAL";
			subBorder = dangerPulse ? 0xEEEC4848u : 0xAA9A3434u;
			subColor = dangerPulse ? 0xFFFFA8A8u : 0xFFFFD0B0u;
		} else if (gDirector.IsFenrirInDanger()) {
			subText = "FENRIR HULL CRITICAL - PRESS THE ATTACK";
			subBorder = dangerPulse ? 0xEE3CD878u : 0xAA2F8A54u;
			subColor = 0xFF90FFC0u;
		} else if (gDirector.ActBannerRemainingSec() > 0.0f) {
			subText = CeFlagshipDuelActTitle(gDirector.CurrentAct());
			subBorder = 0xCC6AA2D4u;
			subColor = 0xFFDCEEFFu;
		}

		if (subText && subText[0]) {
			const float boxW = layout.stripPanel.x1 - layout.stripPanel.x0;
			const float boxH = 22.0f * s;
			const float boxY0 = layout.stripPanel.y1 + 4.0f * s;
			const CeFdHudRect subRect = {cx - boxW * 0.5f, boxY0, cx + boxW * 0.5f, boxY0 + boxH};
			BeginHudUntextured();
			gHudBatch.Reset();
			gHudBatch.EmitNineSlicePanel(dev, subRect, s, 0xD80A141Eu, 0xE2060C14u, subBorder, subBorder);
			gHudBatch.Flush(dev);
			EndHudUntextured();
			DrawAsciiSmallCentered(font, cx, boxY0 + 3.5f * s, 0.86f * s, subColor, subText, boxW - 18.0f * s);
		}
	}

	if (phase == CeFlagshipDuelPhase::kVictoryHold ||
	    phase == CeFlagshipDuelPhase::kDefeatHold) {
		RenderOutcomeBanner(dev, font, layout, phase);
	}

	RestoreHudDeviceState();
}

extern "C" void PortCeFlagshipDuelDrawOverlay(void) {
	if (!PortCeFlagshipDuelActive() || !gDirector.IsRunning()) return;

	const EGameState gs = GAME.GetGameState();
	const bool endState = (gs == GAME_STATE_LEVEL_WON ||
	                       gs == GAME_STATE_LEVEL_LOST ||
	                       gs == GAME_STATE_PLAYER_1_WON ||
	                       gs == GAME_STATE_PLAYER_2_WON);
	if (gs != GAME_STATE_PLAYING && !endState) return;
	if (ShouldSuppressGameplayHud(endState)) return;

	LPDIRECT3DDEVICE9 dev = LT.GetDevice();
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!dev || !font) return;

	const float vw = float(PLATFORM.GetWindowWidth());
	const float vh = float(PLATFORM.GetWindowHeight());
	if (vw <= 64.0f || vh <= 64.0f) return;

	const CeFlagshipDuelPhase phase = EffectiveDisplayPhase(gs);

	if (!gDirector.IsSplitScreen2P()) {
		if (!endState) return;
		const CeFdHudLayout layout = CeFlagshipDuelComputeHudLayout(
			vw, vh,
			gDirector.VenturerHealthFrac(),
			gDirector.FenrirHealthFrac(),
			false);
		RenderOutcomeBanner(dev, font, layout, phase);
		RestoreHudDeviceState();
		return;
	}

	struct CeFdGameSplitAccess : CGame {
		static bool Horizontal() { return static_cast<const CGame &>(GAME).*(&CeFdGameSplitAccess::mHorizontalSplitscreen); }
	};
	const CeFdHudLayout layout = CeFlagshipDuelComputeSplitHudLayout(
		vw, vh,
		gDirector.VenturerHealthFrac(),
		gDirector.FenrirHealthFrac(),
		CeFdGameSplitAccess::Horizontal());
	const float s = layout.uiScale;
	const bool dangerPulse = (int(EVENT_MANAGER.GetTime() * 4.0f) & 1) != 0;

	BeginHudUntextured();
	gHudBatch.Reset();
	gHudBatch.EmitNineSlicePanel(dev, layout.stripPanel, s,
	                             0xE00B1C2Eu, 0xEA06101Au,
	                             0xF23CE4FFu, 0xF2F04E1Au);
	EmitHullBarsToBatch(dev, layout, dangerPulse);
	gHudBatch.Flush(dev);
	EndHudUntextured();

	const float barW = layout.venturerBarOuter.x1 - layout.venturerBarOuter.x0;
	const float labelScale = 0.78f * s;
	const float textY = layout.stripPanel.y0 + std::floor(2.5f * s);
	const float cx = 0.5f * (layout.stripPanel.x0 + layout.stripPanel.x1);

	char leftBuf[64];
	const int vPct = int(gDirector.VenturerHealthFrac() * 100.0f + 0.5f);
	std::snprintf(leftBuf, sizeof(leftBuf), "P1 VENTURER %d%%", vPct);
	DrawAsciiSmallFit(font, layout.venturerBarOuter.x0, textY, labelScale, 0xFFCCEEFFu, leftBuf, barW);

	DrawAsciiSmallCentered(font, cx, textY, labelScale, 0xFFE8EEF4u, "VS", 32.0f * s);

	char rightBuf[64];
	const int fPct = int(gDirector.FenrirHealthFrac() * 100.0f + 0.5f);
	std::snprintf(rightBuf, sizeof(rightBuf), "%d%% FENRIR P2", fPct);
	DrawAsciiSmallRight(font, layout.fenrirBarOuter.x1, textY, labelScale, 0xFFFFD4C0u, rightBuf, barW);

	if (phase == CeFlagshipDuelPhase::kVictoryHold ||
	    phase == CeFlagshipDuelPhase::kDefeatHold ||
	    phase == CeFlagshipDuelPhase::kFinishedP1Won ||
	    phase == CeFlagshipDuelPhase::kFinishedP2Won) {
		RenderOutcomeBanner(dev, font, layout, phase);
	}

	RestoreHudDeviceState();
}

extern "C" void PortCeFlagshipDuelFilterSave(CCareer *career, void *dest) {
	if (!gArmed || !gCareerSnapshotHeld || career != &CAREER || !dest) return;

	CCareer filtered;
	std::memcpy((void *)&filtered, (const void *)gCareerSnapshot, sizeof(filtered));
	filtered.SetMusicVolume(career->GetMusicVolume());
	filtered.SetSoundVolume(career->GetSoundVolume());
	for (int player = 0; player < 2; ++player) {
		filtered.SetInvertYAxis(player, career->GetInvertYAxis(player));
		filtered.SetInvertJetYAxis(player, career->GetInvertJetYAxis(player));
		filtered.SetIsGod(player, career->GetIsGod(player));
		filtered.SetVibration(player, career->GetVibration(player));
		filtered.SetControllerConfigurationNum(player, career->GetControllerConfigurationNum(player));
	}
	filtered.SetScreenPosX(career->GetScreenPosX());
	filtered.SetScreenPosY(career->GetScreenPosY());
	std::memcpy(dest, (const void *)&filtered, sizeof(filtered));
}

#endif // CE_VERSION
