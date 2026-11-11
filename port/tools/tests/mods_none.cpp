// SPDX-License-Identifier: GPL-3.0-only
// port/tools/tests/mods_none.cpp: the mod layer's entry points (platform/common/mods.h) with no mod ever active. A fixture
// that compiles the real file layer (files.cpp, mmio.cpp) without exercising mods links this in place of
// platform/common/mods.cpp, which needs ce.cpp, zlib and the SHA-256 code: every read then takes the user's or the game's
// own file, as it does with no mod enabled.
#include "../../platform/common/mods.h"

void PortModsInit() {}
int PortModsActive() { return 0; }
const char *PortModsFence() { return NULL; }
#ifdef _WIN32
void *PortModOpenRead(const char *) { return NULL; }
#else
int PortModOpenRead(const char *) { return -1; }
#endif
