// SPDX-License-Identifier: GPL-3.0-only
// The render fence (ce_render_fence.h). The kinds kept follow ce_network_world.cpp's lockstep list (KeepController, LockstepEndTick).
#include "ce_render_fence.h"

#ifdef CE_VERSION

#include "common.h"
#include "world.h"
#include "thing.h"
#include "component.h"
#include "BattleEngine.h"
#include "MCBattleEngine.h"
#include "mcbuggy.h"
#include "MCTentacle.h"
#include "MCComponent.h"
#include "MCCannon.h"
#include "MCGroundAttack.h"
#include "Motioncontroller.h"
#include "DestructableSegmentsController.h"
#include "mesh.h"
#include "rtmesh.h"
#include "RenderThing.h"
#include "meshpose.h"
#include "shadows.h"
#include "engine.h"
#include "landscape.h"

#include <stdlib.h>
#include <string.h>

#if defined(CE_COMMANDER_TEST) && defined(__linux__)
// A test build keeps the fence's own records with the Commander's, which the live view's footprint audit leaves out (ce_cmdview_audit.cpp).
#pragma clang section bss = "cecmdb" data = "cecmdd"
#endif

namespace {
struct Region { void* at; size_t size; };
Region* gRegions = NULL;
unsigned char* gBytes = NULL;
size_t gRegionCount = 0, gRegionCap = 0, gByteCount = 0, gByteCap = 0;
bool gOverflow = false;

void Keep(void* at, size_t size)
{
    if (!at || !size || gOverflow) return;
    if (gRegionCount == gRegionCap) {
        const size_t cap = gRegionCap ? gRegionCap * 2 : 4096;
        Region* r = (Region*)realloc(gRegions, cap * sizeof *r);
        if (!r) { gOverflow = true; return; }
        gRegions = r; gRegionCap = cap;
    }
    if (gByteCount + size > gByteCap) {
        size_t cap = gByteCap ? gByteCap : (size_t)1 << 20;
        while (cap < gByteCount + size) cap *= 2;
        unsigned char* b = (unsigned char*)realloc(gBytes, cap);
        if (!b) { gOverflow = true; return; }
        gBytes = b; gByteCap = cap;
    }
    gRegions[gRegionCount++] = {at, size};
    memcpy(gBytes + gByteCount, at, size);
    gByteCount += size;
}

int ThingParts(CThing* t)
{
    CRenderThing* rt = t->GetRenderThing();
    CMesh* mesh = rt ? rt->GetRTMesh() : NULL;
    return mesh ? mesh->mNumParts : 0;
}

void KeepParts(FMatrix* ori, FVector* pos, float* frame, FMatrix* oldOri, FVector* oldPos, float* oldFrame, int parts)
{
    if (!pos || parts <= 0) return;
    Keep(ori, parts * sizeof(FMatrix)); Keep(pos, parts * sizeof(FVector)); Keep(frame, parts * sizeof(float));
    Keep(oldOri, parts * sizeof(FMatrix)); Keep(oldPos, parts * sizeof(FVector)); Keep(oldFrame, parts * sizeof(float));
}

void KeepController(CThing* t, CMotionController* mc)
{
    if (CMCMech* m = dynamic_cast<CMCMech*>(mc)) {
        Keep(m, sizeof(CMCMech));
        const int legs = m->mNumLegs > 0 ? m->mNumLegs : 0, parts = m->mMesh ? m->mMesh->mNumParts : 0;
        if (m->mLegPos) {
            Keep(m->mLegPos, legs * sizeof(FVector)); Keep(m->mLegA, legs * sizeof(BOOL)); Keep(m->mLegB, legs * sizeof(float));
            Keep(m->mLegC, legs * sizeof(SINT)); Keep(m->mLegTarget, legs * sizeof(FVector));
        }
        if (m->mToeFrames) Keep(m->mToeFrames, legs * (m->mNumToes > 0 ? m->mNumToes : 0) * sizeof(SINT));
        KeepParts(m->mPartOri, m->mPartPos, m->mPartFrame, m->mOldPartOri, m->mOldPartPos, m->mOldPartFrame, parts);
    } else if (CMCBuggy* b = dynamic_cast<CMCBuggy*>(mc)) {
        Keep(b, sizeof(CMCBuggy));
        const int wheels = b->mNumWheels > 0 ? b->mNumWheels : 0;
        if (b->mWheelPos) { Keep(b->mWheelPos, wheels * sizeof(FVector)); Keep(b->mWheelSpin, wheels * sizeof(float)); }
        KeepParts(b->mPartOri, b->mPartPos, b->mPartFrame, b->mOldPartOri, b->mOldPartPos, b->mOldPartFrame, ThingParts(t));
    } else if (CMCTentacle* c = dynamic_cast<CMCTentacle*>(mc)) {
        Keep(c, sizeof(CMCTentacle));
        KeepParts(c->mPartOri, c->mPartPos, c->mPartFrame, c->mOldPartOri, c->mOldPartPos, c->mOldPartFrame, ThingParts(t));
    }
    else if (CMCGroundVehicle* v = dynamic_cast<CMCGroundVehicle*>(mc)) Keep(v, sizeof(CMCGroundVehicle));
    else if (CMCCannon* k = dynamic_cast<CMCCannon*>(mc)) Keep(k, sizeof(CMCCannon));
    else if (CMCComponent* o = dynamic_cast<CMCComponent*>(mc)) Keep(o, sizeof(CMCComponent));
    else if (CMCSentinel* n = dynamic_cast<CMCSentinel*>(mc)) Keep(n, sizeof(CMCSentinel));
    else if (CMCGroundAttack* g = dynamic_cast<CMCGroundAttack*>(mc)) Keep(g, sizeof(CMCGroundAttack));
    else if (CMCDropship* d = dynamic_cast<CMCDropship*>(mc)) Keep(d, sizeof(CMCDropship));
    else if (CDestructableSegmentsMotionController* x = dynamic_cast<CDestructableSegmentsMotionController*>(mc)) {
        if (x->mSegments) Keep(&x->mSegments->mChanged, sizeof x->mSegments->mChanged);
    }
}

// Components last, so their saved transform is put back after any controller bytes that overlap nothing of theirs.
struct SavedComponent { CComponent* c; FVector pos; FMatrix ori; SINT stamp; };
SavedComponent* gComponents = NULL;
size_t gComponentCount = 0, gComponentCap = 0;

// CE_RENDER_FENCE_SHADOWS_BEGIN
// The shadow manager (SHADOWS): WORLD.Render calls RequestShadow for units nearest the drawing camera, and the next frame's
// SHADOWS.RenderTextures calls unit->GetRenderPos() and unit->Render(0) on those assigned units, refreshing their components and poses.
// Saving raw CUnit* pointers and restoring via CActiveReader::SetReader keeps each unit's CMonitor deletion-callback list intact (a raw
// memcpy of CShadows would leave dangling deletion callbacks or drop live ones when units die later).
struct SavedShadowRequest { CUnit* unit; float dist; };
struct SavedShadowTexInfo {
    CUnit* unit;
    signed char fade;
    UBYTE state;
    UBYTE unused;
    signed char alpha;
    UBYTE unknown08;
    UBYTE unknown09[3];
    float radius;
    FVector pos;
};
struct SavedShadows {
    bool valid;
    SavedShadowRequest requests[32];
    SavedShadowTexInfo texInfo[32];
    SShadowTexInfo* freeList[32];
    SINT numFree;
    SINT unknown590;
};
SavedShadows gShadows = {};

void SaveShadows()
{
    gShadows.valid = true;
    for (int i = 0; i < 32; ++i) {
        gShadows.requests[i].unit = SHADOWS.mRequests[i].mUnit.ToRead();
        gShadows.requests[i].dist = SHADOWS.mRequests[i].mDist;
        SShadowTexInfo& t = SHADOWS.mTexInfo[i];
        gShadows.texInfo[i].unit = t.mUnit.ToRead();
        gShadows.texInfo[i].fade = t.mFade;
        gShadows.texInfo[i].state = t.mState;
        gShadows.texInfo[i].unused = t.mUnused;
        gShadows.texInfo[i].alpha = t.mAlpha;
        gShadows.texInfo[i].unknown08 = t.mUnknown08;
        memcpy(gShadows.texInfo[i].unknown09, t.mUnknown09, sizeof t.mUnknown09);
        gShadows.texInfo[i].radius = t.mRadius;
        gShadows.texInfo[i].pos = t.mPos;
        gShadows.freeList[i] = SHADOWS.mFree[i];
    }
    gShadows.numFree = SHADOWS.mNumFree;
    gShadows.unknown590 = SHADOWS.mUnknown590;
}

void RestoreShadows()
{
    if (!gShadows.valid) return;
    for (int i = 0; i < 32; ++i) {
        SHADOWS.mRequests[i].mUnit.SetReader(gShadows.requests[i].unit);
        SHADOWS.mRequests[i].mDist = gShadows.requests[i].dist;
        SShadowTexInfo& t = SHADOWS.mTexInfo[i];
        t.mUnit.SetReader(gShadows.texInfo[i].unit);
        t.mFade = gShadows.texInfo[i].fade;
        t.mState = gShadows.texInfo[i].state;
        t.mUnused = gShadows.texInfo[i].unused;
        t.mAlpha = gShadows.texInfo[i].alpha;
        t.mUnknown08 = gShadows.texInfo[i].unknown08;
        memcpy(t.mUnknown09, gShadows.texInfo[i].unknown09, sizeof t.mUnknown09);
        t.mRadius = gShadows.texInfo[i].radius;
        t.mPos = gShadows.texInfo[i].pos;
        SHADOWS.mFree[i] = gShadows.freeList[i];
    }
    SHADOWS.mNumFree = gShadows.numFree;
    SHADOWS.mUnknown590 = gShadows.unknown590;
    gShadows.valid = false;
}
// CE_RENDER_FENCE_SHADOWS_END

struct SavedLandscapeView {
    bool valid;
    FVector lastPos;
    FVector lastLook;
};
SavedLandscapeView gLandscapeView = {};

void SaveLandscapeView()
{
    gLandscapeView.valid = false;
    CLANDSCAPE* land = ENGINE.GetLandscape();
    if (!land || !land->mViews || land->mNumViews <= 0) return;
    CLandscapeView& v = land->mViews[0];
    gLandscapeView.lastPos = v.mLastPos;
    gLandscapeView.lastLook = v.mUnknown24;
    gLandscapeView.valid = true;
    // Snap the fenced pass's smoothed landscape focus directly to the Commander camera
    // instead of lagging 3% per frame from the cockpit's position.
    v.mLastPos.X = 1234567.0f;
}

void RestoreLandscapeView()
{
    if (!gLandscapeView.valid) return;
    gLandscapeView.valid = false;
    CLANDSCAPE* land = ENGINE.GetLandscape();
    if (!land || !land->mViews || land->mNumViews <= 0) return;
    land->mViews[0].mLastPos = gLandscapeView.lastPos;
    land->mViews[0].mUnknown24 = gLandscapeView.lastLook;
}
}

bool CeRenderFenceSave()
{
    gRegionCount = gByteCount = 0; gOverflow = false; gComponentCount = 0; gShadows.valid = false; gLandscapeView.valid = false;
    ListIterator<CThing> it(&WORLD.GetThingNB());
    for (CThing* t = it.First(); t; t = it.Next()) {
        if (CMotionController* mc = t->GetMotionController()) KeepController(t, mc);
        // The thing's pose cache (CMeshPose::Update): rebuilt at most once a render frame, and reused for up to 30 frames by reads that do not
        // force it (a launch point, a targetable position): a pose an extra pass rebuilt would reach the battle through them.
        if (CRenderThing* rt = t->GetRenderThing()) if (CMeshPose* pose = rt->mPose) {
            Keep(pose, sizeof(CMeshPose));
            const int parts = pose->mNumParts > 0 ? pose->mNumParts : 0;
            Keep(pose->mOrientations, parts * sizeof(FMatrix)); Keep(pose->mPositions, parts * sizeof(FVector)); Keep(pose->mFrames, parts * sizeof(SINT));
        }
        if (!t->IsA(THING_TYPE_COMPONENT)) continue;
        if (gComponentCount == gComponentCap) {
            const size_t cap = gComponentCap ? gComponentCap * 2 : 512;
            SavedComponent* grown = (SavedComponent*)realloc(gComponents, cap * sizeof *grown);
            if (!grown) { gOverflow = true; break; }
            gComponents = grown; gComponentCap = cap;
        }
        CComponent* c = (CComponent*)t;
        gComponents[gComponentCount++] = {c, c->GetPos(), c->GetOrientation(), c->mUnknown278};
    }
    if (gOverflow) { gRegionCount = gByteCount = 0; gComponentCount = 0; gShadows.valid = false; return false; }
    SaveShadows();
    SaveLandscapeView();
    return true;
}

void CeRenderFenceRestore()
{
#ifdef CE_COMMANDER_TEST
    static int dropRestore = -1;		// the proof's mutant: with the restore left out, the camera-sweep comparison must fail
    if (dropRestore < 0) dropRestore = getenv("BEA_PORT_RENDER_FENCE_MUTANT_NO_RESTORE") ? 1 : 0;
    if (dropRestore) return;
#endif
    size_t at = 0;
    for (size_t i = 0; i < gRegionCount; ++i) { memcpy(gRegions[i].at, gBytes + at, gRegions[i].size); at += gRegions[i].size; }
    for (size_t i = 0; i < gComponentCount; ++i) {
        CComponent* c = gComponents[i].c;
        c->GetPos() = gComponents[i].pos; c->GetOrientation() = gComponents[i].ori; c->mUnknown278 = gComponents[i].stamp;
    }
    RestoreLandscapeView();
    RestoreShadows();
}

#else
bool CeRenderFenceSave() { return false; }
void CeRenderFenceRestore() {}
#endif
