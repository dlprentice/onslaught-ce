// weapon.h: CWeapon, a weapon a unit carries (0xb0 bytes, vtable 005dfc94; UPhysicsManager::SpawnWeapon constructs
// one, 0050f6d0). Stuart's weapon.h is not in the release; members are named from his calls in BattleEngine.cpp and
// the two parts where retail agrees. Weapon.obj is 00505e00..0050a290.

#ifndef WEAPON_H
#define WEAPON_H

#include "Equipment.h"
#include "WorldPhysicsManager.h"

class CThing;

// Stuart's name (BattleEngineJetPart.cpp compares the old and new weapons' modes).
enum EZoomMode
{
	kUnknownZoom0,
	kNormalZoom,					// CBattleEngine::Move's switch (00408a1a): 1
	kChargeZoom						// 2: the charge's zoom
};

// Stuart's lock modes (CBattleEngine::HandleLocks switches over 0..2, 0040682b).
enum ELockMode
{
	kDirectLockMode,
	kProximityLockMode,
	kSequenceLockMode
};

// Stuart's. A static int, not a constant: every object that uses it has its own copy in .data (the jet part's 1 at
// 006236a4, the walker part's at 006236a8, just before its dash statics) and reads it from memory (fisub,
// CBattleEngineJetPart::WeaponFired 0041214a).
static int kWeaponCoolRate = 1;

class CWeapon : public CEquipment
{
public:
	CWeapon(CWeaponData *data, SINT type);								// 00505e00
	virtual void	HandleEvent(CEvent *event);							// 0 (00506930)
	// 1: the implicit destructor (00505f90; scalar deleting 00505f70): retail does not store the vtable

	void			Unknown509140(SINT a, SINT b);						// 00509140
	float			GetMinRange(FVector target);						// 005096a0
	float			GetMaxRange(FVector target);						// 005099a0
	BOOL			ReadyToFire();										// 00509f70 (Stuart's name, BattleEngine.cpp)
	BOOL			Fire();												// 00506010
	BOOL			Unknown50a0b0(CThing *target);						// 0050a0b0
	BOOL			Unknown50a0d0(ULONG thing_type);					// 0050a0d0
	BOOL			Unknown5088b0(CUnit *target);						// 005088b0: thiscall, ret 4, explicit 0/1 paths (004fb629 consumes EAX)
	BOOL			Unknown507ab0(CUnit *target, BOOL a);				// 00507ab0: thiscall, ret 8 (CUnit::CanFireAt 004fb578)
	float			CalculateDesiredPitch(FVector target);				// 005094b0 (Stuart's name, BattleEngine.cpp)
	char			*GetName() { return mData->mName; }

	// Stuart's accessors, reconstructed from their inline copies (CBattleEngineJetPart::ChargeWeapon 00411bf0).
	BOOL			IsActive()			{ return mActive; }
	BOOL			ReadyToCharge();									// 0050a080
	BOOL			CanCharge()
	{
		for (int i = 1; i < 5; i++)
			if (mData->mChargeModes[i] != -1)
				return TRUE;
		return FALSE;
	}
	int				GetMaxCharge()
	{
		int	max = 0;
		for (int i = 0; i < 5; i++)
			if (mData->mChargeModes[i] != -1)
				max = i * 100;
		return max;
	}
	BOOL			FullyCharged()
	{
		int	max = GetMaxCharge();
		return !CanCharge() || (mCharge >= max);
	}
	void			Charge();											// 005068f0
	void			MoveEmitter(BOOL a);								// 005078f0 (CBattleEngineJetPart::Move 00410c81)
	EZoomMode		GetZoomMode()		{ return (EZoomMode)mData->mZoomMode; }
	void			LoseCharge()		{ mCharge = 0; }
	BOOL			Charged()			{ return mUnknown68 > 0; }		// 00412091
	float			GetCharge()
	{
		if (GetMaxCharge())
			return mCharge / GetMaxCharge();
		return 0.0f;
	}
	float			GetReadiness();										// no retail copy reached yet
	DWORD			GetLanguageName()	{ return mData->mLanguageName; }	// a text id (TEXT_DB.GetString, 00412463)
	char			*GetIconName()		{ return mData->mIconName; }
	SINT			GetPlacement()		{ return mData->mPlacement; }
	void			SetActive(BOOL active)	{ mActive = active; }
	BOOL			IsFiring();											// 0050a290 (Stuart's name: his BattleEngineJetPart.cpp:1070 calls weapon->IsFiring())

	// CBattleEngine::HandleLocks (00406560) places these: the lock mode's switch (00406826), then each case.
	int				GetMaxLocks();										// 00506350
	float			GetLockTime();										// 00506440
	ELockMode		GetLockMode();										// 00506530
	float			GetMaxDeflection();									// 00506620
	float			GetLockRange();										// 00506710
	float			GetLockRadius();									// 00506800
	BOOL			CanLock(CUnit *target);							// 005061f0
	float			GetActualMaxRange(FVector pos);						// 00509c80

	// Stuart's; inline (CBattleEngine::UpdateAutoAim 0040b196..0040b1f3): the mode's predictive flag, and the round's
	// speed (a beam's is 1000).
	BOOL			ValidRound()		{ if ((mMode) && (mMode->mRound)) return TRUE; return FALSE; }
	BOOL			CanPredict()		{ if ((mMode) && (mMode->mPredictive)) return TRUE; return FALSE; }
	float			GetLaunchVelocity()
	{
		if (ValidRound())
		{
			if (mMode->mRound->mBeam)
				return 1000.0f;
			return mMode->mRound->mSpeed;
		}
		return 0.0f;
	}
	// Stuart's; retail reads weapon +0xa4 then data +0x28 (0040cf79) / +0x30 (0040b80d).
	BOOL			AdjustAim() { return mData->mAdjustAim; }
	BOOL			IsSmart() { return mData->mSmart; }
	// The round's ballistic test, with no null checks: CBattleEngine::GetLaunchPosition expands it and calls the
	// round's predicate out of line (0040cac1: mMode, then mRound, then call 0040d0f0).
	BOOL			Gravity() { CWeaponModeData *mode = mMode; return mode->mRound->Unknown40d0f0(); }
	int				GetAmmoStore()		{ return mData->mAmmoStore; }
	float			GetConsumption()	{ return mData->mConsumption; }
	BOOL			AllowMovement()		{ return mData->mAllowMovement; }
	float			GetPower()			{ return mMode->mPower; }		// Stuart's name; inline in CBattleEngine::RecoilWeapon (0040c349)
	float			GetMinTargetHeight() { return mMode->mUnknown6C; }
	float			GetMaxTargetHeight() { return mMode->mUnknown70; }

	// retail (weapon.obj)
	static CWeaponModeData	*GetWeaponMode(SINT index);					// 00509e40: the registry's mode at that position
	CWeaponModeData	*GetCurrentMode();									// 00509e90: the mode of the highest charge level that has one
	BOOL			SpawnRound();										// 005069f0
	FVector			GetPredictedTargetPos(class CUnit *target);			// 0050a0e0: where a predictive mode leads the target

	CParticleEffectLink	mEffects[2];									// +0x14 (the constructor's vector constructor, 00505e43)
	SINT			mEmitters[2];										// +0x24: the launch index for each effect (MoveEmitter 00507938)
	CActiveReader<CThing>	mTarget;									// +0x2c
	FMatrix			mOrientation;										// +0x30: MakeRotation(0, 0, 0) at first (00505e90)
	float			mCharge;											// +0x60 (FullyCharged, 00411d1c)
	float			mNextFireTime;										// +0x64: the event time the weapon may fire again (-200 at first; ReadyToFire 00509f81)
	SINT			mUnknown68;											// +0x68: Charged when positive (00412091)
	SINT			mBurstCount;										// +0x6c: rounds fired in this burst (HandleEvent 005069aa)
	SINT			mUnknown70;											// +0x70: -1 at first
	SINT			mUnknown74;											// +0x74: -1 at first
	SINT			mUnknown78;											// +0x78
	SINT			mUnknown7C;											// +0x7c
	SINT			mUnknown80;											// +0x80
	FVector			mUnknown84;											// +0x84: (-1, -1, -1) at first
	SINT			mUnknown94;											// +0x94
	SINT			mUnknown98;											// +0x98
	BOOL			mActive;											// +0x9c (IsActive; Enable/DisableWeapon)
	CWeaponModeData	*mMode;												// +0xa0
	CWeaponData		*mData;												// +0xa4
	ULONG			mTargetTypes;										// +0xa8: the thing types it can hit (the constructor's second argument; 0050a0bf)
	SINT			mUnknownAC;											// +0xac: its emitter's type (CUnit::GetLaunchPosition 004fc44d)
};

#endif
