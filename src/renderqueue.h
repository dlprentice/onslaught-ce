// renderqueue.h: the render queue (renderqueue.obj, 00551570..00551ace). When cg_userenderqueue is on, draws are
// queued (the items are allocated by their callers) and drawn in one pass with the state changes between them kept
// to a minimum; each item is freed once drawn.

#ifndef RENDERQUEUE_H
#define RENDERQUEUE_H

#ifdef RENDERQUEUE_FULL	// renderqueue.cpp's view; game.cpp keeps the short one (its header-composition tie)
class CDXTexture;
class CVertexShader;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;

struct SRenderQueueItem;					// renderqueueitem.h
#endif

class CRenderQueue
{
public:
	void	Init();								// 005515a0
	void	Reset();							// 005515d0 (CDXEngine::Render 0053e4da)
#ifdef RENDERQUEUE_FULL
	void	RenderBucket(SINT n);				// 005515e0
#endif
	void	Render();							// 00551920

	bool	mUseQueue;							// +0x00 (CDXEngine::PostRender 0053eeb0)
#ifdef RENDERQUEUE_FULL
	SRenderQueueItem	*mItems[1];				// +0x04
	CDXTexture		*mTexture;					// +0x08: the state the last item set
	DWORD			mFVF;						// +0x0c
	CVertexShader	*mShader;					// +0x10
	DWORD			mAlpha;						// +0x14
	float			mU, mV, mUScale, mVScale;	// +0x18
	FVector			mPos;						// +0x28
	FMatrix			mOri;						// +0x38
#endif
};

extern CRenderQueue RENDERQUEUE;				// 009c7490

#endif
