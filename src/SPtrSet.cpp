// SPtrSet.cpp: implementation of the SPtrSet class.
//
//////////////////////////////////////////////////////////////////////

#include "common.h"
#include "stdafx.h"

#include "SPtrSet.h"

#ifndef SIMPLE_MESHES
#include "debuglog.h"
#include "profile.h"
#endif



// used to store all the deleted nodes. 
SPtrSetNode* GenericSPtrSet::mFreeList = NULL;
SPtrSetNode* GenericSPtrSet::mBlock=NULL;
int			 GenericSPtrSet::mBlockSize = 0 ;


//******************************************************************************************
GenericSPtrSet::GenericSPtrSet() :	// FUNCTION: 0x004e5840
	mFirst(NULL),
	mLast(NULL),
	mSize(0)
{
}


//******************************************************************************************
// copy constructor
//******************************************************************************************
GenericSPtrSet::GenericSPtrSet(const GenericSPtrSet& copy):	// FUNCTION: 0x004e5850
	mFirst(NULL),
	mLast(NULL),
	mSize(0)
{
	GenericListIterator iterator((GenericSPtrSet*)&copy) ;
	for (void* item = iterator.First(); item != NULL ; item = iterator.Next() ) 
	{
		Append(item) ;
	}
}




//******************************************************************************************
// assignment operator
//******************************************************************************************
GenericSPtrSet& GenericSPtrSet::operator = (GenericSPtrSet& copy)	// FUNCTION: 0x004e58a0 ??4GenericSPtrSet@@QAEAAV0@AAV0@@Z
{
	RemoveAll();
	GenericListIterator iterator((GenericSPtrSet*)&copy) ;
	for (void* item = iterator.First(); item != NULL ; item = iterator.Next() ) 
	{
		Append(item) ;
	}

	return *this;
}



//******************************************************************************************
// static function that cleans up the free list. call at end of level
//******************************************************************************************
void	GenericSPtrSet::Shutdown()	// FUNCTION: 0x004e5910
{
	while (mFreeList)
	{
		SPtrSetNode* node = mFreeList ;
		mFreeList = mFreeList->mNext ;

		// If node was allocated dynamicaly rather than in block then delete it
		if (node < mBlock || node >= (mBlock + mBlockSize))
		{
			delete node ;
		}
	}
 
	// delete initial block
	delete [] mBlock ;

	mBlock = NULL ;
	mFreeList = NULL ;
}


//******************************************************************************************
void	GenericSPtrSet::ClearAnyDynamicCreatedNodes()	// FUNCTION: 0x004e5990
{
	
	SPtrSetNode* n = mFreeList;
	SPtrSetNode* prev = NULL ;
	while (n)
	{
		SPtrSetNode* node = n ;
		n = n->mNext ;

		// If node was allocated dynamicaly rather than in block then delete it
		if (node < mBlock || node >= (mBlock + mBlockSize))
		{
			if (prev)
			{
				prev->mNext = n ;
			}
			else
			{
				mFreeList = n;
			}

			delete node ;
		}
		else
		{
			prev = node ;
		}
	}
}


//******************************************************************************************
// static function that inits the free list. call before running a level
//******************************************************************************************
void	GenericSPtrSet::Init(int size)	// FUNCTION: 0x004e59f0
{
	if (mBlock != NULL)
	{
		LOG.AddMessage("Warning: Initilise SptrSet twice") ;
		return ;
	}
//	else
//	{
		mBlock = new ( MEMTYPE_SPTRSET )SPtrSetNode[size] ;
		mBlockSize = size ;
//	}

	mFreeList = mBlock ; 
	for (int i=0; i <= mBlockSize-2; i++)
	{
		mFreeList[i].mNext = &mFreeList[i+1] ;
	}
	mFreeList[mBlockSize-1].mNext = NULL ;
}


//******************************************************************************************
// adds to the start of the list
//******************************************************************************************

void	GenericSPtrSet::Add(void* ptr)	// FUNCTION: 0x004e5a80
{
	if (mBlock == NULL)
	{
		// THIS IS NOW FATAL !!!
		LOG.AddMessage("FATAL ERROR: SptSet::Add when freelist has not been setup") ;
		ASSERT(0);
	}

	// has free list got any entries ?
	SPtrSetNode* node = mFreeList;
	if (node != NULL)
	{
		mFreeList=mFreeList->mNext;
	}
	else
	{
		// ok create a new node instead.
#if TARGET==PC
	static int count = 0 ;

	if ((count%20)==0)
	{
		LOG.AddMessage("Warning: SPtrSet creating nodes dynamicaly !! get Stu.") ;
	}
	count++ ;

#endif

		node = new( MEMTYPE_SPTRSET ) SPtrSetNode;
	}

	node->mNext = mFirst;
	node->mItem = ptr;
	mFirst = node;
	mSize++;
	if (mSize==1)
	{
		mLast = mFirst ;
	}
}


//******************************************************************************************
// append to the end of the list
//******************************************************************************************

void	GenericSPtrSet::Append(void* ptr)	// FUNCTION: 0x004e5b20
{
	if (mBlock == NULL)
	{
		LOG.AddMessage("FATAL ERROR: SptSet::Add when freelist has not been setup") ;
		ASSERT(0);
	}
	// has free list got any entries ?
	SPtrSetNode* node = mFreeList;
	if (node != NULL)
	{
		mFreeList=mFreeList->mNext;
	}
	else
	{
		// ok create a new node instead.
#if TARGET==PC
	static int count = 0 ;

	if ((count%20)==0)
	{
		LOG.AddMessage("Warning: SPtrSet creating nodes dynamicaly !! get Stu.") ;
	}
	count++ ;

#endif
		node = new( MEMTYPE_SPTRSET ) SPtrSetNode;
	}

	node->mNext = NULL;
	node->mItem = ptr;

	if (mLast)
	{
		mLast->mNext = node;
	}

	mLast = node ;

	mSize++;
	if (mSize==1)
	{
		mFirst = mLast ;
	}
}


//******************************************************************************************
// removes the first instance of the item found then returns
//******************************************************************************************

void	GenericSPtrSet::Remove(void* ptr)	// FUNCTION: 0x004e5bd0
{
#ifndef SIMPLE_MESHES
	PROFILE_FN(SPtrSetRemove);
#endif

	SPtrSetNode* prev = NULL;
	SPtrSetNode* node = mFirst;
	while (node)
	{
		if (node->mItem == ptr)
		{
			// remove node from linked list
			if (mFirst == node)
			{
				mFirst = node->mNext;
				if (mFirst == NULL)
				{
					mLast = NULL ;
				}
			}
			else
			{
				prev->mNext = node->mNext;
				if (prev->mNext == NULL)
				{
					mLast = prev ;
				}
			}

			// add node to free list
			node->mNext = mFreeList;
			mFreeList = node;
			mSize --;
			return;
		}
		prev = node;
		node = node->mNext;
	}
	// not found?? just return..
}

//******************************************************************************************
BOOL	GenericSPtrSet::Contains(void* ptr)	// FUNCTION: 0x004e5c30
{	
	GenericListIterator iterator(this) ;
	void* item;
	FOR_ALL_ITEMS_IN(iterator, item)
	{
		if (item == ptr) return TRUE ;
	}
	return FALSE ;
}


//******************************************************************************************
#ifdef OUT_OF_LINE_SPTRSET_DESTRUCTOR	// retail: inline in SPtrSet.h (CWorld::~CWorld 0050a89b calls RemoveAll)
GenericSPtrSet::~GenericSPtrSet()
{
	RemoveAll();
}
#endif
//******************************************************************************************
// clears the linked list and add all current nodes to the free list
//******************************************************************************************

void	GenericSPtrSet::RemoveAll()	// FUNCTION: 0x004e5c60
{
	// add all nodes to the free list
	if (mSize==0) return;
	
	if (mLast)
		mLast->mNext = mFreeList;

	mFreeList = mFirst;

	mFirst = NULL;
	mLast = NULL;
	mSize = 0;
}


//******************************************************************************************
void*   GenericSPtrSet::At(int val)	// FUNCTION: 0x004e5c90
{
	SPtrSetNode* current = mFirst ;
	while(current && val > 0)
	{
		current = current->mNext ;
		val--;
	}

	return current->mItem ;
}




// No maths-constant initializers belong to this object (none precede 004e5840); the pair after it (004e5cb0) is
// SquadNormal.obj's.
