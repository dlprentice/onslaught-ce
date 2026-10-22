// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/nvtristrip_bridge.cpp: the game calls NvTriStrip's older five-function API (src/NvTriStrip.h:
// GenerateStrips returns nothing and takes four arguments); the pinned later source (deps.lock, turbulenz c40ad04c,
// private builds only) returns bool and takes a fifth, defaulted. This defines the game's form over it.
#include "NvTriStrip.h"

void GenerateStrips(const unsigned short *in_indices, const unsigned int in_numIndices, PrimitiveGroup **primGroups,
					unsigned short *numGroups)
{
	GenerateStrips(in_indices, in_numIndices, primGroups, numGroups, false);
}
