// LandscapeVB.h: CLandscapeVB, a patch of landscape as a vertex buffer of height samples (LandscapeVB.obj,
// 0048f1e0..0048f35e; vtable 005e5114; Stuart's release has no copy). CDXPatchManager::Init makes them
// (005504db..005505df) and CDXLandscape::UpdateLOD moves them (00546fe6).

#ifndef LANDSCAPEVB_H
#define LANDSCAPEVB_H

#include "vbuffer.h"

class CLandscapeVB : public CVBuffer
{
public:
	virtual HRESULT	InitDeviceObjects();					// 0048f320: rebuilds a built patch
	void		Create(SINT size);							// 0048f1e0: size by size cells
	void		Rebuild();									// 0048f210
	void		SetOrigin(SINT x, SINT y, SINT step, float complexity);	// 0048f2f0 (CDXLandscape::CalcLODs passes the tile's complexity, 00546fc0)

	SINT		mX;											// +0x2c
	SINT		mY;											// +0x30
	SINT		mStep;										// +0x34
	SINT		mNumVerts;									// +0x38
	UWORD		mSlot;										// +0x3c: 0xffff until placed
	BOOL		mBuilt;										// +0x40
	SINT		mCells;										// +0x44
	SINT		mUnknown48;									// +0x48
	float		mComplexity;								// +0x4c: the tile's (SetOrigin's fourth argument)
};

#endif
