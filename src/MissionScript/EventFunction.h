// EventFunction.h: a script's event handler (reconstructed from the retail object; not in Stuart's release).
//
// CEventFunction (vtable 005e4ef8, a CMonitor with no virtuals of its own) runs the script from mEntryPC when one of
// the named events it listens for is posted: CScriptEventNB::PostEvent (00538b70) marks the event fired and calls
// Execute, which pushes one bool per listened-for event and calls the VM. The object code builds one per handler
// from its file (00538ec0) and clones them with itself.

#ifndef EVENTFUNCTION_H
#define EVENTFUNCTION_H

#include "vminternal.h"
#include "..\Monitor.h"

class CEventFunction;

// a named event as CScriptEventNB keeps it (0x18 bytes): the name, readers of the functions listening and whether
// it has fired (ScriptEventNB.cpp defines the members)
struct SScriptEvent
{
	SScriptEvent(CDataType *name, CEventFunction *function);	// RegisterEventListener inlines it
	~SScriptEvent();												// 005387b0
	void	AddFunction(CEventFunction *function);					// RegisterEventListener inlines it

	CDataType				*mName;			// +0x00: a clone of the first parameter's name
	SPtrSet<CActiveReader<CEventFunction> >	mFunctions;	// +0x04
	bool					mFired;			// +0x14: Execute passes it to the script
};

// one event name a handler listens for; the event is bound when CScriptEventNB registers the handler
struct SEventParam
{
	SEventParam(CDataType *name) : mName(name), mEvent(NULL) {}
	~SEventParam() { mName = NULL; mEvent = NULL; }

	CDataType				*mName;			// +0x00: a string symbol's value
	SScriptEvent			*mEvent;		// +0x04
};

class CEventFunction : public CMonitor
{
public:
	CEventFunction(CMissionScriptObjectCode *code, CDXMemBuffer *file);	// 0052fa70
	CEventFunction(int pc, CMissionScriptObjectCode *code) : mEntryPC(pc) { mCode = code; }
	~CEventFunction();														// 0052f9a0

	CEventFunction			*Clone(CMissionScriptObjectCode *code);		// 0052fbb0
	void					Execute();										// 0052fda0

	int						mEntryPC;		// +0x08
	SPtrSet<SEventParam>	mParams;		// +0x0c
	CMissionScriptObjectCode	*mCode;		// +0x1c

private:
	inline void				AddParam(CDataType *name);
};

#endif
