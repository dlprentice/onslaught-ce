// DXMemoryManager.cpp: implementation of the CDXMemoryManager class.
//
// Seeded from Stuart's GPL source (DXMemoryManager.cpp at 5352a81c); divergences are listed in README.md.

#include "common.h"
#define DXMEMORYMANAGER_CPP
#include "MemoryManager.h"
#include "DXMemoryManager.h"
#include "maths.h"
#include "debuglog.h"
#include "console.h"

// FUNCTION: 0x00548d40 init:MEM_MANAGER
// FUNCTION: 0x00548d60 exit:MEM_MANAGER
CDXMemoryManager MEM_MANAGER;

char CDXMemoryManager::mTypeName[ MEMTYPE_LIMIT ][ 32 ];
UINT CDXMemoryManager::mTypeSizeLimit[ MEMTYPE_LIMIT ];
BOOL CDXMemoryManager::mInit=FALSE;

extern MemoryTypeData gMemTypeData[];

//*****************************************************************************
//
//	CMemoryManager
//
//*****************************************************************************

// FUNCTION: 0x00548d70
CDXMemoryManager::CDXMemoryManager()
{
	int i;

	// individual heaps per type:
	for( i=0; i<MEMTYPE_LIMIT; ++i )
	{
		mTypeHeap[ i ] = &mDefaultHeap;
	}

	// heap names and sizes:
	for( i=0; i<MEMTYPE_LIMIT; ++i )
	{
		// find name:
		strcpy( mTypeName[ i ], "Name not found" );
		mTypeSizeLimit[ i ] = 0xffffffff;
		for( UINT j=0; gMemTypeData[ j ].mType != MEMTYPE_LIMIT; ++j )
		{
			if( gMemTypeData[ j ].mType == i )
			{
				strcpy( mTypeName[ i ], gMemTypeData[ j ].mName );
				mTypeSizeLimit[ i ] = gMemTypeData[ j ].mMaxSize;
				break;
			}
		}
	}
}

//*****************************************************************************
// FUNCTION: 0x00548ec0
CDXMemoryManager::~CDXMemoryManager()
{
}

//*****************************************************************************
// FUNCTION: 0x00548f90
BOOL CDXMemoryManager::Init( UINT aSize )
{
	mInit=TRUE;
	mDumpingMemory=FALSE;

	// Default heap is in regular malloc'd memory
	mInit &= mDefaultHeap.Init(aSize,300*1024,"Default heap", true);

	if( !mDumpHeap.Init(2300*1024,0,"Dump memory heap"))
	{
		return FALSE;
	}

	mTypeHeap[ MEMTYPE_DUMPTEMP ] = &mDumpHeap;

	if (!mSoundHeap.Init(30 * 1024 * 1025 + 512 * 1024, 0, "Sound memory heap", false))
	{
		return FALSE;
	}

	mTypeHeap[MEMTYPE_SOUND_SAMPLE] = &MEM_MANAGER.mSoundHeap;

	if( !mThingHeap.Init(THING_HEAP_SIZE,200*1024,"Thing memory heap",false))
	{
		return FALSE;
	}

	mTypeHeap[MT_TREE_THING] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MT_UNIT_THING] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MT_THING] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MT_ROUND] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_WEAPON] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_WEAPONMODE] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_AI] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_ACTIVE_READER] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MT_SQUAD] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MT_UNIT_DATA] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MT_DUNNO_THING] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MT_INIT_THING] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MT_ROUND_DATA] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_NAVIGATION] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_EXPLOSION] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_GUIDE] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_COLLISION] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_DELETION_CALLBACK_LIST] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_GENERAL_VOLUME] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_RENDERTHING] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MT_CST] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_MOTIONCONTROLLER] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MEMTYPE_MAPWHO_ENTRY] = &MEM_MANAGER.mThingHeap;
	mTypeHeap[MT_BUBBLE] = &MEM_MANAGER.mThingHeap;

	return mInit;
}

//*****************************************************************************
// FUNCTION: 0x005490c0
void CDXMemoryManager::Shutdown()
{
	mInit=FALSE;
	mDefaultHeap.Shutdown();
}

//*****************************************************************************
// FUNCTION: 0x005490e0
void * CDXMemoryManager::Alloc( UINT aSize, EMemoryType aType, char * apFilename, UINT aLine )
{
	void	*data;
	ASSERT(mInit);
	data = mTypeHeap[ aType ]->Alloc( aSize, aType, apFilename, aLine );

	if (!data)
	{
		// out of memory! Let's print a deceiving error message
		if (mTypeHeap[aType] == &mDefaultHeap) CONSOLE.FatalError(FALSE, 0xcd, -1);
		else
		if (mTypeHeap[aType] == &mDumpHeap   ) CONSOLE.FatalError(FALSE, 0xce, -1);
		else
		if (mTypeHeap[aType] == &mThingHeap  ) CONSOLE.FatalError(FALSE, 0xcf, -1);
		else
		if (mTypeHeap[aType] == &mSoundHeap  ) CONSOLE.FatalError(FALSE, 0xd0, -1);
	}

	return data;
}

//*****************************************************************************
// FUNCTION: 0x005491b0
void  * CDXMemoryManager::ReAlloc( void * apMem, UINT aSize )
{
	void	*data;

	// maybe it's a tiny block?
	if (mDefaultHeap.ReallocTiny(apMem, aSize, &data)) return data;
	if (mThingHeap  .ReallocTiny(apMem, aSize, &data)) return data;

	// otherwise just go through the normal realloc process
	ASSERT(mInit);
	CMemoryBlock * lpBlock = CMemoryBlock::GetBlock( apMem );
	ASSERT( lpBlock->IsValid() && lpBlock->IsUsed() );

	data = mTypeHeap[ lpBlock->GetType() ]->ReAlloc( lpBlock, aSize );

	return data;
}

//*****************************************************************************
// FUNCTION: 0x00549220
void CDXMemoryManager::Free( void * apMem )
{
	ASSERT(mInit);
	// frees memory pointer passed
	if( !apMem )
	{
		return;
	}

	// Maybe it's a tiny block?
	if (mDefaultHeap.FreeTiny(apMem)) return;
	if (mThingHeap.FreeTiny(apMem)) return;

	// check block:
	CMemoryBlock * lpBlock = CMemoryBlock::GetBlock( apMem );

	mTypeHeap[ lpBlock->GetType() ]->Free( lpBlock );
}

//*****************************************************************************
// FUNCTION: 0x00549270
void CDXMemoryManager::Cleanup()
{
	ASSERT(mInit);
	mDefaultHeap.Cleanup();
	mSoundHeap.Cleanup();
}

//*****************************************************************************

// let's print stats from different heaps.
char heapnr;

// FUNCTION: 0x00549290
void	CDXMemoryManager::PrintStats()
{
	switch(heapnr)
	{
	case 1:
		mThingHeap.PrintStats();
		break;

	case 0:
	default:
		mDefaultHeap.PrintStats() ;
		break;
	}
}

//*****************************************************************************
// FUNCTION: 0x005492b0
void CDXMemoryManager::OutputStats( char * aFilename )
{
	mDefaultHeap.OutputStats( aFilename );
}

//*****************************************************************************
// FUNCTION: 0x005492d0
void CDXMemoryManager::CalcAndShowDeltas()
{
	TRACE("** Memory Deltas **\n");
	mDefaultHeap.CalcAndShowDeltas();
	mDumpHeap.CalcAndShowDeltas();
	mThingHeap.CalcAndShowDeltas();

	TRACE("**  End Deltas   **\n");
}

//*****************************************************************************
// FUNCTION: 0x00549310
void CDXMemoryManager::LogDebugStats()
{
	TRACE("-=-=-=-=-=-=-=-=-=-=-=-=-\n");
	TRACE("Logging Stats\n");
	TRACE("-=-=-=-=-=-=-=-=-=-=-=-=-\n");

	mDefaultHeap.LogStats();
	mDumpHeap.LogStats();
	mThingHeap.LogStats();

	TRACE("--Heap Info--\n");

	char	str[500];
	sprintf(str, "Default heap - Peak %d, Size %d\n",MEM_MANAGER.GetDefaultPeakSize(), MEM_MANAGER.GetDefaultHeapSize());
	TRACE(str);
	sprintf(str, "Thing heap - Peak %d, Size %d\n", mThingHeap.GetPeakSize(), mThingHeap.GetSize());
	TRACE(str);

	TRACE("-=-=-=-=-=-=-=-=-=-=-=-=-\n");
	TRACE("Done Logging Stats\n");
	TRACE("-=-=-=-=-=-=-=-=-=-=-=-=-\n");
}

#include "mathsconst.h"	// the maths constant's include, where retail's initializer falls (placement evidence)	// FUNCTION: 0x00549460 init:ZERO_FVECTOR
// STAND-IN (link layout): retail's 16 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_DXMemoryManager[16] = { 0 };
