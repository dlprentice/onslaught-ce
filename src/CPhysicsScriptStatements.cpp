// CPhysicsScriptStatements.cpp: reconstructed from CPhysicsScriptStatements.obj (0042eaa0..0043e7ef); Stuart's
// release has no copy. The factories' __LINE__ values are the retail ones; lines between their cases are blank.
#include "common.h"
#include "CPhysicsScriptStatements.h"	// FUNCTION: 0x0042eaa0 init:ZERO_FVECTOR
#include "mathsconst.h"	// FUNCTION: 0x0042eac0 init:ID_FMATRIX






//******************************************************************************************
CPhysicsScriptStatement *CPhysicsScriptStatement::Create(SINT type)	// FUNCTION: 0x0042eb90
{
	switch (type)
	{
	case PST_UNIT:	return new(MT_PHYSICS) CUnitStatement;
	case PST_WEAPON_MODE:	return new(MT_PHYSICS) CWeaponModeStatement;
	case PST_WEAPON:	return new(MT_PHYSICS) CWeaponStatement;
	case PST_ROUND:	return new(MT_PHYSICS) CRoundStatement;
	case PST_SPAWNER:	return new(MT_PHYSICS) CSpawnerStatement;
	case PST_EXPLOSION:	return new(MT_PHYSICS) CExplosionStatement;
	case PST_COMPONENT:	return new(MT_PHYSICS) CComponentStatement;
	case PST_FEATURE:	return new(MT_PHYSICS) CFeatureStatement;
	case PST_HAZARD:	return new(MT_PHYSICS) CHazardStatement;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsUnitValue *CreateUnitValue(SINT type)	// FUNCTION: 0x00431bb0
{
	switch (type)
	{
	case PUV_BASED_ON:	return new(MT_PHYSICS) CUnitBasedOn;
	case PUV_GROUND_VELOCITY:	return new(MT_PHYSICS) CUnitGroundVelocity;
	case PUV_AIR_VELOCITY:	return new(MT_PHYSICS) CUnitAirVelocity;
	case PUV_GROUND_TURN_RATE:	return new(MT_PHYSICS) CUnitGroundTurnRate;
	case PUV_AIR_TURN_RATE:	return new(MT_PHYSICS) CUnitAirTurnRate;
	case PUV_TURRET_TURN_RATE:	return new(MT_PHYSICS) CUnitTurretTurnRate;
	case PUV_LIFE:	return new(MT_PHYSICS) CUnitLife;
	case PUV_AIR_SHIELDS:	return new(MT_PHYSICS) CUnitAirShields;
	case PUV_GROUND_SHIELDS:	return new(MT_PHYSICS) CUnitGroundShields;
	case PUV_AIR_SHIELD_REGENERATION:	return new(MT_PHYSICS) CUnitAirShieldRegeneration;
	case PUV_GROUND_SHIELD_REGENERATION:	return new(MT_PHYSICS) CUnitGroundShieldRegeneration;
	case PUV_USE:	return new(MT_PHYSICS) CUnitUse;
	case PUV_BEHAVIOUR:	return new(MT_PHYSICS) CUnitBehaviour;
	case PUV_MESH:	return new(MT_PHYSICS) CUnitMesh;
	case PUV_ALLIGENCE:	return new(MT_PHYSICS) CUnitAlligence;
	case PUV_USE_SPAWNER:	return new(MT_PHYSICS) CUnitUseSpawner;
	case PUV_THREAT:	return new(MT_PHYSICS) CUnitThreat;
	case PUV_IMPORTANCE:	return new(MT_PHYSICS) CUnitImportance;
	case PUV_SOUND_MATERIAL:	return new(MT_PHYSICS) CUnitSoundMaterial;
	case PUV_MAX_TARGET_RANGE:	return new(MT_PHYSICS) CUnitMaxTargetRange;
	case PUV_SHIELD_EFFECT:	return new(MT_PHYSICS) CUnitShieldEffect;
	case PUV_COMPONENT:	return new(MT_PHYSICS) CUnitComponent;
	case PUV_STEALTH:	return new(MT_PHYSICS) CUnitStealth;
	case PUV_ATTACK_PRIORITY:	return new(MT_PHYSICS) CUnitAttackPriority;
	case PUV_BODY_MOTION:	return new(MT_PHYSICS) CUnitBodyMotion;
	case PUV_LEG_SPEED:	return new(MT_PHYSICS) CUnitLegSpeed;
	case PUV_LEG_PLACEMENT_AREA:	return new(MT_PHYSICS) CUnitLegPlacementArea;
	case PUV_STANDING_LEG_PLACEMENT_AREA:	return new(MT_PHYSICS) CUnitStandingLegPlacementArea;
	case PUV_MAX_LEGS_LIFTED:	return new(MT_PHYSICS) CUnitMaxLegsLifted;
	case PUV_BOUNCE_FACTOR:	return new(MT_PHYSICS) CUnitBounceFactor;
	case PUV_MIN_ALTITUDE:	return new(MT_PHYSICS) CUnitMinAltitude;
	case PUV_DEPLOY:	return new(MT_PHYSICS) CUnitDeploy;
	case PUV_DEPLOY_DAMAGE_MULTIPLIER:	return new(MT_PHYSICS) CUnitDeployDamageMultiplier;
	case PUV_STRAFE_CHANGE:	return new(MT_PHYSICS) CUnitStrafeChange;
	case PUV_NAV_MAP:	return new(MT_PHYSICS) CUnitNavMap;
	case PUV_MULTIPLE_SPAWN:	return new(MT_PHYSICS) CUnitMultipleSpawn;
	case PUV_MIN_THRUSTER_HEIGHT:	return new(MT_PHYSICS) CUnitMinThrusterHeight;
	case PUV_MAX_THRUSTER_HEIGHT:	return new(MT_PHYSICS) CUnitMaxThrusterHeight;
	case PUV_DETONATION_DELAY:	return new(MT_PHYSICS) CUnitDetonationDelay;
	case PUV_SHATTER:	return new(MT_PHYSICS) CUnitShatter;
	case PUV_LOCKABLE:	return new(MT_PHYSICS) CUnitLockable;
	case PUV_SWEEPING:	return new(MT_PHYSICS) CUnitSweeping;
	case PUV_IGNORE_THREATS:	return new(MT_PHYSICS) CUnitIgnoreThreats;
	case PUV_ANIMATED_COLLISION:	return new(MT_PHYSICS) CUnitAnimatedCollision;
	case PUV_ICON:	return new(MT_PHYSICS) CUnitIcon;

	case PUV_EXPLOSION:	return new(MT_PHYSICS) CUnitExplosion;
	case PUV_SMALL_EXPLOSION:	return new(MT_PHYSICS) CUnitSmallExplosion;
	case PUV_STOMP_EXPLOSION:	return new(MT_PHYSICS) CUnitStompExplosion;

	case PUV_TRAILS:	return new(MT_PHYSICS) CUnitTrails;
	case PUV_THRUSTERS:	return new(MT_PHYSICS) CUnitThrusters;
	case PUV_SMOKE:	return new(MT_PHYSICS) CUnitSmoke;
	case PUV_ENGINES:	return new(MT_PHYSICS) CUnitEngines;
	case PUV_DUST:	return new(MT_PHYSICS) CUnitDust;
	case PUV_VENT:	return new(MT_PHYSICS) CUnitVent;
	case PUV_ACTIVATION_EFFECT:	return new(MT_PHYSICS) CUnitActivationEffect;
	case PUV_NOISE:	return new(MT_PHYSICS) CUnitNoise;
	case PUV_SPAWN_NOISE:	return new(MT_PHYSICS) CUnitSpawnNoise;
	case PUV_AFTERBURNER:	return new(MT_PHYSICS) CUnitAfterburner;
	case PUV_WATER_WAKE:	return new(MT_PHYSICS) CUnitWaterWake;
	case PUV_GROUND_WAKE:	return new(MT_PHYSICS) CUnitGroundWake;

	case PUV_BUILDING_NUMBER:	return new(MT_PHYSICS) CUnitBuildingNumber;
	case PUV_BUILDING_MESH_TYPE:	return new(MT_PHYSICS) CUnitBuildingMeshType;
	case PUV_PARACHUTE:	return new(MT_PHYSICS) CUnitParachute;
	case PUV_KNOCK_TREES:	return new(MT_PHYSICS) CUnitKnockTrees;
	case PUV_HOVER:	return new(MT_PHYSICS) CUnitHover;
	case PUV_RETREAT:	return new(MT_PHYSICS) CUnitRetreat;
	case PUV_WINGMAN:	return new(MT_PHYSICS) CUnitWingman;
	case PUV_BIG:	return new(MT_PHYSICS) CUnitBig;
	case PUV_INDISCRIMINATE:	return new(MT_PHYSICS) CUnitIndiscriminate;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsWeaponValue *CreateWeaponValue(SINT type)	// FUNCTION: 0x00434300
{
	switch (type)
	{
	case PWV_CHARGE_LEVEL:	return new(MT_PHYSICS) CWeaponChargeLevel;
	case PWV_CHARGE_RATE:	return new(MT_PHYSICS) CWeaponChargeRate;
	case PWV_AMMO_STORE:	return new(MT_PHYSICS) CWeaponAmmoStore;
	case PWV_CONSUMPTION:	return new(MT_PHYSICS) CWeaponConsumption;
	case PWV_ICON_NAME:	return new(MT_PHYSICS) CWeaponIconName;
	case PWV_ADJUST_AIM:	return new(MT_PHYSICS) CWeaponAdjustAim;
	case PWV_ZOOM_MODE:	return new(MT_PHYSICS) CWeaponZoomMode;
	case PWV_ALLOW_MOVEMENT:	return new(MT_PHYSICS) CWeaponAllowMovement;
	case PWV_SMART:	return new(MT_PHYSICS) CWeaponSmart;
	case PWV_PLACEMENT:	return new(MT_PHYSICS) CWeaponPlacement;
	case PWV_LANGUAGE_NAME:	return new(MT_PHYSICS) CWeaponLanguageName;
	case PWV_VERSUS_INFANTRY:	return new(MT_PHYSICS) CWeaponVersusInfantry;
	case PWV_VERSUS_TANKS:	return new(MT_PHYSICS) CWeaponVersusTanks;
	case PWV_VERSUS_AIR:	return new(MT_PHYSICS) CWeaponVersusAir;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsWeaponModeValue *CreateWeaponModeValue(SINT type)	// FUNCTION: 0x00435010
{
	switch (type)
	{
	case PWMV_PREDICTIVE:	return new(MT_PHYSICS) CWeaponPredictive;
	case PWMV_INACCURACY:	return new(MT_PHYSICS) CWeaponInaccuracy;
	case PWMV_BURST_SIZE:	return new(MT_PHYSICS) CWeaponBurstSize;
	case PWMV_BURST_DELAY:	return new(MT_PHYSICS) CWeaponBurstDelay;
	case PWMV_VOLLEY_SIZE:	return new(MT_PHYSICS) CWeaponVolleySize;
	case PWMV_RELOAD_TIME:	return new(MT_PHYSICS) CWeaponReloadTime;
	case PWMV_ROUND:	return new(MT_PHYSICS) CWeaponRound;
	case PWMV_MUZZLE_EFFECT:	return new(MT_PHYSICS) CWeaponMuzzleEffect;
	case PWMV_LAUNCH_SEQUENCE:	return new(MT_PHYSICS) CWeaponLaunchSequence;
	case PWMV_LAUNCH_ANGLE:	return new(MT_PHYSICS) CWeaponLaunchAngle;
	case PWMV_MIN_RANGE:	return new(MT_PHYSICS) CWeaponMinRange;
	case PWMV_MAX_RANGE:	return new(MT_PHYSICS) CWeaponMaxRange;
	case PWMV_MIN_DEFLECTION:	return new(MT_PHYSICS) CWeaponMinDeflection;
	case PWMV_MAX_DEFLECTION:	return new(MT_PHYSICS) CWeaponMaxDeflection;
	case PWMV_YAW_TOLERANCE:	return new(MT_PHYSICS) CWeaponYawTolerance;
	case PWMV_PRE_FIRE_DELAY:	return new(MT_PHYSICS) CWeaponPreFireDelay;
	case PWMV_POST_FIRE_DELAY:	return new(MT_PHYSICS) CWeaponPostFireDelay;
	case PWMV_PRE_FIRE_EFFECT:	return new(MT_PHYSICS) CWeaponPreFireEffect;
	case PWMV_CLIP:	return new(MT_PHYSICS) CWeaponClip;
	case PWMV_LOCK_TIME:	return new(MT_PHYSICS) CWeaponLockTime;
	case PWMV_LOCK_DEFLECTION:	return new(MT_PHYSICS) CWeaponLockDeflection;
	case PWMV_LOCK_MODE:	return new(MT_PHYSICS) CWeaponLockMode;
	case PWMV_LOCK_UNIT:	return new(MT_PHYSICS) CWeaponLockUnit;
	case PWMV_LOCK_RANGE:	return new(MT_PHYSICS) CWeaponLockRange;
	case PWMV_LOCK_RADIUS:	return new(MT_PHYSICS) CWeaponLockRadius;
	case PWMV_MAX_LOCKS:	return new(MT_PHYSICS) CWeaponMaxLocks;
	case PWMV_SOUND_PER_BURST:	return new(MT_PHYSICS) CWeaponSoundPerBurst;
	case PWMV_LAUNCH_SOUND:	return new(MT_PHYSICS) CWeaponLaunchSound;
	case PWMV_PRE_FIRE_SOUND:	return new(MT_PHYSICS) CWeaponPreFireSound;
	case PWMV_POST_FIRE_SOUND:	return new(MT_PHYSICS) CWeaponPostFireSound;
	case PWMV_BASED_ON:	return new(MT_PHYSICS) CWeaponBasedOn;
	case PWMV_MIN_TARGET_HEIGHT:	return new(MT_PHYSICS) CWeaponMinTargetHeight;
	case PWMV_MAX_TARGET_HEIGHT:	return new(MT_PHYSICS) CWeaponMaxTargetHeight;
	case PWMV_TRACK:	return new(MT_PHYSICS) CWeaponTrack;
	case PWMV_POWER:	return new(MT_PHYSICS) CWeaponPower;
	case PWMV_MUZZLE_LIGHT:	return new(MT_PHYSICS) CWeaponMuzzleLight;
	case PWMV_MUZZLE_LIGHT_RADIUS:	return new(MT_PHYSICS) CWeaponMuzzleLightRadius;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsRoundValue *CreateRoundValue(SINT type)	// FUNCTION: 0x00437490
{
	switch (type)
	{
	case PRV_LIFE_SPAN:	return new(MT_PHYSICS) CRoundLifeSpan;
	case PRV_VELOCITY:	return new(MT_PHYSICS) CRoundVelocity;
	case PRV_DAMAGE:	return new(MT_PHYSICS) CRoundDamage;
	case PRV_SEEK:	return new(MT_PHYSICS) CRoundSeek;
	case PRV_TURN_RATE:	return new(MT_PHYSICS) CRoundTurnRate;
	case PRV_GRAVITY:	return new(MT_PHYSICS) CRoundGravity;
	case PRV_BOUNCE:	return new(MT_PHYSICS) CRoundBounce;
	case PRV_EFFECT:	return new(MT_PHYSICS) CRoundEffect;
	case PRV_WATER_EFFECT:	return new(MT_PHYSICS) CRoundWaterEffect;
	case PRV_EXPLOSION:	return new(MT_PHYSICS) CRoundExplosion;
	case PRV_SEEK_DELAY:	return new(MT_PHYSICS) CRoundSeekDelay;
	case PRV_SEEK_ANGLE:	return new(MT_PHYSICS) CRoundSeekAngle;
	case PRV_WIGGLE:	return new(MT_PHYSICS) CRoundWiggle;
	case PRV_FLAK:	return new(MT_PHYSICS) CRoundFlak;
	case PRV_FLAK_INACCURACY:	return new(MT_PHYSICS) CRoundFlakInaccuracy;
	case PRV_BEAM:	return new(MT_PHYSICS) CRoundBeam;
	case PRV_SOUND_MATERIAL:	return new(MT_PHYSICS) CRoundSoundMaterial;
	case PRV_WEIRDO_SEEK:	return new(MT_PHYSICS) CRoundWeirdoSeek;
	case PRV_REARM:	return new(MT_PHYSICS) CRoundRearm;
	case PRV_BASED_ON:	return new(MT_PHYSICS) CRoundBasedOn;
	case PRV_SEEK_TERMINATION_TIME:	return new(MT_PHYSICS) CRoundSeekTerminationTime;
	case PRV_PROXIMITY:	return new(MT_PHYSICS) CRoundProximity;
	case PRV_GRID_OF_FEAR:	return new(MT_PHYSICS) CRoundGridOfFear;
	case PRV_RADIUS:	return new(MT_PHYSICS) CRoundRadius;
	case PRV_UNDER_WATER:	return new(MT_PHYSICS) CRoundUnderWater;
	case PRV_GROUND_HUGGING:	return new(MT_PHYSICS) CRoundGroundHugging;
	case PRV_TORPEDO:	return new(MT_PHYSICS) CRoundTorpedo;
	case PRV_PASSIVE_COLLISION:	return new(MT_PHYSICS) CRoundPassiveCollision;
	case PRV_JUMPS:	return new(MT_PHYSICS) CRoundJumps;
	case PRV_JUMP_RANGE:	return new(MT_PHYSICS) CRoundJumpRange;
	case PRV_JUMP_DELAY:	return new(MT_PHYSICS) CRoundJumpDelay;
	case PRV_EXPLODE:	return new(MT_PHYSICS) CRoundExplode;
	case PRV_FIRE:	return new(MT_PHYSICS) CRoundFire;
	case PRV_TREE_COLLISION:	return new(MT_PHYSICS) CRoundTreeCollision;
	case PRV_MISSILE:	return new(MT_PHYSICS) CRoundMissile;
	case PRV_MESH:	return new(MT_PHYSICS) CRoundMesh;
	case PRV_SMART:	return new(MT_PHYSICS) CRoundSmart;
	case PRV_LENGTH:	return new(MT_PHYSICS) CRoundLength;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsSpawnerValue *CreateSpawnerValue(SINT type)	// FUNCTION: 0x00439b40
{
	switch (type)
	{
	case PSV_UNIT:	return new(MT_PHYSICS) CSpawnerUnit;
	case PSV_DELAY:	return new(MT_PHYSICS) CSpawnerDelay;
	case PSV_AMOUNT:	return new(MT_PHYSICS) CSpawnerAmount;
	case PSV_SQUAD_SIZE:	return new(MT_PHYSICS) CSpawnerSquadSize;
	case PSV_SQUAD_DELAY:	return new(MT_PHYSICS) CSpawnerSquadDelay;
	case PSV_SEEK_DELAY:	return new(MT_PHYSICS) CSpawnerSeekDelay;
	case PSV_RECALL:	return new(MT_PHYSICS) CSpawnerRecall;
	case PSV_MIN_RANGE:	return new(MT_PHYSICS) CSpawnerMinRange;
	case PSV_MAX_RANGE:	return new(MT_PHYSICS) CSpawnerMaxRange;
	case PSV_PRE_SPAWN_DELAY:	return new(MT_PHYSICS) CSpawnerPreSpawnDelay;
	case PSV_POST_SPAWN_DELAY:	return new(MT_PHYSICS) CSpawnerPostSpawnDelay;
	case PSV_BASED_ON:	return new(MT_PHYSICS) CSpawnerBasedOn;
	case PSV_INFINITE:	return new(MT_PHYSICS) CSpawnerInfinite;
	case PSV_CONDITIONS:	return new(MT_PHYSICS) CSpawnerConditions;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsExplosionValue *CreateExplosionValue(SINT type)	// FUNCTION: 0x0043a860
{
	switch (type)
	{
	case PEV_BASED_ON:	return new(MT_PHYSICS) CExplosionBasedOn;
	case PEV_AIR_EFFECT:	return new(MT_PHYSICS) CExplosionAirEffect;
	case PEV_GROUND_EFFECT:	return new(MT_PHYSICS) CExplosionGroundEffect;
	case PEV_WATER_EFFECT:	return new(MT_PHYSICS) CExplosionWaterEffect;
	case PEV_UNIT_EFFECT:	return new(MT_PHYSICS) CExplosionUnitEffect;
	case PEV_DAMAGE:	return new(MT_PHYSICS) CExplosionDamage;
	case PEV_RADIUS:	return new(MT_PHYSICS) CExplosionRadius;
	case PEV_VOLUMETRIC:	return new(MT_PHYSICS) CExplosionVolumetric;
	case PEV_TIME:	return new(MT_PHYSICS) CExplosionTime;
	case PEV_SOUND:	return new(MT_PHYSICS) CExplosionSound;
	case PEV_SMART:	return new(MT_PHYSICS) CExplosionSmart;
	case PEV_LIGHT:	return new(MT_PHYSICS) CExplosionLight;
	case PEV_ORIENTED:	return new(MT_PHYSICS) CExplosionOriented;
	case PEV_SHOCKWAVE:	return new(MT_PHYSICS) CExplosionShockwave;
	case PEV_WATER_SOUND:	return new(MT_PHYSICS) CExplosionWaterSound;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsFeatureValue *CreateFeatureValue(SINT type)	// FUNCTION: 0x0043b990
{
	switch (type)
	{
	case PFV_LIFE:	return new(MT_PHYSICS) CFeatureLife;
	case PFV_MESH:	return new(MT_PHYSICS) CFeatureMesh;
	case PFV_EXPLOSION:	return new(MT_PHYSICS) CFeatureExplosion;
	case PFV_NOISE:	return new(MT_PHYSICS) CFeatureNoise;
	case PFV_INVINCIBLE:	return new(MT_PHYSICS) CFeatureInvincible;
	case PFV_LOCKABLE:	return new(MT_PHYSICS) CFeatureLockable;
	case PFV_ICON:	return new(MT_PHYSICS) CFeatureIcon;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsHazardValue *CreateHazardValue(SINT type)	// FUNCTION: 0x0043c0b0
{
	switch (type)
	{
	case PHV_RADIUS:	return new(MT_PHYSICS) CHazardRadius;
	case PHV_DAMAGE:	return new(MT_PHYSICS) CHazardDamage;
	case PHV_EFFECT:	return new(MT_PHYSICS) CHazardEffect;
	case PHV_NOISE:	return new(MT_PHYSICS) CHazardNoise;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsComponentValue *CreateComponentValue(SINT type)	// FUNCTION: 0x0043c500
{
	switch (type)
	{
	case PCV_MESH:	return new(MT_PHYSICS) CComponentMesh;
	case PCV_MAX_YAW:	return new(MT_PHYSICS) CComponentMaxYaw;
	case PCV_MAX_TURRET_YAW:	return new(MT_PHYSICS) CComponentMaxTurretYaw;
	case PCV_LIFE:	return new(MT_PHYSICS) CComponentLife;
	case PCV_MAX_TARGET_RANGE:	return new(MT_PHYSICS) CComponentMaxTargetRange;
	case PCV_USE:	return new(MT_PHYSICS) CComponentUse;
	case PCV_USE_SPAWNER:	return new(MT_PHYSICS) CComponentUseSpawner;
	case PCV_EXPLOSION:	return new(MT_PHYSICS) CComponentExplosion;
	case PCV_TURN_RATE:	return new(MT_PHYSICS) CComponentTurnRate;
	case PCV_BASED_ON:	return new(MT_PHYSICS) CComponentBasedOn;
	case PCV_TURRET_TURN_RATE:	return new(MT_PHYSICS) CComponentTurretTurnRate;
	case PCV_NOISE:	return new(MT_PHYSICS) CComponentNoise;
	case PCV_BIG:	return new(MT_PHYSICS) CComponentBig;
	case PCV_INDISCRIMINATE:	return new(MT_PHYSICS) CComponentIndiscriminate;
	case PCV_SMALL_EXPLOSION:	return new(MT_PHYSICS) CComponentSmallExplosion;
	case PCV_DEPLOY:	return new(MT_PHYSICS) CComponentDeploy;
	case PCV_DEPLOY_DAMAGE_MULTIPLIER:	return new(MT_PHYSICS) CComponentDeployDamageMultiplier;
	case PCV_TENTACLE:	return new(MT_PHYSICS) CComponentTentacle;
	case PCV_ATTACK_PRIORITY:	return new(MT_PHYSICS) CComponentAttackPriority;
	case PCV_DETACH:	return new(MT_PHYSICS) CComponentDetach;
	case PCV_LOCKABLE:	return new(MT_PHYSICS) CComponentLockable;
	case PCV_SWEEPING:	return new(MT_PHYSICS) CComponentSweeping;
	case PCV_PASS_ON_HIT_EFFECT:	return new(MT_PHYSICS) CComponentPassOnHitEffect;
	case PCV_VENT:	return new(MT_PHYSICS) CComponentVent;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsSeekType *CreateSeekType(SINT type)	// FUNCTION: 0x0043dcd0
{
	switch (type)
	{
	case 1:	return new(MT_PHYSICS) CSeekNothing;
	case 2:	return new(MT_PHYSICS) CSeekTarget;
	case 3:	return new(MT_PHYSICS) CSeekTargetOnly;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsBehaviourType *CreateBehaviourType(SINT type)	// FUNCTION: 0x0043ddc0
{
	switch (type)
	{
	case 1:	return new(MT_PHYSICS) CMechUnitBehaviourType;
	case 3:	return new(MT_PHYSICS) CGroundUnitBehaviourType;
	case 4:	return new(MT_PHYSICS) CInfantryUnitBehaviourType;
	case 5:	return new(MT_PHYSICS) CTurretUnitBehaviourType;
	case 6:	return new(MT_PHYSICS) CBoatUnitBehaviourType;
	case 7:	return new(MT_PHYSICS) CCarrierUnitBehaviourType;
	case 8:	return new(MT_PHYSICS) CBuildingUnitBehaviourType;
	case 9:	return new(MT_PHYSICS) CFighterBehaviourType;
	case 10:	return new(MT_PHYSICS) CBomberBehaviourType;
	case 11:	return new(MT_PHYSICS) CGroundAttackAircraftBehaviourType;
	case 2:	return new(MT_PHYSICS) CJeepBehaviourType;
	case 12:	return new(MT_PHYSICS) CDropshipBehaviourType;
	case 13:	return new(MT_PHYSICS) CMineBehaviourType;
	case 14:	return new(MT_PHYSICS) CHiveBossBehaviourType;
	case 15:	return new(MT_PHYSICS) CSubmarineBehaviourType;
	case 16:	return new(MT_PHYSICS) CDiveBomberBehaviourType;
	case 17:	return new(MT_PHYSICS) CThunderHeadBehaviourType;
	case 18:	return new(MT_PHYSICS) CCarverBehaviourType;
	case 19:	return new(MT_PHYSICS) CGillMBehaviourType;
	case 20:	return new(MT_PHYSICS) CSentinelBehaviourType;
	case 21:	return new(MT_PHYSICS) CWarspiteBehaviourType;
	case 23:	return new(MT_PHYSICS) CWarspiteDomeBehaviourType;
	case 22:	return new(MT_PHYSICS) CFenrirBehaviourType;
	case 24:	return new(MT_PHYSICS) CPodBehaviourType;
	case 25:	return new(MT_PHYSICS) CSimpleBuildingBehaviourType;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsAlligenceType *CreateAlligenceType(SINT type)	// FUNCTION: 0x0043e310
{
	switch (type)
	{
	case 3:	return new(MT_PHYSICS) CNeutralAlligence;
	case 1:	return new(MT_PHYSICS) CFriendlyAlligence;
	case 2:	return new(MT_PHYSICS) CEnemyAlligence;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsNavMapType *CreateNavMapType(SINT type)	// FUNCTION: 0x0043e400
{
	switch (type)
	{
	case 1:	return new(MT_PHYSICS) CWaterNavMapType;
	case 2:	return new(MT_PHYSICS) CFlatNavMapType;
	case 3:	return new(MT_PHYSICS) CSteepNavMapType;
	case 4:	return new(MT_PHYSICS) CMountainousNavMapType;
	}
	return NULL;
}

//******************************************************************************************
CPhysicsStateType *CreateStateType(SINT type)	// FUNCTION: 0x0043e540
{
	switch (type)
	{
	case 1:	return new(MT_PHYSICS) COnStateType;
	case 2:	return new(MT_PHYSICS) COnfStateType;
	case 3:	return new(MT_PHYSICS) COffStateType;
	}
	return NULL;
}

//******************************************************************************************
void SkipStatement(CMEMBUFFER &file, SINT size)	// FUNCTION: 0x0043e630
{
	for (SINT i = size; i > 0; i--)
	{
		char	skip;
		file.Read(&skip, 1);
	}
}

//******************************************************************************************
// The weapons' names in the player's language, by their text ids.
void CWeaponLanguageName::Apply(char *name)	// FUNCTION: 0x0043e660
{
	CWeaponData	*weapon = UPhysicsManager::GetWeaponData(name);
	if (weapon)
	{
		switch (mValue)
		{
		case 1:		weapon->mLanguageName = 0xfe0572;	break;
		case 2:		weapon->mLanguageName = 0xffe389;	break;
		case 3:		weapon->mLanguageName = 0x101c1a0;	break;
		case 4:		weapon->mLanguageName = 0x1039fb7;	break;
		case 5:		weapon->mLanguageName = 0x1057dce;	break;
		case 6:		weapon->mLanguageName = 0x1075be5;	break;
		case 7:		weapon->mLanguageName = 0x10939fc;	break;
		case 8:		weapon->mLanguageName = 0x10b1813;	break;
		case 9:		weapon->mLanguageName = 0x10cf62a;	break;
		case 10:	weapon->mLanguageName = 0x1b1d4bb;	break;
		case 11:	weapon->mLanguageName = 0x1b590e8;	break;
		case 12:	weapon->mLanguageName = 0x1b94d15;	break;
		case 13:	weapon->mLanguageName = 0x1bd0942;	break;
		}
	}
}
// FUNCTION: 0x0042ede0 ?Execute@CUnitStatement@@UAEXXZ
// FUNCTION: 0x0042f230 ?GetSize@CUnitStatement@@UAEHXZ
// FUNCTION: 0x0042f2b0 ?Load@CUnitStatement@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00432a20 ?Load@CUnitAlligence@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00432af0 ?Apply@CUnitAirVelocity@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432b00 ?Apply@CUnitAirTurnRate@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432b10 ?Apply@CUnitTurretTurnRate@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432b20 ?Apply@CUnitLife@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432b30 ?Apply@CUnitIcon@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432b40 ?Apply@CUnitAirShields@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432b50 ?Apply@CUnitGroundShields@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432b60 ?Apply@CUnitAirShieldRegeneration@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432b70 ?Apply@CUnitGroundShieldRegeneration@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432b80 ?Apply@CUnitHover@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432bc0 ?Apply@CUnitThreat@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432bd0 ?Apply@CUnitImportance@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432be0 ?Apply@CUnitMinAltitude@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432bf0 ?Apply@CUnitMaxTargetRange@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432c00 ?Apply@CUnitSoundMaterial@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432c20 ?Apply@CUnitStealth@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432c30 ?Apply@CUnitBodyMotion@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432c40 ?Apply@CUnitLegSpeed@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432c50 ?Apply@CUnitLegPlacementArea@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432c60 ?Apply@CUnitStandingLegPlacementArea@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432c70 ?Apply@CUnitMaxLegsLifted@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432c90 ?Apply@CUnitAttackPriority@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432cb0 ?Apply@CUnitBounceFactor@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432cd0 ?Apply@CUnitParachute@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432ce0 ?Apply@CUnitKnockTrees@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432cf0 ?Apply@CUnitDeploy@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432d20 ?Apply@CUnitDeployDamageMultiplier@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432d30 ?Apply@CUnitRetreat@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432d60 ?Apply@CUnitBig@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432d90 ?Apply@CUnitIndiscriminate@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432dc0 ?Apply@CUnitShatter@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432df0 ?Apply@CUnitLockable@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432e20 ?Apply@CUnitSweeping@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432e50 ?Apply@CUnitIgnoreThreats@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432e80 ?Apply@CUnitAnimatedCollision@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432eb0 ?Apply@CUnitWingman@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432ee0 ?Apply@CUnitMultipleSpawn@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432f10 ?Apply@CUnitStrafeChange@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432f20 ?Apply@CUnitMinThrusterHeight@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432f30 ?Apply@CUnitMaxThrusterHeight@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432f40 ?Apply@CUnitDetonationDelay@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432f50 ?Apply@CUnitNavMap@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00432f70 ?Load@CUnitNavMap@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00433010 ?Apply@CUnitBehaviour@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x004330b0 ?Load@CUnitBehaviour@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00433150 ?Apply@CUnitUse@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433170 ?Load@CUnitUse@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x004331c0 ?Apply@CUnitUseSpawner@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x004331e0 ?GetSize@CUnitUseSpawner@@UAEHXZ
// FUNCTION: 0x00433220 ?Load@CUnitUseSpawner@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00433270 ?Apply@CUnitComponent@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433290 ?GetSize@CUnitComponent@@UAEHXZ
// FUNCTION: 0x004332b0 ?Load@CUnitComponent@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x004332e0 ?Apply@CUnitBasedOn@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433ce0 ?Apply@CUnitMesh@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433d50 ?Apply@CUnitExplosion@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433d60 ?Apply@CUnitSmallExplosion@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433d70 ?Apply@CUnitStompExplosion@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433d80 ?Apply@CUnitActivationEffect@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433df0 ?Apply@CUnitNoise@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433e60 ?Apply@CUnitSpawnNoise@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433ed0 ?Apply@CUnitTrails@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433f40 ?Apply@CUnitThrusters@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433fb0 ?Apply@CUnitSmoke@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00434020 ?Apply@CUnitWaterWake@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00434090 ?Apply@CUnitGroundWake@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00434120 ?Apply@CUnitDust@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00434190 ?Apply@CUnitVent@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00434200 ?Apply@CUnitAfterburner@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00434270 ?Apply@CUnitShieldEffect@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x004342e0 ?Apply@CUnitBuildingNumber@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x004342f0 ?Apply@CUnitBuildingMeshType@@UAEXPAVCUnitData@@@Z
// FUNCTION: 0x00434b60 ?Load@CUnitIndiscriminate@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00435b20 ?Load@CUnitAttackPriority@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x004398f0 ?GetSize@CUnitGroundWake@@UAEHXZ
// FUNCTION: 0x0043b1a0 ?Load@CUnitGroundWake@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0043d670 ?GetSize@CUnitUse@@UAEHXZ
// FUNCTION: 0x0042ee90 ?AddUnit@UPhysicsManager@@SAXPAD@Z
// FUNCTION: 0x0042f280 ?GetSize@CPhysicsUnitValueList@@QAEHXZ
// FUNCTION: 0x0042f3d0 ?Load@CPhysicsUnitValueList@@QAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00430fa0 ?Apply@CPhysicsUnitValueList@@QAEXPAVCUnitData@@@Z
// FUNCTION: 0x00433390 ?CopyFrom@CUnitData@@QAEXPAV1@@Z
// FUNCTION: 0x0042efd0 ?Init@CUnitData@@QAEXXZ
// FUNCTION: 0x00434610 ?Apply@CWeaponChargeLevel@@UAEXPAD@Z
// FUNCTION: 0x00434750 ?GetSize@CWeaponChargeLevel@@UAEHXZ
// FUNCTION: 0x00434770 ?Load@CWeaponChargeLevel@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x004347e0 ?Apply@CWeaponChargeRate@@UAEXPAD@Z
// FUNCTION: 0x00434870 ?Apply@CWeaponAmmoStore@@UAEXPAD@Z
// FUNCTION: 0x00434930 ?Apply@CWeaponConsumption@@UAEXPAD@Z
// FUNCTION: 0x004349c0 ?Apply@CWeaponAdjustAim@@UAEXPAD@Z
// FUNCTION: 0x00434aa0 ?Apply@CWeaponSmart@@UAEXPAD@Z
// FUNCTION: 0x00434b80 ?Apply@CWeaponZoomMode@@UAEXPAD@Z
// FUNCTION: 0x00434c20 ?Apply@CWeaponPlacement@@UAEXPAD@Z
// FUNCTION: 0x00434cc0 ?Apply@CWeaponVersusInfantry@@UAEXPAD@Z
// FUNCTION: 0x00434d50 ?Apply@CWeaponVersusTanks@@UAEXPAD@Z
// FUNCTION: 0x00434de0 ?Apply@CWeaponVersusAir@@UAEXPAD@Z
// FUNCTION: 0x00434e70 ?Apply@CWeaponAllowMovement@@UAEXPAD@Z
// FUNCTION: 0x00434f20 ?Apply@CWeaponIconName@@UAEXPAD@Z
// FUNCTION: 0x00435840 ?Apply@CWeaponBasedOn@@UAEXPAD@Z
// FUNCTION: 0x00435a00 ?Apply@CWeaponLaunchSequence@@UAEXPAD@Z
// FUNCTION: 0x00435b50 ?Apply@CWeaponLaunchAngle@@UAEXPAD@Z
// FUNCTION: 0x00435c90 ?Load@CWeaponLaunchAngle@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00435cd0 ?Apply@CWeaponInaccuracy@@UAEXPAD@Z
// FUNCTION: 0x00435d60 ?Apply@CWeaponPower@@UAEXPAD@Z
// FUNCTION: 0x00435df0 ?Apply@CWeaponMuzzleLight@@UAEXPAD@Z
// FUNCTION: 0x00435e90 ?Apply@CWeaponMuzzleLightRadius@@UAEXPAD@Z
// FUNCTION: 0x00435f30 ?Apply@CWeaponPredictive@@UAEXPAD@Z
// FUNCTION: 0x00435ff0 ?Apply@CWeaponBurstSize@@UAEXPAD@Z
// FUNCTION: 0x004360a0 ?Apply@CWeaponBurstDelay@@UAEXPAD@Z
// FUNCTION: 0x00436130 ?Apply@CWeaponVolleySize@@UAEXPAD@Z
// FUNCTION: 0x004361e0 ?Apply@CWeaponPreFireDelay@@UAEXPAD@Z
// FUNCTION: 0x00436280 ?Apply@CWeaponPostFireDelay@@UAEXPAD@Z
// FUNCTION: 0x00436320 ?Apply@CWeaponPreFireEffect@@UAEXPAD@Z
// FUNCTION: 0x00436410 ?Apply@CWeaponMuzzleEffect@@UAEXPAD@Z
// FUNCTION: 0x00436500 ?Apply@CWeaponClip@@UAEXPAD@Z
// FUNCTION: 0x004365f0 ?Apply@CWeaponReloadTime@@UAEXPAD@Z
// FUNCTION: 0x00436680 ?Apply@CWeaponTrack@@UAEXPAD@Z
// FUNCTION: 0x00436740 ?Apply@CWeaponSoundPerBurst@@UAEXPAD@Z
// FUNCTION: 0x00436800 ?Apply@CWeaponMinRange@@UAEXPAD@Z
// FUNCTION: 0x00436890 ?Apply@CWeaponMaxRange@@UAEXPAD@Z
// FUNCTION: 0x00436920 ?Apply@CWeaponMinDeflection@@UAEXPAD@Z
// FUNCTION: 0x004369b0 ?Apply@CWeaponMaxDeflection@@UAEXPAD@Z
// FUNCTION: 0x00436a50 ?Apply@CWeaponMinTargetHeight@@UAEXPAD@Z
// FUNCTION: 0x00436ae0 ?Apply@CWeaponMaxTargetHeight@@UAEXPAD@Z
// FUNCTION: 0x00436b70 ?Apply@CWeaponYawTolerance@@UAEXPAD@Z
// FUNCTION: 0x00436c10 ?Apply@CWeaponLockTime@@UAEXPAD@Z
// FUNCTION: 0x00436cb0 ?Apply@CWeaponLockDeflection@@UAEXPAD@Z
// FUNCTION: 0x00436d50 ?Apply@CWeaponLockMode@@UAEXPAD@Z
// FUNCTION: 0x00436df0 ?Apply@CWeaponLockUnit@@UAEXPAD@Z
// FUNCTION: 0x00436e90 ?Apply@CWeaponLockRange@@UAEXPAD@Z
// FUNCTION: 0x00436f30 ?Apply@CWeaponLockRadius@@UAEXPAD@Z
// FUNCTION: 0x00436fd0 ?Apply@CWeaponMaxLocks@@UAEXPAD@Z
// FUNCTION: 0x004370a0 ?Apply@CWeaponRound@@UAEXPAD@Z
// FUNCTION: 0x004371c0 ?Apply@CWeaponLaunchSound@@UAEXPAD@Z
// FUNCTION: 0x004372b0 ?Apply@CWeaponPreFireSound@@UAEXPAD@Z
// FUNCTION: 0x004373a0 ?Apply@CWeaponPostFireSound@@UAEXPAD@Z
// FUNCTION: 0x00437d00 ?Apply@CRoundBasedOn@@UAEXPAD@Z
// FUNCTION: 0x00438100 ?Apply@CRoundSoundMaterial@@UAEXPAD@Z
// FUNCTION: 0x004381b0 ?Apply@CRoundDamage@@UAEXPAD@Z
// FUNCTION: 0x00438240 ?Apply@CRoundRearm@@UAEXPAD@Z
// FUNCTION: 0x004382e0 ?Apply@CRoundLifeSpan@@UAEXPAD@Z
// FUNCTION: 0x00438370 ?Apply@CRoundVelocity@@UAEXPAD@Z
// FUNCTION: 0x00438420 ?Apply@CRoundTurnRate@@UAEXPAD@Z
// FUNCTION: 0x004384b0 ?Apply@CRoundGravity@@UAEXPAD@Z
// FUNCTION: 0x00438550 ?Apply@CRoundBounce@@UAEXPAD@Z
// FUNCTION: 0x004385e0 ?Apply@CRoundRadius@@UAEXPAD@Z
// FUNCTION: 0x00438680 ?Apply@CRoundWiggle@@UAEXPAD@Z
// FUNCTION: 0x00438710 ?Apply@CRoundFlak@@UAEXPAD@Z
// FUNCTION: 0x004387b0 ?Apply@CRoundFlakInaccuracy@@UAEXPAD@Z
// FUNCTION: 0x00438850 ?Apply@CRoundBeam@@UAEXPAD@Z
// FUNCTION: 0x004388f0 ?Apply@CRoundSeekDelay@@UAEXPAD@Z
// FUNCTION: 0x00438980 ?Apply@CRoundProximity@@UAEXPAD@Z
// FUNCTION: 0x00438ab0 ?Apply@CRoundSeekTerminationTime@@UAEXPAD@Z
// FUNCTION: 0x00438b40 ?Apply@CRoundGridOfFear@@UAEXPAD@Z
// FUNCTION: 0x00438bf0 ?Apply@CRoundUnderWater@@UAEXPAD@Z
// FUNCTION: 0x00438ca0 ?Apply@CRoundGroundHugging@@UAEXPAD@Z
// FUNCTION: 0x00438d50 ?Apply@CRoundTorpedo@@UAEXPAD@Z
// FUNCTION: 0x00438e10 ?Apply@CRoundMissile@@UAEXPAD@Z
// FUNCTION: 0x00438ed0 ?Apply@CRoundFire@@UAEXPAD@Z
// FUNCTION: 0x00438f90 ?Apply@CRoundSmart@@UAEXPAD@Z
// FUNCTION: 0x00439050 ?Apply@CRoundPassiveCollision@@UAEXPAD@Z
// FUNCTION: 0x00439100 ?Apply@CRoundJumps@@UAEXPAD@Z
// FUNCTION: 0x004391b0 ?Apply@CRoundJumpRange@@UAEXPAD@Z
// FUNCTION: 0x00439250 ?Apply@CRoundJumpDelay@@UAEXPAD@Z
// FUNCTION: 0x004392f0 ?Apply@CRoundLength@@UAEXPAD@Z
// FUNCTION: 0x00439390 ?Apply@CRoundExplode@@UAEXPAD@Z
// FUNCTION: 0x00439440 ?Apply@CRoundWeirdoSeek@@UAEXPAD@Z
// FUNCTION: 0x004394e0 ?Apply@CRoundSeek@@UAEXPAD@Z
// FUNCTION: 0x00439580 ?Load@CRoundSeek@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00439620 ?Apply@CRoundMesh@@UAEXPAD@Z
// FUNCTION: 0x00439710 ?Apply@CRoundEffect@@UAEXPAD@Z
// FUNCTION: 0x00439800 ?Apply@CRoundWaterEffect@@UAEXPAD@Z
// FUNCTION: 0x00439910 ?Apply@CRoundExplosion@@UAEXPAD@Z
// FUNCTION: 0x00439a00 ?Apply@CRoundTreeCollision@@UAEXPAD@Z
// FUNCTION: 0x00439aa0 ?Load@CRoundTreeCollision@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00437fe0 ?SetMesh@CRoundData@@QAEXPAD@Z
// FUNCTION: 0x00438050 ?SetExplosion@CRoundData@@QAEXPAD@Z
// FUNCTION: 0x00439e70 ?Apply@CSpawnerBasedOn@@UAEXPAD@Z
// FUNCTION: 0x0043a080 ?Apply@CSpawnerUnit@@UAEXPAD@Z
// FUNCTION: 0x0043a170 ?Apply@CSpawnerDelay@@UAEXPAD@Z
// FUNCTION: 0x0043a200 ?Apply@CSpawnerAmount@@UAEXPAD@Z
// FUNCTION: 0x0043a290 ?Apply@CSpawnerConditions@@UAEXPAD@Z
// FUNCTION: 0x0043a320 ?Apply@CSpawnerSquadSize@@UAEXPAD@Z
// FUNCTION: 0x0043a3b0 ?Apply@CSpawnerSquadDelay@@UAEXPAD@Z
// FUNCTION: 0x0043a440 ?Apply@CSpawnerSeekDelay@@UAEXPAD@Z
// FUNCTION: 0x0043a4d0 ?Apply@CSpawnerRecall@@UAEXPAD@Z
// FUNCTION: 0x0043a570 ?Apply@CSpawnerMinRange@@UAEXPAD@Z
// FUNCTION: 0x0043a600 ?Apply@CSpawnerMaxRange@@UAEXPAD@Z
// FUNCTION: 0x0043a690 ?Apply@CSpawnerPreSpawnDelay@@UAEXPAD@Z
// FUNCTION: 0x0043a720 ?Apply@CSpawnerPostSpawnDelay@@UAEXPAD@Z
// FUNCTION: 0x0043a7b0 ?Apply@CSpawnerInfinite@@UAEXPAD@Z
// FUNCTION: 0x0043abd0 ?Apply@CExplosionBasedOn@@UAEXPAD@Z
// FUNCTION: 0x0043afc0 ?Apply@CExplosionAirEffect@@UAEXPAD@Z
// FUNCTION: 0x0043b0b0 ?Apply@CExplosionGroundEffect@@UAEXPAD@Z
// FUNCTION: 0x0043b1c0 ?Apply@CExplosionWaterEffect@@UAEXPAD@Z
// FUNCTION: 0x0043b2b0 ?Apply@CExplosionUnitEffect@@UAEXPAD@Z
// FUNCTION: 0x0043b3a0 ?Apply@CExplosionRadius@@UAEXPAD@Z
// FUNCTION: 0x0043b430 ?Apply@CExplosionDamage@@UAEXPAD@Z
// FUNCTION: 0x0043b4c0 ?Apply@CExplosionVolumetric@@UAEXPAD@Z
// FUNCTION: 0x0043b550 ?Apply@CExplosionSmart@@UAEXPAD@Z
// FUNCTION: 0x0043b5e0 ?Apply@CExplosionOriented@@UAEXPAD@Z
// FUNCTION: 0x0043b670 ?Apply@CExplosionLight@@UAEXPAD@Z
// FUNCTION: 0x0043b700 ?Apply@CExplosionTime@@UAEXPAD@Z
// FUNCTION: 0x0043b790 ?Apply@CExplosionSound@@UAEXPAD@Z
// FUNCTION: 0x0043b880 ?Apply@CExplosionWaterSound@@UAEXPAD@Z
// FUNCTION: 0x0043aea0 ?SetSound@CExplosionData@@QAEXPAD@Z
// FUNCTION: 0x0043af10 ?SetWaterSound@CExplosionData@@QAEXPAD@Z
// FUNCTION: 0x0042f5b0 ?Execute@CWeaponStatement@@UAEXXZ
// FUNCTION: 0x0042f700 ?GetSize@CWeaponStatement@@UAEHXZ
// FUNCTION: 0x0042f780 ?Load@CWeaponStatement@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0042fa40 ?Execute@CWeaponModeStatement@@UAEXXZ
// FUNCTION: 0x0042fc20 ?GetSize@CWeaponModeStatement@@UAEHXZ
// FUNCTION: 0x0042fca0 ?Load@CWeaponModeStatement@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0042ff60 ?Execute@CRoundStatement@@UAEXXZ
// FUNCTION: 0x00430190 ?GetSize@CRoundStatement@@UAEHXZ
// FUNCTION: 0x00430210 ?Load@CRoundStatement@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x004304d0 ?Execute@CSpawnerStatement@@UAEXXZ
// FUNCTION: 0x00430660 ?GetSize@CSpawnerStatement@@UAEHXZ
// FUNCTION: 0x004306e0 ?Load@CSpawnerStatement@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x004309a0 ?Execute@CExplosionStatement@@UAEXXZ
// FUNCTION: 0x00430ae0 ?GetSize@CExplosionStatement@@UAEHXZ
// FUNCTION: 0x00430b60 ?Load@CExplosionStatement@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00430e20 ?Execute@CComponentStatement@@UAEXXZ
// FUNCTION: 0x00430fd0 ?GetSize@CComponentStatement@@UAEHXZ
// FUNCTION: 0x00431050 ?Load@CComponentStatement@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00431310 ?Execute@CFeatureStatement@@UAEXXZ
// FUNCTION: 0x00431420 ?GetSize@CFeatureStatement@@UAEHXZ
// FUNCTION: 0x004314a0 ?Load@CFeatureStatement@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00431760 ?Execute@CHazardStatement@@UAEXXZ
// FUNCTION: 0x00431870 ?GetSize@CHazardStatement@@UAEHXZ
// FUNCTION: 0x004318f0 ?Load@CHazardStatement@@UAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0043bb30 ?Apply@CFeatureLife@@UAEXPAD@Z
// FUNCTION: 0x0043bbf0 ?Apply@CFeatureIcon@@UAEXPAD@Z
// FUNCTION: 0x0043bc80 ?Apply@CFeatureInvincible@@UAEXPAD@Z
// FUNCTION: 0x0043bd40 ?Apply@CFeatureLockable@@UAEXPAD@Z
// FUNCTION: 0x0043be10 ?Apply@CFeatureMesh@@UAEXPAD@Z
// FUNCTION: 0x0043bf00 ?Apply@CFeatureNoise@@UAEXPAD@Z
// FUNCTION: 0x0043c010 ?Apply@CFeatureExplosion@@UAEXPAD@Z
// FUNCTION: 0x0043c1a0 ?Apply@CHazardRadius@@UAEXPAD@Z
// FUNCTION: 0x0043c280 ?Apply@CHazardDamage@@UAEXPAD@Z
// FUNCTION: 0x0043c320 ?Apply@CHazardNoise@@UAEXPAD@Z
// FUNCTION: 0x0043c410 ?Apply@CHazardEffect@@UAEXPAD@Z
// FUNCTION: 0x0043ca70 ?Apply@CComponentMaxYaw@@UAEXPAD@Z
// FUNCTION: 0x0043cb40 ?Apply@CComponentMaxTurretYaw@@UAEXPAD@Z
// FUNCTION: 0x0043cbe0 ?Apply@CComponentLife@@UAEXPAD@Z
// FUNCTION: 0x0043cc80 ?Apply@CComponentMaxTargetRange@@UAEXPAD@Z
// FUNCTION: 0x0043cd20 ?Apply@CComponentTurnRate@@UAEXPAD@Z
// FUNCTION: 0x0043cdc0 ?Apply@CComponentTurretTurnRate@@UAEXPAD@Z
// FUNCTION: 0x0043ce60 ?Apply@CComponentBig@@UAEXPAD@Z
// FUNCTION: 0x0043cf20 ?Apply@CComponentIndiscriminate@@UAEXPAD@Z
// FUNCTION: 0x0043cfe0 ?Apply@CComponentTentacle@@UAEXPAD@Z
// FUNCTION: 0x0043d0a0 ?Apply@CComponentDetach@@UAEXPAD@Z
// FUNCTION: 0x0043d160 ?Apply@CComponentLockable@@UAEXPAD@Z
// FUNCTION: 0x0043d220 ?Apply@CComponentSweeping@@UAEXPAD@Z
// FUNCTION: 0x0043d2e0 ?Apply@CComponentPassOnHitEffect@@UAEXPAD@Z
// FUNCTION: 0x0043d3a0 ?Apply@CComponentDeploy@@UAEXPAD@Z
// FUNCTION: 0x0043d460 ?Apply@CComponentDeployDamageMultiplier@@UAEXPAD@Z
// FUNCTION: 0x0043d500 ?Apply@CComponentAttackPriority@@UAEXPAD@Z
// FUNCTION: 0x0043d5c0 ?Apply@CComponentUse@@UAEXPAD@Z
// FUNCTION: 0x0043d6b0 ?Apply@CComponentUseSpawner@@UAEXPAD@Z
// FUNCTION: 0x0043d760 ?Apply@CComponentMesh@@UAEXPAD@Z
// FUNCTION: 0x0043d850 ?Apply@CComponentExplosion@@UAEXPAD@Z
// FUNCTION: 0x0043d8f0 ?Apply@CComponentVent@@UAEXPAD@Z
// FUNCTION: 0x0043d9f0 ?Apply@CComponentSmallExplosion@@UAEXPAD@Z
// FUNCTION: 0x0043da90 ?Apply@CComponentNoise@@UAEXPAD@Z
// FUNCTION: 0x0043db90 ?Apply@CComponentBasedOn@@UAEXPAD@Z
// FUNCTION: 0x0042f5f0 ?AddWeapon@UPhysicsManager@@SAXPAD@Z
// FUNCTION: 0x0042f750 ?GetSize@CPhysicsWeaponValueList@@QAEHXZ
// FUNCTION: 0x0042f8a0 ?Load@CPhysicsWeaponValueList@@QAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0042fa80 ?AddWeaponMode@UPhysicsManager@@SAXPAD@Z
// FUNCTION: 0x0042fc70 ?GetSize@CPhysicsWeaponModeValueList@@QAEHXZ
// FUNCTION: 0x0042fdc0 ?Load@CPhysicsWeaponModeValueList@@QAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0042ffa0 ?AddRound@UPhysicsManager@@SAXPAD@Z
// FUNCTION: 0x00430330 ?Load@CPhysicsRoundValueList@@QAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00430510 ?AddSpawnerData@UPhysicsManager@@SAXPAD@Z
// FUNCTION: 0x00430800 ?Load@CPhysicsSpawnerValueList@@QAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x004309e0 ?AddExplosion@UPhysicsManager@@SAXPAD@Z
// FUNCTION: 0x00430c80 ?Load@CPhysicsExplosionValueList@@QAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00430e60 ?AddComponentData@UPhysicsManager@@SAXPAD@Z
// FUNCTION: 0x00431170 ?Load@CPhysicsComponentValueList@@QAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x00431350 ?AddFeature@UPhysicsManager@@SAXPAD@Z
// FUNCTION: 0x004315c0 ?Load@CPhysicsFeatureValueList@@QAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x004317a0 ?AddHazard@UPhysicsManager@@SAXPAD@Z
// FUNCTION: 0x00431a10 ?Load@CPhysicsHazardValueList@@QAEXAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0042f4b0 ??_GCPhysicsUnitValueList@@UAEPAXI@Z
// FUNCTION: 0x0042f4f0 ??_GCUnitStatement@@UAEPAXI@Z
// FUNCTION: 0x0042f580 ??_GCPhysicsScriptStatement@@UAEPAXI@Z
// FUNCTION: 0x0042f980 ??_GCPhysicsWeaponValueList@@UAEPAXI@Z
// FUNCTION: 0x0042f9c0 ??_GCWeaponStatement@@UAEPAXI@Z
// FUNCTION: 0x0042fea0 ??_GCPhysicsWeaponModeValueList@@UAEPAXI@Z
// FUNCTION: 0x0042fee0 ??_GCWeaponModeStatement@@UAEPAXI@Z
// FUNCTION: 0x00430410 ??_GCPhysicsRoundValueList@@UAEPAXI@Z
// FUNCTION: 0x00430450 ??_GCRoundStatement@@UAEPAXI@Z
// FUNCTION: 0x00430610 ??_GCSpawnerData@@UAEPAXI@Z
// FUNCTION: 0x004308e0 ??_GCPhysicsSpawnerValueList@@UAEPAXI@Z
// FUNCTION: 0x00430920 ??_GCSpawnerStatement@@UAEPAXI@Z
// FUNCTION: 0x00430d60 ??_GCPhysicsExplosionValueList@@UAEPAXI@Z
// FUNCTION: 0x00430da0 ??_GCExplosionStatement@@UAEPAXI@Z
// FUNCTION: 0x00431250 ??_GCPhysicsComponentValueList@@UAEPAXI@Z
// FUNCTION: 0x00431290 ??_GCComponentStatement@@UAEPAXI@Z
// FUNCTION: 0x004316a0 ??_GCPhysicsFeatureValueList@@UAEPAXI@Z
// FUNCTION: 0x004316e0 ??_GCFeatureStatement@@UAEPAXI@Z
// FUNCTION: 0x00431af0 ??_GCPhysicsHazardValueList@@UAEPAXI@Z
// FUNCTION: 0x00431b30 ??_GCHazardStatement@@UAEPAXI@Z
// FUNCTION: 0x00432a50 ??_GCUnitAlligence@@UAEPAXI@Z
// FUNCTION: 0x00432ac0 ??_GCPhysicsUnitValue@@UAEPAXI@Z
// FUNCTION: 0x00432fa0 ??_GCUnitNavMap@@UAEPAXI@Z
// FUNCTION: 0x004330e0 ??_GCUnitBehaviour@@UAEPAXI@Z
// FUNCTION: 0x00434100 ??_GCUnitIndiscriminate@@UAEPAXI@Z
// FUNCTION: 0x004347b0 ??_GCPhysicsWeaponValue@@UAEPAXI@Z
// FUNCTION: 0x00434a80 ??_GCWeaponVersusAir@@UAEPAXI@Z
// FUNCTION: 0x00437080 ??_GCWeaponMuzzleLightRadius@@UAEPAXI@Z
// FUNCTION: 0x004380d0 ??_GCPhysicsRoundValue@@UAEPAXI@Z
// FUNCTION: 0x00438400 ??_GCRoundLength@@UAEPAXI@Z
// FUNCTION: 0x004395b0 ??_GCRoundSeek@@UAEPAXI@Z
// FUNCTION: 0x00439ad0 ??_GCRoundTreeCollision@@UAEPAXI@Z
// FUNCTION: 0x0043a050 ??_GCPhysicsSpawnerValue@@UAEPAXI@Z
// FUNCTION: 0x0043a840 ??_GCSpawnerConditions@@UAEPAXI@Z
// FUNCTION: 0x0043af90 ??_GCPhysicsExplosionValue@@UAEPAXI@Z
// FUNCTION: 0x0043b970 ??_GCExplosionWaterSound@@UAEPAXI@Z
// FUNCTION: 0x0043bbc0 ??_GCPhysicsFeatureValue@@UAEPAXI@Z
// FUNCTION: 0x0043bff0 ??_GCFeatureIcon@@UAEPAXI@Z
// FUNCTION: 0x0043c230 ??_GCHazardNoise@@UAEPAXI@Z
// FUNCTION: 0x0043c250 ??_GCPhysicsHazardValue@@UAEPAXI@Z
// FUNCTION: 0x0043cb10 ??_GCPhysicsComponentValue@@UAEPAXI@Z
// FUNCTION: 0x0043d5a0 ??_GCComponentVent@@UAEPAXI@Z
// FUNCTION: 0x0043dd60 ??_GCPhysicsSeekType@@UAEPAXI@Z
// FUNCTION: 0x0043dd90 ??_GCSeekNothing@@UAEPAXI@Z
// FUNCTION: 0x0043e2b0 ??_GCSimpleBuildingBehaviourType@@UAEPAXI@Z
// FUNCTION: 0x0043e2d0 ??_GCPhysicsBehaviourType@@UAEPAXI@Z
// FUNCTION: 0x0043e3a0 ??_GCFriendlyAlligence@@UAEPAXI@Z
// FUNCTION: 0x0043e3d0 ??_GCPhysicsAlligenceType@@UAEPAXI@Z
// FUNCTION: 0x0043e4e0 ??_GCMountainousNavMapType@@UAEPAXI@Z
// FUNCTION: 0x0043e500 ??_GCPhysicsNavMapType@@UAEPAXI@Z
// FUNCTION: 0x0043e5d0 ??_GCOnStateType@@UAEPAXI@Z
// FUNCTION: 0x0043e5f0 ??_GCPhysicsStateType@@UAEPAXI@Z
// FUNCTION: 0x0042f510 ??1CUnitStatement@@UAE@XZ
// FUNCTION: 0x0042f9e0 ??1CWeaponStatement@@UAE@XZ
// FUNCTION: 0x0042ff00 ??1CWeaponModeStatement@@UAE@XZ
// FUNCTION: 0x00430470 ??1CRoundStatement@@UAE@XZ
// FUNCTION: 0x00430940 ??1CSpawnerStatement@@UAE@XZ
// FUNCTION: 0x00430dc0 ??1CExplosionStatement@@UAE@XZ
// FUNCTION: 0x004312b0 ??1CComponentStatement@@UAE@XZ
// FUNCTION: 0x00431700 ??1CFeatureStatement@@UAE@XZ
// FUNCTION: 0x00431b50 ??1CHazardStatement@@UAE@XZ
// FUNCTION: 0x00432a70 ??1CUnitAlligence@@UAE@XZ
// FUNCTION: 0x00432cc0 ??1CUnitIndiscriminate@@UAE@XZ
// FUNCTION: 0x00432fc0 ??1CUnitNavMap@@UAE@XZ
// FUNCTION: 0x00433100 ??1CUnitBehaviour@@UAE@XZ
// FUNCTION: 0x004347a0 ??1CWeaponVersusAir@@UAE@XZ
// FUNCTION: 0x004359c0 ??1CWeaponMuzzleLightRadius@@UAE@XZ
// FUNCTION: 0x004380c0 ??1CRoundLength@@UAE@XZ
// FUNCTION: 0x004395d0 ??1CRoundSeek@@UAE@XZ
// FUNCTION: 0x00439af0 ??1CRoundTreeCollision@@UAE@XZ
// FUNCTION: 0x0043a040 ??1CSpawnerConditions@@UAE@XZ
// FUNCTION: 0x0043af80 ??1CExplosionWaterSound@@UAE@XZ
// FUNCTION: 0x0043be00 ??1CFeatureIcon@@UAE@XZ
// FUNCTION: 0x0043c310 ??1CHazardNoise@@UAE@XZ
// FUNCTION: 0x0043dcc0 ??1CComponentVent@@UAE@XZ
// FUNCTION: 0x0043ddb0 ??1CSeekNothing@@UAE@XZ
// FUNCTION: 0x0043e300 ??1CSimpleBuildingBehaviourType@@UAE@XZ
// FUNCTION: 0x0043e3c0 ??1CFriendlyAlligence@@UAE@XZ
// FUNCTION: 0x0043e530 ??1CMountainousNavMapType@@UAE@XZ
// FUNCTION: 0x0043e620 ??1COnStateType@@UAE@XZ
// FUNCTION: 0x0042f220 ??1?$SPtrSet@VCUnitWeapon@@@@QAE@XZ
// FUNCTION: 0x0042f570 ??1CPhysicsScriptStatement@@UAE@XZ
// FUNCTION: 0x004301e0 ?GetSize@CPhysicsRoundValueList@@QAEHXZ
// FUNCTION: 0x004306b0 ?GetSize@CPhysicsSpawnerValueList@@QAEHXZ
// FUNCTION: 0x00430b30 ?GetSize@CPhysicsExplosionValueList@@QAEHXZ
// FUNCTION: 0x00431020 ?GetSize@CPhysicsComponentValueList@@QAEHXZ
// FUNCTION: 0x00431470 ?GetSize@CPhysicsFeatureValueList@@QAEHXZ
// FUNCTION: 0x004318c0 ?GetSize@CPhysicsHazardValueList@@QAEHXZ
// FUNCTION: 0x004359d0 ??_GCPhysicsWeaponModeValue@@UAEPAXI@Z
// FUNCTION: 0x00438a20 ?Apply@CRoundSeekAngle@@UAEXPAD@Z

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004ead60 ?GetValue@CSubmarineBehaviourType@@UAEHXZ

// Out of line: retail folds this `return 5` body into 004dbc20 (CInfantryUnit::GetImposterFrames), and LINK folds an
// out-of-line body only with out-of-line ones.
SINT CBoatUnitBehaviourType::GetValue()
{
	return UB_BOAT;
}
