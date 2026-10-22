// FastVB.h: CFastVB, a dynamic buffer of transformed, lit, textured vertices drawn as quads or strips (FastVB.cpp;
// the one instance FASTVB at 00897a98, a name of ours). Not in Stuart's release: reconstructed from retail.

#ifndef FASTVB_H
#define FASTVB_H

class CVBuffer;
class CIBuffer;

// A transformed vertex (0x1c bytes; FVF 0x144: screen position and rhw, diffuse, one texture coordinate).
#define FASTVB_FVF		(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define FASTVB_STRIDE	0x1c

class CFastVB
{
public:
	CFastVB();														// inline, defined in FastVB.cpp after FASTVB (see there)
	~CFastVB() { Destroy(); }

	HRESULT		Create();												// 0051a270
	void		Destroy();												// 0051a340
	WORD		LockAligned(void **ptr, int count);						// 0051a380: starts a batch of quads
	WORD		Lock(void **ptr, int count);							// 0051a430: adds to the batch
	void		Render();												// 0051a510: draws the batch as quads
	void		RenderStrip();											// 0051a6a0: draws the batch as a strip

	static CIBuffer	*mIB;												// 00897a90: the quads' shared index buffer

	CVBuffer	*mVB;													// +0x00
	WORD		mPos;													// +0x04: next free vertex
	WORD		mStart;													// +0x06: the batch's first vertex, 0xffff when none
	int			mCount;													// +0x08: vertices in the batch
	int			mCapacity;												// +0x0c
};

extern CFastVB FASTVB;

#endif
