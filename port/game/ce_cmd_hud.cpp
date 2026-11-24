// SPDX-License-Identifier: GPL-3.0-only
// Commander Mode RTS HUD renderer: faction-symmetric translucent glass command bar
// with true 9-slice mirrored holographic capsule brackets, fixed-scale truncated card
// typography, crisp clickable button borders, and high-contrast Forseti/Muspell palettes.

#include "ce_cmd_hud.h"
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(CE_VERSION) && !defined(CE_COMMANDER_CAMERA_INTEGRATION)
#define CE_CMD_HUD_USE_D3D9 1
#include "common.h"
#include "ltshell.h"
#include "renderinfo.h"
#endif

namespace
{
void SafeBox(const CeCmdHudDrawOps &draw, float x, float y, float w, float h, std::uint32_t colour)
{
	if (w > 0.f && h > 0.f) draw.box(x, y, w, h, colour);
}

void SafeOutline(const CeCmdHudDrawOps &draw, float x, float y, float w, float h, float t, std::uint32_t colour)
{
	if (w > 0.f && h > 0.f && t > 0.f) draw.outline(x, y, w, h, t, colour);
}

std::uint32_t WithAlpha(std::uint32_t colour, std::uint32_t alphaByte)
{
	return ((alphaByte & 0xffu) << 24) | (colour & 0x00ffffffu);
}

#ifdef CE_CMD_HUD_USE_D3D9
std::uint32_t ScaleAlpha(std::uint32_t colour, float factor)
{
	if (factor <= 0.f) return colour & 0x00ffffffu;
	if (factor > 1.f) factor = 1.f;
	std::uint32_t a = static_cast<std::uint32_t>(float((colour >> 24) & 0xffu) * factor + 0.5f);
	if (a > 255u) a = 255u;
	return (a << 24) | (colour & 0x00ffffffu);
}

std::uint32_t LerpColor(std::uint32_t c0, std::uint32_t c1, float t)
{
	if (t <= 0.f) return c0;
	if (t >= 1.f) return c1;
	const auto ch = [t](std::uint32_t a, std::uint32_t b) -> std::uint32_t {
		const float v = float(a) + (float(b) - float(a)) * t;
		return v <= 0.f ? 0u : (v >= 255.f ? 255u : static_cast<std::uint32_t>(v + 0.5f));
	};
	const std::uint32_t a = ch((c0 >> 24) & 0xffu, (c1 >> 24) & 0xffu);
	const std::uint32_t r = ch((c0 >> 16) & 0xffu, (c1 >> 16) & 0xffu);
	const std::uint32_t g = ch((c0 >> 8) & 0xffu, (c1 >> 8) & 0xffu);
	const std::uint32_t b = ch(c0 & 0xffu, c1 & 0xffu);
	return (a << 24) | (r << 16) | (g << 8) | b;
}

struct CeCmdHudVertex
{
	float x, y, z, rhw;
	DWORD color;
};

struct CeCmdHudTriBatch
{
	static constexpr int kMaxVerts = 3072;
	CeCmdHudVertex verts[kMaxVerts];
	int count = 0;

	void Flush(LPDIRECT3DDEVICE9 dev)
	{
		if (count >= 3 && dev)
			dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, count / 3, verts, sizeof(CeCmdHudVertex));
		count = 0;
	}

	void AddTri(LPDIRECT3DDEVICE9 dev, const CeCmdHudVertex &a, const CeCmdHudVertex &b, const CeCmdHudVertex &c)
	{
		if (count + 3 > kMaxVerts) Flush(dev);
		verts[count++] = a;
		verts[count++] = b;
		verts[count++] = c;
	}

	void AddFan(LPDIRECT3DDEVICE9 dev, const CeCmdHudVertex *fan, int n)
	{
		for (int i = 1; i + 1 < n; ++i)
			AddTri(dev, fan[0], fan[i], fan[i + 1]);
	}

	void AddStrip(LPDIRECT3DDEVICE9 dev, const CeCmdHudVertex *strip, int n)
	{
		for (int i = 0; i + 2 < n; ++i)
			AddTri(dev, strip[i], strip[i + 1], strip[i + 2]);
	}

	void AddRect(LPDIRECT3DDEVICE9 dev, float x, float y, float w, float h, float z, std::uint32_t col)
	{
		if (w <= 0.f || h <= 0.f) return;
		const CeCmdHudVertex v0 = {x,     y,     z, 1.f, col};
		const CeCmdHudVertex v1 = {x + w, y,     z, 1.f, col};
		const CeCmdHudVertex v2 = {x,     y + h, z, 1.f, col};
		const CeCmdHudVertex v3 = {x + w, y + h, z, 1.f, col};
		AddTri(dev, v0, v1, v2);
		AddTri(dev, v1, v3, v2);
	}
};

void BeginUntextured2D()
{
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

void EndUntextured2D()
{
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SRS(D3DRS_ALPHAREF, 8);
}

// Emits a smooth 4-way symmetric rounded-capsule glass fill into batch.
void EmitRoundedGlassFillD3D(LPDIRECT3DDEVICE9 dev, CeCmdHudTriBatch &batch, const CeCmdHudNineSlice &ns, float inset,
                             std::uint32_t topCol, std::uint32_t botCol)
{
	static constexpr int kSegs = 8;
	const float rFill = ns.radius - inset > 1.f ? (ns.radius - inset) : 1.f;
	const float cx = ns.x + ns.w * 0.5f;
	const float cy = ns.y + ns.h * 0.5f;
	const float invH = ns.h > 1.f ? (1.f / ns.h) : 0.f;

	float tlx[kSegs + 1], tly[kSegs + 1];
	for (int k = 0; k <= kSegs; ++k)
	{
		const float th = 3.14159265f + (1.57079633f * float(k) / float(kSegs));
		tlx[k] = ns.leftCenterX + rFill * std::cos(th);
		tly[k] = ns.topCenterY + rFill * std::sin(th);
	}

	CeCmdHudVertex fan[1 + (kSegs + 1) * 4 + 1];
	int n = 0;
	fan[n++] = {cx, cy, 0.009f, 1.f, LerpColor(topCol, botCol, 0.5f)};
	auto emit = [&](float px, float py) {
		const float fy = (py - ns.y) * invH;
		fan[n++] = {px, py, 0.009f, 1.f, LerpColor(topCol, botCol, fy)};
	};
	for (int k = 0; k <= kSegs; ++k) emit(tlx[k], tly[k]);
	for (int k = kSegs; k >= 0; --k) emit(2.f * cx - tlx[k], tly[k]);
	for (int k = 0; k <= kSegs; ++k) emit(2.f * cx - tlx[k], 2.f * cy - tly[k]);
	for (int k = kSegs; k >= 0; --k) emit(tlx[k], 2.f * cy - tly[k]);
	fan[n++] = fan[1];

	batch.AddFan(dev, fan, n);
}

// Emits a closed 4-way symmetric rounded border strip into batch.
void EmitRoundedBorderLoopD3D(LPDIRECT3DDEVICE9 dev, CeCmdHudTriBatch &batch, const CeCmdHudNineSlice &ns, float inset,
                              float thickness, std::uint32_t col)
{
	static constexpr int kSegs = 6;
	const float rOut = ns.radius - inset > 1.f ? (ns.radius - inset) : 1.f;
	const float rIn = rOut - thickness > 0.25f ? (rOut - thickness) : 0.25f;
	const float cx = ns.x + ns.w * 0.5f;
	const float cy = ns.y + ns.h * 0.5f;

	float ox[kSegs + 1], oy[kSegs + 1], ix[kSegs + 1], iy[kSegs + 1];
	for (int k = 0; k <= kSegs; ++k)
	{
		const float th = 3.14159265f + (1.57079633f * float(k) / float(kSegs));
		const float c = std::cos(th), s = std::sin(th);
		ox[k] = ns.leftCenterX + rOut * c;
		oy[k] = ns.topCenterY + rOut * s;
		ix[k] = ns.leftCenterX + rIn * c;
		iy[k] = ns.topCenterY + rIn * s;
	}

	CeCmdHudVertex strip[((kSegs + 1) * 4 + 1) * 2];
	int n = 0;
	auto emitPair = [&](float pxo, float pyo, float pxi, float pyi) {
		strip[n++] = {pxo, pyo, 0.0085f, 1.f, col};
		strip[n++] = {pxi, pyi, 0.0085f, 1.f, col};
	};
	for (int k = 0; k <= kSegs; ++k) emitPair(ox[k], oy[k], ix[k], iy[k]);
	for (int k = kSegs; k >= 0; --k) emitPair(2.f * cx - ox[k], oy[k], 2.f * cx - ix[k], iy[k]);
	for (int k = 0; k <= kSegs; ++k) emitPair(2.f * cx - ox[k], 2.f * cy - oy[k], 2.f * cx - ix[k], 2.f * cy - iy[k]);
	for (int k = kSegs; k >= 0; --k) emitPair(ox[k], 2.f * cy - oy[k], ix[k], 2.f * cy - iy[k]);
	strip[n++] = strip[0];
	strip[n++] = strip[1];

	batch.AddStrip(dev, strip, n);
}

// Emits one side (mirror == false -> left, mirror == true -> exact right mirror across cx)
// of a true 9-slice glowing curved capsule bracket into batch.
void EmitBracketSideD3D(LPDIRECT3DDEVICE9 dev, CeCmdHudTriBatch &batch, const CeCmdHudNineSlice &ns, bool mirrorRight,
                        float halfW, std::uint32_t col)
{
	static constexpr int kSegs = 8;
	const float cx = ns.x + ns.w * 0.5f;
	const float cy = ns.y + ns.h * 0.5f;
	const float coreHalf = ns.strokeWidth * 0.5f;
	const float rMid = ns.radius - ns.glowPad - coreHalf > 1.f ? (ns.radius - ns.glowPad - coreHalf) : 1.f;
	const float lipLen = ns.bodySpanW * 0.14f < ns.radius * 0.60f ? (ns.bodySpanW * 0.14f) : (ns.radius * 0.60f);

	struct SpinePt { float x, y, nx, ny, scaleW; };
	SpinePt pts[2 + (kSegs + 1) * 2];
	int count = 0;

	if (lipLen > 1.f)
		pts[count++] = {ns.leftCenterX + lipLen, ns.topCenterY - rMid, 0.f, -1.f, 0.35f};

	for (int k = kSegs; k >= 0; --k)
	{
		const float frac = float(k) / float(kSegs);
		const float th = 3.14159265f + 1.57079633f * frac;
		const float c = std::cos(th), s = std::sin(th);
		const float sw = 1.0f + 0.22f * std::sin(frac * 3.14159265f);
		pts[count++] = {ns.leftCenterX + rMid * c, ns.topCenterY + rMid * s, c, s, sw};
	}
	for (int k = 0; k <= kSegs; ++k)
	{
		const float frac = float(k) / float(kSegs);
		const float th = 3.14159265f + 1.57079633f * frac;
		const float c = std::cos(th), s = std::sin(th);
		const float sw = 1.0f + 0.22f * std::sin(frac * 3.14159265f);
		pts[count++] = {ns.leftCenterX + rMid * c, 2.f * cy - (ns.topCenterY + rMid * s), c, -s, sw};
	}
	if (lipLen > 1.f)
		pts[count++] = {ns.leftCenterX + lipLen, 2.f * cy - (ns.topCenterY - rMid), 0.f, 1.f, 0.35f};

	CeCmdHudVertex strip[(2 + (kSegs + 1) * 2) * 2];
	const float minX = ns.x, maxX = ns.x + ns.w;
	const float minY = ns.y, maxY = ns.y + ns.h;
	auto clampBox = [&](float &px, float &py) {
		if (px < minX) px = minX;
		if (px > maxX) px = maxX;
		if (py < minY) py = minY;
		if (py > maxY) py = maxY;
	};
	for (int i = 0; i < count; ++i)
	{
		float px = pts[i].x, py = pts[i].y;
		float nx = pts[i].nx, ny = pts[i].ny;
		if (mirrorRight) { px = 2.f * cx - px; nx = -nx; }
		const float hw = halfW * pts[i].scaleW;
		float ox = px + nx * hw, oy = py + ny * hw;
		float ix = px - nx * hw, iy = py - ny * hw;
		clampBox(ox, oy);
		clampBox(ix, iy);
		strip[i * 2 + 0] = {ox, oy, 0.008f, 1.f, col};
		strip[i * 2 + 1] = {ix, iy, 0.008f, 1.f, col};
	}
	batch.AddStrip(dev, strip, count * 2);
}

bool DrawNineSlicePanelD3D(float x, float y, float w, float h, float desiredR, float u, float border,
                           std::uint32_t fill, std::uint32_t accent, bool isForseti,
                           float alpha, bool withOuterCaps, bool emphasized,
                           bool withTopHighlight = false, float highlightInset = 0.f)
{
	LPDIRECT3DDEVICE9 dev = LT.GetDevice();
	if (!dev) return false;
	const CeCmdHudNineSlice ns = CeCmdHudComputeNineSlice(x, y, w, h, desiredR, u);
	if (!ns.valid) return false;

	CeCmdHudTriBatch batch;
	BeginUntextured2D();
	const std::uint32_t topFill = ScaleAlpha(LerpColor(fill, accent, 0.08f), alpha);
	const std::uint32_t botFill = ScaleAlpha(fill, alpha);
	const float fillInset = withOuterCaps ? (ns.glowPad + ns.strokeWidth * 0.45f) : 0.5f;
	EmitRoundedGlassFillD3D(dev, batch, ns, fillInset, topFill, botFill);

	if (withOuterCaps)
	{
		const std::uint32_t haloCol = ScaleAlpha(WithAlpha(isForseti ? 0x0028d4ffu : 0x00ff4414u, 0x4cu), alpha);
		const std::uint32_t bodyCol = ScaleAlpha(WithAlpha(isForseti ? 0x003ce4ffu : 0x00f04e1au, 0xf2u), alpha);
		const std::uint32_t coreCol = ScaleAlpha(WithAlpha(isForseti ? 0x00ccf8ffu : 0x00ffc464u, 0xffu), alpha);
		const float coreHalf = ns.strokeWidth * 0.5f;
		EmitBracketSideD3D(dev, batch, ns, false, coreHalf + ns.glowPad, haloCol);
		EmitBracketSideD3D(dev, batch, ns, true,  coreHalf + ns.glowPad, haloCol);
		EmitBracketSideD3D(dev, batch, ns, false, coreHalf, bodyCol);
		EmitBracketSideD3D(dev, batch, ns, true,  coreHalf, bodyCol);
		EmitBracketSideD3D(dev, batch, ns, false, coreHalf * 0.44f, coreCol);
		EmitBracketSideD3D(dev, batch, ns, true,  coreHalf * 0.44f, coreCol);
	}
	else
	{
		const std::uint32_t edgeAlpha = emphasized ? 0xe8u : 0xbcu;
		const float stroke = emphasized ? (border > 1.f ? border * 1.35f : 1.75f) : border;
		EmitRoundedBorderLoopD3D(dev, batch, ns, 0.5f, stroke, ScaleAlpha(WithAlpha(accent, edgeAlpha), alpha));
	}
	if (withTopHighlight)
	{
		const float spanW = w - 2.f * highlightInset;
		if (spanW > 4.f)
		{
			const float topH = emphasized ? (border > 1.f ? border * 1.4f : 2.f) : border;
			batch.AddRect(dev, x + highlightInset, y + border * 0.5f, spanW, topH, 0.008f,
			              WithAlpha(accent, emphasized ? 0xe8u : 0x8cu));
		}
	}
	batch.Flush(dev);
	EndUntextured2D();
	return true;
}
#endif

void DrawPanel(const CeCmdHudDrawOps &draw, float x, float y, float w, float h, float capMax,
               float u, float border, std::uint32_t fill, std::uint32_t capTint, bool nativeCyan,
               float alpha = 1.f, bool withOuterCaps = false, bool emphasized = false)
{
	if (w <= 0.f || h <= 0.f) return;
#ifdef CE_CMD_HUD_USE_D3D9
	if (draw.curvedPanel && DrawNineSlicePanelD3D(x, y, w, h, capMax, u, border, fill, capTint, nativeCyan, alpha, withOuterCaps, emphasized))
		return;
#else
	(void)capMax;
	(void)u;
	(void)nativeCyan;
	(void)alpha;
	(void)withOuterCaps;
	(void)emphasized;
#endif
	SafeBox(draw, x, y, w, h, fill);
	SafeOutline(draw, x, y, w, h, border, capTint);
}

void DrawInnerGlassPill(const CeCmdHudDrawOps &draw, float x, float y, float w, float h, float capMax,
                        float u, float border, std::uint32_t fill, std::uint32_t accent, float alpha = 1.f,
                        bool emphasized = false)
{
	if (w <= 0.f || h <= 0.f) return;
	const float pillR = capMax < std::floor(6.f * u) ? capMax : std::floor(6.f * u);
	const float inset = pillR > 3.f ? (pillR * 0.55f) : 2.f;
#ifdef CE_CMD_HUD_USE_D3D9
	if (draw.curvedPanel && DrawNineSlicePanelD3D(x, y, w, h, pillR, u, border, fill, accent, false, alpha, false, emphasized, true, inset))
		return;
#endif
	DrawPanel(draw, x, y, w, h, pillR, u, border, fill, accent, false, alpha, false, emphasized);
	const float spanW = w - 2.f * inset;
	if (spanW > 4.f)
	{
		const float topH = emphasized ? (border > 1.f ? border * 1.4f : 2.f) : border;
		SafeBox(draw, x + inset, y + border * 0.5f, spanW, topH,
		        WithAlpha(accent, emphasized ? 0xe8u : 0x8cu));
	}
}

// Draws text at a strictly FIXED font scale (stopping jarring per-card shrink-to-fit!)
// and applies clean "..." truncation only when the string exceeds maxWidth at that fixed scale.
void DrawFixedTextTruncated(const CeCmdHudDrawOps &draw, float x, float y, std::uint32_t colour,
                            const char *text, float maxWidth, float fixedScale)
{
	if (!text || !text[0] || maxWidth <= 2.f) return;
	if (draw.textWidth(text, fixedScale) <= maxWidth + 0.5f)
	{
		draw.text(x, y, colour, text, 0.f, false, fixedScale);
		return;
	}
	char buf[96];
	std::snprintf(buf, sizeof(buf), "%s", text);
	std::size_t len = std::strlen(buf);
	const float dotsW = draw.textWidth("...", fixedScale);
	if (dotsW >= maxWidth)
	{
		buf[1] = 0;
		draw.text(x, y, colour, buf, maxWidth, false, fixedScale);
		return;
	}
	while (len > 1)
	{
		--len;
		while (len > 1 && buf[len - 1] == ' ') --len;
		buf[len] = 0;
		char trial[100];
		std::snprintf(trial, sizeof(trial), "%s...", buf);
		if (draw.textWidth(trial, fixedScale) <= maxWidth + 0.5f)
		{
			draw.text(x, y, colour, trial, 0.f, false, fixedScale);
			return;
		}
	}
	draw.text(x, y, colour, buf, maxWidth, false, fixedScale);
}

std::uint32_t BadgeColor(CeCmdOrderTone tone, const CeCmdHudPalette &p)
{
	switch (tone)
	{
	case CE_TONE_ACTIVE: return p.textCyan;
	case CE_TONE_HOLD: return p.armourGood;
	case CE_TONE_ALERT: return p.textAlert;
	default: return p.textMuted;
	}
}

void DrawArmourBar(const CeCmdHudDrawOps &draw, float x, float y, float w, float h, float ratio,
                   float border, const CeCmdHudPalette &p)
{
	if (w <= 2.f || h <= 1.f) return;
	SafeBox(draw, x, y, w, h, WithAlpha(p.armourTrack, 0xaa));
	SafeBox(draw, x, y, w, border > 1.f ? 1.f : border, WithAlpha(p.wellBorder, 0x90));
	const float clamped = ratio < 0.f ? 0.f : (ratio > 1.f ? 1.f : ratio);
	const float pad = border > 1.f ? 1.f : border;
	const float innerW = (w - 2.f * pad) * clamped;
	const float innerH = h - 2.f * pad;
	if (innerW > 0.5f && innerH > 0.5f)
		SafeBox(draw, x + pad, y + pad, innerW, innerH, CeCmdHudArmourColor(clamped, p));
}
}

void CeCmdHudDrawBar(const CeCmdHudDrawOps &draw, const CeCmdHudLayout &layout,
                     const CeCmdPlacard *placards, int placardCount,
                     const CeCmdCardModel &card, const char *forcesSummary,
                     float mouseX, float mouseY, const std::uint64_t *buttonFlashNs,
                     std::uint64_t nowNs, int allegiance)
{
	if (!layout.valid || !draw.box || !draw.outline || !draw.text || !draw.textWidth || !draw.lineHeight)
		return;

	const CeCmdHudPalette p = CeCmdHudPaletteForAllegiance(allegiance);
	const bool isForseti = allegiance != 1;
	const float u = layout.scale;
	const float t = std::floor(u) > 1.f ? std::floor(u) : 1.f;
	const CeCmdHudHit hover = CeCmdHudHitTest(layout, mouseX, mouseY, placardCount);

	// ---- Outer true 9-slice mirrored holographic dock ----
	const CeCmdHudRect &bar = layout.barRect;
	const float outerRadius = std::floor(16.f * u);
	const std::uint32_t dockCapCol = isForseti ? p.cyanAccent : 0xfff04e1au;
	DrawPanel(draw, bar.x, bar.y, bar.w, bar.h, outerRadius, u, t, WithAlpha(p.barFill, 0xf0), dockCapCol, isForseti, 1.f, true, false);
	// Subtle translucent top/bottom rim connecting the left and right 9-slice corner arcs
	const float rimInset = outerRadius + std::floor(4.f * u);
	if (bar.w > 2.f * rimInset + 4.f)
	{
		SafeBox(draw, bar.x + rimInset, bar.y + t * 1.5f, bar.w - 2.f * rimInset, t, WithAlpha(p.barTopBevel, 0x6cu));
		SafeBox(draw, bar.x + rimInset, bar.y + bar.h - t * 2.5f, bar.w - 2.f * rimInset, t, WithAlpha(p.barBorder, 0x5cu));
	}

	// Soft glowing divider between Placard Tray and Command Card
	SafeBox(draw, layout.dividerRect.x, layout.dividerRect.y + std::floor(4.f * u),
	        layout.dividerRect.w, layout.dividerRect.h - std::floor(8.f * u), WithAlpha(p.cyanAccent, 0x84u));

	// ---- Left/Center Dock: Placard Tray Header ----
	const float hdrScale = u * 0.84f;
	const float smallScale = u * 0.78f;
	const float cardNameScale = u * 0.72f;	// uniform fixed card title scale (zero per-card shrink!)
	const float labelScale = u * 0.84f;
	const float hdrH = draw.lineHeight(hdrScale);
	const float hdrTextY = layout.trayHeaderRect.y + (layout.trayHeaderRect.h - hdrH) * 0.5f;

	draw.text(layout.trayHeaderRect.x + 2.f * u, hdrTextY, p.textCyan, "FORCES", 56.f * u, false, hdrScale);
	const float summaryX = layout.trayHeaderRect.x + draw.textWidth("FORCES  ", hdrScale) + 2.f * u;
	const float summaryRight = layout.showPaging ? (layout.prevPageRect.x - 6.f * u) : (layout.trayHeaderRect.x + layout.trayHeaderRect.w);
	if (forcesSummary && forcesSummary[0] && summaryRight > summaryX + 20.f * u)
		DrawFixedTextTruncated(draw, summaryX, hdrTextY, p.textMuted, forcesSummary, summaryRight - summaryX, smallScale);

	if (layout.showPaging)
	{
		const bool prevHov = hover.kind == CE_HUD_HIT_PREV_PAGE;
		const bool nextHov = hover.kind == CE_HUD_HIT_NEXT_PAGE;
		const bool canPrev = layout.pageIndex > 0;
		const bool canNext = layout.pageIndex + 1 < layout.totalPages;
		const float navCap = std::floor(5.f * u);
		DrawInnerGlassPill(draw, layout.prevPageRect.x, layout.prevPageRect.y, layout.prevPageRect.w, layout.prevPageRect.h, navCap, u, t,
		                   canPrev ? (prevHov ? p.buttonHover : p.buttonFill) : p.buttonDisabled,
		                   canPrev ? (prevHov ? p.goldAccent : p.buttonBorder) : p.wellBorder, canPrev ? 0.95f : 0.65f, prevHov && canPrev);
		draw.text(layout.prevPageRect.x + layout.prevPageRect.w * 0.5f, hdrTextY,
		          canPrev ? (prevHov ? p.textGold : p.textBright) : p.textDisabled, "<", layout.prevPageRect.w - 2.f * u, true, hdrScale);

		char pageBuf[16];
		std::snprintf(pageBuf, sizeof(pageBuf), "%d/%d", layout.pageIndex + 1, layout.totalPages);
		const float midX = (layout.prevPageRect.x + layout.prevPageRect.w + layout.nextPageRect.x) * 0.5f;
		draw.text(midX, hdrTextY, p.textMuted, pageBuf, layout.nextPageRect.x - (layout.prevPageRect.x + layout.prevPageRect.w), true, smallScale);

		DrawInnerGlassPill(draw, layout.nextPageRect.x, layout.nextPageRect.y, layout.nextPageRect.w, layout.nextPageRect.h, navCap, u, t,
		                   canNext ? (nextHov ? p.buttonHover : p.buttonFill) : p.buttonDisabled,
		                   canNext ? (nextHov ? p.goldAccent : p.buttonBorder) : p.wellBorder, canNext ? 0.95f : 0.65f, nextHov && canNext);
		draw.text(layout.nextPageRect.x + layout.nextPageRect.w * 0.5f, hdrTextY,
		          canNext ? (nextHov ? p.textGold : p.textBright) : p.textDisabled, ">", layout.nextPageRect.w - 2.f * u, true, hdrScale);
	}

	// ---- Unit, Squad & Structure Placards ----
	if (layout.visibleCount <= 0)
	{
		const CeCmdHudRect &slot0 = layout.placardRects[0];
		const float wellW = layout.trayRect.w;
		DrawInnerGlassPill(draw, slot0.x, slot0.y, wellW, slot0.h, std::floor(6.f * u), u, t,
		                   WithAlpha(p.wellFill, 0xb0), p.wellBorder, 0.92f, false);
		draw.text(slot0.x + wellW * 0.5f, slot0.y + (slot0.h - draw.lineHeight(labelScale)) * 0.5f,
		          p.textMuted, "No friendly forces in sector", wellW - 16.f * u, true, labelScale);
	}
	else
	{
		for (int s = 0; s < layout.visibleCount; ++s)
		{
			const int idx = layout.visibleStart + s;
			if (idx < 0 || idx >= placardCount) break;
			const CeCmdPlacard &pl = placards[idx];
			const CeCmdHudRect &r = layout.placardRects[s];
			const bool hovered = hover.kind == CE_HUD_HIT_PLACARD && hover.index == idx;

			const std::uint32_t fill = pl.selected ? WithAlpha(p.placardSelected, 0xf0) :
			                           (hovered ? WithAlpha(p.placardHover, 0xe0) : WithAlpha(p.placardFill, 0xc8));
			const std::uint32_t stripe = pl.selected ? p.placardSelectedBorder :
			                             (hovered ? p.cyanAccent :
			                              (pl.kind == CE_PLACARD_STRUCTURE ? p.placardStructStripe :
			                               (pl.commandable ? p.buttonBorder : p.placardDimStripe)));

			const float plCap = std::floor(6.f * u);
			DrawInnerGlassPill(draw, r.x, r.y, r.w, r.h, plCap, u, t, fill, stripe, 1.f, pl.selected || hovered);

			const float stripeH = (2.f * u) > 2.f ? std::floor(2.f * u) : 2.f;
			const float padX = std::floor(6.f * u);
			const float nameY = r.y + stripeH + std::floor(3.f * u);
			const float lineH = draw.lineHeight(cardNameScale);

			// Top-right badge: control group pill + count/tag
			float rightEdge = r.x + r.w - padX;
			if (pl.groupTag >= 0 && pl.groupTag <= 9)
			{
				char grp[4];
				std::snprintf(grp, sizeof(grp), "%d", pl.groupTag);
				const float pillW = std::floor(12.f * u);
				const float pillH = std::floor(11.f * u);
				const float pillX = rightEdge - pillW;
				DrawInnerGlassPill(draw, pillX, nameY - 1.f, pillW, pillH, std::floor(3.f * u), u, t,
				                   0xd82b2208, p.goldAccent, 1.f, true);
				draw.text(pillX + pillW * 0.5f, nameY, p.textGold, grp, pillW - 2.f, true, u * 0.72f);
				rightEdge = pillX - std::floor(3.f * u);
			}

			char badge[12] = "";
			if (pl.kind == CE_PLACARD_STRUCTURE) std::snprintf(badge, sizeof(badge), "FAC");
			else if (pl.kind == CE_PLACARD_UNIT) std::snprintf(badge, sizeof(badge), "UNIT");
			else if (pl.count > 0) std::snprintf(badge, sizeof(badge), "x%d", pl.count);
			if (badge[0])
			{
				const float badgeScale = u * 0.68f;
				const float bw = draw.textWidth(badge, badgeScale);
				const float bx = rightEdge - bw;
				if (bx > r.x + padX + 24.f * u)
				{
					draw.text(bx, nameY + std::floor(0.5f * u), pl.kind == CE_PLACARD_STRUCTURE ? p.textGold : p.textCyan,
					          badge, 0.f, false, badgeScale);
					rightEdge = bx - std::floor(4.f * u);
				}
			}

			const float nameRoom = rightEdge - (r.x + padX);
			if (nameRoom > 12.f * u)
			{
				const std::uint32_t nameCol = pl.selected ? p.textBright :
					(pl.commandable || pl.kind == CE_PLACARD_STRUCTURE ? p.textBright : p.textMuted);
				DrawFixedTextTruncated(draw, r.x + padX, nameY, nameCol,
				                       pl.shortName[0] ? pl.shortName : pl.name, nameRoom, cardNameScale);
			}

			// Row 2: Live Armour Bar
			const float barTop = nameY + lineH + std::floor(3.f * u);
			const float armourH = (5.f * u) > 4.f ? std::floor(5.f * u) : 4.f;
			DrawArmourBar(draw, r.x + padX, barTop, r.w - 2.f * padX, armourH, pl.armourRatio, t, p);

			// Row 3: Order / Build Status Badge (with optional build mini-progress strip)
			const float statusY = barTop + armourH + std::floor(3.f * u);
			const float statusRoom = r.w - 2.f * padX;
			if (pl.isBuilding && pl.buildPercent > 0)
			{
				const float progH = (3.f * u) > 2.f ? std::floor(3.f * u) : 2.f;
				const float progY = r.y + r.h - progH - std::floor(2.f * u);
				SafeBox(draw, r.x + padX, progY, statusRoom, progH, p.armourTrack);
				const float fillW = statusRoom * (float(pl.buildPercent) / 100.f);
				if (fillW > 0.5f) SafeBox(draw, r.x + padX, progY, fillW, progH, p.cyanAccent);
			}
			const char *orderText = pl.orderBadge[0] ? pl.orderBadge : (pl.kind == CE_PLACARD_STRUCTURE ? "IDLE" : "AI");
			DrawFixedTextTruncated(draw, r.x + padX, statusY, BadgeColor(pl.orderTone, p), orderText, statusRoom, u * 0.72f);
		}
	}

	// ---- Right Dock: Sleek Rounded Glass Command Card ----
	const CeCmdHudRect &cHdr = layout.cardHeaderRect;
	const std::uint32_t hdrCap = card.hasSelection ? (card.isStructure ? p.placardStructStripe : p.buttonBorder) : p.wellBorder;
	DrawInnerGlassPill(draw, cHdr.x, cHdr.y, cHdr.w, cHdr.h, std::floor(6.f * u), u, t,
	                   WithAlpha(p.wellFill, 0xc8), hdrCap, 1.f, card.hasSelection);

	const float cPad = std::floor(7.f * u);
	const float cardTitleScale = u * 0.72f;
	const float cardSubScale = u * 0.62f;
	const float titleLineH = draw.lineHeight(cardTitleScale);
	const float subLineH = draw.lineHeight(cardSubScale);
	const float titleY = cHdr.y + std::floor(2.f * u);
	float titleRoom = cHdr.w - 2.f * cPad;
	if (card.hasSelection && card.hasArmour)
	{
		char hpBuf[16];
		const float safeRatio = std::isfinite(card.armourRatio) ? (card.armourRatio < 0.f ? 0.f : (card.armourRatio > 1.f ? 1.f : card.armourRatio)) : 0.f;
		const int hpPct = int(safeRatio * 100.f + 0.5f);
		std::snprintf(hpBuf, sizeof(hpBuf), "%d%%", hpPct);
		const float hpW = draw.textWidth(hpBuf, u * 0.68f);
		const float miniW = std::floor(38.f * u);
		const float miniH = std::floor(5.f * u);
		const float hpX = cHdr.x + cHdr.w - cPad - hpW;
		const float miniX = hpX - miniW - std::floor(4.f * u);
		draw.text(hpX, titleY, CeCmdHudArmourColor(card.armourRatio, p), hpBuf, 0.f, false, u * 0.68f);
		DrawArmourBar(draw, miniX, titleY + std::floor((titleLineH - miniH) * 0.5f), miniW, miniH, card.armourRatio, t, p);
		titleRoom = miniX - (cHdr.x + cPad) - std::floor(4.f * u);
	}
	if (titleRoom > 16.f * u)
		DrawFixedTextTruncated(draw, cHdr.x + cPad, titleY,
		                       card.hasSelection ? (card.isStructure ? p.textGold : p.textBright) : p.textMuted,
		                       card.title[0] ? card.title : "Tactical Command", titleRoom, cardTitleScale);

	// Guarantee strict non-overlapping separation between card title and subtitle/order line.
	const float subMinY = titleY + std::floor(titleLineH) + std::floor(1.5f * u);
	float subY = cHdr.y + cHdr.h - subLineH - std::floor(2.f * u);
	if (subY < subMinY) subY = subMinY;
	if (card.showProgress)
	{
		const CeCmdHudRect &pr = layout.cardProgressRect;
		const float barX = pr.x + cPad;
		const float barW = pr.w - 2.f * cPad;
		const float progBarH = (2.f * u) > 2.f ? std::floor(2.f * u) : 2.f;
		const float progBarY = cHdr.y + cHdr.h - progBarH - std::floor(2.f * u);
		if (card.progressLabel[0])
			DrawFixedTextTruncated(draw, barX, subY - std::floor(1.5f * u), p.textCyan, card.progressLabel, barW, u * 0.60f);
		SafeBox(draw, barX, progBarY, barW, progBarH, p.armourTrack);
		const float pct = card.progressPercent < 0 ? 0.f : (card.progressPercent > 100 ? 100.f : float(card.progressPercent));
		const float fillW = barW * (pct / 100.f);
		if (fillW > 0.5f) SafeBox(draw, barX, progBarY, fillW, progBarH, p.cyanAccent);
	}
	else if (card.subtitle[0])
	{
		// High-contrast secondary text: warm pale cream on Muspell, crisp pale cyan on Forseti.
		DrawFixedTextTruncated(draw, cHdr.x + cPad, subY, card.hasSelection ? p.textCyan : p.textMuted,
		                       card.subtitle, cHdr.w - 2.f * cPad, cardSubScale);
	}

	// 2x3 Crisp Clickable Command Button Grid (clearly bordered glass buttons + legible >=10-11px hotkeys at 720p)
	const float btnCap = std::floor(5.f * u);
	for (int i = 0; i < kCeHudCardButtons; ++i)
	{
		const CeCmdCardButton &btn = card.buttons[i];
		const CeCmdHudRect &br = layout.buttonRects[i];
		const bool hovered = hover.kind == CE_HUD_HIT_CARD_BUTTON && hover.index == i;
		const bool flashing = buttonFlashNs && nowNs >= buttonFlashNs[i] && (nowNs - buttonFlashNs[i]) < 220000000ull;
		const bool lit = btn.enabled && (btn.active || flashing);
		const bool inProgress = !btn.enabled && btn.active;

		if (btn.action == CE_CARD_NONE || !btn.label[0])
		{
			DrawInnerGlassPill(draw, br.x, br.y, br.w, br.h, btnCap, u, t, WithAlpha(p.wellFill, 0x84), p.wellBorder, 0.72f, false);
			continue;
		}

		const std::uint32_t bg = inProgress ? p.buttonArmed : (!btn.enabled ? WithAlpha(p.buttonDisabled, 0xa8) : (lit ? p.buttonArmed : (hovered ? p.buttonHover : WithAlpha(p.buttonFill, 0xd8))));
		const std::uint32_t edge = inProgress ? p.cyanAccent : (!btn.enabled ? p.wellBorder : (lit ? p.goldAccent : (hovered ? p.cyanAccent : p.buttonBorder)));
		DrawInnerGlassPill(draw, br.x, br.y, br.w, br.h, btnCap, u, t, bg, edge, (btn.enabled || inProgress) ? 1.f : 0.80f, lit || hovered || inProgress);

		const float keyScale = u * 0.70f;	// >= 11px legible hotkeys at 1280x720
		const float lblScale = u * 0.72f;
		const float btnPad = std::floor(5.f * u);
		const float keyH = draw.lineHeight(keyScale);
		const float lblH = draw.lineHeight(lblScale);
		float hotkeyReserve = 0.f;
		if (btn.hotkey[0])
		{
			const float kw = draw.textWidth(btn.hotkey, keyScale);
			const std::uint32_t hkCol = inProgress ? p.textCyan : (btn.enabled ? (lit || hovered ? p.textGold : p.textCyan) : p.textDisabled);
			const float keyY = br.y + std::floor((br.h - keyH) * 0.5f);
			draw.text(br.x + br.w - btnPad - kw, keyY,
			          hkCol, btn.hotkey, 0.f, false, keyScale);
			hotkeyReserve = kw + std::floor(4.f * u);
		}
		const float lblY = br.y + std::floor((br.h - lblH) * 0.5f);
		const float lblRoom = (br.w - 2.f * btnPad - hotkeyReserve) > 12.f * u ? (br.w - 2.f * btnPad - hotkeyReserve) : (br.w - 2.f * btnPad);
		const std::uint32_t lblCol = inProgress ? p.textCyan : (!btn.enabled ? p.textDisabled : (lit ? p.textGold : p.textBright));
		DrawFixedTextTruncated(draw, br.x + btnPad, lblY, lblCol, btn.label, lblRoom, lblScale);
	}
}
