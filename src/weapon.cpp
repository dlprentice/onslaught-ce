// weapon.cpp: reconstructed from weapon.obj (00505d10..0050a2ae); neither this file nor weapon.h was released.
// Some member names survive in Stuart's BattleEngine/part callers; other labels are reconstruction names.
#include "common.h"
#include "weapon.h"				// FUNCTION: 0x00505d10 init:ZERO_FVECTOR
#include "unit.h"				// FUNCTION: 0x00505d30 init:ID_FMATRIX
#include "eventmanager.h"
#include "WorldPhysicsManager.h"
#include "mathsconst.h"
#include "BattleEngine.h"
#include "SoundManager.h"
#include "FearGrid.h"
#include "map.h"
#include "world.h"
#include "Line.h"
#include "game.h"
#include "round.h"
#include "player.h"
#include "shell.h"
#include "engine.h"

#define WEAPON_BURST_EVENT	5001	// a burst's next round (CWeapon::HandleEvent 00506937); the name is ours

//******************************************************************************************
CWeapon::CWeapon(CWeaponData *data, SINT type)	// FUNCTION: 0x00505e00
	: mTargetTypes(type)
{
	mNextFireTime = -200.0f;
	mCharge = 0;
	mUnknown68 = 0;
	mUnknown70 = -1;
	mUnknown74 = -1;
	mBurstCount = 0;
	mUnknown7C = 0;
	mUnknown78 = 0;
	mData = data;
	FMatrix ori;
	ori.MakeRotation(0, 0, 0);
	mOrientation = ori;
	mUnknown84 = FVector(-1.0f, -1.0f, -1.0f);
	mUnknown80 = 0;
	mUnknown94 = 0;
	mUnknown98 = 0;
	mMode = GetWeaponMode(mData->mChargeModes[mUnknown68]);
	mActive = TRUE;
}
// FUNCTION: 0x00505f70 ??_GCWeapon@@UAEPAXI@Z

// FUNCTION: 0x00505f90 ??1CWeapon@@UAE@XZ

//******************************************************************************************
// CEquipment's accessor, defined here, its one user: an inline body in the shared header moves an unrelated
// scheduling tie-break (CPDTimeline 004c2640 in ParticleDescriptor.cpp loses its match).
inline CUnit *CEquipment::GetAttachedTo() { return mAttachedTo; }

//******************************************************************************************
// The two IsA tests read the unit through GetAttachedTo(): two offered sites after the GetWeaponMode walks, which is
// what VC6's budget needs to call GenericSPtrSet::First out of line in both walks as retail does (each walk's body
// is inlined with what is left divided by the sites still to come).
BOOL CWeapon::Fire()	// FUNCTION: 0x00506010
{
	mUnknown68 = (SINT)mCharge / 100;
	mCharge = 0;
	mMode = GetWeaponMode(mData->mChargeModes[mUnknown68]);
	while (!mMode)
	{
		mUnknown68--;
		if (mUnknown68 < 0)
			return FALSE;
		mMode = GetWeaponMode(mData->mChargeModes[mUnknown68]);
	}
	if (!ReadyToFire())
		return FALSE;
	if (!mMode || !mMode->mRound)
		return FALSE;
	mNextFireTime = EVENT_MANAGER.GetTime() + mMode->mReloadTime;
	mBurstCount = 0;
	if (GetAttachedTo()->IsA(THING_TYPE_BATTLE_ENGINE))
		((CBattleEngine *)mAttachedTo)->ResetTarget();
	if (SpawnRound())
	{
		if (mMode->mSoundA && mMode->mSoundPerBurst)
		{
			if (GetAttachedTo()->IsA(THING_TYPE_BATTLE_ENGINE))
				SOUND.PlayEffect(mMode->mSoundA, NULL, 0.5f, ST_NOTRACKING);
			else
				SOUND.PlayEffect(mMode->mSoundA, mAttachedTo, 0.5f, ST_FOLLOWDONTDIE);
		}
	}
	if (mMode->mBurstSize > 0)
	{
		mBurstCount = 1;
		EVENT_MANAGER.AddEvent(WEAPON_BURST_EVENT, this, EVENT_MANAGER.GetTime() + mMode->mBurstDelay);
	}
	return TRUE;
}

//******************************************************************************************
// Admission prefix 005061f0..0050625b matches; the inlined mode lookup remains unmatched.
BOOL CWeapon::CanLock(CUnit *target)	// FUNCTION: 0x005061f0
{
	CUnit *unit = target;
	if (!unit->Active())
		return FALSE;
	if ((!unit->mUnitData || !unit->mUnitData->mUnknown12C) && unit->HasNexus())
		return FALSE;
	if (unit->HasWeakPoint())
		return FALSE;
	if (unit->mUnitData && !unit->mUnitData->mCanBeLocked)
		return FALSE;
	return (GetCurrentMode()->mLockUnit & target->GetThingType()) != 0;
}

//******************************************************************************************
int CWeapon::GetMaxLocks()	// FUNCTION: 0x00506350
{
	CWeaponModeData *mode = GetCurrentMode();
	if (mode)
		return mode->mMaxLocks;
	return 0;
}

//******************************************************************************************
float CWeapon::GetLockTime()	// FUNCTION: 0x00506440
{
	CWeaponModeData *mode = GetCurrentMode();
	if (mode)
		return mode->mLockTime;
	return 0.0f;
}

//******************************************************************************************
ELockMode CWeapon::GetLockMode()	// FUNCTION: 0x00506530
{
	CWeaponModeData *mode = GetCurrentMode();
	if (mode)
		return (ELockMode)mode->mLockMode;
	return kDirectLockMode;
}

//******************************************************************************************
float CWeapon::GetMaxDeflection()	// FUNCTION: 0x00506620
{
	CWeaponModeData *mode = GetCurrentMode();
	if (mode)
		return mode->mLockDeflection;
	return 0.0f;
}

//******************************************************************************************
float CWeapon::GetLockRange()	// FUNCTION: 0x00506710
{
	CWeaponModeData *mode = GetCurrentMode();
	if (mode)
		return mode->mLockRange;
	return 0.0f;
}

//******************************************************************************************
float CWeapon::GetLockRadius()	// FUNCTION: 0x00506800
{
	CWeaponModeData *mode = GetCurrentMode();
	if (mode)
		return mode->mLockRadius;
	return 0.0f;
}

//******************************************************************************************
void CWeapon::Charge()	// FUNCTION: 0x005068f0
{
	if (CanCharge())
	{
		if (mCharge < 400.0f)
			mCharge += mData->mChargeRate;
	}
}

//******************************************************************************************
void CWeapon::HandleEvent(CEvent *event)	// FUNCTION: 0x00506930
{
	if (event->GetEventNum() == WEAPON_BURST_EVENT)
	{
		if (!mAttachedTo->IsDying())
		{
			CWeaponModeData *mode = GetWeaponMode(mData->mChargeModes[mUnknown68]);
			if (mode && mBurstCount < mode->mBurstSize)
			{
				SpawnRound();
				mBurstCount++;
				EVENT_MANAGER.AddEvent(WEAPON_BURST_EVENT, this, EVENT_MANAGER.GetTime() + mode->mBurstDelay);
			}
		}
	}
}

// STAND-IN source interface: Aim's consumed mode/round readers. The borrowed deflection fields
// keep native's memory operands at 00509286/005092c2; returning float introduces an
// extra x87 value and discard. Names and original interface ownership are unknown.
static inline CWeaponModeData *WeaponModeOf(const CWeapon *weapon) { return weapon->mMode; }
static inline const float &WeaponMode_mMaxDeflection(const CWeaponModeData *mode) { return mode->mMaxDeflection; }
static inline const float &WeaponMode_mMinDeflection(const CWeaponModeData *mode) { return mode->mMinDeflection; }
static inline float WeaponRound_mGravity(const CRoundData *data) { return data->mGravity; }
static inline float WeaponRound_mSpeed(const CRoundData *data) { return data->mSpeed; }
// The same interface's fear-grid flag reader, added for Unknown5088b0 (see there; same stand-in terms).
static inline SINT WeaponRound_mUnknown58(const CRoundData *data) { return data->mUnknown58; }
// The same interface's yaw-tolerance reader, by value, added for Unknown507ab0 (see there; same stand-in terms).
static inline float WeaponMode_mYawTolerance(const CWeaponModeData *mode) { return mode->mYawTolerance; }
// The same interface's descriptor and sound readers, added for SpawnRound (see there; same stand-in terms).
static inline CParticleDescriptor *WeaponMode_mParticleA(const CWeaponModeData *data) { return data->mParticleA; }
static inline CEffect *WeaponMode_mSoundA(const CWeaponModeData *data) { return data->mSoundA; }

// STAND-IN source interface: each relative limit is velocity's elevation plus the mode's
// deflection. These nested operations retain native's Magnitude calls at 0050925b
// and 00509297; the mode is read before each call (00509248/00509289). Original
// helper names/ownership are unknown. The complete Aim section and all refs match.
static inline float WeaponRelativeMinPitch(const CWeaponModeData *mode, const FVector &velocity)
{
    return velocity.Elevation() + WeaponMode_mMinDeflection(mode);
}
static inline float WeaponRelativeMaxPitch(const CWeaponModeData *mode, const FVector &velocity)
{
    return velocity.Elevation() + WeaponMode_mMaxDeflection(mode);
}

// STAND-IN source interface: the actual final aim-request operations (0050945f..00509495):
// mark the request, copy its 16-byte position, attach the reader. Grouping them
// keeps both MakeRotationF calls out of line. Original helper ownership is unknown.
static inline void WeaponPublishAimRequest(CWeapon *weapon, const FVector &position, CThing *target)
{
    weapon->mUnknown80 = 1;
    weapon->mUnknown84 = position;
    weapon->mTarget = target;
}

// A mode's round through a file-static accessor (name and signature ours; original ownership unknown).
// GetActualMaxRange already uses this accessor: its out-of-line First and Next in GetCurrentMode and the
// loop's GetWeaponMode (00509d60), come out of the inline-budget simulator only with five or six offered sites after
// GetCurrentMode, and the tail reads mode->mRound six times. The condition before GetCurrentMode keeps the direct
// reads: sites ahead of it do not change the call order.
static inline CRoundData *RoundOf(CWeaponModeData *mode)
{
	return mode->mRound;
}

//******************************************************************************************
// The launch-angle lookup MoveEmitter inlines, declared here rather than in WorldPhysicsManager.h: one more
// declaration there flips CGame::DrawDebugStuff (00470650) through the declaration-count tie-break.
class CWeaponModeAngles : public CWeaponModeData
{
public:
	CLaunchAngle	*GetLaunchAngle(SINT index);
};

inline CLaunchAngle *CWeaponModeAngles::GetLaunchAngle(SINT index)
{
	SINT i = 0;
	ListIterator<void> it(&mSet5c);
	for (CLaunchAngle *angle = (CLaunchAngle *)it.First(); angle; angle = (CLaunchAngle *)it.Next())
	{
		if (i == index)
			return angle;
		i++;
	}
	return NULL;
}

//******************************************************************************************
// The next launch position: the launch ordinal advanced and wrapped, then looked up in the mode's sequence (the
// reconstruction). Name and ownership ours.
static inline SINT WeaponAdvanceLaunchIndex(CWeapon *weapon)
{
	weapon->mUnknown70++;
	if (weapon->mMode)
	{
		if (weapon->mUnknown70 >= weapon->mMode->mSet4c.Size())
			weapon->mUnknown70 = 0;
		return weapon->mMode->GetLaunchSequence(weapon->mUnknown70);
	}
	return 1;
}

// STAND-IN (product rank keys): SpawnRound's two orientation products through file-local copies
// of maths.h's FMatrix::operator* (same body, same (t0 + t1) + t2 sums), with a (double) conversion on one factor of
// seven products: FSpawnOri's C21, C11 and C01 term 0; FSpawnTurn's C22 terms 1 and 2, C20 term 0 and C10 term 2 (row
// i, column j; term k is Row[i] component k times m.Row[k]). VC6 evaluates an element's three products in rank-key
// order (the rank tool). A converted product's factors become conversion temporaries (the rank tool: `mul tNNN tNNN`),
// so its key comes from those conversions instead of the field symbols' ids and the product moves within its element
// (measured one at a time: a cast on C21's, C11's, C02's or C01's first product or on C'20's first or C'10's last fixes
// that element alone); together the temporaries also move the later symbol ids (the launch angle's fields from 0x50f to
// 0x531, the orientation's this-alias fields from 0x567 to 0x589), which is why C'22 needs two. On the pinned compiler
// the conversions add no instruction: its x87 code multiplies and adds in register precision either way. They change
// only the evaluation order, and in portable C++ they make those products double, so no precision equivalence across
// compilers is claimed; the pinned bytes and relocations are the evidence. Unlike a bracket the conversion adds no
// scheduler node: brackets (0x162 nodes) moved this block's 81-node region cuts, which in an earlier bracketed form
// scheduled the C'21 argument push and the last row's `lea ecx` early (at 0050712e and 005072a7; retail 0050713e and
// 005072bc). Retail's orders come from symbol ids nobody has reproduced (measured: the launch angle's and the
// second product's fields about 43 ids higher, the orientation's at about 0x57c in another creation order). Original
// spelling unknown. A compiler-specific stand-in: orientation and the intermediate product are FMatrix objects read
// through the derived FSpawnOri/FSpawnTurn by static_cast, which is not portable C++ (no dynamic-type guarantee); the
// views add no members or virtuals and emit no out-of-line methods, and VC6 compiles their reads as reads of the
// FMatrix.
struct FSpawnOri : public FMatrix
{
	FMatrix operator*(const FMatrix &m) const
	{
		return FMatrix(
			FVector(Row[0].X * m.Row[0].X + Row[0].Y * m.Row[1].X + Row[0].Z * m.Row[2].X,
				(double)Row[0].X * m.Row[0].Y + Row[0].Y * m.Row[1].Y + Row[0].Z * m.Row[2].Y,
				Row[0].X * m.Row[0].Z + Row[0].Y * m.Row[1].Z + Row[0].Z * m.Row[2].Z),
			FVector(Row[1].X * m.Row[0].X + Row[1].Y * m.Row[1].X + Row[1].Z * m.Row[2].X,
				(double)Row[1].X * m.Row[0].Y + Row[1].Y * m.Row[1].Y + Row[1].Z * m.Row[2].Y,
				Row[1].X * m.Row[0].Z + Row[1].Y * m.Row[1].Z + Row[1].Z * m.Row[2].Z),
			FVector(Row[2].X * m.Row[0].X + Row[2].Y * m.Row[1].X + Row[2].Z * m.Row[2].X,
				(double)Row[2].X * m.Row[0].Y + Row[2].Y * m.Row[1].Y + Row[2].Z * m.Row[2].Y,
				Row[2].X * m.Row[0].Z + Row[2].Y * m.Row[1].Z + Row[2].Z * m.Row[2].Z));
	}
};

struct FSpawnTurn : public FMatrix
{
	FMatrix operator*(const FMatrix &m) const
	{
		return FMatrix(
			FVector(Row[0].X * m.Row[0].X + Row[0].Y * m.Row[1].X + Row[0].Z * m.Row[2].X,
				Row[0].X * m.Row[0].Y + Row[0].Y * m.Row[1].Y + Row[0].Z * m.Row[2].Y,
				Row[0].X * m.Row[0].Z + Row[0].Y * m.Row[1].Z + Row[0].Z * m.Row[2].Z),
			FVector(Row[1].X * m.Row[0].X + Row[1].Y * m.Row[1].X + (double)Row[1].Z * m.Row[2].X,
				Row[1].X * m.Row[0].Y + Row[1].Y * m.Row[1].Y + Row[1].Z * m.Row[2].Y,
				Row[1].X * m.Row[0].Z + Row[1].Y * m.Row[1].Z + Row[1].Z * m.Row[2].Z),
			FVector((double)Row[2].X * m.Row[0].X + Row[2].Y * m.Row[1].X + Row[2].Z * m.Row[2].X,
				Row[2].X * m.Row[0].Y + Row[2].Y * m.Row[1].Y + Row[2].Z * m.Row[2].Y,
				Row[2].X * m.Row[0].Z + (double)Row[2].Y * m.Row[1].Z + (double)Row[2].Z * m.Row[2].Z));
	}
};

//******************************************************************************************
// Matched with labelled stand-ins (the stand-ins are listed at the end).
// Retail's 53 out-of-line calls in retail's order come from these spellings: MoveEmitter's GetLaunchAngle
// for the launch angle (its First out of line, 00506d08), the launch-index helper (GetLaunchSequence out of line,
// 00506b75), the velocity as init.mOrientation times a named launch velocity (one constructor call, 00507327), the
// flak delta written in place (operator- expands and calls its constructor, 005073f1), the unit read through
// GetAttachedTo() and the round through the file's RoundOf accessor (free inline sites that hold the tracking
// Elevation's Magnitude, 00506c6a, and the first product's row constructors, 00506fac, out of line), the speed as a
// ternary, the BattleEngine test as one condition and the Fenrir flag in place (own size).
// The particle block: a loop that only finds a free emitter, with a found flag, then the creation or the fallback
// (retail tests the slots at 005074f7..00507503, jumps from a free slot straight to the creation, 005074fa, and falls
// from the loop's end into the fallback, which constructs its link at 00507511; `if (effect < 2)` re-tests the index
// on the break edge), and the fallback's two CreateEffect calls in an if/else that VC6 cross-jumps (00507528 test,
// push 1 / push 0). The flak error starts as the inaccuracy and is scaled by the distance (0050747a); its fraction is a
// statement of its own, which stores the error as retail does (00507481).
// Registers and orders, each from a measured C2 rule (chooser log from a regalloc-style logger
// and rank IR dumps):
// - The round's data passed to UPhysicsManager::SpawnRound through RoundOf(mode): the accessor's result is a register
//   candidate, not a scratch pick, so the scratch rotation stays at eax and the constructor's FVector(-1, -1, -1) copy
//   loads X, Y, Z, Pad into eax, ecx, edx, eax as retail (00506adc..00506b37; its scheduling follows the registers).
// - The tracking test through CWeapon::Gravity(), weapon.h's inline: its mode local is a candidate, so mMode's load is
//   the next scratch pick and takes eax (00506be8).
// - GetLaunchAngle declares its counter before the iterator: the counter's xor and the iterator's store tie on
//   scheduler priority and the earlier IL node goes first (xor esi before the store, 00506cff).
// - The flak distance as delta.Magnitude(): a member call, so delta's Y and Z are this-alias fields numbered by the CSE
//   pass, and the bracketed squares sum as retail does, (Y*Y + Z*Z) + X*X (005073f6..0050742f).
// - The found branch names the unit: the Fenrir test's unit is a candidate, not a scratch pick, which leaves the
//   rotation where retail's is for the arguments (0050753c..005075c4); the fallback reads mAttachedTo directly for its
//   IsA test, a scratch pick that takes ecx, so the type test folds into `test [ecx+0x34]` (00507516..00507528; through
//   GetAttachedTo() the accessor's result is a candidate in eax and the byte is loaded into cl).
// - The shell names the unit before reading its velocity: the GetAttachedTo site frees one more parameter symbol,
//   so operator+='s binding gives unitVelocity.X a recycled id below velocity.X's, and the X sum loads the velocity
//   first as retail does (0050782a; Y and Z keep the unit's velocity first).
// STAND-IN (reader interface, as in Aim; measured): the sound condition reads mSoundA through
// WeaponMode_mSoundA(WeaponModeOf(this)) and both PlayEffect arms through WeaponMode_mSoundA(mode), and the fallback's
// CreateEffect arms read the descriptor through WeaponMode_mParticleA(mode). Each reader is a read of the real field and
// emits no code of its own. With them the sound block tests before pushing its arguments (00506a50), the fallback's
// identical heads hoist the descriptor load above the IsA test (0050752c), and the inline budget keeps the tracking
// Elevation's Magnitude and the first product's row constructors out of line. Without the sound readers 22/118
// (shape/instructions), without the descriptor readers 38/644 (the frame becomes 0xa74).
// STAND-IN (reader interface): the particle test and the found branch's CreateEffect read the descriptor
// through the same reader too: two more free inline sites, which keep the tracking Elevation's Magnitude out of line
// once the six named locals below have grown the function's own size (without them it is expanded, 620 instructions
// differ; an earlier form used two identity helpers, SameV and SameM, for the same sites).
// Named locals player, locked, light, thing, clip and mask (natural: Stuart names call results, game.cpp:755; the set
// chosen by measurement, symbol-id levers). Each adds one symbol at import. The first product's left operand
// is a member call on the local orientation, whose fields are C1XX's unnamed this-alias fields, numbered by the CSE
// pass after the field pool's other symbols: with three locals or fewer they land past the CSE temporaries (ids above
// 0x800, which hash small) and every product loads the launch angle first; with four or five the products can be
// ordered but the flak delta's squares reorder (005073f6..00507421) or C'22 cannot be; with six everything matches.
// STAND-IN (product rank keys): the orientation products through FSpawnOri and FSpawnTurn above, whose seven
// (double) conversions give retail's term orders (00506ed1..005070d3, 005070e0..005072d5) without scheduler nodes.
BOOL CWeapon::SpawnRound()	// FUNCTION: 0x005069f0
{
	BOOL fired = FALSE;
	if (mAttachedTo->IsA(THING_TYPE_BATTLE_ENGINE) && !((CBattleEngine *)mAttachedTo)->WeaponFired(this))
		return FALSE;

	CWeaponModeData *mode = mMode;
	if (mode)
	{
		if (WeaponMode_mSoundA(WeaponModeOf(this)) && !mode->mSoundPerBurst)
		{
			if (mAttachedTo->IsA(THING_TYPE_BATTLE_ENGINE))
				SOUND.PlayEffect(WeaponMode_mSoundA(mode), NULL, 0.5f, ST_NOTRACKING);
			else
				SOUND.PlayEffect(WeaponMode_mSoundA(mode), mAttachedTo, 0.5f, ST_FOLLOWDONTDIE);
		}
		for (SINT shot = 0; shot < mode->mVolleySize; shot++)
		{
			CRound *round = UPhysicsManager::SpawnRound(RoundOf(mode));
			if (!round)
				continue;

			CRoundInitThing init;
			SINT launchIndex = WeaponAdvanceLaunchIndex(this);
			mUnknown74++;
			SINT angleIndex;
			if (mMode)
			{
				if (mUnknown74 >= mMode->mSet5c.Size())
					mUnknown74 = 0;
				angleIndex = mUnknown74;
			}
			else
				angleIndex = 0;

			FVector pos;
			FMatrix orientation;
			mAttachedTo->GetLaunchPosition(this, launchIndex, pos, orientation, TRUE);
			if (mUnknown80)
			{
				if (mode->mTrack && !Gravity() && mTarget.ToRead())
				{
					FVector centre;
					mTarget->GetCentrePos(centre);
					FVector delta = centre - pos;
					mOrientation = FMatrix(delta.Azimuth(), delta.Elevation(), 0);
				}
				orientation = mOrientation;
			}

			CLaunchAngle *angle = ((CWeaponModeAngles *)mode)->GetLaunchAngle(angleIndex);
			FMatrix launchAngle;
			launchAngle.MakeRotation(0, 1, 0);
			if (angle)
				launchAngle = FMatrix(angle->mA, angle->mB, 0);
			FVector launchVelocity(0, 0, 0);
			launchVelocity.Y = round->mData->mBeam ? GetMaxRange(FVector(0, 0, 0)) : round->mData->mSpeed / 20.0f;
			float pitchError = (GAME.FloatRandom() * 2 - 1) * mode->mInaccuracy;
			float yawError = (GAME.FloatRandom() * 2 - 1) * mode->mInaccuracy;
			FMatrix inaccuracy(yawError, pitchError, 0);

			init.mPos = pos + GetAttachedTo()->GetVelocity();
			init.mDest = mUnknown84;
			// STAND-IN (product rank keys): orientation * launchAngle * inaccuracy through FSpawnOri and FSpawnTurn.
			init.mOrientation = static_cast<const FSpawnTurn &>(static_cast<const FSpawnOri &>(orientation) * launchAngle) * inaccuracy;
			init.mVelocity = init.mOrientation * launchVelocity;
			init.mOrientationType = DIRECTION_COSINE_MATRIX;
			init.mAllegiance = (EAllegiance)GetAttachedTo()->GetAllegiance();
			init.mRoundData = RoundOf(mode);
			init.mLifeSpan = RoundOf(mode)->mLifeSpan;
			CUnit *target = GetAttachedTo()->GetCurrentTarget();
			round->mUnknownEC = GetAttachedTo();
			if (round->mData->mFlak && target)
			{
				// Retail 005073c9..00507433 materializes target-minus-origin as three floats, then sums
				// (Y*Y + Z*Z) + X*X (Magnitude's bracketed squares in rank order, see above).
				// Finite threshold cases agree; NaN payload behavior is unproved.
				FVector delta = target->GetPos() - pos;
				float distance = delta.Magnitude();
				float roundSpeed = round->mData->mSpeed;
				if (roundSpeed * init.mLifeSpan > distance)
				{
					float percent = GAME.FloatRandom() * 100.0f;
					float error = round->mData->mFlakInaccuracy;
					error *= distance;
					float fraction = percent / 100.0f;
					init.mLifeSpan = ((fraction * error) * 2 - error + distance) / roundSpeed;
				}
			}
			if (GetAttachedTo()->IsA(THING_TYPE_BATTLE_ENGINE))
			{
				CBattleEngine *battleEngine = (CBattleEngine *)GetAttachedTo();
				CPlayer *player = battleEngine->GetPlayer();	// named (symbol ids, see above): player and locked
				player->IncStat(PS_ROUNDSFIRED);
				BOOL locked = battleEngine->DisplayLock(this);
				if (locked)
					battleEngine->FireLock(target);
			}
			round->SetTarget(target, FALSE);
			round->Init(&init);

			if (WeaponMode_mParticleA(mode))	// STAND-IN (reader interface): here and the found branch's CreateEffect
			{
				SINT effect;
				BOOL found = FALSE;
				for (effect = 0; effect < 2; effect++)
					if (!mEffects[effect].Full())
					{
						found = TRUE;
						break;
					}
				if (found)
				{
					CUnit *unit = GetAttachedTo();
					PARTICLE_MANAGER.CreateEffect(WeaponMode_mParticleA(mode), &mEffects[effect], ZERO_FVECTOR, 0,
						unit->mUnitData ? unit->mUnitData->mFenrir : TRUE);
					mEffects[effect].SetPos(pos);
					mEffects[effect].SetOrientation(init.mOrientation);
					mEmitters[effect] = launchIndex;
				}
				else
				{
					CParticleEffectLink link;
					if (mAttachedTo->IsA(THING_TYPE_BATTLE_ENGINE))
						PARTICLE_MANAGER.CreateEffect(WeaponMode_mParticleA(mode), &link, ZERO_FVECTOR, 0, TRUE);
					else
						PARTICLE_MANAGER.CreateEffect(WeaponMode_mParticleA(mode), &link, ZERO_FVECTOR, 0, FALSE);
					if (link.Full())
					{
						link.SetPos(pos);
						link.SetOrientation(init.mOrientation);
					}
				}
				if (mode->mMuzzleLight != -1)
				{
					eLightType light = (eLightType)mode->mMuzzleLight;	// named (symbol ids, see above): light
					ENGINE.LogLight(pos, light, mode->mMuzzleLightRadius);
				}
			}
			if (mode->mClipName)
			{
				CThing *thing = SpawnThing(21, FALSE);	// named (symbol ids, see above): thing, clip and mask
				CShell *shell = (CShell *)thing;
				if (shell)
				{
					CInitThing shellInit;
					shellInit.mPos = pos;
					char *clip = mode->mClipName;
					shell->Unknown4df530(clip);
					shell->Init(&shellInit);
					SINT randomX = GAME.Random();
					SINT randomY = GAME.Random();
					SINT randomZ = GAME.Random();
					SINT mask = 400;
					FVector velocity((randomX & mask) / 10000.0f - 0.02f + 0.05f,
						(randomY & mask) / 10000.0f - 0.02f + 0.05f,
						(randomZ & mask) / 10000.0f - 0.02f - 0.1f);
					velocity = GetPrimaryOrientation() * velocity;
					CUnit *unit = GetAttachedTo();
					FVector unitVelocity = unit->GetVelocity();
					velocity += unitVelocity;
					shell->SetVelocity(velocity);
				}
			}
			if (GetAttachedTo()->IsA(THING_TYPE_BATTLE_ENGINE))
				((CBattleEngine *)GetAttachedTo())->RecoilWeapon(this);
			fired = TRUE;
		}
	}
	return fired;
}

//******************************************************************************************
SINT CWeaponModeData::GetLaunchSequence(SINT index)	// FUNCTION: 0x005078b0
{
	ListIterator<void> it(&mSet4c);
	SINT i = 0;
	for (SINT *value = (SINT *)it.First(); value; value = (SINT *)it.Next())
	{
		if (i == index)
			return *value;
		i++;
	}
	return 0;
}

//******************************************************************************************
void CWeapon::MoveEmitter(BOOL orientate)	// FUNCTION: 0x005078f0
{
	CWeaponModeData *mode = mMode;
	if (mode)
	{
		for (int i = 0; i < 2; i++)
		{
			CParticleEffectLink *effect = &mEffects[i];
			if (effect->mEffect)
			{
				FVector pos;
				FMatrix ori;
				mAttachedTo->GetLaunchPosition(this, mEmitters[i], pos, ori);
				effect->SetPos(pos);
				if (orientate)
				{
					CLaunchAngle *angle = ((CWeaponModeAngles *)mode)->GetLaunchAngle(i);
					FMatrix m;
					m.MakeRotation(0, 1, 0);
					if (angle)
						m = FMatrix(angle->mA, angle->mB, 0);
					effect->SetOrientation(ori * m);
				}
			}
		}
	}
}

//******************************************************************************************
// STAND-IN (scheduling regions; name ours): FVector's operator*(float) with each product bracketed, three IL tuples
// more than maths.h's operator and no code. Used for Unknown507ab0's line ends (see there).
static inline FVector ScaledColumn(const FVector &v, float f)
{
	return FVector((v.X * f), (v.Y * f), (v.Z * f));
}

//******************************************************************************************
// An angle difference brought into -PI..PI (name ours).
static inline float WrapAngle(float a)
{
	if (a < -PI)
		a += PI_M2;
	else if (a > PI)
		a -= PI_M2;
	return a;
}

//******************************************************************************************
// The turret's orientation: its yaw (mUnknownE0) turned into the unit's frame. Name and ownership are ours;
// SentinelAI.cpp has the same body (GetSentinelTurretOrientation), so the original was probably one CUnit inline
// that both used. Inlined with a small nested budget, it leaves the constructor's pieces and the product out of
// line, as retail does here in Unknown5088b0 (iterator 00508e9d, MakeRotationF 00508eb2, operator* 00508eca) and
// in CSentinelAI::Unknown3 (004df1dd..004df201).
static inline FMatrix GetTurretOrientation(CUnit *unit)
{
	return FMatrix(unit->mUnknownE0, 0, 0) * unit->GetOrientation();
}

//******************************************************************************************
// Whether the weapon can fire at the target (CUnit::CanFireAt): below the sea's surface, clear of the fear grid,
// the target within the yaw tolerance; then a lobbed round's range (at the barrel's pitch, or across the mode's
// deflections, with its two limiting lines clear), a torpedo's water, or a straight round's deflection band, and
// when asked, a clear line to the target or a friend of its side in the way.
// Matched, with labelled stand-ins.
// Spellings that reproduce retail's code: the velocity as GetPrimaryOrientation() * FVector(0, speed, 0) (retail calls
// FVector with the product's folded arguments, 00507baf; the matched Aim writes it so); the turret getter at both
// products (its constructor and operator* called, 00507bdb/00507be2); the yaw and pitch wraps written out or through
// WrapAngle, since retail expands AngleDifference (00507d22..00507d84); the deflection part as the else of the
// barrel-pitch branch, both falling to one return FALSE, so neither final test becomes a select (00508003, 00508615);
// the swap through rangeA, storing the upper range first, and the torpedo test with the height in a local, both as
// Unknown5088b0 writes them (0050819f, 00508647); the current orientation read through mAttachedTo (through
// GetAttachedTo() the accessor's result is a register candidate and takes eax where retail's chooser gives ecx,
// 005082f6); the final test's ignored unit cleared after the building test and set by an if (retail clears it at
// 00508826, after GetThingType's eax, and branches at 0050882e, where a ternary becomes VC6's neg/sbb/and select);
// the two line tests and the final test in blocks of their own, the final block holding the last return FALSE
// (retail's frame 0x1b4; 0x224 without them: C2 gives every call that may throw an extra use of the address-taken
// locals of its scope and the scopes around it).
// The ranges (IR rank dumps, x87 logger
// output):
// - Each range calls sinf inside the root's argument. An inline call in an argument makes the inliner bind sqrtf's
//   parameter as a variable, so the discriminant is an x87 web of its own, and the calls after it in the statement
//   are expanded before the statement: the vertical term's sinf returns a copy of the CSE'd sine that is born between
//   the discriminant's definition and its use. The x87 colouring test (C2 0x1076590f) refuses the discriminant for
//   that copy, so it is stored and read back at once, `fst`, as retail does (00508132, 005081d8, 00508214). The
//   square's second sinf is the same kind of copy, made before the 2 * gravity * height CSE definition: retail's
//   fld st(0) right after fsin (00508114, 005081ba).
// - The two last ranges (upper in the same-quadrant branch, C in the other) name the root first and take the sine
//   after it: the named root is a leaf, so the product evaluates the cosine first (retail's order, 00508174/00508258),
//   and the sine's copy made after the root's definition ends the CSE'd sine there, below the root and before its
//   last use, so the colouring test refuses the root: retail's fstp, read back by fsubr after the cosine (00508170 and
//   00508180, 00508254 and 00508266).
// - The x87 webs' stores add the references that give retail's slot classes and their order (frame 0x1b4, rangeA's
//   class holding both same-quadrant intermediates and the swap, 00508132/00508170/0050819f).
// STAND-IN (spelling chosen by measurement): the square's second sinf in brackets, `sinf(a) * (sinf(a))`. Retail
// consumes the copy in place (fmul st,st(1) after fld st(0)) and keeps the sine for the vertical term. VC6 orders the
// product's operands by rank key (height, size, hash); without the bracket the CSE'd sine (a temporary whose hash is
// its id << 6, about 0xd240 here) ranks before the copy (a named inline symbol, id << 5, about 0x9040), and ours loads
// the sine again (fld st(1)) and pops the dead copy later. The bracket's tuple raises the copy's key. A late zero,
// `(sinf(a) + zero)`, does the same. Without the brackets: 27/27 shape/instructions (one row at a time 9/9 to 11/11).
// STAND-IN (scheduling regions): the three line ends scale the column through ScaledColumn, FVector's operator*(float)
// with each product bracketed (three more IL tuples per line end, no code). VC6 schedules a block in regions of at
// most 81 tuples (C2 0x10737a43). With the brackets the barrel-pitch block's first region ends with the line copy's
// argument, which then follows the report's stores (`push edx`, 00507fe5), and the deflection block's first region
// ends after the high line's second load, so GetPrimaryPosition's setup sits between that product's fmul and fstp
// (00508450). With maths.h's unbracketed operator the barrel-pitch block fits one region and the deflection block
// cuts after its third fmul (both placements then differ). Without ScaledColumn: 14/24 (only the barrel-pitch line's
// 8/16, only the low or the high line's 12/12).
// Bracketing the operator in maths.h gives the same code here but costs CWeapon::ComputeTargetAimPoint (0050a0e0) its
// match.
// STAND-IN (register allocation): in the final test, `if (target) attached = GetAttachedTo();` assigns the value the
// variable already holds. CSE turns the arm into a self-copy; it reaches the register allocator as its own block, test
// included, and is deleted afterwards with its branch and compare (no code, nothing changes at run time). Its test and
// block bring meshCollision's and the ignored unit's priorities to 24 each (28 and 26 without it; C2's keys, measured
// with a contribution tool), and the ignored unit, coloured first, takes eax and meshCollision ecx as retail
// (005087ef..00508830); without the arm they swap (6/36). Tests of target worked (target, !target, target != NULL);
// tests of a, this, mMode, mAttachedTo or the unit did not.
// STAND-IN (reader interface, as in Aim and Unknown5088b0): the fear-grid flag, the speed, the gravity, the minimum
// deflection at angleA and the torpedo flag read through Aim's file-local readers, a by-value WeaponMode_mYawTolerance
// at both tolerance tests (retail copies the tolerance before each compare, 00507db8, 00508700) and WrapAngle at the
// pitch test. Why: the inline budget. Retail calls the four early nested FVector constructors (00507baf, 00507c18,
// 00507cd2, 0050806f) and expands the barrel-pitch line's (its FMatrix's vector iterator and the three FVectors at
// 00507eab..00507f17); the readers are free sites that set those nested budgets (the inline logger). Each reader
// is a read of the real field and emits no code of its own. Without them: 84/458, 38 calls. Dropping any one of them
// (7/11 to 21/123) or adding the maximum deflection's or the band test's (44 calls, 51/223) loses the match; the
// launch speed's reader is neutral and left out. The yaw test writes its wrap out (WrapAngle there: 44 calls, 53/225).
// A CWeapon::GetYawTolerance() member in weapon.h gives the same code but costs CD3DApplication::BuildDeviceList
// (00529350) its match.
BOOL CWeapon::Unknown507ab0(CUnit *target, BOOL a)	// FUNCTION: 0x00507ab0
{
	if (GetPrimaryPosition().Z > MAP.GetWaterLevel())
		return FALSE;
	if (mMode && mMode->mRound && WeaponRound_mUnknown58(RoundOf(WeaponModeOf(this))))	// STAND-IN (readers, see above)
	{
		CFearGrid *grid = NULL;
		if (mAttachedTo->GetAllegiance() == kForsetiAllegiance)
			grid = GAME.GetForsetiFearGrid();
		else if (mAttachedTo->GetAllegiance() == kMuspellAllegiance)
			grid = GAME.GetMuspellFearGrid();
		if (grid && !grid->Unknown44c780(target->GetPos()))
			return FALSE;
	}

	float speed = WeaponRound_mSpeed(RoundOf(WeaponModeOf(this)));	// STAND-IN (readers)
	FVector velocity = GetPrimaryOrientation() * FVector(0, speed, 0);
	float yaw = mUnknown94 ? (GetTurretOrientation(mAttachedTo) * FVector(0, speed, 0)).Azimuth() : velocity.Azimuth();
	float pitch;
	if (mUnknown98)
		pitch = mAttachedTo->mUnknownE8;
	else
		pitch = velocity.Elevation();

	FVector targetPos = GetPredictedTargetPos(target);
	FVector delta = targetPos - GetPrimaryPosition();
	float targetYaw = delta.Azimuth();
	float targetPitch = delta.Elevation();
	float diff = AngleDifference(targetYaw, yaw);
	if (diff < -PI)
		diff += PI_M2;
	else if (diff > PI)
		diff -= PI_M2;
	if (!(fabsf(diff) < WeaponMode_mYawTolerance(WeaponModeOf(this))))	// STAND-IN (readers)
		return FALSE;

	if (mMode->mRound->IsBallistic())
	{
		float height = targetPos.Z - mAttachedTo->GetPos().Z;
		float launchSpeed = mMode->mRound->mSpeed / 20.0f;
		float gravity = WeaponRound_mGravity(RoundOf(WeaponModeOf(this))) * 0.025f;	// STAND-IN (readers)
		float distance = delta.MagnitudeXY();
		if (mUnknown98)
		{
			float horizontal = cosf(pitch) * launchSpeed;
			float vertical = sinf(pitch) * launchSpeed;
			float range = (sqrtf(vertical * vertical + gravity * height * 2) - vertical) / gravity * horizontal;
			if (range < 0)
				range = 0;
			if (!(fabsf(distance - range) < 12.0f))
				return FALSE;

			FMatrix aim(yaw, pitch, 0);
			CLine line(GetPrimaryPosition(), GetPrimaryPosition() + ScaledColumn(aim.GetColumnY(), 3.0f));	// STAND-IN (regions)
			CWorldLineColReport report;
			ECollisionType hit = WORLD.FindFirstThingToHitLine(line, mAttachedTo, &report, TRUE, FALSE, 1, THING_TYPE_AMMUNITION, TRUE, -1);
			if (hit == kCollideNothing)
				return TRUE;
			if (hit == kCollideThing)
				return TRUE;
		}
		else
		{
			float basePitch = (GetTurretOrientation(mAttachedTo) * FVector(0, speed, 0)).Elevation();
			float angleA = basePitch + WeaponMode_mMinDeflection(WeaponModeOf(this));	// STAND-IN (readers)
			float angleB = basePitch + mMode->mMaxDeflection;
			float lowerRange, upperRange, rangeA;
			if ((angleA > -PI / 4 && angleB > -PI / 4) || (angleA < -PI / 4 && angleB < -PI / 4))
			{
				// STAND-IN (spelling): each square's second sinf in brackets (see above)
				lowerRange = (sqrtf(sinf(angleA) * (sinf(angleA)) * launchSpeed * launchSpeed - gravity * height * 2) - sinf(angleA) * launchSpeed) * (float)cos(angleA) * launchSpeed / gravity;
				float rootB = sqrtf(sinf(angleB) * (sinf(angleB)) * launchSpeed * launchSpeed - gravity * height * 2);
				float sineB = sinf(angleB);
				upperRange = (rootB - sineB * launchSpeed) * (float)cos(angleB) * launchSpeed / gravity;
				if (lowerRange > upperRange)
				{
					rangeA = upperRange;
					upperRange = lowerRange;
					lowerRange = rangeA;
				}
			}
			else
			{
				// STAND-IN (spelling): each square's second sinf in brackets (see above)
				rangeA = (sqrtf(sinf(angleA) * (sinf(angleA)) * launchSpeed * launchSpeed - gravity * height * 2) - sinf(angleA) * launchSpeed) * (float)cos(angleA) * launchSpeed / gravity;
				float rangeB = (sqrtf(sinf(angleB) * (sinf(angleB)) * launchSpeed * launchSpeed - gravity * height * 2) - sinf(angleB) * launchSpeed) * (float)cos(angleB) * launchSpeed / gravity;
				float rootC = sqrtf(sinf(-PI / 4) * (sinf(-PI / 4)) * launchSpeed * launchSpeed - gravity * height * 2);
				float sineC = sinf(-PI / 4);
				float rangeC = (rootC - sineC * launchSpeed) * (float)cos(-PI / 4) * launchSpeed / gravity;
				lowerRange = rangeA;
				if (rangeB < lowerRange)
					lowerRange = rangeB;
				if (rangeC < lowerRange)
					lowerRange = rangeC;
				upperRange = rangeA;
				if (rangeB > upperRange)
					upperRange = rangeB;
				if (rangeC > upperRange)
					upperRange = rangeC;
			}
			if (distance > upperRange || distance < lowerRange)
				return FALSE;

			{
				CEulerAngles current = mAttachedTo->GetCurrentOrientation();
				FMatrix low(current.mYaw, angleA, 0);
				FMatrix high(current.mYaw, angleB, 0);
				CLine lowLine(GetPrimaryPosition(), GetPrimaryPosition() + ScaledColumn(low.GetColumnY(), 3.0f));	// STAND-IN (regions)
				CLine highLine(GetPrimaryPosition(), GetPrimaryPosition() + ScaledColumn(high.GetColumnY(), 3.0f));	// STAND-IN (regions)
				BOOL clear = TRUE;
				CWorldLineColReport report;
				ECollisionType hit = WORLD.FindFirstThingToHitLine(lowLine, mAttachedTo, &report, FALSE, FALSE, 1, THING_TYPE_AMMUNITION, TRUE, -1);
				if (hit == kCollideGround || hit == kCollideWater)
					clear = FALSE;
				if (hit == kCollideThing && report.mHitThing && (report.mHitThing->GetThingType() & THING_TYPE_UNIT) &&
					((CUnit *)report.mHitThing)->GetAllegiance() != target->GetAllegiance())
					clear = FALSE;
				hit = WORLD.FindFirstThingToHitLine(highLine, mAttachedTo, &report, FALSE, FALSE, 1, THING_TYPE_AMMUNITION, TRUE, -1);
				if (hit == kCollideGround || hit == kCollideWater)
					clear = FALSE;
				if (hit == kCollideThing && report.mHitThing && (report.mHitThing->GetThingType() & THING_TYPE_UNIT) &&
					((CUnit *)report.mHitThing)->GetAllegiance() != target->GetAllegiance())
					return FALSE;
				if (clear)
					return TRUE;
			}
		}
		return FALSE;
	}

	if (RoundOf(WeaponModeOf(this))->mUnknown6C)	// STAND-IN (readers)
	{
		float height = MAP.Collide(mAttachedTo->GetPos());
		if (MAP.GetWaterLevel() < height)
			return TRUE;
		return FALSE;
	}
	if (mUnknown98)
	{
		float diff = WrapAngle(AngleDifference(targetPitch, pitch));	// STAND-IN (readers: the budget)
		// TEST AH,0x41 at 00508712 (pristine specimen 74154bfa...): ordered equality and unordered pass, where the
		// yaw test's TEST AH,1 at 00507dc8 is strict when ordered. NaN reachability is not established.
		if (fabsf(diff) > WeaponMode_mYawTolerance(WeaponModeOf(this)))	// STAND-IN (readers)
			return FALSE;
	}
	else
	{
		float diff = targetPitch - pitch;
		if (!(diff < mMode->mMinDeflection) || !(diff > mMode->mMaxDeflection))
			return FALSE;
	}
	if (!a)
		return TRUE;

	{
		CLine line(GetPrimaryPosition(), targetPos);
		CWorldLineColReport report;
		BOOL meshCollision = TRUE;
		if (target->GetThingType() & THING_TYPE_BUILDING)
			meshCollision = FALSE;
		CUnit *attached = GetAttachedTo();
		if (target)
			attached = GetAttachedTo();	// STAND-IN (register allocation): the value it already holds (see above)
		CUnit *ignore = NULL;
		if (attached->mUnknown24C)
			ignore = attached;
		if (WORLD.FindFirstThingToHitLine(line, ignore, &report, FALSE, meshCollision, 1, THING_TYPE_AMMUNITION, TRUE, -1) == kCollideThing)
		{
			if (report.mHitThing && (report.mHitThing->GetThingType() & THING_TYPE_UNIT) &&
				((CUnit *)report.mHitThing)->GetAllegiance() == target->GetAllegiance())
				return TRUE;
		}
		return FALSE;
	}
}

//******************************************************************************************
// The second fire test (CUnit::CanFireAt): below the sea's surface, clear of the fear grid, then a lobbed round's
// range across the mode's deflections, a torpedo's water, or the deflection band with the ground's slope, and a
// clear line to the target or a friend of its side in the way.
// Matched, with labelled stand-ins.
// Natural spellings that give retail's inline decisions and calls (own size about 1570): Gravity() for the ballistic
// test (this function expands the predicate, see CRoundData::Unknown40d0f0); sinf, cosf and sqrtf in the five ranges,
// free inline sites that hold the three Elevations' budgets under Magnitude's 60 (calls at 005089dd, 00508a31,
// 00508a72); the turret getter (00508e9d..00508eca out of line); Stuart's yaw idiom from
// CBattleEngine::UpdateRotation, (ori * FVector(0, 1, 0)).Azimuth() and ym * FVector(0, 1, 0), which leaves
// MakeRotationYawF's third row out of line (00508f13); GetAllegiance() as in Unknown507ab0; the water test with no
// local (operand order, 005088e5); the target pitch and the relative pitch as two variables and the swap that stores
// the upper range first (00508c46), which give retail's slot classes.
// STAND-IN 1 (reader interface): the fear-grid flag, the speed, the torpedo flag and mUnknown48 are read through the
// Aim function's file-local readers (WeaponModeOf, RoundOf, WeaponRound_mSpeed and WeaponRound_mUnknown58). Why:
// retail copies the round pointer between registers in the first test (0050890f mov eax,ecx), schedules the speed
// load as the matched Aim does (00508960..00508978) and reloads the round after the ballistic test (00508adf); the
// readers' bound parameters supply those temporaries and the register rotation the later blocks need. How: each is a
// read of a real field; they emit no code of their own. Plain member reads give 35/389 (shape/instructions).
// STAND-IN 2 (pinned-compiler concession, as in Aim): when the cross-quadrant branch's first range is zero, lowerRange
// is read without being set, as retail does: VC6 shares its slot with that branch's vertical, so the read returns the
// third vertical (00508d21, 00508d71..00508d86). An initialized form changes the slot classes. NaN and runtime
// behavior are not established; this is not a portable algorithm for another compiler or the rebuild.
// Reconstruction choice: one scratch variable (rangeA) holds the swap in one branch and the first range in the
// other; the slot packer's replay on our conflict graph reproduces retail's classes only with those two merged.
BOOL CWeapon::Unknown5088b0(CUnit *target)	// FUNCTION: 0x005088b0
{
	if (GetPrimaryPosition().Z > MAP.GetWaterLevel())
		return FALSE;
	if (mMode && mMode->mRound && WeaponRound_mUnknown58(RoundOf(WeaponModeOf(this))))	// STAND-IN 1 (see above)
	{
		CFearGrid *grid = GAME.GetForsetiFearGrid();
		if (mAttachedTo->mAllegiance != kForsetiAllegiance)
			grid = GAME.GetMuspellFearGrid();
		if (grid && !grid->Unknown44c780(target->GetPos()))
			return FALSE;
	}

	float speed = WeaponRound_mSpeed(RoundOf(WeaponModeOf(this)));	// STAND-IN 1
	FVector velocity = GetPrimaryOrientation() * FVector(0, speed, 0);
	FVector targetPos = GetPredictedTargetPos(target);
	FVector delta = targetPos - GetPrimaryPosition();
	float targetPitch = delta.Elevation();
	float pitch, basePitch;
	if (mUnknown98)
	{
		pitch = mAttachedTo->mUnknownE8;
		basePitch = 0;
	}
	else
	{
		pitch = targetPitch - velocity.Elevation();
		basePitch = velocity.Elevation();
	}

	if (Gravity())
	{
		float height = mAttachedTo->GetPos().Z - target->GetPos().Z;
		float launchSpeed = mMode->mRound->mSpeed / 20.0f;
		float gravity = mMode->mRound->mGravity * 0.025f;
		float distance = delta.MagnitudeXY();
		float angleA = basePitch + mMode->mMinDeflection;
		float angleB = basePitch + mMode->mMaxDeflection;
		float lowerRange, upperRange, rangeA;
		if ((angleA > -PI / 4 && angleB > -PI / 4) || (angleA < -PI / 4 && angleB < -PI / 4))
		{
			float vertical = sinf(angleA) * launchSpeed;
			float discriminant = vertical * vertical + 2 * gravity * height;
			if (discriminant != 0)
			{
				lowerRange = (sqrtf(discriminant) - vertical) / gravity * (cosf(angleA) * launchSpeed);
				if (lowerRange < 0)
					lowerRange = 0;
			}
			else
				lowerRange = 0;
			vertical = sinf(angleB) * launchSpeed;
			discriminant = vertical * vertical + 2 * gravity * height;
			if (discriminant != 0)
			{
				upperRange = (sqrtf(discriminant) - vertical) / gravity * (cosf(angleB) * launchSpeed);
				if (upperRange < 0)
					upperRange = 0;
			}
			else
				upperRange = 0;
			if (lowerRange > upperRange)
			{
				rangeA = upperRange;
				upperRange = lowerRange;
				lowerRange = rangeA;
			}
		}
		else
		{
			float rangeB, rangeC;
			float vertical = sinf(angleA) * launchSpeed;
			float discriminant = vertical * vertical + 2 * gravity * height;
			if (discriminant > 0)
			{
				rangeA = (sqrtf(discriminant) - vertical) / gravity * (cosf(angleA) * launchSpeed);
				if (rangeA < 0)
					rangeA = 0;
			}
			else
				rangeA = 0;
			vertical = sinf(angleB) * launchSpeed;
			discriminant = vertical * vertical + 2 * gravity * height;
			if (discriminant > 0)
			{
				rangeB = (sqrtf(discriminant) - vertical) / gravity * (cosf(angleB) * launchSpeed);
				if (rangeB < 0)
					rangeB = 0;
			}
			else
				rangeB = 0;
			vertical = sinf(-PI / 4) * launchSpeed;
			discriminant = vertical * vertical + 2 * gravity * height;
			if (discriminant > 0)
			{
				rangeC = (sqrtf(discriminant) - vertical) / gravity * (cosf(-PI / 4) * launchSpeed);
				if (rangeC < 0)
					rangeC = 0;
			}
			else
				rangeC = 0;
			if (rangeA != 0)	// STAND-IN 2: otherwise lowerRange is read unset (see above)
				lowerRange = rangeA;
			if (rangeB < lowerRange && rangeB != 0)
				lowerRange = rangeB;
			if (rangeC < lowerRange && rangeC != 0)
				lowerRange = rangeC;
			upperRange = rangeA;
			if (rangeB > upperRange)
				upperRange = rangeB;
			if (rangeC > upperRange)
				upperRange = rangeC;
		}
		if (distance > upperRange || distance < lowerRange)
			return FALSE;
		return TRUE;
	}

	if (RoundOf(WeaponModeOf(this))->mUnknown6C)	// STAND-IN 1
	{
		float height = MAP.Collide(mAttachedTo->GetPos());
		if (MAP.GetWaterLevel() < height)
			return TRUE;
		return FALSE;
	}
	if (mUnknown98)
	{
		FMatrix absolute = GetTurretOrientation(mAttachedTo);
		float yaw = (absolute * FVector(0, 1, 0)).Azimuth();
		FMatrix ym;
		ym.MakeRotationYawF(yaw);
		FVector yv = ym * FVector(0, 1, 0);
		FVector normal = MAP.Normal(mAttachedTo->GetPos());
		pitch += ((yv ^ normal) ^ normal).Z;
	}
	if (mMode->mMinDeflection < pitch || mMode->mMaxDeflection > pitch)
		return FALSE;
	if (RoundOf(WeaponModeOf(this))->mUnknown48)	// STAND-IN 1
		return TRUE;

	CLine line(GetPrimaryPosition(), targetPos);
	CWorldLineColReport report;
	BOOL meshCollision = TRUE;
	if (target->GetThingType() & THING_TYPE_BUILDING)
		meshCollision = FALSE;
	if (WORLD.FindFirstThingToHitLine(line, mAttachedTo, &report, FALSE,
		meshCollision, 1, THING_TYPE_AMMUNITION, TRUE, -1) == kCollideThing)
	{
		if (report.mHitThing && (report.mHitThing->GetThingType() & THING_TYPE_UNIT) &&
			((CUnit *)report.mHitThing)->GetAllegiance() == target->GetAllegiance())
			return TRUE;
	}
	return FALSE;
}

//******************************************************************************************
// Points the weapon at a target and a place (the arguments are the thing and the address of the position). A mode that
// adjusts its aim lobs a gravity round: it tries pitches between the mode's deflections, relative to the primary
// orientation velocity unless +0x98 is set, for the nearest arc; otherwise it points straight along the line.
// Retail constructs delta at frame+0x2c (0050917b) and velocity at frame+0x3c (005091e8); both relative-limit
// Elevation paths use velocity (00509257/0050928f), not delta. The velocity is the orientation times a forward speed
// vector (VC6 drops the zero terms: one out-of-line FVector constructor with the three Row[i].Y products pushed,
// 005091c3..005091e8; the GillMHeadAI form). Each orientation is built by the angle constructor, which calls
// MakeRotationF out of line (005093b0, 00509434) and is copied with rep movs.
// Matched: the consumed readers, nested relative limits and final publication above preserve all 15 native calls.
// MagnitudeXY gives native's square reuse and the direct Elevation schedule; the two scalar source owners below
// give the native frame and slot classes. Original helper spelling and ownership remain unknown.
// PINNED-COMPILER RECONSTRUCTION CONCESSION: pitch is uninitialized in the C++ source when no candidate wins.
// Native's no-winner path passes the earlier height word (00509208, 0050939a). This candidate reproduces it through
// measured VC6 reuse of dz's dead slot for pitch; initializing pitch to dz loses the byte match. This is not a
// proved original source bug or intent, nor a portable source-level algorithm for another compiler or rebuild.
void CWeapon::Unknown509140(SINT a, SINT b)	// FUNCTION: 0x00509140
{
	CThing	*target = (CThing *)a;
	FVector	&pos = *(FVector *)b;
	FVector	d = pos - GetPrimaryPosition();
	if (mData->mAdjustAim)
	{
		if (RoundOf(WeaponModeOf(this))->IsBallistic())
		{
			float	speed = WeaponRound_mSpeed(RoundOf(WeaponModeOf(this)));
			FVector	vel = GetPrimaryOrientation() * FVector(0, speed, 0);
			float	dz = pos.Z - GetPrimaryPosition().Z;
			float	pitch; // Pinned VC6 slot-reuse concession; see the function comment above.
			float	v = WeaponRound_mSpeed(RoundOf(WeaponModeOf(this))) / 20.0f;
			float	g = WeaponRound_mGravity(RoundOf(WeaponModeOf(this))) * 0.025f;
			float	dist = d.MagnitudeXY();
			float	lo, hi;
			if (mUnknown98)
			{
				hi = WeaponMode_mMinDeflection(WeaponModeOf(this));
				lo = WeaponMode_mMaxDeflection(WeaponModeOf(this));
			}
			else
			{
				hi = WeaponRelativeMinPitch(WeaponModeOf(this), vel);
				lo = WeaponRelativeMaxPitch(WeaponModeOf(this), vel);
			}
			float	best = 9999999.0f;
			// The measured VC6 allocation shares dead dz with pitch. Native's no-winner
			// path reads the height word from this home; the C++ result is uninitialized.
			for (float p = lo; p < hi; p += 0.02f)
			{
				float	vh = (float)cos(p) * v;
				float	vv = (float)sin(p) * v;
				float	root = sqrtf(vv * vv + g * dz * 2);
				float	r = (root - vv) / g * vh;
				if (r > 0)
				{
					float	err = (float)fabs(r - dist);
					if (err < best)
					{
						best = err;
						pitch = p;
					}
				}
				r = (-vv - root) / g * vh;
				if (r > 0)
				{
					float	err = (float)fabs(r - dist);
					if (err < best)
					{
						best = err;
						pitch = p;
					}
				}
			}
			mOrientation = FMatrix(d.Azimuth(), pitch, 0);
		}
		else
			mOrientation = FMatrix(d.Azimuth(), d.Elevation(), 0);
	}
	else
		mOrientation = GetPrimaryOrientation();
	WeaponPublishAimRequest(this, pos, target);
}

//******************************************************************************************
// Both branches assign the returned pitch. Together with the declared-then-assigned origin, this retains
// retail's reused float slot and Elevation-path store (0050963e, 0050964f).
float CWeapon::CalculateDesiredPitch(FVector target)	// FUNCTION: 0x005094b0
{
	FVector origin;
	origin = mAttachedTo->GetPos();
	if (mUnknown98)
		mAttachedTo->Unknown113(&origin);
	FVector delta = target - origin;
	float pitch = -PI / 4;
	float angleA = mMode->mMinDeflection;
	float angleB = mMode->mMaxDeflection;
	if (mMode->mRound->Unknown40d0f0())
	{
		float bestError = 99999.0f;
		float height = target.Z - mAttachedTo->GetPos().Z;
		float speed = mMode->mRound->mSpeed / 20.0f;
		float gravity = mMode->mRound->mGravity * 0.025f;
		float distance = delta.MagnitudeXY();
		for (float angle = angleB; angle < angleA; angle += 0.02f)
		{
			float vertical = (float)sin(angle) * speed;
			float discriminant = vertical * vertical + 2 * gravity * height;
			if (discriminant > 0)
			{
				float range = ((float)sqrt(discriminant) - vertical) / gravity * ((float)cos(angle) * speed);
				if (range > 0)
				{
					float error = (float)fabs(range - distance);
					if (error < bestError)
					{
						bestError = error;
						pitch = angle;
					}
				}
			}
		}
	}
	else
		pitch = delta.Elevation();
	return pitch;
}

//******************************************************************************************
float CWeapon::GetMinRange(FVector target)	// FUNCTION: 0x005096a0
{
	if (mMode && mMode->mRound)
	{
		if (mMode->mRound->IsBallistic())
		{
			float height;
			if (target == FVector(0, 0, 0))
				height = 0;
			else
				height = target.Z - GetPrimaryPosition().Z;
			float speed = mMode->mRound->mSpeed / 20.0f;
			float gravity = mMode->mRound->mGravity * 0.025f;
			float angleA = mMode->mMinDeflection;
			float angleB = mMode->mMaxDeflection;
			if ((angleA > -PI / 4 && angleB > -PI / 4) || (angleA < -PI / 4 && angleB < -PI / 4))
			{
				float sineA = (float)sin(angleA);
				float rangeA = ((float)sqrt(fabs(sineA * sineA * speed * speed - 2 * gravity * height)) - sineA * speed)
					* (float)cos(angleA) * speed / gravity;
				float sineB = (float)sin(angleB);
				float rangeB = ((float)sqrt(fabs(sineB * sineB * speed * speed - 2 * gravity * height)) - sineB * speed)
					* (float)cos(angleB) * speed / gravity;
				if (rangeA > rangeB)
					rangeA = rangeB;
				return rangeA;
			}

			float vertical = (float)sin(angleA) * speed;
			float rangeA = ((float)sqrt(vertical * vertical + 2 * gravity * height) - vertical) / gravity
				* ((float)cos(angleA) * speed);
			if (rangeA < 0)
				rangeA = 0;
			vertical = (float)sin(angleB) * speed;
			float rangeB = ((float)sqrt(vertical * vertical + 2 * gravity * height) - vertical) / gravity
				* ((float)cos(angleB) * speed);
			if (rangeB < 0)
				rangeB = 0;
			vertical = (float)sin(-PI / 4) * speed;
			float rangeC = ((float)sqrt(vertical * vertical + 2 * gravity * height) - vertical) / gravity
				* ((float)cos(-PI / 4) * speed);
			if (rangeC < 0)
				rangeC = 0;
			if (rangeA == 0)
				rangeA = rangeC;
			if (rangeB < rangeA && rangeB != 0)
				rangeA = rangeB;
			if (rangeC < rangeA && rangeC != 0)
				rangeA = rangeC;
			return rangeA;
		}
		return mMode->mMinRange;
	}
	return 0;
}

//******************************************************************************************
float CWeapon::GetMaxRange(FVector target)	// FUNCTION: 0x005099a0
{
	if (mMode && mMode->mRound)
	{
		if (mMode->mRound->IsBallistic())
		{
			float height;
			if (target == FVector(0, 0, 0))
				height = 0;
			else
				height = target.Z - GetPrimaryPosition().Z;
			float speed = mMode->mRound->mSpeed / 20.0f;
			float gravity = mMode->mRound->mGravity * 0.025f;
			float angleA = mMode->mMinDeflection;
			float angleB = mMode->mMaxDeflection;
			if ((angleA > -PI / 4 && angleB > -PI / 4) || (angleA < -PI / 4 && angleB < -PI / 4))
			{
				float sineA = (float)sin(angleA);
				float rangeA = ((float)sqrt(fabs(sineA * sineA * speed * speed - 2 * gravity * height)) - sineA * speed)
					* (float)cos(angleA) * speed / gravity;
				float sineB = (float)sin(angleB);
				float rangeB = ((float)sqrt(fabs(sineB * sineB * speed * speed - 2 * gravity * height)) - sineB * speed)
					* (float)cos(angleB) * speed / gravity;
				if (rangeA > rangeB)
					return rangeA;
				return rangeB;
			}

			float vertical = (float)sin(angleA) * speed;
			float rangeA = ((float)sqrt(vertical * vertical + 2 * gravity * height) - vertical) / gravity
				* ((float)cos(angleA) * speed);
			if (rangeA < 0)
				rangeA = 0;
			vertical = (float)sin(angleB) * speed;
			float rangeB = ((float)sqrt(vertical * vertical + 2 * gravity * height) - vertical) / gravity
				* ((float)cos(angleB) * speed);
			if (rangeB < 0)
				rangeB = 0;
			vertical = (float)sin(-PI / 4) * speed;
			float rangeC = ((float)sqrt(vertical * vertical + 2 * gravity * height) - vertical) / gravity
				* ((float)cos(-PI / 4) * speed);
			if (rangeC < 0)
				rangeC = 0;
			float result = rangeA;
			if (rangeB > result)
				result = rangeB;
			if (rangeC > result)
				result = rangeC;
			return result;
		}
		return mMode->mMaxRange;
	}
	return 0;
}

//******************************************************************************************
float CWeapon::GetActualMaxRange(FVector target)	// FUNCTION: 0x00509c80
{
	// written out, with the null checks (00509c8b..00509cbe): beams and torpedoes bypass the ballistic-range path
	if (mMode && mMode->mRound && mMode->mRound->mGravity * 0.025f != 0.0f && !mMode->mRound->mBeam && !mMode->mRound->mUnknown6C)
		return GetMaxRange(target);
	CWeaponModeData *mode = GetCurrentMode();
	// The fallback walk keeps its own result and the tail reads a variable of its own, chosen after the walk
	// (00509dd3 `mov edx,ebx` from the current mode, 00509df2 `mov edx,eax` from the walk): with one variable for the
	// guard, the walk and the tail VC6 knows mode is NULL where the walk runs out and threads that exit to the
	// return of 0 (and drops the tail's first test on the exits that have a mode); a distinct variable that two
	// definitions reach keeps every test and the exit through the tail, and mode stays in ebx across the walk.
	CWeaponModeData *fallback = NULL;
	if (!mode)
	{
		for (int i = 0; i < 5; i++)
		{
			fallback = GetWeaponMode(mData->mChargeModes[i]);
			if (fallback)
				break;
		}
	}
	CWeaponModeData *use = fallback ? fallback : mode;
	if (use && RoundOf(use))
	{
		if (RoundOf(use)->mBeam)
			return use->mMaxRange;
		if (RoundOf(use)->mUnknown28 > 0.0f && RoundOf(use)->mUnknown48)
			return use->mLockRange;
		return RoundOf(use)->mSpeed * RoundOf(use)->mLifeSpan;
	}
	return 0.0f;
}

//******************************************************************************************
CWeaponModeData *CWeapon::GetWeaponMode(SINT index)	// FUNCTION: 0x00509e40
{
	SINT i = 0;
	for (CWeaponModeData *mode = UPhysicsManager::mWeaponModes->First(); mode; mode = UPhysicsManager::mWeaponModes->Next())
	{
		if (i == index)
			return mode;
		i++;
	}
	return NULL;
}

//******************************************************************************************
// Retail looks up the charge level's mode, then walks down the lower levels. GetActualMaxRange's inlined copy shows
// both walks: the first inline, the loop's GetWeaponMode out of line (00509d60), and the induction variable
// lea ebx,[esi*4+0xc] (00509d15). Against the one-walk loop this form takes the shape (registers and stack slots
// masked) of GetCurrentMode from 42 to 11, ReadyToFire 00509f70 from 47 to 9, CanLock 005061f0 from 37 to 11, the
// six getters 00506350..00506800 from 50 to 11 each and Fire from 43 to 26. GetActualMaxRange 00509c80 goes from
// 111 to 145: retail refuses its second walk and the first walk's First/Next there, ours expands them.
// STAND-IN (this body and its inlined copies: ReadyToFire, CanLock, the six getters; found
// 2026-10-05): the `if (level < 0)` arm recomputes the same quotient, which C2's CSE turns into `level = level`. The
// self-copy reaches register allocation as a one-block arm and is deleted afterwards with its branch, so the section
// keeps no trace. The split takes block 0 from 10 allocator nodes to 5 (+1, +8): the /100 quotient web falls from 140
// to 70, below the second walk's registry webs (96, 84), which then take edx first; level gets ebp, the mData load
// edx, and the IV (priority 0) is coloured before the index (-5), which gives retail's IV-first lookup
// (lea edi,[ebp*4+0xc]; mov ebx,[edi+edx]). Any integer test of level works; `level = level`, an if/else pair or a
// float test does not. Which source Stuart wrote is unknown. Without the arm: 11/53 (ReadyToFire 9/53), from the
// colouring order (the quotient first) and the mData address operand rank (IV first only at a temp id 0 mod 4).
CWeaponModeData *CWeapon::GetCurrentMode()	// FUNCTION: 0x00509e90
{
	SINT level = (SINT)mCharge / 100;
	if (level < 0)
		level = (SINT)mCharge / 100;	// STAND-IN: the same quotient again (see above)
	CWeaponModeData *mode = GetWeaponMode(mData->mChargeModes[level]);
	while (!mode)
	{
		level--;
		if (level < 0)
			return NULL;
		mode = GetWeaponMode(mData->mChargeModes[level]);
	}
	return mode;
}

//******************************************************************************************
BOOL CWeapon::ReadyToFire()	// FUNCTION: 0x00509f70
{
	if (mMode)
	{
		if (EVENT_MANAGER.GetTime() > mNextFireTime)
			return TRUE;
		return FALSE;
	}
	if (GetCurrentMode())
		return TRUE;
	return FALSE;
}

//******************************************************************************************
BOOL CWeapon::ReadyToCharge()	// FUNCTION: 0x0050a080
{
	if (mMode && EVENT_MANAGER.GetTime() <= mNextFireTime)
		return FALSE;
	return TRUE;
}

//******************************************************************************************
BOOL CWeapon::Unknown50a0b0(CThing *target)	// FUNCTION: 0x0050a0b0
{
	if (!mActive)
		return FALSE;
	return mTargetTypes & target->GetThingType();
}

//******************************************************************************************
BOOL CWeapon::Unknown50a0d0(ULONG thing_type)	// FUNCTION: 0x0050a0d0
{
	return mTargetTypes & thing_type;
}

//******************************************************************************************
FVector CWeapon::GetPredictedTargetPos(CUnit *target)	// FUNCTION: 0x0050a0e0
{
	if (mMode && mMode->mPredictive)
	{
		FVector pos = GetPrimaryPosition();
		FVector tpos;
		target->GetTargetablePos(&tpos);
		float speed;
		if (mMode && mMode->mRound)
		{
			if (mMode->mRound->mBeam)
				speed = 1000.0f;
			else
				speed = mMode->mRound->mSpeed;
		}
		else
			speed = 0;
		FVector vel = GetPrimaryOrientation() * FVector(0, speed, 0);
		FVector tvel = target->GetVelocity();
		float t = (pos - tpos).Magnitude() / vel.Magnitude();
		t *= 20.0f;
		return tvel * t + tpos;
	}
	FVector tpos;
	target->GetTargetablePos(&tpos);
	return tpos;
}

//******************************************************************************************
BOOL CWeapon::IsFiring()	// FUNCTION: 0x0050a290
{
	if (mMode && mBurstCount && mBurstCount < mMode->mBurstSize)
		return TRUE;
	return FALSE;
}

//******************************************************************************************
// The round flies a ballistic arc: it has gravity, is not a beam and not mUnknown6C. Retail's copy at 0040d0f0 is a
// header inline's, first called out of line by CBattleEngine::GetLaunchPosition (0040caca, inside CWeapon::Gravity);
// SpawnRound (00506bf4), 005088b0 (00508aaf), CalculateDesiredPitch (0050952c) and GetActualMaxRange expand it.
// STAND-IN (layout): the same body as BattleEngine.cpp's plain copy (see there), inline here; this object emits none.
inline BOOL CRoundData::Unknown40d0f0()
{
	return mGravity * 0.025f != 0.0f && !mBeam && !mUnknown6C;
}

//******************************************************************************************
// The same test for the range and aim queries (Unknown507ab0, Unknown509140, GetMinRange, GetMaxRange), which retail
// calls at 00507dd4, 0050919a, 005096c1 and 005099c1 while the functions above expand it. Neither the plain inline cap
// nor the caller's budget explains those calls: the body needs 100+ more nodes to pass the cap, and GetMaxRange needs
// 40-80 more accessor calls to deny it. Retail's copy was a separate, identical function in another object, which the
// linker's identical-COMDAT folding merged into 0040d0f0. The pragma stands in for that object boundary; this copy is
// out of line (selection 1) as BattleEngine.cpp's now is, so it folds there too.
#pragma auto_inline(off)
BOOL CRoundData::IsBallistic()
{
	return mGravity * 0.025f != 0.0f && !mBeam && !mUnknown6C;
}
#pragma auto_inline(on)
