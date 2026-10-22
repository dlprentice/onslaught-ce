// BattleEngineWalkerPart.h: interface for the CBattleEngineWalkerPart class.
//
//////////////////////////////////////////////////////////////////////

#ifndef BATTLE_ENGINE_WALKER_PART_INCLUDE
#define BATTLE_ENGINE_WALKER_PART_INCLUDE

class CBattleEngine;
class CWeapon;
class CResourceAccumulator;

#include "SPtrSet.h"
#include "BattleEngineDataManager.h"
#include "BattleEngine.h"

class CBattleEngineWalkerPart 
{
	public:
					CBattleEngineWalkerPart(CBattleEngine* main_part);
					~CBattleEngineWalkerPart();

		BOOL		ShouldSlide();
		void		Slide();

		void		Forward(float vy) ;
		void		Backward(float vy) ;
		void		Rotate(float vx) ;
		void		Pitch(float vy) ;
		void		Aim(float vx, float vy) ;
		void		ActivateLandingJets() ;
		float		GetCurrentAccleration() ;
		void		StrafeLeft(float vx) ;
		void		StrafeRight(float vx) ;
		void		UpdateWalkCycle() ;
		void		Move() ;

		float		GetCurrentWalkCycle()  { return mWalkCycle ; }
		float		GetOldWalkCycle() { return mOldWalkCycle ; }

		void		FireWeapon();
		void		ChargeWeapon();
		void		ChangeWeapon();
		void		LoseWeaponCharge();

		CWeapon*	GetCurrentWeapon();
		CWeapon*	GetWeapon(
						SINT	inNumber);
		CWeapon*	GetPrimaryWeapon()			{ return mPrimaryWeapon; }
		CWeapon*	GetAugWeapon()				{ return mAugWeapon; }
		SINT		CountWeapons();

		float		GetWeaponAmmoPercentage();
		SINT		GetWeaponAmmoCount();
		BOOL		IsEnergyWeapon();
		float		GetWeaponCharge();
		float		GetWeaponReadiness();
		WCHAR*		GetWeaponName();
		char*		GetWeaponPhysicsName();
		char*		GetWeaponIconName();
		SINT		WhereIsCurrentWeaponAttached();

		BOOL		WeaponFired(
						CWeapon		*inWeapon);

		BOOL		CanWeaponFire();

		void		Rearm(
						float		inAmount);
		BOOL		NeedsRearm();

		void		ResetConfiguration();

		void		AccumulateResources( CResourceAccumulator * accumulator );
		int			GetDashCount() { return mDoingDashCount ; } 
		BOOL		GetIsDoingSpecialWalkerMove() ;

		void		EnableWeapon(char *inWeaponName);
		void		DisableWeapon(char *inWeaponName);
		SINT		CountActiveWeapons();

		BOOL		IsFiring();

		BOOL		IsWeaponOverheated();

	private:
		BOOL					GoingIntoWater();

		SPtrSet<CWeapon>		mWeapons;
		int						mCurrentWeapon;
		BOOL					mShieldsRecharging;
		CWeapon					*mPrimaryWeapon,*mAugWeapon;

		CBattleEngine			*mMainPart;
		
		// Represent how far through the walk cycle we are.
		// Values are in range of -PI to +PI
		float					mWalkCycle;
		float					mOldWalkCycle;


		// needed for special moves
		float					mLastStartHardRightTime ;
		float					mLastStartHardLeftTime ;

		float					mLastStartHardForwardTime ;
		float					mLastStartHardBackwardTime ;

		float					mLastMoveXVal ;
		float					mLastMoveYVal ;
		int						mDoingDashCount ;

		BOOL					mThrusters;

		static float			mDashTime;
		static float			mDashStart;
		static float			mDashEnd;
		static int			mDashLength ;
		static int			mDashFriction ;
		static float			mDashVelocity ;

};

#endif 