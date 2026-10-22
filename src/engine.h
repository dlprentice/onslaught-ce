// engine.h: the renderer (only what matched files need). ENGINE is a CDXEngine at 0089c9a0 (CEngine's
// constructor 00449820 is called on it); vtables CEngine 005db270 (3 slots), CDXEngine 005e4fc4 (4 slots).
// Offsets from CThing::Render (004f36d0) and DrawDebugCuboid (004f37c0): the debug-draw flags +0x4b4 (0089ce54),
// the outline texture +0x4e8 (0089ce88). DrawDebugCuboid is 0053d760 (it pops 0x40 bytes of arguments).

#ifndef ENGINE_H
#define ENGINE_H

#include "mesh.h"
#include "VBufTexture.h"
#include "viewport.h"

class CPlayer;

extern bool			gMirrorPass;				// 0089d680: a mirrored pass (ltshell.cpp; CGame::Render sets it)
extern bool			FORCE_OBJECT_IMPOSTERS;		// 0083cd58 (rtmesh.cpp)

#define USE_BIGLIST_FOR			3

#define DRAW_OUTER_RADIUS 32

class CDXTexture;
#define CTEXTURE CDXTexture

class CCamera;
class CResourceAccumulator;
class FMatrix;

// Stuart's engine.h flag (CFrontEnd::Render tests it, 00468439)
#define DRAW_MEM_MANAGER	(1 << 6)

// The landscape (only what matched files need so far).
// Redirects rendering into a texture and back (DXLandscape.obj; the constructor, zeroing both surfaces, is folded
// with an identical one at 00491900). Names open.
class CRenderTargetSwap
{
public:
#ifdef DXLANDSCAPE_CPP	// inline here (CDXLandscape::RenderShadowMap 00546636); 00491900 is a folded twin
						CRenderTargetSwap() { mRenderTarget = NULL; mDepthStencil = NULL; }
#else
						CRenderTargetSwap();		// 00491900
#endif
						~CRenderTargetSwap();		// 00546460: releases both surfaces
	bool				Begin(struct IDirect3DTexture9 *target);	// 00546220: saves the render target and depth surface, sets the texture's
	void				End();						// 005463f0: restores them

	struct IDirect3DSurface9	*mRenderTarget;		// +0x00
	struct IDirect3DSurface9	*mDepthStencil;		// +0x04
};

// Stuart's engine.h (its field order is the retail layout: mLandscape +0x10, mNearZ +0x430, mViewport +0x438,
// mNavDisplay +0x4a4, mCurrentViewpoint +0x4ac, mDrawDebugStuff +0x4b4; the constructor 00449820). The retail
// vtable has his three virtuals.
#define DEFAULT_CAMERA_OFFSET	0.0f
#define	DEFAULT_Z_FAR			256.0f
#define DEFAULT_Z_NEAR			0.1f
#define VIEWPOINTS				2
#define TREE_SHADOW_SIZE		6
#define TREE_SHADOW_XOFF		(-0.4f)
#define TREE_SHADOW_YOFF		(0.1f)
#define N_MAPMIPS		7
#define	MAX_GLOBAL_MESHES 256

#define DRAW_MAP_WHO 1
#define DRAW_PROFILE 2
#define DRAW_OBJECTS_AS_CUBOIDS 4
#define DRAW_COCKPIT 8
#define DRAW_SKELETAL_ACCURATELY 16

#include "landscape.h"
#include "kempycube.h"
#include "water.h"
#include "Lights.h"

class CEngine
{
protected:
				CEngine();
	class		CCamera				*mCamera[VIEWPOINTS];
	int			mViewpoints;
	CLANDSCAPE			*mLandscape;
	class		CWATER*				mWater;
	class		CLightManager*		mLights;
	class		CSKY*				mSky;
	class		CTEXTURE*			mHitEffectTexture;
	class		CTEXTURE*			mCloakTexture;

	virtual	void	Shutdown();
	virtual BOOL	Init();
	virtual void	InitResources();

	void ShutdownRestartLoop() {}


	CMESH		*mDefaultMesh;
	CMESH		*mGlobalMeshes[MAX_GLOBAL_MESHES];
	SINT		mNumGlobalMeshes;

	float		mNearZ;
	float		mFarZ;

	CViewport			mViewport[VIEWPOINTS];
	class CPlayer		*mPlayer[VIEWPOINTS];

	class C3DGamut		*mGamut;

	CViewport			mCurrentViewport;

	int					mHitEffectFactorR;
	int					mHitEffectFactorG;
	int					mHitEffectFactorB;

	class CKEMPYCUBE	*mKempyCube;

	class	CMapTex		*mMapTexs;

public:
	class		CTEXTURE			*mHilightTexture;

	void		LoadAllNamedMeshes( CMEMBUFFER &dataFile );
	void		ResetNamedMeshes();

	SINT		AddNewGlobalNamedMesh(char *name);

	CMESH		*GetDefaultMesh()		{mDefaultMesh->AddRef(); return mDefaultMesh;};
	CMESH		*GetGlobalMesh(SINT num){mGlobalMeshes[num]->AddRef(); return mGlobalMeshes[num];};

	CCamera		*GetCamera()			{ /*ASSERT(mCamera[mCurrentViewpoint]);*/ return mCamera[mCurrentViewpoint];	}
	void		SetCamera(CCamera *c)	{ mCamera[mCurrentViewpoint] = c;}  // JCL - sorry!

	void				ToggleDebugDraw(ULONG num) { if (mDrawDebugStuff & num) mDrawDebugStuff &=(~num) ; else
										mDrawDebugStuff |= num ;}

	class CTEXTURE	*GetHitEffectTexture() { return(mHitEffectTexture); };
	class CTEXTURE	*GetCloakTexture() { return(mCloakTexture); };

	void		ToggleParticles() {mDebugNoParticles = !mDebugNoParticles;};
	BOOL		DebugNoParticles() {return mDebugNoParticles;};

	ULONG		GetDrawDebugStuff() { return mDrawDebugStuff; }

	void		GetViewMatrixFromCamera(CCamera *cam, FMatrix *matview);

	float		GetNearZ() { return mNearZ; }
	float		GetFarZ() { return mFarZ; }
	void		SetNearZ( const float aNearZ ) { mNearZ = aNearZ; }
	void		SetFarZ( const float aFarZ ) { mFarZ = aFarZ; }

	void				SetCurrentViewport(CViewport *vp) { mCurrentViewport=*vp; };
	CViewport			GetCurrentViewport()		{ return mCurrentViewport; };
	int					GetCurrentViewportWidth()	{ return mCurrentViewport.Width; };
	int					GetCurrentViewportHeight()	{ return mCurrentViewport.Height; };
	int					GetCurrentViewportX()		{ return mCurrentViewport.X; };
	int					GetCurrentViewportY()		{ return mCurrentViewport.Y; };

	CCamera				*GetCameraForViewpoint(int n) { return mCamera[n]; }
	CPlayer				*GetPlayerForViewpoint(int n) { return mPlayer[n]; }
	CViewport			*GetViewportForViewpoint(int n) { return &mViewport[n]; }
	int					GetNumViewpoints() { return mViewpoints; }
	void				SelectViewpoint(int n);

	void				SetViewpoint(int viewpoint,CCamera *camera,CViewport *viewport,CPlayer *player);
	void				SetNumViewpoints(int n);	// 00528b50 (folded with a CTweak setter)

	float				GetRenderTime();

	int					GetHitEffectFactorR() { return mHitEffectFactorR; };
	int					GetHitEffectFactorG() { return mHitEffectFactorG; };
	int					GetHitEffectFactorB() { return mHitEffectFactorB; };

	CLANDSCAPE			*GetLandscape() { return mLandscape; };

	void				UpdatePos(CCamera* cam); // ##GC changed from pos to camera 19/9/01
	void				ResetPos(int x, int y);
	void				BuildLevelSpecifics();
	void				InitDamageSystem();

	void				LoadMixers(int set);

	class	CMapTex*	GetMapTex() { return mMapTexs; }

	void				SetKempyCube(UBYTE number);

	void				SetWater(UBYTE number);
#ifdef DXLANDSCAPE_CPP
	class CWATER		*GetWater() { return mWater; }	// CDXLandscape::RenderShadowMap (0054678f)
#endif
#ifdef MAP_CPP	// defined in Map.cpp, its caller
	// CMap::Deserialize passes the map's byte at +0x1091 (00491103); the body is empty and folds into a shared
	// one-argument no-op (00452b60). Open question: what it set on other platforms.
	void				Unknown452b60(UBYTE number);
#endif
#ifdef RTTREE_CPP	// defined in RTTree.cpp, its caller
	// set around a tree's lit draw (CRTTree); empty on the PC, folded into the same no-op (00452b60). Name ours.
	void				SetTreeLighting(BOOL on);
#endif

	void				SetupLights();

	void				LogLight(const FVector &inPos,eLightType lt,float mRadius);
	void				AddDamage(float x, float y, int size) { GetLandscape()->AddDamage(x, y, size);	}
	void				RemoveDamage(float x, float y, int size) { GetLandscape()->AddDamage(x, y, -size);	} // note: size flipped on remove
	void				LockCurrentDamage() { GetLandscape()->LockCurrentDamage();	}
	void				SwitchAllLights(BOOL onoff);

	void				UpdateArea(
							SINT	inStartX,
							SINT	inStartY,
							SINT	inFinishX,
							SINT	inFinishY, BOOL updategeometryinstead=FALSE);

	void				SetTreeAlphaMode( const bool aSet );
	void				EnableAdditiveAlpha();
	void				EnableAlpha();
	void				DisableAlpha();
	void				DisableLighting();
	void				EnableLighting();

	void				ToggleHudAlphaMode() { mHudAditive = !mHudAditive; }
	BOOL				HudAditive()		{ return mHudAditive; }

	class CLightManager	*GetLights() { return(mLights); };

	void				SetSky(UBYTE number);

	class CTEXTURE*		GetHilightTexture() {return mHilightTexture;};
	void				AccumulateResources(CResourceAccumulator *accumulator);

	void				Serialize(class CChunker *c,CResourceAccumulator *ra);
	void				Deserialize(class CChunkReader *c);

//#ifdef EDITORBUILD2
	BOOL				mNavDisplay;
	void				LandscapeNavDisplay(BOOL onoff) {mNavDisplay=onoff;};
	BOOL				GetNavDisplayMODE() { return mNavDisplay;};
	FVector				GetCursorOffsetMatrix(float x,float y);
//#endif

	bool		mRenderLandscape;
	int			mCurrentViewpoint;

	BOOL		mHudAditive;

	ULONG		mDrawDebugStuff;

	BOOL		mDebugNoParticles;
	BOOL		mDrawPolyBuckets;
};

#include "DXEngine.h"

extern class CDXEngine ENGINE;

#endif
