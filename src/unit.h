// unit.h: CUnit (0x250 bytes) and the unit classes derived from it. Stuart's release has no unit headers.
//
// The hierarchy is the retail RTTI's (config/classes.tsv) and the sizes are UPhysicsManager::SpawnThing's
// allocations (0050df80: CMech 0x26c ... CWarspiteDome 0x2b8; CAirUnit's members end at 0x27c). CUnit adds
// slots 71..116 to CActor's 71 (vtable 005df998); CAirUnit adds 117 and CMech 117..119. The slots' names are
// Stuart's where his CBattleEngine overrides them (GetLaunchPosition, GetImportance, GetCurrentTarget,
// SetInfinateEnergy, IsAThreat, GetStealth, EnableWeapon, DisableWeapon, CanBeLocked); the rest are open and
// numbered, typed from their retail bodies. CUnit's constructor and destructor are out of line in unit.obj
// (004f7e90, 004f84e0). Every unit reports CUnit's class id (8); each class names itself.
//
// The inline members below are the ones WorldPhysicsManager.obj emits for SpawnThing's vtables (0050e860..
// 0050f440); the retail copies of the others (for example CUnit's 00405dc0..00405eb0, 004175c0..00417630) are
// declared here without bodies until the objects that emit them are matched.

#ifndef UNIT_H
#define UNIT_H

#include "actor.h"
#include "ParticleManager.h"
#include "SPtrSet.h"
#include "WorldPhysicsManager.h"
#include "activereader.h"
#include "UnitAI.h"

class CWeapon;
class CParticleDescriptor;

// Stuart's name for the unit event 4002 (CBattleEngine::HandleEvent 0040c1a5).
// Emitter types for GetEmitter; CUnit 004fc6e0 maps each to its mesh emitter name (23 "Engine", 26 "Thruster").
// Stuart's names; the values are CBattleEngine's pushes (0040e9d2, 0040ecea).
enum EUnitEvent
{
	INCOMING_MISSILE_WARNING = 4002
};

// A unit class: it names itself and keeps CUnit's class id.
#define DECLARE_UNIT_CLASS(name, super)									\
	class name : public super											\
	{																	\
	public:																\
		typedef super SUPERTYPE;										\
		virtual char	*_GetClassName() { return #name; }

DECLARE_THING_CLASS(CUnit, CActor)
public:
	CUnit();																// 004f7e90
	virtual			~CUnit();												// 004f84e0

	virtual void	HandleEvent(CEvent *event);								// 0 (004f9820)
	virtual void	Shutdown();												// 2 (004f95d0)
	virtual void	Init(CInitThing *init);									// 9 (004f86d0)
	virtual float	GetMaxVelocity() { return 0.2f; }						// 15 (unit.obj's copy 004f84b0)
	virtual void	TeleportOrientation(const FMatrix &orientation);		// 21 (004fe4a0)
	virtual void	Activate();												// 22 (004fd6a0)
	virtual void	Deactivate();											// 23 (004fd700)
	virtual	void	SetThingType(ULONG t);									// 38 (004fcdc0)
	virtual void	Hit(CThing *other_thing, CCollisionReport *report);		// 39 (004fcc30)
	virtual void	Damage(float amount, CThing *inByThis, BOOL inDamageShields = TRUE, int mesh_part_no = -1);	// 40
	virtual BOOL	ObeyGravity();											// 46 (004fdc90)
	virtual BOOL	StartDieProcess();										// 50 (004fd140)
	virtual void	SetAIState(EAIState inAIState);							// 54 (004fdcb0)
	virtual void	AccumulateScore();										// 55 (004fdc20)
	virtual void	IncreaseThingCounter(CInitThing *inInit);				// 58 (004f9220)
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59 (004fdeb0)
	virtual void	GoToPoint(FVector point, BOOL inOverride = FALSE);		// 61 (004fce00)
	virtual void	SetSpawnScript(char *inScript);							// 63 (004fdd60)
	virtual void	Stop();													// 64 (004fcf00)
	virtual void	Move();													// 66 (004fa8d0)

	// IRenderableThing
	virtual	DWORD	GetRenderColour() { if (mAllegiance == 0) return 0xff008000; return (mAllegiance == 1) ? 0xff800000 : 0xff000080; }	// 4 (00405d90, inline in BattleEngine.obj)
	virtual int		GetShouldDoHitEffect(SINT mesh_part_no);				// 26 (004fd5e0)

	virtual void	Unknown71(float amount, CThing *by) {}				// 71 (00405db0, inline in BattleEngine.obj: nothing; CUnit::Damage calls it as the unit dies, 004f9ecd)
	virtual float	Unknown72() { return 0.25f; }							// 72 (00405dc0, inline in BattleEngine.obj)
	virtual float	Unknown73() { return 0.98f; }							// 73 (00405dd0, inline in BattleEngine.obj)
	virtual BOOL	Unknown74();											// 74 (004fc080)
	virtual void	GetLaunchPosition(CWeapon *inWeapon, int inIndex, FVector &outPos, FMatrix &outOrientation, BOOL inNeedOrientation = FALSE);	// 75 (004fc3c0)
	virtual BOOL	Unknown76() { if ((mUnknown168 || !mActive) && !(mFlags & TF_DYING)) return FALSE; return TRUE; }	// 76 (00405de0, inline in BattleEngine.obj)
	virtual void	Unknown77(CEulerAngles *cur, CEulerAngles *target, CEulerAngles *rate, FMatrix &out);	// 77 (004fa4b0: turns cur toward target)
	virtual float	GetMaxLife();											// 78 (004f9a10; Stuart's name, CBattleEngine's is 00406040)
	virtual float	GetThreat() { return mUnitData->mThreat; }				// 79 (004175c0; Stuart's name, CBattleEngine::Morph calls it 0040a701)
	virtual float	GetImportance() { return mUnitData->mImportance; }		// 80 (004175d0)
	virtual CUnit	*GetCurrentTarget() { if (mAI) return mAI->mTarget.ToRead(); return NULL; }	// 81 (004175e0)
	virtual void	Unknown82() { mUnknown1F0 = 1; }						// 82 (00405e10, inline in BattleEngine.obj)
	virtual void	Unknown83() { mUnknown1F0 = 0; }						// 83 (00405e20, inline in BattleEngine.obj)
	virtual BOOL	Unknown84();											// 84 (004fc000)
	virtual void	SetInfinateEnergy(BOOL val) { mInfinateEnergy = val; }	// 85 (00417600)
	virtual void	SetVulnerable(BOOL val) { mVulnerable = val; }			// 56 (00405e30, inline in BattleEngine.obj)
	virtual BOOL	GetVulnerable() { return mVulnerable; }					// 57 (00405e40, inline in BattleEngine.obj)
	virtual EAIState	GetAIState() { return mAIState; }					// 53 (00405e50, inline in BattleEngine.obj)
	virtual SINT	GetSoundMaterial() { return mUnitData->mSoundMaterial; }	// 43 (Building.obj emits it, 00417610)
	virtual	float	BounceFactor() { return mUnitData->mBounceFactor; }		// 47 (00417620)
	virtual BOOL	Unknown86();											// 86 (004fbcb0: fires the current weapon or spawner)
	virtual void	Unknown87();											// 87 (004fc170)
	virtual void	GetEmitter(SINT type, SINT index, FVector &outPos, FMatrix &outOrientation);	// 88 (004fc4e0: the unit data's cached emitters; Stuart's name, BattleEngine.cpp)
	virtual BOOL	IsAThreat();											// 89 (004fd440)
	virtual void	GetTargetablePos(FVector *out);							// 90 (004fd4d0; Stuart's name: CBattleEngine::UpdateAutoAim calls slot 90 where his source calls GetTargetablePos)
	virtual float	GetStealth();											// 91 (004bfc60: 0)
	virtual float	Unknown92();											// 92 (004fb270: +0x114's yaw)
	virtual void	Unknown93();											// 93 (00459990: nothing)
	virtual float	Unknown94() { return 2.0f; }							// 94 (00405e60, inline in BattleEngine.obj)
	virtual void	Unknown95();											// 95 (004f9260)
	virtual void	Unknown96();											// 96 (004f9490)
	virtual BOOL	Unknown97();											// 97 (004014a0: TRUE)
	virtual BOOL	Unknown98();											// 98 (004fda90)
	virtual BOOL	Unknown99();											// 99 (004fdc10, Unit.cpp)
	virtual void	Unknown100();											// 100 (004fdd00)
	virtual void	ConfirmedKill(CUnit *inKilled);							// 101 (004fe030: a pilot's radio line; Stuart's name, CBattleEngine's is 0040a560)
	virtual void	EnableWeapon(char *name);								// 102 (004fe2b0)
	virtual void	DisableWeapon(char *name);								// 103 (004fe310)
	virtual BOOL	CanBeLocked() { if (mUnitData) return mUnitData->mCanBeLocked; return TRUE; }	// 104 (00417630)
	virtual BOOL	OnScanner();											// 105 (004014a0: TRUE; HandleAutoAim calls +0x1a4 at 0040b9cb)
	virtual float	Unknown106();											// 106 (004bfc60: 0)
	virtual void	Unknown107(float a);									// 107 (004fe540)
	virtual BOOL	Unknown108();											// 108 (004014a0: TRUE)
	virtual BOOL	Unknown109() { return mUnknown168 == 0; }				// 109 (00405e70, inline in BattleEngine.obj)
	virtual FVector	Unknown110() { return FVector(0, 0, 0); }				// 110 (00405e80, inline in BattleEngine.obj: the zero vector)
	virtual float	Unknown111();											// 111 (004fe5c0)
	virtual float	Unknown112() { return 0.05f; }							// 112 (00405ea0, inline in BattleEngine.obj)
	virtual void	Unknown113(FVector *out) { *out = mPos; }				// 113 (00405eb0, inline in BattleEngine.obj: the position; an out-pointer, as CWeapon passes &origin, 005094e4)
	virtual void	Unknown114(float a);									// 114 (004014c0: nothing)
	virtual BOOL	Unknown115();											// 115 (004014a0: TRUE)
	virtual void	Unknown116();											// 116 (004fe5f0)

			void	DamageRandomly();										// 004f9430
			float	GetLife();												// 004f99f0
			float	GetEnergy() { return mEnergy; }
			float	GetLifeUnknown4f9a40();									// 004f9a40
			void	RemoveSpawner(class CSpawner *spawner);			// 004f9a60
			void	SetStateTimer(float delay);								// 004fc3a0
			void	GuideUnknown5(FVector v);								// 004fce40
			void	GuideUnknown6(FVector v);								// 004fce80
			void	GuideUnknown7(FVector v);								// 004fcec0
			void	ClearSpawners();										// 004fcfa0
			void	ReleaseChildren();										// 004fcfe0
			void	GetRenderUnknown90(FVector *out);						// 004fd500
			BOOL	AnyWeaponUnknown94();									// 004fd570
			BOOL	IsLiveUnit(CUnit *unit);								// 004fd5b0
			BOOL	AnyWeaponUnknown50a290();								// 004fd760
			BOOL	AnySpawnerUnknown4e4420();								// 004fd7a0
			BOOL	AllSpawnersReady();										// 004fd7e0
			BOOL	Unknown4fb5a0(CUnit *target);					// 004fb5a0: ret 4, tests unit slot 67, returns BOOL
			BOOL	CanFireAt(CUnit *target, BOOL a);						// 004fb500 (CDropship::CanStayOpen 004480fc)
			CActiveReader<CUnit>	*FindChild(SINT key);					// 004fd8d0
			SINT	GetUnitDataUnknown120();								// 004fda10
			void	SetSquadTarget(CUnit *unit);							// 004fda20
			BOOL	IsDeploying();											// 004fde10
			void	StartDeploying();										// 004fde30
			void	StartUndeploying();										// 004fde70
			void	EnableSpawner(char *name);								// 004fe390
			void	DisableSpawner(char *name);								// 004fe3f0
			class CNormalSquadUnknownD4	*GuideUnknown9();						// 004fe480
			void	SetSquad(CSquad *squad);								// 004fe500
			FVector	FindNearestSafeSide();									// 004fd910
			void	WeaponUnknown509140(SINT a, SINT b);					// 004fb650
			float	GetMinRange(FVector target);							// 004fb780
			float	GetMaxRange(FVector target);							// 004fb7e0
			float	GetWeaponUnknownA0_88();								// 004fbc90
			class COccupancyGrid	*GetOccupancyGrid();					// 004fd380
			void	SetAllegiance(SINT allegiance);							// 004fd830
			BOOL	IsTargetAlligence(SINT allegiance);						// 004fd3d0 (Stuart's name, BattleEngine.cpp)
			EAllegiance	GetAllegiance() { return (EAllegiance)mAllegiance; }	// Stuart's name; his Explode stores it in an EAllegiance
			CEulerAngles	&GetCurrentEulerOrientation() { return mCurrentOrientation; }	// Stuart's (BattleEngineJetPart.cpp); by reference: HandleGroundEffect copies only mYaw (004117d4)
			BOOL	Active()			{ return mActive; }					// Stuart's (BattleEngine.cpp)
			CSquad	*GetSquad()			{ return mSquad.ToRead(); }			// IScript::Attack (00536036)
			BOOL	HasNexus()			{ return mUnknown228; }				// Stuart's: set for a "nexus" part (Init 004f911f)
			BOOL	HasWeakPoint()		{ return mUnknown22C; }				// Stuart's: set for a "weakpoint" part (004f913f)
			BOOL	IsBig() { if (mUnitData) return mUnitData->mUnknown124; return FALSE; }	// 0040b8fe..0040b90e
			BOOL	IsLockable() { if (mUnitData) return mUnitData->mCanBeLocked; return TRUE; }	// 0040b938..0040b942; not virtual CanBeLocked
			// GetTargetablePos uses the virtual pointer declaration above; the former reference overload
			// was undefined and incorrectly emitted direct calls instead of slot 90 in HandleAutoAim.
			BOOL	Unknown4fb3d0(CUnit *target);							// 004fb3d0
			void	Unknown4fb840(CThing *target);							// 004fb840: picks the weapon or spawner to use on the target
			SINT	Unknown4fb670(CThing *target);							// 004fb670: 0 in range, 1 too far, 2 too near
			void	Unknown4fdad0(CThing *target);							// 004fdad0
			void	Unknown4fc6e0(SINT type, SINT index, FVector &outPos, FMatrix &outOrientation, BOOL a, BOOL b);	// 004fc6e0
			void	SpawnComponentEffects();								// 004fc220
			void	UpdateTimer();											// 004fa800
			void	Remove();												// 004fd040
			void	SpawnExplosion();										// 004fd230: the unit data's explosion, attached to the unit
			void	Unknown4fb280(class CScheduledEvent *event);			// 004fb280: UNIT_EVENT_4001, clamps the pitch at +0xec

	void			StartPlayingInitNoise();								// 004f99b0 (CGame::RestartLoopRunLevel 0046e086)

	static SINT		mGuideUpdateCounter;									// 00854d90 (CGame::Update 0046eb31)

	// The layout is CUnit's constructor's (004f7e90); the names are Stuart's where his CBattleEngine uses them.
	float			mUnknownE0;												// +0xe0: a turret's yaw, turned towards the target (Move 004fae3f..004fafe3)
	float			mUnknownE4;												// +0xe4: last frame's (Move 004fad0f)
	float			mUnknownE8;												// +0xe8: a barrel's pitch, turned towards +0xec (004fafef..)
	float			mUnknownEC;												// +0xec
	float			mUnknownF0;												// +0xf0: last frame's +0xe8
	float			mUnknownF4;												// +0xf4
	float			mLife;													// +0xf8: 1 at construction
	float			mEnergy;												// +0xfc (Stuart's; CGame::DrawGameStuff 004716de)
	float			mShields;												// +0x100 (Stuart's)
	float			mMaxShields;											// +0x104: Move caps the shields at it (004fa998)
	float			mShieldRegeneration;									// +0x108 (Stuart's; Move adds it each frame, 004fa986)
	SINT			mUnknown10C;											// +0x10c
	SINT			mUnknown110;											// +0x110
	CEulerAngles	mCurrentOrientation;											// +0x114
	CEulerAngles	mUnknown120;											// +0x120: Stop sets its yaw to +0x114's
	CEulerAngles	mUnknown12C;											// +0x12c
	SINT			mAllegiance;											// +0x138: 2 at construction (Stuart's CBattleEngine reads it)
	class CUnitAI	*GetAI() { return mAI; }
	SINT	GetDeployState() { return mDeployState; }
	CEulerAngles	&GetCurrentOrientation() { return mCurrentOrientation; }
	CUnitData	*GetUnitData() { return mUnitData; }
	void	SetUnknown174(float f) { mUnknown174 = f; }
	void	SetUnknown120(const CEulerAngles &e) { mUnknown120 = e; }
	void	SetUnknown14C(const FVector &v) { mUnknown14C = v; }
	class CUnitAI	*mAI;													// +0x13c
	CWeapon			*mCurrentWeapon;										// +0x140 (DisableWeapon clears it when it is the weapon disabled)
	CActiveReader<class CSpawner>	mCurrentSpawner;							// +0x144 (DisableSpawner clears it likewise)
	CActiveReader<class CSquad>	mSquad;										// +0x148: activated and deactivated with the unit
	FVector			mUnknown14C;											// +0x14c
	BOOL			mVulnerable;											// +0x15c (Set/GetVulnerable)
	BOOL			mInfinateEnergy;										// +0x160 (SetInfinateEnergy)
	CUnitData		*mUnitData;												// +0x164 (the inline members below read it)
	SINT			mUnknown168;											// +0x168
	float			mUnknown16C;											// +0x16c: a time
	float			mUnknown170;											// +0x170: eases towards +0x174 by 0.1 a frame (Move 004fa9e7)
	float			mUnknown174;											// +0x174
	class CDestructableSegmentsController	*mSegments;						// +0x178
	SPtrSet<CWeapon>	mWeapons;												// +0x17c
	SPtrSet<class CSpawner>	mSpawners;								// +0x18c
	SPtrSet<CActiveReader<CUnit> >	mChildren;								// +0x19c: readers of the units it made
	CParticleEffectLink	*mUnknown1AC;										// +0x1ac
	SINT			mUnknown1B0;											// +0x1b0
	SPtrSet<CParticleEffectLink>	mUnknown1B4;							// +0x1b4
	SPtrSet<CParticleEffectLink>	mUnknown1C4;							// +0x1c4
	SPtrSet<CParticleEffectLink>	mThrusters;							// +0x1d4 (Stuart's; CBattleEngine::Init fills it, 004052d6)
	BOOL			mThrustersActive;										// +0x1e4 (Stuart's; CBattleEngine 0040eb38)
	SINT			mUnknown1E8;											// +0x1e8
	SINT			mUnknown1EC;											// +0x1ec
	SINT			mUnknown1F0;											// +0x1f0
	void			*mUnknown1F4;											// +0x1f4 (CComponent's slot 105 tests it, 0050fca0)
	FVector			mUnknown1F8;											// +0x1f8
	class CGuide	*mGuide;												// +0x208: GoToPoint and Stop forward to it
	float			mUnknown20C;											// +0x20c
	EAIState		mAIState;												// +0x210 (GetAIState)
	BOOL			mActive;												// +0x214: 1 at construction (Activate, Deactivate)
	SINT			mUnknown218;											// +0x218
	SINT			mUnknown21C;											// +0x21c: 10 at construction
	SINT			mUnknown220;											// +0x220
	SINT			mUnknown224;											// +0x224: 004fb280 aims only when it is set
	BOOL			mUnknown228;											// +0x228: a nexus part (HasNexus)
	BOOL			mUnknown22C;											// +0x22c: a weak-point part (HasWeakPoint)
	SINT			mUnknown230;											// +0x230
	BOOL			mSaidHalfLife;											// +0x234: the radio lines Damage has played (004fa41d)
	BOOL			mSaidQuarterLife;										// +0x238 (004fa2f0)
	BOOL			mSaidDying;												// +0x23c (004fa1b7)
	float			mUnknown240;											// +0x240: when the next radio line may play (004fe0b1)
	SINT			mDeployState;											// +0x244: 3 deploying, 4 deployed, 5 undeploying; 1 and 2 are out
	SINT			mUnknown248;											// +0x248
	SINT			mUnknown24C;											// +0x24c: 1 at construction
};

//******************************************************************************************
// vtable 005e3778; constructor 0050f0a0: a particle effect link +0x250 and two sets.
DECLARE_UNIT_CLASS(CAirUnit, CUnit)
	virtual void	Shutdown();												// 2 (00402d30)
	virtual void	Init(CInitThing *init);									// 9 (00402ad0)
	virtual float	GetMaxVelocity() { return 13.0f; }						// 15
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_UNKNOWN_40000000 | THING_TYPE_AIR_UNIT); }	// 38
	virtual void	Damage(float amount, CThing *inByThis, BOOL inDamageShields = TRUE, int mesh_part_no = -1);	// 40 (004037a0)
	virtual float	Gravity();												// 45 (00403b60)
	virtual BOOL	StartDieProcess();										// 50 (00403690)
	virtual void	GoToPoint(FVector point, BOOL inOverride = FALSE);		// 61 (00403a90)
	virtual void	Move();													// 66 (00402fa0)
	virtual void	DeclareOnGround();										// 68 (00403730)
	virtual void	DeclareInWater();										// 69 (00403760)
	virtual BOOL	Unknown117();											// 117 (00403a50)

	// IRenderableThing
	virtual float	GetRenderImposterBias() { return 0.5f; }				// 23

			BOOL	IsBoxBelowSurface();									// 00402dd0 (CDropship::Move 004478a3)

	CParticleEffectLink	mEffect;											// +0x250
	SINT				mUnknown258;										// +0x258
	SPtrSet<CParticleEffectLink>	mTrails;								// +0x25c: one link per "Trail" emitter (Init 00402b72)
	SPtrSet<CParticleEffectLink>	mEngines;								// +0x26c: one link per "Engine" emitter (Init 00402c3a)
};

DECLARE_UNIT_CLASS(CBigAirUnit, CAirUnit)							// vtable 005e3524; constructor 0050ed80
	virtual void	Hit(CThing *other_thing, CCollisionReport *report);		// 39 (00403bf0)
};

// No vtable of its own in retail, though CGroundAttackAircraft and CDiveBomber inherit its Hit.
class CSmallAirUnit : public CAirUnit
{
public:
	typedef CAirUnit SUPERTYPE;
	virtual void	Hit(CThing *other_thing, CCollisionReport *report);		// 39 (00403ba0)
};

//******************************************************************************************
// The classes SpawnThing makes, and the base CGroundUnit, in retail's emission order (WorldPhysicsManager.obj).

DECLARE_UNIT_CLASS(CPlane, CSmallAirUnit)							// 0x288, vtable 005e1930
	virtual void	Init(CInitThing *init);									// 9
	virtual float	GetMaxVelocity() { return 15.0f; }						// 15
	virtual void	Hit(CThing *other_thing, CCollisionReport *report);		// 39
	virtual float	Gravity() { if (mFlags & TF_DYING) return 0.025f; return 0; }	// 45
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59
	virtual void	Stop();													// 64
	virtual void	Move();													// 66
	virtual void	DeclareOnGround();										// 68 (004d20a0)
	virtual void	DeclareInWater();										// 69 (0047bf60)
	virtual float	Unknown94() { return 5.0f; }							// 94

			void	OpenWings();											// 004d1f90: "wingopen" once the wings are shut
			void	CloseWings();											// 004d1fd0: "wingclose" once they are open

	SINT			mWingState;												// +0x27c: 0 launching, 1 shut, 2 opening, 3 closing, 4 open
	float			mLaunchTime;											// +0x280: its engines burn until then (Init 004d1aca)
	float			mDeathRoll;												// +0x284: +-0.8, which way it rolls as it falls (Init 004d1bdc)
};

DECLARE_UNIT_CLASS(CBomber, CBigAirUnit)								// 0x27c, vtable 005e2e20
	virtual void	Init(CInitThing *init);									// 9 (004160b0)
	virtual float	Gravity() { if (mFlags & TF_DYING) return 0.015f; return 0; }	// 45
	virtual void	Stop();													// 64 (00459990: nothing)
	virtual void	Move();													// 66 (004162e0)
	virtual void	DeclareOnGround();										// 68 (004d20a0)
	virtual void	DeclareInWater();										// 69 (0047bf60)
};

DECLARE_UNIT_CLASS(CGroundAttackAircraft, CSmallAirUnit)				// 0x288, vtable 005e2bcc (GroundAttackAircraft.obj)
	virtual void	Init(CInitThing *init);									// 9 (0047bbf0)
	virtual void	Hit(CThing *other_thing, CCollisionReport *report);		// 39 (00403ba0)
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59 (0047c040)
	virtual void	Move();													// 66 (0047beb0)
	virtual void	DeclareOnGround();										// 68 (004d20a0)
	virtual void	DeclareInWater();										// 69 (0047bf60)
	virtual float	Unknown73() { return 0.95f; }							// 73

#ifdef GROUNDATTACK_FULL	// GroundAttackAircraft.cpp's and GroundAttackAI.cpp's view; unit.h's other includers keep
						// the byte array (one more declaration here flips CGame::DrawDebugStuff's tie-break)
			void	OpenBay();												// 0047bfa0
			void	CloseBay();												// 0047bff0

	SINT			mBayState;												// +0x27c: as CDiveBomber's
	float			mPhase;													// +0x280
	float			mBob;													// +0x284
#else
	UBYTE			mUnknown27C[0x288 - 0x27c];
#endif
};

DECLARE_UNIT_CLASS(CGroundUnit, CUnit)								// vtable 005e32d4; constructor 0050ed10
	virtual void	Init(CInitThing *init);									// 9 (0047c730)
	virtual float	GetMoveMultiplier() { return 4.0f; }					// 24
	virtual void	InitCollisionSeekingThing(CInitCSThing *init);			// 35 (0047c8e0)
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_UNKNOWN_40000000 | THING_TYPE_GROUND_UNIT); }	// 38
	virtual BOOL	StartDieProcess();										// 50 (0047ce80)
	virtual void	Move();													// 66 (0047c970)
	virtual BOOL	IsOnGround();											// 67 (0047c8b0)

			void	DeactivateThrusters();									// 0047cea0 (Stuart's name for CBattleEngine's: each thruster killed, mThrustersActive cleared)

#ifdef GROUNDUNIT_CPP	// GroundUnit.cpp's view: CGroundUnit's own members (the derived classes' views start theirs at +0x250)
	float			mBob;													// +0x250: the hover's height offset
	float			mOldBob;												// +0x254
	BOOL			mUnknown258;											// +0x258: it bobs like a hovering unit
	float			mBobPhase;												// +0x25c
#endif
};

DECLARE_UNIT_CLASS(CGroundVehicle, CGroundUnit)						// 0x264, vtable 005e297c
	virtual void	Init(CInitThing *init);									// 9 (0047cfd0)
	virtual float	GetMaxVelocity() { return 3.5f; }						// 15
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_VEHICLE); }	// 38
	virtual	float	COfGHeight() { return 0.1f; }							// 48
	virtual BOOL	StartDieProcess();										// 50 (0041b590)
	virtual void	Move();													// 66 (0047d1c0)
	virtual BOOL	Unknown86();											// 86 (0047d3b0)
	virtual void	Unknown87();											// 87 (0047d420)
	virtual float	Unknown112() { return 0.5f; }							// 112

#ifdef MCGROUNDVEHICLE_CPP	// MCGroundVehicle.cpp's view; unit.h's other includers keep the byte array
	float			mUnknown250;											// +0x250: how far the hull sits down (MCGroundVehicle 00496f21)
	float			mUnknown254;											// +0x254: last frame's
	UBYTE			mUnknown258[0x260 - 0x258];
#else
	UBYTE			mUnknown250[0x260 - 0x250];
#endif
	UBYTE			mTiltsWithGround;										// +0x260: pitch and roll follow the ground (Move), unless its buggy controller does
	UBYTE			mUnknown261[0x264 - 0x261];
};

DECLARE_UNIT_CLASS(CMech, CGroundUnit)								// 0x26c, vtable 005e3074
	virtual void	Init(CInitThing *init);									// 9 (0049f820)
	virtual float	GetMaxVelocity() { return 5.8f; }						// 15
	virtual float	GetMoveMultiplier() { return 1.0f; }					// 24 (folded into 004de700)
	virtual void	InitCollisionSeekingThing(CInitCSThing *init);			// 35 (0049fb00)
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_VEHICLE | THING_TYPE_MECH); }	// 38
	virtual BOOL	StartDieProcess();										// 50 (004a00a0)
	virtual	BOOL	SetAnimMode(EAnimMode am, BOOL reset_frame, BOOL force_looped = FALSE);	// 60 (0049fba0)
	virtual void	Move();													// 66 (0049fc10)
	virtual void	Unknown71(float amount, CThing *by);					// 71 (0049fdb0)
	virtual void	GetTargetablePos(FVector *out);								// 90 (004a0130)
	virtual float	Unknown106();											// 106 (004a0120)
	virtual void	InitLegMotion(CInitThing *init);						// 117 (0049f940)
	virtual void	InitCockpit(CInitThing *init);							// 118 (0049fa30)
	virtual void	InitTargeting();										// 119 (0049faa0)

	UBYTE			mUnknown250[0x260 - 0x250];
	float			mCockpitHeight;											// +0x260: its "body" part's height over its position; a dying mech sinks it
	float			mSinkSpeed;												// +0x264
	float			mBodyDepth;												// +0x268: the "body" part's bounding box half-height
};

DECLARE_UNIT_CLASS(CInfantryUnit, CGroundUnit)						// 0x278, vtable 005e272c
	virtual void	Shutdown();												// 2 (00488f60)
	virtual void	Init(CInitThing *init);									// 9 (00488bb0)
	virtual float	GetMaxVelocity();										// 15 (00405e60: 2)
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_INFANTRY); }	// 38
	virtual float	GetMoveMultiplier() { return mMoveMultiplier; }			// 24
	virtual void	InitCollisionSeekingThing(CInitCSThing *init);			// 35
	virtual void	Hit(CThing *other_thing, CCollisionReport *report);		// 39
	virtual void	Damage(float amount, CThing *inByThis, BOOL inDamageShields = TRUE, int mesh_part_no = -1);	// 40
	virtual BOOL	StartDieProcess();										// 50
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59
	virtual	BOOL	SetAnimMode(EAnimMode am, BOOL reset_frame, BOOL force_looped = FALSE);	// 60
	virtual void	Stop();													// 64
	virtual void	Move();													// 66
	virtual void	DeclareOnGround();										// 68
	virtual BOOL	Unknown74();											// 74

	// IRenderableThing
	virtual int		GetImposterFrames();									// 19 (004dbc20)
	virtual EAnimMode	GetImposterAnimMode();								// 20 (004014a0: 1)
	virtual float	GetRenderImposterBias();								// 23 (00489ed0)
	virtual	BOOL	GetRequiresPolyBucket();								// 25 (00405930: FALSE)

	UBYTE				mUnknown250[0x260 - 0x250];
	float				mMoveMultiplier;									// +0x260: 4, or 1 for a parachutist (Init 00488c55..00488c71)
	float				mRemoveTime;										// +0x264: when it goes, 15 after it dies (StartDieProcess 00489b71)
	SINT				mUnknown268;										// +0x268: 18 when it stands to fire (Unknown74 00489077); Move counts it down
	BOOL				mKnockedDown;										// +0x26c: set when an explosion throws it (Damage 00489865)
	CParticleEffectLink	mEffect;											// +0x270
};

DECLARE_UNIT_CLASS(CCannon, CGroundUnit)								// 0x268, vtable 005e24dc
	virtual void	Shutdown();												// 2 (0041b??? shared with CSentinel)
	virtual void	Init(CInitThing *init);									// 9
	virtual float	GetMaxVelocity();										// 15 (004bfc60: 0)
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_EMPLACEMENT | THING_TYPE_STATIONARY); }	// 38
	virtual float	Gravity();												// 45 (004bfc60: 0)
	virtual BOOL	StartDieProcess();										// 50 (0041b590)
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59
	virtual void	Move();													// 66
	virtual void	GetTargetablePos(FVector *out);								// 90

#ifdef MCCANNON_CPP	// MCCannon.cpp's view; unit.h's other includers keep the byte array
	float			mUnknown250;											// +0x250: how far it sits down (MCCannon 0049565d)
	float			mUnknown254;											// +0x254: last frame's
	UBYTE			mUnknown258[0x268 - 0x258];
#elif defined(CANNON_CPP)	// Cannon.cpp's view
	float			mUnknown250;											// +0x250
	float			mUnknown254;											// +0x254
	BOOL			mAboveWater;											// +0x258: Init: not below the water level
	SINT			mUnknown25C;											// +0x25c
	SINT			mState;													// +0x260: 0 active, 1 inactive, 2 deactivating, 3 activating
	float			mLastTargetTime;										// +0x264: when the AI last had a target
#else
	UBYTE			mUnknown250[0x268 - 0x250];
#endif
};

DECLARE_UNIT_CLASS(CBoat, CGroundUnit)								// 0x26c, vtable 005e228c
	virtual void	HandleEvent(CEvent *event);								// 0
	virtual void	Init(CInitThing *init);									// 9
	virtual float	GetMaxVelocity() { return 3.5f; }						// 15
	virtual float	GetMoveMultiplier() { return 1.0f; }					// 24 (folded into 004de700)
	virtual	void	SetThingType(ULONG t) { CUnit::SetThingType(t | THING_TYPE_CAN_BE_WALKED_ON | THING_TYPE_NAVAL); }	// 38: skips CGroundUnit's bits
	virtual BOOL	ClipToGround();											// 44
	virtual	float	COfGHeight() { return 0.1f; }							// 48
	virtual BOOL	CanGoUnderWater();										// 49
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59
	virtual void	Move();													// 66
	virtual void	DeclareOnGround();										// 68
	virtual BOOL	Unknown76();											// 76
	virtual void	Unknown82();											// 82
	virtual void	Unknown83();											// 83
	virtual void	GetTargetablePos(FVector *out);								// 90
	virtual void	Unknown93();											// 93
	virtual void	Unknown96();											// 96

	UBYTE			mUnknown250[0x260 - 0x250];
	BOOL			mDeploying;												// +0x260: "deploying" has started
	BOOL			mLanded;												// +0x264: beached (Unknown93)
	SINT			mBeachCount;											// +0x268: frames its bow has been aground; it lands after 60
};

DECLARE_UNIT_CLASS(CCarrier, CBigAirUnit)							// 0x27c, vtable 005e2038
	virtual void	Init(CInitThing *init);									// 9
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_UNKNOWN_04000000 | THING_TYPE_CAN_BE_WALKED_ON); }	// 38
	virtual void	Stop();													// 64
	virtual void	Move();													// 66
};

// Its own object's; SpawnThing only makes one.
DECLARE_UNIT_CLASS(CBuilding, CUnit)									// 0x26c, vtable 005d8eb4
	virtual			~CBuilding();											// 1 (00417590)
	virtual void	HandleEvent(CEvent *event);								// 0 (00417df0)
	virtual void	Shutdown();												// 2 (00417870)
	virtual void	Init(CInitThing *init);									// 9 (00417190)
	virtual BOOL	GetCanBeStaticallyShadowed() { return TRUE; }			// 10 (the shared 004014a0)
	virtual CStaticShadow	*GetStaticShadow();								// 11 (00417680)
	virtual void	SetStaticShadow(CStaticShadow *shadow);					// 12 (00417690)
	virtual float	GetMaxVelocity() { return 0; }							// 15 (the shared 004bfc60)
	virtual void	StickToGround();										// 18
	virtual	void	InitRenderThing(CInitThing *init);						// 34
	virtual void	Render(DWORD flags = 0);								// 36
	virtual	void	RenderImposter();										// 37
	virtual	void	SetThingType(ULONG t);									// 38
	virtual void	Hit(CThing *other_thing, CCollisionReport *report);		// 39
	virtual void	Damage(float amount, CThing *inByThis, BOOL inDamageShields = TRUE, int mesh_part_no = -1);	// 40
	virtual BOOL	StartDieProcess();										// 50
	virtual void	DrawDebugStuff3d();										// 52
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59
	virtual void	Move();													// 66
	virtual BOOL	IsOnGround() { return TRUE; }							// 67 (the shared 004014a0)
	virtual BOOL	Unknown115();											// 115 (00418090)

	static SINT		sDrawDebugCount;										// 00660540: draws its debug view while it counts down

	void			CreateRepairPadAI(CInitThing *init);					// 00417390

	CStaticShadow	*mStaticShadow;											// +0x250
	SINT			mUnknown254;											// +0x254: 3 after Init (004172ea)
	float			mUnknown258;											// +0x258: -1 before Init (004171f7)
	float			mUnknown25C;											// +0x25c: when it may close (Move 004178be)
	BOOL			mUnknown260;											// +0x260: it can open (Damage 004179ae)
	SINT			mUnknown264;											// +0x264: 3 while open
	float			mUnknown268;											// +0x268: when it was last hit
};

DECLARE_UNIT_CLASS(CDropship, CBigAirUnit)							// 0x2ac, vtable 005e1dd8
	virtual void	Shutdown();												// 2
	virtual void	Init(CInitThing *init);									// 9
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_CAN_DESTROY_TREES | THING_TYPE_CAN_BE_WALKED_ON | THING_TYPE_VEHICLE | THING_TYPE_ANIMAL); }	// 38
	virtual BOOL	ClipToGround();											// 44
	virtual float	Gravity();												// 45
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59
	virtual void	Stop();													// 64 (nothing)
	virtual void	Move();													// 66
	virtual void	DeclareOnGround();										// 68
	virtual void	DeclareInWater();										// 69
	virtual float	Unknown73() { return 0.99f; }							// 73
	virtual void	Unknown93();											// 93
	virtual float	Unknown94() { return 8.0f; }							// 94
	virtual BOOL	Unknown97();											// 97
	virtual BOOL	Unknown99();											// 99
	virtual BOOL	Unknown117();											// 117
	virtual void	Unknown118();											// 118
	virtual void	Unknown119();											// 119
	virtual void	Unknown120();											// 120

			SINT	GetDoorState() { return mDoorState; }
			float	GetYawDelta() { return mYawDelta; }
			BOOL	IsAtAnchor();											// 00447b60 (CDropshipAI 0044866d)
			FVector	GetAnchor();											// 00447bb0 (CDropshipAI 00448690)
			BOOL	IsAnchorClear();										// 00447d50
			BOOL	CanStayOpen();											// 004480c0 (CDropshipAI 004486bf)
			void	Unknown448110();										// 00448110 (CDropshipAI 004486dc)
			void	Unknown448120();										// 00448120 (CDropshipAI 00448737)
			void	SpawnThrusterDust(const FVector &pos, const FMatrix &ori);	// 00448170 (Move 004472b2; it does not use the unit)

	static CParticleDescriptor	*sThrusterDust;								// 00672830 ("Thruster Dust Effect", Init 0044702d)

	SINT			mDoorState;												// +0x27c: 0 flat, 2 opening, 3 folded, 4 open, 5 unfolding, 6 closed, 7
	FVector			mAnchor;												// +0x280: where it lands
	BOOL			mHasAnchor;												// +0x290
	BOOL			mAnchorFixed;											// +0x294: set by slot 93; GetAnchor keeps it
	SINT			mUnknown298;											// +0x298
	SINT			mUnknown29C;											// +0x29c
	float			mYawDelta;												// +0x2a0: its yaw less pi, wrapped to +-pi (Unknown118 00447a60)
	float			mUnknown2A4;											// +0x2a4: 448120 flips it between 0 and 1
	float			mUnknown2A8;											// +0x2a8: last frame's +0x2a4 (Move 00447470)
};

DECLARE_UNIT_CLASS(CMine, CGroundUnit)								// 0x268, vtable 005e1b84
	virtual void	Shutdown();												// 2
	virtual void	Init(CInitThing *init);									// 9
	virtual float	GetMaxVelocity() { return 3.5f; }						// 15
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_EMPLACEMENT | THING_TYPE_STATIONARY); }	// 38
	virtual void	Hit(CThing *other_thing, CCollisionReport *report);		// 39
	virtual BOOL	StartDieProcess();										// 50
	virtual void	Move();													// 66
	virtual float	GetStealth() { return mUnitData->mUnknownD4; }			// 91
	virtual BOOL	OnScanner();											// 105 (00405930: FALSE)
	virtual void	Unknown117();											// 117

	float			mDepth;													// +0x250: how far it has sunk (CMCMine 0049c46d)
	float			mOldDepth;												// +0x254: last frame's (CMCMine 0049c467)
	BOOL			mOnLand;												// +0x258: not below the water level at Init (004ba449..004ba461)
	SINT			mUnknown25C;											// +0x25c
	float			mTriggerTime;											// +0x260: when it goes off, once an enemy has come near (Move 004ba5d3)
	class CParticleEffectLink	*mTriggerEffect;							// +0x264: the unit data's second particle, at emitter 28 (Move 004ba675)
};

DECLARE_UNIT_CLASS(CHiveBoss, CUnit)									// 0x2a8, vtable 005e16e0
	virtual void	HandleEvent(CEvent *event);								// 0
	virtual void	Init(CInitThing *init);									// 9
	virtual float	GetMaxVelocity() { return 13.0f; }						// 15
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_UNKNOWN_40000000 | THING_TYPE_CAN_BE_WALKED_ON | THING_TYPE_AIR_UNIT); }	// 38
	virtual void	Damage(float amount, CThing *inByThis, BOOL inDamageShields = TRUE, int mesh_part_no = -1);	// 40
	virtual float	Gravity() { return 0.001f; }							// 45
	virtual BOOL	StartDieProcess();										// 50
	virtual	void	SetVar(CStringDataType *name, CDataType *data);			// 62
	virtual void	Move();													// 66
	virtual void	GetTargetablePos(FVector *out);								// 90
	virtual void	Unknown95();											// 95
	virtual FVector	Unknown110();											// 110

	// IRenderableThing
	virtual int		GetShouldDoHitEffect(SINT mesh_part_no);				// 26 (00480000)

	// its core's six rings: top, middle and bottom, outer and inner, each turning at its own script-set speed
	float			mCoreAngles[6];											// +0x250
	float			mMinHeight;												// +0x268: "hb_min_height_above_ground", -1 after Init
	float			mOldCoreAngles[6];										// +0x26c: last frame's
	float			mCoreSpeeds[6];											// +0x284
	float			mRotateSpeed;											// +0x29c: "hb_rotate_speed", added to its yaw each frame
	float			mSafeDist;												// +0x2a0: "hb_safe_dist", 30 after Init
	class CDestroyableSegment	*mCore;										// +0x2a4: the "core2" segment
};

DECLARE_UNIT_CLASS(CSubmarine, CUnit)								// 0x254, vtable 005e1490
	virtual void	Init(CInitThing *init);									// 9
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_UNKNOWN_40000000 | THING_TYPE_UNKNOWN_10000000 | THING_TYPE_CAN_BE_WALKED_ON | THING_TYPE_NAVAL); }	// 38
	virtual float	GetMaxVelocity() { return mUnitData->mUnknownB4; }		// 15
	virtual BOOL	ClipToGround();											// 44 (00405930: FALSE)
	virtual BOOL	CanGoUnderWater();										// 49 (004014a0: TRUE)
	virtual void	Stop();													// 64
	virtual void	Move();													// 66
	virtual float	Unknown72() { return 0.99f; }							// 72
	virtual float	Unknown73() { return 0.99f; }							// 73
	virtual void	Unknown96();											// 96
	virtual BOOL	OnScanner();											// 105

			void	Dive();													// 004ef000 (IScript::Dive 005361f0): 1 from 2 or 3, and deactivates what it made
			void	Surface();												// 004ef050 (IScript::Surface 00536210): 3 from 0 or 1
			void	Unknown4ef0f0();										// 004ef0f0: 2
			void	SpawnBubbles();											// 004ef120 (Move 004eeff0)

	SINT			mDiveState;												// +0x250: 0 submerged (Init), 1 diving, 2 surfaced, 3 surfacing
};

DECLARE_UNIT_CLASS(CDiveBomber, CSmallAirUnit)						// 0x288, vtable 005e123c (DiveBomber.obj)
	virtual void	Init(CInitThing *init);									// 9
	virtual float	GetMaxVelocity() { return 15.0f; }						// 15
	virtual void	Hit(CThing *other_thing, CCollisionReport *report);		// 39
	virtual float	Gravity() { if (mFlags & TF_DYING) return 0.3f; return 0; }	// 45
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59
	virtual void	Move();													// 66
	virtual void	DeclareOnGround();										// 68 (004d20a0)
	virtual void	DeclareInWater();										// 69 (0047bf60)

			void	OpenBay();												// 00445570: "open" from flying or closing
			void	CloseBay();												// 004455c0: "close" from open or opening

	float			mBob;													// +0x27c: 0.2 sin(mPhase)
	SINT			mBayState;												// +0x280: 0 open, 1 closed, 2 opening, 3 closing
	float			mPhase;													// +0x284: advances 0.1 a frame
};

DECLARE_UNIT_CLASS(CThunderHead, CMech)								// 0x274, vtable 005e0fe0
	virtual void	Init(CInitThing *init);									// 9
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_CAN_BE_WALKED_ON); }	// 38
	virtual void	Move();													// 66
	virtual void	InitLegMotion(CInitThing *init);						// 117
	virtual void	InitCockpit(CInitThing *init);							// 118
	virtual void	InitTargeting();										// 119

	float			mPitch;													// +0x26c: eases toward mTargetPitch by 0.01 a frame
	float			mTargetPitch;											// +0x270: up or down at its target, within PI/8
};

DECLARE_UNIT_CLASS(CCarver, CSmallAirUnit)							// 0x28c, vtable 005e0d8c (Carver.obj)
	virtual void	Init(CInitThing *init);									// 9 (00422440)
	virtual float	GetMaxVelocity() { return 15.0f; }						// 15
	virtual void	Render(DWORD flags = 0);								// 36 (004228b0: fades with its stealth)
	virtual void	Hit(CThing *other_thing, CCollisionReport *report);		// 39 (004227e0)
	virtual float	Gravity() { if (mFlags & TF_DYING) return 0.025f; return 0; }	// 45
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59 (00422820)
	virtual void	Stop();													// 64 (00422750)
	virtual void	Move();													// 66 (00422620)
	virtual float	GetStealth() { return mStealth; }						// 91
	virtual BOOL	CanBeLocked();											// 104 (00422970)
	virtual BOOL	OnScanner();											// 105 (00422910)
	virtual BOOL	Unknown108();											// 108 (00405930: FALSE)
	virtual void	Unknown114(float a) { mUnknown284 = a; }				// 114

			void	OpenWings();											// 00422760 (its AI 00422c64)
			void	CloseWings();											// 004227a0
			void	SetAttacked();											// 00422930 (its AI, when a round aimed at it is near)
			BOOL	WasAttacked();											// 00422940 (its guide 004233ea)

	SINT			mWingState;												// +0x27c: 0 closed, 1 opening, 2 closing, 3 open
	float			mStealth;												// +0x280: eases 1 a frame towards +0x284
	float			mUnknown284;											// +0x284
	float			mAttackedTime;											// +0x288: when a round aimed at it last came near
};

DECLARE_UNIT_CLASS(CGillM, CMech)									// 0x288, vtable 005e0b30
	virtual void	Init(CInitThing *init);									// 9
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_NAMED_MESH); }	// 38
	virtual BOOL	ClipToGround();											// 44
	virtual float	Gravity();												// 45
	virtual void	Move();													// 66
	virtual float	Unknown72();											// 72
	virtual void	Unknown100();											// 100
	virtual void	Unknown116();											// 116
	virtual void	InitLegMotion(CInitThing *init);						// 117
	virtual void	InitCockpit(CInitThing *init);							// 118
	virtual void	InitTargeting();										// 119

			void	Swing();												// 00479db0 (CGillMAI 0047a392): one arm at random, every 2.5
			float	GetGroundPitch();										// 00479f30 (CMCGillM's slot 11)
			float	GetGroundRoll();										// 0047a0b0 (CMCGillM's slot 12)

	float			mNextSwingTime;											// +0x26c
	float			mUnknown270;											// +0x270: the time at Init
	SINT			mRising;												// +0x274: 1 while it comes up out of the ground
	FVector			mStart;													// +0x278: where it came up; Unknown100 walks back to it
};

DECLARE_UNIT_CLASS(CSentinel, CGroundUnit)							// 0x264, vtable 005e08e0
	virtual void	Shutdown();												// 2
	virtual void	Init(CInitThing *init);									// 9
	virtual float	GetMaxVelocity();										// 15 (004bfc60: 0)
	virtual void	Activate();												// 22
	virtual float	GetMoveMultiplier() { return 1.0f; }					// 24 (folded into 004de700)
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_CAN_BE_WALKED_ON | THING_TYPE_EMPLACEMENT | THING_TYPE_STATIONARY); }	// 38
	virtual float	Gravity();												// 45 (004bfc60: 0)
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59
	virtual void	Move();													// 66
	virtual void	Unknown95();											// 95: five small explosions about it
	virtual void	Unknown113(FVector *out);								// 113: its +0x220 part's position

			BOOL	IsMountFree(CWeapon *weapon);							// 004dee00: no child on the weapon's emitter's mount

	UBYTE			mUnknown250[0x264 - 0x250];
};

DECLARE_UNIT_CLASS(CWarspite, CMech)									// 0x26c, vtable 005e0684
	virtual void	InitCockpit(CInitThing *init);							// 118
};

DECLARE_UNIT_CLASS(CWarspiteDome, CGroundUnit)						// 0x2b8, vtable 005e01dc
	virtual void	Shutdown();												// 2
	virtual void	Init(CInitThing *init);									// 9
	virtual float	GetMaxVelocity();										// 15 (004bfc60: 0)
	virtual float	GetMoveMultiplier() { return 1.0f; }					// 24 (folded into 004de700)
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_EMPLACEMENT | THING_TYPE_STATIONARY); }	// 38
	virtual float	Gravity();												// 45 (004bfc60: 0)
	virtual BOOL	StartDieProcess();										// 50
	virtual void	Move();													// 66
	virtual BOOL	Unknown76();											// 76
	virtual BOOL	Unknown109();											// 109
	virtual BOOL	Unknown117(CWeapon *weapon);							// 117 (00504c40: a weapon fired; the pulse laser recoils three barrels)

			void	UpdatePitch();											// 00504b40: CUnit's 004fb280 each frame, clamped to -5pi/12..5

#ifdef MCWARSPITEDOME_CPP	// MCWarspiteDome.cpp's view: CGroundUnit's bob, which the dome sinks by
	float			mBob;													// +0x250
	UBYTE			mUnknown254[0x260 - 0x254];
#else
	UBYTE			mUnknown250[0x260 - 0x250];
#endif
	float			mSpinSpeed;												// +0x260: the dome's spin, eased while it is active (00504a84)
	BOOL			mOtherSide;												// +0x264: which three barrels fire next (00504c87)
	BOOL			mRecoiling[6];											// +0x268: a barrel kicking back (00504c96)
	float			mSpin;													// +0x280
	float			mOldSpin;												// +0x284
	float			mRecoil[6];												// +0x288: 0..1, rising while it recoils and settling after
	float			mOldRecoil[6];											// +0x2a0
};

DECLARE_UNIT_CLASS(CFenrir, CCarrier)								// 0x290, vtable 005e0430
	virtual void	HandleEvent(CEvent *event);								// 0
	virtual void	Init(CInitThing *init);									// 9
	virtual float	GetRadius() { return SUPERTYPE::GetRadius() * 1.5f; }	// 16
	virtual BOOL	StartDieProcess();										// 50
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59
	virtual	void	SetVar(CStringDataType *name, CDataType *data);			// 62
	virtual void	Move();													// 66
	virtual void	Unknown95();											// 95
	virtual float	Unknown111() { return mUnknown284; }					// 111

#ifdef FENRIR_CPP	// Fenrir.cpp's view; unit.h's other includers keep the byte arrays
			void	SpawnSurfaceExplosion();								// 0044e300: a small explosion at a random point on its mesh

	float			mDieDelay;												// +0x27c: 2; the script's fenrir_diedelay
	BOOL			mDying;													// +0x280: its death animation has started
	float			mUnknown284;											// +0x284: its speed, by its surviving engines
	class CEffect	*mBurningSound;											// +0x288: "Fenrir Inside Burning"
	class CEffect	*mEngineSound;											// +0x28c: "Fenrir Engines"
#else
	UBYTE			mUnknown27C[0x284 - 0x27c];
	float			mUnknown284;											// +0x284
	UBYTE			mUnknown288[0x290 - 0x288];
#endif
};

DECLARE_UNIT_CLASS(CPod, CUnit)										// 0x268, vtable 005dff8c
	virtual void	Init(CInitThing *init);									// 9
	virtual float	GetMaxVelocity();										// 15 (004bfc50: 10)
	virtual	void	SetThingType(ULONG t);									// 38
	virtual float	Gravity();												// 45
	virtual BOOL	StartDieProcess();										// 50
	virtual void	Stop();													// 64
	virtual void	Move();													// 66
	virtual void	DeclareOnGround();										// 68
	virtual float	Unknown72();											// 72 (004de700: 1)
	virtual float	Unknown73();											// 73 (004de700: 1)
	virtual float	GetThreat();											// 79 (004bfc60: 0)
	virtual float	GetImportance();										// 80 (004bfc60: 0)

			void	Launch(FVector target);									// 004d36c0: once, towards target (IScript::Launch 005344f0)
			void	Aim();													// 004d3730: the launch velocity whose arc lands nearest the target

	BOOL			mLanded;												// +0x250: TRUE until launched, and again on the ground
	BOOL			mLaunched;												// +0x254
	FVector			mTarget;												// +0x258: the init's position, then where it was launched to
};

// Shares its building behaviour with CBuilding (the same bodies, 00417660..00417690, 004dfd10) without deriving
// from it.
DECLARE_UNIT_CLASS(CSimpleBuilding, CUnit)							// 0x254, vtable 005dfd3c
	virtual void	Shutdown();												// 2
	virtual void	Init(CInitThing *init);									// 9
	virtual BOOL	GetCanBeStaticallyShadowed();							// 10
	virtual CStaticShadow	*GetStaticShadow();								// 11
	virtual void	SetStaticShadow(CStaticShadow *shadow);					// 12
	virtual float	GetMaxVelocity();										// 15
	virtual void	StickToGround();										// 18
	virtual	void	SetThingType(ULONG t);									// 38
	virtual BOOL	ClipToGround();											// 44
	virtual float	Gravity();												// 45
	virtual BOOL	StartDieProcess();										// 50
	virtual void	Stop();													// 64
	virtual void	Move();													// 66
	virtual void	DeclareOnGround();										// 68

	CStaticShadow	*mStaticShadow;											// +0x250 (slots 11 and 12 fold into CBuilding's, 00417680)
};

#endif
