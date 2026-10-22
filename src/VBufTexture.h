// VBufTexture.h: CVBufTexture, the vertex and index buffers a texture draws from (0x68 bytes; reconstructed from
// vbuftexture.obj, 005003c0..005015c0; not in Stuart's release). Each texture keeps one shared buffer set (the
// texture's +0x140); a caller asking for a unique one gets its own. Vertices go through two CVBuffers used in turn
// when double buffering is on; indices through one CIBuffer. Both grow in powers of two from 1K.

#ifndef VBUFTEXTURE_H
#define VBUFTEXTURE_H
#include "maths.h"

class CDXTexture;
class CVBuffer;
class CIBuffer;

// A lit, textured vertex (0x24 bytes; FVF 0x152: position, normal, diffuse, one texture coordinate). The position is
// three floats, not a 16-byte FVector (CDXEngine::RenderArrow 0053e149..0053e1a3).
#define FVF_GTEX_FLAGS	(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1)
struct CFVF_GTEXPos
{
	float	X, Y, Z;
	CFVF_GTEXPos &operator=(const FVector &v) { X = v.X; Y = v.Y; Z = v.Z; return *this; }	// FUNCTION: 0x004c7d90
};
struct CFVF_GTEX
{
	CFVF_GTEXPos	mPos;			// +0x00
	float			mNX, mNY, mNZ;	// +0x0c
	DWORD			mDiffuse;		// +0x18
	float			mU, mV;			// +0x1c
};

// An entry of the mesh renderer's draw list (0x24 bytes) that CVBufTexture::DrawList walks once per pass (00500d10;
// CMeshRenderer::RenderMeshCore 0054990b). The pass number is +0x88 of the first member's target; names open.
struct SVBufTextureDrawItem
{
	UBYTE			mUnknown00[0x88];
	SINT			mPass;			// +0x88
};
struct SVBufTextureDrawEntry
{
	SVBufTextureDrawItem	*mItem;		// +0x00
	class CVBufTexture		*mVBuf;		// +0x04
	UBYTE			mUnknown08[0x24 - 0x08];
};
struct SVBufTextureDrawList
{
	SVBufTextureDrawEntry	*mEntries;	// +0x00
	SINT			mCount;			// +0x04
};

// Clear, buffers are made static and managed (vbuftexture.cpp 00854de0; SetVBFormat 00500540 reads its value 00854dec).
class CTweakSLONG;
extern CTweakSLONG	CLAIM_WRITEONLY;

class CVBufTexture
{
public:
						CVBufTexture(CDXTexture *texture);								// 005003f0
						~CVBufTexture();												// 00500460
	void				SetVBFormat(DWORD fvf, DWORD usage, SINT stride, SINT primtype, SINT pool);	// 00500540
	void				SetIBFormat(SINT format, DWORD usage, SINT size, SINT pool);	// 00500590: the size is unused
	void				SetPersist();													// 005005d0: never reset by ResetAll
	void				ResizeVB(SINT size);											// 005005e0
	void				ResizeIB(SINT size);											// 005007f0
	void				UnlockVB();														// 005009c0
	void				UnlockIB();														// 005009f0
	void				EnsureLockV();													// inline only
	void				EnsureLockI();													// inline only
	int					AddVertices(void *verts, int count);							// 00500a10: returns the first vertex's index
	void				AddIndices(void *indices, int count);							// 00500ac0
	UWORD				*GetIndexPtr(int count);										// 00500b40: room for count indices
	int					GetVertexPtr(void **verts, int count);							// 00500bb0: room for count vertices; the first's index
	int					GetNumIndexedPrimitives();										// 00500c50
	int					GetNumPrimitives();												// 00500cb0
	static void			DrawList(SVBufTextureDrawList *list);							// 00500d10
	static void			ResetAll();														// 00500d60 (CDXEngine::PostRender 0053ef68)
	void				Draw(bool reset);												// 00500e70
	void				Reset();														// 00500f80
	void				DWATS(bool reset, int numverts, int numprims);					// 00500fa0: validates the device first ("CVBT::DWATS")
	void				DrawIndexed(bool reset, int numverts, int numprims);			// 005010e0
	void				DrawNonIndexed(bool reset, int numprims);						// 005011c0
	static CVBufTexture	*Find(CDXTexture *texture);										// inline only
	static CVBufTexture	*GetOrCreate(CDXTexture *texture, SINT unique);					// 00501280
	void				Release();														// 00501310
	static CVBufTexture	*GetByName(char *name, int a);									// 00501320
	static void			FreeLevelResources();											// 00501360 (CGame::Shutdown 0046ca18)
	static void			FreeAllResources();												// 00501450 (SYSTEM::Shutdown 004f01ba)
	static void			DownSizeSome();													// 00501540 (CDXEngine::PostRender 0053efea)
	static void			ClearOut();														// 005015c0: every dynamic texture
	// Stuart's names (DXEngine.cpp): the texture's buffer and its vertex and index formats
	static CVBufTexture	*GetTextureByTex(CDXTexture *texture, SINT a = 0) { return GetOrCreate(texture, a); }
	void				SetupV(DWORD fvf, DWORD usage, SINT size, SINT primtype, SINT pool) { SetVBFormat(fvf, usage, size, primtype, pool); }
	void				SetupI(SINT format, DWORD usage, SINT size, SINT pool) { SetIBFormat(format, usage, size, pool); }

	CDXTexture			*mTexture;			// +0x00
	DWORD				mFVF;				// +0x04
	DWORD				mVUsage;			// +0x08
	SINT				mVPool;				// +0x0c: a D3DPOOL
	bool				mVLocked;			// +0x10
	SINT				mVSize[2];			// +0x14: each vertex buffer's size in bytes
	SINT				mVUsed;				// +0x1c: bytes
	SINT				mIFormat;			// +0x20: a D3DFORMAT
	DWORD				mIUsage;			// +0x24
	SINT				mIPool;				// +0x28: a D3DPOOL
	bool				mILocked;			// +0x2c
	SINT				mISize;				// +0x30: bytes
	SINT				mIUsed;				// +0x34: bytes
	UBYTE				*mVData;			// +0x38: the locked vertices
	UBYTE				*mIData;			// +0x3c: the locked indices
	CVBuffer			*mVB[2];			// +0x40
	SINT				mCurrentVB;			// +0x48
	CIBuffer			*mIB;				// +0x4c
	SINT				mPrimType;			// +0x50: a D3DPRIMITIVETYPE
	SINT				mStride;			// +0x54
	CVBufTexture		*mNext;				// +0x58
	bool				mPersist;			// +0x5c
	SINT				mRefCount;			// +0x60
	SINT				mLastVUsed;			// +0x64: the last frame's vertex bytes (DownSizeSome)

	static CVBufTexture	*mFirst;			// 00854e00
	static bool			mDoubleBuffer;		// 00854e04
};

#endif
