// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_multibe.h: opt-in Multi-Battle-Engine (>2 BEs, 3-4 simultaneous Battle Engines) helpers.
// Pure math and configuration helpers live in this header (and ce_multibe_state.h) with no game-header dependencies
// so host tests can compile and mutant-test them directly (port/tools/ce_multibe_tests.py).
//
// Strictly gated behind ce.ini (`multi_be=3` or `multi_be=4`, off/2 by default) and CE_GATE_LAYER (never runs in
// replays, recordings, probes, faithful perturbation sweeps, or 2P network lockstep sessions).
#ifndef PORT_CE_MULTIBE_H
#define PORT_CE_MULTIBE_H

#include "ce_multibe_state.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

enum { CE_MULTIBE_MAX = 4 };

struct CeMultiBeConfig
{
	int		target_count;	// 0 (disabled) or 3..CE_MULTIBE_MAX
	bool	panoramic_cam;	// ce.ini multi_be_cam=1: elevated panoramic showcase camera
	int		hud_focus_slot;	// ce.ini multi_be_hud_slot=0..3 (-1 = retail passthrough)
};

inline bool CeMultiBeStrEqCase(const char *a, const char *b)
{
	if (!a || !b)
		return false;
	while (*a && *b)
	{
		char ca = *a++, cb = *b++;
		if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
		if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
		if (ca != cb)
			return false;
	}
	return *a == 0 && *b == 0;
}

// Parses ce.ini `multi_be` value. Returns 0 (off) for absent/off/2/invalid values, 4 for "1"/"on"/"true"/"4",
// and 3 for "3". Fails closed (returns 0) on any unrecognised value so default 1P/2P runs are never perturbed.
inline int CeMultiBeParseCount(const char *text)
{
	if (!text || !*text)
		return 0;
	if (CeMultiBeStrEqCase(text, "1") || CeMultiBeStrEqCase(text, "on") || CeMultiBeStrEqCase(text, "true") || CeMultiBeStrEqCase(text, "4"))
		return 4;
	if (CeMultiBeStrEqCase(text, "3"))
		return 3;
	return 0;
}

// Parses optional ce.ini `multi_be_hud_slot` value (0..CE_MULTIBE_MAX-1). Returns -1 for passthrough/invalid.
inline int CeMultiBeParseHudSlot(const char *text)
{
	if (!text || !text[0] || text[1] != 0)
		return -1;
	if (text[0] >= '0' && text[0] < '0' + CE_MULTIBE_MAX)
		return text[0] - '0';
	return -1;
}

// Returns true iff (x, y, z) are all finite and within reasonable world bounds (rejects NaN, Inf, and physics blowups).
inline bool CeMultiBeIsFinitePos(float x, float y, float z)
{
	if (!isfinite(x) || !isfinite(y) || !isfinite(z))
		return false;
	const float limit = 100000.0f;
	return fabsf(x) < limit && fabsf(y) < limit && fabsf(z) < limit;
}

// Returns true iff terrain height `groundZ` is strictly dry above `waterZ` by at least `margin` meters
// (BEA uses +Z down, so dry land satisfies groundZ < waterZ - margin).
inline bool CeMultiBeIsAboveWater(float groundZ, float waterZ, float margin)
{
	if (!isfinite(groundZ) || !isfinite(waterZ) || !isfinite(margin) || margin < 0.0f)
		return false;
	return groundZ < waterZ - margin;
}

// Computes local (right, forward) formation offsets around anchor slot 0 for a 3- or 4-BE squadron wedge.
// Slot 0 is the anchor at (0, 0). Slots 1..3 fan out cleanly ahead and to the flanks so every BE is separated
// well beyond collision radius and clearly visible simultaneously.
inline void CeMultiBeFormationOffset(int slot, int total, float spacing, float *outRight, float *outForward)
{
	if (!outRight || !outForward)
		return;
	if (slot <= 0 || total < 3)
	{
		*outRight = 0.0f;
		*outForward = 0.0f;
		return;
	}
	if (total == 3)
	{
		if (slot == 1) { *outRight = -spacing; *outForward = spacing * 0.85f; return; }
		*outRight = spacing;
		*outForward = spacing * 0.85f;
		return;
	}
	// 4-BE compact wedge: anchor (0), left flank (1), right flank (2), center vanguard (3)
	if (slot == 1) { *outRight = -spacing * 1.15f; *outForward = spacing * 0.45f; return; }
	if (slot == 2) { *outRight =  spacing * 1.15f; *outForward = spacing * 0.45f; return; }
	*outRight = 0.0f;
	*outForward = spacing * 1.10f;
}

// Computes an elevated panoramic showcase camera framing all `count` Battle Engine positions in BEA coordinates
// (+Y forward along heading `(fwdX, fwdY)`, +Z down so negative Z is higher altitude).
inline bool CeMultiBeComputeOverviewCam(const float (*positions)[3], int count,
                                        float fwdX, float fwdY,
                                        float backDist, float heightAbove,
                                        float *outCamPos, float *outLookAt)
{
	if (!positions || count <= 0 || !outCamPos || !outLookAt)
		return false;
	float cx = 0.0f, cy = 0.0f, cz = 0.0f;
	int valid = 0;
	for (int i = 0; i < count; i++)
	{
		if (!CeMultiBeIsFinitePos(positions[i][0], positions[i][1], positions[i][2]))
			continue;
		cx += positions[i][0];
		cy += positions[i][1];
		cz += positions[i][2];
		valid++;
	}
	if (valid == 0)
		return false;
	cx /= (float)valid;
	cy /= (float)valid;
	cz /= (float)valid;

	float fLen = sqrtf(fwdX * fwdX + fwdY * fwdY);
	float fx = (fLen > 1e-4f) ? (fwdX / fLen) : 0.0f;
	float fy = (fLen > 1e-4f) ? (fwdY / fLen) : 1.0f;
	// Subtle 18-degree oblique offset so every mech shows a dramatic three-quarter silhouette rather than pure rear view.
	const float rx = fy;
	const float ry = -fx;
	const float sideDist = backDist * 0.34f;

	outLookAt[0] = cx;
	outLookAt[1] = cy;
	outLookAt[2] = cz - 1.5f;

	outCamPos[0] = cx - fx * backDist + rx * sideDist;
	outCamPos[1] = cy - fy * backDist + ry * sideDist;
	outCamPos[2] = cz - heightAbove;	// BEA +Z is down, so subtracting heightAbove elevates camera above the mechs
	return CeMultiBeIsFinitePos(outCamPos[0], outCamPos[1], outCamPos[2]);
}

#endif
