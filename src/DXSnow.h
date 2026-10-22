// DXSnow.h: CSnow, the snow atmospherics effect (DXSnow.obj; Atmospherics.cpp makes one, 00404a98).

#ifndef DXSNOW_H
#define DXSNOW_H

class CVBufTexture;
class CDXTexture;
class CVertexShader;

// The snow (vtable 005e5974).
class CSnow : public CAtmospherics
{
public:
					CSnow();
	virtual char	*GetName() { return "Snow"; }
	virtual void	Process();
	virtual void	Render();						// 00555600
	virtual void	Init();							// 00555020
	virtual void	Shutdown();						// 00555410
	void			RenderFlakes();					// 00555460

	CVBufTexture	*mVBuf;							// +0x08
	CDXTexture		*mTexture;						// +0x0c
	CVertexShader	*mShader;						// +0x10
	bool			mEnabled;						// +0x14: DISABLE_SNOW was clear (cg_snow)
	D3DXMATRIX		mMatrix;						// +0x18: the last frame's view
	FVector			mFallVector;						// +0x58: the fall per period (the wind, 15 down)
	D3DXVECTOR4		mOffsets[50];					// +0x68: the random positions (C20..C69)
	float			mPeriod;					// +0x388: cg_snow_period
	float			mFadeDistance;					// +0x38c: cg_snow_fadedistance
	FVector			mLastCameraPosition;					// +0x390: the camera's last position
	float			mScale;					// +0x3a0: cg_snow_scale
};

#endif
