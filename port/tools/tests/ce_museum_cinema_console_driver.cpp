// SPDX-License-Identifier: GPL-3.0-only
// cinema_console_driver.cpp: standalone checks of ce_museum_cinema_console against
// ce_museum_cinema_controls. C++11, std only. Build it with the console's .cpp;
// it prints each test's name and the counts, and returns nonzero if any check
// failed. A failed check prints its test's name and this file's line number.
#include "ce_museum_cinema_console.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

namespace Console = CeMuseumCinemaConsole;
namespace Controls = CeMuseumCinemaControls;
typedef Console::Vec3 Vec3;
typedef Controls::Region Region;

int g_checks = 0;
int g_failures = 0;
const char *g_name = "";

void Check(bool ok, int line) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("  failed check: %s, line %d\n", g_name, line);
}
#define CHECK(condition) Check((condition), __LINE__)

const Region kOrder[3] = {Controls::kRegionPrevious, Controls::kRegionPlay, Controls::kRegionNext};

// ---------------------------------------------------------------------------
// Vectors, mounts and views

Vec3 V(float x, float y, float z) {
    Vec3 v;
    v.x = x; v.y = y; v.z = z;
    return v;
}
Vec3 Add(const Vec3 &a, const Vec3 &b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
Vec3 Sub(const Vec3 &a, const Vec3 &b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
Vec3 Mul(const Vec3 &a, float s) { return V(a.x * s, a.y * s, a.z * s); }
float Dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(const Vec3 &a, const Vec3 &b) {
    return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
float Length(const Vec3 &a) { return std::sqrt(Dot(a, a)); }
bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
bool FiniteVec(const Vec3 &a) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

// At the origin, square to the axes.
Console::Mount Plain() {
    return Console::MakeMount(V(0.0f, 0.0f, 0.0f), V(1.0f, 0.0f, 0.0f), V(0.0f, 1.0f, 0.0f),
                              V(0.0f, 0.0f, 1.0f));
}
// A world whose Z grows downward: up is -Z. Moved well away from the origin.
Console::Mount NativeDown() {
    return Console::MakeMount(V(310.0f, -72.5f, -4.25f), V(1.0f, 0.0f, 0.0f),
                              V(0.0f, 0.0f, -1.0f), V(0.0f, 1.0f, 0.0f));
}
// The same world with the panel facing the other way: the mirror-image basis.
Console::Mount NativeDownMirrored() {
    return Console::MakeMount(V(310.0f, -72.5f, -4.25f), V(1.0f, 0.0f, 0.0f),
                              V(0.0f, 0.0f, -1.0f), V(0.0f, -1.0f, 0.0f));
}
// Turned, leaned back like a desk, moved, and a different size.
Console::Mount Tilted() {
    const float turn = 0.65f, lean = 0.48f;
    const Vec3 right = V(std::cos(turn), std::sin(turn), 0.0f);
    const Vec3 up0 = V(0.0f, 0.0f, 1.0f);
    const Vec3 front0 = V(std::sin(turn), -std::cos(turn), 0.0f);
    const Vec3 up = Add(Mul(up0, std::cos(lean)), Mul(front0, std::sin(lean)));
    const Vec3 front = Sub(Mul(front0, std::cos(lean)), Mul(up0, std::sin(lean)));
    Console::Mount m = Console::MakeMount(V(-118.5f, 46.25f, 9.5f), right, up, front);
    m.halfWidth = 2.4f;
    m.halfHeight = 0.9f;
    m.reach = 14.0f;
    return m;
}

const unsigned kMountCount = 4u;
Console::Mount MountNumber(unsigned i) {
    return i == 0u ? Plain() : i == 1u ? NativeDown() : i == 2u ? NativeDownMirrored() : Tilted();
}

Vec3 EyeOf(const Console::Mount &m, float ahead, float side, float lift) {
    return Add(m.centre, Add(Mul(m.front, ahead), Add(Mul(m.right, side), Mul(m.up, lift))));
}
Vec3 PanelAt(const Console::Mount &m, float u, float v) {
    Vec3 point = V(0.0f, 0.0f, 0.0f);
    CHECK(Console::PanelPoint(m, u, v, &point));
    return point;
}
Vec3 CentreOf(const Console::Mount &m, Region region) {
    Vec3 point = V(0.0f, 0.0f, 0.0f);
    CHECK(Console::ControlCentre(m, region, &point));
    return point;
}
Console::View ViewAlong(const Vec3 &eye, const Vec3 &aim) {
    Console::View view;
    view.origin = eye;
    view.aim = aim;
    view.museumActive = true;
    view.inRoom = true;
    view.focused = true;
    view.paused = false;
    view.photo = false;
    return view;
}
Console::View ViewToward(const Vec3 &eye, const Vec3 &target) {
    return ViewAlong(eye, Sub(target, eye));
}
Console::Hit PickToward(const Console::Mount &m, const Vec3 &eye, const Vec3 &target) {
    return Console::Pick(m, ViewToward(eye, target));
}

// ---------------------------------------------------------------------------
// Mounts

void TestMountValidation() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const Console::Mount good = Plain();
    CHECK(Console::ValidMount(good));
    CHECK(Console::Chirality(good) == 1);
    CHECK(Console::ValidMount(NativeDown()));
    CHECK(Console::Chirality(NativeDown()) == 1);
    CHECK(Console::ValidMount(NativeDownMirrored()));
    CHECK(Console::Chirality(NativeDownMirrored()) == -1);
    CHECK(Console::ValidMount(Tilted()));
    CHECK(Console::Chirality(Tilted()) == 1);
    CHECK(Console::NearLimit(good) > 0.0f);
    CHECK(Console::NearLimit(good) < good.reach);

    Console::Mount bad = good;
    bad.centre.x = nan;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.centre.y = inf;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.centre.z = 5.0e6f;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.right = V(0.0f, 0.0f, 0.0f);
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.up = V(0.0f, 2.0f, 0.0f);
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.front = V(0.0f, 0.0f, nan);
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.front = V(0.0f, 0.0f, inf);
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.right = V(0.8f, 0.6f, 0.0f);       // unit, but leaning into up
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.front = bad.right;                 // collapsed onto one axis
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.halfWidth = 0.0f;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.halfWidth = -1.0f;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.halfWidth = nan;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.halfWidth = inf;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.halfHeight = 0.0f;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.halfHeight = nan;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.reach = 0.0f;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.reach = -3.0f;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.reach = nan;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.reach = inf;
    CHECK(!Console::ValidMount(bad));
    bad = good; bad.reach = 0.1f * good.halfHeight;    // no room to stand in front
    CHECK(!Console::ValidMount(bad));

    // A refused mount is refused by everything that takes one.
    bad = good; bad.up = V(0.0f, 2.0f, 0.0f);
    const Console::Hit hit = Console::Pick(bad, ViewAlong(V(0.0f, 0.0f, 3.0f), V(0.0f, 0.0f, -1.0f)));
    CHECK(hit.status == Console::kPickBadMount);
    CHECK(!hit.inRange && !hit.atConsole && !hit.onPanel && !hit.meetsPlane);
    CHECK(hit.region == Controls::kRegionNone);
    Console::Geometry geometry;
    CHECK(!Console::Build(bad, Console::IdleDisplay(), &geometry));
    CHECK(geometry.faceCount == 0u);
    CHECK(!Console::Build(good, Console::IdleDisplay(), 0));
    Console::Extent extent;
    Vec3 point;
    CHECK(!Console::PanelExtent(bad, &extent));
    CHECK(!Console::ControlExtent(bad, Controls::kRegionPlay, &extent));
    CHECK(!Console::PanelPoint(bad, 0.0f, 0.0f, &point));
    CHECK(!Console::ControlCentre(bad, Controls::kRegionPlay, &point));
    CHECK(Console::Chirality(bad) == 0);
    CHECK(!Console::ControlExtent(good, Controls::kRegionNone, &extent));
    CHECK(!Console::ControlCentre(good, Controls::kRegionNone, &point));
    CHECK(!Console::PanelPoint(good, nan, 0.0f, &point));

    // Axes a hair off, as trigonometry in floats leaves them, are accepted.
    Console::Mount drift = good;
    drift.right = V(1.0004f, 0.0003f, 0.0f);
    CHECK(Console::ValidMount(drift));
}

void TestLayoutMatchesController() {
    for (unsigned n = 0; n < kMountCount; ++n) {
        const Console::Mount m = MountNumber(n);
        Console::Extent panel, e[3];
        CHECK(Console::PanelExtent(m, &panel));
        CHECK(Near(panel.uMin, -m.halfWidth, 1.0e-5f) && Near(panel.uMax, m.halfWidth, 1.0e-5f));
        CHECK(Near(panel.vMin, -m.halfHeight, 1.0e-5f) && Near(panel.vMax, m.halfHeight, 1.0e-5f));
        const Controls::Console flat = {0.0f, 0.0f, 0.0f, 1.0f, m.halfWidth, m.reach};
        const float tolerance = 1.0e-4f * m.halfWidth;
        for (unsigned i = 0; i < 3u; ++i) {
            CHECK(Console::ControlExtent(m, kOrder[i], &e[i]));
            // Across the panel: where the selector's own floor-plan helpers put it.
            CHECK(Near(0.5f * (e[i].uMin + e[i].uMax), Controls::ControlCentre(flat, kOrder[i]), tolerance));
            CHECK(Near(0.5f * (e[i].uMax - e[i].uMin), Controls::ControlHalf(flat), tolerance));
            // Inside the panel with a margin on every side.
            CHECK(e[i].uMin > panel.uMin && e[i].uMax < panel.uMax);
            CHECK(e[i].vMin > panel.vMin && e[i].vMax < panel.vMax);
            CHECK(e[i].vMax > e[i].vMin);
        }
        CHECK(Near(e[0].vMin, e[1].vMin, tolerance) && Near(e[1].vMin, e[2].vMin, tolerance));
        CHECK(Near(e[0].vMax, e[1].vMax, tolerance) && Near(e[1].vMax, e[2].vMax, tolerance));
        // Left to right, with a real gutter between each pair.
        CHECK(e[1].uMin - e[0].uMax > 0.05f * m.halfWidth);
        CHECK(e[2].uMin - e[1].uMax > 0.05f * m.halfWidth);
    }
}

// ---------------------------------------------------------------------------
// Faces

const Console::Face *PickFace(const Console::Geometry &g, Region region) {
    for (unsigned i = 0; i < g.faceCount && i < Console::kMaxFaces; ++i)
        if (g.faces[i].pickable && g.faces[i].region == region) return &g.faces[i];
    return 0;
}

unsigned CountMaterial(const Console::Geometry &g, Console::Material material) {
    unsigned count = 0;
    for (unsigned i = 0; i < g.faceCount && i < Console::kMaxFaces; ++i)
        if (g.faces[i].material == material) ++count;
    return count;
}

// Every face is sound, inside the console's shallow box and wound to its normal;
// exactly three are pickable, in order, and they are the control rectangles.
void CheckFaces(const Console::Mount &m, const Console::Geometry &g) {
    const float tolerance = 2.0e-3f * m.halfWidth;
    const int chirality = Console::Chirality(m);
    CHECK(g.faceCount > 0u && g.faceCount <= Console::kMaxFaces);
    unsigned found = 0;
    for (unsigned i = 0; i < g.faceCount && i < Console::kMaxFaces; ++i) {
        const Console::Face &face = g.faces[i];
        CHECK(face.vertexCount == 3u || face.vertexCount == 4u);
        for (unsigned k = 0; k < 4u; ++k) {
            CHECK(FiniteVec(face.vertices[k]));
            const Vec3 rel = Sub(face.vertices[k], m.centre);
            CHECK(std::fabs(Dot(rel, m.right)) <= m.halfWidth + tolerance);
            CHECK(std::fabs(Dot(rel, m.up)) <= m.halfHeight + tolerance);
            CHECK(Dot(rel, m.front) >= -m.halfHeight - tolerance);
            CHECK(Dot(rel, m.front) <= 0.1f * m.halfHeight + tolerance);
        }
        const Vec3 wound = Cross(Sub(face.vertices[1], face.vertices[0]),
                                 Sub(face.vertices[2], face.vertices[0]));
        CHECK(Dot(wound, face.normal) * static_cast<float>(chirality) > 0.0f);
        CHECK(Near(Length(face.normal), 1.0f, 1.0e-3f));
        if (!face.pickable) {
            CHECK(face.region == Controls::kRegionNone);
            continue;
        }
        CHECK(found < 3u);
        if (found >= 3u) continue;
        CHECK(face.region == kOrder[found]);
        CHECK(face.vertexCount == 4u);
        CHECK(Dot(face.normal, m.front) > 0.999f);
        Console::Extent e;
        CHECK(Console::ControlExtent(m, face.region, &e));
        const Vec3 expected[4] = {PanelAt(m, e.uMin, e.vMin), PanelAt(m, e.uMax, e.vMin),
                                  PanelAt(m, e.uMax, e.vMax), PanelAt(m, e.uMin, e.vMax)};
        for (unsigned k = 0; k < 4u; ++k)
            CHECK(Length(Sub(face.vertices[k], expected[k])) <= tolerance);
        ++found;
    }
    CHECK(found == 3u);
}

void TestFacesArePickedWhereDrawn() {
    for (unsigned n = 0; n < kMountCount; ++n) {
        const Console::Mount m = MountNumber(n);
        const float tolerance = 2.0e-3f * m.halfWidth;
        Console::Geometry g;
        CHECK(Console::Build(m, Console::IdleDisplay(), &g));
        CheckFaces(m, g);
        const Vec3 eyes[3] = {EyeOf(m, 0.5f * m.reach, 0.0f, 0.0f),
                              EyeOf(m, 0.3f * m.reach, 0.2f * m.reach, 0.1f * m.reach),
                              EyeOf(m, 0.35f * m.reach, -0.25f * m.reach, -0.05f * m.reach)};
        for (unsigned i = 0; i < 3u; ++i) {
            const Console::Face *face = PickFace(g, kOrder[i]);
            CHECK(face != 0);
            if (!face) continue;
            const Vec3 centre = Mul(Add(Add(face->vertices[0], face->vertices[1]),
                                        Add(face->vertices[2], face->vertices[3])), 0.25f);
            for (unsigned e = 0; e < 3u; ++e) {
                const Console::Hit hit = PickToward(m, eyes[e], centre);
                CHECK(hit.status == Console::kPickControl);
                CHECK(hit.region == kOrder[i]);
                CHECK(hit.inRange && hit.atConsole && hit.onPanel && hit.meetsPlane);
                CHECK(Length(Sub(hit.point, centre)) <= tolerance);
                CHECK(Near(hit.distance, Length(Sub(centre, eyes[e])), tolerance));
                for (unsigned k = 0; k < 4u; ++k) {
                    // Just inside each drawn corner is the control; just outside is not.
                    const Vec3 spoke = Sub(face->vertices[k], centre);
                    const Console::Hit inside = PickToward(m, eyes[e], Add(centre, Mul(spoke, 0.96f)));
                    CHECK(inside.region == kOrder[i]);
                    const Console::Hit outside = PickToward(m, eyes[e], Add(centre, Mul(spoke, 1.04f)));
                    CHECK(outside.region == Controls::kRegionNone);
                    CHECK(outside.atConsole && outside.onPanel);
                    CHECK(outside.status == Console::kPickPanel);
                }
            }
        }
        // The middle of each gutter is on the panel and on no control.
        Console::Extent e[3];
        for (unsigned i = 0; i < 3u; ++i) CHECK(Console::ControlExtent(m, kOrder[i], &e[i]));
        const float v = 0.5f * (e[0].vMin + e[0].vMax);
        for (unsigned i = 0; i < 2u; ++i) {
            const Vec3 gutter = PanelAt(m, 0.5f * (e[i].uMax + e[i + 1u].uMin), v);
            for (unsigned k = 0; k < 3u; ++k) {
                const Console::Hit hit = PickToward(m, eyes[k], gutter);
                CHECK(hit.region == Controls::kRegionNone);
                CHECK(hit.atConsole && hit.onPanel);
            }
        }
    }
}

bool SamePickFaces(const Console::Geometry &a, const Console::Geometry &b) {
    for (unsigned i = 0; i < 3u; ++i) {
        const Console::Face *fa = PickFace(a, kOrder[i]);
        const Console::Face *fb = PickFace(b, kOrder[i]);
        if (!fa || !fb) return false;
        for (unsigned k = 0; k < 4u; ++k)
            if (Length(Sub(fa->vertices[k], fb->vertices[k])) > 1.0e-6f) return false;
    }
    return true;
}

void TestDisplayChangesLooksNotFaces() {
    const Console::Mount m = Tilted();
    Console::Geometry idle, shown;
    CHECK(Console::Build(m, Console::IdleDisplay(), &idle));
    for (unsigned i = 0; i < 3u; ++i) {
        const Console::Face *face = PickFace(idle, kOrder[i]);
        CHECK(face != 0 && face->material == Console::kMaterialKey);
    }
    CHECK(CountMaterial(idle, Console::kMaterialFocusFrame) == 0u);
    CHECK(CountMaterial(idle, Console::kMaterialLampIdle) == 1u);
    CHECK(CountMaterial(idle, Console::kMaterialPlate) == 1u);

    for (unsigned i = 0; i < 3u; ++i) {
        Console::Display d = Console::IdleDisplay();
        d.focus = kOrder[i];
        CHECK(Console::Build(m, d, &shown));
        CheckFaces(m, shown);
        CHECK(shown.faceCount == idle.faceCount + 4u);
        CHECK(CountMaterial(shown, Console::kMaterialFocusFrame) == 4u);
        CHECK(SamePickFaces(idle, shown));
        for (unsigned k = 0; k < 3u; ++k) {
            const Console::Face *face = PickFace(shown, kOrder[k]);
            CHECK(face != 0 && face->material ==
                                   (k == i ? Console::kMaterialKeyFocus : Console::kMaterialKey));
        }
        d.pressed = kOrder[i];
        CHECK(Console::Build(m, d, &shown));
        CHECK(SamePickFaces(idle, shown));
        const Console::Face *held = PickFace(shown, kOrder[i]);
        CHECK(held != 0 && held->material == Console::kMaterialKeyPressed);
    }

    Console::Display d = Console::IdleDisplay();
    d.usable = false;
    CHECK(Console::Build(m, d, &shown));
    CHECK(SamePickFaces(idle, shown));
    CHECK(CountMaterial(shown, Console::kMaterialKeyDisabled) == 3u);

    d = Console::IdleDisplay();
    d.multipleFilms = false;
    CHECK(Console::Build(m, d, &shown));
    CHECK(CountMaterial(shown, Console::kMaterialKeyDisabled) == 2u);
    const Console::Face *play = PickFace(shown, Controls::kRegionPlay);
    CHECK(play != 0 && play->material == Console::kMaterialKey);

    d = Console::IdleDisplay();
    d.showStop = true;
    d.lamp = Console::kLampPlaying;
    CHECK(Console::Build(m, d, &shown));
    CheckFaces(m, shown);
    CHECK(shown.faceCount == idle.faceCount);
    CHECK(SamePickFaces(idle, shown));
    CHECK(CountMaterial(shown, Console::kMaterialLampPlaying) == 1u);
    CHECK(CountMaterial(shown, Console::kMaterialLampIdle) == 0u);
    d.lamp = Console::kLampEnded;
    CHECK(Console::Build(m, d, &shown));
    CHECK(CountMaterial(shown, Console::kMaterialLampEnded) == 1u);
    d.lamp = Console::kLampUnavailable;
    CHECK(Console::Build(m, d, &shown));
    CHECK(CountMaterial(shown, Console::kMaterialLampUnavailable) == 1u);

    // Every material and style has an opaque-enough authored colour.
    CHECK(Console::MaterialColour(Console::kMaterialPlate).a == 255);
    CHECK(Console::MaterialColour(Console::kMaterialLampUnavailable).a == 255);
    CHECK(Console::StyleColour(Console::kStylePanel).a < 255);
    CHECK(Console::StyleColour(Console::kStyleHint).a == 255);
    // Projector panel note styles stay bright and readable on dark capsules (matching bottom capsule contrast).
    const Console::Style notes[] = {Console::kStyleHeading, Console::kStyleFilmCount, Console::kStyleStatus, Console::kStyleHint};
    for (unsigned i = 0; i < sizeof(notes) / sizeof(notes[0]); ++i) {
        const Console::Rgba c = Console::StyleColour(notes[i]);
        CHECK(c.a == 255 && c.r >= 204 && c.r < 240 && c.r == c.g && c.g == c.b);
    }
}

// ---------------------------------------------------------------------------
// Aiming limits

void CheckPickLimits(const Console::Mount &m) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float hw = m.halfWidth, hh = m.halfHeight, reach = m.reach;
    const Region none = Controls::kRegionNone;
    const Vec3 play = CentreOf(m, Controls::kRegionPlay);
    const Vec3 next = CentreOf(m, Controls::kRegionNext);
    Console::Extent key;
    CHECK(Console::ControlExtent(m, Controls::kRegionPlay, &key));
    const float keyV = 0.5f * (key.vMin + key.vMax);
    const Vec3 eye = EyeOf(m, 0.5f * reach, 0.0f, 0.0f);
    const Vec3 square = Add(play, Mul(m.front, 0.5f * reach));

    // Straight on.
    Console::Hit hit = PickToward(m, square, play);
    CHECK(hit.region == Controls::kRegionPlay);
    CHECK(Near(hit.distance, 0.5f * reach, 1.0e-3f * reach));

    // From behind the panel nothing is picked, however the aim runs.
    hit = PickToward(m, Sub(play, Mul(m.front, 0.5f * reach)), play);
    CHECK(hit.status == Console::kPickBehind);
    CHECK(hit.region == none && !hit.inRange && !hit.atConsole && !hit.onPanel);

    // Above and below the controls: on the plate, then off the panel, then away.
    hit = PickToward(m, eye, PanelAt(m, 0.0f, 0.5f * hh));
    CHECK(hit.status == Console::kPickPanel && hit.region == none && hit.atConsole);
    hit = PickToward(m, eye, PanelAt(m, 0.0f, 1.3f * hh));
    CHECK(hit.status == Console::kPickNear && hit.region == none);
    CHECK(hit.atConsole && !hit.onPanel);
    hit = PickToward(m, eye, PanelAt(m, 0.0f, hh + hw));
    CHECK(hit.status == Console::kPickLookingAway && hit.region == none);
    CHECK(hit.inRange && !hit.atConsole && !hit.onPanel && hit.meetsPlane);
    hit = PickToward(m, eye, PanelAt(m, 0.0f, -0.9f * hh));
    CHECK(hit.status == Console::kPickPanel && hit.region == none);
    hit = PickToward(m, eye, PanelAt(m, 0.0f, -(hh + hw)));
    CHECK(hit.region == none && !hit.atConsole && hit.inRange);

    // Beside the panel.
    hit = PickToward(m, eye, PanelAt(m, -1.2f * hw, keyV));
    CHECK(hit.status == Console::kPickNear && hit.region == none && hit.atConsole);
    hit = PickToward(m, eye, PanelAt(m, -2.0f * hw, keyV));
    CHECK(hit.region == none && !hit.atConsole);
    hit = PickToward(m, eye, PanelAt(m, 2.0f * hw, keyV));
    CHECK(hit.region == none && !hit.atConsole);

    // Reach, and the near limit.
    hit = PickToward(m, Add(play, Mul(m.front, 0.95f * reach)), play);
    CHECK(hit.region == Controls::kRegionPlay);
    hit = PickToward(m, Add(play, Mul(m.front, 1.05f * reach)), play);
    CHECK(hit.status == Console::kPickOutOfReach);
    CHECK(hit.region == none && !hit.inRange && !hit.atConsole);
    const float nearLimit = Console::NearLimit(m);
    hit = PickToward(m, Add(play, Mul(m.front, 0.5f * nearLimit)), play);
    CHECK(hit.status == Console::kPickBehind && hit.region == none && !hit.atConsole);
    hit = PickToward(m, Add(play, Mul(m.front, 2.0f * nearLimit)), play);
    CHECK(hit.region == Controls::kRegionPlay);

    // From far above or below the console, out of reach; from a seat's height, fine.
    hit = PickToward(m, EyeOf(m, 0.2f * reach, 0.0f, 1.1f * reach), play);
    CHECK(hit.status == Console::kPickOutOfReach && hit.region == none);
    hit = PickToward(m, EyeOf(m, 0.2f * reach, 0.0f, -1.1f * reach), play);
    CHECK(hit.status == Console::kPickOutOfReach && hit.region == none);
    hit = PickToward(m, EyeOf(m, 0.4f * reach, 0.0f, 0.3f * reach), next);
    CHECK(hit.region == Controls::kRegionNext);
    hit = PickToward(m, EyeOf(m, 0.4f * reach, 0.0f, -0.3f * reach), next);
    CHECK(hit.region == Controls::kRegionNext);

    // A ray that only skims the panel, leaves it, or runs along it.
    hit = PickToward(m, EyeOf(m, 0.02f * reach, 0.4f * reach, 0.0f), play);
    CHECK(hit.status == Console::kPickLookingAway);
    CHECK(hit.region == none && hit.inRange && !hit.atConsole);
    hit = Console::Pick(m, ViewAlong(eye, m.front));
    CHECK(hit.status == Console::kPickLookingAway);
    CHECK(hit.region == none && hit.inRange && !hit.atConsole && !hit.meetsPlane);
    hit = Console::Pick(m, ViewAlong(eye, m.right));
    CHECK(hit.region == none && !hit.atConsole);
    hit = Console::Pick(m, ViewAlong(eye, m.up));
    CHECK(hit.region == none && !hit.atConsole);

    // Aims and origins that are not directions or places.
    hit = Console::Pick(m, ViewAlong(square, V(0.0f, 0.0f, 0.0f)));
    CHECK(hit.status == Console::kPickBadView && hit.region == none && !hit.atConsole);
    hit = Console::Pick(m, ViewAlong(square, V(nan, 0.0f, 0.0f)));
    CHECK(hit.status == Console::kPickBadView && hit.region == none && !hit.atConsole);
    hit = Console::Pick(m, ViewAlong(square, V(0.0f, inf, 0.0f)));
    CHECK(hit.status == Console::kPickBadView && hit.region == none && !hit.atConsole);
    hit = Console::Pick(m, ViewAlong(V(nan, 0.0f, 0.0f), Mul(m.front, -1.0f)));
    CHECK(hit.status == Console::kPickBadView && hit.region == none && !hit.atConsole);
    hit = Console::Pick(m, ViewAlong(V(0.0f, inf, 0.0f), Mul(m.front, -1.0f)));
    CHECK(hit.region == none && !hit.atConsole);

    // Any finite length of aim is the same aim.
    const Vec3 toward = Sub(play, square);
    hit = Console::Pick(m, ViewAlong(square, Mul(toward, 1.0e30f)));
    CHECK(hit.region == Controls::kRegionPlay);
    hit = Console::Pick(m, ViewAlong(square, Mul(toward, 1.0e-20f)));
    CHECK(hit.region == Controls::kRegionPlay);
}

void TestPickLimits() {
    for (unsigned n = 0; n < kMountCount; ++n) CheckPickLimits(MountNumber(n));
}

Console::PresentInput MakeScene(const Console::Hit &hit) {
    Console::PresentInput in = Console::MakePresentInput();
    in.focus = hit;
    in.films = 3u;
    in.title = "Dawn Patrol";
    in.titleIndex = 0;
    in.fireLabel = "Mouse 1";
    in.controls.selected = 0;
    in.controls.usable = true;
    in.controls.prompt = Controls::kPromptPlay;
    return in;
}

void TestSuspendedAndAwayPickNothing() {
    const Console::Mount m = NativeDown();
    const Vec3 eye = EyeOf(m, 0.4f * m.reach, 0.0f, 0.0f);
    const Console::View base = ViewToward(eye, CentreOf(m, Controls::kRegionPlay));
    const Console::Hit open = Console::Pick(m, base);
    CHECK(open.region == Controls::kRegionPlay && open.atConsole);
    CHECK(Console::Present(MakeScene(open)).visible);

    for (unsigned mode = 0; mode < 5u; ++mode) {
        Console::View view = base;
        if (mode == 0u) view.museumActive = false;
        if (mode == 1u) view.inRoom = false;
        if (mode == 2u) view.focused = false;
        if (mode == 3u) view.paused = true;
        if (mode == 4u) view.photo = true;
        const Console::Hit hit = Console::Pick(m, view);
        CHECK(hit.status == (mode < 2u ? Console::kPickAway : Console::kPickSuspended));
        CHECK(hit.region == Controls::kRegionNone);
        CHECK(!hit.inRange && !hit.atConsole && !hit.onPanel && !hit.meetsPlane);

        // Feed clears what the selector is told, whatever was there before.
        Controls::Input input;
        input.museumActive = true; input.inRoom = true; input.atConsole = true;
        input.region = Controls::kRegionPlay; input.fireDown = false; input.paused = false;
        input.focused = true; input.films = 3u; input.decoder = Controls::kDecoderStopped;
        input.playing = -1;
        Console::Feed(hit, &input);
        CHECK(!input.atConsole);
        CHECK(input.region == Controls::kRegionNone);
        CHECK(input.films == 3u && input.focused && input.inRoom);

        // And the panel is gone on the same frame.
        const Console::Presentation p = Console::Present(MakeScene(hit));
        CHECK(!p.visible && !p.actionable);
        CHECK(p.lineCount == 0u && p.rectCount == 0u);
    }
    Console::Feed(open, 0);
}

// ---------------------------------------------------------------------------
// The console, the selector and a stand-in for the room's player, together

struct Rig {
    Console::Mount mount;
    Console::View view;
    Controls::State state;
    unsigned films;
    Controls::Decoder decoder;
    int playing;
    Console::Hit hit;
    Controls::Output out;
    Console::HostAction action;
    int opened;          // the last index handed to the film accessor, or -1
    unsigned opens;
    unsigned stops;
};

Vec3 RigEye(const Rig &r) {
    return EyeOf(r.mount, 0.4f * r.mount.reach, 0.05f * r.mount.reach, 0.02f * r.mount.reach);
}
// Turning the head changes where the visitor looks, not what the game is doing.
void RigKeepFlags(Rig *r, const Console::View &was) {
    r->view.museumActive = was.museumActive;
    r->view.inRoom = was.inRoom;
    r->view.focused = was.focused;
    r->view.paused = was.paused;
    r->view.photo = was.photo;
}
void RigAimAway(Rig *r) {
    const Console::View was = r->view;
    r->view = ViewAlong(RigEye(*r), r->mount.front);
    RigKeepFlags(r, was);
}
void RigAim(Rig *r, Region region) {
    const Console::View was = r->view;
    r->view = ViewToward(RigEye(*r), CentreOf(r->mount, region));
    RigKeepFlags(r, was);
}
void RigAimGutter(Rig *r) {
    Console::Extent a, b;
    CHECK(Console::ControlExtent(r->mount, Controls::kRegionPrevious, &a));
    CHECK(Console::ControlExtent(r->mount, Controls::kRegionPlay, &b));
    const Console::View was = r->view;
    r->view = ViewToward(RigEye(*r), PanelAt(r->mount, 0.5f * (a.uMax + b.uMin),
                                             0.5f * (a.vMin + a.vMax)));
    RigKeepFlags(r, was);
}
void RigInit(Rig *r, unsigned films) {
    r->mount = NativeDown();
    r->view = ViewAlong(V(0.0f, 0.0f, 0.0f), V(1.0f, 0.0f, 0.0f));
    RigAimAway(r);
    Controls::Reset(&r->state);
    r->films = films;
    r->decoder = Controls::kDecoderStopped;
    r->playing = -1;
    r->hit = Console::NoHit();
    r->out = Console::MakePresentInput().controls;
    r->action = Console::ResolveCommand(r->out, films);
    r->opened = -1;
    r->opens = 0u;
    r->stops = 0u;
}

// One frame as the host runs it: pick with the real view, feed the selector,
// carry out what it returns.
void RigStep(Rig *r, bool fire) {
    r->hit = Console::Pick(r->mount, r->view);
    Controls::Input in;
    in.museumActive = r->view.museumActive;
    in.inRoom = r->view.inRoom;
    in.atConsole = false;
    in.region = Controls::kRegionNone;
    in.fireDown = fire;
    in.paused = r->view.paused;
    in.focused = r->view.focused;
    in.films = r->films;
    in.decoder = r->decoder;
    in.playing = r->playing;
    Console::Feed(r->hit, &in);
    r->out = Controls::Update(&r->state, in);
    r->action = Console::ResolveCommand(r->out, r->films);
    if (r->action.play) {
        r->decoder = Controls::kDecoderPlaying;
        r->playing = static_cast<int>(r->action.index);
        r->opened = r->playing;
        ++r->opens;
    }
    if (r->action.stop) {
        r->decoder = Controls::kDecoderStopped;
        r->playing = -1;
        ++r->stops;
    }
}
void RigClick(Rig *r, Region region) {
    RigAim(r, region);
    RigStep(r, true);
    RigStep(r, false);
}

void TestSelectWrapsAndCarriesAnIndex() {
    Rig r;
    RigInit(&r, 3u);
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, false);
    CHECK(r.hit.region == Controls::kRegionPlay);
    CHECK(r.out.selected == 0 && r.out.usable && !r.out.consumeFire);
    CHECK(r.out.prompt == Controls::kPromptPlay);

    RigAim(&r, Controls::kRegionNext);
    RigStep(&r, true);
    CHECK(r.out.consumeFire && r.out.pressed == Controls::kRegionNext);
    CHECK(r.out.command == Controls::kCommandNone && r.out.prompt == Controls::kPromptNext);
    RigStep(&r, false);
    CHECK(r.out.consumeFire);
    CHECK(r.out.command == Controls::kCommandSelect && r.out.index == 1 && r.out.selected == 1);
    CHECK(r.action.select && r.action.hasIndex && r.action.index == 1u);
    CHECK(!r.action.play && !r.action.stop);
    RigStep(&r, false);
    CHECK(!r.out.consumeFire && r.out.command == Controls::kCommandNone && r.out.selected == 1);

    RigClick(&r, Controls::kRegionNext);
    CHECK(r.action.select && r.action.index == 2u && r.out.selected == 2);
    RigClick(&r, Controls::kRegionNext);                    // past the end
    CHECK(r.action.select && r.action.index == 0u && r.out.selected == 0);
    RigClick(&r, Controls::kRegionPrevious);                // before the start
    CHECK(r.action.select && r.action.index == 2u && r.out.selected == 2);
    RigClick(&r, Controls::kRegionPrevious);
    CHECK(r.action.select && r.action.index == 1u && r.out.selected == 1);
    // Choosing opens nothing.
    CHECK(r.opens == 0u && r.stops == 0u && r.decoder == Controls::kDecoderStopped);

    // One film: Previous and Next take the press and do nothing with it.
    Rig one;
    RigInit(&one, 1u);
    RigAim(&one, Controls::kRegionNext);
    RigStep(&one, false);
    CHECK(one.out.prompt == Controls::kPromptOnlyFilm && one.out.selected == 0);
    RigStep(&one, true);
    CHECK(one.out.consumeFire);
    RigStep(&one, false);
    CHECK(one.out.consumeFire && one.out.command == Controls::kCommandNone && one.out.selected == 0);
    RigClick(&one, Controls::kRegionPlay);
    CHECK(one.action.play && one.action.index == 0u && one.opened == 0);
}

void TestPlayAndStopCarryTheChosenIndex() {
    Rig r;
    RigInit(&r, 4u);
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, false);
    RigClick(&r, Controls::kRegionNext);
    RigClick(&r, Controls::kRegionNext);
    CHECK(r.out.selected == 2);

    RigClick(&r, Controls::kRegionPlay);
    CHECK(r.out.command == Controls::kCommandPlay && r.out.index == 2 && r.out.playing == 2);
    CHECK(r.action.play && r.action.hasIndex && r.action.index == 2u && !r.action.select);
    CHECK(r.opened == 2 && r.opens == 1u);
    CHECK(r.decoder == Controls::kDecoderPlaying && r.playing == 2);
    RigStep(&r, false);
    CHECK(r.out.prompt == Controls::kPromptStop && r.out.playing == 2);

    // Choosing another film leaves the one on the screen alone.
    RigClick(&r, Controls::kRegionPrevious);
    CHECK(r.out.selected == 1 && r.playing == 2 && r.opens == 1u && r.stops == 0u);
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, false);
    CHECK(r.out.prompt == Controls::kPromptPlay);

    // Play then opens the chosen one, not the one that was running.
    RigClick(&r, Controls::kRegionPlay);
    CHECK(r.out.command == Controls::kCommandPlay && r.out.index == 1);
    CHECK(r.opened == 1 && r.opens == 2u && r.playing == 1);

    // The same control now stops it.
    RigClick(&r, Controls::kRegionPlay);
    CHECK(r.out.command == Controls::kCommandStop && r.out.playing == -1);
    CHECK(r.action.stop && !r.action.play && !r.action.hasIndex);
    CHECK(r.decoder == Controls::kDecoderStopped && r.playing == -1 && r.stops == 1u);

    // A film that ran to its end is played again from the same control.
    RigClick(&r, Controls::kRegionPlay);
    CHECK(r.opened == 1 && r.opens == 3u);
    r.decoder = Controls::kDecoderEnded;
    RigStep(&r, false);
    CHECK(r.out.prompt == Controls::kPromptPlay && r.out.playing == 1);
    RigClick(&r, Controls::kRegionPlay);
    CHECK(r.action.play && r.action.index == 1u && r.opens == 4u);
    CHECK(r.decoder == Controls::kDecoderPlaying);
}

void TestHeldFireOnArrivalDoesNothing() {
    // A new visit with Fire already down and the aim already on a control.
    Rig r;
    RigInit(&r, 3u);
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, true);
    CHECK(r.hit.region == Controls::kRegionPlay);
    CHECK(!r.out.consumeFire && r.out.command == Controls::kCommandNone);
    CHECK(r.out.pressed == Controls::kRegionNone);
    RigStep(&r, true);
    CHECK(!r.out.consumeFire && r.out.pressed == Controls::kRegionNone);
    RigStep(&r, false);
    CHECK(!r.out.consumeFire && r.out.command == Controls::kCommandNone && r.opens == 0u);

    // Fire that went down off the console and was carried onto a control.
    RigAimAway(&r);
    RigStep(&r, true);
    CHECK(!r.out.consumeFire);
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, true);
    CHECK(!r.out.consumeFire && r.out.pressed == Controls::kRegionNone);
    RigStep(&r, false);
    CHECK(!r.out.consumeFire && r.out.command == Controls::kCommandNone && r.opens == 0u);

    // A press that begins there afterwards works.
    RigClick(&r, Controls::kRegionPlay);
    CHECK(r.action.play && r.action.index == 0u && r.opens == 1u);
}

void TestOwnedPressIsConsumedToItsRelease() {
    Rig r;
    RigInit(&r, 3u);
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, false);

    // Looks away with Fire still down.
    RigStep(&r, true);
    CHECK(r.out.consumeFire && r.out.pressed == Controls::kRegionPlay);
    RigAimAway(&r);
    RigStep(&r, true);
    CHECK(!r.hit.atConsole);
    CHECK(r.out.consumeFire && r.out.pressed == Controls::kRegionNone);
    CHECK(r.out.command == Controls::kCommandNone);
    RigStep(&r, false);
    CHECK(r.out.consumeFire && r.out.command == Controls::kCommandNone && r.opens == 0u);
    RigStep(&r, false);
    CHECK(!r.out.consumeFire);

    // Walks out of the room with Fire still down.
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, true);
    CHECK(r.out.consumeFire && r.out.pressed == Controls::kRegionPlay);
    r.view.inRoom = false;
    RigStep(&r, true);
    CHECK(r.hit.status == Console::kPickAway);
    CHECK(r.out.consumeFire && r.out.command == Controls::kCommandNone);
    RigStep(&r, false);
    CHECK(r.out.consumeFire && r.out.command == Controls::kCommandNone && r.opens == 0u);
    RigStep(&r, false);
    CHECK(!r.out.consumeFire);
    r.view.inRoom = true;

    // The visit ends under the press.
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, false);
    RigStep(&r, true);
    CHECK(r.out.consumeFire);
    r.view.museumActive = false;
    RigStep(&r, true);
    CHECK(r.out.consumeFire && r.out.command == Controls::kCommandNone);
    RigStep(&r, false);
    CHECK(r.out.consumeFire && r.out.command == Controls::kCommandNone && r.opens == 0u);
    RigStep(&r, false);
    CHECK(!r.out.consumeFire);
}

void TestFireOffTheConsolePassesThrough() {
    Rig r;
    RigInit(&r, 3u);
    RigAimAway(&r);
    RigStep(&r, false);
    RigStep(&r, true);
    CHECK(!r.out.consumeFire && r.out.command == Controls::kCommandNone);
    RigStep(&r, false);
    CHECK(!r.out.consumeFire && r.out.command == Controls::kCommandNone);

    // On the panel, in a gutter: still the game's Fire.
    RigAimGutter(&r);
    RigStep(&r, false);
    CHECK(r.hit.atConsole && r.hit.onPanel && r.hit.region == Controls::kRegionNone);
    CHECK(r.out.prompt == Controls::kPromptAim);
    RigStep(&r, true);
    CHECK(!r.out.consumeFire);
    RigStep(&r, false);
    CHECK(!r.out.consumeFire && r.out.command == Controls::kCommandNone && r.out.selected == 0);

    // Nothing to show: the console never takes Fire.
    Rig empty;
    RigInit(&empty, 0u);
    RigAim(&empty, Controls::kRegionPlay);
    RigStep(&empty, false);
    CHECK(empty.hit.region == Controls::kRegionPlay);
    CHECK(!empty.out.usable && empty.out.selected == -1);
    CHECK(empty.out.prompt == Controls::kPromptUnavailable);
    RigStep(&empty, true);
    CHECK(!empty.out.consumeFire);
    RigStep(&empty, false);
    CHECK(!empty.out.consumeFire && empty.out.command == Controls::kCommandNone);

    Rig broken;
    RigInit(&broken, 3u);
    broken.decoder = Controls::kDecoderUnavailable;
    RigAim(&broken, Controls::kRegionPlay);
    RigStep(&broken, false);
    CHECK(!broken.out.usable && broken.out.prompt == Controls::kPromptUnavailable);
    RigStep(&broken, true);
    CHECK(!broken.out.consumeFire);
    RigStep(&broken, false);
    CHECK(!broken.out.consumeFire && broken.opens == 0u);
}

void TestCrossingControlsSpoilsThePress() {
    Rig r;
    RigInit(&r, 3u);
    RigAim(&r, Controls::kRegionPrevious);
    RigStep(&r, false);

    // Pressed on Previous, dragged to Next, released there.
    RigStep(&r, true);
    CHECK(r.out.consumeFire && r.out.pressed == Controls::kRegionPrevious);
    RigAim(&r, Controls::kRegionNext);
    RigStep(&r, true);
    CHECK(r.hit.region == Controls::kRegionNext);
    CHECK(r.out.consumeFire && r.out.pressed == Controls::kRegionNone);
    RigStep(&r, false);
    CHECK(r.out.consumeFire && r.out.command == Controls::kCommandNone && r.out.selected == 0);

    // Through the gutter and back onto the same control.
    RigAim(&r, Controls::kRegionPrevious);
    RigStep(&r, true);
    CHECK(r.out.pressed == Controls::kRegionPrevious);
    RigAimGutter(&r);
    RigStep(&r, true);
    CHECK(r.hit.region == Controls::kRegionNone);
    CHECK(r.out.consumeFire && r.out.pressed == Controls::kRegionNone);
    RigAim(&r, Controls::kRegionPrevious);
    RigStep(&r, true);
    CHECK(r.out.consumeFire && r.out.pressed == Controls::kRegionNone);
    RigStep(&r, false);
    CHECK(r.out.consumeFire && r.out.command == Controls::kCommandNone && r.out.selected == 0);

    // A press that stays put still acts.
    RigClick(&r, Controls::kRegionPrevious);
    CHECK(r.action.select && r.action.index == 2u && r.out.selected == 2);
}

void SetSuspension(Rig *r, unsigned mode, bool on) {
    if (mode == 0u) r->view.paused = on;
    if (mode == 1u) r->view.photo = on;
    if (mode == 2u) r->view.focused = !on;
}

void TestSuspensionSpoilsThePress() {
    for (unsigned mode = 0; mode < 3u; ++mode) {
        Rig r;
        RigInit(&r, 3u);
        RigAim(&r, Controls::kRegionPlay);
        RigStep(&r, false);
        RigStep(&r, true);
        CHECK(r.out.consumeFire && r.out.pressed == Controls::kRegionPlay);

        SetSuspension(&r, mode, true);
        RigStep(&r, true);
        CHECK(r.hit.status == Console::kPickSuspended && !r.hit.atConsole);
        CHECK(r.out.consumeFire && r.out.pressed == Controls::kRegionNone);
        SetSuspension(&r, mode, false);
        RigStep(&r, true);
        CHECK(r.hit.region == Controls::kRegionPlay);
        CHECK(r.out.consumeFire && r.out.pressed == Controls::kRegionNone);
        RigStep(&r, false);
        CHECK(r.out.consumeFire && r.out.command == Controls::kCommandNone && r.opens == 0u);

        // A press that begins while suspended is not the console's.
        SetSuspension(&r, mode, true);
        RigStep(&r, true);
        CHECK(!r.out.consumeFire);
        RigStep(&r, false);
        CHECK(!r.out.consumeFire && r.out.command == Controls::kCommandNone);
        SetSuspension(&r, mode, false);

        RigClick(&r, Controls::kRegionPlay);
        CHECK(r.action.play && r.action.index == 0u && r.opens == 1u);
    }
}

void TestLeavingStopsAndReturningKeepsTheChoice() {
    Rig r;
    RigInit(&r, 3u);
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, false);
    RigClick(&r, Controls::kRegionNext);
    RigClick(&r, Controls::kRegionPlay);
    CHECK(r.playing == 1 && r.decoder == Controls::kDecoderPlaying);

    // Out of the room: the screen is cleared, once, and the panel is gone.
    r.view.inRoom = false;
    RigStep(&r, false);
    CHECK(r.out.command == Controls::kCommandStop && r.action.stop);
    CHECK(r.decoder == Controls::kDecoderStopped && r.playing == -1);
    CHECK(!r.hit.atConsole);
    CHECK(!Console::Present(MakeScene(r.hit)).visible);
    RigStep(&r, false);
    CHECK(r.out.command == Controls::kCommandNone && r.stops == 1u);
    CHECK(r.out.selected == 1);

    // Back in, holding Fire from outside: nothing happens, the choice stands.
    RigStep(&r, true);
    CHECK(!r.out.consumeFire);
    r.view.inRoom = true;
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, true);
    CHECK(r.hit.region == Controls::kRegionPlay);
    CHECK(!r.out.consumeFire && r.out.pressed == Controls::kRegionNone);
    RigStep(&r, false);
    CHECK(!r.out.consumeFire && r.out.command == Controls::kCommandNone);
    CHECK(r.out.selected == 1 && r.opens == 1u);
    RigClick(&r, Controls::kRegionPlay);
    CHECK(r.action.play && r.action.index == 1u && r.opens == 2u);

    // A new visit starts from the first film with nothing on the screen.
    Controls::Reset(&r.state);
    r.decoder = Controls::kDecoderStopped;
    r.playing = -1;
    RigStep(&r, false);
    CHECK(r.out.selected == 0 && r.out.playing == -1);
    CHECK(r.out.command == Controls::kCommandNone);
}

void TestResolveCommandChecksTheIndex() {
    Controls::Output out = Console::MakePresentInput().controls;
    Console::HostAction a = Console::ResolveCommand(out, 3u);
    CHECK(!a.select && !a.play && !a.stop && !a.hasIndex);

    out.command = Controls::kCommandPlay;
    out.index = 2;
    a = Console::ResolveCommand(out, 3u);
    CHECK(a.play && a.hasIndex && a.index == 2u && !a.select && !a.stop);
    out.index = 7;
    a = Console::ResolveCommand(out, 3u);
    CHECK(!a.play && !a.hasIndex && !a.select && !a.stop);
    out.index = -1;
    a = Console::ResolveCommand(out, 3u);
    CHECK(!a.play && !a.hasIndex);
    out.index = 0;
    a = Console::ResolveCommand(out, 0u);
    CHECK(!a.play && !a.hasIndex);

    out.command = Controls::kCommandSelect;
    out.index = 0;
    a = Console::ResolveCommand(out, 3u);
    CHECK(a.select && a.hasIndex && a.index == 0u && !a.play && !a.stop);

    out.command = Controls::kCommandStop;
    out.index = -1;
    a = Console::ResolveCommand(out, 3u);
    CHECK(a.stop && !a.hasIndex && !a.play && !a.select);
}

// The middle control's mark and label follow the selector's own Play or Stop.
void TestShowsStopMatchesThePrompt() {
    const Controls::Decoder decoders[4] = {Controls::kDecoderStopped, Controls::kDecoderPlaying,
                                           Controls::kDecoderEnded, Controls::kDecoderUnavailable};
    for (unsigned d = 0; d < 4u; ++d) {
        for (int playing = -1; playing <= 3; ++playing) {
            for (int chosen = 0; chosen < 3; ++chosen) {
                Controls::State state;
                Controls::Reset(&state);
                CHECK(Controls::Select(&state, 3u, chosen));
                Controls::Input in;
                in.museumActive = true; in.inRoom = true; in.atConsole = true;
                in.region = Controls::kRegionPlay; in.fireDown = false; in.paused = false;
                in.focused = true; in.films = 3u; in.decoder = decoders[d]; in.playing = playing;
                const Controls::Output out = Controls::Update(&state, in);
                const bool stop = out.prompt == Controls::kPromptStop;
                CHECK(Console::ShowsStop(decoders[d], playing, out.selected, 3u) == stop);
                Console::Hit hit = Console::NoHit();
                hit.atConsole = true;
                hit.region = Controls::kRegionPlay;
                const Console::Display display =
                    Console::MakeDisplay(hit, out, 3u, decoders[d], playing);
                CHECK(display.showStop == stop);
                CHECK(display.usable == out.usable);
                CHECK(display.focus == Controls::kRegionPlay && display.multipleFilms);
            }
        }
    }
    // A display made from a look elsewhere has no focus and nothing held.
    Controls::Output out = Console::MakePresentInput().controls;
    out.usable = true;
    out.pressed = Controls::kRegionPlay;
    const Console::Display away =
        Console::MakeDisplay(Console::NoHit(), out, 1u, Controls::kDecoderEnded, 0);
    CHECK(away.focus == Controls::kRegionNone && away.pressed == Controls::kRegionNone);
    CHECK(!away.multipleFilms && away.lamp == Console::kLampEnded && !away.showStop);
}

// ---------------------------------------------------------------------------
// The panel

float MeasureSeven(const char *text, void *) { return 7.0f * static_cast<float>(std::strlen(text)); }
float MeasureWide(const char *text, void *) { return 13.0f * static_cast<float>(std::strlen(text)); }
float MeasureNaN(const char *, void *) { return std::numeric_limits<float>::quiet_NaN(); }
float MeasureNegative(const char *, void *) { return -40.0f; }

bool EndsWith(const char *text, const char *tail) {
    const std::size_t n = std::strlen(text), t = std::strlen(tail);
    return n >= t && std::strcmp(text + (n - t), tail) == 0;
}

bool ValidUtf8(const char *text) {
    const unsigned char *s = reinterpret_cast<const unsigned char *>(text);
    while (*s != 0u) {
        unsigned more = 0;
        if (*s < 0x80u) more = 0u;
        else if (*s >= 0xC2u && *s <= 0xDFu) more = 1u;
        else if (*s >= 0xE0u && *s <= 0xEFu) more = 2u;
        else if (*s >= 0xF0u && *s <= 0xF4u) more = 3u;
        else return false;
        ++s;
        for (unsigned i = 0; i < more; ++i, ++s)
            if ((*s & 0xC0u) != 0x80u) return false;
    }
    return true;
}

bool LineIs(const Console::Presentation &p, Console::Style style, const char *text) {
    for (unsigned i = 0; i < p.lineCount && i < Console::kMaxLines; ++i)
        if (p.lines[i].style == style && std::strcmp(p.lines[i].text, text) == 0) return true;
    return false;
}
bool AnyLineIs(const Console::Presentation &p, const char *text) {
    for (unsigned i = 0; i < p.lineCount && i < Console::kMaxLines; ++i)
        if (std::strcmp(p.lines[i].text, text) == 0) return true;
    return false;
}
unsigned CountLines(const Console::Presentation &p, Console::Style style) {
    unsigned count = 0;
    for (unsigned i = 0; i < p.lineCount && i < Console::kMaxLines; ++i)
        if (p.lines[i].style == style) ++count;
    return count;
}
const Console::TextLine *LineOf(const Console::Presentation &p, Console::Style style, unsigned nth) {
    for (unsigned i = 0; i < p.lineCount && i < Console::kMaxLines; ++i)
        if (p.lines[i].style == style && nth-- == 0u) return &p.lines[i];
    return 0;
}
unsigned CountRects(const Console::Presentation &p, Console::Style style) {
    unsigned count = 0;
    for (unsigned i = 0; i < p.rectCount && i < Console::kMaxRects; ++i)
        if (p.rects[i].style == style) ++count;
    return count;
}
const Console::PanelRect *ChipOf(const Console::Presentation &p, Region region) {
    for (unsigned i = 0; i < p.rectCount && i < Console::kMaxRects; ++i)
        if (p.rects[i].region == region) return &p.rects[i];
    return 0;
}

// Everything drawn is inside the panel, the panel inside its bounds and the
// viewport, every line as wide as the font says and no two on top of each other.
void CheckLayout(const Console::Presentation &p, const Console::Frame &bounds,
                 Console::MeasureText measure) {
    const float eps = 0.02f;
    if (!p.visible) {
        CHECK(p.lineCount == 0u && p.rectCount == 0u && !p.actionable);
        return;
    }
    CHECK(p.rectCount >= 2u && p.rectCount <= Console::kMaxRects);
    CHECK(p.lineCount >= 1u && p.lineCount <= Console::kMaxLines);
    if (p.rectCount < 1u || p.rectCount > Console::kMaxRects || p.lineCount > Console::kMaxLines)
        return;
    const Console::PanelRect &panel = p.rects[0];
    CHECK(panel.style == Console::kStylePanel);
    CHECK(Near(panel.x, p.panel.x, eps) && Near(panel.y, p.panel.y, eps));
    CHECK(Near(panel.w, p.panel.w, eps) && Near(panel.h, p.panel.h, eps));
    CHECK(panel.x >= bounds.x - eps && panel.x + panel.w <= bounds.x + bounds.w + eps);
    CHECK(panel.y >= bounds.y - eps && panel.y + panel.h <= bounds.y + bounds.h + eps);
    CHECK(panel.x >= 0.0f && panel.x + panel.w <= Console::kViewportWidth);
    CHECK(panel.y >= 0.0f && panel.y + panel.h <= Console::kViewportHeight);
    // Not a full-screen menu: the world stays visible around it.
    CHECK(panel.w * panel.h <= 0.2f * Console::kViewportWidth * Console::kViewportHeight);

    unsigned chips = 0;
    float lastRight = -1.0f;
    for (unsigned i = 0; i < p.rectCount; ++i) {
        const Console::PanelRect &rect = p.rects[i];
        CHECK(rect.w > 0.0f && rect.h > 0.0f);
        CHECK(rect.x >= panel.x - eps && rect.x + rect.w <= panel.x + panel.w + eps);
        CHECK(rect.y >= panel.y - eps && rect.y + rect.h <= panel.y + panel.h + eps);
        if (rect.region == Controls::kRegionNone) continue;
        CHECK(chips < 3u);
        if (chips < 3u) CHECK(rect.region == kOrder[chips]);
        CHECK(rect.x >= lastRight + 1.0f);
        lastRight = rect.x + rect.w;
        ++chips;
    }
    CHECK(chips == 0u || chips == 3u);

    for (unsigned i = 0; i < p.lineCount; ++i) {
        const Console::TextLine &line = p.lines[i];
        const bool terminated = std::memchr(line.text, 0, Console::kTextCapacity) != 0;
        CHECK(terminated);
        if (!terminated) continue;
        CHECK(line.text[0] != '\0');
        CHECK(ValidUtf8(line.text));
        CHECK(line.width >= 0.0f && line.height > 0.0f);
        CHECK(line.x >= panel.x - eps && line.x + line.width <= panel.x + panel.w + eps);
        CHECK(line.y >= panel.y - eps && line.y + line.height <= panel.y + panel.h + eps);
        if (measure) CHECK(Near(line.width, measure(line.text, 0), eps));
        if (line.truncated) CHECK(EndsWith(line.text, "..."));
        for (unsigned k = 0; k < i; ++k) {
            const Console::TextLine &other = p.lines[k];
            const bool overlap = line.x < other.x + other.width - eps &&
                                 other.x < line.x + line.width - eps &&
                                 line.y < other.y + other.height - eps &&
                                 other.y < line.y + line.height - eps;
            CHECK(!overlap);
        }
    }
}

const char *const kTitles[5] = {"Opening Titles", "Dawn Patrol", "The Long Crossing",
                                "Islands At War", "Homecoming"};

// What the host hands Present after a step: titles fetched by the selector's
// own indices, the player's state after the step's command.
Console::PresentInput Scene(const Rig &r, const char *const *titles, const char *fire) {
    Console::PresentInput in = Console::MakePresentInput();
    in.focus = r.hit;
    in.controls = r.out;
    in.films = r.films;
    in.titleIndex = r.out.selected;
    in.title = titles && r.out.selected >= 0 ? titles[r.out.selected] : 0;
    in.playingTitle = titles && r.playing >= 0 ? titles[r.playing] : 0;
    in.fireLabel = fire;
    in.decoder = r.decoder;
    in.playing = r.playing;
    in.lineHeight = 12.0f;
    in.measure = MeasureSeven;
    return in;
}

void TestPanelNamesTheChosenAndTheShownFilm() {
    Rig r;
    RigInit(&r, 5u);
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, false);
    Console::PresentInput in = Scene(r, kTitles, "Mouse 1");
    Console::Presentation p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.visible && p.actionable && p.focus == Controls::kRegionPlay);
    CHECK(LineIs(p, Console::kStyleHeading, "Projector"));
    CHECK(LineIs(p, Console::kStyleFilmCount, "Film 1 of 5"));
    CHECK(LineIs(p, Console::kStyleTitle, "Opening Titles"));
    CHECK(LineIs(p, Console::kStyleStatus, "Not playing"));
    CHECK(LineIs(p, Console::kStyleBinding, "Mouse 1"));
    CHECK(LineIs(p, Console::kStyleAction, "Play this film"));
    CHECK(AnyLineIs(p, "Previous") && AnyLineIs(p, "Play") && AnyLineIs(p, "Next"));
    CHECK(!AnyLineIs(p, "Stop"));
    CHECK(CountRects(p, Console::kStyleAccent) == 1u);
    CHECK(Near(p.panel.x, Console::DefaultBounds().x, 0.02f));

    // The chosen film is the one running.
    RigClick(&r, Controls::kRegionNext);
    RigClick(&r, Controls::kRegionPlay);
    RigStep(&r, false);
    CHECK(r.out.selected == 1 && r.playing == 1);
    in = Scene(r, kTitles, "Mouse 1");
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(LineIs(p, Console::kStyleFilmCount, "Film 2 of 5"));
    CHECK(LineIs(p, Console::kStyleTitle, "Dawn Patrol"));
    CHECK(LineIs(p, Console::kStylePlaying, "Now playing"));
    CHECK(LineIs(p, Console::kStyleAction, "Stop this film"));
    CHECK(AnyLineIs(p, "Stop") && !AnyLineIs(p, "Play"));
    CHECK(CountRects(p, Console::kStyleAccentPlaying) == 1u);
    Console::Display display = Console::MakeDisplay(r.hit, r.out, r.films, r.decoder, r.playing);
    CHECK(display.showStop && display.lamp == Console::kLampPlaying);
    CHECK(display.focus == Controls::kRegionPlay && display.usable && display.multipleFilms);

    // Another film is chosen while that one runs: both are named, apart.
    RigClick(&r, Controls::kRegionNext);
    RigClick(&r, Controls::kRegionNext);
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, false);
    CHECK(r.out.selected == 3 && r.playing == 1);
    in = Scene(r, kTitles, "Mouse 1");
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(LineIs(p, Console::kStyleFilmCount, "Film 4 of 5"));
    CHECK(LineIs(p, Console::kStyleTitle, "Islands At War"));
    CHECK(LineIs(p, Console::kStylePlaying, "On screen: Dawn Patrol"));
    CHECK(LineIs(p, Console::kStyleAction, "Play this film"));
    CHECK(AnyLineIs(p, "Play") && !AnyLineIs(p, "Stop"));
    display = Console::MakeDisplay(r.hit, r.out, r.films, r.decoder, r.playing);
    CHECK(!display.showStop && display.lamp == Console::kLampPlaying);
    in.playingTitle = 0;
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(LineIs(p, Console::kStylePlaying, "On screen: film 2 of 5"));

    // That film runs out and still holds the screen.
    r.decoder = Controls::kDecoderEnded;
    RigStep(&r, false);
    in = Scene(r, kTitles, "Mouse 1");
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(LineIs(p, Console::kStylePlaying, "Finished: Dawn Patrol"));
    CHECK(CountRects(p, Console::kStyleAccent) == 1u);
    RigClick(&r, Controls::kRegionPrevious);
    RigClick(&r, Controls::kRegionPrevious);
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, false);
    CHECK(r.out.selected == 1);
    in = Scene(r, kTitles, "Mouse 1");
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(LineIs(p, Console::kStylePlaying, "Finished"));
    CHECK(LineIs(p, Console::kStyleAction, "Play this film"));

    // A title fetched for some other index is not shown as the chosen film's.
    in.titleIndex = r.out.selected + 1;
    p = Console::Present(in);
    CHECK(LineIs(p, Console::kStyleTitle, "Untitled film"));
    CHECK(!LineIs(p, Console::kStyleTitle, "Dawn Patrol"));
    in.titleIndex = r.out.selected;
    in.title = 0;
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(LineIs(p, Console::kStyleTitle, "Untitled film"));

    // A playing index outside the list is nothing on the screen.
    in = Scene(r, kTitles, "Mouse 1");
    in.decoder = Controls::kDecoderPlaying;
    in.playing = 9;
    p = Console::Present(in);
    CHECK(LineIs(p, Console::kStyleStatus, "Not playing"));
}

void TestCueOnlyForARealUsableFocus() {
    Rig r;
    RigInit(&r, 3u);

    // In a gutter: help, but no control named.
    RigAimGutter(&r);
    RigStep(&r, false);
    Console::PresentInput in = Scene(r, kTitles, "Mouse 1");
    Console::Presentation p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.visible && !p.actionable && p.focus == Controls::kRegionNone);
    CHECK(LineIs(p, Console::kStyleHint, "Aim at a control"));
    CHECK(CountLines(p, Console::kStyleBinding) == 0u && CountLines(p, Console::kStyleAction) == 0u);
    CHECK(CountRects(p, Console::kStyleBindingCap) == 0u);
    CHECK(CountRects(p, Console::kStyleKeyFocus) == 0u && CountRects(p, Console::kStyleKey) == 3u);

    // On Previous, then with Fire held on it.
    RigAim(&r, Controls::kRegionPrevious);
    RigStep(&r, false);
    in = Scene(r, kTitles, "Mouse 1");
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.actionable && p.focus == Controls::kRegionPrevious && p.pressed == Controls::kRegionNone);
    CHECK(LineIs(p, Console::kStyleBinding, "Mouse 1"));
    CHECK(LineIs(p, Console::kStyleAction, "Previous film"));
    CHECK(CountRects(p, Console::kStyleBindingCap) == 1u);
    const Console::PanelRect *chip = ChipOf(p, Controls::kRegionPrevious);
    CHECK(chip != 0 && chip->style == Console::kStyleKeyFocus);
    chip = ChipOf(p, Controls::kRegionNext);
    CHECK(chip != 0 && chip->style == Console::kStyleKey);
    RigStep(&r, true);
    in = Scene(r, kTitles, "Mouse 1");
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.actionable && p.pressed == Controls::kRegionPrevious);
    chip = ChipOf(p, Controls::kRegionPrevious);
    CHECK(chip != 0 && chip->style == Console::kStyleKeyPressed);
    Console::Display display = Console::MakeDisplay(r.hit, r.out, r.films, r.decoder, r.playing);
    CHECK(display.focus == Controls::kRegionPrevious && display.pressed == Controls::kRegionPrevious);
    RigStep(&r, false);

    RigAim(&r, Controls::kRegionNext);
    RigStep(&r, false);
    in = Scene(r, kTitles, "Mouse 1");
    p = Console::Present(in);
    CHECK(p.actionable && LineIs(p, Console::kStyleAction, "Next film"));

    // The caller's label is what is shown, whatever device it names.
    in.fireLabel = "Right Trigger";
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(LineIs(p, Console::kStyleBinding, "Right Trigger"));
    // With none to show, the action's own name stands in; no key is invented.
    in.fireLabel = "";
    p = Console::Present(in);
    CHECK(p.actionable && LineIs(p, Console::kStyleBinding, "Fire"));
    in.fireLabel = 0;
    p = Console::Present(in);
    CHECK(p.actionable && LineIs(p, Console::kStyleBinding, "Fire"));
    in.fireLabel = "  \t ";
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.actionable && LineIs(p, Console::kStyleBinding, "Fire"));

    // A selector result from another aim does not earn a cue for this one.
    const Controls::Output onNext = r.out;
    RigAim(&r, Controls::kRegionPlay);
    in = Scene(r, kTitles, "Mouse 1");
    in.focus = Console::Pick(r.mount, r.view);
    in.controls = onNext;
    CHECK(in.focus.region == Controls::kRegionPlay);
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.visible && !p.actionable);
    CHECK(CountLines(p, Console::kStyleBinding) == 0u && CountLines(p, Console::kStyleHint) == 0u);

    // Looking away clears everything at once.
    RigAimAway(&r);
    RigStep(&r, false);
    in = Scene(r, kTitles, "Mouse 1");
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(!p.visible && !p.actionable);

    // One film: Previous and Next say so and offer nothing; Play still plays.
    Rig one;
    RigInit(&one, 1u);
    RigAim(&one, Controls::kRegionNext);
    RigStep(&one, false);
    in = Scene(one, kTitles, "Mouse 1");
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.visible && !p.actionable);
    CHECK(LineIs(p, Console::kStyleFilmCount, "Film 1 of 1"));
    CHECK(LineIs(p, Console::kStyleTitle, "Opening Titles"));
    CHECK(LineIs(p, Console::kStyleHint, "This is the only film"));
    CHECK(CountLines(p, Console::kStyleBinding) == 0u);
    CHECK(CountRects(p, Console::kStyleKeyDisabled) == 2u);
    display = Console::MakeDisplay(one.hit, one.out, one.films, one.decoder, one.playing);
    CHECK(display.usable && !display.multipleFilms);
    RigAim(&one, Controls::kRegionPlay);
    RigStep(&one, false);
    in = Scene(one, kTitles, "Mouse 1");
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.actionable && LineIs(p, Console::kStyleAction, "Play this film"));
    chip = ChipOf(p, Controls::kRegionPlay);
    CHECK(chip != 0 && chip->style == Console::kStyleKeyFocus);

    // No films: said plainly, nothing offered.
    Rig empty;
    RigInit(&empty, 0u);
    RigAim(&empty, Controls::kRegionPlay);
    RigStep(&empty, false);
    in = Scene(empty, 0, "Mouse 1");
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.visible && !p.actionable);
    CHECK(LineIs(p, Console::kStyleFilmCount, "No films"));
    CHECK(LineIs(p, Console::kStyleStatus, "No films are installed"));
    CHECK(CountLines(p, Console::kStyleBinding) == 0u && CountLines(p, Console::kStyleTitle) == 0u);
    CHECK(CountRects(p, Console::kStyleKeyDisabled) == 3u);
    display = Console::MakeDisplay(empty.hit, empty.out, empty.films, empty.decoder, empty.playing);
    CHECK(!display.usable && display.lamp == Console::kLampIdle);

    // Films that cannot be shown: said plainly, nothing offered.
    Rig broken;
    RigInit(&broken, 3u);
    broken.decoder = Controls::kDecoderUnavailable;
    RigAim(&broken, Controls::kRegionPlay);
    RigStep(&broken, false);
    in = Scene(broken, kTitles, "Mouse 1");
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.visible && !p.actionable);
    CHECK(LineIs(p, Console::kStyleWarning, "Films cannot be shown here"));
    CHECK(LineIs(p, Console::kStyleTitle, "Opening Titles"));
    CHECK(CountLines(p, Console::kStyleBinding) == 0u);
    CHECK(CountRects(p, Console::kStyleKeyDisabled) == 3u);
    display = Console::MakeDisplay(broken.hit, broken.out, broken.films, broken.decoder, broken.playing);
    CHECK(!display.usable && display.lamp == Console::kLampUnavailable);
}

void TestLongTextStaysInsideThePanel() {
    Rig r;
    RigInit(&r, 3u);
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, false);

    // A long title breaks between words onto a second line.
    Console::PresentInput in = Scene(r, kTitles, "Mouse 1");
    in.title = "The Long Night Before The Last Assault On The Northern Islands Directors Assembly Cut";
    Console::Presentation p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(CountLines(p, Console::kStyleTitle) == 2u);
    CHECK(LineIs(p, Console::kStyleTitle, "The Long Night Before The Last Assault On The"));
    CHECK(LineIs(p, Console::kStyleTitle, "Northern Islands Directors Assembly Cut"));
    CHECK(p.actionable && LineIs(p, Console::kStyleAction, "Play this film"));

    // One with nowhere to break is split, and what is left over is cut openly.
    char unbroken[301];
    std::memset(unbroken, 'W', 300);
    unbroken[300] = '\0';
    in.title = unbroken;
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(CountLines(p, Console::kStyleTitle) == 2u);
    const Console::TextLine *first = LineOf(p, Console::kStyleTitle, 0u);
    const Console::TextLine *second = LineOf(p, Console::kStyleTitle, 1u);
    CHECK(first != 0 && !first->truncated && std::strlen(first->text) == 49u);
    CHECK(second != 0 && second->truncated && EndsWith(second->text, "..."));
    CHECK(first != 0 && second != 0 && second->y >= first->y + first->height - 0.02f);

    // A long binding is cut openly and the action beside it stays whole.
    in = Scene(r, kTitles, "Left Mouse Button or Right Trigger on the Second Connected Controller Device");
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.actionable);
    const Console::TextLine *binding = LineOf(p, Console::kStyleBinding, 0u);
    const Console::TextLine *action = LineOf(p, Console::kStyleAction, 0u);
    CHECK(binding != 0 && binding->truncated && EndsWith(binding->text, "..."));
    CHECK(binding != 0 && std::strncmp(binding->text, "Left Mouse", 10) == 0);
    CHECK(action != 0 && !action->truncated && std::strcmp(action->text, "Play this film") == 0);
    CHECK(binding != 0 && action != 0 && action->x >= binding->x + binding->width);

    // Both long, another film on the screen with a long name, a wide tall font.
    in.title = unbroken;
    in.decoder = Controls::kDecoderPlaying;
    in.playing = 2;
    in.playingTitle = in.title;
    in.lineHeight = 20.0f;
    in.measure = MeasureWide;
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureWide);
    CHECK(p.visible);
    CHECK(CountLines(p, Console::kStyleTitle) >= 1u);

    // A font taller than the panel allows is held to the tallest it lays out.
    in = Scene(r, kTitles, "Mouse 1");
    in.lineHeight = 100.0f;
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.visible && p.lineCount >= 1u && p.lines[0].height <= 28.0f + 0.02f);

    // A smaller box of the caller's own: rows drop out, nothing spills.
    in = Scene(r, kTitles, "Mouse 1");
    in.bounds.x = 20.0f; in.bounds.y = 300.0f; in.bounds.w = 200.0f; in.bounds.h = 60.0f;
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.visible && Near(p.panel.x, 20.0f, 0.02f) && Near(p.panel.y, 300.0f, 0.02f));
    CHECK(LineIs(p, Console::kStyleTitle, "Opening Titles"));
    CHECK(p.actionable && LineIs(p, Console::kStyleBinding, "Mouse 1"));

    // Bounds that are not a usable box on the viewport fall back to the default.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Console::Frame odd[4] = {{600.0f, 400.0f, 200.0f, 200.0f}, {nan, 28.0f, 360.0f, 108.0f},
                                   {-50.0f, 28.0f, 360.0f, 108.0f}, {10.0f, 10.0f, 50.0f, 10.0f}};
    for (unsigned i = 0; i < 4u; ++i) {
        in = Scene(r, kTitles, "Mouse 1");
        in.bounds = odd[i];
        p = Console::Present(in);
        CheckLayout(p, Console::DefaultBounds(), MeasureSeven);
        CHECK(p.visible && Near(p.panel.x, Console::DefaultBounds().x, 0.02f));
    }
}

void TestHostileInputIsBounded() {
    Rig r;
    RigInit(&r, 3u);
    RigAim(&r, Controls::kRegionPlay);
    RigStep(&r, false);

    // No measure, or one that answers nonsense: a fixed advance is used instead.
    Console::PresentInput in = Scene(r, kTitles, "Mouse 1");
    in.measure = 0;
    Console::Presentation p = Console::Present(in);
    CheckLayout(p, in.bounds, 0);
    CHECK(p.visible && p.actionable && LineIs(p, Console::kStyleTitle, "Opening Titles"));
    in.measure = MeasureNaN;
    p = Console::Present(in);
    CheckLayout(p, in.bounds, 0);
    CHECK(p.visible && LineIs(p, Console::kStyleTitle, "Opening Titles"));
    in.measure = MeasureNegative;
    p = Console::Present(in);
    CheckLayout(p, in.bounds, 0);
    CHECK(p.visible && LineIs(p, Console::kStyleTitle, "Opening Titles"));

    // A line height that is not one.
    const float heights[3] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, -5.0f};
    for (unsigned i = 0; i < 3u; ++i) {
        in = Scene(r, kTitles, "Mouse 1");
        in.lineHeight = heights[i];
        p = Console::Present(in);
        CheckLayout(p, in.bounds, MeasureSeven);
        CHECK(p.visible && p.lineCount >= 1u && Near(p.lines[0].height, 12.0f, 0.02f));
    }

    // Control characters and stray spacing in caller text.
    in = Scene(r, kTitles, "Mouse\n1");
    in.title = "  Dawn\tPatrol\n\x01  ";
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(LineIs(p, Console::kStyleTitle, "Dawn Patrol"));
    CHECK(LineIs(p, Console::kStyleBinding, "Mouse 1"));

    // Multi-byte text is never cut inside a character.
    char accented[301];
    for (unsigned i = 0; i < 150u; ++i) {
        accented[2u * i] = static_cast<char>(static_cast<unsigned char>(0xC3u));
        accented[2u * i + 1u] = static_cast<char>(static_cast<unsigned char>(0xA9u));
    }
    accented[300] = '\0';
    in = Scene(r, kTitles, accented);
    in.title = accented;
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(CountLines(p, Console::kStyleTitle) == 2u);
    CHECK(p.actionable && CountLines(p, Console::kStyleBinding) == 1u);

    // Nothing but the defaults: nothing drawn.
    p = Console::Present(Console::MakePresentInput());
    CheckLayout(p, Console::DefaultBounds(), 0);
    CHECK(!p.visible);

    // A focus with no selector result behind it draws state and offers nothing.
    in = Console::MakePresentInput();
    in.focus = r.hit;
    in.films = 3u;
    in.fireLabel = "Mouse 1";
    in.measure = MeasureSeven;
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(p.visible && !p.actionable);
    CHECK(LineIs(p, Console::kStyleFilmCount, "3 films"));
    CHECK(CountLines(p, Console::kStyleBinding) == 0u);
    CHECK(CountRects(p, Console::kStyleKeyDisabled) == 3u);

    // A very large list still prints its count.
    in = Scene(r, kTitles, "Mouse 1");
    in.films = 4000000000u;
    p = Console::Present(in);
    CheckLayout(p, in.bounds, MeasureSeven);
    CHECK(LineIs(p, Console::kStyleFilmCount, "Film 1 of 4000000000"));
}

struct Test {
    const char *name;
    void (*run)();
};

} // namespace

int main() {
    const Test tests[] = {
        {"mount validation", TestMountValidation},
        {"layout matches controller", TestLayoutMatchesController},
        {"faces are picked where drawn", TestFacesArePickedWhereDrawn},
        {"display changes looks not faces", TestDisplayChangesLooksNotFaces},
        {"pick limits", TestPickLimits},
        {"suspended and away pick nothing", TestSuspendedAndAwayPickNothing},
        {"select wraps and carries an index", TestSelectWrapsAndCarriesAnIndex},
        {"play and stop carry the chosen index", TestPlayAndStopCarryTheChosenIndex},
        {"held fire on arrival does nothing", TestHeldFireOnArrivalDoesNothing},
        {"owned press is consumed to its release", TestOwnedPressIsConsumedToItsRelease},
        {"fire off the console passes through", TestFireOffTheConsolePassesThrough},
        {"crossing controls spoils the press", TestCrossingControlsSpoilsThePress},
        {"suspension spoils the press", TestSuspensionSpoilsThePress},
        {"leaving stops and returning keeps the choice", TestLeavingStopsAndReturningKeepsTheChoice},
        {"resolve command checks the index", TestResolveCommandChecksTheIndex},
        {"shows stop matches the prompt", TestShowsStopMatchesThePrompt},
        {"panel names the chosen and the shown film", TestPanelNamesTheChosenAndTheShownFilm},
        {"cue only for a real usable focus", TestCueOnlyForARealUsableFocus},
        {"long text stays inside the panel", TestLongTextStaysInsideThePanel},
        {"hostile input is bounded", TestHostileInputIsBounded},
    };
    const unsigned total = static_cast<unsigned>(sizeof(tests) / sizeof(tests[0]));
    unsigned failed = 0;
    for (unsigned i = 0; i < total; ++i) {
        g_name = tests[i].name;
        const int before = g_failures;
        tests[i].run();
        const bool ok = g_failures == before;
        if (!ok) ++failed;
        std::printf("%s %s\n", ok ? "ok    " : "FAILED", tests[i].name);
    }
    std::printf("%u tests, %u failed; %d checks, %d failed\n", total, failed, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
