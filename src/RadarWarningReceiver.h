// RadarWarningReceiver.h: CRadarWarningReceiver, the battle engine's incoming-missile sensor (Stuart's names from
// BattleEngine.cpp; his header is not in the release). Its scalar deleting destructor is 00405a20.

#ifndef RADARWARNINGRECEIVER_H
#define RADARWARNINGRECEIVER_H

#include "Equipment.h"
#include "SPtrSet.h"

class CThing;

class CInitRadarWarningReceiver : public CInitEquipment
{
public:
	float	mScanDistance;
	float	mUpdateFrequency;
};

#ifdef RADARWARNINGRECEIVER_CPP	// RadarWarningReceiver.cpp's view; its other includers keep the short one below
#include "activereader.h"

// A round seen coming at the battle engine (0x18 bytes, MEMTYPE_RADAR).
struct SRadarThreat
{
	SRadarThreat(float yaw, float distance, SINT number, CThing *round, float time)
	{
		mYaw = yaw;
		mDistance = distance;
		mThingNumber = number;
		mRound.SetReader(round);
		mSeen = TRUE;
		mTime = time;
	}

	float					mYaw;									// +0x00: the direction it comes from
	float					mDistance;								// +0x04
	SINT					mThingNumber;							// +0x08
	CActiveReader<CThing>	mRound;									// +0x0c
	float					mTime;									// +0x10: when it was first seen
	BOOL					mSeen;									// +0x14: seen on the last scan
};

class CRadarWarningReceiver : public CEquipment
{
public:
					~CRadarWarningReceiver();						// 004d6600
	virtual void	HandleEvent(CEvent *event);						// 0 (004d6a10)
	void			Init(CInitRadarWarningReceiver &init);			// 004d65a0
	void			Update();										// 004d66b0

	float					mScanDistance;							// +0x14
	float					mUpdateFrequency;						// +0x18
	SPtrSet<SRadarThreat>	mThreats;								// +0x1c
	BOOL					mNewThreat;								// +0x2c
	float					mNewThreatTime;							// +0x30
};
#else
// 0x34 bytes, vtable 005d8810 (CEquipment's is 005d8824); CBattleEngine::Init builds one inline (004056cb..004056f3).
class CRadarWarningReceiver : public CEquipment
{
public:
					~CRadarWarningReceiver();						// 004d6600 (out of line, RadarWarningReceiver.cpp: without this declaration every TU emits its own inline copy, and the first one in the link wins)
	virtual void	HandleEvent(CEvent *event);						// 0 (004d6a10)
	void			Init(CInitRadarWarningReceiver &init);			// 004d65a0 (not virtual: slot 3 stays CEquipment's, CBattleEngine::Init calls it directly 00405710)
	float			GetNewThreatTime() { return mNewThreatTime; }	// inline: CHud::Render00485d50 loads it before the time (004861fd)

	UBYTE			mUnknown14[0x1c - 0x14];
	SPtrSet<CThing>	mUnknown1C;										// +0x1c
	BOOL			mNewThreat;										// +0x2c
	float			mNewThreatTime;									// +0x30 (CHud::Render00485d50 004861fd)
};

#endif

#endif
