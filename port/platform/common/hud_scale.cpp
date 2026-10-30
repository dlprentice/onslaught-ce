// SPDX-License-Identifier: GPL-3.0-only
// Screen-anchored HUD presentation. No world or gameplay coordinates are changed.
#include "hud_scale.h"
#include <stdlib.h>

extern bool PortCeLayer();
extern int PortWideModes();
extern "C" int CE_SettingGetBool(const char *key, int def);	// ce.cpp: the ce.ini switch

namespace
{
float sScale = 1.f, sAnchorX = 0.f, sAnchorY = 0.f;
bool sActive = false;

bool Enabled()
{
	if (!PortCeLayer())
		return false;
	// Even a forced CE menu must not alter a tape, recording, probe or virtual clock.
	const char *const faithful[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"};
	for (const char *name : faithful)
	{
		const char *value = getenv(name);
		if (value && *value)
			return false;
	}
	const char *off = getenv("BEA_PORT_HUD_SCALE");
	return !(off && *off == '0') && PortWideModes();
}
}

// The ini's answer is read once, the first time the HUD scale's gate lets it matter (a replay or a faithful run never reads it),
// so a change of hud_anchor takes effect at the next start.
bool CeHudAnchorEnabled()
{
	if (!Enabled())
		return false;
	static int ini = -1;
	if (ini < 0)
		ini = CE_SettingGetBool("hud_anchor", 1) ? 1 : 0;
	return ini == 1;
}

CeHudScope::CeHudScope(int eligible, float width, float height, float anchorX, float anchorY)
 : mPreviousScale(sScale), mPreviousX(sAnchorX), mPreviousY(sAnchorY), mPreviousActive(sActive)
{
	sActive = false;
	sScale = 1.f;
	if (!eligible || !(width > 0.f) || !(height > 0.f) || !Enabled())
		return;
	float x = width / 640.f, y = height / 480.f;
	float scale = x < y ? x : y;
	if (!(scale > 1.f) || scale > 64.f)
		return;
	sScale = scale;
	sAnchorX = anchorX;
	sAnchorY = anchorY;
	sActive = true;
}

CeHudScope::~CeHudScope()
{
	sScale = mPreviousScale;
	sAnchorX = mPreviousX;
	sAnchorY = mPreviousY;
	sActive = mPreviousActive;
}

void CeHudScope::SetAnchor(float x, float y)
{
	CeHudSetAnchor(x, y);
}

void CeHudSetAnchor(float x, float y)
{
	if (sActive)
	{
		sAnchorX = x;
		sAnchorY = y;
	}
}

bool CeHudScaleActive() { return sActive; }
float CeHudScaleFactor() { return sActive ? sScale : 1.f; }
void CeHudScalePoint(float *x, float *y)
{
	if (sActive)
	{
		*x = sAnchorX + (*x - sAnchorX) * sScale;
		*y = sAnchorY + (*y - sAnchorY) * sScale;
	}
}
