// SPDX-License-Identifier: GPL-3.0-only
// A scoped presentation-only transform for screen-anchored, single-player HUD draws.
#pragma once

class CeHudScope
{
public:
	CeHudScope(int eligible, float width, float height, float anchorX, float anchorY);
	~CeHudScope();
	void SetAnchor(float anchorX, float anchorY);
private:
	float mPreviousScale, mPreviousX, mPreviousY;
	bool mPreviousActive;
	CeHudScope(const CeHudScope &);
	CeHudScope &operator=(const CeHudScope &);
};

// Whether the corner groups of the single-player HUD sit in the corners of a wide screen instead of the 4:3 columns of
// patch 0043: the HUD scale's own gate (so also off in recordings and replays, with BEA_PORT_HUD_SCALE=0 and
// BEA_PORT_WIDESCREEN=0) and ce.ini's hud_anchor (default 1). It does not depend on the HUD's size: hud_scale=0 keeps the
// original size and still anchors. PortHudAnchored (ce_hud_anchor.cpp) adds the viewpoint test.
bool CeHudAnchorEnabled();

// ce.ini's hud_scale (1 Larger, the default, or 0 Original: the HUD keeps its original size) and hud_anchor are each read once,
// the first time an unfenced run asks, and BEA_PORT_HUD_SCALE in the environment, when it is set, wins over hud_scale (0 is
// Original, and also pins the HUD to the 4:3 columns). Whatever writes either line during a run calls this, so that the next
// level's HUD passes ask again; until something does, a change applies at the next start.
void CeHudSettingsReload();

bool CeHudScaleActive();
float CeHudScaleFactor();
void CeHudSetAnchor(float x, float y);
void CeHudScalePoint(float *x, float *y);
