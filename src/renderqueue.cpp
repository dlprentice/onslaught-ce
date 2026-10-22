// renderqueue.cpp: reconstructed from renderqueue.obj (00551570..00551ace); Stuart's release has no copy.
#include "common.h"
#include "ltshell.h"
#include "console.h"
#include "renderinfo.h"
#define RENDERQUEUE_FULL
#include "renderqueue.h"
#include "renderqueueitem.h"
#include <d3dx9.h>
#include "mathsconst.h"			// FUNCTION: 0x00551ab0 init:ZERO_FVECTOR
								// FUNCTION: 0x00551ad0 init:ID_FMATRIX

static FVector	RENDERQUEUE_ZERO(0.0f, 0.0f, 0.0f);			// 009c7440 (only initialized)
// FUNCTION: 0x00551570 init:RENDERQUEUE_ZERO

CRenderQueue	RENDERQUEUE;		// its empty initializer is a lone ret	// FUNCTION: 0x00551590 initorder:4

//******************************************************************************************
void CRenderQueue::Init()	// FUNCTION: 0x005515a0
{
	mUseQueue = false;
	CONSOLE.RegisterVariable("cg_userenderqueue", "Use the render queue", CVar_bool, &mUseQueue);
	mItems[0] = NULL;
}

//******************************************************************************************
void CRenderQueue::Reset()	// FUNCTION: 0x005515d0
{
	mItems[0] = NULL;
}

//******************************************************************************************
void CRenderQueue::RenderBucket(SINT n)	// FUNCTION: 0x005515e0
{
	SRenderQueueItem *item = mItems[n];
	while (item)
	{
		if (item->mShader)
		{
			if (item->mShader != mShader)
			{
				LT.SetVertexShader(item->mShader);
				mShader = item->mShader;
			}
		}
		else if (item->mFVF != mFVF)
		{
			RENDERINFO.SetFVF(item->mFVF);
			mFVF = item->mFVF;
		}
		if (item->mTexture != mTexture)
		{
			LT.D3D_SetTexture(0, item->mTexture->GetTexture());
			mTexture = item->mTexture;
		}
		if (item->mAlpha != mAlpha)
		{
			LT.SRS(D3DRS_TEXTUREFACTOR, item->mAlpha << 24);
			mAlpha = item->mAlpha;
		}
		if (item->mU != mU || item->mV != mV || item->mUScale != mUScale || item->mVScale != mVScale)
		{
			float su = item->mUScale;
			float sv = item->mVScale;
			float u = item->mU;
			D3DXMATRIX t(su, 0, 0, 0,
						 0, sv, 0, 0,
						 u - (su - 1.0f) * 0.5f, (sv - 1.0f) * 0.5f + item->mV, 1.0f, 0,
						 0, 0, 0, 1.0f);
			RENDERINFO.Apply();
			LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &t);
			LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
			mU = item->mU;
			mV = item->mV;
			mUScale = item->mUScale;
			mVScale = item->mVScale;
		}
		if (item->mPos != mPos || item->mOri != mOri)
		{
			RENDERINFO.SetWorld(item->mPos, item->mOri);
			mPos = item->mPos;
			mOri = item->mOri;
		}
		LT.GetDevice()->SetStreamSource(0, item->mVB, 0, item->mStride);
		LT.GetDevice()->SetIndices(item->mIB);
		RENDERINFO.Apply();
		LT.D3D_DrawIndexedPrimitive((D3DPRIMITIVETYPE)item->mPrimType, 0, item->mNumVerts, 0, item->mNumPrims);
		SRenderQueueItem *next = item->mNext;
		delete item;
		item = next;
	}
	mItems[n] = NULL;
}

//******************************************************************************************
void CRenderQueue::Render()	// FUNCTION: 0x00551920
{
	if (mUseQueue)
	{
		mTexture = NULL;
		mUScale = 1.0f;
		mVScale = 1.0f;
		mFVF = 0;
		mShader = NULL;
		mAlpha = 0xffffffff;
		mU = 0;
		mV = 0;
		mPos = ZERO_FVECTOR;
		mOri = ID_FMATRIX;
		RENDERINFO.SetWorld(ZERO_FVECTOR, ID_FMATRIX);
		LT.SetModulate(0);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		LT.STS(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		LT.STS(1, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
		LT.STS(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
		RenderBucket(0);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
		LT.STS(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.STS(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
	}
}
