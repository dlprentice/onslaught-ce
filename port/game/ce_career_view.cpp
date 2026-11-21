// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_career_view.cpp: reads a career for the Service Record without loading it.
//
// The careers are the memory card's own list (the user folder and the game folder as Load Game shows them) and a career's bytes
// come through the card's own read call, which opens the file for reading only. Nothing here calls CCareer::Load (it applies the
// career's options and sets the language) and nothing touches the live career, the options file or any file for writing.
//
// The record itself is worked out from the bytes (ce_career_view.h). Before a page shows it, it is checked against the game's own
// accessors on a scratch copy of the career structure, filled by copy as ce_bonus.cpp fills its own: the Goodie count, the kill
// counts, each mission's completion, state and rank letter and each Goodie's state must agree, or the career is reported as
// unreadable and the log says which check failed. CCareerNodeLink::GetToNode is never used: it answers from the live career.
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "Career.h"
#include "MemoryCard.h"
#include "ce_career_view.h"
#include "ce_extras.h"
#include "../platform/common/portlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool	PortCeMenus();								// ce_frontend.cpp
bool	PortCeProjectCareerAllowed() noexcept;		// ce_project.cpp: false while an editor, museum, project or network session has the card closed

namespace
{
CCareer	*sScratch;		// raw bytes of a career structure, never constructed, never the live career

// The mission's state by the same rule as the record's, asked of the game's own structure.
ECeMissionState	StateOf(CCareer *career, int n)
{
	CCareerNode *node = career->GetNode(n);
	if (node->mComplete)
		return CE_MISSION_COMPLETE;
	bool into = false, reached = false;
	for (int from = 0; from < MAX_NODES; from++)
	{
		CCareerNode *other = career->GetNode(from);
		if (!other->mWorldNumber)
			continue;
		const int links[2] = {other->mLowerLink, other->mHigherLink};
		for (int l = 0; l < 2; l++)
		{
			if ((links[l] < 0) || (links[l] >= MAX_LINKS))
				continue;
			CCareerNodeLink *link = career->GetLink(links[l]);
			if (link->mToNode != n)
				continue;
			into = true;
			if (link->mLinkType == CN_COMPLETE)
				reached = true;
		}
	}
	return (!into || reached) ? CE_MISSION_OPEN : CE_MISSION_NOT_YET_OPEN;
}

// A stored rank the game's own rule is defined for: a number from 0 to 1.
bool	PlainRank(float value)
{
	return (value >= 0.f) && (value <= 1.f);
}

unsigned VolumeBits(float value)
{
	unsigned bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

const char	*Disagreement(const unsigned char *bytes, const SCeCareerView &view)
{
	if (!sScratch)
		sScratch = (CCareer *)malloc(sizeof(CCareer));
	if (!sScratch)
		return "no memory";
	if (int(sizeof(CCareer)) + 2 > CE_CAREER_SIZE)
		return "the career structure is larger than a career file";
	memcpy((void *)sScratch, bytes + 2, sizeof(CCareer));
	if (VolumeBits(sScratch->GetSoundVolume()) != view.mSoundBits ||
		VolumeBits(sScratch->GetMusicVolume()) != view.mMusicBits)
		return "stored volume";
	for (int player = 0; player < 2; ++player)
		if ((sScratch->GetIsGod(player) != 0) != view.mInvincible[player])
			return "stored invincibility";
	if (sScratch->CountGoodies() != view.mGoodiesAll)
		return "the Goodie count";
	for (int k = 0; k < CE_CAREER_KILL_CLASSES; k++)
		if (sScratch->GetNumKilled((EKilledType)k) != view.mKills[k])
			return "a kill count";
	for (int g = 0; g < CE_CAREER_GOODIE_SLOTS; g++)
	{
		const int state = int(sScratch->GetGoodieState(g));
		if (((state >= 0) && (state <= 3)) ? (view.mGoodie[g] != state) : (view.mGoodie[g] != 255))
			return "a Goodie's state";
	}
	int listed = 0, complete = 0;
	for (int n = 0; n < MAX_NODES; n++)
	{
		CCareerNode *node = sScratch->GetNode(n);
		if (!node->mWorldNumber)
			continue;
		if (listed >= view.mMissionCount)
			return "the number of missions";
		const SCeMissionView &mission = view.mMissions[listed++];
		if (mission.mWorld != node->mWorldNumber)
			return "a mission's number";
		if (mission.mState != StateOf(sScratch, n))
			return "a mission's state";
		if (node->mComplete)
		{
			complete++;
			if (PlainRank(node->mRanking) && (mission.mRank != char(sScratch->GetGradeFromRanking(node->mRanking))))
				return "a mission's rank";
		}
	}
	if ((listed != view.mMissionCount) || (complete != view.mMissionsComplete))
		return "the number of missions";
	return NULL;
}
}

// The careers the memory card lists, or -1 when careers cannot be read now (the card is closed, or the CE menus are off).
int	PortCeCareerCount()
{
	if (!PortCeMenus() || !PortCeProjectCareerAllowed())
		return -1;
	int count = 0;
	if (MEMORYCARD.GetNumSaves(FRONTEND.GetMemoryCardNumber(), &count) != MCE_SUCCESS)
		return -1;
	return count;
}

// The name of the card's career number index, as Load Game lists it; name holds 256 characters.
bool	PortCeCareerName(int index, WCHAR *name)
{
	name[0] = 0;
	if (!PortCeMenus() || !PortCeProjectCareerAllowed())
		return false;
	return MEMORYCARD.GetSaveName(FRONTEND.GetMemoryCardNumber(), index, name) == MCE_SUCCESS;
}

// Reads the career of that name into view, for showing. False when it cannot be read as a career of this version.
static bool ReadRecord(const WCHAR *name, SCeCareerView *view, unsigned char *copy)
{
	memset(view, 0, sizeof(*view));
	if (!PortCeMenus() || !PortCeProjectCareerAllowed())
		return false;
	if (CCareer::SizeOfSaveGame() != CE_CAREER_SIZE)
	{
		fprintf(stderr, PORT_LOG_PREFIX "extras: this build's career is not %d bytes; careers are not shown\n", int(CE_CAREER_SIZE));
		return false;
	}
	static unsigned char bytes[CE_CAREER_SIZE];
	static WCHAR asked[256];
	wcsncpy(asked, name, 255);
	asked[255] = 0;
	int read = 0;
	if (MEMORYCARD.ReadSave(FRONTEND.GetMemoryCardNumber(), 0, asked, bytes, CE_CAREER_SIZE, &read) != MCE_SUCCESS)
		return false;
	if (!CeCareerViewBuild(bytes, read, view))
		return false;
	if (!CeCareerViewPlausible(*view))
	{
		fprintf(stderr, PORT_LOG_PREFIX "extras: a career holds values the game never writes; it is not shown\n");
		memset(view, 0, sizeof(*view));
		return false;
	}
	if (const char *what = Disagreement(bytes, *view))
	{
		fprintf(stderr, PORT_LOG_PREFIX "extras: a career's record disagrees with the game's own reading (%s); it is not shown\n", what);
		memset(view, 0, sizeof(*view));
		return false;
	}
	if (copy) memcpy(copy, bytes, CE_CAREER_SIZE);
	return true;
}

bool	PortCeCareerRead(const WCHAR *name, SCeCareerView *view)
{
	return ReadRecord(name, view, NULL);
}
bool PortCeCareerReadSnapshot(const WCHAR *name, SCeCareerView *view, unsigned char *copy)
{
	return copy && ReadRecord(name, view, copy);
}

#endif
