// CarrierGuide.cpp: reconstructed from CarrierGuide.obj (00421ea0..0042234e); Stuart's release has no copy. It
// turns the carrier towards its destination, banks it into the turn and flies it at four times its top speed.

#include "common.h"
#include "unit.h"
#include "CarrierGuide.h"

// FUNCTION: 0x00421ea0 init:ZERO_FVECTOR
// FUNCTION: 0x00421ec0 init:ID_FMATRIX

// An angle brought back into (-PI, PI] by one turn (as BomberGuide.cpp's).
static inline void WrapAngle(float &a)
{
	if (a > PI)
		a -= PI_M2;
	if (a < -PI)
		a += PI_M2;
}

// STAND-IN (ids) for CCarrierGuide::Unknown3 below: the bank angle's turn gap through a forced-inline helper whose
// eighteen named float locals take symbol ids. Retail's Elevation loads d.Z, then d.Y, and squares d.Y first
// (00422251..0042225f). VC6 squares the CSE temporary with the higher key first, the key being 3 * (id << 6) + 0x47 in
// 16 bits. d.Y's temporary is numbered at its first load, in the distance, and d.Z's later; without the helper they
// are t207 (0x8587) and t268 (0xce47) and d.Z is squared first (11/11). Each float local here costs one local id and
// one id of the function-wide kind-0xf pass from C2's 32-id blocks, and adds one temporary numbered between the two
// (the gap grows from 97 by one per local): with eighteen they are t247 (0xb587) and t2ba (0x0bc7, wrapped) and the
// function matches. Measured: 18..37 and 39..62 locals match; 15..17 square d.X before d.Z (6/6), 38 multiplies
// right * d's Y terms before its Z terms (0/4), 14 and 63..69 are 11/11. Integer or pointer copies cost one id each
// and need 72..165 and 167, the window of an unused-locals diagnostic; float copies of `current` move it onto
// the FPU where retail copies it with integer moves (0042205f), and AngleDifference inside the helper goes out of
// line. Each local is a copy consumed by the next and the last is the gap; forced inlining spends no inline budget
// (one site in `left`, which changes no decision), and the copies emit no code. Original spelling unknown.
static __forceinline float TurnGap(float gap)
{
	float	g0 = (float)fabs(gap);
	float	g1 = g0;
	float	g2 = g1;
	float	g3 = g2;
	float	g4 = g3;
	float	g5 = g4;
	float	g6 = g5;
	float	g7 = g6;
	float	g8 = g7;
	float	g9 = g8;
	float	g10 = g9;
	float	g11 = g10;
	float	g12 = g11;
	float	g13 = g12;
	float	g14 = g13;
	float	g15 = g14;
	float	g16 = g15;
	float	g17 = g16;
	return g17;
}

//******************************************************************************************
// The flight vector is published as the mech and infantry guides publish their walk: the matrix times a forward
// vector (0, speed, 0). Retail keeps the speed in that vector's Y (0042224d: esp+0x1c inside a 16-byte object at
// esp+0x18) and multiplies each row's Y by it (004222c4..004222e0). Both angles are wrapped by the guides' WrapAngle;
// with it the three FVector constructors inside operator-, GetColumnX and the matrix product stay out of line as in
// retail (00422000, 00422085, 004222ec) and the frame is 0x74.
// The flight vector replaces forward and is then published: the unit pointer read for the copy is loaded after the
// product's statement, so its alias class does not hold the escaped product temporary and each load of the copy is
// issued ahead of the previous store (004222f4..0042231a), as retail's are.
// The horizontal distance is stored, compared from the sqrt and reloaded for the pitch (00422033, 00422058) because the
// steering test recomputes the magnitude: the compare then reads the common temporary, which dies under distance and
// pushes distance off the x87 stack (C2's LIFO test) into Elevation's slot (esp+0x10, 00422273). With the stored
// distance tested instead: 16/16.
// Matched with three code-neutral STAND-INs:
// - STAND-IN (budget): the roll is an if/else. The second MagnitudeXY site spends 47 of the inline budget, and
//   Elevation's nested Magnitude (60) is then inlined only when the own size reaches 570: the if/else adds 11 over the
//   ternary (own size 574, budget 61 at the site; with the ternary 563, budget 58, Magnitude out of line, 15/15). The
//   earlier fit's brace pairs are not needed (twice and the plain if/else alone give 0/6 at own size 571).
// - STAND-IN (ids): the pitch reads d.Z through `const FVector &v = d;`, declared after the distance.
//   Written d.Z, it is the front-end symbol s9 (key 0x0120), which ranks right * d's Z product (0x3fe7) under the Y
//   product (0x9b47) and makes Elevation load d.Y first, where retail multiplies the Z terms first (004221a7) and loads
//   d.Z first (00422251); through the reference d.Z is a field symbol numbered after d.Y's. Without it: 15/21 with the
//   earlier fit's braces keeping the budget (the reference's local adds 5 to the own size; at 569 Magnitude is denied,
//   59 < 60). Declared at the pitch, after the yaw test: 11/11.
// - STAND-IN (ids): TurnGap above, eighteen float copies of the turn gap that number Elevation's d.Y temporary above
//   d.Z's. Without it: 11/11.
// The second MagnitudeXY site and TurnGap are the only inline decisions added to the plain form's.
// Specimen: BEA.exe.original.backup, SHA-256 74154bfae14ddc8ecb87a0766f5bc381c7b7f1ab334ed7a753040eda1e1e7750.
void CCarrierGuide::Unknown3()	// FUNCTION: 0x00421f90
{
	if (GetUnit()->IsDying())
		return;
	GetUnit()->SetUnknown14C(FVector(0, 0, 0));
	if (!GetMode())
		return;

	FVector	d = GetDestination() - GetUnit()->GetPos();
	float	yaw = GetUnit()->GetCurrentOrientation().mYaw;
	float	distance = d.MagnitudeXY();
	const FVector	&v = d;	// STAND-IN (ids), see above
	if (d.MagnitudeXY() > 2.0f)
		yaw = d.Azimuth();
	float	pitch = (float)atan2(v.Z, distance);
	float	current = GetUnit()->GetCurrentOrientation().mYaw;
	FVector	right = GetUnit()->GetOrientation().GetColumnX();

	switch (GetMode())
	{
	case 2:
		yaw -= PI_M2;
		pitch -= PI_M2;
		break;
	}
	WrapAngle(yaw);
	WrapAngle(pitch);

	// bank into the turn, at most PI/8
	float	diff = TurnGap(AngleDifference(current, yaw));	// STAND-IN (ids), see TurnGap
	if (diff > PI / 8)
		diff = PI / 8;
	float	roll;	// STAND-IN (budget): an if/else, see above
	if ((right * d) < 0.0)
		roll = diff;
	else
		roll = -diff;

	if (pitch > PI / 16)
		pitch = PI / 16;
	else if (pitch < -PI / 16)
		pitch = -PI / 16;

	if (fabsf(roll) > 0.1f)
		GetUnit()->SetUnknown174(1.0f);
	else
		GetUnit()->SetUnknown174(0);

	FVector	forward(0, (GetUnit()->GetMaxVelocity() / 20.0f) * 4.0f, 0);
	FMatrix	ori(d.Azimuth(), d.Elevation(), 0);
	forward = ori * forward;
	GetUnit()->SetUnknown14C(forward);
	GetUnit()->SetUnknown120(CEulerAngles(yaw, pitch, roll));
}
