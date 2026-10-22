// BattleEngineJetPart.h: interface for the CBattleEngineJetPart class.
//
//////////////////////////////////////////////////////////////////////

#ifndef BATTLE_ENGINE_JET_PART_INCLUDE
#define BATTLE_ENGINE_JET_PART_INCLUDE

#include	"SPtrSet.h"
//#include	"ParticleManager.h"

class CBattleEngine ;
class CWeapon;

enum EJetFlightModel
{
	SIMPLE_JET_FLIGHTMODEL,
	ADVANCED_JET_FLIGHTMODEL
};

class CBattleEngine ;
class CResourceAccumulator;

class CBattleEngineJetPart 
{
public:
			CBattleEngineJetPart(CBattleEngine* main_part);
			~CBattleEngineJetPart();

	void	Turn(float vx) ;
	void    Pitch(float vy) ;
	void    YawLeft(float vx) ;
	void    YawRight(float vx) ;

	void    Thrust(float vy) ;

	void	Move() ;
	EJetFlightModel	GetFlightModel() { return mFlightModel ; }
	void			SetFlightModel(EJetFlightModel nm) { mFlightModel = nm ; }

	void		FireWeapon();
	void		ChargeWeapon();
	void		ChangeWeapon();
	void		LoseWeaponCharge();

	CWeapon*	GetCurrentWeapon();
	CWeapon*	GetWeapon(
					SINT	inNumber);
	SINT		CountWeapons();

	float			GetWeaponAmmoPercentage();
	SINT			GetWeaponAmmoCount();
	BOOL			IsEnergyWeapon();
	float		GetWeaponCharge();
	float		GetWeaponReadiness();
	WCHAR*		GetWeaponName();
	char*		GetWeaponPhysicsName();
	char*		GetWeaponIconName();
	SINT		WhereIsCurrentWeaponAttached();

	BOOL		WeaponFired(CWeapon *inWeapon);

	float		Gravity();

	BOOL		CanWeaponFire();

	void		ResetConfiguration();
	BOOL		GetIsDoingSpecialAirMove() ;

	void		AccumulateResources( CResourceAccumulator * accumulator );

	void		EnableWeapon(char *inWeaponName);
	void		DisableWeapon(char *inWeaponName);
	SINT		CountActiveWeapons();

	BOOL		AutoLevel();

	BOOL		DoingLoop()				{ return mDoingLoop; }
	BOOL		DoingRoll()				{ return (mDoingBarrelCount>0) ? true : false; }

	float		GetThrusterValue()	{return mThrusterValue;};

	BOOL		IsFiring();

	BOOL		IsWeaponOverheated();

private:
	void					HandleSkimming();
	void					HandleGroundEffect();
	float					GetFriction();

	SPtrSet<CWeapon>		mWeapons;
	int						mCurrentWeapon;
	float					mOnGround;

	CBattleEngine			*mMainPart;
	EJetFlightModel			mFlightModel;
	float					mThrusterValue;
	float					mLastMoveYVal;
	float					mLastMoveXVal;
	BOOL					mDoingLoop,mLoopHalfway,mLoopBroken;
	float					mLastStartHardBackwardTime;
	float					mLastStartHardLeftTime;
	float					mLastStartHardRightTime;
	float					mLastStartHardForwardTime;
	float					mDoingBarrelCount;
	BOOL					mDoingBarrelLeft;
	float					mStrafingStartTime;
};

#endif 


