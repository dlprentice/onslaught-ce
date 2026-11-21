#include "ce_hud_reservation.h"

#include <algorithm>
#include <cmath>

namespace {
std::uint64_t gOwner = 0, gFrame = 0;
int gWidth = 0, gHeight = 0, gDepth = 0;
bool gHaveBounds = false;
CeHudReservation gBounds{};
}

bool gCeHudReservationCollecting = false;

bool CeHudReservationBegin(std::uint64_t owner, std::uint64_t frame,
                           int width, int height) {
    if (owner == 0 || width <= 0 || height <= 0) return false;
    const bool same = owner == gOwner && frame == gFrame &&
                      width == gWidth && height == gHeight;
    if (gDepth != 0 && !same) return false;
    if (!same) {
        gOwner = owner; gFrame = frame; gWidth = width; gHeight = height;
        gHaveBounds = false;
        gBounds = CeHudReservation{};
    }
    ++gDepth;
    gCeHudReservationCollecting = true;
    return true;
}

void CeHudReservationEnd() {
    if (gDepth > 0) --gDepth;
    gCeHudReservationCollecting = gDepth != 0;
}

void CeHudReservationReset() {
    gOwner = gFrame = 0;
    gWidth = gHeight = gDepth = 0;
    gHaveBounds = gCeHudReservationCollecting = false;
    gBounds = CeHudReservation{};
}

bool CeHudReservationRead(std::uint64_t owner, std::uint64_t frame,
                          int width, int height, CeHudReservation *out) {
    if (out == nullptr) return false;
    *out = CeHudReservation{};
    if (owner == 0 || owner != gOwner || frame != gFrame ||
        width != gWidth || height != gHeight || gDepth != 0 || !gHaveBounds)
        return false;
    *out = gBounds;
    return true;
}

void CeHudReservationQuad(float x0, float y0, float x1, float y1,
                         float x2, float y2, float x3, float y3,
                         std::uint32_t color) {
    if (!gCeHudReservationCollecting || (color >> 24) == 0) return;
    const float xs[] = {x0, x1, x2, x3};
    const float ys[] = {y0, y1, y2, y3};
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(xs[i]) || !std::isfinite(ys[i])) return;
    CeHudReservation r{
        (std::max)(0.0f, *std::min_element(xs, xs + 4)),
        (std::max)(0.0f, *std::min_element(ys, ys + 4)),
        (std::min)(float(gWidth), *std::max_element(xs, xs + 4)),
        (std::min)(float(gHeight), *std::max_element(ys, ys + 4))};
    if (r.left >= r.right || r.top >= r.bottom) return;
    if (gHaveBounds) {
        gBounds.left = (std::min)(gBounds.left, r.left);
        gBounds.top = (std::min)(gBounds.top, r.top);
        gBounds.right = (std::max)(gBounds.right, r.right);
        gBounds.bottom = (std::max)(gBounds.bottom, r.bottom);
    } else {
        gBounds = r;
        gHaveBounds = true;
    }
}
