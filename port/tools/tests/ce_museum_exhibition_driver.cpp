// SPDX-License-Identifier: GPL-3.0-only
// Host checks for port/game/ce_museum_exhibition.h and ce_museum_cinema_controls.h,
// built and run by port/tools/ce_museum_exhibition_tests.py. Every room, display
// and film list here is an authored fixture: the figures are invented to be
// dissimilar and describe nothing that ships.
//
//   driver          run every check; exit 1 if any fails
//   driver --dump   print the six-bay fixture's plan as text (for determinism)
//
// The hall is judged twice over: CheckPlanSound asks whether a plan can be walked
// and used, CheckDesign (run for every plan CheckPlanSound sees) whether it is a
// designed space: classified pieces that meet and do not float, clear floor where
// promised, machines and labels in view from where they are viewed. The hall,
// reservations and awkward groups add fixtures and causal negatives for those.
#include "../../game/ce_museum_exhibition.h"
#include "../../game/ce_museum_cinema_controls.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace Ex = CeMuseumExhibition;
namespace Cc = CeMuseumCinemaControls;

namespace {
int gChecks = 0, gFailures = 0;
void Check(bool ok, const char *what, int line) {
    ++gChecks;
    if (!ok) { ++gFailures; std::printf("FAIL line %d: %s\n", line, what); }
}
#define CHECK(x) Check((x), #x, __LINE__)

Ex::RoomSpec Room(int portalSide = 1, float portalMin = 6.0f, float portalMax = 11.0f) {
    Ex::RoomSpec r = {-29.0f, 29.0f, -7.0f, 45.0f, {0.0f, 0.0f}, portalSide, portalMin, portalMax};
    return r;
}
// Six dissimilar footprints: small, broad and low, thin and tall, tall, flat, largest.
const Ex::DisplayInput kSix[6] = {{41, 1.4f, 1.2f}, {7, 4.6f, 2.2f}, {19, 0.6f, 3.8f},
                                  {3, 3.2f, 5.5f}, {88, 2.3f, 0.9f}, {52, 5.4f, 3.0f}};

// A refused plan prints as refused, with its reason and nothing else; every count is
// bounded by its array, so a plan that was never filled cannot be read past its end.
void Dump(const Ex::Plan &p, std::string *text) {
    char line[320];
    if (!p.valid) {
        std::snprintf(line, sizeof line, "refused reason %d\n", int(p.reason));
        *text += line;
        return;
    }
    const unsigned bays = p.bayCount < unsigned(Ex::kMaxBays) ? p.bayCount : unsigned(Ex::kMaxBays);
    const unsigned boxes = p.boxCount < unsigned(Ex::kMaxBoxes) ? p.boxCount : unsigned(Ex::kMaxBoxes);
    const unsigned routes = p.routeCount < unsigned(Ex::kMaxRoute) ? p.routeCount : unsigned(Ex::kMaxRoute);
    const unsigned zones = p.zoneCount < unsigned(Ex::kMaxZones) ? p.zoneCount : unsigned(Ex::kMaxZones);
    std::snprintf(line, sizeof line, "valid %d reason %d bays %u boxes %u route %u door %.9g %.9g %.9g forecourt %.9g %.9g\n",
                  int(p.valid), int(p.reason), p.bayCount, p.boxCount, p.routeCount, double(p.doorCentre.x),
                  double(p.doorCentre.y), double(p.doorHalf), double(p.forecourt.x), double(p.forecourt.y));
    *text += line;
    for (unsigned i = 0; i < bays; ++i) {
        const Ex::Bay &b = p.bays[i];
        std::snprintf(line, sizeof line, "bay %d input %u label %d r %.9g h %.9g side %d model %.9g %.9g %.9g plinth %.9g pad %.9g %.9g "
                      "view %.9g short %d stand %.9g %.9g\n", b.number, b.input, b.stableLabel, double(b.radius), double(b.height),
                      b.side, double(b.model.x), double(b.model.y), double(b.model.z), double(b.plinthHalf), double(b.pad.x),
                      double(b.pad.y), double(b.viewDistance), int(b.viewShortened), double(b.stand.x), double(b.stand.y));
        *text += line;
    }
    for (unsigned i = 0; i < boxes; ++i) {
        const Ex::Box &b = p.boxes[i];
        std::snprintf(line, sizeof line, "box %u purpose %d bay %d colour %08x min %.9g %.9g %.9g max %.9g %.9g %.9g\n", i,
                      int(b.purpose), b.bay, b.colour, double(b.min.x), double(b.min.y), double(b.min.z), double(b.max.x),
                      double(b.max.y), double(b.max.z));
        *text += line;
    }
    for (unsigned i = 0; i < routes; ++i) {
        const Ex::Segment &s = p.route[i];
        std::snprintf(line, sizeof line, "route %u kind %d bay %d %.9g %.9g -> %.9g %.9g\n", i, int(s.kind), s.bay,
                      double(s.a.x), double(s.a.y), double(s.b.x), double(s.b.y));
        *text += line;
    }
    std::snprintf(line, sizeof line, "walls %.9g portal clear %.9g zones %u\n", double(p.wallHeight),
                  double(p.portalClearHeight), p.zoneCount);
    *text += line;
    for (unsigned i = 0; i < zones; ++i) {
        const Ex::Zone &z = p.zones[i];
        std::snprintf(line, sizeof line, "zone %u kind %d bay %d %.9g %.9g -> %.9g %.9g\n", i, int(z.kind), z.bay,
                      double(z.min.x), double(z.min.y), double(z.max.x), double(z.max.y));
        *text += line;
    }
}

// The driver's own measurements, so the plan is not only judged by itself.
bool Shares(const Ex::Box &a, const Ex::Box &b) {
    const float e = 1.0e-4f;
    return a.min.x < b.max.x - e && b.min.x < a.max.x - e && a.min.y < b.max.y - e && b.min.y < a.max.y - e &&
           a.min.z < b.max.z - e && b.min.z < a.max.z - e;
}
unsigned Count(const Ex::Plan &p, Ex::Purpose purpose) {
    unsigned n = 0;
    for (unsigned i = 0; i < p.boxCount; ++i) n += p.boxes[i].purpose == purpose;
    return n;
}
const Ex::Box *Find(const Ex::Plan &p, Ex::Purpose purpose, int bay) {
    for (unsigned i = 0; i < p.boxCount; ++i)
        if (p.boxes[i].purpose == purpose && p.boxes[i].bay == bay) return &p.boxes[i];
    return nullptr;
}
Ex::Vec2 Turn(Ex::Vec2 v, float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    return Ex::Vec2{v.x * c - v.y * s, v.x * s + v.y * c};
}
Ex::Vec2 Toward(Ex::Vec2 from, float x, float y) { return Ex::Vec2{x - from.x, y - from.y}; }

// Whether two boxes share part of a face: they meet with no gap and no overlap.
bool FaceContact(const Ex::Box &a, const Ex::Box &b) {
    const float e = 1.0e-3f;
    const float lo[2][3] = {{a.min.x, a.min.y, a.min.z}, {b.min.x, b.min.y, b.min.z}};
    const float hi[2][3] = {{a.max.x, a.max.y, a.max.z}, {b.max.x, b.max.y, b.max.z}};
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (std::fabs(lo[0][axis] - hi[1][axis]) > e && std::fabs(hi[0][axis] - lo[1][axis]) > e) continue;
        bool area = true;
        for (unsigned other = 0; other < 3; ++other)
            if (other != axis) area = area && lo[0][other] < hi[1][other] - e && lo[1][other] < hi[0][other] - e;
        if (area) return true;
    }
    return false;
}
// Whether a box stands over part of a floor rectangle.
bool OverRect(const Ex::Box &b, float x0, float y0, float x1, float y1) {
    const float e = 1.0e-3f;
    return b.min.x < x1 - e && b.max.x > x0 + e && b.min.y < y1 - e && b.max.y > y0 + e;
}
// Whether two floor panels meet along an edge.
bool Adjoins(const Ex::Box &a, const Ex::Box &b) {
    const float e = 1.0e-3f;
    const bool xMeet = std::fabs(a.max.x - b.min.x) <= e || std::fabs(b.max.x - a.min.x) <= e;
    const bool yMeet = std::fabs(a.max.y - b.min.y) <= e || std::fabs(b.max.y - a.min.y) <= e;
    const bool xOver = a.min.x < b.max.x - e && b.min.x < a.max.x - e;
    const bool yOver = a.min.y < b.max.y - e && b.min.y < a.max.y - e;
    return (xMeet && yOver) || (yMeet && xOver);
}

// Where a visitor can get to on foot from the entrance: a flood over a grid of
// standing points centred on the entrance, each tested with the plan's own WalkAt.
struct Reach {
    float x0, y0, step;
    int nx, ny;
    std::vector<unsigned char> seen;
    bool At(Ex::Vec2 v) const {
        const int i = int(std::floor((v.x - x0) / step + 0.5f)), j = int(std::floor((v.y - y0) / step + 0.5f));
        return i >= 0 && j >= 0 && i < nx && j < ny && seen[size_t(j) * size_t(nx) + size_t(i)] != 0;
    }
};
Reach Flood(const Ex::Plan &p, float step) {
    const Ex::RoomSpec &r = p.room;
    const int left = int((r.entrance.x - r.minX) / step), right = int((r.maxX - r.entrance.x) / step);
    const int down = int((r.entrance.y - r.minY) / step), up = int((r.maxY - r.entrance.y) / step);
    Reach g;
    g.step = step; g.nx = left + right + 1; g.ny = down + up + 1;
    g.x0 = r.entrance.x - float(left) * step; g.y0 = r.entrance.y - float(down) * step;
    g.seen.assign(size_t(g.nx) * size_t(g.ny), 0);
    std::vector<int> stack;
    if (Ex::WalkAt(p, r.entrance) != Ex::kWalkOpen) return g;
    g.seen[size_t(down) * size_t(g.nx) + size_t(left)] = 1;
    stack.push_back(down * g.nx + left);
    while (!stack.empty()) {
        const int cell = stack.back(), i = cell % g.nx, j = cell / g.nx;
        stack.pop_back();
        const int next[4][2] = {{i + 1, j}, {i - 1, j}, {i, j + 1}, {i, j - 1}};
        for (unsigned k = 0; k < 4; ++k) {
            const int a = next[k][0], b = next[k][1];
            if (a < 0 || b < 0 || a >= g.nx || b >= g.ny) continue;
            const size_t at = size_t(b) * size_t(g.nx) + size_t(a);
            if (g.seen[at]) continue;
            if (Ex::WalkAt(p, Ex::Vec2{g.x0 + float(a) * step, g.y0 + float(b) * step}) != Ex::kWalkOpen) continue;
            g.seen[at] = 1;
            stack.push_back(b * g.nx + a);
        }
    }
    return g;
}
// A copy of a plan without the boxes drop picks out, for asking what a piece is for.
Ex::Plan Without(const Ex::Plan &p, bool (*drop)(const Ex::Box &)) {
    Ex::Plan q = p;
    q.boxCount = 0;
    for (unsigned i = 0; i < p.boxCount; ++i)
        if (!drop(p.boxes[i])) q.boxes[q.boxCount++] = p.boxes[i];
    return q;
}

// The hall as a designed space, measured from the boxes, zones and bays a host
// receives: bounded and classified pieces that meet cleanly, clear floor where the
// plan promises it, and every machine and label in view from where it is viewed.
unsigned gMostBoxes = 0;
void CheckDesign(const Ex::Plan &p) {
    if (!p.valid) return;
    const Ex::RoomSpec &r = p.room;
    const float e = 1.0e-3f, eye = Ex::kEyeHeight, ex = r.entrance.x;
    if (p.boxCount > gMostBoxes) gMostBoxes = p.boxCount;
    CHECK(p.boxCount <= unsigned(Ex::kMaxBoxes) && p.zoneCount <= unsigned(Ex::kMaxZones));
    CHECK(p.routeCount <= unsigned(Ex::kMaxRoute) && p.bayCount <= unsigned(Ex::kMaxBays));

    unsigned wild = 0, misfiled = 0, floating = 0, narrow = 0, headers = 0, lowHeaders = 0, shortWalls = 0;
    for (unsigned i = 0; i < p.boxCount; ++i) {
        const Ex::Box &b = p.boxes[i];
        const float v[6] = {b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z};
        bool sane = (b.colour >> 24) == 0xffu;
        for (unsigned k = 0; k < 6; ++k) sane = sane && std::isfinite(v[k]);
        sane = sane && b.min.x < b.max.x && b.min.y < b.max.y && b.min.z < b.max.z;
        sane = sane && b.min.x >= r.minX - Ex::kWallHalf - e && b.max.x <= r.maxX + Ex::kWallHalf + e;
        sane = sane && b.min.y >= r.minY - Ex::kWallHalf - e && b.max.y <= r.maxY + Ex::kWallHalf + e;
        sane = sane && b.min.z >= -e && b.max.z <= Ex::kWallHeightMax + e;
        wild += !sane;
        // Trim and floor are drawn only; everything else stops a visitor or is overhead.
        const Ex::Category c = Ex::CategoryOf(b.purpose);
        const bool drawnOnly = c == Ex::kCategoryTrim || c == Ex::kCategoryFloor;
        misfiled += drawnOnly == Ex::IsSolid(b.purpose);
        misfiled += c == Ex::kCategoryFloor && b.max.z > 0.05f;
        // Nothing floats: a piece off the floor is held by a solid one it meets face to face.
        if (b.min.z > Ex::kFloorTop + e) {
            bool held = false;
            for (unsigned j = 0; j < p.boxCount && !held; ++j)
                held = j != i && Ex::IsSolid(p.boxes[j].purpose) && FaceContact(b, p.boxes[j]);
            floating += !held;
        }
        const bool furniture = Ex::Obstructs(b) && b.purpose != Ex::kSolidWall && b.purpose != Ex::kSolidDoor;
        narrow += furniture && b.min.x < ex + Ex::kAisleHalf - e && b.max.x > ex - Ex::kAisleHalf + e;
        headers += b.purpose == Ex::kSolidHeader;
        lowHeaders += b.purpose == Ex::kSolidHeader &&
                      (b.min.z < 2.0f * Ex::kBodyTop || std::fabs(b.min.z - p.portalClearHeight) > e);
        shortWalls += b.purpose == Ex::kSolidWall && std::fabs(b.max.z - p.wallHeight) > e;
    }
    CHECK(wild == 0);
    CHECK(misfiled == 0);
    CHECK(floating == 0);
    CHECK(narrow == 0);
    CHECK(headers == (r.portalSide ? 1u : 0u) && lowHeaders == 0 && shortWalls == 0);
    CHECK(p.wallHeight >= Ex::kWallHeight && p.wallHeight <= Ex::kWallHeightMax);
    CHECK((p.portalClearHeight > 0.0f) == (r.portalSide != 0));

    // The floor the plan promises to keep clear is clear, and can be stood on throughout.
    CHECK(Ex::ZoneFor(p, Ex::kZoneArrival) && Ex::ZoneFor(p, Ex::kZoneNave));
    CHECK((Ex::ZoneFor(p, Ex::kZoneApproach) != nullptr) == (r.portalSide != 0));
    CHECK((Ex::ZoneFor(p, Ex::kZoneThroat) != nullptr) == (r.portalSide != 0));
    for (unsigned z = 0; z < p.zoneCount; ++z) {
        const Ex::Zone &zone = p.zones[z];
        CHECK(zone.max.x > zone.min.x && zone.max.y > zone.min.y);
        if (zone.kind == Ex::kZoneHost) continue;
        unsigned inside = 0;
        for (unsigned i = 0; i < p.boxCount; ++i) {
            const Ex::Box &b = p.boxes[i];
            if (Ex::CategoryOf(b.purpose) == Ex::kCategoryFloor || b.min.z >= Ex::kBodyTop) continue;
            inside += OverRect(b, zone.min.x, zone.min.y, zone.max.x, zone.max.y);
        }
        CHECK(inside == 0);
        if (zone.kind == Ex::kZoneThroat) continue;
        const float in = Ex::kVisitorRadius + 0.02f;
        float x0 = zone.min.x + in, x1 = zone.max.x - in, y0 = zone.min.y + in, y1 = zone.max.y - in;
        if (x1 < x0) x0 = x1 = 0.5f * (zone.min.x + zone.max.x);
        if (y1 < y0) y0 = y1 = 0.5f * (zone.min.y + zone.max.y);
        unsigned shut = 0;
        for (int i = 0; i <= 4; ++i)
            for (int j = 0; j <= 4; ++j)
                shut += Ex::WalkAt(p, Ex::Vec2{x0 + (x1 - x0) * 0.25f * float(i), y0 + (y1 - y0) * 0.25f * float(j)}) != Ex::kWalkOpen;
        CHECK(shut == 0);
    }

    for (unsigned i = 0; i < p.bayCount; ++i) {
        const Ex::Bay &b = p.bays[i];
        CHECK(Ex::ZoneFor(p, Ex::kZoneViewing, b.number) != nullptr);
        // Measured bounds are respected: nothing the plan draws enters the machine's
        // space, and the walls rise past its top unless it is taller than any hall.
        Ex::Vec3 lo, hi;
        Ex::ExhibitEnvelope(b, &lo, &hi);
        unsigned intrusions = 0;
        for (unsigned k = 0; k < p.boxCount; ++k) {
            const Ex::Box &x = p.boxes[k];
            intrusions += OverRect(x, lo.x, lo.y, hi.x, hi.y) && x.min.z < hi.z - e && x.max.z > lo.z + e;
        }
        CHECK(intrusions == 0);
        CHECK(p.wallHeight >= (hi.z + 1.0f < Ex::kWallHeightMax ? hi.z + 1.0f : Ex::kWallHeightMax) - e);
        // The stand is built against its plinth: no sliver of floor between them.
        const Ex::Box *plinth = Find(p, Ex::kSolidPlinth, b.number), *stand = Find(p, Ex::kSolidStand, b.number);
        if (plinth && stand)
            CHECK(std::fabs(stand->min.y - plinth->max.y) < 1.0e-4f || std::fabs(stand->max.y - plinth->min.y) < 1.0e-4f);
        // From the pad at eye height the machine's middle, both edges of its base and
        // its top are in clear view; so is its middle from the nave abreast of it.
        const Ex::ViewPose pose = Ex::RecommendedPose(b, eye);
        CHECK(Ex::SightClear(p, pose.eye, b.lookAt));
        CHECK(Ex::SightClear(p, pose.eye, Ex::Vec3{b.model.x, b.model.y - 0.98f * b.radius, b.model.z + 0.05f}));
        CHECK(Ex::SightClear(p, pose.eye, Ex::Vec3{b.model.x, b.model.y + 0.98f * b.radius, b.model.z + 0.05f}));
        CHECK(Ex::SightClear(p, pose.eye, Ex::Vec3{b.model.x, b.model.y, b.model.z + b.height}));
        CHECK(Ex::SightClear(p, Ex::Vec3{ex, b.pad.y, eye}, b.lookAt));
        // The label: a numbered face is seen fairly square-on from the pad, square-on
        // from the nave abreast of the stand, and from behind the stand not at all.
        float square = 0.0f;
        CHECK(Ex::LabelFaceSeen(p, b, pose.eye, &square) >= 0 && square > 0.0f);
        CHECK(Ex::LabelFaceSeen(p, b, Ex::Vec3{ex, b.stand.y, eye}, &square) == 0 && square > 0.99f);
        CHECK(Ex::LabelFaceSeen(p, b, Ex::Vec3{b.stand.x + 2.0f * float(b.side), b.stand.y - 2.0f * float(b.side), eye}, &square) == -1);
    }

    // The walking surface is one piece: every part of it meets another, back to the door.
    unsigned surface[16], parts = 0;
    for (unsigned i = 0; i < p.boxCount; ++i)
        if (p.boxes[i].purpose == Ex::kDecorInlay && p.boxes[i].bay == 0 && parts < 16) surface[parts++] = i;
    bool joined[16] = {false};
    if (parts) joined[0] = true;
    for (unsigned pass = 0; pass < parts; ++pass)
        for (unsigned a = 0; a < parts; ++a)
            for (unsigned b = 0; b < parts; ++b)
                if (joined[a] && !joined[b] && Adjoins(p.boxes[surface[a]], p.boxes[surface[b]])) joined[b] = true;
    unsigned apart = 0;
    for (unsigned a = 0; a < parts; ++a) apart += !joined[a];
    CHECK(parts >= 2 && apart == 0);

    if (r.portalSide) {
        // The cinema is found and seen: up the nave to the branch, then straight
        // through the opening at eye height, under a header well above a visitor.
        const float mid = 0.5f * (r.portalMinY + r.portalMaxY), wallMid = r.portalSide > 0 ? r.maxX : r.minX;
        CHECK(Ex::SightClear(p, Ex::Vec3{ex, r.entrance.y, eye}, Ex::Vec3{ex, mid, eye}));
        CHECK(Ex::SightClear(p, Ex::Vec3{ex, mid, eye}, Ex::Vec3{wallMid, mid, eye}));
        CHECK(p.portalClearHeight >= 2.0f * Ex::kBodyTop);
        const Ex::Zone *approach = Ex::ZoneFor(p, Ex::kZoneApproach);
        CHECK(approach && p.forecourt.x > approach->min.x && p.forecourt.x < approach->max.x &&
              p.forecourt.y > approach->min.y && p.forecourt.y < approach->max.y);
    }
}

// Everything a finished plan must satisfy, whatever went into it.
void CheckPlanSound(const Ex::Plan &p, const Ex::DisplayInput *in, unsigned count) {
    CHECK(p.valid && p.reason == Ex::kReasonNone);
    CHECK(p.bayCount == count);
    if (!p.valid || p.bayCount != count) return;
    const Ex::RoomSpec &r = p.room;
    const float westIn = r.minX + Ex::kWallHalf, eastIn = r.maxX - Ex::kWallHalf;
    const float frontIn = r.minY + Ex::kWallHalf, backIn = r.maxY - Ex::kWallHalf;
    unsigned shared = 0;
    for (unsigned i = 0; i < p.boxCount; ++i)
        for (unsigned j = i + 1; j < p.boxCount; ++j) shared += Shares(p.boxes[i], p.boxes[j]);
    CHECK(shared == 0);
    CHECK(Count(p, Ex::kSolidPlinth) == count && Count(p, Ex::kSolidStand) == count);
    CHECK(Count(p, Ex::kSolidDoor) == 1 && Count(p, Ex::kDecorFloor) == (r.portalSide ? 2u : 1u));
    CHECK(Count(p, Ex::kSolidWall) == (r.portalSide ? 5u : 4u));
    for (unsigned i = 0; i < count; ++i) {
        const Ex::Bay &b = p.bays[i];
        // Identity, order and size are the caller's, untouched.
        CHECK(b.number == int(i) + 1 && b.input == i && b.stableLabel == in[i].stableLabel);
        CHECK(std::memcmp(&b.radius, &in[i].radius, sizeof(float)) == 0);
        CHECK(std::memcmp(&b.height, &in[i].height, sizeof(float)) == 0);
        CHECK(Ex::LabelForBay(p, b.number) == in[i].stableLabel);
        // The machine stands wholly on its own plinth, inside the walls, clear of the aisle.
        const bool small = Ex::IsSmall(b.radius, b.height);
        CHECK(std::fabs(b.plinthHalf - Ex::PlinthHalf(b.radius, b.height)) < 1.0e-6f);
        CHECK(b.plinthHalf >= b.radius + (small ? Ex::kSmallMargin : 0.5f));
        CHECK(b.model.x - b.radius > westIn && b.model.x + b.radius < eastIn);
        CHECK(b.model.y - b.radius > frontIn && b.model.y + b.radius < backIn);
        CHECK(std::fabs(b.model.x - r.entrance.x) - b.plinthHalf >= Ex::kAisleHalf);
        const Ex::Box *plinth = Find(p, Ex::kSolidPlinth, b.number), *stand = Find(p, Ex::kSolidStand, b.number);
        CHECK(plinth && stand);
        if (!plinth || !stand) continue;
        // A real plinth under every machine, a taller pedestal under a small one, never above the eye.
        CHECK(std::fabs(plinth->max.z - b.model.z) < 1.0e-6f && std::fabs(b.model.z - Ex::PlinthTop(b.height, b.radius)) < 1.0e-6f);
        CHECK(plinth->max.z >= Ex::kFloorTop + Ex::kPlinthHeight - 1.0e-6f && plinth->max.z < Ex::kEyeHeight);
        if (small) {
            // A small object's top is just under the eye, on a plinth taller than any pedestal.
            CHECK(std::fabs(plinth->max.z + b.height - (Ex::kFloorTop + Ex::kEyeHeight - Ex::kSmallBelowEye)) < 1.0e-5f);
            CHECK(plinth->max.z > Ex::kFloorTop + Ex::kPedestalHeight);
        } else {
            CHECK(plinth->max.z <= Ex::kFloorTop + Ex::kPedestalHeight + 1.0e-6f);
            CHECK(b.height + 1.0e-3f >= Ex::kPedestalReach - Ex::kPedestalHeight || plinth->max.z > Ex::kFloorTop + Ex::kPlinthHeight);
        }
        // The stand is modest and beside the plinth, never between pad and machine.
        CHECK(stand->max.z <= 1.1f);
        CHECK(stand->min.y >= plinth->max.y || stand->max.y <= plinth->min.y);
        CHECK(std::fabs(stand->min.x - r.entrance.x) >= Ex::kAisleHalf && std::fabs(stand->max.x - r.entrance.x) >= Ex::kAisleHalf);
        // The pad is on the bay's own side, reachable, and a real distance back.
        CHECK(float(b.side) * (b.pad.x - r.entrance.x) >= Ex::kPadFromSpine - 1.0e-3f);
        CHECK(Ex::WalkAt(p, b.pad) == Ex::kWalkOpen);
        if (small) {
            // Nearer than a machine's, and the pad keeps off the plinth and its stand.
            const float nearest = b.plinthHalf + Ex::kStandHalfDeep + Ex::kPadHalf;
            CHECK(b.viewDistance >= nearest - 1.0e-3f);
            CHECK(b.viewDistance <= std::max(Ex::kSmallView, nearest + 0.05f) + 1.0e-3f);
        } else {
            CHECK(b.viewDistance >= b.radius + Ex::kViewFloor - 1.0e-3f);
        }
        // Near enough to fill the view: a machine of a metre's radius or more, no taller
        // than it is wide, spans more than a quarter of a 90-degree view from its pad.
        if (b.radius >= 1.0f && b.height <= 2.0f * b.radius && !b.viewShortened)
            CHECK(b.radius / b.viewDistance >= 0.3f);
        CHECK(std::fabs(std::hypot(b.model.x - b.pad.x, b.model.y - b.pad.y) - b.viewDistance) < 1.0e-3f);
        CHECK(Ex::PathClear(p, b.pad, Ex::Vec2{r.entrance.x, b.pad.y}));
        CHECK(Ex::PathClear(p, Ex::Vec2{r.entrance.x, b.pad.y}, r.entrance));
        CHECK(Ex::WalkAt(p, Ex::Vec2{b.model.x, b.model.y}) == Ex::kWalkBlocked);
        CHECK(Ex::WalkAt(p, b.stand) == Ex::kWalkBlocked);
        const float edge = b.model.x - float(b.side) * b.plinthHalf;
        CHECK(Ex::WalkAt(p, Ex::Vec2{edge - float(b.side) * (Ex::kVisitorRadius + 0.05f), b.model.y}) == Ex::kWalkOpen);
        CHECK(Ex::WalkAt(p, Ex::Vec2{edge - float(b.side) * (Ex::kVisitorRadius - 0.05f), b.model.y}) == Ex::kWalkBlocked);
        for (unsigned j = i + 1; j < count; ++j) {
            const Ex::Bay &o = p.bays[j];
            CHECK(o.stableLabel != b.stableLabel);
            CHECK(std::hypot(o.model.x - b.model.x, o.model.y - b.model.y) >= o.radius + b.radius + 3.0f);
        }
    }
    // The whole centre aisle is walkable from the door to the back wall.
    unsigned blocked = 0;
    for (float x = -2.4f; x <= 2.41f; x += 0.4f)
        for (float y = frontIn + 0.8f; y <= backIn - 0.6f; y += 0.25f)
            blocked += Ex::WalkAt(p, Ex::Vec2{r.entrance.x + x, y}) != Ex::kWalkOpen;
    CHECK(blocked == 0);
    CHECK(Ex::PathClear(p, r.entrance, Ex::Vec2{r.entrance.x, frontIn + 0.8f}));
    if (r.portalSide) {
        // The portal is open: floor through it, no solid in it or its forecourt.
        const float mid = 0.5f * (r.portalMinY + r.portalMaxY), side = float(r.portalSide);
        const float wallIn = r.portalSide > 0 ? eastIn : westIn, wallMid = r.portalSide > 0 ? r.maxX : r.minX;
        unsigned inPortal = 0;
        for (unsigned i = 0; i < p.boxCount; ++i) {
            const Ex::Box &b = p.boxes[i];
            // The header beam is solid but wholly overhead; CheckDesign measures its height.
            if (!Ex::Obstructs(b) || b.max.y <= r.portalMinY + 1.0e-4f || b.min.y >= r.portalMaxY - 1.0e-4f) continue;
            const float nearest = r.portalSide > 0 ? wallIn - b.max.x : b.min.x - wallIn;
            inPortal += nearest < Ex::kForecourtDepth + 2.0f;
        }
        CHECK(inPortal == 0);
        CHECK(Ex::PathClear(p, r.entrance, Ex::Vec2{r.entrance.x, mid}));
        CHECK(Ex::PathClear(p, Ex::Vec2{r.entrance.x, mid}, Ex::Vec2{wallMid + side * 2.0f, mid}));
        CHECK(Ex::WalkAt(p, Ex::Vec2{wallMid, mid}) == Ex::kWalkThroat);
        CHECK(Ex::WalkAt(p, Ex::Vec2{wallMid + side * 1.0f, mid}) == Ex::kWalkOutside);
        CHECK(Ex::WalkAt(p, Ex::Vec2{wallMid, r.portalMinY - 1.0f}) == Ex::kWalkBlocked);
        CHECK(Ex::WalkAt(p, Ex::Vec2{wallMid, r.portalMaxY + 1.0f}) == Ex::kWalkBlocked);
        CHECK(Ex::WalkAt(p, p.forecourt) == Ex::kWalkOpen);
    }
    // No way out through the other walls, and nonsense is not walkable.
    CHECK(Ex::WalkAt(p, Ex::Vec2{r.entrance.x, r.minY - 2.0f}) == Ex::kWalkBlocked);
    CHECK(Ex::WalkAt(p, Ex::Vec2{r.entrance.x, r.maxY + 2.0f}) == Ex::kWalkBlocked);
    if (r.portalSide <= 0) CHECK(Ex::WalkAt(p, Ex::Vec2{r.maxX + 2.0f, 20.0f}) == Ex::kWalkBlocked);
    if (r.portalSide >= 0) CHECK(Ex::WalkAt(p, Ex::Vec2{r.minX - 2.0f, 20.0f}) == Ex::kWalkBlocked);
    CHECK(Ex::WalkAt(p, Ex::Vec2{std::numeric_limits<float>::quiet_NaN(), 0.0f}) == Ex::kWalkBlocked);
    CheckDesign(p);
}

void ExpectRejected(const Ex::RoomSpec &room, const Ex::DisplayInput *in, unsigned count, Ex::Reason reason) {
    Ex::Plan p;
    std::memset(&p, 0x5a, sizeof p);
    CHECK(!Ex::MakePlan(room, in, count, &p));
    CHECK(!p.valid && p.reason == reason);
    // A refusal draws and admits nothing: no partial room and no missing display.
    CHECK(p.bayCount == 0 && p.boxCount == 0 && p.routeCount == 0 && p.zoneCount == 0);
    CHECK(Ex::WalkAt(p, Ex::Vec2{0.0f, 0.0f}) == Ex::kWalkBlocked);
    CHECK(Ex::FocusBay(p, Ex::Vec2{0.0f, 0.0f}, Ex::Vec2{1.0f, 0.0f}, 1) == 0);
    CHECK(!Ex::FacesExit(p, Ex::Vec2{0.0f, 0.0f}, Ex::Vec2{0.0f, -1.0f}, true));
    CHECK(Ex::LabelForBay(p, 1) == -1 && std::strlen(Ex::ReasonText(p.reason)) > 2);
}

// The hall's own room (CeMuseumWorld: half width 11, front -4, back 30, the cinema door
// on the maxX wall, y 4.5 +- 2.8) and the six machines a Forseti visit shows, as the host measures them
// (radius half the footprint's diagonal): Battle Engine, tank, Air Trainer, Sat Turret,
// Forseti Trooper, City Building.
Ex::RoomSpec HallRoom() {
    Ex::RoomSpec r = {-11.0f, 11.0f, -4.0f, 30.0f, {0.0f, 0.0f}, 1, 4.5f - 2.8f, 4.5f + 2.8f};
    return r;
}
float Diagonal(float w, float d) { return 0.5f * std::sqrt(w * w + d * d); }
const Ex::DisplayInput kVisit[6] = {{0, Diagonal(2.3f, 3.2f), 2.3f}, {1, Diagonal(1.5f, 2.1f), 0.7f},
                                    {2, Diagonal(1.0f, 2.8f), 0.4f}, {3, Diagonal(1.6f, 3.1f), 1.0f},
                                    {4, Diagonal(0.2f, 0.2f), 0.3f}, {5, Diagonal(2.2f, 3.6f), 1.4f}};

// A small object is staged on a tall narrow plinth near the eye, seen from nearer than a
// machine; nothing about it is resized, and the rest of the hall plans as before.
void TestSmall() {
    CHECK(Ex::IsSmall(0.14f, 0.3f) && !Ex::IsSmall(1.49f, 0.4f) && !Ex::IsSmall(0.3f, 3.8f) && !Ex::IsSmall(0.3f, 0.5f));
    for (unsigned portal = 0; portal < 2; ++portal) {
        Ex::RoomSpec room = HallRoom();
        if (!portal) room.portalSide = 0;
        Ex::Plan p;
        CHECK(Ex::MakePlan(room, kVisit, 6, &p));
        CheckPlanSound(p, kVisit, 6);
        if (!p.valid) continue;
        const Ex::Bay &t = p.bays[4];
        CHECK(Ex::IsSmall(t.radius, t.height));
        // Its top 0.05 m under a 1.7 m eye, on a plinth under a metre across, seen from 2 m.
        CHECK(std::fabs(t.model.z + t.height - (Ex::kFloorTop + 1.65f)) < 1.0e-4f);
        CHECK(t.plinthHalf < 0.5f && t.plinthHalf >= Ex::kSmallHalfMin);
        CHECK(std::fabs(t.viewDistance - 2.0f) < 1.0e-4f);
        // The visitor cannot reach the object, and stands clear of its plinth on the pad.
        CHECK(Ex::WalkAt(p, Ex::Vec2{t.model.x, t.model.y}) == Ex::kWalkBlocked);
        CHECK(Ex::WalkAt(p, t.pad) == Ex::kWalkOpen);
        // The Air Trainer is low but long: it keeps its pedestal and a machine's distance.
        const Ex::Bay &a = p.bays[2];
        CHECK(!Ex::IsSmall(a.radius, a.height) && a.model.z <= Ex::kFloorTop + Ex::kPedestalHeight + 1.0e-6f);
        CHECK(a.viewDistance >= a.radius + Ex::kViewFloor - 1.0e-3f);
    }
    // Any count of soldiers alone, or among machines, still plans: a small object is never refused.
    const Ex::DisplayInput squad[6] = {{1, 0.14f, 0.3f}, {2, 0.2f, 0.45f}, {3, 0.05f, 0.05f},
                                       {4, 0.49f, 0.49f}, {5, 0.3f, 0.2f}, {6, 0.1f, 0.4f}};
    for (unsigned n = 1; n <= 6; ++n) {
        Ex::Plan p;
        CHECK(Ex::MakePlan(HallRoom(), squad, n, &p));
        CheckPlanSound(p, squad, n);
        CHECK(Ex::MakePlan(Room(), squad, n, &p));
        CheckPlanSound(p, squad, n);
    }
}

void TestPlans() {
    Ex::Plan p;
    CHECK(Ex::MakePlan(Room(), kSix, 6, &p));
    CheckPlanSound(p, kSix, 6);
    CHECK(p.boxCount <= Ex::kMaxBoxes && p.routeCount == 9);
    // Bays 1 to 3 run up one wall and 4 to 6 come back down the other.
    CHECK(p.bays[0].side == -1 && p.bays[2].side == -1 && p.bays[3].side == 1 && p.bays[5].side == 1);
    CHECK(p.bays[0].model.y < p.bays[1].model.y && p.bays[1].model.y < p.bays[2].model.y);
    CHECK(p.bays[3].model.y > p.bays[4].model.y && p.bays[4].model.y > p.bays[5].model.y);

    // The same inputs give the same plan whatever the output held before.
    // Both plans are made whatever the first one gives, so neither is printed unfilled.
    Ex::Plan a, b;
    std::memset(&a, 0xaa, sizeof a); std::memset(&b, 0x55, sizeof b);
    const bool madeA = Ex::MakePlan(Room(), kSix, 6, &a), madeB = Ex::MakePlan(Room(), kSix, 6, &b);
    CHECK(madeA && madeB);
    std::string ta, tb;
    Dump(a, &ta); Dump(b, &tb);
    CHECK(!ta.empty() && ta == tb);
    // A refused plan prints as refused and reads nothing past its own counts.
    Ex::Plan refused;
    std::memset(&refused, 0x55, sizeof refused);
    CHECK(!Ex::MakePlan(Room(), kSix, 7, &refused));
    std::string tr;
    Dump(refused, &tr);
    CHECK(tr == "refused reason " + std::to_string(int(Ex::kReasonBadCount)) + "\n");

    // Fewer displays, each alone, and other portal positions.
    for (unsigned n = 1; n <= 6; ++n) { CHECK(Ex::MakePlan(Room(), kSix, n, &p)); CheckPlanSound(p, kSix, n); }
    CHECK(Ex::MakePlan(Room(0), kSix, 6, &p)); CheckPlanSound(p, kSix, 6);
    CHECK(Ex::MakePlan(Room(-1, 8.0f, 12.0f), kSix, 6, &p)); CheckPlanSound(p, kSix, 6);
    CHECK(Ex::MakePlan(Room(1, -2.0f, 3.0f), kSix, 6, &p)); CheckPlanSound(p, kSix, 6);
    CHECK(Ex::MakePlan(Room(1, -5.5f, -2.5f), kSix, 6, &p)); CheckPlanSound(p, kSix, 6);
    Ex::RoomSpec shifted = Room();
    shifted.entrance = Ex::Vec2{-4.0f, 1.0f};
    CHECK(Ex::MakePlan(shifted, kSix, 6, &p)); CheckPlanSound(p, kSix, 6);

    // Regression guard: a portal in the middle of the wall splits it into two short
    // stretches. The even split no longer fits; the plan must move a bay across the
    // aisle and still keep every bay out of the portal and its forecourt.
    CHECK(Ex::MakePlan(Room(1, 20.0f, 26.0f), kSix, 6, &p)); CheckPlanSound(p, kSix, 6);
    for (unsigned i = 0; p.valid && i < p.bayCount; ++i) {
        const Ex::Bay &bay = p.bays[i];
        if (bay.side > 0) CHECK(bay.model.y + bay.plinthHalf <= 20.0f - Ex::kForecourtMargin + 1.0e-3f ||
                                bay.model.y - bay.plinthHalf >= 26.0f + Ex::kForecourtMargin - 1.0e-3f);
    }

    // Adversarial: one very wide machine among five ordinary ones. It must be
    // placed whole with a shortened but real standing distance, not shrunk or dropped.
    const Ex::DisplayInput wide[6] = {{1, 2.0f, 2.0f}, {2, 2.0f, 2.0f}, {3, 2.0f, 2.0f},
                                      {4, 2.0f, 2.0f}, {5, 2.0f, 2.0f}, {6, 11.0f, 6.0f}};
    CHECK(Ex::MakePlan(Room(), wide, 6, &p)); CheckPlanSound(p, wide, 6);
    CHECK(p.valid && p.bays[5].viewShortened && p.bays[5].radius == 11.0f && !p.bays[0].viewShortened);

    // Impossible or nonsensical input is refused outright.
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    Ex::DisplayInput bad[6];
    const float badRadius[] = {nan, inf, -1.0f, 0.0f, 41.0f};
    for (unsigned k = 0; k < 5; ++k) {
        std::memcpy(bad, kSix, sizeof bad); bad[2].radius = badRadius[k];
        ExpectRejected(Room(), bad, 6, Ex::kReasonBadDisplay);
    }
    std::memcpy(bad, kSix, sizeof bad); bad[5].height = nan; ExpectRejected(Room(), bad, 6, Ex::kReasonBadDisplay);
    std::memcpy(bad, kSix, sizeof bad); bad[0].height = -inf; ExpectRejected(Room(), bad, 6, Ex::kReasonBadDisplay);
    std::memcpy(bad, kSix, sizeof bad); bad[4].stableLabel = bad[1].stableLabel; ExpectRejected(Room(), bad, 6, Ex::kReasonDuplicateLabel);
    std::memcpy(bad, kSix, sizeof bad); bad[3].stableLabel = -1; ExpectRejected(Room(), bad, 6, Ex::kReasonDuplicateLabel);
    std::memcpy(bad, kSix, sizeof bad); bad[1].radius = 13.0f; ExpectRejected(Room(), bad, 6, Ex::kReasonTooDeep);
    for (unsigned i = 0; i < 6; ++i) bad[i].radius = 6.0f;
    ExpectRejected(Room(), bad, 6, Ex::kReasonNoFit);
    ExpectRejected(Room(), kSix, 0, Ex::kReasonBadCount);
    ExpectRejected(Room(), kSix, 7, Ex::kReasonBadCount);
    ExpectRejected(Room(), nullptr, 3, Ex::kReasonBadCount);
    Ex::RoomSpec room = Room(); room.maxX = nan; ExpectRejected(room, kSix, 6, Ex::kReasonBadRoom);
    room = Room(); room.minX = 29.0f; room.maxX = -29.0f; ExpectRejected(room, kSix, 6, Ex::kReasonBadRoom);
    room = Room(); room.entrance.y = -9.0f; ExpectRejected(room, kSix, 6, Ex::kReasonBadRoom);
    room = Room(); room.entrance.x = 27.0f; ExpectRejected(room, kSix, 6, Ex::kReasonBadRoom);
    ExpectRejected(Room(2), kSix, 6, Ex::kReasonBadRoom);
    ExpectRejected(Room(1, 10.0f, 10.5f), kSix, 6, Ex::kReasonBadRoom);
    ExpectRejected(Room(1, 40.0f, 46.0f), kSix, 6, Ex::kReasonBadRoom);
    ExpectRejected(Room(1, nan, 11.0f), kSix, 6, Ex::kReasonBadRoom);
    ExpectRejected(Room(1, 11.0f, 6.0f), kSix, 6, Ex::kReasonBadRoom);
    CHECK(!Ex::MakePlan(Room(), kSix, 6, nullptr));

    // Drawing data: thirty vertices inside the box, top in its colour, sides darker.
    CHECK(Ex::MakePlan(Room(), kSix, 6, &p));
    Ex::Vertex v[Ex::kBoxVertices];
    const Ex::Box *plinth = Find(p, Ex::kSolidPlinth, 2);
    CHECK(plinth && Ex::EmitBox(*plinth, v) == unsigned(Ex::kBoxVertices) && Ex::EmitBox(*plinth, nullptr) == 0);
    for (unsigned i = 0; plinth && i < unsigned(Ex::kBoxVertices); ++i) {
        CHECK(v[i].x >= plinth->min.x && v[i].x <= plinth->max.x && v[i].y >= plinth->min.y && v[i].y <= plinth->max.y);
        CHECK(v[i].z >= plinth->min.z && v[i].z <= plinth->max.z && (v[i].colour >> 24) == 0xffu);
        if (i < 6) CHECK(v[i].z == plinth->max.z && v[i].colour == plinth->colour);
        else CHECK(v[i].colour != plinth->colour);
    }
    const Ex::ViewPose low = Ex::RecommendedPose(p.bays[3], 0.5f), high = Ex::RecommendedPose(p.bays[3], 9.0f);
    CHECK(low.eye.x == p.bays[3].pad.x && low.eye.y == p.bays[3].pad.y && low.heading.x == 1.0f && low.heading.y == 0.0f);
    CHECK(low.pitch > 0.0f && high.pitch < 0.0f && std::fabs(low.pitch) < 1.2f);
}

// What any finished plan offers a visitor, whatever went into it.
void CheckPlanUse(const Ex::Plan &p) {
    if (!p.valid) return;
    const Ex::Vec2 e = p.room.entrance, out = {0.0f, -1.0f};
    CHECK(Ex::FacesExit(p, e, out, false) && !Ex::FacesExit(p, e, Ex::Vec2{0.0f, 1.0f}, true));
    CHECK(Ex::StandingBay(p, e, 0) == 0 && Ex::FocusBay(p, e, out, 0) == 0);
    // One way back, one spine, one line to every pad and, with a portal, one to the cinema.
    unsigned kinds[4] = {0, 0, 0, 0};
    for (unsigned i = 0; i < p.routeCount; ++i) {
        const Ex::Segment &s = p.route[i];
        const int kind = int(s.kind);
        CHECK(kind >= 0 && kind < 4);
        if (kind >= 0 && kind < 4) ++kinds[kind];
        CHECK(s.a.x == e.x && Ex::PathClear(p, s.a, s.b));
        const Ex::Bay *bay = Ex::BayByNumber(p, s.bay);
        if (s.kind == Ex::kRouteBay) CHECK(bay && s.b.x == bay->pad.x && s.b.y == bay->pad.y);
        else CHECK(s.bay == 0);
    }
    CHECK(kinds[Ex::kRouteReturn] == 1 && kinds[Ex::kRouteSpine] == 1 && kinds[Ex::kRouteBay] == p.bayCount);
    CHECK(kinds[Ex::kRouteCinema] == (p.room.portalSide ? 1u : 0u));
    for (unsigned i = 0; i < p.bayCount; ++i) {
        const Ex::Bay &b = p.bays[i];
        const Ex::Vec2 away = {-b.facing.x, -b.facing.y};
        CHECK(b.facing.x == float(b.side) && b.facing.y == 0.0f);
        CHECK(Ex::ZoneDistance(b, b.pad) == 0.0f && Ex::ZoneDistance(b, b.zoneEnd) < 1.0e-4f);
        // On its pad, looking at the machine: this bay's card. Turned to the aisle: none.
        CHECK(Ex::StandingBay(p, b.pad, 0) == b.number);
        CHECK(Ex::FocusBay(p, b.pad, b.facing, 0) == b.number);
        CHECK(Ex::FocusBay(p, b.pad, away, b.number) == 0);
        CHECK(!Ex::FacesExit(p, b.pad, out, false));
        const Ex::ViewPose pose = Ex::RecommendedPose(b, 1.7f);
        CHECK(pose.eye.x == b.pad.x && pose.eye.y == b.pad.y && pose.eye.z == 1.7f);
        CHECK(pose.heading.x == b.facing.x && pose.heading.y == b.facing.y && std::fabs(pose.pitch) < 1.3f);
        CHECK(Ex::FocusBay(p, Ex::Vec2{pose.eye.x, pose.eye.y}, pose.heading, 0) == b.number);
    }
}

// Which card a visitor is shown: standing in a bay and looking at its machine.
void TestFocus() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Ex::Plan p;
    CHECK(Ex::MakePlan(Room(), kSix, 6, &p));
    if (!p.valid) return;
    CheckPlanUse(p);
    const Ex::Vec2 entrance = p.room.entrance;
    for (unsigned i = 0; i < p.bayCount; ++i) {
        const Ex::Bay &b = p.bays[i];
        const int n = b.number, other = n % 6 + 1;
        const Ex::Vec2 away = {-b.facing.x, -b.facing.y};
        // Half the angle the machine's outline takes up, seen from the pad.
        const float edge = std::asin(b.radius / b.viewDistance);
        // On the pad, facing the machine: this bay, whatever was shown before.
        CHECK(Ex::FocusBay(p, b.pad, b.facing, 0) == n && Ex::FocusBay(p, b.pad, b.facing, n) == n);
        CHECK(Ex::FocusBay(p, b.pad, b.facing, other) == n);
        CHECK(Ex::FocusBay(p, b.pad, b.facing, 7) == n && Ex::FocusBay(p, b.pad, b.facing, -3) == n);
        CHECK(Ex::FocusBay(p, b.pad, Ex::Vec2{b.facing.x * 250.0f, b.facing.y * 250.0f}, 0) == n);
        CHECK(Ex::FocusBay(p, b.pad, Ex::Vec2{b.facing.x * 0.004f, b.facing.y * 0.004f}, 0) == n);
        for (int turn = -1; turn <= 1; turn += 2) {
            const float t = float(turn);
            // A small turn gains the card, a wider one only keeps it, wider still loses it.
            CHECK(Ex::FocusBay(p, b.pad, Turn(b.facing, t * (edge + 0.10f)), 0) == n);
            CHECK(Ex::FocusBay(p, b.pad, Turn(b.facing, t * (edge + 0.26f)), 0) == 0);
            CHECK(Ex::FocusBay(p, b.pad, Turn(b.facing, t * (edge + 0.26f)), n) == n);
            CHECK(Ex::FocusBay(p, b.pad, Turn(b.facing, t * (edge + 0.45f)), n) == 0);
            CHECK(Ex::FocusBay(p, b.pad, Turn(b.facing, t * 1.5708f), n) == 0);
        }
        // Looking away clears it at once.
        CHECK(Ex::FocusBay(p, b.pad, away, n) == 0 && Ex::FocusBay(p, b.pad, away, 0) == 0);

        // Stepping off the pad: the card is kept a little further out than it is gained.
        const Ex::Vec2 back1 = {b.pad.x - b.facing.x * 2.4f, b.pad.y}, back2 = {b.pad.x - b.facing.x * 3.2f, b.pad.y};
        const Ex::Vec2 side1 = {b.pad.x, b.pad.y + 2.4f}, side2 = {b.pad.x, b.pad.y - 3.2f};
        const Ex::Vec2 at1 = Toward(side1, b.model.x, b.model.y), at2 = Toward(side2, b.model.x, b.model.y);
        CHECK(Ex::PathClear(p, b.pad, back2));
        CHECK(Ex::WalkAt(p, side1) == Ex::kWalkOpen && Ex::WalkAt(p, side2) == Ex::kWalkOpen);
        CHECK(Ex::FocusBay(p, back1, b.facing, 0) == 0 && Ex::FocusBay(p, back1, b.facing, n) == n);
        CHECK(Ex::FocusBay(p, back2, b.facing, n) == 0);
        CHECK(Ex::FocusBay(p, side1, at1, 0) == 0 && Ex::FocusBay(p, side1, at1, n) == n);
        CHECK(Ex::FocusBay(p, side2, at2, n) == 0);
        CHECK(Ex::StandingBay(p, back1, 0) == 0 && Ex::StandingBay(p, back1, n) == n);
        CHECK(Ex::StandingBay(p, back2, n) == 0);

        // Close in, at the foot of the plinth.
        const Ex::Vec2 foot = {b.zoneEnd.x - b.facing.x * 0.7f, b.zoneEnd.y};
        CHECK(Ex::WalkAt(p, foot) == Ex::kWalkOpen && Ex::StandingBay(p, foot, 0) == n);
        CHECK(Ex::FocusBay(p, foot, b.facing, 0) == n && Ex::FocusBay(p, foot, away, n) == 0);

        // In the aisle, level with the machine and looking straight at it: too far to name it.
        const Ex::Vec2 aisle = {entrance.x, b.model.y};
        CHECK(Ex::FocusBay(p, aisle, b.facing, 0) == 0 && Ex::FocusBay(p, aisle, b.facing, n) == 0);
        CHECK(Ex::StandingBay(p, aisle, n) == 0);

        // From this pad, looking at any other machine never names that machine.
        for (unsigned j = 0; j < p.bayCount; ++j) {
            if (j == i) continue;
            const Ex::Bay &o = p.bays[j];
            const Ex::Vec2 at = Toward(b.pad, o.model.x, o.model.y);
            const int fresh = Ex::FocusBay(p, b.pad, at, 0), stale = Ex::FocusBay(p, b.pad, at, o.number);
            CHECK(fresh == 0 || fresh == n);
            CHECK(stale == 0 || stale == n);
        }
    }
    // Nowhere near a bay, or with nonsense for a position or heading: nothing.
    CHECK(Ex::StandingBay(p, entrance, 0) == 0 && Ex::StandingBay(p, entrance, 3) == 0);
    CHECK(Ex::FocusBay(p, Ex::Vec2{nan, 0.0f}, Ex::Vec2{1.0f, 0.0f}, 1) == 0);
    CHECK(Ex::StandingBay(p, Ex::Vec2{0.0f, nan}, 1) == 0);
    CHECK(Ex::FocusBay(p, p.bays[0].pad, Ex::Vec2{0.0f, 0.0f}, 1) == 0);
    CHECK(Ex::FocusBay(p, p.bays[0].pad, Ex::Vec2{nan, nan}, 1) == 0);
}

// Six small machines in a short room: neighbouring bays as close as a plan puts them.
const Ex::DisplayInput kSmall[6] = {{11, 0.3f, 0.8f}, {12, 0.3f, 1.0f}, {13, 0.3f, 1.2f},
                                    {14, 0.3f, 1.4f}, {15, 0.3f, 1.6f}, {16, 0.3f, 1.8f}};
Ex::RoomSpec SmallRoom() {
    Ex::RoomSpec r = {-14.0f, 14.0f, -4.0f, 14.0f, {0.0f, 0.0f}, 0, 0.0f, 0.0f};
    return r;
}

// Two bays side by side: the card must not flicker or jump between them.
void TestNeighbours() {
    Ex::Plan p;
    CHECK(Ex::MakePlan(SmallRoom(), kSmall, 6, &p));
    CheckPlanSound(p, kSmall, 6);
    if (!p.valid) return;
    CheckPlanUse(p);
    const Ex::Bay &a = p.bays[0], &b = p.bays[1];
    const float gap = b.model.y - a.model.y, x = a.pad.x, y0 = a.model.y;
    // The fixture's premise: bays 1 and 2 share a wall and stand between 4.1 and 4.3 apart.
    CHECK(a.side == -1 && b.side == -1 && a.pad.x == b.pad.x && gap > 4.1f && gap < 4.3f);
    CHECK(Ex::PathClear(p, a.pad, b.pad));

    // Half way: outside both zones. Nothing new is shown; what was shown stays.
    const Ex::Vec2 mid = {x, y0 + 0.5f * gap};
    CHECK(Ex::StandingBay(p, mid, 0) == 0 && Ex::StandingBay(p, mid, 1) == 1 && Ex::StandingBay(p, mid, 2) == 2);
    CHECK(Ex::StandingBay(p, mid, 3) == 0);
    // Just bay 1's side of half way: bay 1 for a newcomer, but bay 2 is still held.
    const Ex::Vec2 seam = {x, y0 + 0.5f * gap - 0.175f};
    CHECK(Ex::StandingBay(p, seam, 0) == 1 && Ex::StandingBay(p, seam, 1) == 1 && Ex::StandingBay(p, seam, 2) == 2);
    // Well into bay 1's zone: bay 2 is let go.
    const Ex::Vec2 well = {x, y0 + 0.5f * gap - 0.5f};
    CHECK(Ex::StandingBay(p, well, 0) == 1 && Ex::StandingBay(p, well, 2) == 1);

    // The card needs the right machine in view as well.
    const Ex::Vec2 midA = Toward(mid, a.model.x, a.model.y), midB = Toward(mid, b.model.x, b.model.y);
    CHECK(Ex::FocusBay(p, mid, midA, 0) == 0 && Ex::FocusBay(p, mid, midB, 0) == 0);
    CHECK(Ex::FocusBay(p, mid, midA, 1) == 1 && Ex::FocusBay(p, mid, midB, 2) == 2);
    CHECK(Ex::FocusBay(p, mid, midA, 2) == 0 && Ex::FocusBay(p, mid, midB, 1) == 0);
    const Ex::Vec2 seamA = Toward(seam, a.model.x, a.model.y), seamB = Toward(seam, b.model.x, b.model.y);
    CHECK(Ex::FocusBay(p, seam, seamA, 0) == 1 && Ex::FocusBay(p, seam, seamA, 2) == 1);
    CHECK(Ex::FocusBay(p, seam, seamB, 0) == 0 && Ex::FocusBay(p, seam, seamB, 2) == 2);
    CHECK(Ex::FocusBay(p, seam, seamB, 1) == 0);

    // Walking pad to pad while facing the wall: one card, then none, then the next.
    const int steps = 200;
    int seen[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    unsigned count = 0;
    float droppedAt = -1.0f, gainedAt = -1.0f;
    int held = Ex::FocusBay(p, a.pad, a.facing, 0);
    seen[count++] = held;
    for (int k = 1; k <= steps; ++k) {
        const float along = gap * float(k) / float(steps);
        const int now = Ex::FocusBay(p, Ex::Vec2{x, y0 + along}, a.facing, held);
        if (now != held) {
            if (count < 8) seen[count++] = now;
            if (held == 1 && droppedAt < 0.0f) droppedAt = along;
        }
        held = now;
    }
    CHECK(count == 3 && seen[0] == 1 && seen[1] == 0 && seen[2] == 2);
    // And back again.
    count = 0;
    held = Ex::FocusBay(p, b.pad, b.facing, 0);
    seen[count++] = held;
    for (int k = steps - 1; k >= 0; --k) {
        const float along = gap * float(k) / float(steps);
        const int now = Ex::FocusBay(p, Ex::Vec2{x, y0 + along}, a.facing, held);
        if (now != held) {
            if (count < 8) seen[count++] = now;
            if (now == 1 && gainedAt < 0.0f) gainedAt = along;
        }
        held = now;
    }
    CHECK(count == 3 && seen[0] == 2 && seen[1] == 0 && seen[2] == 1);
    // Bay 1's card is let go further from its pad than it is gained.
    CHECK(droppedAt > 0.0f && gainedAt > 0.0f && droppedAt > gainedAt + 0.3f);

    // A visitor shifting about where the card is first gained does not make it flicker:
    // a tenth either side of that point, beginning outside it.
    unsigned flips = 0;
    held = 0;
    for (int k = 0; k < 40; ++k) {
        const int now = Ex::FocusBay(p, Ex::Vec2{x, y0 + gainedAt + (k % 2 ? -0.1f : 0.1f)}, a.facing, held);
        flips += now != held;
        held = now;
    }
    CHECK(flips == 1 && held == 1);
}

// The way out: offered at the return door, facing it, and nowhere else.
void TestExit() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Ex::Plan p;
    CHECK(Ex::MakePlan(Room(), kSix, 6, &p));
    if (!p.valid) return;
    const Ex::Vec2 e = p.room.entrance, door = p.doorCentre, out = {0.0f, -1.0f};
    const Ex::Box *leaf = Find(p, Ex::kSolidDoor, 0);
    CHECK(leaf && door.x == e.x && door.y < e.y);
    if (!leaf) return;
    CHECK(std::fabs(leaf->max.y - door.y) < 1.0e-5f && std::fabs(0.5f * (leaf->min.x + leaf->max.x) - door.x) < 1.0e-5f);
    CHECK(std::fabs(0.5f * (leaf->max.x - leaf->min.x) - p.doorHalf) < 1.0e-5f && leaf->max.z > Ex::kBodyTop);

    // The closed door stops a visitor; the floor in front of it does not.
    const Ex::Vec2 threshold = {door.x, door.y + Ex::kVisitorRadius + 0.12f};
    CHECK(Ex::WalkAt(p, Ex::Vec2{door.x, door.y + 0.3f}) == Ex::kWalkBlocked);
    CHECK(Ex::WalkAt(p, threshold) == Ex::kWalkOpen && Ex::PathClear(p, e, threshold));

    // Facing it from the entrance or the threshold offers the way out; other headings do not.
    CHECK(Ex::FacesExit(p, e, out, false) && Ex::FacesExit(p, threshold, out, false));
    CHECK(Ex::FacesExit(p, e, Ex::Vec2{0.0f, -40.0f}, false));
    CHECK(!Ex::FacesExit(p, e, Ex::Vec2{0.0f, 1.0f}, true) && !Ex::FacesExit(p, e, Ex::Vec2{1.0f, 0.0f}, true));
    CHECK(!Ex::FacesExit(p, e, Ex::Vec2{-1.0f, 0.0f}, true) && !Ex::FacesExit(p, e, Ex::Vec2{0.0f, 0.0f}, true));
    CHECK(!Ex::FacesExit(p, e, Ex::Vec2{nan, -1.0f}, true));

    for (int s = -1; s <= 1; s += 2) {
        const float t = float(s);
        // Along the door: gained inside the leaf, kept just past its edge, lost beyond.
        CHECK(Ex::FacesExit(p, e, Toward(e, door.x + t * (p.doorHalf - 0.3f), door.y), false));
        CHECK(!Ex::FacesExit(p, e, Toward(e, door.x + t * (p.doorHalf + 0.02f), door.y), false));
        CHECK(Ex::FacesExit(p, e, Toward(e, door.x + t * (p.doorHalf + 0.02f), door.y), true));
        CHECK(!Ex::FacesExit(p, e, Toward(e, door.x + t * (p.doorHalf + 0.4f), door.y), true));
        // To either side of the entrance: gained close by, kept a little wider, lost beyond.
        const Ex::Vec2 inner = {e.x + t * 2.6f, e.y}, wide = {e.x + t * 3.2f, e.y}, outer = {e.x + t * 3.8f, e.y};
        CHECK(Ex::FacesExit(p, inner, Toward(inner, door.x, door.y), false));
        CHECK(!Ex::FacesExit(p, wide, Toward(wide, door.x, door.y), false));
        CHECK(Ex::FacesExit(p, wide, Toward(wide, door.x, door.y), true));
        CHECK(!Ex::FacesExit(p, outer, Toward(outer, door.x, door.y), true));
        // Walking straight at the wall beside the door is not facing the door.
        CHECK(!Ex::FacesExit(p, wide, out, true));
        // A glancing heading counts only close to the door, and only to keep the cue.
        CHECK(Ex::FacesExit(p, threshold, Ex::Vec2{t, -0.7f}, false));
        CHECK(!Ex::FacesExit(p, threshold, Ex::Vec2{t, -0.5f}, false) && Ex::FacesExit(p, threshold, Ex::Vec2{t, -0.5f}, true));
        CHECK(!Ex::FacesExit(p, threshold, Ex::Vec2{t, -0.3f}, true));
    }

    // Up the aisle: gained near the entrance, kept a few steps further, never from deep in the room.
    CHECK(Ex::FacesExit(p, Ex::Vec2{e.x, e.y + 1.6f}, out, false));
    CHECK(!Ex::FacesExit(p, Ex::Vec2{e.x, e.y + 2.4f}, out, false) && Ex::FacesExit(p, Ex::Vec2{e.x, e.y + 2.4f}, out, true));
    CHECK(!Ex::FacesExit(p, Ex::Vec2{e.x, e.y + 3.2f}, out, true) && !Ex::FacesExit(p, Ex::Vec2{e.x, e.y + 10.0f}, out, true));
    CHECK(!Ex::FacesExit(p, Ex::Vec2{door.x, door.y - 0.4f}, out, true));
    for (unsigned i = 0; i < p.bayCount; ++i) {
        const Ex::Vec2 pad = p.bays[i].pad;
        CHECK(!Ex::FacesExit(p, pad, out, true) && !Ex::FacesExit(p, pad, Toward(pad, door.x, door.y), true));
    }

    // Turning away and back at the entrance: the cue goes once and returns once,
    // and returns at a narrower angle than it went.
    bool shown = Ex::FacesExit(p, e, out, false);
    unsigned changes = 0;
    float wentAt = -1.0f, cameAt = -1.0f;
    for (int k = 0; k <= 160; ++k) {
        const float angle = 0.005f * float(k);
        const bool now = Ex::FacesExit(p, e, Turn(out, angle), shown);
        if (now != shown) { ++changes; if (!now) wentAt = angle; }
        shown = now;
    }
    CHECK(changes == 1 && !shown && wentAt > 0.0f);
    for (int k = 160; k >= 0; --k) {
        const float angle = 0.005f * float(k);
        const bool now = Ex::FacesExit(p, e, Turn(out, angle), shown);
        if (now != shown) { ++changes; if (now) cameAt = angle; }
        shown = now;
    }
    CHECK(changes == 2 && shown && cameAt > 0.0f && wentAt > cameAt + 0.015f);
}

// Moving the whole room moves the whole plan and changes nothing else.
void TestTranslation() {
    Ex::Plan base;
    CHECK(Ex::MakePlan(Room(), kSix, 6, &base));
    const float shift[4][2] = {{64.0f, -32.0f}, {-128.0f, 16.0f}, {7.25f, 91.5f}, {-100.0f, -75.0f}};
    for (unsigned k = 0; k < 4; ++k) {
        const float tx = shift[k][0], ty = shift[k][1];
        Ex::RoomSpec room = Room();
        room.minX += tx; room.maxX += tx; room.minY += ty; room.maxY += ty;
        room.entrance.x += tx; room.entrance.y += ty; room.portalMinY += ty; room.portalMaxY += ty;
        Ex::Plan moved;
        CHECK(Ex::MakePlan(room, kSix, 6, &moved));
        CheckPlanSound(moved, kSix, 6);
        CheckPlanUse(moved);
        const bool alike = base.valid && moved.valid && moved.bayCount == base.bayCount &&
                           moved.boxCount == base.boxCount && moved.routeCount == base.routeCount;
        CHECK(alike);
        if (!alike) continue;
        float worst = 0.0f;
        auto off = [&worst](float got, float want) { const float d = std::fabs(got - want); if (!(d <= worst)) worst = d; };
        for (unsigned i = 0; i < base.bayCount; ++i) {
            const Ex::Bay &a = base.bays[i], &m = moved.bays[i];
            CHECK(a.number == m.number && a.stableLabel == m.stableLabel && a.side == m.side && a.viewShortened == m.viewShortened);
            off(m.model.x, a.model.x + tx); off(m.model.y, a.model.y + ty); off(m.model.z, a.model.z);
            off(m.pad.x, a.pad.x + tx); off(m.pad.y, a.pad.y + ty);
            off(m.stand.x, a.stand.x + tx); off(m.stand.y, a.stand.y + ty);
            off(m.zoneEnd.x, a.zoneEnd.x + tx); off(m.zoneEnd.y, a.zoneEnd.y + ty);
            off(m.viewDistance, a.viewDistance); off(m.plinthHalf, a.plinthHalf);
        }
        for (unsigned i = 0; i < base.boxCount; ++i) {
            const Ex::Box &a = base.boxes[i], &m = moved.boxes[i];
            CHECK(a.purpose == m.purpose && a.bay == m.bay && a.colour == m.colour);
            off(m.min.x, a.min.x + tx); off(m.min.y, a.min.y + ty); off(m.min.z, a.min.z);
            off(m.max.x, a.max.x + tx); off(m.max.y, a.max.y + ty); off(m.max.z, a.max.z);
        }
        for (unsigned i = 0; i < base.routeCount; ++i) {
            const Ex::Segment &a = base.route[i], &m = moved.route[i];
            CHECK(a.kind == m.kind && a.bay == m.bay);
            off(m.a.x, a.a.x + tx); off(m.a.y, a.a.y + ty); off(m.b.x, a.b.x + tx); off(m.b.y, a.b.y + ty);
        }
        off(moved.doorCentre.x, base.doorCentre.x + tx); off(moved.doorCentre.y, base.doorCentre.y + ty);
        off(moved.forecourt.x, base.forecourt.x + tx); off(moved.forecourt.y, base.forecourt.y + ty);
        CHECK(worst < 2.0e-3f);
    }
}

// A small generator with a fixed seed, so every run draws the same rooms and frames.
unsigned gSeed = 1u;
unsigned Draw() { gSeed = gSeed * 1664525u + 1013904223u; return gSeed >> 8; }   // 24 bits
float Unit() { return float(Draw()) * (1.0f / 16777216.0f); }                    // 0 up to, not including, 1
bool Chance(float p) { return Unit() < p; }

// Many invented rooms and line-ups. Each is either planned whole and sound, or
// refused for a reason a caller could act on; never half-built.
void TestGenerated() {
    gSeed = 20261114u;
    unsigned placed = 0, refused = 0, unexplained = 0;
    for (unsigned n = 0; n < 400u; ++n) {
        const float halfWidth = 12.0f + 28.0f * Unit();
        const float cx = 60.0f * Unit() - 30.0f;
        const float cy = 60.0f * Unit() - 30.0f;
        Ex::RoomSpec room;
        room.minX = cx - halfWidth; room.maxX = cx + halfWidth;
        // At least eight clear from the entrance to either side wall.
        room.entrance.x = cx + (halfWidth - 9.0f) * (2.0f * Unit() - 1.0f);
        room.entrance.y = cy;
        room.minY = cy - 2.6f - 6.0f * Unit();
        room.maxY = cy + 11.0f + 45.0f * Unit();
        room.portalSide = int(Draw() % 3u) - 1;
        const float width = 2.0f + 5.0f * Unit();
        const float lowest = room.minY + 0.5f, highest = room.maxY - 0.5f - width;
        room.portalMinY = lowest + (highest - lowest) * Unit();
        room.portalMaxY = room.portalMinY + width;
        Ex::DisplayInput displays[Ex::kMaxBays];
        const unsigned count = 1u + Draw() % 6u;
        for (unsigned i = 0; i < count; ++i) {
            const float u = Unit();
            displays[i].stableLabel = int(n * 10u + i);
            displays[i].radius = 0.3f + 6.0f * u * u;
            displays[i].height = 0.4f + 9.0f * Unit();
        }
        Ex::Plan p;
        if (Ex::MakePlan(room, displays, count, &p)) {
            ++placed;
            CheckPlanSound(p, displays, count);
            CheckPlanUse(p);
        } else {
            ++refused;
            const bool explained = p.reason == Ex::kReasonTooDeep || p.reason == Ex::kReasonNoFit;
            if (!explained && !unexplained++)
                std::printf("generated room %u (%u displays) refused: %s\n", n, count, Ex::ReasonText(p.reason));
            CHECK(!p.valid && p.bayCount == 0 && p.boxCount == 0 && p.routeCount == 0);
            CHECK(Ex::WalkAt(p, room.entrance) == Ex::kWalkBlocked && Ex::LabelForBay(p, 1) == -1);
        }
    }
    std::printf("generated rooms: %u planned, %u refused\n", placed, refused);
    CHECK(unexplained == 0);
    CHECK(placed >= 20 && refused >= 1);
}

bool IsRail(const Ex::Box &b) { return b.purpose == Ex::kSolidRail; }
bool IsStand(const Ex::Box &b) { return b.purpose == Ex::kSolidStand; }
bool IsDrawnOnly(const Ex::Box &b) { return !Ex::IsSolid(b.purpose); }
unsigned Shared(const Ex::Plan &p) {
    unsigned n = 0;
    for (unsigned i = 0; i < p.boxCount; ++i)
        for (unsigned j = i + 1; j < p.boxCount; ++j) n += Shares(p.boxes[i], p.boxes[j]);
    return n;
}

// The hall as architecture: what each kind of piece is for, shown by what changes
// when it is there, absent or in the wrong place.
void TestHall() {
    Ex::Plan p;
    CHECK(Ex::MakePlan(Room(), kSix, 6, &p));
    if (!p.valid) return;
    const float eye = Ex::kEyeHeight, ex = p.room.entrance.x;

    // Every kind of piece is present; planning with empty options is the same plan.
    unsigned kinds[5] = {0, 0, 0, 0, 0};
    for (unsigned i = 0; i < p.boxCount; ++i) ++kinds[Ex::CategoryOf(p.boxes[i].purpose)];
    CHECK(kinds[Ex::kCategoryEnclosure] && kinds[Ex::kCategoryDisplay] && kinds[Ex::kCategoryBarrier]);
    CHECK(kinds[Ex::kCategoryTrim] && kinds[Ex::kCategoryFloor]);
    const Ex::PlanOptions none = {nullptr, 0u, false};
    Ex::Plan same;
    std::memset(&same, 0x33, sizeof same);
    CHECK(Ex::MakePlan(Room(), kSix, 6, none, &same));
    std::string ta, tb;
    Dump(p, &ta); Dump(same, &tb);
    CHECK(!ta.empty() && ta == tb);

    // The cinema's doorway is framed: a jamb at each edge and one header overhead.
    // A room with no portal has none of it.
    unsigned jambs = 0;
    for (unsigned i = 0; i < p.boxCount; ++i) {
        const Ex::Box &b = p.boxes[i];
        jambs += b.purpose == Ex::kSolidPier && (std::fabs(b.max.y - 6.0f) < 1.0e-4f || std::fabs(b.min.y - 11.0f) < 1.0e-4f);
    }
    CHECK(Count(p, Ex::kSolidHeader) == 1 && jambs == 2);
    Ex::Plan q;
    CHECK(Ex::MakePlan(Room(0), kSix, 6, &q));
    CHECK(q.valid && Count(q, Ex::kSolidHeader) == 0 && !Ex::ZoneFor(q, Ex::kZoneApproach) && q.portalClearHeight == 0.0f);

    // Each machine has its own bay: one pier stands at the wall between neighbours.
    for (unsigned i = 0; i + 1 < p.bayCount; ++i) {
        const Ex::Bay &a = p.bays[i], &b = p.bays[i + 1];
        if (a.side != b.side) continue;
        const bool up = a.model.y < b.model.y;
        const float lo = up ? a.model.y + a.plinthHalf : b.model.y + b.plinthHalf;
        const float hi = up ? b.model.y - b.plinthHalf : a.model.y - a.plinthHalf;
        unsigned between = 0;
        for (unsigned k = 0; k < p.boxCount; ++k) {
            const Ex::Box &x = p.boxes[k];
            between += x.purpose == Ex::kSolidPier && x.min.y > lo && x.max.y < hi &&
                       (a.side < 0 ? x.min.x < a.model.x : x.max.x > a.model.x);
        }
        CHECK(between == 1);
    }

    // Bays 4 to 6 stand close along one wall, so the pockets between them are railed
    // off; bays 1 to 3 have room between them and their recesses stay open floor.
    CHECK(Count(p, Ex::kSolidRail) == 2);
    for (unsigned i = 0; i < p.boxCount; ++i)
        if (p.boxes[i].purpose == Ex::kSolidRail) CHECK(p.boxes[i].max.z < 0.8f && p.boxes[i].min.x > ex);
    const Reach all = Flood(p, 0.25f);
    for (unsigned i = 0; i < p.bayCount; ++i) CHECK(all.At(p.bays[i].pad));
    CHECK(all.At(p.forecourt) && all.At(Ex::Vec2{p.doorCentre.x, p.doorCentre.y + 0.75f}));
    const Ex::Vec2 recess = {p.bays[0].model.x, 0.5f * (p.bays[0].model.y + p.bays[0].plinthHalf +
                                                        p.bays[1].model.y - p.bays[1].plinthHalf)};
    CHECK(Ex::WalkAt(p, recess) == Ex::kWalkOpen && all.At(recess));
    const Ex::Vec2 pocket = {p.bays[4].model.x, 0.5f * (p.bays[5].model.y + p.bays[5].plinthHalf +
                                                        p.bays[4].model.y - p.bays[4].plinthHalf)};
    CHECK(Ex::WalkAt(p, pocket) == Ex::kWalkOpen && !all.At(pocket));
    // The rail is what closes it: take the rails away and the pocket can be walked into.
    const Reach unrailed = Flood(Without(p, IsRail), 0.25f);
    CHECK(unrailed.At(pocket) && unrailed.At(p.bays[4].pad));

    // The flood has teeth: one bar across the room cuts every bay and the cinema off.
    Ex::Plan barred = p;
    const Ex::Box bar = {{p.room.minX, 2.0f, Ex::kFloorTop}, {p.room.maxX, 2.2f, 1.0f}, 0xff000000u, Ex::kSolidRail, 0};
    barred.boxes[barred.boxCount++] = bar;
    const Reach cut = Flood(barred, 0.25f);
    CHECK(cut.At(p.room.entrance) && !cut.At(p.bays[2].pad) && !cut.At(p.bays[5].pad) && !cut.At(p.forecourt));
    CHECK(Ex::PathClear(p, p.room.entrance, Ex::Vec2{ex, 8.5f}) && !Ex::PathClear(barred, p.room.entrance, Ex::Vec2{ex, 8.5f}));

    // A drawn-only piece never stops a visitor: without any of them the walkable floor
    // is the same everywhere. A solid one does: without the stands, a stand's place opens.
    const Ex::Plan bare = Without(p, IsDrawnOnly);
    CHECK(bare.boxCount > 0 && bare.boxCount < p.boxCount);
    unsigned points = 0, differ = 0;
    for (float x = p.room.minX - 1.0f; x <= p.room.maxX + 1.0f; x += 0.7f)
        for (float y = p.room.minY - 1.0f; y <= p.room.maxY + 1.0f; y += 0.7f) {
            ++points;
            differ += Ex::WalkAt(p, Ex::Vec2{x, y}) != Ex::WalkAt(bare, Ex::Vec2{x, y});
        }
    CHECK(points > 1000 && differ == 0);
    const Ex::Bay &s = p.bays[1];
    const Ex::Vec2 tip = {s.stand.x, s.stand.y - float(s.side) * 0.30f};
    const Ex::Plan unlabelled = Without(p, IsStand);
    CHECK(Ex::WalkAt(p, tip) == Ex::kWalkBlocked && Ex::WalkAt(unlabelled, tip) == Ex::kWalkOpen);

    // The overlap measure has teeth: a stand pushed into its plinth is caught.
    Ex::Plan nudged = p;
    for (unsigned i = 0; i < nudged.boxCount; ++i)
        if (nudged.boxes[i].purpose == Ex::kSolidStand && nudged.boxes[i].bay == 1) {
            nudged.boxes[i].min.y -= 0.1f;
            nudged.boxes[i].max.y -= 0.1f;
        }
    CHECK(Shared(p) == 0 && Shared(nudged) > 0);

    // The sight measure has teeth, and is not the walk measure: a banner of trim hung
    // between pad and machine hides the machine and stops nobody.
    Ex::Plan screened = p;
    const float sx = 0.5f * (s.pad.x + s.zoneEnd.x);
    const Ex::Box banner = {{sx - 0.1f, s.pad.y - 1.0f, Ex::kFloorTop}, {sx + 0.1f, s.pad.y + 1.0f, 3.0f}, 0xff000000u, Ex::kDecorTrim, 0};
    screened.boxes[screened.boxCount++] = banner;
    const Ex::ViewPose pose = Ex::RecommendedPose(s, eye);
    CHECK(Ex::SightClear(p, pose.eye, s.lookAt) && !Ex::SightClear(screened, pose.eye, s.lookAt));
    CHECK(Ex::WalkAt(screened, Ex::Vec2{sx, s.pad.y}) == Ex::kWalkOpen);
    CHECK(!Ex::SightClear(p, Ex::Vec3{std::numeric_limits<float>::quiet_NaN(), 0.0f, eye}, s.lookAt));

    // The walls rise for a tall machine, to a limit, and never change the machine.
    const Ex::DisplayInput tall[3] = {{1, 2.0f, 12.0f}, {2, 1.0f, 80.0f}, {3, 1.5f, 3.0f}};
    CHECK(Ex::MakePlan(Room(), tall, 1, &q)); CheckPlanSound(q, tall, 1);
    CHECK(q.wallHeight > 13.0f && q.wallHeight < Ex::kWallHeightMax);
    CHECK(Ex::MakePlan(Room(), tall, 3, &q)); CheckPlanSound(q, tall, 3);
    CHECK(q.wallHeight == Ex::kWallHeightMax && q.bays[1].height == 80.0f);
    CHECK(Ex::MakePlan(Room(), tall + 2, 1, &q)); CheckPlanSound(q, tall + 2, 1);
    CHECK(q.wallHeight == Ex::kWallHeight);

    // No displays: refused by default (TestPlans), and on request an empty hall that
    // still has its arrival, its nave and its way to the cinema.
    const Ex::PlanOptions empty = {nullptr, 0u, true};
    CHECK(Ex::MakePlan(Room(), nullptr, 0, empty, &q));
    CheckPlanSound(q, kSix, 0);
    CheckPlanUse(q);
    CHECK(q.valid && q.bayCount == 0 && Count(q, Ex::kSolidPlinth) == 0 && Count(q, Ex::kSolidRail) == 0);
    CHECK(Ex::PathClear(q, q.room.entrance, Ex::Vec2{ex, 8.5f}) && Ex::PathClear(q, Ex::Vec2{ex, 8.5f}, q.forecourt));
}

// How much of the plan's own furniture, structure or bay markings stands on a rectangle.
unsigned Built(const Ex::Plan &p, const Ex::HostKeep &k) {
    unsigned n = 0;
    for (unsigned i = 0; i < p.boxCount; ++i) {
        const Ex::Box &b = p.boxes[i];
        const Ex::Category c = Ex::CategoryOf(b.purpose);
        const bool own = c == Ex::kCategoryDisplay || c == Ex::kCategoryBarrier || b.purpose == Ex::kSolidPier || b.bay != 0;
        n += own && OverRect(b, k.minX, k.minY, k.maxX, k.maxY);
    }
    return n;
}
void ExpectReserved(const Ex::RoomSpec &room, const Ex::DisplayInput *in, unsigned count, const Ex::PlanOptions &options,
                    Ex::Reason reason) {
    Ex::Plan p;
    std::memset(&p, 0x5a, sizeof p);
    CHECK(!Ex::MakePlan(room, in, count, options, &p));
    CHECK(!p.valid && p.reason == reason);
    CHECK(p.bayCount == 0 && p.boxCount == 0 && p.routeCount == 0 && p.zoneCount == 0);
    CHECK(Ex::WalkAt(p, room.entrance) == Ex::kWalkBlocked && !Ex::ZoneFor(p, Ex::kZoneNave));
    CHECK(!Ex::SightClear(p, Ex::Vec3{0.0f, 0.0f, 1.7f}, Ex::Vec3{0.0f, 5.0f, 1.7f}));
    CHECK(std::strlen(Ex::ReasonText(p.reason)) > 2);
}

// Floor the host has filled itself: built around, echoed back, or refused.
void TestReservations() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Ex::Plan open, q;
    CHECK(Ex::MakePlan(Room(), kSmall, 4, &open));
    const Ex::HostKeep keep[1] = {{-28.0f, 10.0f, -20.0f, 16.0f}};
    const Ex::PlanOptions held = {keep, 1u, false};
    CHECK(Ex::MakePlan(Room(), kSmall, 4, held, &q));
    CheckPlanSound(q, kSmall, 4);
    CheckPlanUse(q);
    if (open.valid && q.valid) {
        // Left alone, a bay stands on that floor. Reserved, nothing of the plan's does,
        // every display is still shown, and the reservation is echoed as a zone.
        CHECK(Built(open, keep[0]) > 0 && Built(q, keep[0]) == 0);
        const Ex::Zone *z = Ex::ZoneFor(q, Ex::kZoneHost, 1);
        CHECK(z && z->min.x == -28.0f && z->min.y == 10.0f && z->max.x == -20.0f && z->max.y == 16.0f);
        CHECK(!Ex::ZoneFor(open, Ex::kZoneHost, 1));
        unsigned crossed = 0;
        for (unsigned i = 0; i < q.zoneCount; ++i) {
            const Ex::Zone &v = q.zones[i];
            crossed += v.kind != Ex::kZoneHost && v.min.x < keep[0].maxX && v.max.x > keep[0].minX &&
                       v.min.y < keep[0].maxY && v.max.y > keep[0].minY;
        }
        CHECK(crossed == 0);
    }

    // A reservation outside the room changes nothing.
    const Ex::HostKeep far[1] = {{100.0f, 0.0f, 110.0f, 5.0f}};
    const Ex::PlanOptions afar = {far, 1u, false};
    CHECK(Ex::MakePlan(Room(), kSmall, 4, afar, &q));
    std::string ta, tb;
    Dump(open, &ta); Dump(q, &tb);
    CHECK(!ta.empty() && ta == tb);

    // One on the aisle, reaching into it, or on the way to the cinema cannot be
    // planned around: refused whole. Just clear of the cinema's approach it is fine.
    const Ex::HostKeep inAisle[1] = {{-1.0f, 20.0f, 1.0f, 22.0f}}, intoAisle[1] = {{-10.0f, 20.0f, -2.5f, 22.0f}};
    const Ex::HostKeep atPortal[1] = {{20.0f, 9.0f, 24.0f, 13.0f}}, pastPortal[1] = {{20.0f, 13.0f, 24.0f, 15.0f}};
    const Ex::PlanOptions a = {inAisle, 1u, false}, b = {intoAisle, 1u, false}, c = {atPortal, 1u, false};
    const Ex::PlanOptions d = {pastPortal, 1u, false};
    ExpectReserved(Room(), kSmall, 4, a, Ex::kReasonReserved);
    ExpectReserved(Room(), kSmall, 4, b, Ex::kReasonReserved);
    ExpectReserved(Room(), kSmall, 4, c, Ex::kReasonReserved);
    CHECK(Ex::MakePlan(Room(), kSmall, 4, d, &q));
    CheckPlanSound(q, kSmall, 4);
    CHECK(q.valid && Built(q, pastPortal[0]) == 0);

    // Nonsense is refused, not guessed at.
    const Ex::HostKeep bad[4] = {{nan, 0.0f, 5.0f, 1.0f}, {5.0f, 5.0f, 5.0f, 9.0f}, {0.0f, 0.0f, 5000.0f, 1.0f}, {24.0f, 30.0f, 20.0f, 33.0f}};
    for (unsigned k = 0; k < 4; ++k) {
        const Ex::PlanOptions o = {&bad[k], 1u, false};
        ExpectReserved(Room(), kSmall, 4, o, Ex::kReasonReserved);
    }
    const Ex::HostKeep five[5] = {{-28.0f, 30.0f, -27.0f, 31.0f}, {-28.0f, 33.0f, -27.0f, 34.0f}, {-28.0f, 36.0f, -27.0f, 37.0f},
                                  {-28.0f, 39.0f, -27.0f, 40.0f}, {-28.0f, 42.0f, -27.0f, 43.0f}};
    const Ex::PlanOptions tooMany = {five, 5u, false}, missing = {nullptr, 2u, false};
    ExpectReserved(Room(), kSmall, 4, tooMany, Ex::kReasonReserved);
    ExpectReserved(Room(), kSmall, 4, missing, Ex::kReasonReserved);
    // Both walls taken: no display is dropped to make a plan.
    const Ex::HostKeep walls[2] = {{-28.5f, -6.0f, -20.0f, 44.5f}, {20.0f, -6.0f, 28.5f, 44.5f}};
    const Ex::PlanOptions both = {walls, 2u, false};
    ExpectReserved(Room(0), kSmall, 4, both, Ex::kReasonNoFit);

    // Many invented reservations: planned around whole, or refused for a reason a host can act on.
    gSeed = 5150u;
    unsigned planned = 0, refused = 0, odd = 0;
    for (unsigned n = 0; n < 120u; ++n) {
        Ex::HostKeep k[2];
        const unsigned keeps = 1u + Draw() % 2u;
        for (unsigned i = 0; i < keeps; ++i) {
            k[i].minX = -28.0f + 50.0f * Unit(); k[i].maxX = k[i].minX + 1.0f + 6.0f * Unit();
            k[i].minY = -6.0f + 48.0f * Unit(); k[i].maxY = k[i].minY + 1.0f + 6.0f * Unit();
        }
        const unsigned count = 1u + Draw() % 6u;
        const Ex::PlanOptions o = {k, keeps, false};
        if (Ex::MakePlan(Room(), kSmall, count, o, &q)) {
            ++planned;
            CheckPlanSound(q, kSmall, count);
            CheckPlanUse(q);
            for (unsigned i = 0; i < keeps; ++i) CHECK(Built(q, k[i]) == 0);
        } else {
            ++refused;
            odd += q.reason != Ex::kReasonReserved && q.reason != Ex::kReasonNoFit;
            CHECK(!q.valid && q.boxCount == 0 && q.zoneCount == 0);
        }
    }
    std::printf("reservations: %u planned, %u refused\n", planned, refused);
    CHECK(odd == 0 && planned >= 10 && refused >= 10);
}

// Awkward line-ups and doorways: the smallest and the largest machines, a crowded
// wall, and a portal hard against a corner of the room.
void TestAwkward() {
    Ex::Plan p;
    // Six machines as small as a machine may be: the furniture still fits around each.
    const Ex::DisplayInput specks[6] = {{1, 0.05f, 0.05f}, {2, 0.05f, 0.05f}, {3, 0.05f, 0.05f},
                                        {4, 0.05f, 0.05f}, {5, 0.05f, 0.05f}, {6, 0.05f, 0.05f}};
    CHECK(Ex::MakePlan(SmallRoom(), specks, 6, &p));
    CheckPlanSound(p, specks, 6);
    CheckPlanUse(p);

    // A short room full of small machines: every pocket between neighbours is railed
    // off, and nothing a visitor can reach is a dead end narrower than a rail closes.
    CHECK(Ex::MakePlan(SmallRoom(), kSmall, 6, &p));
    if (p.valid) {
        CHECK(Count(p, Ex::kSolidRail) == 4);
        const Reach all = Flood(p, 0.25f);
        const Reach unrailed = Flood(Without(p, IsRail), 0.25f);
        for (unsigned i = 0; i < p.bayCount; ++i) CHECK(all.At(p.bays[i].pad));
        for (unsigned i = 0; i + 1 < p.bayCount; ++i) {
            const Ex::Bay &a = p.bays[i], &b = p.bays[i + 1];
            if (a.side != b.side) continue;
            const Ex::Vec2 pocket = {a.model.x, 0.5f * (a.model.y + b.model.y)};
            CHECK(Ex::WalkAt(p, pocket) == Ex::kWalkOpen && !all.At(pocket) && unrailed.At(pocket));
        }
    }

    // Six large machines, as close as they may stand: railed pockets, every pad
    // reached, and room for a visitor twice as wide on every approach and up the nave.
    const Ex::DisplayInput big[6] = {{1, 4.6f, 2.0f}, {2, 4.6f, 3.0f}, {3, 4.6f, 4.0f},
                                     {4, 4.6f, 5.0f}, {5, 4.6f, 6.0f}, {6, 4.6f, 7.0f}};
    CHECK(Ex::MakePlan(Room(0), big, 6, &p));
    CheckPlanSound(p, big, 6);
    CheckPlanUse(p);
    if (p.valid) {
        const float ex = p.room.entrance.x;
        CHECK(Count(p, Ex::kSolidRail) == 4);
        const Reach all = Flood(p, 0.25f);
        for (unsigned i = 0; i < p.bayCount; ++i) {
            const Ex::Bay &b = p.bays[i];
            CHECK(all.At(b.pad));
            CHECK(Ex::PathClear(p, Ex::Vec2{ex, b.pad.y}, b.pad, 2.0f * Ex::kVisitorRadius));
            // Past the front of the plinth, from one end of it to the other.
            const float x = b.zoneEnd.x - float(b.side) * (2.0f * Ex::kVisitorRadius + 0.3f);
            CHECK(Ex::PathClear(p, Ex::Vec2{x, b.model.y - b.plinthHalf}, Ex::Vec2{x, b.model.y + b.plinthHalf}, 2.0f * Ex::kVisitorRadius));
        }
        CHECK(Ex::PathClear(p, Ex::Vec2{ex, 2.0f}, Ex::Vec2{ex, 41.0f}, 2.8f));
    }

    // The widest machine a wall takes, and far taller than the hall: placed whole, its
    // space left alone, walls at their limit, a shortened but real standing distance.
    const Ex::DisplayInput giant[1] = {{9, 11.0f, 80.0f}};
    CHECK(Ex::MakePlan(Room(), giant, 1, &p));
    CheckPlanSound(p, giant, 1);
    CheckPlanUse(p);
    CHECK(p.valid && p.bays[0].viewShortened && p.bays[0].radius == 11.0f && p.wallHeight == Ex::kWallHeightMax);

    // Portals hard against the back corner, on either wall, and against the front
    // corner where there is no room for a jamb: the frame is cut to fit, never overlapped.
    CHECK(Ex::MakePlan(Room(1, 43.1f, 44.6f), kSix, 6, &p)); CheckPlanSound(p, kSix, 6); CheckPlanUse(p);
    CHECK(Ex::MakePlan(Room(-1, 43.1f, 44.6f), kSix, 6, &p)); CheckPlanSound(p, kSix, 6); CheckPlanUse(p);
    CHECK(Ex::MakePlan(Room(1, -6.6f, -4.0f), kSix, 6, &p)); CheckPlanSound(p, kSix, 6); CheckPlanUse(p);
    if (p.valid) {
        unsigned below = 0, above = 0;
        for (unsigned i = 0; i < p.boxCount; ++i) {
            const Ex::Box &b = p.boxes[i];
            below += b.purpose == Ex::kSolidPier && std::fabs(b.max.y - -6.6f) < 1.0e-4f;
            above += b.purpose == Ex::kSolidPier && std::fabs(b.min.y - -4.0f) < 1.0e-4f;
        }
        CHECK(below == 0 && above == 1);
    }
    // A narrow portal in the middle of a wall.
    CHECK(Ex::MakePlan(Room(-1, 20.0f, 21.5f), kSix, 6, &p)); CheckPlanSound(p, kSix, 6); CheckPlanUse(p);
}

// A visitor at the console, and a film player that does as it is told. The film
// lists are bare counts: no film here has a name or a file.
struct Rig {
    Cc::State s;
    Cc::Input in;
    Cc::Output down;   // what the press half of the last Click returned
    explicit Rig(unsigned films) {
        Cc::Reset(&s);
        const Cc::Input start = {true, true, true, Cc::kRegionNone, false, false, true, films, Cc::kDecoderStopped, -1};
        in = start;
        down = Cc::Output();
    }
    void Obey(const Cc::Output &o) {
        if (o.command == Cc::kCommandPlay) { in.decoder = Cc::kDecoderPlaying; in.playing = o.index; }
        else if (o.command == Cc::kCommandStop) { in.decoder = Cc::kDecoderStopped; in.playing = -1; }
    }
    Cc::Output Step(Cc::Region region, bool fire) {
        in.region = region;
        in.fireDown = fire;
        return Cc::Update(&s, in);
    }
    // Aim, press and release on one control; the player then carries out what was asked.
    Cc::Output Click(Cc::Region region) {
        Step(region, false);
        down = Step(region, true);
        const Cc::Output up = Step(region, false);
        Obey(up);
        return up;
    }
};
// Fire was left to the game and nothing was asked of the player.
bool Idle(const Cc::Output &o) {
    return !o.consumeFire && o.command == Cc::kCommandNone && o.index == -1 && o.pressed == Cc::kRegionNone;
}
// Fire was kept from the game, and nothing was asked of the player either.
bool Swallowed(const Cc::Output &o) {
    return o.consumeFire && o.command == Cc::kCommandNone && o.index == -1 && o.pressed == Cc::kRegionNone;
}
const Cc::Region kControls[3] = {Cc::kRegionPrevious, Cc::kRegionPlay, Cc::kRegionNext};

// No film, one film, several: choosing, wrapping, playing and stopping.
void TestCinemaLists() {
    {   // No films: the console says so and Fire stays the game's.
        Rig r(0);
        Cc::Output o = r.Step(Cc::kRegionPlay, false);
        CHECK(!o.usable && o.prompt == Cc::kPromptUnavailable && o.selected == -1 && o.playing == -1 && Idle(o));
        for (unsigned k = 0; k < 3; ++k) {
            o = r.Click(kControls[k]);
            CHECK(Idle(o) && Idle(r.down) && o.selected == -1 && o.prompt == Cc::kPromptUnavailable);
        }
        CHECK(!Cc::Select(&r.s, 0, 0));
    }
    {   // One film: it is the chosen one, Previous and Next have nowhere to go, Play and Stop work.
        Rig r(1);
        Cc::Output o = r.Step(Cc::kRegionNone, false);
        CHECK(o.usable && o.selected == 0 && o.playing == -1 && o.prompt == Cc::kPromptAim && Idle(o));
        CHECK(r.Step(Cc::kRegionNext, false).prompt == Cc::kPromptOnlyFilm);
        CHECK(r.Step(Cc::kRegionPrevious, false).prompt == Cc::kPromptOnlyFilm);
        o = r.Click(Cc::kRegionNext);
        CHECK(r.down.consumeFire && r.down.pressed == Cc::kRegionNext && Swallowed(o) && o.selected == 0);
        o = r.Click(Cc::kRegionPrevious);
        CHECK(r.down.consumeFire && r.down.pressed == Cc::kRegionPrevious && Swallowed(o) && o.selected == 0);
        CHECK(r.Step(Cc::kRegionPlay, false).prompt == Cc::kPromptPlay);
        o = r.Click(Cc::kRegionPlay);
        CHECK(r.down.consumeFire && r.down.pressed == Cc::kRegionPlay && r.down.command == Cc::kCommandNone);
        CHECK(o.consumeFire && o.command == Cc::kCommandPlay && o.index == 0 && o.playing == 0 && o.selected == 0);
        o = r.Step(Cc::kRegionPlay, false);
        CHECK(Idle(o) && o.prompt == Cc::kPromptStop && o.playing == 0);
        o = r.Click(Cc::kRegionPlay);
        CHECK(o.consumeFire && o.command == Cc::kCommandStop && o.index == -1 && o.playing == -1 && o.selected == 0);
        o = r.Step(Cc::kRegionPlay, false);
        CHECK(Idle(o) && o.prompt == Cc::kPromptPlay && o.playing == -1);
    }
    {   // Two films: either direction reaches the other one.
        Rig r(2);
        CHECK(r.Step(Cc::kRegionNone, false).selected == 0);
        CHECK(r.Click(Cc::kRegionPrevious).index == 1 && r.Click(Cc::kRegionPrevious).index == 0);
        CHECK(r.Click(Cc::kRegionNext).index == 1 && r.Click(Cc::kRegionNext).index == 0);
    }
    {   // Four films: Next and Previous wrap; Play shows the chosen film and Stop clears it.
        Rig r(4);
        Cc::Output o = r.Step(Cc::kRegionNone, false);
        CHECK(o.selected == 0 && o.prompt == Cc::kPromptAim);
        CHECK(r.Step(Cc::kRegionNext, false).prompt == Cc::kPromptNext);
        CHECK(r.Step(Cc::kRegionPrevious, false).prompt == Cc::kPromptPrevious);
        const int next[5] = {1, 2, 3, 0, 1}, previous[4] = {0, 3, 2, 1};
        for (unsigned k = 0; k < 5; ++k) {
            o = r.Click(Cc::kRegionNext);
            CHECK(r.down.consumeFire && r.down.pressed == Cc::kRegionNext && o.consumeFire);
            CHECK(o.command == Cc::kCommandSelect && o.index == next[k] && o.selected == next[k] && o.playing == -1);
        }
        for (unsigned k = 0; k < 4; ++k) {
            o = r.Click(Cc::kRegionPrevious);
            CHECK(o.consumeFire && o.command == Cc::kCommandSelect && o.index == previous[k] && o.selected == previous[k]);
        }
        CHECK(r.in.decoder == Cc::kDecoderStopped);        // choosing alone starts nothing
        o = r.Click(Cc::kRegionPlay);
        CHECK(o.command == Cc::kCommandPlay && o.index == 1 && o.playing == 1);
        // Stepping Next/Previous while a film plays auto-switches playback to the new film immediately.
        o = r.Click(Cc::kRegionNext);
        CHECK(o.command == Cc::kCommandPlay && o.index == 2 && o.playing == 2 && r.in.decoder == Cc::kDecoderPlaying);
        CHECK(r.Step(Cc::kRegionPlay, false).prompt == Cc::kPromptStop);
        o = r.Click(Cc::kRegionPrevious);
        CHECK(o.command == Cc::kCommandPlay && o.index == 1 && o.playing == 1 && r.in.decoder == Cc::kDecoderPlaying);
        CHECK(r.Step(Cc::kRegionPlay, false).prompt == Cc::kPromptStop);
        o = r.Click(Cc::kRegionNext);
        CHECK(o.command == Cc::kCommandPlay && o.index == 2 && o.playing == 2 && r.in.decoder == Cc::kDecoderPlaying);
        CHECK(r.Step(Cc::kRegionPlay, false).prompt == Cc::kPromptStop);
        // If selection diverges from playing externally (e.g. Select()), Play switches and Stop resumes once aligned.
        CHECK(Cc::Select(&r.s, 4, 1) && r.Step(Cc::kRegionPlay, false).prompt == Cc::kPromptPlay);
        CHECK(Cc::Select(&r.s, 4, 2) && r.Step(Cc::kRegionPlay, false).prompt == Cc::kPromptStop);
        o = r.Click(Cc::kRegionPlay);
        CHECK(o.command == Cc::kCommandStop && o.index == -1 && o.playing == -1 && o.selected == 2);
        CHECK(r.in.decoder == Cc::kDecoderStopped && r.in.playing == -1);
        // A film that ran to its end still holds the screen; Next/Prev only select, and Play shows it again.
        o = r.Click(Cc::kRegionPlay);
        CHECK(o.command == Cc::kCommandPlay && o.index == 2);
        r.in.decoder = Cc::kDecoderEnded;
        o = r.Step(Cc::kRegionPlay, false);
        CHECK(Idle(o) && o.prompt == Cc::kPromptPlay && o.playing == 2);
        o = r.Click(Cc::kRegionNext);
        CHECK(o.command == Cc::kCommandSelect && o.index == 3 && o.playing == 2);
        o = r.Click(Cc::kRegionPrevious);
        CHECK(o.command == Cc::kCommandSelect && o.index == 2 && o.playing == 2);
        o = r.Click(Cc::kRegionPlay);
        CHECK(o.command == Cc::kCommandPlay && o.index == 2 && o.playing == 2);
    }
    {   // The list changes under the choice: it stays inside the list.
        Rig r(5);
        CHECK(Cc::Select(&r.s, 5, 4) && r.Step(Cc::kRegionNone, false).selected == 4);
        r.in.films = 3;
        CHECK(r.Step(Cc::kRegionNone, false).selected == 2);
        r.in.films = 0;
        Cc::Output o = r.Step(Cc::kRegionNone, false);
        CHECK(o.selected == -1 && !o.usable && o.prompt == Cc::kPromptUnavailable);
        r.in.films = 2;
        o = r.Step(Cc::kRegionNone, false);
        CHECK(o.selected == 0 && o.usable && o.prompt == Cc::kPromptAim);
        CHECK(!Cc::Select(&r.s, 2, 2) && !Cc::Select(&r.s, 2, -1) && !Cc::Select(nullptr, 2, 1));
        CHECK(Cc::Select(&r.s, 2, 1) && r.Step(Cc::kRegionNone, false).selected == 1);
    }
    {   // A film already on the screen when the visitor arrives is the chosen one.
        Rig r(4);
        r.in.decoder = Cc::kDecoderPlaying;
        r.in.playing = 2;
        const Cc::Output o = r.Step(Cc::kRegionPlay, false);
        CHECK(o.selected == 2 && o.playing == 2 && o.prompt == Cc::kPromptStop && Idle(o));
    }
    {   // A player naming a film outside the list is not believed.
        Rig r(3);
        r.in.decoder = Cc::kDecoderPlaying;
        r.in.playing = 7;
        Cc::Output o = r.Step(Cc::kRegionPlay, false);
        CHECK(o.selected == 0 && o.playing == -1 && o.prompt == Cc::kPromptPlay);
        r.in.playing = -1;
        o = r.Step(Cc::kRegionPlay, false);
        CHECK(o.playing == -1 && o.prompt == Cc::kPromptPlay);
    }
    {   // No state: nothing happens and nothing is consumed.
        Rig r(3);
        r.in.fireDown = true;
        const Cc::Output o = Cc::Update(nullptr, r.in);
        CHECK(Idle(o) && o.selected == -1 && o.playing == -1 && !o.usable && o.prompt == Cc::kPromptNone);
        Cc::Reset(nullptr);
    }
}

// Whose press it is: the console's only when it began on a control, and then
// press and release together.
void TestCinemaOwnership() {
    {   // Fire already held as the visit starts at the console: the game's, through its release.
        Rig r(3);
        Cc::Output o = r.Step(Cc::kRegionPlay, true);
        CHECK(Idle(o) && o.prompt == Cc::kPromptPlay);
        CHECK(Idle(r.Step(Cc::kRegionPlay, true)) && Idle(r.Step(Cc::kRegionPlay, false)));
        CHECK(r.in.decoder == Cc::kDecoderStopped);
        o = r.Click(Cc::kRegionPlay);                       // a fresh press then works
        CHECK(r.down.consumeFire && o.command == Cc::kCommandPlay && o.index == 0);
    }
    {   // Pressed in the hall and carried in to the console: an ordinary shot throughout.
        Rig r(3);
        r.in.inRoom = false;
        r.in.atConsole = false;
        CHECK(Idle(r.Step(Cc::kRegionNone, false)) && Idle(r.Step(Cc::kRegionNone, true)));
        r.in.inRoom = true;
        CHECK(Idle(r.Step(Cc::kRegionNone, true)));
        r.in.atConsole = true;
        Cc::Output o = r.Step(Cc::kRegionPlay, true);
        CHECK(Idle(o) && o.prompt == Cc::kPromptPlay);
        CHECK(Idle(r.Step(Cc::kRegionPlay, false)) && r.in.decoder == Cc::kDecoderStopped);
        o = r.Click(Cc::kRegionPlay);
        CHECK(r.down.consumeFire && o.command == Cc::kCommandPlay);
    }
    {   // Pressed on a control, then the visitor steps away: the press stays the console's
        // to its release, and does nothing. Coming back does not revive it.
        Rig r(3);
        r.Step(Cc::kRegionNext, false);
        Cc::Output o = r.Step(Cc::kRegionNext, true);
        CHECK(o.consumeFire && o.pressed == Cc::kRegionNext && o.command == Cc::kCommandNone);
        r.in.atConsole = false;
        CHECK(Swallowed(r.Step(Cc::kRegionNone, true)));
        r.in.atConsole = true;
        CHECK(Swallowed(r.Step(Cc::kRegionNext, true)));
        o = r.Step(Cc::kRegionNext, false);
        CHECK(Swallowed(o) && o.selected == 0);
        CHECK(Idle(r.Step(Cc::kRegionNext, false)));
    }
    {   // Released while away: the release is swallowed too, never handed to the game alone.
        Rig r(3);
        r.Step(Cc::kRegionNext, false);
        CHECK(r.Step(Cc::kRegionNext, true).consumeFire);
        r.in.atConsole = false;
        r.in.inRoom = false;
        Cc::Output o = r.Step(Cc::kRegionNone, false);
        CHECK(Swallowed(o) && o.selected == 0);
        CHECK(Idle(r.Step(Cc::kRegionNone, false)));
        // The next press begins away from the console: ordinary Fire, even carried back to it.
        CHECK(Idle(r.Step(Cc::kRegionNone, true)));
        r.in.inRoom = true;
        r.in.atConsole = true;
        CHECK(Idle(r.Step(Cc::kRegionNext, true)));
        o = r.Step(Cc::kRegionNext, false);
        CHECK(Idle(o) && o.selected == 0);
    }
    {   // The aim slides off the control that was pressed: cancelled, even if it slides back.
        Rig r(3);
        r.Step(Cc::kRegionPlay, false);
        CHECK(r.Step(Cc::kRegionPlay, true).pressed == Cc::kRegionPlay);
        Cc::Output o = r.Step(Cc::kRegionNext, true);
        CHECK(Swallowed(o) && o.prompt == Cc::kPromptNext);
        CHECK(Swallowed(r.Step(Cc::kRegionPlay, true)) && Swallowed(r.Step(Cc::kRegionPlay, false)));
        CHECK(r.in.decoder == Cc::kDecoderStopped);
        r.Step(Cc::kRegionPrevious, false);
        CHECK(r.Step(Cc::kRegionPrevious, true).pressed == Cc::kRegionPrevious);
        CHECK(Swallowed(r.Step(Cc::kRegionNone, true)) && Swallowed(r.Step(Cc::kRegionPrevious, true)));
        o = r.Step(Cc::kRegionPrevious, false);
        CHECK(Swallowed(o) && o.selected == 0);
        // Aimed between the controls, a press is not the console's at all.
        r.Step(Cc::kRegionNone, false);
        CHECK(Idle(r.Step(Cc::kRegionNone, true)) && Idle(r.Step(Cc::kRegionPlay, true)));
        CHECK(Idle(r.Step(Cc::kRegionPlay, false)) && r.in.decoder == Cc::kDecoderStopped);
    }
    for (int what = 0; what < 2; ++what) {
        // The game pauses (0) or loses the keyboard (1) under a press on a control.
        Rig r(3);
        bool &flag = what ? r.in.focused : r.in.paused;
        const bool lost = what == 0, normal = !lost;
        r.Step(Cc::kRegionNext, false);
        CHECK(r.Step(Cc::kRegionNext, true).pressed == Cc::kRegionNext);
        flag = lost;
        CHECK(Swallowed(r.Step(Cc::kRegionNext, true)));
        Cc::Output o = r.Step(Cc::kRegionNext, false);      // released while still away
        CHECK(Swallowed(o) && o.selected == 0);
        flag = normal;
        CHECK(Idle(r.Step(Cc::kRegionNext, false)));
        // Back before the release: still cancelled.
        CHECK(r.Step(Cc::kRegionNext, true).pressed == Cc::kRegionNext);
        flag = lost;
        CHECK(Swallowed(r.Step(Cc::kRegionNext, true)));
        flag = normal;
        CHECK(Swallowed(r.Step(Cc::kRegionNext, true)));
        o = r.Step(Cc::kRegionNext, false);
        CHECK(Swallowed(o) && o.selected == 0);
        // A press that begins while paused or unfocused is not the console's.
        flag = lost;
        CHECK(Idle(r.Step(Cc::kRegionNext, false)) && Idle(r.Step(Cc::kRegionNext, true)));
        flag = normal;
        CHECK(Idle(r.Step(Cc::kRegionNext, true)));
        o = r.Step(Cc::kRegionNext, false);
        CHECK(Idle(o) && o.selected == 0);
        // And afterwards the console works as before.
        o = r.Click(Cc::kRegionNext);
        CHECK(o.command == Cc::kCommandSelect && o.index == 1);
    }
    {   // Ordinary play, no visit: nothing is consumed, asked or shown, whatever the aim says.
        Rig r(3);
        r.in.museumActive = false;
        for (unsigned k = 0; k < 3; ++k) {
            Cc::Output o = r.Step(kControls[k], false);
            CHECK(Idle(o) && o.selected == -1 && !o.usable && o.prompt == Cc::kPromptNone);
            CHECK(Idle(r.Step(kControls[k], true)) && Idle(r.Step(kControls[k], true)));
            o = r.Step(kControls[k], false);
            CHECK(Idle(o) && o.playing == -1);
        }
        // Fire held as the visit begins under it is not a press on a control.
        CHECK(Idle(r.Step(Cc::kRegionPlay, true)));
        r.in.museumActive = true;
        CHECK(Idle(r.Step(Cc::kRegionPlay, true)) && Idle(r.Step(Cc::kRegionPlay, false)));
        CHECK(r.in.decoder == Cc::kDecoderStopped);
        const Cc::Output o = r.Click(Cc::kRegionPlay);
        CHECK(r.down.consumeFire && o.command == Cc::kCommandPlay && o.index == 0);
    }
}

// Walking out of the screening room, and coming back.
void TestCinemaLeaving() {
    Rig r(3);
    CHECK(r.Click(Cc::kRegionNext).index == 1);
    Cc::Output o = r.Click(Cc::kRegionPlay);
    CHECK(o.command == Cc::kCommandPlay && o.index == 1);
    // Stepping back from the console, still in the room: the film runs on.
    r.in.atConsole = false;
    o = r.Step(Cc::kRegionNone, false);
    CHECK(Idle(o) && o.playing == 1 && o.prompt == Cc::kPromptNone);
    // Out of the room: stopped, and Fire is not touched.
    r.in.inRoom = false;
    o = r.Step(Cc::kRegionNone, false);
    CHECK(!o.consumeFire && o.command == Cc::kCommandStop && o.index == -1 && o.playing == -1);
    r.Obey(o);
    o = r.Step(Cc::kRegionNone, false);
    CHECK(Idle(o) && o.playing == -1);
    CHECK(Idle(r.Step(Cc::kRegionNone, true)) && Idle(r.Step(Cc::kRegionNone, false)));   // a shot in the hall
    // Back in: the choice is remembered, the screen is empty, and Play works again.
    r.in.inRoom = true;
    r.in.atConsole = true;
    o = r.Step(Cc::kRegionPlay, false);
    CHECK(Idle(o) && o.selected == 1 && o.playing == -1 && o.prompt == Cc::kPromptPlay);
    o = r.Click(Cc::kRegionPlay);
    CHECK(o.command == Cc::kCommandPlay && o.index == 1);
    // Walking out with Fire held on a control: the film stops and the press is swallowed whole.
    r.Step(Cc::kRegionNext, false);
    CHECK(r.Step(Cc::kRegionNext, true).pressed == Cc::kRegionNext);
    r.in.atConsole = false;
    r.in.inRoom = false;
    o = r.Step(Cc::kRegionNone, true);
    CHECK(o.consumeFire && o.command == Cc::kCommandStop && o.pressed == Cc::kRegionNone && o.playing == -1);
    r.Obey(o);
    o = r.Step(Cc::kRegionNone, false);
    CHECK(Swallowed(o) && o.selected == 1);
    CHECK(Idle(r.Step(Cc::kRegionNone, false)));
    // A film that ran to its end still holds the screen, and is cleared on leaving too.
    r.in.inRoom = true;
    r.in.atConsole = true;
    CHECK(r.Click(Cc::kRegionPlay).command == Cc::kCommandPlay);
    r.in.decoder = Cc::kDecoderEnded;
    r.in.atConsole = false;
    r.in.inRoom = false;
    o = r.Step(Cc::kRegionNone, false);
    CHECK(o.command == Cc::kCommandStop && o.playing == -1 && !o.consumeFire);
    r.Obey(o);
    // Leaving with nothing on the screen asks for nothing.
    r.in.inRoom = true;
    CHECK(Idle(r.Step(Cc::kRegionNone, false)));
    r.in.inRoom = false;
    CHECK(Idle(r.Step(Cc::kRegionNone, false)));
}

// A film player that is missing, comes back, fails to start or dies.
void TestCinemaDecoder() {
    {   // Missing: the console says so, asks for nothing and leaves Fire to the game.
        Rig r(2);
        r.in.decoder = Cc::kDecoderUnavailable;
        Cc::Output o = r.Step(Cc::kRegionPlay, false);
        CHECK(!o.usable && o.prompt == Cc::kPromptUnavailable && o.playing == -1 && Idle(o));
        for (unsigned k = 0; k < 3; ++k) {
            o = r.Click(kControls[k]);
            CHECK(Idle(o) && Idle(r.down) && o.prompt == Cc::kPromptUnavailable);
        }
        CHECK(r.in.decoder == Cc::kDecoderUnavailable);
        // It comes back: the console works.
        r.in.decoder = Cc::kDecoderStopped;
        o = r.Step(Cc::kRegionPlay, false);
        CHECK(o.usable && o.prompt == Cc::kPromptPlay && o.selected == 0);
        o = r.Click(Cc::kRegionPlay);
        CHECK(r.down.consumeFire && o.command == Cc::kCommandPlay && o.index == 0);
    }
    {   // Fails to start: asked once per click, never assumed to be running, and may be asked again.
        Rig r(2);
        r.Step(Cc::kRegionPlay, false);
        r.Step(Cc::kRegionPlay, true);
        Cc::Output o = r.Step(Cc::kRegionPlay, false);      // asked, but this player does nothing
        CHECK(o.command == Cc::kCommandPlay && o.index == 0 && o.playing == 0);
        o = r.Step(Cc::kRegionPlay, false);
        CHECK(Idle(o) && o.playing == -1 && o.prompt == Cc::kPromptPlay);
        CHECK(Idle(r.Step(Cc::kRegionPlay, false)));
        r.Step(Cc::kRegionPlay, true);
        o = r.Step(Cc::kRegionPlay, false);
        CHECK(o.consumeFire && o.command == Cc::kCommandPlay && o.index == 0);
        // Nothing reached the screen, so walking out has nothing to stop.
        r.in.atConsole = false;
        r.in.inRoom = false;
        CHECK(Idle(r.Step(Cc::kRegionNone, false)));
    }
    {   // Fails while Fire is held on Play: swallowed, nothing asked.
        Rig r(2);
        r.Step(Cc::kRegionPlay, false);
        CHECK(r.Step(Cc::kRegionPlay, true).pressed == Cc::kRegionPlay);
        r.in.decoder = Cc::kDecoderUnavailable;
        Cc::Output o = r.Step(Cc::kRegionPlay, true);
        CHECK(Swallowed(o) && !o.usable && o.prompt == Cc::kPromptUnavailable);
        CHECK(Swallowed(r.Step(Cc::kRegionPlay, false)) && Idle(r.Step(Cc::kRegionPlay, false)));
    }
    {   // The list empties while Fire is held on Next.
        Rig r(3);
        r.Step(Cc::kRegionNext, false);
        CHECK(r.Step(Cc::kRegionNext, true).consumeFire);
        r.in.films = 0;
        Cc::Output o = r.Step(Cc::kRegionNext, true);
        CHECK(Swallowed(o) && o.selected == -1 && !o.usable);
        o = r.Step(Cc::kRegionNext, false);
        CHECK(Swallowed(o) && o.selected == -1);
    }
    {   // Dies mid-film: nothing is reported on the screen, Stop is not offered or sent.
        Rig r(2);
        CHECK(r.Click(Cc::kRegionPlay).command == Cc::kCommandPlay);
        r.in.decoder = Cc::kDecoderUnavailable;
        const Cc::Output o = r.Step(Cc::kRegionPlay, false);
        CHECK(Idle(o) && o.playing == -1 && o.prompt == Cc::kPromptUnavailable);
        r.in.atConsole = false;
        r.in.inRoom = false;
        CHECK(Idle(r.Step(Cc::kRegionNone, false)));
    }
}

// Where the visitor stands at the console and which control the view meets.
void TestConsole() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Cc::Console consoles[3] = {{50.0f, 10.0f, -1.0f, 0.0f, 1.5f, 3.0f},
                                     {0.0f, 0.0f, 0.0f, 1.0f, 1.2f, 2.5f},
                                     {-7.0f, 33.0f, 0.6f, 0.8f, 2.0f, 3.5f}};
    for (unsigned n = 0; n < 3; ++n) {
        const Cc::Console &c = consoles[n];
        const float ax = -c.towardY, ay = c.towardX;      // along the face, to a facing visitor's right
        const float half = Cc::ControlHalf(c);
        const float left = Cc::ControlCentre(c, Cc::kRegionPrevious), right = Cc::ControlCentre(c, Cc::kRegionNext);
        // Previous, Play, Next from left to right, apart from each other and on the face.
        CHECK(Cc::ControlCentre(c, Cc::kRegionPlay) == 0.0f && left == -right && right > 0.0f && half > 0.0f);
        CHECK(right - half > half + 0.05f && right + half <= c.halfWidth);
        for (int f = 0; f < 3; ++f) {
            for (int a = -1; a <= 1; ++a) {
                const float front = 1.2f + 0.5f * float(f), across = 0.8f * float(a);
                const float x = c.x + c.towardX * front + ax * across, y = c.y + c.towardY * front + ay * across;
                // The view from here to the point of the face that is `target` along it.
                auto aim = [&](float target) {
                    return Cc::AimRegion(c, x, y, c.x + ax * target - x, c.y + ay * target - y);
                };
                float gotFront = 0.0f, gotAcross = 0.0f;
                Cc::ConsoleLocal(c, x, y, &gotFront, &gotAcross);
                CHECK(std::fabs(gotFront - front) < 1.0e-3f && std::fabs(gotAcross - across) < 1.0e-3f);
                CHECK(Cc::AtConsole(c, x, y));
                for (unsigned k = 0; k < 3; ++k) {
                    const float centre = Cc::ControlCentre(c, kControls[k]);
                    CHECK(aim(centre) == kControls[k]);
                    CHECK(aim(centre - 0.8f * half) == kControls[k] && aim(centre + 0.8f * half) == kControls[k]);
                }
                // Between two controls and past either end: no control.
                CHECK(aim(c.halfWidth / 3.0f) == Cc::kRegionNone && aim(-c.halfWidth / 3.0f) == Cc::kRegionNone);
                CHECK(aim(1.1f * c.halfWidth) == Cc::kRegionNone && aim(-1.1f * c.halfWidth) == Cc::kRegionNone);
                // The heading's length does not matter; away, along the face, or nonsense aims at nothing.
                CHECK(Cc::AimRegion(c, x, y, 30.0f * (c.x - x), 30.0f * (c.y - y)) == Cc::AimRegion(c, x, y, c.x - x, c.y - y));
                CHECK(Cc::AimRegion(c, x, y, c.towardX, c.towardY) == Cc::kRegionNone);
                CHECK(Cc::AimRegion(c, x, y, ax, ay) == Cc::kRegionNone && Cc::AimRegion(c, x, y, -ax, -ay) == Cc::kRegionNone);
                CHECK(Cc::AimRegion(c, x, y, 0.0f, 0.0f) == Cc::kRegionNone && Cc::AimRegion(c, x, y, nan, 0.0f) == Cc::kRegionNone);
            }
        }
    }

    // One console in plain numbers: its face at x 50 looks toward smaller x, so a
    // visitor facing it has smaller y to the right.
    const Cc::Console &c = consoles[0];
    CHECK(Cc::AimRegion(c, 48.0f, 10.0f, 1.0f, 0.0f) == Cc::kRegionPlay);
    CHECK(Cc::AimRegion(c, 48.0f, 10.0f, 2.0f, -1.0f) == Cc::kRegionNext);
    CHECK(Cc::AimRegion(c, 48.0f, 10.0f, 2.0f, 1.0f) == Cc::kRegionPrevious);
    CHECK(Cc::AimRegion(c, 48.0f, 10.0f, 2.0f, -0.5f) == Cc::kRegionNone);
    // Reach: not from too far back, too close, too far along, or behind.
    CHECK(Cc::AtConsole(c, 47.1f, 10.0f) && !Cc::AtConsole(c, 46.8f, 10.0f) && !Cc::AtConsole(c, 49.9f, 10.0f));
    CHECK(Cc::AtConsole(c, 48.0f, 7.6f) && !Cc::AtConsole(c, 48.0f, 12.7f) && !Cc::AtConsole(c, 51.0f, 10.0f));
    CHECK(Cc::AimRegion(c, 46.8f, 10.0f, 1.0f, 0.0f) == Cc::kRegionNone);
    CHECK(Cc::AimRegion(c, 51.0f, 10.0f, -1.0f, 0.0f) == Cc::kRegionNone);
    // From well to one side the far control is in reach at a fair angle, not at a glancing one.
    CHECK(Cc::AimRegion(c, 48.0f, 8.0f, 2.0f, 3.0f) == Cc::kRegionPrevious);
    CHECK(Cc::AimRegion(c, 49.5f, 8.0f, 0.5f, 3.0f) == Cc::kRegionNone);

    // Walking up and using it, with the aim worked out from where the visitor stands.
    Rig r(3);
    auto stand = [&](float x, float y, float hx, float hy, bool fire) {
        r.in.atConsole = Cc::AtConsole(c, x, y);
        return r.Step(Cc::AimRegion(c, x, y, hx, hy), fire);
    };
    Cc::Output o = stand(44.0f, 10.0f, 1.0f, 0.0f, false);
    CHECK(Idle(o) && o.prompt == Cc::kPromptNone);
    CHECK(Idle(stand(44.0f, 10.0f, 1.0f, 0.0f, true)) && Idle(stand(44.0f, 10.0f, 1.0f, 0.0f, false)));   // too far: a shot
    CHECK(stand(48.0f, 10.0f, 1.0f, 0.0f, false).prompt == Cc::kPromptPlay);
    CHECK(stand(48.0f, 10.0f, 2.0f, -0.5f, false).prompt == Cc::kPromptAim);
    CHECK(stand(48.0f, 10.0f, 2.0f, -1.0f, false).prompt == Cc::kPromptNext);
    o = stand(48.0f, 10.0f, 2.0f, -1.0f, true);
    CHECK(o.consumeFire && o.pressed == Cc::kRegionNext);
    o = stand(48.0f, 10.0f, 2.0f, -1.0f, false);
    CHECK(o.consumeFire && o.command == Cc::kCommandSelect && o.index == 1);
    // Pressed on Play, then backing out of reach before letting go: nothing plays.
    stand(48.0f, 10.0f, 1.0f, 0.0f, false);
    o = stand(48.0f, 10.0f, 1.0f, 0.0f, true);
    CHECK(o.consumeFire && o.pressed == Cc::kRegionPlay);
    CHECK(Swallowed(stand(46.5f, 10.0f, 1.0f, 0.0f, true)) && Swallowed(stand(46.5f, 10.0f, 1.0f, 0.0f, false)));
    CHECK(Idle(stand(46.5f, 10.0f, 1.0f, 0.0f, false)) && r.in.decoder == Cc::kDecoderStopped);
    // And done properly: pressed and released on Play.
    stand(48.0f, 10.0f, 1.0f, 0.0f, true);
    o = stand(48.0f, 10.0f, 1.0f, 0.0f, false);
    CHECK(o.consumeFire && o.command == Cc::kCommandPlay && o.index == 1 && o.playing == 1);
}

// The visit ending under a press or a film, and a player that lags behind.
void TestCinemaEdges() {
    {   // The visit ends under a press the console owned: the rest of it never reaches the game.
        Rig r(3);
        r.Step(Cc::kRegionPlay, false);
        Cc::Output o = r.Step(Cc::kRegionPlay, true);
        CHECK(o.consumeFire && o.pressed == Cc::kRegionPlay);
        r.in.museumActive = false;
        o = r.Step(Cc::kRegionPlay, true);
        CHECK(Swallowed(o) && o.selected == -1 && !o.usable && o.prompt == Cc::kPromptNone);
        CHECK(Swallowed(r.Step(Cc::kRegionPlay, false)) && Idle(r.Step(Cc::kRegionPlay, false)));
        // After that, ordinary play is untouched.
        CHECK(Idle(r.Step(Cc::kRegionPlay, true)) && Idle(r.Step(Cc::kRegionPlay, false)));
        CHECK(r.in.decoder == Cc::kDecoderStopped);
    }
    {   // The visit ends and resumes under the same held press: still swallowed, still no action.
        Rig r(3);
        r.Step(Cc::kRegionNext, false);
        CHECK(r.Step(Cc::kRegionNext, true).pressed == Cc::kRegionNext);
        r.in.museumActive = false;
        CHECK(Swallowed(r.Step(Cc::kRegionNext, true)));
        r.in.museumActive = true;
        CHECK(Swallowed(r.Step(Cc::kRegionNext, true)));
        Cc::Output o = r.Step(Cc::kRegionNext, false);
        CHECK(Swallowed(o) && o.selected == 0);
        o = r.Click(Cc::kRegionNext);
        CHECK(o.command == Cc::kCommandSelect && o.index == 1);
    }
    {   // With no visit running nothing is asked of the player, even with a film on its screen:
        // clearing the screen when a visit ends is the caller's own work.
        Rig r(3);
        CHECK(r.Click(Cc::kRegionPlay).command == Cc::kCommandPlay);
        r.in.museumActive = false;
        CHECK(Idle(r.Step(Cc::kRegionPlay, false)));
        r.in.atConsole = false;
        r.in.inRoom = false;
        CHECK(Idle(r.Step(Cc::kRegionNone, false)));
    }
    {   // A player slow to stop is asked again on every step outside, until the screen is clear.
        Rig r(3);
        CHECK(r.Click(Cc::kRegionPlay).command == Cc::kCommandPlay);
        r.in.atConsole = false;
        r.in.inRoom = false;
        Cc::Output o = r.Step(Cc::kRegionNone, false);
        for (int k = 0; k < 4; ++k) {
            CHECK(!o.consumeFire && o.command == Cc::kCommandStop && o.index == -1 && o.playing == -1);
            o = r.Step(Cc::kRegionNone, false);
        }
        CHECK(o.command == Cc::kCommandStop);
        r.Obey(o);
        CHECK(Idle(r.Step(Cc::kRegionNone, false)));
    }
    {   // A film still starting when the visitor walked out is stopped when it appears.
        Rig r(3);
        r.Step(Cc::kRegionPlay, false);
        r.Step(Cc::kRegionPlay, true);
        Cc::Output o = r.Step(Cc::kRegionPlay, false);
        CHECK(o.command == Cc::kCommandPlay && o.index == 0);
        r.in.atConsole = false;
        r.in.inRoom = false;
        CHECK(Idle(r.Step(Cc::kRegionNone, false)));        // nothing on the screen yet
        r.in.decoder = Cc::kDecoderPlaying;
        r.in.playing = 0;
        o = r.Step(Cc::kRegionNone, false);
        CHECK(!o.consumeFire && o.command == Cc::kCommandStop && o.playing == -1);
    }
    {   // A player showing a film it cannot name is cleared all the same.
        Rig r(3);
        CHECK(r.Click(Cc::kRegionPlay).command == Cc::kCommandPlay);
        r.in.playing = 9;
        r.in.atConsole = false;
        r.in.inRoom = false;
        const Cc::Output o = r.Step(Cc::kRegionNone, false);
        CHECK(!o.consumeFire && o.command == Cc::kCommandStop && o.playing == -1);
    }
}

// The same rules written a second way, around one press at a time: whose the
// press now held is, and whether it is still good.
struct Model {
    bool prevDown, owned, clean;
    Cc::Region region;
    int selected;
    void Reset() {
        prevDown = true;
        owned = clean = false;
        region = Cc::kRegionNone;
        selected = -1;
    }
    void Step(const Cc::Input &in, bool *consume, Cc::Command *command, int *index) {
        *command = Cc::kCommandNone;
        *index = -1;
        const bool shown = in.decoder == Cc::kDecoderPlaying || in.decoder == Cc::kDecoderEnded;
        const int films = int(in.films);
        const int playing = shown && in.playing >= 0 && in.playing < films ? in.playing : -1;
        if (in.museumActive) {
            if (films == 0) selected = -1;
            else if (selected < 0) selected = playing >= 0 ? playing : 0;
            else if (selected >= films) selected = films - 1;
        }
        const Cc::Region aim = in.inRoom && in.atConsole ? in.region : Cc::kRegionNone;
        const bool can = in.museumActive && films > 0 && in.decoder != Cc::kDecoderUnavailable &&
                         aim != Cc::kRegionNone && in.focused && !in.paused;
        const bool released = prevDown && !in.fireDown;
        if (in.fireDown && !prevDown) {
            owned = clean = can;
            region = aim;
        } else if (!can || aim != region) {
            clean = false;
        }
        *consume = owned && (in.fireDown || released);
        if (released) {
            if (owned && clean) {
                if (aim == Cc::kRegionPlay) {
                    if (in.decoder == Cc::kDecoderPlaying && playing == selected) *command = Cc::kCommandStop;
                    else { *command = Cc::kCommandPlay; *index = selected; }
                } else if (films > 1) {
                    selected = (selected + (aim == Cc::kRegionNext ? 1 : films - 1)) % films;
                    *command = (in.decoder == Cc::kDecoderPlaying) ? Cc::kCommandPlay : Cc::kCommandSelect;
                    *index = selected;
                }
            }
            owned = clean = false;
        }
        if (in.museumActive && !in.inRoom && shown && *command == Cc::kCommandNone) *command = Cc::kCommandStop;
        prevDown = in.fireDown;
    }
};

// A long, seeded stream of everything changing at once, checked against the
// second writing of the rules and against what the game itself would be shown.
void TestCinemaStream() {
    gSeed = 77003u;
    Cc::State s;
    Cc::Reset(&s);
    Model m;
    m.Reset();
    Cc::Input in = {true, false, false, Cc::kRegionNone, false, false, true, 3u, Cc::kDecoderStopped, -1};
    bool prevDown = true, pressConsumed = false;            // as Reset has it: held, and the game's
    unsigned mismatches = 0, halves = 0, orphans = 0, strays = 0, left = 0;
    unsigned selects = 0, plays = 0, stops = 0, exits = 0, swallowed = 0, passed = 0;
    for (unsigned frame = 0; frame < 200000u; ++frame) {
        // The visitor, the game and the player each move on a little.
        if (Chance(0.2f)) in.fireDown = !in.fireDown;
        if (Chance(0.15f)) in.region = Cc::Region(Draw() % 4u);
        if (Chance(in.atConsole ? 0.03f : 0.10f)) in.atConsole = !in.atConsole;
        if (Chance(in.inRoom ? 0.02f : 0.05f)) in.inRoom = !in.inRoom;
        if (Chance(in.paused ? 0.20f : 0.01f)) in.paused = !in.paused;
        if (Chance(in.focused ? 0.01f : 0.20f)) in.focused = !in.focused;
        if (Chance(in.museumActive ? 0.003f : 0.05f)) in.museumActive = !in.museumActive;
        if (Chance(0.005f)) in.films = Draw() % 5u;
        if (in.decoder == Cc::kDecoderPlaying && Chance(0.01f)) in.decoder = Cc::kDecoderEnded;
        if (Chance(in.decoder == Cc::kDecoderUnavailable ? 0.05f : 0.002f)) {
            in.decoder = in.decoder == Cc::kDecoderUnavailable ? Cc::kDecoderStopped : Cc::kDecoderUnavailable;
            in.playing = -1;
        }
        if (Chance(0.002f)) in.playing = 9;                 // a player reporting nonsense
        if (Chance(0.0005f)) {                              // a new visit
            Cc::Reset(&s);
            m.Reset();
            prevDown = true;
            pressConsumed = false;
        }

        bool wantConsume = false;
        Cc::Command wantCommand = Cc::kCommandNone;
        int wantIndex = -1;
        m.Step(in, &wantConsume, &wantCommand, &wantIndex);
        const Cc::Output o = Cc::Update(&s, in);
        const bool shown = in.decoder == Cc::kDecoderPlaying || in.decoder == Cc::kDecoderEnded;
        const int playing = shown && in.playing >= 0 && unsigned(in.playing) < in.films ? in.playing : -1;
        const int wantPlaying = o.command == Cc::kCommandStop ? -1 : o.command == Cc::kCommandPlay ? o.index : playing;
        bool same = o.consumeFire == wantConsume && o.command == wantCommand && o.index == wantIndex;
        if (in.museumActive) same = same && o.selected == m.selected && o.playing == wantPlaying;
        if (!same) {
            if (!mismatches)
                std::printf("stream frame %u: consume %d want %d, command %d want %d, index %d want %d, selected %d want %d\n",
                            frame, int(o.consumeFire), int(wantConsume), int(o.command), int(wantCommand), o.index,
                            wantIndex, o.selected, m.selected);
            ++mismatches;
        }

        // What the game is shown: each press is its own or the console's from first to last.
        const bool press = in.fireDown && !prevDown, release = !in.fireDown && prevDown;
        if (press) {
            pressConsumed = o.consumeFire;
            if (o.consumeFire) ++swallowed;
            else ++passed;
        } else if (in.fireDown || release) {
            halves += o.consumeFire != pressConsumed;
        } else {
            orphans += o.consumeFire;
        }
        prevDown = in.fireDown;

        if (o.command != Cc::kCommandNone) {
            const bool atControl = in.museumActive && in.inRoom && in.atConsole && in.region != Cc::kRegionNone &&
                                   in.focused && !in.paused && in.films > 0 && in.decoder != Cc::kDecoderUnavailable;
            if (o.command == Cc::kCommandStop && in.museumActive && !in.inRoom && shown) {
                ++exits;
            } else {
                // Anything else is the visitor's own click, finished on the control it began on.
                bool fair = release && o.consumeFire && atControl;
                if (o.command == Cc::kCommandSelect) {
                    ++selects;
                    fair = fair && in.region != Cc::kRegionPlay && in.decoder != Cc::kDecoderPlaying &&
                           in.films > 1 && o.index >= 0 && unsigned(o.index) < in.films;
                } else if (o.command == Cc::kCommandPlay) {
                    ++plays;
                    const bool autoSwitch = in.region != Cc::kRegionPlay && in.decoder == Cc::kDecoderPlaying && in.films > 1;
                    fair = fair && (in.region == Cc::kRegionPlay || autoSwitch) &&
                           o.index >= 0 && unsigned(o.index) < in.films;
                } else {
                    ++stops;
                    fair = fair && in.region == Cc::kRegionPlay && in.decoder == Cc::kDecoderPlaying;
                }
                strays += !fair;
            }
        }
        // During a visit, nothing stays on the screen behind a visitor who is outside the room.
        if (in.museumActive && !in.inRoom && shown && o.command != Cc::kCommandStop) ++left;

        // The player obeys, mostly: now and then it fails to start or is slow to stop.
        if (o.command == Cc::kCommandPlay) {
            if (Chance(0.05f)) { in.decoder = Cc::kDecoderStopped; in.playing = -1; }
            else { in.decoder = Cc::kDecoderPlaying; in.playing = o.index; }
        } else if (o.command == Cc::kCommandStop && !Chance(0.1f)) {
            in.decoder = Cc::kDecoderStopped;
            in.playing = -1;
        }
    }
    std::printf("stream: %u presses to the console, %u to the game; %u select, %u play, %u stop, %u cleared on leaving\n",
                swallowed, passed, selects, plays, stops, exits);
    CHECK(mismatches == 0);
    CHECK(halves == 0 && orphans == 0);
    CHECK(strays == 0);
    CHECK(left == 0);
    // The stream is only worth its length if it reached every kind of event.
    CHECK(swallowed > 50 && passed > 50 && selects > 20 && plays > 20 && stops > 5 && exits > 5);
}

// Grand Outdoor/Indoor Multi-Wing Museum Complex (M1-M4 RoomGraph walkability, portals,
// obstacles, sightlines, and Return Arch checks).
void TestGrandComplex() {
    namespace G = Ex::Grand;
    const G::RoomGraph g = G::MakeRoomGraph();
    CHECK(g.obstacleCount > 30u && g.obstacleCount <= unsigned(G::kMaxObstacles));

    // 1. All 8 rooms are well-formed, non-overlapping, and stay within kCeMuseumHallRadius (90 m).
    for (unsigned r = 0; r < G::kRoomCount; ++r) {
        const G::RoomBounds &b = g.rooms[r];
        CHECK(b.maxX > b.minX + 10.0f && b.maxY > b.minY + 10.0f);
        CHECK((r == G::kRoomCourtyard) == b.openSky);
        const float corners[4][2] = {{b.minX, b.minY}, {b.maxX, b.minY}, {b.minX, b.maxY}, {b.maxX, b.maxY}};
        for (unsigned c = 0; c < 4; ++c)
            CHECK(std::hypot(corners[c][0], corners[c][1]) < 86.0f);
        for (unsigned o = r + 1; o < G::kRoomCount; ++o) {
            const G::RoomBounds &ob = g.rooms[o];
            const bool overlapX = b.minX < ob.maxX - 1e-3f && ob.minX < b.maxX - 1e-3f;
            const bool overlapY = b.minY < ob.maxY - 1e-3f && ob.minY < b.maxY - 1e-3f;
            CHECK(!(overlapX && overlapY));
        }
    }

    // 2. Arrival point is open in Courtyard, looking north sees Hero Monument clearly, looking south faces Return Arch.
    CHECK(G::RoomAt(g, Ex::Vec2{0.0f, 0.0f}) == G::kRoomCourtyard);
    CHECK(G::WalkAt(g, Ex::Vec2{0.0f, 0.0f}) == Ex::kWalkOpen);
    CHECK(G::SightClear(g, Ex::Vec3{0.0f, 0.0f, 1.7f}, Ex::Vec3{0.0f, 10.5f, 3.0f}));
    CHECK(G::FacesExit(Ex::Vec2{0.0f, 0.0f}, Ex::Vec2{0.0f, -1.0f}, false));
    CHECK(!G::FacesExit(Ex::Vec2{0.0f, 0.0f}, Ex::Vec2{0.0f, 1.0f}, true));
    CHECK(!G::FacesExit(Ex::Vec2{0.0f, 25.0f}, Ex::Vec2{0.0f, -1.0f}, false));

    // 3. Hero Aquila plinth, reflecting pools, side directory console, rotunda pedestal, skylights, and audio kiosks block walking.
    CHECK(G::WalkAt(g, Ex::Vec2{0.0f, 10.5f}) == Ex::kWalkBlocked);
    CHECK(G::WalkAt(g, Ex::Vec2{-10.0f, 10.0f}) == Ex::kWalkBlocked);
    CHECK(G::WalkAt(g, Ex::Vec2{10.0f, 10.0f}) == Ex::kWalkBlocked);
    // Central entrance sightline at (0, 31.5) is completely unobstructed; lectern sits on west flank (-5.4, 26.8); rotunda centerpiece stands at (0, 36).
    CHECK(G::WalkAt(g, Ex::Vec2{0.0f, 31.5f}) == Ex::kWalkOpen);
    CHECK(G::WalkAt(g, Ex::Vec2{-5.4f, 26.8f}) == Ex::kWalkBlocked);
    CHECK(G::WalkAt(g, Ex::Vec2{-5.4f, 24.8f}) == Ex::kWalkOpen);
    CHECK(G::WalkAt(g, Ex::Vec2{0.0f, 36.0f}) == Ex::kWalkBlocked);
    CHECK(G::SightClear(g, Ex::Vec3{0.0f, 21.0f, 1.7f}, Ex::Vec3{0.0f, 36.0f, 1.7f}));
    CHECK(G::PathClear(g, Ex::Vec2{0.0f, 0.0f}, Ex::Vec2{4.5f, 4.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{4.5f, 4.0f}, Ex::Vec2{4.5f, 16.5f}));
    CHECK(G::PathClear(g, Ex::Vec2{4.5f, 16.5f}, Ex::Vec2{0.0f, 25.0f}));
    // Walking straight through the Hero plinth is rejected.
    CHECK(!G::PathClear(g, Ex::Vec2{0.0f, 0.0f}, Ex::Vec2{0.0f, 25.0f}));

    // 4. Every portal connects its rooms cleanly (centre is walkable/throat, jambs solid).
    for (unsigned p = 0; p < G::kPortalCount; ++p) {
        const G::Portal &pt = g.portals[p];
        CHECK(pt.hi - pt.lo >= 4.9f && pt.clearHeight >= 4.5f);
        const float mid = 0.5f * (pt.lo + pt.hi);
        if (pt.vertical) {
            CHECK(G::WalkAt(g, Ex::Vec2{pt.line, mid}) == Ex::kWalkThroat);
            CHECK(G::PathClear(g, Ex::Vec2{pt.line - 2.0f, mid}, Ex::Vec2{pt.line + 2.0f, mid}));
            CHECK(G::WalkAt(g, Ex::Vec2{pt.line, pt.lo - 1.2f}) == Ex::kWalkBlocked);
            CHECK(G::WalkAt(g, Ex::Vec2{pt.line, pt.hi + 1.2f}) == Ex::kWalkBlocked);
        } else {
            CHECK(G::WalkAt(g, Ex::Vec2{mid, pt.line}) == Ex::kWalkThroat);
            CHECK(G::PathClear(g, Ex::Vec2{mid, pt.line - 2.0f}, Ex::Vec2{mid, pt.line + 2.0f}));
            CHECK(G::WalkAt(g, Ex::Vec2{pt.lo - 1.2f, pt.line}) == Ex::kWalkBlocked);
            CHECK(G::WalkAt(g, Ex::Vec2{pt.hi + 1.2f, pt.line}) == Ex::kWalkBlocked);
        }
    }

    // 5. Continuous walkable route from Courtyard -> Atrium -> EVERY wing -> Screening Room.
    // Central approach (0, 25) -> (0, 32) is straight and unobstructed; rotunda promenade loops cleanly around the (0, 36) pedestal.
    CHECK(G::PathClear(g, Ex::Vec2{0.0f, 25.0f}, Ex::Vec2{0.0f, 32.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{0.0f, 32.0f}, Ex::Vec2{-4.5f, 32.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{-4.5f, 32.0f}, Ex::Vec2{-4.5f, 36.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{0.0f, 32.0f}, Ex::Vec2{4.5f, 32.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{4.5f, 32.0f}, Ex::Vec2{4.5f, 36.0f}));
    // Atrium -> Forseti Wing (all 6 native bay pads reachable)
    CHECK(G::PathClear(g, Ex::Vec2{-4.5f, 36.0f}, Ex::Vec2{-12.0f, 41.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{-12.0f, 41.0f}, Ex::Vec2{-36.5f, 41.0f}));
    for (unsigned i = 0; i < G::kNativeBayCount; ++i) {
        const G::NativeBaySpot &nb = g.nativeBays[i];
        CHECK(G::WalkAt(g, Ex::Vec2{nb.x, nb.y}) == Ex::kWalkBlocked);
        CHECK(G::WalkAt(g, Ex::Vec2{nb.padX, nb.padY}) == Ex::kWalkOpen);
        CHECK(G::PathClear(g, Ex::Vec2{nb.padX, 41.0f}, Ex::Vec2{nb.padX, nb.padY}));
    }
    // Atrium -> Muspell Wing
    CHECK(G::PathClear(g, Ex::Vec2{4.5f, 36.0f}, Ex::Vec2{12.0f, 41.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{12.0f, 41.0f}, Ex::Vec2{38.0f, 41.0f}));
    // Atrium -> Aviation & Naval Hangar (around rotunda pedestal)
    CHECK(G::PathClear(g, Ex::Vec2{-4.5f, 36.0f}, Ex::Vec2{-4.5f, 41.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{-4.5f, 41.0f}, Ex::Vec2{0.0f, 41.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{0.0f, 41.0f}, Ex::Vec2{0.0f, 70.0f}));
    // Naval basin blocks walking; west & centre hangar aisle stay clear.
    CHECK(G::WalkAt(g, Ex::Vec2{13.0f, 65.0f}) == Ex::kWalkBlocked);
    CHECK(G::WalkAt(g, Ex::Vec2{-8.0f, 65.0f}) == Ex::kWalkOpen);
    // Atrium -> Early Designs Vault
    CHECK(G::PathClear(g, Ex::Vec2{0.0f, 25.0f}, Ex::Vec2{-20.0f, 24.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{-20.0f, 24.0f}, Ex::Vec2{-36.0f, 24.0f}));
    // Atrium -> Concept Art Gallery -> Screening Room -> Cinema aisle
    CHECK(G::PathClear(g, Ex::Vec2{0.0f, 25.0f}, Ex::Vec2{20.0f, 24.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{20.0f, 24.0f}, Ex::Vec2{36.0f, 24.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{36.0f, 24.0f}, Ex::Vec2{36.0f, 14.5f}));
    CHECK(G::PathClear(g, Ex::Vec2{36.0f, 14.5f}, Ex::Vec2{42.0f, 14.5f}));
    CHECK(G::PathClear(g, Ex::Vec2{42.0f, 14.5f}, Ex::Vec2{42.0f, 19.0f}));
    CHECK(G::PathClear(g, Ex::Vec2{42.0f, 19.0f}, Ex::Vec2{47.5f, 19.0f}));
    // Outer void and NaN are always blocked.
    CHECK(G::WalkAt(g, Ex::Vec2{0.0f, -12.0f}) == Ex::kWalkBlocked);
    CHECK(G::WalkAt(g, Ex::Vec2{60.0f, 15.0f}) == Ex::kWalkBlocked);
    CHECK(G::WalkAt(g, Ex::Vec2{std::numeric_limits<float>::quiet_NaN(), 0.0f}) == Ex::kWalkBlocked);
}
} // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--visit") == 0) {
        // The hall's own room and a Forseti visit's six: where each stands and is seen from.
        Ex::Plan p;
        if (!Ex::MakePlan(HallRoom(), kVisit, 6, &p)) {
            std::printf("refused: %s\n", Ex::ReasonText(p.reason));
            return 1;
        }
        for (unsigned i = 0; i < p.bayCount; ++i) {
            const Ex::Bay &b = p.bays[i];
            std::printf("bay %d model %.2f,%.2f top %.2f half %.2f pad %.2f,%.2f view %.2f\n", b.number,
                        double(b.model.x), double(b.model.y), double(b.model.z), double(b.plinthHalf),
                        double(b.pad.x), double(b.pad.y), double(b.viewDistance));
        }
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--dump") == 0) {
        Ex::Plan p;
        if (!Ex::MakePlan(Room(), kSix, 6, &p)) {
            std::printf("refused: %s\n", Ex::ReasonText(p.reason));
            return 1;
        }
        std::string text;
        Dump(p, &text);
        std::fputs(text.c_str(), stdout);
        return 0;
    }
    if (argc != 1) {
        std::fprintf(stderr, "usage: %s [--dump | --visit]\n", argv[0]);
        return 2;
    }
    struct Group { const char *name; void (*run)(); };
    const Group groups[] = {
        {"plans", TestPlans},
        {"small", TestSmall},
        {"focus", TestFocus},
        {"neighbours", TestNeighbours},
        {"exit", TestExit},
        {"translation", TestTranslation},
        {"generated", TestGenerated},
        {"hall", TestHall},
        {"reservations", TestReservations},
        {"awkward", TestAwkward},
        {"cinema-lists", TestCinemaLists},
        {"cinema-ownership", TestCinemaOwnership},
        {"cinema-leaving", TestCinemaLeaving},
        {"cinema-decoder", TestCinemaDecoder},
        {"cinema-edges", TestCinemaEdges},
        {"cinema-stream", TestCinemaStream},
        {"console", TestConsole},
        {"grand-complex", TestGrandComplex},
    };
    for (unsigned i = 0; i < sizeof groups / sizeof groups[0]; ++i) {
        const int checks = gChecks, failures = gFailures;
        groups[i].run();
        std::printf("%s %s (%d checks)\n", gFailures == failures ? "ok" : "FAIL", groups[i].name, gChecks - checks);
    }
    std::printf("largest plan: %u boxes of %d\n", gMostBoxes, int(Ex::kMaxBoxes));
    std::printf("ce_museum_exhibition_driver: %d checks, %d failed\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
