// landscape.h: the PC landscape, its damage marks and patch manager (only what matched files need so far).

#ifndef LANDSCAPE_H
#define LANDSCAPE_H
#include "maths.h"

#include "deviceobject.h"

#ifdef DAMAGE_FULL
// A mark (8 bytes): links to the previous and next marks of its cell (the previous of a cell's first mark is the cell
// with the top bit set), its position in the cell and its size (Add 00440eb0).
struct CDamageMark
{
	UWORD	mPrev;
	UWORD	mNext;
	UBYTE	mX, mY;
	UBYTE	mUnknown6;
	UBYTE	mSize;
};

#if defined(DAMAGE_FULL) || defined(LANDSCAPETEXTURE_CPP)	// only these, so the other objects keep their declaration counts
extern SINT	MIP_OFFSETS[];		// 00628298 (damage.cpp): each mip level's offset in the damage texture's data
#endif

// The damage texture (0xc bytes): mip levels of an inverted, rescaled greyscale TGA (LoadTexture 00440c70).
class CDamageTexture
{
public:
	CDamageTexture() { mData = NULL; }
	void	Free() { SAFE_DELETE_ARRAY(mData); }
	void	Load(char *name);									// 00440c70
	void	Deserialize(class CChunkReader *c);					// inlined in CDamage::DeserializeAll (00441032)

#ifdef LANDSCAPETEXTURE_CPP
	UBYTE	*GetLevel(SINT level) { return mData + MIP_OFFSETS[level]; }	// a mip level's data (the smallest is 0)
#endif

	UBYTE	*mData;												// +0x00
	SINT	mLevels;											// +0x04
	SINT	mSize;												// +0x08
};
#endif

// The landscape's damage marks (damage.cpp). One object at 008aa9f0.
class CDamage
{
public:
	void				Reset();								// 00440c40 (CEngine::BuildLevelSpecifics 0044a13a)
	void				DeserializeAll(class CChunkReader *c);	// 00441000 (ReadResources 004d78b4)
	void				LockCurrent() { mLockedCount = mCount; }	// CEngine::BuildLevelSpecifics 0044a19c (names open)
#if defined(SYSTEM_CPP) && !defined(DAMAGE_FULL)
	void				Shutdown();								// 00440c00 (SYSTEM.Shutdown 004f01e7)
#endif

#ifdef DAMAGE_FULL	// damage.cpp's view; engine.h's many includers keep the short one (header-composition ties)
						CDamage();								// 00440b70 (CDXLandscape's static initializer 005446c5)
						~CDamage();								// empty (folded with DebugTrace, 0040c640; CDXLandscape's exit 005446f9)
	void				Init();									// 00440b90 (CDXLandscape::Init 00544b2b)
	void				Shutdown();								// 00440c00 (SYSTEM.Shutdown 004f01e7)
	void				Add(SWORD cell, UBYTE x, UBYTE y, UBYTE size);	// 00440eb0 (CDXLandscape::AddDamage 0054772a)
	void				Remove(SWORD cell, UBYTE x, UBYTE y);	// 00440f80 (0054773a)

	class CDamageTexture	*mTexture;							// +0x00
	CDamageMark			mMarks[10000];							// +0x04: a free list and one list per cell
	UWORD				mCells[0x1000];							// +0x13884: each cell's first mark (the top bit marks a cell head)
#else
	UBYTE				mUnknown00[0x15884];
#endif
	SINT				mCount;									// +0x15884 (008c0274)
	SINT				mLockedCount;							// +0x15888 (008c0278)
#ifdef DAMAGE_FULL
	BOOL				mLoaded;								// +0x1588c: DeserializeAll set it; Init then keeps the texture
#endif
};

// A list of landscape patches (8 bytes; 0x50-byte patches, free while their word at +0x3c is 0xffff).
struct CDXPatchList
{
#ifdef DXPATCHMANAGER_CPP
	~CDXPatchList();								// 005503b0 (the lists' array destructor calls it)
	void				Init(SINT count, SINT cells);	// inline in DXPatchManager.cpp (its line 17)
	class CLandscapeVB	*Allocate(SINT slot);		// 00550400 (CDXLandscape::UpdateLOD 00546fa6)
#endif
#ifdef DXLANDSCAPE_CPP
	class CLandscapeVB	*Allocate(SINT slot);		// 00550400 (CDXLandscape::CalcLODs 00546fa6)
#endif
	void				Reset();					// 005503d0: frees every patch

	UBYTE				*mPatches;					// +0x00
	SINT				mCount;						// +0x04
};

// The landscape patch manager (DXPatchManager.cpp, 005502d0..005507e7).
class CDXPatchManager
{
public:
	CDXPatchList		*mLists;					// +0x00: three lists (CDXLandscape::BuildLevelSpecifics 0054538a)

	void				Init(int a, int b, int c);	// 00550430 (CDXEngine::Init passes 800, 300, 90)
	void				Shutdown();					// 005506e0
	void				DeserializeAll(class CChunkReader *c);	// 00550750 (ReadResources 004d7875)
#ifdef DXPATCHMANAGER_CPP
	CDXPatchManager() { mLists = NULL; mIBs = NULL; mData = NULL; mLoaded = FALSE; }
	~CDXPatchManager() { FreeData(); }
	void				FreeData();					// 00550730 (LT's PATCHMANAGER.FreeData, 00512070)
#endif
#if defined(DXPATCHMANAGER_CPP) || defined(DXLANDSCAPE_CPP) || defined(LANDSCAPEIB_CPP)	// CDXLandscape::RenderTiles reads mIBs
	class CLandscapeIB	*mIBs;						// +0x04: 3 levels of 16 edge masks
	BOOL				mLoaded;					// +0x08 (009c64e0): the strips below came with the level
	UWORD				*mData;						// +0x0c (009c64e4): every level and edge set's strips, read with the level
	SINT				mOffsets[3 * 16 + 1];		// +0x10 (009c64e8): [level * 16 + edges], each set's first word in mData;
													// the last (+0xd0) is the count. CLandscapeIB::CreateIndexBuffer copies
													// the range between one and the next.
#endif
};

extern CDXPatchManager	PATCHMANAGER;				// 009c64d8

class CMapTex;
class CCamera;
class CLandscapeTexture;
class CVBuffer;
class CIBuffer;
class CVertexShader;
class CDXTexture;

// The base of the PC landscape (landscape.obj): only the mixer detail texture, one for every landscape.
class CLandscape
{
public:
						CLandscape();				// 0048de90
						~CLandscape();				// 0048dea0
	static void			LoadMixerTexture(SINT n);	// 0048dec0 (CMap::Deserialize 00491124)
	static CDXTexture		*mMixerTexture;				// 0067a7d0
};

// A level of detail's cell list entry (0xc bytes): a 2-byte-per-cell map of the sub-tiles.
struct SLandscapeLOD
{
	SLandscapeLOD() { mData = NULL; }
	~SLandscapeLOD() { delete mData; }
	void				Init(SINT count);			// inline in DXLandscape.h (its line 170)

	UWORD				*mData;						// +0x00
	SINT				mSize;						// +0x04: cells
	SINT				mUnknown08;					// +0x08
};

// A landscape patch (0x50 bytes; CDXPatchList).
struct SLandscapePatch
{
	UBYTE				mUnknown00[0x3c];
	UWORD				mSlot;						// +0x3c: 0xffff when free
	UBYTE				mUnknown3E[0x50 - 0x3e];
};

// A landscape tile (0x14 bytes; 64x64 of them).
struct SLandscapeCell
{
	signed char			mMinHeight;					// +0x00: the lowest point, 127 per 60 metres
	signed char			mMaxHeight;					// +0x01
	UBYTE				mUnknown02[2];
	float				mMidHeight;					// +0x04
	float				mComplexity;				// +0x08
	struct SLandscapePatch	*mPatch;			// +0x0c: its patch in the patch manager, or NULL
	signed char			mUnknown10;					// +0x10: the level of detail drawn, negative for none (0x9d at first)
	UBYTE				mEdges;						// +0x11: the edges stitched to coarser neighbours (PATCHMANAGER.mIBs)
	UBYTE				mUnknown12[2];
};

// One viewpoint's landscape (0x34 bytes): its textures, tiles and levels of detail.
class CLandscapeView
{
public:
	CLandscapeView();
	~CLandscapeView();								// 00544770
	void				CreateMipLevels(SINT levels);	// 005447e0

	CLandscapeTexture	*mTextures;					// +0x00: levels - 1 mip levels
	SLandscapeCell		*mTiles;					// +0x04
	SLandscapeLOD		*mLODs;						// +0x08
	SINT				mNumLevels;					// +0x0c
	CCamera				*mCamera;					// +0x10
	FVector				mLastPos;					// +0x14
	FVector				mUnknown24;					// +0x24
};

// 0x40 bytes (CEngine::Init 00449ba8); deleted through its virtual destructor (CEngine::Shutdown 004498e6).
// RTTI CDXLandscape (vtable 005e50d0): DeviceObject at +0, CLandscape at +8.
class CDXLandscape : public CLandscape, public DeviceObject
{
public:
						CDXLandscape();				// 00544a00
	virtual				~CDXLandscape();			// 00544a60
	virtual HRESULT		InitDeviceObjects();		// 1 (00544cc0)
	virtual HRESULT		DeleteDeviceObjects();		// 4 (00544eb0)
	BOOL				Init(CMapTex *maptexs);		// 00544af0
	void				InitResources();			// 005453d0
	void				ShutDown();					// 00544f10
	void				ResetPos(int x, int y);		// 00544fb0
	void				BuildVertexBuffer();		// 00544fc0
	void				BuildLevelSpecifics();		// 00545070
	void				UpdatePos(CCamera *cam, int viewpoint);	// 005453f0
	void				CalcLODs(class C3DGamut *gamut, int viewpoint);	// 00546b40 (CDXEngine::Render 0053e49f)
	void				Render(class C3DGamut *gamut, int viewpoint);	// 00545410
	void				RenderTerrain(int viewpoint);	// 00545590
	bool				RenderShadowMap(int viewpoint);	// 00546490: the GeForce FX shadow map (RenderTerrain 005461f5)
	void				RenderTiles(SINT a, SINT b, SINT c, SINT d);	// 00546900
	void				UpdateArea(SINT x0, SINT y0, SINT x1, SINT y1, BOOL geometry);	// 005473b0
	void				AddDamage(float x, float y, int size);	// 005475d0
	void				BuildTextureCache();		// 00547860 (the console's BuildLandscapeCache)
	float				TileComplexity(SINT tile);	// 00547a60
	void				MakeNextUpdateBeFast();		// 00546b10 (Stuart's _DIRECTX call in CGame::InitRestartLoop)
	inline void			InvalidateTile(SINT tile);	// every view's mip levels (DXLandscape.cpp)

	void				ResetDamage() { mDamage.Reset(); }
	void				LockCurrentDamage() { mDamage.LockCurrent(); }
	class CDamage		*GetDamage() { return &mDamage; }

	static class CDamage	mDamage;				// 008aa9f0: one object, not a per-landscape member (ReadResources 004d78af)

	struct IDirect3DTexture9	*mUnknown08;		// +0x08: a 512 (256) float texture the particle shader samples
													// (CDXParticleTexture::RenderAll 0054ff7c)
	struct IDirect3DSurface9	*mDepthSurface;		// +0x0c: the fake Z buffer
	struct IDirect3DTexture9	*mDepthTexture;		// +0x10: the Z buffer as a texture (OTHER_ZBUFFER_AS_TEXTURE)
	struct IDirect3DTexture9	*mMirrorTexture;	// +0x14: the reflection pass's target (CGame::Render 0046e4cd)
	CVertexShader		*mVertexShader;				// +0x18
	struct IDirect3DPixelShader9	*mPixelShader;	// +0x1c
	SINT				mNumViews;					// +0x20: 2 in multiplayer
	CLandscapeView		*mViews;					// +0x24
	CVBuffer			*mVB;						// +0x28
	CIBuffer			*mIB;						// +0x2c
	CLandscapeTexture	*mTexture;					// +0x30
	SINT				mUnknown34;					// +0x34
	CDXTexture			*mCloudTexture;				// +0x38
	bool				mUnknown3C;					// +0x3c
};
#define CLANDSCAPE CDXLandscape

#endif
