// Monitor.cpp: reconstructed from Monitor.obj (004bac40..004bad10); Stuart's release has no copy. A monitor tells
// every reader still watching it that it has gone.

#include "common.h"
#include "Monitor.h"
#include "activereader.h"

//******************************************************************************************
CMonitor::~CMonitor()	// FUNCTION: 0x004bac40
{
	if (mDeletionList)
	{
		CGenericActiveReader	*reader;
		for (reader = mDeletionList->First(); reader; reader = mDeletionList->Next())
			reader->ToReadDied();
		SAFE_DELETE(mDeletionList);
	}
}

//******************************************************************************************
void CMonitor::Shutdown()	// FUNCTION: 0x004bacb0
{
	if (mDeletionList)
	{
		CGenericActiveReader	*reader;
		for (reader = mDeletionList->First(); reader; reader = mDeletionList->Next())
			reader->ToReadDied();
		SAFE_DELETE(mDeletionList);
	}
}
