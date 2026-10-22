// WorldPhysicsManager.cpp: reconstructed from WorldPhysicsManager.obj (0050de90..00511eef); Stuart's release has no
// copy. The allocations' __LINE__ values are the retail ones; the #line directives place them, because the retail
// file's layout is unknown.
#include "common.h"
class CHazard;class CFeature;class CExplosion;class CGillMHead;class CComponent;class CTentacle;class CMissile;class CRelaxedSquad;class CAirUnit;class CBigAirUnit;class CGroundUnit;class CCannon;class CHiveBoss;class CGroundVehicle;class CGroundAttackAircraft;class CThunderHead;class CFenrir;class CBoat;class CMech;class CDropship;class CSentinel;class CSubmarine;class CPod;class CPlane;class CCarrier;class CInfantryUnit;class CWarspite;class CCarver;class CGillM;class CMine;class CDiveBomber;class CBomber;class CSimpleBuilding;class CWarspiteDome;class CBuilding;	// STAND-IN (end-stage RTTI order): retail's ??_R4 order implies these type records were created in this order (an unidentified header mention); found by an RTTI-order search
#include "SpawnerThng.h"
#include "weapon.h"
#include "WorldPhysicsManager.h"
#include "BattleEngineDataManager.h"
#include "DebugLog.h"
#include "console.h"
#include "explosion.h"
#include "hazard.h"
#include "feature.h"
#include "missile.h"
#include "unit.h"
#include "squad.h"
#include "component.h"
#include "CPhysicsScript.h"
// What the manager reaches outside its own files; declared here until their headers are reconstructed.

SPtrSet<CWeaponData>		*UPhysicsManager::mWeapons = NULL;
SPtrSet<CWeaponModeData>	*UPhysicsManager::mWeaponModes = NULL;
SPtrSet<CRoundData>			*UPhysicsManager::mRounds = NULL;
SPtrSet<CSpawnerData>		*UPhysicsManager::mSpawners = NULL;
SPtrSet<CExplosionData>		*UPhysicsManager::mExplosions = NULL;
SPtrSet<CUnitData>			*UPhysicsManager::mUnits = NULL;
SPtrSet<CUnitData>			*UPhysicsManager::mComponents = NULL;
SPtrSet<CFeatureData>		*UPhysicsManager::mFeatures = NULL;
SPtrSet<CHazardData>		*UPhysicsManager::mHazards = NULL;

// FUNCTION: 0x0050de90 init:ZERO_FVECTOR
// FUNCTION: 0x0050deb0 init:ID_FMATRIX

//******************************************************************************************
#line 64
CThing *UPhysicsManager::SpawnThing(SINT unit)	// FUNCTION: 0x0050df80
{
	if (MEM_MANAGER.GetThingHeap()->GetFreeSize() < 10240)
		return NULL;

	CUnitData	*data = GetUnitData(unit);
	if (!data)
		return NULL;

	switch (data->mBehaviour)
	{
	case UB_MECH:					return new(MT_UNIT_THING) CMech;
	case UB_BOMBER:					return new(MT_UNIT_THING) CBomber;
	case UB_GROUND_ATTACK_AIRCRAFT:	return new(MT_UNIT_THING) CGroundAttackAircraft;
	case UB_GROUND_UNIT:			return new(MT_UNIT_THING) CGroundVehicle;
	case UB_INFANTRY:				return new(MT_UNIT_THING) CInfantryUnit;
	case UB_TURRET:					return new(MT_UNIT_THING) CCannon;
	case UB_BOAT:					return new(MT_UNIT_THING) CBoat;
	case UB_CARRIER:				return new(MT_UNIT_THING) CCarrier;
	case UB_BUILDING:				return new(MT_UNIT_THING) CBuilding;
	case UB_JEEP:					break;
	case UB_DROPSHIP:				return new(MT_UNIT_THING) CDropship;
	case UB_MINE:					return new(MT_UNIT_THING) CMine;
	case UB_AIRCRAFT: case UB_FIGHTER:	return new(MT_UNIT_THING) CPlane;
	case UB_HIVE_BOSS:				return new(MT_UNIT_THING) CHiveBoss;
	case UB_SUBMARINE:				return new(MT_UNIT_THING) CSubmarine;
	case UB_DIVE_BOMBER:			return new(MT_UNIT_THING) CDiveBomber;
	case UB_THUNDERHEAD:			return new(MT_UNIT_THING) CThunderHead;
	case UB_CARVER:					return new(MT_UNIT_THING) CCarver;
	case UB_GILLM:					return new(MT_UNIT_THING) CGillM;
	case UB_SENTINEL:				return new(MT_UNIT_THING) CSentinel;
	case UB_WARSPITE:				return new(MT_UNIT_THING) CWarspite;
	case UB_FENRIR:					return new(MT_UNIT_THING) CFenrir;
	case UB_WARSPITE_DOME:			return new(MT_UNIT_THING) CWarspiteDome;
	case UB_POD:					return new(MT_UNIT_THING) CPod;
	case UB_SIMPLE_BUILDING:		return new(MT_UNIT_THING) CSimpleBuilding;
	}
	return NULL;
}

// The unit classes' inline members and constructors SpawnThing's vtables and allocations make this object emit
// (unit.h).
// FUNCTION: 0x0050e860 ?GetMaxVelocity@CAirUnit@@UAEMXZ
// FUNCTION: 0x0050e870 ?SetThingType@CAirUnit@@UAEXK@Z
// FUNCTION: 0x0050e890 ?Unknown112@CGroundVehicle@@UAEMXZ
// FUNCTION: 0x0050e8a0 ?_GetClassName@CPlane@@UAEPADXZ
// FUNCTION: 0x0050e8b0 ?GetMaxVelocity@CPlane@@UAEMXZ
// FUNCTION: 0x0050e8c0 ?Gravity@CPlane@@UAEMXZ
// FUNCTION: 0x0050e8e0 ?Unknown94@CPlane@@UAEMXZ
// FUNCTION: 0x0050e8f0 ?_GetClassName@CBomber@@UAEPADXZ
// FUNCTION: 0x0050e900 ?Gravity@CBomber@@UAEMXZ
// FUNCTION: 0x0050e920 ?_GetClassName@CGroundAttackAircraft@@UAEPADXZ
// FUNCTION: 0x0050e930 ?Unknown73@CGroundAttackAircraft@@UAEMXZ
// FUNCTION: 0x0050e940 ?GetMoveMultiplier@CGroundUnit@@UAEMXZ
// FUNCTION: 0x0050e950 ?_GetClassName@CGroundVehicle@@UAEPADXZ
// FUNCTION: 0x0050e960 ?COfGHeight@CGroundVehicle@@UAEMXZ
// FUNCTION: 0x0050e970 ?SetThingType@CGroundVehicle@@UAEXK@Z
// FUNCTION: 0x0050e990 ?_GetClassName@CMech@@UAEPADXZ
// FUNCTION: 0x0050e9a0 ?GetMaxVelocity@CMech@@UAEMXZ
// FUNCTION: 0x0050e9b0 ?SetThingType@CMech@@UAEXK@Z
// FUNCTION: 0x0050e9d0 ?_GetClassName@CInfantryUnit@@UAEPADXZ
// FUNCTION: 0x0050e9e0 ?SetThingType@CInfantryUnit@@UAEXK@Z
// FUNCTION: 0x0050ea00 ?GetMoveMultiplier@CInfantryUnit@@UAEMXZ
// FUNCTION: 0x0050ea10 ?_GetClassName@CCannon@@UAEPADXZ
// FUNCTION: 0x0050ea20 ?SetThingType@CCannon@@UAEXK@Z
// FUNCTION: 0x0050ea40 ?_GetClassName@CBoat@@UAEPADXZ
// FUNCTION: 0x0050ea50 ?SetThingType@CBoat@@UAEXK@Z
// FUNCTION: 0x0050ea70 ?_GetClassName@CCarrier@@UAEPADXZ
// FUNCTION: 0x0050ea80 ?SetThingType@CCarrier@@UAEXK@Z
// FUNCTION: 0x0050eaa0 ?_GetClassName@CDropship@@UAEPADXZ
// FUNCTION: 0x0050eab0 ?SetThingType@CDropship@@UAEXK@Z
// FUNCTION: 0x0050ead0 ?Unknown94@CDropship@@UAEMXZ
// FUNCTION: 0x0050eae0 ?_GetClassName@CMine@@UAEPADXZ
// FUNCTION: 0x0050eaf0 ?GetMaxVelocity@CGroundVehicle@@UAEMXZ
// FUNCTION: 0x0050eb00 ?GetStealth@CMine@@UAEMXZ
// FUNCTION: 0x0050eb10 ?_GetClassName@CHiveBoss@@UAEPADXZ
// FUNCTION: 0x0050eb20 ?SetThingType@CHiveBoss@@UAEXK@Z
// FUNCTION: 0x0050eb40 ?Gravity@CHiveBoss@@UAEMXZ
// FUNCTION: 0x0050eb50 ?_GetClassName@CSubmarine@@UAEPADXZ
// FUNCTION: 0x0050eb60 ?Unknown73@CDropship@@UAEMXZ
// FUNCTION: 0x0050eb70 ?SetThingType@CSubmarine@@UAEXK@Z
// FUNCTION: 0x0050eb90 ?GetMaxVelocity@CSubmarine@@UAEMXZ
// FUNCTION: 0x0050eba0 ?_GetClassName@CDiveBomber@@UAEPADXZ
// FUNCTION: 0x0050ebb0 ?Gravity@CDiveBomber@@UAEMXZ
// FUNCTION: 0x0050ebd0 ?_GetClassName@CThunderHead@@UAEPADXZ
// FUNCTION: 0x0050ebe0 ?SetThingType@CThunderHead@@UAEXK@Z
// FUNCTION: 0x0050ec00 ?_GetClassName@CCarver@@UAEPADXZ
// FUNCTION: 0x0050ec10 ?GetStealth@CCarver@@UAEMXZ
// FUNCTION: 0x0050ec20 ?Unknown114@CCarver@@UAEXM@Z
// FUNCTION: 0x0050ec30 ?_GetClassName@CGillM@@UAEPADXZ
// FUNCTION: 0x0050ec40 ?SetThingType@CGillM@@UAEXK@Z
// FUNCTION: 0x0050ec60 ?_GetClassName@CSentinel@@UAEPADXZ
// FUNCTION: 0x0050ec70 ?SetThingType@CSentinel@@UAEXK@Z
// FUNCTION: 0x0050ec90 ?_GetClassName@CWarspite@@UAEPADXZ
// FUNCTION: 0x0050eca0 ?_GetClassName@CWarspiteDome@@UAEPADXZ
// FUNCTION: 0x0050ecb0 ?_GetClassName@CFenrir@@UAEPADXZ
// FUNCTION: 0x0050ecc0 ?GetRadius@CFenrir@@UAEMXZ
// FUNCTION: 0x0050ece0 ?Unknown111@CFenrir@@UAEMXZ
// FUNCTION: 0x0050ecf0 ?_GetClassName@CPod@@UAEPADXZ
// FUNCTION: 0x0050ed00 ?_GetClassName@CSimpleBuilding@@UAEPADXZ
// FUNCTION: 0x0050ed10 ??0CGroundUnit@@QAE@XZ
// FUNCTION: 0x0050ed30 ?_GetClassName@CGroundUnit@@UAEPADXZ
// FUNCTION: 0x0050ed40 ?SetThingType@CGroundUnit@@UAEXK@Z
// FUNCTION: 0x0050ed60 ??_GCBomber@@UAEPAXI@Z
// FUNCTION: 0x0050ed80 ??0CBigAirUnit@@QAE@XZ
// FUNCTION: 0x0050ee00 ?_GetClassName@CBigAirUnit@@UAEPADXZ
// FUNCTION: 0x0050ee10 ??_GCGroundAttackAircraft@@UAEPAXI@Z
// FUNCTION: 0x0050ee30 ??_GCInfantryUnit@@UAEPAXI@Z
// FUNCTION: 0x0050ee50 ??_GCCarrier@@UAEPAXI@Z
// FUNCTION: 0x0050ee70 ??_GCDropship@@UAEPAXI@Z
// FUNCTION: 0x0050ee90 ??_GCGroundUnit@@UAEPAXI@Z
// FUNCTION: 0x0050eeb0 ??_GCPlane@@UAEPAXI@Z
// FUNCTION: 0x0050eed0 ??_GCDiveBomber@@UAEPAXI@Z
// FUNCTION: 0x0050eef0 ??_GCCarver@@UAEPAXI@Z
// FUNCTION: 0x0050ef10 ??_GCFenrir@@UAEPAXI@Z
// FUNCTION: 0x0050ef30 ??1CCarrier@@UAE@XZ
// FUNCTION: 0x0050efa0 ??1CBomber@@UAE@XZ
// FUNCTION: 0x0050f010 ??_GCBigAirUnit@@UAEPAXI@Z
// FUNCTION: 0x0050f030 ??1CBigAirUnit@@UAE@XZ
// FUNCTION: 0x0050f0a0 ??0CAirUnit@@QAE@XZ
// FUNCTION: 0x0050f120 ?_GetClassName@CAirUnit@@UAEPADXZ
// FUNCTION: 0x0050f130 ??1CGroundAttackAircraft@@UAE@XZ
// FUNCTION: 0x0050f1a0 ??1CInfantryUnit@@UAE@XZ
// FUNCTION: 0x0050f1f0 ??1CDropship@@UAE@XZ
// FUNCTION: 0x0050f260 ??1CPlane@@UAE@XZ
// FUNCTION: 0x0050f2d0 ??1CDiveBomber@@UAE@XZ
// FUNCTION: 0x0050f340 ??1CCarver@@UAE@XZ
// FUNCTION: 0x0050f3b0 ??1CFenrir@@UAE@XZ
// FUNCTION: 0x0050f420 ??_GCAirUnit@@UAEPAXI@Z
// FUNCTION: 0x0050f440 ??1CAirUnit@@UAE@XZ

//******************************************************************************************
#line 132
CSquad *UPhysicsManager::SpawnSquad(SINT behaviour)	// FUNCTION: 0x0050f4b0
{
	if (MEM_MANAGER.IsThingHeapFull())
	{
		printf("Thing heap full\n");
		MEM_MANAGER.GetThingHeap()->LogStats();
		return NULL;
	}

	switch (behaviour)
	{
	case UB_MECH:
	case UB_INFANTRY:
		return new(MT_SQUAD) CRelaxedSquad;
	case UB_TURRET: case UB_BOAT: case UB_CARRIER: case UB_BUILDING: case UB_FIGHTER: case UB_BOMBER: case UB_GROUND_ATTACK_AIRCRAFT: case UB_JEEP: case UB_DROPSHIP: case UB_MINE: case UB_HIVE_BOSS: case UB_SUBMARINE: case UB_DIVE_BOMBER: case UB_THUNDERHEAD: case UB_CARVER: case UB_GILLM: case UB_SENTINEL: case UB_FENRIR: case UB_WARSPITE_DOME: case UB_POD: case UB_SIMPLE_BUILDING:
		return NULL;
	default:
		return new(MT_SQUAD) CNormalSquad;
	}
}

// FUNCTION: 0x0050f5f0 ?_GetClassName@CRelaxedSquad@@UAEPADXZ
// FUNCTION: 0x0050f600 ?Unknown74@CRelaxedSquad@@UAEPAVCUnit@@XZ
// FUNCTION: 0x0050f610 ??_GCRelaxedSquad@@UAEPAXI@Z
// FUNCTION: 0x0050f630 ??1CRelaxedSquad@@UAE@XZ

//******************************************************************************************
BOOL UPhysicsManager::IsSquadBehaviour(SINT behaviour)	// FUNCTION: 0x0050f680
{
	switch (behaviour)
	{
	case UB_TURRET: case UB_BOAT: case UB_CARRIER: case UB_BUILDING: case UB_FIGHTER: case UB_BOMBER:
	case UB_GROUND_ATTACK_AIRCRAFT: case UB_JEEP: case UB_DROPSHIP: case UB_MINE: case UB_HIVE_BOSS:
	case UB_SUBMARINE: case UB_DIVE_BOMBER: case UB_THUNDERHEAD: case UB_CARVER: case UB_GILLM: case UB_SENTINEL:
	case UB_FENRIR: case UB_WARSPITE_DOME: case UB_POD:
		return FALSE;
	}
	return TRUE;
}

//******************************************************************************************
#line 188
CWeapon *UPhysicsManager::SpawnWeapon(SINT index, SINT type)	// FUNCTION: 0x0050f6d0
{
	if (MEM_MANAGER.GetThingHeap()->GetFreeSize() < 10240)
		return NULL;

	CWeaponData	*data = GetWeaponData(index);
	if (!data)
		return NULL;
	return new(MEMTYPE_WEAPON) CWeapon(data, type);
}

//******************************************************************************************
#line 203
CRound *UPhysicsManager::SpawnRound(CRoundData *data)	// FUNCTION: 0x0050f7a0
{
	if (MEM_MANAGER.GetThingHeap()->GetFreeSize() < 204800)
		return NULL;
	if (!data)
		return NULL;

	if (data->mUnknown70)
		return new(MT_ROUND) CMissile(data);
	else
		return new(MT_ROUND) CRound(data);
}

// FUNCTION: 0x0050f890 ?_GetClassName@CMissile@@UAEPADXZ
// FUNCTION: 0x0050f8a0 ?_GetClassID@CMissile@@UAEHXZ
// FUNCTION: 0x0050f8b0 ??_GCMissile@@UAEPAXI@Z
// FUNCTION: 0x0050f8d0 ??1CMissile@@UAE@XZ

//******************************************************************************************
#line 222
CSpawner *UPhysicsManager::SpawnSpawner(SINT index, SINT a)	// FUNCTION: 0x0050f970
{
	if (MEM_MANAGER.GetThingHeap()->GetFreeSize() < 10240)
		return NULL;

	CSpawnerData	*data = GetSpawnerData(index);
	if (!data)
		return NULL;
	return new(MEMTYPE_SPAWNER) CSpawner(data, a);
}

//******************************************************************************************
#line 238
CComponent *UPhysicsManager::SpawnCharacter(SINT index)	// FUNCTION: 0x0050fa40
{
	if (MEM_MANAGER.GetThingHeap()->GetFreeSize() < 10240)
		return NULL;
	if (index <= -1)
		return NULL;

	CUnitData	*data = GetComponentData(index);
	if (!stricmp(data->mName, "Gill-M Head"))
		return new(MEMTYPE_COMPONENT) CGillMHead;
	if (data->mUnknown12C)
		return new(MEMTYPE_COMPONENT) CTentacle;
	else
		return new(MEMTYPE_COMPONENT) CComponent;
}

// FUNCTION: 0x0050fc90 ?_GetClassName@CComponent@@UAEPADXZ
// FUNCTION: 0x0050fca0 ?OnScanner@CComponent@@UAEHXZ
// FUNCTION: 0x0050fcb0 ?SetThingType@CComponent@@UAEXK@Z
// FUNCTION: 0x0050fcd0 ?GetMaxVelocity@CComponent@@UAEMXZ
// FUNCTION: 0x0050fcf0 ?_GetClassName@CTentacle@@UAEPADXZ
// FUNCTION: 0x0050fd00 ?_GetClassName@CGillMHead@@UAEPADXZ
// FUNCTION: 0x0050fd10 ?SetThingType@CGillMHead@@UAEXK@Z
// FUNCTION: 0x0050fd30 ??_GCGillMHead@@UAEPAXI@Z
// FUNCTION: 0x0050fd50 ??_GCTentacle@@UAEPAXI@Z
// FUNCTION: 0x0050fd70 ??_GCComponent@@UAEPAXI@Z
// FUNCTION: 0x0050fd90 ??1CComponent@@UAE@XZ
// FUNCTION: 0x0050fe10 ??1CGillMHead@@UAE@XZ
// FUNCTION: 0x0050fe90 ??1CTentacle@@UAE@XZ

//******************************************************************************************
#line 259
CExplosion *UPhysicsManager::SpawnExplosion(SINT index)	// FUNCTION: 0x0050ff10
{
	if (MEM_MANAGER.GetThingHeap()->GetFreeSize() < 204800)
		return NULL;
	if (index <= -1)
		return NULL;
	return new(MEMTYPE_EXPLOSION) CExplosion;
}

// FUNCTION: 0x0050ffb0 ?_GetClassName@CExplosion@@UAEPADXZ
// FUNCTION: 0x0050ffc0 ?_GetClassID@CExplosion@@UAEHXZ
// FUNCTION: 0x0050ffd0 ??_GCExplosion@@UAEPAXI@Z
// FUNCTION: 0x0050fff0 ??1CExplosion@@UAE@XZ

//******************************************************************************************
#line 274
CFeature *UPhysicsManager::SpawnFeature(SINT index)	// FUNCTION: 0x00510060
{
	if ((MEM_MANAGER.GetThingHeap()->GetFreeSize() < 10240) || (index <= -1))
		return NULL;
	return new(MEMTYPE_FEATURE) CFeature;
}

// FUNCTION: 0x005100f0 ?_GetClassName@CFeature@@UAEPADXZ
// FUNCTION: 0x00510100 ?_GetClassID@CFeature@@UAEHXZ
// FUNCTION: 0x00510110 ?SetThingType@CFeature@@UAEXK@Z
// FUNCTION: 0x00510120 ?GetStaticShadow@CFeature@@UAEPAVCStaticShadow@@XZ
// FUNCTION: 0x00510130 ?SetStaticShadow@CFeature@@UAEXPAVCStaticShadow@@@Z
// FUNCTION: 0x00510140 ?IsLockable@CFeature@@UAEHXZ

//******************************************************************************************
#line 286
CHazard *UPhysicsManager::SpawnHazard(SINT index)	// FUNCTION: 0x00510150
{
	if (MEM_MANAGER.GetThingHeap()->GetFreeSize() < 10240)
		return NULL;
	if (index <= -1)
		return NULL;
	return new(MEMTYPE_FEATURE) CHazard;
}

// FUNCTION: 0x00510200 ?_GetClassName@CHazard@@UAEPADXZ
// FUNCTION: 0x00510210 ?_GetClassID@CHazard@@UAEHXZ
// FUNCTION: 0x00510220 ?SetThingType@CHazard@@UAEXK@Z
// FUNCTION: 0x00510230 ??_GCHazard@@UAEPAXI@Z
// FUNCTION: 0x00510250 ??1CHazard@@UAE@XZ
inline void CFeatureData::Resolve() { if (mSoundName) mSound = SOUND.GetEffectByName(mSoundName, 0); }	// not in the class: see pcsoundmanager
//******************************************************************************************
#line 299
void UPhysicsManager::InitializeLists()	// FUNCTION: 0x005102a0
{
	mWeapons = new(MT_PHYSICS) SPtrSet<CWeaponData>;
	mWeaponModes = new(MT_PHYSICS) SPtrSet<CWeaponModeData>;
	mRounds = new(MT_PHYSICS) SPtrSet<CRoundData>;
	mSpawners = new(MT_PHYSICS) SPtrSet<CSpawnerData>;
	mExplosions = new(MT_PHYSICS) SPtrSet<CExplosionData>;
	mUnits = new(MT_PHYSICS) SPtrSet<CUnitData>;
	mComponents = new(MT_PHYSICS) SPtrSet<CUnitData>;
	mFeatures = new(MT_PHYSICS) SPtrSet<CFeatureData>;
	mHazards = new(MT_PHYSICS) SPtrSet<CHazardData>;
}
inline void CHazardData::Resolve() { if (mParticleName) mParticle = PARTICLE_SET.FindByName(mParticleName); if (mSoundName) mSound = SOUND.GetEffectByName(mSoundName, 0); }
//******************************************************************************************
// Links the loaded definitions to the particle effects and sounds they name.
void UPhysicsManager::InitialiseEffects()	// FUNCTION: 0x00510520
{
	CWeaponModeData	*mode;
	for (mode = mWeaponModes->First(); mode; mode = mWeaponModes->Next())
		mode->Resolve();

	CRoundData	*round;
	for (round = mRounds->First(); round; round = mRounds->Next())
		round->Resolve();

	CExplosionData	*explosion;
	for (explosion = mExplosions->First(); explosion; explosion = mExplosions->Next())
		explosion->Resolve();

	CUnitData	*unit;
	for (unit = mUnits->First(); unit; unit = mUnits->Next())
		unit->Resolve();

	for (unit = mComponents->First(); unit; unit = mComponents->Next())
		unit->Resolve();

	CFeatureData	*feature;
	for (feature = mFeatures->First(); feature; feature = mFeatures->Next())
		feature->Resolve();

	CHazardData	*hazard;
	for (hazard = mHazards->First(); hazard; hazard = mHazards->Next())
		hazard->Resolve();
}

//******************************************************************************************
void UPhysicsManager::ClearCachedEmitters()	// FUNCTION: 0x00510740
{
	CUnitData	*unit;
	CUnitPair	*pair;
	for (unit = mUnits->First(); unit; unit = mUnits->Next())
	{
		ListIterator<CUnitPair>	pairs(&unit->mPairs);
		while (pair = pairs.First())
		{
			unit->mPairs.Remove(pair);
			delete pair;
		}
	}
	for (unit = mComponents->First(); unit; unit = mComponents->Next())
	{
		ListIterator<CUnitPair>	pairs(&unit->mPairs);
		while (pair = pairs.First())
		{
			unit->mPairs.Remove(pair);
			delete pair;
		}
	}
}

//******************************************************************************************
void UPhysicsManager::Reload()	// FUNCTION: 0x00510800
{
	ClearAndFreeAll();
	InitializeLists();

	char	physics[] = "data\\default physics.dat";
	CDXMemBuffer	mem;
	BOOL	loaded = mem.InitFromFile(physics);
	if (loaded)
	{
		CPhysicsScript::Load(mem);
		CPhysicsScript::Process();
		CPhysicsScript::Shutdown();
		mem.Close();
	}

	UBattleEngineDataManager::Initialise();

	char	configurations[] = "data\\battle engine configurations.dat";
	loaded = mem.InitFromFile(configurations);
	if (loaded)
	{
		UBattleEngineDataManager::Load(mem);
		mem.Close();
	}
	else
		CONSOLE.FatalFileError(configurations, -1);
}

//******************************************************************************************
void UPhysicsManager::ClearAndFreeAll()	// FUNCTION: 0x00510a90
{
	UBattleEngineDataManager::Shutdown();

	if (mWeapons)
	{
		while (CWeaponData *weapon = mWeapons->First())
		{
			mWeapons->Remove(weapon);
			delete weapon;
		}
	}
	if (mWeaponModes)
	{
		while (CWeaponModeData *mode = mWeaponModes->First())
		{
			mWeaponModes->Remove(mode);
			delete mode;
		}
	}
	if (mRounds)
	{
		while (CRoundData *round = mRounds->First())
		{
			mRounds->Remove(round);
			delete round;
		}
	}
	if (mSpawners)
	{
		while (CSpawnerData *spawner = mSpawners->First())
		{
			mSpawners->Remove(spawner);
			delete spawner;
		}
	}
	if (mExplosions)
	{
		while (CExplosionData *explosion = mExplosions->First())
		{
			mExplosions->Remove(explosion);
			delete explosion;
		}
	}
	if (mUnits)
	{
		while (CUnitData *unit = mUnits->First())
		{
			mUnits->Remove(unit);
			delete unit;
		}
	}
	if (mComponents)
	{
		while (CUnitData *component = mComponents->First())
		{
			mComponents->Remove(component);
			delete component;
		}
	}
	if (mFeatures)
	{
		while (CFeatureData *feature = mFeatures->First())
		{
			mFeatures->Remove(feature);
			delete feature;
		}
	}
	if (mHazards)
	{
		while (CHazardData *hazard = mHazards->First())
		{
			mHazards->Remove(hazard);
			delete hazard;
		}
	}

	SAFE_DELETE(mWeapons);
	SAFE_DELETE(mWeaponModes);
	SAFE_DELETE(mRounds);
	SAFE_DELETE(mSpawners);
	SAFE_DELETE(mExplosions);
	SAFE_DELETE(mUnits);
	SAFE_DELETE(mComponents);
	SAFE_DELETE(mFeatures);
	SAFE_DELETE(mHazards);
}

// The destructors ClearAndFreeAll's inline budget left out of line:
// FUNCTION: 0x00510e60 ??1CFeatureData@@QAE@XZ
// FUNCTION: 0x00510eb0 ??1CRoundData@@QAE@XZ
// FUNCTION: 0x00510f10 ??1CWeaponModeData@@QAE@XZ
// FUNCTION: 0x00511040 ??1CWeaponData@@QAE@XZ
// FUNCTION: 0x00511070 ??1CExplosionData@@QAE@XZ
// Reconstructed list, effect-name and pair-cache cleanup boundaries reproduce retail's nesting: six list First()
// calls and both external pair-iterator First() calls stay out of line, with the iterator retained on the stack.
// The complete 688-byte section and all 60 relocations match; helper spellings are not original-source claims.
// FUNCTION: 0x005110f0 ??1CUnitData@@QAE@XZ
// FUNCTION: 0x005113a0 ??1CBattleEngineData@@QAE@XZ

//******************************************************************************************
void CWeaponModeData::SetRound(SINT index)	// FUNCTION: 0x005113f0
{
	mRound = UPhysicsManager::GetRound(index);
}

//******************************************************************************************
// Whether a spawner may make the unit definition with that name.
BOOL UPhysicsManager::CanSpawnerMake(char *name)	// FUNCTION: 0x00511440
{
	CUnitData	*unit = GetUnitData(name);
	if (!unit)
		return FALSE;

	switch (unit->mBehaviour)
	{
		case UB_MECH:
		case UB_GROUND_UNIT:
		case UB_INFANTRY:
		case UB_TURRET:
		case UB_BUILDING:
		case UB_JEEP:
		case UB_MINE:
		case UB_THUNDERHEAD:
		case UB_GILLM:
		case UB_SENTINEL:
		case UB_WARSPITE:
		case UB_WARSPITE_DOME:
			return TRUE;
	}
	return FALSE;
}

//******************************************************************************************
SINT UPhysicsManager::GetBehaviourPriority(CUnitData *unit)	// FUNCTION: 0x00511510
{
	switch (unit->mBehaviour)
	{
		case UB_MECH:
		case UB_BOAT:
		case UB_BUILDING:
		case UB_FIGHTER:
		case UB_GROUND_ATTACK_AIRCRAFT:
		case UB_DIVE_BOMBER:
		case UB_POD:
			return 10;
		case UB_GROUND_UNIT:
		case UB_TURRET:
		case UB_JEEP:
			return 5;
		case UB_BOMBER:
		case UB_DROPSHIP:
		case UB_SUBMARINE:
			return 20;
		case UB_INFANTRY:
		case UB_MINE:
			return 1;
		case UB_CARRIER:
		case UB_HIVE_BOSS:
		case UB_THUNDERHEAD:
		case UB_CARVER:
		case UB_GILLM:
		case UB_SENTINEL:
		case UB_WARSPITE:
		case UB_FENRIR:
		case UB_WARSPITE_DOME:
			return 100;
	}
	return 0;
}

//******************************************************************************************
// The mount a gun or spawner tag names (GunA..GunI are 1..9, SpawnerA..SpawnerE 10..14), or 0.
SINT UPhysicsManager::MapMountName(char *name)	// FUNCTION: 0x005115b0
{
	if (!stricmp(name, "GunA"))		return 1;
	if (!stricmp(name, "GunB"))		return 2;
	if (!stricmp(name, "GunC"))		return 3;
	if (!stricmp(name, "GunD"))		return 4;
	if (!stricmp(name, "GunE"))		return 5;
	if (!stricmp(name, "GunF"))		return 6;
	if (!stricmp(name, "GunG"))		return 7;
	if (!stricmp(name, "GunH"))		return 8;
	if (!stricmp(name, "GunI"))		return 9;
	if (!stricmp(name, "SpawnerA"))	return 10;
	if (!stricmp(name, "SpawnerB"))	return 11;
	if (!stricmp(name, "SpawnerC"))	return 12;
	if (!stricmp(name, "SpawnerD"))	return 13;
	if (!stricmp(name, "SpawnerE"))	return 14;
	return 0;
}

//******************************************************************************************
void CUnitData::SetExplosion(char *name)	// FUNCTION: 0x00511720
{
	mExplosion = UPhysicsManager::GetExplosionIndex(name);
}

void CUnitData::SetSmallExplosion(char *name)	// FUNCTION: 0x005117c0
{
	mSmallExplosion = UPhysicsManager::GetExplosionIndex(name);
}

void CUnitData::SetStompExplosion(char *name)	// FUNCTION: 0x00511860
{
	mStompExplosion = UPhysicsManager::GetExplosionIndex(name);
}

//******************************************************************************************
#line 705
void CUnitData::AddComponent(SINT a, char *name)	// FUNCTION: 0x00511900
{
	CUnitComponentReference	*component = new(MT_PHYSICS) CUnitComponentReference;
	component->mUnknown04 = a;
	component->mIndex = UPhysicsManager::GetComponentIndex(name);
	if (component->mIndex == -1)
		LOG.AddMessage("Component \"%s\" not found", name);
	mComponents.Append(component);
}

//******************************************************************************************
#line 722
void CUnitData::AddWeapon(char *name, char *mount, SINT a)	// FUNCTION: 0x005119e0
{
	CUnitWeapon	*weapon = new(MT_PHYSICS) CUnitWeapon;
	weapon->mIndex = UPhysicsManager::GetWeaponIndex(name);
	weapon->mUnknown04 = a;
	weapon->mMount = UPhysicsManager::MapMountName(mount);
	if (weapon->mIndex == -1)
		LOG.AddMessage("Weapon \"%s\" not found", name);
	mWeapons.Append(weapon);
}

//******************************************************************************************
#line 740
void CUnitData::AddSpawner(char *name, char *mount, SINT a)	// FUNCTION: 0x00511ad0
{
	CSpawnerReference	*spawner = new(MT_PHYSICS) CSpawnerReference;
	spawner->mSpawnerIndex = UPhysicsManager::GetSpawnerIndex(name);
	spawner->mUnknown04 = a;
	spawner->mMount = UPhysicsManager::MapMountName(mount);
	if (spawner->mSpawnerIndex == -1)
		LOG.AddMessage("Spawner \"%s\" not found", name);
	mSpawners.Append(spawner);
}

//******************************************************************************************
CUnitPair *CUnitData::FindPair(SINT a, SINT b)	// FUNCTION: 0x00511bc0
{
	CUnitPair	*pair;
	for (pair = mPairs.First(); pair; pair = mPairs.Next())
		if ((pair->mB == b) && (pair->mA == a))
			return pair;
	return NULL;
}

//******************************************************************************************
void CFeatureData::SetExplosion(char *name)	// FUNCTION: 0x00511c10
{
	mExplosion = UPhysicsManager::GetExplosionIndex(name);
}

//******************************************************************************************
void CWeaponModeData::Resolve()	// FUNCTION: 0x00511ca0
{
	if (mParticleAName)
		mParticleA = PARTICLE_SET.FindByName(mParticleAName);
	if (mParticleBName)
		mParticleB = PARTICLE_SET.FindByName(mParticleBName);
	if (mSoundAName)
		mSoundA = SOUND.GetEffectByName(mSoundAName, 0);
	if (mSoundBName)
		mSoundB = SOUND.GetEffectByName(mSoundBName, 0);
	if (mSoundCName)
		mSoundC = SOUND.GetEffectByName(mSoundCName, 0);
}

//******************************************************************************************
void CExplosionData::Resolve()	// FUNCTION: 0x00511d20
{
	if (mParticleAName)
		mParticleA = PARTICLE_SET.FindByName(mParticleAName);
	if (mParticleBName)
		mParticleB = PARTICLE_SET.FindByName(mParticleBName);
	if (mParticleCName)
		mParticleC = PARTICLE_SET.FindByName(mParticleCName);
	if (mParticleDName)
		mParticleD = PARTICLE_SET.FindByName(mParticleDName);
	if (mSoundAName)
		mSoundA = SOUND.GetEffectByName(mSoundAName, 0);
	if (mSoundBName)
		mSoundB = SOUND.GetEffectByName(mSoundBName, 0);
}

//******************************************************************************************
void CUnitData::Resolve()	// FUNCTION: 0x00511db0
{
	if (mEffectNames[2])
		mParticles[2] = PARTICLE_SET.FindByName(mEffectNames[2]);
	if (mEffectNames[3])
		mParticles[3] = PARTICLE_SET.FindByName(mEffectNames[3]);
	if (mEffectNames[4])
		mParticles[4] = PARTICLE_SET.FindByName(mEffectNames[4]);
	if (mEffectNames[5])
		mParticles[5] = PARTICLE_SET.FindByName(mEffectNames[5]);
	if (mEffectNames[6])
		mParticles[6] = PARTICLE_SET.FindByName(mEffectNames[6]);
	if (mEffectNames[7])
		mParticles[7] = PARTICLE_SET.FindByName(mEffectNames[7]);
	if (mEffectNames[8])
		mParticles[8] = PARTICLE_SET.FindByName(mEffectNames[8]);
	if (mEffectNames[0])
		mParticles[0] = PARTICLE_SET.FindByName(mEffectNames[0]);
	if (mEffectNames[9])
		mParticles[9] = PARTICLE_SET.FindByName(mEffectNames[9]);
	if (mEffectNames[10])
		mParticles[10] = PARTICLE_SET.FindByName(mEffectNames[10]);
	if (mEffectNames[1])
		mParticles[1] = PARTICLE_SET.FindByName(mEffectNames[1]);
	if (mEffectNames[11])
		mSounds[0] = SOUND.GetEffectByName(mEffectNames[11], 0);
	if (mEffectNames[12])
		mSounds[1] = SOUND.GetEffectByName(mEffectNames[12], 0);
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004bfd00 ??_GCFeature@@UAEPAXI@Z
// FUNCTION: 0x004bfe00 ??1CGroundUnit@@UAE@XZ
// FUNCTION: 0x004e5de0 ?_GetClassID@CSquad@@UAEHXZ
// FUNCTION: 0x004e5df0 ?SetThingType@CSquad@@UAEXK@Z
// FUNCTION: 0x00551fd0 ??0?$CActiveReader@VCUnit@@@@QAE@XZ

//******************************************************************************************
// STAND-IN (layout): the spawned classes' default answers, defined inline HERE rather than in their class bodies.
// Retail links them as INLINE functions: VC6's /OPT:ICF folds inline and out-of-line functions separately, and retail
// keeps each small body once per class (stage-3 link research, link order notes 3b/3c), so these names sit at
// the inline survivors 0x004014a0, 0x004014c0, 0x00405930, 0x004059c0, 0x004bfc60 and 0x004de700. An inline virtual is
// emitted where its class's table is, and only this object emits these classes' tables (the spawn code inlines their
// constructors), so retail's own definitions were very likely in the class bodies (unit.h, squad.h). That form costs
// a header-count witness (measured 2026-10-15 on bdebcc70 + this batch): unit.h's 16 lose CFEPGoodies::Render 0045e0d0
// (4 bytes); squad.h's 11, alone or with them, lose CGame::DrawDebugStuff 00470650 (1 byte). Move them back into the
// classes once the witnesses' real header context is known.

// FUNCTION: 0x00405930 ?ClipToGround@CSubmarine@@UAEHXZ
inline BOOL CSubmarine::ClipToGround()
{
	return FALSE;
}

// FUNCTION: 0x004014a0 ?CanGoUnderWater@CSubmarine@@UAEHXZ
inline BOOL CSubmarine::CanGoUnderWater()
{
	return TRUE;
}

// FUNCTION: 0x004014a0 ?GetCanBeStaticallyShadowed@CSimpleBuilding@@UAEHXZ
inline BOOL CSimpleBuilding::GetCanBeStaticallyShadowed()
{
	return TRUE;
}

// FUNCTION: 0x004bfc60 ?GetMaxVelocity@CSimpleBuilding@@UAEMXZ
inline float CSimpleBuilding::GetMaxVelocity()
{
	return 0;
}

// FUNCTION: 0x004014a0 ?GetImposterAnimMode@CInfantryUnit@@UAE?AW4EAnimMode@@XZ
inline EAnimMode CInfantryUnit::GetImposterAnimMode()
{
	return AM_WALK;
}

// FUNCTION: 0x00405930 ?GetRequiresPolyBucket@CInfantryUnit@@UAEHXZ
inline BOOL CInfantryUnit::GetRequiresPolyBucket()
{
	return FALSE;
}

// FUNCTION: 0x004bfc60 ?GetMaxVelocity@CCannon@@UAEMXZ
inline float CCannon::GetMaxVelocity()
{
	return 0;
}

// FUNCTION: 0x004bfc60 ?Gravity@CCannon@@UAEMXZ
inline float CCannon::Gravity()
{
	return 0;
}

// FUNCTION: 0x004de700 ?Unknown72@CPod@@UAEMXZ
inline float CPod::Unknown72()
{
	return 1;
}

// FUNCTION: 0x004de700 ?Unknown73@CPod@@UAEMXZ
inline float CPod::Unknown73()
{
	return 1;
}

// FUNCTION: 0x004bfc60 ?GetThreat@CPod@@UAEMXZ
inline float CPod::GetThreat()
{
	return 0;
}

// FUNCTION: 0x004bfc60 ?GetImportance@CPod@@UAEMXZ
inline float CPod::GetImportance()
{
	return 0;
}

// FUNCTION: 0x004bfc60 ?GetMaxVelocity@CSentinel@@UAEMXZ
inline float CSentinel::GetMaxVelocity()
{
	return 0;
}

// FUNCTION: 0x004bfc60 ?Gravity@CSentinel@@UAEMXZ
inline float CSentinel::Gravity()
{
	return 0;
}

// FUNCTION: 0x004bfc60 ?GetMaxVelocity@CWarspiteDome@@UAEMXZ
inline float CWarspiteDome::GetMaxVelocity()
{
	return 0;
}

// FUNCTION: 0x004bfc60 ?Gravity@CWarspiteDome@@UAEMXZ
inline float CWarspiteDome::Gravity()
{
	return 0;
}

// FUNCTION: 0x004014c0 ?Unknown68@CRelaxedSquad@@UAEXPAVCUnit@@@Z
inline void CRelaxedSquad::Unknown68(CUnit *unit)
{
}

// FUNCTION: 0x00405930 ?Unknown69@CRelaxedSquad@@UAEHXZ
inline BOOL CRelaxedSquad::Unknown69()
{
	return FALSE;
}

// FUNCTION: 0x00405930 ?Unknown76@CRelaxedSquad@@UAEHXZ
inline SINT CRelaxedSquad::Unknown76()
{
	return 0;
}

// FUNCTION: 0x00405930 ?Unknown77@CRelaxedSquad@@UAEPAVCThing@@XZ
inline CThing *CRelaxedSquad::Unknown77()
{
	return NULL;
}

// FUNCTION: 0x004bfc60 ?Unknown78@CRelaxedSquad@@UAEMXZ
inline float CRelaxedSquad::Unknown78()
{
	return 0;
}

// FUNCTION: 0x004bfc60 ?Unknown79@CRelaxedSquad@@UAEMXZ
inline float CRelaxedSquad::Unknown79()
{
	return 0;
}

// FUNCTION: 0x004bfc60 ?Unknown80@CRelaxedSquad@@UAEMXZ
inline float CRelaxedSquad::Unknown80()
{
	return 0;
}

// FUNCTION: 0x00405930 ?Unknown82@CRelaxedSquad@@UAEHXZ
inline BOOL CRelaxedSquad::Unknown82()
{
	return FALSE;
}

// FUNCTION: 0x004059c0 ?Unknown83@CRelaxedSquad@@UAEHXZ
inline SINT CRelaxedSquad::Unknown83()
{
	return 2;
}

// FUNCTION: 0x004de700 ?Unknown84@CRelaxedSquad@@UAEMXZ
inline float CRelaxedSquad::Unknown84()
{
	return 1;
}

// FUNCTION: 0x00405930 ?OnScanner@CMine@@UAEHXZ
inline BOOL CMine::OnScanner()
{
	return FALSE;
}

// FUNCTION: 0x00405930 ?Unknown108@CCarver@@UAEHXZ
inline BOOL CCarver::Unknown108()
{
	return FALSE;
}

// FUNCTION: 0x00405e60 ?GetMaxVelocity@CInfantryUnit@@UAEMXZ
inline float CInfantryUnit::GetMaxVelocity()
{
	return 2;
}

// FUNCTION: 0x004bfc50 ?GetMaxVelocity@CPod@@UAEMXZ
inline float CPod::GetMaxVelocity()
{
	return 10;
}

// FUNCTION: 0x00459990
inline void CBomber::Stop()
{
}

// FUNCTION: 0x00459990
inline void CDropship::Stop()
{
}

// FUNCTION: 0x00459990
inline void CSimpleBuilding::Stop()
{
}

// FUNCTION: 0x004014c0 ?Unknown85@CRelaxedSquad@@UAEXH@Z
inline void CRelaxedSquad::Unknown85(SINT a)
{
}
