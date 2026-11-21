// SPDX-License-Identifier: GPL-3.0-only
// Authored CPU checks for the pure camera module (ce_playunit_camera.h). No game, no display, no file: it prints each check that fails and a count, and returns 1 when
// any failed. Every unit here is a made-up box; none is a measurement of anything.
//
// The module is included first and alone, so this file also shows that it stands by itself and compiles as C++98. The shipped look header needs C++11, so the checks
// against it are compiled only where the language allows; without it the same checks run against this file's own copy of the look's documented axes.
#include "ce_playunit_camera.h"

#include <stdio.h>
#include <string.h>

#if __cplusplus >= 201103L || (defined(_MSC_VER) && _MSC_VER >= 1900)
#include "ce_playunit_look.h"
#define CE_CAMERA_DRIVER_HAS_LOOK 1
#endif

static int gChecks = 0, gFailures = 0;

static void Check(bool ok, const char *what)
{
	++gChecks;
	if (!ok)
	{
		++gFailures;
		printf("FAIL: %s\n", what);
	}
}

// False for a number that is not a number, so a pose that went wrong never passes as near.
static bool Near(float a, float b, float tol) { return fabsf(a - b) <= tol; }
static bool Near3(const float *a, const float *b, float tol) { return Near(a[0], b[0], tol) && Near(a[1], b[1], tol) && Near(a[2], b[2], tol); }
static float Dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float Gap(const float *a, const float *b)
{
	const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	return sqrtf(x * x + y * y + z * z);
}

// Numbers the compiler cannot fold: a zero to divide by.
static float Quiet(float v)
{
	volatile float q = v;
	return q;
}
static float NotANumber() { return Quiet(0.f) / Quiet(0.f); }
static float Endless() { return Quiet(1.f) / Quiet(0.f); }

// A figure one unit tall whose root is a little above its sole, and a long low hull whose root is at its base and behind its middle.
static const float kFootLo[3] = {-0.22f, -0.14f, -0.98f}, kFootHi[3] = {0.22f, 0.14f, 0.02f};
static const float kHullLo[3] = {-2.f, -3.f, -3.f}, kHullHi[3] = {2.f, 5.f, 0.f};
static const float kPlace[3] = {10.f, 20.f, -5.f};
static const float kLookUp = -0.35f, kLookDown = 0.3f;	// the look's limits, as the shipped look law has them (checked below where that header compiles)
static const size_t kActor = 1, kOther = 2;

static void Scaled(const float *v, float k, float *out)
{
	out[0] = v[0] * k;
	out[1] = v[1] * k;
	out[2] = v[2] * k;
}

static bool BuildFoot(CePlayUnitCameraRig *rig, float k)
{
	float lo[3], hi[3], eye[3];
	Scaled(kFootLo, k, lo);
	Scaled(kFootHi, k, hi);
	if (!CePlayUnitCameraFootEye(lo, hi, eye)) return false;
	return CePlayUnitCameraBuild(rig, lo, hi, CePlayUnitCameraFootLaw(), eye);
}

static bool BuildHull(CePlayUnitCameraRig *rig, float k)
{
	float lo[3], hi[3];
	Scaled(kHullLo, k, lo);
	Scaled(kHullHi, k, hi);
	return CePlayUnitCameraBuild(rig, lo, hi, CePlayUnitCameraVehicleLaw(), 0);
}

static CePlayUnitCameraState At(int view)
{
	CePlayUnitCameraState s;
	CePlayUnitCameraReset(&s, kActor, view);
	return s;
}

static CePlayUnitCameraState AtRail(float rail)
{
	CePlayUnitCameraState s = At(kCePlayUnitCameraClose);
	s.rail = rail;
	return s;
}

static CePlayUnitCameraPose SeeFrom(const CePlayUnitCameraState &s, const CePlayUnitCameraRig &rig, const float *place, float yaw, float pitch,
	const float *axes, const CePlayUnitCameraLimits *limits)
{
	CePlayUnitCameraPose pose;
	memset(&pose, 0, sizeof pose);
	CePlayUnitCameraPoseFor(s, rig, place, yaw, pitch, axes, limits, &pose);
	return pose;
}

static CePlayUnitCameraPose See(const CePlayUnitCameraState &s, const CePlayUnitCameraRig &rig, float yaw, float pitch)
{
	return SeeFrom(s, rig, kPlace, yaw, pitch, 0, 0);
}

static void Run(CePlayUnitCameraState *s, const CePlayUnitCameraRig &rig, float dt, int frames)
{
	int i;
	for (i = 0; i < frames; ++i) CePlayUnitCameraAdvance(s, rig, kActor, dt);
}

// The look's axes as ce_playunit_look.h documents them.
static void LookAxes(float yaw, float pitch, float *right, float *forward, float *down)
{
	const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
	right[0] = cy; right[1] = sy; right[2] = 0.f;
	forward[0] = -sy * cp; forward[1] = cy * cp; forward[2] = sp;
	down[0] = right[1] * forward[2] - right[2] * forward[1];
	down[1] = right[2] * forward[0] - right[0] * forward[2];
	down[2] = right[0] * forward[1] - right[1] * forward[0];
}

// Every length of a rig comes from the bounds and the law's proportions.
static void TestRig()
{
	CePlayUnitCameraRig foot, hull;
	const CePlayUnitCameraLaw &fl = CePlayUnitCameraFootLaw(), &vl = CePlayUnitCameraVehicleLaw();

	Check(BuildFoot(&foot, 1.f) && foot.valid, "rig: a figure's bounds make a rig");
	Check(Near(foot.span, 1.f, 1e-5f), "rig: a figure's span is its height");
	Check(Near(foot.radius, sqrtf(0.22f * 0.22f + 0.14f * 0.14f), 1e-5f), "rig: the radius is the footprint's, about the root's vertical");
	Check(Near(foot.wideDistance, fl.wide * foot.span, 1e-5f) && Near(foot.closeDistance, fl.close * foot.span, 1e-5f), "rig: both distances are parts of the span");
	Check(foot.wideDistance > foot.closeDistance, "rig: the wide view is farther than the close one");
	Check(Near(foot.lift, 0.98f + fl.lift * foot.span, 1e-5f), "rig: the pivot is measured from the top of the bounds");
	Check(Near(foot.side, fl.side * foot.span, 1e-5f) && foot.side > 0.f, "rig: a figure's camera is to its right, by a part of the span");
	Check(Near(foot.ground, -0.02f + fl.ground * foot.span, 1e-5f), "rig: the lowest camera is measured from the bottom of the bounds");
	Check(foot.firstPerson && CePlayUnitCameraTop(foot) == kCePlayUnitCameraFirst, "rig: a validated eye gives first person");
	Check(foot.eye[0] == 0.f && foot.eye[1] == 0.f, "rig: a figure's eye is on the root's vertical");
	const float eyeUp = (kFootHi[2] - foot.eye[2]) / (kFootHi[2] - kFootLo[2]);
	Check(eyeUp > 0.85f && eyeUp < 0.97f, "rig: a figure's eye is at eye level inside the model, not above it");
	Check(foot.eye[2] > kFootLo[2] && foot.eye[2] < kFootHi[2], "rig: a figure's eye is between its top and its sole");

	Check(BuildHull(&hull, 1.f) && hull.valid, "rig: a hull's bounds make a rig");
	Check(Near(hull.radius, sqrtf(29.f), 1e-4f) && Near(hull.span, kCePlayUnitCameraSpanRadius * sqrtf(29.f), 1e-4f), "rig: a long low hull's span comes from its footprint");
	Check(Near(hull.wideDistance, vl.wide * hull.span, 1e-4f) && Near(hull.closeDistance, vl.close * hull.span, 1e-4f), "rig: a hull's distances are parts of its span");
	Check(hull.wideDistance >= 1.5f * hull.closeDistance, "rig: a hull's near and far views are usefully apart");
	Check(hull.closeDistance * cosf(kLookUp) > hull.radius, "rig: a hull's close camera is outside its bounds at any tilt of the look");
	Check(hull.lift - 3.f >= hull.radius * tanf(kLookDown), "rig: at the lowest look the reticle's line still passes over the whole hull");
	Check(hull.side == 0.f, "rig: a hull's camera is centred");
	Check(!hull.firstPerson && CePlayUnitCameraTop(hull) == kCePlayUnitCameraClose, "rig: no eye given, no first person");
}

// A unit ten times the size is framed the same, ten times as far.
static void TestScale()
{
	static const float scales[4] = {0.01f, 0.3f, 8.f, 250.f};
	static const float origin[3] = {0.f, 0.f, 0.f};
	static const float rails[4] = {0.f, 1.f, 1.5f, 2.f};
	CePlayUnitCameraRig one, big;
	int i, j, c;
	BuildFoot(&one, 1.f);
	for (i = 0; i < 4; ++i)
	{
		const float k = scales[i], tol = 3e-4f * k;
		Check(BuildFoot(&big, k), "scale: a scaled figure makes a rig");
		Check(Near(big.span, one.span * k, tol) && Near(big.radius, one.radius * k, tol) && Near(big.wideDistance, one.wideDistance * k, tol)
			&& Near(big.closeDistance, one.closeDistance * k, tol) && Near(big.lift, one.lift * k, tol) && Near(big.side, one.side * k, tol)
			&& Near(big.ground, one.ground * k, tol) && Near(big.eye[2], one.eye[2] * k, tol), "scale: every length of the rig scales with the bounds");
		for (j = 0; j < 4; ++j)
		{
			const CePlayUnitCameraState s = AtRail(rails[j]);
			const CePlayUnitCameraPose a = SeeFrom(s, one, origin, 0.7f, -0.2f, 0, 0), b = SeeFrom(s, big, origin, 0.7f, -0.2f, 0, 0);
			bool same = a.valid && b.valid;
			for (c = 0; c < 3; ++c) same = same && Near(b.eye[c], a.eye[c] * k, tol) && Near(b.anchor[c], a.anchor[c] * k, tol);
			Check(same, "scale: the camera and its anchor scale with the bounds");
			Check(Near(b.arm, a.arm * k, tol) && Near(b.aimStart, a.aimStart * k, tol) && Near(b.aimNear, a.aimNear * k, tol), "scale: the arm and the aim distances scale");
			Check(Near3(a.forward, b.forward, 1e-6f) && Near3(a.right, b.right, 1e-6f), "scale: size does not turn the view");
			Check(a.hideBody == b.hideBody && a.first == b.first, "scale: size changes neither the view nor whether the body is hidden");
		}
	}
}

// First person: a model-relative eye, the same point whatever the look, and only where the caller has one.
static void TestFirstPerson()
{
	CePlayUnitCameraRig foot, hull, rig;
	CePlayUnitCameraState s = At(kCePlayUnitCameraFirst);
	BuildFoot(&foot, 1.f);
	BuildHull(&hull, 1.f);

	const float want[3] = {kPlace[0], kPlace[1], kPlace[2] + foot.eye[2]};
	const CePlayUnitCameraPose a = See(s, foot, 0.f, 0.f), b = See(s, foot, 2.1f, kLookDown), c = See(s, foot, -1.3f, kLookUp);
	Check(a.valid && a.firstPerson && a.first == 1.f, "first: at rest at the eye the pose says so");
	Check(Near3(a.eye, want, 1e-5f), "first: the eye is at the figure's eye height above its root");
	Check(Near3(b.eye, want, 1e-5f) && Near3(c.eye, want, 1e-5f), "first: turning and tilting the look does not move the eye");
	Check(Near3(a.eye, a.anchor, 0.f) && a.arm == 0.f && a.aimStart == 0.f, "first: the camera is the anchor, and the aim starts at the eye");
	Check(a.hideBody, "first: the body is hidden at the eye");

	// an eye off the root's vertical follows the unit's own axes when they are given, and is taken straight above the root when they are not
	const float eye[3] = {0.1f, 0.1f, -0.9f};
	const float quarter[9] = {0.f, 1.f, 0.f, -1.f, 0.f, 0.f, 0.f, 0.f, 1.f};	// right is north, forward is west
	float bad[9];
	memcpy(bad, quarter, sizeof bad);
	bad[4] = NotANumber();
	Check(CePlayUnitCameraBuild(&rig, kFootLo, kFootHi, CePlayUnitCameraFootLaw(), eye) && rig.firstPerson, "first: an eye inside the bounds is accepted");
	const float turned[3] = {kPlace[0] - 0.1f, kPlace[1] + 0.1f, kPlace[2] - 0.9f}, upright[3] = {kPlace[0], kPlace[1], kPlace[2] - 0.9f};
	const CePlayUnitCameraPose withAxes = SeeFrom(s, rig, kPlace, 0.4f, 0.1f, quarter, 0), noAxes = SeeFrom(s, rig, kPlace, 0.4f, 0.1f, 0, 0),
		badAxes = SeeFrom(s, rig, kPlace, 0.4f, 0.1f, bad, 0);
	Check(withAxes.valid && Near3(withAxes.eye, turned, 1e-5f), "first: the eye turns with the unit's axes");
	Check(noAxes.valid && Near3(noAxes.eye, upright, 1e-5f), "first: with no axes the eye is straight above the root");
	Check(badAxes.valid && Near3(badAxes.eye, upright, 1e-5f), "first: axes that are not finite are not used");

	// no eye, an eye outside the model and an eye that is not a number: the shoulder views remain and first person does not exist
	const float above[3] = {0.f, 0.f, -3.f}, beside[3] = {0.5f, 0.f, -0.9f};
	float broken[3] = {0.f, 0.f, 0.f};
	broken[2] = NotANumber();
	Check(CePlayUnitCameraBuild(&rig, kFootLo, kFootHi, CePlayUnitCameraFootLaw(), 0) && rig.valid && !rig.firstPerson, "first: no eye given, none invented");
	Check(CePlayUnitCameraBuild(&rig, kFootLo, kFootHi, CePlayUnitCameraFootLaw(), above) && rig.valid && !rig.firstPerson, "first: an eye above the model is refused");
	Check(CePlayUnitCameraBuild(&rig, kFootLo, kFootHi, CePlayUnitCameraFootLaw(), beside) && rig.valid && !rig.firstPerson, "first: an eye beside the model is refused");
	Check(CePlayUnitCameraBuild(&rig, kFootLo, kFootHi, CePlayUnitCameraFootLaw(), broken) && rig.valid && !rig.firstPerson, "first: an eye that is not a number is refused");

	// a unit without first person: Zoom In stops at the close view, and a state that asks for the eye is shown and brought to the close view
	s = At(kCePlayUnitCameraClose);
	Check(CePlayUnitCameraZoom(&s, hull, true, false) == kCePlayUnitCameraClose, "first: Zoom In on a unit without an eye stays at the close view");
	s = At(kCePlayUnitCameraFirst);
	const CePlayUnitCameraPose h = See(s, hull, 0.3f, 0.f);
	Check(h.valid && h.first == 0.f && !h.firstPerson && h.rail == 1.f && Near(h.arm, hull.closeDistance, 1e-4f), "first: without an eye the pose is the close view");
	CePlayUnitCameraAdvance(&s, hull, kActor, 1.f / 60.f);
	Check(s.view == kCePlayUnitCameraClose && s.rail == 1.f && s.rate == 0.f, "first: without an eye the state comes to rest at the close view");

	// the eye withdrawn while it is in use (the same unit, its rig made again without one)
	s = At(kCePlayUnitCameraFirst);
	CePlayUnitCameraBuild(&rig, kFootLo, kFootHi, CePlayUnitCameraFootLaw(), 0);
	CePlayUnitCameraAdvance(&s, rig, kActor, 1.f / 60.f);
	Check(s.view == kCePlayUnitCameraClose && s.rail == 1.f && s.rate == 0.f, "first: an eye withdrawn leaves the close view, at once");
}

// Zoom In and Zoom Out: one step each, both ends, both at once, and nothing without a unit.
static void TestZoom()
{
	CePlayUnitCameraRig foot, none;
	CePlayUnitCameraState s = At(kCePlayUnitCameraWide);
	BuildFoot(&foot, 1.f);
	CePlayUnitCameraBuild(&none, 0, 0, CePlayUnitCameraFootLaw(), 0);

	Check(CePlayUnitCameraZoom(&s, foot, true, false) == kCePlayUnitCameraClose, "zoom: In from the wide view asks for the close view");
	Check(s.rail == 0.f && s.rate == 0.f, "zoom: a step alone moves nothing until time passes");
	Check(CePlayUnitCameraZoom(&s, foot, true, false) == kCePlayUnitCameraFirst, "zoom: In from the close view asks for the eye");
	Check(CePlayUnitCameraZoom(&s, foot, true, false) == kCePlayUnitCameraFirst, "zoom: In at the eye stays there");
	Check(CePlayUnitCameraZoom(&s, foot, false, true) == kCePlayUnitCameraClose, "zoom: Out from the eye asks for the close view");
	Check(CePlayUnitCameraZoom(&s, foot, false, true) == kCePlayUnitCameraWide, "zoom: Out from the close view asks for the wide view");
	Check(CePlayUnitCameraZoom(&s, foot, false, true) == kCePlayUnitCameraWide, "zoom: Out at the wide view stays there");

	s = At(kCePlayUnitCameraClose);
	Check(CePlayUnitCameraZoom(&s, foot, true, true) == kCePlayUnitCameraClose && s.view == kCePlayUnitCameraClose, "zoom: In and Out together are no step");
	Check(CePlayUnitCameraZoom(&s, foot, false, false) == kCePlayUnitCameraClose, "zoom: no edge is no step");
	s = At(kCePlayUnitCameraWide);
	Check(CePlayUnitCameraZoom(&s, foot, true, true) == kCePlayUnitCameraWide, "zoom: In and Out together at an end are no step");

	CePlayUnitCameraReset(&s, 0, kCePlayUnitCameraClose);
	Check(CePlayUnitCameraZoom(&s, foot, true, false) == kCePlayUnitCameraClose, "zoom: with nothing held nothing changes");
	s = At(kCePlayUnitCameraClose);
	Check(CePlayUnitCameraZoom(&s, none, true, false) == kCePlayUnitCameraClose && CePlayUnitCameraZoom(&s, none, false, true) == kCePlayUnitCameraClose,
		"zoom: with no bounds nothing changes");
}

// Taking a unit, handing it back and changing unit never blend from one to another.
static void TestReset()
{
	CePlayUnitCameraRig foot, hull;
	CePlayUnitCameraState s = At(kCePlayUnitCameraWide);
	BuildFoot(&foot, 1.f);
	BuildHull(&hull, 1.f);

	Check(s.identity == kActor && s.view == kCePlayUnitCameraWide && s.rail == 0.f && s.rate == 0.f, "reset: at rest in the view asked for");
	CePlayUnitCameraReset(&s, kActor, 99);
	Check(s.view == kCePlayUnitCameraFirst && s.rail == 2.f, "reset: a view past the eye is the eye");
	CePlayUnitCameraReset(&s, kActor, -5);
	Check(s.view == kCePlayUnitCameraWide && s.rail == 0.f, "reset: a view before the wide one is the wide one");

	// mid-move, another unit is held: the camera is at rest on the new unit in the same frame
	CePlayUnitCameraZoom(&s, foot, true, false);
	Run(&s, foot, 1.f / 60.f, 5);
	Check(s.rail > 0.f && s.rail < 1.f && s.rate > 0.f, "reset: (the camera is mid-move)");
	CePlayUnitCameraAdvance(&s, hull, kOther, 1.f / 60.f);
	Check(s.identity == kOther && s.view == kCePlayUnitCameraClose && s.rail == 1.f && s.rate == 0.f, "reset: a change of unit is at rest at once, nothing blended");

	// mid-move at the eye, a unit with no eye is taken
	s = At(kCePlayUnitCameraClose);
	CePlayUnitCameraZoom(&s, foot, true, false);
	Run(&s, foot, 1.f / 60.f, 5);
	CePlayUnitCameraAdvance(&s, hull, kOther, 1.f / 60.f);
	Check(s.view == kCePlayUnitCameraClose && s.rail == 1.f && s.rate == 0.f, "reset: a change to a unit without an eye rests at its close view");

	// handing back mid-move, then taking the same unit again
	s = At(kCePlayUnitCameraWide);
	CePlayUnitCameraZoom(&s, foot, true, false);
	Run(&s, foot, 1.f / 60.f, 5);
	CePlayUnitCameraReset(&s, 0, kCePlayUnitCameraClose);
	Check(s.identity == 0 && s.rail == 1.f && s.rate == 0.f, "reset: handing back leaves nothing in motion");
	CePlayUnitCameraAdvance(&s, foot, 0, 1.f / 60.f);
	Check(s.identity == 0 && s.rail == 1.f && s.rate == 0.f, "reset: time passing with nothing held moves nothing");
	CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 60.f);
	Check(s.identity == kActor && s.rail == 1.f && s.rate == 0.f, "reset: the unit taken again starts at rest");
}

// One step of the rail: straight there, never past, done in a bounded time.
static void TestTransition()
{
	CePlayUnitCameraRig foot;
	CePlayUnitCameraState s = At(kCePlayUnitCameraWide);
	bool forward = true, inside = true;
	float last = 0.f, afterOne = 0.f, early = 0.f, late = 0.f;
	int i;
	BuildFoot(&foot, 1.f);
	CePlayUnitCameraZoom(&s, foot, true, false);
	for (i = 1; i <= 60; ++i)
	{
		CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 60.f);
		forward = forward && s.rail >= last;
		inside = inside && s.rail >= 0.f && s.rail <= 1.f;
		last = s.rail;
		if (i == 1) afterOne = s.rail;
		if (i == 3) early = s.rail;
		if (i == 21) late = s.rail;
	}
	Check(afterOne > 0.f, "move: the first frame already moves");
	Check(early < 0.5f, "move: it is a move, not a jump");
	Check(late >= 0.9f, "move: nine tenths done within 0.35 s");
	Check(forward && inside, "move: straight toward the view asked for, never past it");
	Check(s.rail == 1.f && s.rate == 0.f, "move: exactly at rest within a second");
	Run(&s, foot, 1.f / 60.f, 30);
	Check(s.rail == 1.f && s.rate == 0.f, "move: at rest it stays at rest");
}

// Reversals and repeated taps.
static void TestTaps()
{
	CePlayUnitCameraRig foot;
	CePlayUnitCameraState s;
	bool ok;
	float last, pressed;
	int i;
	BuildFoot(&foot, 1.f);

	// out again a tenth of a second into a move in: back at once, from where it was
	s = At(kCePlayUnitCameraWide);
	CePlayUnitCameraZoom(&s, foot, true, false);
	Run(&s, foot, 1.f / 60.f, 6);
	pressed = last = s.rail;
	Check(pressed > 0.1f && pressed < 0.9f && s.rate > 0.f, "taps: (the camera is mid-move, moving in)");
	CePlayUnitCameraZoom(&s, foot, false, true);
	ok = true;
	for (i = 0; i < 90; ++i)
	{
		CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 60.f);
		ok = ok && s.rail <= last && s.rail >= 0.f;
		if (i == 0) Check(s.rail < pressed, "taps: the frame after a reversal already moves back");
		last = s.rail;
	}
	Check(ok, "taps: after a reversal the camera never carries on the old way");
	Check(s.rail == 0.f && s.rate == 0.f, "taps: a reversal ends exactly where it began");

	// a second tap in mid-move: straight through the close view to the eye
	s = At(kCePlayUnitCameraWide);
	CePlayUnitCameraZoom(&s, foot, true, false);
	Run(&s, foot, 1.f / 60.f, 6);
	CePlayUnitCameraZoom(&s, foot, true, false);
	ok = true;
	last = s.rail;
	for (i = 0; i < 90; ++i)
	{
		CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 60.f);
		ok = ok && s.rail >= last && s.rail <= 2.f;
		last = s.rail;
	}
	Check(ok && s.rail == 2.f && s.rate == 0.f, "taps: two taps in go straight to the eye and stop on it");

	// two taps in, then one out just short of the close view while moving fast: it arrives at the close view and does not swing past it
	s = At(kCePlayUnitCameraWide);
	CePlayUnitCameraZoom(&s, foot, true, false);
	CePlayUnitCameraZoom(&s, foot, true, false);
	for (i = 0; i < 240 && s.rail <= 0.85f; ++i) CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 120.f);
	Check(s.rail > 0.85f && s.rail < 1.f && s.rate > kCePlayUnitCameraOmega * (1.f - s.rail), "taps: (the camera is just short of the close view, too fast to stop on it)");
	CePlayUnitCameraZoom(&s, foot, false, true);
	ok = true;
	last = s.rail;
	for (i = 0; i < 180; ++i)
	{
		CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 120.f);
		ok = ok && s.rail >= last && s.rail <= 1.f;
		last = s.rail;
	}
	Check(ok, "taps: a tap that shortens the move does not swing the camera past the view asked for");
	Check(s.rail == 1.f && s.rate == 0.f, "taps: it stops exactly on the view asked for");

	// In and Out on alternate frames: the camera stays between the two views, and comes to rest on the last one asked for
	s = At(kCePlayUnitCameraClose);
	ok = true;
	for (i = 0; i < 200; ++i)
	{
		CePlayUnitCameraZoom(&s, foot, (i & 1) == 0, (i & 1) != 0);
		CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 120.f);
		ok = ok && s.rail >= 1.f && s.rail <= 2.f;
	}
	Check(ok, "taps: hammering In and Out keeps the camera between the two views");
	Run(&s, foot, 1.f / 120.f, 120);
	Check(s.rail == (float)s.view && s.rate == 0.f, "taps: after hammering it rests exactly on the last view asked for");

	// any taps at any frame times: always on the rail, always a number, and at rest on a view a second after the last tap
	s = At(kCePlayUnitCameraClose);
	ok = true;
	unsigned seed = 12345u;
	for (i = 0; i < 2000; ++i)
	{
		seed = seed * 1664525u + 1013904223u;
		const unsigned bits = (seed >> 16) & 0xffffu;
		const float dt = (bits & 3u) == 0u ? 1.f / 30.f : (bits & 3u) == 1u ? 1.f / 60.f : 1.f / 120.f;
		CePlayUnitCameraZoom(&s, foot, (bits & 0x30u) == 0x10u, (bits & 0xc0u) == 0x40u);
		CePlayUnitCameraAdvance(&s, foot, kActor, dt);
		ok = ok && s.rail >= 0.f && s.rail <= 2.f && CePlayUnitCameraFinite(s.rate);
	}
	Check(ok, "taps: no run of taps takes the camera off the rail");
	Run(&s, foot, 1.f / 60.f, 60);
	Check(s.rail == (float)s.view && s.rate == 0.f, "taps: any run of taps ends at rest on a view");
}

// Two taps in from the wide view, run for some seconds at a frame rate; optionally one tap out a tenth of a second in.
static float RailAfter(const CePlayUnitCameraRig &rig, int fps, float seconds, bool reverse)
{
	CePlayUnitCameraState s = At(kCePlayUnitCameraWide);
	const float dt = 1.f / (float)fps;
	const int frames = (int)(seconds * (float)fps + 0.5f);
	int i;
	CePlayUnitCameraZoom(&s, rig, true, false);
	CePlayUnitCameraZoom(&s, rig, true, false);
	for (i = 0; i < frames; ++i)
	{
		if (reverse && i == fps / 10) CePlayUnitCameraZoom(&s, rig, false, true);
		CePlayUnitCameraAdvance(&s, rig, kActor, dt);
	}
	return s.rail;
}

// The same move at 30, 60 and 120 frames a second, and at uneven frame times.
static void TestFrameRates()
{
	CePlayUnitCameraRig foot;
	CePlayUnitCameraState s, t;
	int i;
	BuildFoot(&foot, 1.f);

	const float w = kCePlayUnitCameraOmega;
	const float exact = 2.f * (1.f - (1.f + w * 0.3f) * expf(-w * 0.3f));
	const float a = RailAfter(foot, 30, 0.3f, false), b = RailAfter(foot, 60, 0.3f, false), c = RailAfter(foot, 120, 0.3f, false);
	Check(Near(a, b, 1e-3f) && Near(b, c, 1e-3f), "rates: the same place 0.3 s into a move at 30, 60 and 120 frames a second");
	Check(Near(a, exact, 2e-3f) && Near(c, exact, 2e-3f), "rates: and it is where the settling law says");
	Check(RailAfter(foot, 30, 1.f, false) == 2.f && RailAfter(foot, 60, 1.f, false) == 2.f && RailAfter(foot, 120, 1.f, false) == 2.f,
		"rates: exactly at the eye after a second at every rate");

	const float ra = RailAfter(foot, 30, 0.2f, true), rb = RailAfter(foot, 60, 0.2f, true), rc = RailAfter(foot, 120, 0.2f, true);
	Check(Near(ra, rb, 1e-3f) && Near(rb, rc, 1e-3f) && ra < 1.f, "rates: a tap out a tenth of a second in leaves the same place at every rate");
	Check(RailAfter(foot, 30, 1.f, true) == 1.f && RailAfter(foot, 60, 1.f, true) == 1.f && RailAfter(foot, 120, 1.f, true) == 1.f,
		"rates: and exactly the close view after a second at every rate");

	// uneven frames that add up to the same 0.3 s
	s = At(kCePlayUnitCameraWide);
	CePlayUnitCameraZoom(&s, foot, true, false);
	CePlayUnitCameraZoom(&s, foot, true, false);
	for (i = 0; i < 4; ++i)
	{
		CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 30.f);
		CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 60.f);
		CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 120.f);
		CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 60.f);
	}
	Check(Near(s.rail, exact, 2e-3f), "rates: uneven frame times end in the same place");

	// one long frame and two short ones
	s = At(kCePlayUnitCameraWide);
	t = s;
	CePlayUnitCameraZoom(&s, foot, true, false);
	CePlayUnitCameraZoom(&t, foot, true, false);
	CePlayUnitCameraAdvance(&s, foot, kActor, 0.2f);
	CePlayUnitCameraAdvance(&t, foot, kActor, 0.1f);
	CePlayUnitCameraAdvance(&t, foot, kActor, 0.1f);
	Check(Near(s.rail, t.rail, 1e-4f) && Near(s.rate, t.rate, 1e-3f), "rates: one long frame is two short ones");
}

// Time that is no time.
static void TestTime()
{
	static const float kinds[4] = {0.f, 0.f, -1.f / 60.f, 0.5f};
	CePlayUnitCameraRig foot;
	CePlayUnitCameraState s;
	float bad[4];
	int i;
	BuildFoot(&foot, 1.f);
	memcpy(bad, kinds, sizeof bad);
	bad[0] = NotANumber();
	bad[1] = Endless();

	s = At(kCePlayUnitCameraWide);
	CePlayUnitCameraZoom(&s, foot, true, false);
	CePlayUnitCameraAdvance(&s, foot, kActor, 0.f);
	Check(s.rail == 0.f && s.rate == 0.f && s.view == kCePlayUnitCameraClose, "time: no time passing moves nothing and forgets nothing");
	Run(&s, foot, 1.f / 60.f, 4);
	const float mid = s.rail, rate = s.rate;
	CePlayUnitCameraAdvance(&s, foot, kActor, 0.f);
	Check(s.rail == mid && s.rate == rate, "time: no time passing mid-move holds the move as it is");

	for (i = 0; i < 4; ++i)
	{
		s = At(kCePlayUnitCameraWide);
		CePlayUnitCameraZoom(&s, foot, true, false);
		Run(&s, foot, 1.f / 60.f, 4);
		CePlayUnitCameraAdvance(&s, foot, kActor, bad[i]);
		Check(s.rail == 1.f && s.rate == 0.f, "time: a time that is not a number, endless, negative or too long ends the move where it was going");
	}

	s = At(kCePlayUnitCameraWide);
	CePlayUnitCameraZoom(&s, foot, true, false);
	CePlayUnitCameraAdvance(&s, foot, kActor, kCePlayUnitCameraMaxStep);
	Check(s.rail > 0.5f && s.rail < 1.f, "time: the longest frame allowed still settles by the law");
}

// Bounds, laws, looks and states that are no use.
static void TestDegenerate()
{
	static const float zero[3] = {0.f, 0.f, 0.f};
	static const float lineLo[3] = {0.f, 0.f, -2.f}, lineHi[3] = {0.f, 0.f, 0.f};
	static const float flatLo[3] = {-1.f, -1.f, 0.f}, flatHi[3] = {1.f, 1.f, 0.f};
	const CePlayUnitCameraLaw &law = CePlayUnitCameraFootLaw();
	CePlayUnitCameraRig rig, foot;
	CePlayUnitCameraState s;
	CePlayUnitCameraPose pose;
	CePlayUnitCameraLaw wrong;
	float lo[3], hi[3], eye[3], place[3];
	BuildFoot(&foot, 1.f);

	Check(!CePlayUnitCameraBuild(&rig, 0, 0, law, 0) && !rig.valid, "bounds: none given, no rig");
	Check(!CePlayUnitCameraBuild(&rig, kFootLo, 0, law, 0) && !rig.valid, "bounds: half given, no rig");
	Check(!CePlayUnitCameraBuild(&rig, kFootHi, kFootLo, law, 0) && !rig.valid, "bounds: the wrong way round, no rig");
	Check(!CePlayUnitCameraBuild(&rig, zero, zero, law, 0) && !rig.valid, "bounds: of no size, no rig");
	memcpy(lo, kFootLo, sizeof lo);
	memcpy(hi, kFootHi, sizeof hi);
	lo[1] = NotANumber();
	Check(!CePlayUnitCameraBuild(&rig, lo, hi, law, 0) && !rig.valid, "bounds: not a number, no rig");
	memcpy(lo, kFootLo, sizeof lo);
	hi[2] = Endless();
	Check(!CePlayUnitCameraBuild(&rig, lo, hi, law, 0) && !rig.valid, "bounds: endless, no rig");
	Check(!CePlayUnitCameraFootEye(0, 0, eye) && !CePlayUnitCameraFootEye(lo, hi, eye), "bounds: no eye from bounds that are no use");
	Check(CePlayUnitCameraBuild(&rig, lineLo, lineHi, law, 0) && rig.valid && rig.radius == 0.f && Near(rig.span, 2.f, 1e-6f), "bounds: a bare height is still a size");
	Check(CePlayUnitCameraBuild(&rig, flatLo, flatHi, law, 0) && rig.valid && rig.span > 0.f, "bounds: a bare footprint is still a size");
	Check(!CePlayUnitCameraFootEye(flatLo, flatHi, eye), "bounds: no figure's eye without a height");

	wrong = law;
	wrong.close = 0.f;
	Check(!CePlayUnitCameraBuild(&rig, kFootLo, kFootHi, wrong, 0) && !rig.valid, "law: no close distance, no rig");
	wrong = law;
	wrong.wide = 0.5f * law.close;
	Check(!CePlayUnitCameraBuild(&rig, kFootLo, kFootHi, wrong, 0) && !rig.valid, "law: a wide view nearer than the close one, no rig");
	wrong = law;
	wrong.lift = NotANumber();
	Check(!CePlayUnitCameraBuild(&rig, kFootLo, kFootHi, wrong, 0) && !rig.valid, "law: not a number, no rig");

	// with no rig there is no pose, no step, and nothing that is not a number
	s = At(kCePlayUnitCameraClose);
	memset(&pose, 0, sizeof pose);
	pose.valid = true;
	Check(!CePlayUnitCameraPoseFor(s, rig, kPlace, 0.f, 0.f, 0, 0, &pose) && !pose.valid, "bounds: no rig, no pose (the caller keeps its own camera)");
	CePlayUnitCameraAdvance(&s, rig, kActor, NotANumber());
	Check(s.rail == 1.f && s.rate == 0.f && s.view == kCePlayUnitCameraClose, "bounds: no rig, the state rests where it was");
	s = At(kCePlayUnitCameraFirst);
	CePlayUnitCameraAdvance(&s, rig, kActor, 1.f / 60.f);
	Check(s.view == kCePlayUnitCameraFirst && s.rail == 2.f, "bounds: no rig for a frame does not forget the view asked for");

	// a place or a look that is not a number
	s = At(kCePlayUnitCameraClose);
	memcpy(place, kPlace, sizeof place);
	place[0] = NotANumber();
	pose.valid = true;
	Check(!CePlayUnitCameraPoseFor(s, foot, place, 0.f, 0.f, 0, 0, &pose) && !pose.valid, "look: a place that is not a number, no pose");
	pose.valid = true;
	Check(!CePlayUnitCameraPoseFor(s, foot, 0, 0.f, 0.f, 0, 0, &pose) && !pose.valid, "look: no place, no pose");
	pose.valid = true;
	Check(!CePlayUnitCameraPoseFor(s, foot, kPlace, Endless(), 0.f, 0, 0, &pose) && !pose.valid, "look: an endless yaw, no pose");
	pose.valid = true;
	Check(!CePlayUnitCameraPoseFor(s, foot, kPlace, 0.f, NotANumber(), 0, 0, &pose) && !pose.valid, "look: a pitch that is not a number, no pose");
	s.rail = NotANumber();
	pose.valid = true;
	Check(!CePlayUnitCameraPoseFor(s, foot, kPlace, 0.f, 0.f, 0, 0, &pose) && !pose.valid, "look: a rail that is not a number, no pose");

	// a state that is rubbish comes to rest on a real view at the next frame
	s.identity = kActor;
	s.view = 77;
	s.rail = NotANumber();
	s.rate = Endless();
	CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 60.f);
	Check(s.view == kCePlayUnitCameraFirst && s.rail == 2.f && s.rate == 0.f, "state: rubbish comes to rest on the nearest real view");
	s.view = -3;
	s.rail = -40.f;
	s.rate = 0.f;
	CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 60.f);
	Check(s.view == kCePlayUnitCameraWide && s.rail == 0.f && s.rate == 0.f, "state: a rail off its end comes to rest on the end");
	s = AtRail(-3.f);
	Check(See(s, foot, 0.f, 0.f).rail == 0.f, "state: a rail before the wide view is shown as the wide view");
	s = AtRail(9.f);
	Check(See(s, foot, 0.f, 0.f).rail == 2.f, "state: a rail past the eye is shown as the eye");
}

// The look is the view's orientation, untouched, in every view and mid-move; and the pose has no memory of an earlier look.
static void TestLook()
{
	static const float yaws[5] = {-3.f, -1.2f, 0.f, 0.7f, 2.9f};
	static const float pitches[3] = {kLookUp, 0.f, kLookDown};
	static const float rails[5] = {0.f, 0.5f, 1.f, 1.4f, 2.f};
	CePlayUnitCameraRig foot, hull;
	float r[3], f[3], d[3];
	bool axes = true, unit = true, orbit = true, anchored = true;
	int i, j, k;
	BuildFoot(&foot, 1.f);
	BuildHull(&hull, 1.f);
	const float arm = sqrtf(foot.closeDistance * foot.closeDistance + foot.side * foot.side);
	const float pivot[3] = {kPlace[0], kPlace[1], kPlace[2] - foot.lift};

	for (i = 0; i < 5; ++i)
		for (j = 0; j < 3; ++j)
		{
			LookAxes(yaws[i], pitches[j], r, f, d);
			for (k = 0; k < 5; ++k)
			{
				const CePlayUnitCameraPose p = See(AtRail(rails[k]), foot, yaws[i], pitches[j]);
				axes = axes && p.valid && Near3(p.right, r, 1e-6f) && Near3(p.forward, f, 1e-6f) && Near3(p.down, d, 1e-6f);
				unit = unit && Near(Dot(p.right, p.right), 1.f, 1e-5f) && Near(Dot(p.forward, p.forward), 1.f, 1e-5f) && Near(Dot(p.down, p.down), 1.f, 1e-5f)
					&& Near(Dot(p.right, p.forward), 0.f, 1e-5f) && Near(Dot(p.right, p.down), 0.f, 1e-5f) && Near(Dot(p.forward, p.down), 0.f, 1e-5f)
					&& p.right[2] == 0.f;
			}
			// at the close view the camera keeps its arm about a pivot that does not move, however the look turns (level and looking down, where nothing shortens it)
			const CePlayUnitCameraPose c = See(At(kCePlayUnitCameraClose), foot, yaws[i], pitches[j]);
			if (pitches[j] >= 0.f) orbit = orbit && Near(Gap(c.eye, c.anchor), arm, 1e-4f) && Near(c.arm, arm, 1e-5f);
			anchored = anchored && Near3(c.anchor, pivot, 1e-5f);
		}
	Check(axes, "look: the view's axes are the look's own, in every view and mid-move");
	Check(unit, "look: the axes are of unit length, square to each other, and never rolled");
	Check(orbit, "look: the camera keeps its exact arm however the look turns");
	Check(anchored, "look: the pivot does not move with the look");

	// no memory: the pose for a look is the same whatever was looked at before, and asking for a pose changes nothing
	CePlayUnitCameraState s = At(kCePlayUnitCameraClose);
	const CePlayUnitCameraState before = s;
	const CePlayUnitCameraPose first = See(s, foot, 0.2f, 0.1f), second = See(s, foot, 1.9f, -0.3f), fresh = See(At(kCePlayUnitCameraClose), foot, 1.9f, -0.3f);
	Check(first.valid && second.valid && Near3(second.eye, fresh.eye, 1e-5f) && Near3(second.forward, fresh.forward, 1e-6f),
		"look: a turn shows in full in the same frame, with no lag");
	Check(s.identity == before.identity && s.view == before.view && s.rail == before.rail && s.rate == before.rate, "look: asking for a pose changes no state");

	// a Zoom and a turn in the same frame: the turn is whole, the move has only begun
	CePlayUnitCameraZoom(&s, foot, true, false);
	CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 60.f);
	const CePlayUnitCameraPose both = See(s, foot, -2.2f, 0.25f);
	LookAxes(-2.2f, 0.25f, r, f, d);
	Check(both.valid && Near3(both.forward, f, 1e-6f) && Near3(both.right, r, 1e-6f) && both.first > 0.f && both.first < 0.2f,
		"look: a Zoom and a turn together: the turn is whole at once and the move has only begun");

#ifdef CE_CAMERA_DRIVER_HAS_LOOK
	// against the shipped look header itself: its axes, its limits, and its own camera place for a centred rig
	bool shipped = true, placed = true;
	Check(CePlayUnitTankLook().pitchUp == kLookUp && CePlayUnitTankLook().pitchDown == kLookDown, "look: these checks use the shipped look's limits");
	CePlayUnitLookLaw law = CePlayUnitTankLook();
	law.distance = hull.closeDistance;
	law.lift = hull.lift;
	for (i = 0; i < 5; ++i)
		for (j = 0; j < 3; ++j)
		{
			float eye[3];
			const CePlayUnitCameraPose p = See(At(kCePlayUnitCameraClose), hull, yaws[i], pitches[j]);
			CePlayUnitLookAxes(yaws[i], pitches[j], r, f, d);
			shipped = shipped && Near3(p.right, r, 1e-6f) && Near3(p.forward, f, 1e-6f) && Near3(p.down, d, 1e-6f);
			CePlayUnitLookEye(kPlace, yaws[i], pitches[j], law, eye);
			placed = placed && Near3(p.eye, eye, 1e-3f);
		}
	Check(shipped, "look: the axes are the shipped look's axes");
	Check(placed, "look: a centred rig hangs the camera where the shipped look does for the same distance and lift");
#endif
}

// The shoulder views: where the camera is, and what going from one to the other does.
static void TestShoulder()
{
	CePlayUnitCameraRig foot, hull, high;
	CePlayUnitCameraLaw law;
	float want[3], across[3];
	int i;
	BuildFoot(&foot, 1.f);
	BuildHull(&hull, 1.f);
	const float quarterTurn = 1.5707964f;

	// looking north and level: behind is south, right is east, above is -z
	CePlayUnitCameraPose p = See(At(kCePlayUnitCameraClose), foot, 0.f, 0.f);
	want[0] = kPlace[0] + foot.side; want[1] = kPlace[1] - foot.closeDistance; want[2] = kPlace[2] - foot.lift;
	Check(p.valid && Near3(p.eye, want, 1e-4f), "shoulder: the close view is behind, above and to the right");
	p = See(At(kCePlayUnitCameraWide), foot, 0.f, 0.f);
	want[1] = kPlace[1] - foot.wideDistance;
	Check(p.valid && Near3(p.eye, want, 1e-4f), "shoulder: the wide view is the same line, farther back");
	// looking west
	p = See(At(kCePlayUnitCameraClose), foot, quarterTurn, 0.f);
	want[0] = kPlace[0] + foot.closeDistance; want[1] = kPlace[1] + foot.side; want[2] = kPlace[2] - foot.lift;
	Check(p.valid && Near3(p.eye, want, 1e-4f), "shoulder: the camera turns about the pivot with the look");
	// looking down: the camera rises
	p = See(At(kCePlayUnitCameraClose), foot, 0.f, kLookDown);
	want[0] = kPlace[0] + foot.side; want[1] = kPlace[1] - foot.closeDistance * cosf(kLookDown); want[2] = kPlace[2] - foot.lift - foot.closeDistance * sinf(kLookDown);
	Check(p.valid && Near3(p.eye, want, 1e-4f), "shoulder: looking down lifts the camera along the look");

	Check(Near(Dot(p.eye, p.right) - Dot(p.anchor, p.right), foot.side, 1e-4f), "shoulder: a figure's camera is over its right shoulder");
	p = See(At(kCePlayUnitCameraClose), hull, 0.9f, 0.1f);
	Check(Near(Dot(p.eye, p.right) - Dot(p.anchor, p.right), 0.f, 1e-4f), "shoulder: a hull's camera is centred");

	// wide to close is a move straight along the look: the reticle's line is one line throughout
	const CePlayUnitCameraPose nearest = See(At(kCePlayUnitCameraClose), foot, 0.7f, 0.1f);
	bool oneLine = true;
	for (i = 0; i <= 10; ++i)
	{
		const CePlayUnitCameraPose q = See(AtRail(i == 10 ? 1.f : 0.1f * (float)i), foot, 0.7f, 0.1f);
		const float along = Dot(q.eye, nearest.forward) - Dot(nearest.eye, nearest.forward);
		across[0] = q.eye[0] - nearest.eye[0] - nearest.forward[0] * along;
		across[1] = q.eye[1] - nearest.eye[1] - nearest.forward[1] * along;
		across[2] = q.eye[2] - nearest.eye[2] - nearest.forward[2] * along;
		oneLine = oneLine && q.valid && Near(Dot(across, across), 0.f, 1e-8f) && along <= 1e-4f;
	}
	Check(oneLine, "shoulder: between the wide and close views the reticle's line does not shift");

	// a high look on a long arm: the camera comes in rather than hang below the unit's own ground level, and the view does not turn
	law = CePlayUnitCameraFootLaw();
	law.wide = 4.f;
	CePlayUnitCameraBuild(&high, kFootLo, kFootHi, law, 0);
	const float longArm = sqrtf(high.wideDistance * high.wideDistance + high.side * high.side);
	p = See(At(kCePlayUnitCameraWide), high, 0.4f, kLookUp);
	const CePlayUnitCameraPose level = See(At(kCePlayUnitCameraWide), high, 0.4f, 0.f);
	Check(p.valid && p.arm < longArm - 0.1f && Near(kPlace[2] - p.eye[2], high.ground, 1e-4f), "shoulder: a high look brings the camera in to the unit's ground level");
	Check(Near(level.arm, longArm, 1e-4f), "shoulder: a level look leaves the arm whole");
	Check(Near3(p.anchor, level.anchor, 1e-5f), "shoulder: coming in does not move the pivot");
	float r[3], f[3], d[3];
	LookAxes(0.4f, kLookUp, r, f, d);
	Check(Near3(p.forward, f, 1e-6f) && Near3(p.right, r, 1e-6f), "shoulder: coming in does not turn the view");
	law.ground = -100.f;
	CePlayUnitCameraBuild(&high, kFootLo, kFootHi, law, 0);
	Check(Near(See(At(kCePlayUnitCameraWide), high, 0.4f, kLookUp).arm, longArm, 1e-4f), "shoulder: a law with its ground far below leaves the arm whole");

	// the shipped laws at the look's highest tilt: never below the unit's own ground level
	p = See(At(kCePlayUnitCameraWide), foot, 0.4f, kLookUp);
	Check(p.valid && kPlace[2] - p.eye[2] >= foot.ground - 1e-4f, "shoulder: a figure's wide camera stays above its ground level at the highest look");
	p = See(At(kCePlayUnitCameraWide), hull, 0.4f, kLookUp);
	Check(p.valid && kPlace[2] - p.eye[2] >= hull.ground - 1e-3f, "shoulder: a hull's wide camera stays above its ground level at the highest look");
}

// What the adapter's queries allow is applied as given, and to the place only.
static void TestLimits()
{
	CePlayUnitCameraRig foot;
	CePlayUnitCameraLimits limits;
	const CePlayUnitCameraState s = At(kCePlayUnitCameraClose);
	float want[3];
	int i;
	BuildFoot(&foot, 1.f);
	const CePlayUnitCameraPose open = SeeFrom(s, foot, kPlace, 0.4f, 0.f, 0, 0);

	limits.arm = 0.6f;
	limits.headroom = -1.f;
	CePlayUnitCameraPose p = SeeFrom(s, foot, kPlace, 0.4f, 0.f, 0, &limits);
	for (i = 0; i < 3; ++i) want[i] = open.anchor[i] + (open.eye[i] - open.anchor[i]) * (0.6f / open.arm);
	Check(p.valid && Near(p.arm, 0.6f, 1e-6f) && Near(Gap(p.eye, p.anchor), 0.6f, 1e-4f), "limits: a short arm is the arm");
	Check(Near3(p.eye, want, 1e-4f) && Near3(p.anchor, open.anchor, 1e-5f), "limits: the camera comes in along its own arm");
	Check(Near3(p.forward, open.forward, 1e-6f) && Near3(p.right, open.right, 1e-6f) && Near3(p.down, open.down, 1e-6f), "limits: a short arm does not turn the view");
	Check(Near(p.aimStart, open.aimStart * (0.6f / open.arm), 1e-5f) && Near(Dot(p.anchor, p.forward) - Dot(p.eye, p.forward), p.aimStart, 1e-4f),
		"limits: the aim still starts at the unit's depth");

	limits.arm = 5.f;
	p = SeeFrom(s, foot, kPlace, 0.4f, 0.f, 0, &limits);
	Check(p.valid && Near3(p.eye, open.eye, 1e-5f) && Near(p.arm, open.arm, 1e-6f), "limits: an arm with room to spare changes nothing");
	limits.arm = 0.f;
	p = SeeFrom(s, foot, kPlace, 0.4f, 0.f, 0, &limits);
	Check(p.valid && p.arm == 0.f && Near3(p.eye, p.anchor, 0.f) && p.hideBody, "limits: no room at all puts the camera at the pivot and hides the body");
	limits.arm = NotANumber();
	p = SeeFrom(s, foot, kPlace, 0.4f, 0.f, 0, &limits);
	Check(p.valid && Near3(p.eye, open.eye, 1e-5f), "limits: an arm that is not a number is no limit");
	limits.arm = Endless();
	p = SeeFrom(s, foot, kPlace, 0.4f, 0.f, 0, &limits);
	Check(p.valid && Near3(p.eye, open.eye, 1e-5f), "limits: an endless arm is no limit");

	limits.arm = -1.f;
	limits.headroom = 0.5f;
	p = SeeFrom(s, foot, kPlace, 0.4f, 0.f, 0, &limits);
	Check(p.valid && Near(p.anchor[2], kPlace[2] - 0.5f, 1e-5f) && Near(p.arm, open.arm, 1e-5f), "limits: low headroom lowers the pivot and leaves the arm");
	Check(Near3(p.forward, open.forward, 1e-6f), "limits: low headroom does not turn the view");
	limits.headroom = 5.f;
	p = SeeFrom(s, foot, kPlace, 0.4f, 0.f, 0, &limits);
	Check(p.valid && Near3(p.eye, open.eye, 1e-5f), "limits: headroom to spare changes nothing");
	limits.headroom = NotANumber();
	p = SeeFrom(s, foot, kPlace, 0.4f, 0.f, 0, &limits);
	Check(p.valid && Near3(p.eye, open.eye, 1e-5f), "limits: headroom that is not a number is no limit");

	// first person under a low ceiling: the eye comes down, the look is the same
	limits.headroom = 0.5f;
	p = SeeFrom(At(kCePlayUnitCameraFirst), foot, kPlace, 0.4f, 0.f, 0, &limits);
	Check(p.valid && p.firstPerson && Near(p.eye[2], kPlace[2] - 0.5f, 1e-5f) && Near3(p.forward, open.forward, 1e-6f), "limits: low headroom lowers the eye");
}

// When the body is in the camera's way.
static void TestHide()
{
	CePlayUnitCameraRig foot, hull;
	CePlayUnitCameraState s;
	int i, flips = 0;
	bool hidden;
	BuildFoot(&foot, 1.f);
	BuildHull(&hull, 1.f);

	Check(!See(At(kCePlayUnitCameraWide), foot, 0.5f, 0.1f).hideBody && !See(At(kCePlayUnitCameraClose), foot, 0.5f, 0.1f).hideBody, "hide: a figure is drawn in both shoulder views");
	Check(!See(At(kCePlayUnitCameraWide), hull, 0.5f, kLookUp).hideBody && !See(At(kCePlayUnitCameraClose), hull, 0.5f, kLookUp).hideBody, "hide: a hull is drawn in both of its views");
	Check(See(At(kCePlayUnitCameraFirst), foot, 0.5f, 0.1f).hideBody, "hide: a figure is hidden at its eye");
	const CePlayUnitCameraPose p = See(At(kCePlayUnitCameraClose), foot, 0.f, 0.f);
	Check(Near(p.clearance, sqrtf(foot.closeDistance * foot.closeDistance + foot.side * foot.side) - foot.radius, 1e-4f), "hide: the clearance is the level gap to the bounds");

	s = At(kCePlayUnitCameraClose);
	CePlayUnitCameraZoom(&s, foot, true, false);
	hidden = See(s, foot, 0.5f, 0.1f).hideBody;
	for (i = 0; i < 120; ++i)
	{
		CePlayUnitCameraAdvance(&s, foot, kActor, 1.f / 120.f);
		const bool now = See(s, foot, 0.5f, 0.1f).hideBody;
		if (now != hidden) ++flips;
		hidden = now;
	}
	Check(flips == 1 && hidden, "hide: on the way to the eye the body is hidden once and stays hidden");
}

// One ray for the view and the aim.
static void TestAim()
{
	CePlayUnitCameraRig foot;
	CePlayUnitCameraPose p, bad;
	float point[3] = {0, 0, 0}, want[3];
	int i;
	BuildFoot(&foot, 1.f);

	p = See(At(kCePlayUnitCameraClose), foot, 0.6f, 0.f);
	Check(Near(p.aimStart, foot.closeDistance, 1e-5f) && Near(p.aimNear, foot.closeDistance + foot.span, 1e-5f), "aim: at the close view the aim starts at the unit's depth");
	p = See(At(kCePlayUnitCameraWide), foot, 0.6f, kLookDown);
	Check(Near(Dot(p.anchor, p.forward) - Dot(p.eye, p.forward), p.aimStart, 1e-4f), "aim: the start is the pivot's depth along the ray, at any tilt");

	Check(CePlayUnitCameraAimPoint(p, 50.f, point), "aim: a hit gives a point");
	for (i = 0; i < 3; ++i) want[i] = p.eye[i] + p.forward[i] * 50.f;
	Check(Near3(point, want, 1e-4f), "aim: the point is on the camera's ray at the hit");
	Check(CePlayUnitCameraAimPoint(p, 0.5f, point), "aim: a hit nearer than the unit gives a point");
	for (i = 0; i < 3; ++i) want[i] = p.eye[i] + p.forward[i] * p.aimNear;
	Check(Near3(point, want, 1e-4f), "aim: the point is never nearer than the nearest allowed");
	point[0] = point[1] = point[2] = 123.f;
	Check(!CePlayUnitCameraAimPoint(p, NotANumber(), point) && !CePlayUnitCameraAimPoint(p, Endless(), point) && point[0] == 123.f,
		"aim: a distance that is not finite gives no point and writes nothing");
	memset(&bad, 0, sizeof bad);
	Check(!CePlayUnitCameraAimPoint(bad, 50.f, point), "aim: a pose that is not valid gives no point");

	p = See(At(kCePlayUnitCameraFirst), foot, 0.6f, 0.1f);
	Check(p.aimStart == 0.f && Near(p.aimNear, foot.span, 1e-6f), "aim: in first person the aim starts at the eye");
	Check(CePlayUnitCameraAimPoint(p, 30.f, point), "aim: first person gives a point");
	for (i = 0; i < 3; ++i) want[i] = p.anchor[i] + p.forward[i] * 30.f;
	Check(Near3(point, want, 1e-4f), "aim: in first person the aim's origin is the rendered eye");
}

// Nothing accumulates: after any number of trips the camera is exactly where it started.
static void TestDrift()
{
	CePlayUnitCameraRig foot;
	CePlayUnitCameraState s = At(kCePlayUnitCameraClose);
	int i;
	BuildFoot(&foot, 1.f);
	const CePlayUnitCameraPose start = See(s, foot, 0.3f, 0.1f);
	for (i = 0; i < 50; ++i)
	{
		CePlayUnitCameraZoom(&s, foot, (i & 1) == 0, (i & 1) != 0);
		Run(&s, foot, 1.f / 60.f, 60);
		CePlayUnitCameraZoom(&s, foot, (i & 1) != 0, (i & 1) == 0);
		Run(&s, foot, 1.f / 60.f, 60);
	}
	const CePlayUnitCameraPose end = See(s, foot, 0.3f, 0.1f);
	Check(s.view == kCePlayUnitCameraClose && s.rail == 1.f && s.rate == 0.f, "drift: fifty trips each way end exactly at rest on the close view");
	Check(start.valid && end.valid && Near3(end.eye, start.eye, 1e-5f) && Near3(end.forward, start.forward, 1e-6f) && Near(end.arm, start.arm, 1e-6f),
		"drift: and the camera is where it started");
}

int main()
{
	TestRig();
	TestScale();
	TestFirstPerson();
	TestZoom();
	TestReset();
	TestTransition();
	TestTaps();
	TestFrameRates();
	TestTime();
	TestDegenerate();
	TestLook();
	TestShoulder();
	TestLimits();
	TestHide();
	TestAim();
	TestDrift();
	printf("ce_playunit_camera: %d checks, %d failed\n", gChecks, gFailures);
	return gFailures ? 1 : 0;
}
