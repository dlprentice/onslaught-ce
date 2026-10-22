// FrontEndThing.h: a mesh the front-end pages show and spin (the Battle Engine on the configuration, multiplayer and
// wingmen pages), reconstructed from the retail PC executable; Stuart's file is not in his release. CFrontEndThing
// (vtable 005db2c4) renders through IRenderableThing; its inline slots are emitted by FEPBEConfig.obj (0044fd10..).

#ifndef FRONTENDTHING_H
#define FRONTENDTHING_H

#include "RenderThing.h"

// what a page initializes one from (CFEPBEConfig::Init builds it on the stack, 0044fb97)
struct SFrontEndThingInit
{
	FVector			mPos;							// +0x00
	FMatrix			mOri;							// +0x10
	char			mName[200];					// +0x40: 200 (the frames of CFEPMultiplayerStart::Init 0051dba0 and CFEPBEConfig::Init 0044fa90 end there)
};

class CFrontEndThing : public IRenderableThing
{
public:
	~CFrontEndThing();								// 0046ba90

	void			Init(SFrontEndThingInit *init);	// 0046b950
	void			SetAnimation(char *name);		// 0046bab0
	void			Process();						// 0046baf0: spins, and runs the animation forwards and back
	void			Render();						// 0046bc20

	// FUNCTION: 0x0044fd70
	virtual	FVector			GetRenderPos() { return mPos; }
	// FUNCTION: 0x0044fd10
	virtual	FMatrix			GetRenderOrientation() { return mOri; }
	virtual	BOOL			GetRenderSelected() { return FALSE; }
	virtual	SINT			GetRenderColourOffset() { return 0; }
	virtual	DWORD			GetRenderColour() { return 0xff000080; }
	virtual float			GetRenderTurn() { return 1.f; }
	// FUNCTION: 0x0044fd30
	virtual float			GetRenderFrame() { return mFrame; }
	// FUNCTION: 0x0044fd40
	virtual SINT			GetRealAnimIndex() { return mAnim; }
	// FUNCTION: 0x0044fd50
	virtual	float			GetRenderRadius() { return mRenderThing ? mRenderThing->GetRTRadius() : 1.f; }
	virtual	FVector			GetRenderStartPos() { return mPos; }
	virtual	FVector			GetRenderEndPos() { return mPos; }
	virtual	SINT			GetRenderMesh() { return -1; }
	virtual	BOOL			GetRenderRotateShadow() { return FALSE; }
	virtual BOOL			GetRenderCanGoFlatShaded() { return FALSE; }
	virtual	BOOL			RenderUseHierarchy(SINT pn) { return TRUE; }
	virtual	float			GetRenderYaw() { return 0.f; }
	virtual float			GetRenderScale() { return 1.f; }
	virtual BOOL			GetRenderWithTransforms() { return TRUE; }
	virtual bool			GetCanBeImpostered() { return false; }
	virtual int				GetImposterFrames() { return 1; }
	virtual EAnimMode		GetImposterAnimMode() { return AM_NONE; }
	virtual CBoundingBox	*GetBoundingBox() { return NULL; }
	virtual EAnimMode		GetRenderAnimation() { return AM_NONE; }
	virtual float			GetRenderImposterBias() { return 1.f; }
	virtual	SINT			GetSnowDensity() { return 0; }
	virtual	BOOL			GetRequiresPolyBucket() { return FALSE; }
	virtual int				GetShouldDoHitEffect(SINT mesh_part_no) { return 0; }
	virtual int				GetRenderImposterNo() { return 0; }
	virtual CMotionController	*GetMotionController() { return NULL; }

	FVector			mPos;							// +0x04
	FMatrix			mOri;							// +0x14
	SINT			mAnim;							// +0x44
	float			mFrameIncrement;				// +0x48
	float			mFrame;							// +0x4c
	BOOL			mForwards;						// +0x50
	int				mCycles;						// +0x54: frames left before the direction turns
	CRenderThing	*mRenderThing;					// +0x58
};

#endif
