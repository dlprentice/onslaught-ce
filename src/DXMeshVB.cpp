// DXMeshVB.cpp: reconstructed from DXMeshVB.obj (0054bf80..0054e4ff); Stuart's release has no copy. The allocations'
// __LINE__ values are the retail ones; the #line directives place them, because the retail file's layout is unknown.
#include "common.h"
#define DXMESHVB_CPP
#include <d3d9.h>
#include "ltshell.h"
#include "console.h"
#include "mesh.h"
#include "DXMeshVB.h"
#include "membuffer.h"
#include "DXMemBuffer.h"
#include "chunker.h"
#include "VertexShader.h"
#include "VBufTexture.h"
#include "NvTriStrip.h"
#include "tweak.h"
#include "engine.h"
#define LT_SHELL_INCLUDE
#include "renderinfo.h"
#include "renderablething.h"
#include "atmospherics.h"
#include "DebugLog.h"
#include "meshrenderer.h"

//******************************************************************************************
CDXMeshVB::CDXMeshVB()	// FUNCTION: 0x0054bf80
{
	mPart = NULL;
	mNumPieces = 0;
	mShader = NULL;
	mFilePos = 0;
	mFileName = NULL;
	for (SINT i = 0; i < 64; i++)
		mPieces[i] = NULL;
	LT.AddDeviceObject(this);
}

// FUNCTION: 0x0054bff0 ??_GCDXMeshVB@@UAEPAXI@Z
//******************************************************************************************
CDXMeshVB::~CDXMeshVB()	// FUNCTION: 0x0054c010
{
	DeleteDeviceObjects();
	for (SINT i = 0; i < 64; i++)
		SAFE_DELETE(mPieces[0]);		// retail frees only the first slot, 64 times (0054c046)
	LT.RemoveDeviceObject(this);
	delete [] mFileName;
}

// A static part's vertex (0x24 bytes; FVF 0x152). The empty constructor leaves the new[] count store (0054c102).
struct SMeshVBVertex
{
	SMeshVBVertex() {}

	CFVF_GTEXPos	Pos;			// +0x00
	CFVF_GTEXPos	Normal;		// +0x0c
	DWORD	Diffuse;				// +0x18
	float	U, V;					// +0x1c
};

//******************************************************************************************
// A static part's buffers: its vertices in one buffer, its triangles sorted into pieces by texture and stripped.
#line 66
HRESULT CDXMeshVB::BuildStaticVB()	// FUNCTION: 0x0054c0a0
{
	mNumPieces = 0;
	SINT	num_vertices = mPart->mNumDVertices;
	SINT	num_triangles;
	if ((num_vertices >= 1) && ((num_triangles = mPart->mNumTriangles) >= 1))
	{
		UINT	vb_size = num_vertices * sizeof(SMeshVBVertex);
#line 81
		SMeshVBVertex	*verts = new(MEMTYPE_MESHVB) SMeshVBVertex[num_vertices];
		WORD			*indices = new(MEMTYPE_MESHVB) WORD[num_vertices * 384];

		SINT	i, p;
		for (i = 0; i < num_vertices; i++)
		{
			CMeshDVertex	*v = &mPart->mDVertices[i];
			for (p = 0; p < mNumPieces; p++)
			{
				BOOL	same = TRUE;
				for (SINT t = 0; t < 6; t++)
				{
					const SINT &index = v->mTexture[t];
					if ((index != -1) && (mPart->mBaseMesh->mTextures[index].mMaxBlend > 0) && (mPart->mBaseMesh->mTextures[index].mTexture))
					{
						if (mPieces[p]->mTextures[t] != &mPart->mBaseMesh->mTextures[index])
							same = FALSE;
					}
					else if (mPieces[p]->mTextures[t] != NULL)
						same = FALSE;
				}
				if (same)
					break;
			}

			if (p == mNumPieces)
			{
#line 132
				mPieces[mNumPieces] = new(MEMTYPE_MESHVB) CMeshVBPiece;
				mPieces[mNumPieces]->mNumVertices = 0;
				mPieces[mNumPieces]->mNumIndices = 0;
				mPieces[mNumPieces]->mNumPrimitives = 0;
				for (SINT t = 0; t < 6; t++)
				{
					const SINT &index = v->mTexture[t];
					if ((index != -1) && (mPart->mBaseMesh->mTextures[index].mMaxBlend > 0) && (mPart->mBaseMesh->mTextures[index].mTexture))
						mPieces[mNumPieces]->mTextures[t] = &mPart->mBaseMesh->mTextures[index];
					else
						mPieces[mNumPieces]->mTextures[t] = NULL;
				}
				mNumPieces++;
			}

			FVector	*pos = &mPart->mPVertices[0][v->mPVert];
			verts[i].Pos = *pos;
			verts[i].Diffuse = v->mDiffuse;
			verts[i].U = v->mU;
			verts[i].V = v->mV;
			verts[i].Normal = v->mNormal;
		}

		SetCacheSize(24);
		SetStitchStrips(true);
		SetMinStripSize(0);
		SetListsOnly(false);

		for (p = 0; p < mNumPieces; p++)
		{
			mPieces[p]->mNumIndices = 0;
			mPieces[p]->mNumPrimitives = 0;
		}

		WORD	*lists[64];
		SINT	counts[64];
		for (p = 0; p < mNumPieces; p++)
		{
#line 186
			lists[p] = new(MEMTYPE_MESHVB) WORD[num_triangles * 3];
			counts[p] = 0;
		}

		for (i = 0; i < num_triangles; i++)
		{
			CMeshTriangle	*tri = &mPart->mTriangles[i];
			for (p = 0; p < mNumPieces; p++)
			{
				BOOL	same = TRUE;
				for (SINT t = 0; t < 6; t++)
				{
					const SINT &index = tri->mVertices[0]->mTexture[t];
					if ((index != -1) && (mPart->mBaseMesh->mTextures[index].mMaxBlend > 0) && (mPart->mBaseMesh->mTextures[index].mTexture))
					{
						if (mPieces[p]->mTextures[t] != &mPart->mBaseMesh->mTextures[index])
							same = FALSE;
					}
					else if (mPieces[p]->mTextures[t] != NULL)
						same = FALSE;
				}
				if (same)
					break;
			}
			counts[p] += 3;
			lists[p][counts[p] - 3] = tri->mVertices[0] - mPart->mDVertices;
			lists[p][counts[p] - 2] = tri->mVertices[1] - mPart->mDVertices;
			lists[p][counts[p] - 1] = tri->mVertices[2] - mPart->mDVertices;
			mPieces[p]->mNumIndices++;
		}

		for (SINT stripPiece = 0; stripPiece < mNumPieces; stripPiece++)
		{
			PrimitiveGroup	*groups;
			unsigned short	num_groups;
			GenerateStrips(lists[stripPiece], counts[stripPiece], &groups, &num_groups);
			mPieces[stripPiece]->mNumIndices = 0;
			for (SINT k = 0; k < SINT(groups->numIndices); k++)
			{
				indices[stripPiece * num_vertices * 6 + mPieces[stripPiece]->mNumIndices] = groups->indices[k];
				mPieces[stripPiece]->mNumIndices++;
			}
			mPieces[stripPiece]->mNumPrimitives = mPieces[stripPiece]->mNumIndices - 2;
			delete [] groups;
		}

		for (p = 0; p < mNumPieces; p++)
			delete [] lists[p];

		BOOL	ok = TRUE;
		if (mNumPieces > 0)
		{
			mPieces[0]->mNumVertices = num_vertices;
			mPieces[0]->mVBSize = mPieces[0]->mNumVertices * sizeof(SMeshVBVertex);
			if (LT.D3D_CreateVertexBuffer(mPieces[0]->mVBSize, D3DUSAGE_WRITEONLY, FVF_GTEX_FLAGS, D3DPOOL_MANAGED, &mPieces[0]->mVB) >= 0)
			{
				MEM_MANAGER.TagMem(this, mPieces[0]->mVBSize, "Meshes/%s/Part %d/Vertex buffer", mPart->mBaseMesh->mName, mPart->mPartNumber);
				void *vertexData;
				if (mPieces[0]->mVB->Lock(0, 0, &vertexData, 0) >= 0)
				{
					memcpy(vertexData, verts, vb_size);
					mPieces[0]->mVB->Unlock();
				}
				else
					ok = FALSE;
			}
			else
				ok = FALSE;

			for (p = 1; p < mNumPieces; p++)
			{
				mPieces[p]->mVBSize = mPieces[0]->mVBSize;
				mPieces[p]->mVB = mPieces[0]->mVB;
				mPieces[p]->mNumVertices = mPieces[0]->mNumVertices;
			}
		}

		for (SINT bufferPiece = 0; bufferPiece < mNumPieces; bufferPiece++)
		{
			mPieces[bufferPiece]->mBuilt = TRUE;
			if (mPieces[bufferPiece]->mBuilt)
			{
				mPieces[bufferPiece]->mIBSize = mPieces[bufferPiece]->mNumIndices * sizeof(WORD);
				if (LT.D3D_CreateIndexBuffer(mPieces[bufferPiece]->mIBSize, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &mPieces[bufferPiece]->mIB) >= 0)
				{
					MEM_MANAGER.TagMem(this, mPieces[bufferPiece]->mIBSize, "Meshes/%s/Part %d/VBPart %d/Index buffer", mPart->mBaseMesh->mName, mPart->mPartNumber, bufferPiece);
					void *indexData;
					if (mPieces[bufferPiece]->mIB->Lock(0, 0, &indexData, 0) >= 0)
					{
						memcpy(indexData, &indices[bufferPiece * num_vertices * 6], mPieces[bufferPiece]->mNumIndices * sizeof(WORD));
						mPieces[bufferPiece]->mIB->Unlock();
					}
					else
						ok = FALSE;
				}
				else
					ok = FALSE;
			}
		}

		mStride = sizeof(SMeshVBVertex);
		mFVF = FVF_GTEX_FLAGS;
		mPrimitiveType = D3DPT_TRIANGLELIST;
		delete [] indices;
		delete [] verts;
		return ok ? S_OK : E_FAIL;
	}
	return S_OK;
}

// A skinned part's vertex (0x30 bytes; no FVF, the skinning shader's declaration reads it): each of the three
// bone slots times 3, the shader's constant registers per bone matrix. Position and normal are CFVF_GTEXPos, as in the
// static vertex: retail fills them through its assignment, with the vertex pointer held at the normal's Z (0054c9da).
struct SMeshVBSkinVertex
{
	SMeshVBSkinVertex() {}

	CFVF_GTEXPos	Pos;			// +0x00
	float	Bones[3];				// +0x0c
	CFVF_GTEXPos	Normal;			// +0x18
	DWORD	Diffuse;				// +0x24
	float	U, V;					// +0x28
};

//******************************************************************************************
// A skinned part's buffers, built like a static part's but with each vertex's three bone slots and, when the device
// skins in software, software-processing buffers in system memory.
// Written like BuildStaticVB (texture slots by reference, success-first failure tails, a signed strip bound), it has
// retail's control flow. The lock pointer is declared in each block that locks: retail's frame (0x228) shares its
// slot with the triangle count's, where a function-scope pointer kept a slot of its own.
// The loops after the vertex loop have their own counters (BuildStaticVB's stripPiece and bufferPiece, and three
// more); only the piece searches and the vertex-buffer copy use p. Each counter moves the numbers of the later
// temporaries, and the texture loops depend on them: C2 orders the piece slot's address sum by operand hash, a
// temporary's being (number << 6) & 0xffff (the rank tool). Retail adds (mPieces[p] + (-0x10 - v)) +
// the vertex pointer in both loops (0054ca5f, 0054cd3a), so the invariant and the pointer must hash below the piece
// load's c007. The five counters add 64: the triangle loop's two temporaries move from 0x7c9/0x7df past 0x800, where
// the hash wraps to small values, and the vertex loop's stay low. In the other order, as in BuildStaticVB, the piece
// pointer takes ebx and the triangle is spilled; in retail's, the triangle keeps ebx, the vertex is reloaded from it
// at every piece (0054cced) and the piece pointer stays in memory.
// The vertex position reads through a `positions` row pointer, as DXMeshRenderer's vertex loops do
// (`FVector *positions = part->mPVertices[0];`). The local adds no code, but it moves the number of the CSE pass's
// address temporary for v + 0x20 (mPositionIndex) from 1 to 3 mod 4, and with it the key of that load from 0x4007 to
// 0xc007 (an IND through an address temporary keys 7 + ((number mod 4) << 14); the rank tool). In the
// bones loop's row address, mBoneSlots + index * 4 (tuple 268), the scaled index (0x4039 -> 0xc039) then sorts ahead
// of the mBoneSlots load (0x81df): it is evaluated first and its single-block temporary is chosen first, so the index
// takes edi and the part pointer ebx, as in retail (0054cbd8). Written straight into the pos expression, the index
// keyed 0x4039, the part pointer was chosen first and took edi (4 instructions differed). A volatile index read also
// gives retail's registers, but it is a statement of its own from the first IL dump on and the scheduler keeps it
// ahead of the part pointer's load (2 instructions differ).
#line 418
HRESULT CDXMeshVB::BuildSkinnedVB()	// FUNCTION: 0x0054c920
{
	mNumPieces = 0;
	CONSOLE.Status("Building skeletal VB");
	SINT	num_vertices = mPart->mNumDVertices;
	SINT	num_triangles = mPart->mNumTriangles;
#line 425
	SMeshVBSkinVertex	*verts = new(MEMTYPE_MESHVB) SMeshVBSkinVertex[num_vertices * 64];
	WORD				*indices = new(MEMTYPE_MESHVB) WORD[num_vertices * 192];
	WORD				*unused = new(MEMTYPE_MESHVB) WORD[num_vertices];
	CVertexShader::CheckCaps();

	SINT	i, p;
	for (i = 0; i < num_vertices; i++)
	{
		CMeshDVertex	*v = &mPart->mDVertices[i];
		for (p = 0; p < mNumPieces; p++)
		{
			BOOL	same = TRUE;
			for (SINT t = 0; t < 6; t++)
			{
				const SINT &index = v->mTexture[t];
				if ((index != -1) && (mPart->mBaseMesh->mTextures[index].mMaxBlend > 0) && (mPart->mBaseMesh->mTextures[index].mTexture))
				{
					if (mPieces[p]->mTextures[t] != &mPart->mBaseMesh->mTextures[index])
						same = FALSE;
				}
				else if (mPieces[p]->mTextures[t] != NULL)
					same = FALSE;
			}
			if (same)
				break;
		}

		if (p == mNumPieces)
		{
#line 472
			mPieces[mNumPieces] = new(MEMTYPE_MESHVB) CMeshVBPiece;
			mPieces[mNumPieces]->mNumVertices = 0;
			mPieces[mNumPieces]->mNumIndices = 0;
			mPieces[mNumPieces]->mNumPrimitives = 0;
			for (SINT t = 0; t < 6; t++)
			{
				const SINT &index = v->mTexture[t];
				if ((index != -1) && (mPart->mBaseMesh->mTextures[index].mMaxBlend > 0) && (mPart->mBaseMesh->mTextures[index].mTexture))
					mPieces[mNumPieces]->mTextures[t] = &mPart->mBaseMesh->mTextures[index];
				else
					mPieces[mNumPieces]->mTextures[t] = NULL;
			}
			mNumPieces++;
		}

		FVector	*positions = mPart->mPVertices[0];
		FVector	*pos = &positions[v->mPVert];
		verts[i].Pos = *pos;
		verts[i].Diffuse = v->mDiffuse;
		verts[i].U = v->mU;
		verts[i].V = v->mV;
		verts[i].Normal = v->mNormal;
		for (SINT b = 0; b < 3; b++)
			verts[i].Bones[b] = mPart->mBoneSlots[v->mPVert][b] * 3.0f;
	}

	SetCacheSize(24);
	SetStitchStrips(true);
	SetMinStripSize(0);
	SetListsOnly(false);

	for (SINT resetPiece = 0; resetPiece < mNumPieces; resetPiece++)
	{
		mPieces[resetPiece]->mNumIndices = 0;
		mPieces[resetPiece]->mNumPrimitives = 0;
	}

	WORD	*lists[64];
	SINT	counts[64];
	for (SINT listPiece = 0; listPiece < mNumPieces; listPiece++)
	{
#line 534
		lists[listPiece] = new(MEMTYPE_MESHVB) WORD[num_triangles * 3];
		counts[listPiece] = 0;
	}

	for (i = 0; i < num_triangles; i++)
	{
		CMeshTriangle	*tri = &mPart->mTriangles[i];
		for (p = 0; p < mNumPieces; p++)
		{
			CMeshDVertex	*v = tri->mVertices[0];
			BOOL	same = TRUE;
			for (SINT t = 0; t < 6; t++)
			{
				const SINT &index = v->mTexture[t];
				if ((index != -1) && (mPart->mBaseMesh->mTextures[index].mMaxBlend > 0) && (mPart->mBaseMesh->mTextures[index].mTexture))
				{
					if (mPieces[p]->mTextures[t] != &mPart->mBaseMesh->mTextures[index])
						same = FALSE;
				}
				else if (mPieces[p]->mTextures[t] != NULL)
					same = FALSE;
			}
			if (same)
				break;
		}
		counts[p] += 3;
		lists[p][counts[p] - 3] = tri->mVertices[0] - mPart->mDVertices;
		lists[p][counts[p] - 2] = tri->mVertices[1] - mPart->mDVertices;
		lists[p][counts[p] - 1] = tri->mVertices[2] - mPart->mDVertices;
		mPieces[p]->mNumIndices++;
	}

	for (SINT stripPiece = 0; stripPiece < mNumPieces; stripPiece++)
	{
		PrimitiveGroup	*groups;
		unsigned short	num_groups;
		GenerateStrips(lists[stripPiece], counts[stripPiece], &groups, &num_groups);
		mPieces[stripPiece]->mNumIndices = 0;
		for (SINT k = 0; k < SINT(groups->numIndices); k++)
		{
			indices[stripPiece * num_vertices * 6 + mPieces[stripPiece]->mNumIndices] = groups->indices[k];
			mPieces[stripPiece]->mNumIndices++;
		}
		mPieces[stripPiece]->mNumPrimitives = mPieces[stripPiece]->mNumIndices - 2;
		delete [] groups;
	}

	for (SINT freePiece = 0; freePiece < mNumPieces; freePiece++)
		delete [] lists[freePiece];

	BOOL	ok = TRUE;
	if (mNumPieces > 0)
	{
		void	*data;
		mPieces[0]->mNumVertices = num_vertices;
		mPieces[0]->mVBSize = mPieces[0]->mNumVertices * sizeof(SMeshVBSkinVertex);
		DWORD	usage = D3DUSAGE_WRITEONLY;
		D3DPOOL	pool = D3DPOOL_MANAGED;
		if (CVertexShader::mSoftware)
		{
			usage = D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING;
			pool = D3DPOOL_SYSTEMMEM;
		}
		if (LT.D3D_CreateVertexBuffer(mPieces[0]->mVBSize, usage, 0, pool, &mPieces[0]->mVB) >= 0)
		{
			MEM_MANAGER.TagMem(this, mPieces[0]->mVBSize, "Meshes/%s/Part %d/Skeletal vertex buffer", mPart->mBaseMesh->mName, mPart->mPartNumber);
			if (mPieces[0]->mVB->Lock(0, 0, &data, 0) >= 0)
			{
				memcpy(data, verts, num_vertices * sizeof(SMeshVBSkinVertex));
				mPieces[0]->mVB->Unlock();
			}
			else
				ok = FALSE;
		}
		else
			ok = FALSE;

		for (p = 1; p < mNumPieces; p++)
		{
			mPieces[p]->mVBSize = mPieces[0]->mVBSize;
			mPieces[p]->mVB = mPieces[0]->mVB;
			mPieces[p]->mNumVertices = mPieces[0]->mNumVertices;
		}
	}

	for (SINT bufferPiece = 0; bufferPiece < mNumPieces; bufferPiece++)
	{
		mPieces[bufferPiece]->mBuilt = TRUE;
		if (mPieces[bufferPiece]->mBuilt)
		{
			DWORD	usage = D3DUSAGE_WRITEONLY;
			D3DPOOL	pool = D3DPOOL_MANAGED;
			if (CVertexShader::mSoftware)
			{
				usage = D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING;
				pool = D3DPOOL_SYSTEMMEM;
			}
			mPieces[bufferPiece]->mIBSize = mPieces[bufferPiece]->mNumIndices * sizeof(WORD);
			if (LT.D3D_CreateIndexBuffer(mPieces[bufferPiece]->mIBSize, usage, D3DFMT_INDEX16, pool, &mPieces[bufferPiece]->mIB) >= 0)
			{
				MEM_MANAGER.TagMem(this, mPieces[bufferPiece]->mIBSize, "Meshes/%s/Part %d/VBPart %d/Index buffer", mPart->mBaseMesh->mName, mPart->mPartNumber, bufferPiece);
				void	*data;
				if (mPieces[bufferPiece]->mIB->Lock(0, 0, &data, 0) >= 0)
				{
					memcpy(data, &indices[bufferPiece * num_vertices * 6], mPieces[bufferPiece]->mNumIndices * sizeof(WORD));
					mPieces[bufferPiece]->mIB->Unlock();
				}
				else
					ok = FALSE;
			}
			else
				ok = FALSE;
		}
	}

	mStride = sizeof(SMeshVBSkinVertex);
	mFVF = 0;
	mPrimitiveType = D3DPT_TRIANGLELIST;
	delete [] indices;
	delete [] verts;
	CONSOLE.StatusDone("Building skeletal VB", ok != 0);
	return ok ? S_OK : E_FAIL;
}

// A zero vector (0054d1f0 initializes it).
static FVector	sZero(0, 0, 0);		// 009c63c8
// FUNCTION: 0x0054d1f0 init:sZero

//******************************************************************************************
// The buffers: read back from the resource file when Load found them there, else built from the part.
HRESULT CDXMeshVB::InitDeviceObjects()	// FUNCTION: 0x0054d210
{
	HRESULT result = LT.mD3DActive;
	if (!result)
		return result;

	if (!mPart)
	{
		mNumPieces = 0;
		return S_OK;
	}

	if ((mFilePos) && (mFileName))
	{
		CDXMemBuffer	file;
		if (!file.InitFromFile(mFileName))
		{
			CONSOLE.FatalResourceFileError(mFileName);
			return -1;
		}
		file.Skip(mFilePos);
		CChunkReader	c;
		c.Open(&file);
		Load(&c, mPart->mPartType == 3);
		c.Close();
		return S_OK;
	}

	result = S_OK;
	if (mPart->mPartType == 3)
	{
		if (CVertexShader::VertexShadersEnabled)
			result = BuildSkinnedVB();
	}
	else
		result = BuildStaticVB();
	return result;
}

//******************************************************************************************
// Both loops index pieces[i], so the first loop's pointer is set after its guard (0054d426), as in retail.
// STAND-IN: the second loop's delete arm re-reads the pointer under a sign test,
// `if ((SINT)piece < 0) piece = pieces[i];`. C2's CSE turns it into the self-copy `piece = piece`; the test reuses the
// null test's compare, and the copy and its branch are deleted after register allocation, so no code is emitted. At
// allocation the arm adds two loop-2 blocks (3x2, 2x2) where loop 2's pointer IV and `this` are live without
// references: both drop from 48 to 38 (ralog -s), below the first loop's pointer IV (46), which then takes esi first as
// in retail, and `pieces` keeps ebp. Any sign or null test on piece works; arms before the null test or with the local
// outside it reach 44/46, same-value ternaries fold before allocation. Without the arm: 44/60 (both loops indexed),
// 2/2 with the first loop's running pointer. Which source Stuart wrote is unknown.
HRESULT CDXMeshVB::DeleteDeviceObjects()	// FUNCTION: 0x0054d3f0
{
	CMeshVBPiece	**pieces = mPieces;
	if ((*pieces) && ((*pieces)->mVB))
	{
		LT.D3D_ReleaseVertexBuffer((*pieces)->mVB);
		(*pieces)->mVB = NULL;
	}

	SINT	i;
	for (i = 0; i < mNumPieces; i++)
	{
		if (pieces[i])
		{
			if (pieces[i]->mIB)
			{
				pieces[i]->mIB->Release();
				pieces[i]->mIB = NULL;
			}
			pieces[i]->mVB = NULL;
		}
	}
	for (i = 0; i < mNumPieces; i++)
	{
		if (pieces[i])
		{
			CMeshVBPiece	*piece = pieces[i];
			if ((SINT)piece < 0)
				piece = pieces[i];	// STAND-IN: the same pointer again (see above)
			delete piece;
			pieces[i] = NULL;
		}
	}
	mNumPieces = 0;

	MEM_MANAGER.UnTagMem(this);
	if (mShader)
	{
		mShader->Release();
		mShader = NULL;
	}
	return S_OK;
}

// The render methods the console can switch (0054d4d0, 0054d500).
CRenderMethod	HIT_EFFECT_METHOD("hit_effect_method", 2);				// 009c63b0
CRenderMethod	SELF_ILLUMINATION_METHOD("self_illumination_method", 2);	// 009c6390
// FUNCTION: 0x0054d4d0 init:HIT_EFFECT_METHOD
// FUNCTION: 0x0054d4f0 exit:HIT_EFFECT_METHOD
// FUNCTION: 0x0054d500 init:SELF_ILLUMINATION_METHOD
// FUNCTION: 0x0054d520 exit:SELF_ILLUMINATION_METHOD


// Each texture layer's state (009c63a4): 0 untried, 1 drawn, 2 disabled (the device rejected it, or Load's default
// for layers 3 and 5).
static UBYTE	sLayerState[6];

extern SINT		MESH_ALPHA;					// 0063012c
extern UINT		MESH_SURFACE_DETAIL;		// 00630130 (MeshRenderer.cpp): the layers after the first, in percent; 0 draws only the first
extern SINT		gMeshRendererHitEffect;		// 00704e64: the hit effect's strength, in percent

// The buffers' primitive type as Direct3D's (inline at each draw, 0054d74f..)
static inline D3DPRIMITIVETYPE GetD3DPrimitiveType(SINT type)
{
	switch (type)
	{
	case 0:		return D3DPT_POINTLIST;
	case 1:		return D3DPT_LINELIST;
	case 2:		return D3DPT_LINESTRIP;
	case 3:		return D3DPT_TRIANGLELIST;
	case 4:		return D3DPT_TRIANGLESTRIP;
	case 5:		return D3DPT_TRIANGLEFAN;
	default:	return D3DPT_POINTLIST;
	}
}

// The mesh alpha scaled by a percentage; the previous alpha is returned for restoring. (As a helper that owns `old`, its product with the
// global ranks old first, as retail's `mov ecx,ebx; imul ecx,[hit]`: a plain local is numbered before the global, so never outranks it.)
static inline SINT ScaleMeshAlpha(SINT percent)
{
	SINT	old = MESH_ALPHA;
	MESH_ALPHA = old * percent / 100;
	return old;
}

//******************************************************************************************
// The part drawn from its buffers: each piece once per texture layer, blended over the first, then the cloak and
// hit-effect passes the render flags ask for (0x40, 0x20); 0x10 draws the snow layer instead of the textures.
void CDXMeshVB::Render(IRenderableThing *thing, SINT flags, FMatrix *ori, FVector *pos)	// FUNCTION: 0x0054d530
{
	if (mNumPieces < 1)
		return;

	SINT	frame = 0;
	SINT	anim;
	if ((thing) && ((anim = thing->GetRealAnimIndex()) >= 0))
	{
		// The call is its own statement: inside the product VC6 would rank the animation's fields first (an operand's key
		// grows with the depth of its chain, a call's is the lowest) and read the chain before the call; retail calls first.
		float	f = thing->GetRenderFrame();
		f *= mPart->mBaseMesh->mAnimModes[anim].mFrameScaler;
		f += mPart->mBaseMesh->mAnimModes[anim].mStart;
		frame = SINT(floor(f)) % mPart->mNumVFrames;
	}

	if ((mShader) || (ENGINE.mDefaultShader))
	{
		if (CVertexShader::mSoftware)
			LT.SetSoftwareVertexProcessing(true);
		if (mShader)
			mShader->SetConstants();
		else if (ENGINE.mDefaultShader)
			ENGINE.mDefaultShader->SetConstants();
	}
	if (mShader)
		LT.SetVertexShader(mShader);
	else if (ENGINE.mDefaultShader)
		LT.SetVertexShader(ENGINE.mDefaultShader);
	else
		RENDERINFO.SetFVF(mFVF);
	if ((!mShader) && (!ENGINE.mDefaultShader))
		RENDERINFO.Apply(true, false);

	SINT	alpha = MESH_ALPHA;
	SINT	count = mNumPieces;
	for (SINT p = 0; p < count; p++)
	{
		SINT	layers = 6;
		if ((flags & 0x10) == 0x10)
			layers = 1;
		if ((CMeshRenderer::GetRenderMode() == 8) || (CMeshRenderer::GetRenderMode() == 4))
			layers = 1;
		if (!MESH_SURFACE_DETAIL)
			layers = 1;
		if (flags & 0x40)
			layers = 0;
		if (!mPieces[p]->mBuilt)
			layers = 0;
		LT.GetDevice()->SetIndices(mPieces[p]->mIB);
		LT.GetDevice()->SetStreamSource(0, mPieces[p]->mVB, 0, mStride);

		for (SINT l = 0; l < layers; l++)
		{
			if (sLayerState[l] == 2)
				continue;
			if (!mPieces[p]->mTextures[l])
				continue;
			if (CMeshRenderer::GetRenderMode() == 8)
			{
				LT.D3D_DrawIndexedPrimitive(GetD3DPrimitiveType(mPrimitiveType), 0, mPieces[p]->mNumVertices, 0, mPieces[p]->mNumPrimitives);
				continue;
			}

			SINT	a = SINT(alpha * mPieces[p]->mTextures[l]->mTextureBlend[frame % mPieces[p]->mTextures[l]->mFrames]);
			if (l)
				a = MESH_SURFACE_DETAIL * a / 100;
			if (a <= 10)
				continue;
			MESH_ALPHA = a;

			bool	ok;
			if ((flags & 0x10) == 0x10)
			{
				CAtmospherics::mSnowLayer->mMode = 5;
				ok = CAtmospherics::mSnowLayer->Activate();
				CAtmospherics::mSnowLayer->Select();
				RENDERINFO.Apply(true, false);
			}
			else
			{
				mPieces[p]->mTextures[l]->mTexture->mMode = l;
				mPieces[p]->mTextures[l]->mTexture->mOffsetU = mPieces[p]->mTextures[l]->mTextureU[frame % mPieces[p]->mTextures[l]->mFrames];
				mPieces[p]->mTextures[l]->mTexture->mOffsetV = mPieces[p]->mTextures[l]->mTextureV[frame % mPieces[p]->mTextures[l]->mFrames];
				mPieces[p]->mTextures[l]->mTexture->mScaleU = mPieces[p]->mTextures[l]->mTextureUScale[frame % mPieces[p]->mTextures[l]->mFrames];
				mPieces[p]->mTextures[l]->mTexture->mScaleV = mPieces[p]->mTextures[l]->mTextureVScale[frame % mPieces[p]->mTextures[l]->mFrames];
				ok = mPieces[p]->mTextures[l]->mTexture->Activate();
				mPieces[p]->mTextures[l]->mTexture->Select();
			}

			if (ok)
			{
				if ((l) && (!sLayerState[l]) && (LT.D3D_ValidateDevice() < 0))
				{
					SETUP_HISTORY.AddMessage("Mesh layer %d disabled", l);
					sLayerState[l] = 2;
				}
				else
				{
					sLayerState[l] = 1;
					if ((mPieces[p]->mTextures[l]->mTexture->mLit) && (!l) && (CMeshRenderer::GetRenderMode() != 4))
					{
						// self-illuminated: the texture's alpha lights it
						if (SELF_ILLUMINATION_METHOD.Try(1))
						{
							LT.SRS(D3DRS_ALPHABLENDENABLE, l);
							LT.SRS(D3DRS_ALPHATESTENABLE, l);
							LT.STS(1, D3DTSS_COLOROP, D3DTOP_BLENDTEXTUREALPHA);
							LT.STS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
							LT.STS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
							LT.STS(1, D3DTSS_TEXCOORDINDEX, l);
							LT.D3D_SetTexture(1, mPieces[p]->mTextures[0]->mTexture->GetTexture());
							if (SELF_ILLUMINATION_METHOD.Validate())
							{
								LT.D3D_DrawIndexedPrimitive(GetD3DPrimitiveType(mPrimitiveType), 0, mPieces[p]->mNumVertices, 0, mPieces[p]->mNumPrimitives);
								SELF_ILLUMINATION_METHOD.Accept();
							}
							LT.ForceTS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
							LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
						}
						else
						{
							// in two passes: opaque, then the texture's alpha unlit over it
							LT.SRS(D3DRS_ALPHABLENDENABLE, l);
							LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
							RENDERINFO.Apply(false, false);
							LT.D3D_DrawIndexedPrimitive(GetD3DPrimitiveType(mPrimitiveType), 0, mPieces[p]->mNumVertices, 0, mPieces[p]->mNumPrimitives);
							DWORD	zwrite = PCLTShell::mRenderStates[D3DRS_ZWRITEENABLE];
							LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
							LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
							LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
							BOOL	lighting = RENDERINFO.mUnknown2ed;
							RENDERINFO.SetLightingEnabled(false);
							RENDERINFO.Apply(false, false);
							LT.D3D_DrawIndexedPrimitive(GetD3DPrimitiveType(mPrimitiveType), 0, mPieces[p]->mNumVertices, 0, mPieces[p]->mNumPrimitives);
							LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
							RENDERINFO.SetLightingEnabled(lighting != 0);
							RENDERINFO.Apply(false, false);
							LT.SRS(D3DRS_ZWRITEENABLE, zwrite);
						}
					}
					else
						LT.D3D_DrawIndexedPrimitive(GetD3DPrimitiveType(mPrimitiveType), 0, mPieces[p]->mNumVertices, 0, mPieces[p]->mNumPrimitives);
				}
			}
			if ((flags & 0x10) == 0x10)
				CAtmospherics::mSnowLayer->Deactivate(true);
			else
				mPieces[p]->mTextures[l]->mTexture->Deactivate(true);
		}

		if ((flags & 0x40) && (CMeshRenderer::GetRenderMode() != 4) && (mPieces[p]->mBuilt))
		{
			// the cloak: its texture scrolled over the part, added
			ENGINE.GetCloakTexture()->mMode = 2;
			ENGINE.GetCloakTexture()->Activate();
			ENGINE.GetCloakTexture()->Select();
			LT.SRS(D3DRS_LIGHTING, FALSE);
			float	t = ENGINE.GetRenderTime() / 10.0f;
			D3DXMATRIX	m(0.5f, 0, 0, 0,
						  0, -0.5f, 0, 0,
						  0, 0, 1.0f, 0,
						  t, t * 0.5f, 0, 1.0f);
			LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &m);
			LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
			RENDERINFO.Apply(true, false);
			LT.D3D_DrawIndexedPrimitive(GetD3DPrimitiveType(mPrimitiveType), 0, mPieces[p]->mNumVertices, 0, mPieces[p]->mNumPrimitives);
			ENGINE.GetCloakTexture()->Deactivate(true);
			ENGINE.GetCloakTexture()->mMode = 0;
			LT.SRS(D3DRS_LIGHTING, TRUE);
			LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		}

		if ((flags & 0x20) && (CMeshRenderer::GetRenderMode() != 4) && (mPieces[p]->mBuilt) && (HIT_EFFECT_METHOD.Try(1)))
		{
			// the hit effect: its texture scrolled over the part, added at the hit's strength
			SINT	old = ScaleMeshAlpha(gMeshRendererHitEffect);
			ENGINE.GetHitEffectTexture()->mMode = 2;
			ENGINE.GetHitEffectTexture()->Activate();
			ENGINE.GetHitEffectTexture()->Select();
			LT.SRS(D3DRS_LIGHTING, FALSE);
			float	t = ENGINE.GetRenderTime() / 5.0f;
			D3DXMATRIX	m(0.5f, 0, 0, 0,
						  0, -0.5f, 0, 0,
						  0, 0, 1.0f, 0,
						  t, t * 0.5f, 0, 1.0f);
			LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &m);
			LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
			RENDERINFO.Apply(true, true);
			// Both outcomes deactivate the texture. VC6 merges the two calls (0054e008), but the longer draw block colours the
			// piece pointer before the primitive type, as in retail (eax and ecx, 0054dfc5..0054dfed).
			if (HIT_EFFECT_METHOD.Validate())
			{
				HIT_EFFECT_METHOD.Accept();
				LT.D3D_DrawIndexedPrimitive(GetD3DPrimitiveType(mPrimitiveType), 0, mPieces[p]->mNumVertices, 0, mPieces[p]->mNumPrimitives);
				ENGINE.GetHitEffectTexture()->Deactivate(true);
			}
			else
				ENGINE.GetHitEffectTexture()->Deactivate(true);
			ENGINE.GetHitEffectTexture()->mMode = 0;
			MESH_ALPHA = old;
			LT.SRS(D3DRS_LIGHTING, TRUE);
			LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		}
	}
	MESH_ALPHA = alpha;
	if ((mShader) && (CVertexShader::mSoftware))
		LT.SetSoftwareVertexProcessing(false);
	LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
}

//******************************************************************************************
// The buffers as the resource file holds them: the object itself, then each piece's counts, index buffer, vertices
// (piece 0's; the others share them) and textures.
#line 1728
void CDXMeshVB::Load(CChunkReader *c, BOOL skinned)	// FUNCTION: 0x0054e160
{
	memset(sLayerState, 0, sizeof(sLayerState));
	sLayerState[3] = 2;
	sLayerState[5] = 2;

	UINT		pos = c->WhereAmI();
	DeviceObject	*next = mNext;
	CMeshPart	*part = mPart;
	void		*vtable = *(void **)this;

	c->GetNext();
	c->GetNext();
	for (SINT j = 0; j < 64; j++)
		SAFE_DELETE(mPieces[j]);
	c->Read(this, sizeof(CDXMeshVB), 1);
	mNext = next;
	mPart = part;
	mFilePos = pos;
	char	*name = c->GetMemBuffer()->mFileName;
#line 1742
	mFileName = new(MEMTYPE_MESHVB) char[strlen(name) + 1];
	strcpy(mFileName, name);
	*(void **)this = vtable;

	for (SINT i = 0; i < mNumPieces; i++)
	{
#line 1756
		mPieces[i] = new(MEMTYPE_MESHVB) CMeshVBPiece;
		c->GetNext();
		c->Read(&mPieces[i]->mVBSize, 4, 1);
		c->Read(&mPieces[i]->mIBSize, 4, 1);
		c->Read(&mPieces[i]->mNumIndices, 4, 1);
		c->Read(&mPieces[i]->mNumVertices, 4, 1);
		c->Read(&mPieces[i]->mNumPrimitives, 4, 1);
		c->Read(&mPieces[i]->mBuilt, 4, 1);
		c->GetNext();

		DWORD	usage = D3DUSAGE_WRITEONLY;
		if ((CVertexShader::mSoftware) && (skinned))
			usage = D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING;

		LT.D3D_CreateIndexBuffer(mPieces[i]->mIBSize, usage, D3DFMT_INDEX16, D3DPOOL_MANAGED, &mPieces[i]->mIB);
		MEM_MANAGER.TagMem(this, mPieces[i]->mIBSize, "Meshes/%s/Part %d/VBPart %d/Deserialized index buffer", mPart->mBaseMesh->mName, mPart->mPartNumber, i);
		void	*data;
		mPieces[i]->mIB->Lock(0, 0, &data, 0);
		c->Read(data, 2, mPieces[i]->mNumIndices);
		mPieces[i]->mIB->Unlock();
		c->GetNext();

		if (i == 0)
		{
			LT.D3D_CreateVertexBuffer(mPieces[0]->mVBSize, usage, mFVF, D3DPOOL_MANAGED, &mPieces[0]->mVB);
			MEM_MANAGER.TagMem(this, mPieces[0]->mVBSize, "Meshes/%s/Part %d/VBPart %d/Deserialized vertex buffer", mPart->mBaseMesh->mName, mPart->mPartNumber, 0);
			void	*verts;
			mPieces[0]->mVB->Lock(0, 0, &verts, 0);
			c->Read(verts, mStride, mPieces[0]->mNumVertices);
			mPieces[0]->mVB->Unlock();
		}
		else
		{
			mPieces[i]->mVBSize = mPieces[0]->mVBSize;
			mPieces[i]->mVB = mPieces[0]->mVB;
			mPieces[i]->mNumVertices = mPieces[0]->mNumVertices;
			c->Skip();
		}
		c->GetNext();

		for (SINT t = 0; t < 6; t++)
		{
			SINT	index;
			c->Read(&index, 4, 1);
			if (index == -1)
				mPieces[i]->mTextures[t] = NULL;
			else
				mPieces[i]->mTextures[t] = &part->mBaseMesh->mTextures[index];
		}
	}
}
// STAND-IN (link layout): retail's 24 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_DXMeshVB[24] = { 0 };
