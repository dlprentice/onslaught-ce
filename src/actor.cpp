#include "common.h"
#include "map.h"
#include "actor.h"
#include "debuglog.h"
#include "mapwho.h" 
#include "mapwhoentry.h"
#include "collisionseekingthing.h"
#include "eventmanager.h"
#include "Profile.h"
#include "game.h"
#include "cliparams.h"
 

//******************************************************************************************
void	CActor::Init(CInitThing* init)	// FUNCTION: 0x004011e0
{
	mLastTimeOnGround=-100.f;
	mLastTimeInWater=-100.f;
	mLastTimeOnObject=-100.0f;
	mLastMoveTime = EVENT_MANAGER.GetTime() ;

	// SRG when initalising collision stuff in Thing::Init is may call GetVelocity() 
	// so setting up the attribute here 
	mVelocity	= init->mVelocity ;

	// SRG collision stuff also needs oldPos to be sensible aswell
	mOldPos			= init->mPos;

	if (init->mOrientationType == EULER_ANGLES)
		mOldOrientation = FMatrix(init->mYaw, init->mPitch, init->mRoll);
	else if (init->mOrientationType == DIRECTION_COSINE_MATRIX)
		mOldOrientation = init->mOrientation; 

	SUPERTYPE::Init(init);

	int r = (int)GetMoveMultiplier() ;

	mDoFullMoveCount = GAME.Random() % r ;

	AddMoveEvent(NULL);
}


//******************************************************************************************
CActor::~CActor()	// FUNCTION: 0x004013d0
{

}

//******************************************************************************************
// don't add to this without my say so (SRG)  
// A basic low fidelity move function which has to be called every frame which the normal
// move function was not called on.  This is here in order for the collision to work properly.
void CActor::LowFidelityMove()
{
	PROFILE_FN(ActorLFMove);
	mOldPos = mPos;
	mPos+=mVelocity;
	
	if (ClipToGround())
	{
		float		alt=MAP.Collide(mPos)-COfGHeight();

		if (alt <=mPos.Z)
		{	
			mPos.Z=alt;
			mLastTimeOnGround = EVENT_MANAGER.GetTime();
		}
	}
}


//******************************************************************************************
void CActor::Move()	// FUNCTION: 0x004015e0
{
	PROFILE_FN(ActorMove);
	mLastMoveTime = EVENT_MANAGER.GetTime();

	// store old position
	mOldPos = mPos;
	mOldOrientation = mOrientation;

	CSector previous_sector;
	if (GetMapWhoEntry())
		previous_sector=mMapWhoEntry.GetSectorIn();

	if (CLIPARAMS.mDeveloperMode)
	{
		// ### TEMP debug check to make sure we don't go above max velocity

		if (IsA(THING_TYPE_UNIT))
		{
			float	m=mVelocity.MagnitudeXY();
			float	mv=GetMaxVelocity()/GAME_FR;
			if (m>(mv+0.001f))
			{
				LOG.AddMessage("Warning object %s travling beyond max velocity (%2.8f per sec) mv/s = %2.8f", _GetClassName(), (m-mv) * GAME_FR, GetMaxVelocity()  ) ;
			}
		}
		else
		{

			float	m=mVelocity.Magnitude();
			float	mv=GetMaxVelocity()/GAME_FR;
			if (m>(mv+0.001f))
			{
				LOG.AddMessage("Warning object %s travling beyond max velocity (%2.8f per sec) mv/s = %2.8f", _GetClassName(), (m-mv) * GAME_FR, GetMaxVelocity()  ) ;
			}
		}
	}

	// SRG end temp check

	// STAND-IN (review): Stuart writes `mPos += mVelocity;`; that form loses CActor::Move's match
	// (the alias changes the += expansion), so the reference stays until the real cause is found.
	FVector &position = mPos;
	position += mVelocity;

	float water_height = MAP.GetWaterLevel()-COfGHeight();

	if (ClipToGround())
	{
		float		alt=MAP.Collide(mPos)-COfGHeight();

		if (alt <=mPos.Z)
		{	
		
			mPos.Z=alt;

			// sorry last minute hack stops you bouncing down hills
			if (mFlags & TF_SLIDE)
			{		
				// slide like you did in moho
				FVector slidePlaneNormal = MAP.Normal(mPos) ;
				float backoff = (mVelocity * slidePlaneNormal);
				mVelocity -=slidePlaneNormal*backoff;
				// not on ground !
			}
			else
			{
				DeclareOnGround();
				if (fabsf(mVelocity.Z)>Gravity()+0.05f)
					mVelocity.Z=-mVelocity.Z*BounceFactor();
				else
					mVelocity.Z=0;
			}
		}

		//if (water_height <= mPos.Z) mPos.Z = water_height 

			if (water_height <= mPos.Z)
		DeclareInWater();
	} 



	if (GetMapWhoEntry())
	{
		if (mMapWhoEntry.UpdateEntry(mOldPos) && mCollisionSeekingThing)
		{
			mCollisionSeekingThing->MovedSector(previous_sector, mMapWhoEntry.GetSectorIn()) ;
		}
	}


	/*
	// If we've moved, remove some snow

	if ((mOldPos!=mPos) || (mOldOrientation!=mOldOrientation))
	{
		mSnowDensity-=8; // Needs to be greater than the snow addition value as snow will still be added!
		if (mSnowDensity<0)
			mSnowDensity=0;
	}
	*/
}


//******************************************************************************************
// used by collsion etc ( resets where the thing has moved over the frame )
void	CActor::MoveTo(const FVector& pos)	// FUNCTION: 0x00401900
{
//	mOldPos = mPos ;
	SUPERTYPE::MoveTo(pos) ;
}

//******************************************************************************************
void	CActor::Teleport(const FVector& pos)	// FUNCTION: 0x00401910
{
	CSector previous_sector;
	if (GetMapWhoEntry())
		previous_sector=mMapWhoEntry.GetSectorIn();

	mOldPos = pos ;

	SUPERTYPE::Teleport(pos) ;

	if (GetMapWhoEntry())
	{
		if (mMapWhoEntry.UpdateEntry(mOldPos) && mCollisionSeekingThing)
		{
			mCollisionSeekingThing->MovedSector(previous_sector, mMapWhoEntry.GetSectorIn()) ;
		}
	}
}

//******************************************************************************************
void	CActor::TeleportOrientation(const FMatrix& orientation)	// FUNCTION: 0x004019b0
{
	mOldOrientation=orientation;
	SUPERTYPE::TeleportOrientation(orientation);
}


//******************************************************************************************
void	CActor::AddMoveEvent(CEvent* event)
{
	if (!(mFlags & TF_DECLARED_SHUTDOWN))
	{		
		mDoFullMoveCount-- ;
		ActorEvent m_event = LF_MOVE ; 
		if (mDoFullMoveCount <=0)
		{
			m_event = MOVE ;
			mDoFullMoveCount = (int)GetMoveMultiplier() ;
		}
	
		EVENT_MANAGER.AddEvent((int)m_event, this, NEXT_FRAME,  START_OF_FRAME, NULL, (CScheduledEvent*)event);
	}
}

//******************************************************************************************
void	CActor::HandleEvent(CEvent* event)	// FUNCTION: 0x004019e0
{
	switch( (ActorEvent)event->GetEventNum() )
	{
		case MOVE:	
		{
			PROFILE_START(MoveThings) ;
			Move() ;
			AddMoveEvent(event);

			
			PROFILE_END(MoveThings) ;
			break ;
		}
		case LF_MOVE:
		{
			PROFILE_START(MoveThings) ;
			LowFidelityMove(); 
			AddMoveEvent(event);
			PROFILE_END(MoveThings) ;
			break ;
		}

	default:
		{
			SUPERTYPE::HandleEvent(event) ;
		}
	}
}


//******************************************************************************************
float CActor::GetFractionTime()	// FUNCTION: 0x00401b50
{
	float mm = GetMoveMultiplier() ;
	if ( mm > 1.0f)
	{	
		float t = EVENT_MANAGER.GetTime() - mLastMoveTime ;
		float step = 1 / GetMoveMultiplier() ;
		float fraction =  step * t * GAME_FR;
		fraction += step * GAME.GetFrameRenderFraction(); 
		if (fraction< 0.0f) fraction = 0.0f ;
		if (fraction> 1.0f) fraction = 1.0f; 
		return fraction ;
	}
	
	return GAME.GetFrameRenderFraction() ;
}



//******************************************************************************************

FVector CActor::GetRenderPos()	// FUNCTION: 0x00401be0
{
	return mOldPos+((mPos-mOldPos) * GAME.GetFrameRenderFraction()) ;
}


//******************************************************************************************

FMatrix CActor::GetRenderOrientation()	// FUNCTION: 0x00401c50
{
	FMatrix newori;

#if TARGET == PS2
	asm __volatile__
	(
	"
		qmtc2		%9,vf31		\n
		vsub		%0,%3,%6	\n
		vsub		%1,%4,%7	\n
		vsub		%2,%5,%8	\n
		vmulax		ACC,%0,vf31	\n
		vmaddw		%0,%6,vf00	\n
		vmulax		ACC,%1,vf31	\n
		vmaddw		%1,%7,vf00	\n
		vmulax		ACC,%2,vf31	\n
		vmaddw		%2,%8,vf00	\n
	"
	:	"=&j" (newori.Row[0].lVec), "=&j" (newori.Row[1].lVec), "=&j" (newori.Row[2].lVec)
	:	"j" (mOrientation.Row[0].lVec), "j" (mOrientation.Row[1].lVec), "j" (mOrientation.Row[2].lVec),
		"j" (mOldOrientation.Row[0].lVec), "j" (mOldOrientation.Row[1].lVec), "j" (mOldOrientation.Row[2].lVec),
		"r" (GetFractionTime())
	:	"vf31"
	);
#else
	newori = mOldOrientation+((mOrientation-mOldOrientation) * GetFractionTime());
#endif
//	newori.Normalise();

	return newori;
}


//******************************************************************************************
BOOL	CActor::IsOnGround()	// FUNCTION: 0x00401f70
{ 
	return	(EVENT_MANAGER.GetTime() - mLastTimeOnGround < 0.15f);
}

//******************************************************************************************
BOOL	CActor::IsInWater()	// FUNCTION: 0x00401fa0
{
	return	(EVENT_MANAGER.GetTime() - mLastTimeInWater < 0.25f);
}

//******************************************************************************************
BOOL	CActor::IsOnObject()	// FUNCTION: 0x00401fd0
{
	return	(EVENT_MANAGER.GetTime() - mLastTimeOnObject < 0.15f);
}

//******************************************************************************************
void	CActor::DeclareOnGround()	// FUNCTION: 0x00402000
{
	mLastTimeOnGround = EVENT_MANAGER.GetTime();
}

//******************************************************************************************
void	CActor::DeclareInWater()	// FUNCTION: 0x00402010
{
	mLastTimeInWater = EVENT_MANAGER.GetTime();
}

//******************************************************************************************
void	CActor::DeclareOnObject(CThing* on_object)	// FUNCTION: 0x00402020
{
	mLastTimeOnObject = EVENT_MANAGER.GetTime();
}

//******************************************************************************************
void	CActor::StickToGround()	// FUNCTION: 0x00402030
{
	SUPERTYPE::StickToGround();

	mOldPos=mPos;
}

// Compiler-generated: the vector constructor iterator, the deleting destructor and the maths initializers.
// FUNCTION: 0x004011b0 ??_H@YGXPAXIHP6EX0@Z@Z
// FUNCTION: 0x004015c0 ??_GCActor@@UAEPAXI@Z
// FUNCTION: 0x004010c0 init:ZERO_FVECTOR
// FUNCTION: 0x004010e0 init:ID_FMATRIX

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00401530 ?_GetClassName@CActor@@UAEPADXZ
// FUNCTION: 0x004040a0 ?GetRenderStartPos@CThing@@UAE?AVFVector@@XZ
// FUNCTION: 0x00404120 ?GetVelocity@CActor@@UAE?AVFVector@@XZ
// FUNCTION: 0x00404150 ?SetVelocity@CActor@@UAEXABVFVector@@@Z
// FUNCTION: 0x00404170 ?AddVelocity@CActor@@UAEXABVFVector@@@Z
// FUNCTION: 0x004041a0 ?GetOldPos@CActor@@UAE?AVFVector@@XZ
// FUNCTION: 0x004041d0 ?GetOldOrientation@CActor@@UAE?AVFMatrix@@XZ
// FUNCTION: 0x00405940 ?GetShouldDoHitEffect@CThing@@UAEHH@Z
// FUNCTION: 0x0043e9f0 ?GetSoundPos@CThing@@UAE?AVFVector@@XZ
// FUNCTION: 0x0043ea20 ?GetSoundOrientation@CComplexThing@@UAE?AVFMatrix@@XZ
// FUNCTION: 0x00459990 ?Activate@CThing@@UAEXXZ
// FUNCTION: 0x004bfc60 ?GetRenderYaw@CThing@@UAEMXZ
// FUNCTION: 0x004de700 ?GetRenderTurn@CThing@@UAEMXZ
