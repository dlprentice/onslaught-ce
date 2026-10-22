// renderqueueitem.h: a render-queue item. Kept out of renderqueue.h, which game.cpp includes: one more declaration
// there flips CGame::DrawDebugStuff (00470650) through the declaration-count tie-break.

#ifndef RENDERQUEUEITEM_H
#define RENDERQUEUEITEM_H

// A queued draw (0x80 bytes; field names from RenderBucket 005515e0).
struct SRenderQueueItem
{
	IDirect3DVertexBuffer9	*mVB;			// +0x00
	IDirect3DIndexBuffer9	*mIB;			// +0x04
	SINT			mPrimType;				// +0x08
	CDXTexture		*mTexture;				// +0x0c
	SINT			mNumVerts;				// +0x10
	SINT			mNumPrims;				// +0x14
	CVertexShader	*mShader;				// +0x18
	DWORD			mFVF;					// +0x1c
	SINT			mUnknown20;				// +0x20
	SINT			mStride;				// +0x24
	float			mU, mV, mUScale, mVScale;	// +0x28: the texture transform
	DWORD			mAlpha;					// +0x38: the texture factor's alpha
	FVector			mPos;					// +0x3c
	FMatrix			mOri;					// +0x4c
	SRenderQueueItem	*mNext;				// +0x7c
};

#endif
