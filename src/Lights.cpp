// Lights.cpp: reconstructed from Lights.obj (004900f0..004907c1); Stuart's release has no copy.

#include "common.h"
#define LIGHTS_FULL
#include "mathsconst.h"
#include "DX.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "Lights.h"
#include "engine.h"
#include "Camera.h"
#include "map.h"

// FUNCTION: 0x004900f0 init:ZERO_FVECTOR
// FUNCTION: 0x00490110 init:ID_FMATRIX

//******************************************************************************************
void	CLight::Init(ELightType type, FVector dir, float r, float g, float b)	// FUNCTION: 0x004901e0
{
	mType = type;
	mDir = dir;
	mDiffuseR = r;
	mDiffuseG = g;
	mDiffuseB = b;
}

//******************************************************************************************
CLightManager::CLightManager()	// FUNCTION: 0x00490220
{
}

//******************************************************************************************
CLightManager::~CLightManager()
{
}

//******************************************************************************************
BOOL	CLightManager::Init()	// FUNCTION: 0x00490280
{
	mNumReqs = 0;
	memset(mSlots, 0, sizeof(mSlots));
	return TRUE;
}

//******************************************************************************************
void	CLightManager::AddReq(const FVector &pos, C3DGamut *gamut, eLightType lt, float radius)	// FUNCTION: 0x004902b0
{
	if (mNumReqs >= MAX_LIGHT_REQS)
		return;

	float	nearest = 100.f;
	SINT	n = ENGINE.GetNumViewpoints();
	for (SINT c0 = 0; c0 < n; c0++)
	{
		FVector	d = ENGINE.GetCameraForViewpoint(c0)->GetPos() - pos;
		float	dist = d.MagnitudeSq();
		if (dist < nearest)
			nearest = dist;
	}

	if (nearest < 100.f)
	{
		SLightReq	&r = mReqs[mNumReqs++];
		r.mPos = pos;
		r.mType = lt;
		r.mCloseness = 1.f / nearest;
		r.mRadius = radius * 15.f;
	}
}

//******************************************************************************************
void	CLightManager::UseReq(SINT slot, SINT req)	// FUNCTION: 0x004903a0
{
	SLightSlot	&s = mSlots[slot];
	SLightReq	&r = mReqs[req];

	memset(&s.mLight, 0, sizeof(CLight));
	s.mLight.mPos.X = r.mPos.X;
	s.mLight.mPos.Y = r.mPos.Y;
	float	h = MAP.Collide(r.mPos) - 0.1f;
	if (!(h < r.mPos.Z))
		h = r.mPos.Z;
	s.mLight.mPos.Z = h;

	switch (r.mType)
	{
	case 0:
		s.mLight.mType = (ELightType)1;
		s.mLight.mDiffuseR = 1.f;
		s.mLight.mDiffuseG = 1.f;
		s.mLight.mDiffuseB = 1.f;
		s.mLight.mUnknown30 = r.mRadius;
		s.mLife = 10;
		s.mFade = s.mStrength / -s.mLife;
		s.mStepR = (0.f - s.mLight.mDiffuseR) / s.mLife;
		s.mStepG = (0.f - s.mLight.mDiffuseG) / s.mLife;
		s.mStepB = (0.f - s.mLight.mDiffuseB) / s.mLife;
		RENDERINFO.SetLight(slot + 2, &s.mLight);
		RENDERINFO.SetLightEnable(slot + 2, true);
		break;
	case 1:
		s.mLight.mType = (ELightType)1;
		s.mLight.mDiffuseR = 1.f;
		s.mLight.mDiffuseG = 0.f;
		s.mLight.mDiffuseB = 0.f;
		s.mLight.mUnknown30 = r.mRadius;
		s.mLife = 5;
		s.mFade = s.mStrength / -s.mLife;
		s.mStepR = (0.f - s.mLight.mDiffuseR) / s.mLife;
		s.mStepG = (0.f - s.mLight.mDiffuseG) / s.mLife;
		s.mStepB = (0.f - s.mLight.mDiffuseB) / s.mLife;
		RENDERINFO.SetLight(slot + 2, &s.mLight);
		RENDERINFO.SetLightEnable(slot + 2, true);
		break;
	case 2:
		s.mLight.mType = (ELightType)1;
		s.mLight.mDiffuseR = 1.f;
		s.mLight.mDiffuseG = 0.5f;
		s.mLight.mDiffuseB = 0.f;
		s.mLight.mUnknown30 = r.mRadius;
		s.mLife = 5;
		s.mFade = s.mStrength / -s.mLife;
		s.mStepR = (0.f - s.mLight.mDiffuseR) / s.mLife;
		s.mStepG = (0.f - s.mLight.mDiffuseG) / s.mLife;
		s.mStepB = (0.f - s.mLight.mDiffuseB) / s.mLife;
		RENDERINFO.SetLight(slot + 2, &s.mLight);
		RENDERINFO.SetLightEnable(slot + 2, true);
		break;
	case 3:
		s.mLight.mType = (ELightType)1;
		s.mLight.mDiffuseR = 1.f;
		s.mLight.mDiffuseG = 1.f;
		s.mLight.mDiffuseB = 0.f;
		s.mLight.mUnknown30 = r.mRadius;
		s.mLife = 5;
		s.mFade = s.mStrength / -s.mLife;
		s.mStepR = (0.f - s.mLight.mDiffuseR) / s.mLife;
		s.mStepG = (0.f - s.mLight.mDiffuseG) / s.mLife;
		s.mStepB = (0.f - s.mLight.mDiffuseB) / s.mLife;
		RENDERINFO.SetLight(slot + 2, &s.mLight);
		RENDERINFO.SetLightEnable(slot + 2, true);
		break;
	case 4:
		s.mLight.mType = (ELightType)1;
		s.mLight.mDiffuseR = 0.f;
		s.mLight.mDiffuseG = 0.5f;
		s.mLight.mDiffuseB = 1.f;
		s.mLight.mUnknown30 = r.mRadius;
		s.mLife = 5;
		s.mFade = s.mStrength / -s.mLife;
		s.mStepR = (0.f - s.mLight.mDiffuseR) / s.mLife;
		s.mStepG = (0.f - s.mLight.mDiffuseG) / s.mLife;
		s.mStepB = (0.f - s.mLight.mDiffuseB) / s.mLife;
		RENDERINFO.SetLight(slot + 2, &s.mLight);
		RENDERINFO.SetLightEnable(slot + 2, true);
		break;
	}
}

//******************************************************************************************
struct SLightSort
{
	SINT	mIndex;
	SINT	mKey;
};

int __cdecl CompareLights(const void *a, const void *b)	// FUNCTION: 0x004905d0
{
	return ((SLightSort *)a)->mKey - ((SLightSort *)b)->mKey;
}

//******************************************************************************************
// Fade the slots, then give the strongest requests the weakest slots.
// Retail passes the sorted positions themselves to UseReq, rather than the saved entry indices.
void	CLightManager::Process()	// FUNCTION: 0x004905f0
{
	SLightSort	slots[NUM_LIGHT_SLOTS];
	SLightSort	reqs[MAX_LIGHT_REQS];
	SINT		c0;

	SLightSlot *s = mSlots;
	for (c0 = 0; c0 < NUM_LIGHT_SLOTS; s++, c0++)
	{
		if (s->mLife)
		{
			if (--s->mLife)
			{
				s->mStrength += s->mFade;
				s->mLight.mDiffuseR += s->mStepR;
				s->mLight.mDiffuseG += s->mStepG;
				s->mLight.mDiffuseB += s->mStepB;
				RENDERINFO.SetLightEnable(c0 + 2, false);
				RENDERINFO.SetLight(c0 + 2, &s->mLight);
				RENDERINFO.SetLightEnable(c0 + 2, true);
			}
			else
			{
				s->mStrength = 0;
				RENDERINFO.SetLightEnable(c0 + 2, false);
			}
		}
		slots[c0].mKey = (SINT)(s->mStrength * 65536.f);
		slots[c0].mIndex = c0;
	}

	if (mNumReqs)
	{
		qsort(slots, NUM_LIGHT_SLOTS, sizeof(SLightSort), CompareLights);
		for (c0 = 0; c0 < mNumReqs; c0++)
		{
			reqs[c0].mKey = (SINT)(mReqs[c0].mCloseness * 65536.f);
			reqs[c0].mIndex = c0;
		}
		qsort(reqs, mNumReqs, sizeof(SLightSort), CompareLights);

		SINT	slot = 0;
		for (SINT req = mNumReqs - 1; req >= 0; slot++, req--)
		{
			if (slot >= NUM_LIGHT_SLOTS || reqs[req].mKey <= slots[slot].mKey)
				break;
			UseReq(slot, req);
		}
		mNumReqs = 0;
	}
}

//******************************************************************************************
void	CLightManager::SwitchAll(BOOL onoff)	// FUNCTION: 0x00490780
{
	RENDERINFO.SetLightEnable(0, onoff != 0);
	for (SINT c0 = 0; c0 < 8; c0++)
		if (mSlots[c0].mLife)
			RENDERINFO.SetLightEnable(c0 + 1, onoff != 0);
}
