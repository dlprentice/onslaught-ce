// gcgamut.h: the 3D gamut (only what matched files need so far).

#ifndef GCGAMUT_H
#define GCGAMUT_H

// 0x38 bytes (CEngine::Init 00449aa3).
class C3DGamut
{
public:
				C3DGamut(int size);				// 004741b0
				~C3DGamut();					// 00474260
	void		MakeIt(FMatrix *view, float far_distance, float zoomx, float zoomy, FVector *pos, bool a);	// 00476a20

#ifdef GCGAMUT_CPP	// gcgamut.cpp's view; the other includers keep the byte array
	void		ComputePlanes(FVector *points);	// 004742a0: each cell's height span under the view's four side planes
	void		RenderThings(bool imposters, bool shadows, bool blob_shadows);	// 00476fe0 (CWorld::Render 0050ab60)

	UBYTE		mUnknown00[0x08];
	SINT		mGridSize;						// +0x08: 0x40, the grid's width and height in cells
	char		*mSpans;					// +0x0c: 0x2000 bytes, a pair per cell: the top and bottom of its span
	UBYTE		*mVisible;					// +0x10: 0x1000 bytes, a flag per cell: some of it can be seen
	SINT		mCellSize;						// +0x14: 8, a cell's size in map units
	SINT		mMinX;						// +0x18: the first cell in X the view covers
	SINT		mMinY;						// +0x1c: the first in Y
	SINT		mMaxX;						// +0x20: the last in X (MakeIt 00476f04..00476f28)
	SINT		mMaxY;						// +0x24: the last in Y
	FVector		mCameraPos;						// +0x28: the camera's position (MakeIt 00476a46..00476a5e)
#elif defined(DXLANDSCAPE_CPP)	// CDXLandscape::CalcLODs reads the cells' spans and the view's bounds (00546dee..)
	UBYTE		mUnknown00[0x08];
	SINT		mGridSize;						// +0x08
	char		*mSpans;					// +0x0c
	UBYTE		*mVisible;					// +0x10
	SINT		mCellSize;						// +0x14
	SINT		mMinX;						// +0x18
	SINT		mMinY;						// +0x1c
	SINT		mMaxX;						// +0x20
	SINT		mMaxY;						// +0x24
	UBYTE		mUnknown28[0x38 - 0x28];
#else
	UBYTE		mUnknown00[0x38];
#endif
};

#endif
