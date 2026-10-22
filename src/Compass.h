// Compass.h: the HUD's compass (Compass.cpp, DXCompass.cpp). Not in Stuart's release: reconstructed from retail.
// The HUD makes a CDXCompass (0x3f14 bytes, CHud::Init 00481467..004814b0); its constructor (0053bd60) calls
// CCompass's out of line (00427200), so the base's members end before +0x3f04. No virtual functions.

#ifndef COMPASS_H
#define COMPASS_H

class CDXTexture;
class CByteSprite;
class CBattleEngine;
class CVBuffer;

// A marker on the compass ring: 30 per player view (two in split screen).
struct SCompassMarker
{
	float			mTime;											// +0x00: counts down; 0 is free
	int				mId;											// +0x04
	float			mYaw;											// +0x08
};

#define COMPASS_MARKERS		30

class CCompass
{
public:
	CCompass();														// 00427200
	void			Init();											// 004270e0
	void			PlatformInit();									// 0053be40 (DXCompass.cpp)
	void			LoadTextures();									// 00427110
	void			ReleaseTextures();								// 00427190
	void			Render(CBattleEngine *be);						// 00427210

	UBYTE			mUnknown0000[0x3c00];
	CDXTexture		*mRingTexture[2];								// +0x3c00 (DXCompass.cpp's; PlatformInit 0053bfc5)
	CByteSprite		*mByteSprite;									// +0x3c08 (set by PlatformInit, 0053bea8)
	UBYTE			mUnknown3c0c[0x3c10 - 0x3c0c];
	int				mUnknown3c10;									// +0x3c10
	int				mLastHealthEnd[2];								// +0x3c14: each view's health bar edge last painted (0053c7c8)
	int				mLastEnergyEnd[2];								// +0x3c1c: and its energy bar's end (0053c7bd)
	SCompassMarker	mMarkers[COMPASS_MARKERS][2];					// +0x3c24
	CDXTexture		*mThreatFlash;									// +0x3ef4
	CDXTexture		*mDamageFlash;									// +0x3ef8
	CDXTexture		*mBarLine;										// +0x3efc
	CDXTexture		*mObjectiveMarker;								// +0x3f00
};

class CDXCompass : public CCompass
{
public:
	CDXCompass();													// 0053bd60 (DXCompass.cpp)
	void			ReleaseResources();								// 0053bda0
	void			DrawThreats(CBattleEngine *be);					// 0053c2e0: the ring's threat marks, into mRingTexture
	void			BuildRing(void *verts, int width, int height, int segments, int radius, float v);	// 0053c1d0
#ifdef DXCOMPASS_CPP	// DXCompass.cpp's view; the other includers keep their declaration counts
	void			Unknown53c510(CBattleEngine *be);				// 0053c510: the ring's second texture
	void			Render(CBattleEngine *be);						// 0053cd30: both rings, then CCompass's markers
#endif

	CDXTexture		*mRingTexture2[2];								// +0x3f04
	CVBuffer		*mOuterRing;									// +0x3f0c
	CVBuffer		*mInnerRing;									// +0x3f10
};

#endif
