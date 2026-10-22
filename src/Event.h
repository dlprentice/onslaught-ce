// Event.h: interface for the Event class.
//
//////////////////////////////////////////////////////////////////////

#ifndef EVENT_INCLUDE
#define EVENT_INCLUDE

class CThing;
class CMonitor;

#include "ActiveReader.h"

class CEvent  
{
public:
		 CEvent() {};
		 CEvent(const int event, CMonitor* to_call=NULL) ;
		 void SetNum(const int event) { mEventNum = (short)event ; }
	const int GetEventNum() { return (int)mEventNum ; }
	void	  SetToCall(CMonitor* to_call) { mToCall.SetReader(to_call) ; }
	CMonitor*	 GetToCall() { return mToCall.ToRead() ;}

protected:
	CActiveReader<CMonitor>	 mToCall;		// who the event is going to happen to.
	short mEventNum;
};

#endif 