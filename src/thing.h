// thing.h: CThing and CComplexThing, the bases of every game object.
//
// Reconstructed against the retail RTTI tables (config/classes.tsv): CThing 005df5c8 (59 slots) with
// IRenderableThing at +8 (005df550, 29 slots); CComplexThing 005df784 (66) and 005df70c (29). Stuart's thing.h
// supplies names and bodies; the retail order differs where noted. Layout from CThing's constructor (004f33e0):
// map-who entry +0x0c, position +0x1c, flags +0x2c, thing number +0x2e, render thing +0x30, type +0x34,
// collision thing +0x38 (size 0x3c); CComplexThing adds orientation +0x3c, animation +0x6c, motion controller
// +0x70, mission script +0x74, name +0x78 (size 0x7c).

#ifndef THING_H
#define THING_H

#include	"mathsconst.h"
#include	"monitor.h"
#include	"ActiveReader.h"
#include	"AudibleThing.h"
#include	"renderablething.h"
#include	"mapwhoentry.h"
#include	"ThingType.h"
#include	"RenderThing.h"
#include	"InitThing.h"
#include	"animation.h"

class CCollisionSeekingThing ;
class CCSPersistentThing ;
class CMotionController;
struct CCollisionReport;
class CAnimation;
class IScript;
class CStringDataType;
class CDataType;
class CStaticShadow;
class CBoundingBox;

#define BATTLE_ENGINE_COFGHEIGHT 1.9f
#define CLOCK_TICK 0.05f			// Stuart's thing.h
#define GAME_FR 20.0f

// Every thing class names itself and its object id; the two virtuals follow the inherited ones (slots 7, 8).
#define DECLARE_THING_CLASS(name, super)								\
	class name : public super											\
	{																	\
	public:																\
		typedef super SUPERTYPE;										\
		virtual char	*_GetClassName() { return #name; }				\
		virtual int		_GetClassID() { return OID_##name; }

#define DECLARE_MULTI_INTERFACE_CLASS(name, base1, base2)				\
	class name : public base1, public base2								\
	{																	\
	public:																\
		virtual char	*_GetClassName() { return #name; }				\
		virtual int		_GetClassID() { return OID_##name; }

#include "oids.h"

// Sound materials (Stuart's SM_ names; only what matched code needs).
enum ESoundMaterial
{
	SM_BATTLE_ENGINE = 0x68,	// CBattleEngine::GetSoundMaterial (00405f40)
};

enum EThingEvent
{
	SHUTDOWN = 2000,
	INIT_SCRIPT,
	START_DIE_PROCESS,
	READY_SCRIPT
};

enum THING_FLAGS
{
	TF_DECLARED_SHUTDOWN = 1,
	TF_IN_MAP_WHO = 2,
	TF_DYING = 4,
	TF_DONT_RENDER = 8,
	TF_INVISIBLE = 16,
	TF_MARKED_OBJECTIVE = 32,
	TF_IS_BIG_THING = 64,
	TF_SLIDE	= 128,
	TF_REMOVED_UNIT_TYPE = 256
};

enum EAIState
{
	AI_ON=0,
	AI_OFF,
	AI_NORMAL,
	AI_DEFENSIVE,
	AI_ONF,
};

//******************************************************************************************
DECLARE_MULTI_INTERFACE_CLASS(CThing, IAudibleThing, IRenderableThing)
public:
	static void				ResetThingCounter() { sThingCount = 0; }	// inline (CGame::LoadLevel 0046ce43)
							CThing();
	virtual					~CThing();

	virtual void			Init(CInitThing* init)	;									// 9
	virtual BOOL			GetCanBeStaticallyShadowed()	{return(FALSE);};			// 10
	virtual CStaticShadow	*GetStaticShadow()				{return(NULL); };			// 11
	virtual void			SetStaticShadow(CStaticShadow *shadow) {};					// 12
	virtual float			GetCueFactor()					{ if (mRenderThing) return mRenderThing->GetCueFactor(); else return 0.0001f;}	// FUNCTION: 0x00401420
	virtual void			AddShutdownEvent() ;										// 14
	virtual float			GetMaxVelocity()	{ return 0.0f;}							// 15
	virtual float			GetRadius()       { if (mRenderThing) return mRenderThing->GetRTRadius(); else return 0.0f;}	// FUNCTION: 0x00401440
	virtual float			GetBoundingRadius() ;										// 17
	virtual void			StickToGround();											// 18
	virtual void			MoveTo(const FVector& pos) ;								// 19
	virtual void			Teleport(const FVector& pos);								// 20
	virtual void			TeleportOrientation(const FMatrix& orientation) {}			// 21
	virtual void			Activate() {}												// 22
	virtual void			Deactivate() {}												// 23
	virtual float			GetMoveMultiplier() { return 1.0f ; }						// 24
	virtual FVector			GetLocalLastFrameMovement() { return FVector(0.0f,0.0f,0.0f) ; }	// 25
	virtual BOOL			IsObjective() { return FALSE ;}								// 26
	virtual	FVector			GetVelocity() { return ZERO_FVECTOR;};						// 27
	virtual	void			SetVelocity(const FVector& vel) {};							// 28
	virtual	void			AddVelocity(const FVector& vel) {};							// 29
	virtual	FVector			GetOldPos() {return mPos;};									// 30
	virtual	FMatrix			GetRenderOrientation()			{return ID_FMATRIX;};
	virtual	FMatrix			GetSoundOrientation()			{return ID_FMATRIX;};
	virtual FMatrix			GetOldOrientation() {return ID_FMATRIX;};					// 31
	virtual void			MakeVisible()   {	mFlags &= ~TF_INVISIBLE ; }				// FUNCTION: 0x00401460
	virtual void			MakeInvisible() { mFlags |= TF_INVISIBLE ; }				// FUNCTION: 0x00401470
	virtual	void			InitRenderThing(CInitThing *init);							// 34
	virtual void			InitCollisionSeekingThing(CInitCSThing *init);				// 35
	virtual void			Render(DWORD flags=0);										// 36
	virtual	void			RenderImposter();											// 37
	virtual	void			SetThingType(ULONG t) { mThingType = (t | THING_TYPE_THING); }	// 38
	virtual void			Hit(CThing* other_thing, CCollisionReport* report) {}		// 39
	virtual void			Damage(float amount,CThing *inByThis,BOOL inDamageShields=TRUE, int mesh_part_no = -1) {}	// FUNCTION: 0x00401490
	virtual	char*			GetName() { return NULL ; }									// 41
	virtual void			SetName(char *inName)					{ }					// 42
	virtual SINT			GetSoundMaterial()						{ return 0; }		// 43
	virtual BOOL			ClipToGround()			{ return TRUE;}						// 44
	virtual float			Gravity()				{ return 0.01f;}					// FUNCTION: 0x004014b0
	virtual BOOL			ObeyGravity()			{ return TRUE; }					// 46
	virtual	float			BounceFactor()			{ return 0;}						// 47
	virtual	float			COfGHeight()			{ return 0;}						// 48
	virtual BOOL			CanGoUnderWater()		{ return FALSE; }					// 49
	virtual BOOL			StartDieProcess() ;											// 50
	virtual void			DrawDebugStuff(float &y_offset) ;							// 51
	virtual void			DrawDebugStuff3d();											// 52
	virtual EAIState		GetAIState()						{ return AI_ON; }		// 53
	virtual void			SetAIState(EAIState inAIState)		{}						// 54
	virtual void			AccumulateScore()				{ }							// 55
	virtual void			SetVulnerable(BOOL val)			{ }							// 56
	virtual BOOL			GetVulnerable()					{ return TRUE;}				// 57
	virtual void			IncreaseThingCounter(CInitThing *inInit) ;				// 58

	// IAudibleThing
	virtual	FVector			GetSoundPos()					{return mPos;};
	virtual FVector			GetSoundVelocity()				{return FVector(0,0,0);};
	virtual void			SampleFinishedPlaying(CSoundEvent* event) {}	// FUNCTION: 0x004014c0

	// CMonitor
	virtual	void			Shutdown() ;
	virtual void			HandleEvent(CEvent *event) ;

	// IRenderableThing
	virtual	FVector			GetRenderPos()					{return mPos;};
	virtual	BOOL			GetRenderSelected()				{return FALSE;};
	virtual	SINT			GetRenderColourOffset()			{return 0;};
	virtual	DWORD			GetRenderColour()				{return 0xff000080 ;}	// FUNCTION: 0x004013f0
	virtual float			GetRenderTurn()					{return 1;};
	virtual float			GetRenderFrame()				{return 0;};
	virtual SINT			GetRealAnimIndex()				{ return -1;};
	virtual	float			GetRenderRadius()				{ if (mRenderThing) return mRenderThing->GetRTRadius(); else return 1.0f;}	// FUNCTION: 0x00401400
	virtual	FVector			GetRenderStartPos()				{return mPos;};
	virtual	FVector			GetRenderEndPos()				{return mPos;};
	virtual	SINT			GetRenderMesh()					{return -1;};
	virtual	BOOL			GetRenderRotateShadow()			{return FALSE;};
	virtual BOOL			GetRenderCanGoFlatShaded()		{return FALSE;};
	virtual	BOOL			RenderUseHierarchy(SINT pn)		{return TRUE;};
	virtual	float			GetRenderYaw()					{return 0;};
	virtual float			GetRenderScale()				{return 1;};
	virtual BOOL			GetRenderWithTransforms()		{return TRUE;};	// FUNCTION: 0x004014a0
	virtual bool			GetCanBeImpostered()			{ return true; }	// FUNCTION: 0x00401480
	virtual int				GetImposterFrames()				{ return 1; };
	virtual EAnimMode		GetImposterAnimMode()			{ return AM_NONE; };
	virtual CBoundingBox*	GetBoundingBox();
	virtual EAnimMode		GetRenderAnimation()			{ return AM_NONE; }
	virtual float			GetRenderImposterBias()			{return 1.0f;};
	virtual	SINT			GetSnowDensity()			{return 0;};
	virtual	BOOL			GetRequiresPolyBucket()			{return TRUE;}
	virtual int				GetShouldDoHitEffect(SINT mesh_part_no) { return 0; }
	virtual int				GetRenderImposterNo()				{return 0;};
	virtual CMotionController	*GetMotionController() { return NULL; }

			void			DrawDebugCuboid() ;
			FVector&		GetPos()						{return mPos;};
	inline	FMatrix& 		GetOrientation(); // This is defined below
			FVector			GetTopPos();
			CCSPersistentThing* GetCSPT() ;
			CMESH			*GetRTMesh() {return (mRenderThing) ? mRenderThing->GetRTMesh() : NULL;};
			void			SetPos(FVector &inPos)			{ mPos=inPos; }
			void			GetCentrePos(FVector &outPos) ;
			void			SitOnGround();
			void			UpdatePosition();
			float			GetRenderThingFrameIncrement(EAnimMode am, int* realindex);
			void			SetObjective(BOOL val) ;
			CMapWhoEntry*   GetMapWhoEntry()				{if (mMapWhoEntry.GetSectorIn().GetLayer() == -1) return NULL; else return &mMapWhoEntry;}
			CRenderThing*	GetRenderThing() { return mRenderThing ; }
	// Two of Stuart's CThing members (his thing.h:212, 213), declared only, never defined or called. DECLARATION-COUNT
	// CARRIERS: moving ~CMapWhoEntry out of line (mapwhoentry.h) lost the witness BuildDeviceList
	// (00529350); two more declarations restore it and keep DrawDebugStuff, the CPDTimeline rows and CFEPGoodies::Render.
			void			Reset(SINT c);
			void			Select();
		    CCollisionSeekingThing* GetCST() { return mCollisionSeekingThing ; }
			ULONG&			GetThingType() { return mThingType ; }
	const	BOOL			IsA(ULONG type) const { return (type & mThingType); }
			short			GetFlags() { return mFlags ;}
			void			SetFlags(short val) { mFlags =val;}
			BOOL			IsDying()			{ if (mFlags & TF_DYING) return TRUE; else return FALSE ; }
			BOOL			IsShuttingDown()	{ if (mFlags & TF_DECLARED_SHUTDOWN) return TRUE; else return FALSE; }
			int				GetThingNumber()				{ return mThingNumber; }
			BOOL			IsOverWater();

protected:
	friend class CMapWhoEntry;

	CMapWhoEntry			mMapWhoEntry ;
	FVector					mPos;
	short					mFlags;
	short					mThingNumber;
	CRenderThing*			mRenderThing;
	ULONG					mThingType;
	CCollisionSeekingThing* mCollisionSeekingThing ;

	static short			sThingCount;
	static FMatrix			sIdMatrix;
};

DECLARE_THING_CLASS(CComplexThing, CThing)
public:
	CComplexThing();
	~CComplexThing();
	virtual void			Init(CInitThing* init)	;
	virtual	void			Shutdown() ;
	virtual void			HandleEvent(CEvent* event);

	virtual	FMatrix			GetRenderOrientation()			{return mOrientation;};
	virtual float			GetRenderFrame();
	virtual	void			SetThingType(ULONG t)		 { SUPERTYPE::SetThingType(t | THING_TYPE_COMPLEX_THING); }
	virtual SINT			GetRealAnimIndex()				{ if (mAnimation) return mAnimation->GetAnimIndex(); else return -1;};	// FUNCTION: 0x004014d0
	virtual	FMatrix			GetSoundOrientation()			{return mOrientation;};
			FMatrix&		GetOrientation()				{return mOrientation;}
	virtual BOOL			IsObjective() { if ((mFlags & TF_MARKED_OBJECTIVE) == 0) return FALSE ; else return TRUE ;}	// FUNCTION: 0x004014e0
	virtual FMatrix			GetOldOrientation() {return mOrientation;};
	virtual CMotionController	*GetMotionController() { return mMotionController; }	// FUNCTION: 0x004014f0
	virtual EAnimMode		GetRenderAnimation()				{ if (mAnimation) return mAnimation->GetAnimMode() ; else return AM_NONE; }	// FUNCTION: 0x00401500
	virtual	char*			GetName() { return mName ;}	// FUNCTION: 0x00401510
	virtual void			SetName(char *inName);
	virtual void			AddShutdownEvent() ;
	virtual BOOL			StartDieProcess() ;
	virtual BOOL			FinishedPlayingCurrentAnimation();								// 59
	virtual	BOOL			SetAnimMode(EAnimMode am, BOOL reset_frame, BOOL force_looped = FALSE);	// 60
			BOOL			SetAnimMode(char* name, BOOL reset_frame, BOOL force_looped = FALSE);
			EAnimMode		GetAnimModeByName(char* name);
			void			SetScript(char *inScript);
			IScript*		GetMissionScript() { return mMissionScript ; }
	virtual void			GoToPoint(FVector  point, BOOL		inOverride=FALSE)				{}	// FUNCTION: 0x00401520
	virtual	void			SetVar(CStringDataType* name, CDataType* data) ;				// 62
	virtual void			SetSpawnScript(char *inScript)		{};							// 63
	virtual void			Stop()								{}							// 64
	virtual void			*GetContainedInside() { return NULL ; }						// 65
	virtual void			TeleportOrientation(const FMatrix& orientation);
	virtual void			Hit(CThing* other_thing, CCollisionReport* report) ;

protected:
	FMatrix					mOrientation;
	CAnimation*				mAnimation;
	CMotionController*		mMotionController;
	IScript					*mMissionScript;
	char					*mName;
};


FMatrix &CThing::GetOrientation()
{
	if (IsA(THING_TYPE_COMPLEX_THING))
		return(((CComplexThing *) this)->GetOrientation());
	else
		return(sIdMatrix);
}

// A new thing of that object id (Stuart's global; retail pushes a second, zero argument, 0046d119).
class CThing;
CThing	*SpawnThing(SINT inID, BOOL inReportErrors = FALSE);	// 004bf090

#endif
