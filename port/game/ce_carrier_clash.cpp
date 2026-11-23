// SPDX-License-Identifier: GPL-3.0-only
// Standalone clean-room Carrier Clash (F.S. Venturer vs. Fenrir) mission
// director, lifecycle, 9-slice HUD strip, and ce_hooks integration.
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
#include "Platform.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "BattleEngine.h"
#include "player.h"
#include "ce_carrier_clash.h"
#include "ce_entity_handles.h"
#include "ce_photo.h"
#include "../platform/common/portlog.h"

#include <cmath>
#include <cstdio>
#include <cstring>

bool PortCeMenus();
bool PortCeLayer();
bool PortCeAuthoringSessionAllowed() noexcept;
extern "C" bool PortCeMuseumActive(void) __attribute__((weak));
extern "C" bool PortCeMuseumVisitActive(void) __attribute__((weak));
extern "C" bool PortCeEditorActive(void) __attribute__((weak));

namespace {

CeCarrierClashDirector gDirector;
bool gArmed = false;
bool gLoadRefused = false;
bool gPendingNotice = false;
const char *gMessage = "Carrier Clash could not be started. Your career is unchanged.";
const char *gNotice = NULL;

unsigned char gCareerSnapshot[sizeof(CCareer)];
bool gCareerSnapshotHeld = false;

struct CeCcQuadVertex {
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
	gMessage = (msg && msg[0]) ? msg : "Carrier Clash could not be started. Your career is unchanged.";
}

bool CanBeginSession() {
	if (!PortCeMenus() || !PortCeLayer()) return false;
	if (!FRONTEND.IsRunning()) return false;
	if (!PortCeAuthoringSessionAllowed()) return false;
	if (gArmed) return false;
	if (PortCeMuseumActive && PortCeMuseumActive()) return false;
	if (PortCeMuseumVisitActive && PortCeMuseumVisitActive()) return false;
	if (PortCeEditorActive && PortCeEditorActive()) return false;
	return true;
}

bool SpawnDynamicEscort(const CeCarrierClashSpawnOrder &order) {
	if (!order.unitName || CeCarrierClashWorld::IsForbiddenUnit(order.unitName))
		return false;
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

	const SINT unitIdx = UPhysicsManager::GetUnitIndex(const_cast<char *>(order.unitName));
	if (unitIdx < 0) return false;
	CThing *thing = UPhysicsManager::SpawnThing(unitIdx);
	if (!thing) return false;
	thing->Init(&init);
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
	const CeCcQuadVertex v[4] = {
		{x,     y,     0.006f, 1.0f, col},
		{x + w, y,     0.006f, 1.0f, col},
		{x,     y + h, 0.006f, 1.0f, col},
		{x + w, y + h, 0.006f, 1.0f, col},
	};
	dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(CeCcQuadVertex));
}

void DrawNineSliceGlass(LPDIRECT3DDEVICE9 dev, const CeCarrierClashHudRect &rect,
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

	CeCcQuadVertex fan[1 + (kSegs + 1) * 4 + 1];
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

	dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, n - 2, fan, sizeof(CeCcQuadVertex));
}

void DrawNineSliceBracket(LPDIRECT3DDEVICE9 dev, const CeCarrierClashHudRect &rect,
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
	if (lipLen > 1.0f) {
		pts[count++] = {lx + lipLen, ty - rMid, 0.0f, -1.0f, 0.40f};
	}
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
	if (lipLen > 1.0f) {
		pts[count++] = {lx + lipLen, 2.0f * cy - (ty - rMid), 0.0f, 1.0f, 0.40f};
	}

	CeCcQuadVertex strip[(2 + (kSegs + 1) * 2) * 2];
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
	dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, (count - 1) * 2, strip, sizeof(CeCcQuadVertex));
}

void DrawSafeHudText(CDXBitmapFont *font, float x, float y, unsigned colour,
                     const char *ascii, float scale, float maxW) {
	if (!font || !ascii || !ascii[0]) return;
	WCHAR wide[80] = {};
	size_t n = 0u;
	for (; ascii[n] && n + 1u < 80u; ++n) {
		const char ch = CeCarrierClashIsFontSmallSafeChar(ascii[n]) ? ascii[n] : ' ';
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
	WCHAR wide[80] = {};
	size_t n = 0u;
	for (; ascii[n] && n + 1u < 80u; ++n) {
		const char ch = CeCarrierClashIsFontSmallSafeChar(ascii[n]) ? ascii[n] : ' ';
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

extern "C" void PortCeCarrierClashSetMessage(const char *message) {
	SetRefusal(message);
}

extern "C" bool PortCeCarrierClashBegin(void) {
	gLoadRefused = false;
	gPendingNotice = false;
	gNotice = NULL;
	if (!CanBeginSession()) {
		SetRefusal("Close other special modes before starting Carrier Clash.");
		PortSessionLog(PORT_SESSION_GAME, false, "carrier clash refused: another mode or fence active");
		return false;
	}
	if (!PortCeCarrierClashPreflightAndBuildWorld()) {
		return false;
	}
	CaptureCareerSnapshot();
	gArmed = true;
	gDirector.Arm();
	return true;
}

extern "C" bool PortCeCarrierClashActive(void) {
	return gArmed && PortCeLayer() && PortCeMenus() && !GAME.IsMultiplayer();
}

extern "C" void PortCeCarrierClashCancel(void) {
	gArmed = false;
	gLoadRefused = false;
	ClearCareerSnapshot();
	gDirector.Reset();
	PortCeCarrierClashDropWorld();
}

extern "C" const char *PortCeCarrierClashMessage(void) {
	return gMessage;
}

extern "C" bool PortCeCarrierClashLoadRefused(void) {
	if (!gArmed) return false;
	gLoadRefused = true;
	gNotice = gMessage;
	gPendingNotice = true;
	PortSessionLog(PORT_SESSION_GAME, true, "carrier clash load refused cleanly: %s", gMessage);
	return true;
}

extern "C" bool PortCeCarrierClashTakeReturn(void) {
	if (!gArmed) return false;
	const bool hadNotice = gLoadRefused && gNotice != NULL;
	const char *savedNotice = gNotice;
	PortCeCarrierClashCancel();
	if (hadNotice) {
		gNotice = savedNotice;
		gPendingNotice = true;
	}
	return true;
}

extern "C" bool PortCeCarrierClashTakeNotice(const char **sentence) {
	if (sentence) *sentence = NULL;
	if (!gPendingNotice) return false;
	gPendingNotice = false;
	if (sentence) *sentence = gNotice;
	gNotice = NULL;
	return true;
}

extern "C" void PortCeCarrierClashLevelBegin(int level) {
	if (!PortCeCarrierClashActive() || level != CeCarrierClashWorld::kArchiveLevel)
		return;
	gDirector.BeginLevel();

	// Bind already-spawned units from the clean-room world body via 32-bit CeEntityHandles.
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next()) {
		if (!u || !u->GetUnitData() || !u->GetUnitData()->mName) continue;
		const CeEntityHandle h = CeEntityHandleFor(u);
		if (h == kCeEntityHandleInvalid) continue;
		gDirector.OnUnitSpawn(h, u->GetUnitData()->mName, static_cast<int>(u->GetAllegiance()),
		                      u->GetLife(), u->GetMaxLife());
	}
	PortSessionLog(PORT_SESSION_GAME, false,
	               "carrier clash started: venturer=0x%08x fenrir=0x%08x",
	               static_cast<unsigned>(gDirector.VenturerHandle()),
	               static_cast<unsigned>(gDirector.FenrirHandle()));
}

extern "C" void PortCeCarrierClashLevelEnd(int level, int quit) {
	(void)level;
	if (!gArmed) return;
	const bool keepForRetry = (quit == QT_RESTART_LEVEL) && !gLoadRefused;
	gDirector.EndLevel(keepForRetry);
	if (keepForRetry) {
		PortCeCarrierClashMarkRetryReset();
	}
}

extern "C" void PortCeCarrierClashUnitSpawn(CUnit *unit, CInitThing *init) {
	(void)init;
	if (!PortCeCarrierClashActive() || !gDirector.IsRunning() || !unit) return;
	CUnitData *data = unit->GetUnitData();
	if (!data || !data->mName) return;
	const CeEntityHandle h = CeEntityHandleFor(unit);
	if (h == kCeEntityHandleInvalid) return;
	gDirector.OnUnitSpawn(h, data->mName, static_cast<int>(unit->GetAllegiance()),
	                      unit->GetLife(), unit->GetMaxLife());
}

extern "C" void PortCeCarrierClashUnitDestroyed(CUnit *unit) {
	if (!PortCeCarrierClashActive() || !gDirector.IsRunning() || !unit) return;
	const CeEntityHandle h = CeEntityHandleLookup(unit);
	if (h != kCeEntityHandleInvalid) {
		gDirector.OnUnitDestroyed(h);
	}
	if (unit->IsA(THING_TYPE_BATTLE_ENGINE)) {
		CPlayer *p0 = GAME.GetPlayer(0);
		if (p0 && p0->GetBattleEngine() == unit) {
			gDirector.OnPlayerLost();
		}
	}
}

extern "C" void PortCeCarrierClashTickPre(void) {
	if (!PortCeCarrierClashActive() || !gDirector.IsRunning()) return;
	if (GAME.IsPaused() || GAME.GetGameState() == GAME_STATE_PRE_RUNNING) return;

	CUnit *venturer = CeEntityHandleResolveUnit(gDirector.VenturerHandle());
	CUnit *fenrir = CeEntityHandleResolveUnit(gDirector.FenrirHandle());
	const bool venturerAlive = venturer && !venturer->IsDying() && !venturer->IsShuttingDown() && venturer->GetLife() > 0.0f;
	const bool fenrirAlive = fenrir && !fenrir->IsDying() && !fenrir->IsShuttingDown() && fenrir->GetLife() > 0.0f;

	gDirector.UpdateFlagshipTelemetry(
		venturerAlive ? venturer->GetLife() : 0.0f,
		venturer ? venturer->GetMaxLife() : 0.0f,
		venturerAlive,
		fenrirAlive ? fenrir->GetLife() : 0.0f,
		fenrir ? fenrir->GetMaxLife() : 0.0f,
		fenrirAlive);

	if (gDirector.Phase() == CE_CC_ACTIVE) {
		int liveUnits = 0;
		ListIterator<CUnit> it(&WORLD.GetUnitNB());
		for (CUnit *u = it.First(); u; u = it.Next()) {
			if (u && !u->IsDying() && !u->IsShuttingDown()) ++liveUnits;
		}

		CeCarrierClashSpawnOrder spawns[4] = {};
		int spawnCount = 0;
		CeCarrierClashPatrolOrder vPatrol = {}, fPatrol = {};
		gDirector.Tick(CLOCK_TICK, liveUnits, spawns, 4, &spawnCount, &vPatrol, &fPatrol);

		if (vPatrol.valid && venturerAlive) {
			venturer->GoToPoint(FVector(vPatrol.x, vPatrol.y, vPatrol.z), FALSE);
		}
		if (fPatrol.valid && fenrirAlive) {
			fenrir->GoToPoint(FVector(fPatrol.x, fPatrol.y, fPatrol.z), FALSE);
		}
		for (int i = 0; i < spawnCount; ++i) {
			SpawnDynamicEscort(spawns[i]);
		}
	}

	if (!gDirector.OutcomeDeclared() && GAME.GetGameState() == GAME_STATE_PLAYING) {
		if (gDirector.Phase() == CE_CC_VICTORY) {
			gDirector.MarkOutcomeDeclared();
			GAME.DeclareLevelWon();
		} else if (gDirector.Phase() == CE_CC_DEFEAT) {
			gDirector.MarkOutcomeDeclared();
			GAME.DeclareLevelLost(0, FALSE);
		}
	}
}

extern "C" void PortCeCarrierClashDrawHud(CPlayer *player, int viewpoint) {
	(void)player;
	if (viewpoint != 0) return;
	if (!PortCeCarrierClashActive() || !gDirector.IsRunning()) return;
	if (PortCePhotoOn()) return;

	LPDIRECT3DDEVICE9 dev = LT.GetDevice();
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!dev || !font) return;

	const float vw = static_cast<float>(PLATFORM.GetWindowWidth());
	const float vh = static_cast<float>(PLATFORM.GetWindowHeight());
	const float hudScale = vh / 480.0f;
	const float uiScale = (hudScale >= 0.85f && hudScale <= 2.5f) ? hudScale : (hudScale < 0.85f ? 0.85f : 2.5f);

	const CeCarrierClashHudState state = gDirector.BuildHudState();
	const bool showBanner = (state.phase == CE_CC_VICTORY || state.phase == CE_CC_DEFEAT);
	const CeCarrierClashHudLayout layout = CeCarrierClashComputeHudLayout(vw, vh, uiScale, showBanner);
	if (!layout.valid) return;

	BeginHudUntextured();

	// 9-slice top-center tactical strip: dark maritime glass + Forseti cyan left
	// bracket + Muspell crimson right bracket + dual flagship hull bars.
	const float inset = layout.glowPad + layout.strokeWidth * 0.45f;
	DrawNineSliceGlass(dev, layout.panel, layout.cornerRadius, inset, 0xd80b1c2eu, 0xe406101au);

	const float coreHalf = layout.strokeWidth * 0.5f;
	// Left bracket (Forseti cyan)
	DrawNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth,
	                     false, coreHalf + layout.glowPad, 0x4c28d4ffu);
	DrawNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth,
	                     false, coreHalf, 0xf23ce4ffu);
	DrawNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth,
	                     false, coreHalf * 0.42f, 0xffccf8ffu);
	// Right bracket (Muspell crimson)
	DrawNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth,
	                     true, coreHalf + layout.glowPad, 0x4cff4414u);
	DrawNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth,
	                     true, coreHalf, 0xf2f04e1au);
	DrawNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth,
	                     true, coreHalf * 0.42f, 0xffffc464u);

	// Hull bar tracks and proportional fills
	DrawRectD3D(dev, layout.venturerBar.x, layout.venturerBar.y, layout.venturerBar.w, layout.venturerBar.h, 0xc0081622u);
	DrawRectD3D(dev, layout.fenrirBar.x, layout.fenrirBar.y, layout.fenrirBar.w, layout.fenrirBar.h, 0xc0220a08u);

	const float vFrac = static_cast<float>(state.venturerPct) * 0.01f;
	const float fFrac = static_cast<float>(state.fenrirPct) * 0.01f;
	const unsigned vFillCol = state.venturerPct > 35 ? 0xf032d8ffu : 0xf0ffaa30u;
	const unsigned fFillCol = state.fenrirPct > 35 ? 0xf0ff4c28u : 0xf0ff9040u;
	DrawRectD3D(dev, layout.venturerBar.x + 1.0f, layout.venturerBar.y + 1.0f,
	            (layout.venturerBar.w - 2.0f) * vFrac, layout.venturerBar.h - 2.0f, vFillCol);
	// Fenrir bar drains inward toward centre for clean mirrored contrast
	const float fFillW = (layout.fenrirBar.w - 2.0f) * fFrac;
	DrawRectD3D(dev, layout.fenrirBar.x + layout.fenrirBar.w - 1.0f - fFillW,
	            layout.fenrirBar.y + 1.0f, fFillW, layout.fenrirBar.h - 2.0f, fFillCol);

	// Optional Victory / Defeat 9-slice outcome banner below the status strip
	if (layout.showBanner) {
		const bool won = (state.phase == CE_CC_VICTORY);
		const unsigned topGlass = won ? 0xe00b2638u : 0xe02c0e0cu;
		const unsigned botGlass = won ? 0xe8061420u : 0xe8180606u;
		const unsigned haloCol = won ? 0x5028e0ffu : 0x50ff3c20u;
		const unsigned bodyCol = won ? 0xf43ce4ffu : 0xf4f04e1au;
		const unsigned coreCol = won ? 0xffd8fbffu : 0xffffc870u;
		const float br = floorf(10.0f * uiScale);
		DrawNineSliceGlass(dev, layout.bannerBox, br, inset, topGlass, botGlass);
		for (int side = 0; side < 2; ++side) {
			const bool right = (side == 1);
			DrawNineSliceBracket(dev, layout.bannerBox, br, layout.glowPad, layout.strokeWidth, right, coreHalf + layout.glowPad, haloCol);
			DrawNineSliceBracket(dev, layout.bannerBox, br, layout.glowPad, layout.strokeWidth, right, coreHalf, bodyCol);
			DrawNineSliceBracket(dev, layout.bannerBox, br, layout.glowPad, layout.strokeWidth, right, coreHalf * 0.42f, coreCol);
		}
	}

	EndHudUntextured();

	// Crisp FONT_SMALL labels (strictly safe glyphs)
	const float textScale = 0.85f * uiScale;
	const float textY = layout.panel.y + floorf(6.0f * uiScale);
	DrawSafeHudText(font, layout.venturerBar.x, textY, 0xffcceeffu,
	                state.venturerText, textScale, layout.venturerBar.w);
	DrawSafeHudTextCentered(font, layout.panel.x + layout.panel.w * 0.5f, textY, 0xffe8eef4u,
	                        "VS", textScale * 0.90f, floorf(28.0f * uiScale));
	{
		WCHAR wideF[64] = {};
		size_t fn = 0u;
		for (; state.fenrirText[fn] && fn + 1u < 64u; ++fn)
			wideF[fn] = static_cast<WCHAR>(static_cast<unsigned char>(state.fenrirText[fn]));
		wideF[fn] = 0;
		SIZE ext = {0, 0};
		font->GetTextExtent(wideF, &ext);
		float fxScale = textScale;
		if (ext.cx > 0 && float(ext.cx) * fxScale > layout.fenrirBar.w)
			fxScale = layout.fenrirBar.w / float(ext.cx);
		const float fx = layout.fenrirBar.x + layout.fenrirBar.w - float(ext.cx) * fxScale;
		DrawSafeHudText(font, floorf(fx), textY, 0xffffd4c0u,
		                state.fenrirText, fxScale, layout.fenrirBar.w);
	}

	if (layout.showBanner) {
		const unsigned bannerCol = (state.phase == CE_CC_VICTORY) ? 0xff7cf4ffu : 0xffff8860u;
		const float bannerY = layout.bannerBox.y + floorf(7.0f * uiScale);
		DrawSafeHudTextCentered(font, layout.bannerBox.x + layout.bannerBox.w * 0.5f, bannerY,
		                        bannerCol, state.bannerText, 0.95f * uiScale,
		                        layout.bannerBox.w - floorf(24.0f * uiScale));
	}

	// Restore depth test/write state after DrawSafeHudText so later draw_hud hooks see clean Z state.
	LT.SRS(D3DRS_ZENABLE, TRUE);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
}

extern "C" void PortCeCarrierClashFilterSave(CCareer *career, void *dest) {
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
