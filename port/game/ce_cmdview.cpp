// SPDX-License-Identifier: GPL-3.0-only
// The Commander live view's read-only friendly unit mark projection and font height fallback.
//
// Commander renders the native 3D scene inside CeRenderFence (ce_render_fence.h). This file projects friendly squad members
// onto the captured world view for picking and ground rings, reports the small font's height as a fallback, and tracks frame
// counts for test-build isolation proofs (ce_cmdview_audit.cpp):
//
// - It READS the world and font only (listed in READS below; port/tools/ce_commander_tests.py holds this file to that list).
// - It performs no rendering and calls no drawing, allocation, random or clock routine of the game.
// - Its memory is the C library's (realloc/free), never the game's heaps.
#ifdef CE_VERSION

#include "common.h"
#include "world.h"
#include "thing.h"
#include "unit.h"
#include "Platform.h"
#include "ce_cmdview.h"
#include "ce_cmdorders.h"
#include "ce_commander.h"

#include <stdlib.h>

// READS (the whole of what this file takes from the game; each only reads):
//   WORLD           GetThingNB, walked node by node (the set's own iterator is state of the game's and is not used)
//   a thing         GetThingType, GetFlags, GetPos; a unit's GetAllegiance and GetSquad
//   PLATFORM        Font(FONT_SMALL): GetHeight
// The memory is the C library's (operator new in a game file is the game's allocator: none is used here).

#if defined(CE_COMMANDER_TEST) && defined(__linux__)
// A test build keeps this file's own records in sections of their own, which the footprint audit leaves out (ce_cmdview_audit.cpp).
#pragma clang section bss = "cecmdb" data = "cecmdd"
#endif

namespace
{
// GenericSPtrSet's storage (SPtrSet.h): first node, last, the iterator, size; a node is item, next (as updatedump.cpp reads it).
struct NodeView { void *item; NodeView *next; };
struct SetView { NodeView *first, *last, *iterator; int size; };

CeWorldViewSample gView;
double gCombined[16];
CeCmdViewUnitMark *gUnitMarks;
unsigned gUnitMarkCount, gUnitMarkRoom;

#ifdef CE_COMMANDER_TEST
CeCmdViewStats gStats;
#endif

// A world point on the window, with how many pixels a world unit takes there. False behind the camera or far outside the window.
bool Place(float wx, float wy, float wz, float *px, float *py, float *scale)
{
	const double point[4] = {wx, wy, wz, 1}; double clip[4];
	CeViewDetail::Transform(gCombined, point, clip);
	if (!(clip[3] > 1e-3)) return false;
	const double nx = clip[0] / clip[3], ny = clip[1] / clip[3];
	if (!(nx > -1.5 && nx < 1.5 && ny > -1.5 && ny < 1.5)) return false;
	*px = gView.viewport.x + float(nx + 1) * 0.5f * gView.viewport.width;
	*py = gView.viewport.y + float(1 - ny) * 0.5f * gView.viewport.height;
	*scale = float(0.5 * gView.viewport.width * gView.projection[0] / clip[3]);
	return true;
}

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Projection layer order, lowest first: what stands, what drives and walks, and what flies.
enum { kLayerStanding, kLayerGround, kLayerAir, kLayers };

int LayerOf(ULONG type)
{
	if (type & THING_TYPE_AIR_UNIT) return kLayerAir;
	if (type & (THING_TYPE_BUILDING | THING_TYPE_EMPLACEMENT)) return kLayerStanding;
	return kLayerGround;
}

void KeepUnitMark(CUnit *unit, int squad, float x, float y, float radius, const FVector &pos, float worldRadius, float ppu)
{
	if (gUnitMarkCount == gUnitMarkRoom)
	{
		if (gUnitMarkRoom > (~0u / sizeof(CeCmdViewUnitMark)) / 2) return;
		const unsigned room = gUnitMarkRoom ? gUnitMarkRoom * 2 : 64;
		void *storage = realloc(gUnitMarks, room * sizeof(CeCmdViewUnitMark));
		if (!storage) return;
		gUnitMarks = static_cast<CeCmdViewUnitMark *>(storage);
		gUnitMarkRoom = room;
	}
	CeCommandLifetimeKey key = {};
	if (!CeCmdUnitVisibleKey(unit, &key)) return;
	gUnitMarks[gUnitMarkCount++] = {squad, x, y, radius, key, pos.X, pos.Y, pos.Z, worldRadius, ppu};
}

void CollectUnitMark(CThing *thing, int friendly, const void *pilot, int layer)
{
	const ULONG type = thing->GetThingType();
	const short flags = thing->GetFlags();
	if (flags & (TF_INVISIBLE | TF_DECLARED_SHUTDOWN)) return;
	if (type & THING_TYPE_EXPLOSION) return;
	if (!(type & THING_TYPE_UNIT) || (type & THING_TYPE_COMPONENT) || (flags & TF_DYING)) return;
	if ((const void *)thing == pilot) return;
	if (LayerOf(type) != layer) return;
	CUnit *unit = dynamic_cast<CUnit *>(thing);
	if (!unit || (int)unit->GetAllegiance() != friendly) return;
	const int squad = CeCmdDrawnSquad(unit->GetSquad());
	if (squad < 0) return;
	const FVector &pos = thing->GetPos();
	float x, y, scale;
	if (!Place(pos.X, pos.Y, pos.Z, &x, &y, &scale)) return;
	float markRadius, worldRadius;
	if (type & THING_TYPE_AIR_UNIT)
	{
		markRadius = Clamp(scale * 3.5f, 6.f, 13.f);
		worldRadius = 1.6f;
	}
	else if (type & (THING_TYPE_BUILDING | THING_TYPE_EMPLACEMENT))
	{
		markRadius = Clamp(scale * 4.f, 4.f, 12.f);
		worldRadius = 4.f;	// what stands: the building's own footprint is larger; the ring marks its middle
	}
	else if (type & THING_TYPE_INFANTRY)
	{
		markRadius = Clamp(scale * 1.2f, 2.f, 4.f);
		worldRadius = 0.6f;
	}
	else
	{
		markRadius = Clamp(scale * ((type & THING_TYPE_MECH) ? 4.5f : 3.2f), 5.f, 13.f);
		worldRadius = (type & THING_TYPE_MECH) ? 2.f : 1.4f;	// about a tank's half length (measured on 851's frames)
	}
	KeepUnitMark(unit, squad, x, y, markRadius, pos, worldRadius, scale);
}
}

void CeCmdViewCollectUnits(const CeWorldViewSample& sample, int friendlyAllegiance, const void *pilot)
{
	gUnitMarkCount = 0;
	if (!CeViewDetail::Valid(sample.viewport) || !CeViewDetail::Combined(sample.view, sample.projection, gCombined)) return;
	gView = sample;
	const SetView *things = (const SetView *)&WORLD.GetThingNB();
	for (int layer = 0; layer < kLayers; ++layer)
	{
		int guard = 0;
		for (const NodeView *n = things->first; n && guard < 65536; n = n->next, ++guard)
			if (n->item) CollectUnitMark((CThing *)n->item, friendlyAllegiance, pilot, layer);
	}
}

const CeCmdViewUnitMark *CeCmdViewUnitMarks(unsigned &count)
{
	count = gUnitMarkCount;
	return gUnitMarks;
}

float CeCmdViewTextHeight()
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	return font ? float(font->GetHeight()) : 0.f;
}

void CeCmdViewRelease()
{
	free(gUnitMarks); gUnitMarks = NULL; gUnitMarkCount = gUnitMarkRoom = 0;
}

#ifdef CE_COMMANDER_TEST
void CeCmdViewGetStats(CeCmdViewStats *out) { *out = gStats; }
void CeCmdViewCountFrame() { ++gStats.frames; }
#endif

#endif
