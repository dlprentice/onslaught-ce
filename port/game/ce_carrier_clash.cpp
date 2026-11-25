// SPDX-License-Identifier: GPL-3.0-only
// Standalone clean-room Carrier Clash (F.S. Venturer vs. Fenrir) mission
// director, lifecycle, 9-slice HUD strip, and ce_hooks integration.
#ifdef CE_VERSION

#include "common.h"
#include "Career.h"
#include "frontend.h"
#include "game.h"
#include "GameInterface.h"
#include "PauseMenu.h"
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
bool PortCeCmdMapOpen() __attribute__((weak));
extern "C" bool PortCeMuseumActive(void) __attribute__((weak));
extern "C" bool PortCeMuseumVisitActive(void) __attribute__((weak));
extern "C" bool PortCeEditorActive(void) __attribute__((weak));
extern "C" bool PortCeLivingSandboxActive(void) __attribute__((weak));
extern "C" int PortCePlayUnitShowsBattleline(int viewpoint) __attribute__((weak));
extern "C" int PortCePlayUnitOwnsPilot(int player) __attribute__((weak));

namespace {

CeCarrierClashDirector gDirector;
bool gArmed = false;
bool gLoadRefused = false;
bool gPendingNotice = false;
const char *gMessage = "Carrier Clash could not be started. Your career is unchanged.";
const char *gNotice = NULL;

unsigned char gCareerSnapshot[sizeof(CCareer)];
bool gCareerSnapshotHeld = false;
bool gP0StasisApplied = false;
bool gOutcomeShieldApplied = false;

bool IsP0DrivingOtherUnit() {
	return (PortCePlayUnitShowsBattleline && PortCePlayUnitShowsBattleline(0) != 0) ||
	       (PortCePlayUnitOwnsPilot && PortCePlayUnitOwnsPilot(0) != 0);
}

float ComputeCarrierClashUiScale(float vh) {
	const float hudScale = vh / 480.0f;
	return (hudScale >= 0.85f && hudScale <= 4.5f) ? hudScale : (hudScale < 0.85f ? 0.85f : 4.5f);
}

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
	if (PortCeLivingSandboxActive && PortCeLivingSandboxActive()) return false;
	return true;
}

void UpdateP0PlayUnitStasis() {
	CPlayer *p0 = GAME.GetPlayer(0);
	CBattleEngine *be = p0 ? p0->GetBattleEngine() : NULL;
	if (!be) return;

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

	if (gP0StasisApplied) {
		be->SetVulnerable(TRUE);
		gP0StasisApplied = false;
	}
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

struct CeCcHudBatch {
	static constexpr int kMaxVerts = 2048;
	CeCcQuadVertex verts[kMaxVerts];
	int count;

	void Reset() { count = 0; }

	void Flush(LPDIRECT3DDEVICE9 dev) {
		if (dev && count >= 3) {
			dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(count / 3),
			                     verts, sizeof(CeCcQuadVertex));
		}
		count = 0;
	}

	void PushTri(LPDIRECT3DDEVICE9 dev, const CeCcQuadVertex &a,
	             const CeCcQuadVertex &b, const CeCcQuadVertex &c) {
		if (count + 3 > kMaxVerts) Flush(dev);
		verts[count++] = a;
		verts[count++] = b;
		verts[count++] = c;
	}

	void EmitRect(LPDIRECT3DDEVICE9 dev, float x, float y, float w, float h, unsigned col) {
		if (w <= 0.0f || h <= 0.0f) return;
		const CeCcQuadVertex v0 = {x,     y,     0.006f, 1.0f, col};
		const CeCcQuadVertex v1 = {x + w, y,     0.006f, 1.0f, col};
		const CeCcQuadVertex v2 = {x,     y + h, 0.006f, 1.0f, col};
		const CeCcQuadVertex v3 = {x + w, y + h, 0.006f, 1.0f, col};
		PushTri(dev, v0, v1, v2);
		PushTri(dev, v1, v3, v2);
	}

	void EmitNineSliceGlass(LPDIRECT3DDEVICE9 dev, const CeCarrierClashHudRect &rect,
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

		for (int i = 1; i + 1 < n; ++i) {
			PushTri(dev, fan[0], fan[i], fan[i + 1]);
		}
	}

	void EmitNineSliceBracket(LPDIRECT3DDEVICE9 dev, const CeCarrierClashHudRect &rect,
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
		int ptCount = 0;
		if (lipLen > 1.0f) {
			pts[ptCount++] = {lx + lipLen, ty - rMid, 0.0f, -1.0f, 0.40f};
		}
		for (int k = kSegs; k >= 0; --k) {
			const float frac = float(k) / float(kSegs);
			const float th = 3.14159265f + (1.57079633f * frac);
			const float c = cosf(th), s = sinf(th);
			const float sw = 1.0f + 0.20f * sinf(frac * 3.14159265f);
			pts[ptCount++] = {lx + rMid * c, ty + rMid * s, c, s, sw};
		}
		for (int k = 0; k <= kSegs; ++k) {
			const float frac = float(k) / float(kSegs);
			const float th = 3.14159265f + (1.57079633f * frac);
			const float c = cosf(th), s = sinf(th);
			const float sw = 1.0f + 0.20f * sinf(frac * 3.14159265f);
			pts[ptCount++] = {lx + rMid * c, 2.0f * cy - (ty + rMid * s), c, -s, sw};
		}
		if (lipLen > 1.0f) {
			pts[ptCount++] = {lx + lipLen, 2.0f * cy - (ty - rMid), 0.0f, 1.0f, 0.40f};
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
		for (int i = 0; i < ptCount; ++i) {
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
		for (int i = 0; i + 1 < ptCount; ++i) {
			const CeCcQuadVertex &a0 = strip[i * 2 + 0];
			const CeCcQuadVertex &a1 = strip[i * 2 + 1];
			const CeCcQuadVertex &b0 = strip[(i + 1) * 2 + 0];
			const CeCcQuadVertex &b1 = strip[(i + 1) * 2 + 1];
			PushTri(dev, a0, a1, b0);
			PushTri(dev, a1, b1, b0);
		}
	}
};

CeCcHudBatch gHudBatch;

// Localized retail text.db IDs (english.dat):
//   534802956 (_231_LOSE_VENTURER): "The Venturer had to withdraw"
//   54406750  (GAME_OVER_DEATH):    "Battle Engine was destroyed."
constexpr int kVenturerLostTextId = 534802956;
constexpr int kBattleEngineLostTextId = 54406750;

// Opening title. Carrier Clash is an Onslaught CE original bonus duel, not a
// retail mission: the opening banner says so once, in place of the director's
// plain opening line. Every glyph is one FONT_SMALL has, and it is shorter than
// the longest banner this box already draws.
const char kOpeningTitle[] = "CARRIER CLASH (CE ORIGINAL) - BROADSIDE DUEL";

const char *BannerTextToDraw(const CeCarrierClashHudState &state) {
	if (state.phase == CE_CC_ACTIVE &&
	    std::strcmp(state.bannerText, CeCarrierClashActBanner(0)) == 0) {
		return kOpeningTitle;
	}
	return state.bannerText;
}

void DrawSafeHudTextScaled(CDXBitmapFont *font, float x, float y, unsigned colour,
                           const WCHAR *wide, float sx, float sy) {
	if (!font || !wide || !wide[0]) return;
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, FALSE);
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	font->DrawTextWithShadow(x, y, colour, const_cast<WCHAR *>(wide), 0, 0.0f, sx, sy);
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
	DrawSafeHudTextScaled(font, x, y, colour, wide, sx, scale);
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
	DrawSafeHudTextScaled(font, floorf(cx - drawW * 0.5f), y, colour, wide, sx, scale);
}

void DrawSafeHudTextRight(CDXBitmapFont *font, float rightX, float y, unsigned colour,
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
	DrawSafeHudTextScaled(font, floorf(rightX - drawW), y, colour, wide, sx, scale);
}

// The live half of the separation guard (the director re-issues lane orders on
// its own): two capital hulls closer than kCeCcHardSeparationDist are eased
// apart along the line between them, a fraction of a metre a tick, and lose the
// part of their velocity that was closing the gap. Sim phase only.
void EnforceFlagshipSeparation(CUnit *venturer, CUnit *fenrir) {
	if (!venturer || !fenrir) return;
	const FVector vp = venturer->GetPos();
	const FVector fp = fenrir->GetPos();
	const float kMaxStepPerTick = 0.60f;
	const CeCarrierClashSeparationPush push = CeCarrierClashComputeSeparationPush(
		vp.X, vp.Y, fp.X, fp.Y, kCeCcHardSeparationDist, kMaxStepPerTick);
	if (!push.push) return;
	float nx = fp.X - vp.X, ny = fp.Y - vp.Y;
	const float d = std::sqrt(nx * nx + ny * ny);
	if (d > 1e-3f) { nx /= d; ny /= d; } else { nx = 1.0f; ny = 0.0f; }

	venturer->MoveTo(FVector(vp.X + push.venturerDx, vp.Y + push.venturerDy, vp.Z));
	fenrir->MoveTo(FVector(fp.X + push.fenrirDx, fp.Y + push.fenrirDy, fp.Z));

	FVector vv = venturer->GetVelocity();
	const float vClosing = vv.X * nx + vv.Y * ny;       // > 0: Venturer heading at Fenrir
	if (vClosing > 0.0f) {
		vv.X -= vClosing * nx;
		vv.Y -= vClosing * ny;
		venturer->SetVelocity(vv);
	}
	FVector fv = fenrir->GetVelocity();
	const float fClosing = -(fv.X * nx + fv.Y * ny);    // > 0: Fenrir heading at the Venturer
	if (fClosing > 0.0f) {
		fv.X += fClosing * nx;
		fv.Y += fClosing * ny;
		fenrir->SetVelocity(fv);
	}
}

// Leaves the device the way HUD.RenderObjectiveBar leaves it for the hooks and
// engine passes that draw after this one: back-face culling, alpha test and
// straight alpha blending on, fog and lighting as the HUD pass has them (off),
// depth test on, stage 0 at the engine's own texture * diffuse modulate.
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
	gP0StasisApplied = false;
	gOutcomeShieldApplied = false;
	gDirector.Arm();
	return true;
}

extern "C" bool PortCeCarrierClashActive(void) {
	return gArmed && PortCeLayer() && PortCeMenus() && !GAME.IsMultiplayer();
}

extern "C" void PortCeCarrierClashCancel(void) {
	gArmed = false;
	gLoadRefused = false;
	gP0StasisApplied = false;
	gOutcomeShieldApplied = false;
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
	gP0StasisApplied = false;
	gOutcomeShieldApplied = false;
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
	gP0StasisApplied = false;
	gOutcomeShieldApplied = false;
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
		const bool drivingOther = gP0StasisApplied || IsP0DrivingOtherUnit();
		CPlayer *p0 = GAME.GetPlayer(0);
		if (!drivingOther && p0 && p0->GetBattleEngine() == unit) {
			gDirector.OnPlayerLost();
		}
	}
}

extern "C" int PortCeCarrierClashBeInStasis(const void *be) {
	if (!PortCeCarrierClashActive() || !gDirector.IsRunning() || !be) return 0;
	CPlayer *p0 = GAME.GetPlayer(0);
	if (!p0 || static_cast<const void *>(p0->GetBattleEngine()) != be) return 0;
	return (gP0StasisApplied || IsP0DrivingOtherUnit()) ? 1 : 0;
}

extern "C" void PortCeCarrierClashTickPre(void) {
	if (!PortCeCarrierClashActive() || !gDirector.IsRunning()) return;
	// Direct P0 death runs GAME.DeclarePlayerDead() before ce_hook_unit_destroyed; keep director synced.
	if (GAME.GetGameState() == GAME_STATE_LEVEL_LOST && gDirector.Phase() == CE_CC_ACTIVE) {
		gDirector.OnPlayerLost();
		gDirector.MarkOutcomeDeclared();
	}
	if (GAME.IsPaused() || GAME.GetGameState() == GAME_STATE_PRE_RUNNING) return;

	UpdateP0PlayUnitStasis();

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

	if (venturerAlive && fenrirAlive) {
		const FVector vp = venturer->GetPos();
		const FVector fp = fenrir->GetPos();
		gDirector.UpdateFlagshipPositions(vp.X, vp.Y, fp.X, fp.Y);
	} else {
		gDirector.ClearFlagshipPositions();
	}

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
		if (venturerAlive && fenrirAlive) {
			EnforceFlagshipSeparation(venturer, fenrir);
		}
		for (int i = 0; i < spawnCount; ++i) {
			SpawnDynamicEscort(spawns[i]);
		}
	}

	// The director's phase turns the instant a flagship falls; the level result
	// waits a few seconds so the death plays out in the world instead of freezing
	// on its first frame. The player cannot lose the level during that hold.
	if (!gDirector.OutcomeDeclared() && GAME.GetGameState() == GAME_STATE_PLAYING &&
	    (gDirector.Phase() == CE_CC_VICTORY || gDirector.Phase() == CE_CC_DEFEAT)) {
		if (gDirector.Phase() == CE_CC_VICTORY && !gOutcomeShieldApplied && !gP0StasisApplied) {
			CPlayer *p0 = GAME.GetPlayer(0);
			CBattleEngine *be = p0 ? p0->GetBattleEngine() : NULL;
			if (be) {
				be->SetVulnerable(FALSE);
				gOutcomeShieldApplied = true;
			}
		}
		if (gDirector.TickOutcomeHold(CLOCK_TICK)) {
			gDirector.MarkOutcomeDeclared();
			if (gDirector.Phase() == CE_CC_VICTORY) {
				GAME.DeclareLevelWon();
			} else {
				const int reasonId = venturerAlive ? kBattleEngineLostTextId : kVenturerLostTextId;
				GAME.DeclareLevelLost(reasonId, FALSE);
			}
		}
	}
}

namespace {

bool ShouldRenderCarrierClashHud(bool overlayPass, bool *outEndState) {
	if (outEndState) *outEndState = false;
	if (!PortCeCarrierClashActive() || !gDirector.IsRunning()) return false;
	const EGameState gs = GAME.GetGameState();
	const bool endState = (gs == GAME_STATE_LEVEL_WON || gs == GAME_STATE_LEVEL_LOST);
	if (outEndState) *outEndState = endState;
	const bool pauseMenuOpen = (GAME.GetPauseMenu() && GAME.GetPauseMenu()->mActive);
	if (PortCePhotoOn() || GAMEINTERFACE.IsMenuUp() || pauseMenuOpen) return false;
	if (PortCeCmdMapOpen && PortCeCmdMapOpen()) return false;
	if (GAME.GetGameState() != GAME_STATE_PLAYING && !endState) return false;
	if (!endState && GAME.IsPaused()) return false;
	if (overlayPass && !endState) return false;
	return true;
}

void RenderCarrierClashHud(bool overlayPass) {
	bool endState = false;
	if (!ShouldRenderCarrierClashHud(overlayPass, &endState)) return;
	// The end-of-level darkener is drawn after the HUD pass: an outcome drawn there
	// sits under it. The overlay pass draws the outcome instead, above the darkener.
	if (endState != overlayPass) return;

	LPDIRECT3DDEVICE9 dev = LT.GetDevice();
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!dev || !font) return;

	const float vw = static_cast<float>(PLATFORM.GetWindowWidth());
	const float vh = static_cast<float>(PLATFORM.GetWindowHeight());
	if (!(vw > 0.0f && vh > 0.0f)) return;
	const float uiScale = ComputeCarrierClashUiScale(vh);

	const CeCarrierClashHudState state = gDirector.BuildHudState();
	const bool outcome = (state.phase == CE_CC_VICTORY || state.phase == CE_CC_DEFEAT);
	const CeCarrierClashHudLayout layout = CeCarrierClashComputeHudLayout(vw, vh, uiScale, state.showBanner);
	if (!layout.valid) return;

	BeginHudUntextured();
	gHudBatch.Reset();

	// 9-slice top-center tactical strip: dark maritime glass + Forseti cyan left
	// bracket + Muspell crimson right bracket + dual flagship hull bars.
	const float inset = layout.glowPad + layout.strokeWidth * 0.45f;
	gHudBatch.EmitNineSliceGlass(dev, layout.panel, layout.cornerRadius, inset, 0xd80b1c2eu, 0xe406101au);

	const float coreHalf = layout.strokeWidth * 0.5f;
	// Left bracket (Forseti cyan)
	gHudBatch.EmitNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth,
	                               false, coreHalf + layout.glowPad, 0x4c28d4ffu);
	gHudBatch.EmitNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth,
	                               false, coreHalf, 0xf23ce4ffu);
	gHudBatch.EmitNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth,
	                               false, coreHalf * 0.42f, 0xffccf8ffu);
	// Right bracket (Muspell crimson)
	gHudBatch.EmitNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth,
	                               true, coreHalf + layout.glowPad, 0x4cff4414u);
	gHudBatch.EmitNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth,
	                               true, coreHalf, 0xf2f04e1au);
	gHudBatch.EmitNineSliceBracket(dev, layout.panel, layout.cornerRadius, layout.glowPad, layout.strokeWidth,
	                               true, coreHalf * 0.42f, 0xffffc464u);

	// Hull bar tracks and proportional fills
	gHudBatch.EmitRect(dev, layout.venturerBar.x, layout.venturerBar.y, layout.venturerBar.w, layout.venturerBar.h, 0xc0081622u);
	gHudBatch.EmitRect(dev, layout.fenrirBar.x, layout.fenrirBar.y, layout.fenrirBar.w, layout.fenrirBar.h, 0xc0220a08u);

	const float vFrac = static_cast<float>(state.venturerPct) * 0.01f;
	const float fFrac = static_cast<float>(state.fenrirPct) * 0.01f;
	// Below 35% a hull bar turns amber and breathes; crossing 75 / 50 / 25 flashes
	// the whole bar white for under a second so the act change reads at a glance.
	const float vPulse = CeCarrierClashLowHullPulse(state.venturerPct, state.elapsedSec);
	const float fPulse = CeCarrierClashLowHullPulse(state.fenrirPct, state.elapsedSec);
	unsigned vFillCol = state.venturerPct >= kCeCcLowHullPct
		? 0xf032d8ffu : LerpArgb(0xf0c87a18u, 0xf0ffd070u, vPulse);
	unsigned fFillCol = state.fenrirPct >= kCeCcLowHullPct
		? 0xf0ff4c28u : LerpArgb(0xf0c85a20u, 0xf0ffc060u, fPulse);
	vFillCol = LerpArgb(vFillCol, 0xffffffffu, state.venturerFlash);
	fFillCol = LerpArgb(fFillCol, 0xffffffffu, state.fenrirFlash);
	if (state.venturerFlash > 0.0f) {
		const float pad = floorf(2.0f * uiScale);
		gHudBatch.EmitRect(dev, layout.venturerBar.x - pad, layout.venturerBar.y - pad,
		                   layout.venturerBar.w + 2.0f * pad, layout.venturerBar.h + 2.0f * pad,
		                   LerpArgb(0x0032d8ffu, 0x9032d8ffu, state.venturerFlash));
	}
	if (state.fenrirFlash > 0.0f) {
		const float pad = floorf(2.0f * uiScale);
		gHudBatch.EmitRect(dev, layout.fenrirBar.x - pad, layout.fenrirBar.y - pad,
		                   layout.fenrirBar.w + 2.0f * pad, layout.fenrirBar.h + 2.0f * pad,
		                   LerpArgb(0x00ff4c28u, 0x90ff4c28u, state.fenrirFlash));
	}
	gHudBatch.EmitRect(dev, layout.venturerBar.x + 1.0f, layout.venturerBar.y + 1.0f,
	                   (layout.venturerBar.w - 2.0f) * vFrac, layout.venturerBar.h - 2.0f, vFillCol);
	// Fenrir bar drains inward toward centre for clean mirrored contrast
	const float fFillW = (layout.fenrirBar.w - 2.0f) * fFrac;
	gHudBatch.EmitRect(dev, layout.fenrirBar.x + layout.fenrirBar.w - 1.0f - fFillW,
	                   layout.fenrirBar.y + 1.0f, fFillW, layout.fenrirBar.h - 2.0f, fFillCol);

	// Mirrored threshold tick marks at 75 / 50 / 25 on both flagship hull bars:
	// Venturer dims crossed damage thresholds; Fenrir lights reached battle acts.
	for (int k = 1; k <= kCeCcActCount; ++k) {
		const float frac = 1.0f - 0.25f * float(k);
		const float tw = uiScale >= 1.5f ? 2.0f : 1.0f;
		const float vx = layout.venturerBar.x + 1.0f + (layout.venturerBar.w - 2.0f) * frac;
		const int threshPct = 100 - 25 * k;
		gHudBatch.EmitRect(dev, floorf(vx), layout.venturerBar.y - floorf(2.0f * uiScale), tw,
		                   layout.venturerBar.h + floorf(4.0f * uiScale),
		                   state.venturerPct <= threshPct ? 0xff9cecffu : 0x78cceeffu);
		const float tx = layout.fenrirBar.x + layout.fenrirBar.w - 1.0f - (layout.fenrirBar.w - 2.0f) * frac;
		gHudBatch.EmitRect(dev, floorf(tx), layout.fenrirBar.y - floorf(2.0f * uiScale), tw,
		                   layout.fenrirBar.h + floorf(4.0f * uiScale),
		                   state.battleAct >= k ? 0xffffe0a0u : 0x90ffffffu);
	}

	// 9-slice banner below the status strip: the outcome (Forseti cyan for a win,
	// Muspell crimson for a loss) or, during the battle, the act that just began (amber).
	if (layout.showBanner) {
		const bool won = (state.phase == CE_CC_VICTORY);
		const unsigned topGlass = !outcome ? 0xe0241a0au : won ? 0xe00b2638u : 0xe02c0e0cu;
		const unsigned botGlass = !outcome ? 0xe8120c04u : won ? 0xe8061420u : 0xe8180606u;
		const unsigned haloCol = !outcome ? 0x50ffb028u : won ? 0x5028e0ffu : 0x50ff3c20u;
		const unsigned bodyCol = !outcome ? 0xf4ffc040u : won ? 0xf43ce4ffu : 0xf4f04e1au;
		const unsigned coreCol = !outcome ? 0xfffff0c8u : won ? 0xffd8fbffu : 0xffffc870u;
		const float br = floorf(10.0f * uiScale);
		gHudBatch.EmitNineSliceGlass(dev, layout.bannerBox, br, inset, topGlass, botGlass);
		for (int side = 0; side < 2; ++side) {
			const bool right = (side == 1);
			gHudBatch.EmitNineSliceBracket(dev, layout.bannerBox, br, layout.glowPad, layout.strokeWidth, right, coreHalf + layout.glowPad, haloCol);
			gHudBatch.EmitNineSliceBracket(dev, layout.bannerBox, br, layout.glowPad, layout.strokeWidth, right, coreHalf, bodyCol);
			gHudBatch.EmitNineSliceBracket(dev, layout.bannerBox, br, layout.glowPad, layout.strokeWidth, right, coreHalf * 0.42f, coreCol);
		}
	}

	gHudBatch.Flush(dev);
	EndHudUntextured();

	// Crisp FONT_SMALL labels (strictly safe glyphs)
	const float textScale = 0.85f * uiScale;
	const float textY = layout.panel.y + floorf(6.0f * uiScale);
	DrawSafeHudText(font, layout.venturerBar.x, textY, 0xffcceeffu,
	                state.venturerText, textScale, layout.venturerBar.w);
	DrawSafeHudTextCentered(font, layout.panel.x + layout.panel.w * 0.5f, textY, 0xffe8eef4u,
	                        "VS", textScale * 0.90f, floorf(28.0f * uiScale));
	DrawSafeHudTextRight(font, layout.fenrirBar.x + layout.fenrirBar.w, textY, 0xffffd4c0u,
	                     state.fenrirText, textScale, layout.fenrirBar.w);

	if (layout.showBanner) {
		const unsigned bannerCol = !outcome ? 0xffffe2a0u
		                         : (state.phase == CE_CC_VICTORY) ? 0xff7cf4ffu : 0xffff8860u;
		const float bannerY = layout.bannerBox.y + floorf(7.0f * uiScale);
		DrawSafeHudTextCentered(font, layout.bannerBox.x + layout.bannerBox.w * 0.5f, bannerY,
		                        bannerCol, BannerTextToDraw(state), 0.95f * uiScale,
		                        layout.bannerBox.w - floorf(24.0f * uiScale));
	}

	// Every state this pass changed goes back, text included, so the hooks and
	// engine passes after it see the device the HUD pass handed over.
	RestoreHudDeviceState();
}

} // namespace

extern "C" float PortCeCarrierClashHudTopBottom(float viewportW, float viewportH) {
	if (!(viewportW > 0.0f && viewportH > 0.0f)) return 0.0f;
	if (!ShouldRenderCarrierClashHud(false, NULL)) return 0.0f;
	const float uiScale = ComputeCarrierClashUiScale(viewportH);
	const CeCarrierClashHudState state = gDirector.BuildHudState();
	const CeCarrierClashHudLayout layout = CeCarrierClashComputeHudLayout(viewportW, viewportH, uiScale, state.showBanner);
	if (!layout.valid) return 0.0f;
	return layout.showBanner ? (layout.bannerBox.y + layout.bannerBox.h)
	                         : (layout.panel.y + layout.panel.h);
}

extern "C" void PortCeCarrierClashDrawHud(CPlayer *player, int viewpoint) {
	(void)player;
	if (viewpoint != 0) return;
	RenderCarrierClashHud(false);
}

// The Victory / Defeat strip and banner: drawn here, after the end-of-level
// darkener, so the outcome reads at full brightness over the dimmed world.
extern "C" void PortCeCarrierClashDrawOverlay(void) {
	RenderCarrierClashHud(true);
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
