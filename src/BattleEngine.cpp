// BattleEngine.cpp: implementation of the CBattleEngine class.
//
//////////////////////////////////////////////////////////////////////

#include "common.h"
class CExplosionInitThing;class CLine;class CBattleEngine;class CGeneralVolume;class CDXCockpit;class CEquipment;class CMCBattleEngine;class CRadarWarningReceiver;class CCylinder;	// STAND-IN (end-stage RTTI order): retail's ??_R4 order implies these type records were created in this order (an unidentified header mention); found by an RTTI-order search
#if TARGET != PS2
#include "ibuffer.h"
#endif
#include "BattleEngine.h"
#include "fcoords.h"
#include "EventManager.h"
#include "debuglog.h"
#include "CollisionSeekingThing.h"
#include "World.h"
#include "Cockpit.h"
#include "ParticleSet.h"
#include "ParticleManager.h"
#include "Game.h"
#include "Line.h"
#include "Map.h"
#include "BattleEngineWalkerPart.h"
#include "BattleEngineJetPart.h"
#include "RadarWarningReceiver.h"
#include "Weapon.h"
#include "cylinder.h"
#include "BattleEngineConfigurations.h"
#include "BattleEngineDataManager.h"
#include "player.h"
#include "mesh.h"
#include "MCBattleEngine.h"
#include "console.h"
#include "mcbuggy.h"
#include "SoundManager.h"
#include "Engine.h"
#include "FearGrid.h"
#include "Explosion.h"
#include "text.h"
#include "Feature.h"
#include "CLIParams.h"
#include "MeshRenderer.h"
#include "MeshPose.h"
#include "InfluenceMap.h"	// not in Stuart's list; the retail __LINE__ values run one past his file from here on
#include <stdio.h>

#define WALK_ACCELERATION 0.025f
#define WALK_FRICTION 0.7f

#define ZOOM_SPEED				0.1f
#define MAX_ZOOM_IN				0.4f
#define MAX_CHARGE_ZOOM_IN		0.2f
#define MAX_ZOOM_OUT			1.0f

#define MAX_AUG_VALUE			10.0f
#define AUG_DECREASE_RATE		0.01f

CParticleDescriptor		*CBattleEngine::sWaterEffect;
CParticleDescriptor		*CBattleEngine::sLandEffect;
// Also defined in BattleEngineWalkerPart.cpp
#define	ENGINE_VOLUME			1.0f
//0.55f

//******************************************************************************************
void	CBattleEngine::Init(CInitThing* init)	// FUNCTION: 0x00404dd0
{
	mEnergyVeryLowSound=NULL;

	mInFlightSound=SOUND.GetEffectByName("BE Engines(in-flight)");
	mLandingSound=SOUND.GetEffectByName("BE Engines(land)");
	mTakeOffSound=SOUND.GetEffectByName("BE Engines(takeoff)");
	mHealthLowSound=SOUND.GetEffectByName("BE Energy Critical");
	mEnergyLowSound=SOUND.GetEffectByName("BE Energy Low");
	//mEnergyVeryLowSound=SOUND.GetEffectByName("BE Energy Critical");
	mStrafeSound=SOUND.GetEffectByName("BE Strafe L/R");
	mTargetLockedSound=SOUND.GetEffectByName("BE Target Locked");
	mIncomingMissileSound=SOUND.GetEffectByName("BE Incoming Missile");
	mAutoAimSound=SOUND.GetEffectByName("BE Target");
	mBattleEngineOnSound=SOUND.GetEffectByName("BE On 02");
	//mBattleEngineFeetSound=SOUND.GetEffectByName("BE Feet");
	mPneumaticSound=SOUND.GetEffectByName("BE Hydraulics 02");

	mThrusterEffect=PARTICLE_SET.GetPD("be thruster effect");
	mEngineEffect=PARTICLE_SET.GetPD("be engine effect");
	mAfterburnerEffect=PARTICLE_SET.GetPD("be afterburner effect");

	sWaterEffect=PARTICLE_SET.GetPD("BE Ground Effect Water Effect");
	sLandEffect=PARTICLE_SET.GetPD("BE Ground Effect Land Effect");

	CBattleEngineInitThing	*beInit=(CBattleEngineInitThing*)init;
	
	mJetPart=NULL;
	mWalkerPart=NULL;
	mCurrentSafeTry = 0 ;

	mConfigurationId=beInit->mConfigurationId;
	mConfiguration=NULL;
	UpdateConfiguration();

	mWalkerPart = new( MEMTYPE_BATTLEENGINE ) CBattleEngineWalkerPart(this) ;
	mJetPart = new( MEMTYPE_BATTLEENGINE ) CBattleEngineJetPart(this) ;
	mCurrentUnitOverCrosshair = NULL ;
	mCurrentUnitOverCrosshairRegardlessOfRange = NULL ;
	mLife=mConfiguration->mLife;
	mEnergy=mConfiguration->mEnergy;
	mStandingOnObjectMovement= FVector(0.0f,0.0f,0.0f);
	mNotMovingCount =0;
	mWalkSoundTime=0.0f;
	mWalkSounds=0;

	mTakeOffTime=0.0f;

	mSlowMovement=FALSE;
	mFlightModeActive=TRUE;
	mLastTimeHitObjectAndDamaged = -20 ;
	mOtherMeshHackRenderThing = NULL ;
	mHackRenderThingIsWalker = TRUE;
	mHackBackupMC = NULL ;
	mZoomOutTime=0.0f;
	mHitDamageDone=FALSE;
	mStealth=0.0f;
	mDesiredStealth=0.0f;
	mVibration=0;
	mWlcr.mHitType=kCollideNothing;
	mCurrentTarget=0;
	mInSafeCollisionPlace = TRUE ;
	mPlayIncomingMissileSound = FALSE;
	mHadAutoAimTarget=NULL;

	if (beInit->mPlaneMode)
	{
		mState=BATTLE_ENGINE_STATE_JET;
		mShields=0;
	}
	else
	{
		mState=BATTLE_ENGINE_STATE_WALKER;
		mShields=mEnergy;
	}

	if (GAME.IsMultiplayer() == FALSE)
	{
		init->mInitCST.mMaxCollision	= ECL_APPROX_GEOMETRY_SHAPES ;
	}

	// Mesh setup

	CRenderData			data;
	
	data.mInit[0].mThing=this;
	data.mRTID[0]=RTID_CRTMesh;	


	// ### SRG hack because the battle engine in a different state, is a different MESH!
	if (init->mAllegiance==kMuspellAllegiance)
		strcpy(data.mInit[0].mName,"m_be1.msh");
	else
		strcpy(data.mInit[0].mName,"f_be1.msh"); // f_be1.msh

	if ((mRenderThing=SpawnRenderThing(data.mRTID[0])))
		mRenderThing->Init(&data.mInit[0]);

	if (init->mAllegiance==kMuspellAllegiance)
		strcpy(data.mInit[0].mName,"m_be2.msh");
	else
		strcpy(data.mInit[0].mName,"f_be2.msh");

	if ((mOtherMeshHackRenderThing=SpawnRenderThing(data.mRTID[0])))
		mOtherMeshHackRenderThing->Init(&data.mInit[0]);

	// do this before mc setup!!
	SINT	n=1;

	if (mRenderThing)
	{
		while (mRenderThing->GetRTEmitter("Thruster",n)!=FVector(0,0,0))
		{
			CValidatedFoR	*frame=new( MEMTYPE_PARTICLE ) CValidatedFoR();
			mThrusters.Append(frame);
			n++;
		}
	}

	n=1;

	if (mOtherMeshHackRenderThing)
	{
		while (mOtherMeshHackRenderThing->GetRTEmitter("Engine",n)!=FVector(0,0,0))
		{
			CValidatedFoR	*frame=new( MEMTYPE_PARTICLE ) CValidatedFoR();
			mEngines.Append(frame);
			n++;
		}
	}

	// Check if this is a walker-style mech, and load the motion controller
	// if so
	
	if (mRenderThing->GetRTMesh()->GetAnimModeByName("LegMotion")!=AM_INVALID)
	{
		mMotionController = new( MEMTYPE_BATTLEENGINE ) CMCBattleEngine(this);
		((CMCBattleEngine *) mMotionController)->SetMechParameters(0.0f,400.0f,1.0f, 0.4f, 0.9f,0.05f,2);

		// SRG massive hack (part 1) sorry not my fault stops legs going through carrier on 231 etc

		LOG.AddMessage("currently running level = %d", GAME.GetCurrentlyRunningLevelNum() ) ;

		// -> -> -> -> ->  5 axes awarded for this HACK!
		if (GAME.GetCurrentlyRunningLevelNum() == 231 ||
			GAME.GetCurrentlyRunningLevelNum() == 232 ||
			GAME.GetCurrentlyRunningLevelNum() == 331 ||
			GAME.GetCurrentlyRunningLevelNum() == 221 ||
			GAME.GetCurrentlyRunningLevelNum() == 222 ||
			GAME.GetCurrentlyRunningLevelNum() == 524 ||
			GAME.GetCurrentlyRunningLevelNum() == 523 ||
			GAME.GetCurrentlyRunningLevelNum() == 332 )
		{
			((CMCBattleEngine *) mMotionController)->SetAnimationBlend(1.0f) ;
		}

	}
//	else if (mRenderThing->GetRTMesh()->GetAnimModeByName("WheelMotion")!=AM_INVALID)
//	{
//		mMotionController = new( MEMTYPE_BATTLEENGINE ) CMCBuggy(this);
//		((CMCBuggy *) mMotionController)->SetBuggyParameters(0.0f);
//	}
	else
		mMotionController = NULL;

	// Initially invisible

	mVisible=FALSE;

	CONSOLE.RegisterVariable("cg_battleenginevisible","Is the battle engine visible?",CVar_bool,&mVisible);



	// End mesh setup

//	CCylinder* shape = new CCylinder(GetRadius(), COfGHeight() * 0.6f) ;
//	mCollisionSeekingThing->SetShape(shape) ;
	
	SUPERTYPE::Init(init) ;



	SetCollisionShape() ;

	mYawvel =0.0f ;
	mPitchvel =0.0f ;
	mRollvel = 0.0f ;
	mZoom = 1.0f ;
	mDesiredZoom = 1.0f;
	mOldZoom=1.0f;
	mPoweredUp = TRUE ;
	mShieldRegeneration=0;

	mLastDamageTime = -1.0f;
	mYawShake = 0.0f;
	mRollShake = 0.0f;
	mPitchShake = 0.0f;
	mShakeR = 0.0f ;

	mChangedWeaponTime = -20.0f;
	mCockpit = new( MEMTYPE_BATTLEENGINE ) CCOCKPIT(this);

    mOldLife = mLife;
	mOldEnergy = mEnergy;
	mLowArmourStartTime = -20.0f;
    mLowEnergyStartTime = -20.0f;
	mDangerStartTime = -20.0f;
	mAugActiveTime = -20.0f;
	mLastIncomingMissileVoice = -20.0f;

	mPlayedAmmunitionDepletedSound=-20.0f;
	mPlayedIncomingWarheadSound=-20.0f;
	mPlayedWeaponOverheatedSound=-20.0f;

	mLastTimeInHostileEnviroment=0.0f;
	mSafePos.SetAll(mPos) ;
	mSafePosTime.SetAll(EVENT_MANAGER.GetTime());

	mRecentLocks=0;
	mThrustersOn=FALSE;

	CInitRadarWarningReceiver init_rwr ;


	init_rwr.mScanDistance = 80.0f ;
	init_rwr.mUpdateFrequency = 0.03f ;
	init_rwr.mAttachedTo = this;

	CRadarWarningReceiver* rwr = new( MEMTYPE_BATTLEENGINE ) CRadarWarningReceiver ;

	if (rwr)
	{
		rwr->Init(init_rwr) ;
		mEquipment.Add(rwr) ;

		mCachedRadarWarningReceiver = rwr;
	}
	else
		mCachedRadarWarningReceiver = NULL;

	for (n=0; n<kBattleEngineStores; n++)
	{
		mStoreOverheat[n]=FALSE;

		mStoreHeat[n]=mConfiguration->mStoreHeat[n];

		if (!mStoreHeat[n])
			mStoreValue[n]=mConfiguration->mStoreValue[n];
		else
			mStoreValue[n]=0;
	}

	mTransformStartTime=-20.0f;

	if (beInit->mPlaneMode)
		AddVelocity(mOrientation*FVector(0.0f,0.5f,0.0f));

	mAugActive=FALSE;
	mAugValue=0.0f;

	mCloaked=FALSE;

	mOldEulerAngles = mCurrentOrientation;

	mStallTime=-99999.0f;
	mAugmentedTime = -99999.0f;
	mAmmoDepletedTime=-99999.0f;
	mWeaponOverheatedTime=-99999.0f;
	mStalling=FALSE;

	// Initialise targeting
	mAutoAimTarget=NULL;

	mOldAutoAimYawOffset=0.0f;
	mOldAutoAimPitchOffset=0.0f;

	mDesiredAutoAimYawOffset=0.0f;
	mDesiredAutoAimPitchOffset=0.0f;

	mAutoAimYawOffset=0.0f;
	mAutoAimPitchOffset=0.0f;

	mEngineState=kEnginesOff;
	mOldEngineState=kEnginesOff;

	MassiveHackPutUsInRightMesh();

	EVENT_MANAGER.AddEvent((SINT)CALC_UNIT_OVER_CROSSHAIR,this,EVENT_MANAGER.GetTime()+GAME.FloatRandom()*0.2f+0.1f,START_OF_FRAME,NULL,NULL);

	HandleAutoAim(NULL);
}

//******************************************************************************************
CBattleEngine::~CBattleEngine()	// FUNCTION: 0x00405a40
{
	ListIterator<CValidatedFoR>		engineIterator(&mEngines);

	while (CValidatedFoR *engine=engineIterator.First())
	{
		mEngines.Remove(engine);
		engine->Kill();
		delete engine;
	}

	for (CEquipment* item = mEquipment.First() ;
		 item!=NULL;
		 item = mEquipment.Next() )
	{
		delete item ;
	}

	CLockInfo	*lock;

	for (lock=mLocks.First(); lock; lock=mLocks.Next())
		delete lock;

	for (lock=mFiredLocks.First(); lock; lock=mFiredLocks.Next())
		delete lock;

	mCurrentWeapon=NULL;

	if (mCockpit)
	{
		delete mCockpit ;
		mCockpit = NULL ;
	}

	if (mWalkerPart) 
	{
		delete mWalkerPart ;
		mWalkerPart = NULL ;
	}

	if (mJetPart)
	{
		delete mJetPart ;
		mJetPart = NULL ;
	}

	SAFE_DELETE(mOtherMeshHackRenderThing);
	SAFE_DELETE(mHackBackupMC);
}

//******************************************************************************************
void CBattleEngine::Shutdown()	// FUNCTION: 0x00405f80
{
	while (CDamageFlash *flash=mDamageFlashes.First())
	{
		mDamageFlashes.Remove(flash);
		delete flash;
	}

	if (mPlayer.ToRead())
	{
		CCONTROLLER		*controller=GAME.GetController(mPlayer->GetNumber()-1);

		if (controller)
			controller->SetVibration(0, mPlayer->GetNumber()-1);
	}

	mSmoke.Kill();

	SUPERTYPE::Shutdown();
}

//******************************************************************************************
void	CBattleEngine::Activate()	// FUNCTION: 0x00406000
{
	mPoweredUp = TRUE ;
	LOG.AddMessage("Activating Battle Engine") ;
}

//******************************************************************************************
void	CBattleEngine::Deactivate()	// FUNCTION: 0x00406020
{
	mPoweredUp = FALSE ;
	LOG.AddMessage("Deactivating Battle Engine") ;
}

//******************************************************************************************
float	CBattleEngine::GetMaxLife()	// FUNCTION: 0x00406040
{
	return mConfiguration->mLife;
}

//******************************************************************************************
FVector	CBattleEngine::GetRenderPos()	// FUNCTION: 0x00406050
{
	FVector pos = SUPERTYPE::GetRenderPos() ;
	if (mHackRenderThingIsWalker == TRUE)
	{
		pos.Z+=COfGHeight();
	}
	return pos ;
}


//******************************************************************************************
FMatrix	CBattleEngine::GetRenderOrientation()	// FUNCTION: 0x004060b0
{
	FMatrix ori = SUPERTYPE::GetRenderOrientation() ;

	if (mState==BATTLE_ENGINE_STATE_WALKER)
	{
		CEulerAngles		a(ori);

		return FMatrix(a.mYaw,0.0f,0.0f);
	}
	else if (mState==BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER)
	{
		CEulerAngles		a(ori);

		return FMatrix(a.mYaw,0.0f,0.0f);
	}
	else if (mState==BATTLE_ENGINE_STATE_MORPHING_INTO_JET)
	{
		CEulerAngles		a(ori);

		return FMatrix(a.mYaw,0.0f,0.0f);
	}

	return ori;
}

//******************************************************************************************
float CBattleEngine::CalculatePitch()	// FUNCTION: 0x004063a0
{
	return mCurrentOrientation.mPitch;
}

//******************************************************************************************
// call this when morphed into new state (i.e. changes our collision shape )
void	CBattleEngine::SetCollisionShape()	// FUNCTION: 0x004063b0
{

	float r = GetRadius() ;
	
	CCylinder* shape = new( MEMTYPE_BATTLEENGINE ) CCylinder(r, COfGHeight() * 0.5f) ;
	mCollisionSeekingThing->SetShape(shape) ;
}


//******************************************************************************************

// SRG THIS IS A MASSIVE HACK TO GET TWO MESHES WORKING FOR MILESTONE
void CBattleEngine::MassiveHackPutUsInRightMesh()	// FUNCTION: 0x00406460
{
  

  BOOL changed = FALSE ;
  if ((mState==BATTLE_ENGINE_STATE_MORPHING_INTO_JET || mState==BATTLE_ENGINE_STATE_JET)&& mHackRenderThingIsWalker == TRUE)
  { 
	  CRenderThing* temp = mRenderThing ;
	  mRenderThing = mOtherMeshHackRenderThing ;
	  mOtherMeshHackRenderThing = temp ;
	  mHackRenderThingIsWalker = FALSE ;
	  mHackBackupMC = mMotionController ;
	  mHackBackupMC->SetThing(NULL) ;
	  mMotionController = NULL ;
	  changed = TRUE;
  }
  else if (mState==BATTLE_ENGINE_STATE_WALKER && mHackRenderThingIsWalker == FALSE)
  {
	  CRenderThing* temp = mRenderThing ;
	  mRenderThing = mOtherMeshHackRenderThing ;
	  mOtherMeshHackRenderThing = temp ;
	  mHackRenderThingIsWalker = TRUE ;
	  mMotionController = mHackBackupMC ;
	  mMotionController->SetThing(this);
	  mHackBackupMC = NULL ;
	  changed = TRUE;
  }

  if (changed)
  {
		if (GAME.IsMultiplayer() == TRUE)
		{
			// update motion controller  ( do a seconds worth of update to stop legs snapping )
			if (mRenderThing && mMotionController)
			{
				for (int i= 0; i<20;i++)
				{
					((CMCBattleEngine*)mMotionController)->ProcessMovement(TRUE);
				}
			}

			// set out collision shape to right mesh
			if (mCollisionSeekingThing)
			{
				CMeshCollisionVolume* mesh_col_vol = (CMeshCollisionVolume*)mCollisionSeekingThing->GetCollisionMeshVolume() ;
				if (mesh_col_vol && mRenderThing)
				{
					mesh_col_vol->SetRenderThing(mRenderThing) ;
				}
			}
		}
	}
}


//******************************************************************************************
// implemented cos battle engine has no mesh
/*void CBattleEngine::GetCentrePos(
	FVector		&outPos)
{
	if (mState == BATTLE_ENGINE_STATE_JET)
	{
		outPos=mPos;
		return;
	}
	
	outPos=GetPos();
	outPos.Z+=COfGHeight()*0.4f;
}*/

//******************************************************************************************	
float	CBattleEngine::GetAltitudeAboveGround() 
{ 
	return (-mPos.Z) - (-MAP.Collide(mPos)) ;
}


//******************************************************************************************
void    CBattleEngine::HandleLocks()	// FUNCTION: 0x00406560
{
	if (IsFiring())
		return;

	CWeapon			*weapon;
	BOOL			loseLocks=FALSE;

	if (mState==BATTLE_ENGINE_STATE_JET)
	{
		weapon=mJetPart->GetCurrentWeapon();
	
		if (!mJetPart->CanWeaponFire())
			loseLocks=TRUE;
	}
	else
	{
		weapon=mWalkerPart->GetCurrentWeapon();
		
		if (!mWalkerPart->CanWeaponFire())
			loseLocks=TRUE;
	}

	if (!weapon)
		return;

	// Lose any locks that are outside of the deflection
	CLockInfo		*info=mLocks.First();

	while (info)
	{
		if (CUnit *unit=info->mUnit.ToRead())
		{
			FMatrix		inverseOri=mOrientation;
			FVector		path=unit->GetPos()-mPos;

			inverseOri.TransposeInPlace();

			FVector		heading=inverseOri*path;
			heading.Normalise();

			if ((heading.Y<cosf(weapon->GetMaxDeflection())) || (loseLocks) || (unit->IsDying()))
			{
				mLocks.Remove(info);
				delete info;
				info=mLocks.First();
			}
		}
		else
		{
			mLocks.Remove(info);
			delete info;
			info=mLocks.First();
		}

		if (info)
			info=mLocks.Next();
	}

	if ((CountLocks()>=weapon->GetMaxLocks()) || (loseLocks))
		return;

	// Test to see if we can acquire any new locks
	if (weapon->ReadyToFire())
	{
		switch (weapon->GetLockMode())
		{
			case kDirectLockMode:
			{
				CUnit	*unit=mCurrentUnitOverCrosshair.ToRead();

				if (!unit)
				{
					unit=CalcUnitOverCrossHair(NULL,FALSE,FALSE);
				}

				if (unit)
				{
					if (!Locked(unit))
					{
						if ((IsTargetAlligence(unit->GetAllegiance())) &&
							(weapon->CanLock(unit)))
						{
							FMatrix		inverseOri=mOrientation;
							FVector		path=unit->GetPos()-mPos;
							float		pathMagnitudeSq=path.MagnitudeSq();
							float		magnitudeSq=weapon->GetLockRange()*(1.0f-unit->GetStealth()/100.0f);
							magnitudeSq*=magnitudeSq;

							if (pathMagnitudeSq<magnitudeSq)
							{
								inverseOri.TransposeInPlace();

								FVector		heading=inverseOri*path;
								heading.Normalise();

								if (heading.Y>cosf(weapon->GetMaxDeflection()))
									StartLock(unit,weapon->GetLockTime(),TRUE);
							}
						}
					}
				}
			}
			break;

			case kProximityLockMode:
			{
				while (CUnit *item=GetClosestLockableUnit(weapon,mPos,weapon->GetLockRange()))
				{
					StartLock(item,weapon->GetLockTime());

					if (CountLocks()>=weapon->GetMaxLocks())
						return;
				}
			}
			break;

			case kSequenceLockMode:
			{
				if (LocksFinished())
				{
					BOOL		locking=FALSE;

					if (CLockInfo	*info=mLocks.First())
					{
						if (info->mDirectLock)
						{
							if (CUnit *unit=info->mUnit.ToRead())
							{
								if (CUnit *item=GetClosestLockableUnit(weapon,unit->GetPos(),weapon->GetLockRadius()))
								{
									StartLock(item,weapon->GetLockTime());
									locking=TRUE;
								}
							}
						}
					}

					if ((mCurrentUnitOverCrosshair.ToRead()) && (!locking))
					{
						if ((IsTargetAlligence(mCurrentUnitOverCrosshair->GetAllegiance())) &&
							(weapon->CanLock(mCurrentUnitOverCrosshair.ToRead())))
						{
							FMatrix		inverseOri=mOrientation;
							FVector		path=mCurrentUnitOverCrosshair->GetPos()-mPos;
							float		pathMagnitudeSq=path.MagnitudeSq();
							float		lockRangeSq=weapon->GetLockRange();
							lockRangeSq*=lockRangeSq;

							if (pathMagnitudeSq<lockRangeSq)
							{
								inverseOri.TransposeInPlace();

								FVector		heading=inverseOri*path;
								heading.Normalise();

								if (heading.Y>cosf(weapon->GetMaxDeflection()))
									StartLock(mCurrentUnitOverCrosshair.ToRead(),weapon->GetLockTime(),TRUE);
							}
						}
					}
				}
			}
			break;
		}
	}
}

//******************************************************************************************
CUnit*	CBattleEngine::GetClosestLockableUnit(	// FUNCTION: 0x00406da0
	CWeapon			*inWeapon,
	FVector			inPos,
	float			inDistance)
{
	SPtrSet<CUnit>&		list=WORLD.GetUnitNB();
	float				dist=inDistance;
	CUnit				*favourite=NULL;
	float				minDistSq=9999999.0f;
 
	for (CUnit* item=list.First(); item; item=list.Next())
	{
		if ((IsTargetAlligence(item->GetAllegiance())) &&
			(inWeapon->CanLock(item)))
		{
			FVector		distVector=item->GetPos()-inPos;
			float		distMagnitudeSq=distVector.MagnitudeSq();
			float		distSq=dist*(1.0f-item->GetStealth()/100.0f);
			distSq*=distSq;

			if ((distMagnitudeSq<distSq) && (distMagnitudeSq<minDistSq))
			{
				FVector		path=item->GetPos()-mPos;
				FMatrix		inverseOri=mOrientation;

				inverseOri.TransposeInPlace();

				FVector		heading=inverseOri*path;
				heading.Normalise();

				if (heading.Y>cosf(inWeapon->GetMaxDeflection()))
				{
					if (!Locked(item))
					{
						minDistSq=distMagnitudeSq;
						favourite=item;
					}
				}
			}
		}
	}

	return favourite;
}

//******************************************************************************************
void	CBattleEngine::StartLock(	// FUNCTION: 0x00406fc0
	CUnit		*inUnit,
	float		inLockTime,
	BOOL		inDirectLock)
{
	// Don't lock onto dying units
	if (inUnit->IsDying())
		return;

	// Don't lock onto the same thing twice
	for (CLockInfo* item=mLocks.First(); item; item=mLocks.Next())
	{
		if (item->mUnit.ToRead()==inUnit)
			return;
	}

	CLockInfo	*info=new( MEMTYPE_BATTLEENGINE ) CLockInfo();

	info->mUnit.SetReader(inUnit);
	info->mStart=EVENT_MANAGER.GetTime();
	info->mFinish=info->mStart+inLockTime;
	info->mDirectLock=inDirectLock;

	mLocks.Append(info);
}

//******************************************************************************************
int		CBattleEngine::CountLocks()
{
	int		count=0;

	for (CLockInfo *item=mLocks.First(); item; item=mLocks.Next())
	{
		if (item->mUnit.ToRead())
			count++;
	}

	return count;
}

//******************************************************************************************
void	CBattleEngine::FireLock(	// FUNCTION: 0x00407060
	CThing		*inUnit)
{
	if (inUnit)
	{
		for (CLockInfo *item=mLocks.First(); item; item=mLocks.Next())
		{
			if ((item->mFinish<EVENT_MANAGER.GetTime()) &&
				(item->mUnit.ToRead()==inUnit))
			{
				mLocks.Remove(item);

				if (!FiredAt(inUnit))
				{
					mFiredLocks.Add(item);
					item->Fired();
				}
				else
					delete item;

				return;
			}
		}
	}
}

//******************************************************************************************
BOOL	CBattleEngine::FiredAt(
	CThing		*inUnit)
{
	for (CLockInfo *item=mFiredLocks.First(); item; item=mFiredLocks.Next())
	{
		if (item->mUnit.ToRead()==inUnit)
			return TRUE;
	}

	return FALSE;
}

//******************************************************************************************
void	CBattleEngine::LockHit(	// FUNCTION: 0x00407140
	CThing		*inUnit)
{
	if (inUnit)
	{
		for (CLockInfo *item=mFiredLocks.First(); item; item=mFiredLocks.Next())
		{
			if (item->mUnit.ToRead()==inUnit)
			{
				mFiredLocks.Remove(item);
				delete item;
				return;
			}
		}
	}
}

//******************************************************************************************
BOOL	CBattleEngine::Locked(
	CThing		*inUnit)
{
	for (CLockInfo *item=mLocks.First(); item; item=mLocks.Next())
	{
		if (item->mUnit.ToRead()==inUnit)
			return TRUE;
	}

	return FALSE;
}

//******************************************************************************************
CUnit* CBattleEngine::GetCurrentTarget()	// FUNCTION: 0x004071b0
{
	SINT		target=mCurrentTarget;
	mCurrentTarget++;

	float		time=EVENT_MANAGER.GetTime();
	CLockInfo	*item=NULL;
	BOOL		lock=FALSE;

	if (mRecentLocks==0)
		return NULL;

	// Is there a valid unfired lock?
	for (item=mLocks.First(); item; item=mLocks.Next())
	{
		if ((item->mFinish<time) && (item->mUnit.ToRead()))
		{
			// If there is then return that
			return item->mUnit.ToRead();
		}
	}

	// Is there a valid secondary lock
	for (item=mFiredLocks.First(); item; item=mFiredLocks.Next())
	{
		if (item->mUnit.ToRead())
			lock=TRUE;
	}

	// Loop through the locks until the index is found
	if (lock)
	{
		UWORD	n=1;

		for (item=mFiredLocks.First(); TRUE; item=mFiredLocks.Next())
		{
			if (n==mRecentLocks)
			{
				item=mFiredLocks.First();
				n=0;
			}
				
			if (!item)
			{
				item=mFiredLocks.First();
			
				if (!item)
					return NULL;
			}

			if (target==0)
			{
				if (item->mUnit.ToRead())
					return item->mUnit.ToRead();
			}
			else
			{
				target--;
				n++;
			}
		}
	}

	return NULL;
}

//******************************************************************************************
BOOL	CBattleEngine::DisplayLock(	// FUNCTION: 0x00407310
	CWeapon		*inWeapon)
{
	CWeapon		*weapon;

	if (mState==BATTLE_ENGINE_STATE_JET)
		weapon=mJetPart->GetCurrentWeapon();
	else
		weapon=mWalkerPart->GetCurrentWeapon();

	if ((weapon) && (weapon==inWeapon))
		return TRUE;

	return FALSE;
}

//******************************************************************************************
BOOL	CBattleEngine::LocksFinished()
{
	float		time=EVENT_MANAGER.GetTime();

	for (CLockInfo *item=mLocks.First(); item; item=mLocks.Next())
	{
		if (item->mUnit.ToRead())
		{
			if (item->mFinish>time)
				return FALSE;
		}
	}

	return TRUE;
}

//******************************************************************************************
void		CBattleEngine::Hit(CThing* other_thing, CCollisionReport* report)	// FUNCTION: 0x00407350
{
	if (report->mStuck)
	{
		mInSafeCollisionPlace = FALSE ;

	}

	if (IsDying())
	{
		if ((other_thing->IsA(THING_TYPE_UNIT)) ||
			(other_thing->IsA(THING_TYPE_TREE)) ||
			(other_thing->IsA(THING_TYPE_CAN_BE_WALKED_ON)))
		{
			Explode();
			AddShutdownEvent();
		}
	}


	SUPERTYPE::Hit(other_thing, report) ;

	// SRG EXTRA check to help player stop getting lodged between polys.
	// If we are stuck move us back to last reported safe collision position
	if (report->mStuck == TRUE)
	{
		if (other_thing->IsA(THING_TYPE_CAN_BE_WALKED_ON))
		{
			DeclareOnObject(other_thing) ;
		}

		FVector safe_pos = mSafePos[mCurrentSafeTry] ;

		// transpose safe pos back to where it would have been now given the velocity of the other thing
		FVector vel = other_thing->GetVelocity() *2.0f;
		float m = EVENT_MANAGER.GetTime() - mSafePosTime[mCurrentSafeTry] ;
		m = m / CLOCK_TICK ;
		vel += other_thing->GetVelocity() * m ;
		safe_pos+=vel ;
		MoveTo(safe_pos) ;
		mCurrentSafeTry++ ;
		if (mCurrentSafeTry >= (mSafePos.Size()-1) )
		{
		//	LOG.AddMessage("ERROR: arggghh collision nightmare !!") ;
			mCurrentSafeTry --;
		}
	}
}

//******************************************************************************************
float	CBattleEngine::Gravity()	// FUNCTION: 0x004074d0
{
	if (IsDying())
	{
		switch(mState)
		{
			case BATTLE_ENGINE_STATE_WALKER:				return SUPERTYPE::Gravity();
			case BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER:	return SUPERTYPE::Gravity();
			case BATTLE_ENGINE_STATE_MORPHING_INTO_JET:		return SUPERTYPE::Gravity();
			case BATTLE_ENGINE_STATE_JET:					return mJetPart->Gravity();
		}
	}
	else
	{
		switch(mState)
		{
			case BATTLE_ENGINE_STATE_WALKER:				return SUPERTYPE::Gravity();
			case BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER:	return SUPERTYPE::Gravity()*0.2f;
			case BATTLE_ENGINE_STATE_MORPHING_INTO_JET:		return SUPERTYPE::Gravity();
			case BATTLE_ENGINE_STATE_JET:					return mJetPart->Gravity();
		}
	}

	return 0.0f;
}

//******************************************************************************************
// PC (not in Stuart's release): mouse look for a walker on the ground. CGame::Render calls it (0046e650) and so
// does UpdateRotation (00407ec7). The cursor is drawn back to the window's centre every frame; the offset since the
// last frame turns and pitches the engine, the pitch held within the ground's limits as UpdateRotation holds it.
// PC: the walker is on the ground with the mouse bound to its yaw, in a running game (UpdateRotation 00407c5b
// and UpdateMouseLookAngles inline it; name open)
static inline BOOL MouseLookActive(CBattleEngine *be)
{
	int	player = be->GetPlayer()->GetNumber() - 1;

	if ((GAME.IsPaused()) || (GAME.GetGameState() != GAME_STATE_PLAYING) || (be->IsDying()) || (!be->GetIsPoweredUp()) ||
		(be->GetState() != BATTLE_ENGINE_STATE_WALKER))
		return FALSE;

	SINT	type = CONTROL_BINDINGS[1].mInput[player].mType;
	if ((type != MOUSE_AXIS_MINUS) && (type != MOUSE_AXIS_PLUS))
		return FALSE;

	if ((!be->IsOnGround()) && (!be->IsOnObject()))
		return FALSE;

	return TRUE;
}

extern float	gMouseSensitivity;			// 006254f4 (Controller.cpp)

void	CBattleEngine::UpdateMouseLookAngles()	// FUNCTION: 0x00407540
{
	static int	frame = 0;				// 006601f8
	static int	last_frame = 2;		// 00622f0c

	if (CLIPARAMS.mAutoConfigTest)
		return;

	frame++;

	if (!MouseLookActive(this))
		return;

	if (frame == last_frame + 1)
	{
		mCurrentOrientation.mYaw -= (float)(MOUSE_X - PLATFORM.GetWindowWidth() / 2) * (gMouseSensitivity * 0.002f);

		float	pitch = (float)(MOUSE_Y - PLATFORM.GetWindowHeight() / 2) * (gMouseSensitivity * 0.002f) * 0.5f;
		if (CAREER.GetInvertYAxis(mPlayer->GetNumber() - 1))
			mCurrentOrientation.mPitch += pitch;
		else
			mCurrentOrientation.mPitch -= pitch;

		mOrientation = FMatrix(mCurrentOrientation.mYaw + (float) cosf(mShakeR) * mYawShake,
							   mCurrentOrientation.mPitch + (float) cosf(mShakeR) * mPitchShake,
							   mCurrentOrientation.mRoll + (float) cosf(mShakeR) * mRollShake);
		mOldOrientation = mOrientation;

		// the ground's pitch, as UpdateRotation finds it
		FMatrix		ym;
		ym.MakeRotationYawF(mCurrentOrientation.mYaw);
		FVector		yv=ym*FVector(0.0f,1.0f,0.0f);

		FVector		map_normal;

		if ((IsOnGround()) && (!IsOnObject()))
		{
			FVector	n = MAP.Normal(GetPos());
			map_normal = n;
		}
		else
			map_normal=FVector(0,0,-1);

		FVector cross_yaw_and_normal = yv ^ map_normal;

		FVector p = cross_yaw_and_normal ^ map_normal;
		p.Normalise();
		float	groundPitch=-p.Z;

		if (mCurrentOrientation.mPitch-groundPitch>0.6f)
			mCurrentOrientation.mPitch=groundPitch+0.6f;
		else if (mCurrentOrientation.mPitch-groundPitch<-0.9f)
			mCurrentOrientation.mPitch=groundPitch-0.9f;
	}

	MOUSE_X = PLATFORM.GetWindowWidth() / 2;
	MOUSE_Y = PLATFORM.GetWindowHeight() / 2;
	last_frame = frame;
}
#line 1090

//******************************************************************************************
// 0 = dont shake at all
// 1 = max shake

void	CBattleEngine::AddShockShake(float amount)	// FUNCTION: 0x00407940
{
	if (amount < 0.001) return ;

	if (amount > 0.75f) amount = 0.75f;

	float s = 16.0f / amount ;
	float s1 = 1.0f * amount ;

	// ok shake us
	mYawShake = (((float)(GAME.Random()%32)) / s) - s1;
	mPitchShake = (((float)(GAME.Random()%32)) / s) - s1 ;
	mRollShake = (((float)(GAME.Random()%32)) / s) - s1  ;
	mShakeR = 0.0;

	// ok shake cockpit aswell
	if (mCockpit)
	{
		mCockpit->AddShockShake(amount) ;
	}

	PLATFORM.TriggerRumble(mPlayer->GetNumber()-1); // FIXME : Should do a lookup to see what pad this player is on	
}

//******************************************************************************************
// The PC version: the mouse-look test (no ground clamp while the mouse looks) and the call to UpdateMouseLookAngles.
void	CBattleEngine::UpdateRotation()	// FUNCTION: 0x00407a50
{
	// store old rotation
	mOldEulerAngles = mCurrentOrientation;

	// stop you pitching too high or too low
	if ((mState==BATTLE_ENGINE_STATE_WALKER) && ((IsOnGround()) || (IsOnObject())))
	{
		// Calculate the ground pitch
		FMatrix		ym;
		ym.MakeRotationYawF(mCurrentOrientation.mYaw);
		FVector		yv=ym*FVector(0.0f,1.0f,0.0f);

		FVector		map_normal;

		if ((IsOnGround()) && (!IsOnObject()))
			map_normal=MAP.Normal(GetPos());
		else
			map_normal=FVector(0,0,-1);

		FVector cross_yaw_and_normal = yv ^ map_normal;

		FVector p = cross_yaw_and_normal ^ map_normal;
		p.Normalise();  // not needed 
		float	groundPitch=-p.Z;
		
		if (MouseLookActive(this))
			;	// PC: the mouse holds the pitch within the ground's limits itself (00407c5b..00407cc5)
		else if (mCurrentOrientation.mPitch-groundPitch>0.5f)
		{
			if (mCurrentOrientation.mPitch-groundPitch>0.7f)
			{
				mPitchvel-=(mCurrentOrientation.mPitch-groundPitch-0.7f)*0.03f;
			}
			else if (mPitchvel>0.0f)
			{
				float	mult=1.0f-((mCurrentOrientation.mPitch-groundPitch-0.5f)*6.0f);
				
				if (mult<0.0f)
					mult=0.0f;

				mPitchvel*=mult;
			}
		}
		else if (mCurrentOrientation.mPitch-groundPitch<-0.8f)
		{
			if (mCurrentOrientation.mPitch-groundPitch<-1.0f)
			{
				mPitchvel-=(mCurrentOrientation.mPitch-groundPitch+1.0f)*0.03f;
			}
			else if (mPitchvel<0.0f)
			{
				float	mult=1.0f-fabsf((mCurrentOrientation.mPitch-groundPitch+0.8f)*6.0f);
				
				if (mult<0.0f)
					mult=0.0f;

				mPitchvel*=mult;
			}
		}
	}
	else
	{
		if (mCurrentOrientation.mPitch > 1.17f && mPitchvel > 0.0f && (!mJetPart->DoingLoop()))
		{
			float mult = 1.0f - ((mCurrentOrientation.mPitch - 1.17f)*1.5f) ;
			if (mult <0.0f) mult = 0.0f ;
			mPitchvel*=mult  ;
		}	

		if (mCurrentOrientation.mPitch < -1.17f && mPitchvel < 0.0f && (!mJetPart->DoingLoop()))
		{
			float mult = 1.0f - fabsf((mCurrentOrientation.mPitch + 1.17f)*1.5f) ;
			if (mult <0.0 ) mult = 0.0f ;
			mPitchvel*=mult  ;
		}
	}

	// update yaw pitch roll
	mCurrentOrientation.mYaw+=mYawvel ;
	mCurrentOrientation.mPitch+=mPitchvel ;
	mCurrentOrientation.mRoll+=mRollvel ;

	UpdateMouseLookAngles();	// PC (00407ec7)

	if (mCurrentOrientation.mYaw > PI ) mCurrentOrientation.mYaw-=PI_M2 ;
	if (mCurrentOrientation.mYaw < -PI ) mCurrentOrientation.mYaw+=PI_M2 ;
	if (mCurrentOrientation.mPitch > PI ) mCurrentOrientation.mPitch-=PI_M2 ;
	if (mCurrentOrientation.mPitch < -PI ) mCurrentOrientation.mPitch+=PI_M2 ;
	if (mCurrentOrientation.mRoll> PI ) mCurrentOrientation.mRoll-=PI_M2 ;
	if (mCurrentOrientation.mRoll < -PI ) mCurrentOrientation.mRoll+=PI_M2 ;

	mYawvel*=0.8f;
	mPitchvel*=0.8f;
	mRollvel*=0.8f;


	if (mState!=BATTLE_ENGINE_STATE_JET)
	{
		mCurrentOrientation.mRoll*=0.8f ;
	}
	else
	{
		if (mJetPart->AutoLevel())
			mCurrentOrientation.mRoll*=0.97f ;
	}

	mOrientation = FMatrix(mCurrentOrientation.mYaw + (float) cosf(mShakeR) *mYawShake,
							mCurrentOrientation.mPitch + (float) cosf(mShakeR) * mPitchShake,
							mCurrentOrientation.mRoll + (float) cosf(mShakeR) * mRollShake) ;

	// update shake values
	if (fabsf(mYawShake) > 0.001f || fabsf(mPitchShake) > 0.001f || fabsf(mRollShake) > 0.001f)
	{
		mYawShake*=0.8f;
		mPitchShake*=0.8f;
		mRollShake*=0.8f;
		mShakeR+=0.8f ;
	}
}

// PC (not in Stuart's release): UpdateRotation's pitch-limit test, out of line for CGame::Update (0046eb8d) and
// CPlayer::ReceiveButtonAction (004d31d3).
BOOL	CBattleEngine::IsWalkerOnGroundOrObject()	// FUNCTION: 0x004080f0
{
	if ((mState==BATTLE_ENGINE_STATE_WALKER) && ((IsOnGround()) || (IsOnObject())))
		return TRUE;
	return FALSE;
}
#line 1237
//******************************************************************************************
// returns TRUE if we are actually walking not just in walker mode ( i.e. might be falling )
BOOL	CBattleEngine::IsWalking()	// FUNCTION: 0x00408120
{
	if (mState == BATTLE_ENGINE_STATE_WALKER && 
	    (EVENT_MANAGER.GetTime() - GetLastTimeOnGround() < 0.5f) )
	{
		return TRUE ;
	}
	return FALSE ;
}

//******************************************************************************************
void		CBattleEngine::DeclareInWater()	// FUNCTION: 0x00408150
{
	MassiveHackPutUsInRightMesh();
	if (IsDying())
	{
		Explode();
		AddShutdownEvent();
	}
	else
	{
		float	altitude=mPos.Z-MAP.GetWaterLevel();

		if ((altitude>-0.2f) && ((mVulnerable) || (!CLIPARAMS.mDeveloperMode)))
			StartDieProcess();
	}

	SUPERTYPE::DeclareInWater() ;
}


//******************************************************************************************
void CBattleEngine::Move()	// FUNCTION: 0x004081c0
{
	ProcessDamageFlashes();
	HandleEngines();

	mHitDamageDone=FALSE;

	#define	STEALTHSPEED	5.0f

	if (mStealth<mDesiredStealth-STEALTHSPEED)
		mStealth+=STEALTHSPEED;
	else if (mStealth>mDesiredStealth+STEALTHSPEED)
		mStealth-=STEALTHSPEED;
	else
		mStealth=mDesiredStealth;

	// used incase we get lodge somewhere
	if (mInSafeCollisionPlace==TRUE)
	{
		if (mCurrentSafeTry > 0) 
		{
			mCurrentSafeTry-- ;
			mSafePos[mCurrentSafeTry] = mPos ;
			mSafePosTime[mCurrentSafeTry] = EVENT_MANAGER.GetTime() ;
		}
		else
		{
			int i;
			for (i=mSafePos.Size()-2;i >= 0 ;i--)
			{
				mSafePos[i+1] = mSafePos[i] ;
				mSafePosTime[i+1] = mSafePosTime[i] ;
			}
			mSafePos[0] = mPos ;
			mSafePosTime[0] = EVENT_MANAGER.GetTime() ;
		}
	
	}
	mInSafeCollisionPlace = TRUE;

	// Set the vibration
	mVibration*=0.7f;

	if ((mPlayer.ToRead()) && (!GAME.IsPaused()) && (!IsDying()))
	{
		CCONTROLLER		*controller=GAME.GetController(mPlayer->GetNumber()-1);

		if (controller)
		{
			float			vibration=mVibration;

			if (vibration<0.0f)
				vibration=0.0f;
			else if (vibration>1.0f)
				vibration=1.0f;

			controller->SetVibration(vibration, mPlayer->GetNumber()-1);
		}
	}

	if (IsDying())
	{
		mSmoke.SetPos(mPos);

		if (mState==BATTLE_ENGINE_STATE_JET)
		{
			mPitchvel=0.01f;
			mVelocity=mOrientation*FVector(0.0f,mVelocity.Magnitude(),0.0f);
		}
	}
	else
	{
		// SRG massive hack (part 2) sorry not my fault stops legs going through carrier on 231
		if (GAME.GetGameState() > GAME_STATE_PANNING && mMotionController)
		{
			((CMCBattleEngine *) mMotionController)->SetAnimationBlend(0.0f) ;
		}

		// Handle the aug values
		if (mAugActive)
		{
			mAugValue-=AUG_DECREASE_RATE;

			if (GetAugValue()<=0)
			{
				UnaugmentWeapon();
			}
		}
		else if (GetAugValue()>=MAX_AUG_VALUE)
		{
			AugmentWeapon();
		}

		// If cloaked then burn more energy
		if (mCloaked)
		{
			mEnergy-=mConfiguration->mMaxAirEnergyCost;

			if (GetEnergy()<0)
			{
				mEnergy=0;
				Decloak();
			}
		}

		// check if we should generate warnings
		float low_life = mConfiguration->mLife / 4.0f;
		float low_energy = 	mConfiguration->mEnergy / 4.0f;

		if (mOldLife > low_life && mLife <= low_life)
		{
			mLowArmourStartTime = EVENT_MANAGER.GetTime();
//			LOG.AddMessage("play sample armour low");
			PlayHudSample("hud_armour_low");
		}

		if (mOldEnergy > low_energy && mEnergy <= low_energy)
		{
			mLowEnergyStartTime = EVENT_MANAGER.GetTime() ;
//			LOG.AddMessage("play sample energy low");
			PlayHudSample("hud_energy_low");
		}

		// ok remind us of warnings every 15 secs if we are not recovering
		if (mLife <= low_life && mLife <= mOldLife &&((EVENT_MANAGER.GetTime() - mLowArmourStartTime) > 15.0f))
		{
			mLowArmourStartTime = EVENT_MANAGER.GetTime() ;
//			LOG.AddMessage("play sample armour low");
			PlayHudSample("hud_armour_low");
		}

		if (mEnergy <= low_energy && mEnergy <=mOldEnergy && ((EVENT_MANAGER.GetTime() - mLowEnergyStartTime) > 15.0f))
		{
			mLowEnergyStartTime = EVENT_MANAGER.GetTime() ;
//			LOG.AddMessage("play sample energy low");
			PlayHudSample("hud_energy_low");
		}

		if ((GetDangerStartTime()+8.0f>EVENT_MANAGER.GetTime()) &&
			(mPlayedIncomingWarheadSound+8.0f<EVENT_MANAGER.GetTime()))
		{
			PlayHudSample("hud_incoming_warhead");
			mPlayedIncomingWarheadSound=EVENT_MANAGER.GetTime();
		}

		if ((GetAmmoDepletedTime()+8.0f>EVENT_MANAGER.GetTime()) &&
			(mPlayedAmmunitionDepletedSound+8.0f<EVENT_MANAGER.GetTime()))
		{
			PlayHudSample("hud_ammunition_depleted");
			mPlayedAmmunitionDepletedSound=EVENT_MANAGER.GetTime();
		}

		if ((GetWeaponOverheatedTime()+8.0f>EVENT_MANAGER.GetTime()) &&
			(mPlayedWeaponOverheatedSound+8.0f<EVENT_MANAGER.GetTime()))
		{
			PlayHudSample("HUD_Weapon_Overheating");
			mPlayedWeaponOverheatedSound=EVENT_MANAGER.GetTime();
		}

		// Update visibility flag

		mOldLife = mLife ;
		mOldEnergy = mEnergy ;
 
		// Handle zoom
		mOldZoom=mZoom;

		if (CWeapon	*weapon=GetCurrentWeapon())
		{
			switch (weapon->GetZoomMode())
			{
				case kChargeZoom:
				{
					float	charge=weapon->GetCharge();
					float	newZoom=(1.0f-charge)*(MAX_ZOOM_OUT-MAX_CHARGE_ZOOM_IN)+MAX_CHARGE_ZOOM_IN;
					
					if (charge>=1.0f)
						mZoomOutTime=EVENT_MANAGER.GetTime()+0.5f;

					if (mDesiredZoom>newZoom)
						mDesiredZoom=newZoom;
					else if ((mDesiredZoom<newZoom) && (EVENT_MANAGER.GetTime()>mZoomOutTime))
						mDesiredZoom=MAX_ZOOM_OUT;
				}
				break;

				case kNormalZoom:
				{
					if (mDesiredZoom<MAX_ZOOM_IN)
					{
						mDesiredZoom+=ZOOM_SPEED;
						if (mDesiredZoom>MAX_ZOOM_OUT) 
							mDesiredZoom=MAX_ZOOM_OUT;
					}
				}
				break;
			}
		}

		if (fabs(mDesiredZoom-mZoom)<ZOOM_SPEED)
			mZoom=mDesiredZoom;
		else if (mDesiredZoom>mZoom)
			mZoom+=ZOOM_SPEED;
		else if (mDesiredZoom<mZoom)
			mZoom-=ZOOM_SPEED;

		HandleLocks() ;

		// ok cause we moved set if we are an air thing or a ground thing
	//	float alt = MAP.Collide(mPos)-COfGHeight()-4.0f;

		if(mState != BATTLE_ENGINE_STATE_JET)
		{
			mThingType |= THING_TYPE_GROUND_UNIT ;
			mThingType |= THING_TYPE_VEHICLE ;
			mThingType &= ~THING_TYPE_AIR_UNIT ;
		}
		else
		{
			mThingType |= THING_TYPE_AIR_UNIT ;
			mThingType &= ~THING_TYPE_GROUND_UNIT ;
			mThingType &= ~THING_TYPE_VEHICLE ;
		}

		if (mState!=BATTLE_ENGINE_STATE_JET)
		{
			// hack for now
			if (mPlayer.ToRead())
			{
				if (mPlayer->GetPreferredCurrentViewMode() == PLAYER_3RD_PERSON_VIEW)
				{
					mPlayer->GotoControlView() ;
				}
			}
		
			if (mState==BATTLE_ENGINE_STATE_MORPHING_INTO_JET)
			{
				if (mCurrentOrientation.mPitch>-0.03f)
					mPitchvel-=0.015f;
				
				if ((mPos.Z>mTakeOffHeight-0.1f) && (EVENT_MANAGER.GetTime()<mTakeOffTime+2.0f))
					mVelocity.Z-=0.7f;
				else
					EVENT_MANAGER.AddEvent((int)BECOME_JET,this,EVENT_MANAGER.GetTime()+0.5f,START_OF_FRAME);
			}

			mWalkerPart->Move();
		}
		else
		{  
			// hack for now
			if (mPlayer.ToRead())
			{
				if (mPlayer->GetPreferredCurrentViewMode() == PLAYER_3RD_PERSON_VIEW)
				{
					mPlayer->GotoFPView() ;
				}
			}

			// make sure the engine Sound is playing
			if (mEnergy)
			{
				if (!SOUND.IsEffectPlaying(mInFlightSound, this))
				{
					// TRACE("Starting effect\n");	// neither trace is in retail (00408cc5..00408ced)
					SOUND.PlayEffect(mInFlightSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE, true, 0.2f, 0, -1.f, true);
				}
				else
					;	// TRACE("Effect playing\n");

				CSoundEvent	*event=SOUND.GetSoundEventForThing(mInFlightSound->mSample, this);

				SOUND.SetPitch(event, 1.f + (GetJetPart()->GetThrusterValue() * 0.25f));
			}
			else
			{
				if (mInFlightSound)
				{
					CSoundEvent		*s=SOUND.GetSoundEventForThing(mInFlightSound->mSample, this);
					if (s)
						SOUND.FadeTo(s->mSample, 0, 0.02f, this);
				}
			}

			mJetPart->Move();

			// if we are in jet mode and we have been deactivated morph into walker mode
			if (mPoweredUp == FALSE)
			{
				Morph() ;
			}
		}
	}

	if (mVelocity.Magnitude() < 0.0001f)
	{
		mNotMovingCount++;
		if (mNotMovingCount > 5)
		{
			DeclareOnGround();
		}
	}
	else
	{
		mNotMovingCount=0;
	}

	mVelocity.Z+=Gravity();  // should this be called before the move parts above ??? SRG !!!!!!! 

	// Clamp the velocity to stop the player leaving the map
	if (mPos.X<0.0f)
	{
		if (mVelocity.X<0.0f)
		{
			float	fraction=(20.0f+mPos.X)/20.0f;

			if (fraction<0.0f)
				fraction=0.0f;

			mVelocity.X*=fraction;
		}	
	}
	else if (mPos.X>512.f)
	{
		if (mVelocity.X>0.0f)
		{
			float	fraction=1.0f-(mPos.X-512.f)/20.0f;

			if (fraction<0.0f)
				fraction=0.0f;

			mVelocity.X*=fraction;
		}
	}

	if (mPos.Y<0.0f)
	{
		if (mVelocity.Y<0.0f)
		{
			float	fraction=(20.0f+mPos.Y)/20.0f;

			if (fraction<0.0f)
				fraction=0.0f;

			mVelocity.Y*=fraction;
		}	
	}
	else if (mPos.Y>512.f)
	{
		if (mVelocity.Y>0.0f)
		{
			float	fraction=1.0f-(mPos.Y-512.f)/20.0f;

			if (fraction<0.0f)
				fraction=0.0f;

			mVelocity.Y*=fraction;
		}
	}

	if (mPos.Z<-150.0f)
	{
		if (mVelocity.Z<0.0f)
		{
			float	fraction=(140.0f+mPos.Z)/10.0f;
			
			if (fraction<0.0f)
				fraction=0.0f;

			mVelocity.Z*=fraction;
		}
	}

	CActor::Move();
	
//	mVelocity=oldVelocity;

	FVector pos_before_forced_movement = mPos ;

	if (IsStandingOnAMoveingObject())
	{
		mStandingOnObjectMovement = mStandingOnThing->GetVelocity();
		FVector &position = mPos;	position+= mStandingOnObjectMovement;	// STAND-IN (order): as CActor::Move binds it (X's load order, 0040901b); Stuart's `mPos+=` is 2 bytes off
	
		FMatrix old_ori = mStandingOnThing->GetOldOrientation() ;
		FMatrix new_ori = mStandingOnThing->GetOrientation() ;

		// ok move us to new translated point
		if (old_ori != new_ori)
		{
			old_ori.TransposeInPlace() ;
			new_ori = new_ori * old_ori ;
	
			FVector them_to_us = mPos - mStandingOnThing->GetPos() ;
			them_to_us = new_ori * them_to_us ;
			mPos = mStandingOnThing->GetPos() +them_to_us ;

			// rotate us aswell
			CEulerAngles eu(new_ori);
			mCurrentOrientation.mYaw+=eu.mYaw ;
		}
	}
	else
	{
		mStandingOnObjectMovement = FVector(0.0f,0.0f,0.0f) ;
	}

	// ok used in multiplayer which moves all the legs and feet etc if we had a forced movement (e.g. standing on a moving object)
	FVector forced_movement = mPos - pos_before_forced_movement;
	if (mMotionController)
	{
		((CMCBattleEngine*)mMotionController)->AddMovement(forced_movement) ;
	}

	// ok as our sphere collision shape doesn't quite touch the floor we would expect a set space between us and the floor which doesn't collide.
	// .  So if this distance is less than we expect then we have hit a step so move us up( good method to climb stairs in meshes etc )

	CLine ray(mPos, mPos +FVector(0.0f,0.0f,2.4f)) ; // line straight down
	CWorldLineColReport wlcr;
	// ignore water so we drop through it			
	WORLD.FindFirstThingToHitLine(ray, this , &wlcr,FALSE,TRUE, ECL_MESH , THING_TYPE_AMMUNITION, FALSE);
	float dist = wlcr.mDistToImpact ;
//	LOG.AddMessage("dist = %2.8f, cof = %2.8f", dist, COfGHeight()) ;

	if (dist > 0.0f && dist < COfGHeight())  // the dist we expect from the middle of us to the floor.
	{
		if (wlcr.mHitThing && wlcr.mHitThing->IsA(THING_TYPE_CAN_BE_WALKED_ON) )
		{
			mPos.Z += (dist - COfGHeight()) ;
			DeclareOnObject(wlcr.mHitThing);
			DeclareOnGround() ;
		}
	}



	UpdateRotation();

	for (int n=0; n<kBattleEngineStores; n++)
	{
		if (mStoreHeat[n])
		{
			mStoreValue[n]-=kWeaponCoolRate;

			if (mStoreValue[n]<0)
				mStoreValue[n]=0;
			else if (mStoreValue[n]<mConfiguration->mStoreValue[n]*0.75f)
				mStoreOverheat[n]=FALSE;
		}
	}

	UpdateAutoAim();
	HandleSounds();

	if (mThrustersOn)
	{
		// Burn things that are beneath you
		CMapWhoEntry			*entry=MAP_WHO.GetFirstEntryWithinRadius(GetPos(),4);
		
		while (entry)
		{
			if (CThing *tempThing=entry->GetThing())
			{
				if ((tempThing->IsA(THING_TYPE_UNIT)) && (tempThing!=this))
				{
					if (GetPos().Z-2.0f<tempThing->GetPos().Z)
					{
						FVector		direction=tempThing->GetPos()-GetPos();
						float		magnitude=direction.Magnitude();

						if (magnitude<2.5f)
						{
							float	damage=0.2f/magnitude;

							((CUnit*)tempThing)->Damage(damage,this,TRUE,-1);
						}
					}
				}
			}

			entry=MAP_WHO.GetNextEntryWithinRadius();
		}

		GroundParticleEffect();
		ActivateThrusters();
	}
	else
		DeactivateThrusters();

	mThrustersOn=FALSE;
}
inline void CBattleEngine::PlayHudSample(CEffect *effect) { SOUND.PlayEffect(effect, this, SOUND.GetHUDMessageVolume(), ST_FOLLOWDONTDIE, 0, 0, 0, -1.f, 0, 1.f, ST_GAME_SOUND, 1); }
//******************************************************************************************
void CBattleEngine::HandleSounds()	// FUNCTION: 0x00409950
{
	if (!SOUND.IsEffectPlaying(mBattleEngineOnSound, this))
		PlayHudSample(mBattleEngineOnSound);
	
	// Energy and Health sounds
	if (mLife<7.0f)
	{
		SOUND.StopSample(mEnergyLowSound->mSample, this);

		if (mEnergyVeryLowSound)
			SOUND.StopSample(mEnergyVeryLowSound->mSample, this);

		if (!SOUND.IsEffectPlaying(mHealthLowSound, this))
			SOUND.PlayEffect(mHealthLowSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE, false, 0.0f, 0, -1.f, true);
	}
	/*else if (mEnergy<1.0f)
	{
		SOUND.StopSample(mHealthLowSound->mSample this);
		SOUND.StopSample(mEnergyLowSound->mSample, this);
		
		if (!SOUND.IsEffectPlaying(mEnergyVeryLowSound, this))
			SOUND.PlayEffect(mEnergyVeryLowSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE, false, 0.0f, 0, -1.f, true);
	}*/
	else if (mEnergy<2.0f)
	{
		SOUND.StopSample(mHealthLowSound->mSample, this);

		if (mEnergyVeryLowSound)
			SOUND.StopSample(mEnergyVeryLowSound->mSample, this);

		if (!SOUND.IsEffectPlaying(mEnergyLowSound, this))
			SOUND.PlayEffect(mEnergyLowSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE, false, 0.0f, 0, -1.f, true);	
	}
	else
	{
		CSoundEvent *s;
		
		s = SOUND.GetSoundEventForThing(mEnergyLowSound->mSample, this);
		if (s)
			SOUND.FadeTo(s->mSample, 0, 0.02f, this);

		if (mEnergyVeryLowSound)
		{
			s = SOUND.GetSoundEventForThing(mEnergyVeryLowSound->mSample,this);
			if (s)
				SOUND.FadeTo(s->mSample, 0, 0.02f, this);
		}

		s = SOUND.GetSoundEventForThing(mHealthLowSound->mSample, this);
		if (s)
			SOUND.FadeTo(s->mSample, 0, 0.02f, this);
	}

	// Incoming missile sounds
	if (mPlayIncomingMissileSound)
	{
		if (!SOUND.IsEffectPlaying(mIncomingMissileSound, this))
			PlayHudSample(mIncomingMissileSound);

		mPlayIncomingMissileSound=FALSE;
	}

	// Lock sounds
	CLockInfo		*info;
	
	for (info=mLocks.First(); info; info=mLocks.Next())
	{
		if ((!info->mLocked) && (info->mFinish<EVENT_MANAGER.GetTime()))
		{
			PlayHudSample(mTargetLockedSound);
			//SOUND.PlayEffect(mTargetLockedSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE);
			info->mLocked=TRUE;
		}
	}

	// Aiming sounds
	if (mAutoAimTarget.ToRead())
	{
		if ((!mHadAutoAimTarget.ToRead()) && (mAutoAimTarget.ToRead()))
		{
			PlayHudSample(mAutoAimSound);
			//SOUND.PlayEffect(mAutoAimSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE);
			mHadAutoAimTarget=mAutoAimTarget.ToRead();
		}
	}
	else if ((mCurrentUnitOverCrosshair.ToRead()) &&
			 (IsTargetAlligence(mCurrentUnitOverCrosshair->GetAllegiance())))
	{
		if ((!mHadAutoAimTarget.ToRead()) && (mCurrentUnitOverCrosshair.ToRead()))
		{
			PlayHudSample(mAutoAimSound);
			//SOUND.PlayEffect(mAutoAimSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE);
			mHadAutoAimTarget=mCurrentUnitOverCrosshair.ToRead();
		}
	}
	else
		mHadAutoAimTarget=NULL;

	// Walking sounds
	if ((IsOnGround()) && (mState==BATTLE_ENGINE_STATE_WALKER))
	{
		float	magnitude=mVelocity.Magnitude();

		mWalkSoundTime+=magnitude;
	
		if (mWalkSoundTime>1.5f)
		{
			//PlayHudSample(mBattleEngineFeetSound);
		//	SOUND.PlayEffect(mBattleEngineFeetSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE);
			mWalkSoundTime-=1.5f;
			mWalkSounds++;
		}

		if (magnitude<0.01f)
		{
			if (mWalkSounds>5)
				PlayHudSample(mPneumaticSound);
				//SOUND.PlayEffect(mPneumaticSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE);

			mWalkSounds=0;
		}
	}
	else
		mWalkSounds=0;
}

//******************************************************************************************
BOOL CBattleEngine::IsStandingOnAMoveingObject()
{
	if (IsOnObject() && mStandingOnThing.ToRead())
	{
		if (mStandingOnThing->GetVelocity().MagnitudeSq()>0.0f)
			return TRUE;
	}

	return FALSE;
}

//******************************************************************************************
FVector		CBattleEngine::GetLocalLastFrameMovement()	// FUNCTION: 0x00409de0
{
	FVector movement = SUPERTYPE::GetLocalLastFrameMovement() ;
	movement -=mStandingOnObjectMovement ;

	return movement ;
}


//******************************************************************************************
float	CBattleEngine::ZoomModifier(float z)	// FUNCTION: 0x00409e60
{
	return 1.f - ((1.f - z) );
}

//******************************************************************************************
void CBattleEngine::AutoZoomOut()	// FUNCTION: 0x00409e80
{
	mDesiredZoom=MAX_ZOOM_OUT;
}

//******************************************************************************************
void CBattleEngine::ZoomOut()	// FUNCTION: 0x00409e90
{
	CWeapon		*weapon=GetCurrentWeapon();

	if (weapon)
	{
		if (weapon->GetZoomMode()==kNormalZoom)
			mDesiredZoom=MAX_ZOOM_OUT;
	}
}

//******************************************************************************************
void CBattleEngine::ZoomIn()	// FUNCTION: 0x00409ec0
{
	CWeapon		*weapon=GetCurrentWeapon();

	if (weapon)
	{
		if (weapon->GetZoomMode()==kNormalZoom)
			mDesiredZoom=MAX_ZOOM_IN;
	}
}

//******************************************************************************************
void	CBattleEngine::ChargeWeapon()	// FUNCTION: 0x00409ef0
{
	if (mState==BATTLE_ENGINE_STATE_WALKER)
		mWalkerPart->ChargeWeapon();
	else if (mState==BATTLE_ENGINE_STATE_JET)
		mJetPart->ChargeWeapon();
}

//******************************************************************************************
void	CBattleEngine::FireWeapon()	// FUNCTION: 0x00409f20
{
	mSlowMovement=FALSE;

	mRecentLocks=mLocks.Size();
	if (mRecentLocks==0)
		mRecentLocks=mFiredLocks.Size();

	if (mState==BATTLE_ENGINE_STATE_WALKER)
		mWalkerPart->FireWeapon();
	else if (mState==BATTLE_ENGINE_STATE_JET)
		mJetPart->FireWeapon();
}

//******************************************************************************************
void	CBattleEngine::ChangeWeapon()	// FUNCTION: 0x00409f70
{
	if (CountActiveWeapons()<=1)
		return;

	mSlowMovement=FALSE;

	if (mState==BATTLE_ENGINE_STATE_WALKER)
		mWalkerPart->ChangeWeapon();
	else if (mState==BATTLE_ENGINE_STATE_JET)
		mJetPart->ChangeWeapon();

	mChangedWeaponTime = EVENT_MANAGER.GetTime() ;

	if (char *name = GetWeaponIconName())
	{
		// remove weapon part
		name+=7;

		// $$$!

		if (strcmp("Vulcan Cannon", name)==0)			PlayHudSample("hud_Vulcan_Cannon");
		else if (strcmp("Grenade", name)==0)			PlayHudSample("hud_Grenade_launcher");
		else if (strcmp("Torpedo", name)==0)			PlayHudSample("hud_Torpedo_launcher");
		else if (strcmp("Blaster", name)==0)			PlayHudSample("hud_Blaster");
		else if (strcmp("Flux Pod", name)==0)			PlayHudSample("hud_Flux_Missile");
		else if (strcmp("Micro Missile", name)==0)		PlayHudSample("hud_Micro_Missiles");
		else if (strcmp("Spread Bomb", name)==0)		PlayHudSample("hud_Spread_Bomb");
		{
			// These little puppies are augmentable.
			if (mAugActive)
			{
				     if (strcmp("Rail Gun"     , name)==0) PlayHudSample("hud_Rail_Gun_augmented");
				else if (strcmp("Beam Laser"   , name)==0) PlayHudSample("hud_beam_laser_augmented");
				else if (strcmp("Plasma Cannon", name)==0) PlayHudSample("hud_Pulse_Cannon_augmented");
			}
			else
			{
				     if (strcmp("Rail Gun"     , name)==0) PlayHudSample("hud_Rail_Gun");
				else if (strcmp("Beam Laser"   , name)==0) PlayHudSample("hud_beam_laser");
				else if (strcmp("Plasma Cannon", name)==0) PlayHudSample("hud_Pulse_Cannon");
			}
		}
	}
}

//******************************************************************************************
void	CBattleEngine::LoseWeaponCharge() 
{
	if (mState==BATTLE_ENGINE_STATE_WALKER)
		mWalkerPart->LoseWeaponCharge();
	else if (mState==BATTLE_ENGINE_STATE_JET)
		mJetPart->LoseWeaponCharge();
}

//******************************************************************************************
void	CBattleEngine::ConfirmedKill(CUnit		*inKilled)	// FUNCTION: 0x0040a560
{
	if (inKilled->GetAllegiance()!=kMuspellAllegiance) return ;
	if (mPlayer.ToRead()) mPlayer->KilledEnemyThing(inKilled) ;

}


//******************************************************************************************
void CBattleEngine::Morph()	// FUNCTION: 0x0040a580
{
	if ((!mFlightModeActive) && (mState==BATTLE_ENGINE_STATE_WALKER))
		return;

	mSlowMovement=FALSE;

	if (mState == BATTLE_ENGINE_STATE_MORPHING_INTO_JET) return ;
	if (mState == BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER) return ;

	if (mJetPart->GetIsDoingSpecialAirMove() == TRUE) return ;
	if (mWalkerPart->GetIsDoingSpecialWalkerMove() == TRUE) return ;

	mWalkerPart->LoseWeaponCharge();
	mJetPart->LoseWeaponCharge();

	AutoZoomOut();
	
	if (mState == BATTLE_ENGINE_STATE_JET)
	{
		EVENT_MANAGER.AddEvent((int)BECOME_WALKER, this, EVENT_MANAGER.GetTime() + BATTLE_ENGINE_TRANSFORM_TIME, START_OF_FRAME) ;
		
		if (IsOnGround())
			mTransformStartTime=EVENT_MANAGER.GetTime();
		else
			mTransformStartTime=0.0f;
	
		if (mCockpit)
			mCockpit->MorphIntoWalkerCockpit();

		mState=BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER;
		SetAnimMode("flytowalk", TRUE, FALSE) ;

		if (mInFlightSound)
		{
			CSoundEvent *s = SOUND.GetSoundEventForThing(mInFlightSound->mSample, this);
			if (s)
				SOUND.FadeTo(s->mSample, 0, 0.02f, this);
		}

		SOUND.PlayEffect(mLandingSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE);

		mStalling=FALSE;
	}
	else
	{
		if (mEnergy>=mConfiguration->mMinTransformEnergy)
		{
			if (GetThreat()>0.0f)
				INFLUENCEMAP->AddResidual(mPos.X,mPos.Y,GetThreat(),mAllegiance);

			mStalling=FALSE;

			if (mCockpit)
				mCockpit->MorphIntoJetCockpit();

			mState=BATTLE_ENGINE_STATE_MORPHING_INTO_JET;
			MassiveHackPutUsInRightMesh();
		
			SetAnimMode("walktofly", TRUE,FALSE);

			if (EVENT_MANAGER.GetTime()-mLastTimeOnGround<0.6f)
				mTakeOffHeight=mPos.Z;
			else
			{
				mTakeOffHeight=99999.0f;
				EVENT_MANAGER.AddEvent((int)BECOME_JET,this,EVENT_MANAGER.GetTime()+0.1f,START_OF_FRAME);
			}

			mTakeOffTime=EVENT_MANAGER.GetTime();

			SOUND.PlayEffect(mTakeOffSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE);

			if (mInFlightSound)
			{
				if (!SOUND.IsEffectPlaying(mInFlightSound, this))
					SOUND.PlayEffect(mInFlightSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE, true, 0.02f, 0, -1.f, true);
				else
				{
					CSoundEvent *s = SOUND.GetSoundEventForThing(mInFlightSound->mSample, this);
					if (s)
						SOUND.FadeTo(s->mSample, 1.f, 0.02f, this);
				}
			}
		}
	}
}

//******************************************************************************************
void	CBattleEngine::Damage(	// FUNCTION: 0x0040a890
	float		inAmount,
	CThing		*inByThis,
	BOOL		inDamageShields,
	int			mesh_part_no)
{
	float	lastLife=mLife;
	float   lastshields = mShields;
	float   lastenergy = mEnergy;

	if (inAmount>0)
	{
		mPlayer->IncStat(PS_DAMAGETAKEN,(int) (inAmount*256));

		if (mLife>=0.0f)
		{
			if (mShields>=inAmount)
			{
				float	shieldDamage=inAmount*(mConfiguration->mShieldEfficiency/100.0f);

				if (!inDamageShields)
					shieldDamage=0;

				mShields-=shieldDamage;
				inAmount-=shieldDamage;

				if ((!mAugActive) && (mWalkerPart->GetAugWeapon()))
					mAugValue+=shieldDamage;
			}
			
			if ((inAmount>mShields) && (inDamageShields))
			{
				if (!mAugActive)
					mAugValue+=mShields;

				inAmount-=mShields;
				mShields=0;
			}

			mLife-=inAmount;
			if (mLife<0.0f && mVulnerable == TRUE)
			{
				StartDieProcess(); 
			}
		}

		mLastDamageTime=EVENT_MANAGER.GetTime();

		if (inByThis)
			AddDamageFlash(inByThis->GetPos());
	}

	if (mAugValue>MAX_AUG_VALUE)
		mAugValue=MAX_AUG_VALUE;

	if (mState==BATTLE_ENGINE_STATE_WALKER)
		mEnergy=mShields;

	if (inAmount<0)
	{
		// Damage <0 therefore repair
		if (mLife<mConfiguration->mLife)
		{
			if (mConfiguration->mLife-mLife<-inAmount)
			{
				float	diff=mConfiguration->mLife-mLife;

				inAmount+=diff;
				mLife=mConfiguration->mLife;
			}
			else
			{
				mLife-=inAmount;
				inAmount=0;
			}
		}

		if (mEnergy<mConfiguration->mEnergy)
		{
			if (mConfiguration->mEnergy-mEnergy<-inAmount)
			{
				float	diff=mConfiguration->mLife-mLife;

				inAmount+=diff;
				mEnergy=mConfiguration->mEnergy;
			}
			else
			{
				mEnergy-=inAmount;
				inAmount=0;
			}
		}
	}

	float	diff=(lastLife-mLife)/8;

	if (mShields)
		diff/=2.0f;
	if (diff>0.25f)
		diff=0.25f;

	AddShockShake(diff);

	if (diff>0.05f)
		diff=0.05f;
	mVibration+=diff*50.0f;

	if (mVulnerable == FALSE)
	{
		mLife = lastLife; 
		mShields =lastshields ;
		mEnergy = lastenergy ;
	}
}


//******************************************************************************************
BOOL			CBattleEngine::GetRequiresPolyBucket()	// FUNCTION: 0x0040ac30
{
	if (GAME.IsMultiplayer())
	{
		return TRUE ;
	}

	return FALSE ;
}


//******************************************************************************************
void	CBattleEngine::Rearm(	// FUNCTION: 0x0040ac50
	float		inAmount)
{
	if (inAmount>0)
	{
		float		amount;

		for (int n=0; n<kBattleEngineStores; n++)
		{
			if (!mStoreHeat[n])
			{
				amount=inAmount*mConfiguration->mStoreValue[n];
				mStoreValue[n]+=amount;

				if (mStoreValue[n]>mConfiguration->mStoreValue[n])
					mStoreValue[n]=mConfiguration->mStoreValue[n];
			}
		}
	}
}

//******************************************************************************************
CUnit*	CBattleEngine::CalcUnitOverCrossHair(	// FUNCTION: 0x0040acc0
	CEvent		*inEvent,
	BOOL		inMeshCollision,
	BOOL		inUpdateData)
{
	CUnit	*result=NULL;

	if (mPlayer.ToRead())
	{
		if (inUpdateData)
		{
			mCurrentUnitOverCrosshair=NULL;
			mCurrentUnitOverCrosshairRegardlessOfRange=NULL;
		}

		FMatrix		autoAimMatrix=FMatrix(mAutoAimYawOffset,mAutoAimPitchOffset,0.0f);
		
		CWeapon		*weapon=GetCurrentWeapon();
		float		range=weapon->GetActualMaxRange(mPos);

		if (range<=0.0f)
			range=1000.0f;

		FVector		pos = mPlayer->GetCurrentViewPoint() ;
		FMatrix		ori = mPlayer->GetCurrentViewOrientation()*autoAimMatrix;

//		CLine ray(pos, pos + (ori * FVector(0.0f,range,0.0f))) ;
		CLine ray(pos, pos + (ori * FVector(0.0f,1000.f,0.0f))) ;

		ECollisionLevel		collisionLevel;

		if (inMeshCollision)
			collisionLevel=ECL_MESH;
		else
			collisionLevel=ECL_OUTER_SPHERE;

		CWorldLineColReport		*wlcr;
		CWorldLineColReport		tempWlcr;

		if (inUpdateData)
			wlcr=&mWlcr;
		else
			wlcr=&tempWlcr;

		if (WORLD.FindFirstThingToHitLine(ray, this , wlcr,FALSE,inMeshCollision,collisionLevel,THING_TYPE_TREE+THING_TYPE_AMMUNITION,TRUE) == kCollideThing)	// PC: the map flag set (0040affc)
		{
			if (wlcr->mHitThing->IsA(THING_TYPE_UNIT))
			{
				CUnit* unit = (CUnit*)wlcr->mHitThing;

				if (!unit->IsA(THING_TYPE_BUILDING) || (unit->GetLife() >0.0f))
				{
					if (range > wlcr->mDistToImpact)
						result = (CUnit*)wlcr->mHitThing;
					
					if (inUpdateData)
						mCurrentUnitOverCrosshairRegardlessOfRange = (CUnit*)wlcr->mHitThing;
				}
			}
		}
	}

	if (inEvent)
		EVENT_MANAGER.AddEvent((SINT)CALC_UNIT_OVER_CROSSHAIR,this,EVENT_MANAGER.GetTime()+GAME.FloatRandom()*0.2f+0.1f,START_OF_FRAME,NULL,(CScheduledEvent*)inEvent);

	return result;
}

//******************************************************************************************
CThing*	CBattleEngine::GetThingOverCrossHair()
{
	float			range=200.0f;

	FVector pos = mPlayer->GetCurrentViewPoint() ;
	FMatrix ori = mPlayer->GetCurrentViewOrientation() ;

	CLine ray(pos, pos + (mOrientation * FVector(0.0f,range,0.0f))) ;

	CWorldLineColReport wlcr;
	if (WORLD.FindFirstThingToHitLine(ray, this , &wlcr,FALSE,TRUE,ECL_MESH) == kCollideThing)
	{
		return wlcr.mHitThing ;
	}

	return NULL;
}

//******************************************************************************************
void CBattleEngine::UpdateAutoAim()	// FUNCTION: 0x0040b120
{	// Naming the launch vector preserves retail's speed-sum order and complete inline expansion.
	CWeapon		*weapon=GetCurrentWeapon();

	float		currentYaw=mCurrentOrientation.mYaw;
	float		currentPitch=mCurrentOrientation.mPitch;

	// Calculate desired firing offsets
	mOldTrackingYawOffset=mTrackingYawOffset;
	mOldTrackingPitchOffset=mTrackingPitchOffset;

	if (mAutoAimTarget.ToRead())
	{
		FVector		op;
			
		if (mAutoAimTarget->IsA(THING_TYPE_UNIT))
			((CUnit*)mAutoAimTarget.ToRead())->GetTargetablePos(&op);	// Stuart's GetTargetablePos: CUnit's slot 90 (0040b17a)
		else
			mAutoAimTarget->GetCentrePos(op);

		if (weapon->CanPredict())
		{
			// Calculate predicting pos
			FVector		bp=GetPos()+GetVelocity();
			FVector		launchVector(0,weapon->GetLaunchVelocity(),0);	// STAND-IN (ids): Stuart builds the vector in the product; 13 bytes off
			FVector		bv=mOrientation*launchVector;
			FVector		ov=mAutoAimTarget->GetVelocity();

			float		t=(bp-op).Magnitude()/(bv.Magnitude())*GAME_FR;

			FVector		predictedPos=op+ov*t;

			FVector		diff=predictedPos-(GetPos()+GetVelocity());

			mDesiredAutoAimYawOffset=-AngleDifference(currentYaw,diff.Azimuth());
			mDesiredAutoAimPitchOffset=-AngleDifference(currentPitch,diff.Elevation());

			if (fabsf(mDesiredAutoAimYawOffset)+fabsf(mDesiredAutoAimPitchOffset)>0.2f)
			{
				mTrackingYawOffset=mDesiredAutoAimYawOffset;
				mTrackingPitchOffset=mDesiredAutoAimPitchOffset;
				
				if (!mTrackingActive)
				{
					mOldTrackingYawOffset=mTrackingYawOffset;
					mOldTrackingPitchOffset=mTrackingPitchOffset;
				}
				
				mTrackingActive=TRUE;
	
				mDesiredAutoAimYawOffset=0;
				mDesiredAutoAimPitchOffset=0;
			}
			else
				mTrackingActive=FALSE;
		}
		else
		{
			FVector		diff=op-(GetPos()+GetVelocity());

			mDesiredAutoAimYawOffset=-AngleDifference(currentYaw,diff.Azimuth());
			mDesiredAutoAimPitchOffset=-AngleDifference(currentPitch,diff.Elevation());
			mTrackingActive=FALSE;
		}
	}
	else
	{
		mDesiredAutoAimYawOffset=0.0f;
		mDesiredAutoAimPitchOffset=0.0f;
		mTrackingActive=FALSE;
	}

	// Update firing offsets
	mOldAutoAimYawOffset=mAutoAimYawOffset;
	mOldAutoAimPitchOffset=mAutoAimPitchOffset;

	mAutoAimYawOffset+=(mDesiredAutoAimYawOffset-mAutoAimYawOffset)*0.5f;
	mAutoAimPitchOffset+=(mDesiredAutoAimPitchOffset-mAutoAimPitchOffset)*0.5f;
}

//******************************************************************************************
// STAND-IN (inline budget): a local reconstruction of Stuart's reference call shape. The retail callers
// pass this same output address through unit slot 90 (0040b95c, 0040be40).
// Keeping the interface also leaves feature Elevation's Magnitude out of line.
// Used at the auto-aim line test only: HandleAutoAim's target loop calls the member directly
// (checked, 2026-10-14: the helper there changes nothing; here it loses 1364 bytes).
static inline void GetTargetablePosition(CUnit *unit,FVector &position)
{
	unit->GetTargetablePos(&position);
}

// Retail 0040bbec..0040bc32 keeps the Z difference extended, rounds X/Y to
// float, and adds (Z squared + X squared) before Y squared. This distinct range
// value must not be reused for Elevation's later Magnitude call at 0040bcc8.
// STAND-IN (spelling, ids): this helper expresses the measured boundary; its name is not recovered. Stuart's
// `diff.MagnitudeSq()` at the feature range test is 761 bytes off.
static inline float GetAutoAimFeatureRangeSquared(const FVector &delta)
{
	float zSquared=delta.Z*delta.Z;
	return (zSquared+(delta.X*delta.X))+(delta.Y*delta.Y);
}

void CBattleEngine::HandleAutoAim(	// FUNCTION: 0x0040b6d0
	CEvent		*inEvent)
{
	CWeapon		*weapon=GetCurrentWeapon();

	float		currentYaw=(mOrientation*FVector(0,1,0)).Azimuth();
	float		currentPitch=(mOrientation*FVector(0,1,0)).Elevation();

	float		minRange=weapon->GetMinRange(mPos);
	float		maxRange=weapon->GetMaxRange(mPos);

	mAutoAimTarget=NULL;

	if (GAME.GetAllowedAutoAim() == FALSE) 
	{
		mAutoAimYawOffset=0;
		mAutoAimPitchOffset=0;
		mOldAutoAimYawOffset=0;
		mOldAutoAimPitchOffset=0;
		return ;
	}
	
	// Calculate auto aim position 
	if (weapon->IsSmart())
	{
		float			range=maxRange;

		if (range<=0.0f)
		{
			range=1000.0f;
			maxRange=1000.0f;
		}

		CMapWhoEntry	*entry=MAP_WHO.GetFirstEntryWithinRadius(GetPos(),range);
		float			bestAngleDiff=0.2f;

		if (weapon->CanPredict())
			bestAngleDiff=0.8f;

		while (entry)
		{
			if (CThing *thing=entry->GetThing())
			{
				if (thing->IsA(THING_TYPE_UNIT))
				{
					if ((IsTargetAlligence(((CUnit*)thing)->GetAllegiance())) &&
						(!thing->IsDying()))
					{
						BOOL	canAutoAim=TRUE;

						if ((thing->IsA(THING_TYPE_AIR_UNIT)) && (!weapon->CanPredict()))
							canAutoAim=FALSE;
						
						if (((CUnit*)thing)->IsBig())
							canAutoAim=FALSE;
						else if (!((CUnit*)thing)->Active())
							canAutoAim=FALSE;
						else if (((CUnit*)thing)->HasNexus())
							canAutoAim=FALSE;
						else if (((CUnit*)thing)->HasWeakPoint())
							canAutoAim=FALSE;
						else if (!((CUnit*)thing)->IsLockable())
							canAutoAim=FALSE;

						if (canAutoAim)
						{
							FVector		targetPos;
							((CUnit*)thing)->GetTargetablePos(&targetPos);

							FVector		diff=targetPos-GetPos();
							float		distSq=diff.MagnitudeSq();
							float		tempMaxRange=maxRange*(1.0f-((CUnit*)thing)->GetStealth()/100.0f);

							// Half the maximum auto aim range on things that are not on the scanner
							if (!((CUnit*)thing)->OnScanner())
								tempMaxRange/=2;

							if ((minRange*minRange<=distSq) && (tempMaxRange*tempMaxRange>=distSq))
							{
								float		yawDiff=-AngleDifference(currentYaw,diff.Azimuth());
								float		pitchDiff=-AngleDifference(currentPitch,diff.Elevation());
								float		angleDiff=fabsf(yawDiff)+fabsf(pitchDiff);
								
								float		tempAngleDiff=bestAngleDiff;

								// Don't predict on units that are slow
								if ((thing->GetVelocity().MagnitudeSq()<0.1f*0.1f) && (tempAngleDiff>0.2f))
									tempAngleDiff=0.2f;

								if (angleDiff<tempAngleDiff)
								{						
									mAutoAimTarget=(CUnit*)thing;
									bestAngleDiff=angleDiff;
								}
							}
						}
					}
				}
				else if (thing->IsA(THING_TYPE_FEATURE))
				{
					CFeature	*feature=(CFeature*)thing;

					if (feature->IsLockable())
					{
						if (IsTargetAlligence(feature->GetAllegiance()))
						{
							FVector		targetPos;
							
							feature->GetCentrePos(targetPos);

							FVector		diff=targetPos-GetPos();
							float		distSq=GetAutoAimFeatureRangeSquared(diff);

							if ((minRange*minRange<=distSq) && (maxRange*maxRange>=distSq))
							{
								float		yawDiff=-AngleDifference(currentYaw,diff.Azimuth());
								float		pitchDiff=-AngleDifference(currentPitch,diff.Elevation());
								float		angleDiff=fabsf(yawDiff)+fabsf(pitchDiff);
								float		tempAngleDiff=bestAngleDiff;

								// Don't predict on units that are slow
								if ((thing->GetVelocity().MagnitudeSq()<0.1f*0.1f) && (tempAngleDiff>0.2f))
									tempAngleDiff=0.2f;

								if (angleDiff<tempAngleDiff)
								{						
									mAutoAimTarget=(CUnit*)thing;
									bestAngleDiff=angleDiff;
								}
							}
						}
					}
				}
			}

			entry=MAP_WHO.GetNextEntryWithinRadius();
		}
	}

	// Is the target visible
	if (mAutoAimTarget.ToRead())
	{
		if (mAutoAimTarget->IsA(THING_TYPE_BUILDING))
			mAutoAimTarget=NULL;
	}

	if (mAutoAimTarget.ToRead())
	{
		FVector					targetPos;
			
		if (mAutoAimTarget->IsA(THING_TYPE_UNIT))
			GetTargetablePosition((CUnit*)mAutoAimTarget.ToRead(),targetPos);
		else
			mAutoAimTarget->GetCentrePos(targetPos);

		CLine					line(GetPos(),targetPos);
		CWorldLineColReport		wlcr;

		// Retail 0040becc..0040bed8 selects approximate shapes, ignores rounds,
		// and enables the map query flag; the other callers use different options.
		if (WORLD.FindFirstThingToHitLine(line,this,&wlcr,FALSE,TRUE,
			ECL_APPROX_GEOMETRY_SHAPES,THING_TYPE_AMMUNITION,TRUE,-1)==kCollideThing)
		{
			if (wlcr.mHitThing!=mAutoAimTarget.ToRead())
				mAutoAimTarget=NULL;
		}
		else
			mAutoAimTarget=NULL;
	}

	EVENT_MANAGER.AddEvent((SINT)HANDLE_AUTO_AIM,this,EVENT_MANAGER.GetTime()+GAME.FloatRandom()*0.1f+0.2f,START_OF_FRAME,NULL,(CScheduledEvent*)inEvent);
}

//******************************************************************************************
BOOL CBattleEngine::StartDieProcess()	// FUNCTION: 0x0040bfd0
{
	if (IsDying())
		return FALSE;
	
	if (mPlayer.ToRead())
	{
		CCONTROLLER		*controller=GAME.GetController(mPlayer->GetNumber()-1);

		if (controller)
			controller->SetVibration(0, mPlayer->GetNumber()-1);
	}

	mFlags |= TF_DYING;

	GAME.DeclarePlayerDead(mPlayer->GetNumber());

	if (mMissionScript)
	{
		mMissionScript->Died();
		delete mMissionScript;
		mMissionScript=NULL;
	}
	
	Explode();

	// Kick off the smoke
	CParticleDescriptor	*pd=PARTICLE_SET.GetPD("Oily Smoke Effect");

	PARTICLE_MANAGER.AddParticle(pd,&mSmoke);
	mSmoke.SetPos(mPos);

	return TRUE;
}


//******************************************************************************************
void CBattleEngine::HandleIncomingMissle()
{
	if (mLastIncomingMissileVoice<EVENT_MANAGER.GetTime())
	{
		PlayHudSample("hud_incoming_missile");
		mLastIncomingMissileVoice=EVENT_MANAGER.GetTime()+2.0f;
	}
}


//******************************************************************************************
void CBattleEngine::HandleEvent(CEvent* event)	// FUNCTION: 0x0040c180
{
	switch ((EBattleEngineEvent)event->GetEventNum())
	{
		case BECOME_JET:
		{
			if (mState==BATTLE_ENGINE_STATE_MORPHING_INTO_JET)
			{
				mTransformStartTime=EVENT_MANAGER.GetTime();
				mState=BATTLE_ENGINE_STATE_JET;
				SetCollisionShape();
			}
			break ;
		}

		case BECOME_WALKER:
		{
			mState=BATTLE_ENGINE_STATE_WALKER;
			SetCollisionShape();
			break ;
		}

		case INCOMING_MISSILE_WARNING:
		{
			HandleIncomingMissle() ;
			break;
		}

		case CALC_UNIT_OVER_CROSSHAIR:
		{
			mCurrentUnitOverCrosshair=CalcUnitOverCrossHair(event,TRUE,TRUE);
			break;
		}

		case HANDLE_AUTO_AIM:
		{
			HandleAutoAim(event);
			break;
		}

		default:
		{
			SUPERTYPE::HandleEvent(event) ;
		}
	}
}

//******************************************************************************************
BOOL CBattleEngine::WeaponFired(	// FUNCTION: 0x0040c2e0
	CWeapon		*inWeapon)
{
	if (mJetPart->WeaponFired(inWeapon))
	{
		mStealth=0.0f;
		return TRUE;
	}

	if (mWalkerPart->WeaponFired(inWeapon))
	{
		mStealth=0.0f;
		return TRUE;
	}

	return FALSE;
}

//******************************************************************************************
void CBattleEngine::RecoilWeapon(	// FUNCTION: 0x0040c340
	CWeapon	*inWeapon)
{
	AddShockShake(inWeapon->GetPower());
	mVibration+=inWeapon->GetPower()*2.0f;
}

//******************************************************************************************
CWeapon* CBattleEngine::GetCurrentWeapon()	// FUNCTION: 0x0040c380
{
	if (mState!=BATTLE_ENGINE_STATE_JET)
		return mWalkerPart->GetCurrentWeapon();
	else
		return mJetPart->GetCurrentWeapon();
}

//******************************************************************************************
BOOL CBattleEngine::IsWeaponOverheated()	// FUNCTION: 0x0040c3a0
{
	if (mState!=BATTLE_ENGINE_STATE_JET)
		return mWalkerPart->IsWeaponOverheated();
	else
		return mJetPart->IsWeaponOverheated();
}

//******************************************************************************************
float CBattleEngine::GetWeaponAmmoPercentage()	// FUNCTION: 0x0040c3c0
{
	if (stricmp(mConfiguration->mConfigurationName,"Racer")==0)
	{
		float v=GetVelocity().Magnitude()/1.5f;
		if (v>1.0f)
			v=1.0f;
		return(v);
	}
	else
	{
		if (mState!=BATTLE_ENGINE_STATE_JET)
			return mWalkerPart->GetWeaponAmmoPercentage();
		else
			return mJetPart->GetWeaponAmmoPercentage();
	}
}

//******************************************************************************************
SINT CBattleEngine::GetWeaponAmmoCount()	// FUNCTION: 0x0040c460
{
	if (mState!=BATTLE_ENGINE_STATE_JET)
		return mWalkerPart->GetWeaponAmmoCount();
	else
		return mJetPart->GetWeaponAmmoCount();
}

//******************************************************************************************
BOOL CBattleEngine::IsEnergyWeapon()	// FUNCTION: 0x0040c480
{
	if (mState!=BATTLE_ENGINE_STATE_JET)
		return mWalkerPart->IsEnergyWeapon();
	else
		return mJetPart->IsEnergyWeapon();
}

//******************************************************************************************
float CBattleEngine::GetWeaponCharge()	// FUNCTION: 0x0040c4a0
{
	if (stricmp(mConfiguration->mConfigurationName,"Racer")==0)
	{
		FVector pos=GetRenderPos();
		float tfr=MAP.Collide(pos);
		if (MAP.GetWaterLevel()<tfr)
			tfr=MAP.GetWaterLevel();
		float v=fabsf(tfr-pos.Z)/5.0f;
		
		if (v>1.0f)
			v=1.0f;
		return(v);
	}
	else
	{
		if (mState!=BATTLE_ENGINE_STATE_JET)
			return mWalkerPart->GetWeaponCharge();
		else
			return mJetPart->GetWeaponCharge();
	}
}

//******************************************************************************************
WCHAR* CBattleEngine::GetWeaponName()	// FUNCTION: 0x0040c550
{
	if (mState!=BATTLE_ENGINE_STATE_JET)
		return mWalkerPart->GetWeaponName();
	else
		return mJetPart->GetWeaponName();
}

//******************************************************************************************
char* CBattleEngine::GetWeaponPhysicsName()	// FUNCTION: 0x0040c570
{
	if (mState!=BATTLE_ENGINE_STATE_JET)
		return mWalkerPart->GetWeaponPhysicsName();
	else
		return mJetPart->GetWeaponPhysicsName();
}

//******************************************************************************************
char* CBattleEngine::GetWeaponIconName()
{
	if (mState!=BATTLE_ENGINE_STATE_JET)
		return mWalkerPart->GetWeaponIconName();
	else
		return mJetPart->GetWeaponIconName();
}

//******************************************************************************************
float CBattleEngine::GetWeaponReadiness()
{
	if (mState!=BATTLE_ENGINE_STATE_JET)
		return mWalkerPart->GetWeaponReadiness();
	else
		return mJetPart->GetWeaponReadiness();
}

//******************************************************************************************
SINT CBattleEngine::WhereIsCurrentWeaponAttached()	// FUNCTION: 0x0040c590
{
	if (mState!=BATTLE_ENGINE_STATE_JET)
		return mWalkerPart->WhereIsCurrentWeaponAttached();
	else
		return mJetPart->WhereIsCurrentWeaponAttached();
}

//******************************************************************************************
BOOL CBattleEngine::FullHealth()	// FUNCTION: 0x0040c5b0
{
	if ((mEnergy<mConfiguration->mEnergy) || (mLife<mConfiguration->mLife))
		return FALSE;

	return TRUE;
}

//******************************************************************************************
BOOL CBattleEngine::NeedsRearm()	// FUNCTION: 0x0040c5e0
{
	for (int n=0; n<kBattleEngineStores; n++)
	{
		if (!mStoreHeat[n])
		{
			if (mStoreValue[n]<mConfiguration->mStoreValue[n])
				return TRUE;
		}
	}

	return FALSE;
}

//******************************************************************************************
float CBattleEngine::GetMaxEnergy()	// FUNCTION: 0x0040c630
{
	return mConfiguration->mEnergy;
}

//******************************************************************************************
void CBattleEngine::ConfigurationUp()
{

}

//******************************************************************************************
void CBattleEngine::ConfigurationDown()
{

}

//******************************************************************************************
void CBattleEngine::UpdateConfiguration()	// FUNCTION: 0x0040c650
{
	CBattleEngineData	*data=UBattleEngineConfigurations::GetConfiguration(mConfigurationId);

	if (mConfiguration!=data)
	{
		mConfiguration=data;

		mEnergy=mConfiguration->mEnergy;
		mLife=mConfiguration->mLife;

		if (mJetPart)
			mJetPart->ResetConfiguration();

		if (mWalkerPart)
			mWalkerPart->ResetConfiguration();

		for (int n=0; n<kBattleEngineStores; n++)
		{
			mStoreOverheat[n]=FALSE;

			mStoreHeat[n]=mConfiguration->mStoreHeat[n];

			if (!mStoreHeat[n])
				mStoreValue[n]=mConfiguration->mStoreValue[n];
			else
				mStoreValue[n]=0;
		}

		LOG.AddMessage(data->mConfigurationName);
	}
}

//******************************************************************************************
void CBattleEngine::DeclareOnObject(CThing* object)	// FUNCTION: 0x0040c720
{
	MassiveHackPutUsInRightMesh();
	mStandingOnThing.SetReader(object) ;
	SUPERTYPE::DeclareOnObject(object) ;
}


//******************************************************************************************
void CBattleEngine::DeclareOnGround()	// FUNCTION: 0x0040c750
{
	MassiveHackPutUsInRightMesh();
	SUPERTYPE::DeclareOnGround();

	if (IsDying())
	{
		Explode();
		AddShutdownEvent();
	}
	else
	{
		if (IsOnObject())
			return;

		float	cap=0.2f;

		if (mState==BATTLE_ENGINE_STATE_WALKER)
		{
			//can't damage yourself on ground in dash mode
			if (mWalkerPart->GetDashCount() != 0)
				return;

			cap=0.4f;
		}

		float	velocitySq=mVelocity.MagnitudeSq();

		if (velocitySq>cap*cap)
		{
			FVector		mapNormal=MAP.Normal(mPos);
			FVector		velocity=mVelocity;

			mapNormal.Normalise();
			velocity.Normalise();

			float		cosSurface=mapNormal*velocity;
			cosSurface*=cosSurface;

			Damage(sqrtf(velocitySq)*16*cosSurface,NULL,FALSE);

			SetVelocity(mVelocity*(1-cosSurface));
		}
		else if (mState==BATTLE_ENGINE_STATE_JET) 
		{
			SetVelocity(mVelocity*0.90f);
		}
	}
}

//******************************************************************************************
void CBattleEngine::GetLaunchPosition(	// FUNCTION: 0x0040c990
	CWeapon		*inWeapon,
	int			inIndex,
	FVector		&outPos,
	FMatrix		&outOrientation,
	BOOL		inNeedOrientation)
{
	if (mCockpit)
		mCockpit->GetRenderThing()->GetRTEmitter("Gun",inIndex,outPos,outOrientation,FALSE,TRUE);

	if (outPos==FVector(0,0,0))
	{
		outPos=inWeapon->GetPrimaryPosition();
		outOrientation=inWeapon->GetPrimaryOrientation();
	}

	// Adjust the angle to target if the weapon is facing forward
	if (inNeedOrientation)
	{
		FMatrix		autoAimMatrix=FMatrix(mAutoAimYawOffset,mAutoAimPitchOffset,0.0f);

		if (inWeapon->Gravity())
		{
			FVector cam_pos = mPlayer->GetCurrentViewPoint() ;
			FMatrix cam_ori = mPlayer->GetCurrentViewOrientation() ;
			FVector					endPoint=cam_pos+(cam_ori*autoAimMatrix*FVector(0.0f,200.0f,0.0f));
			CLine					targetLine(cam_pos,endPoint);	
			float					pitch=-PI/4;
			FVector					hitPoint;

			if (mWlcr.mHitType!=kCollideNothing)
			{
				FVector		targetVector=endPoint-cam_pos;
				FVector		hitPoint=targetVector*(mWlcr.mDistToImpact/targetVector.Magnitude())+cam_pos;

				if (inWeapon->ValidRound())
					pitch=inWeapon->CalculateDesiredPitch(hitPoint);
			}
		
			outOrientation=FMatrix(mCurrentOrientation.mYaw+mAutoAimYawOffset,pitch,0.0f);
		}
		else
		{
			FMatrix		orientation=inWeapon->GetPrimaryOrientation();
			FVector		vectorA=orientation*FVector(0,1,0);
			FVector		vectorB=outOrientation*FVector(0,1,0);

			float		angle=vectorA*vectorB;

			if ((angle>0.9f) && (inWeapon->AdjustAim()))
			{
				FVector cam_pos = mPlayer->GetCurrentViewPoint() ;
				FMatrix cam_ori = mPlayer->GetCurrentViewOrientation() ;
				FVector					endPoint=cam_pos+(cam_ori*autoAimMatrix*FVector(0.0f,200.0f,0.0f));
				CLine					targetLine(cam_pos,endPoint);
				
				if (mWlcr.mHitType!=kCollideNothing)
				{
					FVector		targetVector=endPoint-cam_pos;
					FVector		hitPoint=targetVector*(mWlcr.mDistToImpact/targetVector.Magnitude())+cam_pos;
					FVector		adjustedLine=hitPoint-outPos;

					outOrientation=FMatrix(adjustedLine.Azimuth(),adjustedLine.Elevation(),0.0f);
				}
				else
					outOrientation=mOrientation*autoAimMatrix;
			}
		}
	}
}

// STAND-IN (layout): CRoundData's ballistic-arc predicate (it has gravity, is not a beam and not mUnknown6C), a plain
// function here, not inline, so weapon.cpp's identical out-of-line IsBallistic folds into it (/OPT:ICF folds only
// copies of the same selection) and .text keeps retail's size. Retail's was an inline: its 0040d0f0 heads
// GetLaunchPosition's batch of deferred inlines, while this plain copy comes after that batch. GetLaunchPosition, defined
// above, calls it out of line (0040caca, inside CWeapon::Gravity); weapon.cpp's expanders use its own inline copy.
// FUNCTION: 0x0040d0f0 ?Unknown40d0f0@CRoundData@@QAEHXZ
BOOL CRoundData::Unknown40d0f0()
{
	return mGravity * 0.025f != 0.0f && !mBeam && !mUnknown6C;
}

//******************************************************************************************
#ifdef RESBUILDER
void CBattleEngine::AccumulateResources( CResourceAccumulator * accumulator,DWORD flags )
{
	if( mCockpit )
		mCockpit->AccumulateResources( accumulator);

	mWalkerPart->AccumulateResources( accumulator );
	mJetPart->AccumulateResources( accumulator );

	accumulator->AddMesh(CMesh::GetMesh("f_be1.msh"),flags);
	accumulator->AddMesh(CMesh::GetMesh("f_be2.msh"),flags);

	if (GAME.IsMultiplayer())
	{
		accumulator->AddMesh(CMesh::GetMesh("m_be1.msh"),flags);
		accumulator->AddMesh(CMesh::GetMesh("m_be2.msh"),flags);
	}

	UBattleEngineDataManager::AccumulateResources( accumulator );

	SUPERTYPE::AccumulateResources(accumulator,flags);
}
#endif
//******************************************************************************************
void CBattleEngine::HandleCloak()	// FUNCTION: 0x0040d4d0
{
	if (mCloaked)
		Decloak();
	else if (mEnergy>=mConfiguration->mMinTransformEnergy)
		Cloak();
}

//******************************************************************************************
void CBattleEngine::Cloak()
{
	if (mConfiguration->mStealth>0)
	{
		mDesiredStealth=mConfiguration->mStealth;
		mCloaked=TRUE;
	}
}

//******************************************************************************************
void CBattleEngine::Decloak()
{
	mDesiredStealth=0;
	mCloaked=FALSE;
}

//******************************************************************************************
void	CBattleEngine::Render(DWORD flags)	// FUNCTION: 0x0040d530
{
	if (mStealth>0.0f)
	{
		CMeshRenderer::SetRenderAlpha((SINT)(255.0f-(mStealth/100.0f*255.0f)));
		flags|=RF_CLOAKED;
	}

	// only render battle engine when we are not in first person view
#ifndef EDITORBUILD2
	if (mPlayer->GetIsInFPV() == FALSE || mVisible == TRUE || (mPlayer->GetNumber() - 1 != ENGINE.mCurrentViewpoint))
		SUPERTYPE::Render(flags) ;
#else
	SUPERTYPE::Render(flags) ;
#endif

	CMeshRenderer::SetRenderAlpha(255);
}

//******************************************************************************************
float CLockInfo::GetLockPercentage()	// FUNCTION: 0x0040d5b0
{
	float	value=(EVENT_MANAGER.GetTime()+GAME.GetFrameRenderFraction()*CLOCK_TICK-mStart)/(mFinish-mStart);

	if (value>1.0f)
		value=1.0f;

	return value;
}

//******************************************************************************************
void CLockInfo::Fired()
{
	mStart=EVENT_MANAGER.GetTime();
	mFinish=mStart+0.5f;
}

//******************************************************************************************

void CBattleEngine::PlayHudSample(char* name1)	// FUNCTION: 0x0040d5f0
{
	char name[256];
	sprintf(name, "hud\\%s", name1) ;

	CEffect	*e = SOUND.GetEffectByName(name);
	SOUND.PlayEffect(e, this, SOUND.GetHUDMessageVolume(), ST_FOLLOWDONTDIE, 0, 0, 0, -1.f, 0, 1.f, ST_GAME_SOUND, 1);	// the PC's last argument is 1 here (0040d621), 0 by default

/*	char n2[200];

	sprintf(n2, "%s_L", name);
	SOUND.PlayNamedSample(n2, this, HUD_SAMPLE_VOLUME); 

	sprintf(n2, "%s_R", name);
	SOUND.PlayNamedSample(n2, this, HUD_SAMPLE_VOLUME); */
}


//******************************************************************************************
// PlayHudSample(CEffect *) is inline, defined before HandleSounds (Stuart defines it in this file): retail has no copy.

//******************************************************************************************
CEulerAngles	CBattleEngine::GetInterpolatedEulerOrientation()	// FUNCTION: 0x0040d660
{
	CEulerAngles ea;

	ea.mYaw   = mOldEulerAngles.mYaw   + (AngleDifference(mCurrentOrientation.mYaw  , mOldEulerAngles.mYaw  ) * GAME.GetFrameRenderFraction());
	ea.mPitch = mOldEulerAngles.mPitch + (AngleDifference(mCurrentOrientation.mPitch, mOldEulerAngles.mPitch) * GAME.GetFrameRenderFraction());
	ea.mRoll  = mOldEulerAngles.mRoll  + (AngleDifference(mCurrentOrientation.mRoll , mOldEulerAngles.mRoll ) * GAME.GetFrameRenderFraction());

	return ea;
}

//******************************************************************************************
FVector	CBattleEngine::GetInterpolatedAutoAimPos()	// FUNCTION: 0x0040d7c0
{
	FVector cam_pos = mPlayer->GetCurrentViewPoint() ;
	FMatrix cam_ori = mPlayer->GetCurrentViewOrientation() ;

	FVector cam_old_pos = mPlayer->GetOldCurrentViewPoint() ;
	FMatrix cam_old_ori = mPlayer->GetOldCurrentViewOrientation() ;

	FVector		pos=(cam_pos-cam_old_pos)*GAME.GetFrameRenderFraction()+cam_old_pos;
	FMatrix		orientation=(cam_ori-cam_old_ori)*GAME.GetFrameRenderFraction()+cam_old_ori;

	float		yawOffset=(mAutoAimYawOffset-mOldAutoAimYawOffset)*GAME.GetFrameRenderFraction()+mOldAutoAimYawOffset;
	float		pitchOffset=(mAutoAimPitchOffset-mOldAutoAimPitchOffset)*GAME.GetFrameRenderFraction()+mOldAutoAimPitchOffset;

	return pos+orientation*FMatrix(yawOffset,pitchOffset,0.0f)*FVector(0,1,0);
}

//******************************************************************************************
FVector	CBattleEngine::GetInterpolatedTrackingPos()	// FUNCTION: 0x0040da30
{
	FVector		pos=(mPos-mOldPos)*GAME.GetFrameRenderFraction()+mOldPos;
	FMatrix		orientation=(mOrientation-mOldOrientation)*GAME.GetFrameRenderFraction()+mOldOrientation;

	float		yawOffset=(mTrackingYawOffset-mOldTrackingYawOffset)*GAME.GetFrameRenderFraction()+mOldTrackingYawOffset;
	float		pitchOffset=(mTrackingPitchOffset-mOldTrackingPitchOffset)*GAME.GetFrameRenderFraction()+mOldTrackingPitchOffset;

	return pos+orientation*FMatrix(yawOffset,pitchOffset,0.0f)*FVector(0,1,0);
}

//******************************************************************************************
void CBattleEngine::EnableWeapon(	// FUNCTION: 0x0040dc30
	char *inWeaponName)
{
	mWalkerPart->EnableWeapon(inWeaponName);
	mJetPart->EnableWeapon(inWeaponName);
}

//******************************************************************************************
void CBattleEngine::DisableWeapon(	// FUNCTION: 0x0040dc60
	char *inWeaponName)
{
	mWalkerPart->DisableWeapon(inWeaponName);
	mJetPart->DisableWeapon(inWeaponName);
}

//******************************************************************************************
SINT CBattleEngine::CountActiveWeapons()	// FUNCTION: 0x0040dc90
{
	if (mState==BATTLE_ENGINE_STATE_JET)
		return mJetPart->CountActiveWeapons();
	else
		return mWalkerPart->CountActiveWeapons();
}

//******************************************************************************************
void CBattleEngine::EnableFlightMode()	// FUNCTION: 0x0040dcb0
{
	mFlightModeActive=TRUE;
}

//******************************************************************************************
void CBattleEngine::DisableFlightMode()	// FUNCTION: 0x0040dcc0
{
	mFlightModeActive=FALSE;

	if (mState==BATTLE_ENGINE_STATE_JET)
		Morph();
}

//******************************************************************************************
void CBattleEngine::HostileEnvironment()	// FUNCTION: 0x0040dce0
{
	if (EVENT_MANAGER.GetTime()-mLastTimeInHostileEnviroment>5.0f)
	{
		PlayHudSample("hud_hostile_environment");
		LOG.AddMessage("playing sample :  hostile environment") ;
	}

	mLastTimeInHostileEnviroment=EVENT_MANAGER.GetTime();
}

//******************************************************************************************
void CBattleEngine::TestDanger()	// FUNCTION: 0x0040dda0
{
	if (mDangerStartTime<EVENT_MANAGER.GetTime()-8.0f)
	{
		BOOL		danger=FALSE;

		if (IsOnGround())
		{
			if (GAME.GetForsetiFearGrid()->InDanger(GetPos()))
				danger=TRUE;

			if (GAME.GetMuspellFearGrid()->InDanger(GetPos()))
				danger=TRUE;
		}

		if (danger)
			mDangerStartTime=EVENT_MANAGER.GetTime();
	}
}

//******************************************************************************************
void CBattleEngine::AugmentWeapon()	// FUNCTION: 0x0040de40
{
	CWeapon		*primaryWeapon=mWalkerPart->GetPrimaryWeapon();
	SINT		store=primaryWeapon->GetAmmoStore();

	if ((mStoreHeat[store]) || (mStoreValue[store]>0))
	{
		if (primaryWeapon==GetCurrentWeapon())
		{
			EZoomMode	oldZoomMode=GetCurrentWeapon()->GetZoomMode();

			mSlowMovement=FALSE;
			LoseWeaponCharge();

			if (oldZoomMode!=GetCurrentWeapon()->GetZoomMode())
				AutoZoomOut();
		}

		mAugActiveTime=EVENT_MANAGER.GetTime();
		mAugValue=MAX_AUG_VALUE;
		mAugActive=TRUE;
		PlayHudSample("hud_weapon_augmented");
		mAugmentedTime = EVENT_MANAGER.GetTime();
	}
}

//******************************************************************************************
void CBattleEngine::UnaugmentWeapon()
{
	if (mWalkerPart->GetAugWeapon()==GetCurrentWeapon())
	{
		EZoomMode	oldZoomMode=GetCurrentWeapon()->GetZoomMode();

		mSlowMovement=FALSE;
		LoseWeaponCharge();

		if (oldZoomMode!=GetCurrentWeapon()->GetZoomMode())
			AutoZoomOut();
	}

	mAugValue=0.0f;
	mAugActive=FALSE;
}

//******************************************************************************************
float CBattleEngine::GetRadius()	// FUNCTION: 0x0040df80
{
	if (GAME.IsMultiplayer())
		return 1.0f;
	else
		return 0.4f;
}

//******************************************************************************************
float CBattleEngine::COfGHeight()	// FUNCTION: 0x0040dfa0
{
	return BATTLE_ENGINE_COFGHEIGHT;
}

//******************************************************************************************
void CBattleEngine::Explode()	// FUNCTION: 0x0040dfb0
{
	CExplosion				*t=UPhysicsManager::SpawnExplosion(mConfiguration->mExplosion);
	CExplosionInitThing		i;

	i.mBehaviour=UPhysicsManager::GetExplosion(mConfiguration->mExplosion);
	i.mAttachedTo=this;
	i.mAllegiance=GetAllegiance();
	i.mUseAttachedRadius=TRUE;

	if (IsOnGround())
		i.mColType=kCollideGround;
	else if (IsInWater())
		i.mColType=kCollideWater;
	else
		i.mColType=kCollideNothing;

	// Initialise the explosion
	if (t)
	{
		i.mPos=mPos;
		t->Init(&i);
	}
}

//******************************************************************************************
BOOL CBattleEngine::CanBeLocked()	// FUNCTION: 0x0040e7d0
{
	if (mStealth>0.0f)
		return FALSE;

	if (mState==BATTLE_ENGINE_STATE_JET)
	{
		if (mJetPart->DoingLoop())
			return FALSE;

		if (mJetPart->DoingRoll())
			return FALSE;
	}

	if (mState==BATTLE_ENGINE_STATE_WALKER)
	{
		if (mWalkerPart->GetIsDoingSpecialWalkerMove())
			return FALSE;
	}

	return TRUE;
}

//******************************************************************************************
BOOL CBattleEngine::IsFiring()
{
	if (mState==BATTLE_ENGINE_STATE_JET)
		return mJetPart->IsFiring();

	return mWalkerPart->IsFiring();
}

//******************************************************************************************
void CBattleEngine::ToggleCockpit()	// FUNCTION: 0x0040e840
{
	if (mCockpit)
		mCockpit->mShouldRender=!mCockpit->mShouldRender;
}

//******************************************************************************************
void CBattleEngine::GetBeamPosition(	// FUNCTION: 0x0040e860
	FVector		&outPos,
	FMatrix		&outOrientation)
{
	FMatrix		temp;

	if (mCockpit)
		mCockpit->GetRenderThing()->GetRTEmitter("Gun",1,outPos,temp,FALSE,TRUE);

	outPos+=GetVelocity()/2;
	//outOrientation=mOrientation;
}

//******************************************************************************************
float CBattleEngine::GetThreat()	// FUNCTION: 0x0040e8e0
{
	float	mapHeight=MAP.Collide(mPos);

	if (mapHeight-2.0f<mPos.Z)
		return 5.0f;

	return 0.0f;
}

//******************************************************************************************
float CBattleEngine::GetImportance()	// FUNCTION: 0x0040e910
{
	if ((IsOnGround()) && (!IsOnObject()))
		return 5.0f;

	return 0.0f;
}

//******************************************************************************************
float CBattleEngine::GetInitialLife()	// FUNCTION: 0x00406040
{
	return mConfiguration->mLife;
}

// The retail file is longer than Stuart's here (its PC-only functions); its __LINE__ for the new() below is 3595
// (CBattleEngine::Damage 0040aa3e, where AddDamageFlash is inlined).
#line 3587
//******************************************************************************************
void CBattleEngine::AddDamageFlash(
	FVector		inPos)
{
	if (mDamageFlashes.Size()<15)
	{
		FVector d = inPos - GetOldPos();

		CDamageFlash	*flash=new(MEMTYPE_BATTLEENGINE) CDamageFlash;

		flash->mYaw = mCurrentOrientation.mYaw - (-atan2f(d.X, d.Y));
		flash->mStartTime = EVENT_MANAGER.GetTime();

		mDamageFlashes.Add(flash);
	}
}

//******************************************************************************************
void CBattleEngine::ProcessDamageFlashes()
{
	ListIterator<CDamageFlash>		iterator(&mDamageFlashes);
	CDamageFlash					*flash;

	for (flash=iterator.First(); flash; flash=iterator.Next())
	{
		if (flash->mStartTime+DAMAGE_FLASH_TIME<EVENT_MANAGER.GetTime())
		{
			mDamageFlashes.Remove(flash);
			delete flash;
			break;
		}
	}
}

//******************************************************************************************
void CBattleEngine::ActivateThrusters()	// FUNCTION: 0x0040e940
{
	if (GAME.IsMultiplayer())
	{
		FVector							offset;
		FMatrix							matrix;
		ListIterator<CValidatedFoR>		iterator(&mThrusters);
		SINT							n=1;

		for (CValidatedFoR *thruster=iterator.First(); thruster; thruster=iterator.Next())
		{
			if (!thruster->Full())
				PARTICLE_MANAGER.AddParticle(mThrusterEffect,thruster);

			GetEmitter(kThrusterEmitter,n,offset,matrix);

			thruster->SetPos(offset);
			thruster->SetOrientation(matrix);
			n++;
		}
	}

	if (!mThrustersActive)
		SOUND.PlayEffect(mInFlightSound, this, ENGINE_VOLUME, ST_FOLLOWDONTDIE, true, 0.0f, 0, -1.f, true);
	
	mThrustersActive=TRUE;
}

//******************************************************************************************
void CBattleEngine::DeactivateThrusters()	// FUNCTION: 0x0040eb50
{
	if (mThrustersActive)
	{
		if (GAME.IsMultiplayer())
		{
			ListIterator<CValidatedFoR>		iterator(&mThrusters);

			for (CValidatedFoR *thruster=iterator.First(); thruster; thruster=iterator.Next())
				thruster->Kill();
		}

		CSoundEvent *s = SOUND.GetSoundEventForThing(mInFlightSound->mSample, this);
		
		if ((s) && (mState==BATTLE_ENGINE_STATE_WALKER))
		{
#if TARGET == PS2
			SOUND.StopSoundEvent(s);
#else
			SOUND.FadeTo(s->mSample, 0, 0.02f, this);
#endif
		}

		mThrustersActive=FALSE;
	}
}

//******************************************************************************************
// STAND-IN (inline budget): the inactive path owns the traversal separately from the caller's iterator. This
// preserves both expanded position setters and their out-of-line SetCurrentPos tails; Stuart's loop in place is 169 bytes
// off. The name is not recovered.
static void StopEngineEffects(ListIterator<CValidatedFoR> &iterator)
{
	CValidatedFoR *frame;
	for (frame=iterator.First(); frame; frame=iterator.Next())
		frame->Kill();
}

void CBattleEngine::HandleEngines()	// FUNCTION: 0x0040ebf0
{
	if ((GAME.IsMultiplayer()) || (GAME.GetGameState() == GAME_STATE_PANNING))
	{
		FVector							offset;
		FMatrix							matrix;
		ListIterator<CValidatedFoR>		iterator(&mEngines);
		SINT							n=1;
		CValidatedFoR					*frame;

		for (frame=iterator.First(); frame; frame=iterator.Next())
		{
			if (mOldEngineState!=mEngineState)
				frame->Kill();

			switch (mEngineState)
			{
				case kAfterburnerEngines:
				{
					if (!frame->Full())
						PARTICLE_MANAGER.AddParticle(mAfterburnerEffect,frame);

					GetEmitter(kEngineEmitter,n,offset,matrix);

					frame->SetPos(offset);
					frame->SetOrientation(matrix);
				}
				break;

				case kNormalEngines:
				{
					if (!frame->Full())
						PARTICLE_MANAGER.AddParticle(mEngineEffect,frame);

					GetEmitter(kEngineEmitter,n,offset,matrix);

					frame->SetPos(offset);
					frame->SetOrientation(matrix);
				}
				break;
			}

			n++;
		}

		mOldEngineState=mEngineState;
	}
	else
	{
		ListIterator<CValidatedFoR>		iterator(&mEngines);

		StopEngineEffects(iterator);
	}
}

//******************************************************************************************
BOOL	CBattleEngine::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x0040eeb0
{
	EAnimMode mode = GetRenderAnimation() ;

	if (mode!=AM_INVALID)
	{
		if (mode==GetRenderThing()->GetRTMesh()->GetAnimModeByName("flytowalk"))
		{
			SetAnimMode("walk", TRUE, TRUE) ;
		}
		else if (mode==GetRenderThing()->GetRTMesh()->GetAnimModeByName("walktofly"))
		{
		
			SetAnimMode("fly", TRUE, TRUE) ;
		}
	}

	return FALSE ;
}

//******************************************************************************************
void	CBattleEngine::GroundParticleEffect()	// FUNCTION: 0x0040ef20
{
	// Kick off ground effect water
	float		altitude;
	float		waterLevel=MAP.GetWaterLevel();
	float		groundLevel=MAP.Collide(mPos);

	if (waterLevel<groundLevel)
		altitude=waterLevel-mPos.Z;
	else
		altitude=groundLevel-mPos.Z;

	if (altitude<10)
	{
		CValidatedFoR			vfr;

		FVector		particlePos=mPos;
		particlePos.Z+=altitude*1.5f;

		// Kick off a particle
		if (waterLevel<groundLevel)
			PARTICLE_MANAGER.AddParticle(sWaterEffect,&vfr);
		else
			PARTICLE_MANAGER.AddParticle(sLandEffect,&vfr);

		vfr.SetPos(particlePos);
	}
}

//******************************************************************************************
void	CBattleEngine::WeaponOverheated()	// FUNCTION: 0x0040f110
{
	if (mWeaponOverheatedTime+4.0f<EVENT_MANAGER.GetTime())
		mWeaponOverheatedTime=EVENT_MANAGER.GetTime();
}
// Inline functions from headers that BattleEngine.obj emits; its vtables and matched code place them.
// FUNCTION: 0x00405900 ?Unknown3@CCockpit@@UAEHXZ
// FUNCTION: 0x00405910 ?GetRenderMesh@CThing@@UAEHXZ
// FUNCTION: 0x00405920 ?RenderUseHierarchy@CThing@@UAEHH@Z
// FUNCTION: 0x00405930 ?GetRenderSelected@CThing@@UAEHXZ
// FUNCTION: 0x00405970 ??_GCDXCockpit@@UAEPAXI@Z
// FUNCTION: 0x00405990 ??1CDXCockpit@@UAE@XZ
// FUNCTION: 0x004059c0 ?GetType@CCylinder@@UAEHXZ
// FUNCTION: 0x004059d0 ??_GCGeneralVolume@@UAEPAXI@Z
// FUNCTION: 0x00405a20 ??_GCRadarWarningReceiver@@UAEPAXI@Z
// FUNCTION: 0x00405e80 ?GetSoundVelocity@CThing@@UAE?AVFVector@@XZ
// FUNCTION: 0x00405ed0 ?_GetClassName@CBattleEngine@@UAEPADXZ
// FUNCTION: 0x00405ee0 ?_GetClassID@CBattleEngine@@UAEHXZ
// FUNCTION: 0x00405ef0 ?GetMaxVelocity@CBattleEngine@@UAEMXZ
// FUNCTION: 0x00405f00 ?SetThingType@CBattleEngine@@UAEXK@Z
// FUNCTION: 0x00405f20 ?SetInfinateEnergy@CBattleEngine@@UAEXH@Z
// FUNCTION: 0x00405f40 ?GetSoundMaterial@CBattleEngine@@UAEHXZ
// FUNCTION: 0x00405f50 ?GetStealth@CBattleEngine@@UAEMXZ
// FUNCTION: 0x00405f60 ??_GCBattleEngine@@UAEPAXI@Z
// FUNCTION: 0x004062d0 ?MakeRotationF@FMatrix@@QAEXMMM@Z
// FUNCTION: 0x0040d120 ??GFVector@@QBE?AV0@ABV0@@Z
// FUNCTION: 0x0040d150 ??DFVector@@QBE?AV0@M@Z
// FUNCTION: 0x0040d320 ??DFMatrix@@QBE?AV0@ABV0@@Z
// FUNCTION: 0x00409780 ?Next@GenericListIterator@@QAEPAXXZ
// FUNCTION: 0x004097a0 ?SetPos@CFoR@@QAEXABVFVector@@@Z
// FUNCTION: 0x004098e0 ??0CLine@@QAE@ABV0@@Z
// FUNCTION: 0x0040b100 ??0CGeneralVolume@@QAE@XZ
// FUNCTION: 0x0040d470 ??0CLine@@QAE@ABVFVector@@0@Z
// FUNCTION: 0x0040d1a0 ?Elevation@FVector@@QBEMXZ
// FUNCTION: 0x0040d1f0 ??0FMatrix@@QAE@MMM@Z
// FUNCTION: 0x00405950 ?GetRenderAnimation@CCockpit@@UAE?AW4EAnimMode@@XZ
// FUNCTION: 0x00405960 ?GetRealAnimIndex@CCockpit@@UAEHXZ
// FUNCTION: 0x00405d80 ??1CParticleEffectLink@@QAE@XZ
// FUNCTION: 0x00405a00 ?Unknown13@CMCMech@@UAEHXZ
// FUNCTION: 0x00405a10 ??1CMCBattleEngine@@UAE@XZ
// CUnit's inline virtuals (unit.h): CBattleEngine's vtable is the first to reference them.
// FUNCTION: 0x00405d90 ?GetRenderColour@CUnit@@UAEKXZ
// FUNCTION: 0x00405dc0 ?Unknown72@CUnit@@UAEMXZ
// FUNCTION: 0x00405dd0 ?Unknown73@CUnit@@UAEMXZ
// FUNCTION: 0x00405de0 ?Unknown76@CUnit@@UAEHXZ
// FUNCTION: 0x00405e10 ?Unknown82@CUnit@@UAEXXZ
// FUNCTION: 0x00405e20 ?Unknown83@CUnit@@UAEXXZ
// FUNCTION: 0x00405e30 ?SetVulnerable@CUnit@@UAEXH@Z
// FUNCTION: 0x00405e40 ?GetVulnerable@CUnit@@UAEHXZ
// FUNCTION: 0x00405e50 ?GetAIState@CUnit@@UAE?AW4EAIState@@XZ
// FUNCTION: 0x00405e60 ?Unknown94@CUnit@@UAEMXZ
// FUNCTION: 0x00405e70 ?Unknown109@CUnit@@UAEHXZ
// FUNCTION: 0x00405ea0 ?Unknown112@CUnit@@UAEMXZ
// Near miss: retail copies through eax/edx/ecx where this build saves esi (*out = mPos through an explicit pointer
// gives retail's shape, 00428d30, but the slot keeps its FVector return).
// FUNCTION: 0x00405eb0 ?Unknown113@CUnit@@UAEXPAVFVector@@@Z
// FUNCTION: 0x00409880 ?GetMaxCharge@CWeapon@@QAEHXZ
// FUNCTION: 0x0040b660 ?AngleDifference@@YAMMM@Z

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00479b40 ??_GCMCBattleEngine@@UAEPAXI@Z

// FUNCTION: 0x00404ce0 init:ZERO_FVECTOR
// FUNCTION: 0x00404d00 init:ID_FMATRIX

//******************************************************************************************
// STAND-IN (link layout): retail's 8 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_BattleEngine[8] = { 0 };
