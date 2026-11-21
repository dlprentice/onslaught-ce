// SPDX-License-Identifier: GPL-3.0-only
// The Service Record's reading of a career as the game does it (port/game/ce_career_view.cpp), run on stand-ins for the game's career
// and memory card (tests/career_fake/), for port/tools/ce_career_cross_tests.py.
//   ce_career_cross_driver FILE SKEW     reads the career file through PortCeCareerRead; SKEW makes one of the stand-in's answers wrong
// Prints "read 1 missions N" or "read 0" and leaves the cross-check's reason on stderr.
#include "common.h"
#include "frontend.h"
#include "Career.h"
#include "MemoryCard.h"
#include "ce_career_view.h"

CFrontEnd	FRONTEND;
CMemoryCard	MEMORYCARD;
bool	PortCeMenus() { return true; }
bool	PortCeProjectCareerAllowed() noexcept { return true; }
bool	PortDevLog() { return false; }

bool	PortCeCareerRead(const WCHAR *name, SCeCareerView *view);

int main(int argc, char **argv)
{
	if (argc != 3)
		return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f)
		return 2;
	MEMORYCARD.mSize = int(fread(MEMORYCARD.mBytes, 1, sizeof(MEMORYCARD.mBytes), f));
	fclose(f);
	CeFakeSkew() = atoi(argv[2]);
	static SCeCareerView view;
	static const WCHAR name[] = {'A', 0};
	if (PortCeCareerRead(name, &view))
		printf("read 1 missions %d complete %d goodies %d\n", view.mMissionCount, view.mMissionsComplete, view.mGoodiesFound);
	else
		printf("read 0\n");
	return 0;
}
