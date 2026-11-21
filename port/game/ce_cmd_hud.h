// SPDX-License-Identifier: GPL-3.0-only
// Commander Mode RTS HUD: pure geometry, Forseti palette, placard/card models,
// hit-testing, bitmap-font-safe short names, and double-click detection.
//
// Zero game types, zero heap allocations, zero RNG: host unit tests compile
// this header directly alongside ce_cmdmap.cpp and ce_cmd_hud.cpp.
#ifndef PORT_CE_CMD_HUD_H
#define PORT_CE_CMD_HUD_H

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

static constexpr int kCeHudMaxPlacards = 32;
static constexpr int kCeHudMaxVisiblePlacards = 6;
static constexpr int kCeHudCardButtons = 6;
static constexpr int kCeHudNameLen = 32;
static constexpr int kCeHudShortLen = 20;
static constexpr int kCeHudBadgeLen = 20;

enum CeCmdPlacardKind
{
	CE_PLACARD_SQUAD = 0,
	CE_PLACARD_UNIT,
	CE_PLACARD_STRUCTURE
};

enum CeCmdOrderTone
{
	CE_TONE_NEUTRAL = 0,	// AI / Idle
	CE_TONE_ACTIVE,			// Move / Advance / Follow / Build
	CE_TONE_HOLD,			// Hold / Arrived / Ready
	CE_TONE_ALERT			// Fighting / Stuck / Blocked
};

enum CeCmdCardAction
{
	CE_CARD_NONE = 0,
	CE_CARD_MOVE,
	CE_CARD_ATTACK,
	CE_CARD_HOLD,
	CE_CARD_FOLLOW_BE,
	CE_CARD_TAKE_CONTROL,
	CE_CARD_RETURN_AI,
	CE_CARD_BUILD_0,
	CE_CARD_BUILD_1,
	CE_CARD_BUILD_2,
	CE_CARD_BUILD_3,
	CE_CARD_CANCEL_BUILD,
	CE_CARD_SELECT_DELIVERED,
	CE_CARD_TAKE_DELIVERED
};

struct CeCmdHudRect
{
	float x, y, w, h;
};

struct CeCmdPlacard
{
	CeCmdPlacardKind kind;
	int id;							// squad ordinal, native unit index, or structure thingNumber
	char name[kCeHudNameLen];
	char shortName[kCeHudShortLen];
	int count;						// members for squads, 1 for units, 0 for structures
	float armourRatio;				// 0.0..1.0
	char orderBadge[kCeHudBadgeLen];
	CeCmdOrderTone orderTone;
	int groupTag;					// 0..9 when in a control group, else -1
	bool selected;
	bool commandable;
	bool isBuilding;
	int buildPercent;				// 0..100 when isBuilding
	float worldX, worldY;
};

struct CeCmdCardButton
{
	CeCmdCardAction action;
	char label[20];
	char hotkey[12];
	bool enabled;
	bool active;					// armed or currently highlighted
};

struct CeCmdCardModel
{
	bool hasSelection;
	bool isStructure;
	bool isMulti;
	char title[64];
	char subtitle[64];
	float armourRatio;				// 0..1 when hasArmour
	bool hasArmour;
	bool showProgress;
	int progressPercent;			// 0..100
	char progressLabel[40];
	CeCmdCardButton buttons[kCeHudCardButtons];
};

struct CeCmdHudPalette
{
	std::uint32_t barFill;
	std::uint32_t barBorder;
	std::uint32_t barTopBevel;
	std::uint32_t cyanAccent;
	std::uint32_t cyanDim;
	std::uint32_t goldAccent;
	std::uint32_t wellFill;
	std::uint32_t wellBorder;
	std::uint32_t placardFill;
	std::uint32_t placardHover;
	std::uint32_t placardSelected;
	std::uint32_t placardStructStripe;
	std::uint32_t placardDimStripe;
	std::uint32_t buttonFill;
	std::uint32_t buttonHover;
	std::uint32_t buttonArmed;
	std::uint32_t buttonDisabled;
	std::uint32_t buttonBorder;
	std::uint32_t armourTrack;
	std::uint32_t armourGood;
	std::uint32_t armourWarn;
	std::uint32_t armourCrit;
	std::uint32_t textBright;
	std::uint32_t textMuted;
	std::uint32_t textDisabled;
	std::uint32_t textCyan;
	std::uint32_t textGold;
	std::uint32_t textAlert;
};

inline CeCmdHudPalette CeCmdHudDefaultPalette()
{
	CeCmdHudPalette p = {};
	p.barFill = 0xdc06101c;
	p.barBorder = 0xff2b5e7a;
	p.barTopBevel = 0xb058d8f8;
	p.cyanAccent = 0xff38c8e8;
	p.cyanDim = 0xff23647c;
	p.goldAccent = 0xfff2c94c;
	p.wellFill = 0xcc091524;
	p.wellBorder = 0xff1f445c;
	p.placardFill = 0xd60e1e30;
	p.placardHover = 0xe216324d;
	p.placardSelected = 0xeb183856;
	p.placardStructStripe = 0xffe8b048;
	p.placardDimStripe = 0xff5a6f80;
	p.buttonFill = 0xde102438;
	p.buttonHover = 0xeb1a3c5c;
	p.buttonArmed = 0xeb2a4b68;
	p.buttonDisabled = 0xb80a1420;
	p.buttonBorder = 0xff347090;
	p.armourTrack = 0xd0040910;
	p.armourGood = 0xff46d96a;
	p.armourWarn = 0xfff0b429;
	p.armourCrit = 0xffe84848;
	p.textBright = 0xfff2f6fa;
	p.textMuted = 0xff9eb8cc;
	p.textDisabled = 0xff8098ab;
	p.textCyan = 0xff6ae0ff;
	p.textGold = 0xfff6d365;
	p.textAlert = 0xffff7066;
	return p;
}

// Bitmap-font safety: the game's small font lacks these glyphs and renders them as '-'.
inline bool CeCmdHudCharForbidden(unsigned char c)
{
	switch (c)
	{
	case '$': case '@': case '[': case '\\': case ']': case '^':
	case '_': case '{': case '|': case '}': case '~': case '`':
		return true;
	default:
		return c < 0x20 || c >= 0x7f;
	}
}

// Copies src into out while replacing any forbidden glyph with a space or hyphen,
// collapsing repeated spaces, and clamping to room (always null-terminated).
inline void CeCmdHudSanitizeText(const char *src, char *out, unsigned room)
{
	if (!out || room == 0) return;
	out[0] = 0;
	if (!src) return;
	unsigned n = 0;
	bool prevSpace = true;
	for (const unsigned char *p = reinterpret_cast<const unsigned char *>(src); *p && n + 1 < room; ++p)
	{
		char ch = static_cast<char>(*p);
		if (CeCmdHudCharForbidden(*p)) ch = (*p == '_') ? ' ' : '-';
		if (ch == ' ')
		{
			if (prevSpace) continue;
			prevSpace = true;
		}
		else prevSpace = false;
		out[n++] = ch;
	}
	while (n > 0 && out[n - 1] == ' ') --n;
	out[n] = 0;
}

inline bool CeCmdHudStartsWithCI(const char *s, const char *prefix)
{
	if (!s || !prefix) return false;
	while (*prefix)
	{
		unsigned char a = static_cast<unsigned char>(*s++);
		unsigned char b = static_cast<unsigned char>(*prefix++);
		if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b - 'A' + 'a');
		if (a != b) return false;
	}
	return true;
}

inline bool CeCmdHudContainsCI(const char *haystack, const char *needle)
{
	if (!haystack || !needle || !*needle) return false;
	for (const char *p = haystack; *p; ++p)
		if (CeCmdHudStartsWithCI(p, needle)) return true;
	return false;
}

// Formats a crisp, bitmap-font-safe short unit/squad/structure name for placards and buttons.
inline void CeCmdHudShortName(const char *rawName, bool plural, bool isStructure, char *out, unsigned room)
{
	if (!out || room == 0) return;
	char clean[64];
	CeCmdHudSanitizeText(rawName ? rawName : "", clean, sizeof(clean));
	if (!clean[0])
	{
		std::snprintf(out, room, "%s", isStructure ? "Factory" : (plural ? "Squad" : "Unit"));
		return;
	}
	const char *mapped = NULL;
	if (isStructure)
	{
		if (CeCmdHudContainsCI(clean, "Pulse Tank Factory")) mapped = "Pulse Factory";
		else if (CeCmdHudContainsCI(clean, "Heavy Tank Factory")) mapped = "Heavy Factory";
		else if (CeCmdHudContainsCI(clean, "Tank Factory")) mapped = "Tank Factory";
		else if (CeCmdHudContainsCI(clean, "Barracks")) mapped = "Barracks";
		else if (CeCmdHudContainsCI(clean, "Airfield")) mapped = "Airfield";
		else if (CeCmdHudContainsCI(clean, "Hangar")) mapped = "Hangar";
		else if (CeCmdHudContainsCI(clean, "Helipad")) mapped = "Helipad";
		else if (CeCmdHudContainsCI(clean, "Shipyard")) mapped = "Shipyard";
		else if (CeCmdHudContainsCI(clean, "Factory")) mapped = "Factory";
	}
	else
	{
		if (CeCmdHudContainsCI(clean, "Sabre")) mapped = plural ? "Sabre Tanks" : "Sabre Tank";
		else if (CeCmdHudContainsCI(clean, "Marauder")) mapped = plural ? "Marauders" : "Marauder";
		else if (CeCmdHudContainsCI(clean, "Cerberus")) mapped = plural ? "Cerberus" : "Cerberus";
		else if (CeCmdHudContainsCI(clean, "Infantry")) mapped = "Infantry";
		else if (CeCmdHudContainsCI(clean, "Artillery")) mapped = "Artillery";
		else if (CeCmdHudContainsCI(clean, "Howitzer")) mapped = plural ? "Howitzers" : "Howitzer";
		else if (CeCmdHudContainsCI(clean, "Hover")) mapped = plural ? "Hover Tanks" : "Hover Tank";
		else if (CeCmdHudContainsCI(clean, "Jeep")) mapped = plural ? "Recon Jeeps" : "Recon Jeep";
		else if (CeCmdHudContainsCI(clean, "Mech")) mapped = plural ? "Mechs" : "Mech";
		else if (CeCmdHudContainsCI(clean, "Bomber")) mapped = plural ? "Bombers" : "Bomber";
		else if (CeCmdHudContainsCI(clean, "Fighter")) mapped = plural ? "Fighters" : "Fighter";
		else if (CeCmdHudContainsCI(clean, "Gunship")) mapped = plural ? "Gunships" : "Gunship";
		else if (CeCmdHudContainsCI(clean, "Tank")) mapped = plural ? "Tanks" : "Tank";
	}
	if (mapped)
	{
		CeCmdHudSanitizeText(mapped, out, room);
		return;
	}
	const char *trimmed = clean;
	if (CeCmdHudStartsWithCI(trimmed, "Forseti ")) trimmed += 8;
	else if (CeCmdHudStartsWithCI(trimmed, "Muspell ")) trimmed += 8;
	// Strip leading tactical designations like "AV-14B " or "W-11 " when the remainder is readable.
	const char *space = std::strchr(trimmed, ' ');
	if (space && space - trimmed <= 8 && std::strchr(trimmed, '-') && std::strchr(trimmed, '-') < space && space[1])
		trimmed = space + 1;
	CeCmdHudSanitizeText(trimmed, out, room);
}

inline float CeCmdHudArmourRatio(float life, float maxLife, int members, int peak)
{
	if (std::isfinite(life) && std::isfinite(maxLife) && maxLife > 0.f)
	{
		float r = life / maxLife;
		if (r < 0.f) r = 0.f;
		if (r > 1.f) r = 1.f;
		return r;
	}
	if (peak > 0 && members >= 0)
	{
		float r = float(members) / float(peak);
		if (r < 0.f) r = 0.f;
		if (r > 1.f) r = 1.f;
		return r;
	}
	return 1.f;
}

inline std::uint32_t CeCmdHudArmourColor(float ratio, const CeCmdHudPalette &p)
{
	if (!std::isfinite(ratio) || ratio < 0.30f) return p.armourCrit;
	if (ratio < 0.60f) return p.armourWarn;
	return p.armourGood;
}

inline bool CeCmdHudRectContains(const CeCmdHudRect &r, float px, float py)
{
	return r.w > 0.f && r.h > 0.f && px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

struct CeCmdHudLayout
{
	bool valid;
	float viewportW, viewportH, scale;
	CeCmdHudRect barRect;
	CeCmdHudRect trayRect;
	CeCmdHudRect trayHeaderRect;
	CeCmdHudRect prevPageRect;
	CeCmdHudRect nextPageRect;
	bool showPaging;
	int totalPlacards;
	int visibleCount;
	int visibleStart;
	int slotsPerPage;
	int pageIndex;
	int totalPages;
	CeCmdHudRect placardRects[kCeHudMaxVisiblePlacards];
	CeCmdHudRect dividerRect;
	CeCmdHudRect cardRect;
	CeCmdHudRect cardHeaderRect;
	CeCmdHudRect cardProgressRect;
	CeCmdHudRect buttonRects[kCeHudCardButtons];
	float battlefieldFraction;		// unobstructed vertical share (bar + top strip excluded)
};

// Pure responsive bottom RTS Command Bar layout for 640x480, 1280x720, 1920x1080 and beyond.
// Keeps >= 75% of the vertical viewport unobstructed and cleanly shifts above uiBottom
// whenever radio dialogue reserves the lower viewport.
inline CeCmdHudLayout CeCmdHudComputeLayout(float viewportW, float viewportH, float uiScale,
                                            float uiBottom, int totalPlacards, int requestedPage)
{
	CeCmdHudLayout out = {};
	if (!(viewportW >= 320.f && viewportH >= 240.f && uiScale > 0.f)) return out;
	const float u = uiScale;
	const float margin = std::floor(8.f * u);
	const float topReserved = std::floor(30.f * u);
	const float bottomGap = std::floor(4.f * u);
	// Bar height is capped to 84*u (17.5% of height) so top + bar + gap <= 24.6% (>= 75.4% battlefield open).
	float barH = std::floor(84.f * u);
	const float maxBarH = std::floor(viewportH * 0.18f);
	if (barH > maxBarH) barH = maxBarH;
	if (barH < 64.f) barH = 64.f;
	const float clampedBottom = (uiBottom > topReserved + barH + margin && uiBottom <= viewportH) ? uiBottom : viewportH;
	const float barY = std::floor(clampedBottom - barH - bottomGap);
	const float barW = std::floor(viewportW - 2.f * margin);
	if (barW < 240.f || barY < topReserved) return out;

	out.valid = true;
	out.viewportW = viewportW;
	out.viewportH = viewportH;
	out.scale = u;
	out.barRect = {margin, barY, barW, barH};

	const float innerPad = std::floor(5.f * u);
	const float dividerGap = std::floor(8.f * u);
	const float innerW = barW - 2.f * innerPad;
	const float innerH = barH - 2.f * innerPad;
	const float innerY = barY + innerPad;

	// Right dock: RTS Command Card. Target ~250*u on 4:3, ~286*u on 16:9, clamped cleanly.
	const float desiredCardW = (viewportW / viewportH >= 1.55f) ? std::floor(286.f * u) : std::floor(244.f * u);
	float cardW = desiredCardW;
	const float maxCardW = std::floor(innerW * 0.45f);
	if (cardW > maxCardW) cardW = maxCardW;
	if (cardW < 180.f) cardW = 180.f;

	const float trayW = innerW - cardW - dividerGap;
	out.trayRect = {margin + innerPad, innerY, trayW, innerH};
	out.dividerRect = {out.trayRect.x + trayW + std::floor(dividerGap * 0.5f) - 1.f,
	                   innerY + std::floor(2.f * u), std::floor(u) > 1.f ? std::floor(u) : 1.f,
	                   innerH - std::floor(4.f * u)};
	out.cardRect = {out.trayRect.x + trayW + dividerGap, innerY, cardW, innerH};

	// Placard tray header + card row
	const float hdrH = std::floor(14.f * u);
	out.trayHeaderRect = {out.trayRect.x, out.trayRect.y, trayW, hdrH};
	const float rowY = out.trayRect.y + hdrH + std::floor(3.f * u);
	const float rowH = out.trayRect.h - hdrH - std::floor(3.f * u);
	const float cardGap = std::floor(5.f * u);
	const float minPlacardW = std::floor(96.f * u);
	int slots = int((trayW + cardGap) / (minPlacardW + cardGap));
	if (slots < 1) slots = 1;
	if (slots > kCeHudMaxVisiblePlacards) slots = kCeHudMaxVisiblePlacards;
	out.slotsPerPage = slots;

	const int clampedTotal = totalPlacards < 0 ? 0 : (totalPlacards > kCeHudMaxPlacards ? kCeHudMaxPlacards : totalPlacards);
	out.totalPlacards = clampedTotal;
	out.totalPages = clampedTotal > 0 ? ((clampedTotal + slots - 1) / slots) : 1;
	int page = requestedPage;
	if (page < 0) page = 0;
	if (page >= out.totalPages) page = out.totalPages - 1;
	out.pageIndex = page;
	out.visibleStart = page * slots;
	int rem = clampedTotal - out.visibleStart;
	if (rem < 0) rem = 0;
	out.visibleCount = rem < slots ? rem : slots;

	out.showPaging = out.totalPages > 1;
	if (out.showPaging)
	{
		const float navW = std::floor(18.f * u);
		const float navH = hdrH;
		const float pageLabelW = std::floor(30.f * u);
		out.nextPageRect = {out.trayHeaderRect.x + trayW - navW, out.trayHeaderRect.y, navW, navH};
		out.prevPageRect = {out.nextPageRect.x - pageLabelW - navW, out.trayHeaderRect.y, navW, navH};
	}

	const float slotW = std::floor((trayW - float(slots - 1) * cardGap) / float(slots));
	for (int i = 0; i < slots; ++i)
		out.placardRects[i] = {out.trayRect.x + float(i) * (slotW + cardGap), rowY, slotW, rowH};

	// Right dock: Command Card header + 2x3 metallic button grid
	const float cardHdrH = std::floor(24.f * u);
	out.cardHeaderRect = {out.cardRect.x, out.cardRect.y, cardW, cardHdrH};
	const float gridTop = out.cardRect.y + cardHdrH + std::floor(3.f * u);
	const float gridH = out.cardRect.h - cardHdrH - std::floor(3.f * u);
	const float btnGapX = std::floor(4.f * u);
	const float btnGapY = std::floor(3.f * u);
	const float btnW = std::floor((cardW - 2.f * btnGapX) / 3.f);
	const float btnH = std::floor((gridH - btnGapY) * 0.5f);
	// Structure progress bar sits cleanly inside the bottom strip of cardHeaderRect.
	out.cardProgressRect = {out.cardRect.x, out.cardRect.y + cardHdrH - std::floor(4.f * u), cardW, std::floor(3.f * u)};

	for (int row = 0; row < 2; ++row)
	{
		for (int col = 0; col < 3; ++col)
		{
			const int idx = row * 3 + col;
			out.buttonRects[idx] = {out.cardRect.x + float(col) * (btnW + btnGapX),
			                        gridTop + float(row) * (btnH + btnGapY), btnW, btnH};
		}
	}

	const float usedVertical = topReserved + barH + bottomGap;
	out.battlefieldFraction = 1.f - (usedVertical / viewportH);
	return out;
}

enum CeCmdHudHitKind
{
	CE_HUD_HIT_NONE = 0,
	CE_HUD_HIT_BAR_PADDING,
	CE_HUD_HIT_PLACARD,
	CE_HUD_HIT_PREV_PAGE,
	CE_HUD_HIT_NEXT_PAGE,
	CE_HUD_HIT_CARD_BUTTON
};

struct CeCmdHudHit
{
	CeCmdHudHitKind kind;
	int index;						// placard global index (visibleStart + slot) or button slot 0..5
};

inline CeCmdHudHit CeCmdHudHitTest(const CeCmdHudLayout &layout, float px, float py)
{
	CeCmdHudHit hit = {CE_HUD_HIT_NONE, -1};
	if (!layout.valid || !CeCmdHudRectContains(layout.barRect, px, py)) return hit;
	if (layout.showPaging)
	{
		if (CeCmdHudRectContains(layout.prevPageRect, px, py)) return {CE_HUD_HIT_PREV_PAGE, 0};
		if (CeCmdHudRectContains(layout.nextPageRect, px, py)) return {CE_HUD_HIT_NEXT_PAGE, 0};
	}
	for (int i = 0; i < layout.visibleCount; ++i)
	{
		if (CeCmdHudRectContains(layout.placardRects[i], px, py))
			return {CE_HUD_HIT_PLACARD, layout.visibleStart + i};
	}
	for (int i = 0; i < kCeHudCardButtons; ++i)
	{
		if (CeCmdHudRectContains(layout.buttonRects[i], px, py))
			return {CE_HUD_HIT_CARD_BUTTON, i};
	}
	return {CE_HUD_HIT_BAR_PADDING, -1};
}

// Pure deterministic double-click tracker for placard camera focus.
struct CeCmdClickTracker
{
	CeCmdPlacardKind lastKind;
	int lastId;
	std::uint64_t lastNs;
	bool primed;

	void Reset()
	{
		lastKind = CE_PLACARD_SQUAD;
		lastId = -1;
		lastNs = 0;
		primed = false;
	}

	bool RegisterClick(CeCmdPlacardKind kind, int id, std::uint64_t nowNs,
	                   std::uint64_t windowNs = 400000000ull)
	{
		if (primed && lastKind == kind && lastId == id && nowNs >= lastNs && (nowNs - lastNs) <= windowNs)
		{
			Reset();
			return true;
		}
		lastKind = kind;
		lastId = id;
		lastNs = nowNs;
		primed = true;
		return false;
	}
};

// Drawing callback surface implemented by ce_cmdmap.cpp (used by ce_cmd_hud.cpp).
struct CeCmdHudDrawOps
{
	void (*box)(float x, float y, float w, float h, std::uint32_t colour);
	void (*outline)(float x, float y, float w, float h, float t, std::uint32_t colour);
	void (*text)(float x, float y, std::uint32_t colour, const char *text, float maxWidth, bool centred, float scale);
	float (*textWidth)(const char *text, float scale);
	float (*lineHeight)(float scale);
};

void CeCmdHudDrawBar(const CeCmdHudDrawOps &draw, const CeCmdHudLayout &layout,
                     const CeCmdPlacard *placards, int placardCount,
                     const CeCmdCardModel &card, const char *forcesSummary,
                     float mouseX, float mouseY, const std::uint64_t *buttonFlashNs,
                     std::uint64_t nowNs);

#endif
