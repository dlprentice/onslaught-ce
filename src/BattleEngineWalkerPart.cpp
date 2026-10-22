// BattleEngineWalkerPart.cpp: implementation of the CBattleEngineWalkerPart class.
//
//////////////////////////////////////////////////////////////////////

#include "common.h"
#include "BattleEngineWalkerPart.h"
#include "BattleEngine.h"
#include "WorldPhysicsManager.h"
#include "Line.h"
#include "Map.h"
#include "World.h"
#include "BattleEngineDataManager.h"
#include "Weapon.h"
#include "EventManager.h"
#include "player.h"
#include "Controller.h"
#include "Console.h"
#include "debuglog.h"
#include "Text.h"
#include "SoundManager.h"

#include <stdio.h>

#define DASH_BOOST_VELOCITY 25.0f // dash boost velocity
#define DASH_BOOST_ROLL 0.08f     // dash boost roll velocity

// Also defined in BattleEngine.cpp
#define	ENGINE_VOLUME			1.0f
//0.55f

float CBattleEngineWalkerPart::mDashTime = 0.2f;
float CBattleEngineWalkerPart::mDashStart = 0.9f;
float CBattleEngineWalkerPart::mDashEnd = 0.8f;
int CBattleEngineWalkerPart::mDashLength = 15;
int CBattleEngineWalkerPart::mDashFriction = 5;
float CBattleEngineWalkerPart::mDashVelocity = 25.0f;

//******************************************************************************************
void	CBattleEngineWalkerPart::UpdateWalkCycle()	// FUNCTION: 0x00412ad0
{
	mOldWalkCycle = mWalkCycle;
	FMatrix ori = mMainPart->mOrientation ;
	ori.TransposeInPlace() ;
		
	FVector vel = mMainPart->GetVelocity();
	FVector rel_vel = ori * vel ;

	if (fabsf(rel_vel.X) > fabsf(rel_vel.Y)) 
	{	
		mWalkCycle+=rel_vel.X*2.5f ;
	}
	else
	{
		mWalkCycle+=rel_vel.Y*3.0f ;
	}

	if (mWalkCycle > PI ) mWalkCycle-=PI_M2 ;
	if (mWalkCycle < -PI ) mWalkCycle+=PI_M2 ;
}

//******************************************************************************************
CBattleEngineWalkerPart::CBattleEngineWalkerPart(CBattleEngine* main_part)	// FUNCTION: 0x00412bc0
{
	mMainPart=main_part;
	mWalkCycle=-PI;
	mOldWalkCycle = -PI;

	mCurrentWeapon=0;

	mPrimaryWeapon=NULL;
	mAugWeapon=NULL;

	ResetConfiguration();

	mShieldsRecharging=TRUE;
	mLastMoveXVal = 0 ;
	mLastMoveYVal = 0 ;
	mLastStartHardRightTime = -10.0f ;
	mLastStartHardLeftTime = -10.0f;
	mLastStartHardForwardTime = -10.0f;
	mLastStartHardBackwardTime = -10.0f;
	mDoingDashCount = 0 ;

	CONSOLE.RegisterVariable("g_dash_start","(default 0.9) When the dash sepecial move starts when joy is over this value (0.0..1.0)",CVar_float,&mDashStart);
	CONSOLE.RegisterVariable("g_dash_end","(default 0.8) When the dash sepecial move ends when joy is over this value (0.0..1.0)",CVar_float,&mDashEnd);
	CONSOLE.RegisterVariable("g_dash_time","(default 0.2) Dash move kicks off if start move is done and then end move in less than this time",CVar_float,&mDashTime);
	CONSOLE.RegisterVariable("g_dash_length","(default 15) Number of game turns until user has control of Battle engine again",CVar_int,&mDashLength);
	CONSOLE.RegisterVariable("g_dash_friction","(default 5) Number of game turns left on length when friction kicks in (i.e. stops you)",CVar_int,&mDashFriction);
	CONSOLE.RegisterVariable("g_dash_velocity","(default 25.0) Initial velocity given at start of boost",CVar_float,&mDashVelocity);
}

//******************************************************************************************
CBattleEngineWalkerPart::~CBattleEngineWalkerPart()	// FUNCTION: 0x00412cf0
{
	while (CWeapon *weapon=mWeapons.First())
	{
		mWeapons.Remove(weapon);
		delete weapon;
	}

	delete mPrimaryWeapon;
	delete mAugWeapon;
}

//******************************************************************************************
// return what the current acceleration should be when walking (i.e. takes into account 
// which part of the walk cycle we are in
float  CBattleEngineWalkerPart::GetCurrentAccleration()
{
//	float p = cosf(mWalkCycle*2.0f) * (mMainPart->mConfiguration->mGroundVelocity/50*0.45f) ;
//	if (p<0.0f) p = 0.0f ;

//	return (mMainPart->mConfiguration->mGroundVelocity/50*0.55f)+p;
	return (mMainPart->mConfiguration->mGroundVelocity/50);
}

#include "debuglog.h"

//******************************************************************************************
void	CBattleEngineWalkerPart::Forward(float vy)	// FUNCTION: 0x00412d80
{
	float val = GetCurrentAccleration() * -(vy) ;
	if (!mMainPart->IsOnGround()) return ;
	if (mDoingDashCount > 0 ) return ;
	
//	if (ShouldSlide())	return;


	if (mLastMoveYVal > -mDashStart &&
		vy < -mDashStart &&
		mDoingDashCount == 0)
	{
		mLastStartHardForwardTime = EVENT_MANAGER.GetTime() ;
	
	}

	if (mLastMoveYVal > -mDashEnd &&
		vy < -mDashEnd &&
		mDoingDashCount == 0)
	{
		float	t = mLastStartHardBackwardTime;
		if ((t > ( EVENT_MANAGER.GetTime() - mDashTime)) &&
			(t < ( EVENT_MANAGER.GetTime() - mDashTime * 0.5f)) )	// PC: not within half the dash time (Forward 00412e3d)
		{
			LOG.AddMessage("do dash Forward") ;
			
			SOUND.PlayEffect(mMainPart->mStrafeSound, mMainPart, ENGINE_VOLUME, ST_FOLLOWANDDIE);

			LoseWeaponCharge();
			mMainPart->mZoomOutTime=0.0f;
			
			val = val * mDashVelocity;
			mDoingDashCount = mDashLength ;
		}
	}

	mLastMoveYVal = vy ;

	FVector forw(0.0f,val ,0.0f) ;
	FMatrix ori(mMainPart->mCurrentOrientation.mYaw,0.0f,0.0f) ;
	FVector move = ori *forw  ;
//	move.Z = 0.0f;

	if (mMainPart->mSlowMovement)
		move/=BATTLE_ENGINE_SLOW_MOVEMENT_FACTOR;

	mMainPart->AddVelocity(move);
}

//******************************************************************************************
void	CBattleEngineWalkerPart::Backward(float vy)	// FUNCTION: 0x00412f70
{
	float val = GetCurrentAccleration() * (vy) ;
	if (!mMainPart->IsOnGround()) return ;
	if (mDoingDashCount > 0 ) return ;
//	if (ShouldSlide())return;

	if (mLastMoveYVal < mDashStart &&
		vy > mDashStart &&
		mDoingDashCount == 0)
	{
		mLastStartHardBackwardTime = EVENT_MANAGER.GetTime() ;
	}

	if (mLastMoveYVal < mDashEnd &&
		vy > mDashEnd &&
		mDoingDashCount == 0)
	{
		float	t = mLastStartHardForwardTime;
		if ((t > ( EVENT_MANAGER.GetTime() - mDashTime)) &&
			(t < ( EVENT_MANAGER.GetTime() - mDashTime * 0.5f)) )	// PC, as Forward
		{
			LOG.AddMessage("do dash Backward") ;

			SOUND.PlayEffect(mMainPart->mStrafeSound, mMainPart, ENGINE_VOLUME, ST_FOLLOWANDDIE);

			LoseWeaponCharge();
			mMainPart->mZoomOutTime=0.0f;
			
			val = val * mDashVelocity;
			mDoingDashCount = mDashLength ;
		}
	}

	mLastMoveYVal = vy ;

	FVector forw(0.0f,-val,0.0f) ;
	FMatrix ori(mMainPart->mCurrentOrientation.mYaw,0.0f,0.0f) ;
	FVector move = ori *forw  ;
//	move.Z =0.0f ;

	if (mMainPart->mSlowMovement)
		move/=BATTLE_ENGINE_SLOW_MOVEMENT_FACTOR;

	mMainPart->AddVelocity(move) ;
}

//******************************************************************************************
void	CBattleEngineWalkerPart::StrafeLeft(float vx)	// FUNCTION: 0x00413160
{
	if (!mMainPart->IsOnGround()) return ;
//	if (ShouldSlide())	return;
	if (mDoingDashCount > 0 ) return ;

//	LOG.AddMessage("stafe left vx = %2.8f", vx) ;

	float val = GetCurrentAccleration() * -(vx) ;

	if (mLastMoveXVal > -mDashStart &&
		vx < -mDashStart &&
		mDoingDashCount == 0)
	{
		mLastStartHardLeftTime = EVENT_MANAGER.GetTime() ;
	
	}

	if (mLastMoveXVal >-mDashEnd &&
		vx <-mDashEnd &&
		mDoingDashCount == 0)
	{
		float	t = mLastStartHardRightTime;
		if ((t > ( EVENT_MANAGER.GetTime() - mDashTime)) &&
			(t < ( EVENT_MANAGER.GetTime() - mDashTime * 0.5f)) )	// PC, as Forward
		{
			LOG.AddMessage("do dash LEFT") ;
			
			SOUND.PlayEffect(mMainPart->mStrafeSound, mMainPart, ENGINE_VOLUME, ST_FOLLOWANDDIE);

			LoseWeaponCharge();
			mMainPart->mZoomOutTime=0.0f;
			
			val = val * mDashVelocity;
			mMainPart->mRollvel=+DASH_BOOST_ROLL;
			mDoingDashCount = mDashLength ;
		}
	}

	mLastMoveXVal = vx ;

	FVector forw(-val,0.0f,0.0f) ;
	FMatrix ori(mMainPart->mCurrentOrientation.mYaw,0.0f,0.0f) ;
	FVector move = ori *forw  ;
//	move.Z =0.0f ;

	if (mMainPart->mSlowMovement)
		move/=BATTLE_ENGINE_SLOW_MOVEMENT_FACTOR;

	mMainPart->AddVelocity(move);
}

//******************************************************************************************
void   CBattleEngineWalkerPart::StrafeRight(float vx)	// FUNCTION: 0x00413360
{
	if (!mMainPart->IsOnGround()) return ;

//	if (ShouldSlide())
//		return;

	if (mDoingDashCount > 0 ) return ;

//	LOG.AddMessage("right vx = %2.8f", vx) ;

	float val = GetCurrentAccleration() * (vx) ;

	if (mLastMoveXVal < mDashStart &&
		vx > mDashStart &&
		mDoingDashCount == 0 )
	{
		mLastStartHardRightTime = EVENT_MANAGER.GetTime() ;
		
	}

	if (mLastMoveXVal < mDashEnd &&
		vx > mDashEnd &&
		mDoingDashCount == 0 )
	{
		float	t = mLastStartHardLeftTime;
		if ((t > ( EVENT_MANAGER.GetTime() - mDashTime)) &&
			(t < ( EVENT_MANAGER.GetTime() - mDashTime * 0.5f)) )	// PC, as Forward
		{
			LOG.AddMessage("do dash RIGHT") ;
			
			SOUND.PlayEffect(mMainPart->mStrafeSound, mMainPart, ENGINE_VOLUME, ST_FOLLOWANDDIE);

			LoseWeaponCharge();
			mMainPart->mZoomOutTime=0.0f;

			val = val * mDashVelocity ;
			mMainPart->mRollvel-=DASH_BOOST_ROLL;
			mDoingDashCount = mDashLength ;
		}
	}

	mLastMoveXVal = vx ;

	FVector forw(val,0.0f,0.0f) ;
	FMatrix ori(mMainPart->mCurrentOrientation.mYaw,0.0f,0.0f) ;
	FVector move = ori *forw  ;
//	move.Z =0.0f ;

	if (mMainPart->mSlowMovement)
		val/=BATTLE_ENGINE_SLOW_MOVEMENT_FACTOR;

	mMainPart->AddVelocity(move);
}


//******************************************************************************************
BOOL	CBattleEngineWalkerPart::GetIsDoingSpecialWalkerMove()	// FUNCTION: 0x004135d0
{
	if (mDoingDashCount > 0 ) return TRUE;
	return FALSE ;
}


#include "debuglog.h"

//******************************************************************************************
void	CBattleEngineWalkerPart::ActivateLandingJets()	// FUNCTION: 0x004135e0
{
	FVector		vel=mMainPart->GetVelocity();

	vel.X=-vel.X*0.025f;
	vel.Y=-vel.Y*0.025f;

	if (vel.Z>0.01f)
		vel.Z=-vel.Z*0.075f;
	else
		vel.Z=0;

	mMainPart->AddVelocity(vel);
	mMainPart->mThrustersOn=TRUE;
}

//******************************************************************************************
void	CBattleEngineWalkerPart::Rotate(float vx)	// FUNCTION: 0x00413660
{
	// PC (retail, not in Stuart's): a mouse bound to yaw in either direction turns 1.7 times as fast.
	// STAND-IN (ids): retain the zero-based index's value boundary: retail decrements it before scaling the table address
	// (0041366f), which a plain value local lets VC6 fold into the displacement (89 bytes off).
	float	mult;
	const SINT &playerIndex = mMainPart->mPlayer->GetNumber() - 1;	// STAND-IN (ids), see above
	SINT	type = CONTROL_BINDINGS[1].mInput[playerIndex].mType;
	if ((type == MOUSE_AXIS_MINUS_ANY) || (type == MOUSE_AXIS_PLUS_ANY))
		mult = 1.7f;
	else
		mult = 1.0f;

	float	yawvel = mMainPart->mYawvel;		// STAND-IN (order): read before the zoom modifier's call (00413694); Stuart's `-=` is 107 bytes off
	mMainPart->mYawvel = yawvel - mMainPart->ZoomModifier(mMainPart->mZoom) * ((mult * vx) * mMainPart->mConfiguration->mGroundTurnRate/75.0f);
}

//******************************************************************************************
void	CBattleEngineWalkerPart::Pitch(float vy)	// FUNCTION: 0x004136e0
{
	// PC: as Rotate (the yaw binding's type, 004136f6); the same two STAND-INs (the plain index local is 78 bytes off,
	// `-=` 95)
	float	mult;
	const SINT &playerIndex = mMainPart->mPlayer->GetNumber() - 1;	// STAND-IN (ids), see Rotate
	SINT	type = CONTROL_BINDINGS[1].mInput[playerIndex].mType;
	if ((type == MOUSE_AXIS_MINUS_ANY) || (type == MOUSE_AXIS_PLUS_ANY))
		mult = 1.7f;
	else
		mult = 1.0f;

	float	pitchvel = mMainPart->mPitchvel;	// STAND-IN (order), see Rotate
	mMainPart->mPitchvel = pitchvel - (mult*vy/117.0f) * mMainPart->ZoomModifier(mMainPart->mZoom);
}

#include "debuglog.h"

//******************************************************************************************
void	CBattleEngineWalkerPart::Move()	// FUNCTION: 0x00413760
{
	mMainPart->mEngineState=kEnginesOff;

	// Handle Aug Weapon
	if (!GetCurrentWeapon())
		mCurrentWeapon=0;

	if (mMainPart->mPlayer.ToRead())
		mMainPart->mPlayer->IncStat(PS_TIMEASWALKER);

	for (CWeapon *weapon=mWeapons.First(); weapon; weapon=mWeapons.Next())
		weapon->MoveEmitter(FALSE);

	if (EVENT_MANAGER.GetTime()-mMainPart->mLastTimeOnGround<0.3f)
	{
		if ((!mMainPart->mInfinateEnergy) && (!mMainPart->mCloaked))
		{
			float	recharge=mMainPart->mConfiguration->mGroundEnergyIncrease;
			
			if (!mShieldsRecharging)
				recharge/=2;

			mMainPart->mEnergy+=recharge;
			if (mMainPart->mEnergy>mMainPart->mConfiguration->mEnergy)
				mMainPart->mEnergy=mMainPart->mConfiguration->mEnergy;
		}
	}

	mShieldsRecharging=TRUE;
	mMainPart->mShields=mMainPart->mEnergy;

	if (GoingIntoWater())
	{
		mMainPart->SetVelocity(FVector(0,0,mMainPart->GetVelocity().Z));
	}
	else if (ShouldSlide())
	{
		mMainPart->SetFlags(mMainPart->GetFlags() | TF_SLIDE) ;
		Slide();
	}
	else
	{
		short flags = mMainPart->GetFlags();
		mMainPart->SetFlags(flags &= ~TF_SLIDE) ;
	}

	if (!GoingIntoWater() )
	{
		if (mMainPart->IsOnGround() || mDoingDashCount != 0 )
		{
			mMainPart->SetVelocity(mMainPart->GetVelocity()*mMainPart->mConfiguration->mWalkFriction);
			FVector	temp=mMainPart->GetVelocity();
			temp.Z=0;

			float	m=temp.Magnitude();
			float	mv=mMainPart->mConfiguration->mMaxWalkVelocity;

			if (m>mv && mDoingDashCount < mDashFriction)
			{
				temp*=(mv/m);
				FVector new_vel(0.0f,0.0f,0.0f) ;

				new_vel.X=temp.X;
				new_vel.Y=temp.Y;
				new_vel.Z=mMainPart->GetVelocity().Z;
				mMainPart->SetVelocity(new_vel) ;
			}
		}
	}

	if (mDoingDashCount > 0)
	{
		mDoingDashCount-- ;
	}

	if (mMainPart->IsWalking())
		UpdateWalkCycle();
}

//******************************************************************************************
BOOL CBattleEngineWalkerPart::GoingIntoWater()	// FUNCTION: 0x00413a70
{
	if ((mMainPart->IsOnGround()) && (!mMainPart->IsOnObject()))
	{
		float	waterLevel=MAP.GetWaterLevel();
		
		if (MAP.Collide(mMainPart->GetPos())-waterLevel>0.3f)
		{
			if (MAP.Collide(mMainPart->GetPos()+mMainPart->GetVelocity())>MAP.Collide(mMainPart->GetPos()))
				return TRUE;
		}
		else if (MAP.Collide(mMainPart->GetPos()+mMainPart->GetVelocity())-waterLevel>0.3f)
		{
			return TRUE;
		}
	}

	return FALSE;
}


//******************************************************************************************
BOOL CBattleEngineWalkerPart::ShouldSlide()
{
	if (((mMainPart->IsOnGround()) || (mMainPart->GetFlags() & TF_SLIDE)) && (!mMainPart->IsOnObject()))
	{
		float	angle=PI/2+MAP.Normal(mMainPart->mPos+ mMainPart->mVelocity).Elevation();

		if (angle>40.0f*PI/180)
			return TRUE;
	}

	return FALSE;
}

//******************************************************************************************
void CBattleEngineWalkerPart::Slide()	// FUNCTION: 0x00413b90
{

	float backoff ;
	FVector change;
	int count = 0;

	while (count<6)
	{
		FVector vel = 	mMainPart->GetVelocity();
		FVector slidePlaneNormal = MAP.Normal(mMainPart->mPos+vel) ;

		slidePlaneNormal.Z = 0.0f;
		slidePlaneNormal.Normalise() ;
	
		if ((vel * slidePlaneNormal) >-0.00001f) return ;

		backoff = (vel * slidePlaneNormal);
		change = slidePlaneNormal*backoff;
		FVector cand_vel = vel - change ;

		mMainPart->SetVelocity(cand_vel);
		count++;
	}

}


//******************************************************************************************
void	CBattleEngineWalkerPart::FireWeapon()	// FUNCTION: 0x00413cc0
{
	mMainPart->mSlowMovement=FALSE;

	if (CWeapon *weapon=GetCurrentWeapon())
	{
		if (weapon->IsActive())
			weapon->Fire();
	}
}

//******************************************************************************************
void	CBattleEngineWalkerPart::ChargeWeapon()	// FUNCTION: 0x00413cf0
{
	if (CWeapon *weapon=GetCurrentWeapon())
	{
		if (weapon->IsActive())
		{
			if (weapon->ReadyToCharge())
			{
				if (weapon->CanCharge())
				{
					int		store=weapon->GetAmmoStore();
			
					if (((mMainPart->mStoreHeat[store]) || (mMainPart->mStoreValue[store]>0)) &&
						(!weapon->FullyCharged()) && (!mMainPart->mStoreOverheat[store]))
					{
						mMainPart->mSlowMovement=!weapon->AllowMovement();
						weapon->Charge();
						mShieldsRecharging=FALSE;
					
						if (mMainPart->mStoreHeat[store])
						{
							if ((mMainPart->mStoreValue[store]<mMainPart->mConfiguration->mStoreValue[store]) &&
								(!mMainPart->mStoreOverheat[store]))
							{
								mMainPart->mStoreValue[store]+=weapon->GetConsumption();
							}
							else
							{
								mMainPart->mStoreOverheat[store]=true;
								mMainPart->WeaponOverheated();
								FireWeapon();
							}
						}
					}
				}
				else
					FireWeapon();
			}
		}
	}
}

//******************************************************************************************
void CBattleEngineWalkerPart::ChangeWeapon()	// FUNCTION: 0x00413eb0
{
	EZoomMode	oldZoomMode=GetCurrentWeapon()->GetZoomMode();

	SINT	n=mCurrentWeapon+1;
	SINT	totalWeapons=CountWeapons();
	BOOL	changedWeapon=FALSE;

	while (n!=mCurrentWeapon)
	{
		if (CWeapon	*weapon=GetWeapon(n))
		{
			SINT	store=weapon->GetAmmoStore();

			if ((weapon->IsActive()) &&
				((mMainPart->mStoreHeat[store]) ||
				 (mMainPart->mStoreValue[store]>=weapon->GetConsumption())))
			{
				mCurrentWeapon=n;
				changedWeapon=TRUE;
				break;
			}
		}

		n++;
		if (n>=totalWeapons)
			n=0;
	}

	if (changedWeapon)
	{
		mMainPart->mSlowMovement=FALSE;
		LoseWeaponCharge();

		if (oldZoomMode!=GetCurrentWeapon()->GetZoomMode())
			mMainPart->AutoZoomOut();
	}
}

//******************************************************************************************
void	CBattleEngineWalkerPart::LoseWeaponCharge()	// FUNCTION: 0x00414010
{
	if (CWeapon	*weapon=GetCurrentWeapon())
		weapon->LoseCharge();
}

//******************************************************************************************
CWeapon* CBattleEngineWalkerPart::GetCurrentWeapon()	// FUNCTION: 0x00414030
{
	if (mCurrentWeapon==0)
	{
		if ((mMainPart->IsAugActive()) && (mAugWeapon))
			return mAugWeapon;
		else if (mPrimaryWeapon)
			return mPrimaryWeapon;
	}

	SINT		n=0;

	if (mPrimaryWeapon)
		n++;

	for (CWeapon *weapon=mWeapons.First(); weapon; weapon=mWeapons.Next())
	{
		if (n==mCurrentWeapon)
			return weapon;

		n++;
	}

	mCurrentWeapon=0;

	if (mPrimaryWeapon)
		return mPrimaryWeapon;

	if (mWeapons.First())
		return mWeapons.First();

	return NULL;
}

//******************************************************************************************
CWeapon* CBattleEngineWalkerPart::GetWeapon(
	SINT	inNumber) 
{
	if ((inNumber==0) && (mPrimaryWeapon))
	{
		if ((mMainPart->IsAugActive()) && (mAugWeapon))
			return mAugWeapon;
		else
			return mPrimaryWeapon;
	}

	SINT		n=0;

	if (mPrimaryWeapon)
		n++;

	for (CWeapon *weapon=mWeapons.First(); weapon; weapon=mWeapons.Next())
	{
		if (n==inNumber)
			return weapon;

		n++;
	}

	return NULL;
}

//******************************************************************************************
SINT CBattleEngineWalkerPart::CountWeapons()
{
	SINT	count=0;

	if (mPrimaryWeapon)
		count++;

	for (CWeapon *weapon=mWeapons.First(); weapon; weapon=mWeapons.Next())
		count++;

	return count;
}

//******************************************************************************************
BOOL	CBattleEngineWalkerPart::WeaponFired(	// FUNCTION: 0x004140d0
	CWeapon		*inWeapon)
{
	for (CWeapon *weapon=mWeapons.First(); weapon; weapon=mWeapons.Next())
	{
		if (weapon==inWeapon)
		{
			int		store=weapon->GetAmmoStore();

			if (mMainPart->mStoreHeat[store])
			{
				if (weapon->Charged())
					return TRUE;

				if ((mMainPart->mStoreValue[store]<mMainPart->mConfiguration->mStoreValue[store]) && 
					(!mMainPart->mStoreOverheat[store]))
				{
					mMainPart->mStoreValue[store]+=weapon->GetConsumption();
					mShieldsRecharging=FALSE;
					return TRUE;
				}
				else
				{
					mMainPart->mStoreOverheat[store]=true;
					mMainPart->WeaponOverheated();
				}
			}
			else
			{
				if (mMainPart->mStoreValue[store]>0)
				{
					mMainPart->mStoreValue[store]-=weapon->GetConsumption();

					if (mMainPart->mStoreValue[store]<0)
						mMainPart->mStoreValue[store]=0;

					return TRUE;
				}
				else
				{
					if (mMainPart->mAmmoDepletedTime<EVENT_MANAGER.GetTime()-8.0f)
						mMainPart->mAmmoDepletedTime=EVENT_MANAGER.GetTime();
				}
			}
		}
	}

	// Handle the Primary weapon
	if ((mPrimaryWeapon) && (inWeapon==mPrimaryWeapon))
	{
		int		store=mPrimaryWeapon->GetAmmoStore();

		if (mMainPart->mStoreHeat[store])
		{
			if (mPrimaryWeapon->Charged())
				return TRUE;

			if ((mMainPart->mStoreValue[store]<mMainPart->mConfiguration->mStoreValue[store]) && 
				(!mMainPart->mStoreOverheat[store]))
			{
				mMainPart->mStoreValue[store]+=mPrimaryWeapon->GetConsumption();
				mShieldsRecharging=FALSE;
				return TRUE;
			}
			else
			{
				mMainPart->mStoreOverheat[store]=true;
				mMainPart->WeaponOverheated();
			}
		}
		else
		{
			if (mMainPart->mStoreValue[store]>0)
			{
				mMainPart->mStoreValue[store]-=mPrimaryWeapon->GetConsumption();

				if (mMainPart->mStoreValue[store]<0)
					mMainPart->mStoreValue[store]=0;

				return TRUE;
			}
			else
			{
				if (mMainPart->mAmmoDepletedTime<EVENT_MANAGER.GetTime()-8.0f)
					mMainPart->mAmmoDepletedTime=EVENT_MANAGER.GetTime();
			}
		}
	}

	// Handle the Aug weapon
	if ((mAugWeapon) && (inWeapon==mAugWeapon))
	{
		int		store=mAugWeapon->GetAmmoStore();

		if (mMainPart->mStoreHeat[store])
		{
			if (mAugWeapon->Charged())
			{
				mMainPart->mAugValue=0.0f;
				return TRUE;
			}

			if ((mMainPart->mStoreValue[store]<mMainPart->mConfiguration->mStoreValue[store]) && 
				(!mMainPart->mStoreOverheat[store]))
			{
				mMainPart->mStoreValue[store]+=mAugWeapon->GetConsumption();
				mShieldsRecharging=FALSE;
				mMainPart->mAugValue=0.0f;
				return TRUE;
			}
			else
			{
				mMainPart->mStoreOverheat[store]=true;
				mMainPart->WeaponOverheated();
			}
		}
		else
		{
			if (mMainPart->mStoreValue[store]>0)
			{
				mMainPart->mStoreValue[store]-=mAugWeapon->GetConsumption();
				mMainPart->mAugValue=0.0f;

				if (mMainPart->mStoreValue[store]<0)
					mMainPart->mStoreValue[store]=0;

				return TRUE;
			}
			else
			{
				if (mMainPart->mAmmoDepletedTime<EVENT_MANAGER.GetTime()-8.0f)
					mMainPart->mAmmoDepletedTime=EVENT_MANAGER.GetTime();
			}
		}
	}

	return FALSE;
}

//******************************************************************************************
float	CBattleEngineWalkerPart::GetWeaponAmmoPercentage()	// FUNCTION: 0x00414410
{
	float	value=0;

	if (CWeapon *weapon=GetCurrentWeapon())
	{
		int		store=weapon->GetAmmoStore();

		if (mMainPart->mStoreHeat[store])
			value=mMainPart->mStoreValue[store]/mMainPart->mConfiguration->mStoreValue[store];
		else
			value=(float)mMainPart->mStoreValue[store]/(float)mMainPart->mConfiguration->mStoreValue[store];
	
		if (value>1.0f)
			value=1.0f;
	}

	return value;
}

//******************************************************************************************
SINT	CBattleEngineWalkerPart::GetWeaponAmmoCount()	// FUNCTION: 0x00414470
{
	if (CWeapon *weapon=GetCurrentWeapon())
	{
		int		store=weapon->GetAmmoStore();

		if (!mMainPart->mStoreHeat[store])
			return (SINT)mMainPart->mStoreValue[store];
	}

	return 0;
}

//******************************************************************************************
BOOL	CBattleEngineWalkerPart::IsEnergyWeapon()	// FUNCTION: 0x004144c0
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return mMainPart->mStoreHeat[weapon->GetAmmoStore()];

	return FALSE;
}

//******************************************************************************************
BOOL	CBattleEngineWalkerPart::IsWeaponOverheated()	// FUNCTION: 0x004144f0
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return mMainPart->mStoreOverheat[weapon->GetAmmoStore()];

	return FALSE;
}

//******************************************************************************************
float	CBattleEngineWalkerPart::GetWeaponCharge()	// FUNCTION: 0x00414520
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return weapon->GetCharge();

	return 0.0f;
}

//******************************************************************************************
float	CBattleEngineWalkerPart::GetWeaponReadiness()
{
	if (CanWeaponFire())
	{
		if (CWeapon *weapon=GetCurrentWeapon())
			return weapon->GetReadiness();
	}

	return 0.0f;
}

//******************************************************************************************
WCHAR*	CBattleEngineWalkerPart::GetWeaponName()	// FUNCTION: 0x004145a0
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return TEXT_DB.GetString(weapon->GetLanguageName());

	return NULL;
}

//******************************************************************************************
char*	CBattleEngineWalkerPart::GetWeaponPhysicsName()	// FUNCTION: 0x004145d0
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return weapon->GetName();

	return NULL;
}

//******************************************************************************************
char*	CBattleEngineWalkerPart::GetWeaponIconName()	// FUNCTION: 0x004145f0
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return weapon->GetIconName();

	return NULL;
}

//******************************************************************************************
SINT	CBattleEngineWalkerPart::WhereIsCurrentWeaponAttached()	// FUNCTION: 0x00414610
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return weapon->GetPlacement();

	return 0;
}

//******************************************************************************************
BOOL	CBattleEngineWalkerPart::CanWeaponFire()	// FUNCTION: 0x00414630
{
	if (CWeapon *weapon=GetCurrentWeapon())
	{
		if (weapon->IsActive())
		{
			int		store=weapon->GetAmmoStore();

			if (mMainPart->mStoreHeat[store])
			{
				if ((mMainPart->mStoreValue[store]<mMainPart->mConfiguration->mStoreValue[store]) &&
					(!mMainPart->mStoreOverheat[store]))
				{
					return TRUE;
				}
			}
			else
			{
				if (mMainPart->mStoreValue[store]>0)
					return TRUE;
			}
		}
	}

	return FALSE;
}

//******************************************************************************************
void CBattleEngineWalkerPart::ResetConfiguration()	// FUNCTION: 0x004146b0
{
	while (CWeapon *weapon=mWeapons.First())
	{
		mWeapons.Remove(weapon);
		delete weapon;
	}

	delete mPrimaryWeapon;
	mPrimaryWeapon=NULL;

	delete mAugWeapon;
	mAugWeapon=NULL;

	CInitEquipment		init;
	CWeapon				*weapon;

	for (char **weaponName=mMainPart->mConfiguration->mWalkerWeapons.First();
		 weaponName;
		 weaponName=mMainPart->mConfiguration->mWalkerWeapons.Next())
	{
		if (weapon=UPhysicsManager::SpawnWeapon(*weaponName,THING_TYPE_EVERYTHING))
		{
			init.mAttachedTo = mMainPart;

			weapon->Init(init);

			mWeapons.Append(weapon);
		}
	}

	// Attach the primary weapon
	if (mPrimaryWeapon=UPhysicsManager::SpawnWeapon(mMainPart->mConfiguration->mPrimaryWeapon,THING_TYPE_EVERYTHING))
	{
		init.mAttachedTo = mMainPart;

		mPrimaryWeapon->Init(init);
	}

	// Attach the aug weapon
	if (mAugWeapon=UPhysicsManager::SpawnWeapon(mMainPart->mConfiguration->mAugWeapon,THING_TYPE_EVERYTHING))
	{
		init.mAttachedTo = mMainPart;

		mAugWeapon->Init(init);
	}

	mCurrentWeapon=0;
}

//******************************************************************************************		
#ifdef RESBUILDER
void CBattleEngineWalkerPart::AccumulateResources( CResourceAccumulator * accumulator )
{
	char filename[100];

	CWeapon * lpWeapon;
	for( lpWeapon = mWeapons.First(); lpWeapon; lpWeapon = mWeapons.Next() )
	{
		sprintf(filename,"hud\\%s.tga",lpWeapon->GetIconName());
		accumulator->AddTexture( CTEXTURE::GetTextureByName( filename ) );
	}

}
#endif
//******************************************************************************************		
void CBattleEngineWalkerPart::EnableWeapon(	// FUNCTION: 0x00414970
	char	*inWeaponName)
{
	for(CWeapon *weapon=mWeapons.First(); weapon; weapon=mWeapons.Next())
	{
		if (strcmp(inWeaponName,weapon->GetName())==0)
			weapon->SetActive(TRUE);
	}
	
	// Test the primary
	if (strcmp(inWeaponName,mPrimaryWeapon->GetName())==0)
		mPrimaryWeapon->SetActive(TRUE);
}

//******************************************************************************************		
void CBattleEngineWalkerPart::DisableWeapon(	// FUNCTION: 0x00414a40
	char	*inWeaponName)
{
	ListIterator<CWeapon>	iterator(&mWeapons);

	for(CWeapon *weapon=iterator.First(); weapon; weapon=iterator.Next())
	{
		if (strcmp(inWeaponName,weapon->GetName())==0)
		{
			weapon->SetActive(FALSE);

			// If we have that weapon selected then skip it
			if (GetCurrentWeapon()==weapon)
				ChangeWeapon();
		}
	}

	// Test the primary
	if (strcmp(inWeaponName,mPrimaryWeapon->GetName())==0)
		mPrimaryWeapon->SetActive(FALSE);

	if (GetCurrentWeapon()==mPrimaryWeapon)
		ChangeWeapon();
}

//******************************************************************************************
BOOL CBattleEngineWalkerPart::IsFiring()	// FUNCTION: 0x00414b30
{
	ListIterator<CWeapon>	iterator(&mWeapons);

	for(CWeapon *weapon=iterator.First(); weapon; weapon=iterator.Next())
	{
		if (weapon->IsFiring())
			return TRUE;
	}

	return FALSE;
}

//******************************************************************************************
SINT CBattleEngineWalkerPart::CountActiveWeapons()	// FUNCTION: 0x00414b70
{
	SINT					count=0;
	ListIterator<CWeapon>	iterator(&mWeapons);

	for(CWeapon *weapon=iterator.First(); weapon; weapon=iterator.Next())
	{
		if (weapon->IsActive())
			count++;
	}

	if (mPrimaryWeapon)
	{
		if (mPrimaryWeapon->IsActive())
			count++;
	}

	return count;
}
// FUNCTION: 0x004129e0 init:ZERO_FVECTOR
// FUNCTION: 0x00412a00 init:ID_FMATRIX
