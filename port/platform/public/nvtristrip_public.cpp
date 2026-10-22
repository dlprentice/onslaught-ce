// SPDX-License-Identifier: GPL-3.0-only
// port/platform/public/nvtristrip_public.cpp: the public build's stand-in for NvTriStrip (build.py --public), whose
// repository states no licence, so it cannot ship (port/PLAN.md). The game's mesh and landscape index buffers come
// precomputed in the retail resources (PMIB and every deserialized mesh part), so with retail data the game never
// strips at run time: GenerateStrips is reached only by building resources from raw .msh files, which retail data does
// not ship. Rather than produce different index buffers there, it stops with a message. The setters keep nothing.
#include <stdio.h>
#include <stdlib.h>

struct PrimitiveGroup;

void SetCacheSize(const unsigned int) {}
void SetStitchStrips(const bool) {}
void SetMinStripSize(const unsigned int) {}
void SetListsOnly(const bool) {}

void GenerateStrips(const unsigned short *, const unsigned int, PrimitiveGroup **, unsigned short *)
{
	fprintf(stderr, "bea: this build has no triangle-strip generator (NvTriStrip is not part of the public build); "
			"raw .msh meshes cannot be built into resources. Use the retail game data.\n");
	fflush(stderr);
	_Exit(70);
}
