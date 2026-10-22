// Lights.h: the light manager (Lights.obj, 004900f0..004907c1; Stuart's Lights.cpp is not in his release). Explosions
// and muzzle flashes log a light request near a camera; each frame the strongest requests take over the weakest of six
// fading point lights, which go to the render state's lights 2..7.

#ifndef LIGHTS_H
#define LIGHTS_H

enum eLightType
{
	LT_UNKNOWN0 = 0							// white, fading over ten frames; types 1..4 are red, orange, yellow and blue
};

class C3DGamut;

#ifdef LIGHTS_FULL	// Lights.cpp's view; the other objects keep the short one below
#define MAX_LIGHT_REQS		16
#define NUM_LIGHT_SLOTS		6

// A frame's request (0x1c bytes).
struct SLightReq
{
	float		mRadius;					// +0x00: the caller's radius times 15
	eLightType	mType;						// +0x04
	float		mCloseness;					// +0x08: 1 / the squared distance to the nearest camera
	FVector		mPos;						// +0x0c
};

// A fading light (0x74 bytes).
struct SLightSlot
{
	float		mStrength;					// +0x00
	float		mFade;						// +0x04: added to the strength each frame
	SINT		mLife;						// +0x08: frames left
	float		mStepR, mStepG, mStepB;		// +0x0c: added to the colour each frame
	CLight		mLight;						// +0x18
};

// 0x47c bytes (CEngine::Init 00449c8b); its destructor is out of line and empty (folded into 0040c640).
class CLightManager
{
public:
				CLightManager();				// 00490220
				~CLightManager();
	BOOL		Init();							// 00490280
	void		AddReq(const FVector &pos, C3DGamut *gamut, eLightType lt, float radius);	// 004902b0
	void		UseReq(SINT slot, SINT req);	// 004903a0
	void		Process();						// 004905f0 (CDXEngine::Render 0053e5c0)
	void		SwitchAll(BOOL onoff);			// 00490780

	SLightReq	mReqs[MAX_LIGHT_REQS];			// +0x000
	SINT		mNumReqs;						// +0x1c0
	SLightSlot	mSlots[NUM_LIGHT_SLOTS];		// +0x1c4
};
#else
class CLightManager
{
public:
				CLightManager();				// 00490220
				~CLightManager();
	BOOL		Init();							// 00490280
	void		AddReq(const FVector &pos, C3DGamut *gamut, eLightType lt, float radius);	// 004902b0
	void		SwitchAll(BOOL onoff);			// 00490780
	void		Process();						// 004905f0 (CDXEngine::Render 0053e5c0)

	UBYTE		mUnknown00[0x47c];
};
#endif

#endif
