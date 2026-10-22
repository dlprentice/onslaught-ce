// map.h: the landscape (only what matched files need so far).

#ifndef MAP_H
#define MAP_H

#include "mapwho.h"
#include "heightfield.h"

// MAP is at 006fadc8. Collide is a __fastcall member: ECX = MAP, EDX = the position (00401a5b..00401a62).
class CMap : public CHeightField
{
public:
	CMap();
	~CMap();
	BOOL	Init();									// 00490f10
	void	InitQuickCollisionMap();				// 00490e30
	void	Shutdown();								// 00490f40
	BOOL	Unknown490a40(class CLine *line, FVector *hit, SINT includeWater);	// 00490a40: sampled terrain trace; exact TRUE additionally tests water
	// The surface normal under a position. Retail's callers call CHeightField::Normal (0047ec60) directly, but through this
	// forwarding inline: returning the inner call's FVector through it gives the caller's vector an early reference to X,
	// so a later Normalise scales X reciprocal-first (DeclareOnGround 0040c750, Slide 00413b90).
	FVector	Normal(const FVector &pos) { return CHeightField::Normal(pos); }
	float	Collide(const FVector &pos) { return CHeightField::Collide(pos); }	// forwarding too: its inline cost puts HandleGroundEffect's FVector ctor out of line (00411663)
	float	GetWaterLevel() { float w = mWaterLevel; return w; }	// the copy through a local makes VC6 copy the level through the FPU where retail does (eight sites, e.g. 00403adf, 004116b2) and keeps GroundParticleEffect's mov/mov (0040ef20)
	float	GetHeightScale() { return mHeightScale; }
	float	GetHeight(SINT x, SINT y) { return GetHeightScale() * GetHeightSample(x, y); }
	// The ground or water surface under a position, whichever is higher (CUnit 004fb46b..004fb4bc).
	float	GetHeightAt(const FVector &pos)
	{
		float	h = GetHeight(SINT(pos.X), SINT(pos.Y));
		if (mWaterLevel < h)
			h = mWaterLevel;
		return h;
	}

	// The line trace indexes 64x64 eight-byte records at 00490c18/00490c33.
	struct CellHeights { float mMaxZ, mMinZ; } mCellHeights[64 * 64];	// +0x13dc, a proven layout prefix

	const SHeightFieldProperties	&GetHFProperties() { return mHFP; }
	DWORD	GetSun() { return mHFP.Sun; }
	DWORD	GetAntiSun() { return mHFP.AntiSun; }
	DWORD	GetAmbient() { return mHFP.Ambient; }
	DWORD	GetFog() { return mHFP.Fog; }
	float	GetFogDensity() { return mHFP.FogDensity; }
#ifdef DXWATER_CPP
	DWORD	GetSunReflectColour() { return *(DWORD *)&mHFP.mUnknown0c[0]; }	// +0x0c: the sun's glint on the water (0055c3ed)
	DWORD	GetWaterColour() { return *(DWORD *)&mHFP.mUnknown24[0]; }		// +0x24: the texture factor (0055bbfd)
#endif
	void	Deserialize(class CChunkReader *c);		// 00491060 (CEngine::Deserialize 0044a72f)
	// 00490f50 (CWorld::LoadWorld 0050bc29): TRUE when the parts asked for are already loaded (or the map is -1)
	BOOL	AlreadyLoaded(SINT number, BOOL geometry, BOOL properties);

	SINT	mMapNumber;								// +0x93dc
	BOOL	mGeometryLoaded;						// +0x93e0
	BOOL	mPropertiesLoaded;						// +0x93e4
};

extern CMap MAP;

#endif
