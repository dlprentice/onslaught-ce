// Scheduled_event.h: interface for the Scheduled_event class.
//
//////////////////////////////////////////////////////////////////////

#ifndef SCHEDULED_EVENT_INCLUDE
#define SCHEDULED_EVENT_INCLUDE


#include "Event.h" 
#include "activereader.h"
class IListener;

class CScheduledEvent :public CEvent 
{
public:
	CScheduledEvent() : mData(NULL) {mNumCreated++;}
	void Set(int event_num, const float& time, CMonitor* to_call, CMonitor* data);
	~CScheduledEvent();

	const float& GetTime() { return mTime; }
	void		 SetTime(float new_time) { mTime = new_time ; }
	void		 SetReuse(BOOL val) { mBeingReused = (short)val ; }

	BOOL		 GetReuse() { return (BOOL)mBeingReused ; }
	CMonitor*	 GetData() { return   mData.ToRead()  ;}
	void		 SetData(CMonitor* data) { mData.SetReader(data) ; }

	CScheduledEvent* GetNextFreeSE() { return mNextFreeSE ; }
	void		 SetNextFreeSE(CScheduledEvent* se) { mNextFreeSE = se; }
	
	static int	 GetNumCreated() { return mNumCreated ; }
private:
	short					 mBeingReused;	// (used by event manager for effeciency use).

	CActiveReader<CMonitor>  mData;			// any data associated with this event.

	// sorry about this union but does help reduce memory as a lot of scheduled events get created
	union
	{
		float					 mTime;			// time the event is to be executed.
		CScheduledEvent*		 mNextFreeSE;	// used when stored in event manager free list
	};

	static int				 mNumCreated;	// global event count.

};

#endif 


