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

bool CeHudScaleActive();
float CeHudScaleFactor();
void CeHudSetAnchor(float x, float y);
void CeHudScalePoint(float *x, float *y);
