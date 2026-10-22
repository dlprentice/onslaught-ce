#ifndef ACTOR_INCLUDE
#define ACTOR_INCLUDE

#include "thing.h"


enum ActorEvent
{
	MOVE = 3000, 
	LF_MOVE
};

DECLARE_THING_CLASS(CActor, CComplexThing)
public:

							~CActor() ;
	virtual void			Init(CInitThing* init)	;
	virtual	void			SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_ACTOR); }	// FUNCTION: 0x00401540
	virtual void			Move() ;
	virtual void			MoveTo(const FVector& pos) ;
	virtual void			Teleport(const FVector& pos);
	virtual void			TeleportOrientation(const FMatrix& orientation);

	virtual	FVector			GetVelocity() { return mVelocity ; }
	virtual	void			SetVelocity(const FVector& vel) { mVelocity = vel ; }
	virtual	void			AddVelocity(const FVector& vel) { mVelocity+=vel ; }
	virtual	FVector			GetOldPos() {return mOldPos;};
	virtual FMatrix			GetOldOrientation() {return mOldOrientation;};
			void			LowFidelityMove();
			void			AddMoveEvent(CEvent* event) ;


	virtual void			HandleEvent(CEvent* event) ;

	virtual FVector			GetLocalLastFrameMovement() { return mPos - mOldPos; }	// FUNCTION: 0x00401550
	
	virtual	FVector			GetRenderPos();
	virtual	FMatrix			GetRenderOrientation();

	virtual void			Stop()				{ mVelocity=FVector(0,0,0); }	// FUNCTION: 0x00401580
			float			GetFractionTime() ;

	virtual void			StickToGround();

	virtual	BOOL			IsOnGround();
			BOOL			IsInWater();
			BOOL			IsOnObject();

	virtual	void			DeclareOnGround();
	virtual void			DeclareInWater();
	virtual void			DeclareOnObject(CThing* on_object);

			float			GetLastTimeOnGround()	{ return mLastTimeOnGround ; }
			float			GetLastTimeInWater()	{ return mLastTimeInWater; }
			float			GetLastTimeOnObject()	{ return mLastTimeOnObject; }

protected:
	FVector mVelocity;
	FVector	mOldPos;
	FMatrix mOldOrientation;

	float	mLastTimeOnGround,mLastTimeInWater,mLastTimeOnObject;
	float	mLastMoveTime;
	int		mDoFullMoveCount;
};


#endif

