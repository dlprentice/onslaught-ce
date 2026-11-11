// SPDX-License-Identifier: GPL-3.0-only
// Stand-ins for the tree imposter decision: model_quality_tests.py compiles the mirror's CRTTree::RenderImposter
// (TREE_FUNCTION, written by the test from port/build.py's patched text) against these types and prints one line per
// case: the imposter renders, their alpha and frame, the drawn flag and the counter. With and without patch 0208 the
// lines must be equal while Full models is off.
#include "model_quality.h"
#include <stdio.h>

typedef int BOOL;
#define TRUE 1
#define FALSE 0
#ifndef NULL
#define NULL 0
#endif

struct FVector { float X, Y, Z; };
struct CThing { FVector mPos; FVector GetRenderPos() { return mPos; } };
struct CFallingState { int unused; };
struct CTree : CThing { CFallingState *mFalling; };
struct CCamera { FVector mPos; FVector GetPos() { return mPos; } };
struct CEngine { CCamera mCamera; CCamera *GetCamera() { return &mCamera; } };
static CEngine ENGINE;
static float TREE_MESH_DISTANCE = 100.0f;
static BOOL FORCE_OBJECT_IMPOSTERS = FALSE;

static int renders, lastAlpha, lastFrame, counted, otherCounts;
static bool fullModels;

bool CeModelQualityEnabled() { return fullModels; }
void CeModelQualityCount(CeModelQualityEvent event)
{
    if (event == CE_MODEL_TREE_IMPOSTOR) ++counted;
    else ++otherCounts;
}

struct CImposter {
    void Render(CThing *, int alpha, int frame) { ++renders; lastAlpha = alpha; lastFrame = frame; }
};

struct CRTTree {
    CThing *mThing;
    CImposter *mImposter;
    float mMaxSlope;
    BOOL mImposterDrawn;
    void RenderImposter();
};

#include TREE_FUNCTION

int main(int argc, char **argv)
{
    fullModels = argc > 1 && argv[1][0] == '1';
    const bool counter = !(argc > 2 && argv[2][0] == 'b');   // "base": the function without patch 0208 has no counter
    // Distance from the camera (inside, at and beyond TREE_MESH_DISTANCE), height above it (steep and shallow against
    // mMaxSlope), falling or standing, imposters forced or not.
    const float distances[] = {10.0f, 99.0f, 100.0f, 101.0f, 400.0f, 2000.0f};
    const float heights[] = {0.0f, 1.0f, 30.0f, 120.0f, 900.0f};
    const float slopes[] = {0.0f, 0.01f, 0.2f, 1.0f, 50.0f};
    CFallingState falling = {0};
    CImposter imposter;
    unsigned cases = 0, miscounted = 0;
    for (float distance : distances)
        for (float height : heights)
            for (float slope : slopes)
                for (int fall = 0; fall < 2; ++fall)
                    for (int force = 0; force < 2; ++force) {
                        CTree tree;
                        tree.mPos = FVector{distance, 0.0f, height};
                        tree.mFalling = fall ? &falling : NULL;
                        ENGINE.mCamera.mPos = FVector{0.0f, 0.0f, 0.0f};
                        FORCE_OBJECT_IMPOSTERS = force;
                        CRTTree render = {&tree, &imposter, slope, FALSE};
                        renders = lastAlpha = lastFrame = counted = otherCounts = 0;
                        render.RenderImposter();
                        printf("d=%g h=%g s=%g fall=%d force=%d renders=%d alpha=%d frame=%d drawn=%d\n", distance,
                               height, slope, fall, force, renders, lastAlpha, lastFrame, render.mImposterDrawn);
                        // A counter counts exactly the renders that happen and nothing else.
                        if (counter && (counted != renders || otherCounts)) ++miscounted;
                        ++cases;
                    }
    fprintf(stderr, "%u cases, %u miscounted\n", cases, miscounted);
    return miscounted ? 2 : 0;
}
