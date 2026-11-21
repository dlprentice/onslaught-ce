// SPDX-License-Identifier: GPL-3.0-only
// Play as a unit: where the driven unit's camera hangs under the direct look (ce_playunit_look.h), as plain arithmetic with no game object, clock or allocation, so
// that a host test can compile and run it on its own (ce_playunit_camera_driver.cpp). Nothing here includes another header of the game, and the language is kept to
// C++98.
//
// One camera, three places along one rail: a wide shoulder view (0), a close shoulder view (1) and, where the caller has a validated eye, first person (2). Zoom In
// steps toward the eye and Zoom Out away from it; both stop at the ends. Every length comes from the unit's own local bounds (CePlayUnitCameraBuild), so a unit of any
// size is framed alike and no world-unit constant is carried from one model to another.
//
// What is smoothed and what is not. The only thing that settles over time is the place on the rail (CePlayUnitCameraAdvance). The pose is then worked out afresh each
// frame from that one number, the unit's place and the look (CePlayUnitCameraPoseFor): the orientation is the look's own axes, so a turn of the mouse or the stick
// shows in the same frame, the camera keeps its exact arm round the unit however fast the look turns, and nothing can drift because no position is remembered.
// Nothing here writes the look or the unit, so a change of view cannot steer anything.
//
// The axes are the look's: x east, y north, z down; a unit's local axes are x right, y forward, z down. "Above" is always toward -z.
//
// For each rendered frame of the direct look, the adapter:
//   on taking a unit    CePlayUnitCameraBuild (its bounds, its class's law, its validated eye or none), then CePlayUnitCameraReset (its identity, the view to start in)
//   on a Zoom edge      CePlayUnitCameraZoom
//   once a frame        CePlayUnitCameraAdvance (the rendered frame's seconds)
//   as often as needed  CePlayUnitCameraPoseFor: once without limits to learn where the camera wants to be, and again with what its own queries allow
//   on handing back     CePlayUnitCameraReset with identity 0
// The pose is for the direct look only. It is already settled: it must not be passed through the chase camera's smoother (ce_playunit_view.h) as well.
#ifndef PORT_CE_PLAYUNIT_CAMERA_H
#define PORT_CE_PLAYUNIT_CAMERA_H

#include <float.h>
#include <math.h>
#include <stddef.h>

// The views, in the order Zoom In walks them. They are also the places on the rail where the camera rests.
enum { kCePlayUnitCameraWide = 0, kCePlayUnitCameraClose = 1, kCePlayUnitCameraFirst = 2 };

const float kCePlayUnitCameraOmega = 14.f;			// 1/s: the rail settles as a critically damped spring does; one step from rest is 90% done in about 0.28 s
const float kCePlayUnitCameraMaxStep = 0.25f;		// s: a frame longer than this (or a time that is no time at all) ends the move where it was going
const float kCePlayUnitCameraDone = 0.002f;			// of one step of the rail: this near, the move is over and the camera is exactly at rest
const float kCePlayUnitCameraSpanRadius = 1.4f;		// a footprint counts toward a unit's span as this many times its radius about the root axis
const float kCePlayUnitCameraFootEyeHeight = 0.93f;	// a standing figure's eye, as a part of its height above its sole
const float kCePlayUnitCameraVehicleEyeHeight = 0.90f;	// a vehicle's turret optic, as a part of its height above its base

inline bool CePlayUnitCameraFinite(float v) { return v >= -FLT_MAX && v <= FLT_MAX; }

inline bool CePlayUnitCameraFinite3(const float *v)
{
	return v && CePlayUnitCameraFinite(v[0]) && CePlayUnitCameraFinite(v[1]) && CePlayUnitCameraFinite(v[2]);
}

// How a class of unit is framed, as parts of its span (the larger of its height and kCePlayUnitCameraSpanRadius times its footprint's radius):
//   wide, close  the two shoulder distances behind the pivot, along the look
//   lift         how far above the top of the bounds the pivot is
//   side         how far to the right of the pivot the camera sits (negative: to the left); the unit is then left of the reticle and the reticle's line passes beside it
//   ground       the lowest the camera hangs above the bottom of the bounds. This is the unit's own level, not the terrain: it only keeps a high look on level ground
//                from carrying the camera under the unit's feet. A law for a unit that flies would put it far below.
// The proportions are starting points to be judged in the running game, not a finished framing.
struct CePlayUnitCameraLaw
{
	float wide, close, lift, side, ground;
};

// A figure on foot: over its right shoulder, the pivot just above its head; half a span to the side, so the reticle stands clear beside the head at every tilt.
inline const CePlayUnitCameraLaw &CePlayUnitCameraFootLaw()
{
	static const CePlayUnitCameraLaw law = {3.f, 1.2f, 0.2f, 0.5f, 0.1f};
	return law;
}

// A vehicle: centred, from above. A lift of 0.6 of the span keeps the hull in the lower part of the picture and the reticle clear above it, so the player sees
// where a round lands beyond the hull rather than on it; the reticle's line passes over the whole of the bounds at every tilt of the look (a lift of a quarter of
// the span is the least that does that, down to a look about 0.33 radians below the horizon: 0.25 * 1.4 = 0.35 = the tangent of it).
inline const CePlayUnitCameraLaw &CePlayUnitCameraVehicleLaw()
{
	static const CePlayUnitCameraLaw law = {2.2f, 1.4f, 0.6f, 0.f, 0.1f};
	return law;
}

// A unit's camera, in its own units: made once from its bounds, read every frame.
//   span, radius                 the framing size, and the radius of the bounds about the vertical through the root (whichever way the unit faces, it is inside that)
//   wideDistance, closeDistance  the shoulder distances
//   lift, side                   the pivot's height above the root and the camera's place to the right of it: the same in both shoulder views, so that going from one
//                                to the other is a move straight along the look and the reticle's line does not shift
//   ground                       the lowest the camera hangs, as a height above the root
//   eye                          the validated first-person eye in the unit's local axes; meaningful only when firstPerson is true
struct CePlayUnitCameraRig
{
	bool valid, firstPerson;
	float span, radius, wideDistance, closeDistance, lift, side, ground;
	float eye[3];
};

// The size of a unit's bounds (local minimum and maximum corners). False when a corner is missing or not finite, the corners are the wrong way round, or the bounds have
// no size at all.
inline bool CePlayUnitCameraMeasure(const float *lo, const float *hi, float *height, float *radius, float *span)
{
	if (!CePlayUnitCameraFinite3(lo) || !CePlayUnitCameraFinite3(hi)) return false;
	if (!(lo[0] <= hi[0]) || !(lo[1] <= hi[1]) || !(lo[2] <= hi[2])) return false;
	const float x = fabsf(lo[0]) > fabsf(hi[0]) ? fabsf(lo[0]) : fabsf(hi[0]);
	const float y = fabsf(lo[1]) > fabsf(hi[1]) ? fabsf(lo[1]) : fabsf(hi[1]);
	const float r = sqrtf(x * x + y * y), h = hi[2] - lo[2];
	const float s = h > kCePlayUnitCameraSpanRadius * r ? h : kCePlayUnitCameraSpanRadius * r;
	if (!CePlayUnitCameraFinite(s) || !(s > 0.f)) return false;
	*height = h;
	*radius = r;
	*span = s;
	return true;
}

// A standing figure's eye from its bounds: on the vertical through its root (so that turning the look never carries the view sideways through a wall), at eye level
// above its sole. Only for a model the caller knows to be an upright figure; false when the bounds are no use or have no height.
inline bool CePlayUnitCameraFootEye(const float *lo, const float *hi, float *eye)
{
	float height, radius, span;
	if (!CePlayUnitCameraMeasure(lo, hi, &height, &radius, &span) || !(height > 0.f)) return false;
	eye[0] = eye[1] = 0.f;
	eye[2] = hi[2] - kCePlayUnitCameraFootEyeHeight * height;
	return true;
}

// A vehicle's turret/weapon vantage from its bounds: on the root's vertical (clamped into the horizontal footprint if asymmetric), near the top of the turret bounds.
// False when the bounds are no use or have no height.
inline bool CePlayUnitCameraVehicleEye(const float *lo, const float *hi, float *eye)
{
	float height, radius, span;
	if (!CePlayUnitCameraMeasure(lo, hi, &height, &radius, &span) || !(height > 0.f)) return false;
	eye[0] = 0.f < lo[0] ? lo[0] : (0.f > hi[0] ? hi[0] : 0.f);
	eye[1] = 0.f < lo[1] ? lo[1] : (0.f > hi[1] ? hi[1] : 0.f);
	eye[2] = hi[2] - kCePlayUnitCameraVehicleEyeHeight * height;
	return true;
}

// The camera of a unit with these bounds under this law. `eye` is the caller's validated first-person eye in the unit's local axes, or null for a unit that has none:
// first person exists only when it is given, finite and inside the bounds (an eye above or outside the model is refused, and the two shoulder views remain). False,
// with a rig that is not valid, when the bounds or the law are no use: the caller then keeps the camera it had before this header.
inline bool CePlayUnitCameraBuild(CePlayUnitCameraRig *rig, const float *lo, const float *hi, const CePlayUnitCameraLaw &law, const float *eye)
{
	float height, radius, span;
	rig->valid = rig->firstPerson = false;
	rig->span = rig->radius = rig->wideDistance = rig->closeDistance = rig->lift = rig->side = rig->ground = 0.f;
	rig->eye[0] = rig->eye[1] = rig->eye[2] = 0.f;
	if (!CePlayUnitCameraMeasure(lo, hi, &height, &radius, &span)) return false;
	if (!CePlayUnitCameraFinite(law.wide) || !CePlayUnitCameraFinite(law.close) || !CePlayUnitCameraFinite(law.lift) || !CePlayUnitCameraFinite(law.side)
		|| !CePlayUnitCameraFinite(law.ground))
		return false;
	if (!(law.close > 0.f) || !(law.wide >= law.close)) return false;
	const float wideD = law.wide * span, closeD = law.close * span, side = law.side * span;
	const float lift = -lo[2] + law.lift * span, ground = -hi[2] + law.ground * span;
	if (!CePlayUnitCameraFinite(wideD) || !CePlayUnitCameraFinite(closeD) || !CePlayUnitCameraFinite(side) || !CePlayUnitCameraFinite(lift)
		|| !CePlayUnitCameraFinite(ground))
		return false;
	rig->valid = true;
	rig->span = span;
	rig->radius = radius;
	rig->wideDistance = wideD;
	rig->closeDistance = closeD;
	rig->lift = lift;
	rig->side = side;
	rig->ground = ground;
	if (CePlayUnitCameraFinite3(eye) && eye[0] >= lo[0] && eye[0] <= hi[0] && eye[1] >= lo[1] && eye[1] <= hi[1] && eye[2] >= lo[2] && eye[2] <= hi[2])
	{
		rig->firstPerson = true;
		rig->eye[0] = eye[0];
		rig->eye[1] = eye[1];
		rig->eye[2] = eye[2];
	}
	return true;
}

// The last view Zoom In reaches on this unit.
inline int CePlayUnitCameraTop(const CePlayUnitCameraRig &rig)
{
	return rig.valid && rig.firstPerson ? kCePlayUnitCameraFirst : kCePlayUnitCameraClose;
}

// Which unit is held (0: none), the view asked for, and where the camera is on the rail and how fast it is moving along it. Reset it before its first use.
struct CePlayUnitCameraState
{
	size_t identity;
	int view;
	float rail, rate;
};

// At rest in a view, for a unit (identity 0: nothing is held). Taking a unit, handing it back and any wish to start somewhere all come here; nothing is blended.
inline void CePlayUnitCameraReset(CePlayUnitCameraState *s, size_t identity, int view)
{
	if (view < kCePlayUnitCameraWide) view = kCePlayUnitCameraWide;
	if (view > kCePlayUnitCameraFirst) view = kCePlayUnitCameraFirst;
	s->identity = identity;
	s->view = view;
	s->rail = (float)view;
	s->rate = 0.f;
}

// One frame's Zoom edges: In steps one view toward the eye, Out one away, both together are no step, and the ends hold (no wrapping, and no first person on a unit
// without it). Only the view asked for changes; the camera moves when time passes. With nothing held or a rig that is not valid, nothing changes. The view now asked for.
inline int CePlayUnitCameraZoom(CePlayUnitCameraState *s, const CePlayUnitCameraRig &rig, bool zoomIn, bool zoomOut)
{
	if (s->identity == 0 || !rig.valid) return s->view;
	int view = s->view + (zoomIn ? 1 : 0) - (zoomOut ? 1 : 0);
	const int top = CePlayUnitCameraTop(rig);
	if (view < kCePlayUnitCameraWide) view = kCePlayUnitCameraWide;
	if (view > top) view = top;
	s->view = view;
	return view;
}

// One toggle press between third-person and first-person (or between Wide and Close on a unit without a validated eye).
// Remembers the last third-person view in *savedThird so returning from first person lands back where the player left.
inline int CePlayUnitCameraToggleFirst(CePlayUnitCameraState *s, const CePlayUnitCameraRig &rig, int *savedThird)
{
	if (s->identity == 0 || !rig.valid) return s->view;
	const int top = CePlayUnitCameraTop(rig);
	if (top == kCePlayUnitCameraFirst)
	{
		if (s->view == kCePlayUnitCameraFirst)
		{
			int back = savedThird ? *savedThird : (int)kCePlayUnitCameraWide;
			if (back < kCePlayUnitCameraWide || back > kCePlayUnitCameraClose) back = kCePlayUnitCameraWide;
			s->view = back;
		}
		else
		{
			if (savedThird) *savedThird = s->view;
			s->view = kCePlayUnitCameraFirst;
		}
	}
	else
	{
		s->view = (s->view == kCePlayUnitCameraWide) ? kCePlayUnitCameraClose : kCePlayUnitCameraWide;
	}
	return s->view;
}

// A frame's time passes: the rail settles toward the view asked for. The settling is the closed form of a critically damped spring, so a long frame and several short
// ones end in the same place. Before each step the rate is held to what the new request allows: none of it away from the view asked for (a reversal starts back at
// once, from rest) and no more toward it than arrives without passing it (a tap mid-move cannot carry the camera beyond the view asked for). Between requests those two
// limits never bind, so the rail only ever moves toward the view asked for and stops exactly on it.
//   a different identity, a rig that is not valid, a state that is not finite or lies off the rail (first person lost included): at rest at the view asked for, at once
//   dt of zero (the same frame seen again, or a pause): nothing moves
//   dt negative, not finite or longer than kCePlayUnitCameraMaxStep: the move ends at the view asked for, at once
inline void CePlayUnitCameraAdvance(CePlayUnitCameraState *s, const CePlayUnitCameraRig &rig, size_t identity, float dt)
{
	const int top = rig.valid ? CePlayUnitCameraTop(rig) : (int)kCePlayUnitCameraFirst;
	if (s->view < kCePlayUnitCameraWide) s->view = kCePlayUnitCameraWide;
	if (s->view > top) s->view = top;
	const float target = (float)s->view;
	const bool same = identity != 0 && s->identity == identity && rig.valid && CePlayUnitCameraFinite(s->rail) && CePlayUnitCameraFinite(s->rate) && s->rail >= 0.f
		&& s->rail <= (float)top;
	if (same && dt == 0.f) return;
	if (!same || !(dt > 0.f) || !(dt <= kCePlayUnitCameraMaxStep))
	{
		s->identity = identity;
		s->rail = target;
		s->rate = 0.f;
		return;
	}
	const float from = s->rail, d = from - target, most = kCePlayUnitCameraOmega * fabsf(d);
	if (d * s->rate > 0.f) s->rate = 0.f;
	if (s->rate > most) s->rate = most;
	if (s->rate < -most) s->rate = -most;
	const float e = expf(-kCePlayUnitCameraOmega * dt), t = s->rate + kCePlayUnitCameraOmega * d;
	float x = target + (d + t * dt) * e;
	s->rate = (s->rate - kCePlayUnitCameraOmega * t * dt) * e;
	// rounding is not allowed to carry the rail back, or past the view asked for
	const float lo = from < target ? from : target, hi = from < target ? target : from;
	if (x < lo) x = lo;
	if (x > hi) x = hi;
	if (fabsf(x - target) <= kCePlayUnitCameraDone)
	{
		x = target;
		s->rate = 0.f;
	}
	s->rail = x;
}

// What the adapter's own queries allow this frame, in world units; a negative or not finite number is no limit, and a null pointer is no limits. This header
// queries nothing: it only applies what it is given, at once and without smoothing.
//   arm       the free length from the pose's anchor toward the camera (terrain or an obstruction in the way): the camera comes in along its arm to that length
//   headroom  the free height above the unit's place for the anchor itself (an overhang above a tall pivot, or a low ceiling above an eye)
struct CePlayUnitCameraLimits
{
	float arm, headroom;
};

// The one pose a frame is drawn and aimed from.
//   eye, right, forward, down  the camera: where it is and its axes. The axes are the look's and nothing else; the screen's middle (the reticle) is along `forward`
//                              from `eye`. This one ray is both the rendered view and the player's line of aim, in every view and mid-move.
//   anchor, arm                the point the camera hangs from (the pivot above the unit, becoming the eye) and how far from it the camera is
//   aimStart                   how far along the ray the unit's own depth is: a query for what the reticle is on starts here, so that nothing beside or behind
//                              the unit is aimed at. Zero in first person.
//   aimNear                    the nearest point on the ray a weapon is asked to converge on (CePlayUnitCameraAimPoint)
//   clearance, hideBody        the level gap between the camera and the upright cylinder that holds the bounds; hideBody when the camera is within it, as it is
//                              in first person, on the way in, and when a limit has pulled it in
//   rail, first, firstPerson   the place on the rail, how much of the pose is the eye's (0 to 1), and whether it is at rest at the eye
struct CePlayUnitCameraPose
{
	bool valid;
	float eye[3], right[3], forward[3], down[3];
	float anchor[3], arm;
	float aimStart, aimNear;
	float clearance;
	float rail, first;
	bool firstPerson, hideBody;
};

// The pose for a place on the rail, the unit's displayed place and the look. `bodyAxes` is the unit's displayed right, forward and down axes in the world's (nine
// numbers, each axis of unit length), used only to carry a first-person eye that is off the root's vertical or on a tilted unit; null (or not finite) and the eye is
// taken straight above the root at its own height. False, with only `valid` written (as false), when the rig is not valid or the place, the look or the rail is not
// finite: the caller then keeps the camera it had before this header. The state is only read, so the same frame may be asked for again.
inline bool CePlayUnitCameraPoseFor(const CePlayUnitCameraState &s, const CePlayUnitCameraRig &rig, const float *place, float yaw, float pitch,
	const float *bodyAxes, const CePlayUnitCameraLimits *limits, CePlayUnitCameraPose *out)
{
	int i;
	out->valid = false;
	if (!rig.valid || !CePlayUnitCameraFinite3(place) || !CePlayUnitCameraFinite(yaw) || !CePlayUnitCameraFinite(pitch) || !CePlayUnitCameraFinite(s.rail))
		return false;
	const float top = rig.firstPerson ? 2.f : 1.f;
	const float rail = s.rail < 0.f ? 0.f : s.rail > top ? top : s.rail;
	const float u = rail < 1.f ? rail : 1.f, w = rail > 1.f ? rail - 1.f : 0.f;

	// the look's axes, exactly as ce_playunit_look.h's CePlayUnitLookAxes makes them
	CePlayUnitCameraPose p;
	const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
	p.right[0] = cy; p.right[1] = sy; p.right[2] = 0.f;
	p.forward[0] = -sy * cp; p.forward[1] = cy * cp; p.forward[2] = sp;
	p.down[0] = p.right[1] * p.forward[2] - p.right[2] * p.forward[1];
	p.down[1] = p.right[2] * p.forward[0] - p.right[0] * p.forward[2];
	p.down[2] = p.right[0] * p.forward[1] - p.right[1] * p.forward[0];

	// the anchor: the pivot above the root, becoming the eye
	float hang[3] = {0.f, 0.f, 0.f};
	hang[2] = -rig.lift * (1.f - w);
	if (w > 0.f)
	{
		float e[3] = {0.f, 0.f, 0.f};
		bool turned = bodyAxes != 0;
		e[2] = rig.eye[2];
		for (i = 0; turned && i < 9; ++i) turned = CePlayUnitCameraFinite(bodyAxes[i]);
		for (i = 0; turned && i < 3; ++i) e[i] = bodyAxes[i] * rig.eye[0] + bodyAxes[3 + i] * rig.eye[1] + bodyAxes[6 + i] * rig.eye[2];
		for (i = 0; i < 3; ++i) hang[i] += w * e[i];
	}
	if (limits && CePlayUnitCameraFinite(limits->headroom) && limits->headroom >= 0.f && -hang[2] > limits->headroom) hang[2] = -limits->headroom;

	// the arm: behind the anchor along the look and to its right, closing to nothing at the eye
	float distance = (rig.wideDistance * (1.f - u) + rig.closeDistance * u) * (1.f - w), side = rig.side * (1.f - w);
	const float drop = -sp * distance, room = -hang[2] - rig.ground;
	if (drop > 0.f && drop > room)
	{
		// a high look would hang the camera below the unit's own ground level: it comes in along its arm instead
		const float k = room > 0.f ? room / drop : 0.f;
		distance *= k;
		side *= k;
	}
	float arm = sqrtf(distance * distance + side * side);
	if (limits && CePlayUnitCameraFinite(limits->arm) && limits->arm >= 0.f && arm > limits->arm)
	{
		const float k = limits->arm / arm;
		distance *= k;
		side *= k;
		arm = limits->arm;
	}

	float off[3];
	for (i = 0; i < 3; ++i)
	{
		off[i] = hang[i] - p.forward[i] * distance + p.right[i] * side;
		p.anchor[i] = place[i] + hang[i];
		p.eye[i] = place[i] + off[i];
	}
	if (!CePlayUnitCameraFinite3(p.eye) || !CePlayUnitCameraFinite3(p.anchor)) return false;
	p.arm = arm;
	p.aimStart = distance;
	p.aimNear = distance + rig.span;
	p.clearance = sqrtf(off[0] * off[0] + off[1] * off[1]) - rig.radius;
	p.hideBody = !(p.clearance > 0.f);
	p.rail = rail;
	p.first = w;
	p.firstPerson = w >= 1.f;
	p.valid = true;
	*out = p;
	return true;
}

// The point a weapon may be asked to converge on: along the pose's ray at `hit`, the distance at which the adapter's own query (started at aimStart) found something, or
// its own far range when it found nothing; never nearer than aimNear, so that something close in front of the unit cannot bend a shot sharply across the gap between the
// camera and the weapon. A shot still starts at the weapon and nowhere else. False, with nothing written, for a pose that is not valid or a distance that is not finite.
inline bool CePlayUnitCameraAimPoint(const CePlayUnitCameraPose &pose, float hit, float *point)
{
	if (!pose.valid || !CePlayUnitCameraFinite(hit)) return false;
	const float d = hit > pose.aimNear ? hit : pose.aimNear;
	point[0] = pose.eye[0] + pose.forward[0] * d;
	point[1] = pose.eye[1] + pose.forward[1] * d;
	point[2] = pose.eye[2] + pose.forward[2] * d;
	return true;
}

#endif
