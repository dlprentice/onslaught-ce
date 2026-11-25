// SPDX-License-Identifier: GPL-3.0-only
// What every Grand Museum renderer does after it gives the device back.
//
// Each of them draws inside a D3DSBT_ALL state block, so the device itself
// returns to what it was. The engine, though, keeps two caches of device state
// (LT's and RENDERINFO's) and neither saw the block being applied: left alone,
// the next engine pass would skip state it believes is already set, and draw
// the game's own models with the museum's cull mode, alpha test, stage 0
// operation or transforms. This puts both caches and the device back in step
// and re-asserts the three states the world renderer takes for granted between
// passes, so a museum pass is symmetrical: what it changed, it restores.
#ifndef CE_MUSEUM_RENDER_STATE_H
#define CE_MUSEUM_RENDER_STATE_H

#ifdef CE_VERSION
#include "ltshell.h"
#include "renderinfo.h"

inline void CeMuseumRestoreEngineRenderState() {
    LT.InvalidateAllStates();
    RENDERINFO.mWorldChanged = true;
    RENDERINFO.mViewChanged = true;
    RENDERINFO.mProjectionChanged = true;
    RENDERINFO.mFogStartChanged = true;
    RENDERINFO.mFogEndChanged = true;
    RENDERINFO.mFogDensityChanged = true;
    RENDERINFO.mFVFChanged = true;
    RENDERINFO.Apply();
    LT.SRS(D3DRS_CULLMODE, D3DCULL_CCW);
    LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
    LT.SetModulate(0);
}
#endif

#endif
