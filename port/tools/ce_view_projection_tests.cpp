// SPDX-FileCopyrightText: Onslaught CE contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <cmath>
#include <cstdio>
#include <limits>
// Production Windows headers supply these macros. The helper must tolerate them.
#define min(a,b) bad_min_macro(a,b)
#define max(a,b) bad_max_macro(a,b)
#include "ce_view_projection.h"
#undef min
#undef max

static int failed;
static void Check(bool value, const char* name) {
    if (!value) { std::fprintf(stderr, "FAILED: %s\n", name); ++failed; }
}
static bool Near(float a, float b, float epsilon = .002f) { return std::fabs(a - b) < epsilon; }
static const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
static const float perspective[16] = {.75f,0,0,0, 0,1,0,0, 0,0,100.f/99.f,1, 0,0,-100.f/99.f,0};

int main() {
    const CeViewViewport viewport = {10,20,800,600,.2f,.8f};
    const float translated[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, -3,2,-4,1};
    CeViewPoint pixel = {}, nearPoint = {}, farPoint = {};
    Check(CeViewProject(translated,perspective,viewport,{5,-1,14},&pixel), "translated camera visible");
    Check(Near(pixel.x,470) && Near(pixel.y,290) && Near(pixel.z,.2f + .6f * (100.f/99.f-10.f/99.f)), "known off-centre pixel and depth");
    Check(CeViewRay(translated,perspective,viewport,470,290,&nearPoint,&farPoint), "translated camera ray");
    Check(Near(nearPoint.x,3.2f) && Near(nearPoint.y,-1.9f) && Near(nearPoint.z,5), "near plane in world space");
    Check(Near(farPoint.x,23,.02f) && Near(farPoint.y,8,.02f) && Near(farPoint.z,104,.02f), "far plane in world space");

    const float turned[16] = {1,0,0,0, 0,0,-1,0, 0,1,0,0, 0,0,10,1};
    Check(CeViewProject(turned,perspective,viewport,{0,0,0},&pixel) && Near(pixel.x,410) && Near(pixel.y,320), "turned camera centre");
    Check(CeViewRay(turned,perspective,viewport,410,320,&nearPoint,&farPoint), "turned forward ray");
    Check(Near(nearPoint.y,9) && Near(nearPoint.z,0) && Near(farPoint.y,-90,.02f) && Near(farPoint.z,0), "turned ray faces world negativeY");

    // Round trips exercise many off-axis rays; expected screen positions come
    // from independent similar-triangle geometry, not a duplicate inverse.
    for (int z = 3; z <= 63; z += 20) for (int x = -2; x <= 2; ++x) for (int y = -1; y <= 1; ++y) {
        CeViewPoint world = {float(x),float(y),float(z)};
        Check(CeViewProject(identity,perspective,viewport,world,&pixel), "grid project");
        Check(Near(pixel.x,410 + 300.f*x/z) && Near(pixel.y,320 - 300.f*y/z), "independent perspective geometry");
        Check(CeViewRay(identity,perspective,viewport,pixel.x,pixel.y,&nearPoint,&farPoint), "grid ray");
        const float t = (world.z-nearPoint.z)/(farPoint.z-nearPoint.z);
        Check(Near(nearPoint.x+(farPoint.x-nearPoint.x)*t,world.x) && Near(nearPoint.y+(farPoint.y-nearPoint.y)*t,world.y), "ray contains source point");
    }
    const CeViewPoint sentinel = {123,456,789};
    pixel = sentinel;
    Check(!CeViewProject(identity,perspective,viewport,{0,0,-10},&pixel), "behind camera refused");
    Check(!CeViewProject(identity,perspective,viewport,{0,0,.5f},&pixel), "before near plane refused");
    Check(!CeViewProject(identity,perspective,viewport,{0,0,101},&pixel), "beyond far plane refused");
    Check(!CeViewProject(identity,perspective,viewport,{50,0,10},&pixel), "horizontal viewport clipping");
    Check(!CeViewProject(identity,perspective,viewport,{0,50,10},&pixel), "vertical viewport clipping");
    Check(pixel.x==sentinel.x && pixel.y==sentinel.y && pixel.z==sentinel.z, "project failure preserves output");
    nearPoint = sentinel; farPoint = sentinel;
    Check(!CeViewRay(identity,perspective,viewport,9,320,&nearPoint,&farPoint), "left outside viewport");
    Check(!CeViewRay(identity,perspective,viewport,811,320,&nearPoint,&farPoint), "right outside viewport");
    Check(!CeViewRay(identity,perspective,viewport,410,19,&nearPoint,&farPoint), "above viewport");
    Check(!CeViewRay(identity,perspective,viewport,410,621,&nearPoint,&farPoint), "below viewport");
    float invalid[16] = {};
    Check(!CeViewRay(invalid,perspective,viewport,410,320,&nearPoint,&farPoint), "singular matrix refused");
    invalid[0] = std::numeric_limits<float>::quiet_NaN();
    Check(!CeViewRay(invalid,perspective,viewport,410,320,&nearPoint,&farPoint), "nonfinite matrix refused");
    Check(!CeViewProject(invalid,perspective,viewport,{0,0,2},&pixel), "nonfinite project matrix refused");
    CeViewViewport bad = viewport; bad.width=0;
    Check(!CeViewRay(identity,perspective,bad,410,320,&nearPoint,&farPoint), "zero viewport refused");
    bad=viewport; bad.maxDepth=bad.minDepth;
    Check(!CeViewRay(identity,perspective,bad,410,320,&nearPoint,&farPoint), "collapsed depth refused");
    Check(!CeViewRay(nullptr,perspective,viewport,410,320,&nearPoint,&farPoint), "null matrix refused");
    Check(!CeViewRay(identity,perspective,viewport,410,320,&nearPoint,&nearPoint), "aliased endpoints refused");
    Check(!CeViewRay(identity,perspective,viewport,std::numeric_limits<float>::infinity(),320,&nearPoint,&farPoint), "nonfinite pixel refused");
    Check(nearPoint.x==123 && nearPoint.y==456 && nearPoint.z==789 && farPoint.x==123 && farPoint.y==456 && farPoint.z==789, "ray failure preserves both outputs");
    Check(!CeViewProject(identity,perspective,viewport,{0,0,std::numeric_limits<float>::infinity()},&pixel), "nonfinite world point refused");
    const CeViewViewport ortho = {0,0,640,480,0,1};
    Check(CeViewRay(identity,identity,ortho,320,240,&nearPoint,&farPoint) && Near(nearPoint.z,0) && Near(farPoint.z,1), "orthographic forward segment");
    std::printf("projection authored checks: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
