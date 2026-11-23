// SPDX-License-Identifier: GPL-3.0-only
// Play as a unit: pure tactics, physics, camera, visibility, and faction HUD helpers
// for all combat and transport unit classes (infantry, tanks, turrets, aircraft, naval/
// landing craft, and artillery). Zero game types, zero heap allocations, zero RNG:
// host unit tests compile this header directly.
#ifndef PORT_CE_PLAYUNIT_TACTICS_H
#define PORT_CE_PLAYUNIT_TACTICS_H

#include "ce_playunit_rules.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// ---- bitmap-font-safe text helpers -------------------------------------------------------------------------

inline bool CePlayUnitCharForbidden(unsigned char c)
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

inline void CePlayUnitSanitizeHudText(const char *src, char *out, unsigned room)
{
	if (!out || room == 0) return;
	out[0] = 0;
	if (!src) return;
	unsigned n = 0;
	bool prevSpace = true;
	for (const unsigned char *p = reinterpret_cast<const unsigned char *>(src); *p && n + 1 < room; ++p)
	{
		char ch = static_cast<char>(*p);
		if (CePlayUnitCharForbidden(*p)) ch = (*p == '_') ? ' ' : '-';
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

inline bool CePlayUnitContainsCI(const char *haystack, const char *needle)
{
	if (!haystack || !needle || !*needle) return false;
	for (const char *h = haystack; *h; ++h)
	{
		const char *a = h;
		const char *b = needle;
		while (*a && *b)
		{
			unsigned char ca = static_cast<unsigned char>(*a);
			unsigned char cb = static_cast<unsigned char>(*b);
			if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca - 'A' + 'a');
			if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb - 'A' + 'a');
			if (ca != cb) break;
			++a;
			++b;
		}
		if (!*b) return true;
	}
	return false;
}

// ---- 1st-person mesh-part barrel filter & weapon viewmodel animation ---------------------------------------

// Recognizes retail turret/vehicle gun barrel sub-meshes so first-person gunner view can
// render only the live animated barrel without clipping through hull or turret interior walls.
inline bool CePlayUnitPartIsBarrel(const char *partName)
{
	if (!partName || !*partName) return false;
	return CePlayUnitContainsCI(partName, "barrel")
		|| CePlayUnitContainsCI(partName, "cannon")
		|| CePlayUnitContainsCI(partName, "blaster")
		|| CePlayUnitContainsCI(partName, "gun-lid")
		|| CePlayUnitContainsCI(partName, "gun-inside")
		|| CePlayUnitContainsCI(partName, "gun-base")
		|| CePlayUnitContainsCI(partName, "firer")
		|| CePlayUnitContainsCI(partName, "muzzle");
}

// During the dedicated first-person barrel pass, skip every non-barrel mesh part.
// Outside that pass, never skip anything.
inline bool CePlayUnitFirstPersonShouldSkipPart(bool barrelPassActive, const char *partName)
{
	if (!barrelPassActive) return false;
	return !CePlayUnitPartIsBarrel(partName);
}

// Gunner/commander-hatch camera framing offset in unit-local space (x right, y forward, z down)
// tuned so the animated barrel sits cleanly below the centre reticle without interior clipping.
inline void CePlayUnitFirstPersonEyeAdjust(int classId, float span, float outOffset[3])
{
	outOffset[0] = 0.f;
	outOffset[1] = 0.f;
	outOffset[2] = 0.f;
	if (!isfinite(span) || !(span > 0.f)) return;
	switch (classId)
	{
	case CE_PLAYUNIT_CLASS_TANK:
		outOffset[1] = 0.04f * span;
		outOffset[2] = -0.14f * span;	// slightly above turret roof / hatch so barrel projects cleanly below reticle
		break;
	case CE_PLAYUNIT_CLASS_ARTILLERY:
		outOffset[1] = 0.02f * span;
		outOffset[2] = -0.18f * span;	// commander cupola vantage above elevated cannon
		break;
	case CE_PLAYUNIT_CLASS_TURRET:
		outOffset[1] = -0.14f * span;
		outOffset[2] = -0.10f * span;	// gunner sight right behind gun mount so barrel projects cleanly below reticle
		break;
	case CE_PLAYUNIT_CLASS_NAVAL:
		outOffset[1] = 0.06f * span;
		outOffset[2] = -0.18f * span;	// bridge / gun deck overlook
		break;
	case CE_PLAYUNIT_CLASS_AIRCRAFT:
		outOffset[1] = 0.22f * span;	// forward cockpit/nose vantage
		outOffset[2] = -0.06f * span;
		break;
	default:
		break;
	}
}

struct CePlayUnitWeaponViewState
{
	float bobPhase;
	float bobAmp;
	float recoil;
	float flash;
	float swayX;
	float swayY;
};

inline void CePlayUnitWeaponViewReset(CePlayUnitWeaponViewState *s)
{
	if (!s) return;
	memset(s, 0, sizeof(*s));
}

inline void CePlayUnitWeaponViewStep(CePlayUnitWeaponViewState *s, float dt, float speedUps, float yawDelta, bool fired)
{
	if (!s) return;
	if (!isfinite(dt) || dt < 0.f) dt = 0.f;
	if (dt > 0.1f) dt = 0.1f;
	if (!isfinite(speedUps) || speedUps < 0.f) speedUps = 0.f;
	if (!isfinite(yawDelta)) yawDelta = 0.f;

	const float moveNorm = speedUps > 4.f ? 1.f : (speedUps / 4.f);
	const float targetAmp = moveNorm > 0.08f ? moveNorm : 0.f;
	const float kAmp = dt * 8.f > 1.f ? 1.f : dt * 8.f;
	s->bobAmp += (targetAmp - s->bobAmp) * kAmp;
	s->bobPhase += dt * (2.2f + 7.5f * moveNorm);
	if (s->bobPhase > 6.2831853f) s->bobPhase -= 6.2831853f;

	if (fired)
	{
		s->recoil = 1.f;
		s->flash = 1.f;
	}
	else
	{
		s->recoil -= dt * 6.5f;
		if (s->recoil < 0.f) s->recoil = 0.f;
		s->flash -= dt * 14.f;
		if (s->flash < 0.f) s->flash = 0.f;
	}

	const float targetSwayX = CePlayUnitClamp(yawDelta * 0.45f, -0.055f, 0.055f);
	const float kSway = dt * 10.f > 1.f ? 1.f : dt * 10.f;
	s->swayX += (targetSwayX - s->swayX) * kSway;
	s->swayY = sinf(s->bobPhase * 2.f) * 0.014f * s->bobAmp;
}

// ---- artillery 3D ballistic arc solver (exact BEA tick physics) --------------------------------------------
// Matches retail CRound::Init/Gravity & CWeapon::Unknown507ab0:
//   GAME_FR = 20 Hz, launch speed per tick = roundSpeed / 20.0, gravity per tick = roundGravity * 0.025 (down = +Z).

static const int kCePlayUnitArcMaxPoints = 64;

struct CePlayUnitBallisticPoint
{
	float x, y, z;
};

struct CePlayUnitBallisticArc
{
	bool valid;
	bool hitsGround;
	int count;
	CePlayUnitBallisticPoint pts[kCePlayUnitArcMaxPoints];
	float impact[3];
	float rangeMeters;
	float flightSeconds;
	float elevationDeg;
	float azimuthDeg;
};

inline float CePlayUnitHeadingDegFromYaw(float yaw)
{
	if (!isfinite(yaw)) return 0.f;
	const float rad = remainderf(-yaw, 6.283185307179586f);
	if (!isfinite(rad)) return 0.f;
	float deg = rad * (180.f / 3.14159265f);
	if (deg < 0.f) deg += 360.f;
	if (deg >= 360.f) deg -= 360.f;
	return (deg >= 0.f && deg < 360.f) ? deg : 0.f;
}

inline bool CePlayUnitBallisticSolve(const float muzzle[3], float yaw, float pitch,
                                     float roundSpeedPerSec, float roundGravity, float lifeSpanSec,
                                     float waterLevel, CePlayUnitHeightFn height, void *ctx,
                                     CePlayUnitBallisticArc *out)
{
	if (!out) return false;
	memset(out, 0, sizeof(*out));
	if (!muzzle || !height || !CePlayUnitFinite3(muzzle) || !isfinite(yaw) || !isfinite(pitch)) return false;
	if (!isfinite(roundSpeedPerSec) || !(roundSpeedPerSec > 0.1f)) return false;
	if (!isfinite(roundGravity) || roundGravity < 0.f) return false;
	if (!isfinite(lifeSpanSec) || !(lifeSpanSec > 0.05f)) lifeSpanSec = 10.f;
	if (!isfinite(waterLevel)) waterLevel = 1e6f;

	const float v0 = roundSpeedPerSec / 20.f;
	const float g = roundGravity * 0.025f;
	const float sy = sinf(yaw), cy = cosf(yaw);
	const float sp = sinf(pitch), cp = cosf(pitch);

	float vx = -sy * cp * v0;
	float vy =  cy * cp * v0;
	float vz =  sp * v0;

	int maxTicks = static_cast<int>(lifeSpanSec * 20.f + 0.5f);
	if (maxTicks < 4) maxTicks = 4;
	if (maxTicks > 400) maxTicks = 400;

	CePlayUnitBallisticPoint raw[402];
	int rawCount = 0;
	float px = muzzle[0], py = muzzle[1], pz = muzzle[2];
	raw[rawCount++] = {px, py, pz};

	bool hit = false;
	int hitTicks = maxTicks;
	float hitFrac = 1.f;
	float impact[3] = {px, py, pz};

	for (int tick = 1; tick <= maxTicks; ++tick)
	{
		vz += g;
		const float nx = px + vx;
		const float ny = py + vy;
		const float nz = pz + vz;
		if (!isfinite(nx) || !isfinite(ny) || !isfinite(nz)) break;

		const float ground = height(ctx, nx, ny);
		const float surface = (isfinite(ground) && ground < waterLevel) ? ground : waterLevel;
		if (nz >= surface)
		{
			// Refine impact point along [0, 1] of this tick segment
			float lo = 0.f, hi = 1.f;
			for (int iter = 0; iter < 8; ++iter)
			{
				const float mid = 0.5f * (lo + hi);
				const float mx = px + vx * mid;
				const float my = py + vy * mid;
				const float mz = pz + vz * mid;
				const float mg = height(ctx, mx, my);
				const float ms = (isfinite(mg) && mg < waterLevel) ? mg : waterLevel;
				if (mz >= ms) hi = mid;
				else lo = mid;
			}
			impact[0] = px + vx * hi;
			impact[1] = py + vy * hi;
			const float ig = height(ctx, impact[0], impact[1]);
			impact[2] = (isfinite(ig) && ig < waterLevel) ? ig : waterLevel;
			raw[rawCount++] = {impact[0], impact[1], impact[2]};
			hit = true;
			hitTicks = tick - 1;
			hitFrac = hi;
			break;
		}
		px = nx; py = ny; pz = nz;
		raw[rawCount++] = {px, py, pz};
	}

	if (!hit)
	{
		impact[0] = px; impact[1] = py; impact[2] = pz;
	}
	if (rawCount < 2) return false;

	// Downsample cleanly into up to kCePlayUnitArcMaxPoints points while keeping exact start & end.
	if (rawCount <= kCePlayUnitArcMaxPoints)
	{
		out->count = rawCount;
		for (int i = 0; i < rawCount; ++i) out->pts[i] = raw[i];
	}
	else
	{
		out->count = kCePlayUnitArcMaxPoints;
		for (int i = 0; i < kCePlayUnitArcMaxPoints; ++i)
		{
			const int srcIdx = (i == kCePlayUnitArcMaxPoints - 1)
				? (rawCount - 1)
				: (i * (rawCount - 1)) / (kCePlayUnitArcMaxPoints - 1);
			out->pts[i] = raw[srcIdx];
		}
	}

	out->valid = true;
	out->hitsGround = hit;
	out->impact[0] = impact[0];
	out->impact[1] = impact[1];
	out->impact[2] = impact[2];
	const float dx = impact[0] - muzzle[0], dy = impact[1] - muzzle[1];
	out->rangeMeters = sqrtf(dx * dx + dy * dy);
	out->flightSeconds = (float(hitTicks) + hitFrac) * 0.05f;
	out->elevationDeg = -pitch * (180.f / 3.14159265f);
	out->azimuthDeg = CePlayUnitHeadingDegFromYaw(yaw);
	return true;
}

// Exact vertical error (zReached - dz, +Z down) of the 20 Hz semi-implicit Euler integrator used by CePlayUnitBallisticSolve.
// Positive means the shell arrives lower (short) than target; negative means higher (passes above target).
inline float CePlayUnitBallisticDiscreteZErr(float pitch, float dist, float dz, float v, float g)
{
	const float cp = cosf(pitch);
	if (!(cp > 1e-4f)) return 1e30f;
	const float T = dist / (v * cp);
	if (!isfinite(T) || !(T > 0.f) || T > 400.f) return 1e30f;
	const float vz0 = sinf(pitch) * v;
	const float n0f = floorf(T);
	const float frac = T - n0f;
	const float zReached = n0f * vz0 + g * n0f * (n0f + 1.f) * 0.5f + frac * (vz0 + (n0f + 1.f) * g);
	return zReached - dz;
}

// Computes launch pitch (negative = up, BEA convention) to strike target from muzzle under retail 20 Hz ballistic gravity.
// Refines the continuous analytic solution against the exact discrete tick recurrence and returns false when the target is
// unreachable (out of range or outside [minPitch, maxPitch]) while still populating *outPitch with the best clamped pitch.
inline bool CePlayUnitBallisticPitchForTarget(const float muzzle[3], const float target[3],
                                              float roundSpeedPerSec, float roundGravity,
                                              float minPitch, float maxPitch, bool highArc,
                                              float *outPitch)
{
	if (!muzzle || !target || !outPitch || !CePlayUnitFinite3(muzzle) || !CePlayUnitFinite3(target)) return false;
	if (!isfinite(roundSpeedPerSec) || !(roundSpeedPerSec > 0.1f)) return false;
	if (!isfinite(roundGravity) || roundGravity < 0.f) return false;
	if (!isfinite(minPitch) || !isfinite(maxPitch) || minPitch > maxPitch) return false;

	const float dx = target[0] - muzzle[0], dy = target[1] - muzzle[1];
	const float dist = sqrtf(dx * dx + dy * dy);
	const float dz = target[2] - muzzle[2];		// positive = target is lower than muzzle
	if (!(dist > 0.1f))
	{
		*outPitch = CePlayUnitClamp(0.f, minPitch, maxPitch);
		return true;
	}
	if (roundGravity <= 1e-5f)
	{
		const float direct = atan2f(dz, dist);
		*outPitch = CePlayUnitClamp(direct, minPitch, maxPitch);
		return direct >= minPitch && direct <= maxPitch;
	}

	const float v = roundSpeedPerSec / 20.f;
	const float g = roundGravity * 0.025f;
	const float loLim = minPitch > -1.52f ? minPitch : -1.52f;
	const float hiLim = maxPitch < 1.52f ? maxPitch : 1.52f;
	if (loLim > hiLim) return false;

	// Locate the discrete optimal-elevation pitch (minimum of the convex zReached - dz curve) inside [loLim, hiLim].
	const float invPhi = 0.61803399f;
	float ga = loLim, gb = hiLim;
	float gc = gb - (gb - ga) * invPhi;
	float gd = ga + (gb - ga) * invPhi;
	float fc = CePlayUnitBallisticDiscreteZErr(gc, dist, dz, v, g);
	float fd = CePlayUnitBallisticDiscreteZErr(gd, dist, dz, v, g);
	for (int iter = 0; iter < 14; ++iter)
	{
		if (fc <= fd)
		{
			gb = gd; gd = gc; fd = fc;
			gc = gb - (gb - ga) * invPhi;
			fc = CePlayUnitBallisticDiscreteZErr(gc, dist, dz, v, g);
		}
		else
		{
			ga = gc; gc = gd; fc = fd;
			gd = ga + (gb - ga) * invPhi;
			fd = CePlayUnitBallisticDiscreteZErr(gd, dist, dz, v, g);
		}
	}
	const float pPeak = (fc <= fd) ? gc : gd;
	const float ePeak = (fc <= fd) ? fc : fd;
	if (!isfinite(ePeak) || ePeak > 0.05f)
	{
		*outPitch = CePlayUnitClamp(pPeak, minPitch, maxPitch);
		return false;
	}
	if (ePeak >= -0.05f)
	{
		*outPitch = CePlayUnitClamp(pPeak, minPitch, maxPitch);
		return true;
	}

	// Set up guaranteed sign-changing bracket for the requested branch (lowArc: [pPeak, hiLim], highArc: [loLim, pPeak]).
	float a = highArc ? loLim : pPeak;
	float b = highArc ? pPeak : hiLim;
	float fa = highArc ? CePlayUnitBallisticDiscreteZErr(a, dist, dz, v, g) : ePeak;
	float fb = highArc ? ePeak : CePlayUnitBallisticDiscreteZErr(b, dist, dz, v, g);
	if (highArc && fa < 0.f)
	{
		*outPitch = CePlayUnitClamp(loLim, minPitch, maxPitch);
		return false;
	}
	if (!highArc && fb < 0.f)
	{
		*outPitch = CePlayUnitClamp(hiLim, minPitch, maxPitch);
		return false;
	}

	// Seed with continuous analytic solution if inside the bracket, then refine via safeguarded secant + bisection.
	const float v2 = v * v;
	const float v4 = v2 * v2;
	const float h = -dz;
	const float disc = v4 - g * (g * dist * dist + 2.f * h * v2);
	float p = 0.5f * (a + b);
	if (disc >= 0.f)
	{
		const float root = sqrtf(disc);
		const float tanTheta = highArc ? ((v2 + root) / (g * dist)) : ((v2 - root) / (g * dist));
		const float guess = -atanf(tanTheta);
		if (guess > a && guess < b) p = guess;
	}
	float fp = CePlayUnitBallisticDiscreteZErr(p, dist, dz, v, g);
	if ((fa <= 0.f && fp <= 0.f) || (fa > 0.f && fp > 0.f)) { a = p; fa = fp; }
	else { b = p; fb = fp; }

	// Seed a nearby second point so secant converges quadratically near the analytic guess, safeguarded by [a, b].
	float step0 = (b - a) * 0.02f;
	if (step0 > 0.01f) step0 = 0.01f;
	float prevP = p + ((fp > 0.f) ? -step0 : step0);
	if (!(prevP > a && prevP < b)) prevP = 0.5f * (a + b);
	float prevF = CePlayUnitBallisticDiscreteZErr(prevP, dist, dz, v, g);
	if ((fa <= 0.f && prevF <= 0.f) || (fa > 0.f && prevF > 0.f)) { a = prevP; fa = prevF; }
	else { b = prevP; fb = prevF; }

	for (int iter = 0; iter < 12; ++iter)
	{
		if (fabsf(fp) < 1e-4f || fabsf(b - a) < 1e-6f) break;
		float next = 0.5f * (a + b);
		if (isfinite(fp) && isfinite(prevF) && fabsf(fp - prevF) > 1e-7f)
		{
			const float cand = p - fp * (p - prevP) / (fp - prevF);
			if (cand > a && cand < b) next = cand;
		}
		prevP = p; prevF = fp;
		p = next;
		fp = CePlayUnitBallisticDiscreteZErr(p, dist, dz, v, g);
		if ((fa <= 0.f && fp <= 0.f) || (fa > 0.f && fp > 0.f)) { a = p; fa = fp; }
		else { b = p; fb = fp; }
	}
	*outPitch = CePlayUnitClamp(p, minPitch, maxPitch);
	return true;
}

// Verifies whether a ballistic indirect-fire arc from muzzle actually clears intervening terrain ridges and impacts near target.
inline bool CePlayUnitBallisticArcClearsRidge(const float muzzle[3], const float target[3],
                                             float roundSpeedPerSec, float roundGravity, float lifeSpanSec,
                                             float waterLevel, CePlayUnitHeightFn height, void *ctx,
                                             float minPitch, float maxPitch, bool highArc,
                                             CePlayUnitBallisticArc *outArc = NULL)
{
	float pitch = 0.f;
	if (!CePlayUnitBallisticPitchForTarget(muzzle, target, roundSpeedPerSec, roundGravity, minPitch, maxPitch, highArc, &pitch))
		return false;
	const float dx = target[0] - muzzle[0], dy = target[1] - muzzle[1];
	const float dist = sqrtf(dx * dx + dy * dy);
	const float yaw = atan2f(-dx, dy);
	CePlayUnitBallisticArc localArc;
	CePlayUnitBallisticArc *arc = outArc ? outArc : &localArc;
	if (!CePlayUnitBallisticSolve(muzzle, yaw, pitch, roundSpeedPerSec, roundGravity, lifeSpanSec, waterLevel, height, ctx, arc))
		return false;
	if (!arc->valid || !arc->hitsGround)
		return false;
	const float ex = arc->impact[0] - target[0];
	const float ey = arc->impact[1] - target[1];
	const float horizErr = sqrtf(ex * ex + ey * ey);
	const float tol = dist * 0.12f > 4.5f ? (dist * 0.12f) : 4.5f;
	return horizErr <= tol;
}

// ---- artillery aerial spotting / shot-follow camera state machine ------------------------------------------

enum CePlayUnitSpotterMode
{
	CE_SPOTTER_OFF = 0,
	CE_SPOTTER_OVERWATCH = 1,
	CE_SPOTTER_FOLLOW_SHELL = 2,
	CE_SPOTTER_HOLD_IMPACT = 3
};

struct CePlayUnitSpotterState
{
	bool enabled;
	int mode;
	float shellPos[3];
	float shellVel[3];
	float impactPos[3];
	float holdTimer;
	bool hasShell;
	bool hasImpact;
};

static const float kCeSpotterHoldSeconds = 2.2f;

inline void CePlayUnitSpotterReset(CePlayUnitSpotterState *st)
{
	if (!st) return;
	memset(st, 0, sizeof(*st));
}

inline bool CePlayUnitSpotterToggle(CePlayUnitSpotterState *st, bool hasPredictedImpact, const float predictedImpact[3])
{
	if (!st) return false;
	st->enabled = !st->enabled;
	st->holdTimer = 0.f;
	st->hasShell = false;
	st->hasImpact = false;
	if (!st->enabled)
	{
		st->mode = CE_SPOTTER_OFF;
		return false;
	}
	if (hasPredictedImpact && predictedImpact && CePlayUnitFinite3(predictedImpact))
	{
		st->impactPos[0] = predictedImpact[0];
		st->impactPos[1] = predictedImpact[1];
		st->impactPos[2] = predictedImpact[2];
		st->hasImpact = true;
	}
	st->mode = CE_SPOTTER_OVERWATCH;
	return true;
}

inline void CePlayUnitSpotterOnShellLaunch(CePlayUnitSpotterState *st, const float shellPos[3], const float shellVel[3])
{
	if (!st || !shellPos || !CePlayUnitFinite3(shellPos)) return;
	st->shellPos[0] = shellPos[0];
	st->shellPos[1] = shellPos[1];
	st->shellPos[2] = shellPos[2];
	if (shellVel && CePlayUnitFinite3(shellVel))
	{
		st->shellVel[0] = shellVel[0];
		st->shellVel[1] = shellVel[1];
		st->shellVel[2] = shellVel[2];
	}
	st->hasShell = true;
	if (st->enabled)
	{
		st->mode = CE_SPOTTER_FOLLOW_SHELL;
		st->holdTimer = 0.f;
	}
}

inline void CePlayUnitSpotterUpdate(CePlayUnitSpotterState *st, float dt,
                                    bool shellAlive, const float shellPos[3], const float shellVel[3],
                                    bool hasPredictedImpact, const float predictedImpact[3])
{
	if (!st) return;
	if (!isfinite(dt) || dt < 0.f) dt = 0.f;
	if (dt > 0.25f) dt = 0.25f;
	if (!st->enabled)
	{
		st->mode = CE_SPOTTER_OFF;
		st->hasShell = false;
		st->holdTimer = 0.f;
		return;
	}
	if (shellAlive && shellPos && CePlayUnitFinite3(shellPos))
	{
		st->shellPos[0] = shellPos[0];
		st->shellPos[1] = shellPos[1];
		st->shellPos[2] = shellPos[2];
		if (shellVel && CePlayUnitFinite3(shellVel))
		{
			st->shellVel[0] = shellVel[0];
			st->shellVel[1] = shellVel[1];
			st->shellVel[2] = shellVel[2];
		}
		st->hasShell = true;
		st->mode = CE_SPOTTER_FOLLOW_SHELL;
		st->holdTimer = 0.f;
		return;
	}
	if (st->hasShell)
	{
		// Shell just impacted or expired: latch its last known point for impact spotting hold.
		st->hasShell = false;
		st->impactPos[0] = st->shellPos[0];
		st->impactPos[1] = st->shellPos[1];
		st->impactPos[2] = st->shellPos[2];
		st->hasImpact = true;
		st->mode = CE_SPOTTER_HOLD_IMPACT;
		st->holdTimer = kCeSpotterHoldSeconds;
		return;
	}
	if (st->mode == CE_SPOTTER_HOLD_IMPACT)
	{
		st->holdTimer -= dt;
		if (st->holdTimer <= 0.f)
		{
			st->holdTimer = 0.f;
			st->mode = CE_SPOTTER_OVERWATCH;
		}
		return;
	}
	st->mode = CE_SPOTTER_OVERWATCH;
	if (hasPredictedImpact && predictedImpact && CePlayUnitFinite3(predictedImpact))
	{
		st->impactPos[0] = predictedImpact[0];
		st->impactPos[1] = predictedImpact[1];
		st->impactPos[2] = predictedImpact[2];
		st->hasImpact = true;
	}
}

inline bool CePlayUnitSpotterCameraPose(const CePlayUnitSpotterState &st, const float unitPos[3],
                                        float lookYaw, float terrainAtFocus,
                                        float outEye[3], float outRight[3], float outForward[3], float outDown[3],
                                        CePlayUnitHeightFn terrainFn = NULL, void *terrainCtx = NULL)
{
	if (!st.enabled || st.mode == CE_SPOTTER_OFF || !unitPos || !CePlayUnitFinite3(unitPos) || !isfinite(lookYaw)) return false;
	if (!outEye || !outRight || !outForward || !outDown) return false;

	float focus[3] = {unitPos[0], unitPos[1], unitPos[2]};
	float backDist = 36.f;
	float liftHeight = 42.f;

	if (st.mode == CE_SPOTTER_FOLLOW_SHELL && st.hasShell)
	{
		focus[0] = st.shellPos[0];
		focus[1] = st.shellPos[1];
		focus[2] = st.shellPos[2];
		backDist = 18.f;
		liftHeight = 14.f;
	}
	else if (st.hasImpact)
	{
		const float dx = st.impactPos[0] - unitPos[0];
		const float dy = st.impactPos[1] - unitPos[1];
		const float range = sqrtf(dx * dx + dy * dy);
		// Smoothly transition between battery+impact overwatch framing (near/mid range) and impact-centered framing (long range)
		// with smoothstep ramps on [4..10] and [105..165] so crossing 125u never causes a jarring camera cut.
		float overwatchWeight = 0.f;
		if (st.mode == CE_SPOTTER_OVERWATCH)
		{
			if (range <= 4.f || range >= 165.f) overwatchWeight = 0.f;
			else if (range < 10.f)
			{
				const float u = (range - 4.f) / 6.f;
				overwatchWeight = u * u * (3.f - 2.f * u);
			}
			else if (range <= 105.f) overwatchWeight = 1.f;
			else
			{
				const float u = (165.f - range) / 60.f;
				overwatchWeight = u * u * (3.f - 2.f * u);
			}
		}
		const float ovFocusX = unitPos[0] + dx * 0.60f;
		const float ovFocusY = unitPos[1] + dy * 0.60f;
		const float ovFocusZ = isfinite(terrainAtFocus) ? terrainAtFocus : st.impactPos[2];
		float ovBack = 0.74f * range + 14.f;
		if (ovBack < 36.f) ovBack = 36.f;
		float ovLift = 0.52f * range + 24.f;
		if (ovLift < 40.f) ovLift = 40.f;

		const float farFocusX = st.impactPos[0];
		const float farFocusY = st.impactPos[1];
		const float farFocusZ = st.impactPos[2];
		const float farBack = 34.f;
		const float farLift = 40.f;

		focus[0] = farFocusX + (ovFocusX - farFocusX) * overwatchWeight;
		focus[1] = farFocusY + (ovFocusY - farFocusY) * overwatchWeight;
		focus[2] = farFocusZ + (ovFocusZ - farFocusZ) * overwatchWeight;
		backDist = farBack + (ovBack - farBack) * overwatchWeight;
		liftHeight = farLift + (ovLift - farLift) * overwatchWeight;
	}
	else
	{
		const float fx = -sinf(lookYaw), fy = cosf(lookYaw);
		focus[0] = unitPos[0] + fx * 65.f;
		focus[1] = unitPos[1] + fy * 65.f;
		focus[2] = isfinite(terrainAtFocus) ? terrainAtFocus : unitPos[2];
	}

	// Place spotter camera behind focus along the firing bearing from the artillery battery.
	float bx = focus[0] - unitPos[0], by = focus[1] - unitPos[1];
	float bLen = sqrtf(bx * bx + by * by);
	if (bLen > 1.f) { bx /= bLen; by /= bLen; }
	else { bx = -sinf(lookYaw); by = cosf(lookYaw); }

	outEye[0] = focus[0] - bx * backDist;
	outEye[1] = focus[1] - by * backDist;
	outEye[2] = focus[2] - liftHeight;
	float eyeFloor = terrainAtFocus;
	if (terrainFn)
	{
		const float terrainAtEye = terrainFn(terrainCtx, outEye[0], outEye[1]);
		if (isfinite(terrainAtEye) && (!isfinite(eyeFloor) || terrainAtEye < eyeFloor))
			eyeFloor = terrainAtEye;
	}
	if (isfinite(eyeFloor) && outEye[2] > eyeFloor - 12.f)
		outEye[2] = eyeFloor - 12.f;

	float fx = focus[0] - outEye[0], fy = focus[1] - outEye[1], fz = focus[2] - outEye[2];
	const float flen = sqrtf(fx * fx + fy * fy + fz * fz);
	if (!(flen > 1e-4f)) return false;
	outForward[0] = fx / flen;
	outForward[1] = fy / flen;
	outForward[2] = fz / flen;

	// BEA camera basis: right x forward = down (Z-down world, worldDown = (0, 0, +1)).
	// Horizontal right = forward x worldDown = (+fy, -fx, 0), ensuring outDown.z = +(fx^2 + fy^2)/rlen > 0 (true down).
	float rx = outForward[1], ry = -outForward[0];
	const float rlen = sqrtf(rx * rx + ry * ry);
	if (rlen > 1e-4f) { outRight[0] = rx / rlen; outRight[1] = ry / rlen; outRight[2] = 0.f; }
	else { outRight[0] = cosf(lookYaw); outRight[1] = sinf(lookYaw); outRight[2] = 0.f; }

	outDown[0] = outRight[1] * outForward[2] - outRight[2] * outForward[1];
	outDown[1] = outRight[2] * outForward[0] - outRight[0] * outForward[2];
	outDown[2] = outRight[0] * outForward[1] - outRight[1] * outForward[0];
	return true;
}

// ---- aircraft flight control law ---------------------------------------------------------------------------

struct CePlayUnitFlightState
{
	bool initialized;
	float yaw;
	float pitch;
	float roll;
	float throttle;		// 0..1 (1.35 on boost)
	float speedUps;		// world units per second
};

struct CePlayUnitFlightParams
{
	float maxSpeedUps;
	float minSpeedUps;
	float cruiseThrottle;
	float boostScale;
	float yawRate;
	float pitchRate;
	float maxPitchUp;		// negative radians (up)
	float maxPitchDown;		// positive radians (down)
	float maxBank;			// radians
	float minClearance;		// minimum world units above ground/water
	bool canHover;
};

inline CePlayUnitFlightParams CePlayUnitFighterParams()
{
	const CePlayUnitFlightParams p = {24.f, 8.f, 0.72f, 1.35f, 1.9f, 1.6f, -0.78f, 0.72f, 0.95f, 8.5f, false};
	return p;
}

inline CePlayUnitFlightParams CePlayUnitBomberParams()
{
	const CePlayUnitFlightParams p = {16.f, 6.f, 0.70f, 1.25f, 1.25f, 1.1f, -0.52f, 0.50f, 0.65f, 11.f, false};
	return p;
}

inline CePlayUnitFlightParams CePlayUnitDropshipParams()
{
	const CePlayUnitFlightParams p = {15.f, 0.f, 0.45f, 1.25f, 1.35f, 1.0f, -0.45f, 0.45f, 0.42f, 3.8f, true};
	return p;
}

inline void CePlayUnitFlightReset(CePlayUnitFlightState *st, float initYaw, const CePlayUnitFlightParams &p)
{
	if (!st) return;
	st->initialized = true;
	st->yaw = CePlayUnitWrap(initYaw);
	st->pitch = 0.f;
	st->roll = 0.f;
	st->throttle = p.cruiseThrottle;
	st->speedUps = p.minSpeedUps + (p.maxSpeedUps - p.minSpeedUps) * p.cruiseThrottle;
}

// Advances aircraft attitude & velocity for one simulation step (dt seconds).
// outVel is in world units per tick (dt = 0.05s -> velPerTick = speedUps * dt).
inline void CePlayUnitFlightStep(CePlayUnitFlightState *st, const CePlayUnitFlightParams &p, float dt,
                                 float forwardIn, float backIn, float turnIn,
                                 float targetYaw, float targetPitch, bool boostHeld,
                                 float currentZ, float surfaceZ, float outVelTick[3])
{
	if (!st || !outVelTick) return;
	outVelTick[0] = outVelTick[1] = outVelTick[2] = 0.f;
	if (!isfinite(dt) || !(dt > 0.f)) dt = 0.05f;
	if (dt > 0.1f) dt = 0.1f;
	if (!st->initialized) CePlayUnitFlightReset(st, targetYaw, p);

	const float fwd = CePlayUnitClamp(forwardIn, 0.f, 1.f);
	const float bak = CePlayUnitClamp(backIn, 0.f, 1.f);
	const float trn = CePlayUnitClamp(turnIn, -1.f, 1.f);

	// Throttle adjustment: W increases, S decreases; dropship can hover at 0.
	if (fwd > 0.05f) st->throttle += fwd * dt * 0.9f;
	else if (bak > 0.05f) st->throttle -= bak * dt * 0.9f;
	else if (p.canHover && st->throttle < 0.08f) st->throttle = 0.f;
	st->throttle = CePlayUnitClamp(st->throttle, p.canHover ? 0.f : 0.15f, 1.f);

	// Desired heading combines mouse look targetYaw and explicit A/D roll-turn input.
	float desiredYaw = CePlayUnitWrap(targetYaw - trn * p.yawRate * 0.45f);
	const float yawDiff = CePlayUnitWrap(desiredYaw - st->yaw);
	const float maxYawStep = p.yawRate * dt;
	const float yawStep = CePlayUnitClamp(yawDiff, -maxYawStep, maxYawStep);
	st->yaw = CePlayUnitWrap(st->yaw + yawStep);

	// Bank cleanly into turns: in BEA's left-handed +Z-down frame, negative roll dips the right wing
	// (right turn -> yawStep < 0 -> turnRatio < 0, and trn > 0 -> -trn < 0; both terms agree).
	const float turnRatio = maxYawStep > 1e-5f ? (yawStep / maxYawStep) : 0.f;
	const float desiredRoll = CePlayUnitClamp(turnRatio * p.maxBank - trn * p.maxBank * 0.35f, -p.maxBank, p.maxBank);
	const float kRoll = dt * 6.f > 1.f ? 1.f : dt * 6.f;
	st->roll += (desiredRoll - st->roll) * kRoll;

	// Pitch tracks look pitch, guarded smoothly above terrain/water floor so planes never insta-explode.
	float desiredPitch = CePlayUnitClamp(targetPitch, p.maxPitchUp, p.maxPitchDown);
	const float clearance = isfinite(surfaceZ) && isfinite(currentZ) ? (surfaceZ - currentZ) : 50.f;
	if (clearance < p.minClearance + 4.f)
	{
		if (clearance < p.minClearance)
			desiredPitch = p.maxPitchUp * 0.85f;	// strong smooth automatic pull-up
		else if (desiredPitch > 0.f)
			desiredPitch = 0.f;						// level off near terrain floor
	}
	const float pitchDiff = desiredPitch - st->pitch;
	const float maxPitchStep = p.pitchRate * dt;
	st->pitch += CePlayUnitClamp(pitchDiff, -maxPitchStep, maxPitchStep);

	const float boostMult = boostHeld ? p.boostScale : 1.f;
	const float targetSpeed = (p.minSpeedUps + (p.maxSpeedUps - p.minSpeedUps) * st->throttle) * boostMult;
	const float kSpeed = dt * 4.5f > 1.f ? 1.f : dt * 4.5f;
	st->speedUps += (targetSpeed - st->speedUps) * kSpeed;

	const float sy = sinf(st->yaw), cy = cosf(st->yaw);
	const float sp = sinf(st->pitch), cp = cosf(st->pitch);
	const float perTick = st->speedUps * dt;
	outVelTick[0] = -sy * cp * perTick;
	outVelTick[1] =  cy * cp * perTick;
	outVelTick[2] =  sp * perTick;

	// Safety floor with bounded vertical pull-up so cliffs never teleport the aircraft in one tick.
	if (isfinite(surfaceZ) && isfinite(currentZ))
	{
		const float floorZ = surfaceZ - p.minClearance;
		if (currentZ + outVelTick[2] > floorZ)
		{
			float needed = floorZ - currentZ;
			const float maxClimbTick = (perTick * 1.5f > 2.0f) ? (perTick * 1.5f) : 2.0f;
			if (needed < -maxClimbTick) needed = -maxClimbTick;
			outVelTick[2] = needed;
		}
	}
}

// ---- naval & transport beach-unload state machine ----------------------------------------------------------

enum CePlayUnitNavalUnloadStatus
{
	CE_NAVAL_NO_CARGO = 0,
	CE_NAVAL_AT_SEA = 1,
	CE_NAVAL_READY_UNLOAD = 2,
	CE_NAVAL_DEPLOYING = 3,
	CE_NAVAL_DEPLOYED = 4
};

// Determines landing craft unload readiness from live cargo, retail animation/landed flags,
// and shallow-water / beach ground height at the bow (BEA Z points down: land has bowGroundZ <= waterZ).
inline int CePlayUnitNavalEvaluateUnload(int cargoRemaining, bool deploying, bool landed, bool rampOpen,
                                         float bowGroundZ, float waterZ)
{
	if (cargoRemaining <= 0)
		return (landed || rampOpen) ? CE_NAVAL_DEPLOYED : CE_NAVAL_NO_CARGO;
	if (deploying && !rampOpen)
		return CE_NAVAL_DEPLOYING;
	if (landed || rampOpen)
		return CE_NAVAL_READY_UNLOAD;
	if (isfinite(bowGroundZ) && isfinite(waterZ) && bowGroundZ <= waterZ + 2.4f)
		return CE_NAVAL_READY_UNLOAD;
	return CE_NAVAL_AT_SEA;
}

// Determines dropship cargo drop readiness (embarked cargo + over land within drop altitude).
inline int CePlayUnitDropshipEvaluateUnload(int cargoRemaining, bool doorOpen, float altitudeAboveGround,
                                            float groundZ, float waterZ)
{
	if (cargoRemaining <= 0)
		return doorOpen ? CE_NAVAL_DEPLOYED : CE_NAVAL_NO_CARGO;
	const bool overLand = !isfinite(waterZ) || (isfinite(groundZ) && groundZ < waterZ - 0.1f);
	if (doorOpen || (overLand && isfinite(altitudeAboveGround) && altitudeAboveGround <= 22.f))
		return CE_NAVAL_READY_UNLOAD;
	return CE_NAVAL_AT_SEA;
}

// ---- faction-symmetric BEA class HUD palette & telemetry formatting ----------------------------------------

struct CePlayUnitHudPalette
{
	unsigned panelFill;
	unsigned panelBorder;
	unsigned bevelAccent;
	unsigned accentBright;
	unsigned accentDim;
	unsigned highlight;
	unsigned textBright;
	unsigned textMuted;
	unsigned warn;
	unsigned crit;
	unsigned ok;
	unsigned radarOutline;
	unsigned radarNorth;
	unsigned arcMain;
	unsigned arcImpact;
};

// allegiance == 1 -> Muspell (fiery amber/crimson, dark iron-charcoal); else Forseti (cool cyan/steel-blue, deep slate-navy).
inline CePlayUnitHudPalette CePlayUnitHudPaletteFor(int allegiance)
{
	CePlayUnitHudPalette p;
	if (allegiance == 1)
	{
		p.panelFill    = 0xe0160d0b;
		p.panelBorder  = 0xff7a3422;
		p.bevelAccent  = 0xd0ff8a30;
		p.accentBright = 0xffff9632;
		p.accentDim    = 0xff8a3a20;
		p.highlight    = 0xffffd878;
		p.textBright   = 0xfffef2e8;
		p.textMuted    = 0xffcfa896;
		p.warn         = 0xffffb830;
		p.crit         = 0xffff3b28;
		p.ok           = 0xff5ce070;
		p.radarOutline = 0xffe85a2e;
		p.radarNorth   = 0xffff9e38;
		p.arcMain      = 0xffff8828;
		p.arcImpact    = 0xffff3824;
	}
	else
	{
		p.panelFill    = 0xdc06101c;
		p.panelBorder  = 0xff2b5e7a;
		p.bevelAccent  = 0xb858d8f8;
		p.accentBright = 0xff46d4f4;
		p.accentDim    = 0xff23647c;
		p.highlight    = 0xfff6d365;
		p.textBright   = 0xfff2f6fa;
		p.textMuted    = 0xff9eb8cc;
		p.warn         = 0xfff0b429;
		p.crit         = 0xffe84848;
		p.ok           = 0xff46d96a;
		p.radarOutline = 0xff5ca8d8;
		p.radarNorth   = 0xff5fd4ff;
		p.arcMain      = 0xff48e0ff;
		p.arcImpact    = 0xfff6d365;
	}
	return p;
}

struct CePlayUnitHudTelemetry
{
	bool valid;
	int classId;
	int allegiance;			// 0 Forseti, 1 Muspell
	bool firstPerson;
	bool isDropship;
	bool isBomber;
	char unitName[48];
	char weaponName[36];
	int hullPct;
	int headingDeg;
	float speedUps;
	float altitude;
	float pitchDeg;
	float azDeg;
	float elDeg;
	float rangeMeters;
	bool arcOn;
	int spotterMode;		// CePlayUnitSpotterMode
	int deployState;		// 0 mobile, 3 deploying, 4 deployed, 5 undeploying
	int cargoCount;
	int unloadStatus;		// CePlayUnitNavalUnloadStatus
	int fireState;
	int stance;				// 0 idle/ready, 1 moving, 2 firing
};

inline int CePlayUnitSafeRoundInt(float v, int lo, int hi)
{
	if (!isfinite(v)) return 0;
	if (v < float(lo)) return lo;
	if (v > float(hi)) return hi;
	return int(v >= 0.f ? (v + 0.5f) : (v - 0.5f));
}

inline void CePlayUnitFormatClassHud(const CePlayUnitHudTelemetry &t,
                                     char headerOut[48], char line1Out[64],
                                     char line2Out[64], char actionOut[48])
{
	headerOut[0] = line1Out[0] = line2Out[0] = actionOut[0] = 0;
	char wpn[28];
	CePlayUnitSanitizeHudText(t.weaponName[0] ? t.weaponName : "PRIMARY", wpn, sizeof(wpn));
	if (strlen(wpn) > 22) wpn[22] = 0;
	const int hdg = ((t.headingDeg % 360) + 360) % 360;
	float azWrapped = isfinite(t.azDeg) ? remainderf(t.azDeg, 360.f) : 0.f;
	if (!isfinite(azWrapped)) azWrapped = 0.f;
	if (azWrapped < 0.f) azWrapped += 360.f;
	const int az = (CePlayUnitSafeRoundInt(azWrapped, 0, 360) % 360 + 360) % 360;
	const int el = CePlayUnitSafeRoundInt(t.elDeg, -90, 90);
	const int spd = CePlayUnitSafeRoundInt(t.speedUps > 0.f ? t.speedUps * 5.f : 0.f, 0, 9999);	// display knots/kph-like scale
	const int alt = CePlayUnitSafeRoundInt(t.altitude > 0.f ? t.altitude : 0.f, 0, 9999);
	const int pitch = CePlayUnitSafeRoundInt(t.pitchDeg, -90, 90);
	const int hull = t.hullPct < 0 ? 0 : (t.hullPct > 100 ? 100 : t.hullPct);

	switch (t.classId)
	{
	case CE_PLAYUNIT_CLASS_INFANTRY:
	{
		const char *stanceWord = t.stance == 2 ? "FIRING" : (t.stance == 1 ? "MOVING" : "READY");
		snprintf(headerOut, 48, "INFANTRY");
		snprintf(line1Out, 64, "WPN %s  HDG %03d", wpn, hdg);
		snprintf(line2Out, 64, "STANCE %s  HULL %d%%", stanceWord, hull);
		break;
	}
	case CE_PLAYUNIT_CLASS_TANK:
		snprintf(headerOut, 48, "ARMOUR");
		snprintf(line1Out, 64, "WPN %s  SPD %d", wpn, spd);
		snprintf(line2Out, 64, "AZ %03d  HDG %03d  HULL %d%%", az, hdg, hull);
		break;
	case CE_PLAYUNIT_CLASS_TURRET:
		snprintf(headerOut, 48, "TURRET");
		snprintf(line1Out, 64, "AZ %03d  EL %+d  WPN %s", az, el, wpn);
		snprintf(line2Out, 64, "MOUNT ONLINE  HULL %d%%", hull);
		break;
	case CE_PLAYUNIT_CLASS_AIRCRAFT:
		snprintf(headerOut, 48, t.isDropship ? "DROPSHIP" : (t.isBomber ? "BOMBER" : "AIRCRAFT"));
		snprintf(line1Out, 64, "SPD %d  ALT %dM  PIT %+d", spd, alt, pitch);
		if (t.isDropship)
		{
			snprintf(line2Out, 64, "CARGO %d  HULL %d%%", t.cargoCount, hull);
			if (t.unloadStatus == CE_NAVAL_READY_UNLOAD)
				snprintf(actionOut, 48, "U: UNLOAD CARGO");
			else if (t.cargoCount > 0)
				snprintf(actionOut, 48, "DESCEND OVER LAND TO UNLOAD");
		}
		else
		{
			snprintf(line2Out, 64, "PAYLOAD %s  HULL %d%%", wpn, hull);
			snprintf(actionOut, 48, "SHIFT: BOOST");
		}
		break;
	case CE_PLAYUNIT_CLASS_NAVAL:
	{
		const bool lander = t.cargoCount > 0 || t.unloadStatus != CE_NAVAL_NO_CARGO;
		snprintf(headerOut, 48, lander ? "LANDING CRAFT" : "NAVAL VESSEL");
		if (lander)
			snprintf(line1Out, 64, "HDG %03d  SPD %d  CARGO %d", hdg, spd, t.cargoCount);
		else
			snprintf(line1Out, 64, "HDG %03d  SPD %d  WPN %s", hdg, spd, wpn);
		const char *ramp = t.unloadStatus == CE_NAVAL_READY_UNLOAD ? "RAMP READY"
			: t.unloadStatus == CE_NAVAL_DEPLOYING ? "LOWERING RAMP"
			: t.unloadStatus == CE_NAVAL_DEPLOYED ? "CARGO DEPLOYED"
			: lander ? "AT SEA" : "ONLINE";
		snprintf(line2Out, 64, "%s  HULL %d%%", ramp, hull);
		if (t.unloadStatus == CE_NAVAL_READY_UNLOAD)
			snprintf(actionOut, 48, "U: UNLOAD");
		else if (lander && t.cargoCount > 0)
			snprintf(actionOut, 48, "APPROACH SHORE TO UNLOAD");
		break;
	}
	case CE_PLAYUNIT_CLASS_ARTILLERY:
	{
		const int rng = CePlayUnitSafeRoundInt(t.rangeMeters > 0.f ? t.rangeMeters : 0.f, 0, 99999);
		const char *spot = t.spotterMode == CE_SPOTTER_FOLLOW_SHELL ? "TRACK"
			: t.spotterMode == CE_SPOTTER_HOLD_IMPACT ? "IMPACT"
			: t.spotterMode == CE_SPOTTER_OVERWATCH ? "ON" : "OFF";
		snprintf(headerOut, 48, "ARTILLERY");
		snprintf(line1Out, 64, "AZ %03d  EL %+d  RNG %dM", az, el, rng);
		snprintf(line2Out, 64, "%s  SPOT %s  HULL %d%%", t.arcOn ? "ARC ON" : "ARC OFF", spot, hull);
		snprintf(actionOut, 48, "C: SPOT CAM");
		break;
	}
	default:
		snprintf(headerOut, 48, "UNIT");
		snprintf(line1Out, 64, "HDG %03d  SPD %d", hdg, spd);
		snprintf(line2Out, 64, "HULL %d%%", hull);
		break;
	}
}

// Multi-weapon fire selection: keep the current weapon while it is ready or mid-arming;
// otherwise select the first ready weapon so a recovering secondary never locks out a ready primary.
inline int CePlayUnitSelectReadyWeapon(int count, int currentIdx, bool armingCurrent, const bool *hasMode, const bool *ready)
{
	if (count <= 0 || !hasMode || !ready) return -1;
	if (currentIdx >= 0 && currentIdx < count && hasMode[currentIdx] && (ready[currentIdx] || armingCurrent))
		return currentIdx;
	for (int i = 0; i < count; ++i)
		if (hasMode[i] && ready[i])
			return i;
	if (currentIdx >= 0 && currentIdx < count && hasMode[currentIdx])
		return currentIdx;
	for (int i = 0; i < count; ++i)
		if (hasMode[i])
			return i;
	return -1;
}

inline const char *CePlayUnitControlVerb(int classId)
{
	switch (classId)
	{
	case CE_PLAYUNIT_CLASS_INFANTRY: return "On foot";
	case CE_PLAYUNIT_CLASS_TANK:
	case CE_PLAYUNIT_CLASS_JEEP:
	case CE_PLAYUNIT_CLASS_ARTILLERY: return "Driving";
	case CE_PLAYUNIT_CLASS_TURRET: return "Operating";
	case CE_PLAYUNIT_CLASS_AIRCRAFT: return "Flying";
	case CE_PLAYUNIT_CLASS_NAVAL: return "Piloting";
	default: return "Controlling";
	}
}

static const int kCePlayUnitPromptMaxPieces = 6;

// Pure horizontal layout for the upper-left PlayUnit curved telemetry card so its right curved
// crescent and vertical aircraft gauges ALWAYS stay strictly clear of BEA's central circular
// compass ring (centered at w*0.5 with outer halo radius ~108*s) at both 1280x720 and 640x480.
struct CePlayUnitClassCardLayout
{
	float x0;
	float bodyLeft;
	float bodyW;
	float capW;
	float totalW;
	float padX;
	float gaugeRoom;
	float textX;
	float textMaxW;
	float compassLeft;
};

inline bool CePlayUnitComputeClassCardLayout(float w, float h, float s, float desiredTextW, bool isAir,
                                             CePlayUnitClassCardLayout *out)
{
	if (!out) return false;
	memset(out, 0, sizeof(*out));
	if (!isfinite(w) || !isfinite(h) || !isfinite(s) || w <= 0.f || h <= 0.f || s <= 0.f) return false;
	if (!isfinite(desiredTextW) || desiredTextW < 0.f) desiredTextW = 0.f;

	const float compassHalfSpan = 108.f * s;	// BEA compass outer ring (~101.3*s) + outer glow/chevron halo
	const float compassLeft = w * 0.5f - compassHalfSpan;
	const float compassGap = 14.f * s;
	const float x0 = floorf(12.f * s);
	const float capW = floorf(24.f * s);
	const float padX = floorf(9.f * s);
	const float gaugeRoom = isAir ? floorf(26.f * s) : 0.f;

	float maxRight = compassLeft - compassGap;
	if (maxRight > w - 8.f) maxRight = w - 8.f;
	const float maxTotalW = floorf(maxRight - x0);
	const float availTextW = maxTotalW - capW - 2.f * padX - gaugeRoom;
	if (availTextW < 40.f) return false;

	float minTextW = floorf((isAir ? 192.f : 180.f) * s);
	if (minTextW > availTextW) minTextW = availTextW;
	float maxTextW = floorf(280.f * s);
	if (maxTextW > availTextW) maxTextW = availTextW;

	float textW = desiredTextW;
	if (textW < minTextW) textW = minTextW;
	if (textW > maxTextW) textW = maxTextW;

	const float bodyW = floorf(textW + gaugeRoom + 2.f * padX);
	const float totalW = bodyW + capW;
	if (x0 + totalW > maxRight + 0.5f) return false;

	out->x0 = x0;
	out->bodyLeft = x0 + capW * 0.5f;
	out->bodyW = bodyW;
	out->capW = capW;
	out->totalW = totalW;
	out->padX = padX;
	out->gaugeRoom = gaugeRoom;
	out->textX = out->bodyLeft + padX;
	out->textMaxW = bodyW - 2.f * padX - gaugeRoom;
	out->compassLeft = compassLeft;
	return out->textMaxW > 0.f;
}

#endif
