// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_hud_testkit.cpp: the test kit's state keys for widescreen and the HUD (ce_testkit.h;
// port/journeys/README.md): where the 3D view, the 4:3 interface box and the HUD's columns sit for the back buffer shown.
// Compiled only by build.py --test-kit; it reads port.h's and hud_scale.h's plain answers and changes nothing.
#include "port.h"
#include "hud_scale.h"
#include "ce_testkit.h"
#include <stdio.h>

int PortHudAnchored();	// ce_hud_anchor.cpp

namespace
{
bool	StateBuffer(char *out, size_t size)
{
	unsigned w = 0, h = 0;
	PortWideBackBufferSize(&w, &h);
	snprintf(out, size, "%ux%u", w, h);
	return true;
}
bool	StateWide(char *out, size_t size) { snprintf(out, size, "%s", PortWideActive() ? "on" : "off"); return true; }
bool	StateView(char *out, size_t size) { snprintf(out, size, "%.4f,%.4f", PortWideX(), PortWideY()); return true; }
bool	StateUi(char *out, size_t size) { snprintf(out, size, "%.4f,%.1f,%.1f", PortUiScale(), PortUiLeft(), PortUiTop()); return true; }
bool	StateInset(char *out, size_t size)
{
	unsigned w = 0, h = 0;
	PortWideBackBufferSize(&w, &h);
	snprintf(out, size, "%.1f", PortHudInset(float(w)));
	return true;
}
bool	StateAnchored(char *out, size_t size) { snprintf(out, size, "%s", PortHudAnchored() ? "yes" : "no"); return true; }
bool	StateScale(char *out, size_t size)
{
	snprintf(out, size, "%s,%.4f", CeHudScaleActive() ? "on" : "off", CeHudScaleFactor());
	return true;
}
}

CE_TEST_STATE("hud.buffer", StateBuffer);		// the back buffer, WxH
CE_TEST_STATE("hud.wide", StateWide);			// on: the view is widened (not 4:3)
CE_TEST_STATE("hud.view", StateView);			// Hor+ and Vert+ factors
CE_TEST_STATE("hud.ui", StateUi);				// the 4:3 interface box: scale, left, top
CE_TEST_STATE("hud.inset", StateInset);			// the HUD's shift into that box's columns, in pixels (0 when anchored)
CE_TEST_STATE("hud.anchored", StateAnchored);	// the HUD's corner groups sit in the screen's corners
CE_TEST_STATE("hud.scale", StateScale);			// HUD scale on or off, and its factor
