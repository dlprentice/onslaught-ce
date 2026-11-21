// SPDX-License-Identifier: GPL-3.0-only
// port/tools/ce_project_empty_variable_driver.cpp: what the actual port/game/ce_project.cpp answers at each place a start asks it,
// with BEA_CE_PROJECT as the caller's environment has it. Authored here: the two folder names the module asks the platform layer for.
// Usage: driver GAME_DIR USER_DIR   (two existing folders the caller owns, neither inside the other)   prints one line per answer.
#include "ce_project.h"
#include <cstdio>
#include <cstdlib>

static const char *gGame = "", *gUser = "";
const char *PortGameDir() { return gGame; }
const char *PortUserDir() { return gUser; }

int main(int argc, char **argv)
{
	if (argc != 3)
		return 2;
	gGame = argv[1];
	gUser = argv[2];
	const char *value = std::getenv("BEA_CE_PROJECT");
	std::printf("variable %s\n", !value ? "unset" : !*value ? "empty" : "set");
	std::printf("requested %d\n", PortCeProjectRequested() ? 1 : 0);											// port/platform/linux/paths.cpp and main.cpp, CSystem::Run
	std::printf("career allowed %d\n", PortCeProjectCareerAllowed() ? 1 : 0);								// the save gates of patches 0139 and 0202
	std::printf("authoring allowed %d\n", PortCeAuthoringSessionAllowed() ? 1 : 0);							// the map editor's and the museum visit's entry
	std::printf("preflight without explicit folders %d\n", PortCeProjectProfilePreflight(NULL, NULL, 4096, 4096) ? 1 : 0);	// a start from the shell
	std::printf("preflight with explicit folders %d\n", PortCeProjectProfilePreflight(gGame, gUser, 4096, 4096) ? 1 : 0);
	std::printf("launch level %d\n", PortCeProjectLaunchLevel());											// CSystem::Run: below 0 ends the run
	std::printf("exit status %d\n", PortCeProjectRequested() ? PortCeProjectExitStatus() : 0);				// the Linux entry point's
	return 0;
}
