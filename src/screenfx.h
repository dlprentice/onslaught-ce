// screenfx.h: full-screen effects (only what matched files need so far).

#ifndef SCREENFX_H
#define SCREENFX_H

class CScreenFX
{
public:
	void		Init();							// 00551c90
	void		InitResources();				// 00551cc0
	void		Shutdown();						// 00551d40
	void		Render();						// empty on the PC: folded into 0040c640 (CDXEngine::Render 0053ec8b)

	class CVBufTexture	*mZoomLines;			// +0x00
	class CVBufTexture	*mReticle;				// +0x04
	int			mUnknown08[2];					// +0x08
	bool		mZoomEffectEnabled;				// +0x10: cg_zoomeffectenabled
};

extern CScreenFX SCREENFX;						// 009c74f8

#endif
