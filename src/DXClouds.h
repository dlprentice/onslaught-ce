// DXClouds.h: CClouds, the clouds atmospherics effect (DXClouds.obj, 0053b810..0053ba5c; vtable 005e4f9c; 0x334
// bytes: Atmospherics.cpp allocates it, 00404aa6). Not in Stuart's release: reconstructed from the retail object.
// Process and Render are empty in this build.

#ifndef DXCLOUDS_H
#define DXCLOUDS_H

class CVBufTexture;
class CDXTexture;

#define NUM_CLOUDS	50

class CClouds : public CAtmospherics
{
public:
					CClouds();						// 0053b900
	virtual char	*GetName() { return "Clouds"; }	// 0053b9e0
	virtual void	Process() {}
	virtual void	Render() {}
	virtual void	Init();							// 0053b9f0
	virtual void	Shutdown();						// 0053ba20

	CVBufTexture	*mVBuf;							// +0x08
	CDXTexture		*mTexture;						// +0x0c
	float			mWidth;							// +0x10: cg_cloudwidth
	FVector			mClouds[NUM_CLOUDS];			// +0x14: Init parks each at (-1, -1, -1)
};

#endif
