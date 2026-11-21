// SPDX-License-Identifier: GPL-3.0-only
// A stand-in for the game's career (see common.h here): the same members the Service Record's cross-check asks of it, laid out as a career file
// lays them out after its version word, answering from the bytes. gSkew makes one answer wrong on purpose, so that the test can show the
// cross-check notices each kind of disagreement.
#ifndef CE_FAKE_CAREER_H
#define CE_FAKE_CAREER_H
#include "common.h"
#include <string.h>
#define MAX_NODES 100
#define MAX_LINKS 200
enum { CN_NONE = 0, CN_COMPLETE = 1 };
enum EKilledType { TK_AIRCRAFT, TK_VEHICLES, TK_EMPLACEMENTS, TK_INFANTY, TK_MECHS };
enum { SKEW_NONE, SKEW_GOODIE_COUNT, SKEW_KILLS, SKEW_GOODIE_STATE, SKEW_WORLD, SKEW_COMPLETE, SKEW_RANK, SKEW_EXTRA_MISSION, SKEW_LINK, SKEW_LAST_HIDDEN, SKEW_SOUND, SKEW_MUSIC, SKEW_PLAYER_ONE, SKEW_PLAYER_TWO, SKEW_COUNT };
inline int	&CeFakeSkew() { static int skew; return skew; }

#pragma pack(push, 1)
struct CCareerNode
{
	int		mIsStartOfNewIsland, mComplete, mLowerLink, mHigherLink, mWorldNumber;
	int		mPad[9];
	int		mNumAttempts;
	float	mRanking;
};
struct CCareerNodeLink { int mLinkType, mToNode; };
struct CGoodie { int mState; };

class CCareer
{
public:
	static int	SizeOfSaveGame() { return 10004; }
	CCareerNode	*GetNode(int n)
	{
		static CCareerNode shown;
		shown = mNode[n];
		const int skew = CeFakeSkew();
		if ((skew == SKEW_WORLD) && (n == 2) && shown.mWorldNumber)
			shown.mWorldNumber++;
		if ((skew == SKEW_COMPLETE) && (n == 0) && shown.mWorldNumber)
			shown.mComplete = !shown.mComplete;
		if ((skew == SKEW_LAST_HIDDEN) && (shown.mWorldNumber == 800))
			shown.mWorldNumber = 0;
		if ((skew == SKEW_EXTRA_MISSION) && (n == 99) && !shown.mWorldNumber)
			shown.mWorldNumber = 999;
		return &shown;
	}
	CCareerNodeLink	*GetLink(int n)
	{
		static CCareerNodeLink shown;
		shown = mLink[n];
		if (CeFakeSkew() == SKEW_LINK)
			shown.mLinkType = !shown.mLinkType;
		return &shown;
	}
	int		GetGoodieState(int g)
	{
		return mGoodie[g].mState + (((CeFakeSkew() == SKEW_GOODIE_STATE) && (g == 5)) ? 1 : 0);
	}
	int		CountGoodies()
	{
		int n = 0;
		for (int g = 0; g < 300; g++)
			if (mGoodie[g].mState >= 2)
				n++;
		return n + ((CeFakeSkew() == SKEW_GOODIE_COUNT) ? 1 : 0);
	}
	unsigned StoredWord(int at) const
	{
		const unsigned char *p = reinterpret_cast<const unsigned char *>(this) + at;
		return unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16 | unsigned(p[3]) << 24;
	}
	float SavedVolume(int at, int skew) const
	{
		unsigned bits = StoredWord(at) ^ (CeFakeSkew() == skew ? 1u : 0u);
		float volume;
		memcpy(&volume, &bits, sizeof(volume));
		return volume;
	}
	float GetSoundVolume() const { return SavedVolume(9356, SKEW_SOUND); }
	float GetMusicVolume() const { return SavedVolume(9360, SKEW_MUSIC); }
	int GetIsGod(int player) const
	{
		const bool stored = StoredWord(player ? 9368 : 9364) != 0;
		return stored != (CeFakeSkew() == (player ? SKEW_PLAYER_TWO : SKEW_PLAYER_ONE));
	}
	int		GetNumKilled(EKilledType type)
	{
		return int(mKilled[int(type)] & 0xffffff) + (((CeFakeSkew() == SKEW_KILLS) && (type == TK_MECHS)) ? 1 : 0);
	}
	WCHAR	GetGradeFromRanking(float f)
	{
		if (CeFakeSkew() == SKEW_RANK)
			return 'Q';
		if (f == 1.f)
			return 'S';
		if (f <= 0.f)
			return 'E';
		return WCHAR('D' - int(f * 4.f));
	}
	int				mGoodiesToAnnounce;
	CCareerNode		mNode[MAX_NODES];
	CCareerNodeLink	mLink[MAX_LINKS];
	CGoodie			mGoodie[300];
	unsigned		mKilled[5];
	unsigned char	mSettings[10002 - 4 - 64 * MAX_NODES - 8 * MAX_LINKS - 4 * 300 - 20];
};
#pragma pack(pop)
static_assert(sizeof(CCareerNode) == 64, "a mission record is 64 bytes");
static_assert(sizeof(CCareer) == 10002, "a career is 10,002 bytes after its version word");
#endif
