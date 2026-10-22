// Cockpit.h: the battle engine's cockpit (Stuart's BattleEngine.h includes Cockpit.h; his header is not in the
// release). CCockpit is a CMonitor and an IRenderableThing (vtables 005d9524 and +8 005d94ac); on the PC the cockpit
// is a CDXCockpit (0x130 bytes, vtables 005d88b0 and +8 005d8838), built inline on CCockpit's constructor
// (CBattleEngine::Init 004055c2..004055e7).

#ifndef COCKPIT_H
#define COCKPIT_H

#include "Monitor.h"
#include "renderablething.h"

class CBattleEngine;
class CRenderThing;
class CResourceAccumulator;

class CCockpit : public CMonitor, public IRenderableThing
{
public:
	CCockpit(CBattleEngine *engine);								// 004244b0
	virtual void	HandleEvent(CEvent *event);						// 0 (00424a00)
	virtual			~CCockpit();									// 1 (CDXCockpit's 00405970)
	virtual SINT	Unknown3() { return 10001; }					// 3 (00405900, inline in BattleEngine.obj)
	virtual CRenderThing	*GetRenderThing();						// 4 (004258f0: returns mRenderThing; Stuart's name, CBattleEngine 0040c9bd)
	virtual void	AccumulateResources(CResourceAccumulator *accumulator);	// 5 (00425900: mRenderThing's slot 26; Stuart's name)
	void			AddShockShake(float amount);					// Stuart's (BattleEngine.cpp); not placed yet
	void			MorphIntoWalkerCockpit();						// 00424920 (Stuart's; CBattleEngine::Morph 0040a663)
	void			MorphIntoJetCockpit();							// 00424990 (Stuart's; CBattleEngine::Morph 0040a74c)

	// IRenderableThing
	virtual	FVector			GetRenderPos();						// 0
	virtual	FMatrix			GetRenderOrientation();				// 1
	virtual	BOOL			GetRenderSelected();				// 2
	virtual	SINT			GetRenderColourOffset();			// 3
	virtual	DWORD			GetRenderColour();					// 4
	virtual float			GetRenderTurn();					// 5
	virtual float			GetRenderFrame();					// 6
	virtual SINT			GetRealAnimIndex();	// 7 (00405960, inline in BattleEngine.obj)
	virtual	float			GetRenderRadius();					// 8
	virtual	FVector			GetRenderStartPos();				// 9
	virtual	FVector			GetRenderEndPos();					// 10
	virtual	SINT			GetRenderMesh();					// 11
	virtual	BOOL			GetRenderRotateShadow();			// 12
	virtual BOOL			GetRenderCanGoFlatShaded();			// 13
	virtual	BOOL			RenderUseHierarchy(SINT pn);		// 14
	virtual	float			GetRenderYaw();						// 15
	virtual float			GetRenderScale();					// 16
	virtual BOOL			GetRenderWithTransforms();			// 17
	virtual bool			GetCanBeImpostered();				// 18
	virtual int				GetImposterFrames();				// 19
	virtual EAnimMode		GetImposterAnimMode();				// 20
	virtual CBoundingBox	*GetBoundingBox();					// 21
	virtual EAnimMode		GetRenderAnimation();	// 22 (00405950, inline in BattleEngine.obj)
	virtual float			GetRenderImposterBias();			// 23
	virtual	SINT			GetSnowDensity();				// 24
	virtual	BOOL			GetRequiresPolyBucket();			// 25
	virtual int				GetShouldDoHitEffect(SINT mesh_part_no);	// 26
	virtual int				GetRenderImposterNo();					// 27
	virtual CMotionController	*GetMotionController();			// 28

	void			Process();										// 00424a20: event 2001, every frame
	void			ProcessAnimation();								// 00424be0
	void			ProcessWalker();								// 00424ca0
	void			ProcessJet();									// 004250f0

	FVector			mShakePos;										// +0x0c
	FVector			mOldShakePos;									// +0x1c
	FMatrix			mOrientation;									// +0x2c
	FMatrix			mOldOrientation;								// +0x5c
	CRenderThing	*mRenderThing;									// +0x8c
	float			mShake[4];										// +0x90 (AddShockShake 004247a0)
	float			mShakeTime;										// +0xa0
	float			mUnknownA4;										// +0xa4
	float			mUnknownA8;										// +0xa8
	int				mUnknownAC;										// +0xac
	FMatrix			mBaseOrientation;								// +0xb0
	FMatrix			mUnknownE0;										// +0xe0
	CBattleEngine	*mBattleEngine;									// +0x110
	int				mMorphState;									// +0x114: 1 into walker, 2 into jet
	int				mRealAnimIndex;									// +0x118
	EAnimMode		mAnimMode;										// +0x11c
	float			mFrameIncrement;								// +0x120
	float			mFrame;											// +0x124
	float			mOldFrame;										// +0x128
	BOOL			mShouldRender;									// +0x12c (Stuart's; CBattleEngine toggles it, 0040e84a)
};

// CCockpit's render answers, inline after the class in retail's emission order (BattleEngine.obj 00405910..00405960)
inline SINT	CCockpit::GetRenderMesh() { return -1; }
inline BOOL	CCockpit::RenderUseHierarchy(SINT pn) { return TRUE; }
inline BOOL	CCockpit::GetRenderSelected() { return FALSE; }
inline EAnimMode	CCockpit::GetRenderAnimation() { return mAnimMode; }
inline SINT	CCockpit::GetRealAnimIndex() { return mRealAnimIndex; }

class CDXCockpit : public CCockpit
{
public:
	CDXCockpit(CBattleEngine *engine) : CCockpit(engine) {}
	void	Render();						// 0053bb50: called directly (CDXEngine::Render 0053ec6a)
};

#define CCOCKPIT CDXCockpit

#endif
