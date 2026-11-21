// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_career_edit.h: pure-byte career editor over 10,004-byte career buffers.
//
// Operates strictly on an in-memory copy of a valid career file, never touches the live CCareer
// or any file on disk, and preserves every untouched or unknown byte bit-for-bit. Every edited
// buffer is verified through CeCareerViewBuild and CeCareerViewPlausible before returning.
#ifndef CE_CAREER_EDIT_H
#define CE_CAREER_EDIT_H

#include "ce_career_view.h"

#include <string.h>

enum ECeGoodieEdit
{
	CE_EDIT_GOODIES_UNCHANGED = 0,
	CE_EDIT_GOODIES_UNLOCK_ALL = 1,
	CE_EDIT_GOODIES_LOCK_ALL = 2,
	CE_EDIT_GOODIES_COUNT
};

enum ECeMissionEdit
{
	CE_EDIT_MISSIONS_UNCHANGED = 0,
	CE_EDIT_MISSIONS_UNLOCK_ALL = 1,
	CE_EDIT_MISSIONS_SRANK_COMPLETED = 2,
	CE_EDIT_MISSIONS_SRANK_ALL = 3,
	CE_EDIT_MISSIONS_COUNT
};

enum
{
	CE_EDIT_MAX_KILLS = 999999,
	CE_EDIT_VOLUME_UNKNOWN = -1
};

struct SCeCareerEdits
{
	ECeGoodieEdit	mGoodies;
	ECeMissionEdit	mMissions;
	bool			mKillsChanged[CE_CAREER_KILL_CLASSES];
	int				mKills[CE_CAREER_KILL_CLASSES];
	bool			mSoundChanged;
	int				mSoundPercent;
	bool			mMusicChanged;
	int				mMusicPercent;
	bool			mInvincibleChanged[2];
	bool			mInvincible[2];
};

namespace ce_career_edit_detail
{
enum
{
	kPendingGoodiesOffset = 0x0002,		// CCareer::mPendingExtraGoodies (4 bytes, immediately after 2-byte version)
	kCareerInProgressOffset = 0x248A	// CCareer::mCareerInProgress (4 bytes, immediately before mSoundVolume)
};

inline void	PutU32(unsigned char *p, unsigned value)
{
	p[0] = (unsigned char)(value & 0xffu);
	p[1] = (unsigned char)((value >> 8) & 0xffu);
	p[2] = (unsigned char)((value >> 16) & 0xffu);
	p[3] = (unsigned char)((value >> 24) & 0xffu);
}

inline void	PutF32(unsigned char *p, float value)
{
	unsigned bits = 0;
	memcpy(&bits, &value, sizeof(bits));
	PutU32(p, bits);
}
}

inline int	CeCareerEditClampKills(int value)
{
	if (value < 0)
		return 0;
	if (value > CE_EDIT_MAX_KILLS)
		return CE_EDIT_MAX_KILLS;
	return value;
}

inline int	CeCareerEditStepKills(int current, int delta)
{
	const int base = CeCareerEditClampKills(current);
	if (delta > 0)
		return CeCareerEditClampKills(base + 100);
	if (delta < 0)
		return CeCareerEditClampKills(base - 100);
	return base;
}

inline int	CeCareerEditCycleKillsPreset(int current)
{
	static const int kPresets[] = {0, 100, 500, 1000, 5000, 10000, 100000, CE_EDIT_MAX_KILLS};
	const int clamped = CeCareerEditClampKills(current);
	for (int i = 0; i < int(sizeof(kPresets) / sizeof(kPresets[0])); ++i)
		if (kPresets[i] > clamped)
			return kPresets[i];
	return 0;
}

inline int	CeCareerEditStepVolume(int current, int delta, bool wrap)
{
	if (delta > 0)
	{
		if (current < 0)
			return 80;
		const int next = ((current / 10) + 1) * 10;
		if (next > 100)
			return wrap ? 0 : 100;
		return next;
	}
	if (delta < 0)
	{
		if (current < 0)
			return 0;
		if ((current % 10) != 0)
			return (current / 10) * 10;
		const int prev = current - 10;
		if (prev < 0)
			return wrap ? 100 : 0;
		return prev;
	}
	if (current < 0)
		return CE_EDIT_VOLUME_UNKNOWN;
	return (current > 100) ? 100 : current;
}

inline void	CeCareerEditInit(SCeCareerEdits *edits, const SCeCareerView &view)
{
	if (!edits)
		return;
	memset(edits, 0, sizeof(*edits));
	edits->mGoodies = CE_EDIT_GOODIES_UNCHANGED;
	edits->mMissions = CE_EDIT_MISSIONS_UNCHANGED;
	for (int k = 0; k < CE_CAREER_KILL_CLASSES; ++k)
	{
		edits->mKills[k] = CeCareerEditClampKills(view.mKills[k]);
		edits->mKillsChanged[k] = false;
	}
	edits->mSoundPercent = CeStoredVolumePercent(view.mSoundBits);
	edits->mMusicPercent = CeStoredVolumePercent(view.mMusicBits);
	edits->mSoundChanged = false;
	edits->mMusicChanged = false;
	for (int p = 0; p < 2; ++p)
	{
		edits->mInvincible[p] = view.mInvincible[p];
		edits->mInvincibleChanged[p] = false;
	}
}

inline bool	CeCareerEditHasChanges(const SCeCareerEdits &edits)
{
	if ((edits.mGoodies != CE_EDIT_GOODIES_UNCHANGED) || (edits.mMissions != CE_EDIT_MISSIONS_UNCHANGED))
		return true;
	for (int k = 0; k < CE_CAREER_KILL_CLASSES; ++k)
		if (edits.mKillsChanged[k])
			return true;
	if (edits.mSoundChanged || edits.mMusicChanged)
		return true;
	return edits.mInvincibleChanged[0] || edits.mInvincibleChanged[1];
}

// Applies edits onto a copy of orig into out (10,004 bytes). Returns false if orig is invalid,
// an edit option is out of range, or the transformed buffer fails CeCareerViewBuild / Plausible.
inline bool	CeCareerEditApply(const unsigned char *orig, int size, const SCeCareerEdits &edits,
							  unsigned char *out, SCeCareerView *viewOut = 0)
{
	using namespace ce_career_view_detail;
	using namespace ce_career_edit_detail;

	if (viewOut)
		memset(viewOut, 0, sizeof(*viewOut));
	if (!orig || !out || (size != CE_CAREER_SIZE))
		return false;
	if ((int(edits.mGoodies) < 0) || (int(edits.mGoodies) >= int(CE_EDIT_GOODIES_COUNT)))
		return false;
	if ((int(edits.mMissions) < 0) || (int(edits.mMissions) >= int(CE_EDIT_MISSIONS_COUNT)))
		return false;

	SCeCareerView before;
	if (!CeCareerViewBuild(orig, size, &before) || !CeCareerViewPlausible(before))
		return false;

	memmove(out, orig, CE_CAREER_SIZE);

	if (edits.mGoodies == CE_EDIT_GOODIES_UNLOCK_ALL)
	{
		for (int g = 0; g < CE_CAREER_GOODIE_SLOTS; ++g)
			if (CeGoodieShown(g))
				PutU32(out + kGoodieOffset + g * 4, unsigned(CE_GOODIE_VIEWED));
		PutU32(out + kPendingGoodiesOffset, 0u);
	}
	else if (edits.mGoodies == CE_EDIT_GOODIES_LOCK_ALL)
	{
		for (int g = 0; g < CE_CAREER_GOODIE_SLOTS; ++g)
			if (CeGoodieShown(g))
				PutU32(out + kGoodieOffset + g * 4, unsigned(CE_GOODIE_LOCKED));
		PutU32(out + kPendingGoodiesOffset, 0u);
	}

	if (edits.mMissions == CE_EDIT_MISSIONS_UNLOCK_ALL)
	{
		for (int n = 0; n < CE_CAREER_MISSIONS; ++n)
		{
			unsigned char *mission = out + kMissionOffset + n * kMissionStride;
			if (!U32(mission + kMissionWorld))
				continue;
			const int links[2] = {int(U32(mission + kMissionLower)), int(U32(mission + kMissionHigher))};
			for (int l = 0; l < 2; ++l)
			{
				if ((links[l] < 0) || (links[l] >= CE_CAREER_ROUTES))
					continue;
				unsigned char *route = out + kRouteOffset + links[l] * kRouteStride;
				const unsigned to = U32(route + 4);
				if ((to >= unsigned(CE_CAREER_MISSIONS)) ||
					!U32(out + kMissionOffset + int(to) * kMissionStride + kMissionWorld))
					continue;
				PutU32(route, 1u);
			}
		}
	}
	else if (edits.mMissions == CE_EDIT_MISSIONS_SRANK_COMPLETED)
	{
		for (int n = 0; n < CE_CAREER_MISSIONS; ++n)
		{
			unsigned char *mission = out + kMissionOffset + n * kMissionStride;
			if (!U32(mission + kMissionWorld))
				continue;
			if (U32(mission + kMissionComplete))
				PutF32(mission + kMissionRank, 1.0f);
		}
	}
	else if (edits.mMissions == CE_EDIT_MISSIONS_SRANK_ALL)
	{
		for (int n = 0; n < CE_CAREER_MISSIONS; ++n)
		{
			unsigned char *mission = out + kMissionOffset + n * kMissionStride;
			if (!U32(mission + kMissionWorld))
				continue;
			PutU32(mission + kMissionComplete, 1u);
			PutF32(mission + kMissionRank, 1.0f);
			const int links[2] = {int(U32(mission + kMissionLower)), int(U32(mission + kMissionHigher))};
			for (int l = 0; l < 2; ++l)
			{
				if ((links[l] < 0) || (links[l] >= CE_CAREER_ROUTES))
					continue;
				unsigned char *route = out + kRouteOffset + links[l] * kRouteStride;
				const unsigned to = U32(route + 4);
				if ((to >= unsigned(CE_CAREER_MISSIONS)) ||
					!U32(out + kMissionOffset + int(to) * kMissionStride + kMissionWorld))
					continue;
				PutU32(route, 1u);
			}
		}
		PutU32(out + kCareerInProgressOffset, 1u);
	}

	for (int k = 0; k < CE_CAREER_KILL_CLASSES; ++k)
	{
		if (!edits.mKillsChanged[k])
			continue;
		unsigned char *slot = out + kKillsOffset + k * 4;
		const unsigned topByte = U32(slot) & 0xff000000u;
		const unsigned count = unsigned(CeCareerEditClampKills(edits.mKills[k])) & 0x00ffffffu;
		PutU32(slot, topByte | count);
	}

	if (edits.mSoundChanged)
	{
		int pct = edits.mSoundPercent;
		if (pct < 0)
			pct = 0;
		if (pct > 100)
			pct = 100;
		PutF32(out + kSoundOffset, float(pct) / 100.f);
	}
	if (edits.mMusicChanged)
	{
		int pct = edits.mMusicPercent;
		if (pct < 0)
			pct = 0;
		if (pct > 100)
			pct = 100;
		PutF32(out + kMusicOffset, float(pct) / 100.f);
	}
	for (int p = 0; p < 2; ++p)
	{
		if (!edits.mInvincibleChanged[p])
			continue;
		PutU32(out + kInvincibleOffset + p * 4, edits.mInvincible[p] ? 1u : 0u);
	}

	SCeCareerView after;
	if (!CeCareerViewBuild(out, CE_CAREER_SIZE, &after) || !CeCareerViewPlausible(after))
		return false;
	if (viewOut)
		*viewOut = after;
	return true;
}

#endif
