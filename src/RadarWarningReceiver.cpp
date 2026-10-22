// RadarWarningReceiver.cpp: CRadarWarningReceiver, the battle engine's incoming-missile sensor (reconstructed; the
// file name is the object's __FILE__, 00631784). Every scan it lists the rounds aimed at its unit within range, and a
// new one ahead of a battle engine sets off its incoming-missile sound.

#include	"common.h"

#define RADARWARNINGRECEIVER_CPP
#include	"RadarWarningReceiver.h"
#include	"BattleEngine.h"
#include	"round.h"
#include	"world.h"
#include	"eventmanager.h"
#include	"Event.h"
#include	"WorldPhysicsManager.h"

// The per-file maths constants from maths.h:
// FUNCTION: 0x004d64b0 init:ZERO_FVECTOR
// FUNCTION: 0x004d64d0 init:ID_FMATRIX

#define RWR_EVENT_UPDATE		4000
#define RWR_EVENT_NEW_THREAT	4002

//*********************************************************************************
void	CRadarWarningReceiver::Init(CInitRadarWarningReceiver &init)	// FUNCTION: 0x004d65a0
{
	CEquipment::Init(init);
	mScanDistance = init.mScanDistance;
	mUpdateFrequency = init.mUpdateFrequency;
	EVENT_MANAGER.AddEvent(RWR_EVENT_UPDATE, this, EVENT_MANAGER.GetTime() + mUpdateFrequency, END_OF_FRAME);
	mNewThreat = FALSE;
	mNewThreatTime = -1.f;
}

//*********************************************************************************
CRadarWarningReceiver::~CRadarWarningReceiver()	// FUNCTION: 0x004d6600
{
	mThreats.DeleteAll();
}

//*********************************************************************************
// The scan's parts, each expanded into Update. Retail's Update calls the list iterator's First/Next out of line in
// its first three loops (004d66dd..004d67c2, 004d691c), the difference's FVector constructor (004d674c) and
// CMonitor::RemoveDeletionEvent (004d69c5), and inlines the rest: written out in Update, the same code expands all
// of those (own size 840 against about 600); as these helpers it gives retail's call list exactly.
static inline void ClearSeen(SPtrSet<SRadarThreat> *threats)
{
	ListIterator<SRadarThreat>	it(threats);
	for (SRadarThreat *t = it.First(); t; t = it.Next())
		t->mSeen = FALSE;
}

static inline SRadarThreat *FindThreat(SPtrSet<SRadarThreat> *threats, CThing *round)
{
	ListIterator<SRadarThreat>	find(threats);
	for (SRadarThreat *t = find.First(); t; t = find.Next())
		if (t->mRound.ToRead() == round)
			return t;
	return NULL;
}

// one coming from ahead sounds the alarm
static inline void SoundAlarm(CThing *attached, float yaw)
{
	if (attached->IsA(THING_TYPE_BATTLE_ENGINE))
	{
		float	facing = ((CBattleEngine *)attached)->mCurrentOrientation.mYaw;
		if ((yaw < -PI / 2) && (facing > PI / 2))
			facing -= PI * 2;
		else if ((yaw > PI / 2) && (facing < -PI / 2))
			facing += PI * 2;
		if ((float)fabs(yaw - facing) < PI / 4)
			((CBattleEngine *)attached)->PlayIncommingMissileSound();
	}
}

static inline void NoteNewThreat(CRadarWarningReceiver *rwr)
{
	rwr->mNewThreat = TRUE;
	rwr->mNewThreatTime = EVENT_MANAGER.GetTime();
	EVENT_MANAGER.AddEvent(RWR_EVENT_NEW_THREAT, rwr->mAttachedTo, NEXT_FRAME);
}

// STAND-IN (scope; review F 2026-10-15): the forget loop's iterator has its own block. In a function with an exception frame, C2's slot packer (the pre-
// pass at 0x1074b243) treats every call that may throw as a use of the alias class of the last block scope holding
// an address-taken local, so that scope's symbols stay live back to each such call. Without the block that scope is
// NoteNewThreat's NEXT_FRAME temporary, which then conflicts with count and takes yaw's slot (esp+0x10) instead of
// count's (esp+0x1c, 004d6969, 004d6986).
//*********************************************************************************
void	CRadarWarningReceiver::Update()	// FUNCTION: 0x004d66b0
{
	ClearSeen(&mThreats);
	SRadarThreat	*t;

	SINT	count = mThreats.Size();

	ListIterator<CThing>	rounds(&WORLD.mSet110);
	for (CRound *r = (CRound *)rounds.First(); r; r = (CRound *)rounds.Next())
	{
		FVector	d = r->GetPos() - GetPrimaryPosition();
		if ((d.Magnitude() <= mScanDistance) && (r->mUnknownE8.ToRead() == (CThing *)mAttachedTo) && (r->mData->mUnknown1C > 0))
		{
			t = FindThreat(&mThreats, r);

			float	yaw = d.Azimuth();
			if (t)
			{
				t->mSeen = TRUE;
				t->mYaw = yaw;
				t->mDistance = d.Magnitude();
			}
			else
			{
#line 65
				t = new(MEMTYPE_RADAR) SRadarThreat(yaw, d.Magnitude(), r->GetThingNumber(), r, EVENT_MANAGER.GetTime());
				mThreats.Add(t);

				SoundAlarm(mAttachedTo, yaw);
			}
		}
	}

	EVENT_MANAGER.AddEvent(RWR_EVENT_UPDATE, this, EVENT_MANAGER.GetTime() + mUpdateFrequency, END_OF_FRAME);

	if (mThreats.Size() > count)
		NoteNewThreat(this);

	// forget the ones not seen this time
	{
		ListIterator<SRadarThreat>	old(&mThreats);
		t = old.First();
		while (t)
		{
			if (!t->mSeen)
			{
				mThreats.Remove(t);
				delete t;
				t = old.First();
			}
			else
				t = old.Next();
		}
	}
}

//*********************************************************************************
void	CRadarWarningReceiver::HandleEvent(CEvent *event)	// FUNCTION: 0x004d6a10
{
	if (event->GetEventNum() == RWR_EVENT_UPDATE)
		Update();
}
