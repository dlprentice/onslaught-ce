// ScriptEventNB.cpp: the named events scripts post and listen for (reconstructed from the retail object,
// 00538740..00538dac; not in Stuart's release). The allocations' line numbers are retail's (__FILE__ 0064fe98).

#include "..\common.h"
#include "..\monitor.h"
#include "IScript.h"
#include "EventFunction.h"
#include "scripteventnb.h"
#include "..\scheduledevent.h"
#include "..\DebugLog.h"

// FUNCTION: 0x00538740 init:SCRIPT_EVENT_NB
// FUNCTION: 0x00538760 ??0CScriptEventNB@@QAE@XZ
// FUNCTION: 0x00538780 ??_GCScriptEventNB@@UAEPAXI@Z
// FUNCTION: 0x005387a0 exit:SCRIPT_EVENT_NB
CScriptEventNB	SCRIPT_EVENT_NB;

//******************************************************************************************
SScriptEvent::SScriptEvent(CDataType *name, CEventFunction *function)
{
	mName = name->Clone();
#line 17
	mFunctions.Append(new (MEMTYPE_SCRIPT) CActiveReader<CEventFunction>(function));
	mFired = FALSE;
}

//******************************************************************************************
void	SScriptEvent::AddFunction(CEventFunction *function)
{
#line 25
	mFunctions.Append(new (MEMTYPE_SCRIPT) CActiveReader<CEventFunction>(function));
}

//******************************************************************************************
SScriptEvent::~SScriptEvent()	// FUNCTION: 0x005387b0
{
	delete mName;
	mName = NULL;
	mFunctions.DeleteAll();
}

//******************************************************************************************
void	CScriptEventNB::Init()	// FUNCTION: 0x00538860
{
#line 66
	mEvents = new (MEMTYPE_SCRIPT) SPtrSet<SScriptEvent>;
}

//******************************************************************************************
void	CScriptEventNB::Shutdown()	// FUNCTION: 0x005388d0
{
	CMonitor::Shutdown();

	if (mEvents)
		mEvents->DeleteAll();
	delete mEvents;
	mEvents = NULL;
}

//******************************************************************************************
CScriptEventNB::~CScriptEventNB()	// FUNCTION: 0x00538950
{
}

//******************************************************************************************
SScriptEvent	*CScriptEventNB::RegisterEventListener(SEventParam *param, CEventFunction *function)	// FUNCTION: 0x00538960
{
	SScriptEvent	*found = NULL;
	SScriptEvent	*event;
	FOR_ALL_ITEMS_IN(*mEvents, event)
	{
		if (strcmp(param->mName->GetString(), event->mName->GetString()) == 0)
			found = event;
	}

	if (!found)
	{
#line 104
		found = new (MEMTYPE_SCRIPT) SScriptEvent(param->mName, function);
		mEvents->Append(found);
	}
	else
		found->AddFunction(function);

	return found;
}

//******************************************************************************************
// the event's functions run now; the name has to match exactly
void	CScriptEventNB::PostEvent(char *name)	// FUNCTION: 0x00538b70
{
	if (!mEvents->Size() && strcmp("game playing", name) != 0)
		LOG.AddMessage("Warning: No listeners for posted event '%s'", name);

	ListIterator<SScriptEvent>	events(mEvents);
	for (SScriptEvent *event = events.First(); event; event = events.Next())
	{
		if (strcmp(event->mName->GetString(), name) == 0)
		{
			event->mFired = TRUE;

			ListIterator<CActiveReader<CEventFunction> >	functions(&event->mFunctions);
			for (CActiveReader<CEventFunction> *reader = functions.First(); reader; reader = functions.Next())
			{
				if (reader->ToRead())
					reader->ToRead()->Execute();
			}
		}
	}
}

//******************************************************************************************
// IScript::PostEvent's message: its data carries the name
void	CScriptEventNB::HandleEvent(CEvent *event)	// FUNCTION: 0x00538c70
{
	if (event->GetEventNum() == SCRIPT_EVENT_NB_POST)
	{
		CPostEventData	*data = (CPostEventData *)((CScheduledEvent *)event)->GetData();
		char			*name = data->mName->GetString();

		if (!mEvents->Size() && strcmp("game playing", name) != 0)
			LOG.AddMessage("Warning: No listeners for posted event '%s'", name);

		ListIterator<SScriptEvent>	events(mEvents);
		for (SScriptEvent *e = events.First(); e; e = events.Next())
		{
			if (strcmp(e->mName->GetString(), name) == 0)
			{
				e->mFired = TRUE;

				ListIterator<CActiveReader<CEventFunction> >	functions(&e->mFunctions);
				for (CActiveReader<CEventFunction> *reader = functions.First(); reader; reader = functions.Next())
				{
					if (reader->ToRead())
						reader->ToRead()->Execute();
				}
			}
		}

		delete data;
	}
}
