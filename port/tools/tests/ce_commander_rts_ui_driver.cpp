// SPDX-License-Identifier: GPL-3.0-only
// Host unit driver for Commander Mode RTS HUD layout, placard/card hit-testing,
// double-click focus tracker, bitmap-font-safe short names, armour color tiers,
// and local retail voice cooldown/path logic (ce_cmd_hud.h + ce_cmd_voice.h).
#include "ce_cmd_hud.h"
#include "ce_cmd_voice.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

static int gChecks = 0;

static void Require(bool ok, const char *label)
{
	++gChecks;
	if (!ok)
	{
		std::fprintf(stderr, "FAIL %s\n", label);
		std::exit(1);
	}
}

static bool Overlaps(const CeCmdHudRect &a, const CeCmdHudRect &b)
{
	return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

static bool Inside(const CeCmdHudRect &inner, const CeCmdHudRect &outer)
{
	return inner.x >= outer.x - 0.5f && inner.y >= outer.y - 0.5f &&
	       inner.x + inner.w <= outer.x + outer.w + 0.5f &&
	       inner.y + inner.h <= outer.y + outer.h + 0.5f;
}

static void CheckResponsiveLayouts()
{
	struct Mode { float w, h; };
	const Mode modes[3] = {{640.f, 480.f}, {1280.f, 720.f}, {1920.f, 1080.f}};
	for (int m = 0; m < 3; ++m)
	{
		const float w = modes[m].w, h = modes[m].h;
		const float s = (w / 640.f < h / 480.f) ? (w / 640.f) : (h / 480.f);
		const CeCmdHudLayout lay = CeCmdHudComputeLayout(w, h, s, h, 8, 0);
		Require(lay.valid, "layout valid at standard resolutions");
		Require(lay.battlefieldFraction >= 0.75f, "battlefield leaves at least 75 percent unobstructed");
		Require(lay.barRect.x >= 0.f && lay.barRect.y > 0.f, "bar inside viewport origin");
		Require(lay.barRect.x + lay.barRect.w <= w + 0.5f && lay.barRect.y + lay.barRect.h <= h + 0.5f,
		        "bar fits within viewport bounds");
		Require(Inside(lay.trayRect, lay.barRect) && Inside(lay.cardRect, lay.barRect),
		        "tray and command card fit inside bar");
		Require(!Overlaps(lay.trayRect, lay.cardRect), "tray and command card do not overlap");
		Require(lay.slotsPerPage >= 3 && lay.slotsPerPage <= kCeHudMaxVisiblePlacards,
		        "placard row holds 3 to 6 cards");
		Require(lay.showPaging && lay.totalPages >= 2, "8 placards enable paging");
		Require(Inside(lay.prevPageRect, lay.trayHeaderRect) && Inside(lay.nextPageRect, lay.trayHeaderRect),
		        "paging buttons fit inside tray header");
		Require(!Overlaps(lay.prevPageRect, lay.nextPageRect), "paging buttons do not overlap");

		for (int i = 0; i < lay.visibleCount; ++i)
		{
			Require(Inside(lay.placardRects[i], lay.trayRect), "placard inside tray");
			Require(!Overlaps(lay.placardRects[i], lay.trayHeaderRect), "placard clears tray header");
			for (int j = i + 1; j < lay.visibleCount; ++j)
				Require(!Overlaps(lay.placardRects[i], lay.placardRects[j]), "placards do not overlap each other");
		}
		for (int i = 0; i < kCeHudCardButtons; ++i)
		{
			Require(Inside(lay.buttonRects[i], lay.cardRect), "card button inside card");
			Require(!Overlaps(lay.buttonRects[i], lay.cardHeaderRect), "card button clears card header");
			for (int j = i + 1; j < kCeHudCardButtons; ++j)
				Require(!Overlaps(lay.buttonRects[i], lay.buttonRects[j]), "card buttons do not overlap");
		}

		// Hit testing every interactive region and padding gap
		const CeCmdHudHit outside = CeCmdHudHitTest(lay, w * 0.5f, h * 0.25f);
		Require(outside.kind == CE_HUD_HIT_NONE, "battlefield click is not captured by bar");

		const CeCmdHudRect &p0 = lay.placardRects[0];
		const CeCmdHudHit hp0 = CeCmdHudHitTest(lay, p0.x + p0.w * 0.5f, p0.y + p0.h * 0.5f);
		Require(hp0.kind == CE_HUD_HIT_PLACARD && hp0.index == 0, "placard 0 hit test");

		const CeCmdHudHit hPrev = CeCmdHudHitTest(lay, lay.prevPageRect.x + 2.f, lay.prevPageRect.y + 2.f);
		const CeCmdHudHit hNext = CeCmdHudHitTest(lay, lay.nextPageRect.x + 2.f, lay.nextPageRect.y + 2.f);
		Require(hPrev.kind == CE_HUD_HIT_PREV_PAGE && hNext.kind == CE_HUD_HIT_NEXT_PAGE, "paging hit tests");

		for (int b = 0; b < kCeHudCardButtons; ++b)
		{
			const CeCmdHudRect &br = lay.buttonRects[b];
			const CeCmdHudHit hb = CeCmdHudHitTest(lay, br.x + br.w * 0.5f, br.y + br.h * 0.5f);
			Require(hb.kind == CE_HUD_HIT_CARD_BUTTON && hb.index == b, "command card button hit test");
		}

		// Gap between button 0 and button 1 counts as bar padding (never leaks to world or hits button)
		const float gapX = lay.buttonRects[0].x + lay.buttonRects[0].w +
		                   (lay.buttonRects[1].x - (lay.buttonRects[0].x + lay.buttonRects[0].w)) * 0.5f;
		const CeCmdHudHit hGap = CeCmdHudHitTest(lay, gapX, lay.buttonRects[0].y + 4.f);
		Require(hGap.kind == CE_HUD_HIT_BAR_PADDING, "gap between buttons is bar padding");

		// Dialogue reservation shifts the entire RTS bar cleanly above uiBottom
		const float dlgBottom = h * 0.75f;
		const CeCmdHudLayout shifted = CeCmdHudComputeLayout(w, h, s, dlgBottom, 4, 0);
		Require(shifted.valid && shifted.barRect.y + shifted.barRect.h <= dlgBottom,
		        "dialogue reservation shifts bar above uiBottom");
		Require(shifted.barRect.y < lay.barRect.y, "shifted bar sits higher than unshifted bar");
	}

	// Page clamping & page-2 placard indices
	const CeCmdHudLayout p2 = CeCmdHudComputeLayout(1280.f, 720.f, 1.5f, 720.f, 9, 99);
	Require(p2.valid && p2.pageIndex == p2.totalPages - 1, "excessive page index clamps to last page");
	Require(p2.visibleStart == p2.pageIndex * p2.slotsPerPage, "page visibleStart matches pageIndex");
	const CeCmdHudHit hpLast = CeCmdHudHitTest(p2, p2.placardRects[0].x + 4.f, p2.placardRects[0].y + 4.f);
	Require(hpLast.kind == CE_HUD_HIT_PLACARD && hpLast.index == p2.visibleStart,
	        "paged placard hit returns global index");

	// Stale layout bounds: passing placardCount prevents out-of-bounds placard hits
	{
		CeCmdHudLayout stale = CeCmdHudComputeLayout(1280.f, 720.f, 1.5f, 720.f, 4, 0);
		const CeCmdHudRect &slot3 = stale.placardRects[3];
		const CeCmdHudHit hStale = CeCmdHudHitTest(stale, slot3.x + 4.f, slot3.y + 4.f, 2);
		Require(hStale.kind == CE_HUD_HIT_BAR_PADDING, "stale layout placard beyond placardCount never hits OOB");
		const CeCmdHudRect &slot1 = stale.placardRects[1];
		const CeCmdHudHit hValid = CeCmdHudHitTest(stale, slot1.x + 4.f, slot1.y + 4.f, 2);
		Require(hValid.kind == CE_HUD_HIT_PLACARD && hValid.index == 1, "in-bounds placard still hits with placardCount");
		stale.visibleCount = kCeHudMaxVisiblePlacards + 10;
		const CeCmdHudHit hClamp = CeCmdHudHitTest(stale, slot1.x + 4.f, slot1.y + 4.f, 4);
		Require(hClamp.kind == CE_HUD_HIT_PLACARD && hClamp.index == 1, "corrupted visibleCount safely clamped to kCeHudMaxVisiblePlacards");
	}

	// Invalid viewport gracefully returns invalid layout
	const CeCmdHudLayout bad = CeCmdHudComputeLayout(0.f, 0.f, 1.f, 0.f, 3, 0);
	Require(!bad.valid && CeCmdHudHitTest(bad, 10.f, 10.f).kind == CE_HUD_HIT_NONE, "zero viewport rejected");
}

static void CheckTextAndArmour()
{
	char buf[32];
	CeCmdHudShortName("AV-14B Sabre Pulse Tank", true, false, buf, sizeof(buf));
	Require(std::strcmp(buf, "Sabre Tanks") == 0, "plural sabre tank short name");
	CeCmdHudShortName("AV-14B Sabre Pulse Tank", false, false, buf, sizeof(buf));
	Require(std::strcmp(buf, "Sabre Tank") == 0, "singular sabre tank short name");
	CeCmdHudShortName("Forseti Pulse Tank Factory", false, true, buf, sizeof(buf));
	Require(std::strcmp(buf, "Pulse Factory") == 0, "pulse tank factory short name");
	CeCmdHudShortName("Forseti Infantry", true, false, buf, sizeof(buf));
	Require(std::strcmp(buf, "Infantry") == 0, "infantry short name");

	// Forbidden bitmap-font glyphs must never survive sanitization
	char clean[40];
	CeCmdHudSanitizeText("Unit_A [1] | $test @foo ^bar {baz} ~`\\", clean, sizeof(clean));
	const char *forbidden = "$@[\\]^_{|}~`";
	for (const char *c = clean; *c; ++c)
		Require(std::strchr(forbidden, *c) == NULL && !CeCmdHudCharForbidden(static_cast<unsigned char>(*c)),
		        "sanitized string holds zero forbidden bitmap glyphs");

	// Truncation stays null-terminated within tiny buffer
	char tiny[8];
	CeCmdHudShortName("Forseti Experimental Long Name Unit", false, false, tiny, sizeof(tiny));
	Require(std::strlen(tiny) < sizeof(tiny), "tiny buffer truncation null-terminated");

	// Armour ratio, disabled text legibility, faction palette symmetry, and color tier boundaries
	const CeCmdHudPalette pal = CeCmdHudDefaultPalette();
	const CeCmdHudPalette forseti = CeCmdHudPaletteForAllegiance(0);
	const CeCmdHudPalette muspell = CeCmdHudPaletteForAllegiance(1);
	Require(forseti.barFill == pal.barFill && forseti.cyanAccent == pal.cyanAccent, "allegiance 0 selects Forseti palette");
	Require(muspell.barFill != pal.barFill && muspell.cyanAccent != pal.cyanAccent && muspell.textCyan != pal.textCyan, "allegiance 1 selects distinct Muspell amber/crimson palette");
	Require(((muspell.cyanAccent >> 16) & 0xffu) > ((muspell.cyanAccent) & 0xffu) + 0x80u, "Muspell accent is warm red/amber");
	Require((muspell.textDisabled & 0xffu) >= 0x80u && ((muspell.textDisabled >> 8) & 0xffu) >= 0x80u &&
	        ((muspell.textDisabled >> 16) & 0xffu) >= 0x80u, "Muspell disabled button text keeps legible contrast");
	Require((pal.textDisabled & 0xffu) >= 0x80u && ((pal.textDisabled >> 8) & 0xffu) >= 0x80u &&
	        ((pal.textDisabled >> 16) & 0xffu) >= 0x80u, "disabled button text keeps legible contrast");
	// Selected placard highlight must be unmistakably distinct from structure stripe on BOTH Forseti and Muspell
	auto RgbL1 = [](std::uint32_t a, std::uint32_t b) -> int {
		const int dr = int((a >> 16) & 0xffu) - int((b >> 16) & 0xffu);
		const int dg = int((a >> 8) & 0xffu) - int((b >> 8) & 0xffu);
		const int db = int(a & 0xffu) - int(b & 0xffu);
		return (dr < 0 ? -dr : dr) + (dg < 0 ? -dg : dg) + (db < 0 ? -db : db);
	};
	Require(RgbL1(forseti.placardSelectedBorder, forseti.placardStructStripe) >= 180,
	        "Forseti selected border has unmistakable contrast against structure stripe");
	Require(RgbL1(muspell.placardSelectedBorder, muspell.placardStructStripe) >= 180,
	        "Muspell selected border has unmistakable contrast against structure stripe");
	Require(std::fabs(CeCmdHudArmourRatio(60.f, 100.f, 1, 1) - 0.60f) < 1e-4f, "exact 60 percent ratio");
	Require(CeCmdHudArmourRatio(-5.f, 100.f, 1, 1) == 0.f, "negative life clamps to 0");
	Require(CeCmdHudArmourRatio(250.f, 100.f, 1, 1) == 1.f, "excess life clamps to 1");
	Require(std::fabs(CeCmdHudArmourRatio(0.f, 0.f, 2, 4) - 0.50f) < 1e-4f, "fallback to member/peak ratio");
	Require(CeCmdHudArmourRatio(std::numeric_limits<float>::quiet_NaN(), 100.f, 1, 1) == 1.f, "NaN life falls back cleanly");
	Require(CeCmdHudArmourColor(0.60f, pal) == pal.armourGood, "60 percent is green");
	Require(CeCmdHudArmourColor(0.59f, pal) == pal.armourWarn, "59 percent is amber");
	Require(CeCmdHudArmourColor(0.30f, pal) == pal.armourWarn, "30 percent is amber");
	Require(CeCmdHudArmourColor(0.29f, pal) == pal.armourCrit, "29 percent is red");
	Require(CeCmdHudArmourColor(std::numeric_limits<float>::quiet_NaN(), pal) == pal.armourCrit, "NaN armour color is red");
}

static void CheckDoubleClick()
{
	CeCmdClickTracker tr = {};
	tr.Reset();
	const std::uint64_t t0 = 1000000000ull;
	Require(!tr.RegisterClick(CE_PLACARD_SQUAD, 5, t0), "first click is not double click");
	Require(tr.RegisterClick(CE_PLACARD_SQUAD, 5, t0 + 250000000ull), "second click within 400ms triggers focus");
	Require(!tr.RegisterClick(CE_PLACARD_SQUAD, 5, t0 + 300000000ull), "triple click does not double fire");
	Require(!tr.RegisterClick(CE_PLACARD_SQUAD, 6, t0 + 350000000ull), "different target resets tracker");
	Require(!tr.RegisterClick(CE_PLACARD_STRUCTURE, 6, t0 + 400000000ull), "different kind with same id is not double click");
	Require(!tr.RegisterClick(CE_PLACARD_STRUCTURE, 6, t0 + 900000000ull), "click after 400ms window does not double fire");
	Require(!tr.RegisterClick(CE_PLACARD_STRUCTURE, 6, t0 + 800000000ull), "backwards clock never triggers double click");
}

static void CheckVoiceGate()
{
	CeCmdVoiceGate gate = {};
	gate.Reset();
	Require(kCeCmdVoiceCooldownNs >= 1800000000ull, "voice cooldown is at least 1.8 seconds");
	const std::uint64_t t0 = 5000000000ull;
	Require(!gate.ShouldPlay(CE_VOICE_NONE, t0), "none cue rejected");
	Require(!gate.ShouldPlay(CE_VOICE_COUNT, t0), "out-of-range cue rejected");

	const char *s1 = gate.TryNextStem(CE_VOICE_MOVE, t0);
	Require(s1 != NULL && s1[0] != 0, "first voice cue allowed");
	Require(gate.TryNextStem(CE_VOICE_MOVE, t0 + 1000000000ull) == NULL, "cue within 1.8s cooldown suppressed");
	Require(gate.TryNextStem(CE_VOICE_ATTACK, t0 + 1799999999ull) == NULL, "cue 1ns before cooldown suppressed");
	Require(gate.TryNextStem(CE_VOICE_MOVE, t0 - 100ull) == NULL, "backwards clock suppressed");

	const char *s2 = gate.TryNextStem(CE_VOICE_MOVE, t0 + kCeCmdVoiceCooldownNs);
	Require(s2 != NULL && std::strcmp(s1, s2) != 0, "next cue after cooldown cycles deterministically");

	const char *s3 = gate.TryNextStem(CE_VOICE_BUILD_QUEUE, t0 + 2 * kCeCmdVoiceCooldownNs);
	Require(s3 != NULL && s3[0] != 0, "build queue cue returns valid retail stem");

	// Language sanitization & path formatting
	Require(std::strcmp(CeCmdVoiceGate::SafeLanguage("FRENCH"), "french") == 0, "case-insensitive language match");
	Require(std::strcmp(CeCmdVoiceGate::SafeLanguage("../evil"), "english") == 0, "traversal language falls back to english");
	Require(std::strcmp(CeCmdVoiceGate::SafeLanguage(NULL), "english") == 0, "null language falls back to english");

	char path[260];
	Require(CeCmdVoiceGate::FormatSpeechPath("German", "racing_start_3", path, sizeof(path)), "valid path formats");
	Require(std::strcmp(path, "data\\sounds\\german\\MessageBox\\racing_start_3.ogg") == 0, "formatted retail speech path");
	Require(!CeCmdVoiceGate::FormatSpeechPath("english", "../bad", path, sizeof(path)), "unsafe stem rejected");
	Require(!CeCmdVoiceGate::FormatSpeechPath("english", "racing_start_3", path, 16), "undersized buffer rejected");
}

int main()
{
	CheckResponsiveLayouts();
	CheckTextAndArmour();
	CheckDoubleClick();
	CheckVoiceGate();
	std::printf("checks=%d failures=0 skips=0\nPASS\n", gChecks);
	return 0;
}
