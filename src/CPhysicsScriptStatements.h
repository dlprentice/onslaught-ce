// CPhysicsScriptStatements.h: the physics script's statements and values. Reconstructed from
// CPhysicsScriptStatements.obj (0042eaa0..0043e7ef); Stuart's release has no copy. The class names are RTTI's; the
// ids are the compiled script's. The allocations' __LINE__ values are the retail ones (#line: the retail layout is
// unknown); where the value classes sat in the original headers is not known either.

#ifndef CPHYSICSSCRIPTSTATEMENTS_H
#define CPHYSICSSCRIPTSTATEMENTS_H

#include "membuffer.h"
#include "DXMemBuffer.h"
#include "WorldPhysicsManager.h"

// Adds a string's size in the compiled script, its terminator included.
inline void AddStringSize(SINT &size, char *s)
{
	size++;
	for (char *c = s; *c; c++)
		size++;
}

// Reads a string up to and including its terminator.
inline void PhysicsReadString(CMEMBUFFER &file, char *s)
{
	s--;
	do
	{
		s++;
		file.Read(s, 1);
	} while (*s);
}

// The statements' ids in the compiled script.
enum EPhysicsStatement
{
	PST_UNIT = 1,
	PST_WEAPON,
	PST_WEAPON_MODE,
	PST_ROUND,
	PST_SPAWNER,
	PST_EXPLOSION,
	PST_COMPONENT,
	PST_FEATURE,
	PST_HAZARD
};

// A statement: one definition in the script (a unit, a weapon, ...) and the values set on it.
class CPhysicsScriptStatement
{
public:
	CPhysicsScriptStatement() { mUnknown08 = 0; }
	virtual			~CPhysicsScriptStatement() {}
	virtual void	Execute() = 0;					// makes the definition and applies the values
	virtual SINT	GetSize() = 0;					// the statement's size in the compiled script
	virtual void	Load(CMEMBUFFER &file) = 0;

	static CPhysicsScriptStatement	*Create(SINT type);	// 0042eb90: NULL for an unknown type

	SINT			mType;							// +0x04: EPhysicsStatement
	SINT			mUnknown08;						// +0x08
};

void	SkipStatement(CMEMBUFFER &file, SINT size);	// 0043e630: skips an unknown statement's bytes

// The script's enumerated values: each keyword is a class whose GetValue returns the game's number for it.
class CPhysicsSeekType
{
public:
	virtual			~CPhysicsSeekType() {}
	virtual SINT	GetValue() = 0;

	SINT			mType;			// +0x04
};

class CPhysicsBehaviourType
{
public:
	virtual			~CPhysicsBehaviourType() {}
	virtual SINT	GetValue() = 0;

	SINT			mType;			// +0x04
};

#define PHYSICS_BEHAVIOUR_TYPE(name, type, value)							\
class name : public CPhysicsBehaviourType									\
{																			\
public:																		\
	name() { mType = type; }												\
	virtual SINT	GetValue() { return value; }							\
};

PHYSICS_BEHAVIOUR_TYPE(CMechUnitBehaviourType,				1,	UB_MECH)
PHYSICS_BEHAVIOUR_TYPE(CJeepBehaviourType,					2,	UB_JEEP)
PHYSICS_BEHAVIOUR_TYPE(CGroundUnitBehaviourType,			3,	UB_GROUND_UNIT)
PHYSICS_BEHAVIOUR_TYPE(CInfantryUnitBehaviourType,			4,	UB_INFANTRY)
PHYSICS_BEHAVIOUR_TYPE(CTurretUnitBehaviourType,			5,	UB_TURRET)
class CBoatUnitBehaviourType : public CPhysicsBehaviourType { public: CBoatUnitBehaviourType() { mType = 6; } virtual SINT GetValue(); };	// not the macro: GetValue out of line (CPhysicsScriptStatements.cpp), the only behaviour type that retail folds into the out-of-line 004dbc20; with every type out of line the other 24 stop folding (+320 bytes)
PHYSICS_BEHAVIOUR_TYPE(CCarrierUnitBehaviourType,			7,	UB_CARRIER)
PHYSICS_BEHAVIOUR_TYPE(CBuildingUnitBehaviourType,			8,	UB_BUILDING)
PHYSICS_BEHAVIOUR_TYPE(CFighterBehaviourType,				9,	UB_FIGHTER)
PHYSICS_BEHAVIOUR_TYPE(CBomberBehaviourType,				10,	UB_BOMBER)
PHYSICS_BEHAVIOUR_TYPE(CGroundAttackAircraftBehaviourType,	11,	UB_GROUND_ATTACK_AIRCRAFT)
PHYSICS_BEHAVIOUR_TYPE(CDropshipBehaviourType,				12,	UB_DROPSHIP)
PHYSICS_BEHAVIOUR_TYPE(CMineBehaviourType,					13,	UB_MINE)
PHYSICS_BEHAVIOUR_TYPE(CHiveBossBehaviourType,				14,	UB_HIVE_BOSS)
PHYSICS_BEHAVIOUR_TYPE(CSubmarineBehaviourType,				15,	UB_SUBMARINE)
PHYSICS_BEHAVIOUR_TYPE(CDiveBomberBehaviourType,			16,	UB_DIVE_BOMBER)
PHYSICS_BEHAVIOUR_TYPE(CThunderHeadBehaviourType,			17,	UB_THUNDERHEAD)
PHYSICS_BEHAVIOUR_TYPE(CCarverBehaviourType,				18,	UB_CARVER)
PHYSICS_BEHAVIOUR_TYPE(CGillMBehaviourType,					19,	UB_GILLM)
PHYSICS_BEHAVIOUR_TYPE(CSentinelBehaviourType,				20,	UB_SENTINEL)
PHYSICS_BEHAVIOUR_TYPE(CWarspiteBehaviourType,				21,	UB_WARSPITE)
PHYSICS_BEHAVIOUR_TYPE(CFenrirBehaviourType,				22,	UB_FENRIR)
PHYSICS_BEHAVIOUR_TYPE(CWarspiteDomeBehaviourType,			23,	UB_WARSPITE_DOME)
PHYSICS_BEHAVIOUR_TYPE(CPodBehaviourType,					24,	UB_POD)
PHYSICS_BEHAVIOUR_TYPE(CSimpleBuildingBehaviourType,		25,	UB_SIMPLE_BUILDING)

class CPhysicsAlligenceType
{
public:
	virtual			~CPhysicsAlligenceType() {}
	virtual SINT	GetValue() = 0;

	SINT			mType;			// +0x04
};

class CPhysicsNavMapType
{
public:
	virtual			~CPhysicsNavMapType() {}
	virtual SINT	GetValue() = 0;

	SINT			mType;			// +0x04
};

class CPhysicsStateType
{
public:
	virtual			~CPhysicsStateType() {}
	virtual SINT	GetValue() = 0;

	SINT			mType;			// +0x04
};

CPhysicsSeekType		*CreateSeekType(SINT type);			// 0043dcd0
CPhysicsBehaviourType	*CreateBehaviourType(SINT type);	// 0043ddc0
CPhysicsAlligenceType	*CreateAlligenceType(SINT type);	// 0043e310
CPhysicsNavMapType		*CreateNavMapType(SINT type);		// 0043e400
CPhysicsStateType		*CreateStateType(SINT type);		// 0043e540

// The unit values (the physics script's keywords inside a unit statement), by their compiled ids.
enum EPhysicsUnitValue
{
	PUV_GROUND_VELOCITY	= 1,
	PUV_AIR_VELOCITY	= 2,
	PUV_LIFE	= 3,
	PUV_AIR_SHIELDS	= 4,
	PUV_GROUND_TURN_RATE	= 5,
	PUV_AIR_TURN_RATE	= 6,
	PUV_USE	= 7,
	PUV_BEHAVIOUR	= 8,
	PUV_MESH	= 9,
	PUV_EXPLOSION	= 10,
	PUV_ALLIGENCE	= 11,
	PUV_TRAILS	= 12,
	PUV_THRUSTERS	= 13,
	PUV_SMOKE	= 14,
	PUV_GROUND_SHIELDS	= 15,
	PUV_AIR_SHIELD_REGENERATION	= 16,
	PUV_GROUND_SHIELD_REGENERATION	= 17,
	PUV_USE_SPAWNER	= 18,
	PUV_THREAT	= 19,
	PUV_IMPORTANCE	= 20,
	PUV_SOUND_MATERIAL	= 21,
	PUV_STRAFE_CHANGE	= 22,
	PUV_MAX_TARGET_RANGE	= 23,
	PUV_SHIELD_EFFECT	= 24,
	PUV_NAV_MAP	= 25,
	PUV_MULTIPLE_SPAWN	= 26,
	PUV_COMPONENT	= 27,
	PUV_STEALTH	= 28,
	PUV_ATTACK_PRIORITY	= 29,
	PUV_BODY_MOTION	= 30,
	PUV_LEG_SPEED	= 31,
	PUV_LEG_PLACEMENT_AREA	= 32,
	PUV_DETONATION_DELAY	= 33,
	PUV_BOUNCE_FACTOR	= 34,
	PUV_WATER_WAKE	= 35,
	PUV_ENGINES	= 36,
	PUV_DUST	= 37,
	PUV_VENT	= 38,
	PUV_BASED_ON	= 39,
	PUV_MIN_THRUSTER_HEIGHT	= 40,
	PUV_MAX_THRUSTER_HEIGHT	= 41,
	PUV_MIN_ALTITUDE	= 42,
	PUV_BUILDING_NUMBER	= 43,
	PUV_BUILDING_MESH_TYPE	= 44,
	PUV_PARACHUTE	= 45,
	PUV_KNOCK_TREES	= 46,
	PUV_TURRET_TURN_RATE	= 47,
	PUV_HOVER	= 48,
	PUV_DEPLOY	= 50,
	PUV_ACTIVATION_EFFECT	= 51,
	PUV_RETREAT	= 52,
	PUV_WINGMAN	= 53,
	PUV_NOISE	= 54,
	PUV_AFTERBURNER	= 55,
	PUV_BIG	= 56,
	PUV_SMALL_EXPLOSION	= 57,
	PUV_STOMP_EXPLOSION	= 58,
	PUV_DEPLOY_DAMAGE_MULTIPLIER	= 59,
	PUV_STANDING_LEG_PLACEMENT_AREA	= 60,
	PUV_MAX_LEGS_LIFTED	= 61,
	PUV_INDISCRIMINATE	= 62,
	PUV_SPAWN_NOISE	= 63,
	PUV_GROUND_WAKE	= 64,
	PUV_SHATTER	= 65,
	PUV_LOCKABLE	= 66,
	PUV_SWEEPING	= 67,
	PUV_IGNORE_THREATS	= 68,
	PUV_ANIMATED_COLLISION	= 69,
	PUV_ICON	= 70,
};

// A value set in a unit statement; applied to the unit definition the statement made.
class CPhysicsUnitValue
{
public:
	virtual			~CPhysicsUnitValue() {}
	virtual void	Apply(CUnitData *unit) = 0;
	virtual SINT	GetSize() = 0;
	virtual void	Load(CMEMBUFFER &file) = 0;

	SINT			mType;			// +0x04: EPhysicsUnitValue
};

CPhysicsUnitValue	*CreateUnitValue(SINT type);		// 00431bb0

// The values set in one statement, as a list; the compiled script ends it with a nonzero flag.
class CPhysicsUnitValueList
{
public:
	CPhysicsUnitValueList() { mValue = NULL; mNext = NULL; }
	virtual			~CPhysicsUnitValueList()
	{
		delete mValue;
		delete mNext;
	}

	void			Apply(CUnitData *unit)
	{
		if (mValue)
			mValue->Apply(unit);
		if (mNext)
			mNext->Apply(unit);
	}
	SINT			GetSize()
	{
		SINT	size = 8;
		if (mValue)
			size = mValue->GetSize() + 8;
		if (mNext)
			size += mNext->GetSize();
		return size;
	}
	void			Load(CMEMBUFFER &file)
	{
		SINT	type, size;
		char	skip;
		file.Read(&type, 4);
		file.Read(&size, 4);

		mValue = CreateUnitValue(type);
		if (mValue)
			mValue->Load(file);
		else
		{
			for (SINT i = size; i > 0; i--)
				file.Read(&skip, 1);
		}

		file.Read(&type, 4);						// nonzero ends the list
		if (type == 0)
		{
#line 666
			mNext = new(MT_PHYSICS) CPhysicsUnitValueList;
			mNext->Load(file);
		}
		else
			mNext = NULL;
	}

	CPhysicsUnitValue		*mValue;	// +0x04
	CPhysicsUnitValueList	*mNext;		// +0x08
};

class CUnitStatement : public CPhysicsScriptStatement
{
public:
	CUnitStatement() { mType = PST_UNIT; mName[0] = 0; mValues = NULL; }
	virtual			~CUnitStatement() { delete mValues; }

	virtual void	Execute()
	{
		UPhysicsManager::AddUnit(mName);
		mValues->Apply(UPhysicsManager::GetUnitData(mName));
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mName); return size + mValues->GetSize(); }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mName);
#line 1989
		mValues = new(MT_PHYSICS) CPhysicsUnitValueList;
		mValues->Load(file);
	}

	char					mName[256];	// +0x0c
	CPhysicsUnitValueList	*mValues;	// +0x10c
};

// The weapon values, by their compiled ids.
enum EPhysicsWeaponValue
{
	PWV_CHARGE_LEVEL	= 1,
	PWV_CHARGE_RATE	= 2,
	PWV_AMMO_STORE	= 3,
	PWV_CONSUMPTION	= 4,
	PWV_ICON_NAME	= 5,
	PWV_SMART	= 6,
	PWV_ADJUST_AIM	= 7,
	PWV_ZOOM_MODE	= 8,
	PWV_ALLOW_MOVEMENT	= 9,
	PWV_PLACEMENT	= 10,
	PWV_LANGUAGE_NAME	= 11,
	PWV_VERSUS_INFANTRY	= 12,
	PWV_VERSUS_TANKS	= 13,
	PWV_VERSUS_AIR	= 14,
};

// A value set in a weapon statement; applied to the definition with the statement's name.
class CPhysicsWeaponValue
{
public:
	virtual			~CPhysicsWeaponValue() {}
	virtual void	Apply(char *name) = 0;
	virtual SINT	GetSize() = 0;
	virtual void	Load(CMEMBUFFER &file) = 0;

	SINT			mType;			// +0x04: EPhysicsWeaponValue
};

CPhysicsWeaponValue	*CreateWeaponValue(SINT type);

class CPhysicsWeaponValueList
{
public:
	CPhysicsWeaponValueList() { mValue = NULL; mNext = NULL; }
	virtual			~CPhysicsWeaponValueList()
	{
		delete mValue;
		delete mNext;
	}

	void			Apply(char *name)
	{
		if (mValue)
			mValue->Apply(name);
		if (mNext)
			mNext->Apply(name);
	}
	SINT			GetSize()
	{
		SINT	size = 8;
		if (mValue)
			size = mValue->GetSize() + 8;
		if (mNext)
			size += mNext->GetSize();
		return size;
	}
	void			Load(CMEMBUFFER &file)
	{
		SINT	type, size;
		char	skip;
		file.Read(&type, 4);
		file.Read(&size, 4);

		mValue = CreateWeaponValue(type);
		if (mValue)
			mValue->Load(file);
		else
		{
			for (SINT i = size; i > 0; i--)
				file.Read(&skip, 1);
		}

		file.Read(&type, 4);						// nonzero ends the list
		if (type == 0)
		{
#line 814
			mNext = new(MT_PHYSICS) CPhysicsWeaponValueList;
			mNext->Load(file);
		}
		else
			mNext = NULL;
	}

	CPhysicsWeaponValue		*mValue;	// +0x04
	CPhysicsWeaponValueList	*mNext;		// +0x08
};

class CWeaponStatement : public CPhysicsScriptStatement
{
public:
	CWeaponStatement() { mType = PST_WEAPON; mValues = NULL; mName[0] = 0; }
	virtual			~CWeaponStatement() { delete mValues; }

	virtual void	Execute()
	{
		UPhysicsManager::AddWeapon(mName);
		mValues->Apply(mName);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mName); return size + mValues->GetSize(); }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mName);
#line 2110
		mValues = new(MT_PHYSICS) CPhysicsWeaponValueList;
		mValues->Load(file);
	}

	char					mName[256];	// +0x0c
	CPhysicsWeaponValueList	*mValues;	// +0x10c
};

// The weaponmode values, by their compiled ids.
enum EPhysicsWeaponModeValue
{
	PWMV_INACCURACY	= 1,
	PWMV_ROUND	= 2,
	PWMV_RELOAD_TIME	= 3,
	PWMV_BURST_SIZE	= 4,
	PWMV_BURST_DELAY	= 5,
	PWMV_MUZZLE_EFFECT	= 6,
	PWMV_LAUNCH_SEQUENCE	= 8,
	PWMV_MIN_RANGE	= 9,
	PWMV_MAX_RANGE	= 10,
	PWMV_MIN_DEFLECTION	= 11,
	PWMV_MAX_DEFLECTION	= 12,
	PWMV_PRE_FIRE_DELAY	= 13,
	PWMV_POST_FIRE_DELAY	= 14,
	PWMV_CLIP	= 15,
	PWMV_LOCK_TIME	= 16,
	PWMV_LOCK_DEFLECTION	= 17,
	PWMV_PRE_FIRE_EFFECT	= 18,
	PWMV_LOCK_MODE	= 19,
	PWMV_LOCK_UNIT	= 20,
	PWMV_LOCK_RANGE	= 21,
	PWMV_MAX_LOCKS	= 22,
	PWMV_LOCK_RADIUS	= 23,
	PWMV_LAUNCH_SOUND	= 24,
	PWMV_BASED_ON	= 25,
	PWMV_MIN_TARGET_HEIGHT	= 26,
	PWMV_MAX_TARGET_HEIGHT	= 27,
	PWMV_VOLLEY_SIZE	= 28,
	PWMV_TRACK	= 29,
	PWMV_YAW_TOLERANCE	= 30,
	PWMV_LAUNCH_ANGLE	= 31,
	PWMV_POWER	= 32,
	PWMV_PREDICTIVE	= 33,
	PWMV_PRE_FIRE_SOUND	= 34,
	PWMV_SOUND_PER_BURST	= 35,
	PWMV_POST_FIRE_SOUND	= 36,
	PWMV_MUZZLE_LIGHT	= 37,
	PWMV_MUZZLE_LIGHT_RADIUS	= 38,
};

// A value set in a weaponmode statement; applied to the definition with the statement's name.
class CPhysicsWeaponModeValue
{
public:
	virtual			~CPhysicsWeaponModeValue() {}
	virtual void	Apply(char *name) = 0;
	virtual SINT	GetSize() = 0;
	virtual void	Load(CMEMBUFFER &file) = 0;

	SINT			mType;			// +0x04: EPhysicsWeaponModeValue
};

CPhysicsWeaponModeValue	*CreateWeaponModeValue(SINT type);

class CPhysicsWeaponModeValueList
{
public:
	CPhysicsWeaponModeValueList() { mValue = NULL; mNext = NULL; }
	virtual			~CPhysicsWeaponModeValueList()
	{
		delete mValue;
		delete mNext;
	}

	void			Apply(char *name)
	{
		if (mValue)
			mValue->Apply(name);
		if (mNext)
			mNext->Apply(name);
	}
	SINT			GetSize()
	{
		SINT	size = 8;
		if (mValue)
			size = mValue->GetSize() + 8;
		if (mNext)
			size += mNext->GetSize();
		return size;
	}
	void			Load(CMEMBUFFER &file)
	{
		SINT	type, size;
		char	skip;
		file.Read(&type, 4);
		file.Read(&size, 4);

		mValue = CreateWeaponModeValue(type);
		if (mValue)
			mValue->Load(file);
		else
		{
			for (SINT i = size; i > 0; i--)
				file.Read(&skip, 1);
		}

		file.Read(&type, 4);						// nonzero ends the list
		if (type == 0)
		{
#line 962
			mNext = new(MT_PHYSICS) CPhysicsWeaponModeValueList;
			mNext->Load(file);
		}
		else
			mNext = NULL;
	}

	CPhysicsWeaponModeValue		*mValue;	// +0x04
	CPhysicsWeaponModeValueList	*mNext;		// +0x08
};

class CWeaponModeStatement : public CPhysicsScriptStatement
{
public:
	CWeaponModeStatement() { mType = PST_WEAPON_MODE; mValues = NULL; mName[0] = 0; }
	virtual			~CWeaponModeStatement() { delete mValues; }

	virtual void	Execute()
	{
		UPhysicsManager::AddWeaponMode(mName);
		mValues->Apply(mName);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mName); return size + mValues->GetSize(); }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mName);
#line 2231
		mValues = new(MT_PHYSICS) CPhysicsWeaponModeValueList;
		mValues->Load(file);
	}

	char					mName[256];	// +0x0c
	CPhysicsWeaponModeValueList	*mValues;	// +0x10c
};

// The round values, by their compiled ids.
enum EPhysicsRoundValue
{
	PRV_LIFE_SPAN	= 1,
	PRV_DAMAGE	= 2,
	PRV_VELOCITY	= 3,
	PRV_SEEK	= 4,
	PRV_TURN_RATE	= 5,
	PRV_GRAVITY	= 6,
	PRV_BOUNCE	= 7,
	PRV_EFFECT	= 8,
	PRV_EXPLOSION	= 9,
	PRV_SEEK_DELAY	= 10,
	PRV_WIGGLE	= 11,
	PRV_RADIUS	= 12,
	PRV_FLAK	= 13,
	PRV_FLAK_INACCURACY	= 14,
	PRV_SEEK_ANGLE	= 15,
	PRV_BEAM	= 16,
	PRV_SOUND_MATERIAL	= 17,
	PRV_WEIRDO_SEEK	= 18,
	PRV_REARM	= 19,
	PRV_BASED_ON	= 20,
	PRV_SEEK_TERMINATION_TIME	= 21,
	PRV_MISSILE	= 22,
	PRV_PROXIMITY	= 23,
	PRV_GRID_OF_FEAR	= 24,
	PRV_UNDER_WATER	= 25,
	PRV_GROUND_HUGGING	= 26,
	PRV_PASSIVE_COLLISION	= 27,
	PRV_JUMPS	= 28,
	PRV_JUMP_RANGE	= 29,
	PRV_JUMP_DELAY	= 30,
	PRV_EXPLODE	= 31,
	PRV_TORPEDO	= 32,
	PRV_WATER_EFFECT	= 33,
	PRV_FIRE	= 34,
	PRV_TREE_COLLISION	= 35,
	PRV_MESH	= 36,
	PRV_SMART	= 37,
	PRV_LENGTH	= 38,
};

// A value set in a round statement; applied to the definition with the statement's name.
class CPhysicsRoundValue
{
public:
	virtual			~CPhysicsRoundValue() {}
	virtual void	Apply(char *name) = 0;
	virtual SINT	GetSize() = 0;
	virtual void	Load(CMEMBUFFER &file) = 0;

	SINT			mType;			// +0x04: EPhysicsRoundValue
};

CPhysicsRoundValue	*CreateRoundValue(SINT type);

class CPhysicsRoundValueList
{
public:
	CPhysicsRoundValueList() { mValue = NULL; mNext = NULL; }
	virtual			~CPhysicsRoundValueList()
	{
		delete mValue;
		delete mNext;
	}

	void			Apply(char *name)
	{
		if (mValue)
			mValue->Apply(name);
		if (mNext)
			mNext->Apply(name);
	}
	SINT			GetSize()
	{
		SINT	size = mValue->GetSize() + 8;
		if (mNext)
			size += mNext->GetSize() + 4;
		else
			size += 4;
		return size;
	}
	void			Load(CMEMBUFFER &file)
	{
		SINT	type, size;
		char	skip;
		file.Read(&type, 4);
		file.Read(&size, 4);

		mValue = CreateRoundValue(type);
		if (mValue)
			mValue->Load(file);
		else
		{
			for (SINT i = size; i > 0; i--)
				file.Read(&skip, 1);
		}

		file.Read(&type, 4);						// nonzero ends the list
		if (type == 0)
		{
#line 1112
			mNext = new(MT_PHYSICS) CPhysicsRoundValueList;
			mNext->Load(file);
		}
		else
			mNext = NULL;
	}

	CPhysicsRoundValue		*mValue;	// +0x04
	CPhysicsRoundValueList	*mNext;		// +0x08
};

class CRoundStatement : public CPhysicsScriptStatement
{
public:
	CRoundStatement() { mType = PST_ROUND; mValues = NULL; mName[0] = 0; }
	virtual			~CRoundStatement() { delete mValues; }

	virtual void	Execute()
	{
		UPhysicsManager::AddRound(mName);
		mValues->Apply(mName);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mName); return size + mValues->GetSize(); }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mName);
#line 2352
		mValues = new(MT_PHYSICS) CPhysicsRoundValueList;
		mValues->Load(file);
	}

	char					mName[256];	// +0x0c
	CPhysicsRoundValueList	*mValues;	// +0x10c
};

// The spawner values, by their compiled ids.
enum EPhysicsSpawnerValue
{
	PSV_UNIT	= 1,
	PSV_DELAY	= 2,
	PSV_AMOUNT	= 3,
	PSV_SEEK_DELAY	= 4,
	PSV_RECALL	= 5,
	PSV_MIN_RANGE	= 6,
	PSV_MAX_RANGE	= 7,
	PSV_PRE_SPAWN_DELAY	= 8,
	PSV_POST_SPAWN_DELAY	= 9,
	PSV_BASED_ON	= 10,
	PSV_SQUAD_SIZE	= 11,
	PSV_SQUAD_DELAY	= 12,
	PSV_INFINITE	= 13,
	PSV_CONDITIONS	= 14,
};

// A value set in a spawner statement; applied to the definition with the statement's name.
class CPhysicsSpawnerValue
{
public:
	virtual			~CPhysicsSpawnerValue() {}
	virtual void	Apply(char *name) = 0;
	virtual SINT	GetSize() = 0;
	virtual void	Load(CMEMBUFFER &file) = 0;

	SINT			mType;			// +0x04: EPhysicsSpawnerValue
};

CPhysicsSpawnerValue	*CreateSpawnerValue(SINT type);

class CPhysicsSpawnerValueList
{
public:
	CPhysicsSpawnerValueList() { mValue = NULL; mNext = NULL; }
	virtual			~CPhysicsSpawnerValueList()
	{
		delete mValue;
		delete mNext;
	}

	void			Apply(char *name)
	{
		if (mValue)
			mValue->Apply(name);
		if (mNext)
			mNext->Apply(name);
	}
	SINT			GetSize()
	{
		SINT	size = mValue->GetSize() + 8;
		if (mNext)
			size += mNext->GetSize() + 4;
		else
			size += 4;
		return size;
	}
	void			Load(CMEMBUFFER &file)
	{
		SINT	type, size;
		char	skip;
		file.Read(&type, 4);
		file.Read(&size, 4);

		mValue = CreateSpawnerValue(type);
		if (mValue)
			mValue->Load(file);
		else
		{
			for (SINT i = size; i > 0; i--)
				file.Read(&skip, 1);
		}

		file.Read(&type, 4);						// nonzero ends the list
		if (type == 0)
		{
#line 1262
			mNext = new(MT_PHYSICS) CPhysicsSpawnerValueList;
			mNext->Load(file);
		}
		else
			mNext = NULL;
	}

	CPhysicsSpawnerValue		*mValue;	// +0x04
	CPhysicsSpawnerValueList	*mNext;		// +0x08
};

class CSpawnerStatement : public CPhysicsScriptStatement
{
public:
	CSpawnerStatement() { mType = PST_SPAWNER; mValues = NULL; mName[0] = 0; }
	virtual			~CSpawnerStatement() { delete mValues; }

	virtual void	Execute()
	{
		UPhysicsManager::AddSpawnerData(mName);
		mValues->Apply(mName);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mName); return size + mValues->GetSize(); }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mName);
#line 2473
		mValues = new(MT_PHYSICS) CPhysicsSpawnerValueList;
		mValues->Load(file);
	}

	char					mName[256];	// +0x0c
	CPhysicsSpawnerValueList	*mValues;	// +0x10c
};

// The explosion values, by their compiled ids.
enum EPhysicsExplosionValue
{
	PEV_BASED_ON	= 1,
	PEV_AIR_EFFECT	= 2,
	PEV_RADIUS	= 3,
	PEV_DAMAGE	= 4,
	PEV_GROUND_EFFECT	= 5,
	PEV_WATER_EFFECT	= 6,
	PEV_UNIT_EFFECT	= 7,
	PEV_VOLUMETRIC	= 8,
	PEV_TIME	= 9,
	PEV_SOUND	= 10,
	PEV_SMART	= 11,
	PEV_LIGHT	= 12,
	PEV_ORIENTED	= 13,
	PEV_SHOCKWAVE	= 14,
	PEV_WATER_SOUND	= 15,
};

// A value set in a explosion statement; applied to the definition with the statement's name.
class CPhysicsExplosionValue
{
public:
	virtual			~CPhysicsExplosionValue() {}
	virtual void	Apply(char *name) = 0;
	virtual SINT	GetSize() = 0;
	virtual void	Load(CMEMBUFFER &file) = 0;

	SINT			mType;			// +0x04: EPhysicsExplosionValue
};

CPhysicsExplosionValue	*CreateExplosionValue(SINT type);

class CPhysicsExplosionValueList
{
public:
	CPhysicsExplosionValueList() { mValue = NULL; mNext = NULL; }
	virtual			~CPhysicsExplosionValueList()
	{
		delete mValue;
		delete mNext;
	}

	void			Apply(char *name)
	{
		if (mValue)
			mValue->Apply(name);
		if (mNext)
			mNext->Apply(name);
	}
	SINT			GetSize()
	{
		SINT	size = mValue->GetSize() + 8;
		if (mNext)
			size += mNext->GetSize() + 4;
		else
			size += 4;
		return size;
	}
	void			Load(CMEMBUFFER &file)
	{
		SINT	type, size;
		char	skip;
		file.Read(&type, 4);
		file.Read(&size, 4);

		mValue = CreateExplosionValue(type);
		if (mValue)
			mValue->Load(file);
		else
		{
			for (SINT i = size; i > 0; i--)
				file.Read(&skip, 1);
		}

		file.Read(&type, 4);						// nonzero ends the list
		if (type == 0)
		{
#line 1413
			mNext = new(MT_PHYSICS) CPhysicsExplosionValueList;
			mNext->Load(file);
		}
		else
			mNext = NULL;
	}

	CPhysicsExplosionValue		*mValue;	// +0x04
	CPhysicsExplosionValueList	*mNext;		// +0x08
};

class CExplosionStatement : public CPhysicsScriptStatement
{
public:
	CExplosionStatement() { mType = PST_EXPLOSION; mValues = NULL; mName[0] = 0; }
	virtual			~CExplosionStatement() { delete mValues; }

	virtual void	Execute()
	{
		UPhysicsManager::AddExplosion(mName);
		mValues->Apply(mName);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mName); return size + mValues->GetSize(); }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mName);
#line 2593
		mValues = new(MT_PHYSICS) CPhysicsExplosionValueList;
		mValues->Load(file);
	}

	char					mName[256];	// +0x0c
	CPhysicsExplosionValueList	*mValues;	// +0x10c
};

// The component values, by their compiled ids.
enum EPhysicsComponentValue
{
	PCV_LIFE	= 1,
	PCV_USE	= 2,
	PCV_MESH	= 3,
	PCV_EXPLOSION	= 4,
	PCV_MAX_TARGET_RANGE	= 6,
	PCV_MAX_TURRET_YAW	= 7,
	PCV_MAX_YAW	= 8,
	PCV_TURN_RATE	= 9,
	PCV_BASED_ON	= 10,
	PCV_TURRET_TURN_RATE	= 11,
	PCV_NOISE	= 12,
	PCV_BIG	= 13,
	PCV_SMALL_EXPLOSION	= 14,
	PCV_INDISCRIMINATE	= 15,
	PCV_DEPLOY	= 16,
	PCV_DEPLOY_DAMAGE_MULTIPLIER	= 17,
	PCV_TENTACLE	= 18,
	PCV_USE_SPAWNER	= 19,
	PCV_ATTACK_PRIORITY	= 20,
	PCV_DETACH	= 21,
	PCV_LOCKABLE	= 22,
	PCV_SWEEPING	= 23,
	PCV_PASS_ON_HIT_EFFECT	= 24,
	PCV_VENT	= 25,
};

// A value set in a component statement; applied to the definition with the statement's name.
class CPhysicsComponentValue
{
public:
	virtual			~CPhysicsComponentValue() {}
	virtual void	Apply(char *name) = 0;
	virtual SINT	GetSize() = 0;
	virtual void	Load(CMEMBUFFER &file) = 0;

	SINT			mType;			// +0x04: EPhysicsComponentValue
};

CPhysicsComponentValue	*CreateComponentValue(SINT type);

class CPhysicsComponentValueList
{
public:
	CPhysicsComponentValueList() { mValue = NULL; mNext = NULL; }
	virtual			~CPhysicsComponentValueList()
	{
		delete mValue;
		delete mNext;
	}

	void			Apply(char *name)
	{
		if (mValue)
			mValue->Apply(name);
		if (mNext)
			mNext->Apply(name);
	}
	SINT			GetSize()
	{
		SINT	size = mValue->GetSize() + 8;
		if (mNext)
			size += mNext->GetSize() + 4;
		else
			size += 4;
		return size;
	}
	void			Load(CMEMBUFFER &file)
	{
		SINT	type, size;
		char	skip;
		file.Read(&type, 4);
		file.Read(&size, 4);

		mValue = CreateComponentValue(type);
		if (mValue)
			mValue->Load(file);
		else
		{
			for (SINT i = size; i > 0; i--)
				file.Read(&skip, 1);
		}

		file.Read(&type, 4);						// nonzero ends the list
		if (type == 0)
		{
#line 1563
			mNext = new(MT_PHYSICS) CPhysicsComponentValueList;
			mNext->Load(file);
		}
		else
			mNext = NULL;
	}

	CPhysicsComponentValue		*mValue;	// +0x04
	CPhysicsComponentValueList	*mNext;		// +0x08
};

class CComponentStatement : public CPhysicsScriptStatement
{
public:
	CComponentStatement() { mType = PST_COMPONENT; mValues = NULL; mName[0] = 0; }
	virtual			~CComponentStatement() { delete mValues; }

	virtual void	Execute()
	{
		UPhysicsManager::AddComponentData(mName);
		mValues->Apply(mName);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mName); return size + mValues->GetSize(); }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mName);
#line 2714
		mValues = new(MT_PHYSICS) CPhysicsComponentValueList;
		mValues->Load(file);
	}

	char					mName[256];	// +0x0c
	CPhysicsComponentValueList	*mValues;	// +0x10c
};

// The feature values, by their compiled ids.
enum EPhysicsFeatureValue
{
	PFV_LIFE	= 1,
	PFV_MESH	= 2,
	PFV_EXPLOSION	= 3,
	PFV_INVINCIBLE	= 4,
	PFV_NOISE	= 5,
	PFV_LOCKABLE	= 6,
	PFV_ICON	= 7,
};

// A value set in a feature statement; applied to the definition with the statement's name.
class CPhysicsFeatureValue
{
public:
	virtual			~CPhysicsFeatureValue() {}
	virtual void	Apply(char *name) = 0;
	virtual SINT	GetSize() = 0;
	virtual void	Load(CMEMBUFFER &file) = 0;

	SINT			mType;			// +0x04: EPhysicsFeatureValue
};

CPhysicsFeatureValue	*CreateFeatureValue(SINT type);

class CPhysicsFeatureValueList
{
public:
	CPhysicsFeatureValueList() { mValue = NULL; mNext = NULL; }
	virtual			~CPhysicsFeatureValueList()
	{
		delete mValue;
		delete mNext;
	}

	void			Apply(char *name)
	{
		if (mValue)
			mValue->Apply(name);
		if (mNext)
			mNext->Apply(name);
	}
	SINT			GetSize()
	{
		SINT	size = mValue->GetSize() + 8;
		if (mNext)
			size += mNext->GetSize() + 4;
		else
			size += 4;
		return size;
	}
	void			Load(CMEMBUFFER &file)
	{
		SINT	type, size;
		char	skip;
		file.Read(&type, 4);
		file.Read(&size, 4);

		mValue = CreateFeatureValue(type);
		if (mValue)
			mValue->Load(file);
		else
		{
			for (SINT i = size; i > 0; i--)
				file.Read(&skip, 1);
		}

		file.Read(&type, 4);						// nonzero ends the list
		if (type == 0)
		{
#line 1713
			mNext = new(MT_PHYSICS) CPhysicsFeatureValueList;
			mNext->Load(file);
		}
		else
			mNext = NULL;
	}

	CPhysicsFeatureValue		*mValue;	// +0x04
	CPhysicsFeatureValueList	*mNext;		// +0x08
};

class CFeatureStatement : public CPhysicsScriptStatement
{
public:
	CFeatureStatement() { mType = PST_FEATURE; mValues = NULL; mName[0] = 0; }
	virtual			~CFeatureStatement() { delete mValues; }

	virtual void	Execute()
	{
		UPhysicsManager::AddFeature(mName);
		mValues->Apply(mName);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mName); return size + mValues->GetSize(); }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mName);
#line 2836
		mValues = new(MT_PHYSICS) CPhysicsFeatureValueList;
		mValues->Load(file);
	}

	char					mName[256];	// +0x0c
	CPhysicsFeatureValueList	*mValues;	// +0x10c
};

// The hazard values, by their compiled ids.
enum EPhysicsHazardValue
{
	PHV_RADIUS	= 1,
	PHV_EFFECT	= 2,
	PHV_DAMAGE	= 3,
	PHV_NOISE	= 4,
};

// A value set in a hazard statement; applied to the definition with the statement's name.
class CPhysicsHazardValue
{
public:
	virtual			~CPhysicsHazardValue() {}
	virtual void	Apply(char *name) = 0;
	virtual SINT	GetSize() = 0;
	virtual void	Load(CMEMBUFFER &file) = 0;

	SINT			mType;			// +0x04: EPhysicsHazardValue
};

CPhysicsHazardValue	*CreateHazardValue(SINT type);

class CPhysicsHazardValueList
{
public:
	CPhysicsHazardValueList() { mValue = NULL; mNext = NULL; }
	virtual			~CPhysicsHazardValueList()
	{
		delete mValue;
		delete mNext;
	}

	void			Apply(char *name)
	{
		if (mValue)
			mValue->Apply(name);
		if (mNext)
			mNext->Apply(name);
	}
	SINT			GetSize()
	{
		SINT	size = mValue->GetSize() + 8;
		if (mNext)
			size += mNext->GetSize() + 4;
		else
			size += 4;
		return size;
	}
	void			Load(CMEMBUFFER &file)
	{
		SINT	type, size;
		char	skip;
		file.Read(&type, 4);
		file.Read(&size, 4);

		mValue = CreateHazardValue(type);
		if (mValue)
			mValue->Load(file);
		else
		{
			for (SINT i = size; i > 0; i--)
				file.Read(&skip, 1);
		}

		file.Read(&type, 4);						// nonzero ends the list
		if (type == 0)
		{
#line 1859
			mNext = new(MT_PHYSICS) CPhysicsHazardValueList;
			mNext->Load(file);
		}
		else
			mNext = NULL;
	}

	CPhysicsHazardValue		*mValue;	// +0x04
	CPhysicsHazardValueList	*mNext;		// +0x08
};

class CHazardStatement : public CPhysicsScriptStatement
{
public:
	CHazardStatement() { mType = PST_HAZARD; mValues = NULL; mName[0] = 0; }
	virtual			~CHazardStatement() { delete mValues; }

	virtual void	Execute()
	{
		UPhysicsManager::AddHazard(mName);
		mValues->Apply(mName);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mName); return size + mValues->GetSize(); }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mName);
#line 2958
		mValues = new(MT_PHYSICS) CPhysicsHazardValueList;
		mValues->Load(file);
	}

	char					mName[256];	// +0x0c
	CPhysicsHazardValueList	*mValues;	// +0x10c
};

class CSeekNothing : public CPhysicsSeekType
{
public:
	CSeekNothing() { mType = 1; }
	virtual SINT	GetValue() { return 0; }
};

class CSeekTarget : public CPhysicsSeekType
{
public:
	CSeekTarget() { mType = 2; }
	virtual SINT	GetValue() { return 1; }
};

class CSeekTargetOnly : public CPhysicsSeekType
{
public:
	CSeekTargetOnly() { mType = 3; }
	virtual SINT	GetValue() { return 2; }
};

class CFriendlyAlligence : public CPhysicsAlligenceType
{
public:
	CFriendlyAlligence() { mType = 1; }
	virtual SINT	GetValue() { return 0; }
};

class CEnemyAlligence : public CPhysicsAlligenceType
{
public:
	CEnemyAlligence() { mType = 2; }
	virtual SINT	GetValue() { return 1; }
};

class CNeutralAlligence : public CPhysicsAlligenceType
{
public:
	CNeutralAlligence() { mType = 3; }
	virtual SINT	GetValue() { return 2; }
};

class CWaterNavMapType : public CPhysicsNavMapType
{
public:
	CWaterNavMapType() { mType = 1; }
	virtual SINT	GetValue() { return 0; }
};

class CFlatNavMapType : public CPhysicsNavMapType
{
public:
	CFlatNavMapType() { mType = 2; }
	virtual SINT	GetValue() { return 1; }
};

class CSteepNavMapType : public CPhysicsNavMapType
{
public:
	CSteepNavMapType() { mType = 3; }
	virtual SINT	GetValue() { return 2; }
};

class CMountainousNavMapType : public CPhysicsNavMapType
{
public:
	CMountainousNavMapType() { mType = 4; }
	virtual SINT	GetValue() { return 3; }
};

class COnStateType : public CPhysicsStateType
{
public:
	COnStateType() { mType = 1; }
	virtual SINT	GetValue() { return 0; }
};

class COnfStateType : public CPhysicsStateType
{
public:
	COnfStateType() { mType = 2; }
	virtual SINT	GetValue() { return 1; }
};

class COffStateType : public CPhysicsStateType
{
public:
	COffStateType() { mType = 3; }
	virtual SINT	GetValue() { return 2; }
};

// Retail reads the allegiance and never applies it.
class CUnitAlligence : public CPhysicsUnitValue
{
public:
	CUnitAlligence() { mType = PUV_ALLIGENCE; mValue = NULL; }
	virtual			~CUnitAlligence() { delete mValue; }
	virtual void	Apply(CUnitData *unit) {}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file)
	{
		SINT	type;
		file.Read(&type, 4);
		mValue = CreateAlligenceType(type);
	}

	CPhysicsAlligenceType	*mValue;	// +0x08
};



class CUnitGroundVelocity : public CPhysicsUnitValue
{
public:
	CUnitGroundVelocity() { mType = PUV_GROUND_VELOCITY; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mUnknownB4 = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitAirVelocity : public CPhysicsUnitValue
{
public:
	CUnitAirVelocity() { mType = PUV_AIR_VELOCITY; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mUnknownB4 = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitGroundTurnRate : public CPhysicsUnitValue
{
public:
	CUnitGroundTurnRate() { mType = PUV_GROUND_TURN_RATE; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mTurnRate = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitAirTurnRate : public CPhysicsUnitValue
{
public:
	CUnitAirTurnRate() { mType = PUV_AIR_TURN_RATE; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mTurnRate = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitTurretTurnRate : public CPhysicsUnitValue
{
public:
	CUnitTurretTurnRate() { mType = PUV_TURRET_TURN_RATE; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mUnknownBC = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitLife : public CPhysicsUnitValue
{
public:
	CUnitLife() { mType = PUV_LIFE; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mUnknownC0 = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitIcon : public CPhysicsUnitValue
{
public:
	CUnitIcon() { mType = PUV_ICON; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mIcon = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitAirShields : public CPhysicsUnitValue
{
public:
	CUnitAirShields() { mType = PUV_AIR_SHIELDS; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mAirShields = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitGroundShields : public CPhysicsUnitValue
{
public:
	CUnitGroundShields() { mType = PUV_GROUND_SHIELDS; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mGroundShields = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitAirShieldRegeneration : public CPhysicsUnitValue
{
public:
	CUnitAirShieldRegeneration() { mType = PUV_AIR_SHIELD_REGENERATION; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mAirShieldRegeneration = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitGroundShieldRegeneration : public CPhysicsUnitValue
{
public:
	CUnitGroundShieldRegeneration() { mType = PUV_GROUND_SHIELD_REGENERATION; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mGroundShieldRegeneration = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitHover : public CPhysicsUnitValue
{
public:
	CUnitHover() { mType = PUV_HOVER; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mUnknown10C = mValue > 0.0f ? true : false; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};



class CUnitThreat : public CPhysicsUnitValue
{
public:
	CUnitThreat() { mType = PUV_THREAT; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mThreat = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitImportance : public CPhysicsUnitValue
{
public:
	CUnitImportance() { mType = PUV_IMPORTANCE; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mImportance = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitMinAltitude : public CPhysicsUnitValue
{
public:
	CUnitMinAltitude() { mType = PUV_MIN_ALTITUDE; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mMinAltitude = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitMaxTargetRange : public CPhysicsUnitValue
{
public:
	CUnitMaxTargetRange() { mType = PUV_MAX_TARGET_RANGE; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mMaxTargetRange = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitSoundMaterial : public CPhysicsUnitValue
{
public:
	CUnitSoundMaterial() { mType = PUV_SOUND_MATERIAL; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mSoundMaterial = (SINT)mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};


class CUnitStealth : public CPhysicsUnitValue
{
public:
	CUnitStealth() { mType = PUV_STEALTH; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mUnknownD4 = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};


class CUnitBodyMotion : public CPhysicsUnitValue
{
public:
	CUnitBodyMotion() { mType = PUV_BODY_MOTION; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mBodyMotion = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitLegSpeed : public CPhysicsUnitValue
{
public:
	CUnitLegSpeed() { mType = PUV_LEG_SPEED; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mLegSpeed = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitLegPlacementArea : public CPhysicsUnitValue
{
public:
	CUnitLegPlacementArea() { mType = PUV_LEG_PLACEMENT_AREA; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mLegPlacementArea = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitStandingLegPlacementArea : public CPhysicsUnitValue
{
public:
	CUnitStandingLegPlacementArea() { mType = PUV_STANDING_LEG_PLACEMENT_AREA; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mStandingLegPlacementArea = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitMaxLegsLifted : public CPhysicsUnitValue
{
public:
	CUnitMaxLegsLifted() { mType = PUV_MAX_LEGS_LIFTED; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mMaxLegsLifted = (SINT)mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitAttackPriority : public CPhysicsUnitValue
{
public:
	CUnitAttackPriority() { mType = PUV_ATTACK_PRIORITY; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mAttackPriority[mIndex] = mValue; }
	virtual SINT	GetSize() { return 8; }
	virtual void	Load(CMEMBUFFER &file)
	{
		file.Read(&mValue, 4);
		file.Read(&mIndex, 4);
	}

	float			mValue;			// +0x08
	SINT			mIndex;			// +0x0c
};

class CUnitBounceFactor : public CPhysicsUnitValue
{
public:
	CUnitBounceFactor() { mType = PUV_BOUNCE_FACTOR; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mBounceFactor = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitParachute : public CPhysicsUnitValue
{
public:
	CUnitParachute() { mType = PUV_PARACHUTE; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mParachute = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitKnockTrees : public CPhysicsUnitValue
{
public:
	CUnitKnockTrees() { mType = PUV_KNOCK_TREES; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mUnknown104 = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitDeploy : public CPhysicsUnitValue
{
public:
	CUnitDeploy() { mType = PUV_DEPLOY; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { if (mValue) unit->mUnknown108 = TRUE; else unit->mUnknown108 = FALSE; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitDeployDamageMultiplier : public CPhysicsUnitValue
{
public:
	CUnitDeployDamageMultiplier() { mType = PUV_DEPLOY_DAMAGE_MULTIPLIER; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mUnknown160 = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitRetreat : public CPhysicsUnitValue
{
public:
	CUnitRetreat() { mType = PUV_RETREAT; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { if (mValue) unit->mUnknown11C = TRUE; else unit->mUnknown11C = FALSE; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitBig : public CPhysicsUnitValue
{
public:
	CUnitBig() { mType = PUV_BIG; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { if (mValue) unit->mUnknown124 = TRUE; else unit->mUnknown124 = FALSE; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitIndiscriminate : public CPhysicsUnitValue
{
public:
	CUnitIndiscriminate() { mType = PUV_INDISCRIMINATE; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { if (mValue) unit->mUnknown128 = TRUE; else unit->mUnknown128 = FALSE; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitShatter : public CPhysicsUnitValue
{
public:
	CUnitShatter() { mType = PUV_SHATTER; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { if (mValue) unit->mShatter = TRUE; else unit->mShatter = FALSE; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitLockable : public CPhysicsUnitValue
{
public:
	CUnitLockable() { mType = PUV_LOCKABLE; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { if (mValue) unit->mCanBeLocked = TRUE; else unit->mCanBeLocked = FALSE; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitSweeping : public CPhysicsUnitValue
{
public:
	CUnitSweeping() { mType = PUV_SWEEPING; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { if (mValue) unit->mSweeping = TRUE; else unit->mSweeping = FALSE; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitIgnoreThreats : public CPhysicsUnitValue
{
public:
	CUnitIgnoreThreats() { mType = PUV_IGNORE_THREATS; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { if (mValue) unit->mIgnoreThreats = TRUE; else unit->mIgnoreThreats = FALSE; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitAnimatedCollision : public CPhysicsUnitValue
{
public:
	CUnitAnimatedCollision() { mType = PUV_ANIMATED_COLLISION; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { if (mValue) unit->mUnknown13C = TRUE; else unit->mUnknown13C = FALSE; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitWingman : public CPhysicsUnitValue
{
public:
	CUnitWingman() { mType = PUV_WINGMAN; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { if (mValue) unit->mUnknown120 = TRUE; else unit->mUnknown120 = FALSE; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};


class CUnitMultipleSpawn : public CPhysicsUnitValue
{
public:
	CUnitMultipleSpawn() { mType = PUV_MULTIPLE_SPAWN; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { if (mValue) unit->mUnknown110 = TRUE; else unit->mUnknown110 = FALSE; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitStrafeChange : public CPhysicsUnitValue
{
public:
	CUnitStrafeChange() { mType = PUV_STRAFE_CHANGE; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mStrafeChange = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitMinThrusterHeight : public CPhysicsUnitValue
{
public:
	CUnitMinThrusterHeight() { mType = PUV_MIN_THRUSTER_HEIGHT; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mMinThrusterHeight = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitMaxThrusterHeight : public CPhysicsUnitValue
{
public:
	CUnitMaxThrusterHeight() { mType = PUV_MAX_THRUSTER_HEIGHT; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mMaxThrusterHeight = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitDetonationDelay : public CPhysicsUnitValue
{
public:
	CUnitDetonationDelay() { mType = PUV_DETONATION_DELAY; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mDetonationDelay = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CUnitNavMap : public CPhysicsUnitValue
{
public:
	CUnitNavMap() { mType = PUV_NAV_MAP; mValue = NULL; }
	virtual			~CUnitNavMap() { delete mValue; }
	virtual void	Apply(CUnitData *unit)
	{
		if (mValue)
			unit->mUnknownFC = mValue->GetValue();
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file)
	{
		SINT	type;
		file.Read(&type, 4);
		mValue = CreateNavMapType(type);
	}

	CPhysicsNavMapType	*mValue;		// +0x08
};

class CUnitBehaviour : public CPhysicsUnitValue
{
public:
	CUnitBehaviour() { mType = PUV_BEHAVIOUR; mValue = NULL; }
	virtual			~CUnitBehaviour() { delete mValue; }
	virtual void	Apply(CUnitData *unit)
	{
		if (mValue)
		{
			unit->mBehaviour = (EUnitBehaviour)mValue->GetValue();
			switch (unit->mBehaviour)				// the behaviour's default navigation map
			{
			case UB_INFANTRY:		unit->mUnknownFC = 3;	break;
			case UB_MECH:			unit->mUnknownFC = 2;	break;
			case UB_GROUND_UNIT:	unit->mUnknownFC = 1;	break;
			case UB_BOAT:
			case UB_SUBMARINE:		unit->mUnknownFC = 0;	break;
			case UB_GILLM:			unit->mUnknownFC = 4;	break;
			}
		}
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file)
	{
		SINT	type;
		file.Read(&type, 4);
		mValue = CreateBehaviourType(type);
	}

	CPhysicsBehaviourType	*mValue;	// +0x08
};

// A unit's weapon (the definition's name and the mount it sits on).
class CUnitUse : public CPhysicsUnitValue
{
public:
	CUnitUse() { mType = PUV_USE; mUnknown208 = 0; mValue[0] = 0; mMount[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->AddWeapon(mValue, mMount, mUnknown208); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); AddStringSize(size, mMount); return size + 4; }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mValue);
		PhysicsReadString(file, mMount);
		file.Read(&mUnknown208, 4);
	}

	char			mValue[256];	// +0x08
	char			mMount[256];	// +0x108
	SINT			mUnknown208;	// +0x208
};

// A unit's spawner; the mount comes first in the object, the name first in the file.
class CUnitUseSpawner : public CPhysicsUnitValue
{
public:
	CUnitUseSpawner() { mType = PUV_USE_SPAWNER; mUnknown208 = 0; mValue[0] = 0; mMount[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->AddSpawner(mValue, mMount, mUnknown208); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); AddStringSize(size, mMount); return size + 4; }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mValue);
		PhysicsReadString(file, mMount);
		file.Read(&mUnknown208, 4);
	}

	char			mMount[256];	// +0x08
	char			mValue[256];	// +0x108
	SINT			mUnknown208;	// +0x208
};

class CUnitComponent : public CPhysicsUnitValue
{
public:
	CUnitComponent() { mType = PUV_COMPONENT; mName[0] = 0; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->AddComponent(mValue, mName); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mName); return size + 4; }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mName);
		file.Read(&mValue, 4);
	}

	SINT			mValue;			// +0x08
	char			mName[256];		// +0x0c
};

class CUnitBasedOn : public CPhysicsUnitValue
{
public:
	CUnitBasedOn() { mType = PUV_BASED_ON; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->CopyFrom(UPhysicsManager::GetUnitData(mValue)); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};



class CUnitMesh : public CPhysicsUnitValue
{
public:
	CUnitMesh() { mType = PUV_MESH; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetMesh(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitExplosion : public CPhysicsUnitValue
{
public:
	CUnitExplosion() { mType = PUV_EXPLOSION; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetExplosion(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitSmallExplosion : public CPhysicsUnitValue
{
public:
	CUnitSmallExplosion() { mType = PUV_SMALL_EXPLOSION; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetSmallExplosion(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitStompExplosion : public CPhysicsUnitValue
{
public:
	CUnitStompExplosion() { mType = PUV_STOMP_EXPLOSION; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetStompExplosion(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitActivationEffect : public CPhysicsUnitValue
{
public:
	CUnitActivationEffect() { mType = PUV_ACTIVATION_EFFECT; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetActivationEffect(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitNoise : public CPhysicsUnitValue
{
public:
	CUnitNoise() { mType = PUV_NOISE; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetNoise(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitSpawnNoise : public CPhysicsUnitValue
{
public:
	CUnitSpawnNoise() { mType = PUV_SPAWN_NOISE; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetSpawnNoise(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitTrails : public CPhysicsUnitValue
{
public:
	CUnitTrails() { mType = PUV_TRAILS; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetTrails(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitThrusters : public CPhysicsUnitValue
{
public:
	CUnitThrusters() { mType = PUV_THRUSTERS; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetThrusters(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitSmoke : public CPhysicsUnitValue
{
public:
	CUnitSmoke() { mType = PUV_SMOKE; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetSmoke(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitWaterWake : public CPhysicsUnitValue
{
public:
	CUnitWaterWake() { mType = PUV_WATER_WAKE; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetWaterWake(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitGroundWake : public CPhysicsUnitValue
{
public:
	CUnitGroundWake() { mType = PUV_GROUND_WAKE; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetGroundWake(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitEngines : public CPhysicsUnitValue
{
public:
	CUnitEngines() { mType = PUV_ENGINES; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetDust(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitDust : public CPhysicsUnitValue
{
public:
	CUnitDust() { mType = PUV_DUST; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetDust(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitVent : public CPhysicsUnitValue
{
public:
	CUnitVent() { mType = PUV_VENT; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetVent(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitAfterburner : public CPhysicsUnitValue
{
public:
	CUnitAfterburner() { mType = PUV_AFTERBURNER; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetAfterburner(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitShieldEffect : public CPhysicsUnitValue
{
public:
	CUnitShieldEffect() { mType = PUV_SHIELD_EFFECT; mValue[0] = 0; }
	virtual void	Apply(CUnitData *unit) { unit->SetShieldEffect(mValue); }
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CUnitBuildingNumber : public CPhysicsUnitValue
{
public:
	CUnitBuildingNumber() { mType = PUV_BUILDING_NUMBER; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mBuildingNumber = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CUnitBuildingMeshType : public CPhysicsUnitValue
{
public:
	CUnitBuildingMeshType() { mType = PUV_BUILDING_MESH_TYPE; mValue = 0; }
	virtual void	Apply(CUnitData *unit) { unit->mBuildingMeshType = mValue; }
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

// The weapon mode a charge level fires.
class CWeaponChargeLevel : public CPhysicsWeaponValue
{
public:
	CWeaponChargeLevel() { mType = PWV_CHARGE_LEVEL; mValue[0] = 0; mLevel = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon && mLevel >= 0 && mLevel < 5)
			weapon->mChargeModes[mLevel] = UPhysicsManager::GetWeaponModeIndex(mValue);
	}
	virtual SINT	GetSize() { SINT size = 4; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file)
	{
		file.Read(&mLevel, 4);
		PhysicsReadString(file, mValue);
	}

	char			mValue[256];	// +0x08: the mode's name
	SINT			mLevel;			// +0x108
};

class CWeaponChargeRate : public CPhysicsWeaponValue
{
public:
	CWeaponChargeRate() { mType = PWV_CHARGE_RATE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
			weapon->mChargeRate = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

// Clamps its own value to 0..5 as it applies it.
class CWeaponAmmoStore : public CPhysicsWeaponValue
{
public:
	CWeaponAmmoStore() { mType = PWV_AMMO_STORE; mValue = -1; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
		{
			if (mValue < 0)
				mValue = 0;
			else if (mValue > 5)
				mValue = 5;
			weapon->mAmmoStore = mValue;
		}
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;			// +0x08
};

class CWeaponConsumption : public CPhysicsWeaponValue
{
public:
	CWeaponConsumption() { mType = PWV_CONSUMPTION; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
			weapon->mConsumption = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponAdjustAim : public CPhysicsWeaponValue
{
public:
	CWeaponAdjustAim() { mType = PWV_ADJUST_AIM; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
			if (mValue != 0.0f) weapon->mAdjustAim = TRUE; else weapon->mAdjustAim = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponSmart : public CPhysicsWeaponValue
{
public:
	CWeaponSmart() { mType = PWV_SMART; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
			if (mValue != 0.0f) weapon->mSmart = TRUE; else weapon->mSmart = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponZoomMode : public CPhysicsWeaponValue
{
public:
	CWeaponZoomMode() { mType = PWV_ZOOM_MODE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
			if (mValue) weapon->mZoomMode = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CWeaponPlacement : public CPhysicsWeaponValue
{
public:
	CWeaponPlacement() { mType = PWV_PLACEMENT; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
			if (mValue) weapon->mPlacement = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CWeaponVersusInfantry : public CPhysicsWeaponValue
{
public:
	CWeaponVersusInfantry() { mType = PWV_VERSUS_INFANTRY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
			weapon->mRating[0] = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CWeaponVersusTanks : public CPhysicsWeaponValue
{
public:
	CWeaponVersusTanks() { mType = PWV_VERSUS_TANKS; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
			weapon->mRating[1] = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CWeaponVersusAir : public CPhysicsWeaponValue
{
public:
	CWeaponVersusAir() { mType = PWV_VERSUS_AIR; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
			weapon->mRating[2] = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CWeaponAllowMovement : public CPhysicsWeaponValue
{
public:
	CWeaponAllowMovement() { mType = PWV_ALLOW_MOVEMENT; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
			if (mValue) weapon->mAllowMovement = TRUE; else weapon->mAllowMovement = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CWeaponIconName : public CPhysicsWeaponValue
{
public:
	CWeaponIconName() { mType = PWV_ICON_NAME; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
			weapon->SetIconName(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

// A weapon-mode value that copies a whole weapon definition onto the weapon with the statement's name.
class CWeaponBasedOn : public CPhysicsWeaponModeValue
{
public:
	CWeaponBasedOn() { mType = PWMV_BASED_ON; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
		if (weapon)
		{
			CWeaponData	*other = UPhysicsManager::GetWeaponData(mValue);
			if (other)
			{
				weapon->mAmmoStore = other->mAmmoStore;
				weapon->mConsumption = other->mConsumption;
				weapon->mChargeRate = other->mChargeRate;
				weapon->mAdjustAim = other->mAdjustAim;
				weapon->mZoomMode = other->mZoomMode;
				weapon->mAllowMovement = other->mAllowMovement;
				weapon->mPlacement = other->mPlacement;
				weapon->mSmart = other->mSmart;
				weapon->mLanguageName = other->mLanguageName;
				for (SINT i = 0; i < 5; i++)
					weapon->mChargeModes[i] = other->mChargeModes[i];
				weapon->mRating[0] = other->mRating[0];
				weapon->mRating[1] = other->mRating[1];
				weapon->mRating[2] = other->mRating[2];
			}
		}
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

// One entry of the mode's launch sequence, by its position (1-based); the sequence grows to hold it.
class CWeaponLaunchSequence : public CPhysicsWeaponModeValue
{
public:
	CWeaponLaunchSequence() { mType = PWMV_LAUNCH_SEQUENCE; mIndex = 0; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
		{
			mode->SetLaunchSequence((SINT)mIndex, (SINT)mValue);
		}
	}
	virtual SINT	GetSize() { return 8; }
	virtual void	Load(CMEMBUFFER &file)
	{
		file.Read(&mIndex, 4);
		file.Read(&mValue, 4);
	}

	float			mIndex;			// +0x08
	float			mValue;			// +0x0c
};

// One launch angle pair, by its position (1-based); the list grows to hold it.
class CWeaponLaunchAngle : public CPhysicsWeaponModeValue
{
public:
	CWeaponLaunchAngle() { mType = PWMV_LAUNCH_ANGLE; mIndex = 0; mA = 0; mB = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
		{
			mode->SetLaunchAngle((SINT)mIndex, mA, mB);
		}
	}
	virtual SINT	GetSize() { return 12; }
	virtual void	Load(CMEMBUFFER &file)
	{
		file.Read(&mIndex, 4);
		file.Read(&mA, 4);
		file.Read(&mB, 4);
	}

	float			mIndex;			// +0x08
	float			mA;				// +0x0c
	float			mB;				// +0x10
};

class CWeaponInaccuracy : public CPhysicsWeaponModeValue
{
public:
	CWeaponInaccuracy() { mType = PWMV_INACCURACY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mInaccuracy = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponPower : public CPhysicsWeaponModeValue
{
public:
	CWeaponPower() { mType = PWMV_POWER; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mPower = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponMuzzleLight : public CPhysicsWeaponModeValue
{
public:
	CWeaponMuzzleLight() { mType = PWMV_MUZZLE_LIGHT; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mMuzzleLight = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CWeaponMuzzleLightRadius : public CPhysicsWeaponModeValue
{
public:
	CWeaponMuzzleLightRadius() { mType = PWMV_MUZZLE_LIGHT_RADIUS; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mMuzzleLightRadius = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponPredictive : public CPhysicsWeaponModeValue
{
public:
	CWeaponPredictive() { mType = PWMV_PREDICTIVE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			if (mValue > 0.0f) mode->mPredictive = TRUE; else mode->mPredictive = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponBurstSize : public CPhysicsWeaponModeValue
{
public:
	CWeaponBurstSize() { mType = PWMV_BURST_SIZE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mBurstSize = (SINT)mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponBurstDelay : public CPhysicsWeaponModeValue
{
public:
	CWeaponBurstDelay() { mType = PWMV_BURST_DELAY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mBurstDelay = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponVolleySize : public CPhysicsWeaponModeValue
{
public:
	CWeaponVolleySize() { mType = PWMV_VOLLEY_SIZE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mVolleySize = (SINT)mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponPreFireDelay : public CPhysicsWeaponModeValue
{
public:
	CWeaponPreFireDelay() { mType = PWMV_PRE_FIRE_DELAY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mUnknown88 = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponPostFireDelay : public CPhysicsWeaponModeValue
{
public:
	CWeaponPostFireDelay() { mType = PWMV_POST_FIRE_DELAY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mUnknown8C = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponPreFireEffect : public CPhysicsWeaponModeValue
{
public:
	CWeaponPreFireEffect() { mType = PWMV_PRE_FIRE_EFFECT; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->SetPreFireEffect(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CWeaponMuzzleEffect : public CPhysicsWeaponModeValue
{
public:
	CWeaponMuzzleEffect() { mType = PWMV_MUZZLE_EFFECT; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->SetMuzzleEffect(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CWeaponClip : public CPhysicsWeaponModeValue
{
public:
	CWeaponClip() { mType = PWMV_CLIP; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->SetClip(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CWeaponReloadTime : public CPhysicsWeaponModeValue
{
public:
	CWeaponReloadTime() { mType = PWMV_RELOAD_TIME; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mReloadTime = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponTrack : public CPhysicsWeaponModeValue
{
public:
	CWeaponTrack() { mType = PWMV_TRACK; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mTrack = mValue != 0.0f ? TRUE : FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponSoundPerBurst : public CPhysicsWeaponModeValue
{
public:
	CWeaponSoundPerBurst() { mType = PWMV_SOUND_PER_BURST; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mSoundPerBurst = mValue != 0.0f ? TRUE : FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponMinRange : public CPhysicsWeaponModeValue
{
public:
	CWeaponMinRange() { mType = PWMV_MIN_RANGE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mMinRange = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponMaxRange : public CPhysicsWeaponModeValue
{
public:
	CWeaponMaxRange() { mType = PWMV_MAX_RANGE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mMaxRange = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponMinDeflection : public CPhysicsWeaponModeValue
{
public:
	CWeaponMinDeflection() { mType = PWMV_MIN_DEFLECTION; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mMinDeflection = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponMaxDeflection : public CPhysicsWeaponModeValue
{
public:
	CWeaponMaxDeflection() { mType = PWMV_MAX_DEFLECTION; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mMaxDeflection = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponMinTargetHeight : public CPhysicsWeaponModeValue
{
public:
	CWeaponMinTargetHeight() { mType = PWMV_MIN_TARGET_HEIGHT; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mUnknown6C = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponMaxTargetHeight : public CPhysicsWeaponModeValue
{
public:
	CWeaponMaxTargetHeight() { mType = PWMV_MAX_TARGET_HEIGHT; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mUnknown70 = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponYawTolerance : public CPhysicsWeaponModeValue
{
public:
	CWeaponYawTolerance() { mType = PWMV_YAW_TOLERANCE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mYawTolerance = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponLockTime : public CPhysicsWeaponModeValue
{
public:
	CWeaponLockTime() { mType = PWMV_LOCK_TIME; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mLockTime = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponLockDeflection : public CPhysicsWeaponModeValue
{
public:
	CWeaponLockDeflection() { mType = PWMV_LOCK_DEFLECTION; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mLockDeflection = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponLockMode : public CPhysicsWeaponModeValue
{
public:
	CWeaponLockMode() { mType = PWMV_LOCK_MODE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mLockMode = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CWeaponLockUnit : public CPhysicsWeaponModeValue
{
public:
	CWeaponLockUnit() { mType = PWMV_LOCK_UNIT; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mLockUnit = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CWeaponLockRange : public CPhysicsWeaponModeValue
{
public:
	CWeaponLockRange() { mType = PWMV_LOCK_RANGE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mLockRange = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponLockRadius : public CPhysicsWeaponModeValue
{
public:
	CWeaponLockRadius() { mType = PWMV_LOCK_RADIUS; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mLockRadius = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponMaxLocks : public CPhysicsWeaponModeValue
{
public:
	CWeaponMaxLocks() { mType = PWMV_MAX_LOCKS; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->mMaxLocks = (SINT)mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CWeaponRound : public CPhysicsWeaponModeValue
{
public:
	CWeaponRound() { mType = PWMV_ROUND; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->SetRound(UPhysicsManager::GetRoundIndex(mValue));
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CWeaponLaunchSound : public CPhysicsWeaponModeValue
{
public:
	CWeaponLaunchSound() { mType = PWMV_LAUNCH_SOUND; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->SetLaunchSound(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CWeaponPreFireSound : public CPhysicsWeaponModeValue
{
public:
	CWeaponPreFireSound() { mType = PWMV_PRE_FIRE_SOUND; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->SetPreFireSound(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CWeaponPostFireSound : public CPhysicsWeaponModeValue
{
public:
	CWeaponPostFireSound() { mType = PWMV_POST_FIRE_SOUND; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CWeaponModeData	*mode = UPhysicsManager::GetWeaponModeData(name);
		if (mode)
			mode->SetPostFireSound(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CRoundBasedOn : public CPhysicsRoundValue
{
public:
	CRoundBasedOn() { mType = PRV_BASED_ON; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->CopyFrom(UPhysicsManager::GetRoundData(mValue));
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CRoundSoundMaterial : public CPhysicsRoundValue
{
public:
	CRoundSoundMaterial() { mType = PRV_SOUND_MATERIAL; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mSoundMaterial = (SINT)mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundDamage : public CPhysicsRoundValue
{
public:
	CRoundDamage() { mType = PRV_DAMAGE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mUnknown1C = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundRearm : public CPhysicsRoundValue
{
public:
	CRoundRearm() { mType = PRV_REARM; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mRearm = mValue / 100.0f;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundLifeSpan : public CPhysicsRoundValue
{
public:
	CRoundLifeSpan() { mType = PRV_LIFE_SPAN; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mLifeSpan = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundVelocity : public CPhysicsRoundValue
{
public:
	CRoundVelocity() { mType = PRV_VELOCITY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mSpeed = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundTurnRate : public CPhysicsRoundValue
{
public:
	CRoundTurnRate() { mType = PRV_TURN_RATE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mUnknown28 = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundGravity : public CPhysicsRoundValue
{
public:
	CRoundGravity() { mType = PRV_GRAVITY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mGravity = mValue / 100.0f;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundBounce : public CPhysicsRoundValue
{
public:
	CRoundBounce() { mType = PRV_BOUNCE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mBounceFactor = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundRadius : public CPhysicsRoundValue
{
public:
	CRoundRadius() { mType = PRV_RADIUS; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mRadius = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundWiggle : public CPhysicsRoundValue
{
public:
	CRoundWiggle() { mType = PRV_WIGGLE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mWiggle = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundFlak : public CPhysicsRoundValue
{
public:
	CRoundFlak() { mType = PRV_FLAK; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mFlak = TRUE;
	}
	virtual SINT	GetSize() { return 0; }
	virtual void	Load(CMEMBUFFER &file) {}
};

class CRoundFlakInaccuracy : public CPhysicsRoundValue
{
public:
	CRoundFlakInaccuracy() { mType = PRV_FLAK_INACCURACY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mFlakInaccuracy = mValue / 100.0f;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundBeam : public CPhysicsRoundValue
{
public:
	CRoundBeam() { mType = PRV_BEAM; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mBeam = TRUE;
	}
	virtual SINT	GetSize() { return 0; }
	virtual void	Load(CMEMBUFFER &file) {}
};

class CRoundSeekDelay : public CPhysicsRoundValue
{
public:
	CRoundSeekDelay() { mType = PRV_SEEK_DELAY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mSeekDelay = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundProximity : public CPhysicsRoundValue
{
public:
	CRoundProximity() { mType = PRV_PROXIMITY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mProximity = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundSeekAngle : public CPhysicsRoundValue
{
public:
	CRoundSeekAngle() { mType = PRV_SEEK_ANGLE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mSeekAngle = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundSeekTerminationTime : public CPhysicsRoundValue
{
public:
	CRoundSeekTerminationTime() { mType = PRV_SEEK_TERMINATION_TIME; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mSeekTerminationTime = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundGridOfFear : public CPhysicsRoundValue
{
public:
	CRoundGridOfFear() { mType = PRV_GRID_OF_FEAR; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mUnknown58 = (SINT)mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundUnderWater : public CPhysicsRoundValue
{
public:
	CRoundUnderWater() { mType = PRV_UNDER_WATER; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mCanGoUnderWater = (SINT)mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundGroundHugging : public CPhysicsRoundValue
{
public:
	CRoundGroundHugging() { mType = PRV_GROUND_HUGGING; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mGroundHugging = (SINT)mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundTorpedo : public CPhysicsRoundValue
{
public:
	CRoundTorpedo() { mType = PRV_TORPEDO; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			if (mValue != 0.0f) round->mUnknown6C = TRUE; else round->mUnknown6C = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundMissile : public CPhysicsRoundValue
{
public:
	CRoundMissile() { mType = PRV_MISSILE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			if (mValue != 0.0f) round->mUnknown70 = TRUE; else round->mUnknown70 = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundFire : public CPhysicsRoundValue
{
public:
	CRoundFire() { mType = PRV_FIRE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			if (mValue != 0.0f) round->mUnknown60 = TRUE; else round->mUnknown60 = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundSmart : public CPhysicsRoundValue
{
public:
	CRoundSmart() { mType = PRV_SMART; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			if (mValue != 0.0f) round->mSmart = TRUE; else round->mSmart = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundPassiveCollision : public CPhysicsRoundValue
{
public:
	CRoundPassiveCollision() { mType = PRV_PASSIVE_COLLISION; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mPassiveCollision = (SINT)mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundJumps : public CPhysicsRoundValue
{
public:
	CRoundJumps() { mType = PRV_JUMPS; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mJumps = (SINT)mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundJumpRange : public CPhysicsRoundValue
{
public:
	CRoundJumpRange() { mType = PRV_JUMP_RANGE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mUnknown90 = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundJumpDelay : public CPhysicsRoundValue
{
public:
	CRoundJumpDelay() { mType = PRV_JUMP_DELAY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mJumpDelay = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundLength : public CPhysicsRoundValue
{
public:
	CRoundLength() { mType = PRV_LENGTH; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mLength = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CRoundExplode : public CPhysicsRoundValue
{
public:
	CRoundExplode() { mType = PRV_EXPLODE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			if (mValue > 0) round->mExplode = TRUE; else round->mExplode = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CRoundWeirdoSeek : public CPhysicsRoundValue
{
public:
	CRoundWeirdoSeek() { mType = PRV_WEIRDO_SEEK; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mWeirdoSeek = TRUE;
	}
	virtual SINT	GetSize() { return 0; }
	virtual void	Load(CMEMBUFFER &file) {}
};

class CRoundSeek : public CPhysicsRoundValue
{
public:
	CRoundSeek() { mType = PRV_SEEK; mValue = NULL; }
	virtual			~CRoundSeek() { delete mValue; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mUnknown48 = mValue->GetValue();
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file)
	{
		SINT	type;
		file.Read(&type, 4);
		mValue = CreateSeekType(type);
	}

	CPhysicsSeekType	*mValue;	// +0x08
};

class CRoundMesh : public CPhysicsRoundValue
{
public:
	CRoundMesh() { mType = PRV_MESH; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->SetMesh(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CRoundEffect : public CPhysicsRoundValue
{
public:
	CRoundEffect() { mType = PRV_EFFECT; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->SetEffect(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CRoundWaterEffect : public CPhysicsRoundValue
{
public:
	CRoundWaterEffect() { mType = PRV_WATER_EFFECT; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->SetWaterEffect(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CRoundExplosion : public CPhysicsRoundValue
{
public:
	CRoundExplosion() { mType = PRV_EXPLOSION; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->SetExplosion(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CRoundTreeCollision : public CPhysicsRoundValue
{
public:
	CRoundTreeCollision() { mType = PRV_TREE_COLLISION; mValue = NULL; }
	virtual			~CRoundTreeCollision() { delete mValue; }
	virtual void	Apply(char *name)
	{
		CRoundData	*round = UPhysicsManager::GetRoundData(name);
		if (round)
			round->mTreeCollision = mValue->GetValue();
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file)
	{
		SINT	type;
		file.Read(&type, 4);
		mValue = CreateStateType(type);
	}

	CPhysicsStateType	*mValue;	// +0x08
};

class CSpawnerBasedOn : public CPhysicsSpawnerValue
{
public:
	CSpawnerBasedOn() { mType = PSV_BASED_ON; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->CopyFrom(UPhysicsManager::GetSpawnerData(mValue));
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CSpawnerUnit : public CPhysicsSpawnerValue
{
public:
	CSpawnerUnit() { mType = PSV_UNIT; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->SetThingName(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CSpawnerDelay : public CPhysicsSpawnerValue
{
public:
	CSpawnerDelay() { mType = PSV_DELAY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->mDelay = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CSpawnerAmount : public CPhysicsSpawnerValue
{
public:
	CSpawnerAmount() { mType = PSV_AMOUNT; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->mAmount = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CSpawnerConditions : public CPhysicsSpawnerValue
{
public:
	CSpawnerConditions() { mType = PSV_CONDITIONS; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->mConditions = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CSpawnerSquadSize : public CPhysicsSpawnerValue
{
public:
	CSpawnerSquadSize() { mType = PSV_SQUAD_SIZE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->mSquadSize = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CSpawnerSquadDelay : public CPhysicsSpawnerValue
{
public:
	CSpawnerSquadDelay() { mType = PSV_SQUAD_DELAY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->mSquadDelay = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CSpawnerSeekDelay : public CPhysicsSpawnerValue
{
public:
	CSpawnerSeekDelay() { mType = PSV_SEEK_DELAY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->mSeekDelay = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CSpawnerRecall : public CPhysicsSpawnerValue
{
public:
	CSpawnerRecall() { mType = PSV_RECALL; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->mRecall = TRUE;
	}
	virtual SINT	GetSize() { return 0; }
	virtual void	Load(CMEMBUFFER &file) {}
};

class CSpawnerMinRange : public CPhysicsSpawnerValue
{
public:
	CSpawnerMinRange() { mType = PSV_MIN_RANGE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->mMinRange = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CSpawnerMaxRange : public CPhysicsSpawnerValue
{
public:
	CSpawnerMaxRange() { mType = PSV_MAX_RANGE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->mMaxRange = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CSpawnerPreSpawnDelay : public CPhysicsSpawnerValue
{
public:
	CSpawnerPreSpawnDelay() { mType = PSV_PRE_SPAWN_DELAY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->mPreSpawnDelay = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CSpawnerPostSpawnDelay : public CPhysicsSpawnerValue
{
public:
	CSpawnerPostSpawnDelay() { mType = PSV_POST_SPAWN_DELAY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->mPostSpawnDelay = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CSpawnerInfinite : public CPhysicsSpawnerValue
{
public:
	CSpawnerInfinite() { mType = PSV_INFINITE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CSpawnerData	*spawner = UPhysicsManager::GetSpawnerData(name);
		if (spawner)
			spawner->mInfinite = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CExplosionBasedOn : public CPhysicsExplosionValue
{
public:
	CExplosionBasedOn() { mType = PEV_BASED_ON; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->CopyFrom(UPhysicsManager::GetExplosion(mValue));
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CExplosionAirEffect : public CPhysicsExplosionValue
{
public:
	CExplosionAirEffect() { mType = PEV_AIR_EFFECT; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->SetAirEffect(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CExplosionGroundEffect : public CPhysicsExplosionValue
{
public:
	CExplosionGroundEffect() { mType = PEV_GROUND_EFFECT; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->SetGroundEffect(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CExplosionWaterEffect : public CPhysicsExplosionValue
{
public:
	CExplosionWaterEffect() { mType = PEV_WATER_EFFECT; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->SetWaterEffect(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CExplosionUnitEffect : public CPhysicsExplosionValue
{
public:
	CExplosionUnitEffect() { mType = PEV_UNIT_EFFECT; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->SetUnitEffect(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CExplosionRadius : public CPhysicsExplosionValue
{
public:
	CExplosionRadius() { mType = PEV_RADIUS; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->mRadius = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CExplosionDamage : public CPhysicsExplosionValue
{
public:
	CExplosionDamage() { mType = PEV_DAMAGE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->mDamage = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CExplosionVolumetric : public CPhysicsExplosionValue
{
public:
	CExplosionVolumetric() { mType = PEV_VOLUMETRIC; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->mVolumetric = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CExplosionSmart : public CPhysicsExplosionValue
{
public:
	CExplosionSmart() { mType = PEV_SMART; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->mSmart = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CExplosionOriented : public CPhysicsExplosionValue
{
public:
	CExplosionOriented() { mType = PEV_ORIENTED; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->mOriented = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CExplosionLight : public CPhysicsExplosionValue
{
public:
	CExplosionLight() { mType = PEV_LIGHT; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->mLight = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CExplosionTime : public CPhysicsExplosionValue
{
public:
	CExplosionTime() { mType = PEV_TIME; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->mTime = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CExplosionSound : public CPhysicsExplosionValue
{
public:
	CExplosionSound() { mType = PEV_SOUND; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->SetSound(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

// Read and never applied.
class CExplosionShockwave : public CPhysicsExplosionValue
{
public:
	CExplosionShockwave() { mType = PEV_SHOCKWAVE; mValue = 0; }
	virtual void	Apply(char *name) {}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;			// +0x08
};

class CExplosionWaterSound : public CPhysicsExplosionValue
{
public:
	CExplosionWaterSound() { mType = PEV_WATER_SOUND; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CExplosionData	*explosion = UPhysicsManager::GetExplosion(name);
		if (explosion)
			explosion->SetWaterSound(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CFeatureLife : public CPhysicsFeatureValue
{
public:
	CFeatureLife() { mType = PFV_LIFE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CFeatureData	*feature = UPhysicsManager::GetFeatureData(name);
		if (feature)
			feature->mLife = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CFeatureIcon : public CPhysicsFeatureValue
{
public:
	CFeatureIcon() { mType = PFV_ICON; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CFeatureData	*feature = UPhysicsManager::GetFeatureData(name);
		if (feature)
			feature->mIcon = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;		// +0x08
};

class CFeatureInvincible : public CPhysicsFeatureValue
{
public:
	CFeatureInvincible() { mType = PFV_INVINCIBLE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CFeatureData	*feature = UPhysicsManager::GetFeatureData(name);
		if (feature)
			if (mValue != 0.0f) feature->mInvincible = TRUE; else feature->mInvincible = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CFeatureLockable : public CPhysicsFeatureValue
{
public:
	CFeatureLockable() { mType = PFV_LOCKABLE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CFeatureData	*feature = UPhysicsManager::GetFeatureData(name);
		if (feature)
			if (mValue != 0.0f) feature->mUnknown14 = TRUE; else feature->mUnknown14 = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CFeatureMesh : public CPhysicsFeatureValue
{
public:
	CFeatureMesh() { mType = PFV_MESH; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CFeatureData	*feature = UPhysicsManager::GetFeatureData(name);
		if (feature)
			feature->SetMesh(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CFeatureNoise : public CPhysicsFeatureValue
{
public:
	CFeatureNoise() { mType = PFV_NOISE; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CFeatureData	*feature = UPhysicsManager::GetFeatureData(name);
		if (feature)
			feature->SetNoise(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CFeatureExplosion : public CPhysicsFeatureValue
{
public:
	CFeatureExplosion() { mType = PFV_EXPLOSION; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CFeatureData	*feature = UPhysicsManager::GetFeatureData(name);
		if (feature)
			feature->SetExplosion(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CHazardRadius : public CPhysicsHazardValue
{
public:
	CHazardRadius() { mType = PHV_RADIUS; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CHazardData	*hazard = UPhysicsManager::GetHazardData(name);
		if (hazard)
			hazard->mRadius = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CHazardDamage : public CPhysicsHazardValue
{
public:
	CHazardDamage() { mType = PHV_DAMAGE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CHazardData	*hazard = UPhysicsManager::GetHazardData(name);
		if (hazard)
			hazard->mDamage = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CHazardNoise : public CPhysicsHazardValue
{
public:
	CHazardNoise() { mType = PHV_NOISE; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CHazardData	*hazard = UPhysicsManager::GetHazardData(name);
		if (hazard)
			hazard->SetNoise(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CHazardEffect : public CPhysicsHazardValue
{
public:
	CHazardEffect() { mType = PHV_EFFECT; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CHazardData	*hazard = UPhysicsManager::GetHazardData(name);
		if (hazard)
			hazard->SetEffect(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CComponentMaxYaw : public CPhysicsComponentValue
{
public:
	CComponentMaxYaw() { mType = PCV_MAX_YAW; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->mMaxYaw = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentMaxTurretYaw : public CPhysicsComponentValue
{
public:
	CComponentMaxTurretYaw() { mType = PCV_MAX_TURRET_YAW; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->mUnknownDC = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentLife : public CPhysicsComponentValue
{
public:
	CComponentLife() { mType = PCV_LIFE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->mUnknownC0 = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentMaxTargetRange : public CPhysicsComponentValue
{
public:
	CComponentMaxTargetRange() { mType = PCV_MAX_TARGET_RANGE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->mMaxTargetRange = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentTurnRate : public CPhysicsComponentValue
{
public:
	CComponentTurnRate() { mType = PCV_TURN_RATE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->mTurnRate = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentTurretTurnRate : public CPhysicsComponentValue
{
public:
	CComponentTurretTurnRate() { mType = PCV_TURRET_TURN_RATE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->mUnknownBC = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentBig : public CPhysicsComponentValue
{
public:
	CComponentBig() { mType = PCV_BIG; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			if (mValue != 0.0f) component->mUnknown124 = TRUE; else component->mUnknown124 = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentIndiscriminate : public CPhysicsComponentValue
{
public:
	CComponentIndiscriminate() { mType = PCV_INDISCRIMINATE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			if (mValue != 0.0f) component->mUnknown128 = TRUE; else component->mUnknown128 = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentTentacle : public CPhysicsComponentValue
{
public:
	CComponentTentacle() { mType = PCV_TENTACLE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			if (mValue != 0.0f) component->mUnknown12C = TRUE; else component->mUnknown12C = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentDetach : public CPhysicsComponentValue
{
public:
	CComponentDetach() { mType = PCV_DETACH; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			if (mValue != 0.0f) component->mUnknown198 = TRUE; else component->mUnknown198 = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentLockable : public CPhysicsComponentValue
{
public:
	CComponentLockable() { mType = PCV_LOCKABLE; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			if (mValue != 0.0f) component->mCanBeLocked = TRUE; else component->mCanBeLocked = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentSweeping : public CPhysicsComponentValue
{
public:
	CComponentSweeping() { mType = PCV_SWEEPING; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			if (mValue != 0.0f) component->mSweeping = TRUE; else component->mSweeping = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentPassOnHitEffect : public CPhysicsComponentValue
{
public:
	CComponentPassOnHitEffect() { mType = PCV_PASS_ON_HIT_EFFECT; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			if (mValue != 0.0f) component->mUnknown134 = TRUE; else component->mUnknown134 = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentDeploy : public CPhysicsComponentValue
{
public:
	CComponentDeploy() { mType = PCV_DEPLOY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			if (mValue != 0.0f) component->mUnknown108 = TRUE; else component->mUnknown108 = FALSE;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentDeployDamageMultiplier : public CPhysicsComponentValue
{
public:
	CComponentDeployDamageMultiplier() { mType = PCV_DEPLOY_DAMAGE_MULTIPLIER; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->mUnknown160 = mValue;
	}
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	float			mValue;		// +0x08
};

class CComponentAttackPriority : public CPhysicsComponentValue
{
public:
	CComponentAttackPriority() { mType = PCV_ATTACK_PRIORITY; mValue = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->mAttackPriority[mIndex] = mValue;
	}
	virtual SINT	GetSize() { return 8; }
	virtual void	Load(CMEMBUFFER &file)
	{
		file.Read(&mValue, 4);
		file.Read(&mIndex, 4);
	}

	float			mValue;			// +0x08
	SINT			mIndex;			// +0x0c
};

class CComponentUse : public CPhysicsComponentValue
{
public:
	CComponentUse() { mType = PCV_USE; mUnknown208 = 0; mValue[0] = 0; mMount[0] = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->AddWeapon(mValue, mMount, mUnknown208);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); AddStringSize(size, mMount); return size + 4; }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mValue);
		PhysicsReadString(file, mMount);
		file.Read(&mUnknown208, 4);
	}

	char			mValue[256];	// +0x08
	char			mMount[256];	// +0x108
	SINT			mUnknown208;	// +0x208
};

class CComponentUseSpawner : public CPhysicsComponentValue
{
public:
	CComponentUseSpawner() { mType = PCV_USE_SPAWNER; mUnknown208 = 0; mValue[0] = 0; mMount[0] = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->AddSpawner(mValue, mMount, mUnknown208);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); AddStringSize(size, mMount); return size + 4; }
	virtual void	Load(CMEMBUFFER &file)
	{
		PhysicsReadString(file, mValue);
		PhysicsReadString(file, mMount);
		file.Read(&mUnknown208, 4);
	}

	char			mMount[256];	// +0x08
	char			mValue[256];	// +0x108
	SINT			mUnknown208;	// +0x208
};

class CComponentMesh : public CPhysicsComponentValue
{
public:
	CComponentMesh() { mType = PCV_MESH; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->SetMesh(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CComponentExplosion : public CPhysicsComponentValue
{
public:
	CComponentExplosion() { mType = PCV_EXPLOSION; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->SetExplosion(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CComponentVent : public CPhysicsComponentValue
{
public:
	CComponentVent() { mType = PCV_VENT; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->SetVent(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CComponentSmallExplosion : public CPhysicsComponentValue
{
public:
	CComponentSmallExplosion() { mType = PCV_SMALL_EXPLOSION; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->SetSmallExplosion(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CComponentNoise : public CPhysicsComponentValue
{
public:
	CComponentNoise() { mType = PCV_NOISE; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->SetNoise(mValue);
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

class CComponentBasedOn : public CPhysicsComponentValue
{
public:
	CComponentBasedOn() { mType = PCV_BASED_ON; mValue[0] = 0; }
	virtual void	Apply(char *name)
	{
		CUnitData	*component = UPhysicsManager::GetComponentData(name);
		if (component)
			component->CopyFrom(UPhysicsManager::GetComponentData(mValue));
	}
	virtual SINT	GetSize() { SINT size = 0; AddStringSize(size, mValue); return size; }
	virtual void	Load(CMEMBUFFER &file) { PhysicsReadString(file, mValue); }

	char			mValue[256];	// +0x08
};

// The weapon's name in the player's language (Apply is in CPhysicsScriptStatements.cpp).
class CWeaponLanguageName : public CPhysicsWeaponValue
{
public:
	CWeaponLanguageName() { mType = PWV_LANGUAGE_NAME; mValue = 0; }
	virtual void	Apply(char *name);
	virtual SINT	GetSize() { return 4; }
	virtual void	Load(CMEMBUFFER &file) { file.Read(&mValue, 4); }

	SINT			mValue;			// +0x08
};

#endif
