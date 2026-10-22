// Monitor.h: CMonitor (vtable 005d92d4: HandleEvent, deleting destructor, Shutdown; deletion list at +4).
// The inline AddDeletionEvent's allocation must stay on line 24 (retail pushes 0x18).

#ifndef MONITOR_INCLUDE
#define MONITOR_INCLUDE

#include "SPtrSet.h"

#include "Listener.h"

class CGenericActiveReader;

class CMonitor : public IListener
{
public:
	CMonitor() : mDeletionList(NULL) {}
	virtual void	HandleEvent(CEvent *event) {}
	virtual			~CMonitor();
	virtual void	Shutdown();

	void AddDeletionEvent(CGenericActiveReader *reader)	// FUNCTION: 0x00401040
	{
		if (!mDeletionList)
			mDeletionList = new (MEMTYPE_DELETION_CALLBACK_LIST) SPtrSet<CGenericActiveReader>;
		mDeletionList->Add(reader);
	}
	void RemoveDeletionEvent(CGenericActiveReader *reader)
	{
		if (mDeletionList)
			mDeletionList->Remove(reader);
	}

protected:
	SPtrSet<CGenericActiveReader>	*mDeletionList;
};

#endif
