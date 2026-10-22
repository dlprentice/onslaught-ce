// BattleEngineJetPart.cpp: implementation of the CBattleEngineJetPart class.
//
//////////////////////////////////////////////////////////////////////

#include "common.h"
#include "BattleEngineJetPart.h"
#include "BattleEngine.h"
#include "WorldPhysicsManager.h"
#include "Line.h"
#include "Map.h"
#include "BattleEngineDataManager.h"
#include "World.h"
#include "Weapon.h"
#include "EventManager.h"
#include "player.h"
#include "debuglog.h"
#include "Text.h"
#include "Game.h"

#include <stdio.h>

#define kMinManoeuvreVelocitySq			0.3f*0.3f

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

//******************************************************************************************
CBattleEngineJetPart::CBattleEngineJetPart(CBattleEngine* main_part)	// FUNCTION: 0x00410210
{
	mMainPart = main_part ;
	mFlightModel = SIMPLE_JET_FLIGHTMODEL ;

	mCurrentWeapon=0L;
	mLastMoveYVal = 0.0;
	mLastMoveXVal = 0.0;
	
	mDoingLoop=FALSE;
	mLoopHalfway=FALSE;
	mLoopBroken=FALSE;

	mLastStartHardBackwardTime = -10.0;
	mLastStartHardForwardTime = -10.0;
	mLastStartHardLeftTime = -10.0;
	mLastStartHardRightTime = -10.0;
	ResetConfiguration();
	mDoingBarrelCount = 0 ;
	mDoingBarrelLeft = FALSE ;
	mThrusterValue=0.5f;
	mStrafingStartTime=0.0f;
}

//******************************************************************************************
CBattleEngineJetPart::~CBattleEngineJetPart()	// FUNCTION: 0x004102a0
{
	while (CWeapon *weapon=mWeapons.First())
	{
		mWeapons.Remove(weapon);
		delete weapon;
	}
}

//******************************************************************************************
void    CBattleEngineJetPart::Thrust(float vy)	// FUNCTION: 0x00410310
{
	if (mDoingBarrelCount>0)
		return;

	if (!mDoingLoop)
	{
		if (mMainPart->mEnergy)
		{
			mThrusterValue=0.5f-vy/2.0f;

			if ((mLastMoveYVal>-0.6f) && (vy<-0.9f))
				mLastStartHardForwardTime=EVENT_MANAGER.GetTime();

			if (vy>0.8f) { float lastStart=mLastStartHardForwardTime;	// retail: the start time is read once and kept on the x87 stack (004103a0)
				if ((lastStart>EVENT_MANAGER.GetTime()-0.2f) && (lastStart<EVENT_MANAGER.GetTime()-0.1f))	// retail: a window, 0.2 to 0.1 seconds ago (004103a9, 004103c4)
			{
				if (mMainPart->mEnergy>0)
				{
					if (mMainPart->GetVelocity().MagnitudeSq()>kMinManoeuvreVelocitySq)
					{
						mDoingLoop=TRUE;
						mLoopHalfway=FALSE;
						mLoopBroken=FALSE;

						mMainPart->mPitchvel-=0.015f;
					}
					else
					{
						// Not enough velocity
					}
				}
				else
				{
					// Not enough energy to perform loop
					mMainPart->mLowEnergyStartTime=EVENT_MANAGER.GetTime();
				}
			}}	// retail: also closes the vy block
		}
	}

	mLastMoveYVal=vy;
}

//******************************************************************************************
void	CBattleEngineJetPart::Turn(float vx)	// FUNCTION: 0x00410490
{
	if (mDoingLoop) return;
	if (mDoingBarrelCount!=0) return ;

	if (mMainPart->GetVelocity().MagnitudeSq()>0.01f*0.01f)
	{
		float	yawRate=(vx*mMainPart->mConfiguration->mAirTurnRate/94.0f) *mMainPart->ZoomModifier(mMainPart->mZoom);
		float	rollRate=(vx/117.0f)  *mMainPart->ZoomModifier(mMainPart->mZoom);

		if ((mMainPart->GetVelocity().MagnitudeSq()<1.0f*1.0f) && (mMainPart->IsOnGround()))
		{
			float	percent=mMainPart->GetVelocity().Magnitude();

			if (percent<0.1f)
				percent=0;

			yawRate*=percent;
			rollRate*=percent;
		}

		if (mMainPart->mSlowMovement)
		{
			yawRate/=BATTLE_ENGINE_SLOW_MOVEMENT_FACTOR;
			rollRate/=BATTLE_ENGINE_SLOW_MOVEMENT_FACTOR;
		}

		if (mMainPart->mTransformStartTime+1.5f>EVENT_MANAGER.GetTime())
		{
			float	mix=(EVENT_MANAGER.GetTime()-mMainPart->mTransformStartTime)/1.5f;

			yawRate*=mix;
			rollRate*=mix;
		}

		mMainPart->mYawvel-=yawRate;
		mMainPart->mRollvel-=rollRate;
	}
}

//******************************************************************************************
void    CBattleEngineJetPart::Pitch(float vy)	// FUNCTION: 0x00410670
{
	if (mDoingLoop) return ;
	if (mDoingBarrelCount!=0) return ;

	if (mMainPart->GetVelocity().MagnitudeSq()>0.01f*0.01f)
	{
		float	value=(vy/117.0f) *mMainPart->ZoomModifier(mMainPart->mZoom);

		if (mMainPart->mSlowMovement)
			value/=BATTLE_ENGINE_SLOW_MOVEMENT_FACTOR;

		if (mMainPart->mTransformStartTime+1.5f>EVENT_MANAGER.GetTime())
		{
			float	mix=(EVENT_MANAGER.GetTime()-mMainPart->mTransformStartTime)/1.5f;

			value*=mix;
		}

		mMainPart->mPitchvel-=value;
	}
}

//******************************************************************************************
void	CBattleEngineJetPart::YawLeft(float vx)	// FUNCTION: 0x00410740
{
	if (mDoingBarrelCount!=0)
		return;

	if (mDoingLoop)
	{
		if (!mLoopBroken)
		{
			if ((mLastMoveXVal>-0.9f) && (vx<-0.9f))
				mLastStartHardLeftTime=EVENT_MANAGER.GetTime();

			if (vx>0.8f)
			{
				float	t=mLastStartHardRightTime;
				if ((t>EVENT_MANAGER.GetTime()-0.2f) && (t<EVENT_MANAGER.GetTime()-0.1f))	// PC: not within 0.1s (004107cc)
					mLoopBroken=TRUE;
			}
		}
	}
	else
	{
		if ((mLastMoveXVal>-0.9f) && (vx<-0.9f))
			mLastStartHardLeftTime=EVENT_MANAGER.GetTime();

		if ((mLastMoveXVal>-0.8f) && (vx<-0.8f))
		{
			float	t=mLastStartHardRightTime;
			if ((t>(EVENT_MANAGER.GetTime()-0.2f)) &&
				(t<(EVENT_MANAGER.GetTime()-0.1f)))	// PC, as above
			{
				if (mMainPart->mEnergy>0)
				{
					if (mMainPart->GetVelocity().MagnitudeSq()>kMinManoeuvreVelocitySq)
					{
						mDoingBarrelCount = 26;
						FVector forw(-0.2f,0.0f,0.0f) ;
						FMatrix ori(mMainPart->mCurrentOrientation.mYaw,0.0f,0.0f) ;
						FVector move = ori *forw  ;
						mMainPart->AddVelocity(move) ;
						mDoingBarrelLeft = TRUE ;
					}
					else
					{
						// Not enough velocity to perform roll
					}
				}
				else
				{
					// Not enough energy to perform roll
					mMainPart->mLowEnergyStartTime=EVENT_MANAGER.GetTime();
				}
			}
		}

		mLastMoveXVal=vx;

		if ((mMainPart->mEnergy) && (fabsf(vx)>0.5f))
		{
			FVector		acceleration=mMainPart->mOrientation*FVector(vx/300,0,0);

			mMainPart->AddVelocity(acceleration);

			mStrafingStartTime=EVENT_MANAGER.GetTime();
		}
	}
}

//******************************************************************************************
void	CBattleEngineJetPart::YawRight(float vx)	// FUNCTION: 0x004109d0
{
	if (mDoingBarrelCount!=0)
		return;

	if (mDoingLoop)
	{
		if (!mLoopBroken)
		{
			if ((mLastMoveXVal<0.9f) && (vx>0.9f))
				mLastStartHardRightTime=EVENT_MANAGER.GetTime();

			if (vx>0.8f)
			{
				float	t=mLastStartHardLeftTime;
				if ((t>EVENT_MANAGER.GetTime()-0.2f) && (t<EVENT_MANAGER.GetTime()-0.1f))	// PC: not within 0.1s (00410a5c)
					mLoopBroken=TRUE;
			}
		}
	}
	else
	{
		if ((mLastMoveXVal<0.9f) && (vx>0.9f))
			mLastStartHardRightTime=EVENT_MANAGER.GetTime();

		if ((mLastMoveXVal<0.8f) && (vx>0.8f))
		{
			float	t=mLastStartHardLeftTime;
			if ((t>(EVENT_MANAGER.GetTime()-0.2f)) &&
				(t<(EVENT_MANAGER.GetTime()-0.1f)))	// PC, as above
			{
				if (mMainPart->mEnergy>0)
				{
					if (mMainPart->GetVelocity().MagnitudeSq()>kMinManoeuvreVelocitySq)
					{
						mDoingBarrelCount = 26;
						FVector forw(0.2f,0.0f,0.0f) ;
						FMatrix ori(mMainPart->mCurrentOrientation.mYaw,0.0f,0.0f) ;
						FVector move = ori *forw  ;
						mMainPart->AddVelocity(move);
					}
					else
					{
						// Not enough velocity to perform roll
					}
				}
				else
				{
					// Not enough energy to perform roll
					mMainPart->mLowEnergyStartTime=EVENT_MANAGER.GetTime();
				}
			}
		}

		mLastMoveXVal=vx;
		
		if ((mMainPart->mEnergy) && (fabsf(vx)>0.5f))
		{
			FVector		acceleration=mMainPart->mOrientation*FVector(vx/300,0,0);

			mMainPart->AddVelocity(acceleration);

			mStrafingStartTime=EVENT_MANAGER.GetTime();
		}
	}
}

//******************************************************************************************
void	CBattleEngineJetPart::Move()	// FUNCTION: 0x00410c50
{
	if (mMainPart->mPlayer.ToRead())
		mMainPart->mPlayer->IncStat(PS_TIMEASJET);

	for (CWeapon *weapon=mWeapons.First(); weapon; weapon=mWeapons.Next())
		weapon->MoveEmitter(TRUE);

	if ((!mMainPart->mInfinateEnergy) && (mMainPart->mEnergy))
	{
		float	minCost=mMainPart->mConfiguration->mMinAirEnergyCost;
		float	maxCost=mMainPart->mConfiguration->mMaxAirEnergyCost;
		float	cost=(maxCost-minCost)*mThrusterValue+minCost;

		mMainPart->mEnergy-=cost;
		if (mMainPart->mEnergy<0)
			mMainPart->mEnergy=0;
	}

	// Handle thrusters
	if (mMainPart->mEnergy)
	{
		if (mThrusterValue>0.75f)
			mMainPart->mEngineState=kAfterburnerEngines;
		else
			mMainPart->mEngineState=kNormalEngines;
	}
	else
		mMainPart->mEngineState=kEnginesOff;

	if ((mMainPart->mEnergy) && (!mMainPart->IsOnGround()))
	{
		// Handle stalling
		if (mMainPart->mTransformStartTime+2.5f<EVENT_MANAGER.GetTime())
		{
			if (!mMainPart->mStalling)
			{
				if (mMainPart->mVelocity.MagnitudeSq()<0.15f*0.15f)
				{
					mMainPart->mStalling=TRUE;
					mMainPart->mStallTime=EVENT_MANAGER.GetTime();
				}
			}
			else
			{
				if (mMainPart->mVelocity.MagnitudeSq()>=0.15f*0.15f)
				{
					mMainPart->mStalling=FALSE;
				}
				else
				{
					if (mMainPart->mStallTime+2.5f<EVENT_MANAGER.GetTime())
					{
						mMainPart->Morph();
					}
				}
			}
		}
		else
		{
			mMainPart->mStalling=FALSE;
		}

		HandleGroundEffect();

		if (!GetIsDoingSpecialAirMove())
		{
			// Apply the thrusters
			float	minVel=mMainPart->mConfiguration->mMinAirVelocity;
			float	maxVel=mMainPart->mConfiguration->mMaxAirVelocity;
			float	finalVel=(maxVel-minVel)*mThrusterValue+minVel;

			float	velocity=mMainPart->GetVelocity().Magnitude();
			
			FVector move=mMainPart->mOrientation*FVector(0.0f,(finalVel-velocity)/25.0f,0.0f);
		
			mMainPart->AddVelocity(move);
		}

		mThrusterValue=0.5f;

		if (mDoingBarrelCount==0)
		{
			FVector		vel = mMainPart->GetVelocity();
			FVector		new_vel = FVector(0.0f, vel.Magnitude(), 0.0f);
			FVector		new_vel_o = mMainPart->mOrientation * new_vel;
			
			if (mMainPart->mTransformStartTime+2.5f>EVENT_MANAGER.GetTime())
			{
				float	mix=(EVENT_MANAGER.GetTime()-mMainPart->mTransformStartTime)/2.5f;

				new_vel_o=vel*(1-mix)+new_vel_o*mix;
			}
			else if (mStrafingStartTime+4.0f>EVENT_MANAGER.GetTime())
			{
				float	mix=(EVENT_MANAGER.GetTime()-mStrafingStartTime)/4.0f;

				new_vel_o=vel*(1-mix)+new_vel_o*mix;
			}

			mMainPart->SetVelocity(new_vel_o * GetFriction());
		}

		mOnGround=0.0f;
	}
	else
	{
		// Apply friction
		FVector	vel = mMainPart->GetVelocity()*0.95f;
		mMainPart->SetVelocity(vel);

		FVector		new_vel=FVector(0.0f, vel.Magnitude(), 0.0f);
		FVector		new_vel_o=mMainPart->mOrientation * new_vel;

		mMainPart->AddVelocity(new_vel_o*0.05f);
	}

	if ((mMainPart->IsOnGround()) &&
		(mMainPart->GetVelocity().MagnitudeSq()<0.1f*0.1f) &&
		(mOnGround==0.0f))
	{
		mOnGround=EVENT_MANAGER.GetTime()+2.5f;
	}

	if (mMainPart->mTransformStartTime+BATTLE_ENGINE_TRANSFORM_TIME*2<=EVENT_MANAGER.GetTime())
	{
		if (((EVENT_MANAGER.GetTime()>mOnGround) && (mOnGround!=0.0f)) ||
			(mMainPart->GetVelocity().MagnitudeSq()<0.025f*0.025f))
		{
			mMainPart->Morph();
		}
	}

	if (mDoingBarrelCount >0)
	{
		// slow us down cos sideways thrust is converted to forward thrust
		// when barrell roll is over
		if (mDoingBarrelCount <10)
		{
			FVector	vel = mMainPart->GetVelocity()*0.9f;
			mMainPart->SetVelocity(vel);
		}

		if (mDoingBarrelLeft==TRUE)
		{
			mMainPart->mRollvel+=(0.05f) ;
		}
		else
		{
			mMainPart->mRollvel-=(0.05f) ;
		}

		mDoingBarrelCount-- ;
		if (mDoingBarrelCount == 0)
		{
			if (mLoopBroken)
			{
				mMainPart->mCurrentOrientation =  CEulerAngles(mMainPart->mOrientation);
				mMainPart->mOrientation=FMatrix(mMainPart->mCurrentOrientation.mYaw+(float)cosf(mMainPart->mShakeR)*mMainPart->mYawShake,
												mMainPart->mCurrentOrientation.mPitch+(float)cosf(mMainPart->mShakeR)*mMainPart->mPitchShake,
												mMainPart->mCurrentOrientation.mRoll+(float)cosf(mMainPart->mShakeR)*mMainPart->mRollShake);
			
				mLoopBroken=FALSE;
				mDoingLoop=FALSE;
			}

			mDoingBarrelLeft = FALSE ;
		}
	}

	if ((mDoingLoop) && (mDoingBarrelCount==0))
	{
		mMainPart->mPitchvel-=(0.015f);
		
		if (mLoopHalfway)
		{
			if ((mMainPart->mCurrentOrientation.mPitch<0.4f) &&
				(mMainPart->mCurrentOrientation.mPitch>-PI/2))
			{
				mDoingLoop=FALSE;
			}
		}
		else
		{
			if (mMainPart->mCurrentOrientation.mPitch<-PI+0.4f)
			{
				mLoopHalfway=TRUE;

				if (mLoopBroken)
					mDoingBarrelCount=13;
			}
		}
	}

	mMainPart->mShields=0;

	mMainPart->GroundParticleEffect();

	HandleSkimming();
}

//******************************************************************************************
float	CBattleEngineJetPart::Gravity()	// FUNCTION: 0x004114d0
{
	if (mMainPart->mEnergy==0)
		return 0.005f;

	return 0.0f;
}

//******************************************************************************************
void	CBattleEngineJetPart::HandleSkimming()	// FUNCTION: 0x00411500
{
	FVector		pos=mMainPart->mPos;
	float		waterLevel=MAP.GetWaterLevel();
	float		groundLevel=MAP.Collide(pos);

	if (waterLevel<groundLevel)
	{
		float	altitude=waterLevel-pos.Z;

		if (altitude<0.5f)
		{
			float	magnitudeXY=mMainPart->mVelocity.MagnitudeXY();

			if (magnitudeXY>0.3f)
			{
				mMainPart->mVelocity.Z-=magnitudeXY*0.3f;
				mMainPart->mPitchvel-=0.01f*magnitudeXY;
				mMainPart->mVelocity*=0.8f;
				
				float	damage=(0.5f-altitude)*20.0f;
				mMainPart->Damage(damage,NULL,FALSE);

				mMainPart->HostileEnvironment();
			}
		}
	}
}

//******************************************************************************************
void	CBattleEngineJetPart::HandleGroundEffect()	// FUNCTION: 0x00411630
{
	FVector		pos=mMainPart->mPos+(mMainPart->mVelocity*GAME_FR*0.5f);
	float		altitude;
	float		waterLevel=MAP.GetWaterLevel();
	float		groundLevel=MAP.Collide(pos);

	if (waterLevel<groundLevel)
		altitude=waterLevel-pos.Z;
	else
		altitude=groundLevel-pos.Z;

	if (mMainPart->mEnergy)
	{
		if (altitude<5)
		{
			if (altitude<0)
				altitude=0;

			FVector		acceleration=mMainPart->mOrientation*FVector(0,(5-altitude)/400,0);

			mMainPart->AddVelocity(acceleration);

			if ((!mDoingLoop) && (mDoingBarrelCount==0))
			{
				// Apply a hovering effect
				if (mMainPart->mVelocity.Z>0)
					mMainPart->mVelocity.Z*=0.90f;

				// Follow the ground or water normal
				float		beYaw=mMainPart->GetCurrentEulerOrientation().mYaw;
				float		pitch,roll;

				FMatrix ym;
				ym.MakeRotationYawF(beYaw) ;
				FVector yv = ym*FVector(0.0f,1.0f,0.0f);

				FVector		map_normal;

				if (groundLevel<waterLevel)
					map_normal=MAP.Normal(pos);
				else
					map_normal=FVector(0,0,-1);

				FVector		cross_yaw_and_normal = yv ^ map_normal;

				FVector		p=cross_yaw_and_normal ^ map_normal;
				p.Normalise();  // not needed 
				pitch=-p.Z;

				mMainPart->mPitchvel+=(pitch-mMainPart->GetCurrentEulerOrientation().mPitch)*0.02f*(1-altitude/5);

				FVector		r=cross_yaw_and_normal ;
				r.Normalise();  // not needed 
				roll=r.Z*0.5f;

				mMainPart->mRollvel+=(roll-mMainPart->GetCurrentEulerOrientation().mRoll)*0.02f*(1-altitude/5);
			}
		}
	}
}

//******************************************************************************************
float	CBattleEngineJetPart::GetFriction()	// FUNCTION: 0x00411aa0
{
	float	altitude;
	float	waterLevel=MAP.GetWaterLevel();
	float	groundLevel=MAP.Collide(mMainPart->GetPos());

	if (waterLevel<groundLevel)
		altitude=waterLevel-mMainPart->GetPos().Z;
	else
		altitude=groundLevel-mMainPart->GetPos().Z;

	if (altitude<1)
	{
		return 0.99f;
	}
	else if (altitude<3)
	{
		float	velocity=mMainPart->GetVelocity().Magnitude();

		if (velocity<1.5f)
			return 1.0f-(altitude*0.01f);
		else
			return 0.99f;
	}

	return 0.98f;
}

//******************************************************************************************
BOOL	CBattleEngineJetPart::GetIsDoingSpecialAirMove()	// FUNCTION: 0x00411b70
{
	if ((mDoingLoop) || mDoingBarrelCount!=0)
		return TRUE;

	return FALSE;
}

//******************************************************************************************
void	CBattleEngineJetPart::FireWeapon()	// FUNCTION: 0x00411b90
{
	mMainPart->mSlowMovement=FALSE;

	if (CWeapon *weapon=GetCurrentWeapon())
	{
		if (weapon->IsActive())
			weapon->Fire();
	}
}

//******************************************************************************************
void	CBattleEngineJetPart::ChargeWeapon()	// FUNCTION: 0x00411bf0
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
void CBattleEngineJetPart::ChangeWeapon()	// FUNCTION: 0x00411e70
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
CWeapon* CBattleEngineJetPart::GetWeapon(
	SINT	inNumber) 
{
	int		n=0;

	for (CWeapon *weapon=mWeapons.First(); weapon; weapon=mWeapons.Next())
	{
		if (n==inNumber)
			return weapon;

		n++;
	}

	return NULL;
}

//******************************************************************************************
SINT CBattleEngineJetPart::CountWeapons()
{
	SINT	count=0;

	for (CWeapon *weapon=mWeapons.First(); weapon; weapon=mWeapons.Next())
		count++;

	return count;
}

//******************************************************************************************
void	CBattleEngineJetPart::LoseWeaponCharge()	// FUNCTION: 0x00412000
{
	if (CWeapon	*weapon=GetCurrentWeapon())
		weapon->LoseCharge();
}

//******************************************************************************************
BOOL	CBattleEngineJetPart::WeaponFired(	// FUNCTION: 0x00412050
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
					mMainPart->mStoreValue[store]+=weapon->GetConsumption()-kWeaponCoolRate;
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

	return FALSE;
}

//******************************************************************************************
float	CBattleEngineJetPart::GetWeaponAmmoPercentage()	// FUNCTION: 0x004121b0
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
SINT	CBattleEngineJetPart::GetWeaponAmmoCount()	// FUNCTION: 0x00412240
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
BOOL	CBattleEngineJetPart::IsEnergyWeapon()	// FUNCTION: 0x004122b0
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return mMainPart->mStoreHeat[weapon->GetAmmoStore()];

	return FALSE;
}

//******************************************************************************************
BOOL	CBattleEngineJetPart::IsWeaponOverheated()	// FUNCTION: 0x00412310
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return mMainPart->mStoreOverheat[weapon->GetAmmoStore()];

	return FALSE;
}

//******************************************************************************************
float	CBattleEngineJetPart::GetWeaponCharge()	// FUNCTION: 0x00412370
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return weapon->GetCharge();

	return 0.0f;
}

//******************************************************************************************
float	CBattleEngineJetPart::GetWeaponReadiness()
{
	if (CanWeaponFire())
	{
		if (CWeapon *weapon=GetCurrentWeapon())
			return weapon->GetReadiness();
	}

	return 0.0f;
}

//******************************************************************************************
WCHAR*	CBattleEngineJetPart::GetWeaponName()	// FUNCTION: 0x00412420
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return TEXT_DB.GetString(weapon->GetLanguageName());

	return NULL;
}

//******************************************************************************************
char*	CBattleEngineJetPart::GetWeaponPhysicsName()	// FUNCTION: 0x00412480
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return weapon->GetName();

	return NULL;
}

//******************************************************************************************
char*	CBattleEngineJetPart::GetWeaponIconName()	// FUNCTION: 0x004124d0
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return weapon->GetIconName();

	return NULL;
}

//******************************************************************************************
SINT	CBattleEngineJetPart::WhereIsCurrentWeaponAttached()	// FUNCTION: 0x00412520
{
	if (CWeapon *weapon=GetCurrentWeapon())
		return weapon->GetPlacement();

	return 0;
}

//******************************************************************************************
BOOL	CBattleEngineJetPart::CanWeaponFire()	// FUNCTION: 0x00412570
{
	if (CWeapon *weapon=GetCurrentWeapon())
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

	return FALSE;
}

//******************************************************************************************
CWeapon* CBattleEngineJetPart::GetCurrentWeapon()	// FUNCTION: 0x00412610
{
	int		n=0;

	for (CWeapon *weapon=mWeapons.First(); weapon; weapon=mWeapons.Next())
	{
		if (n==mCurrentWeapon)
			return weapon;

		n++;
	}

	return 0L;
}

//******************************************************************************************
void CBattleEngineJetPart::ResetConfiguration()	// FUNCTION: 0x00412650
{
	while (CWeapon *weapon=mWeapons.First())
	{
		mWeapons.Remove(weapon);
		delete weapon;
	}

	CInitEquipment		init;

	for (char **weaponName=mMainPart->mConfiguration->mJetWeapons.First();
		 weaponName;
		 weaponName=mMainPart->mConfiguration->mJetWeapons.Next())
	{
		if (CWeapon *weapon=UPhysicsManager::SpawnWeapon(*weaponName,THING_TYPE_EVERYTHING))
		{
			init.mAttachedTo = mMainPart;

			weapon->Init(init);

			mWeapons.Append(weapon);
		}
	}

	mCurrentWeapon=0;
}

//******************************************************************************************		
#ifdef RESBUILDER
void CBattleEngineJetPart::AccumulateResources( CResourceAccumulator * accumulator )
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
void CBattleEngineJetPart::EnableWeapon(	// FUNCTION: 0x004127a0
	char	*inWeaponName)
{
	for(CWeapon *weapon=mWeapons.First(); weapon; weapon=mWeapons.Next())
	{
		if (strcmp(inWeaponName,weapon->GetName())==0)
			weapon->SetActive(TRUE);
	}
}

//******************************************************************************************		
void CBattleEngineJetPart::DisableWeapon(	// FUNCTION: 0x00412830
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
}

//******************************************************************************************		
BOOL CBattleEngineJetPart::AutoLevel()	// FUNCTION: 0x00412900
{
	if ((mMainPart->IsOnGround()) && (mMainPart->GetVelocity().MagnitudeSq()<0.1f*0.1f))
		return FALSE;

	if (mMainPart->mEnergy<0)
		return FALSE;

	if (mDoingBarrelCount>0)
		return FALSE;
	
	return TRUE;
}

//******************************************************************************************
BOOL CBattleEngineJetPart::IsFiring()
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
SINT CBattleEngineJetPart::CountActiveWeapons()	// FUNCTION: 0x004129a0
{
	SINT					count=0;
	ListIterator<CWeapon>	iterator(&mWeapons);

	for(CWeapon *weapon=iterator.First(); weapon; weapon=iterator.Next())
	{
		if (weapon->IsActive())
			count++;
	}

	return count;
}
// FUNCTION: 0x00410120 init:ZERO_FVECTOR
// FUNCTION: 0x00410140 init:ID_FMATRIX
