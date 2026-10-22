// WorldPhysicsManager.h: the physics script's definitions and the manager that creates things from them.
// Reconstructed from WorldPhysicsManager.obj (0050de90..00511eef); Stuart's release has no copy, only callers
// (UPhysicsManager::SpawnWeapon, GetWeapon, SpawnExplosion, GetExplosion).
//
// UPhysicsManager is a class of statics. Its registries are pointers to sets, allocated in this order by
// InitializeLists (00510... lines 301..309): weapons 008553e8, weapon modes 008553ec, rounds 008553f0, spawners
// 008553f4, explosions 008553f8, units 008553fc, components 00855400, features 00855404, hazards 00855408.
// The definitions' destructors and small resolvers are inline: ClearAndFreeAll (00510a90) and
// InitialiseEffects (00510520) call the ones the inline budget left out of line (the COMDATs at
// 00510e60..005113a0) and inline the rest.

#ifndef WORLD_PHYSICS_MANAGER_H
#define WORLD_PHYSICS_MANAGER_H

#include "SPtrSet.h"
#include "ParticleManager.h"
#include "music.h"

// The emitter types (Stuart's InitThing.h defaults a waypoint path to kNullEmitter, 0: CInitThing's constructor 0048ddc8)
enum EEmitterType
{
	kNullEmitter		= 0,
	kEngineEmitter		= 23,
	kThrusterEmitter	= 26
};

class CThing;
class CUnit;
class CWeapon;
class CRound;
class CExplosion;
class CFeature;
class CHazard;
class CSquad;
class CEffect;

// The unit definitions' behaviour types (CUnitData +0xe0): the values the physics script's behaviour keywords
// return (CMechUnitBehaviourType returns 0 ... CSimpleBuildingBehaviourType 25) and the classes SpawnThing makes.
enum EUnitBehaviour
{
	UB_MECH = 0,
	UB_AIRCRAFT,				// no keyword class; SpawnThing makes a CPlane, as for UB_FIGHTER
	UB_GROUND_UNIT,
	UB_INFANTRY,
	UB_TURRET,
	UB_BOAT,
	UB_CARRIER,
	UB_BUILDING,
	UB_FIGHTER,
	UB_BOMBER,
	UB_GROUND_ATTACK_AIRCRAFT,
	UB_JEEP,					// SpawnThing makes nothing for it
	UB_DROPSHIP,
	UB_MINE,
	UB_HIVE_BOSS,
	UB_SUBMARINE,
	UB_DIVE_BOMBER,
	UB_THUNDERHEAD,
	UB_CARVER,
	UB_GILLM,
	UB_SENTINEL,
	UB_WARSPITE,
	UB_FENRIR,
	UB_WARSPITE_DOME,
	UB_POD,
	UB_SIMPLE_BUILDING
};

// A weapon definition (the registry 008553e8; its destructor is emitted at 00511040).
class CWeaponData
{
public:
	CWeaponData(char *name);				// the physics script's; the retail header defines these inline
	void		SetIconName(char *name);
	~CWeaponData();

	char		*mName;						// +0x00
	char		*mIconName;					// +0x04 (CWeapon::GetIconName, 00412514)
	float		mChargeRate;				// +0x08
	SINT		mChargeModes[5];			// +0x0c: the mode for each charge level, -1 for none (CWeapon::CanCharge,
											// FullyCharged; CBattleEngineJetPart::ChargeWeapon 00411c79, 00411cde)
	float		mConsumption;				// +0x20 (CWeapon::GetConsumption, 00411d8e)
	SINT		mAmmoStore;					// +0x24 (CWeapon::GetAmmoStore, 00411cab)
	BOOL		mAdjustAim;					// +0x28
	BOOL		mAllowMovement;				// +0x2c (CWeapon::AllowMovement, 00411d39)
	BOOL		mSmart;						// +0x30
	SINT		mZoomMode;					// +0x34 (CWeapon::GetZoomMode, 00411eac)
	SINT		mPlacement;					// +0x38 (CWeapon::GetPlacement, 00412564)
	SINT		mLanguageName;				// +0x3c: its text id (CFEPBEConfig::GetWalkerWeaponName 0044f73f; CWeapon::GetLanguageName 00412463)
	SINT		mRating[3];					// +0x40: the configuration page's three ratings (CFEPBEConfig's
											// weapon-stat helpers 0044f1e8, a switch over 0..2)
};

class CRoundData;

// One of a weapon mode's launch angles (the physics script's LaunchAngle).
class CLaunchAngle
{
public:
	float		mA;							// +0x00
	float		mB;							// +0x04
};

// A weapon mode (the registry 008553ec; its destructor is emitted at 00510f10).
class CWeaponModeData
{
public:
	~CWeaponModeData();

	void		SetRound(SINT index);		// 005113f0
	CWeaponModeData(char *name);			// the physics script's; the retail header defines these inline
	void		SetMuzzleEffect(char *name);
	void		SetPreFireEffect(char *name);
	void		SetClip(char *name);
	void		SetLaunchSound(char *name);
	void		SetPreFireSound(char *name);
	void		SetPostFireSound(char *name);
	void		SetLaunchSequence(SINT index, SINT value);
	void		SetLaunchAngle(SINT index, float a, float b);
	void		Resolve();					// 00511ca0
	SINT		GetLaunchSequence(SINT index);	// 005078b0 (weapon.obj): the sequence's value at that position, or 0

	char				*mClipName;			// +0x00: Clip
	CParticleDescriptor	*mParticleA;		// +0x04
	CParticleDescriptor	*mParticleB;		// +0x08
	CEffect				*mSoundA;			// +0x0c
	CEffect				*mSoundB;			// +0x10
	CEffect				*mSoundC;			// +0x14
	CRoundData			*mRound;			// +0x18
	char				*mParticleAName;	// +0x1c
	char				*mParticleBName;	// +0x20
	char				*mSoundAName;		// +0x24
	char				*mSoundBName;		// +0x28
	char				*mSoundCName;		// +0x2c
	char				*mName;				// +0x30
	float				mInaccuracy;		// +0x34
	float				mReloadTime;		// +0x38
	float				mBurstDelay;		// +0x3c
	float				mPower;				// +0x40: the recoil (CWeapon::GetPower, CBattleEngine::RecoilWeapon 0040c34f)
	SINT				mBurstSize;			// +0x44
	SINT				mVolleySize;		// +0x48
	SPtrSet<void>		mSet4c;				// +0x4c: the launch sequence (SINTs)
	SPtrSet<void>		mSet5c;				// +0x5c: the launch angles (CLaunchAngle)
	float				mUnknown6C;			// +0x6c: the lowest target height above the ground (CUnit 004fb4c5)
	float				mUnknown70;			// +0x70: the highest (004fb4d3)
	float				mMinRange;			// +0x74
	float				mMaxRange;			// +0x78
	float				mMinDeflection;		// +0x7c
	float				mMaxDeflection;		// +0x80
	float				mYawTolerance;		// +0x84
	float				mUnknown88;			// +0x88 (CUnit 004fbd54)
	float				mUnknown8C;			// +0x8c: PostFireDelay; a delay (CUnit 004fc1d4)
	SINT				mMaxLocks;			// +0x90
	float				mLockTime;			// +0x94
	float				mLockDeflection;	// +0x98
	float				mLockRange;			// +0x9c
	float				mLockRadius;		// +0xa0
	SINT				mLockUnit;			// +0xa4
	SINT				mLockMode;			// +0xa8
	BOOL				mTrack;				// +0xac
	BOOL				mPredictive;		// +0xb0
	BOOL				mSoundPerBurst;		// +0xb4
	SINT				mMuzzleLight;		// +0xb8
	float				mMuzzleLightRadius;	// +0xbc
};

// A round (the registry 008553f0; its destructor is emitted at 00510eb0).
class CRoundData
{
public:
	~CRoundData();
	CRoundData(char *name);					// the physics script's; the retail header defines these inline
	void		SetMesh(char *name);			// 00437fe0 (out of line in CRoundBasedOn's Apply)
	void		SetEffect(char *name);
	void		SetWaterEffect(char *name);
	void		SetExplosion(char *name);		// 00438050 (likewise)
	void		CopyFrom(CRoundData *other);
	BOOL		Unknown40d0f0();				// 0040d0f0: no-argument round predicate; explicit BOOL result
	BOOL		IsBallistic();					// weapon.cpp's never-inlined copy of it (see there); retail's is folded into 0040d0f0
	void		Resolve()
	{
		if (mParticleAName)
			mParticleA = PARTICLE_SET.FindByName(mParticleAName);
		if (mParticleBName)
			mParticleB = PARTICLE_SET.FindByName(mParticleBName);
	}

	CParticleDescriptor	*mParticleA;		// +0x00
	CParticleDescriptor	*mParticleB;		// +0x04
	char				*mUnknown08;		// +0x08: its explosion's name (CRound 004db099)
	char				*mUnknown0c;		// +0x0c
	char				*mParticleAName;	// +0x10
	char				*mParticleBName;	// +0x14
	char				*mName;				// +0x18
	float				mUnknown1C;			// +0x1c: Damage; 004fb280 re-aims only when it is positive (CUnit 004f934d region)
	float				mRearm;				// +0x20: Rearm / 100
	float				mLifeSpan;			// +0x24
	float				mUnknown28;			// +0x28: TurnRate (CRound::ClipToGround 004d8306)
	float				mSpeed;				// +0x2c: per second (CRound::GetMaxVelocity 004d82c7, GetRadius)
	float				mBounceFactor;		// +0x30 (CRound::BounceFactor 004d82d6)
	float				mSeekDelay;			// +0x34
	float				mWiggle;			// +0x38
	float				mGravity;			// +0x3c: scaled by 0.025 (CRound::Gravity 004db61e)
	float				mSeekAngle;			// +0x40
	float				mSeekTerminationTime;	// +0x44
	BOOL				mUnknown48;			// +0x48: Seek; it may take a target (CRound 004daac6)
	BOOL				mFlak;				// +0x4c
	BOOL				mBeam;				// +0x50
	BOOL				mWeirdoSeek;		// +0x54
	BOOL				mUnknown58;			// +0x58: GridOfFear; the round joins the world's set +0x60 (CRound::Shutdown 004d8dca)
	BOOL				mCanGoUnderWater;	// +0x5c (CRound::CanGoUnderWater 004d8336)
	BOOL				mUnknown60;			// +0x60: Fire; it sticks where it lands (CRound::DeclareOnGround 004d9e18)
	SINT				mPassiveCollision;	// +0x64
	SINT				mGroundHugging;		// +0x68
	BOOL				mUnknown6C;			// +0x6c: Torpedo (CRound::Gravity 004db606)
	BOOL				mUnknown70;			// +0x70: Missile; SpawnRound makes a CRound when it is set (0050f7d3)
	BOOL				mExplode;			// +0x74
	BOOL				mSmart;				// +0x78
	float				mFlakInaccuracy;	// +0x7c: FlakInaccuracy / 100
	SINT				mSoundMaterial;		// +0x80 (CRound::GetSoundMaterial 004d8346)
	SINT				mJumps;				// +0x84
	float				mProximity;			// +0x88
	float				mRadius;			// +0x8c: added to half a frame's travel (CRound::GetRadius 004d8ad5)
	float				mUnknown90;			// +0x90: JumpRange; how near it looks for targets (CRound 004dabb3)
	float				mJumpDelay;			// +0x94
	float				mLength;			// +0x98
	BOOL				mStreamLaser;		// +0x9c: its name is "Stream Laser"
	BOOL				mGillMBreath;		// +0xa0: its name is "Gill-M Breath"
	SINT				mTreeCollision;		// +0xa4
};

// A spawner definition (the registry 008553f4; polymorphic, RTTI names it).
class CSpawnerData
{
public:
	CSpawnerData(char *name);				// the physics script's; the retail header defines these inline
	virtual		~CSpawnerData()				// +0x00 is the virtual table
	{
		delete [] mThingName;	mThingName = NULL;
		delete [] mName;		mName = NULL;
	}
	void		SetThingName(char *name);
	void		CopyFrom(CSpawnerData *other);

	char		*mThingName;				// +0x04: the definition it spawns (Unit)
	char		*mName;						// +0x08 (AddSpawner 00511b16)
	SINT		mAmount;					// +0x0c
	SINT		mSquadSize;					// +0x10
	SINT		mConditions;				// +0x14
	float		mDelay;						// +0x18
	float		mSeekDelay;					// +0x1c
	float		mSquadDelay;				// +0x20
	SINT		mInfinite;					// +0x24
	BOOL		mRecall;					// +0x28
	float		mMinRange;					// +0x2c
	float		mMaxRange;					// +0x30
	float		mPreSpawnDelay;				// +0x34
	float		mPostSpawnDelay;			// +0x38
};

// An explosion (the registry 008553f8; its destructor is emitted at 00511070).
class CExplosionData
{
public:
	~CExplosionData();
	void		Resolve();					// 00511d20
	CExplosionData(char *name);				// the physics script's; the retail header defines these inline
	void		SetAirEffect(char *name);
	void		SetWaterEffect(char *name);
	void		SetGroundEffect(char *name);
	void		SetUnitEffect(char *name);
	void		SetSound(char *name);			// 0043aea0 (out of line in CExplosionBasedOn's Apply)
	void		SetWaterSound(char *name);		// 0043af10 (likewise)
	void		CopyFrom(CExplosionData *other);

	CParticleDescriptor	*mParticleA;		// +0x00
	CParticleDescriptor	*mParticleB;		// +0x04
	CParticleDescriptor	*mParticleC;		// +0x08
	CParticleDescriptor	*mParticleD;		// +0x0c
	CEffect				*mSoundA;			// +0x10
	CEffect				*mSoundB;			// +0x14
	char				*mParticleAName;	// +0x18
	char				*mParticleBName;	// +0x1c
	char				*mParticleCName;	// +0x20
	char				*mParticleDName;	// +0x24
	char				*mSoundAName;		// +0x28
	char				*mSoundBName;		// +0x2c
	char				*mName;				// +0x30
	float				mRadius;			// +0x34 (CRound 004db080)
	float				mDamage;			// +0x38: over 3 destroys trees (CExplosion::SetThingType 0044c194)
	SINT				mVolumetric;		// +0x3c
	SINT				mTime;				// +0x40
	SINT				mSmart;				// +0x44
	SINT				mOriented;			// +0x48
	SINT				mLight;				// +0x4c
};

// A unit's weapon, spawner or component, by its position in that registry (AddWeapon 005119e0, AddSpawner
// 00511ad0: 12 bytes; AddComponent 00511900: 8 bytes).
class CUnitWeapon
{
public:
	SINT		mIndex;						// +0x00
	SINT		mUnknown04;					// +0x04
	SINT		mMount;						// +0x08: MapMountName's number for the gun or spawner tag
};

class CSpawnerReference
{
public:
	SINT		mSpawnerIndex;				// +0x00
	SINT		mUnknown04;					// +0x04
	SINT		mMount;						// +0x08
};

class CUnitComponentReference
{
public:
	SINT		mIndex;						// +0x00
	SINT		mUnknown04;					// +0x04
};

// A pair the unit keeps in +0x6c (FindPair 00511bc0): a cached emitter in the unit's own space (0x48 bytes,
// CUnit's slot 88 makes them, 004fc5ac).
class CUnitPair
{
public:
	CUnitPair(SINT a, SINT b);

	SINT		mA;							// +0x00: the emitter's type
	SINT		mB;							// +0x04: its index
	FVector		mPos;						// +0x08
	FMatrix		mOrientation;				// +0x18
};

// Reconstructed list cleanup boundary: all six GenericSPtrSet::First calls stay out of line as in retail.
// Together with the effect-name and pair cleanup boundaries below, ~CUnitData (005110f0) also calls
// GenericListIterator::First and retains the external cursor on its stack.
template <class T> inline void DeleteEach(SPtrSet<T> &list)
{
	T	*item;
	while (item = list.First())
	{
		list.Remove(item);
		delete item;
	}
}

// A unit or component definition (0x1ac bytes; the registries 008553fc and 00855400; its destructor is emitted at
// 005110f0).
class CUnitData
{
public:
	~CUnitData();

	// Reconstructed cleanup boundaries: release the owned effect names, then drain the external-cursor pair cache.
	// These helper names are not recovered original names.
	void ClearEffectNames()
	{
		delete [] mEffectNames[0];	mEffectNames[0] = NULL;
		delete [] mEffectNames[1];	mEffectNames[1] = NULL;
		delete [] mEffectNames[2];	mEffectNames[2] = NULL;
		delete [] mEffectNames[3];	mEffectNames[3] = NULL;
		delete [] mEffectNames[4];	mEffectNames[4] = NULL;
		delete [] mEffectNames[6];	mEffectNames[6] = NULL;
		delete [] mEffectNames[7];	mEffectNames[7] = NULL;
		delete [] mEffectNames[8];	mEffectNames[8] = NULL;
		delete [] mEffectNames[9];	mEffectNames[9] = NULL;
		delete [] mEffectNames[10];	mEffectNames[10] = NULL;
		delete [] mEffectNames[11];	mEffectNames[11] = NULL;
		delete [] mEffectNames[12];	mEffectNames[12] = NULL;
		delete [] mEffectNames[5];	mEffectNames[5] = NULL;
	}

	void ClearPairCache()
	{
		ListIterator<CUnitPair>	pairs(&mPairs);
		CUnitPair	*pair;
		while (pair = pairs.First())
		{
			mPairs.Remove(pair);
			delete pair;
		}
	}

	// Made by the physics script (CPhysicsScriptStatements.obj); the retail header defines these inline.
	CUnitData(char *name);
	void		Init();							// 0042efd0: the defaults
	void		SetMesh(char *name);
	void		SetNoise(char *name);
	void		SetSpawnNoise(char *name);
	void		SetTrails(char *name);
	void		SetThrusters(char *name);
	void		SetSmoke(char *name);
	void		SetDust(char *name);
	void		SetEffect94(char *name);
	void		SetVent(char *name);
	void		SetAfterburner(char *name);
	void		SetShieldEffect(char *name);
	void		SetWaterWake(char *name);
	void		SetGroundWake(char *name);
	void		SetUnknown30(char *name);
	void		SetActivationEffect(char *name);
	void		CopyFrom(CUnitData *other);		// 00433390: a BasedOn copy

	void		SetExplosion(char *name);		// 00511720
	void		SetSmallExplosion(char *name);	// 005117c0
	void		SetStompExplosion(char *name);	// 00511860
	void		AddComponent(SINT a, char *name);			// 00511900
	void		AddWeapon(char *name, char *mount, SINT a);	// 005119e0
	void		AddSpawner(char *name, char *mount, SINT a);	// 00511ad0
	CUnitPair	*FindPair(SINT a, SINT b);		// 00511bc0
	void		Resolve();						// 00511db0

	CParticleDescriptor			*mParticles[11];	// +0x00
	char						*mUnknown2c;		// +0x2c
	char						*mUnknown30;		// +0x30
	CEffect						*mSounds[2];		// +0x34
	SPtrSet<CUnitWeapon>		mWeapons;			// +0x3c
	SPtrSet<CSpawnerReference>	mSpawners;			// +0x4c
	SPtrSet<CUnitComponentReference>		mComponents;		// +0x5c
	SPtrSet<CUnitPair>			mPairs;				// +0x6c
	char						*mEffectNames[13];	// +0x7c: the eleven particles', then the two sounds'
	char						*mName;				// +0xb0
	// The physics script's keywords name the fields its values set (CPhysicsScriptStatements.h); the older names
	// stay until their users move over.
	float						mUnknownB4;			// +0xb4: Velocity; CSubmarine's maximum velocity (0050eb90)
	float						mTurnRate;			// +0xb8: TurnRate
	float						mUnknownBC;			// +0xbc: TurretTurnRate; a barrel's pitch rate (CUnit::Move 004fae85)
	float						mUnknownC0;			// +0xc0: Life; CUnit's slot 78 when it has no segments (004f9a21)
	float						mAirShields;		// +0xc4
	float						mGroundShields;		// +0xc8
	float						mAirShieldRegeneration;		// +0xcc
	float						mGroundShieldRegeneration;	// +0xd0
	float						mUnknownD4;			// +0xd4: Stealth; CMine's stealth (0050eb00)
	float						mMaxYaw;			// +0xd8: a component's MaxYaw
	float						mUnknownDC;			// +0xdc: a component's MaxTurretYaw; a turret's yaw limit and rate (CUnit::Move 004fae49, 004fafab)
	EUnitBehaviour				mBehaviour;			// +0xe0
	SINT						mSoundMaterial;		// +0xe4 (CUnit::GetSoundMaterial, 00417610)
	SINT						mExplosion;			// +0xe8
	SINT						mSmallExplosion;	// +0xec
	SINT						mStompExplosion;	// +0xf0
	float						mThreat;			// +0xf4 (CUnit::GetThreat, 004175c6)
	float						mImportance;		// +0xf8 (CUnit::GetImportance, 004175d6)
	SINT						mUnknownFC;			// +0xfc: NavMap; which of the world's occupancy grids (CUnit 004fd380)
	SINT						mParachute;			// +0x100
	SINT						mUnknown104;		// +0x104: KnockTrees; its units destroy trees (CUnit::SetThingType 004fcdc0)
	SINT						mUnknown108;		// +0x108: Deploy; its units deploy before they fire (CUnit 004fbcc2)
	SINT						mUnknown10C;		// +0x10c: Hover; its units raise dust (CUnit::Move 004fab44)
	SINT						mUnknown110;		// +0x110: MultipleSpawn (CUnit's slot 84, 004fc036)
	BOOL						mCanBeLocked;		// +0x114: Lockable (CUnit::CanBeLocked, 0041763a)
	SINT						mUnknown118;		// +0x118: no keyword sets it
	SINT						mUnknown11C;		// +0x11c: Retreat; its units don't die at zero life (CUnit::Damage 004f9eac)
	SINT						mUnknown120;		// +0x120: Wingman (CUnit 004fda10)
	SINT						mUnknown124;		// +0x124: Big; explosions don't hurt its dying units (CUnit::Damage 004f9af9)
	SINT						mUnknown128;		// +0x128: Indiscriminate; its units also target allegiance 2 (CUnit::IsTargetAlligence 004fd3df)
	SINT						mUnknown12C;		// +0x12c: a component's Tentacle; SpawnCharacter makes a CTentacle when it is set (0050fb6d)
	SINT						mShatter;			// +0x130
	SINT						mUnknown134;		// +0x134: a component's PassOnHitEffect; it passes its damage flashes to its parent (CComponent 00428cfe)
	SINT						mIgnoreThreats;		// +0x138
	SINT						mUnknown13C;		// +0x13c: AnimatedCollision; its buildings' meshes move (CBuilding::Init 004171d1 uses fixed mesh transforms when it is 0)
	SINT						mMaxLegsLifted;		// +0x140
	float						mBodyMotion;		// +0x144
	float						mLegSpeed;			// +0x148
	float						mLegPlacementArea;	// +0x14c
	float						mStandingLegPlacementArea;	// +0x150
	float						mBounceFactor;		// +0x154 (CUnit::BounceFactor, 00417626)
	float						mMaxTargetRange;	// +0x158
	float						mMinAltitude;		// +0x15c
	float						mUnknown160;		// +0x160: DeployDamageMultiplier; the damage factor while deploying (CUnit::Damage 004f9b29)
	float						mAttackPriority[7];	// +0x164
	float						mStrafeChange;		// +0x180
	float						mDetonationDelay;	// +0x184
	float						mMinThrusterHeight;	// +0x188
	float						mMaxThrusterHeight;	// +0x18c
	SINT						mBuildingNumber;	// +0x190
	SINT						mBuildingMeshType;	// +0x194
	SINT						mUnknown198;		// +0x198: a component's Detach; its components clip to the ground (CComponent::ClipToGround 00428c58)
	SINT						mSweeping;			// +0x19c
	SINT						mUnknown1A0;		// +0x1a0: no keyword sets it; its units don't cache emitters (CUnit 004fc50d)
	SINT						mFenrir;			// +0x1a4: set for "Fenrir" and "Fenrir Main Gun" (0042ef8e)
	SINT						mIcon;				// +0x1a8
};

// A feature (the registry 00855404; its destructor is emitted at 00510e60).
class CFeatureData
{
public:
	~CFeatureData();
	void		Resolve();					// inline, defined in WorldPhysicsManager.cpp (its one caller)




	void		SetExplosion(char *name);	// 00511c10
	CFeatureData(char *name);				// the physics script's; the retail header defines these inline
	void		SetMesh(char *name);
	void		SetNoise(char *name);

	char		*mMeshName;					// +0x00: Mesh
	CEffect		*mSound;					// +0x04
	SINT		mExplosion;					// +0x08
	char		*mSoundName;				// +0x0c
	BOOL		mInvincible;				// +0x10
	SINT		mUnknown14;					// +0x14: Lockable (CFeature's slot 71 returns it, 00510146)
	float		mLife;						// +0x18
	SINT		mIcon;						// +0x1c
	char		*mName;						// +0x20
};

// A hazard (the registry 00855408; ClearAndFreeAll inlines its destructor).
class CHazardData
{
public:
	~CHazardData();
	CHazardData(char *name);				// the physics script's; the retail header defines these inline
	void		SetEffect(char *name);
	void		SetNoise(char *name);
	void		Resolve();					// inline, defined in WorldPhysicsManager.cpp (its one caller)







	CParticleDescriptor	*mParticle;			// +0x00
	CEffect				*mSound;			// +0x04
	char				*mParticleName;		// +0x08
	char				*mSoundName;		// +0x0c
	char				*mName;				// +0x10
	float				mRadius;			// +0x14
	float				mDamage;			// +0x18
};

class UPhysicsManager
{
public:
	static CThing	*SpawnThing(SINT unit);		// 0050df80: a thing for the unit definition at that position
	static CSquad	*SpawnSquad(SINT behaviour);	// 0050f4b0: a squad for units of that behaviour, or NULL
	static class CComponent	*SpawnCharacter(SINT index);	// 0050fa40: a component for the component definition at that position
	static BOOL		IsSquadBehaviour(SINT behaviour);	// 0050f680: FALSE for 4..24 but 21 (the spawners ask)

	// The position of the unit definition with that name, or -1 (inlined by WorldMeshList.obj, 0050dce3).
	static SINT		GetUnitIndex(char *name)
	{
		if (name)
		{
			SINT		i = 0;
			CUnitData	*unit;
			for (unit = mUnits->First(); unit; unit = mUnits->Next())
			{
				if (strcmp(name, unit->mName) == 0)
					return i;
				i++;
			}
		}
		return -1;
	}

	// The unit definition with that name, or NULL (inlined by WorldMeshList.obj, 0050dd7a).
	static CUnitData	*GetUnitData(char *name)
	{
		if (name)
		{
			CUnitData	*unit;
			for (unit = mUnits->First(); unit; unit = mUnits->Next())
				if (strcmp(name, unit->mName) == 0)
					return unit;
		}
		return NULL;
	}

	// The weapon definition with that name, or NULL (inlined by FEPBEConfig.obj's weapon helpers, 0044f176).
	static CWeaponData	*GetWeaponData(char *name)
	{
		if (name)
		{
			CWeaponData	*w;
			for (w = mWeapons->First(); w; w = mWeapons->Next())
				if (strcmp(name, w->mName) == 0)
					return w;
		}
		return NULL;
	}

	// The position of the definition with that name, or -1 (inlined by the unit definitions' setters).
	static SINT		GetExplosionIndex(char *name)
	{
		if (name)
		{
			SINT			i = 0;
			CExplosionData	*e;
			for (e = mExplosions->First(); e; e = mExplosions->Next())
			{
				if (strcmp(name, e->mName) == 0)
					return i;
				i++;
			}
		}
		return -1;
	}
	static SINT		GetComponentIndex(char *name)
	{
		if (name)
		{
			SINT		i = 0;
			CUnitData	*c;
			for (c = mComponents->First(); c; c = mComponents->Next())
			{
				if (strcmp(name, c->mName) == 0)
					return i;
				i++;
			}
		}
		return -1;
	}
	static SINT		GetWeaponIndex(char *name)
	{
		if (name)
		{
			SINT		i = 0;
			CWeaponData	*w;
			for (w = mWeapons->First(); w; w = mWeapons->Next())
			{
				if (strcmp(name, w->mName) == 0)
					return i;
				i++;
			}
		}
		return -1;
	}
	static SINT		GetSpawnerIndex(char *name)
	{
		if (name)
		{
			SINT			i = 0;
			CSpawnerData	*s;
			for (s = mSpawners->First(); s; s = mSpawners->Next())
			{
				if (strcmp(name, s->mName) == 0)
					return i;
				i++;
			}
		}
		return -1;
	}

	// The round definition at that position, or NULL (inlined by CWeaponModeData::SetRound 005113f0).
	static CRoundData	*GetRound(SINT index)
	{
		SINT		i = 0;
		CRoundData	*round;
		for (round = mRounds->First(); round; round = mRounds->Next())
		{
			if (i == index)
				return round;
			i++;
		}
		return NULL;
	}

	// The definitions at those positions, or NULL (inlined by the Spawn functions).
	static CWeaponData	*GetWeaponData(SINT index)
	{
		SINT		i = 0;
		CWeaponData	*w;
		for (w = mWeapons->First(); w; w = mWeapons->Next())
		{
			if (i == index)
				return w;
			i++;
		}
		return NULL;
	}
	static CUnitData	*GetUnitData(SINT index)
	{
		SINT		i = 0;
		CUnitData	*u;
		for (u = mUnits->First(); u; u = mUnits->Next())
		{
			if (i == index)
				return u;
			i++;
		}
		return NULL;
	}
	static CUnitData	*GetComponentData(SINT index)
	{
		SINT		i = 0;
		CUnitData	*c;
		for (c = mComponents->First(); c; c = mComponents->Next())
		{
			if (i == index)
				return c;
			i++;
		}
		return NULL;
	}
	// Inlined by CUnit's explosions (004f93a9, 004fd26c).
	static CExplosionData	*GetExplosionData(SINT index)
	{
		SINT			i = 0;
		CExplosionData	*e;
		for (e = mExplosions->First(); e; e = mExplosions->Next())
		{
			if (i == index)
				return e;
			i++;
		}
		return NULL;
	}
	static CSpawnerData	*GetSpawnerData(SINT index)
	{
		SINT			i = 0;
		CSpawnerData	*s;
		for (s = mSpawners->First(); s; s = mSpawners->Next())
		{
			if (i == index)
				return s;
			i++;
		}
		return NULL;
	}

	static CWeapon			*SpawnWeapon(SINT index, SINT type);		// 0050f6d0
	// Stuart's callers pass the name (BattleEngineJetPart.cpp); retail inlines the lookup (00412703..0041272b).
	static CWeapon			*SpawnWeapon(char *name, SINT type) { return SpawnWeapon(GetWeaponIndex(name), type); }
	static class CSpawner	*SpawnSpawner(SINT index, SINT a);	// 0050f970
	static CRound		*SpawnRound(CRoundData *data);	// 0050f7a0
	static CExplosion	*SpawnExplosion(SINT index);	// 0050ff10
	// Stuart's callers pass the name (BattleEngine.cpp); not checked against retail yet.
	static CExplosion	*SpawnExplosion(char *name) { return SpawnExplosion(GetExplosionIndex(name)); }
	// the matching definition itself, no index (CBattleEngine::Explode 0040e098..0040e155)
	static CExplosionData	*GetExplosion(char *name)
	{
		if (name)
		{
			CExplosionData	*e;
			for (e = mExplosions->First(); e; e = mExplosions->Next())
				if (strcmp(name, e->mName) == 0)
					return e;
		}
		return NULL;
	}
	static CFeature		*SpawnFeature(SINT index);		// 00510060
	static CHazard		*SpawnHazard(SINT index);		// 00510150
	static void		InitializeLists();						// 005102a0
	static void		InitialiseEffects();					// 00510520 (Stuart's name: CGame::LoadResources calls it)
	static void		ClearCachedEmitters();					// 00510740 (Stuart's name: CGame::ShutdownRestartLoop 0046cc61)
	static void		Reload();								// 00510800
	static void		ClearAndFreeAll();						// 00510a90
	static BOOL		CanSpawnerMake(char *name);				// 00511440
	static SINT		GetBehaviourPriority(CUnitData *unit);	// 00511510
	static SINT		MapMountName(char *name);				// 005115b0
	static void		AddUnit(char *name);					// 0042ee90 (the physics script)
	static void		AddWeapon(char *name);					// 0042f5f0
	static void		AddWeaponMode(char *name);				// 0042fa80
	static void		AddRound(char *name);					// 0042ffa0
	static void		AddSpawnerData(char *name);				// 00430510
	static void		AddExplosion(char *name);				// 004309e0
	static void		AddComponentData(char *name);			// 00430e60
	static void		AddFeature(char *name);					// 00431350
	static void		AddHazard(char *name);					// 004317a0
	static CUnitData	*GetComponentData(char *name)
	{
		if (name)
		{
			CUnitData	*c;
			for (c = mComponents->First(); c; c = mComponents->Next())
				if (strcmp(name, c->mName) == 0)
					return c;
		}
		return NULL;
	}
	static CFeatureData	*GetFeatureData(char *name)
	{
		if (name)
		{
			CFeatureData	*f;
			for (f = mFeatures->First(); f; f = mFeatures->Next())
				if (strcmp(name, f->mName) == 0)
					return f;
		}
		return NULL;
	}
	static CHazardData	*GetHazardData(char *name)
	{
		if (name)
		{
			CHazardData	*h;
			for (h = mHazards->First(); h; h = mHazards->Next())
				if (strcmp(name, h->mName) == 0)
					return h;
		}
		return NULL;
	}
#ifdef WORLD_CPP	// world.cpp's view; the other includers keep their declaration counts
	// The positions of the definitions with that name, or -1 (inlined by CWorld::LoadWorld, 0050bee4, 0050c0d4).
	static SINT		GetFeatureIndex(char *name)
	{
		if (name)
		{
			SINT			i = 0;
			CFeatureData	*f;
			for (f = mFeatures->First(); f; f = mFeatures->Next())
			{
				if (strcmp(name, f->mName) == 0)
					return i;
				i++;
			}
		}
		return -1;
	}
	static SINT		GetHazardIndex(char *name)
	{
		if (name)
		{
			SINT		i = 0;
			CHazardData	*h;
			for (h = mHazards->First(); h; h = mHazards->Next())
			{
				if (strcmp(name, h->mName) == 0)
					return i;
				i++;
			}
		}
		return -1;
	}
	// Spawn by name; the names and signatures are ours. Retail's CWorld::LoadWorld calls GenericSPtrSet::First out of
	// line in its feature and hazard lookups (0050beec, 0050c0dc); the inline budget gives that only when each lookup
	// sits one level deeper, inside a small wrapper such as these (the inline-budget simulator).
	static CFeature		*SpawnFeature(char *name) { return SpawnFeature(GetFeatureIndex(name)); }
	static CHazard		*SpawnHazard(char *name) { return SpawnHazard(GetHazardIndex(name)); }
	static CThing		*SpawnThing(char *name) { return SpawnThing(GetUnitIndex(name)); }
#endif
	static CSpawnerData	*GetSpawnerData(char *name)
	{
		if (name)
		{
			CSpawnerData	*s;
			for (s = mSpawners->First(); s; s = mSpawners->Next())
				if (strcmp(name, s->mName) == 0)
					return s;
		}
		return NULL;
	}
	static CRoundData	*GetRoundData(char *name)
	{
		if (name)
		{
			CRoundData	*round;
			for (round = mRounds->First(); round; round = mRounds->Next())
				if (strcmp(name, round->mName) == 0)
					return round;
		}
		return NULL;
	}
	static CWeaponModeData	*GetWeaponModeData(char *name)
	{
		if (name)
		{
			CWeaponModeData	*mode;
			for (mode = mWeaponModes->First(); mode; mode = mWeaponModes->Next())
				if (strcmp(name, mode->mName) == 0)
					return mode;
		}
		return NULL;
	}
	static SINT		GetRoundIndex(char *name)
	{
		if (name)
		{
			SINT		i = 0;
			CRoundData	*round;
			for (round = mRounds->First(); round; round = mRounds->Next())
			{
				if (strcmp(name, round->mName) == 0)
					return i;
				i++;
			}
		}
		return -1;
	}
	// The position of the weapon mode with that name, or -1 (the physics script's charge levels).
	static SINT		GetWeaponModeIndex(char *name)
	{
		if (name)
		{
			SINT			i = 0;
			CWeaponModeData	*mode;
			for (mode = mWeaponModes->First(); mode; mode = mWeaponModes->Next())
			{
				if (strcmp(name, mode->mName) == 0)
					return i;
				i++;
			}
		}
		return -1;
	}

	static SPtrSet<CWeaponData>		*mWeapons;		// 008553e8
	static SPtrSet<CWeaponModeData>	*mWeaponModes;	// 008553ec
	static SPtrSet<CRoundData>		*mRounds;		// 008553f0
	static SPtrSet<CSpawnerData>	*mSpawners;		// 008553f4
	static SPtrSet<CExplosionData>	*mExplosions;	// 008553f8
	static SPtrSet<CUnitData>		*mUnits;		// 008553fc
	static SPtrSet<CUnitData>		*mComponents;	// 00855400
	static SPtrSet<CFeatureData>	*mFeatures;		// 00855404
	static SPtrSet<CHazardData>		*mHazards;		// 00855408
};

// The unit definitions' construction and setters, called by the physics script's values. The retail header defines
// them inline; the #line directives give each allocation its retail __LINE__ (the file's layout is unknown).
inline void CUnitData::Init()
{
	mBehaviour = (EUnitBehaviour)3;
	mUnknownFC = 3;
	mTurnRate = 0;
	mUnknownB4 = 0;
	mSounds[0] = NULL;
	mSounds[1] = NULL;
	mUnknown2c = NULL;
	mParticles[0] = NULL;
#line 1511
	mUnknown30 = new(MT_UNIT_DATA) char[sizeof("m-b-rubble")];
	strcpy(mUnknown30, "m-b-rubble");
	mUnknownC0 = 0;
	mAirShields = 0;
	mGroundShields = 0;
	mUnknownD4 = 0;
	mAirShieldRegeneration = 0;
	mGroundShieldRegeneration = 0;
	mThreat = 1.0f;
	mImportance = 1.0f;
	mBodyMotion = 0;
	mLegSpeed = 200.0f;
	mLegPlacementArea = 1.0f;
	mStandingLegPlacementArea = 0.05f;
	mMaxLegsLifted = 2;
	mParachute = 0;
	mUnknown104 = 0;
	mUnknown108 = 0;
	mUnknown160 = 1.0f;
	mUnknown110 = 0;
	mBounceFactor = 0;
	mSoundMaterial = 0;
	mMinThrusterHeight = 3.0f;
	mMaxThrusterHeight = 12.0f;
	mMaxTargetRange = 1000.0f;
	mMinAltitude = 4.0f;
	mExplosion = -1;
	mSmallExplosion = -1;
	mStompExplosion = -1;
	mMaxYaw = PI * 2;
	mUnknownDC = PI * 2;
	mParticles[2] = NULL;
	mParticles[3] = NULL;
	mParticles[4] = NULL;
	mParticles[6] = NULL;
	mParticles[7] = NULL;
	mParticles[5] = NULL;
	mParticles[1] = NULL;
	mParticles[8] = NULL;
	mParticles[9] = NULL;
	mParticles[10] = NULL;
	mUnknown118 = 0;
	mUnknown11C = 0;
	mUnknown120 = 0;
	mUnknown124 = 0;
	for (SINT i = 0; i < 7; i++)
		mAttackPriority[i] = 1.0f;
	mBuildingNumber = 0;
	mBuildingMeshType = 0;
	mStrafeChange = 0;
	mUnknownBC = 0;
	mDetonationDelay = 0;
	mUnknown10C = 0;
	mUnknown128 = 0;
	mUnknown12C = 0;
	mShatter = 0;
	mUnknown198 = 0;
	mSweeping = 0;
	mUnknown134 = 0;
	mIgnoreThreats = 0;
	mUnknown13C = 0;
	mUnknown1A0 = 0;
	mFenrir = 0;
	mEffectNames[0] = NULL;
	mEffectNames[1] = NULL;
	mEffectNames[2] = NULL;
	mEffectNames[3] = NULL;
	mEffectNames[4] = NULL;
	mEffectNames[5] = NULL;
	mEffectNames[6] = NULL;
	mEffectNames[7] = NULL;
	mEffectNames[8] = NULL;
	mEffectNames[9] = NULL;
	mEffectNames[10] = NULL;
	mEffectNames[11] = NULL;
	mEffectNames[12] = NULL;
	mIcon = -1;
	mCanBeLocked = TRUE;
}

inline CUnitData::CUnitData(char *name)
{
	SINT	len = 0;
	while (name[len])
		len++;
#line 1623
	mName = new(MT_UNIT_DATA) char[len + 1];
	SINT	i;
	for (i = 0; name[i]; i++)
		mName[i] = name[i];
	mName[i] = 0;

	Init();

	if (stricmp("Fenrir Main Gun", name) == 0 || stricmp("Fenrir", name) == 0)
		mFenrir = TRUE;
}

#line 1729
inline void CUnitData::SetMesh(char *name)
{
	if (name)
	{
		delete [] mUnknown2c;
		mUnknown2c = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mUnknown2c, name);
	}
}

#line 1750
inline void CUnitData::SetNoise(char *name)
{
	if (name)
	{
		delete [] mEffectNames[11];
		mEffectNames[11] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[11], name);
	}
}

#line 1762
inline void CUnitData::SetSpawnNoise(char *name)
{
	if (name)
	{
		delete [] mEffectNames[12];
		mEffectNames[12] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[12], name);
	}
}

#line 1774
inline void CUnitData::SetTrails(char *name)
{
	if (name)
	{
		delete [] mEffectNames[2];
		mEffectNames[2] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[2], name);
	}
}

#line 1786
inline void CUnitData::SetThrusters(char *name)
{
	if (name)
	{
		delete [] mEffectNames[3];
		mEffectNames[3] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[3], name);
	}
}

#line 1798
inline void CUnitData::SetSmoke(char *name)
{
	if (name)
	{
		delete [] mEffectNames[4];
		mEffectNames[4] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[4], name);
	}
}

#line 1810
inline void CUnitData::SetDust(char *name)
{
	if (name)
	{
		delete [] mEffectNames[5];
		mEffectNames[5] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[5], name);
	}
}

#line 1822
inline void CUnitData::SetEffect94(char *name)
{
	if (name)
	{
		delete [] mEffectNames[6];
		mEffectNames[6] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[6], name);
	}
}

#line 1834
inline void CUnitData::SetVent(char *name)
{
	if (name)
	{
		delete [] mEffectNames[7];
		mEffectNames[7] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[7], name);
	}
}

#line 1846
inline void CUnitData::SetAfterburner(char *name)
{
	if (name)
	{
		delete [] mEffectNames[8];
		mEffectNames[8] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[8], name);
	}
}

#line 1858
inline void CUnitData::SetShieldEffect(char *name)
{
	if (name)
	{
		delete [] mEffectNames[0];
		mEffectNames[0] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[0], name);
	}
}

#line 1870
inline void CUnitData::SetWaterWake(char *name)
{
	if (name)
	{
		delete [] mEffectNames[9];
		mEffectNames[9] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[9], name);
	}
}

#line 1882
inline void CUnitData::SetGroundWake(char *name)
{
	if (name)
	{
		delete [] mEffectNames[10];
		mEffectNames[10] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[10], name);
	}
}

#line 1894
inline void CUnitData::SetUnknown30(char *name)
{
	if (name)
	{
		delete [] mUnknown30;
		mUnknown30 = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mUnknown30, name);
	}
}

#line 1906
inline void CUnitData::SetActivationEffect(char *name)
{
	if (name)
	{
		delete [] mEffectNames[1];
		mEffectNames[1] = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mEffectNames[1], name);
	}
}

inline void CUnitData::CopyFrom(CUnitData *other)
{
	if (other)
	{
		mBehaviour = other->mBehaviour;
		SetMesh(other->mUnknown2c);
		SetUnknown30(other->mUnknown30);
		SetNoise(other->mEffectNames[11]);
		SetSpawnNoise(other->mEffectNames[12]);
		SetShieldEffect(other->mEffectNames[0]);
		mExplosion = other->mExplosion;
		mSmallExplosion = other->mSmallExplosion;
		mStompExplosion = other->mStompExplosion;
		mUnknownC0 = other->mUnknownC0;
		mAirShields = other->mAirShields;
		mGroundShields = other->mGroundShields;
		mUnknownD4 = other->mUnknownD4;
		mAirShieldRegeneration = other->mAirShieldRegeneration;
		mGroundShieldRegeneration = other->mGroundShieldRegeneration;
		mThreat = other->mThreat;
		mImportance = other->mImportance;
		mParachute = other->mParachute;
		mUnknown104 = other->mUnknown104;
		mUnknown110 = other->mUnknown110;
		mUnknown108 = other->mUnknown108;
		mUnknown160 = other->mUnknown160;
		mBodyMotion = other->mBodyMotion;
		mLegSpeed = other->mLegSpeed;
		mLegPlacementArea = other->mLegPlacementArea;
		mStandingLegPlacementArea = other->mStandingLegPlacementArea;
		mMaxLegsLifted = other->mMaxLegsLifted;
		mUnknown118 = other->mUnknown118;
		mUnknown11C = other->mUnknown11C;
		mUnknown120 = other->mUnknown120;
		mUnknown124 = 0;
		mBounceFactor = other->mBounceFactor;
		mSoundMaterial = other->mSoundMaterial;
		mUnknown10C = other->mUnknown10C;
		mMinThrusterHeight = other->mMinThrusterHeight;
		mMaxThrusterHeight = other->mMaxThrusterHeight;
		mMaxTargetRange = other->mMaxTargetRange;
		mMinAltitude = other->mMinAltitude;
		mUnknownBC = other->mUnknownBC;
		mMaxYaw = other->mMaxYaw;
		mUnknownDC = other->mUnknownDC;
		mStrafeChange = other->mStrafeChange;
		mDetonationDelay = other->mDetonationDelay;
		mIcon = other->mIcon;
		SetTrails(other->mEffectNames[2]);
		SetThrusters(other->mEffectNames[3]);
		SetSmoke(other->mEffectNames[4]);
		SetEffect94(other->mEffectNames[6]);
		SetVent(other->mEffectNames[7]);
		SetDust(other->mEffectNames[5]);
		SetActivationEffect(other->mEffectNames[1]);
		SetAfterburner(other->mEffectNames[8]);
		SetWaterWake(other->mEffectNames[9]);
		SetGroundWake(other->mEffectNames[10]);
		mUnknown128 = other->mUnknown128;
		mUnknown12C = other->mUnknown12C;
		mShatter = other->mShatter;
		mUnknown198 = other->mUnknown198;
		mCanBeLocked = other->mCanBeLocked;
		mSweeping = other->mSweeping;
		mUnknown134 = other->mUnknown134;
		mIgnoreThreats = other->mIgnoreThreats;
		mUnknown13C = other->mUnknown13C;
		mUnknown1A0 = other->mUnknown1A0;

		// Retail never adds the copied weapons and spawners, and copies the spawners' first two fields from the
		// finished weapon loop's pointer (00433bc1..00433c3d).
		CUnitWeapon	*weapon;
		for (weapon = other->mWeapons.First(); weapon; weapon = other->mWeapons.Next())
		{
#line 2009
			CUnitWeapon	*w = new(MT_UNIT_DATA) CUnitWeapon;
			*w = *weapon;
		}
		CSpawnerReference	*spawner;
		for (spawner = other->mSpawners.First(); spawner; spawner = other->mSpawners.Next())
		{
#line 2020
			CSpawnerReference	*s = new(MT_UNIT_DATA) CSpawnerReference;
			s->mSpawnerIndex = weapon->mIndex;
			s->mUnknown04 = weapon->mUnknown04;
			s->mMount = spawner->mMount;
		}
		CUnitComponentReference	*component;
		for (component = other->mComponents.First(); component; component = other->mComponents.Next())
		{
#line 2031
			CUnitComponentReference	*c = new(MT_UNIT_DATA) CUnitComponentReference;
			c->mUnknown04 = component->mUnknown04;
			c->mIndex = component->mIndex;
			mComponents.Append(c);
		}

		for (SINT i = 0; i < 7; i++)
			mAttackPriority[i] = other->mAttackPriority[i];
	}
}

inline void UPhysicsManager::AddUnit(char *name)
{
#line 2417
	CUnitData	*unit = new(MT_UNIT_DATA) CUnitData(name);

	mUnits->Append(unit);
}

inline CWeaponData::CWeaponData(char *name)
{
	SINT	len = 0;
	while (name[len])
		len++;
#line 1136
	mName = new(MT_PHYSICS) char[len + 1];
	SINT	i;
	for (i = 0; name[i]; i++)
		mName[i] = name[i];
	mName[i] = 0;

	mChargeRate = 2.0f;
	for (SINT j = 0; j < 5; j++)
		mChargeModes[j] = -1;
	mAmmoStore = 0;
	mConsumption = 1.0f;
	mIconName = NULL;
	mAdjustAim = TRUE;
	mAllowMovement = TRUE;
	mSmart = FALSE;
	mZoomMode = 0;
	mPlacement = 0;
	mLanguageName = 0xfe0572;
	mRating[0] = 0;
	mRating[1] = 0;
	mRating[2] = 0;
}

inline void CWeaponData::SetIconName(char *name)
{
	if (name)
	{
		delete [] mIconName;
		mIconName = NULL;
		SINT	len = 0;
		while (name[len])
			len++;
		SINT	size = len + 1;
#line 1210
		mIconName = new(MT_PHYSICS) char[size];
		for (SINT i = 0; i < size; i++)
			mIconName[i] = name[i];
	}
}

inline void UPhysicsManager::AddWeapon(char *name)
{
#line 2396
	CWeaponData	*weapon = new(MEMTYPE_WEAPON) CWeaponData(name);

	mWeapons->Append(weapon);
}

inline CWeaponModeData::CWeaponModeData(char *name)
{
	SINT	len = 0;
	while (name[len])
		len++;
#line 732
	mName = new(MT_PHYSICS) char[len + 1];
	SINT	i;
	for (i = 0; name[i]; i++)
		mName[i] = name[i];
	mName[i] = 0;

	mInaccuracy = 0;
	mReloadTime = 0;
	mBurstSize = 1;
	mVolleySize = 1;
	mBurstDelay = 0;
	mRound = NULL;
	mParticleA = NULL;
	mParticleB = NULL;
	mClipName = NULL;
	mSoundA = NULL;
	mSoundB = NULL;
	mSoundC = NULL;
	mMinRange = 0;
	mMaxRange = 100.0f;
	mUnknown6C = -10.0f;
	mUnknown70 = 10000.0f;
	mMinDeflection = 0;
	mMaxDeflection = 0;
	mYawTolerance = 0.5f;
	mPower = 0;
	mMaxLocks = 5;
	mUnknown88 = 0;
	mUnknown8C = 0;
	mLockTime = 0;
	mLockDeflection = 0;
	mLockUnit = 0;
	mLockMode = 0;
	mLockRange = 40.0f;
	mLockRadius = 10.0f;
	mTrack = FALSE;
	mPredictive = FALSE;
	mSoundPerBurst = TRUE;
	mMuzzleLight = -1;
	mMuzzleLightRadius = 0;
	mParticleAName = NULL;
	mParticleBName = NULL;
	mSoundAName = NULL;
	mSoundBName = NULL;
	mSoundCName = NULL;
}

#line 831
inline void CWeaponModeData::SetMuzzleEffect(char *name)
{
	if (name)
	{
		delete [] mParticleAName;
		mParticleAName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mParticleAName, name);
	}
}

#line 842
inline void CWeaponModeData::SetPreFireEffect(char *name)
{
	if (name)
	{
		delete [] mParticleBName;
		mParticleBName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mParticleBName, name);
	}
}

#line 853
inline void CWeaponModeData::SetClip(char *name)
{
	if (name)
	{
		delete [] mClipName;
		mClipName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mClipName, name);
	}
}

#line 864
inline void CWeaponModeData::SetLaunchSound(char *name)
{
	if (name)
	{
		delete [] mSoundAName;
		mSoundAName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mSoundAName, name);
	}
}

#line 875
inline void CWeaponModeData::SetPreFireSound(char *name)
{
	if (name)
	{
		delete [] mSoundBName;
		mSoundBName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mSoundBName, name);
	}
}

#line 886
inline void CWeaponModeData::SetPostFireSound(char *name)
{
	if (name)
	{
		delete [] mSoundCName;
		mSoundCName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mSoundCName, name);
	}
}

inline void UPhysicsManager::AddWeaponMode(char *name)
{
#line 2389
	CWeaponModeData	*mode = new(MEMTYPE_WEAPONMODE) CWeaponModeData(name);

	mWeaponModes->Append(mode);
}

// Sets one entry of the launch sequence by its position (1-based), growing the sequence to hold it.
inline void CWeaponModeData::SetLaunchSequence(SINT index, SINT value)
{
	while (index > mSet4c.Size())
	{
#line 997
		SINT	*entry = new(MT_PHYSICS) SINT;
		*entry = 0;
		mSet4c.Append(entry);
	}
	index--;
	ListIterator<void>	entries(&mSet4c);
	SINT	i = 0;
	for (SINT *entry = (SINT *)entries.First(); entry; i++, entry = (SINT *)entries.Next())
	{
		if (i == index)
		{
			*entry = value;
			return;
		}
	}
}

// The same for a launch angle pair.
inline void CWeaponModeData::SetLaunchAngle(SINT index, float a, float b)
{
	while (index > mSet5c.Size())
	{
#line 1029
		CLaunchAngle	*entry = new(MT_PHYSICS) CLaunchAngle;
		entry->mA = 0;
		entry->mB = 0;
		mSet5c.Append(entry);
	}
	index--;
	ListIterator<void>	entries(&mSet5c);
	SINT	i = 0;
	for (CLaunchAngle *entry = (CLaunchAngle *)entries.First(); entry; i++, entry = (CLaunchAngle *)entries.Next())
	{
		if (i == index)
		{
			entry->mA = a;
			entry->mB = b;
			return;
		}
	}
}

inline CRoundData::CRoundData(char *name)
{
	SINT	len = 0;
	while (name[len])
		len++;
#line 472
	mName = new(MT_PHYSICS) char[len + 1];
	SINT	i;
	for (i = 0; name[i]; i++)
		mName[i] = name[i];
	mName[i] = 0;

	if (strcmp("Stream Laser", mName) == 0)
		mStreamLaser = TRUE;
	else
		mStreamLaser = FALSE;
	if (strcmp("Gill-M Breath", mName) == 0)
		mGillMBreath = TRUE;
	else
		mGillMBreath = FALSE;

	mUnknown1C = 0;
	mRearm = 0;
	mLifeSpan = 0;
	mUnknown28 = 0;
	mSpeed = 0;
	mBounceFactor = 0;
	mSeekDelay = 0;
	mSeekAngle = 0.785f;
	mSeekTerminationTime = 1000.0f;
	mWiggle = 0;
	mRadius = 0;
	mPassiveCollision = 0;
	mLength = 0;
	mGravity = 0;
	mUnknown58 = 0;
	mCanGoUnderWater = 0;
	mGroundHugging = 0;
	mUnknown6C = FALSE;
	mUnknown70 = FALSE;
	mUnknown48 = 0;
	mUnknown0c = NULL;
	mParticleA = NULL;
	mParticleB = NULL;
	mUnknown08 = NULL;
	mFlak = FALSE;
	mBeam = FALSE;
	mWeirdoSeek = FALSE;
	mSmart = FALSE;
	mSoundMaterial = 0;
	mProximity = 0;
	mJumps = 0;
	mUnknown90 = 10.0f;
	mJumpDelay = 0;
	mExplode = FALSE;
	mUnknown60 = FALSE;
	mTreeCollision = 0;
	mParticleAName = NULL;
	mParticleBName = NULL;
}

inline void CRoundData::SetMesh(char *name)
{
	if (name)
	{
		delete [] mUnknown0c;
		mUnknown0c = NULL;
		SINT	len = 0;
		while (name[len])
			len++;
		SINT	size = len + 1;
#line 572
		mUnknown0c = new(MT_PHYSICS) char[size];
		for (SINT i = 0; i < size; i++)
			mUnknown0c[i] = name[i];
	}
}

#line 580
inline void CRoundData::SetEffect(char *name)
{
	if (name)
	{
		delete [] mParticleAName;
		mParticleAName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mParticleAName, name);
	}
}

#line 591
inline void CRoundData::SetWaterEffect(char *name)
{
	if (name)
	{
		delete [] mParticleBName;
		mParticleBName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mParticleBName, name);
	}
}

#line 602
inline void CRoundData::SetExplosion(char *name)
{
	if (name)
	{
		delete [] mUnknown08;
		mUnknown08 = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mUnknown08, name);
	}
}

inline void CRoundData::CopyFrom(CRoundData *other)
{
	if (other)
	{
		mUnknown1C = other->mUnknown1C;
		mRearm = other->mRearm;
		mLifeSpan = other->mLifeSpan;
		mUnknown28 = other->mUnknown28;
		mSpeed = other->mSpeed;
		mBounceFactor = other->mBounceFactor;
		mSeekDelay = other->mSeekDelay;
		mSeekAngle = other->mSeekAngle;
		mWiggle = other->mWiggle;
		mRadius = other->mRadius;
		mSeekTerminationTime = other->mSeekTerminationTime;
		mCanGoUnderWater = other->mCanGoUnderWater;
		mGroundHugging = other->mGroundHugging;
		mUnknown6C = other->mUnknown6C;
		mUnknown70 = other->mUnknown70;
		mPassiveCollision = other->mPassiveCollision;
		mLength = other->mLength;
		mGravity = other->mGravity;
		mUnknown58 = other->mUnknown58;
		mUnknown48 = other->mUnknown48;
		mSmart = other->mSmart;
		SetEffect(other->mParticleAName);
		SetWaterEffect(other->mParticleBName);
		SetExplosion(other->mUnknown08);
		SetMesh(other->mUnknown0c);
		mFlak = other->mFlak;
		mBeam = other->mBeam;
		mWeirdoSeek = other->mWeirdoSeek;
		mSoundMaterial = other->mSoundMaterial;
		mProximity = other->mProximity;
		mJumps = other->mJumps;
		mUnknown90 = other->mUnknown90;
		mJumpDelay = other->mJumpDelay;
		mExplode = other->mExplode;
		mUnknown60 = other->mUnknown60;
		mTreeCollision = other->mTreeCollision;
	}
}

inline void UPhysicsManager::AddRound(char *name)
{
#line 2382
	CRoundData	*round = new(MT_ROUND_DATA) CRoundData(name);

	mRounds->Append(round);
}

inline CSpawnerData::CSpawnerData(char *name)
{
	SINT	len = 0;
	while (name[len])
		len++;
#line 357
	mName = new(MT_PHYSICS) char[len + 1];
	SINT	i;
	for (i = 0; name[i]; i++)
		mName[i] = name[i];
	mName[i] = 0;

	mDelay = 0;
	mSeekDelay = 0;
	mAmount = 0;
	mSquadSize = 1;
	mSquadDelay = 0;
	mRecall = FALSE;
	mInfinite = 0;
	mConditions = 7;
	mMinRange = 0;
	mMaxRange = 100.0f;
	mPreSpawnDelay = 0;
	mPostSpawnDelay = 0;
	mThingName = NULL;
}

inline void CSpawnerData::SetThingName(char *name)
{
	if (name)
	{
		delete [] mThingName;
		mThingName = NULL;
		SINT	len = 0;
		while (name[len])
			len++;
		SINT	size = len + 1;
#line 404
		mThingName = new(MT_PHYSICS) char[size];
		for (SINT i = 0; i < len + 1; i++)
			mThingName[i] = name[i];
	}
}

// Retail copies only when this spawner's own Delay is set, and reads the other spawner unchecked (0043a... ).
inline void CSpawnerData::CopyFrom(CSpawnerData *other)
{
	if (mDelay != 0.0f)
	{
		mDelay = other->mDelay;
		mSeekDelay = other->mSeekDelay;
		mAmount = other->mAmount;
		mSquadSize = other->mSquadSize;
		mSquadDelay = other->mSquadDelay;
		mRecall = other->mRecall;
		mInfinite = other->mInfinite;
		mConditions = other->mConditions;
		mMinRange = other->mMinRange;
		mMaxRange = other->mMaxRange;
		mPreSpawnDelay = other->mPreSpawnDelay;
		mPostSpawnDelay = other->mPostSpawnDelay;
		SetThingName(other->mThingName);
	}
}

inline CExplosionData::CExplosionData(char *name)
{
	SINT	len = 0;
	while (name[len])
		len++;
#line 1244
	mName = new(MT_PHYSICS) char[len + 1];
	SINT	i;
	for (i = 0; name[i]; i++)
		mName[i] = name[i];
	mName[i] = 0;

	mParticleA = NULL;
	mParticleB = NULL;
	mParticleC = NULL;
	mParticleD = NULL;
	mRadius = 0;
	mDamage = 0;
	mVolumetric = 1;
	mTime = 10;
	mSoundA = NULL;
	mSoundB = NULL;
	mSmart = 0;
	mLight = -1;
	mOriented = 0;
	mParticleAName = NULL;
	mParticleBName = NULL;
	mParticleCName = NULL;
	mParticleDName = NULL;
	mSoundAName = NULL;
	mSoundBName = NULL;
}

#line 1305
inline void CExplosionData::SetAirEffect(char *name)
{
	if (name)
	{
		delete [] mParticleAName;
		mParticleAName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mParticleAName, name);
	}
}

#line 1316
inline void CExplosionData::SetWaterEffect(char *name)
{
	if (name)
	{
		delete [] mParticleBName;
		mParticleBName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mParticleBName, name);
	}
}

#line 1327
inline void CExplosionData::SetGroundEffect(char *name)
{
	if (name)
	{
		delete [] mParticleCName;
		mParticleCName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mParticleCName, name);
	}
}

#line 1338
inline void CExplosionData::SetUnitEffect(char *name)
{
	if (name)
	{
		delete [] mParticleDName;
		mParticleDName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mParticleDName, name);
	}
}

#line 1349
inline void CExplosionData::SetSound(char *name)
{
	if (name)
	{
		delete [] mSoundAName;
		mSoundAName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mSoundAName, name);
	}
}

#line 1360
inline void CExplosionData::SetWaterSound(char *name)
{
	if (name)
	{
		delete [] mSoundBName;
		mSoundBName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mSoundBName, name);
	}
}

inline void CExplosionData::CopyFrom(CExplosionData *other)
{
	if (other)
	{
		SetAirEffect(other->mParticleAName);
		SetGroundEffect(other->mParticleCName);
		SetWaterEffect(other->mParticleBName);
		SetUnitEffect(other->mParticleDName);
		mRadius = other->mRadius;
		mDamage = other->mDamage;
		mVolumetric = other->mVolumetric;
		SetSound(other->mSoundAName);
		SetWaterSound(other->mSoundBName);
		mSmart = other->mSmart;
		mLight = other->mLight;
		mOriented = other->mOriented;
	}
}

inline void UPhysicsManager::AddSpawnerData(char *name)
{
#line 2403
	CSpawnerData	*spawner = new(MEMTYPE_SPAWNER) CSpawnerData(name);

	mSpawners->Append(spawner);
}

inline void UPhysicsManager::AddExplosion(char *name)
{
#line 2410
	CExplosionData	*explosion = new(MEMTYPE_EXPLOSION) CExplosionData(name);

	mExplosions->Append(explosion);
}

inline CHazardData::CHazardData(char *name)
{
#line 193
	mName = new(MT_PHYSICS) char[strlen(name) + 1];
	strcpy(mName, name);
	mRadius = 1.0f;
	mDamage = 0;
	mParticle = NULL;
	mSound = NULL;
	mParticleName = NULL;
	mSoundName = NULL;
}

#line 223
inline void CHazardData::SetEffect(char *name)
{
	if (name)
	{
		delete [] mParticleName;
		mParticleName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mParticleName, name);
	}
}

#line 236
inline void CHazardData::SetNoise(char *name)
{
	if (name)
	{
		delete [] mSoundName;
		mSoundName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mSoundName, name);
	}
}

inline CFeatureData::CFeatureData(char *name)
{
#line 265
	mName = new(MT_PHYSICS) char[strlen(name) + 1];
	strcpy(mName, name);
	mLife = 0;
	mInvincible = FALSE;
	mUnknown14 = 0;
	mMeshName = NULL;
	mExplosion = -1;
	mSound = NULL;
	mSoundName = NULL;
	mIcon = -1;
}

inline void CFeatureData::SetMesh(char *name)
{
	if (name)
	{
		delete [] mMeshName;
		mMeshName = NULL;
		SINT	len = 0;
		while (name[len])
			len++;
		SINT	size = len + 1;
#line 313
		mMeshName = new(MT_PHYSICS) char[size];
		for (SINT i = 0; i < size; i++)
			mMeshName[i] = name[i];
	}
}

#line 321
inline void CFeatureData::SetNoise(char *name)
{
	if (name)
	{
		delete [] mSoundName;
		mSoundName = new(MT_PHYSICS) char[strlen(name) + 1];
		strcpy(mSoundName, name);
	}
}

inline void UPhysicsManager::AddComponentData(char *name)
{
#line 2424
	CUnitData	*component = new(MEMTYPE_COMPONENT) CUnitData(name);

	mComponents->Append(component);
}

inline void UPhysicsManager::AddFeature(char *name)
{
#line 2431
	CFeatureData	*feature = new(MEMTYPE_FEATURE) CFeatureData(name);

	mFeatures->Append(feature);
}

inline void UPhysicsManager::AddHazard(char *name)
{
#line 2438
	CHazardData	*hazard = new(MEMTYPE_FEATURE) CHazardData(name);

	mHazards->Append(hazard);
}

// The data destructors, defined after the classes. ClearAndFreeAll needs six of them out of line, and they come out in
// the order of these definitions: retail 00510e60 CFeatureData, 00510eb0 CRoundData, 00510f10 CWeaponModeData, 00511040
// CWeaponData, 00511070 CExplosionData, 005110f0 CUnitData (placement evidence). WITNESS-PLACED: CHazardData's destructor
// and CUnitPair's constructor (below) have no placement evidence of their own; they stay inline everywhere, and defining
// them here too keeps the compile-state witnesses CFEPGoodies::Render and CPDTimeline's rows (a witness scan).
inline CFeatureData::~CFeatureData()
{
	delete [] mMeshName;		mMeshName = NULL;
	delete [] mName;			mName = NULL;
	delete [] mSoundName;		mSoundName = NULL;
}

inline CRoundData::~CRoundData()
{
	delete [] mName;			mName = NULL;
	delete [] mUnknown08;		mUnknown08 = NULL;
	delete [] mUnknown0c;		mUnknown0c = NULL;
	delete [] mParticleAName;	mParticleAName = NULL;
	delete [] mParticleBName;	mParticleBName = NULL;
}

inline CWeaponModeData::~CWeaponModeData()
{
	void	*item;
	while (item = mSet5c.First())
	{
		mSet5c.Remove(item);
		delete item;
	}
	while (item = mSet4c.First())
	{
		mSet4c.Remove(item);
		delete item;
	}
	delete [] mName;			mName = NULL;
	delete [] mClipName;		mClipName = NULL;
	delete [] mParticleAName;	mParticleAName = NULL;
	delete [] mParticleBName;	mParticleBName = NULL;
	delete [] mSoundAName;		mSoundAName = NULL;
	delete [] mSoundBName;		mSoundBName = NULL;
	delete [] mSoundCName;		mSoundCName = NULL;
}

inline CWeaponData::~CWeaponData()
{
	delete [] mName;		mName = NULL;
	delete [] mIconName;	mIconName = NULL;
}

inline CExplosionData::~CExplosionData()
{
	delete [] mName;			mName = NULL;
	delete [] mParticleAName;	mParticleAName = NULL;
	delete [] mParticleBName;	mParticleBName = NULL;
	delete [] mParticleCName;	mParticleCName = NULL;
	delete [] mParticleDName;	mParticleDName = NULL;
	delete [] mSoundAName;		mSoundAName = NULL;
	delete [] mSoundBName;		mSoundBName = NULL;
}

inline CUnitData::~CUnitData()
{
	delete [] mName;			mName = NULL;
	delete [] mUnknown2c;		mUnknown2c = NULL;
	delete [] mUnknown30;		mUnknown30 = NULL;

	DeleteEach(mWeapons);
	DeleteEach(mSpawners);
	DeleteEach(mComponents);

	ClearEffectNames();
	ClearPairCache();
}

inline CHazardData::~CHazardData()
{
	delete [] mName;			mName = NULL;
	delete [] mParticleName;	mParticleName = NULL;
	delete [] mSoundName;		mSoundName = NULL;
}

inline CUnitPair::CUnitPair(SINT a, SINT b) : mA(a), mB(b) {}

#endif
