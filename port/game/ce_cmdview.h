// SPDX-License-Identifier: GPL-3.0-only
// The Commander live view's read-only friendly unit mark projection and font height fallback (port/game/ce_cmdview.cpp).
// The live view renders the native 3D scene inside CeRenderFence (ce_render_fence.h); this module only projects friendly
// squad members onto the captured world view for picking and ground rings, and exposes test-build frame stats and audit hooks.
#ifndef PORT_CE_CMDVIEW_H
#define PORT_CE_CMDVIEW_H

#ifdef CE_VERSION

#include "ce_world_view.h"
#include "ce_command_lifetime.h"

// Collect native-view friendly member hits without mutating the game world.
void CeCmdViewCollectUnits(const CeWorldViewSample& sample, int friendlyAllegiance, const void *pilot);
// A friendly unit's mark projected onto the last captured world view: its squad, its window point and pick radius, its identity, and
// (for the map's rings, which stand on the ground) where it is in the world, the world radius its kind is drawn with, and the window pixels
// one world unit spans there.
struct CeCmdViewUnitMark { int squad; float x, y, radius; CeCommandLifetimeKey key; float wx, wy, wz, worldRadius, ppu; };
const CeCmdViewUnitMark *CeCmdViewUnitMarks(unsigned &count);

// The small font's height at scale one (fallback when the map's layout scale is zero). Zero where the font is not loaded.
float CeCmdViewTextHeight();

// Frees the projected unit mark buffer (the view closed, the level ended).
void CeCmdViewRelease();

#ifdef CE_COMMANDER_TEST
// A test build's footprint audit (ce_cmdview_audit.cpp, BEA_PORT_COMMANDER_AUDIT=1): the game's heaps and the executable's data copied before a
// frame of the live view's overlay is drawn and compared after it. The live view may change none of it but its own records.
void CeCmdViewAuditBefore();
void CeCmdViewAuditAfter();
void CeCmdViewAuditReport();
// Frame counter and legacy stat columns kept for the isolation proof's close-note format (ce_commander_isolation_run.py).
struct CeCmdViewStats { unsigned frames, terrainChunks, markers, vertices, texels; bool mixer; };
void CeCmdViewGetStats(CeCmdViewStats *out);
// One frame of the live view drawn over the game's own render of the battle from the Commander's camera (the native picture): counted so the
// isolation proof can see that the view was open (ce_commander_isolation_run.py).
void CeCmdViewCountFrame();
#endif

#endif

#endif
