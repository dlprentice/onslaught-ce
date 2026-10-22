// MissionScript/scripteventnb.h: the named script events (only what matched files need so far).

#ifndef SCRIPTEVENTNB_H
#define SCRIPTEVENTNB_H

#include "vm.h"

// the named events and their listeners (RTTI: a CMonitor, vtable 005e4f44). An event function's parameters join
// their named events when its script starts (IScript::Init); IScript::PostEvent posts a name, which marks the event
// fired and runs its functions.
#ifdef ISCRIPT_FULL	// IScript.cpp's view: game.cpp's DrawDebugStuff (00470825) moves with its declaration count
class CScriptEventNB : public CMonitor
{
public:
	enum { SCRIPT_EVENT_NB_POST = 2000 };	// IScript::PostEvent posts a CPostEventData with it (005383c0)

	CScriptEventNB() : mEvents(NULL) {}		// 00538760 (the initializer calls it)
	~CScriptEventNB();						// 00538950
	virtual void	HandleEvent(CEvent *event);	// 00538c70: a posted event arrives
	virtual void	Shutdown();				// 005388d0

	void	Init();							// 00538860
	void	PostEvent(char *name);			// 00538b70
	// an event function's parameter joins the named event's listeners; returns the event (00538960)
	struct SScriptEvent	*RegisterEventListener(struct SEventParam *param, class CEventFunction *function);

	SPtrSet<struct SScriptEvent>	*mEvents;	// +0x08
};
#else
class CScriptEventNB
{
public:
	void	PostEvent(char *name);				// 00538b70
	void	Init();								// 00538860
	virtual void	Shutdown();					// 005388d0
};
#endif

extern CScriptEventNB SCRIPT_EVENT_NB;			// 0089c590

#endif
