// ScreenOverlay.h: a mesh a cutscene shows over the HUD, playing its "Effect" animation once (reconstructed;
// ScreenOverlay.obj, 004de3a0..004de8b3). RTTI: CScreenOverlay renders through IRenderableThing (vtable 005dec20,
// 30 slots) and is a CMonitor at +0x04 (vtable 005dec10). 0x68 bytes (CHud::SetHudComponent 00481f92).

#ifndef SCREENOVERLAY_H
#define SCREENOVERLAY_H

#include "Monitor.h"
#include "renderablething.h"

class CRenderThing;

#define SCREEN_OVERLAY_EVENT	4000		// the per-frame step (004de7d5)

class CScreenOverlay : public IRenderableThing, public CMonitor
{
public:
	CScreenOverlay(char *name);						// 004de3a0
	virtual			~CScreenOverlay();				// CMonitor slot 1 (004de760)
	virtual void	HandleEvent(CEvent *event);		// CMonitor slot 0 (004de7d0)

	void			Finish();						// 004de850
	void			Render();						// 004de860 (CHud::RenderOverlay 0048815f)
	BOOL			IsFinished() { return mFinished; }

	// IRenderableThing
	virtual SINT			Unknown29()					{ return 10002; }
	virtual	FVector			GetRenderPos()				{ return mPos; }			// 0 (004de6b0)
	virtual	FMatrix			GetRenderOrientation()		{ return mOrientation; }	// 1 (004de6e0)
	virtual	BOOL			GetRenderSelected()			{ return FALSE; }			// 2
	virtual	SINT			GetRenderColourOffset()		{ return 0; }				// 3
	virtual	DWORD			GetRenderColour()			{ return 0xffffffff; }		// 4
	virtual float			GetRenderTurn()				{ return 1.f; }				// 5
	virtual float			GetRenderFrame();										// 6 (004de8b0)
	virtual	float			GetRenderRadius()			{ return 1.f; }				// 8
	virtual	FVector			GetRenderStartPos()			{ return mPos; }			// 9
	virtual	FVector			GetRenderEndPos()			{ return mPos; }			// 10
	virtual	SINT			GetRenderMesh()				{ return -1; }				// 11
	virtual	BOOL			GetRenderRotateShadow()		{ return FALSE; }			// 12
	virtual BOOL			GetRenderCanGoFlatShaded()	{ return FALSE; }			// 13
	virtual	BOOL			RenderUseHierarchy(SINT pn)	{ return TRUE; }			// 14
	virtual	float			GetRenderYaw()				{ return 0.f; }				// 15
	virtual float			GetRenderScale()			{ return 1.f; }				// 16
	virtual BOOL			GetRenderWithTransforms()	{ return FALSE; }			// 17
	virtual bool			GetCanBeImpostered()		{ return false; }			// 18
	virtual int				GetImposterFrames()			{ return 1; }				// 19
	virtual EAnimMode		GetImposterAnimMode()		{ return (EAnimMode)0; }	// 20
	virtual CBoundingBox	*GetBoundingBox()			{ return NULL; }			// 21
	virtual float			GetRenderImposterBias()		{ return 1.f; }				// 23
	virtual EAnimMode		GetRenderAnimation()		{ return mAnimMode; }		// 22 (004de710)
	virtual SINT			GetRealAnimIndex()			{ return mRealAnimIndex; }	// 7 (004de720)
	virtual	SINT			GetSnowDensity()		{ return 0; }				// 24
	virtual	BOOL			GetRequiresPolyBucket()		{ return FALSE; }			// 25
	virtual int				GetShouldDoHitEffect(SINT mesh_part_no)	{ return 0; }	// 26
	virtual int				GetRenderImposterNo()			{ return 0; }				// 27
	virtual CMotionController	*GetMotionController()	{ return NULL; }			// 28
	// 29 (004de6a0): the overlay's own slot. Open question: nothing in the retail executable calls it through this
	// table; the constant may be a type id.

	FVector			mPos;							// +0x0c: (0, 2, 0)
	FMatrix			mOrientation;					// +0x1c: identity
	CRenderThing	*mRenderThing;					// +0x4c
	SINT			mUnknown50;						// +0x50
	SINT			mRealAnimIndex;					// +0x54: GetFrameIncrement's out-parameter
	EAnimMode		mAnimMode;						// +0x58: the mesh's "Effect"
	float			mFrameIncrement;				// +0x5c
	float			mFrame;							// +0x60: stops at 0.99
	bool			mFinished;						// +0x64 (CHud::RenderOverlay 0048816a)
	bool			mDeleteSelf;					// +0x65: set by Finish; the next step deletes the overlay
};

#endif
