
#include "common.h"
#include "activereader.h"
#include "debuglog.h"
#include "Event.h"

//*************************************************************************************************
void CGenericActiveReader::SetReader(CMonitor* to_read)	// FUNCTION: 0x00401000
{
	if (to_read == mToRead) return ;

	if (mToRead)
	{
		mToRead->RemoveDeletionEvent(this) ;
	}
	mToRead = to_read ; 
	if (mToRead)
	{

		mToRead->AddDeletionEvent(this);
	}
}

// No maths-constant initializers belong to this object: the pair after it (004010c0) is actor.obj's, which puts
// its own first.
