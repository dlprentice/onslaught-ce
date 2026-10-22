// BattleEngine.h: Stuart's, verbatim (CRLF converted) except where marked PC. His member order is retail's:
// mState +0x260 (0040c3a0), mAugValue/mAugActive +0x2f8/+0x2fc (CGame::DrawGameStuff 0047170e), mConfiguration
// +0x4b0 and mConfigurationId +0x600 (UpdateConfiguration 0040c650), mWalkerPart/mJetPart +0x578/+0x57c (0040c3a9).
//
// BattleEngine.h: interface for the CBattleEgine class.
//
//////////////////////////////////////////////////////////////////////

#ifndef BATTLE_ENGINE_INCLUDE
#define BATTLE_ENGINE_INCLUDE

#include "Unit.h"
#include "PtrSet.h"
#include "BattleEngineDataManager.h"
#include "Cockpit.h"
#include "weapon.h"
#include "World.h"
#include "Array.h"				// not in Stuart's list: his Unit.h must have brought CSArray in
#include "ParticleManager.h"		// likewise for CValidatedFoR
#include "Script.h"				// likewise for IScript (BattleEngine.cpp deletes mMissionScript)
#include "music.h"				// for PlayHudSample(CEffect *), inline on the PC

class CWeapon;
class CEquipment;
class CBattleEngineData;
class CPlayer;

enum EBattleEngineEvent
{
	BECOME_JET = 6000,
	BECOME_WALKER,
	CALC_UNIT_OVER_CROSSHAIR,
	HANDLE_AUTO_AIM,
};

enum EBattleEngineState
{
	BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER,
	BATTLE_ENGINE_STATE_MORPHING_INTO_JET,
	BATTLE_ENGINE_STATE_WALKER,
	BATTLE_ENGINE_STATE_JET
};

enum EEngineState
{
	kAfterburnerEngines=0,
	kNormalEngines,
	kEnginesOff,
};

#define		BATTLE_ENGINE_TRANSFORM_TIME			(0.5f)
#define		BATTLE_ENGINE_SLOW_MOVEMENT_FACTOR		4.0f

class	CRadarWarningReceiver;

class CLockInfo
{
	public:
		CActiveReader<CUnit>		mUnit;
		float						mStart,mFinish;
		BOOL						mLocked,mDirectLock;

									CLockInfo()
									{
										mLocked=FALSE;
									}

		float						GetLockPercentage();
		void						Fired();
};

#define DAMAGE_FLASH_TIME	2.0f

class CDamageFlash
{
	public:
		float				mYaw,mStartTime;
};

DECLARE_THING_CLASS(CBattleEngine, CUnit)  
public:
	friend class CBattleEngineWalkerPart ;
	friend class CBattleEngineJetPart ;

	virtual void	Init(CInitThing* init);
	virtual void	Shutdown();

	virtual ~CBattleEngine();

	void	ZoomIn();
	void	ZoomOut();
	void    AutoZoomOut();

	void	Morph() ;

	// PC: a walker on the ground or on an object: Stuart's pitch-limit test from UpdateRotation, out of line in
	// retail (CGame::Update stores it in gMouseAxisLocked, 0046eb8d; name open)
	BOOL	IsWalkerOnGroundOrObject();			// 004080f0
	// PC: the mouse's look input for the walker (CGame::Render 0046e650; name open)
	void	UpdateMouseLookAngles();			// 00407540

//	FVector& GetPos();
//	FVector  GetOldPos();

	void	ChargeWeapon();
	void	FireWeapon();
	void    ChangeWeapon();
	void	LoseWeaponCharge();

	float   GetZoom() { return mZoom ; }
	float   GetOldZoom() { return mOldZoom ; }	
	

	void	FireCurrentWeapon() ;
	virtual void Move() ;
	virtual void	ConfirmedKill(CUnit		*inKilled);


	virtual void			Hit(CThing* other_thing, CCollisionReport* report) ;
	virtual float			Gravity() ;
	void	UpdateRotation() ;
	void	AttachEquipment(CEquipment* eqi) { mEquipment.Add(eqi) ;}
	float	ZoomModifier(float z);
	CUnit*	GetUnitOverCrossHair()	{ return 	mCurrentUnitOverCrosshair.ToRead(); }
	CUnit*	GetUnitOverCrossHairRegardlessOfRange()	{ return 	mCurrentUnitOverCrosshairRegardlessOfRange.ToRead(); }
	CThing*	GetThingOverCrossHair();
	CUnit*	CalcUnitOverCrossHair(CEvent *inEvent,BOOL inMeshCollision,BOOL inUpdateData);

	float					CalculatePitch();

	CThing*		GetAutoAimTarget()				{ return mAutoAimTarget.ToRead(); }
	FVector		GetInterpolatedAutoAimPos();

	BOOL		TrackingActive()				{ return mTrackingActive; }
	FVector		GetInterpolatedTrackingPos();

	virtual	BOOL			GetRequiresPolyBucket();

	virtual	float			GetMaxVelocity()		{ return 35.0f; }

	virtual	void			SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_BATTLE_ENGINE
																			  | THING_TYPE_GROUND_UNIT					
																			  | THING_TYPE_AIR_UNIT
																			  | THING_TYPE_MECH
																			  | THING_TYPE_CAN_DESTROY_TREES
																			  | THING_TYPE_VEHICLE); }
	virtual void			Damage(float amount,CThing *inByThis,BOOL inDamageShields=TRUE, int mesh_part_no = -1);
			void			Rearm(float inAmount);

	void					HandleLocks();

	virtual void			Activate();
	virtual void			Deactivate();
	virtual void			DeclareInWater();
			void			HandleIncomingMissle();
	void					StartLock(
								CUnit			*inUnit,
								float			inLockTime,
								BOOL			inDirectLock=FALSE);
	CUnit*					GetClosestLockableUnit(
								CWeapon			*inWeapon,
								FVector			inPos,
								float			inDistance);
	int						CountLocks();
	void					FireLock(
								CThing			*inUnit);
	void					LockHit(
								CThing			*inUnit);
	BOOL					Locked(
								CThing			*inUnit);
	BOOL					DisplayLock(
								CWeapon			*inWeapon);
	BOOL					LocksFinished();

	SPtrSet<CLockInfo>&		GetLockList()			{ return mLocks; }
	SPtrSet<CLockInfo>&		GetFiredLockList()		{ return mFiredLocks; }
	
	virtual float			GetRadius();
	void					HandleEvent(CEvent* event) ;
	BOOL					FinishedPlayingCurrentAnimation();



	EBattleEngineState	GetState() { return mState ; }
	const float&	GetYawVel() { return mYawvel ; }
	const float&    GetRollVel() { return mRollvel ; }
	const float&    GetPitchVel() { return mPitchvel ; }

	const float&    GetLastDamageTime() { return mLastDamageTime ; } 
	void			AddShockShake(float amount) ;
	virtual BOOL	StartDieProcess();
	BOOL			IsWalking() ;
	void			PlayHudSample(char* name);
	void			PlayHudSample(CEffect *effect);	// inline, defined in BattleEngine.cpp before HandleSounds (as Stuart defines it there)
	BOOL			GetIsPoweredUp() { return mPoweredUp ; }

	CCOCKPIT*		GetCockpit() { return mCockpit ; }
	class CBattleEngineWalkerPart* GetWalkerPart() { return mWalkerPart ; }
	class CBattleEngineJetPart* GetJetPart() { return mJetPart ; }

	virtual	float			BounceFactor()			{ return 0.01f;}		// :-)
	virtual	float			COfGHeight();
	//virtual	void			GetCentrePos(FVector &outPos);   // implemented cos battle engine has no mesh

			float			GetMaxEnergy();
			float			GetMaxLife();
			float			GetTransformStartTime() {return mTransformStartTime;};

			CRadarWarningReceiver	*GetRadarWarningReceiver() {return mCachedRadarWarningReceiver;};

	virtual CUnit*			GetCurrentTarget();
			void			ResetTarget()			{ mCurrentTarget=0; }

			BOOL			WeaponFired(
								CWeapon		*inWeapon);
			void			RecoilWeapon(
								CWeapon		*inWeapon);

	virtual CWeapon*		GetCurrentWeapon();
			float			GetWeaponAmmoPercentage();
			SINT			GetWeaponAmmoCount();
			BOOL			IsEnergyWeapon();
			BOOL			IsWeaponOverheated();
			float			GetWeaponOverheatedTime()		{ return mWeaponOverheatedTime; }
			float			GetWeaponCharge();
			float			GetWeaponReadiness();
			WCHAR*			GetWeaponName();
			char*			GetWeaponPhysicsName();
			char*			GetWeaponIconName();
			SINT			WhereIsCurrentWeaponAttached();

	void			SetConfiguration(
						SINT	inValue)
					{
						mConfigurationId=inValue;
						UpdateConfiguration();
					}
	void			UpdateConfiguration();

	CBattleEngineData		*GetConfiguration()			{ return mConfiguration; }
	char*					GetConfigurationName()		{ return mConfiguration->mConfigurationName; }

	void			SetPlayer(class CPlayer *player) { mPlayer.SetReader(player); };

			BOOL			FullHealth();
			BOOL			NeedsRearm();

	virtual	void			SetInfinateEnergy(BOOL val)				{SUPERTYPE::SetInfinateEnergy(val); mEnergy=GetMaxEnergy(); }

	virtual void			DeclareOnGround();

	virtual SINT			GetSoundMaterial()						{ return SM_BATTLE_ENGINE; }

	virtual float			GetThreat();
	virtual float			GetImportance();

	virtual void			GetLaunchPosition(
								CWeapon		*inWeapon,
								int			inIndex,
								FVector		&outPos,
								FMatrix		&outOrientation,
								BOOL		inNeedOrientation=FALSE);

	virtual BOOL			IsAThreat()								{ return TRUE; }
	
			CPlayer			*GetPlayer()  { return(mPlayer.ToRead()); };

#ifdef RESBUILDER
	virtual	void			AccumulateResources( CResourceAccumulator * accumulator,DWORD flags=0 );
#endif
			float			GetChangedWeaponTime() { return mChangedWeaponTime; }
			float			GetAltitudeAboveGround() ;

			float			GetLowEnergyStartTime() { return mLowEnergyStartTime; }
			float			GetLowArmourStartTime() { return mLowArmourStartTime; }
			float			GetDangerStartTime()	{ return mDangerStartTime; }
	virtual FVector			GetLocalLastFrameMovement() ;

			BOOL			IsStalling()			{ return mStalling; }
			float			GetStallTime()			{ return mStallTime; }
			float			GetAugmentedTime()		{ return mAugmentedTime ; }

	virtual	void			Render(DWORD flags=0) ;
	virtual	FVector			GetRenderPos();
	virtual	FMatrix			GetRenderOrientation();
	virtual void			DeclareOnObject(CThing* on_object) ;

	virtual float			GetStealth()			{ return mStealth; }

			bool			GetCanBeImpostered() { return(false); };
			BOOL			IsStandingOnAMoveingObject() ;

			float			GetAugActiveTime()		{ return mAugActiveTime; }
			float			GetAugValue()			{ return mAugValue; }
			BOOL			IsAugActive()			{ return mAugActive; }

			void			HandleCloak();
			void			Cloak();
			void			Decloak();

			void			EnableWeapon(char *inWeaponName);
			void			DisableWeapon(char *inWeaponName);
			SINT			CountActiveWeapons();

			void			EnableFlightMode();
			void			DisableFlightMode();

	CEulerAngles	GetInterpolatedEulerOrientation();
			
			void			HostileEnvironment();

			void			TestDanger();
	
			void			AugmentWeapon();
			void			UnaugmentWeapon();

			void			Explode();

	virtual BOOL			CanBeLocked();

			void			ConfigurationUp();
			void			ConfigurationDown();

			float			GetAmmoDepletedTime()		{ return mAmmoDepletedTime; }

			BOOL			IsFiring();

			void			PlayIncommingMissileSound()	{ mPlayIncomingMissileSound=TRUE; }

			void			ToggleCockpit();

			void			GetBeamPosition(
								FVector		&outPos,
								FMatrix		&outOrientation);

			float			GetInitialLife();

			SPtrSet<CDamageFlash>*	GetDamageFlashList()	{ return &mDamageFlashes; }

			void			ActivateThrusters();
			void			DeactivateThrusters();
			void			HandleEngines();

			void			GroundParticleEffect();
private:
			BOOL			FiredAt(
								CThing		*inUnit);

			void			HandleAutoAim(
								CEvent		*inEvent);
			void			UpdateAutoAim();

			void			SetCollisionShape();
			void			MassiveHackPutUsInRightMesh();

			void			HandleSounds();

			void			AddDamageFlash(
								FVector		inPos);
			void			ProcessDamageFlashes();

			void			WeaponOverheated();

	SPtrSet<CDamageFlash>	mDamageFlashes;

	EBattleEngineState mState ;

	CActiveReader<CThing> mStandingOnThing;
	FVector	mStandingOnObjectMovement;	// last movement we we're forced to do cos we were standing on a moving object

	float mYawvel ;
	float mRollvel;
	float mPitchvel ;
	SPtrSet<CEquipment>		mEquipment ;

	SPtrSet<CLockInfo>		mLocks;
	SPtrSet<CLockInfo>		mFiredLocks;
	UWORD					mRecentLocks;

	CRadarWarningReceiver	*mCachedRadarWarningReceiver;

	INT				   mUpdatedOriCount ;
	BOOL		       mUpdatedOri;
	int				   mAimCount ;
	float			   mZoom,mDesiredZoom,mOldZoom;
	float			   mLastDamageTime;
	float			   mOldLife;
	float			   mOldEnergy;
	float			   mLowArmourStartTime;
	float			   mLowEnergyStartTime;
	float				mDangerStartTime;
	float				mZoomOutTime;
	float				mTakeOffHeight,mTakeOffTime;

	float				mAugValue;
	BOOL				mAugActive;
	float				mAugActiveTime;

	BOOL				mStalling;
	float				mStallTime;
	float				mAugmentedTime;
	int					mNotMovingCount;
	BOOL				mInSafeCollisionPlace;
	CSArray<FVector,20>	mSafePos ;
	CSArray<float, 20>  mSafePosTime ;
	int					mCurrentSafeTry;

	BOOL				mCloaked;

	CBattleEngineData	*mConfiguration;

	float			   mLastTimeHitObjectAndDamaged ;


	float   mYawShake;
	float   mPitchShake;
	float   mRollShake;
	float   mShakeR ;

	CActiveReader<CUnit>	mCurrentUnitOverCrosshair;
	CActiveReader<CUnit>	mCurrentUnitOverCrosshairRegardlessOfRange;
	CWorldLineColReport		mWlcr;

	CActiveReader<CThing>	mAutoAimTarget;
	float					mDesiredAutoAimYawOffset,mAutoAimYawOffset,mOldAutoAimYawOffset;
	float					mDesiredAutoAimPitchOffset,mAutoAimPitchOffset,mOldAutoAimPitchOffset;

	BOOL					mTrackingActive;
	float					mTrackingYawOffset,mTrackingPitchOffset;
	float					mOldTrackingYawOffset,mOldTrackingPitchOffset;

	float					mLastTimeInHostileEnviroment;

	float					mPlayedIncomingWarheadSound,mPlayedAmmunitionDepletedSound;
	float					mPlayedWeaponOverheatedSound;
	float					mTransformStartTime;

	BOOL					mVisible;

	CCOCKPIT				*mCockpit;

	float					mStoreValue[kBattleEngineStores];
	BOOL					mStoreOverheat[kBattleEngineStores];
	BOOL					mStoreHeat[kBattleEngineStores];
	
	CActiveReader<CPlayer>  mPlayer;

	class CBattleEngineWalkerPart* mWalkerPart ;
	class CBattleEngineJetPart* mJetPart ;
	BOOL					mPoweredUp ;
	float					mChangedWeaponTime;
	BOOL					mSlowMovement;
	BOOL					mFlightModeActive;

	CEulerAngles			mOldEulerAngles;

	CEffect					*mInFlightSound,*mLandingSound,*mTakeOffSound;
	CEffect					*mHealthLowSound,*mEnergyLowSound,*mEnergyVeryLowSound;
	CEffect					*mStrafeSound;
	CEffect					*mIncomingMissileSound;
	CEffect					*mTargetLockedSound,*mAutoAimSound;
	CEffect					*mBattleEngineOnSound;//,*mBattleEngineFeetSound;
	CEffect					*mPneumaticSound;

	float					mWalkSoundTime;
	SINT					mWalkSounds;

	BOOL					mHitDamageDone;

	float					mStealth,mDesiredStealth;

	SINT					mCurrentTarget;

	BOOL					mPlayIncomingMissileSound;
	CActiveReader<CThing>	mHadAutoAimTarget;

	// yes battle engine has two meshes
	CRenderThing*			mOtherMeshHackRenderThing;
	BOOL					mHackRenderThingIsWalker ;
	CMotionController*	    mHackBackupMC;

	CValidatedFoR			mSmoke;

	SINT					mConfigurationId;

	float					mVibration;

	float					mAmmoDepletedTime,mWeaponOverheatedTime;
	float					mLastIncomingMissileVoice;

	CParticleDescriptor		*mThrusterEffect,*mEngineEffect,*mAfterburnerEffect;
	SPtrSet<CValidatedFoR>	mEngines;

	EEngineState			mEngineState,mOldEngineState;

	BOOL					mThrustersOn;

	static class CParticleDescriptor		*sWaterEffect,*sLandEffect;
};

#endif 