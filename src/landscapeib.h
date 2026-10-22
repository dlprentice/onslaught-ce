// landscapeib.h: CLandscapeIB (vtable 005dc1d8; landscapeib.obj 0048df20..0048e3c7), the index buffer of one landscape
// patch level: a (2 << level)-square grid of triangles with the edges its flags name stitched to the coarser level
// beside it, stripped by NvTriStrip. The patch manager (CDXPatchManager::Init 00550650) sets each up.

#ifndef LANDSCAPEIB_H
#define LANDSCAPEIB_H

#include "ibuffer.h"

class CLandscapeIB : public CIBuffer
{
public:
					CLandscapeIB();						// 0048e330
	void			Init(SINT level, SINT edges);		// 0048e360
	void			CreateIndexBuffer();				// 0048df20
	virtual HRESULT	InitDeviceObjects();				// 0048e3c0

	SINT			mLevel;								// +0x24
	SINT			mEdges;								// +0x28: 1, 2, 4, 8 the edges stitched to a coarser neighbour
	SINT			mSize;								// +0x2c: cells along a side, 2 << mLevel
	SINT			mCount;								// +0x30: triangles
};

#endif
