// mixermap.h: the landscape's mixer map (reconstructed; mixermap.cpp's __FILE__ is 00640030, Stuart's file is not in
// his release). MIXERMAP (0089bd80) is read from the map chunk by the height field's loader (004910e1): 0x1000 slots,
// each with its own data, and a 0x40000-byte map.

#ifndef MIXERMAP_H
#define MIXERMAP_H

class CChunkReader;

// a slot (0x14 bytes), read whole from the chunk; its data follows when it has any
struct SMixerSlot
{
	SMixerSlot();								// 00523200
	~SMixerSlot() { Free(); }					// 00523210: inline (retail does not fold it with CText::Shutdown's identical body)
	void	Free() { if (mData) { delete [] mData; mData = NULL; } }
	void	Init(CChunkReader *reader);			// 00523190

	SINT	mCount;								// +0x00: entries of 81 bytes
	UBYTE	*mData;								// +0x04: non-NULL in the file when the slot has data
	UWORD	mTextures[(0x14 - 0x08) / 2];		// +0x08: each entry's mixer texture (CHeightField 0047f283)
};

#define MIXER_MAP_SLOTS		0x1000
#define MIXER_MAP_SIZE		0x40000

class CMixerMap
{
public:
	CMixerMap() { mSlots = NULL; }				// MIXERMAP's initializer (00523180)

	void	Destroy();							// 00523230
	void	Init(CChunkReader *reader);			// 005232b0

	SMixerSlot	*mSlots;						// +0x00
	UBYTE		*mMap;							// +0x04
};

extern CMixerMap	MIXERMAP;					// 0089bd80

#endif
