// SPDX-License-Identifier: GPL-3.0-only
#include "ce_museum_hall.h"
#include <cmath>

bool CeHallPlan(const CeHallPoint &start, const CeHallDisplay *front, unsigned total, CeHallTour *tour)
{
    if (!tour || !front || !total || total > 16) return false;
    const unsigned count = total < 3 ? total : 3;
    CeHallTour t = {};
    t.entrance = start;
    const unsigned first = count / 2;
    float dx = front[first].x - start.x, dy = front[first].y - start.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (!(distance > 0.01f)) return false;
    t.forward = {dx / distance, dy / distance};
    t.right = {t.forward.y, -t.forward.x};
    t.exhibit[0] = first;
    t.stops = 1;
    float bestSide = -1.0e20f;
    for (unsigned n = 0; n < count; ++n) {
        if (!std::isfinite(front[n].x) || !std::isfinite(front[n].y) ||
            !(front[n].radius >= 0.0f && front[n].radius < 100.0f)) return false;
        if (n == first) continue;
        const float side = (front[n].x - front[first].x) * t.right.x +
                           (front[n].y - front[first].y) * t.right.y;
        if (side > bestSide) { bestSide = side; t.exhibit[1] = n; t.stops = 2; }
    }
    // Both stops lie in front of the nearest model face. The cross aisle can
    // never cut through the larger of the two exhibits.
    float aisle = distance;
    for (unsigned k = 0; k < t.stops; ++k) {
        const CeHallDisplay &d = front[t.exhibit[k]];
        const float depth = (d.x - start.x) * t.forward.x + (d.y - start.y) * t.forward.y;
        const float face = depth - d.radius - 2.0f;
        if (face < aisle) aisle = face;
    }
    if (!(aisle >= 2.6f)) return false;
    for (unsigned k = 0; k < t.stops; ++k) {
        const CeHallDisplay &d = front[t.exhibit[k]];
        t.display[k] = d;
        const float side = (d.x - start.x) * t.right.x + (d.y - start.y) * t.right.y;
        t.stop[k] = {start.x + t.forward.x * aisle + t.right.x * side,
                     start.y + t.forward.y * aisle + t.right.y * side};
    }
    // The front aisle stays put in a small collection. A larger collection earns a
    // separate viewing room only when a walk can actually pass the front machines.
    if (t.stops == 2 && total > count) {
        auto side = [&](const CeHallDisplay &d) { return (d.x-start.x)*t.right.x + (d.y-start.y)*t.right.y; };
        auto depth = [&](const CeHallDisplay &d) { return (d.x-start.x)*t.forward.x + (d.y-start.y)*t.forward.y; };
        auto point = [&](float x, float y) { return CeHallPoint{start.x+t.right.x*x+t.forward.x*y,
                                                                  start.y+t.right.y*x+t.forward.y*y}; };
        float outer = side(t.display[1]) + t.display[1].radius;
        unsigned rear = total;
        float farRight = -1.0e20f;
        for (unsigned n = 0; n < total; ++n) {
            const CeHallDisplay &d = front[n];
            if (!std::isfinite(d.x) || !std::isfinite(d.y) || !(d.radius >= 0 && d.radius < 100)) continue;
            if (side(d) + d.radius > outer) outer = side(d) + d.radius;
            if (n >= count && d.label >= 0 && depth(d) > depth(t.display[1]) + 2 && side(d) > farRight) {
                rear = n; farRight = side(d);
            }
        }
        if (rear < total) {
            const CeHallDisplay &d = front[rear];
            // Keep the full walking strip clear on both sides of the real gap. Centre it
            // between the machines when either one's preferred reading distance is tight.
            const float outside = outer + 3.0f;
            const float walkingEdge = 1.24f;
            float nearest = aisle + 3.0f;
            for (unsigned n = 0; n < count; ++n) {
                const CeHallDisplay &frontMachine = front[n];
                if (side(frontMachine) + frontMachine.radius + walkingEdge > side(d)) {
                    const float edge = depth(frontMachine) + frontMachine.radius + walkingEdge;
                    if (edge > nearest) nearest = edge;
                }
            }
            const float farthest = depth(d) - d.radius - walkingEdge;
            float roomDepth = depth(d) - d.radius - 2.2f;
            if (roomDepth < nearest) roomDepth = nearest;
            if (roomDepth > farthest) roomDepth = farthest;
            const bool clear = nearest <= farthest && roomDepth-aisle < 45.0f && outside-side(d) < 32.0f;
            if (clear) {
                t.exhibit[2] = rear; t.display[2] = d; t.stops = 3;
                t.annexEntry = point(outside, aisle); t.annexCorner = point(outside, roomDepth);
                t.stop[2] = point(side(d), roomDepth);
            }
        }
    }
    *tour = t;
    return true;
}

namespace {
float DistanceSquared(CeHallPoint a, CeHallPoint b) {
    const float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}
}

bool CeHallBayAllows(const CeHallTour &tour, CeHallPoint visitor) {
    if (tour.stops != 3) return true;
    const CeHallDisplay &d=tour.display[2];
    const float x=visitor.x-d.x, y=visitor.y-d.y;
    const float side=x*tour.right.x+y*tour.right.y, depth=x*tour.forward.x+y*tour.forward.y;
    // The staff side begins at the window's sill. Both perpendicular aisles and the
    // full visitor apron stay outside it; the same footprint guards the rear and ends.
    return std::fabs(side) > d.radius+1.47f || depth < -d.radius-1.04f || depth > d.radius+1.27f;
}

int CeHallZone(const CeHallTour &tour, CeHallPoint visitor, int previous) {
    const float enter = 2.35f * 2.35f, leave = 3.1f * 3.1f;
    int nearest = -1;
    float best = enter;
    for (unsigned k = 0; k < tour.stops; ++k) {
        const float d = DistanceSquared(visitor, tour.stop[k]);
        if (d <= best) { best = d; nearest = int(k) + 1; }
    }
    const float home = DistanceSquared(visitor, tour.entrance);
    if (home <= 2.1f * 2.1f && (nearest < 0 || home < best)) return 0;
    if (nearest > 0) {
        // Crossing into the closer stand wins; small movements on the seam do not switch cards.
        if (previous > 0 && unsigned(previous) <= tour.stops) {
            const float old = DistanceSquared(visitor, tour.stop[previous - 1]);
            if (old <= enter && old <= best + 0.45f) return previous;
        }
        return nearest;
    }
    if (previous >= 0 && unsigned(previous) <= tour.stops) {
        const CeHallPoint old = previous ? tour.stop[previous - 1] : tour.entrance;
        if (DistanceSquared(visitor, old) <= leave) return previous;
    }
    return -1;
}

int CeHallSignLabel(const CeHallTour &tour, int zone, CeHallPoint heading, int previousLabel) {
    if (zone <= 0 || unsigned(zone) > tour.stops) return -1;
    const unsigned k = unsigned(zone - 1);
    const CeHallDisplay &display = tour.display[k];
    if (display.label < 0) return -1;
    // Use the face of the stand: walking just beside a small exhibit must not point its card at the rear row.
    const float dx = display.x - tour.stop[k].x, dy = display.y - tour.stop[k].y;
    const float along = dx * heading.x + dy * heading.y;
    const float lengths = (dx * dx + dy * dy) * (heading.x * heading.x + heading.y * heading.y);
    const float cosine = previousLabel == display.label ? 0.4f : 0.5f;
    return along > 0.0f && along * along >= cosine * cosine * lengths ? display.label : -1;
}

CeHallPoint CeHallEntranceArch(const CeHallTour &tour) {
    const float depth = 4.8f;
    return {tour.entrance.x + depth * tour.forward.x, tour.entrance.y + depth * tour.forward.y};
}

bool CeHallFacesExit(const CeHallTour &tour, CeHallPoint visitor, CeHallPoint heading, bool held) {
    const CeHallPoint gate = CeHallEntranceArch(tour);
    const float dx = gate.x - visitor.x, dy = gate.y - visitor.y;
    const float ahead = -dx * tour.forward.x - dy * tour.forward.y;
    const float side = dx * tour.right.x + dy * tour.right.y;
    // Only the gallery side of the arch has an exit to point out.
    if (!(ahead > 0.35f && ahead < (held ? 5.8f : 5.0f) &&
          std::fabs(side) < (held ? 1.9f : 1.6f))) return false;
    // The forward view must cross the gate plane outward between its two feet.
    // Test the projected side coordinate without dividing by a near-parallel heading.
    const float outward = -heading.x * tour.forward.x - heading.y * tour.forward.y;
    if (!(outward > 0.0f)) return false;
    const float across = heading.x * tour.right.x + heading.y * tour.right.y;
    const float halfOpening = kCeHallArchPostOffset - kCeHallArchFootHalfWidth;
    const float edge = held ? halfOpening : halfOpening - 0.08f;
    if (!(std::fabs(ahead * across - side * outward) <= edge * outward)) return false;
    const float dot = dx * heading.x + dy * heading.y;
    const float lengths = (dx * dx + dy * dy) * (heading.x * heading.x + heading.y * heading.y);
    const float cone = held ? 0.81f : 0.87f;
    return dot > 0.0f && dot * dot >= cone * cone * lengths;
}
