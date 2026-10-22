// asynccache.h: a file read on a worker thread into a memory buffer (reconstructed; Stuart's file is not in his
// release). Only CFEPGoodies uses one (+0x0c, 0x128 bytes). The thread base keeps its mutex at +0x08 and its busy
// flag at +0x15 (Busy 00528d90, GrabMutex 00528da0, ReleaseMutex 00528dc0); LoadFile (00539dc0) waits for the
// thread, copies the name to +0x20 and the size to +0x124, then starts it.

#ifndef ASYNCCACHE_H
#define ASYNCCACHE_H

#ifdef ASYNCCACHE_FULL	// the full class (a CWaitingThread, vtable 005e4f5c); the other objects keep the short view
#include "WaitingThread.h"

class CXBOXAsyncCache : public CWaitingThread
{
public:
	CXBOXAsyncCache();										// 00539c80
	virtual void	Run();								// 0 (00539ca0): the read
	void		LoadFile(char *name, SINT size);		// 00539dc0

#else
class CXBOXAsyncCache
{
public:
	BOOL		Busy();									// 00528d90
	void		GrabMutex();							// 00528da0
	void		ReleaseMutex();							// 00528dc0
	void		LoadFile(char *name, SINT size);		// 00539dc0

	UBYTE		mUnknown00[0x1c];
#endif
	CMEMBUFFER	*mMemBuffer;							// +0x1c (CFEPGoodies::LoadingGoodyPoll 0045cc37)
	char		mFileName[MAX_PATH];					// +0x20
	SINT		mSize;									// +0x124
};
#define CASYNCCACHE CXBOXAsyncCache

#endif
