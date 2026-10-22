// DXEngine.h: interface for the CDXEngine class.
//
//////////////////////////////////////////////////////////////////////
// Stuart's header for the retail PC layout (ENGINE is at 0089c9a0; vtable 005e4fc4). The PC build has no
// mCaptureScreen and no mTexOpaque: CDXEngine::Init stores the capture area at +0x4c4/+0x4c8 (0053d61d), the
// default texture is at +0x4e4 (0053d6fb), the outline texture +0x4e8, the arrow +0x4ec, the default shader
// +0x4f0 (0053d601), the gamma ramp +0x4f4 (0053d64c), the reflection switches +0xaf4/+0xaf5, mFirstFrame +0xafc
// and the sun's particle descriptor +0xc80 (0053d74a).

#ifndef DXENGINE_H
#define DXENGINE_H

#include "Engine.h"
#include "Landscape.h"
#include "Lights.h"
#include "ResourceAccumulator.h"

class	CParticleDescriptor;

class CDXEngine : public CEngine
{
	friend class		CEditorRenderer;

	class				CTEXTURE			*mScreenTexture;

	int					mCaptureAreaTop;
	int					mCaptureAreaBottom;
	int					mBlurAlpha;
	int					mNewBlurAlpha; // Needed as the effect of changing this must start *next* frame!
	D3DLOCKED_RECT		mTopLevelLockedRect;
	int					mMaxValidX;
	int					mMaxValidY;

public:
	void				ReleaseDefaults();			// 0053d3a0 (retail PC; CSystem::Shutdown 004f01b0 calls it; name open)
	void				ShutdownRestartLoop();		// 0040c640: empty, folded by the linker with DebugTrace (CGame::ShutdownRestartLoop 0046cbc5)

						CDXEngine();
						~CDXEngine();
	virtual BOOL		Init();						// 0053d5f0
	virtual void		InitResources();			// 0053d6d0
	virtual void		ShutDown();					// 0053d3e0

	class CTEXTURE		*GetDefaultTexture()
	{
		if (!mDefaultTexture)
		{
			// This sometimes gets called before the engine is initresources'ed but after the textures are available...
			mDefaultTexture = CTEXTURE::GetTextureByName("meshtex\\default.tga");
		}

		return mDefaultTexture;
	}; //###

	class CTEXTURE		*GetScreenTexture() { return mScreenTexture; };
	void				SetBlurAlpha(int a,bool thisframe);	// 0053f010

	void				DrawDebugCuboid(DWORD col, FVector &axes, FVector &pos, FMatrix ori, CTEXTURE* for_texture) ;
	void				DrawDebugLine(DWORD col, FVector &start, FVector &end);
	void				RenderArrow(FVector v1, FVector v2, DWORD col);	// 0053df40

	CTEXTURE*			GetOutlineTexture() { return mTexOutline; }

	void				SetGammaBias(float value);	// 0053d4c0

	// Stuff for other Rendering things to use
	HRESULT				PreRender(CViewport *viewport);	// 0053e220
	HRESULT				Render(int viewpoint);		// 0053e2e0
	HRESULT				PostRender(CViewport *viewport);	// 0053ecc0

	void				SetModulate2X( const bool aSet );	// 0053f040 (CWorld::Render 0050ab67)
	void				ReallySetDefaultMaterial();

	CTEXTURE*			mDefaultTexture;
	CTEXTURE*			mTexOutline;
	CTEXTURE*			mTexEdArrow;				// +0x4ec (retail PC keeps it and drops mTexOpaque, 0053d6f6)
	class CVertexShader	*mDefaultShader;

	D3DGAMMARAMP		mDefaultGammaRamp;

	bool				mRenderReflections;
	bool				mTextureReflections;
	int					mRipplePhase;
	BOOL				mFirstFrame;

	// JCL - Backup matrices for HUD
	D3DMATRIX			mWorldMatrices[VIEWPOINTS];
	D3DMATRIX			mViewMatrices[VIEWPOINTS];
	D3DMATRIX			mProjectionMatrices[VIEWPOINTS];

	CParticleDescriptor	*mSunPD;
};

#endif // DXENGINE_H
