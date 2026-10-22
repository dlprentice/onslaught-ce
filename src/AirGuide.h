// AirGuide.h: CAirGuide (0x30 bytes, vtable 005d8594), the guide of a flying unit (AirGuide.obj, 00402060..
// 004029dd). It keeps the lowest clearance over the ground around the unit (event 2000) and the nearest thing it
// could hit (event 2001), climbs or dives away from them, and banks into its turns. The bomber, carver, dive bomber
// and ground-attack guides derive from it.

#ifndef AIRGUIDE_H
#define AIRGUIDE_H

#include "Guide.h"
#include "activereader.h"

class CThing;

enum EAirGuideEvent
{
	AIR_GUIDE_CLEARANCE = 2000,
	AIR_GUIDE_NEAREST
};

class CAirGuide : public CGuide
{
public:
	CAirGuide(CUnit *unit);								// 00402150
	virtual void	HandleEvent(CEvent *event);			// 0 (004026e0)
	virtual void	Unknown3();							// 3 (00402280)

			void	FindNearest();						// 004027c0
			void	UpdateClearance();					// 004028e0

	float	GetClearance() { return mClearance; }
	CThing	*GetNearest() { return mNearest.ToRead(); }
	float					mClearance;					// +0x20: the unit's height over the ground or sea below it, least of a 9x9 sample
	SINT					mCellX;						// +0x24: the whole-unit position mClearance was measured at
	SINT					mCellY;						// +0x28
	CActiveReader<CThing>	mNearest;					// +0x2c: the nearest thing within 15 (FindNearest)
};

#endif
