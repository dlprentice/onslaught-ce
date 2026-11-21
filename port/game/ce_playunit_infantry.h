// View-relative walking for the optional infantry pilot.
#ifndef CE_PLAYUNIT_INFANTRY_H
#define CE_PLAYUNIT_INFANTRY_H
#include <math.h>

inline bool CePlayUnitFootDirection(float yaw, float forward, float back, float side, float &x, float &y)
{
	x = y = 0.f;
	if (!isfinite(yaw) || !isfinite(forward) || !isfinite(back) || !isfinite(side)) return false;
	const float ahead = forward - back, length = sqrtf(ahead*ahead + side*side);
	if (length < 0.12f) return false;
	const float a = ahead/length, s = side/length, cy = cosf(yaw), sy = sinf(yaw);
	x = -sy*a + cy*s;
	y = cy*a + sy*s;
	return true;
}
// A disappearing squad may leave its soldier independent. A replacement owner keeps
// the unit, its movement and its current orders.
inline bool CePlayUnitFootReturnsToAI(bool live, bool sameTeam, bool stillHeld, bool sameAssignment, bool independent)
{
    return live && sameTeam && stillHeld && (sameAssignment || independent);
}

#endif
