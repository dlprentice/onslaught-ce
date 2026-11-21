// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_catalogue_testkit.cpp: the test kit's state key for the Catalogue's sounds (ce_testkit.h;
// port/journeys/README.md). Compiled only by build.py --test-kit; it reads ce_catalogue_sounds.h and changes nothing.
#include "ce_catalogue_sounds.h"
#include "../platform/common/ce_testkit.h"
#include <stdio.h>

namespace
{
bool	StatePlaying(char *out, size_t size)
{
	snprintf(out, size, "%s", PortCeCatalogueSoundsPlaying() ? "yes" : "no");
	return true;
}
}

CE_TEST_STATE("catalogue.playing", StatePlaying);
