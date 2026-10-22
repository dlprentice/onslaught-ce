// shadows.h: shadows (reconstructed from DXShadows.obj, 00551d70..00554e5f; Stuart's release has no copy).
// The nearest units get a render-to-texture shadow each (a pool of 16, 32 in high detail, fading in and out);
// everything else can have a blob shadow.

#ifndef SHADOWS_H
#define SHADOWS_H

#include "deviceobject.h"
#include "activereader.h"
#include "maths.h"
#include "DX.h"

class CUnit;
class CThing;
class CDXTexture;
class CVBufTexture;
class CUMTexture;

// A unit that wants a shadow texture this frame, nearest first (AddShadow).
struct SShadowRequest
{
	CActiveReader<CUnit>	mUnit;				// +0x00
	float					mDist;				// +0x04
};

// A shadow texture's owner (0x20 bytes).
struct SShadowTexInfo
{
	CActiveReader<CUnit>	mUnit;				// +0x00
	signed char				mFade;				// +0x04: 2..32
	UBYTE					mState;				// +0x05: 0 shown, 1 fading out, 2 fading in
	UBYTE					mUnused;			// +0x06: nobody asked for it this frame
	signed char				mAlpha;				// +0x07
	UBYTE					mUnknown08;			// +0x08
	UBYTE					mUnknown09[0x0c - 0x09];
	float					mRadius;			// +0x0c: the shadow's half width on the ground (RenderLandscapeShadows 00553d26)
	FVector					mPos;				// +0x10: where the shadow falls
};

// RTTI CShadows (vtable 005e512c), 0x708 bytes.
class CShadows : public DeviceObject
{
public:
	CShadows();									// 00551f20
	virtual			~CShadows();				// 00551fe0
	virtual HRESULT	RestoreDeviceObjects();		// 2 (00552410)
	virtual HRESULT	InvalidateDeviceObjects();	// 3 (00552470)

	void		Init();							// 005520f0
	void		InitResources();				// 00552330
	void		ShutDown();						// 00552060
	void		MakeView(FVector light);		// 005524a0
	void		RenderTextures();				// 005528b0 (CDXEngine::Render 0053e503)
	void		ResetShads();					// 00552660
	void		AddShadow(CUnit *unit, float dist);	// 005526c0 (the gamut, 00477a36)
	void		UpdateTextures();				// 00552740
	void		AssignTextures();				// 00552800
	void		RenderLandscapeShadows();		// 00553960
	void		TestDraw();						// 00554170
	void		AddBlobShadow(CThing *thing);	// 005545d0 (the gamut, 004773ab)
	void		DrawBlobShadow(FVector pos, float size, SINT alpha);	// 00554750
	void		DrawAllBlobShadows();			// 00554df0
	BOOL		BlobShadowsEnabled() { return mBlobShadows; }
#ifdef GCGAMUT_CPP	// the gamut's view (its inline budget)
	// A shadow for the unit when it is nearer than the furthest request kept (out of line in the gamut, 00477b70).
	void		RequestShadow(CUnit *unit, float dist) { if ((!gMirrorPass) && (mRequests[mNumTextures - 1].mDist > dist)) AddShadow(unit, dist); }
#endif

	float			mBlobShadowFadeDist;		// +0x008 (cg_BlobShadowFadeDist; 009c7558): CRTTree::Init sets it by detail, the career saves it
	SShadowRequest	mRequests[32];				// +0x00c
	SShadowTexInfo	mTexInfo[32];				// +0x10c
	SShadowTexInfo	*mFree[32];					// +0x50c
	SINT			mNumFree;					// +0x58c
	SINT			mUnknown590;				// +0x590
	FVector			mLight;						// +0x594
	float			mShadowDX;					// +0x5a4: the ground offset per unit of height
	float			mShadowDY;					// +0x5a8
	float			mShadowMax;					// +0x5ac
	bool			mShowShadowMap;				// +0x5b0 (cg_ShowShadowMap)
	CDXTexture		*mBlobTexture;				// +0x5b4
	CVBufTexture	*mBlobVB;					// +0x5b8
	SINT			mNumTextures;				// +0x5bc
	SINT			mSizes[32];					// +0x5c0
	CUMTexture		*mTextures[32];				// +0x640
	IDirect3DSurface9	*mDepthSurface;			// +0x6c0
	D3DXMATRIX		mProjection;				// +0x6c4
	bool			mShadows;					// +0x704 (cg_Shadows; 009c7c54): CDXLandscape::BuildLevelSpecifics and the detail menu set it
	bool			mShowShadowExtents;			// +0x705 (cg_ShowShadowExtents)
	bool			mBlobShadows;				// +0x706 (009c7c56; CDXEngine::Render 0053e770; the detail menu sets it)
};
#define CDXShadows CShadows

extern CShadows SHADOWS;						// 009c7550

#endif
