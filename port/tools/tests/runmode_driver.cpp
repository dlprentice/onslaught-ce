// SPDX-License-Identifier: GPL-3.0-only
// Host driver for port/platform/common/runmode.cpp (runmode_tests.py): prints the run mode this environment gives and whether
// the online pages are offered. Built twice: as a player's build (no test hooks, as window.cpp leaves them) and with
// -DKIT_BUILD, where the hooks answer as the test kit's own file does (a journey drives the run when BEA_PORT_VERBS is set).
#include "../../platform/common/runmode.h"
#include "../../platform/common/testhooks.h"
#include <stdio.h>
#include <stdlib.h>

PortTestHooks *gPortTestHooks = nullptr;
bool *PortWindowFakeHeld() { return nullptr; }

#ifdef KIT_BUILD
static bool Journey() { const char *v = getenv("BEA_PORT_VERBS"); return v && *v; }
static PortTestHooks sHooks = {nullptr, nullptr, nullptr, nullptr, nullptr, Journey};
#endif

int main()
{
#ifdef KIT_BUILD
	gPortTestHooks = &sHooks;
#endif
	printf("%s %d\n", PortRunModeName(PortRunModeNow()), PortRunOffersNetworkPages() ? 1 : 0);
	return 0;
}
