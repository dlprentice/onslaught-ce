// SPDX-License-Identifier: GPL-3.0-only
// Host driver exercising port/game/ce_career_edit.h without game or D3D headers.
#include "ce_career_edit.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int	RunApply(const char *inPath, const char *outPath, int argc, char **argv)
{
	FILE *f = std::fopen(inPath, "rb");
	if (!f)
		return 2;
	std::vector<unsigned char> orig(1 << 16);
	const size_t size = std::fread(orig.data(), 1, orig.size(), f);
	std::fclose(f);

	SCeCareerView before{};
	SCeCareerEdits edits{};
	if (CeCareerViewBuild(orig.data(), int(size), &before))
		CeCareerEditInit(&edits, before);

	for (int i = 0; i < argc; ++i)
	{
		std::string arg(argv[i]);
		const size_t eq = arg.find('=');
		if (eq == std::string::npos)
			return 2;
		const std::string key = arg.substr(0, eq);
		const int val = std::atoi(arg.c_str() + eq + 1);
		if (key == "goodies")
			edits.mGoodies = ECeGoodieEdit(val);
		else if (key == "missions")
			edits.mMissions = ECeMissionEdit(val);
		else if (key.size() == 5 && key.compare(0, 4, "kill") == 0 && key[4] >= '0' && key[4] <= '4')
		{
			const int idx = key[4] - '0';
			edits.mKills[idx] = val;
			edits.mKillsChanged[idx] = true;
		}
		else if (key == "sound")
		{
			edits.mSoundPercent = val;
			edits.mSoundChanged = true;
		}
		else if (key == "music")
		{
			edits.mMusicPercent = val;
			edits.mMusicChanged = true;
		}
		else if (key == "p1")
		{
			edits.mInvincible[0] = val != 0;
			edits.mInvincibleChanged[0] = true;
		}
		else if (key == "p2")
		{
			edits.mInvincible[1] = val != 0;
			edits.mInvincibleChanged[1] = true;
		}
		else
			return 2;
	}

	unsigned char out[CE_CAREER_SIZE];
	std::memset(out, 0xA5, sizeof(out));
	SCeCareerView after{};
	if (!CeCareerEditApply(orig.data(), int(size), edits, out, &after))
	{
		std::printf("refused\n");
		return 0;
	}

	FILE *wf = std::fopen(outPath, "wb");
	if (!wf)
		return 3;
	const size_t wrote = std::fwrite(out, 1, CE_CAREER_SIZE, wf);
	std::fclose(wf);
	if (wrote != CE_CAREER_SIZE)
		return 3;

	int openCount = 0, lockedCount = 0;
	for (int n = 0; n < after.mMissionCount; ++n)
	{
		if (after.mMissions[n].mState == CE_MISSION_OPEN)
			++openCount;
		else if (after.mMissions[n].mState == CE_MISSION_NOT_YET_OPEN)
			++lockedCount;
	}
	std::printf("ok\n");
	std::printf("changed %d\n", CeCareerEditHasChanges(edits) ? 1 : 0);
	std::printf("missions %d complete %d open %d locked %d rank_s %d\n",
				after.mMissionCount, after.mMissionsComplete, openCount, lockedCount, after.mRankS);
	std::printf("goodies %d all %d\n", after.mGoodiesFound, after.mGoodiesAll);
	std::printf("kills %d %d %d %d %d\n",
				after.mKills[0], after.mKills[1], after.mKills[2], after.mKills[3], after.mKills[4]);
	std::printf("settings %d %d %d %d\n",
				CeStoredVolumePercent(after.mSoundBits), CeStoredVolumePercent(after.mMusicBits),
				int(after.mInvincible[0]), int(after.mInvincible[1]));
	return 0;
}

static int	RunSteps()
{
	std::printf("clamp %d %d %d %d\n",
				CeCareerEditClampKills(-50), CeCareerEditClampKills(0),
				CeCareerEditClampKills(12345), CeCareerEditClampKills(5000000));
	std::printf("step %d %d %d %d\n",
				CeCareerEditStepKills(50, -1), CeCareerEditStepKills(350, 1),
				CeCareerEditStepKills(450, -1), CeCareerEditStepKills(999950, 1));
	std::printf("preset %d %d %d %d %d\n",
				CeCareerEditCycleKillsPreset(0), CeCareerEditCycleKillsPreset(100),
				CeCareerEditCycleKillsPreset(750), CeCareerEditCycleKillsPreset(100000),
				CeCareerEditCycleKillsPreset(999999));
	std::printf("vol %d %d %d %d %d %d %d\n",
				CeCareerEditStepVolume(-1, 1, false), CeCareerEditStepVolume(-1, -1, false),
				CeCareerEditStepVolume(85, 1, false), CeCareerEditStepVolume(85, -1, false),
				CeCareerEditStepVolume(100, 1, false), CeCareerEditStepVolume(100, 1, true),
				CeCareerEditStepVolume(0, -1, true));
	return 0;
}

int	main(int argc, char **argv)
{
	if (argc == 2 && !std::strcmp(argv[1], "steps"))
		return RunSteps();
	if (argc >= 4 && !std::strcmp(argv[1], "apply"))
		return RunApply(argv[2], argv[3], argc - 4, argv + 4);
	return 2;
}
