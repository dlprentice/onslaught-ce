// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_TERRAIN_RESERVE_H
#define CE_TERRAIN_RESERVE_H

class CDXPatchManager;
struct CDXPatchList;
class CLandscapeVB;

// The original pool objects and counts remain untouched. Extra buffers are
// owned here and are offered only after the original allocator is exhausted.
// Init records the original pools' shape and installs the readiness check; the
// buffers are created later, once, at the first frame that asks for Extended.
void CeTerrainReserveInit(CDXPatchManager *manager);
void CeTerrainReserveShutdown();
void CeTerrainReserveReset(const CDXPatchList *list);
CLandscapeVB *CeTerrainReserveAllocate(int level, unsigned short slot);
bool CeTerrainReserveOwns(const void *patch);
void CeTerrainReserveReleased(const void *patch);

#endif
