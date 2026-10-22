// CEventManager.cpp: implementation of the CEventManager class.
//
//////////////////////////////////////////////////////////////////////

#include "common.h"	// FUNCTION: 0x0044ae80 init:ZERO_FVECTOR
#include "EventManager.h"	// FUNCTION: 0x0044aea0 init:ID_FMATRIX
#include "Thing.h"	// FUNCTION: 0x0044b190 ??0CScheduledEvent@@QAE@XZ
#include "DebugLog.h"	// FUNCTION: 0x0044b1d0 ??1CGenericActiveReader@@QAE@XZ
#include "ActiveReader.h"
#include "HLCollisionDetector.h"
#include "CollisionSeekingRound.h"
#include "Profile.h"
#include "thing.h"
#include "cliparams.h"

#include <stdio.h>

CEventManager EVENT_MANAGER ;	// FUNCTION: 0x0044af70 init:EVENT_MANAGER

//#define GENERATE_DEBUG_REPORT	// FUNCTION: 0x0044af90 exit:EVENT_MANAGER
static float last_debug_time = 0.0f ; 	// FUNCTION: 0x0044b290 ??1?$OPtrSet@VCScheduledEvent@@@@QAE@XZ

//******************************************************************************************

CEventManager::CEventManager()	// FUNCTION: 0x0044afa0
{
	mOverflowEventListBuffer = NULL;
	mValid = FALSE ;
}


//******************************************************************************************
CEventManager::~CEventManager()	// FUNCTION: 0x0044b000
{
	ASSERT( mOverflowEventListBuffer == NULL );
	Shutdown();
}


//******************************************************************************************
void CEventManager::Init()	// FUNCTION: 0x0044b060
{
	mTime=0.0;
	mCurrentBufferNum = 0 ;
	mFrameCount = 0;
	mNumEventsInEventManager = 0;
	mEventsProcessedThisUpdate = 0;
	mTotalEventProcessedNum = 0;
	mReadyToFlushBuffer = 0 ;
	mCurrentProcessOverflowEventNum = 0 ;

	mOverflowEventListBuffer = new( MEMTYPE_EVENTMANAGER ) OPtrSet<CScheduledEvent> ;

	LOG.AddMessage("Size of scheduled event = %d", sizeof(CScheduledEvent) ) ;
	mEventsPool = new ( MEMTYPE_EVENTMANAGER )CScheduledEvent[MAX_NUM_EVENTS] ;

	int i ;
	for (i=0;i<MAX_NUM_EVENTS-1;i++)
	{
		mEventsPool[i].SetNextFreeSE(&mEventsPool[i+1]);
	}

	mEventsPool[MAX_NUM_EVENTS-1].SetNextFreeSE(NULL);
	mEventFreeList = &mEventsPool[0] ;
	last_debug_time = 0.0f;
	mValid = TRUE;
	LogEventManager() ;
}


//******************************************************************************************
void CEventManager::Shutdown()	// FUNCTION: 0x0044b1f0
{
	if (mEventsPool)
	{
		char* cunt = ((char*)&(*mEventsPool))-0x44 ;

		if (*cunt != 'C' )
		{
			int v = 2 ;
		}
	}

	mValid = FALSE ;
	// clean event list buffers
	int i;
	int j;
	for (j=0 ; j < NUM_PRIORITY; j++)
	{
		for (i=0 ; i < NUM_EVENT_LIST_BUFFERS; i++)
		{
			mEventListBuffer[i][j].RemoveAll() ;
		}
	}

	// clean overflow buffer

	if (mOverflowEventListBuffer != NULL)
	{
		mOverflowEventListBuffer->reset();
		delete mOverflowEventListBuffer ;
		mOverflowEventListBuffer = NULL;
	}

	delete [] mEventsPool ;
    mEventsPool=  NULL ;
}



//******************************************************************************************
CScheduledEvent* CEventManager::GetNextFreeEvent()	// FUNCTION: 0x0044b2a0
{
	CScheduledEvent* r =mEventFreeList ;
	if (mEventFreeList) 
	{
		mEventFreeList = mEventFreeList->GetNextFreeSE() ;
	}
	else
	{
		LOG.AddMessage("FATAL ERROR:  Run out of free scheduled events!!");
	}
	
	return r;
}


//******************************************************************************************
void	CEventManager::FreeEvent(CScheduledEvent* event)
{
	event->SetNextFreeSE(mEventFreeList) ;
	event->SetData(NULL);
	event->SetToCall(NULL);
	mEventFreeList = event ;
}



//******************************************************************************************
// Add an event to the event manager
// note event manager will have a readers point to the 'data' part in AddEvent

void CEventManager::AddEvent(const float& time_from_now, int event_num, CMonitor* to_call, const int start_or_end, CMonitor* data, CScheduledEvent* re_use_event)	// FUNCTION: 0x0044b2d0 ?AddEvent@CEventManager@@QAEXABMHPAVCMonitor@@H1PAVCScheduledEvent@@@Z
{
	AddEvent(event_num, to_call, time_from_now + mTime, start_or_end, data, re_use_event) ;
}


//******************************************************************************************
// Add an schuled event to the event manager. NOTE the time part means from time from now.
// Also note the event manager the takes ownership of the event
void CEventManager::AddEvent(CScheduledEvent* event)	// FUNCTION: 0x0044b310
{
	if (event == NULL) return ;
	AddEvent(event->GetEventNum(),
			 event->GetToCall(),
			 event->GetTime() + mTime,
			 START_OF_FRAME,
			 event->GetData(),
			 NULL) ;
	FreeEvent(event);
}



//******************************************************************************************
// Add an event to the event manager
// note event manager will have a readers pointer of 'data' part in AddEvent

void CEventManager::AddEvent(const int event_num, CMonitor* to_call, const float& time, const int start_or_end, CMonitor* data, CScheduledEvent* re_use_event)	// FUNCTION: 0x0044b370 ?AddEvent@CEventManager@@QAEXHPAVCMonitor@@ABMH0PAVCScheduledEvent@@@Z
{
//	PROFILE_START(AddEvent) ;

	if (mValid == FALSE)
	{
		LOG.AddMessage("FATAL ERROR: Trying to add an event when event manager was invalid") ;
		return ;
	}

	if (to_call == NULL) return ;

	float next_time = time ;
	int offset_buffer=0;

	// ok quick case for events which are to be executed in the next frame
	if (time <= (mTime+0.051f)) 
	{
		offset_buffer = mCurrentBufferNum ;	

		if (time < 0.0)
		{
			next_time = mTime +0.0001f; 
		}
		else
		{
			next_time = time ;
		}
	}
	else
	{
		// ignore event if to big
		if (time > 1000000.0f)
		{
			//PROFILE_END(AddEvent) ;
			return ;
		}

		// calculate witch offset buffer to add the event into
		float delay = time - mTime - 0.001f ; //(ensures up to and including )
		delay*=GAME_FR ;
		delay=(float)floorf(delay) ;
		offset_buffer = (int)delay ;

		// if buffer is outside our range then add the event to the overflow buffer

		if (offset_buffer >= (NUM_EVENT_LIST_BUFFERS -2) ) 
		{
			int size = mOverflowEventListBuffer->size() ;
			int add_point = mCurrentProcessOverflowEventNum ;
		
			// make sure we add the event is time order
			while (add_point < size &&
				   mOverflowEventListBuffer->at(add_point)->GetTime() <= next_time)
			{
				add_point++;
			}

			CScheduledEvent* se = re_use_event ;

			if (se == NULL)
			{
				se = GetNextFreeEvent() ;
				if (se == NULL) return ;
				se->Set(event_num, next_time, to_call, data);
			}
			else
			{
				se->SetTime(next_time) ;
				se->SetNum(event_num);
				se->SetReuse(TRUE) ;
				if (data != NULL)
				{
					se->SetData(data) ;
				}
			}

			mOverflowEventListBuffer->insert_at(add_point,se) ;
			mNumEventsInEventManager++ ;
			//PROFILE_END(AddEvent) ;
			return ;
		}

		offset_buffer += mCurrentBufferNum ;
		offset_buffer %= NUM_EVENT_LIST_BUFFERS ;
	}

	CScheduledEvent* se = re_use_event ;
	if (se == NULL)
	{
		se = GetNextFreeEvent() ;
		if (se == NULL) return ;
		se->Set(event_num, next_time, to_call, data);
	}
	else
	{
		se->SetTime(next_time) ;
		se->SetNum(event_num);
		se->SetReuse(TRUE) ;
		if (data != NULL)
		{
			se->SetData(data) ;
		}
	}

	mEventListBuffer[offset_buffer][start_or_end].Append(se) ;

	mNumEventsInEventManager++ ;
	//PROFILE_END(AddEvent) ;
}


//******************************************************************************************
// this can be called instaed of 'AdvanceTime' and then 'Flush'
void CEventManager::Update()	// FUNCTION: 0x0044b5c0
{
	AdvanceTime() ;
	Flush() ;
}


//******************************************************************************************
// advance time to the next game code time
void CEventManager::AdvanceTime()	// FUNCTION: 0x0044b600
{
	mFrameCount++ ;
	mTime = mFrameCount * CLOCK_TICK ;

	// advance the current event buffer so new events will be added from there on.

	mReadyToFlushBuffer = mCurrentBufferNum ;

	mCurrentBufferNum++ ;
	mCurrentBufferNum%=NUM_EVENT_LIST_BUFFERS ;
}


//******************************************************************************************
//  This function is called after advance time.  It flushes out all the events in the current event
//  buffer. It should also checks that if any events need to be executed in the overflow buffer

void CEventManager::Flush()	// FUNCTION: 0x0044b640
{

#ifdef GENERATE_DEBUG_REPORT
	// do debug stuff

	if ((mTime - last_debug_time) > 60.0f)
	{
		last_debug_time = mTime ;
		LogEventManager() ;
	}
#endif

	ULONG previus_num_events_called = mTotalEventProcessedNum ;
	int next_bufffer = mReadyToFlushBuffer;
	mCurrentProcessOverflowEventNum = 0 ;

	// execute all the events int the current buffer
	CScheduledEvent * next_event;
	int j = 0 ;
	for (j=0 ; j< NUM_PRIORITY; j++)
	{
		int cunt = 0 ;
		FOR_ALL_ITEMS_IN(mEventListBuffer[next_bufffer][j], next_event)
		{
			cunt++;
			IListener* to_call = next_event->GetToCall() ;
			next_event->SetReuse(FALSE);
			if (to_call)
			{
				to_call->HandleEvent(next_event);
			}
			mTotalEventProcessedNum++;
		}
	}

	// Check the overflow buffer incase any of the events need to be executed
	int size = mOverflowEventListBuffer->size() ;

	while (size > mCurrentProcessOverflowEventNum &&
		   mOverflowEventListBuffer->at(mCurrentProcessOverflowEventNum)->GetTime() < mTime )
	{
		CScheduledEvent* next_event = mOverflowEventListBuffer->at(mCurrentProcessOverflowEventNum) ;
		IListener* to_call = next_event->GetToCall() ;
		next_event->SetReuse(FALSE);
		mCurrentProcessOverflowEventNum++;
		if (to_call)
		{
		   to_call->HandleEvent(next_event);
		}
		mTotalEventProcessedNum++;
    }

	//  clean up

	// delete events from the current buffer 
	for (j=0 ; j<NUM_PRIORITY;j++)
	{
		FOR_ALL_ITEMS_IN(mEventListBuffer[next_bufffer][j], next_event)
		{
			if (next_event->GetReuse() == FALSE)
			{
				FreeEvent(next_event) ;
			}
			mNumEventsInEventManager--;
		}
		mEventListBuffer[next_bufffer][j].RemoveAll() ;
	}


    // delete events from the overflow buffer
	if (mCurrentProcessOverflowEventNum > 0)
	{
		for (int i=mCurrentProcessOverflowEventNum-1;i>=0;i--)
		{
			CScheduledEvent* ev = mOverflowEventListBuffer->at(i) ;
			if (ev->GetReuse() == FALSE)
			{
				FreeEvent(mOverflowEventListBuffer->at(i));	
			}
			mNumEventsInEventManager--;
		}	
		mOverflowEventListBuffer->remove(0, mCurrentProcessOverflowEventNum-1);
	}
	mCurrentProcessOverflowEventNum = 0 ;

	mEventsProcessedThisUpdate = mTotalEventProcessedNum - previus_num_events_called;

	// ok sanity check to make sure overflow buffer has not be corrupted
	if (CLIPARAMS.mDeveloperMode == TRUE)
	{
		int size1 = mOverflowEventListBuffer->size() ;
		for (int i=0 ;i < size1-1 ; i++)
		{
			if (mOverflowEventListBuffer->at(i)->GetTime() > mOverflowEventListBuffer->at(i+1)->GetTime() )
			{
				LOG.AddMessage("FATAL ERROR: event manager overflow buffer corrupt!") ;
			}
		}
	}
}


//******************************************************************************************

// DEBUG STUFF

#include "UnitAi.h"
#include "Unit.h"


//******************************************************************************************
void CEventManager::LogEvent(CScheduledEvent* event)
{
// need to switch RTTI on for this code to work
#ifdef GENERATE_DEBUG_REPORT
	char temp[100] ;
	IListener* to_call = event->GetToCall() ;
	if (to_call == NULL)
	{
		sprintf(temp,"Event time: %2.2f,  Object to call has died", event->GetTime() ) ;
		LOG.AddMessage(temp);
		return ;
	}

	while(to_call)
	{
		CThing* thing = dynamic_cast<CThing*>(to_call) ;
		
		if (thing)
		{
			sprintf(temp,"Event time: %2.2f,  to call %s,  event no:%d", event->GetTime(), thing->_GetClassName(), event->GetEventNum() ) ;
			LOG.AddMessage(temp);
			break ;
		}
		CHLCollisionDetector* ct = dynamic_cast<CHLCollisionDetector*>(to_call) ;
		if (ct)
		{

			BOOL do_special = FALSE ;
			if (ct->GetCST()->GetThing()->IsA(THING_TYPE_UNIT))
			{
				CUnit* unit = (CUnit*)ct->GetCST()->GetThing() ;

				if (unit->GetBehaviour())
				{
					CCSPersistentThing* cspt = (CCSPersistentThing*)((CScheduledEvent*)event)->GetData() ;
					if ( cspt != NULL)
					{
						sprintf(temp,"Event time: %2.2f,  HL collision for %s  %08x %s with %s", event->GetTime(), unit->_GetClassName(), unit, unit->GetBehaviour()->mName, cspt->GetThing()->_GetClassName() ) ;
						do_special = TRUE ;
					}
					else
					{
						sprintf(temp,"Event time: %2.2f,  HL collision for %s  %08x %s with NULL", event->GetTime(), unit->_GetClassName(), unit, unit->GetBehaviour()->mName ) ;
						do_special = TRUE ;
					}
				}
			}
			
			if (do_special == FALSE)
			{
				sprintf(temp,"Event time: %2.2f,  HL collision for %s  %08x ", event->GetTime(), ct->GetCST()->GetThing()->_GetClassName(), ct->GetCST()->GetThing() ) ;
			}
			LOG.AddMessage(temp);
			break ;
		}

		CUnitAI* ai = dynamic_cast<CUnitAI*>(to_call) ;
		if (ai)
		{
			sprintf(temp,"Event time: %2.2f,  Unit AI for %s  event no:%d ", event->GetTime(), ai->GetForUnit()->_GetClassName(), event->GetEventNum() ) ;
			LOG.AddMessage(temp);
			break ;
		}

		sprintf(temp,"Event time: %2.2f,   dunno ??  no = %d", event->GetTime(),  event->GetEventNum()) ;
		LOG.AddMessage(temp) ;

		break ;
	}
#endif
}


//******************************************************************************************
void	CEventManager::LogEventManager()
{
#ifdef GENERATE_DEBUG_REPORT
	// debug utility

	char temp[100] ;

	LOG.AddMessage("_________________________________________________________") ;
	LOG.AddMessage("___________________ EVENT LIST STATUS ___________________\n") ;
	sprintf(temp,"Time : %2.2f Seconds \n", mTime) ;
	LOG.AddMessage(temp) ;

	int i;
	int j;

	for (i=0 ; i < NUM_EVENT_LIST_BUFFERS; i++)
	{
		sprintf(temp,"______________________TIME + %2.2f______________________________", i*CLOCK_TICK) ;
		for (j=0 ;j < NUM_PRIORITY;j++)
		{
			int buff = mCurrentBufferNum + i ;
			buff %= NUM_EVENT_LIST_BUFFERS ;
			LOG.AddMessage(temp) ;

			CScheduledEvent* next_event;
			FOR_ALL_ITEMS_IN(mEventListBuffer[buff][j], next_event)
			{
		
				LogEvent(next_event);
			}
		}
	}

	LOG.AddMessage("_____________________ NORMAL BUFFER ______________________________") ;

	int size = mOverflowEventListBuffer->size() ;
	for (i=0 ; i<size ;i++)
	{
		CScheduledEvent* next_event = mOverflowEventListBuffer->at(i) ;
		LogEvent(next_event);
    }


	LOG.AddMessage("\n_________________________________________________________\n") ;

	sprintf(temp,"HL Collision checks per sec = %2.2f", CHLCollisionDetector::GetChecksDone() / mTime) ;
	LOG.AddMessage(temp) ;
	sprintf(temp,"LL Round Collision checks per sec = %2.2f", CCollisionSeekingRound::GetChecksDone() / mTime) ;
	LOG.AddMessage(temp) ;
	LOG.AddMessage("\n_________________________________________________________\n") ;
#endif	
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x0044afe0 ??_GCEventManager@@UAEPAXI@Z
