// SPDX-License-Identifier: GPL-3.0-only
// Play as a unit: the look of a driven unit's camera (port/game/ce_playunit.cpp's CeLookCamera), as plain arithmetic with no game object, so that a host test can run it
// (ce_playunit_tests.py). The view turns and tilts with the player's look alone, so the ground under the unit cannot tilt it; it hangs behind and above the unit, on the line of
// the look, and the reticle at the middle of the screen is where the look points.
//
// The game's axes: x east, y north, z down. A yaw turns the forward axis (0, 1, 0) to (-sin, cos, 0); a pitch turns it to (0, cos, sin), so a positive pitch looks down. A
// camera's orientation has the right axis, the forward axis and their cross product (down at rest) as its columns, as the game's own cameras build it.
#ifndef PORT_CE_PLAYUNIT_LOOK_H
#define PORT_CE_PLAYUNIT_LOOK_H

#include <math.h>

// How a class of unit is looked at: the camera's distance behind the look's pivot, how far above the unit's place the pivot is, the look's limits up and down (radians, up
// negative as the game's pitch is), and how far one count of the mouse turns the look (radians).
struct CePlayUnitLookLaw
{
	float distance, lift, pitchUp, pitchDown, perCount, stickRate;
};

// The tank's: the camera ten units behind a pivot three and a half above the hull, looking from 20 degrees up to 17 down; 400 counts of the mouse turn the look by
// about 57 degrees; a stick held over turns it 1.5 radians a second. At the lowest look the point under the reticle on flat ground is lift / tan(pitchDown), about 11
// units ahead of the pivot: beyond the hull and the gun's least range, so the player never aims at the driven unit (kCePlayUnitLookAimFloor).
inline const CePlayUnitLookLaw &CePlayUnitTankLook()
{
	static const CePlayUnitLookLaw law = {10.f, 3.5f, -0.35f, 0.3f, 0.0025f, 1.5f};
	return law;
}

// The nearest the point under the reticle may be to the unit on flat ground at the lowest look, in world units.
const float kCePlayUnitLookAimFloor = 10.f;

// The held soldier stays in frame at walking distance. Retain the player's look sensitivity.
// First person shares the soldier's horizontal position. Looking around changes
// the orientation alone; an eye offset never carries the view around a corner.
inline void CePlayUnitFootFirstEye(const float place[3], float eye[3])
{
    eye[0] = place[0];
    eye[1] = place[1];
    eye[2] = place[2] - 1.55f;
}

inline const CePlayUnitLookLaw &CePlayUnitFootLook()
{
	static const CePlayUnitLookLaw law = [] {
		CePlayUnitLookLaw foot = CePlayUnitTankLook();
		foot.distance = 3.15f;
		foot.lift = 1.7f;
		return foot;
	}();
	return law;
}

inline float CePlayUnitLookWrap(float a)
{
	const float pi = 3.14159265f;
	if (!isfinite(a)) return 0.f;
	while (a > pi) a -= 2.f * pi;
	while (a < -pi) a += 2.f * pi;
	return a;
}

// The yaw of a heading (the forward axis's x and y): the angle the game's yaw turns (0, 1, 0) by to point along it.
inline float CePlayUnitLookYawOf(float hx, float hy)
{
	if (!isfinite(hx) || !isfinite(hy) || (hx == 0.f && hy == 0.f)) return 0.f;
	return atan2f(-hx, hy);
}

// The look's axes: right, forward and their cross product, each of unit length.
inline void CePlayUnitLookAxes(float yaw, float pitch, float *right, float *forward, float *down)
{
	const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
	right[0] = cy; right[1] = sy; right[2] = 0.f;
	forward[0] = -sy * cp; forward[1] = cy * cp; forward[2] = sp;
	down[0] = right[1] * forward[2] - right[2] * forward[1];
	down[1] = right[2] * forward[0] - right[0] * forward[2];
	down[2] = right[0] * forward[1] - right[1] * forward[0];
}

// Where the camera is: behind the pivot (lift above the unit's place) along the look, by the law's distance.
inline void CePlayUnitLookEye(const float *place, float yaw, float pitch, const CePlayUnitLookLaw &law, float *eye)
{
	float r[3], f[3], d[3];
	CePlayUnitLookAxes(yaw, pitch, r, f, d);
	eye[0] = place[0] - f[0] * law.distance;
	eye[1] = place[1] - f[1] * law.distance;
	eye[2] = place[2] - law.lift - f[2] * law.distance;
}

// The mouse turns the look: right (a positive dx) turns it right, down (a positive dy) tilts it down; the tilt stays within the law's limits and the yaw within a turn.
inline void CePlayUnitLookTurn(float *yaw, float *pitch, float dx, float dy, const CePlayUnitLookLaw &law)
{
	if (!isfinite(dx) || !isfinite(dy)) return;
	*yaw = CePlayUnitLookWrap(*yaw - dx * law.perCount);
	float p = *pitch + dy * law.perCount;
	if (p < law.pitchUp) p = law.pitchUp;
	if (p > law.pitchDown) p = law.pitchDown;
	*pitch = p;
}

// A stick (or a bound key) turns the look at the law's rate: sx right, sy down, each -1 to 1, held for seconds; the same limits as the mouse.
inline void CePlayUnitLookStick(float *yaw, float *pitch, float sx, float sy, float seconds, const CePlayUnitLookLaw &law)
{
	if (!isfinite(sx) || !isfinite(sy) || !isfinite(seconds) || !(seconds > 0.f) || !(law.perCount > 0.f)) return;
	sx = sx < -1.f ? -1.f : sx > 1.f ? 1.f : sx;
	sy = sy < -1.f ? -1.f : sy > 1.f ? 1.f : sy;
	const float counts = law.stickRate * seconds / law.perCount;
	CePlayUnitLookTurn(yaw, pitch, sx * counts, sy * counts, law);
}

#endif
