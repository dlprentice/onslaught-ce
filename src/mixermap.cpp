// mixermap.cpp: the landscape's mixer map (reconstructed; the file name is the object's __FILE__, 00640030).
#include	"Common.h"
#define MIXERMAP_CPP
#include "mathsconst.h"

#include	"mixermap.h"
#include	"chunker.h"

// FUNCTION: 0x00523180 init:MIXERMAP
CMixerMap	MIXERMAP;

//*********************************************************************************
// Retail calls this out of line from CMixerMap::Init (00523381) although /Ob2 would inline it as written here: its
// source must have been too large to inline, so the pragma stands in for whatever made it so.
#pragma auto_inline(off)
void	SMixerSlot::Init(CChunkReader *reader)	// FUNCTION: 0x00523190
{
	reader->GetNext();
	reader->GetNext();
	reader->Read(this, sizeof(SMixerSlot), 1);

	if (mData)
	{
		reader->GetNext();
#line 134
		mData = new (MEMTYPE_MAP) UBYTE[mCount * 81];
		reader->Read(mData, 1, mCount * 81);
	}
}

// The slots' constructor: retail emits it right after Init, so it was defined here out of line (the array's
// constructor iterator takes its address).
SMixerSlot::SMixerSlot()	// FUNCTION: 0x00523200
{
	mData = NULL;
}

// WITNESS-PLACED (link concession, no code in the image): retail emits the inline destructor (00523210) before Destroy,
// so a function between them needed it out of line; /OPT:REF dropped it. Its original is unknown.
void	MixerSlotsDelete(SMixerSlot *slots)
{
	delete [] slots;
}
#pragma auto_inline(on)
// the slots' destructor, out of line for delete [] (005232d7)
// FUNCTION: 0x00523210 ??1SMixerSlot@@QAE@XZ

//*********************************************************************************
void	CMixerMap::Destroy()	// FUNCTION: 0x00523230
{
	if (mSlots)
	{
		for (SINT i = 0; i < MIXER_MAP_SLOTS; i++)
			mSlots[i].Free();
	}

	if (mSlots)
	{
		delete [] mSlots;
		mSlots = NULL;
	}

	if (mMap)
	{
		delete [] mMap;
		mMap = NULL;
	}
}

//*********************************************************************************
void	CMixerMap::Init(CChunkReader *reader)	// FUNCTION: 0x005232b0
{
	if (mSlots)
	{
		delete [] mSlots;
		mSlots = NULL;
	}

#line 246
	mSlots = new (MEMTYPE_MIXERMAP) SMixerSlot[MIXER_MAP_SLOTS];
	mMap = new (MEMTYPE_MIXERMAP) UBYTE[MIXER_MAP_SIZE];

	reader->GetNext();
	for (SINT i = 0; i < MIXER_MAP_SLOTS; i++)
		mSlots[i].Init(reader);

	reader->GetNext();
	reader->Read(mMap, 1, MIXER_MAP_SIZE);
}

// FUNCTION: 0x005233c0 init:ZERO_FVECTOR
