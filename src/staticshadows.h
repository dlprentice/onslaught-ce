// staticshadows.h: static shadows (reconstructed from StaticShadows.obj, 004ebac0..004eeb8f; Stuart's release has
// no copy). Each shadowed thing owns a CStaticShadow: one shadow map per mesh part, each a rectangle of 64x64-bit
// cells on the landscape's 64x64 grid. CStaticShadows ORs the visible maps into its grid for the landscape.

#ifndef STATICSHADOWS_H
#define STATICSHADOWS_H

#include "maths.h"

class CThing;
class CChunkReader;

// A cell's shadow: 64 rows of 64 bits (0x200 bytes; ApplyToGrid 004ee314, Deserialize 004eeafb).
struct SShadowCell
{
	DWORD	mBits[64][2];
};

// A mesh part's shadow map (0x1c bytes; the vector constructor at 004ec394 and 004ee9f6).
class SShadowMap
{
public:
	SShadowMap() { mCells = NULL; }				// 004ee0c0
	~SShadowMap();								// 004ebdf0

	SINT			mX, mY;						// +0x00: the rectangle in grid cells
	SINT			mW, mH;						// +0x08
	SShadowCell		**mCells;					// +0x10: mW * mH cells, NULL where nothing falls
	BOOL			mShadowed;					// +0x14: the part casts a shadow
	BOOL			mVisible;					// +0x18: the part is shown (UpdateVisibility)
};

// A thing's static shadow (0x24 bytes, MEMTYPE_STATICSHADOW).
class CStaticShadow
{
public:
	inline			CStaticShadow();
	inline			~CStaticShadow();

	void			Build();									// 004ec2f0
	void			ApplyToGrid(SINT x, SINT y, SINT w, SINT h);	// 004ee0f0: -1 takes the shadow's own bound
	static void		Deserialize(CChunkReader *c);				// 004ee8f0
	static BOOL		RayHitsTriangle(FVector *a, FVector *b, FVector *c, FVector start, FVector end);	// 004ee410

	CThing			*mThing;					// +0x00
	SINT			mThingNumber;				// +0x04: -1 once attached (Reattach finds the thing by it)
	SShadowMap		*mMaps;						// +0x08: one per mesh part
	SINT			mNumMaps;					// +0x0c
	SINT			mX, mY, mW, mH;				// +0x10: the bound of the maps, in grid cells
	CStaticShadow	*mNext;						// +0x20
};

class CStaticShadows
{
public:
	void	Initialise();						// 004ebbc0
	void	Reattach();							// 004ebc00
	void	Shutdown();							// 004ebd10
	void	Rebuild();							// 004ebe40
	void	DeserializeAll(class CChunkReader *c);	// 004ee8a0 (CResourceAccumulator::ReadResources 004d7836)
	void	UpdateVisibility(class CThing *thing, BOOL a);	// 004ebfb0 (CBuilding::Render 00417567, Shutdown 00417886)

	CStaticShadow	*mFirst;					// +0x00
	SINT			mUnknown04;					// +0x04 (Initialise clears it)
	FVector			mLight;						// +0x08: the light's direction, at most 45 degrees from vertical
	SShadowCell		*mGrid[64][64];				// +0x18
	SShadowCell		*mUnknown4018;				// +0x4018 (Initialise clears it, Shutdown frees it)
};

extern CStaticShadows STATICSHADOWS;			// 009c8010

inline CStaticShadow::CStaticShadow()
{
	mNext = STATICSHADOWS.mFirst;
	STATICSHADOWS.mFirst = this;
	mMaps = NULL;
	mNumMaps = 0;
	mThing = NULL;
}

#endif
