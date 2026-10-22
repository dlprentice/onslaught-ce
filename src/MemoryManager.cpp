// MemoryManager.cpp: implementation of the CMemoryHeap and CMemoryManager classes.
//
// Seeded from Stuart's GPL source (MemoryManager.cpp at 5352a81c); divergences are listed in README.md.

#include "common.h"
#include "MemoryManager.h"
#include "DebugLog.h"
#include "membuffer.h"
#include "Platform.h"

// No maths-constant initializers belong to this object (none precede 004a1390); the pair after it (004a3010) is
// MenuItem.obj's.

// The retail PC build guards its heaps with a Win32 mutex in place of the source's BTS spinlock.
static void GRAB_MUTEX(HANDLE mutex)
{
	WaitForSingleObject(mutex, INFINITE);
}

static void RELEASE_MUTEX(HANDLE mutex)
{
	ReleaseMutex(mutex);
}

// We've got one of those classic constructor/destructor classette things
class CMutexGrabber
{
	HANDLE m_Mutex;

public:
	CMutexGrabber(HANDLE mutex)
	{
		// save it for the destructor.
		m_Mutex = mutex;

		// do the work.
		GRAB_MUTEX(mutex);
	}

	// FUNCTION: 0x004a1c30
	~CMutexGrabber()
	{
		RELEASE_MUTEX(m_Mutex);
	}
};
#define PROTECT_WITH_MUTEX() CMutexGrabber grabber(m_Mutex)

// Each memory type's name and size limit (0062e140; 130 entries, Stuart's table byte for byte).
MemoryTypeData gMemTypeData[] =
{

//		Type,						Name,					Size Limit,

	{	MEMTYPE_GENERIC,			"Generic",				0xffffffff	},
	{	MEMTYPE_MESH,				"Mesh",					0xffffffff	},
	{	MEMTYPE_TEXTURE,			"Texture",				0xffffffff	},
	{	MEMTYPE_TEXTURE_DATA,		"Texture surface",		0xffffffff	},
	{	MT_PHYSICS		,			"Physics",				0xffffffff	},
	{	MT_THING,					"Thing",				0xffffffff	},
	{	MT_UNIT_THING,				"Unit Thing",			0xffffffff	},
	{	MT_TREE_THING,				"Tree",					0xffffffff  },
	{   MT_SQUAD,					"Squad",				0xffffffff  },
	{   MT_INIT_THING,				"Init Thing",			0xffffffff  },
	{	MT_BUBBLE,					"Bubble",				0xffffffff  },
	{	MT_CST,						"CST",					0xffffffff  },
	{   MT_DUNNO_THING,				"Dunno thing?",			0xffffffff  },
	{	MT_ROUND,					"Round",				0xffffffff  },
	{	MT_ROUND_DATA,				"Round Data",			0xffffffff  },
	{	MT_UNIT_DATA,				"Unit Data",			0xffffffff	},
	{	MEMTYPE_PARTICLE,			"Particle",				0xffffffff	},
	{	MEMTYPE_MEMBUFFER,			"MemBuffer",			0xffffffff	},
	{	MEMTYPE_FLEXARRAY,			"FlexArray",			0xffffffff	},
	{	MEMTYPE_CAPTURE,			"Capture",				0xffffffff	},
	{	MEMTYPE_MUSIC,				"Music",				0xffffffff	},
	{	MEMTYPE_BATTLEENGINE,		"Battle Engine",		0xffffffff	},
	{	MEMTYPE_AI,					"AI",					0xffffffff	},
	{	MEMTYPE_GUIDE,				"Guide",				0xffffffff	},
	{	MT_SCRIPT,					"Script",				0xffffffff	},
	{	MT_VM_SCRIPT,				"VM Script",			0xffffffff	},
	{   MT_INST_SCRIPT,             "Script Inst",			0xffffffff  },
	{	MEMTYPE_MOTIONCONTROLLER,	"Motion Controller",	0xffffffff	},
	{	MEMTYPE_CHUNKER,			"Chunker",				0xffffffff	},
	{	MEMTYPE_CUTSCENE,			"Cutscene",				0xffffffff	},
	{	MEMTYPE_EQUIPMENT,			"Equipment",			0xffffffff	},	
	{	MEMTYPE_VBUFTEXTURE,		"VBufTexture",			0xffffffff	},
	{	MEMTYPE_INFLUENCEMAP,		"InfleunceMap",			0xffffffff	},
	{	MEMTYPE_MAP,				"Map",					0xffffffff	},
	{	MEMTYPE_HEIGHTFIELD,		"Heightfield",			0xffffffff	},
	{	MEMTYPE_JCLTEXTURE,			"JCLTexture",			0xffffffff	},
	{	MEMTYPE_MESHTEXTURE,		"MeshTexture",			0xffffffff	},
	{	MEMTYPE_NAVIGATION,			"Navigation",			0xffffffff	},
	{	MEMTYPE_CAMERA,				"Camera",				0xffffffff	},
	{	MEMTYPE_CONTROLLER,			"Controller",			0xffffffff	},
	{	MEMTYPE_PLAYER,				"Player",				0xffffffff	},
	{	MEMTYPE_MESSAGEBOX,			"MessageBox",			0xffffffff	},
	{	MEMTYPE_MESSAGEBOX,			"MessageLog",			0xffffffff	},
	{	MEMTYPE_FEARGRID,			"FearGrid",				0xffffffff	},
	{	MEMTYPE_VBUFFER,			"VBuffer",				0xffffffff	},
	{	MEMTYPE_VBUFFER_DATA,		"VBufferData",			0xffffffff	},
	{	MEMTYPE_DYNAMIC_VBUFFER_DATA,"Dynamic VBufferData",	0xffffffff	},
	{	MEMTYPE_MAPTEX,				"MapTex",				0xffffffff	},
	{	MEMTYPE_KEMPYCUBE,			"KempyCube",			0xffffffff	},
	{	MEMTYPE_SKY,				"Sky",					0xffffffff	},
	{	MEMTYPE_WATER,				"Water",				0xffffffff	},
	{	MEMTYPE_LIGHT,				"Light",				0xffffffff	},
	{	MEMTYPE_LANDSCAPE,			"Landscape",			0xffffffff	},
	{	MEMTYPE_GAMUT,				"Gamut",				0xffffffff	},
	{	MEMTYPE_FRONTEND,			"FrontEnd",				0xffffffff	},	
	{	MEMTYPE_HUD,				"HUD",					0xffffffff	},
	{	MEMTYPE_IMPOSTER,			"Imposter",				0xffffffff	},
	{	MEMTYPE_MESHVB,				"Mesh VB",				0xffffffff	},
	{	MEMTYPE_RENDERTHING,		"Render Thing",			0xffffffff	},
	{	MEMTYPE_WEAPONMODE,			"Weapon Mode",			0xffffffff	},
	{	MEMTYPE_WEAPON,				"Weapon",				0xffffffff	},
	{	MEMTYPE_SPAWNER,			"Spawner",				0xffffffff	},
	{	MEMTYPE_EXPLOSION,			"Explosion",			0xffffffff	},
	{	MEMTYPE_COMPONENT,			"Component",			0xffffffff	},
	{	MEMTYPE_FEATURE,			"Feature",				0xffffffff	},
	{	MEMTYPE_DETAILLEVEL,		"Detail Level",			0xffffffff	},
	{	MEMTYPE_EVENTMANAGER,		"Event Manager",		0xffffffff	},
	{	MEMTYPE_SHADOW,				"Shadow",				0xffffffff	},
	{	MEMTYPE_MAPWHO,				"Map Who",				0xffffffff	},
	{	MEMTYPE_POLYBUCKET,			"Poly Bucket",			0xffffffff	},
	{	MEMTYPE_POLYBUCKET_ENTRY,	"Poly Bucket Entry",	0xffffffff	},
	{	MEMTYPE_PTRSET,				"PtrSet",				0xffffffff	},
	{	MEMTYPE_RADAR,				"Radar",				0xffffffff	},
	{	MEMTYPE_SOUND,				"Sound",				0xffffffff	},
	{	MEMTYPE_SOUND_SAMPLE,		"Sound sample",			0xffffffff	},
	{	MEMTYPE_SPTRSET,			"SPtrSet",				0xffffffff	},
	{	MEMTYPE_BYTESPRITE,			"ByteSprite",			0xffffffff	},
	{	MEMTYPE_CONSOLE,			"Console",				0xffffffff	},
	{	MEMTYPE_WORLDMESHLIST,		"World Mesh List",		0xffffffff	},
	{	MEMTYPE_VERTEXSHADER,		"Vertex Shader",		0xffffffff	},
	{	MEMTYPE_WALL,				"Wall",					0xffffffff	},
	{	MEMTYPE_IBUFFER,			"IBuffer",				0xffffffff	},
	{	MEMTYPE_TREE,				"Tree man stuff",		0xffffffff	},
	{	MEMTYPE_FONT,				"Font",					0xffffffff	},	
	{	MEMTYPE_ARRAY,				"Array",				0xffffffff	},	

	{	MT_DESTROYABLESEGMENT,		"Destroyable Segment",	0xffffffff	},
	{	MT_DISPLAYLIST,				"Display List",			0xffffffff	},
	{	MT_PS2PROFILER,				"PS2 Profiler",			0xffffffff	},
	{	MT_PRIMITIVECOMPILER,		"Primitive Compiler",	0xffffffff	},
	{   MEMTYPE_ACTIVE_READER,		"Active Reader",		0xffffffff  },
	{   MT_LANDSCAPEDATA,			"Land scape data",	    0xffffffff  },
	{   MEMTYPE_SPHERETRIGGER,		"Sphere trig contains", 0xffffffff  }, 
	{   MEMTYPE_GENERAL_VOLUME,     "General volume",		0xffffffff  },
	{   MEMTYPE_SCRIPT_THING_PTR,   "Script thing ptr",		0xffffffff  },
	{   MEMTYPE_DELETION_CALLBACK_LIST, "Deletion callback list", 0xffffffff },
	{   MT_PALETTIZER,				"Palettizer",			0xffffffff },

	{   MEMTYPE_SCRATCHPAD,			"Scratchpad",			0xffffffff },
	{   MEMTYPE_TEMP,				"Temp",					0xffffffff },
	{   MEMTYPE_MEMTAG,				"Memory tag",			0xffffffff },
	{   MEMTYPE_DUMPTEMP,			"Memory dump temporary",0xffffffff },

	{	MEMTYPE_ATMOSPHERICS,		"Atmospherics",			0xffffffff },

	{	MEMTYPE_AREA,				"Area",					0xffffffff },
	{	MEMTYPE_DIRECTOR,			"Director",				0xffffffff },
	{	MEMTYPE_GROUPMANAGER,		"Group manager",		0xffffffff },
	{	MEMTYPE_PATH,				"Path",					0xffffffff },
	{	MEMTYPE_WORLD,				"World",				0xffffffff },
	{	MEMTYPE_CONFIGURATIONS,		"Configurations",		0xffffffff },
	{	MEMTYPE_COLLISION,			"Collision",			0xffffffff },
	{	MEMTYPE_MODELVIEWER,		"Model viewer",			0xffffffff },
	{	MEMTYPE_NODE,				"Node",					0xffffffff },
	{	MEMTYPE_GAMEDISC,			"Gamedisc",				0xffffffff },
	{	MEMTYPE_STATICSHADOW,		"Static shadow",		0xffffffff },
	{	MEMTYPE_DEBUGMARKER,		"Debug marker",			0xffffffff },
	{	MEMTYPE_TEXT,				"Text",					0xffffffff },
	{	MEMTYPE_SCENENODE,			"Scene nodes",			0xffffffff },
	{	MEMTYPE_MESHCACHE,			"Meshpart cache",		0xffffffff },
	{	MEMTYPE_SCENE,				"Scene",				0xffffffff },
	{	MEMTYPE_SCRIPT,				"Script2",				0xffffffff },
	{	MEMTYPE_MIXERMAP,			"Mixer map",			0xffffffff },
	{   MEMTYPE_HELP_TEXT_DISPLAY,  "Help Text Display",    0xffffffff },
	{   MEMTYPE_STORAGE,			"Storage fallback",		0xffffffff },

	{   MEMTYPE_BINK_VIDEO,			"Bink Video",			0xffffffff },
	{   MEMTYPE_MAPWHO_ENTRY,		"Mapwho entry",			0xffffffff },
	{   MEMTYPE_MEMORYCARD,			"Memory card",			0xffffffff },
	{   MEMTYPE_POSE,				"Mesh pose data",		0xffffffff },
	{	MEMTYPE_INDEX_BUFFER,		"Index buffer data",	0xffffffff },
	
	{	MEMTYPE_TINY_HEAP,			"Tiny Block heap",		0xffffffff },
	{	MEMTYPE_FMV,				"FMV",					0xffffffff },

	{	MEMTYPE_UNKNOWN,			"Unknown",				0xffffffff	},
	{	MEMTYPE_LIMIT,				"",						0xffffffff	}
};

#define SMALL_BLOCK_GAIN_TOLERANCE 1     // multiply by 16 bytes

//*******************************************************************
//
//	CMemoryBlock
//
//*******************************************************************

void CMemoryBlock::Init( UINT aSize, BOOL aUsed, EMemoryType aType, char * aFilename, UINT aLine )
{
	mMagic = MEM_MAGIC_HEADER;

	ASSERT( (aSize&0xf) == 0 );
	mSize = aSize - MEMBLOCK_HEADER_SIZE;
	SetUsed( aUsed );
	SetBaseSet(FALSE);
	mType = aType;
	mpNext = 0;
}

void CMemoryBlock::Resize( UINT aSize )
{
	// round size up to nearest 16 bytes:
	ASSERT( (aSize&0xf) == 0 );
	mSize = aSize - MEMBLOCK_HEADER_SIZE;
}

void CMemoryBlock::SetUsed( BOOL aUsed )
{
	if( aUsed )
	{
		mSize |= 1;
	}
	else
	{
		mSize &= 0xfffffffe;
	}
}

//*****************************************************************************
//
//	CMemoryHeap
//
//*****************************************************************************

void *CMemoryHeap::AllocTiny()
{
	// do we have space?
	if (mTinyHeapFreePtr)
	{
		// We have space!
		void *retval = mTinyHeapFreePtr;
		mTinyHeapFreePtr = *(void **)mTinyHeapFreePtr;

		return retval;
	}

	// oh dear.
	return NULL;
}

CMemoryBlock * CMemoryHeap::GetFreeBlock( UINT aMinSize )
{
	// searches free blocks to find another free block with size at least aMinSize
	CMemoryBlock *lpPrev = 0;
	for( CMemoryBlock * lpBlock = mpFree; lpBlock; lpPrev = lpBlock, lpBlock = lpBlock->mpNext  )
	{
		if( lpBlock->GetSize() >= aMinSize )
		{
			if( lpPrev )
			{
				lpPrev->mpNext = lpBlock->mpNext;
			}
			else
			{
				// block is first in free list:
				mpFree = lpBlock->mpNext;
			}
			lpBlock->mpNext = 0;
			return lpBlock;
		}
	}

	return 0;
}

// FUNCTION: 0x004a1390
CMemoryHeap::CMemoryHeap()
{
	m_Mutex = CreateMutexA(NULL, FALSE, NULL);
}

// FUNCTION: 0x004a13b0
BOOL CMemoryHeap::Init( UINT aSize, UINT tinysize, char *name, BOOL bSupportSmallAllocs)
{
	m_bSupportSmallAllocs = bSupportSmallAllocs;

	// memory usage statistics:
	int i;
	mUsedSize = mFreeSize = 0;
	mUsedMaxSize = 0;
	for( i=0; i<MEMTYPE_LIMIT; ++i )
	{
		mTypeSize[ i ] = 0;
		mLastTypeSize[ i ] = 0;
	}

	// block statistics:
	mUsedBlocks = mFreeBlocks = 0;
	for( i=0; i<MEMTYPE_LIMIT; ++i )
	{
		mTypeBlocks[ i ] = 0;
		mLastTypeBlocks[ i ] = 0;
	}

	for( i=0; i<16; ++i )
	{
		mpSmallFree[ i ] = 0;
	}

	if( aSize & 0xf )
	{
		aSize += 0x10;
	}
	aSize &= 0xfffffff0;

	mMerge = TRUE;

	ASSERT(strlen(name)<64);
	strcpy(mName,name);

	// Add heap to the heap list

	mNextHeap=MEM_MANAGER.mFirstHeap;
	MEM_MANAGER.mFirstHeap=this;

	// allocate memory and add to free list
	mSize = aSize;

	// just a simple malloc thanks.
	mpMem = malloc( mSize );

	if( mpMem )
	{
		while( (UINT)mpMem & 0xf )
		{
			// nasty - will break deletion
			mpMem = (void*)( (UINT)mpMem + 1 );
			mSize--;
		}
		mSize = (mSize>>4)<<4;
		mpFree = (CMemoryBlock*)mpMem;
		mpFree->Init( mSize );
		mpFree->SetUsed( FALSE );
		mpFree->SetBaseSet(FALSE);
		mFreeSize = aSize;
		mFreeBlocks++;

		ASSERT( ( (UINT)mpMem & 0xf ) == 0 );

		if (tinysize != 0)
		{
			// Now allocate a block for the tiny stuff.
			ASSERT((tinysize & (TINY_BLOCK - 1)) == 0);
#line 379
			mTinyHeap = Alloc(tinysize, MEMTYPE_TINY_HEAP, __FILE__, __LINE__);

			// And fill it
			void *freeness;
			for (freeness = mTinyHeap; freeness < (void *)(((char *)mTinyHeap) + tinysize - TINY_BLOCK); freeness = *(void **)freeness)
			{
				*(void **)freeness = (void *)(((char *)freeness) + TINY_BLOCK);
			}

			// terminate it
			*(void **)freeness = NULL;

			// and start it.
			mTinyHeapFreePtr = mTinyHeap;

			mTinyHeapEnd = (void *)(((char *)mTinyHeap) + tinysize);
		}
		else
		{
			// no tiny heap.
			mTinyHeapFreePtr = mTinyHeapEnd = mTinyHeap = NULL;
		}

		return TRUE;
	}

	return FALSE;
}

// FUNCTION: 0x004a1570
bool CMemoryHeap::FreeTiny(void *mem)
{
	if (mem && mem >= mTinyHeap && mem < mTinyHeapEnd)
	{
		// it's one of mine! Stick it back in the free list.
		ASSERT(((char *)mem - (char *)mTinyHeap) % TINY_BLOCK == 0); // should be on a block boundary.
		*(void **)mem = mTinyHeapFreePtr;
		mTinyHeapFreePtr = mem;

		return true;
	}

	return false;
}

// FUNCTION: 0x004a15a0
bool CMemoryHeap::ReallocTiny(void *mem, UINT newsize, void **retval)
{
	if (!IsTiny(mem)) return false;

	// Let's just get the data into temporary space.
	char temp[TINY_BLOCK];
	memcpy(temp, mem, TINY_BLOCK);

	// get rid of the current alloc
	FreeTiny(mem);

	// create a new one.
#line 463
	void *data = Alloc(newsize, MEMTYPE_GENERIC, __FILE__, __LINE__);

	if (data)
	{
		UINT copysize = min(newsize, TINY_BLOCK);
		memmove(data, temp, copysize);
	}
	else
	{
		// Oh dear out of memory. The complaint will happen in the Alloc method so
		// nothing needs doing here.
	}

	*retval = data;

	return true;
}

// FUNCTION: 0x004a1640
void CMemoryHeap::Cleanup(bool needs_mutex)
{
	if (needs_mutex) GRAB_MUTEX(&m_Mutex);

	// removes all blocks from free lists,
	// then merges blocks and adds to lists again.
	// Should result in non-fragmented free blocks:
	int i;
	for( i=0; i<16; ++i )
	{
		mpSmallFree[ i ] = 0;
	}
	mpFree = 0;

	CMemoryBlock * lpBlock;
	for( lpBlock = (CMemoryBlock*)mpMem; ; lpBlock = lpBlock->GetBlockBelow() )
	{
		lpBlock->mpNext = 0;
		if( lpBlock->IsFree() )
		{
			while( (!IsLastBlock(lpBlock) )&& lpBlock->GetBlockBelow()->IsFree() )
			{
				// can merge these blocks:
				UINT lMergedSize = lpBlock->GetBlockSize() + lpBlock->GetBlockBelow()->GetBlockSize();
				lpBlock->Resize( lMergedSize );
				mFreeBlocks--;
			}

			// special case for small blocks:
			if ((lpBlock->GetSize() < 256) && m_bSupportSmallAllocs)
			{
				// add free block into list in memory order (to get maximum reuse):
				CMemoryBlock * lpPrev = 0;
				for( CMemoryBlock * lpListBlock = mpSmallFree[ lpBlock->GetSize() >> 4 ]; lpListBlock; lpPrev = lpListBlock, lpListBlock = lpListBlock->mpNext )
				{
					if( (UINT)lpListBlock < (UINT)lpBlock )
					{
						break;
					}
				}
				if( lpPrev )
				{
					// add into list:
					lpBlock->mpNext = lpPrev->mpNext;
					lpPrev->mpNext = lpBlock;
				}
				else
				{
					// add to start of list:
					lpBlock->mpNext = mpSmallFree[ lpBlock->GetSize() >> 4 ];
					mpSmallFree[ lpBlock->GetSize() >> 4 ] = lpBlock;
				}
			}
			else
			{
				// add free block into list in size order:
				CMemoryBlock * lpPrev = 0;
				BOOL lBreak = FALSE;
				for( CMemoryBlock * lpListBlock = mpFree; lpListBlock; lpPrev = lpListBlock, lpListBlock = lpListBlock->mpNext )
				{
					if( lpListBlock->GetSize() >= lpBlock->GetSize() )
					{
						break;
					}
				}
				if( lpPrev )
				{
					// add into list:
					lpBlock->mpNext = lpPrev->mpNext;
					lpPrev->mpNext = lpBlock;
				}
				else
				{
					// add to start of list:
					lpBlock->mpNext = mpFree;
					mpFree = lpBlock;
				}
			}
		}

		if( IsLastBlock( lpBlock ) )
		{
			break;
		}
	}

	if (needs_mutex) RELEASE_MUTEX(&m_Mutex);
}

// FUNCTION: 0x004a17b0
void CMemoryHeap::Shutdown()
{
	// Remove ourselves from the heap list
	CMemoryHeap *c=MEM_MANAGER.mFirstHeap;
	CMemoryHeap *l=NULL;

	while (c && (c!=this))
	{
		l=c;
		c=c->mNextHeap;
	}

	ASSERT(c);

	if (l)
		l->mNextHeap=this->mNextHeap;
	else
		MEM_MANAGER.mFirstHeap=this->mNextHeap;

	free(mpMem);

	RELEASE_MUTEX(m_Mutex);
	m_Mutex = 0;
}

//*****************************************************************************
// FUNCTION: 0x004a1810
void * CMemoryHeap::Alloc( UINT aSize, EMemoryType aType, char * apFilename, UINT aLine )
{
	PROTECT_WITH_MUTEX();

	// round up to next 16
	aSize += 0xf;
	aSize &= 0xfffffff0;

	// Try allocating in the tiny heap.
	if (aSize <= TINY_BLOCK)
	{
		void *retval = AllocTiny();
		if (retval) return retval;

		// otherwise, no space, drop through to default allocator.
	}

	UINT lUsedBlockSize = aSize + MEMBLOCK_HEADER_SIZE;

	// do some special stuff if block is small:
	if ((aSize < 256) && (m_bSupportSmallAllocs))
	{
		int min_size = (aSize >> 4) + SMALL_BLOCK_GAIN_TOLERANCE ;
		if (min_size > 16 ) min_size = 16 ;

		for( int i=aSize >> 4; i< min_size;  ++i )
		{
			if( mpSmallFree[ i ] )
			{
				CMemoryBlock * lpBlock = mpSmallFree[ i ];
				mpSmallFree[ i ]->SetUsed( TRUE );
				mpSmallFree[ i ]->SetBaseSet( FALSE );
				mpSmallFree[ i ]->mType = aType;

				// find new free block:
				mpSmallFree[ i ] = mpSmallFree[ i ]->mpNext;

				mFreeSize -= lpBlock->GetBlockSize();
				mUsedSize += lpBlock->GetBlockSize();
				mTypeSize[ aType ] += lpBlock->GetBlockSize();
				mUsedBlocks++;
				mFreeBlocks--;
				mTypeBlocks[ aType ]++;

				return lpBlock->GetMem();
			}
		}
	}

	CMemoryBlock * lpFree = GetFreeBlock( aSize );

	if( !lpFree )
	{
		lpFree = GetFreeBlock( aSize );

		// no free memory at all:
		Cleanup(false);

		lpFree = GetFreeBlock( aSize );

		if( !lpFree )
		{
			// still no free memory at all!:
			LOG.AddMessage("FATAL ERROR: Out of memory!!! (trying to allocate %d bytes)", aSize) ;
			LogStats();
			MEM_MANAGER.DumpMemory("Out of memory");
			SASSERT(0,"Out of memory!");
			return 0;
		}
	}

	if( lUsedBlockSize < lpFree->GetSize() )
	{
		// no problem to split block

		// calculate new block sizes (including pad);
		UINT lFreeBlockSize = lpFree->GetBlockSize() - lUsedBlockSize;

		CMemoryBlock * lpUsed = lpFree;
		lpFree = (CMemoryBlock*)( (UINT)lpUsed + lUsedBlockSize );

		// resize free block:
		lpFree->Init( lFreeBlockSize, FALSE, aType, apFilename, aLine );
		AddToFreeList( lpFree );

		// create used block:
		lpUsed->Init( lUsedBlockSize, TRUE, aType, apFilename, aLine );

		mFreeSize -= lUsedBlockSize;
		mUsedSize += lUsedBlockSize;
		if( mUsedSize > mUsedMaxSize )
		{
			mUsedMaxSize = mUsedSize;
		}
		mTypeSize[ aType ] += lUsedBlockSize;
		mUsedBlocks++;
		mTypeBlocks[ aType ]++;

		return lpUsed->GetMem();
	}

	else if( aSize <= lpFree->GetSize() )
	{
		// free block too small to split. just convert whole block to used
		lUsedBlockSize = lpFree->GetBlockSize();

		// add to used list:
		CMemoryBlock * lpBlock = lpFree;
		lpFree->SetUsed( TRUE );
		lpFree->SetBaseSet( FALSE );
		lpFree->mType = aType;

		mFreeSize -= lUsedBlockSize;
		mUsedSize += lUsedBlockSize;
		mTypeSize[ aType ] += lUsedBlockSize;
		mUsedBlocks++;
		mFreeBlocks--;
		mTypeBlocks[ aType ]++;

		return lpBlock->GetMem();
	}
	else
	{
		// not enough free memory:
		Cleanup(false);													// slow, but might find us the memory we need
		CMemoryBlock * lpFree = GetFreeBlock( aSize );

		if( !lpFree )
		{
			ASSERT( 0 );
			return 0;
		}
		else
		{
			return Alloc( aSize, aType, apFilename, aLine );
		}
	}
}

//*****************************************************************************
// FUNCTION: 0x004a1c40
void * CMemoryHeap::ReAlloc( CMemoryBlock * apBlock, UINT aSize )
{
	// don't grab the mutex, the functions called will do that instead
	void * lpMem = Alloc( aSize, apBlock->GetType(), "", 0 );
	if( lpMem )
	{
		SINT lSize = ( apBlock->GetSize() > aSize ) ? aSize : apBlock->GetSize();
		memcpy( lpMem, apBlock->GetMem(), lSize );
	}
	Free( apBlock );

	return lpMem;
}

//*****************************************************************************
// FUNCTION: 0x004a1ca0
void CMemoryHeap::Free( CMemoryBlock * apBlock )
{
	PROTECT_WITH_MUTEX();

	UINT lBlockSize = apBlock->GetBlockSize();

	mFreeSize += lBlockSize;
	mUsedSize -= lBlockSize;
	mTypeSize[ apBlock->GetType() ] -= lBlockSize;
	ASSERT( mTypeSize[ apBlock->GetType() ] >= 0 );
	mUsedBlocks--;
	ASSERT( mUsedBlocks >= 0 );
	mFreeBlocks++;
	mTypeBlocks[ apBlock->GetType() ]--;

	apBlock->SetUsed( FALSE );
	apBlock->SetBaseSet( FALSE );

	AddToFreeList( apBlock );
}

//*****************************************************************************
// FUNCTION: 0x004a1d60
void CMemoryHeap::AddToFreeList( CMemoryBlock * apBlock )
{
	// special case for small blocks:
	if ((apBlock->GetSize() < 256) && m_bSupportSmallAllocs)
	{
		apBlock->mpNext = mpSmallFree[ apBlock->GetSize() >> 4 ];
		mpSmallFree[ apBlock->GetSize() >> 4 ] = apBlock;
	}
	else
	{
		CMemoryBlock * lpPrev = 0;
		CMemoryBlock * lpBlock;

		if( mMerge )
		{
			// attempt to merge with existing free blocks:
			for( lpBlock = mpFree; lpBlock; lpPrev = lpBlock, lpBlock = lpBlock->mpNext )
			{
				ASSERT( lpBlock->IsValid() );
				if( lpBlock->GetBlockBelow() == apBlock )
				{
					// remove block from list:
					if( lpPrev )
					{
						lpPrev->mpNext = lpBlock->mpNext;
					}
					else
					{
						mpFree = lpBlock->mpNext;
					}

					// merge blocks:
					UINT lMergedSize = lpBlock->GetBlockSize() + apBlock->GetBlockSize();
					apBlock = lpBlock;
					apBlock->Resize( lMergedSize );
					mFreeBlocks--;
					break;
				}
			}

			lpPrev = 0;
			for( lpBlock = mpFree; lpBlock; lpPrev = lpBlock, lpBlock = lpBlock->mpNext )
			{
				if( apBlock->GetBlockBelow() == lpBlock )
				{
					// remove block from list:
					if( lpPrev )
					{
						lpPrev->mpNext = lpBlock->mpNext;
					}
					else
					{
						mpFree = lpBlock->mpNext;
					}

					// merge blocks:
					UINT lMergedSize = lpBlock->GetBlockSize() + apBlock->GetBlockSize();
					apBlock->Resize( lMergedSize );
					mFreeBlocks--;
					break;
				}
			}

			// add free block into list in size order:
			lpPrev = 0;
			for( lpBlock = mpFree; lpBlock; lpPrev = lpBlock, lpBlock = lpBlock->mpNext )
			{
				if( lpBlock->GetSize() >= apBlock->GetSize() )
				{
					break;
				}
			}
		}

		if( lpPrev )
		{
			// add into list:
			apBlock->mpNext = lpPrev->mpNext;
			lpPrev->mpNext = apBlock;
		}
		else
		{
			// add to start of list:
			apBlock->mpNext = mpFree;
			mpFree = apBlock;
		}
	}
}

//*****************************************************************************
// FUNCTION: 0x004a1ea0
void	CMemoryHeap::SetMerge( BOOL aMerge )
{
	if (aMerge == TRUE)
	{
		if (mMerge == FALSE)
		{
			// going from non-merged to merged.

			// Cleanup, then sort the blocks
			Cleanup();

			CMemoryBlock	*fb = mpFree;
			BOOL			first_time = TRUE;

			while (fb)
			{
				// simple (shite) sort - find smallest first.
				CMemoryBlock	*b = fb;
				CMemoryBlock	*smallest = fb;
				CMemoryBlock	*prev = NULL;
				CMemoryBlock	*prev_small = NULL;
				UINT			size = 0x8fffffff;

				while (b)
				{
					if (b->GetSize() < size)
					{
						size = b->GetSize();
						smallest = b;
						prev_small = prev;
					}
					prev = b;
					b = b->mpNext;
				};

				// remove from list and put at head
				if (prev_small)
				{
					prev_small->mpNext = smallest->mpNext;
					smallest->mpNext = fb;
					fb = smallest;
				}
				else
				{
					// was first one, so don't need to do anything
				}

				if (first_time)
				{
					mpFree = fb;
					first_time = FALSE;
				}

				fb = fb->mpNext;
			}
		}
	}

	mMerge = aMerge;
}

//*****************************************************************************
// FUNCTION: 0x004a1f60
void CMemoryHeap::OutputStats( char * aFilename )
{
	char lBuffer[ 100 ];
	char lOutput[ 256*100 ];

	sprintf( lOutput, "Used: %d bytes\n", mUsedSize );

	sprintf( lBuffer, "Free: %d bytes\n", mFreeSize );
	strcat( lOutput, lBuffer );

	sprintf( lBuffer, "Total: %d bytes\n\n", mSize );
	strcat( lOutput, lBuffer );

	// print out details:

	UINT BySize[ MEMTYPE_LIMIT ];
	UINT i;
	for( i=0; i<MEMTYPE_LIMIT; ++i )
	{
		BySize[ i ] = i;
	}
	BOOL lSwap = TRUE;
	while( lSwap )
	{
		lSwap = FALSE;
		for( i=0; i<MEMTYPE_LIMIT-1; ++i )
		{
			if( mTypeSize[ BySize[ i ] ] < mTypeSize[ BySize[ i+1 ] ] )
			{
				lSwap = TRUE;
				UINT lTemp = BySize[ i ];
				BySize[ i ] = BySize[ i+1 ];
				BySize[ i+1 ] = lTemp;
			}
		}
	}

	for( i=0; i<MEMTYPE_LIMIT; ++i )
	{
		sprintf( lBuffer, "%-32s : %15d bytes : %15d blocks\n", CMEMORYMANAGER::mTypeName[BySize[i]], mTypeSize[BySize[i]], mTypeBlocks[BySize[i]] );
		strcat( lOutput, lBuffer );
	}

	char lFilename[ 256 ];
	sprintf( lFilename, "data\\Memory\\%s", aFilename );

	CMEMBUFFER lMemBuf;
	lMemBuf.InitFromMem( lFilename );
	lMemBuf.Write( lOutput, strlen( lOutput ) );
	lMemBuf.Close();
}

//*****************************************************************************
void CMemoryHeap::CalcSmallBlocksLeft(int num, int* blocks_used, int* mem_used)
{
	CMemoryBlock* block = 	mpSmallFree[ num ] ;
	while (block)
	{
		(*blocks_used)++;
		(*mem_used)+=block->GetBlockSize() ;
		block = block->mpNext ;
	}
}

//*****************************************************************************
// FUNCTION: 0x004a2190
void CMemoryHeap::PrintStats()
{
	char lBuffer[ 100 ];

	int font_height = PLATFORM.Font(FONT_DEBUG)->GetHeight();

	float font_left = 32;

	sprintf(lBuffer, "Used: %d bytes\n", mUsedSize );
	int y = 32;
	PLATFORM.Font(FONT_DEBUG)->DrawText( font_left, float(y), 0xffffffff, ToWCHAR(lBuffer));
	y+=font_height;

	sprintf( lBuffer, "Free: %d bytes\n", mFreeSize );
	PLATFORM.Font(FONT_DEBUG)->DrawText( font_left, float(y), 0xffffffff, ToWCHAR(lBuffer));
	y+=font_height;
	sprintf( lBuffer, "Total: %d bytes\n", mSize );
	PLATFORM.Font(FONT_DEBUG)->DrawText( font_left, float(y), 0xffffffff, ToWCHAR(lBuffer));
	y+=font_height;

	int ii=0;
	for (ii=0;ii<16;ii+=2)
	{
		int num_small_blocks[2]  = {0,0};
		int small_blocks_mem_used[2] ={0,0};

		CalcSmallBlocksLeft(ii, &num_small_blocks[0], &small_blocks_mem_used[0]) ;
		CalcSmallBlocksLeft(ii+1, &num_small_blocks[1], &small_blocks_mem_used[1]) ;
		sprintf( lBuffer, "(%2d) num blocks: %5d  (bytes %8d)         (%2d) num blocks: %5d  (bytes %8d) \n\n", ii, num_small_blocks[0], small_blocks_mem_used[0], ii+1,num_small_blocks[1], small_blocks_mem_used[1] );
		PLATFORM.Font(FONT_DEBUG)->DrawText( font_left, float(y), 0xffffffff, ToWCHAR(lBuffer));
		y+=font_height;
	}

	// print out details:

	UINT BySize[ MEMTYPE_LIMIT ];
	UINT i;
	for( i=0; i<MEMTYPE_LIMIT; ++i )
	{
		BySize[ i ] = i;
	}
	BOOL lSwap = TRUE;
	while( lSwap )
	{
		lSwap = FALSE;
		for( i=0; i<MEMTYPE_LIMIT-1; ++i )
		{
			if( mTypeSize[ BySize[ i ] ] < mTypeSize[ BySize[ i+1 ] ] )
			{
				lSwap = TRUE;
				UINT lTemp = BySize[ i ];
				BySize[ i ] = BySize[ i+1 ];
				BySize[ i+1 ] = lTemp;
			}
		}
	}

	for( i=0; i<MEMTYPE_LIMIT; ++i )
	{
		y+=font_height;
		sprintf( lBuffer, "%-32s : %15d bytes : %15d blocks\n", CMEMORYMANAGER::mTypeName[BySize[i]], mTypeSize[BySize[i]], mTypeBlocks[BySize[i]] );
		PLATFORM.Font(FONT_DEBUG)->DrawText( font_left, float(y), 0xffffff00, ToWCHAR(lBuffer));
	}

}

//*****************************************************************************
// FUNCTION: 0x004a2460
void CMemoryHeap::LogStats()
{
	char	str[1000];

	sprintf(str, "Used: %d bytes\n", mUsedSize );
	LOG.AddMessage(str);
	TRACE(str);
	sprintf(str, "Free: %d bytes\n", mFreeSize );
	LOG.AddMessage(str);
	TRACE(str);
	sprintf(str, "Total: %d bytes\n", mSize );
	LOG.AddMessage(str);
	TRACE(str);

	// print out details:

	UINT BySize[ MEMTYPE_LIMIT ];
	UINT i;
	for( i=0; i<MEMTYPE_LIMIT; ++i )
	{
		BySize[ i ] = i;
	}
	BOOL lSwap = TRUE;
	while( lSwap )
	{
		lSwap = FALSE;
		for( i=0; i<MEMTYPE_LIMIT-1; ++i )
		{
			if( mTypeSize[ BySize[ i ] ] < mTypeSize[ BySize[ i+1 ] ] )
			{
				lSwap = TRUE;
				UINT lTemp = BySize[ i ];
				BySize[ i ] = BySize[ i+1 ];
				BySize[ i+1 ] = lTemp;
			}
		}
	}

	for( i=0; i<MEMTYPE_LIMIT; ++i )
	{
		if (mTypeSize[BySize[i]] > 0)
		{
			sprintf(str, "%-32s : %15d bytes : %15d blocks\n", CMEMORYMANAGER::mTypeName[BySize[i]], mTypeSize[BySize[i]], mTypeBlocks[BySize[i]] );
			LOG.AddMessage(str);
			TRACE(str);
		}
	}
}

//*****************************************************************************
// FUNCTION: 0x004a25c0
void CMemoryHeap::CalcAndShowDeltas()
{
	// JCL - display differences between this time and Last time.
	SINT	c0;
	char	str[400];

	for(c0 = 0; c0 < MEMTYPE_LIMIT; c0 ++)
	{
		if ((mTypeSize  [c0] != mLastTypeSize  [c0]) ||
			(mTypeBlocks[c0] != mLastTypeBlocks[c0]))
		{
			sprintf(str, "Heap Delta: %-32s : %15d bytes : %15d blocks\n", CMEMORYMANAGER::mTypeName[c0],
							mTypeSize[c0] - mLastTypeSize[c0],
							mTypeBlocks[c0] - mLastTypeBlocks[c0]);
			TRACE(str);
		}
	}

	// and store for next time
	for(c0 = 0; c0 < MEMTYPE_LIMIT; c0 ++)
	{
		mLastTypeSize[c0]   = mTypeSize[c0];
		mLastTypeBlocks[c0] = mTypeBlocks[c0];
	}
}

//*****************************************************************************
// FUNCTION: 0x004a2660
void CMemoryHeap::DumpMap(CMEMBUFFER *mb,int heapno)
{
	int blocks=0;
	CMemoryBlock *block;
	char buffer[256];

	for (block= (CMemoryBlock*) mpMem; !IsLastBlock(block); block=block->GetBlockBelow())
		if (block->GetType()!=MEMTYPE_DUMPTEMP)
			blocks++;

	sprintf(buffer,"# Heap %d\n",heapno);
	mb->Write(buffer,strlen(buffer));

	sprintf(buffer,"%s\n",mName);
	mb->Write(buffer,strlen(buffer));

	sprintf(buffer,"# Size\n");
	mb->Write(buffer,strlen(buffer));

	sprintf(buffer,"%d\n",mSize);
	mb->Write(buffer,strlen(buffer));

	sprintf(buffer,"# NumBlocks\n");
	mb->Write(buffer,strlen(buffer));

	sprintf(buffer,"%d\n",blocks);
	mb->Write(buffer,strlen(buffer));

	for (block= (CMemoryBlock*) mpMem; !IsLastBlock(block); block=block->GetBlockBelow())
	{
		if (block->GetType()!=MEMTYPE_DUMPTEMP)
		{
			sprintf(buffer,"# Heap %d Block %d\n",heapno,blocks);
			mb->Write(buffer,strlen(buffer));

			if (block->IsValid())
				sprintf(buffer,"1\n");
			else
				sprintf(buffer,"0\n");
			mb->Write(buffer,strlen(buffer));

			sprintf(buffer,"%d\n",block->GetSize());
			mb->Write(buffer,strlen(buffer));

			sprintf(buffer,"%d\n",block->GetBlockSize());
			mb->Write(buffer,strlen(buffer));

			sprintf(buffer,"%d\n",(UINT) block->GetMem());
			mb->Write(buffer,strlen(buffer));

			sprintf(buffer,"%d\n",block->GetType());
			mb->Write(buffer,strlen(buffer));

			if (block->IsFree())
				sprintf(buffer,"1\n");
			else
				sprintf(buffer,"0\n");
			mb->Write(buffer,strlen(buffer));

			sprintf(buffer,"Unknown\n");
			mb->Write(buffer,strlen(buffer));
			sprintf(buffer,"0\n");
			mb->Write(buffer,strlen(buffer));

			blocks++;
		}
	}
}

// FUNCTION: 0x004a2a20
void CMemoryManager::FlagAsBaseSet()
{
	CMemoryHeap *c=mFirstHeap;
	while (c)
	{
		c->FlagAsBaseSet();
		c=c->mNextHeap;
	}
}

void CMemoryHeap::FlagAsBaseSet()
{
	CMemoryBlock *block;
	for (block= (CMemoryBlock*) mpMem; !IsLastBlock(block); block=block->GetBlockBelow())
	{
		if (block->IsUsed())
		{
			block->SetBaseSet(TRUE);
		}
	}
}

//*****************************************************************************
// FUNCTION: 0x004a2a80
void CMemoryManager::DumpMemory(char *tracename)
{
	if (mDumpingMemory)
		return;

	mDumpingMemory=TRUE;

	char buf[256];
	sprintf(buf,"Writing memory dump '%s'\n",tracename);
	TRACE(buf);

#line 1800
	CMEMBUFFER *mb=new (MEMTYPE_DUMPTEMP) CMEMBUFFER;
	char filename[256];
	sprintf(filename,"MemoryDumps\\dump%d.mem",mTraceNumber);
	if (!mb->InitFromMem(filename,MEMTYPE_DUMPTEMP))
	{
		SAFE_DELETE(mb);
		return;
	}

	char buffer[8192];

	// Trace name

	sprintf(buffer,"#Trace name\n");
	mb->Write(buffer,strlen(buffer));

	sprintf(buffer,"%s\n",tracename);
	mb->Write(buffer,strlen(buffer));

	// Memory types

	sprintf(buffer,"#MemTypes\n");
	mb->Write(buffer,strlen(buffer));

	sprintf(buffer,"%d\n",MEMTYPE_LIMIT);
	mb->Write(buffer,strlen(buffer));

	for (int i=0;i<MEMTYPE_LIMIT;i++)
	{
		sprintf(buffer,"%s\n",CMEMORYMANAGER::mTypeName[i]);
		mb->Write(buffer,strlen(buffer));
	}

	// Memory map

	int heaps=0;
	CMemoryHeap *c=mFirstHeap;
	while (c)
	{
		heaps++;
		c=c->mNextHeap;
	}

	sprintf(buffer,"#Heaps\n");
	mb->Write(buffer,strlen(buffer));

	sprintf(buffer,"%d\n",heaps);
	mb->Write(buffer,strlen(buffer));

	c=mFirstHeap;
	heaps=0;
	while (c)
	{
		c->DumpMap(mb,heaps);
		heaps++;
		c=c->mNextHeap;
	}

	// Tags

	int tags=0;

	CMemoryTag *ctag=mFirstTag;
	while (ctag)
	{
		tags++;
		ctag=ctag->mNext;
	}

	sprintf(buffer,"# NumTags\n");
	mb->Write(buffer,strlen(buffer));

	sprintf(buffer,"%d\n",tags);
	mb->Write(buffer,strlen(buffer));

	ctag=mFirstTag;
	while (ctag)
	{
		sprintf(buffer,"# Tag\n");
		mb->Write(buffer,strlen(buffer));

		sprintf(buffer,"%s\n",ctag->mTag);
		mb->Write(buffer,strlen(buffer));

		sprintf(buffer,"%d\n",ctag->mAddress);
		mb->Write(buffer,strlen(buffer));

		sprintf(buffer,"%d\n",ctag->mSize);
		mb->Write(buffer,strlen(buffer));

		if (ctag->mDeleted)
			sprintf(buffer,"1\n");
		else
			sprintf(buffer,"0\n");
		mb->Write(buffer,strlen(buffer));

		ctag=ctag->mNext;
	}

	mb->Close();
	SAFE_DELETE(mb);

	mTraceNumber++;

	// Update the trace numbering file

#line 1914
	mb=new (MEMTYPE_DUMPTEMP) CMEMBUFFER;
	mb->InitFromMem("MemoryDumps\\trace.no",MEMTYPE_DUMPTEMP);

	sprintf(buffer,"%d\n",mTraceNumber);
	mb->Write(buffer,strlen(buffer));

	mb->Close();
	SAFE_DELETE(mb);
	mDumpingMemory=FALSE;
}

// Restored from pinned MemoryManager.cpp:1622-1659. MEMORY_TAGGING is absent in this retail build;
// variadic TagMem is an empty cdecl body, UnTagMem an empty thiscall body with ret 4.
void CMemoryManager::TagMem(void *addr,UINT size,const char *fmt,...)	// FUNCTION: 0x0040c640
{
#ifdef MEMORY_TAGGING
	CMemoryTag *t=new (MEMTYPE_MEMTAG) CMemoryTag;

	va_list arglist;

	va_start(arglist, fmt);
	vsprintf(t->mTag, fmt, arglist);
	va_end(arglist);

	for (int i=0;i<(int) strlen(t->mTag);i++)
		if (t->mTag[i]=='\\')
			t->mTag[i]='/';

	t->mAddress=addr;
	t->mSize=size;
	t->mNext=mFirstTag;
	t->mDeleted=FALSE;
	mFirstTag=t;
#endif
}

//*****************************************************************************

void CMemoryManager::UnTagMem(void *addr)	// FUNCTION: 0x00452b60
{
#ifdef MEMORY_TAGGING
	CMemoryTag *c=mFirstTag;

	while (c)
	{
		if (c->mAddress==addr)
			c->mDeleted=TRUE;
		c=c->mNext;
	}
#endif
}

//******************************************************************************************
// A heap frees nothing when destroyed (CDXMemoryManager's exit); the empty body is folded into the shared one
// (/OPT:ICF).

// FUNCTION: 0x0040c640
CMemoryHeap::~CMemoryHeap()
{
}
