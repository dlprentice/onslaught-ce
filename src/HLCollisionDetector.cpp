// Persistent collision scheduling reconstructed from the pristine PC executable.
#include "common.h"
#define HLCOLLISIONDETECTOR_CPP
#include "collisionseekingthing.h"
#include "thing.h"
#include "mapwho.h"
#include "eventmanager.h"
#include "DebugLog.h"

// FUNCTION: 0x00480940 init:ZERO_FVECTOR
// FUNCTION: 0x00480960 init:ID_FMATRIX

SINT CHLCollisionDetector::mChecksDone = 0;

void CHLCollisionDetector::Init(CCSPersistentThing *thing)	// FUNCTION: 0x00480a30
{
	mThing = thing;
	mCheckingCollision = FALSE;
	mEvent = NULL;
	SINT firstLayer = mThing->mForThing->GetMapWhoEntry()->GetSectorIn().GetLayer();
	for (SINT layer = firstLayer; layer >= 0; layer--)
	{
		CSector centre = MAP_WHO.WorldToSector(mThing->mForThing->GetPos(), layer);
		SINT minX = centre.mX - 1;
		SINT maxX = centre.mX + 1;
		SINT minY = centre.mY - 1;
		SINT maxY = centre.mY + 1;
		for (SINT x = minX; x <= maxX; x++)
			for (SINT y = minY; y <= maxY; y++)
			{
				CSector sector(x, y, layer);
				if (MAP_WHO.IsValidSector(sector))
				{
					CMapWhoNode *node = MAP_WHO.GetSector(sector);
					if (firstLayer == layer)
						ScanSector(node);
					else
						for (CMapWhoEntry *entry = MAP_WHO.GetFirstEntry(node); entry; entry = MAP_WHO.GetNextEntry())
							CheckCandidate(entry->GetThing()->GetCSPT());
				}
			}
	}
}

void CHLCollisionDetector::CheckCollision(CCSPersistentThing *other)	// FUNCTION: 0x00480c90
{
	mChecksDone++;
	if (other->Unknown10(mThing->mForThing->GetMapWhoEntry()->GetSectorIn()) > 1)
	{
		mCheckingCollision = TRUE;
		return;
	}
	FVector otherCentre, centre;
	other->mForThing->GetCentrePos(otherCentre);
	mThing->mForThing->GetCentrePos(centre);
	FVector delta = otherCentre - centre;
	float radius = mThing->mRadius + other->mRadius;
	float distanceSquared = delta.MagnitudeSq();
	if (distanceSquared - radius * radius < 0.0)
	{
		if ((mThing->mCollisionFlags & 0x100) || (other->mCollisionFlags & 0x100))
			if (!mThing->Unknown8(other) || !other->Unknown8(mThing))
			{
				mCheckingCollision = TRUE;
				return;
			}
		mThing->Unknown6(other);
	}
	mCheckingCollision = TRUE;
	ScheduleCollisionCheck(other);
}

void CHLCollisionDetector::CheckCandidate(CCSPersistentThing *other)	// FUNCTION: 0x00480db0
{
	if (other && other != mThing && mThing->Unknown8(other) && other->Unknown8(mThing))
	{
		if (mCheckingCollision == TRUE)
			LOG.AddMessage("WARNING: Unexpected collision check already done");
		else
		{
			ScheduleCollisionCheck(other);
			mCheckingCollision = FALSE;
		}
	}
}

void CHLCollisionDetector::ScanSector(CMapWhoNode *node)	// FUNCTION: 0x00480e10
{
	CMapWhoNode **children = node->mChildren;
	if (children)
	{
		ScanSector(children[0]);
		ScanSector(children[1]);
		ScanSector(children[2]);
		ScanSector(children[3]);
	}
	for (CMapWhoEntry *entry = MAP_WHO.GetFirstEntry(node); entry; entry = MAP_WHO.GetNextEntry())
	{
		CCSPersistentThing *other = entry->GetThing()->GetCSPT();
		if (other)
			CheckCandidate(other);
	}
}

void CHLCollisionDetector::ScheduleCollisionCheck(CCSPersistentThing *other)	// FUNCTION: 0x00480ed0
{
	if (mDeletionList && mDeletionList->Size() > 950)
	{
		LOG.AddMessage("WARNING: Object colliding with too many objects %08x", this);
		return;
	}
	FVector otherCentre, centre;
	other->mForThing->GetCentrePos(otherCentre);
	mThing->mForThing->GetCentrePos(centre);
	FVector delta = otherCentre - centre;
	float distance = delta.Magnitude() - (mThing->mRadius + other->mRadius);
	if (distance > 0.0f)
	{
		CThing *otherThing = other->mForThing;
		CThing *thing = mThing->mForThing;
		float otherSpeed = otherThing->GetMaxVelocity();
		float speed = otherSpeed + thing->GetMaxVelocity();
		if (speed == 0.0f)
			return;
		float time = EVENT_MANAGER.GetTime() + distance / speed;
		if (time > EVENT_MANAGER.GetTime())
		{
			float eventTime = time;
			if (mEvent)
			{
				EVENT_MANAGER.AddEvent(2000, this, eventTime, MIDDLE_OF_FRAME, other, mEvent);
				mEvent = NULL;
			}
			else
				EVENT_MANAGER.AddEvent(2000, this, eventTime, MIDDLE_OF_FRAME, other);
			return;
		}
	}
	if (!mCheckingCollision)
		CheckCollision(other);
	else
	{
		float time = NEXT_FRAME;
		if (mEvent)
		{
			EVENT_MANAGER.AddEvent(2000, this, time, MIDDLE_OF_FRAME, other, mEvent);
			mEvent = NULL;
		}
		else
			EVENT_MANAGER.AddEvent(2000, this, time, MIDDLE_OF_FRAME, other);
	}
}

void CHLCollisionDetector::MovedSector(CSector &from, CSector &to)	// FUNCTION: 0x00481060
{
	CSector oldSector = from;
	CSector newSector = to;
	for (SINT layer = to.GetLayer(); layer >= 0; layer--)
	{
		if (!(newSector == oldSector))
		{
			for (SINT x = -1; x < 2; x++)
				for (SINT y = -1; y < 2; y++)
				{
					// STAND-IN (review F 2026-10-15): the four const SINT references below; written plain, ProcessMapWhoCollisionSweep 00481060 loses its match.
					const SINT &originX = newSector.GetX();
					const SINT &originY = newSector.GetY();
					const SINT &offsetY = y;
					CSector sector(originX + x, originY + offsetY, layer);
					const SINT &oldY = oldSector.GetY();
					if (MAP_WHO.IsValidSector(sector) &&
						(abs(sector.GetX() - oldSector.GetX()) > 1 || abs(sector.GetY() - oldY) > 1))
					{
						CMapWhoNode *node = MAP_WHO.GetSector(sector);
						if (layer == to.GetLayer())
							ScanSector(node);
						else
							for (CMapWhoEntry *entry = MAP_WHO.GetFirstEntry(node); entry; entry = MAP_WHO.GetNextEntry())
								CheckCandidate(entry->GetThing()->GetCSPT());
					}
				}
		}
		if (layer != 0)
		{
			newSector.SetLayer(newSector.GetLayer() - 1);	newSector.SetX(newSector.GetX() >> 1);	newSector.SetY(newSector.GetY() >> 1);
			oldSector.SetLayer(oldSector.GetLayer() - 1);	oldSector.SetX(oldSector.GetX() >> 1);	oldSector.SetY(oldSector.GetY() >> 1);
		}
	}
}

void CHLCollisionDetector::HandleEvent(CEvent *event)	// FUNCTION: 0x004812d0
{
	if (event->GetEventNum() == 2000)
	{
		CScheduledEvent *scheduled = (CScheduledEvent *)event;
		CCSPersistentThing *other = (CCSPersistentThing *)scheduled->GetData();
		if (other)
		{
			mEvent = scheduled;
			CheckCollision(other);
			mCheckingCollision = FALSE;
			mEvent = NULL;
		}
	}
}
