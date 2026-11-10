// SPDX-License-Identifier: GPL-3.0-only
// Play as a unit (port/game/ce_playunit.cpp): the driven unit's chase camera, smoothed. The maths only, so that a test can compile and run it on its own
// (port/tools/ce_playunit_tests.py): nothing here reads the game, the environment or a clock.
//
// Why. The game's third-person camera on a unit is worked out once for each update (20 a second), and a unit on uneven ground changes its own tilt only every few
// updates, so a camera that is fixed to the unit's displayed pose (or to the camera's own latest pose) moves in steps of the update, and the steps are large where
// the ground is: the frame-feel log of a tank climbing a slope showed the camera's roll jump by up to 9 degrees between two frames and its pitch by 1.4 degrees, always
// on the first frame after an update, with the unit's own displayed pose smooth. The view is hung from the unit's displayed position instead, and what the ground does
// to the camera is spread over a fifth of a second: the camera's tilt and its offset from the unit settle toward where the game's camera is, as a critically damped
// spring does (exact for any frame time, so the same at 30, 60 or 360 frames a second). The turning of the unit is not smoothed: the camera's heading is the unit's
// displayed heading plus whatever the game's camera adds to it, and only that addition is.
#ifndef PORT_CE_PLAYUNIT_VIEW_H
#define PORT_CE_PLAYUNIT_VIEW_H

#include <math.h>

// A camera hung from an anchor (the driven unit's displayed position), as six numbers. The world's z axis points down, so a camera that looks down has a positive pitch.
//   yaw    the camera's heading minus the unit's displayed heading, radians (zero for a camera behind the unit)
//   pitch  the camera's forward axis above (negative) or below (positive) the horizon, radians
//   roll   the dip of the camera's right axis: positive when its right side is lower, radians
//   along, side, down   the camera's offset from the anchor in the frame of the unit's heading: along it, to its right, and down
struct CeUnitView
{
	float yaw, pitch, roll, along, side, down;
};

// The settling of the six numbers (CeUnitViewStep keeps it): where they are and how fast they are changing.
struct CeUnitViewSmoother
{
	bool valid;
	CeUnitView at, rate;
};

const float kUnitViewOmega = 10.f;			// 1/s: critically damped with both poles at -omega, so a step is 90% done in about 0.23 s
const float kUnitViewStall = 0.25f;			// s: after a gap this long between two frames the settling starts again from where the camera is
const float kUnitViewSnapAngle = 0.9f;		// rad: a tilt or heading this far from where the camera is was not made by the ground: it is taken as it stands
const float kUnitViewSnapDistance = 8.f;	// world units: the same for the offset

inline float CeUnitViewClamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

inline float CeUnitViewWrap(float a)
{
	if (!isfinite(a)) return 0.f;
	const float pi = 3.14159265358979f;
	while (a > pi) a -= 2.f * pi;
	while (a <= -pi) a += 2.f * pi;
	return a;
}

// The six numbers of a camera, from its right and forward axes and its offset from the anchor, all in the world's axes, and the unit's displayed heading (radians from the
// x axis, counter-clockwise seen from above, as atan2(y, x) gives it). False, with nothing written, when a number is not finite or the camera looks (nearly) straight up or
// down, which has no heading: the camera is then shown as it is.
inline bool CeUnitViewDescribe(const float right[3], const float forward[3], const float offset[3], float unitYaw, CeUnitView *out)
{
	for (int i = 0; i < 3; ++i)
		if (!isfinite(right[i]) || !isfinite(forward[i]) || !isfinite(offset[i])) return false;
	if (!isfinite(unitYaw)) return false;
	const float length = sqrtf(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
	if (!(length > 0.5f)) return false;
	const float fx = forward[0] / length, fy = forward[1] / length, fz = forward[2] / length;
	const float level = sqrtf(fx * fx + fy * fy);
	if (!(level > 0.05f)) return false;
	CeUnitView v;
	v.yaw = CeUnitViewWrap(atan2f(fy, fx) - unitYaw);
	v.pitch = asinf(CeUnitViewClamp(fz, -1.f, 1.f));
	v.roll = asinf(CeUnitViewClamp(right[2] / level, -1.f, 1.f));
	const float c = cosf(unitYaw), s = sinf(unitYaw);
	v.along = offset[0] * c + offset[1] * s;
	v.side = offset[0] * s - offset[1] * c;
	v.down = offset[2];
	*out = v;
	return true;
}

// The camera's right and forward axes and its offset from the anchor, in the world's axes, for six numbers and the unit's displayed heading: the inverse of
// CeUnitViewDescribe. The camera's third axis is the cross product of the two (right times forward) as in any rotation matrix.
inline void CeUnitViewBuild(const CeUnitView &v, float unitYaw, float right[3], float forward[3], float offset[3])
{
	const float heading = unitYaw + v.yaw;
	const float cy = cosf(heading), sy = sinf(heading), cp = cosf(v.pitch), sp = sinf(v.pitch);
	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = sp;
	// the right axis of a level camera with this heading, and the direction a roll tips it toward (level right times forward: straight down for a level camera)
	const float r0x = sy, r0y = -cy, r0z = 0.f;
	const float wx = r0y * forward[2] - r0z * forward[1];
	const float wy = r0z * forward[0] - r0x * forward[2];
	const float wz = r0x * forward[1] - r0y * forward[0];
	const float cr = cosf(v.roll), sr = sinf(v.roll);
	right[0] = r0x * cr + wx * sr;
	right[1] = r0y * cr + wy * sr;
	right[2] = r0z * cr + wz * sr;
	const float c = cosf(unitYaw), s = sinf(unitYaw);
	offset[0] = v.along * c + v.side * s;
	offset[1] = v.along * s - v.side * c;
	offset[2] = v.down;
}

// One number of the settling: x and its rate move toward target for dt seconds, exactly (a critically damped spring has a closed form, so a long frame and two short
// ones end in the same place).
inline void CeUnitViewSettle(float *x, float *rate, float target, float dt, float omega)
{
	const float d = *x - target, e = expf(-omega * dt), t = *rate + omega * d;
	*x = target + (d + t * dt) * e;
	*rate = (*rate - omega * t * dt) * e;
}

// The camera for one frame: `target` is where the game's camera is, dt the seconds since the last frame. The first frame, a gap longer than kUnitViewStall, a number that is
// not finite and a target far from where the camera is (the unit was put somewhere, or turned over: nothing the ground does between two frames) start the settling afresh
// at the target. A dt of zero (another view of the same frame) leaves the camera where it is.
inline void CeUnitViewStep(CeUnitViewSmoother *s, const CeUnitView &target, float dt, float omega, CeUnitView *out)
{
	bool restart = !s->valid || !(dt >= 0.f) || dt > kUnitViewStall || !(omega > 0.f);
	if (!restart)
	{
		const CeUnitView &a = s->at;
		const float angle = fmaxf(fmaxf(fabsf(CeUnitViewWrap(target.yaw - a.yaw)), fabsf(target.pitch - a.pitch)), fabsf(target.roll - a.roll));
		const float distance = fmaxf(fmaxf(fabsf(target.along - a.along), fabsf(target.side - a.side)), fabsf(target.down - a.down));
		restart = !(angle <= kUnitViewSnapAngle) || !(distance <= kUnitViewSnapDistance);
	}
	if (restart)
	{
		s->valid = true;
		s->at = target;
		s->rate.yaw = s->rate.pitch = s->rate.roll = s->rate.along = s->rate.side = s->rate.down = 0.f;
		*out = target;
		return;
	}
	if (dt > 0.f)
	{
		// the heading is wrapped around where the camera is, so that a camera behind the unit never swings the long way round
		const float yaw = s->at.yaw + CeUnitViewWrap(target.yaw - s->at.yaw);
		CeUnitViewSettle(&s->at.yaw, &s->rate.yaw, yaw, dt, omega);
		CeUnitViewSettle(&s->at.pitch, &s->rate.pitch, target.pitch, dt, omega);
		CeUnitViewSettle(&s->at.roll, &s->rate.roll, target.roll, dt, omega);
		CeUnitViewSettle(&s->at.along, &s->rate.along, target.along, dt, omega);
		CeUnitViewSettle(&s->at.side, &s->rate.side, target.side, dt, omega);
		CeUnitViewSettle(&s->at.down, &s->rate.down, target.down, dt, omega);
	}
	*out = s->at;
}

#endif
