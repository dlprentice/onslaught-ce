// SPDX-License-Identifier: GPL-3.0-only
// Standalone clean-room Living Sandbox (512 m Combined-Arms Persistent Battle
// Director) engine wiring, 2 Hz host-driven director, P0 Citadel respawn +
// Play-Any-Unit stasis protection, 9-slice Tug-of-War HUD, ce_hooks integration,
// and optional Sanctuary Citadel seam.
#ifdef CE_VERSION

#include "common.h"
#include "Career.h"
#include "frontend.h"
#include "game.h"
#include "world.h"
#include "unit.h"
#include "squad.h"
#include "InitThing.h"
#include "WorldPhysicsManager.h"
#include "DXMemoryManager.h"
#include "Platform.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "engine.h"
#include "Camera.h"
#include "BattleEngine.h"
#include "player.h"
#include "ce_sandbox_director.h"
#include "ce_sandbox_telemetry.h"
#include "ce_entity_handles.h"
#include "ce_photo.h"
#include "../platform/common/portlog.h"

#if __has_include("ce_sanctuary_citadel.h")
#include "ce_sanctuary_citadel.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstring>

bool PortCeMenus();
bool PortCeLayer();
bool PortCeAuthoringSessionAllowed() noexcept;
extern "C" bool PortCeMuseumActive(void) __attribute__((weak));
extern "C" bool PortCeMuseumVisitActive(void) __attribute__((weak));
extern "C" bool PortCeEditorActive(void) __attribute__((weak));
extern "C" bool PortCeCarrierClashActive(void) __attribute__((weak));
extern "C" int PortCePlayUnitShowsBattleline(int viewpoint) __attribute__((weak));

namespace {

CeSandboxDirector gDirector;
bool gArmed = false;
bool gLoadRefused = false;
bool gPendingNotice = false;
const char *gMessage = "Living Sandbox could not be started. Your career is unchanged.";
const char *gNotice = NULL;

unsigned char gCareerSnapshot[sizeof(CCareer)];
bool gCareerSnapshotHeld = false;
bool gP0StasisApplied = false;

struct CeSbQuadVertex {
	float x, y, z, rhw;
	DWORD color;
};

void CaptureCareerSnapshot() {
	std::memcpy((void *)gCareerSnapshot, (const void *)&CAREER, sizeof(CCareer));
	gCareerSnapshotHeld = true;
}

void ClearCareerSnapshot() {
	gCareerSnapshotHeld = false;
}

void SetRefusal(const char *msg) {
	gMessage = (msg && msg[0]) ? msg : "Living Sandbox could not be started. Your career is unchanged.";
}

bool CanBeginSession() {
	if (!PortCeMenus() || !PortCeLayer()) return false;
	if (!FRONTEND.IsRunning()) return false;
	if (!PortCeAuthoringSessionAllowed()) return false;
	if (gArmed) return false;
	if (PortCeMuseumActive && PortCeMuseumActive()) return false;
	if (PortCeMuseumVisitActive && PortCeMuseumVisitActive()) return false;
	if (PortCeEditorActive && PortCeEditorActive()) return false;
	if (PortCeCarrierClashActive && PortCeCarrierClashActive()) return false;
	return true;
}

CeSandboxDirectorConfig BuildRuntimeConfig() {
	CMemoryHeap *thingHeap = MEM_MANAGER.GetThingHeap();
	const bool expanded = thingHeap && (thingHeap->GetSize() >= 12u * 1024u * 1024u);
	return CeSandboxDefaultConfig(expanded);
}

void ReadViewEyeAndAim(float &eyeX, float &eyeY, float &eyeZ,
                       float &aimX, float &aimY, float &aimZ) {
	eyeX = CeSandboxWorld::kPlayerSpawnX;
	eyeY = CeSandboxWorld::kPlayerSpawnY;
	eyeZ = CeSandboxWorld::kPlayerSpawnZ - 2.0f;
	aimX = 0.795f;
	aimY = -0.607f;
	aimZ = 0.0f;

	CCamera *cam = ENGINE.GetCameraForViewpoint(0);
	if (cam) {
		const FVector pos = cam->GetPos();
		const FVector fwd = cam->GetOrientation() * FVector(0.0f, 1.0f, 0.0f);
		eyeX = pos.X;
		eyeY = pos.Y;
		eyeZ = pos.Z;
		aimX = fwd.X;
		aimY = fwd.Y;
		aimZ = fwd.Z;
		return;
	}
	CPlayer *p0 = GAME.GetPlayer(0);
	CBattleEngine *be = p0 ? p0->GetBattleEngine() : NULL;
	if (be) {
		const FVector pos = be->GetPos();
		const FVector fwd = be->GetOrientation() * FVector(0.0f, 1.0f, 0.0f);
		eyeX = pos.X;
		eyeY = pos.Y;
		eyeZ = pos.Z - 2.0f;
		aimX = fwd.X;
		aimY = fwd.Y;
		aimZ = fwd.Z;
	}
}

void RecallP0ToCitadelPad(CBattleEngine *be) {
	if (!be) return;
	const float maxLife = be->GetMaxLife();
	if (maxLife > 0.0f) be->mLife = maxLife;
	if (be->mMaxShields > 0.0f) be->mShields = be->mMaxShields;
	const float maxEnergy = be->GetMaxEnergy();
	if (maxEnergy > 0.0f) be->mEnergy = maxEnergy;
	be->Rearm(1.0f);
	be->SetVelocity(FVector(0.0f, 0.0f, 0.0f));
	const FVector padPos(CeSandboxWorld::kPlayerSpawnX,
	                     CeSandboxWorld::kPlayerSpawnY,
	                     CeSandboxWorld::kPlayerSpawnZ);
	be->Teleport(padPos);
	FMatrix padOri;
	padOri.MakeRotationYawF(CeSandboxWorld::kPlayerSpawnYaw);
	be->TeleportOrientation(padOri);
	gDirector.RecordPlayerRecall();
}

void UpdateP0SurvivalAndStasis() {
	CPlayer *p0 = GAME.GetPlayer(0);
	CBattleEngine *be = p0 ? p0->GetBattleEngine() : NULL;
	if (!be) return;

	const bool drivingOther = (PortCePlayUnitShowsBattleline && PortCePlayUnitShowsBattleline(0) != 0);
	CeSandboxPlayerStatus st = {};
	st.alive = !be->IsDying() && !be->IsShuttingDown() && (be->GetLife() > 0.0f);
	st.inWater = (be->IsInWater() != FALSE);
	st.life = be->GetLife();
	st.maxLife = be->GetMaxLife();
	st.drivingOtherUnit = drivingOther;

	const CeSandboxPlayerAction act = CeSandboxEvaluatePlayerProtection(st);
	gDirector.SetP0StasisActive(act.engageStasisShield);

	if (act.engageStasisShield) {
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

	if (gP0StasisApplied) {
		be->SetVulnerable(TRUE);
		gP0StasisApplied = false;
	}

	if (act.recallToCitadel && !be->IsDying() && !be->IsShuttingDown()) {
		RecallP0ToCitadelPad(be);
	}
}

bool SpawnSandboxUnit(const CeSandboxSpawnOrder &order) {
	if (!order.unitName || CeSandboxWorld::IsForbiddenUnit(order.unitName))
		return false;
	if (CeSandboxWorld::IsInsideCitadelKeepOut(order.x, order.y, CeSandboxWorld::kDirectorKeepOutRadius))
		return false;

	// Live mThingHeap safety guard before every dynamic spawn.
	CeSandboxTelemetrySnapshot snap = {};
	if (CeSandboxTelemetrySample(&snap) && snap.valid &&
	    snap.thingHeapFreeBytes < gDirector.Config().minThingHeapFreeBytes) {
		return false;
	}

	CUnitData *stats = UPhysicsManager::GetUnitData(const_cast<char *>(order.unitName));
	if (!stats) return false;

	CUnitInitThing init;
	init.mPos = FVector(order.x, order.y, order.z);
	init.mYaw = order.yaw;
	init.mPitch = 0.0f;
	init.mRoll = 0.0f;
	init.mAllegiance = static_cast<EAllegiance>(order.allegiance);
	init.mActive = TRUE;
	init.mStats = stats;

	// HQ cores and naval ships spawn directly as single units so the director
	// strips factory spawners cleanly and tracks them by 32-bit CeEntityHandle.
	if (order.role != CE_SB_ROLE_HQ && order.role != CE_SB_ROLE_NAVAL) {
		CSquad *squad = UPhysicsManager::SpawnSquad(stats->mBehaviour);
		if (squad) {
			CSquadInitThing sinit;
			sinit.Copy(&init);
			std::snprintf(sinit.mUnitName, sizeof(sinit.mUnitName), "%s", order.unitName);
			sinit.mAmount = 1;
			squad->mUnknown84 = TRUE;
			squad->Init(&sinit);
			return true;
		}
	}

	const SINT unitIdx = UPhysicsManager::GetUnitIndex(const_cast<char *>(order.unitName));
	if (unitIdx < 0) return false;
	CThing *thing = UPhysicsManager::SpawnThing(unitIdx);
	if (!thing) return false;
	thing->Init(&init);
	if (thing->IsA(THING_TYPE_UNIT)) {
		CUnit *u = static_cast<CUnit *>(thing);
		if (order.role == CE_SB_ROLE_HQ) {
			u->ClearSpawners();
		} else {
			u->GoToPoint(FVector(order.targetX, order.targetY, order.targetZ), FALSE);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// 9-slice HUD strip rendering (CE_HOOK_RENDER: strictly read-only on sim state)
// ---------------------------------------------------------------------------
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

void DrawRectD3D(LPDIRECT3DDEVICE9 dev, float x, float y, float w, float h, unsigned col) {
	if (!dev || w <= 0.0f || h <= 0.0f) return;
	const CeSbQuadVertex v[4] = {
		{x,     y,     0.006f, 1.0f, col},
		{x + w, y,     0.006f, 1.0f, col},
		{x,     y + h, 0.006f, 1.0f, col},
		{x + w, y + h, 0.006f, 1.0f, col},
	};
	dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(CeSbQuadVertex));
}

void DrawNineSliceGlass(LPDIRECT3DDEVICE9 dev, const CeSandboxHudRect &rect,
                        float radius, float inset, unsigned topCol, unsigned botCol) {
	const int kSegs = 8;
	const float rFill = (radius - inset > 1.0f) ? (radius - inset) : 1.0f;
	const float cx = rect.x + rect.w * 0.5f;
	const float cy = rect.y + rect.h * 0.5f;
	const float lx = rect.x + radius;
	const float ty = rect.y + radius;
	const float invH = rect.h > 1.0f ? (1.0f / rect.h) : 0.0f;

	float tlx[kSegs + 1], tly[kSegs + 1];
	for (int k = 0; k <= kSegs; ++k) {
		const float th = 3.14159265f + (1.57079633f * float(k) / float(kSegs));
		tlx[k] = lx + rFill * cosf(th);
		tly[k] = ty + rFill * sinf(th);
	}

	CeSbQuadVertex fan[1 + (kSegs + 1) * 4 + 1];
	int n = 0;
	fan[n++] = {cx, cy, 0.009f, 1.0f, LerpArgb(topCol, botCol, 0.5f)};
	auto emit = [&](float px, float py) {
		fan[n++] = {px, py, 0.009f, 1.0f, LerpArgb(topCol, botCol, (py - rect.y) * invH)};
	};
	for (int k = 0; k <= kSegs; ++k) emit(tlx[k], tly[k]);
	for (int k = kSegs; k >= 0; --k) emit(2.0f * cx - tlx[k], tly[k]);
	for (int k = 0; k <= kSegs; ++k) emit(2.0f * cx - tlx[k], 2.0f * cy - tly[k]);
	for (int k = kSegs; k >= 0; --k) emit(tlx[k], 2.0f * cy - tly[k]);
	fan[n++] = fan[1];

	dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, n - 2, fan, sizeof(CeSbQuadVertex));
}

void DrawNineSliceBracket(LPDIRECT3DDEVICE9 dev, const CeSandboxHudRect &rect,
                          float radius, float glowPad, float strokeW,
                          bool mirrorRight, float halfW, unsigned col) {
	const int kSegs = 8;
	const float cx = rect.x + rect.w * 0.5f;
	const float cy = rect.y + rect.h * 0.5f;
	const float lx = rect.x + radius;
	const float ty = rect.y + radius;
	const float coreHalf = strokeW * 0.5f;
	const float rMid = (radius - glowPad - coreHalf > 1.0f) ? (radius - glowPad - coreHalf) : 1.0f;
	const float bodySpan = (rect.w > 2.0f * radius) ? (rect.w - 2.0f * radius) : 0.0f;
	const float lipLen = (bodySpan * 0.14f < radius * 0.65f) ? (bodySpan * 0.14f) : (radius * 0.65f);

	struct SpinePt { float x, y, nx, ny, sw; };
	SpinePt pts[2 + (kSegs + 1) * 2];
	int count = 0;
	if (lipLen > 1.0f) pts[count++] = {lx + lipLen, ty - rMid, 0.0f, -1.0f, 0.40f};
	for (int k = kSegs; k >= 0; --k) {
		const float frac = float(k) / float(kSegs);
		const float th = 3.14159265f + 1.57079633f * frac;
		const float c = cosf(th), s = sinf(th);
		const float sw = 1.0f + 0.20f * sinf(frac * 3.14159265f);
		pts[count++] = {lx + rMid * c, ty + rMid * s, c, s, sw};
	}
	for (int k = 0; k <= kSegs; ++k) {
		const float frac = float(k) / float(kSegs);
		const float th = 3.14159265f + 1.57079633f * frac;
		const float c = cosf(th), s = sinf(th);
		const float sw = 1.0f + 0.20f * sinf(frac * 3.14159265f);
		pts[count++] = {lx + rMid * c, 2.0f * cy - (ty + rMid * s), c, -s, sw};
	}
	if (lipLen > 1.0f) pts[count++] = {lx + lipLen, 2.0f * cy - (ty - rMid), 0.0f, 1.0f, 0.40f};

	CeSbQuadVertex strip[(2 + (kSegs + 1) * 2) * 2];
	const float minX = rect.x, maxX = rect.x + rect.w;
	const float minY = rect.y, maxY = rect.y + rect.h;
	auto clampPt = [&](float &px, float &py) {
		if (px < minX) px = minX;
		if (px > maxX) px = maxX;
		if (py < minY) py = minY;
		if (py > maxY) py = maxY;
	};
	for (int i = 0; i < count; ++i) {
		float px = pts[i].x, py = pts[i].y;
		float nx = pts[i].nx, ny = pts[i].ny;
		if (mirrorRight) { px = 2.0f * cx - px; nx = -nx; }
		const float hw = halfW * pts[i].sw;
		float ox = px + nx * hw, oy = py + ny * hw;
		float ix = px - nx * hw, iy = py - ny * hw;
		clampPt(ox, oy);
		clampPt(ix, iy);
		strip[i * 2 + 0] = {ox, oy, 0.007f, 1.0f, col};
		strip[i * 2 + 1] = {ix, iy, 0.007f, 1.0f, col};
	}
	dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, (count - 1) * 2, strip, sizeof(CeSbQuadVertex));
}

void DrawSafeHudText(CDXBitmapFont *font, float x, float y, unsigned colour,
                     const char *ascii, float scale, float maxW) {
	if (!font || !ascii || !ascii[0]) return;
	WCHAR wide[96] = {};
	size_t n = 0u;
	for (; ascii[n] && n + 1u < 96u; ++n) {
		const char ch = CeSandboxIsFontSmallSafeChar(ascii[n]) ? ascii[n] : ' ';
		wide[n] = static_cast<WCHAR>(static_cast<unsigned char>(ch));
	}
	wide[n] = 0;
	SIZE ext = {0, 0};
	font->GetTextExtent(wide, &ext);
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
	font->DrawTextWithShadow(x, y, colour, wide, 0, 0.0f, sx, scale);
}

void DrawSafeHudTextCentered(CDXBitmapFont *font, float cx, float y, unsigned colour,
                             const char *ascii, float scale, float maxW) {
	if (!font || !ascii || !ascii[0]) return;
	WCHAR wide[96] = {};
	size_t n = 0u;
	for (; ascii[n] && n + 1u < 96u; ++n) {
		const char ch = CeSandboxIsFontSmallSafeChar(ascii[n]) ? ascii[n] : ' ';
		wide[n] = static_cast<WCHAR>(static_cast<unsigned char>(ch));
	}
	wide[n] = 0;
	SIZE ext = {0, 0};
	font->GetTextExtent(wide, &ext);
	float sx = scale;
	if (maxW > 8.0f && ext.cx > 0 && float(ext.cx) * sx > maxW) {
		sx = maxW / float(ext.cx);
		if (sx < scale * 0.55f) sx = scale * 0.55f;
	}
	const float drawW = float(ext.cx) * sx;
	DrawSafeHudText(font, floorf(cx - drawW * 0.5f), y, colour, ascii, sx, maxW);
}

} // namespace

extern "C" void PortCeLivingSandboxSetMessage(const char *message) {
	SetRefusal(message);
}

extern "C" bool PortCeLivingSandboxBegin(void) {
	gLoadRefused = false;
	gPendingNotice = false;
	gNotice = NULL;
	if (!CanBeginSession()) {
		SetRefusal("Close other special modes before starting Living Sandbox.");
		PortSessionLog(PORT_SESSION_GAME, false, "living sandbox refused: another mode or fence active");
		return false;
	}
	if (!PortCeLivingSandboxPreflightAndBuildWorld()) {
		return false;
	}
	CaptureCareerSnapshot();
	gArmed = true;
	gP0StasisApplied = false;
	CeSandboxProfileSetNonCampaignSession(true);
	gDirector.Arm(BuildRuntimeConfig());
	return true;
}

extern "C" bool PortCeLivingSandboxActive(void) {
	return gArmed && PortCeLayer() && PortCeMenus() && !GAME.IsMultiplayer();
}

extern "C" void PortCeLivingSandboxCancel(void) {
	gArmed = false;
	gLoadRefused = false;
	gP0StasisApplied = false;
	CeSandboxProfileSetNonCampaignSession(false);
#ifdef CE_SANDBOX_HAS_CITADEL
	CeSanctuaryCitadel::RuntimeShutdown();
#endif
	ClearCareerSnapshot();
	gDirector.Reset();
	PortCeLivingSandboxDropWorld();
}

extern "C" const char *PortCeLivingSandboxMessage(void) {
	return gMessage;
}

extern "C" bool PortCeLivingSandboxLoadRefused(void) {
	if (!gArmed) return false;
	gLoadRefused = true;
	gNotice = gMessage;
	gPendingNotice = true;
	PortSessionLog(PORT_SESSION_GAME, true, "living sandbox load refused cleanly: %s", gMessage);
	return true;
}

extern "C" bool PortCeLivingSandboxTakeReturn(void) {
	if (!gArmed) return false;
	const bool hadNotice = gLoadRefused && gNotice != NULL;
	const char *savedNotice = gNotice;
	PortCeLivingSandboxCancel();
	if (hadNotice) {
		gNotice = savedNotice;
		gPendingNotice = true;
	}
	return true;
}

extern "C" bool PortCeLivingSandboxTakeNotice(const char **sentence) {
	if (sentence) *sentence = NULL;
	if (!gPendingNotice) return false;
	gPendingNotice = false;
	if (sentence) *sentence = gNotice;
	gNotice = NULL;
	return true;
}

extern "C" bool PortCeLivingSandboxRescuePlayer(int playerNumber) {
	if (!PortCeLivingSandboxActive() || playerNumber != 1) return false;
	CPlayer *p0 = GAME.GetPlayer(0);
	CBattleEngine *be = p0 ? p0->GetBattleEngine() : NULL;
	if (!be) return false;
	be->SetFlags(static_cast<short>(be->GetFlags() & ~TF_DYING));
	RecallP0ToCitadelPad(be);
	p0->GotoControlView();
	return true;
}

extern "C" void PortCeLivingSandboxLevelBegin(int level) {
	if (!PortCeLivingSandboxActive() || level != CeSandboxWorld::kArchiveLevel)
		return;
	gP0StasisApplied = false;
	const CeSandboxDirectorConfig cfg = BuildRuntimeConfig();
	gDirector.BeginLevel(cfg);

#ifdef CE_SANDBOX_HAS_CITADEL
	CeSanctuaryCitadel::RuntimeInit(CeSandboxWorld::kCitadelFloorZ);
#endif

	// Register initial world units and strip any factory spawners on HQ cores so
	// the 2 Hz CE director owns 100% of reinforcement pacing and heap budgets.
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next()) {
		if (!u || !u->GetUnitData() || !u->GetUnitData()->mName) continue;
		if (u->IsA(THING_TYPE_BATTLE_ENGINE)) continue;
		const char *def = u->GetUnitData()->mName;
		if (CeSandboxWorld::StrEqualsIgnoreCase(def, CeSandboxWorld::kUnitForsetiHq) ||
		    CeSandboxWorld::StrEqualsIgnoreCase(def, CeSandboxWorld::kUnitMuspellHq)) {
			u->ClearSpawners();
		}
		const CeEntityHandle h = CeEntityHandleFor(u);
		if (h == kCeEntityHandleInvalid) continue;
		const FVector pos = u->GetPos();
		gDirector.OnUnitSpawn(h, def, static_cast<int>(u->GetAllegiance()), pos.X, pos.Y, u->GetLife());
	}

	PortSessionLog(PORT_SESSION_GAME, false,
	               "living sandbox started: target_cap=%d tracked=%u venturer=%d",
	               cfg.targetUnitCap, gDirector.TrackedCount(),
	               PortCeLivingSandboxVenturerReady() ? 1 : 0);
}

extern "C" void PortCeLivingSandboxLevelEnd(int level, int quit) {
	(void)level;
	if (!gArmed) return;
#ifdef CE_SANDBOX_HAS_CITADEL
	CeSanctuaryCitadel::RuntimeShutdown();
#endif
	gP0StasisApplied = false;
	const bool keepForRetry = (quit == QT_RESTART_LEVEL) && !gLoadRefused;
	gDirector.EndLevel(keepForRetry);
	if (keepForRetry) {
		PortCeLivingSandboxMarkRetryReset();
	}
}

extern "C" void PortCeLivingSandboxUnitSpawn(CUnit *unit, CInitThing *init) {
	(void)init;
	if (!PortCeLivingSandboxActive() || !gDirector.IsRunning() || !unit) return;
	if (unit->IsA(THING_TYPE_BATTLE_ENGINE)) return;
	CUnitData *data = unit->GetUnitData();
	if (!data || !data->mName) return;
	if (CeSandboxWorld::StrEqualsIgnoreCase(data->mName, CeSandboxWorld::kUnitForsetiHq) ||
	    CeSandboxWorld::StrEqualsIgnoreCase(data->mName, CeSandboxWorld::kUnitMuspellHq)) {
		unit->ClearSpawners();
	}
	const CeEntityHandle h = CeEntityHandleFor(unit);
	if (h == kCeEntityHandleInvalid) return;
	const FVector pos = unit->GetPos();
	gDirector.OnUnitSpawn(h, data->mName, static_cast<int>(unit->GetAllegiance()),
	                      pos.X, pos.Y, unit->GetLife());
}

extern "C" void PortCeLivingSandboxUnitDestroyed(CUnit *unit) {
	if (!PortCeLivingSandboxActive() || !gDirector.IsRunning() || !unit) return;
	const CeEntityHandle h = CeEntityHandleLookup(unit);
	if (h != kCeEntityHandleInvalid) {
		gDirector.OnUnitDestroyed(h);
	}
}

extern "C" void PortCeLivingSandboxTickPre(void) {
	if (!PortCeLivingSandboxActive() || !gDirector.IsRunning()) return;
	if (GAME.IsPaused() || GAME.GetGameState() == GAME_STATE_PRE_RUNNING) return;

	UpdateP0SurvivalAndStasis();

	// Collect lightweight node presence and tracked unit mobility samples.
	CeSandboxNodePresence presence = {};
	CeSandboxTrackedSample samples[CeSandboxDirector::kMaxTrackedUnits];
	unsigned sampleCount = 0u;
	int liveUnits = 0;

	const float capR2 = gDirector.Config().captureRadius * gDirector.Config().captureRadius;
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next()) {
		if (!u || u->IsDying() || u->IsShuttingDown() || u->GetLife() <= 0.0f) continue;
		++liveUnits;

		const FVector pos = u->GetPos();
		const int alleg = static_cast<int>(u->GetAllegiance());
		const bool isP0 = (u->IsA(THING_TYPE_BATTLE_ENGINE) != FALSE);
		const int weight = isP0 ? 2 : 1;

		for (unsigned n = 0; n < CeSandboxWorld::kCaptureNodeCount; ++n) {
			const CeSandboxWorld::CaptureNodeSpec &ns = CeSandboxWorld::CaptureNode(n);
			const float dx = pos.X - ns.x;
			const float dy = pos.Y - ns.y;
			if (dx * dx + dy * dy <= capR2) {
				if (alleg == CeSandboxWorld::kAllegianceForseti) presence.forsetiWeight[n] += weight;
				else if (alleg == CeSandboxWorld::kAllegianceMuspell) presence.muspellWeight[n] += weight;
			}
		}

		if (!isP0 && sampleCount < CeSandboxDirector::kMaxTrackedUnits) {
			const CeEntityHandle h = CeEntityHandleLookup(u);
			if (h != kCeEntityHandleInvalid) {
				samples[sampleCount++] = {
					h, pos.X, pos.Y, pos.Z, u->GetLife(),
					u->GetCurrentTarget() != NULL, true
				};
			}
		}
	}

	CeSandboxTelemetrySnapshot tel = {};
	CeSandboxTelemetrySample(&tel);
	CeSandboxPressure pressure = {};
	const float frameDt = GAME.GetRenderFrameLength();
	pressure.frameTimeMs = (frameDt > 0.0f && frameDt < 0.5f) ? (frameDt * 1000.0f) : 16.67f;
	pressure.liveWorldUnits = liveUnits;
	pressure.eventPending = tel.valid ? tel.eventManagerPending : 0;
	pressure.eventCapacity = tel.valid ? tel.eventManagerCapacity : kCeRetailEventPoolCapacity;
	pressure.thingHeapFreeBytes = tel.valid ? tel.thingHeapFreeBytes : (2u * 1024u * 1024u);
	pressure.venturerAvailable = PortCeLivingSandboxVenturerReady();

	CeSandboxStepOutput stepOut = {};
	if (!gDirector.Tick(CLOCK_TICK, pressure, presence, samples, sampleCount, &stepOut)) {
		return;
	}

	// Execute stuck-unit reaps first to reclaim heap headroom before spawning.
	for (int i = 0; i < stepOut.reapCount; ++i) {
		CUnit *u = CeEntityHandleResolveUnit(stepOut.reaps[i].handle);
		if (u && !u->IsDying() && !u->IsShuttingDown() && !u->IsA(THING_TYPE_BATTLE_ENGINE)) {
			u->Remove();
		}
	}

	// Issue frontline movement / Sanctuary keep-out steer orders.
	for (int i = 0; i < stepOut.moveCount; ++i) {
		CUnit *u = CeEntityHandleResolveUnit(stepOut.moves[i].handle);
		if (u && !u->IsDying() && !u->IsShuttingDown()) {
			u->GoToPoint(FVector(stepOut.moves[i].x, stepOut.moves[i].y, stepOut.moves[i].z), FALSE);
		}
	}

	// Coalesce mThingHeap cleanly during diegetic regroup lulls.
	if (stepOut.requestHeapCoalesce) {
		CMemoryHeap *thingHeap = MEM_MANAGER.GetThingHeap();
		if (thingHeap) thingHeap->Cleanup();
	}

	// Spawn capped reinforcement / HQ rebuild / Naval Escalation units.
	for (int i = 0; i < stepOut.spawnCount; ++i) {
		SpawnSandboxUnit(stepOut.spawns[i]);
	}
}

extern "C" void PortCeLivingSandboxDrawWorld(CPlayer *player, int viewpoint) {
	(void)player;
	if (viewpoint != 0) return;
	if (!PortCeLivingSandboxActive() || !gDirector.IsRunning()) return;

#ifdef CE_SANDBOX_HAS_CITADEL
	float eyeX, eyeY, eyeZ, aimX, aimY, aimZ;
	ReadViewEyeAndAim(eyeX, eyeY, eyeZ, aimX, aimY, aimZ);
	float dt = GAME.GetRenderFrameLength();
	if (!(dt > 0.0f && dt <= 0.25f)) dt = 1.0f / 60.0f;
	CeSanctuaryCitadel::RuntimeUpdate(dt, GAME.IsPaused() != FALSE,
	                                  eyeX, eyeY, eyeZ, aimX, aimY, aimZ);
	CeSanctuaryCitadel::RuntimeDrawWorld();
	CeSanctuaryCitadel::RuntimeDrawDomeFx();
#endif
}

extern "C" bool PortCeLivingSandboxKey(unsigned char key, bool repeat) {
	if (!PortCeLivingSandboxActive() || !gDirector.IsRunning()) return false;
#ifdef CE_SANDBOX_HAS_CITADEL
	return CeSanctuaryCitadel::RuntimeHandleKey(static_cast<unsigned>(key), repeat);
#else
	(void)key;
	(void)repeat;
	return false;
#endif
}

extern "C" void PortCeLivingSandboxDrawHud(CPlayer *player, int viewpoint) {
	(void)player;
	if (viewpoint != 0) return;
	if (!PortCeLivingSandboxActive() || !gDirector.IsRunning()) return;
	if (PortCePhotoOn()) return;

	LPDIRECT3DDEVICE9 dev = LT.GetDevice();
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!dev || !font) return;

	const float vw = static_cast<float>(PLATFORM.GetWindowWidth());
	const float vh = static_cast<float>(PLATFORM.GetWindowHeight());
	const float hudScale = vh / 480.0f;
	const float uiScale = (hudScale >= 0.85f && hudScale <= 2.5f) ? hudScale : (hudScale < 0.85f ? 0.85f : 2.5f);

	char cueTitle[64] = {};
	char cueLine[96] = {};
	char cueNote[96] = {};
	bool showCue = false;
#ifdef CE_SANDBOX_HAS_CITADEL
	{
		float eyeX, eyeY, eyeZ, aimX, aimY, aimZ;
		ReadViewEyeAndAim(eyeX, eyeY, eyeZ, aimX, aimY, aimZ);
		showCue = CeSanctuaryCitadel::RuntimeCue(eyeX, eyeY,
		                                         cueTitle, sizeof(cueTitle),
		                                         cueLine, sizeof(cueLine),
		                                         cueNote, sizeof(cueNote));
	}
#endif

	const CeSandboxHudState state = gDirector.BuildHudState();
	const CeSandboxHudLayout layout = CeSandboxComputeHudLayout(vw, vh, uiScale, state.showBanner, showCue);
	if (!layout.valid) return;

	BeginHudUntextured();

	const float inset = layout.glowPad + layout.strokeWidth * 0.45f;
	const float coreHalf = layout.strokeWidth * 0.5f;

	// Top-center 9-slice Tug-of-War strip: dark glass + Forseti cyan left bracket + Muspell crimson right bracket.
	DrawNineSliceGlass(dev, layout.panel, layout.cornerRadius, inset, 0xd80b1c2eu, 0xe406101au);
	DrawNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth, false, coreHalf + layout.glowPad, 0x4c28d4ffu);
	DrawNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth, false, coreHalf, 0xf23ce4ffu);
	DrawNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth, false, coreHalf * 0.42f, 0xffccf8ffu);
	DrawNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth, true, coreHalf + layout.glowPad, 0x4cff4414u);
	DrawNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth, true, coreHalf, 0xf2f04e1au);
	DrawNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth, true, coreHalf * 0.42f, 0xffffc464u);

	// 5 Contested Capture Node pips with proportional control fill.
	for (unsigned i = 0; i < CeSandboxWorld::kCaptureNodeCount; ++i) {
		const CeSandboxHudRect &pip = layout.nodePips[i];
		const float ctrl = state.nodeControl[i]; // [-100..+100]
		const CeSandboxOwner owner = state.nodeOwner[i];
		const unsigned bg = (owner == CE_SB_OWNER_FORSETI) ? 0xc80a2234u
		                  : (owner == CE_SB_OWNER_MUSPELL) ? 0xc82c0e0cu
		                                                   : 0xc81a1c24u;
		DrawRectD3D(dev, pip.x, pip.y, pip.w, pip.h, bg);

		const float frac = (ctrl + 100.0f) * 0.005f; // [0..1], 1 = Forseti, 0 = Muspell
		const float innerW = pip.w - 2.0f;
		const float innerH = pip.h - 2.0f;
		const unsigned fillCol = (owner == CE_SB_OWNER_FORSETI) ? 0xf032d8ffu
		                       : (owner == CE_SB_OWNER_MUSPELL) ? 0xf0ff4c28u
		                                                        : 0xf0f2c040u;
		if (ctrl >= 0.0f) {
			const float wFill = innerW * (0.5f + (ctrl * 0.005f));
			DrawRectD3D(dev, pip.x + 1.0f, pip.y + 1.0f, wFill, innerH, fillCol);
		} else {
			const float wFill = innerW * (0.5f + ((-ctrl) * 0.005f));
			DrawRectD3D(dev, pip.x + pip.w - 1.0f - wFill, pip.y + 1.0f, wFill, innerH, fillCol);
		}
		(void)frac;
	}

	// Event / status banner below the 5-node strip
	if (layout.showBanner) {
		const unsigned topGlass = state.regroupActive ? 0xe01c222eu : 0xe00e1e2cu;
		const unsigned botGlass = state.regroupActive ? 0xe80c1018u : 0xe8061018u;
		const unsigned haloCol  = state.navalEscalationActive ? 0x50ffb028u : 0x5028e0ffu;
		const unsigned bodyCol  = state.navalEscalationActive ? 0xf4ffc040u : 0xf43ce4ffu;
		const unsigned coreCol  = 0xffe8fbffu;
		const float br = floorf(9.0f * uiScale);
		DrawNineSliceGlass(dev, layout.bannerBox, br, inset, topGlass, botGlass);
		for (int side = 0; side < 2; ++side) {
			const bool right = (side == 1);
			DrawNineSliceBracket(dev, layout.bannerBox, br, layout.glowPad, layout.strokeWidth, right, coreHalf + layout.glowPad, haloCol);
			DrawNineSliceBracket(dev, layout.bannerBox, br, layout.glowPad, layout.strokeWidth, right, coreHalf, bodyCol);
			DrawNineSliceBracket(dev, layout.bannerBox, br, layout.glowPad, layout.strokeWidth, right, coreHalf * 0.42f, coreCol);
		}
	}

	// Optional Sanctuary Citadel exhibit / pedestal cue box at bottom-center
	if (layout.showCue) {
		const float cr = floorf(10.0f * uiScale);
		DrawNineSliceGlass(dev, layout.cueBox, cr, inset, 0xdc0c1c2au, 0xe8061018u);
		for (int side = 0; side < 2; ++side) {
			const bool right = (side == 1);
			DrawNineSliceBracket(dev, layout.cueBox, cr, layout.glowPad, layout.strokeWidth, right, coreHalf + layout.glowPad, 0x4838d8ffu);
			DrawNineSliceBracket(dev, layout.cueBox, cr, layout.glowPad, layout.strokeWidth, right, coreHalf, 0xee60e8ffu);
			DrawNineSliceBracket(dev, layout.cueBox, cr, layout.glowPad, layout.strokeWidth, right, coreHalf * 0.42f, 0xffd8fbffu);
		}
	}

	EndHudUntextured();

	// Crisp FONT_SMALL labels
	const float textScale = 0.85f * uiScale;
	const float textY = layout.panel.y + floorf(6.0f * uiScale);
	const float sidePad = floorf(18.0f * uiScale);
	DrawSafeHudText(font, layout.panel.x + sidePad, textY, 0xffcceeffu,
	                state.headerLeft, textScale, floorf(115.0f * uiScale));
	DrawSafeHudTextCentered(font, layout.panel.x + layout.panel.w * 0.5f, textY, 0xfff4f8ffu,
	                        state.headerMid, textScale, floorf(210.0f * uiScale));
	{
		WCHAR wideR[48] = {};
		size_t rn = 0u;
		for (; state.headerRight[rn] && rn + 1u < 48u; ++rn)
			wideR[rn] = static_cast<WCHAR>(static_cast<unsigned char>(state.headerRight[rn]));
		wideR[rn] = 0;
		SIZE ext = {0, 0};
		font->GetTextExtent(wideR, &ext);
		float rxScale = textScale;
		const float maxRW = floorf(115.0f * uiScale);
		if (ext.cx > 0 && float(ext.cx) * rxScale > maxRW) rxScale = maxRW / float(ext.cx);
		const float rx = layout.panel.x + layout.panel.w - sidePad - float(ext.cx) * rxScale;
		DrawSafeHudText(font, floorf(rx), textY, 0xffffd4c0u, state.headerRight, rxScale, maxRW);
	}

	if (layout.showBanner) {
		const float bannerY = layout.bannerBox.y + floorf(6.0f * uiScale);
		DrawSafeHudTextCentered(font, layout.bannerBox.x + layout.bannerBox.w * 0.5f, bannerY,
		                        0xffe6f8ffu, state.bannerText, 0.88f * uiScale,
		                        layout.bannerBox.w - floorf(24.0f * uiScale));
	}

	if (layout.showCue) {
		char sTitle[64], sLine[96], sNote[96];
		CeSandboxSanitizeHudText(cueTitle, sTitle, sizeof(sTitle));
		CeSandboxSanitizeHudText(cueLine, sLine, sizeof(sLine));
		CeSandboxSanitizeHudText(cueNote, sNote, sizeof(sNote));
		const float cx = layout.cueBox.x + layout.cueBox.w * 0.5f;
		const float maxW = layout.cueBox.w - floorf(24.0f * uiScale);
		DrawSafeHudTextCentered(font, cx, layout.cueBox.y + floorf(6.0f * uiScale), 0xff8cf4ffu, sTitle, 0.86f * uiScale, maxW);
		DrawSafeHudTextCentered(font, cx, layout.cueBox.y + floorf(22.0f * uiScale), 0xffeef6ffu, sLine, 0.78f * uiScale, maxW);
		DrawSafeHudTextCentered(font, cx, layout.cueBox.y + floorf(37.0f * uiScale), 0xffb8d4e8u, sNote, 0.72f * uiScale, maxW);
	}

	LT.SRS(D3DRS_ZENABLE, TRUE);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
}

extern "C" void PortCeLivingSandboxFilterSave(CCareer *career, void *dest) {
	if (career != &CAREER || !dest || !gCareerSnapshotHeld || !gArmed) return;
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

#endif
