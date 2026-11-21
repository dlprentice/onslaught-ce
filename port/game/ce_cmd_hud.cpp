// SPDX-License-Identifier: GPL-3.0-only
// Commander Mode RTS HUD renderer: Forseti translucent dark navy/slate metallic
// command bar, unit/squad/structure placards with live armour bars and order badges,
// and classic right-docked C&C command card.

#include "ce_cmd_hud.h"
#include <cmath>
#include <cstdio>

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
	SafeBox(draw, x, y, w, h, p.armourTrack);
	SafeOutline(draw, x, y, w, h, border, p.wellBorder);
	const float clamped = ratio < 0.f ? 0.f : (ratio > 1.f ? 1.f : ratio);
	const float innerW = (w - 2.f * border) * clamped;
	const float innerH = h - 2.f * border;
	if (innerW > 0.5f && innerH > 0.5f)
		SafeBox(draw, x + border, y + border, innerW, innerH, CeCmdHudArmourColor(clamped, p));
}
}

void CeCmdHudDrawBar(const CeCmdHudDrawOps &draw, const CeCmdHudLayout &layout,
                     const CeCmdPlacard *placards, int placardCount,
                     const CeCmdCardModel &card, const char *forcesSummary,
                     float mouseX, float mouseY, const std::uint64_t *buttonFlashNs,
                     std::uint64_t nowNs)
{
	if (!layout.valid || !draw.box || !draw.outline || !draw.text || !draw.textWidth || !draw.lineHeight)
		return;

	const CeCmdHudPalette p = CeCmdHudDefaultPalette();
	const float u = layout.scale;
	const float t = std::floor(u) > 1.f ? std::floor(u) : 1.f;
	const CeCmdHudHit hover = CeCmdHudHitTest(layout, mouseX, mouseY);

	// ---- Outer metallic Forseti dock ----
	const CeCmdHudRect &bar = layout.barRect;
	SafeBox(draw, bar.x, bar.y, bar.w, bar.h, p.barFill);
	SafeOutline(draw, bar.x, bar.y, bar.w, bar.h, t, p.barBorder);
	// Crisp BEA cyan top bevel and vertical Forseti end-cap bars
	SafeBox(draw, bar.x + t, bar.y + t, bar.w - 2.f * t, t, p.barTopBevel);
	const float capW = (2.f * u) > 2.f ? std::floor(2.f * u) : 2.f;
	const float capInset = std::floor(5.f * u);
	SafeBox(draw, bar.x + capW, bar.y + capInset, capW, bar.h - 2.f * capInset, p.cyanAccent);
	SafeBox(draw, bar.x + bar.w - 2.f * capW, bar.y + capInset, capW, bar.h - 2.f * capInset, p.cyanAccent);

	// Divider between Placard Tray and Command Card
	SafeBox(draw, layout.dividerRect.x, layout.dividerRect.y, layout.dividerRect.w, layout.dividerRect.h, p.cyanDim);

	// ---- Left/Center Dock: Placard Tray Header ----
	const float hdrScale = u * 0.85f;
	const float smallScale = u * 0.78f;
	const float labelScale = u * 0.86f;
	const float hdrH = draw.lineHeight(hdrScale);
	const float hdrTextY = layout.trayHeaderRect.y + (layout.trayHeaderRect.h - hdrH) * 0.5f;

	draw.text(layout.trayHeaderRect.x + 2.f * u, hdrTextY, p.textCyan, "FORCES", 56.f * u, false, hdrScale);
	const float summaryX = layout.trayHeaderRect.x + draw.textWidth("FORCES  ", hdrScale) + 2.f * u;
	const float summaryRight = layout.showPaging ? (layout.prevPageRect.x - 6.f * u) : (layout.trayHeaderRect.x + layout.trayHeaderRect.w);
	if (forcesSummary && forcesSummary[0] && summaryRight > summaryX + 20.f * u)
		draw.text(summaryX, hdrTextY, p.textMuted, forcesSummary, summaryRight - summaryX, false, smallScale);

	if (layout.showPaging)
	{
		const bool prevHov = hover.kind == CE_HUD_HIT_PREV_PAGE;
		const bool nextHov = hover.kind == CE_HUD_HIT_NEXT_PAGE;
		const bool canPrev = layout.pageIndex > 0;
		const bool canNext = layout.pageIndex + 1 < layout.totalPages;
		SafeBox(draw, layout.prevPageRect.x, layout.prevPageRect.y, layout.prevPageRect.w, layout.prevPageRect.h,
		        canPrev ? (prevHov ? p.buttonHover : p.buttonFill) : p.buttonDisabled);
		SafeOutline(draw, layout.prevPageRect.x, layout.prevPageRect.y, layout.prevPageRect.w, layout.prevPageRect.h,
		            t, canPrev ? (prevHov ? p.goldAccent : p.buttonBorder) : p.wellBorder);
		draw.text(layout.prevPageRect.x + layout.prevPageRect.w * 0.5f, hdrTextY,
		          canPrev ? (prevHov ? p.textGold : p.textBright) : p.textDisabled, "<", layout.prevPageRect.w - 2.f * u, true, hdrScale);

		char pageBuf[16];
		std::snprintf(pageBuf, sizeof(pageBuf), "%d/%d", layout.pageIndex + 1, layout.totalPages);
		const float midX = (layout.prevPageRect.x + layout.prevPageRect.w + layout.nextPageRect.x) * 0.5f;
		draw.text(midX, hdrTextY, p.textMuted, pageBuf, layout.nextPageRect.x - (layout.prevPageRect.x + layout.prevPageRect.w), true, smallScale);

		SafeBox(draw, layout.nextPageRect.x, layout.nextPageRect.y, layout.nextPageRect.w, layout.nextPageRect.h,
		        canNext ? (nextHov ? p.buttonHover : p.buttonFill) : p.buttonDisabled);
		SafeOutline(draw, layout.nextPageRect.x, layout.nextPageRect.y, layout.nextPageRect.w, layout.nextPageRect.h,
		            t, canNext ? (nextHov ? p.goldAccent : p.buttonBorder) : p.wellBorder);
		draw.text(layout.nextPageRect.x + layout.nextPageRect.w * 0.5f, hdrTextY,
		          canNext ? (nextHov ? p.textGold : p.textBright) : p.textDisabled, ">", layout.nextPageRect.w - 2.f * u, true, hdrScale);
	}

	// ---- Unit, Squad & Structure Placards ----
	if (layout.visibleCount <= 0)
	{
		const CeCmdHudRect &slot0 = layout.placardRects[0];
		const float wellW = layout.trayRect.w;
		SafeBox(draw, slot0.x, slot0.y, wellW, slot0.h, p.wellFill);
		SafeOutline(draw, slot0.x, slot0.y, wellW, slot0.h, t, p.wellBorder);
		draw.text(slot0.x + wellW * 0.5f, slot0.y + (slot0.h - draw.lineHeight(labelScale)) * 0.5f,
		          p.textMuted, "No friendly forces in sector", wellW - 12.f * u, true, labelScale);
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

			const std::uint32_t fill = pl.selected ? p.placardSelected : (hovered ? p.placardHover : p.placardFill);
			const std::uint32_t border = pl.selected ? p.goldAccent : (hovered ? p.cyanAccent : p.wellBorder);
			const std::uint32_t stripe = pl.selected ? p.goldAccent :
			                             (pl.kind == CE_PLACARD_STRUCTURE ? p.placardStructStripe :
			                              (pl.commandable ? p.cyanAccent : p.placardDimStripe));

			SafeBox(draw, r.x, r.y, r.w, r.h, fill);
			SafeOutline(draw, r.x, r.y, r.w, r.h, pl.selected ? (t * 1.5f) : t, border);
			const float stripeH = (3.f * u) > 2.f ? std::floor(3.f * u) : 2.f;
			SafeBox(draw, r.x + t, r.y + t, r.w - 2.f * t, stripeH, stripe);

			const float padX = std::floor(5.f * u);
			const float nameY = r.y + stripeH + std::floor(3.f * u);
			const float lineH = draw.lineHeight(smallScale);

			// Top-right badge: control group pill + count/tag
			float rightEdge = r.x + r.w - padX;
			if (pl.groupTag >= 0 && pl.groupTag <= 9)
			{
				char grp[4];
				std::snprintf(grp, sizeof(grp), "%d", pl.groupTag);
				const float pillW = std::floor(13.f * u);
				const float pillH = std::floor(12.f * u);
				const float pillX = rightEdge - pillW;
				SafeBox(draw, pillX, nameY - 1.f, pillW, pillH, 0xd82b2208);
				SafeOutline(draw, pillX, nameY - 1.f, pillW, pillH, t, p.goldAccent);
				draw.text(pillX + pillW * 0.5f, nameY, p.textGold, grp, pillW - 2.f, true, u * 0.72f);
				rightEdge = pillX - std::floor(3.f * u);
			}

			char badge[12] = "";
			if (pl.kind == CE_PLACARD_STRUCTURE) std::snprintf(badge, sizeof(badge), "FAC");
			else if (pl.kind == CE_PLACARD_UNIT) std::snprintf(badge, sizeof(badge), "UNIT");
			else if (pl.count > 0) std::snprintf(badge, sizeof(badge), "x%d", pl.count);
			if (badge[0])
			{
				const float badgeScale = u * 0.70f;
				const float bw = draw.textWidth(badge, badgeScale);
				const float bx = rightEdge - bw;
				if (bx > r.x + padX + 24.f * u)
				{
					draw.text(bx, nameY, pl.kind == CE_PLACARD_STRUCTURE ? p.textGold : p.textCyan,
					          badge, bw + 2.f, false, badgeScale);
					rightEdge = bx - std::floor(6.f * u);
				}
			}

			const float nameRoom = rightEdge - (r.x + padX);
			if (nameRoom > 12.f * u)
				draw.text(r.x + padX, nameY, pl.selected ? p.textBright : (pl.commandable || pl.kind == CE_PLACARD_STRUCTURE ? p.textBright : p.textMuted),
				          pl.shortName[0] ? pl.shortName : pl.name, nameRoom, false, smallScale);

			// Row 2: Live Armour Bar
			const float barTop = nameY + lineH + std::floor(4.f * u);
			const float armourH = (6.f * u) > 4.f ? std::floor(6.f * u) : 4.f;
			DrawArmourBar(draw, r.x + padX, barTop, r.w - 2.f * padX, armourH, pl.armourRatio, t, p);

			// Row 3: Order / Build Status Badge (with optional build mini-progress strip)
			const float statusY = barTop + armourH + std::floor(4.f * u);
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
			draw.text(r.x + padX, statusY, BadgeColor(pl.orderTone, p), orderText, statusRoom, false, u * 0.74f);
		}
	}

	// ---- Right Dock: Classic C&C RTS Command Card ----
	const CeCmdHudRect &cHdr = layout.cardHeaderRect;
	SafeBox(draw, cHdr.x, cHdr.y, cHdr.w, cHdr.h, p.wellFill);
	SafeOutline(draw, cHdr.x, cHdr.y, cHdr.w, cHdr.h, t, card.hasSelection ? (card.isStructure ? p.placardStructStripe : p.cyanDim) : p.wellBorder);

	const float cPad = std::floor(5.f * u);
	const float cardTitleScale = u * 0.74f;
	const float cardSubScale = u * 0.60f;
	const float titleY = cHdr.y + std::floor(1.5f * u);
	float titleRoom = cHdr.w - 2.f * cPad;
	if (card.hasSelection && card.hasArmour)
	{
		char hpBuf[16];
		const int hpPct = int(card.armourRatio * 100.f + 0.5f);
		std::snprintf(hpBuf, sizeof(hpBuf), "%d%%", hpPct);
		const float hpW = draw.textWidth(hpBuf, u * 0.68f);
		const float miniW = std::floor(40.f * u);
		const float miniH = std::floor(5.f * u);
		const float hpX = cHdr.x + cHdr.w - cPad - hpW;
		const float miniX = hpX - miniW - std::floor(4.f * u);
		draw.text(hpX, titleY, CeCmdHudArmourColor(card.armourRatio, p), hpBuf, hpW + 2.f, false, u * 0.68f);
		DrawArmourBar(draw, miniX, titleY + std::floor(2.5f * u), miniW, miniH, card.armourRatio, t, p);
		titleRoom = miniX - (cHdr.x + cPad) - std::floor(4.f * u);
	}
	if (titleRoom > 16.f * u)
		draw.text(cHdr.x + cPad, titleY, card.hasSelection ? (card.isStructure ? p.textGold : p.textBright) : p.textMuted,
		          card.title[0] ? card.title : "Tactical Command", titleRoom, false, cardTitleScale);

	const float subY = titleY + draw.lineHeight(cardTitleScale) + std::floor(0.5f * u);
	if (card.showProgress)
	{
		const CeCmdHudRect &pr = layout.cardProgressRect;
		const float barX = pr.x + cPad;
		const float barW = pr.w - 2.f * cPad;
		const float progBarH = (2.f * u) > 2.f ? std::floor(2.f * u) : 2.f;
		const float progBarY = cHdr.y + cHdr.h - progBarH - std::floor(1.5f * u);
		if (card.progressLabel[0])
			draw.text(barX, subY - std::floor(1.f * u), p.textCyan, card.progressLabel, barW, false, u * 0.56f);
		SafeBox(draw, barX, progBarY, barW, progBarH, p.armourTrack);
		const float pct = card.progressPercent < 0 ? 0.f : (card.progressPercent > 100 ? 100.f : float(card.progressPercent));
		const float fillW = barW * (pct / 100.f);
		if (fillW > 0.5f) SafeBox(draw, barX, progBarY, fillW, progBarH, p.cyanAccent);
	}
	else if (card.subtitle[0])
	{
		draw.text(cHdr.x + cPad, subY, card.hasSelection ? p.textCyan : p.textMuted,
		          card.subtitle, cHdr.w - 2.f * cPad, false, cardSubScale);
	}

	// 2x3 Metallic Command Button Grid
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
			SafeBox(draw, br.x, br.y, br.w, br.h, p.wellFill);
			SafeOutline(draw, br.x, br.y, br.w, br.h, t, p.wellBorder);
			continue;
		}

		const std::uint32_t bg = inProgress ? p.buttonArmed : (!btn.enabled ? p.buttonDisabled : (lit ? p.buttonArmed : (hovered ? p.buttonHover : p.buttonFill)));
		const std::uint32_t edge = inProgress ? p.cyanAccent : (!btn.enabled ? p.wellBorder : (lit ? p.goldAccent : (hovered ? p.cyanAccent : p.buttonBorder)));
		SafeBox(draw, br.x, br.y, br.w, br.h, bg);
		SafeOutline(draw, br.x, br.y, br.w, br.h, (lit || inProgress) ? (t * 1.5f) : t, edge);
		if (btn.enabled || inProgress)
			SafeBox(draw, br.x + t, br.y + t, br.w - 2.f * t, t, inProgress ? p.cyanAccent : (lit ? p.goldAccent : p.cyanDim));

		const float keyScale = u * 0.60f;
		const float lblScale = u * 0.72f;
		float hotkeyReserve = 0.f;
		if (btn.hotkey[0])
		{
			const float kw = draw.textWidth(btn.hotkey, keyScale);
			const std::uint32_t hkCol = inProgress ? p.textCyan : (btn.enabled ? (lit || hovered ? p.textGold : p.textCyan) : p.textDisabled);
			draw.text(br.x + br.w - std::floor(4.f * u) - kw, br.y + std::floor(1.5f * u),
			          hkCol, btn.hotkey, br.w - 6.f * u, false, keyScale);
			// Reserve right-side hotkey room on compact viewports (e.g. 640x480) so label and hotkey never crowd.
			const float keyH = draw.lineHeight(keyScale);
			const float lblHCheck = draw.lineHeight(lblScale);
			if (br.h < keyH + lblHCheck + std::floor(5.f * u))
				hotkeyReserve = kw + std::floor(4.f * u);
		}
		const float lblH = draw.lineHeight(lblScale);
		const float lblY = br.y + br.h - lblH - std::floor(2.f * u);
		const float lblRoom = (br.w - std::floor(9.f * u) - hotkeyReserve) > 12.f * u ? (br.w - std::floor(9.f * u) - hotkeyReserve) : (br.w - std::floor(8.f * u));
		const std::uint32_t lblCol = inProgress ? p.textCyan : (!btn.enabled ? p.textDisabled : (lit ? p.textGold : p.textBright));
		draw.text(br.x + std::floor(4.f * u), lblY, lblCol, btn.label, lblRoom, false, lblScale);
	}
}
