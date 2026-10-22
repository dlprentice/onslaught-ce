// Scheduled_event.cpp: implementation of the Scheduled_event class.
//
//////////////////////////////////////////////////////////////////////

#include "common.h"
#include "scheduledevent.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

int CScheduledEvent::mNumCreated = 0 ;



//******************************************************************************************
void CScheduledEvent::Set(int event_num, const float& time, CMonitor* to_call, CMonitor* data)	// FUNCTION: 0x004de1f0
{
	CEvent::SetNum(event_num);
	mTime = time;
	mToCall.SetReader(to_call),
	mBeingReused = 0 ;
	mData.SetReader(data) ;
}

 


 //******************************************************************************************
CScheduledEvent::~CScheduledEvent()	// FUNCTION: 0x004de230
{
	mNumCreated--;
}
// STAND-IN (link layout): retail's 16 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_scheduledevent[16] = { 0 };


