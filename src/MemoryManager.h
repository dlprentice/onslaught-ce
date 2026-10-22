// MemoryManager.h: interface for the CMemoryManager class.
//
// Layout confirmed against the retail executable: CMemoryHeap is 0x8cc bytes (mMerge at +0x874, the mutex
// handle at +0x8bc, the tiny heap at +0x8c0..+0x8c8) and there are 129 memory types.

#ifndef MEMORYMANAGER_H
#define MEMORYMANAGER_H

enum EMemoryType
{
	MEMTYPE_GENERIC = 0,				// 0
	MEMTYPE_MESH,						// 1
	MEMTYPE_TEXTURE,					// 2
	MEMTYPE_TEXTURE_DATA,				// 3
	MT_PHYSICS,							// 4
	MT_THING,							// 5
	MT_UNIT_THING,						// 6
	MT_TREE_THING,						// 7
	MT_SQUAD,							// 8
	MT_INIT_THING,						// 9
	MT_BUBBLE,							// 10
	MT_CST,								// 11
	MT_DUNNO_THING,						// 12
	MT_ROUND,							// 13
	MT_ROUND_DATA,						// 14
	MT_UNIT_DATA,						// 15
	MEMTYPE_PARTICLE,					// 16
	MEMTYPE_MEMBUFFER,					// 17
	MEMTYPE_FLEXARRAY,					// 18
	MEMTYPE_CAPTURE,					// 19
	MEMTYPE_MUSIC,						// 20
	MEMTYPE_BATTLEENGINE,				// 21
	MEMTYPE_AI,							// 22
	MEMTYPE_GUIDE,						// 23
	MT_SCRIPT,							// 24
	MT_VM_SCRIPT,						// 25
	MT_INST_SCRIPT,						// 26
	MEMTYPE_MOTIONCONTROLLER,			// 27
	MEMTYPE_CHUNKER,					// 28
	MEMTYPE_CUTSCENE,					// 29
	MEMTYPE_EQUIPMENT,					// 30
	MEMTYPE_VBUFTEXTURE,				// 31
	MEMTYPE_INFLUENCEMAP,				// 32
	MEMTYPE_MAP,						// 33
	MEMTYPE_HEIGHTFIELD,				// 34
	MEMTYPE_JCLTEXTURE,					// 35
	MEMTYPE_MESHTEXTURE,				// 36
	MEMTYPE_NAVIGATION,					// 37
	MEMTYPE_CAMERA,						// 38
	MEMTYPE_CONTROLLER,					// 39
	MEMTYPE_PLAYER,						// 40
	MEMTYPE_MESSAGEBOX,					// 41
	MEMTYPE_MESSAGELOG,					// 42
	MEMTYPE_FEARGRID,					// 43
	MEMTYPE_VBUFFER,					// 44
	MEMTYPE_VBUFFER_DATA,				// 45
	MEMTYPE_DYNAMIC_VBUFFER_DATA,		// 46
	MEMTYPE_IBUFFER,					// 47
	MEMTYPE_MAPTEX,						// 48
	MEMTYPE_KEMPYCUBE,					// 49
	MEMTYPE_SKY,						// 50
	MEMTYPE_WATER,						// 51
	MEMTYPE_LIGHT,						// 52
	MEMTYPE_LANDSCAPE,					// 53
	MEMTYPE_GAMUT,						// 54
	MEMTYPE_FRONTEND,					// 55
	MEMTYPE_HUD,						// 56
	MEMTYPE_IMPOSTER,					// 57
	MEMTYPE_MESHVB,						// 58
	MEMTYPE_RENDERTHING,				// 59
	MEMTYPE_WEAPONMODE,					// 60
	MEMTYPE_WEAPON,						// 61
	MEMTYPE_SPAWNER,					// 62
	MEMTYPE_EXPLOSION,					// 63
	MEMTYPE_COMPONENT,					// 64
	MEMTYPE_FEATURE,					// 65
	MEMTYPE_DETAILLEVEL,				// 66
	MEMTYPE_EVENTMANAGER,				// 67
	MEMTYPE_SHADOW,						// 68
	MEMTYPE_MAPWHO,						// 69
	MEMTYPE_POLYBUCKET,					// 70
	MEMTYPE_POLYBUCKET_ENTRY,			// 71
	MEMTYPE_PTRSET,						// 72
	MEMTYPE_RADAR,						// 73
	MEMTYPE_SOUND,						// 74
	MEMTYPE_SOUND_SAMPLE,				// 75
	MEMTYPE_SPTRSET,					// 76
	MEMTYPE_BYTESPRITE,					// 77
	MEMTYPE_CONSOLE,					// 78
	MEMTYPE_WORLDMESHLIST,				// 79
	MEMTYPE_VERTEXSHADER,				// 80
	MEMTYPE_WALL,						// 81
	MEMTYPE_TREE,						// 82
	MEMTYPE_FONT,						// 83
	MEMTYPE_ARRAY,						// 84

	MT_DESTROYABLESEGMENT,				// 85
	MT_DISPLAYLIST,						// 86
	MT_PS2PROFILER,						// 87
	MT_PRIMITIVECOMPILER,				// 88
	MEMTYPE_ACTIVE_READER,				// 89
	MT_LANDSCAPEDATA,					// 90
	MEMTYPE_SPHERETRIGGER,				// 91
	MEMTYPE_GENERAL_VOLUME,				// 92
	MEMTYPE_SCRIPT_THING_PTR,			// 93
	MEMTYPE_DELETION_CALLBACK_LIST,		// 94
	MT_PALETTIZER,						// 95
	MEMTYPE_FMV,						// 96
	
	MEMTYPE_SCRATCHPAD,					// 97
	MEMTYPE_TEMP,
	
	MEMTYPE_MEMTAG,
	MEMTYPE_DUMPTEMP,

	MEMTYPE_ATMOSPHERICS,
	MEMTYPE_AREA,
	MEMTYPE_DIRECTOR,
	MEMTYPE_GROUPMANAGER,
	MEMTYPE_PATH,
	MEMTYPE_WORLD,
	MEMTYPE_CONFIGURATIONS,
	MEMTYPE_COLLISION,
	MEMTYPE_MODELVIEWER,
	MEMTYPE_NODE,
	MEMTYPE_GAMEDISC,
	MEMTYPE_STATICSHADOW,
	MEMTYPE_DEBUGMARKER,
	MEMTYPE_TEXT,
	MEMTYPE_SCENENODE,
	MEMTYPE_MESHCACHE,
	MEMTYPE_SCENE,
	MEMTYPE_SCRIPT,
	MEMTYPE_MIXERMAP,
	MEMTYPE_HELP_TEXT_DISPLAY,
	MEMTYPE_STORAGE,

	MEMTYPE_BINK_VIDEO,
	MEMTYPE_MAPWHO_ENTRY,
	MEMTYPE_MEMORYCARD,
	MEMTYPE_POSE,
		
	MEMTYPE_TINY_HEAP,

	MEMTYPE_INDEX_BUFFER,

	MEMTYPE_UNKNOWN,

	MEMTYPE_LIMIT
};

#define USE_THING_HEAP
#define THING_HEAP_SIZE (3500*1024)

#define TINY_BLOCK 16

struct MemoryTypeData
{
	EMemoryType		mType;
	char			mName[32];
	UINT			mMaxSize;
};

#define MEM_MAGIC_HEADER	0x4f69ea21
#define MEMBLOCK_HEADER_SIZE	(16)

class CDXMemBuffer;
#define CMEMBUFFER CDXMemBuffer

class CMemoryBlock
{
public:
	void			Init( UINT aSize, BOOL aUsed = FALSE, EMemoryType aType = MEMTYPE_GENERIC, char * apFilename = "", UINT aLine = 0 );
	void			Resize( UINT aSize );
	UINT			GetSize()		{ return (mSize & 0xfffffff0); }
	void *			GetMem()		{ return (void*)( (UBYTE*)(this) + MEMBLOCK_HEADER_SIZE ); }
	EMemoryType		GetType()		{ return mType; }
	UINT			GetBlockSize()	{ return ( GetSize() + MEMBLOCK_HEADER_SIZE ); }

	CMemoryBlock  *	GetBlockBelow()	{ return (CMemoryBlock*)( (UINT)this + GetBlockSize() ); }

	static CMemoryBlock  * GetBlock( void * apMem )	{ return (CMemoryBlock*)( (UINT)apMem - MEMBLOCK_HEADER_SIZE ); }

	BOOL			IsUsed() { return ( mSize & 0x1 ); }
	BOOL			IsFree() { return !( mSize & 0x1 ); }
	void			SetUsed( BOOL aUsed );
	BOOL			IsBaseSet() { return ( mSize & 0x2 ); }
	// FUNCTION: 0x004a2ff0
	void			SetBaseSet( BOOL bs )
	{
		if (bs)
			mSize |= 0x2;
		else
			mSize &= ~0x2;
	}
	BOOL			IsValid() { return ( mMagic == MEM_MAGIC_HEADER); }

	UINT			mMagic;
	UINT			mSize;				// size will always be a multiple of 16, so using lowest bit as used flag
	EMemoryType		mType;
	CMemoryBlock  *	mpNext;
};

class CMemoryHeap
{
public:
	BOOL	Init( UINT aSize, UINT tinysize, char *name, BOOL bSupportSmallAllocs=true);

	void	Shutdown();

	void  *	Alloc( UINT aSize, EMemoryType aType, char * apFilename, UINT aLine );
	void  * ReAlloc( CMemoryBlock * apBlock, UINT aSize );
	void	Free( CMemoryBlock * apBlock );
	void	AddToFreeList( CMemoryBlock * apBlock );
	void	Cleanup(bool needs_mutex = true);
	BOOL	FreeAll( EMemoryType aType );

	void	FlagAsBaseSet();
	void	ClearToBaseSet();

	UINT	GetTypeSize(EMemoryType type) { return(mTypeSize[type]); };

	BOOL	Validate(char *msg=NULL);

	void	CalcSmallBlocksLeft(int num, int* blocks_used, int* mem_used);
	void	OutputStats( char * aFilename );
	void 	LogStats();
	void    PrintStats() ;
	void	OutputBlocks( char * aFilename, EMemoryType aType );
	void	OutputMap(char *aFilename);

	void	SetMerge( BOOL aMerge );

	UINT	FindLargestFree();

	void	DumpMap(CMEMBUFFER *mb,int heapno);

	char	*GetName() { return(mName); };

	UINT	GetSize() { return ( mSize ); }
	UINT	GetPeakSize() { return ( mUsedMaxSize ); }
	UINT	GetUsedSize() { return ( mUsedSize ); }
	UINT	GetFreeSize() { return (mFreeSize);	}
	UINT	GetTotalFreeSize()	{ return mFreeSize+mFreeBlocks*MEMBLOCK_HEADER_SIZE; }

	// returns true if this was a tiny block after all
	bool	FreeTiny(void *data);
	bool	ReallocTiny(void *mem, UINT newsize, void **retval);

	// and this does too
	bool	IsTiny(void *data)
	{
		return data && data >= mTinyHeap && data < mTinyHeapEnd;
	}

	void	CalcAndShowDeltas();

	CMemoryHeap		*mNextHeap;

	CMemoryHeap();
	~CMemoryHeap();

protected:

	CMemoryBlock  *	GetFreeBlock( UINT aMinSize );

	CMemoryBlock  *	FindSmallestFree( UINT aMinSize = 0 );
	BOOL			IsLastBlock( CMemoryBlock * apBlock ) { return ( (UINT)apBlock->GetBlockBelow() >= (UINT)mpMem + mSize ); }

	void		  *	mpMem;
	UINT			mSize;
	CMemoryBlock  *	mpSmallFree[ 16 ];
	CMemoryBlock  *	mpFree;

	// memory usage statistics:
	SINT			mUsedSize, mFreeSize;
	SINT			mTypeSize[ MEMTYPE_LIMIT ];
	SINT			mUsedMaxSize;

	// JCL - more storage for deltas
	SINT			mLastTypeSize[ MEMTYPE_LIMIT ];
	SINT			mLastTypeBlocks[ MEMTYPE_LIMIT ];

	// block statistics:
	SINT			mUsedBlocks, mFreeBlocks;
	SINT			mTypeBlocks[ MEMTYPE_LIMIT ];

	// no merge hack to speed up quit:
	BOOL			mMerge;

	char			mName[64];

	BOOL m_bSupportSmallAllocs;

	// Multithreading: a Win32 mutex in the retail build.
	HANDLE m_Mutex;

	// And a tiny heap for tiny stuff.
	void		*	mTinyHeap;

	// which contains a linked list of free blocks.
	void		*	mTinyHeapFreePtr;

	// and is this big.
	void		*	mTinyHeapEnd;

	// alloc...
	void		*	AllocTiny();
};

class CMemoryTag
{
public:
					CMemoryTag()
					{
						mNext=NULL;
					}

	void			*mAddress;
	UINT			mSize;
	char			mTag[512];
	CMemoryTag		*mNext;
	BOOL			mDeleted;
};

class CMemoryManager
{
public:
					CMemoryManager()
					{
						mFirstTag=NULL;
						mTraceNumber=0;
						mFirstHeap=NULL;
					}

					// FUNCTION: 0x00549400
					~CMemoryManager()
					{
						while (mFirstTag)
						{
							CMemoryTag *n=mFirstTag->mNext;
							delete mFirstTag;
							mFirstTag=n;
						}
					}

	void			TagMem(void *addr,UINT size,const char *fmt,...);
	void			UnTagMem(void *addr);
	void			DumpMemory(char *tracename);

	void			FlagAsBaseSet();
	void			FlagBlockAsBaseSet( void * apMem );
	void			ClearToBaseSet();

	CMemoryHeap		*mFirstHeap;
protected:
	CMemoryTag		*mFirstTag;
	int				mTraceNumber;
	BOOL			mDumpingMemory;
};

#include "DXMemoryManager.h"
extern CDXMemoryManager MEM_MANAGER;
#define CMEMORYMANAGER CDXMemoryManager

// Typed allocation: new (MEMTYPE_X) CFoo allocates from the memory manager, tagged with the source line. The line
// is UINT, as Alloc's: an int line needs a conversion, which keeps one inlined parameter symbol alive per new and
// shifts the ids (and so the operand order) of every later inline in the function (CDXPatchManager::Init 00550430).
inline void *operator new(size_t size, EMemoryType type, char *file, UINT line)
{
	return MEM_MANAGER.Alloc(size, type, file, line);
}
// retail: a plain new goes to the memory manager too, untyped and unnamed (CPCPlatform::Deserialize 00515c70).
// The game always inlines the plain new and delete; their single out-of-line copies serve only the libraries
// (NvTriStrip and the STL call ::operator new and delete; no game code does). VC6 emits an extern inline
// replacement in every object that sees it and the linker keeps the first in link order, and retail keeps new's
// copy in collisionseekingthing.obj (00426fd0) and delete's in engine.obj (00449d40): so they are file-local
// everywhere else, and no earlier object emits a copy. Delete is also extern in the objects whose unwind funclets call
// it out of line (SoundManager, PCPlatform, PCMusicStream, IScript; retail's funclets call 00449d40), all after
// engine.obj in link order.
#ifdef COLLISIONSEEKINGTHING_CPP
inline void *operator new(size_t size)
#else
static inline void *operator new(size_t size)
#endif
{
	return MEM_MANAGER.Alloc(size, MEMTYPE_GENERIC, "", 0);
}
inline void operator delete(void *p, EMemoryType type, char *file, UINT line)
{
	MEM_MANAGER.Free(p);
}
#if defined(ENGINE_CPP) || defined(SOUNDMANAGER_CPP) || defined(PCPLATFORM_CPP) || defined(PCMUSICSTREAM_CPP) || defined(ISCRIPT_CPP)
inline void operator delete(void *p)
#else
static inline void operator delete(void *p)
#endif
{
	MEM_MANAGER.Free(p);
}
#define new(type) new(type, __FILE__, __LINE__)

#define SAFE_DELETE(p) { if (p) { delete (p); (p) = NULL; } }
#define SAFE_DELETE_ARRAY(p) { if (p) { delete [] (p); (p) = NULL; } }

#endif
