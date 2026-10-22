// DXMeshVB.h: CDXMeshVB, a mesh part's vertex buffers (vtable 005e50fc; 0x128 bytes; DXMeshVB.obj, 0054bf80..0054e4ff;
// Stuart's release has no copy). A DeviceObject with CMeshVB at +8 (RTTI): the part's geometry in up to 64 pieces
// that share one vertex buffer, each with its own index buffer and textures. CMeshPart's constructor makes one
// (004ae616) and points +0x10c back at the part; its destructor deletes it through slot 0 (004ae84b).

#ifndef DXMESHVB_H
#define DXMESHVB_H

#include "deviceobject.h"

class CMeshPart;
class CMeshTexture;
class CChunkReader;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;
class CVertexShader;

// One piece (0x3c bytes, MEMTYPE 58): the vertices it draws and the six textures its triangles use.
class CMeshVBPiece
{
public:
	CMeshVBPiece() { mVB = NULL; mIB = NULL; }

	IDirect3DVertexBuffer9	*mVB;			// +0x00: shared with piece 0
	IDirect3DIndexBuffer9	*mIB;			// +0x04
	UINT			mVBSize;				// +0x08: bytes
	UINT			mIBSize;				// +0x0c: bytes
	SINT			mNumIndices;			// +0x10
	SINT			mNumVertices;			// +0x14
	SINT			mNumPrimitives;			// +0x18
	BOOL			mBuilt;					// +0x1c
	CMeshTexture	*mTextures[6];			// +0x20
	UBYTE			mUnknown38[0x3c - 0x38];
};

class CMeshVB
{
public:
	CMeshVBPiece	*mPieces[64];			// +0x08
	SINT			mNumPieces;				// +0x108
	CMeshPart		*mPart;					// +0x10c
	CVertexShader	*mShader;				// +0x110: skinned parts
	UINT			mStride;				// +0x114
	DWORD			mFVF;					// +0x118
	SINT			mPrimitiveType;			// +0x11c
	UINT			mFilePos;				// +0x120: where Load found the buffers in the resource file
	char			*mFileName;				// +0x124
};

class CDXMeshVB : public DeviceObject, public CMeshVB
{
public:
	CDXMeshVB();									// 0054bf80
	virtual				~CDXMeshVB();				// 0054c010
	virtual HRESULT		InitDeviceObjects();		// 0054d210
	virtual HRESULT		DeleteDeviceObjects();		// 0054d3f0

	HRESULT				BuildStaticVB();			// 0054c0a0
	HRESULT				BuildSkinnedVB();			// 0054c920
	void				Load(CChunkReader *c, BOOL skinned);	// 0054e160 (CMeshPart::Deserialize 004b3159)
#if defined(DXMESHRENDERER_CPP) || defined(DXMESHVB_CPP)	// only these, so the other objects keep their declaration counts
	// the part drawn from its buffers, a pass per texture layer (name ours; the mesh renderer calls it, 0054a4b6)
	void				Render(class IRenderableThing *thing, SINT flags, FMatrix *ori, FVector *pos);	// 0054d530
#endif
};

#endif
