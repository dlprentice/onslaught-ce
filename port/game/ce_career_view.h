// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_career_view.h: what the Service Record shows of a career, worked out from the career file's bytes and nothing else.
//
// The layout is the one the Onslaught Toolkit publishes for its Companion (Careers/CareerSave.cs and the save-file notes it cites):
// a career file is 10,004 bytes, a two-byte version word and then the game's career structure as the game copies it: a count of
// Goodies still to announce, 100 mission records of 64 bytes, 200 route records of 8, 300 Goodie states of 4, five kill counts and
// the settings. A file of another length or version is refused by the reader below; the page itself reads at most one career's bytes of a
// longer file, as the game's own Load Game does, so a longer file is shown as the career its first bytes make.
//
// What a mission's state means is the Companion's rule (Careers/CampaignGraph.cs): complete when the game recorded a win for it; open
// when a route into it is complete, or when no route leads to it (the training mission); else not yet open. A rank letter is the
// game's own rule for the stored value (CCareer::GetGradeFromRanking), and is shown only for a completed mission.
//
// Reading only: these functions take bytes and fill a record. The page that shows it (ce_service_record.cpp) reads the bytes through
// the memory card's own read call and checks this record against the game's own accessors on a scratch copy (ce_career_view.cpp).
// A host test runs these same functions on careers the game wrote (port/tools/ce_career_view_tests.py).
#ifndef CE_CAREER_VIEW_H
#define CE_CAREER_VIEW_H

#include <cmath>
#include <string.h>

enum
{
	CE_CAREER_SIZE = 10004,
	CE_CAREER_VERSION = 0x4BD1,
	CE_CAREER_MISSIONS = 100,			// mission records in the file; the campaign uses 43 of them
	CE_CAREER_ROUTES = 200,
	CE_CAREER_GOODIE_SLOTS = 300,		// Goodie states in the file
	CE_CAREER_GOODIE_TABLE = 233,		// the game's own table: slots 0 to 232
	CE_CAREER_GOODIES_SHOWN = 230,		// what the game's Goodies wall shows: the table less 71, 72 and 73
	CE_CAREER_KILL_CLASSES = 5
};

enum ECeMissionState
{
	CE_MISSION_COMPLETE,
	CE_MISSION_OPEN,
	CE_MISSION_NOT_YET_OPEN
};

// A Goodie's stored state, as the game numbers it.
enum ECeGoodieState
{
	CE_GOODIE_LOCKED = 0,
	CE_GOODIE_HINT = 1,					// locked, with its hint shown
	CE_GOODIE_NEW = 2,
	CE_GOODIE_VIEWED = 3
};

struct SCeMissionView
{
	int				mWorld;				// the game's level number: 311 is "3.11"
	ECeMissionState	mState;
	char			mRank;				// 'S', 'A' to 'E', or 0 when the mission is not complete or the stored value gives no letter
};

struct SCeCareerView
{
	int				mMissionCount;						// missions the career's campaign has
	int				mMissionsComplete;
	int				mRankS;								// completed missions with rank S
	SCeMissionView	mMissions[CE_CAREER_MISSIONS];		// in the campaign's order
	int				mGoodiesFound;						// of the 230 shown: new or viewed
	int				mGoodiesAll;						// of all 300 slots, as the game's own count takes them (CCareer::CountGoodies)
	unsigned char	mGoodie[CE_CAREER_GOODIE_SLOTS];	// each slot's stored state, 255 for a value the game never stores
	int				mKills[CE_CAREER_KILL_CLASSES];		// aircraft, vehicles, emplacements, infantry, mechs
	unsigned mSoundBits, mMusicBits;
	bool mInvincible[2];
};

namespace ce_career_view_detail
{
enum
{
	kMissionOffset = 0x0006, kMissionStride = 64,
	kMissionComplete = 0x04, kMissionLower = 0x08, kMissionHigher = 0x0C, kMissionWorld = 0x10, kMissionRank = 0x3C,
	kRouteOffset = 0x1906, kRouteStride = 8,
	kGoodieOffset = 0x1F46,
	kKillsOffset = 0x23F6,
	kSoundOffset = 0x248e, kMusicOffset = 0x2492, kInvincibleOffset = 0x2496
};

inline unsigned	U16(const unsigned char *p) { return unsigned(p[0]) | (unsigned(p[1]) << 8); }
inline unsigned	U32(const unsigned char *p)
{
	return unsigned(p[0]) | (unsigned(p[1]) << 8) | (unsigned(p[2]) << 16) | (unsigned(p[3]) << 24);
}
}

// Whether a Goodie slot has a place on the game's Goodies wall.
inline bool	CeGoodieShown(int slot)
{
	return (slot >= 0) && (slot < CE_CAREER_GOODIE_TABLE) && ((slot < 71) || (slot > 73));
}

// The letter for a stored rank, by the game's rule: exactly 1 is S, zero or less is E, else 'D' less the whole quarters; 0 for a value
// the rule gives no letter (above 1, or not a number the game ever stores).
inline char	CeRankLetter(unsigned bits)
{
	float value;
	memcpy(&value, &bits, sizeof(value));
	const unsigned exponent = (bits >> 23) & 0xff, fraction = bits & 0x7fffff;
	if ((exponent == 0xff) && fraction)
		return 'S';								// not a number: the game's comparison chain falls through to S, as the Companion shows it
	if (value == 1.f)
		return 'S';
	if (value <= 0.f)
		return 'E';
	if ((exponent == 0xff) || (value > 1.f))
		return 0;
	const int quarters = static_cast<int>(std::floor(value * 4.f));		// 0 to 3: the value is above 0 and below 1
	return char('D' - quarters);
}

// Fills view from a career file's bytes. False, with view cleared, when the bytes are not a career this version of the game writes.
// A damaged optional volume does not make the rest of a readable career unusable.
inline int CeStoredVolumePercent(unsigned bits)
{
	float volume;
	memcpy(&volume, &bits, sizeof(volume));
	if (!(volume >= 0.f && volume <= 1.f))
		return -1;
	return int(volume * 100.f + 0.5f);
}

inline bool	CeCareerViewBuild(const unsigned char *bytes, int size, SCeCareerView *view)
{
	using namespace ce_career_view_detail;
	memset(view, 0, sizeof(*view));
	if (!bytes || (size != CE_CAREER_SIZE) || (U16(bytes) != CE_CAREER_VERSION))
		return false;

	// which missions a completed route leads into, and which any route leads into
	bool into[CE_CAREER_MISSIONS], reached[CE_CAREER_MISSIONS];
	memset(into, 0, sizeof(into));
	memset(reached, 0, sizeof(reached));
	for (int n = 0; n < CE_CAREER_MISSIONS; n++)
	{
		const unsigned char *mission = bytes + kMissionOffset + n * kMissionStride;
		if (!U32(mission + kMissionWorld))
			continue;
		const int links[2] = {int(U32(mission + kMissionLower)), int(U32(mission + kMissionHigher))};
		for (int l = 0; l < 2; l++)
		{
			if ((links[l] < 0) || (links[l] >= CE_CAREER_ROUTES))
				continue;
			const unsigned char *route = bytes + kRouteOffset + links[l] * kRouteStride;
			const unsigned state = U32(route), to = U32(route + 4);
			if ((to >= unsigned(CE_CAREER_MISSIONS)) || !U32(bytes + kMissionOffset + int(to) * kMissionStride + kMissionWorld))
				continue;
			into[to] = true;
			if (state == 1)
				reached[to] = true;
		}
	}
	for (int n = 0; n < CE_CAREER_MISSIONS; n++)
	{
		const unsigned char *mission = bytes + kMissionOffset + n * kMissionStride;
		const unsigned world = U32(mission + kMissionWorld);
		if (!world)
			continue;
		SCeMissionView &out = view->mMissions[view->mMissionCount++];
		out.mWorld = int(world);
		out.mRank = 0;
		if (U32(mission + kMissionComplete))
		{
			out.mState = CE_MISSION_COMPLETE;
			out.mRank = CeRankLetter(U32(mission + kMissionRank));
			view->mMissionsComplete++;
			if (out.mRank == 'S')
				view->mRankS++;
		}
		else
			out.mState = (!into[n] || reached[n]) ? CE_MISSION_OPEN : CE_MISSION_NOT_YET_OPEN;
	}
	for (int g = 0; g < CE_CAREER_GOODIE_SLOTS; g++)
	{
		const unsigned state = U32(bytes + kGoodieOffset + g * 4);
		view->mGoodie[g] = (state <= 3) ? (unsigned char)state : (unsigned char)255;
		if (int(state) >= CE_GOODIE_NEW)
			view->mGoodiesAll++;				// as the game counts: every slot, any value from new upwards
		if (((state == CE_GOODIE_NEW) || (state == CE_GOODIE_VIEWED)) && CeGoodieShown(g))
			view->mGoodiesFound++;
	}
	view->mSoundBits = U32(bytes + kSoundOffset);
	view->mMusicBits = U32(bytes + kMusicOffset);
	for (int player = 0; player < 2; ++player)
		view->mInvincible[player] = U32(bytes + kInvincibleOffset + player * 4) != 0;
	for (int k = 0; k < CE_CAREER_KILL_CLASSES; k++)
		view->mKills[k] = int(U32(bytes + kKillsOffset + k * 4) & 0xffffff);
	return true;
}

// Whether a record holds values the game could have written: every mission has a level number the campaign could use (100 to 999), and
// every Goodie on the wall is in one of its four states. A career that fails this is shown as one that could not be read, never as a
// record with values nobody earned.
inline bool	CeCareerViewPlausible(const SCeCareerView &view)
{
	if (view.mMissionCount < 1)
		return false;
	for (int n = 0; n < view.mMissionCount; n++)
		if ((view.mMissions[n].mWorld < 100) || (view.mMissions[n].mWorld > 999))
			return false;
	for (int g = 0; g < CE_CAREER_GOODIE_SLOTS; g++)
		if (CeGoodieShown(g) && (view.mGoodie[g] > CE_GOODIE_VIEWED))
			return false;
	return true;
}

#endif
