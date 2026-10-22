// heightfield.h: the serialized 0x13dc-byte base of CMap (Load at 0047f750).
#ifndef HEIGHTFIELD_H
#define HEIGHTFIELD_H
#include "maths.h"

// The level's lighting and height-field settings (Stuart's names from engine.cpp; the fields before Sun are open).
struct SHeightFieldProperties
{
	DWORD	Fog;								// +0x00 (006fbe40; CDXEngine::Render 0053e5c5)
	DWORD	Sun;								// +0x04 (006fbe44)
	DWORD	AntiSun;							// +0x08 (006fbe48)
	UBYTE	mUnknown0c[0x14 - 0x0c];
	DWORD	Ambient;							// +0x14 (006fbe54)
	UBYTE	mUnknown18[0x20 - 0x18];
	float	FogDensity;							// +0x20 (006fbe60)
	UBYTE	mUnknown24[0x28 - 0x24];
	bool	VisibleSun;							// +0x28 (006fbe68)
	UBYTE	mUnknown29[0x2c - 0x29];
	float	SunPosX;							// +0x2c (006fbe6c)
	float	SunPosY;							// +0x30
	float	SunPosZ;							// +0x34
};

// The occupied part of the height field (the first 0x20 bytes of CHeightField; CDXBattleLine::UpdateHeightmap reads
// the cell bounds at 0053a3ec..0053a3f8 and the water level at 0053a507).
struct SExtents
{
	SINT	mMinX, mMinY, mMaxX, mMaxY;			// +0x00
	float	mWaterLevel;						// +0x10: 0xcaa24af0 until measured
	UBYTE	mUnknown14[0x20 - 0x14];
};

// No vptr: construction at 0047e870 only clears the owned buffers and a 1024-word table.
// CMap's constructor forwards unchanged this, and its cell-height table begins at +0x13dc.
class CHeightField
{
public:
	CHeightField();
	~CHeightField();
	BOOL	Init();							// the shared return-TRUE (004fdc10; CMap::Init 00490f13)
	void	Shutdown();						// folds into the destructor's body (0047e8a0; CMap::Shutdown 00490f40)
	void Unknown47e8e0(); // dimensions, masks and 65-entry color interpolation table
	SWORD __fastcall GetHeightSample(SINT x, SINT y); // 0047ea20: ECX=this, EDX=x, ret 4
	SINT __fastcall Unknown47eb00(SINT packedX, SINT packedY); // 0047eb00: full integer result; Map explicitly consumes AX
	float __fastcall Collide(const FVector &pos); // 0047eb80: packed interpolation, scaled
	FVector __fastcall Normal(const FVector &pos); // 0047ec60: EDX hidden result, position on stack
	// 0047eff0: the lit texel blitter (ret 0x28: ten arguments; CLandscapeTexture::UpdateTile 0048eb5e, 0048ecee):
	// a tile's texels at a mip shift into a 565 surface of the given pitch in texels (the last five open)
	void	Unknown47eff0(SINT shift, class CMapTex *maptex, void *dst, SINT pitch, SINT tile, SINT e, SINT f, SINT g, SINT w, SINT h);
	// 00541f50 (DXGame.obj): a tile's lit texels at one mip shift for the PS2 texture cache
	// (CDXLandscape::BuildTextureCache 00547975)
	void	GetCacheTexels(SINT shift, class CMapTex *maptex, DWORD *dst, SINT y, SINT x, SINT a, SINT b, SINT w, SINT h, SINT size);
	void	Load(class CChunkReader *c);	// 0047f750 (CMap::Deserialize 004910d6)
	struct SExtents	*GetExtents();			// 0047ef20: measures the occupied cells once and returns the record at +0x00

	UBYTE	mUnknown000[0x20];				// +0x00: GetExtents' cell bounds (MinX, MinY, MaxX, MaxY), the water level
											// it was measured at (+0x10) and the height range (+0x18, +0x1c)
	DWORD	mUnknown20;                     // +0x20, constructor clears it
	UBYTE	*mOwned24;                       // +0x24, destructor and Load free it
	DWORD	mUnknown28[1024];               // +0x28, constructor clears all words
	SWORD	*mHeightData;                    // +0x1028, Load allocates 0xa2000 bytes
	float	mHeightScale;							// +0x102c (006fbdf4)
	UBYTE	mUnknown1030[0x1034 - 0x1030];
	float	mWaterLevel;							// +0x1034 (006fbdfc)
	SINT	mGridShiftX;                    // +0x1038
	SINT	mGridShiftY;                    // +0x103c
	UBYTE	mUnknown1040[0x1078 - 0x1040];
	SHeightFieldProperties	mHFP;					// +0x1078 (CEngine::SetupLights 0044a2d6..0044a402; where it starts is open)
	UBYTE	mUnknown10B0[0x10bc - 0x10b0];
	SINT	mGridWidth, mGridHeight;         // +0x10bc/+0x10c0
	SINT	mGridMaskX, mGridMaskY;          // +0x10c4/+0x10c8
	SINT	mGridRowMask;                   // +0x10cc
	struct ColorEntry { SINT red, green, blue; } mColors[65]; // +0x10d0..0x13db
};

#endif
