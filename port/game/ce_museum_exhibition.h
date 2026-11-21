// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_exhibition.h: the exhibition room as a plan. Plain arithmetic
// with no engine type, so a host test runs it (ce_museum_exhibition_tests.py).
//
// The room is one industrial hall, built from a small vocabulary:
//   enclosure  floor, four walls, the return door, wall piers and the portal's
//              jambs and header beam;
//   display    one plinth per machine, a taller pedestal for a small one, and a
//              numbered lectern that abuts it;
//   barrier    a low rail closing the pocket between two plinths that stand close;
//   trim       skirting, one band on the back wall, accents over the two doorways;
//   floor      one walking surface (arrival panel, nave runner, cinema branch and
//              forecourt) and a viewing pad with a toe line in front of each machine.
// Up to six machines stand in bays along the two side walls, so the middle of the
// floor is an open nave from the return door to the back wall. Piers stand between
// neighbouring bays and at the ends of a row, so each machine has its own framed
// bay. The lectern carries the bay's numeral once, on the face toward the nave, so
// it reads as one digit from any angle, and never stands between pad and machine. The
// cinema portal, whose interval the caller supplies, is framed and never built
// over; its approach is a branch of the same walking surface.
//
// Besides boxes the plan names the floor it promises to keep clear (zones), so a
// host knows where its own native pieces must not stand, and accepts rectangles
// the host has already filled (HostKeep), which it then builds nothing on.
//
// Coordinates are visitor-relative: X to the right and Y forward of the entrance
// view, Z the height above the authored floor (up is positive; a caller whose
// world counts Z downward negates it). Nothing here rescales a machine: radius and
// height are echoed as given and only decide where things stand and how high: a
// low machine stands on a taller pedestal, and a small object (a soldier) on a tall
// narrow plinth that brings it to the eye, viewed from nearer (IsSmall).
#ifndef CE_MUSEUM_EXHIBITION_H
#define CE_MUSEUM_EXHIBITION_H
#include <cmath>

namespace CeMuseumExhibition {

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

enum { kMaxBays = 6, kMaxBoxes = 224, kMaxRoute = 12, kMaxKeeps = 4, kMaxZones = 14,
       kMaxSpans = 6, kMaxCuts = 16 };

// One machine to show. stableLabel is the caller's own identity for its name,
// class and armament card (not negative, and different for every display).
// radius is the footprint radius and height the standing height, both as drawn.
struct DisplayInput { int stableLabel; float radius, height; };

// The room's outline, on the centre lines of its walls. The entrance is the point
// the visitor arrives at; the return door is on the front (minY) wall behind it.
// portalSide is +1 for an opening in the maxX wall, -1 for the minX wall, 0 for
// none; the opening spans portalMinY to portalMaxY and stays open.
struct RoomSpec {
    float minX, maxX, minY, maxY;
    Vec2 entrance;
    int portalSide;
    float portalMinY, portalMaxY;
};

// What a box is for. The solid ones stop a visitor; the rest are only drawn.
// The first eight values are as they always were; new purposes are added after.
enum Purpose {
    kSolidWall, kSolidPlinth, kSolidStand, kSolidDoor,
    kDecorFloor, kDecorInlay, kDecorTrim, kDecorNumeral,
    kSolidPier,     // a pier standing against a wall, or a jamb of the portal
    kSolidHeader,   // the beam over the portal: enclosure, wholly overhead
    kSolidRail      // a low guard rail between two plinths
};
inline bool IsSolid(Purpose purpose) {
    return purpose <= kSolidDoor || purpose == kSolidPier || purpose == kSolidHeader || purpose == kSolidRail;
}
// The five kinds of piece a host tells apart. Only trim and floor are never solid.
enum Category { kCategoryEnclosure, kCategoryDisplay, kCategoryBarrier, kCategoryTrim, kCategoryFloor };
inline Category CategoryOf(Purpose purpose) {
    switch (purpose) {
    case kSolidPlinth: case kSolidStand: return kCategoryDisplay;
    case kSolidRail: return kCategoryBarrier;
    case kDecorTrim: case kDecorNumeral: return kCategoryTrim;
    case kDecorFloor: case kDecorInlay: return kCategoryFloor;
    default: return kCategoryEnclosure;
    }
}

// An opaque axis-aligned box. colour is 0xAARRGGBB with full alpha. bay is the
// number of the bay it belongs to, or 0 for the room itself.
struct Box { Vec3 min, max; unsigned colour; Purpose purpose; int bay; };

enum RouteKind { kRouteReturn, kRouteSpine, kRouteBay, kRouteCinema };
struct Segment { Vec2 a, b; RouteKind kind; int bay; };

struct Bay {
    int number;            // 1 to 6, the numeral on the stand
    unsigned input;        // index of the display this bay shows
    int stableLabel;       // that display's identity, for the card
    float radius, height;  // echoed unchanged
    int side;              // -1 on the minX wall, +1 on the maxX wall
    Vec3 model;            // where the machine's base centre stands (on the plinth; z is the plinth's top)
    float plinthHalf;      // half the plinth's side
    Vec2 pad;              // centre of the floor pad to view it from
    Vec2 facing;           // unit direction from the pad to the machine
    float viewDistance;    // from the pad's centre to the machine's centre
    bool viewShortened;    // the room was too narrow for the preferred distance
    Vec2 zoneEnd;          // the viewing zone runs from pad to this point at the plinth
    Vec2 stand;            // centre of the numbered stand
    Vec3 labelAnchor;      // just above the stand, for a caller that pins text there
    Vec3 lookAt;           // the machine's middle, for a viewing pose
    Vec3 labelFace;        // middle of the numbered face, the stand's face toward the nave
    Vec2 labelNormal;      // the unit direction that face looks in
};

enum Reason {
    kReasonNone, kReasonNoOutput, kReasonBadRoom, kReasonBadCount, kReasonBadDisplay,
    kReasonDuplicateLabel, kReasonTooDeep, kReasonNoFit, kReasonBlocked, kReasonInternal,
    kReasonReserved
};
inline const char *ReasonText(Reason reason) {
    switch (reason) {
    case kReasonNone: return "ok";
    case kReasonNoOutput: return "no plan to fill";
    case kReasonBadRoom: return "room outline, entrance or portal is not usable";
    case kReasonBadCount: return "between one and six displays are needed";
    case kReasonBadDisplay: return "a display's radius or height is not a usable number";
    case kReasonDuplicateLabel: return "a display's label is negative or repeated";
    case kReasonTooDeep: return "a display is too wide to view from its own side of the aisle";
    case kReasonNoFit: return "the displays do not fit along the walls with clearance";
    case kReasonBlocked: return "the finished plan failed its own clearance check";
    case kReasonReserved: return "a host reservation is unusable, or lies on the aisle or the cinema approach";
    default: return "internal limit reached";
    }
}

// Floor the plan promises to keep clear: nothing of its own but floor stands there
// below head height, and the host must put nothing of its own there either.
enum ZoneKind {
    kZoneArrival,    // inside the return door, the full width of the nave
    kZoneNave,       // the centre aisle from the arrival to the back of the room
    kZoneApproach,   // from the nave to the portal, the portal's whole width
    kZoneThroat,     // within the wall's thickness, in the portal
    kZoneViewing,    // from a bay's pad to the foot of its plinth, the plinth's width
    kZoneHost        // a host reservation, echoed: the plan built nothing on it
};
// bay is the bay's number for kZoneViewing, the reservation's ordinal (from 1) for
// kZoneHost, and 0 otherwise.
struct Zone { Vec2 min, max; ZoneKind kind; int bay; };

// A floor rectangle the host has filled with something of its own. The plan
// stands no bay, pier or rail on it. One that reaches the aisle or the cinema
// approach is refused (kReasonReserved): the plan cannot promise a way past it.
struct HostKeep { float minX, minY, maxX, maxY; };
// allowEmpty plans the hall and the cinema approach for no displays at all.
struct PlanOptions { const HostKeep *keeps; unsigned keepCount; bool allowEmpty; };

struct Plan {
    bool valid;
    Reason reason;
    RoomSpec room;
    unsigned bayCount, boxCount, routeCount, zoneCount;
    Zone zones[kMaxZones];
    float wallHeight;          // top of the walls and piers; raised for a tall machine
    float portalClearHeight;   // underside of the portal's header beam, or 0 with no portal
    Bay bays[kMaxBays];
    Box boxes[kMaxBoxes];
    Segment route[kMaxRoute];
    Vec2 doorCentre;       // middle of the return door, on the front wall's face
    float doorHalf;
    Vec2 forecourt;        // a clear standing point in front of the portal
};

// Dimensions, in the same units as the room.
constexpr float kWallHalf = 0.30f, kWallHeight = 7.0f;
constexpr float kFloorTop = 0.02f, kInlayTop = 0.032f;
constexpr float kVisitorRadius = 0.56f, kBodyTop = 2.2f;
constexpr float kAisleHalf = 3.0f;          // no solid comes nearer the centre line
// A plinth is kPlinthHeight tall; a machine shorter than kPedestalReach - kPlinthHeight
// stands on a taller pedestal, its top kPedestalReach less the machine's height, up
// to kPedestalHeight, so a small machine is raised toward the eye (PlinthTop).
constexpr float kPlinthMargin = 0.60f, kPlinthHeight = 0.60f, kRearGap = 0.40f;
constexpr float kPedestalHeight = 0.90f, kPedestalReach = 1.30f;
constexpr float kPadHalf = 1.10f, kPadFromSpine = 1.80f, kLineHalf = 0.08f;
// A small object, lower than kSmallHeight and within kSmallRadius, is staged as a museum
// stages one: a narrow plinth (its margin kSmallMargin, its half at least kSmallHalfMin)
// tall enough that the object's top is kSmallBelowEye under the eye, and a viewing pad
// kSmallView from it, nearer than a machine's, never so near that the pad meets the plinth
// or its stand.
// kSmallHalfMin keeps the walk from the pad to the plinth clear of the stand's corner.
constexpr float kSmallHeight = 0.50f, kSmallRadius = 0.50f, kSmallMargin = 0.20f, kSmallHalfMin = 0.48f;
constexpr float kSmallBelowEye = 0.05f, kSmallView = 2.0f;
// The pad stands back about half the machine's larger dimension from its near edge,
// at least kViewMin; never nearer than kViewFloor, which keeps the pad off the plinth.
constexpr float kViewMin = 2.0f, kViewMax = 10.0f, kViewFloor = 1.8f, kViewShare = 0.5f;
// The lectern abuts its plinth's side (offset equals its half length); kStandReach
// is the wall length kept for it, as before.
constexpr float kStandHalfAcross = 0.35f, kStandHalfDeep = 0.25f, kStandHeight = 0.98f;
constexpr float kStandOffset = 0.35f, kStandReach = 0.90f, kBayGap = 1.50f;
constexpr float kNumeralMid = 0.58f, kNumeralProud = 0.02f, kLabelStandOff = 0.05f;
// Enclosure: walls rise for a tall machine up to a limit; piers and the portal's frame.
constexpr float kWallHeightMax = 16.0f, kHeadroom = 1.20f;
constexpr float kPierDeep = 0.45f, kPierHalf = 0.35f, kPierLead = 1.20f, kPierKeep = 0.40f;
constexpr float kJambClear = 0.30f, kPortalHeight = 4.60f, kTrimDeep = 0.06f;
// Barrier: plinths nearer than kPocketClose have the pocket between them railed off.
constexpr float kRailDeep = 0.10f, kRailHeight = 0.55f, kPocketClose = 4.0f;
// Floor: the runner's half width, a bay's tick beside it and a pad's toe line.
constexpr float kNaveHalf = 0.60f, kTickLong = 0.50f, kToeDeep = 0.16f;
constexpr float kZoneInset = 0.05f, kEyeHeight = 1.70f;
constexpr float kEntrancePadHalf = 1.20f, kBayStart = 1.80f, kBackKeep = 0.60f;
constexpr float kForecourtMargin = 1.50f, kForecourtDepth = 3.0f;
constexpr float kDoorHalf = 2.20f, kDoorDeep = 0.12f, kDoorHeight = 3.40f;
constexpr float kSkirtDeep = 0.08f, kSkirtHeight = 0.30f;
// Standing in a bay's viewing zone, and looking at its machine.
constexpr float kZoneEnter = 2.0f, kZoneKeep = 2.8f;
constexpr float kFaceEnter = 0.175f, kFaceKeep = 0.35f;   // radians beyond the machine's edge

// Pale concrete and plaster, steel-grey structure and furniture, one amber accent.
// The walking surface is one tone wherever it runs, so it reads as one path.
constexpr unsigned kColourFloor = 0xffdfe1ddu, kColourWall = 0xffefede7u;
constexpr unsigned kColourSkirt = 0xff8d9aa0u, kColourBand = 0xffbcd2d8u;
constexpr unsigned kColourPlinth = 0xffb4bdc0u, kColourPad = 0xffaab4b8u;
constexpr unsigned kColourStand = 0xff55646du, kColourNumeral = 0xfff4f2ecu;
constexpr unsigned kColourAccent = 0xffd9a441u, kColourDoor = 0xff6f838cu;
constexpr unsigned kColourRunner = 0xffc6cbcau, kColourPier = 0xff66767fu, kColourRail = 0xff414d55u;
constexpr unsigned kColourEntrance = kColourRunner, kColourForecourt = kColourRunner;

// Whether a box can stop a visitor: solid, and not wholly above head height.
inline bool Obstructs(const Box &box) { return IsSolid(box.purpose) && box.min.z < kBodyTop; }

// Whether a display is a small object, staged on a tall narrow plinth near the eye.
inline bool IsSmall(float radius, float height) { return height < kSmallHeight && radius < kSmallRadius; }
// Half the side of the plinth a display stands on.
inline float PlinthHalf(float radius, float height) {
    if (!IsSmall(radius, height)) return radius + kPlinthMargin;
    return radius + kSmallMargin > kSmallHalfMin ? radius + kSmallMargin : kSmallHalfMin;
}
// The height of the plinth top a display stands on, above the floor: a small object's
// top just under the eye, a low machine's on a pedestal, any other on a plinth.
inline float PlinthTop(float height, float radius) {
    if (IsSmall(radius, height)) return kFloorTop + kEyeHeight - kSmallBelowEye - height;
    const float raised = kPedestalReach - height;
    return kFloorTop + (raised > kPedestalHeight ? kPedestalHeight : raised > kPlinthHeight ? raised : kPlinthHeight);
}

namespace detail {
inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }
inline float WestIn(const RoomSpec &r) { return r.minX + kWallHalf; }
inline float EastIn(const RoomSpec &r) { return r.maxX - kWallHalf; }
inline float FrontIn(const RoomSpec &r) { return r.minY + kWallHalf; }
inline float BackIn(const RoomSpec &r) { return r.maxY - kWallHalf; }

inline void Push(Plan &p, float x0, float y0, float z0, float x1, float y1, float z1,
                 unsigned colour, Purpose purpose, int bay) {
    if (p.boxCount >= kMaxBoxes || !(x1 > x0 && y1 > y0 && z1 > z0)) { p.reason = kReasonInternal; return; }
    Box &b = p.boxes[p.boxCount++];
    b.min = Vec3{x0, y0, z0}; b.max = Vec3{x1, y1, z1};
    b.colour = colour; b.purpose = purpose; b.bay = bay;
}
inline void Route(Plan &p, Vec2 a, Vec2 b, RouteKind kind, int bay) {
    if (p.routeCount >= kMaxRoute) { p.reason = kReasonInternal; return; }
    Segment &s = p.route[p.routeCount++];
    s.a = a; s.b = b; s.kind = kind; s.bay = bay;
}
inline bool Overlap(const Box &a, const Box &b) {
    const float e = 1.0e-4f;
    return a.min.x < b.max.x - e && b.min.x < a.max.x - e &&
           a.min.y < b.max.y - e && b.min.y < a.max.y - e &&
           a.min.z < b.max.z - e && b.min.z < a.max.z - e;
}
// Whether a host reservation reaches the bays on one side of the aisle.
inline bool KeepOnSide(const HostKeep &k, float ex, int side) {
    return side < 0 ? k.minX < ex - kAisleHalf : k.maxX > ex + kAisleHalf;
}
// Removes the stretch a to b from a list of ascending stretches.
inline void Cut(float span[kMaxSpans][2], unsigned &n, float a, float b) {
    float out[kMaxSpans][2];
    unsigned m = 0;
    for (unsigned i = 0; i < n; ++i) {
        const float lo = span[i][0], hi = span[i][1];
        if (b <= lo || a >= hi) {
            if (m < unsigned(kMaxSpans)) { out[m][0] = lo; out[m][1] = hi; ++m; }
            continue;
        }
        if (a > lo && m < unsigned(kMaxSpans)) { out[m][0] = lo; out[m][1] = a; ++m; }
        if (b < hi && m < unsigned(kMaxSpans)) { out[m][0] = b; out[m][1] = hi; ++m; }
    }
    for (unsigned i = 0; i < m; ++i) { span[i][0] = out[i][0]; span[i][1] = out[i][1]; }
    n = m;
}
// The free stretches of one side wall between lo and hi, in ascending order,
// leaving alone the portal with its forecourt and every host reservation that
// reaches this side of the aisle.
inline unsigned Spans(const RoomSpec &r, int side, float lo, float hi, const PlanOptions &o,
                      float out[kMaxSpans][2]) {
    unsigned n = 0;
    if (hi > lo) { out[0][0] = lo; out[0][1] = hi; n = 1; }
    if (r.portalSide == side) Cut(out, n, r.portalMinY - kForecourtMargin, r.portalMaxY + kForecourtMargin);
    for (unsigned k = 0; k < o.keepCount; ++k)
        if (KeepOnSide(o.keeps[k], r.entrance.x, side))
            Cut(out, n, o.keeps[k].minY - kBayGap, o.keeps[k].maxY + kBayGap);
    return n;
}
// Places displays first to last-1, in order, along the free stretches, walking
// toward the back wall (dir +1) or the front (dir -1). Each takes its plinth and
// the stand that trails it; the room left over is shared out evenly.
inline bool Pack(const float *half, unsigned first, unsigned last, const float span[][2],
                 unsigned spans, int dir, float *centre) {
    unsigned next = first;
    for (unsigned s = 0; s < spans && next < last; ++s) {
        const unsigned k = dir > 0 ? s : spans - 1 - s;
        const float length = span[k][1] - span[k][0];
        float used = 0.0f;
        unsigned end = next;
        while (end < last) {
            const float need = 2.0f * half[end] + kStandReach + (end > next ? kBayGap : 0.0f);
            if (used + need > length) break;
            used += need; ++end;
        }
        if (end == next) continue;
        const float spare = (length - used) / float(end - next + 1);
        float walked = spare;
        for (unsigned i = next; i < end; ++i) {
            const float c = walked + half[i];
            centre[i] = dir > 0 ? span[k][0] + c : span[k][1] - c;
            walked += 2.0f * half[i] + kStandReach + kBayGap + spare;
        }
        next = end;
    }
    return next == last;
}
inline float SegmentDistance(Vec2 p, Vec2 a, Vec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const float ex = p.x - (a.x + t * dx), ey = p.y - (a.y + t * dy);
    return std::sqrt(ex * ex + ey * ey);
}
} // namespace detail

enum Walk {
    kWalkBlocked,   // a wall, pier, plinth, stand, rail or the closed door is in the way
    kWalkOpen,      // on the exhibition floor
    kWalkThroat,    // within the wall's thickness, in the portal
    kWalkOutside    // through the portal: the room beyond decides, not this plan
};

namespace detail {
inline Walk WalkGeometry(const Plan &p, Vec2 v, float radius) {
    const RoomSpec &r = p.room;
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !(radius >= 0.0f)) return kWalkBlocked;
    const float past = r.portalSide > 0 ? v.x - EastIn(r) : r.portalSide < 0 ? WestIn(r) - v.x : -1.0f;
    if (past > 2.0f * kWallHalf) return kWalkOutside;
    for (unsigned i = 0; i < p.boxCount; ++i) {
        const Box &b = p.boxes[i];
        if (!IsSolid(b.purpose) || b.min.z >= kBodyTop) continue;
        const float dx = Max(Max(b.min.x - v.x, v.x - b.max.x), 0.0f);
        const float dy = Max(Max(b.min.y - v.y, v.y - b.max.y), 0.0f);
        if (dx * dx + dy * dy < radius * radius) return kWalkBlocked;
    }
    if (v.x >= WestIn(r) && v.x <= EastIn(r) && v.y >= FrontIn(r) && v.y <= BackIn(r)) return kWalkOpen;
    if (past > 0.0f && v.y >= r.portalMinY && v.y <= r.portalMaxY) return kWalkThroat;
    return kWalkBlocked;
}
inline bool PathGeometry(const Plan &p, Vec2 a, Vec2 b, float radius) {
    const float dx = b.x - a.x, dy = b.y - a.y, length = std::sqrt(dx * dx + dy * dy);
    if (!std::isfinite(length) || length > 400.0f) return false;
    const unsigned steps = unsigned(length / 0.2f) + 1;
    for (unsigned i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        if (WalkGeometry(p, Vec2{a.x + t * dx, a.y + t * dy}, radius) == kWalkBlocked) return false;
    }
    return true;
}
// The bay's number as seven raised bars, once, on the face of its stand that looks
// into the aisle. A second numeral on a neighbouring face read as a doubled digit
// from an angle, and one on the end face toward the plinth met a taller plinth.
inline void Numeral(Plan &p, const Bay &b) {
    static const unsigned char mask[7] = {0, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d};
    static const float bar[7][4] = {   // across, up, half across, half up
        {0.0f, 0.27f, 0.17f, 0.0225f}, {0.17f, 0.135f, 0.0225f, 0.11f}, {0.17f, -0.135f, 0.0225f, 0.11f},
        {0.0f, -0.27f, 0.17f, 0.0225f}, {-0.17f, -0.135f, 0.0225f, 0.11f}, {-0.17f, 0.135f, 0.0225f, 0.11f},
        {0.0f, 0.0f, 0.17f, 0.0225f}};
    if (b.number < 1 || b.number > 6) return;
    const float face = b.stand.x - float(b.side) * kStandHalfDeep, zc = kFloorTop + kNumeralMid;
    const float xa = b.side > 0 ? face - kNumeralProud : face, xb = b.side > 0 ? face : face + kNumeralProud;
    for (unsigned s = 0; s < 7; ++s) {
        if (!(mask[b.number] & (1u << s))) continue;
        const float yc = b.stand.y - float(b.side) * bar[s][0], z = zc + bar[s][1];
        Push(p, xa, yc - bar[s][2], z - bar[s][3], xb, yc + bar[s][2], z + bar[s][3],
             kColourNumeral, kDecorNumeral, b.number);
    }
}
} // namespace detail

// Fills *out with the room for these displays, in the order given: bay 1 is the
// first display. Returns false, with out->reason set and nothing to draw, when
// the room or a display is unusable or the displays cannot all stand with
// clearance; a display is never dropped, moved onto another or resized to fit.
// options carries the host's reservations and whether an empty hall may be planned.
inline bool MakePlan(const RoomSpec &room, const DisplayInput *displays, unsigned count,
                     const PlanOptions &options, Plan *out) {
    using namespace detail;
    if (!out) return false;
    Plan &p = *out;
    p = Plan();
    p.room = room;
    auto fail = [&p](Reason why) {
        p.valid = false; p.reason = why; p.bayCount = p.boxCount = p.routeCount = p.zoneCount = 0;
        return false;
    };
    const float outline[] = {room.minX, room.maxX, room.minY, room.maxY, room.entrance.x, room.entrance.y};
    for (unsigned i = 0; i < 6; ++i)
        if (!std::isfinite(outline[i]) || std::fabs(outline[i]) > 4096.0f) return fail(kReasonBadRoom);
    const float westIn = WestIn(room), eastIn = EastIn(room), frontIn = FrontIn(room), backIn = BackIn(room);
    const float ex = room.entrance.x, ey = room.entrance.y;
    if (!(ex - westIn >= 8.0f && eastIn - ex >= 8.0f && ey - frontIn >= 2.0f && backIn - ey >= 10.0f))
        return fail(kReasonBadRoom);
    if (room.portalSide < -1 || room.portalSide > 1) return fail(kReasonBadRoom);
    const bool portal = room.portalSide != 0;
    const float pMin = room.portalMinY, pMax = room.portalMaxY, pMid = 0.5f * (pMin + pMax);
    if (portal && !(std::isfinite(pMin) && std::isfinite(pMax) && pMax - pMin >= 2.0f * kVisitorRadius + 0.28f &&
                    pMin >= frontIn + kSkirtDeep && pMax <= backIn - kSkirtDeep)) return fail(kReasonBadRoom);

    // Host reservations: usable rectangles, none on the aisle or the cinema approach.
    // One that lies wholly outside the room is ignored.
    HostKeep kept[kMaxKeeps];
    PlanOptions use = {kept, 0u, options.allowEmpty};
    if (options.keepCount > unsigned(kMaxKeeps) || (options.keepCount && !options.keeps)) return fail(kReasonReserved);
    for (unsigned k = 0; k < options.keepCount; ++k) {
        const HostKeep &h = options.keeps[k];
        const float edge[] = {h.minX, h.minY, h.maxX, h.maxY};
        for (unsigned i = 0; i < 4; ++i)
            if (!std::isfinite(edge[i]) || std::fabs(edge[i]) > 4096.0f) return fail(kReasonReserved);
        if (!(h.maxX > h.minX && h.maxY > h.minY)) return fail(kReasonReserved);
        if (!(h.minX < eastIn && h.maxX > westIn && h.minY < backIn && h.maxY > frontIn)) continue;
        if (h.minX < ex + kAisleHalf && h.maxX > ex - kAisleHalf) return fail(kReasonReserved);
        if (portal && KeepOnSide(h, ex, room.portalSide) && h.minY < pMax + kForecourtMargin &&
            h.maxY > pMin - kForecourtMargin) return fail(kReasonReserved);
        kept[use.keepCount++] = h;
    }

    if (count > unsigned(kMaxBays) || (count && !displays) || (!count && !options.allowEmpty))
        return fail(kReasonBadCount);
    float half[kMaxBays], face[2][kMaxBays];
    bool fits[2][kMaxBays], shortened[2][kMaxBays];
    for (unsigned i = 0; i < count; ++i) {
        const float r = displays[i].radius, h = displays[i].height;
        if (!std::isfinite(r) || !std::isfinite(h) || !(r >= 0.05f && r <= 40.0f && h >= 0.05f && h <= 80.0f))
            return fail(kReasonBadDisplay);
        if (displays[i].stableLabel < 0) return fail(kReasonDuplicateLabel);
        for (unsigned j = 0; j < i; ++j)
            if (displays[j].stableLabel == displays[i].stableLabel) return fail(kReasonDuplicateLabel);
        const bool small = IsSmall(r, h);
        half[i] = PlinthHalf(r, h);
        // Stand back about half the machine's larger dimension from its near edge,
        // within what this side of the aisle allows; the pad never crosses the centre line.
        // A small object is seen from kSmallView; the pad keeps off its plinth and the
        // stand that overhangs the plinth's front.
        const float nearest = half[i] + kStandHalfDeep + kPadHalf + 0.05f - r;
        const float want = small ? Max(kSmallView - r, nearest) : Min(Max(kViewShare * Max(2.0f * r, h), kViewMin), kViewMax);
        for (unsigned s = 0; s < 2; ++s) {
            const float across = s ? eastIn - ex : ex - westIn;
            const float reach = across - kRearGap - half[i] - r - kPadFromSpine;
            face[s][i] = Min(want, reach);
            // The stand overhangs the plinth's front by its half depth, its numeral a little
            // more; they, too, keep out of the aisle.
            fits[s][i] = face[s][i] >= (small ? nearest : kViewFloor) &&
                         across - kRearGap - 2.0f * half[i] - kStandHalfDeep - kNumeralProud >= kAisleHalf;
            shortened[s][i] = face[s][i] < want;
        }
        if (!fits[0][i] && !fits[1][i]) return fail(kReasonTooDeep);
    }

    // Bays 1.. run up the minX wall from the entrance and the rest come back down
    // the maxX wall, so the numbers follow one loop. An even split is tried first.
    const float lo = ey + kBayStart, hi = backIn - kBackKeep;
    float spanWest[kMaxSpans][2], spanEast[kMaxSpans][2], centre[kMaxBays];
    const unsigned spansWest = Spans(room, -1, lo, hi, use, spanWest), spansEast = Spans(room, 1, lo, hi, use, spanEast);
    int west = -1;
    const int even = int((count + 1) / 2);
    for (unsigned step = 0; step <= 2 * count && west < 0; ++step) {
        const int k = even + (step % 2 ? int(step + 1) / 2 : -int(step / 2));
        if (k < 0 || k > int(count)) continue;
        bool ok = true;
        for (unsigned i = 0; i < count; ++i) ok = ok && fits[int(i) < k ? 0 : 1][i];
        if (ok && Pack(half, 0, unsigned(k), spanWest, spansWest, 1, centre) &&
            Pack(half, unsigned(k), count, spanEast, spansEast, -1, centre)) west = k;
    }
    if (west < 0) return fail(kReasonNoFit);

    p.bayCount = count;
    // Each plinth's two side faces and its stand's two ends, along the wall: every
    // piece that meets them is built from these same numbers, so faces meet exactly.
    float py0[kMaxBays], py1[kMaxBays], sy0[kMaxBays], sy1[kMaxBays];
    float spineEnd = ey + kEntrancePadHalf + 0.5f;
    if (portal && pMid > spineEnd) spineEnd = pMid;
    for (unsigned i = 0; i < count; ++i) {
        Bay &b = p.bays[i];
        const int side = int(i) < west ? -1 : 1, dir = -side;
        const unsigned s = side > 0 ? 1u : 0u;
        const float r = displays[i].radius, cy = centre[i];
        const float cx = (side > 0 ? eastIn : westIn) - float(side) * (kRearGap + half[i]);
        b.number = int(i) + 1; b.input = i; b.stableLabel = displays[i].stableLabel;
        b.radius = r; b.height = displays[i].height; b.side = side;
        b.model = Vec3{cx, cy, PlinthTop(b.height, r)};
        b.plinthHalf = half[i];
        b.viewDistance = r + face[s][i];
        b.viewShortened = shortened[s][i];
        b.pad = Vec2{cx - float(side) * b.viewDistance, cy};
        b.facing = Vec2{float(side), 0.0f};
        b.zoneEnd = Vec2{cx - float(side) * half[i], cy};
        b.stand = Vec2{b.zoneEnd.x, cy + float(dir) * (half[i] + kStandOffset)};
        b.labelAnchor = Vec3{b.stand.x, b.stand.y, kFloorTop + kStandHeight + 0.35f};
        b.lookAt = Vec3{cx, cy, b.model.z + 0.5f * b.height};
        py0[i] = cy - half[i]; py1[i] = cy + half[i];
        if (dir > 0) { sy0[i] = py1[i]; sy1[i] = py1[i] + 2.0f * kStandHalfAcross; }
        else { sy0[i] = py0[i] - 2.0f * kStandHalfAcross; sy1[i] = py0[i]; }
        b.labelFace = Vec3{b.stand.x - float(side) * kStandHalfDeep, b.stand.y, kFloorTop + kNumeralMid};
        b.labelNormal = Vec2{-float(side), 0.0f};
        if (cy > spineEnd) spineEnd = cy;
    }

    // Enclosure: one floor and four walls, tall enough for the tallest machine. The
    // portal's wall is in two parts under a header beam, between two jambs.
    float wallTop = kWallHeight;
    for (unsigned i = 0; i < count; ++i)
        wallTop = Max(wallTop, p.bays[i].model.z + displays[i].height + kHeadroom);
    wallTop = Min(wallTop, kWallHeightMax);
    p.wallHeight = wallTop;
    p.portalClearHeight = portal ? kPortalHeight : 0.0f;
    Push(p, westIn, frontIn, 0.0f, eastIn, backIn, kFloorTop, kColourFloor, kDecorFloor, 0);
    const float outFront = room.minY - kWallHalf, outBack = room.maxY + kWallHalf;
    const float skirtTop = kFloorTop + kSkirtHeight;
    for (int side = -1; side <= 1; side += 2) {
        const float x0 = side < 0 ? room.minX - kWallHalf : eastIn, x1 = side < 0 ? westIn : room.maxX + kWallHalf;
        const float wallIn = side < 0 ? westIn : eastIn;
        const float s0 = side < 0 ? wallIn : wallIn - kSkirtDeep, s1 = side < 0 ? wallIn + kSkirtDeep : wallIn;
        const float px0 = side < 0 ? wallIn : wallIn - kPierDeep, px1 = side < 0 ? wallIn + kPierDeep : wallIn;
        // What interrupts this wall's skirting: the portal with its jambs, and piers.
        float cut[kMaxCuts][2];
        unsigned cuts = 0;
        if (room.portalSide == side) {
            Push(p, x0, outFront, 0.0f, x1, pMin, wallTop, kColourWall, kSolidWall, 0);
            Push(p, x0, pMax, 0.0f, x1, outBack, wallTop, kColourWall, kSolidWall, 0);
            Push(p, x0, pMin, 0.0f, x1, pMax, kFloorTop, kColourFloor, kDecorFloor, 0);
            Push(p, x0, pMin, kPortalHeight, x1, pMax, wallTop, kColourWall, kSolidHeader, 0);
            const float t0 = side < 0 ? wallIn : wallIn - kTrimDeep, t1 = side < 0 ? wallIn + kTrimDeep : wallIn;
            Push(p, t0, pMin, kPortalHeight, t1, pMax, kPortalHeight + 0.22f, kColourAccent, kDecorTrim, 0);
            const float j0 = Max(pMin - 2.0f * kPierHalf, frontIn + kSkirtDeep);
            const float j1 = Min(pMax + 2.0f * kPierHalf, backIn - kSkirtDeep);
            cut[0][0] = pMin; cut[0][1] = pMax; cuts = 1;
            if (pMin - j0 >= 0.15f) {
                Push(p, px0, j0, kFloorTop, px1, pMin, wallTop, kColourPier, kSolidPier, 0);
                cut[0][0] = j0;
            }
            if (j1 - pMax >= 0.15f) {
                Push(p, px0, pMax, kFloorTop, px1, j1, wallTop, kColourPier, kSolidPier, 0);
                cut[0][1] = j1;
            }
        } else {
            Push(p, x0, outFront, 0.0f, x1, outBack, wallTop, kColourWall, kSolidWall, 0);
        }

        // This wall's bays in ascending order along it.
        unsigned order[kMaxBays], n = 0;
        for (unsigned i = 0; i < count; ++i)
            if (p.bays[i].side == side) order[n++] = i;
        if (side > 0)
            for (unsigned a = 0, z = n; a + 1 < z; ++a) {
                --z;
                const unsigned swap = order[a]; order[a] = order[z]; order[z] = swap;
            }

        // Supports: a pier against the wall between neighbouring bays and at each end
        // of a row, so every machine has its own framed bay. One is left out where
        // the portal's frame, the room's corner or a host reservation already stands.
        auto pier = [&](float c) {
            const float a = c - kPierHalf, b = c + kPierHalf;
            if (!(a >= frontIn + kPierKeep && b <= backIn - kPierKeep) || cuts >= unsigned(kMaxCuts)) return;
            for (unsigned k = 0; k < cuts; ++k) {
                const float clear = room.portalSide == side && k == 0 ? kJambClear : 0.05f;
                if (a < cut[k][1] + clear && b > cut[k][0] - clear) return;
            }
            for (unsigned k = 0; k < use.keepCount; ++k)
                if (px0 < kept[k].maxX && px1 > kept[k].minX && a < kept[k].maxY && b > kept[k].minY) return;
            Push(p, px0, a, kFloorTop, px1, b, wallTop, kColourPier, kSolidPier, 0);
            cut[cuts][0] = a; cut[cuts][1] = b; ++cuts;
        };
        for (unsigned j = 0; j < n; ++j) {
            const unsigned i = order[j];
            if (j == 0) pier(py0[i] - kPierLead);
            if (j + 1 < n) {
                const unsigned before = cuts;
                pier(0.5f * (py1[i] + py0[order[j + 1]]));
                if (cuts == before) { pier(py1[i] + kPierLead); pier(py0[order[j + 1]] - kPierLead); }
            } else {
                pier(py1[i] + kPierLead);
            }
        }

        // Barrier: where two plinths stand close, a low rail closes the pocket between
        // them. It runs in line with the front of the plinth nearer the wall, from one
        // plinth's side to the other's, and stops at the stand where the stand is in line.
        for (unsigned j = 0; j + 1 < n; ++j) {
            const unsigned low = order[j], up = order[j + 1];
            if (!(py0[up] - py1[low] <= kPocketClose)) continue;
            const float fa = p.bays[low].zoneEnd.x, fb = p.bays[up].zoneEnd.x;
            const float line = side > 0 ? Max(fa, fb) : Min(fa, fb);
            const float rx0 = side > 0 ? line : line - kRailDeep, rx1 = side > 0 ? line + kRailDeep : line;
            const unsigned owner = side > 0 ? up : low;      // whose stand is in this gap
            const float sx = p.bays[owner].stand.x;
            float a = py1[low], b = py0[up];
            if (sx - kStandHalfDeep < rx1 && rx0 < sx + kStandHalfDeep) {
                if (side > 0) b = sy0[owner];
                else a = sy1[owner];
            }
            bool reserved = false;
            for (unsigned k = 0; k < use.keepCount; ++k)
                reserved = reserved || (rx0 < kept[k].maxX && rx1 > kept[k].minX && a < kept[k].maxY && b > kept[k].minY);
            if (reserved || b - a < 0.05f) continue;
            Push(p, rx0, a, kFloorTop, rx1, b, kFloorTop + kRailHeight, kColourRail, kSolidRail, 0);
        }

        // Skirting along the wall, between whatever interrupts it.
        for (unsigned a = 1; a < cuts; ++a)
            for (unsigned b = a; b > 0 && cut[b][0] < cut[b - 1][0]; --b) {
                const float c0 = cut[b][0], c1 = cut[b][1];
                cut[b][0] = cut[b - 1][0]; cut[b][1] = cut[b - 1][1];
                cut[b - 1][0] = c0; cut[b - 1][1] = c1;
            }
        float from = frontIn;
        for (unsigned k = 0; k < cuts; ++k) {
            if (cut[k][0] - from > 0.01f) Push(p, s0, from, kFloorTop, s1, cut[k][0], skirtTop, kColourSkirt, kDecorTrim, 0);
            from = Max(from, cut[k][1]);
        }
        if (backIn - from > 0.01f) Push(p, s0, from, kFloorTop, s1, backIn, skirtTop, kColourSkirt, kDecorTrim, 0);
    }
    Push(p, westIn, outFront, 0.0f, eastIn, frontIn, wallTop, kColourWall, kSolidWall, 0);
    Push(p, westIn, backIn, 0.0f, eastIn, outBack, wallTop, kColourWall, kSolidWall, 0);
    Push(p, westIn + kSkirtDeep, backIn - kSkirtDeep, kFloorTop, eastIn - kSkirtDeep, backIn, skirtTop, kColourSkirt, kDecorTrim, 0);
    Push(p, westIn + 2.0f, backIn - kSkirtDeep, 4.2f, eastIn - 2.0f, backIn, 5.6f, kColourBand, kDecorTrim, 0);
    Push(p, westIn + kSkirtDeep, frontIn, kFloorTop, ex - kDoorHalf, frontIn + kSkirtDeep, skirtTop, kColourSkirt, kDecorTrim, 0);
    Push(p, ex + kDoorHalf, frontIn, kFloorTop, eastIn - kSkirtDeep, frontIn + kSkirtDeep, skirtTop, kColourSkirt, kDecorTrim, 0);
    Push(p, ex - kDoorHalf, frontIn, kFloorTop, ex + kDoorHalf, frontIn + kDoorDeep, kDoorHeight, kColourDoor, kSolidDoor, 0);
    Push(p, ex - kDoorHalf, frontIn, kDoorHeight, ex + kDoorHalf, frontIn + kDoorDeep, kDoorHeight + 0.22f, kColourAccent, kDecorTrim, 0);
    p.doorCentre = Vec2{ex, frontIn + kDoorDeep};
    p.doorHalf = kDoorHalf;

    // The walking surface, one tone throughout and each part meeting the next edge
    // to edge: the arrival panel inside the door, the runner up the nave and, for a
    // portal, a branch from the runner to the forecourt in front of it.
    const float doorFace = frontIn + kDoorDeep, arrivalTop = ey + kEntrancePadHalf;
    const float runnerEnd = Min(spineEnd + kNaveHalf, backIn - 2.0f * kSkirtDeep);
    Push(p, ex - kDoorHalf, doorFace, kFloorTop, ex + kDoorHalf, arrivalTop, kInlayTop, kColourEntrance, kDecorInlay, 0);
    Push(p, ex - kNaveHalf, arrivalTop, kFloorTop, ex + kNaveHalf, runnerEnd, kInlayTop, kColourRunner, kDecorInlay, 0);
    Route(p, room.entrance, Vec2{ex, doorFace + kVisitorRadius + 0.1f}, kRouteReturn, 0);
    Route(p, room.entrance, Vec2{ex, spineEnd}, kRouteSpine, 0);
    auto keepClear = [&p](float x0, float y0, float x1, float y1, ZoneKind kind, int bay) {
        if (p.zoneCount >= unsigned(kMaxZones) || !(x1 > x0 && y1 > y0)) { p.reason = kReasonInternal; return; }
        Zone &z = p.zones[p.zoneCount++];
        z.min = Vec2{x0, y0}; z.max = Vec2{x1, y1}; z.kind = kind; z.bay = bay;
    };
    keepClear(ex - kAisleHalf, doorFace, ex + kAisleHalf, ey + kBayStart, kZoneArrival, 0);
    keepClear(ex - kAisleHalf, ey + kBayStart, ex + kAisleHalf, backIn - kBackKeep, kZoneNave, 0);
    if (portal) {
        const int side = room.portalSide;
        const float wallIn = side > 0 ? eastIn : westIn, wallOut = side > 0 ? room.maxX + kWallHalf : room.minX - kWallHalf;
        const float f0 = side > 0 ? wallIn - kForecourtDepth : wallIn, f1 = side > 0 ? wallIn : wallIn + kForecourtDepth;
        Push(p, f0, pMin, kFloorTop, f1, pMax, kInlayTop, kColourForecourt, kDecorInlay, 0);
        // The branch leaves the runner's edge, or the arrival panel's where the portal
        // is level with the panel, and ends at the forecourt's.
        const float reach = Min(kNaveHalf, 0.5f * (pMax - pMin));
        const float xa = ex + float(side) * (pMid - reach < arrivalTop ? kDoorHalf : kNaveHalf);
        const float xb = side > 0 ? f0 : f1;
        Push(p, Min(xa, xb), pMid - reach, kFloorTop, Max(xa, xb), pMid + reach, kInlayTop, kColourRunner, kDecorInlay, 0);
        p.forecourt = Vec2{wallIn - float(side) * 0.5f * kForecourtDepth, pMid};
        Route(p, Vec2{ex, pMid}, Vec2{side > 0 ? room.maxX : room.minX, pMid}, kRouteCinema, 0);
        const float edge = ex + float(side) * kAisleHalf;
        keepClear(Min(edge, wallIn), pMin, Max(edge, wallIn), pMax, kZoneApproach, 0);
        keepClear(Min(wallIn, wallOut), pMin, Max(wallIn, wallOut), pMax, kZoneThroat, 0);
    }

    // Each bay: the plinth; the viewing pad with a toe line on the machine's side; a
    // short tick where the bay's approach leaves the runner; and the numbered stand,
    // built against the plinth's side, with its cap.
    for (unsigned i = 0; i < count; ++i) {
        const Bay &b = p.bays[i];
        const float h = b.plinthHalf, side = float(b.side);
        Push(p, b.model.x - h, py0[i], kFloorTop, b.model.x + h, py1[i], b.model.z, kColourPlinth, kSolidPlinth, b.number);
        const float inner = b.pad.x - side * kPadHalf, outer = b.pad.x + side * kPadHalf;
        const float toe = b.pad.x + side * (kPadHalf - kToeDeep);
        Push(p, Min(inner, toe), b.pad.y - kPadHalf, kFloorTop, Max(inner, toe), b.pad.y + kPadHalf, kInlayTop, kColourPad, kDecorInlay, b.number);
        Push(p, Min(toe, outer), b.pad.y - kPadHalf, kFloorTop, Max(toe, outer), b.pad.y + kPadHalf, kInlayTop, kColourAccent, kDecorInlay, b.number);
        const float tick = Min(kNaveHalf + kTickLong, std::fabs(inner - ex) - 0.02f);
        if (tick - kNaveHalf >= 0.05f) {
            const float xa = ex + side * kNaveHalf, xb = ex + side * tick;
            Push(p, Min(xa, xb), b.pad.y - kLineHalf, kFloorTop, Max(xa, xb), b.pad.y + kLineHalf, kInlayTop, kColourAccent, kDecorInlay, b.number);
        }
        const float standTop = kFloorTop + kStandHeight;
        const float sx0 = b.stand.x - kStandHalfDeep, sx1 = b.stand.x + kStandHalfDeep;
        Push(p, sx0, sy0[i], kFloorTop, sx1, sy1[i], standTop, kColourStand, kSolidStand, b.number);
        Push(p, sx0, sy0[i], standTop, sx1, sy1[i], standTop + 0.04f, kColourAccent, kDecorTrim, b.number);
        Numeral(p, b);
        Route(p, Vec2{ex, b.pad.y}, b.pad, kRouteBay, b.number);
        const float zh = h - kZoneInset;
        keepClear(Min(b.zoneEnd.x, inner), b.model.y - zh, Max(b.zoneEnd.x, inner), b.model.y + zh, kZoneViewing, b.number);
    }
    for (unsigned k = 0; k < use.keepCount; ++k)
        keepClear(kept[k].minX, kept[k].minY, kept[k].maxX, kept[k].maxY, kZoneHost, int(k) + 1);
    if (p.reason == kReasonInternal) return fail(kReasonInternal);

    // The plan checks itself: no two boxes share space, no plinth or stand reaches
    // the aisle, and every pad, line and the way to the portal can be walked.
    for (unsigned i = 0; i < p.boxCount; ++i) {
        const Box &a = p.boxes[i];
        if ((a.purpose == kSolidPlinth || a.purpose == kSolidStand || a.purpose == kSolidPier || a.purpose == kSolidRail) &&
            a.min.x < ex + kAisleHalf && a.max.x > ex - kAisleHalf) return fail(kReasonBlocked);
        for (unsigned j = i + 1; j < p.boxCount; ++j)
            if (Overlap(a, p.boxes[j])) return fail(kReasonBlocked);
    }
    for (unsigned i = 0; i < p.routeCount; ++i)
        if (!PathGeometry(p, p.route[i].a, p.route[i].b, kVisitorRadius)) return fail(kReasonBlocked);
    if (portal && !PathGeometry(p, room.entrance, Vec2{ex, pMid}, kVisitorRadius)) return fail(kReasonBlocked);
    for (unsigned i = 0; i < count; ++i)
        if (WalkGeometry(p, p.bays[i].pad, kVisitorRadius) != kWalkOpen ||
            !PathGeometry(p, p.bays[i].pad, p.bays[i].zoneEnd.x == p.bays[i].pad.x ? p.bays[i].pad :
                          Vec2{p.bays[i].zoneEnd.x - float(p.bays[i].side) * (kVisitorRadius + 0.05f), p.bays[i].pad.y},
                          kVisitorRadius)) return fail(kReasonBlocked);
    // Every zone the plan promises to keep clear holds nothing of its own but floor
    // below head height.
    for (unsigned z = 0; z < p.zoneCount; ++z) {
        const Zone &clear = p.zones[z];
        if (clear.kind == kZoneHost) continue;
        for (unsigned i = 0; i < p.boxCount; ++i) {
            const Box &a = p.boxes[i];
            if (CategoryOf(a.purpose) == kCategoryFloor || a.min.z >= kBodyTop) continue;
            if (a.min.x < clear.max.x - 1.0e-3f && a.max.x > clear.min.x + 1.0e-3f &&
                a.min.y < clear.max.y - 1.0e-3f && a.max.y > clear.min.y + 1.0e-3f) return fail(kReasonBlocked);
        }
    }
    p.valid = true;
    p.reason = kReasonNone;
    return true;
}

// The plan for these displays with no host reservation; no displays is refused.
inline bool MakePlan(const RoomSpec &room, const DisplayInput *displays, unsigned count, Plan *out) {
    const PlanOptions none = {nullptr, 0u, false};
    return MakePlan(room, displays, count, none, out);
}

// The first zone of a kind (and, for a viewing zone, of a bay), or null.
inline const Zone *ZoneFor(const Plan &p, ZoneKind kind, int bay = 0) {
    if (!p.valid) return nullptr;
    for (unsigned i = 0; i < p.zoneCount; ++i)
        if (p.zones[i].kind == kind && p.zones[i].bay == bay) return &p.zones[i];
    return nullptr;
}

// The space a bay's machine is given, as measured: its footprint's bounding
// square, from the plinth's top to the machine's height. Nothing in the plan
// enters it; the plan never moves or resizes what stands in it.
inline void ExhibitEnvelope(const Bay &b, Vec3 *min, Vec3 *max) {
    if (min) *min = Vec3{b.model.x - b.radius, b.model.y - b.radius, b.model.z};
    if (max) *max = Vec3{b.model.x + b.radius, b.model.y + b.radius, b.model.z + b.height};
}

namespace detail {
// Whether the straight line from o along d (to o + d) passes through a box,
// ignoring a graze along its surface.
inline bool SegmentMeets(const Box &box, const float o[3], const float d[3]) {
    const float lo[3] = {box.min.x, box.min.y, box.min.z}, hi[3] = {box.max.x, box.max.y, box.max.z};
    float t0 = 0.0f, t1 = 1.0f;
    for (unsigned a = 0; a < 3; ++a) {
        const float l = lo[a] + 1.0e-3f, h = hi[a] - 1.0e-3f;
        if (!(h > l)) return false;
        if (std::fabs(d[a]) < 1.0e-9f) {
            if (o[a] < l || o[a] > h) return false;
            continue;
        }
        float ta = (l - o[a]) / d[a], tb = (h - o[a]) / d[a];
        if (ta > tb) { const float s = ta; ta = tb; tb = s; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return false;
    }
    return true;
}
} // namespace detail

// Whether the plan puts nothing between two points in space: every piece that
// rises above the floor counts, solid or trim, so this is what an eye sees past
// the plan's own geometry. It knows nothing of machines or of the host's pieces.
inline bool SightClear(const Plan &p, Vec3 from, Vec3 to) {
    if (!p.valid) return false;
    const float o[3] = {from.x, from.y, from.z}, d[3] = {to.x - from.x, to.y - from.y, to.z - from.z};
    for (unsigned a = 0; a < 3; ++a)
        if (!std::isfinite(o[a]) || !std::isfinite(d[a])) return false;
    for (unsigned i = 0; i < p.boxCount; ++i)
        if (CategoryOf(p.boxes[i].purpose) != kCategoryFloor && detail::SegmentMeets(p.boxes[i], o, d)) return false;
    return true;
}

// Whether an eye sees a bay's numbered face: 0 when it is in clear view from in
// front, -1 when it is not. *cosine receives how square-on it is seen (1 is
// square-on), or 0 when it is not seen.
inline int LabelFaceSeen(const Plan &p, const Bay &b, Vec3 eye, float *cosine) {
    const Vec3 c = b.labelFace;
    const Vec2 n = b.labelNormal;
    const float dx = eye.x - c.x, dy = eye.y - c.y, d = std::sqrt(dx * dx + dy * dy);
    float seen = std::isfinite(d) && d > 1.0e-4f ? (dx * n.x + dy * n.y) / d : 0.0f;
    if (!(seen > 0.0f) || !SightClear(p, eye, Vec3{c.x + n.x * kLabelStandOff, c.y + n.y * kLabelStandOff, c.z}))
        seen = 0.0f;
    if (cosine) *cosine = seen;
    return seen > 0.0f ? 0 : -1;
}

// Whether a visitor of this radius may stand at v. Authoritative for the room and
// the portal's throat; past the wall it answers kWalkOutside and the caller asks
// the room beyond. An invalid plan blocks everything, so a caller must not use
// one for collision.
inline Walk WalkAt(const Plan &p, Vec2 v, float radius = kVisitorRadius) {
    return p.valid ? detail::WalkGeometry(p, v, radius) : kWalkBlocked;
}
// Whether the straight walk from a to b meets nothing solid.
inline bool PathClear(const Plan &p, Vec2 a, Vec2 b, float radius = kVisitorRadius) {
    return p.valid && detail::PathGeometry(p, a, b, radius);
}

inline const Bay *BayByNumber(const Plan &p, int number) {
    return p.valid && number >= 1 && unsigned(number) <= p.bayCount ? &p.bays[number - 1] : nullptr;
}
// The card identity that goes with a stand's numeral, or -1.
inline int LabelForBay(const Plan &p, int number) {
    const Bay *b = BayByNumber(p, number);
    return b ? b->stableLabel : -1;
}
// How far v is from the bay's viewing zone: the strip of floor from the middle of
// its pad to the foot of its plinth.
inline float ZoneDistance(const Bay &b, Vec2 v) { return detail::SegmentDistance(v, b.pad, b.zoneEnd); }

// The number of the bay whose viewing zone the visitor stands in, or 0. previous
// is the last answer: a visitor on the seam between two zones keeps the bay held.
inline int StandingBay(const Plan &p, Vec2 visitor, int previous) {
    if (!p.valid || !std::isfinite(visitor.x) || !std::isfinite(visitor.y)) return 0;
    int best = 0;
    float bestDistance = 0.0f;
    for (unsigned i = 0; i < p.bayCount; ++i) {
        const float d = ZoneDistance(p.bays[i], visitor);
        if (d <= kZoneEnter && (!best || d < bestDistance)) { best = p.bays[i].number; bestDistance = d; }
    }
    if (const Bay *old = BayByNumber(p, previous)) {
        const float d = ZoneDistance(*old, visitor);
        if (d <= kZoneKeep && (!best || d <= bestDistance + 0.4f)) return previous;
    }
    return best;
}

// Whether the heading points at the bay's machine: within a small angle of its
// outline as seen from the visitor, a wider one while its card is already shown.
inline bool FacesBay(const Bay &b, Vec2 visitor, Vec2 heading, bool held) {
    const float dx = b.model.x - visitor.x, dy = b.model.y - visitor.y;
    const float d = std::sqrt(dx * dx + dy * dy), h = std::sqrt(heading.x * heading.x + heading.y * heading.y);
    if (!std::isfinite(d) || !std::isfinite(h) || !(h > 1.0e-6f)) return false;
    if (d <= b.radius) return true;
    const float off = std::atan2(std::fabs(dx * heading.y - dy * heading.x), dx * heading.x + dy * heading.y);
    return off <= std::asin(b.radius / d) + (held ? kFaceKeep : kFaceEnter);
}

// The number of the bay whose card to show, or 0: the visitor stands in that
// bay's own zone and faces its machine. A machine in another bay, beside, behind
// or across the aisle, is never named from here. previous is the last answer and
// is kept through small turns and steps; turning to the aisle or away clears it.
inline int FocusBay(const Plan &p, Vec2 visitor, Vec2 heading, int previous) {
    if (!p.valid || !std::isfinite(visitor.x) || !std::isfinite(visitor.y)) return 0;
    if (const Bay *old = BayByNumber(p, previous))
        if (ZoneDistance(*old, visitor) <= kZoneKeep && FacesBay(*old, visitor, heading, true)) return previous;
    int best = 0;
    float bestDistance = 0.0f;
    for (unsigned i = 0; i < p.bayCount; ++i) {
        const Bay &b = p.bays[i];
        const float d = ZoneDistance(b, visitor);
        if (d <= kZoneEnter && (!best || d < bestDistance) && FacesBay(b, visitor, heading, false)) {
            best = b.number; bestDistance = d;
        }
    }
    return best;
}

// Whether to offer the way out: the visitor is in the vestibule between the
// entrance pad and the return door, and the heading meets the door itself.
// held is whether the cue is already shown.
inline bool FacesExit(const Plan &p, Vec2 visitor, Vec2 heading, bool held) {
    if (!p.valid) return false;
    const float h = std::sqrt(heading.x * heading.x + heading.y * heading.y);
    if (!std::isfinite(h) || !(h > 1.0e-6f)) return false;
    const float side = visitor.x - p.room.entrance.x, gap = visitor.y - p.doorCentre.y;
    if (!(std::fabs(side) <= (held ? 3.4f : 3.0f) && gap >= 0.0f &&
          visitor.y <= p.room.entrance.y + (held ? 2.8f : 2.0f))) return false;
    const float out = -heading.y / h, across = heading.x / h;
    if (!(out >= (held ? 0.4f : 0.5f))) return false;
    return std::fabs(side + gap * across / out) <= p.doorHalf + (held ? 0.15f : -0.10f);
}

// Where to look at a bay from, as data: the caller decides whether anything uses
// it. pitch is in radians, positive upward, toward the machine's middle.
struct ViewPose { Vec3 eye; Vec2 heading; float pitch; };
inline ViewPose RecommendedPose(const Bay &b, float eyeHeight) {
    ViewPose pose;
    pose.eye = Vec3{b.pad.x, b.pad.y, eyeHeight};
    pose.heading = b.facing;
    pose.pitch = std::atan2(b.lookAt.z - eyeHeight, b.viewDistance);
    return pose;
}

// A box as flat-shaded triangles: the top in the box's colour, then the minY,
// maxX, maxY and minX sides a little darker so its form reads. No underside.
// Winding is not uniform; draw without culling. out holds kBoxVertices.
struct Vertex { float x, y, z; unsigned colour; };
enum { kBoxVertices = 30 };
inline unsigned Shade(unsigned colour, unsigned percent) {
    const unsigned r = ((colour >> 16) & 255u) * percent / 100u, g = ((colour >> 8) & 255u) * percent / 100u;
    const unsigned b = (colour & 255u) * percent / 100u;
    return (colour & 0xff000000u) | (r << 16) | (g << 8) | b;
}
inline unsigned EmitBox(const Box &box, Vertex *out) {
    if (!out) return 0;
    const Vec3 &a = box.min, &b = box.max;
    unsigned n = 0;
    auto quad = [&](Vec3 p, Vec3 q, Vec3 r, Vec3 s, unsigned colour) {
        const Vec3 order[6] = {p, q, r, p, r, s};
        for (unsigned i = 0; i < 6; ++i) out[n++] = Vertex{order[i].x, order[i].y, order[i].z, colour};
    };
    const unsigned side = Shade(box.colour, 90), end = Shade(box.colour, 80);
    quad(Vec3{a.x, a.y, b.z}, Vec3{b.x, a.y, b.z}, Vec3{b.x, b.y, b.z}, Vec3{a.x, b.y, b.z}, box.colour);
    quad(Vec3{a.x, a.y, a.z}, Vec3{b.x, a.y, a.z}, Vec3{b.x, a.y, b.z}, Vec3{a.x, a.y, b.z}, side);
    quad(Vec3{b.x, a.y, a.z}, Vec3{b.x, b.y, a.z}, Vec3{b.x, b.y, b.z}, Vec3{b.x, a.y, b.z}, end);
    quad(Vec3{b.x, b.y, a.z}, Vec3{a.x, b.y, a.z}, Vec3{a.x, b.y, b.z}, Vec3{b.x, b.y, b.z}, side);
    quad(Vec3{a.x, b.y, a.z}, Vec3{a.x, a.y, a.z}, Vec3{a.x, a.y, b.z}, Vec3{a.x, b.y, b.z}, end);
    return n;
}

} // namespace CeMuseumExhibition
#endif
