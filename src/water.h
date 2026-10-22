// water.h: the water (DXWater.obj, 0055af90..0055d5dd).

#ifndef WATER_H
#define WATER_H

#include "deviceobject.h"

class CDXTexture;
class CVBufTexture;
class CVertexShader;
struct IDirect3DPixelShader9;

// The platform water (RTTI CWater; no virtuals of its own).
class CWater : public DeviceObject
{
};

// 0x3abc bytes (CEngine::Init 00449b5e); deleted through its virtual destructor (CEngine::Shutdown 00449931).
class CDXWater : public CWater
{
public:
				CDXWater();						// 0055b0e0
	virtual		~CDXWater();					// 0055b160
	void		Load(UBYTE number);				// 0055b230: the mixer's reflection and caustic textures, and the sun's
	void		Change(UBYTE number);			// 0055b330
	virtual HRESULT	InitDeviceObjects();		// 0055b3a0: the pixel and vertex shaders when the card has ps 2.0
	virtual HRESULT	DeleteDeviceObjects();		// 0055b400
	void		BuildGrid(float level, DWORD colour);	// 0055b440: the 25x25 grid round the camera, rebuilt on a new colour
	void		RenderShadow();					// 0055b660: the grid at the water level with the landscape's shadow shaders
	void		Render(float wlevel);			// 0055b6c0 (CDXEngine::Render 0053e6f1)

	CDXTexture		*mReflection;				// +0x08
	CDXTexture		*mCaustic;					// +0x0c
	CDXTexture		*mSunBlob;					// +0x10
	CDXTexture		*mSunReflect;				// +0x14
	CVBufTexture	*mVB;						// +0x18
	UBYTE			mUnknown1C[0x3ab4 - 0x1c];
	int				mUnknown3ab4;				// +0x3ab4
	DWORD			mGridColour;				// +0x3ab8: the colour the grid was built with; InitDeviceObjects bumps it

	static IDirect3DPixelShader9	*sPixelShader;	// 009cc21c
	static CVertexShader			*sShader;		// 009cc218
};
#define CWATER CDXWater

#endif
