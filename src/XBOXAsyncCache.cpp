// XBOXAsyncCache.cpp: CXBOXAsyncCache, a file read on a CWaitingThread into a memory buffer (reconstructed; the file name
// is the object's __FILE__, 006502a0). CFEPGoodies loads its goodies through one.

#include	"Common.h"

#define ASYNCCACHE_FULL
#include	"asynccache.h"
#include	"membuffer.h"
#include	"DXMemBuffer.h"
#include	"DebugLog.h"

// the file exists but could not be read (MessageBox.cpp reads it). 0089c808 follows vm.obj's MISSION_SCRIPT_VM
// (0089c5e0, 0x228 bytes), so it is this object's.
bool	gFileReadFailed;						// 0089c808

//*********************************************************************************
CXBOXAsyncCache::CXBOXAsyncCache()	// FUNCTION: 0x00539c80
{
	mFileName[0] = 0;
}

//*********************************************************************************
// the job, on the thread: reads mFileName into a new buffer, mSize at a time
void	CXBOXAsyncCache::Run()	// FUNCTION: 0x00539ca0
{
	if (mMemBuffer)
	{
		mMemBuffer->Close();
		mMemBuffer = NULL;
	}

#line 29
	mMemBuffer = new (MEMTYPE_MEMBUFFER) CMEMBUFFER;
	CMEMBUFFER::SetNextReadBufferSize(mSize);
	BOOL	ok = mMemBuffer->InitFromFile(mFileName);
	CMEMBUFFER::SetNextReadBufferSize(0);

	if (!ok)
	{
		FILE	*f = fopen(mFileName, "rb");
		if (f)
		{
			gFileReadFailed = true;
			fclose(f);
		}
		TRACE("Warning : unable to load sample ");
		TRACE(mFileName);
		TRACE(" - file not found!\n");
		delete mMemBuffer;
		mMemBuffer = NULL;
	}

	mFileName[0] = 0;
}

//*********************************************************************************
// waits for the thread, then starts it on the file
void	CXBOXAsyncCache::LoadFile(char *name, SINT size)	// FUNCTION: 0x00539dc0
{
	Acquire();
	strcpy(mFileName, name);
	mSize = size;
	StartAsync();
}
