#include "common.h"
#include "mesh.h"			// ########
#include "thing.h"
#include "ScheduledEvent.h"
#include "eventmanager.h"
#include "activereader.h"
#include "world.h"
#include "debuglog.h"
#include "engine.h"
#include "CollisionSeekingThing.h"
#include "sphere.h"
//#include "d3dutils.h"
#include "map.h"
#include "MissionScript/vm.h"
#include "game.h"
#include "renderinfo.h"
#include "atmospherics.h"
#include "Motioncontroller.h"
#include "meshrenderer.h"
#include "rtmesh.h"

#include <string.h>

short		CThing::sThingCount=0;
FMatrix		CThing::sIdMatrix=FMatrix(FVector(1.0f,0.0f,0.0f),FVector(0.0f,1.0f,0.0f),FVector(0.0f,0.0f,1.0f));

//******************************************************************************************
CThing::CThing()	// FUNCTION: 0x004f33e0
{
	mThingType=THING_TYPE_NONE;
	mFlags=TF_IN_MAP_WHO;
	mRenderThing=NULL;
	mCollisionSeekingThing=NULL;

	mThingNumber=sThingCount;
	sThingCount++;
}

//******************************************************************************************
void CThing::Init(CInitThing* init)	// FUNCTION: 0x004f34a0
{
	if (!mRenderThing)
		InitRenderThing(init);

	SetThingType(0) ;

	mPos = init->mPos;
 
	if (ClipToGround())
	{
		float	groundHeight=MAP.Collide(mPos);

		if (mPos.Z>groundHeight)
		{
			FVector new_pos = mPos ;
			new_pos.Z=groundHeight;
			Teleport(new_pos) ;
		}
	}

	if (!CanGoUnderWater())
	{
		float water_height = MAP.GetWaterLevel();
		if (mPos.Z > water_height) mPos.Z= water_height ;
	}
	
	if (mFlags & TF_IN_MAP_WHO)
	{
		mMapWhoEntry.Init(mPos, this, init->mForceRadius) ;
		if (mMapWhoEntry.GetSectorIn().GetLayer() < USE_BIGLIST_FOR)
		{
			WORLD.GetBigThingsNB().Append(this) ;
			mFlags|=TF_IS_BIG_THING ;
		}

		InitCollisionSeekingThing(&init->mInitCST);
	}
	
	if (mRenderThing)
		mThingType|=THING_TYPE_RENDERABLE;

	if (!init->mActive)
		Deactivate();

	WORLD.AddThing(this) ;
}




//******************************************************************************************
void	CThing::InitRenderThing(CInitThing *init)	// FUNCTION: 0x004f35d0
{
	mRenderThing = CreateAndGetRenderThing(_GetClassID(), this);
}





//******************************************************************************************
void CThing::Shutdown()	// FUNCTION: 0x004f3600
{
	WORLD.RemoveThing(this);
	if ((mFlags & TF_IS_BIG_THING) != 0)
	{	
		WORLD.GetBigThingsNB().Remove(this) ;
	}

	CMonitor::Shutdown() ;
	delete this ;
}


//******************************************************************************************
CThing::~CThing()	// FUNCTION: 0x004f3640
{
	if (mCollisionSeekingThing)
	{
		delete mCollisionSeekingThing;
	}

	if (mRenderThing)
	{
		delete mRenderThing;
	}

	mCollisionSeekingThing = NULL ;
	mRenderThing = NULL ;
}

//******************************************************************************************
void CThing::Render(DWORD flags)	// FUNCTION: 0x004f36d0
{
	if ((mFlags & TF_INVISIBLE) !=0) return ;

	if ((mFlags & TF_MARKED_OBJECTIVE)!=0)
		flags|=RF_HILIGHT;

	if ((mRenderThing) && (!(mFlags & TF_DONT_RENDER)))
	{
		mRenderThing->Render(flags) ;
		if (ENGINE.GetDrawDebugStuff() & DRAW_OUTER_RADIUS)
		{
			DrawDebugCuboid();
		}
	}
}
//******************************************************************************************
void CThing::RenderImposter()	// FUNCTION: 0x004f3710
{
	if ((mRenderThing) && (!(mFlags & TF_DONT_RENDER)))
	{
		mRenderThing->RenderImposter();
	}
}

//******************************************************************************************
void	CThing::HandleEvent(CEvent *event)	// FUNCTION: 0x004f3730
{
	switch ((EThingEvent)event->GetEventNum())
	{
	case SHUTDOWN:
		{
			Shutdown() ;
			break ;
		}

	case START_DIE_PROCESS:
		{
			StartDieProcess() ;
			break ;
		}
	default:
		{
			CMonitor::HandleEvent(event);
			break ;
		}
	}
}

//******************************************************************************************
void	CThing::AddShutdownEvent()	// FUNCTION: 0x004f3760
{
	if ((mFlags & TF_DECLARED_SHUTDOWN) ==0)
	{
		mFlags |= TF_DECLARED_SHUTDOWN ;
		EVENT_MANAGER.AddEvent((int)SHUTDOWN,this,NEXT_FRAME,START_OF_FRAME) ;
	}
}

//******************************************************************************************
BOOL	CThing::StartDieProcess()	// FUNCTION: 0x004f37a0
{
	if (IsDying()==FALSE)
	{
		mFlags |= TF_DYING ;
		AddShutdownEvent() ; 

		return TRUE;
	}

	return FALSE;
}

//******************************************************************************************
void CThing::DrawDebugCuboid()	// FUNCTION: 0x004f37c0
{
	FMatrix ori = ID_FMATRIX;
	FVector ps  = FVector(0, 0, 0);


	CBoundingBox* box = GetBoundingBox();
	if (box != NULL)
	{
		float x = box->mAxes.X;
		float y = box->mAxes.Y;

//		x +=(float)box->mOrigin.X);
//		y +=(float)box->mOrigin.Y) ;

		RENDERINFO.SetWorld(GetPos(), ori);
		ENGINE.DrawDebugCuboid(0xff00ff00, FVector(x,y,box->mAxes.Z), FVector(0.0f,0.0f,0.0f), ori, ENGINE.GetOutlineTexture());


	}


	RENDERINFO.SetWorld(GetPos(), ori);
	float r =GetBoundingRadius();
	float r1 = GetRadius() ;
	FVector pos  =GetPos();
	FVector axes = FVector( r,  r, r);
	FVector axes1 = FVector( r1,  r1, r1);
	DWORD col = 0xffffffff ;

//	if (mHit > 0)
//	{
//		col = 0xffff0000;
//		mHit--;
//	}

		
	ENGINE.DrawDebugCuboid(col, axes1, FVector(0.0f,0.0f,0.0f), ori, ENGINE.GetOutlineTexture());



	//	ENGINE.DrawDebugCuboid(0xffff00ff, axes1, pos, ori, ENGINE.GetOutlineTexture());
}


//******************************************************************************************
// this minimun sphere which could competly seround the mesh of this thing
float	CThing::GetBoundingRadius()	// FUNCTION: 0x004f3940
{

	if (mRenderThing)
	{
		if (mRenderThing->GetBoundingBox())
		{
			return mRenderThing->GetBoundingBox()->mRadius ;
		}
	}
	return GetRadius() ;
}


//******************************************************************************************
void	CThing::SetObjective(BOOL val)	// FUNCTION: 0x004f3970
{
	if (val == TRUE)
	{
		if ((mFlags & TF_MARKED_OBJECTIVE) ==0)
		{
			WORLD.GetObjectiveThingNB().Add(this) ;
			mFlags |= TF_MARKED_OBJECTIVE ;
		}
	}
	else
	{
		if ((mFlags & TF_MARKED_OBJECTIVE) !=0)
		{
			WORLD.GetObjectiveThingNB().Remove(this) ;
			mFlags &= ~TF_MARKED_OBJECTIVE ;
		}
	}
}

//******************************************************************************************
void	CThing::UpdatePosition()	// FUNCTION: 0x004f39b0
{
	if (mRenderThing)
	{
		CRTMesh *rt=(CRTMesh *) mRenderThing; // Eeeek!
		rt->UpdatePosition();
	}
}

//******************************************************************************************
void	CThing::InitCollisionSeekingThing(CInitCSThing *init)	// FUNCTION: 0x004f39c0
{

	// ok does this thing do some kind of collision
	// NOTE: this is default case when a collision seekingthing hasn't been setup yet.
	if (init->mNotSeekCollisionWithBF !=THING_TYPE_EVERYTHING)
	{
		// check if a collision_seeking_thing has been set else give it a default one
		if (mCollisionSeekingThing == NULL)
		{
			mCollisionSeekingThing = new( MT_CST ) CCSPersistentThing  ;
		}

		// ok if has no renderthing make sure mesh collision switched off
		if (mRenderThing == NULL && init->mMaxCollision == ECL_MESH)
		{
			LOG.AddMessage("Warning: Trying to do mesh collision on a object that has no mesh") ;

			init->mMaxCollision = ECL_APPROX_GEOMETRY_SHAPES ;
		}

		// initialise collision seeking thing
	    init->mForThing = this ;
		mCollisionSeekingThing->Init(init);
	}
	else
	{
		// ok just a quick debug thing. If a child has set up the collision seeking thing
		// but seeks collision with nothing then something is wrong ?
		if (mCollisionSeekingThing)
		{
			ASSERT(0) ;
		}
	}
}


//******************************************************************************************
// this returns the position of the centre of the bounding box which could competly surround the 
// mesh of this thing (note: returns vector in world coordinated )

// SRG note has to have the same x and y and getpos else collision won't like it
void	CThing::GetCentrePos(FVector &outPos)	// FUNCTION: 0x004f3ac0
{
	if ((IsA(THING_TYPE_BUILDING)) || (IsA(THING_TYPE_COMPONENT)))
	{
		if (mRenderThing && mRenderThing->GetBoundingBox())
		{	
			outPos = GetPos() + GetOrientation() * mRenderThing->GetBoundingBox()->mOrigin;
			return;
		}
	}
	else if (IsA(THING_TYPE_BATTLE_ENGINE))
	{
		if (IsA(THING_TYPE_GROUND_UNIT))
		{
			outPos=GetPos();
			outPos.Z+=BATTLE_ENGINE_COFGHEIGHT*0.4f;
			return;
		}
	}
	else
	{
#if TARGET == PS2
		PREFETCH_L1( &mPos );
		CBoundingBox * box;
		if( mRenderThing && ( box = mRenderThing->GetBoundingBox() ) )
		{
			asm __volatile__
			(
			"
				vadda.xyz	ACC,%1,vf00	\n
				vmaddw.z	%0,%2,vf00	\n
			"
			:	"=j" (outPos.lVec)
			:	"j" (GetPos().lVec), "j" (box->mOrigin.lVec)
			);

			return;
		}
#else
		if (mRenderThing)
		{	
			CBoundingBox* box = mRenderThing->GetBoundingBox();

			if (box)
			{
				outPos = FVector(mPos.X, mPos.Y, mPos.Z +box->mOrigin.Z);
				return;
			}
		}
#endif
	}

	outPos=GetPos();
}



//******************************************************************************************
void	CThing::StickToGround()	// FUNCTION: 0x004f3c50
{
	mPos.Z = MAP.Collide(mPos);
	UpdatePosition();	
}

//******************************************************************************************
void	CThing::SitOnGround()
{
	mPos.Z = MAP.Collide(mPos) - GetRadius();
	UpdatePosition();
}

//******************************************************************************************
float	CThing::GetRenderThingFrameIncrement(EAnimMode am, int* realindex)	// FUNCTION: 0x004f3c80
{
	float	ret;
	if(mRenderThing) ret = mRenderThing->GetFrameIncrement(am, realindex);
	else ret = 0.0;
	return ret;
}

//******************************************************************************************
void	CThing::MoveTo(const FVector& pos)	// FUNCTION: 0x004f3cb0
{
	mPos = pos ;
	UpdatePosition();
}

//******************************************************************************************
void	CThing::Teleport(const FVector& pos)	// FUNCTION: 0x004f3ce0
{
	mPos = pos ; // this makes oldpos and pos be the same (for actors)  when the function is
				 // complete
	MoveTo(pos) ;
}




//*****************************************************************************************
CCSPersistentThing* CThing::GetCSPT()	// FUNCTION: 0x004f3d10
{ 
	if (mCollisionSeekingThing)
	{
		return mCollisionSeekingThing->IsPersistentCSThing() ;
	}
	return NULL ;
}


//*****************************************************************************************
/*FVector	CThing::GetTargettingPos()
{
	FVector	spos, wpos;

	wpos = GetPos();

	CMESH *mesh = GetRTMesh();
	if (mesh)
	{
		FVector	mpos;
		mpos  = mesh->GetPart(0)->mOffsetPos;
		mpos += mesh->GetPart(0)->mOrientation * mesh->GetPart(0)->mBoundingBox->mOrigin;

		wpos += GetRenderOrientation() * mpos;
	}

	return wpos;
}*/

//*****************************************************************************************
FVector	CThing::GetTopPos()
{
	FVector	spos, wpos;

	wpos = GetPos();

	CMESH *mesh = GetRTMesh();
	if (mesh)
	{
		FVector			mpos;

		mpos=mesh->GetPart(0)->mOffsetPos;
		
		FVector			vector=mesh->mBoundingBox->mOrigin-FVector(0,0,mesh->mBoundingBox->mAxes.Z/2);
		
		mpos+=mesh->GetPart(0)->mOrientation*vector;

		wpos+=GetRenderOrientation()*mpos;
	}

	return wpos;
}


//*****************************************************************************************
CBoundingBox*	CThing::GetBoundingBox()	// FUNCTION: 0x004f3d20
{
	if (mRenderThing)  
		return mRenderThing->GetBoundingBox(); 
	else 
		return NULL;
}


//******************************************************************************************
void CThing::DrawDebugStuff(float &y_offset)
{

}

//******************************************************************************************
void CThing::DrawDebugStuff3d()	// FUNCTION: 0x004f3d30
{
	FMatrix ori = ID_FMATRIX;
	FVector ps  = FVector(0, 0, 0);
	RENDERINFO.SetWorld(GetPos(), ori);

	float r = GetRadius() ;
	FVector axes = FVector( r , r, r);
	DWORD col = 0xffffffff ;
	ENGINE.DrawDebugCuboid(col, axes, FVector(0.0f,0.0f,0.0f)  , ori, ENGINE.GetOutlineTexture());
}

//******************************************************************************************
#ifdef RESBUILDER
void CThing::AccumulateResources(CResourceAccumulator * accumulator,DWORD flags)
{
	if( mRenderThing )
	{
		mRenderThing->AccumulateResources(accumulator,flags);
	}
}
#endif
//******************************************************************************************
void CThing::IncreaseThingCounter(
	CInitThing		*inInit)
{
	
}

//*****************************************************************************************
BOOL CThing::IsOverWater()	// FUNCTION: 0x004f3de0
{
	float	waterHeight=MAP.GetWaterLevel();
	float	groundHeight=MAP.Collide(mPos);

	if (waterHeight<groundHeight)
		return TRUE;

	return FALSE;
}

//******************************************************************************************
//******************************************************************************************
//******************************************************************************************
//******************************************************************************************




//******************************************************************************************
CComplexThing::CComplexThing()	// FUNCTION: 0x004f3e10
{
	mOrientation=ID_FMATRIX;
	mMotionController=NULL;

	mMissionScript=NULL;
	mName=NULL;
	mAnimation = NULL ;
}



//******************************************************************************************
CComplexThing::~CComplexThing()	// FUNCTION: 0x004f3f00
{
	delete mMissionScript ;
	mMissionScript = NULL;

	delete mAnimation;
	mAnimation = NULL;

	SAFE_DELETE(mMotionController);

}



//******************************************************************************************
void CComplexThing::Init(CInitThing* init)	// FUNCTION: 0x004f3fd0
{
	// has the thing got a name
	SetName(init->mName);
	
	// has the thing got a script
	SetScript(init->mScript);

	if (init->mOrientationType == EULER_ANGLES)
	{
		mOrientation = FMatrix(init->mYaw, init->mPitch, init->mRoll) ;
	}
	else if (init->mOrientationType == DIRECTION_COSINE_MATRIX)
	{
		mOrientation = init->mOrientation ; 
	}

	SUPERTYPE::Init(init);

}

//******************************************************************************************
void CComplexThing::SetName(char *inName)	// FUNCTION: 0x004f4120
{
	if (mName)
	{
		WORLD.GetNamedThingNB().Remove(this);
		delete mName;
		mName=NULL;
	}

	if (inName[0] != '\0')
	{
		int len = strlen(inName) ;
		mName = new( MT_THING ) char[len+1] ;
		strncpy(mName, inName, len);
		mName[len] = '\0';
		WORLD.GetNamedThingNB().Add(this);
	}
}

//******************************************************************************************
void CComplexThing::Shutdown()	// FUNCTION: 0x004f41b0
{
	if (mName)
	{
		WORLD.GetNamedThingNB().Remove(this) ;
		delete [] mName ;
		mName = NULL ;
	}

	// will remove us as objective if we are set as an objective (i.e. remove us from
	// world Noticeboard class )
	SetObjective(FALSE) ;

	SUPERTYPE::Shutdown();
}


//******************************************************************************************
void	CComplexThing::SetScript(char *inScriptName)	// FUNCTION: 0x004f4230
{
	if (mMissionScript)
	{
		delete mMissionScript;
		mMissionScript=NULL;
	}

	if ((inScriptName) && (inScriptName[0]!=0))
	{
		CMissionScriptObjectCode *code=WORLD.GetCopyOfScript(inScriptName) ;
		if (code)
		{
			// make interface to it
			mMissionScript = new( MT_SCRIPT ) IScript(this, code) ;
			EVENT_MANAGER.AddEvent(INIT_SCRIPT,this,NEXT_FRAME,START_OF_FRAME);
		}
	}
}



//******************************************************************************************
void	CComplexThing::HandleEvent(CEvent *event)	// FUNCTION: 0x004f4300
{
	switch ((EThingEvent)event->GetEventNum())
	{
	case SHUTDOWN:
		{
			if ( GAME.GetGameState() <= GAME_STATE_PLAYING && mMissionScript)
			{
				mMissionScript->Shutdown() ;
			}

			SUPERTYPE::HandleEvent(event);
			break ;
		}
	case INIT_SCRIPT:
		{
			if (mMissionScript) mMissionScript->Init();

			if (!IsA(THING_TYPE_UNIT))
				EVENT_MANAGER.AddEvent(READY_SCRIPT,this,NEXT_FRAME);
			break ;
		}
	case READY_SCRIPT:
		{
			if (mMissionScript) mMissionScript->Ready();
			break;
		}
	default:
		{
			SUPERTYPE::HandleEvent(event);
			break ;
		}
	}
}


//******************************************************************************************
void	CComplexThing::AddShutdownEvent()	// FUNCTION: 0x004f43d0
{
	if ((mFlags & TF_DECLARED_SHUTDOWN) ==0)
	{
		if (mMissionScript)
		{
			mMissionScript->Died() ;
			delete mMissionScript;
			mMissionScript = NULL ;
		}
	}

	SUPERTYPE::AddShutdownEvent();
}


//******************************************************************************************
BOOL	CComplexThing::StartDieProcess()	// FUNCTION: 0x004f4430
{
	if (SUPERTYPE::StartDieProcess() )
	{
		if (mMissionScript)
			mMissionScript->StartedDying() ;
		return TRUE;
	}

	return FALSE;
}


//******************************************************************************************
void	CComplexThing::TeleportOrientation(const FMatrix& orientation)	// FUNCTION: 0x004f4460
{
	mOrientation=orientation;
}

//******************************************************************************************
void CComplexThing::Hit(CThing* other_thing, CCollisionReport* report)	// FUNCTION: 0x004f4480
{
//	mHit = 3;

	if (mMissionScript && other_thing->IsA(THING_TYPE_COMPLEX_THING))
	{
		mMissionScript->Hit((CComplexThing*)other_thing) ;
	}
}





//******************************************************************************************
BOOL CComplexThing::SetAnimMode(EAnimMode am, BOOL reset_frame, BOOL force_looped)	// FUNCTION: 0x004f44a0 ?SetAnimMode@CComplexThing@@UAEHW4EAnimMode@@HH@Z
{
	if (mAnimation == NULL)
	{
		mAnimation = new (MT_THING) CAnimation(this) ;
	}

	return mAnimation->SetAnimMode(am, reset_frame, force_looped) ;
}



//******************************************************************************************
EAnimMode CComplexThing::GetAnimModeByName(char* name)	// FUNCTION: 0x004f4530
{
	CRenderThing* rt =GetRenderThing() ;
	if (rt == NULL) return AM_NONE ;
	CMesh* rtmesh = rt->GetRTMesh() ;
	if (rtmesh == NULL) return AM_NONE ;
	return rtmesh->GetAnimModeByName(name) ;
}



//******************************************************************************************
BOOL CComplexThing::SetAnimMode(char* name, BOOL reset_frame, BOOL force_looped)	// FUNCTION: 0x004f4560 ?SetAnimMode@CComplexThing@@QAEHPADHH@Z
{
	return SetAnimMode(GetAnimModeByName(name), reset_frame, force_looped) ;
}


//******************************************************************************************
BOOL CComplexThing::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x004f45a0
{
	if (mMissionScript)
	{
		mMissionScript->FinishedPlayingAnim() ;
	}

	return TRUE;
}


//*****************************************************************************************
float CComplexThing::GetRenderFrame()	// FUNCTION: 0x004f45c0
{
	if (mAnimation == NULL) return 0.0f ;
	
	return mAnimation->GetRenderFrame() ;

}

//******************************************************************************************
#ifdef RESBUILDER
void CComplexThing::AccumulateResources( CResourceAccumulator * accumulator,DWORD flags )
{
	if ( mMissionScript )
	{
		mMissionScript->AccumulateResources( accumulator );
	}
	SUPERTYPE::AccumulateResources(accumulator,flags);
}
#endif
//******************************************************************************************
void	CComplexThing::SetVar(CStringDataType* name, CDataType* data)	// FUNCTION: 0x004f45e0
{
	LOG.AddMessage("Warning: Uknown var '%s' in call to SetVar", name->GetString()) ;
}

// FUNCTION: 0x004f3220 init:ZERO_FVECTOR
// FUNCTION: 0x004f3240 init:ID_FMATRIX
// FUNCTION: 0x004f3310 init:sIdMatrix
// FUNCTION: 0x004f3460 ?_GetClassName@CThing@@UAEPADXZ
// FUNCTION: 0x004f3470 ?SetThingType@CThing@@UAEXK@Z
// FUNCTION: 0x004f3480 ??_GCThing@@UAEPAXI@Z
// FUNCTION: 0x004f3a50 ??_GCCSPersistentThing@@UAEPAXI@Z
// FUNCTION: 0x004f3a70 ??1CCSPersistentThing@@UAE@XZ
// FUNCTION: 0x004f3ec0 ?_GetClassName@CComplexThing@@UAEPADXZ
// FUNCTION: 0x004f3ed0 ?_GetClassID@CComplexThing@@UAEHXZ
// FUNCTION: 0x004f3ee0 ??_GCComplexThing@@UAEPAXI@Z

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004040d0 ?GetRenderOrientation@CComplexThing@@UAE?AVFMatrix@@XZ
// FUNCTION: 0x00404110 ?SetThingType@CComplexThing@@UAEXK@Z
// FUNCTION: 0x00405db0 ?Hit@CThing@@UAEXPAV1@PAUCCollisionReport@@@Z
