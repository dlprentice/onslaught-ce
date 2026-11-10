// SPDX-License-Identifier: GPL-3.0-only
bool PortPathsProjectPreflight();
void PortPathsInit();
const char* PortGameDir();
const char* PortUserDir();
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int SourceStartup()
{
#include "ce_project_profile_start.private.h"
    return 0;
}
int main()
{
    int result = SourceStartup();
    if (result != 0) {
        std::printf("CE_PROJECT_PROFILE: refused before path initialization.\n");
        return result;
    }
    const char* game = std::getenv("BEA_GAME_DIR");
    const char* user = std::getenv("BEA_USER_DIR");
    if ((game && *game && std::strcmp(game, PortGameDir()) != 0) ||
        (user && *user && std::strcmp(user, PortUserDir()) != 0)) {
        std::printf("CE_PROJECT_PROFILE: startup root cache changed the selected spelling.\n");
        return 2;
    }
    std::printf("CE_PROJECT_PROFILE: initialized selected roots.\n");
    return 0;
}
