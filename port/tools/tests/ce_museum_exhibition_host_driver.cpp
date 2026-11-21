#include "../../game/ce_museum_exhibition_host.h"
#include <cmath>
#include <cstdio>

static unsigned checks = 0, failures = 0;
static void Check(bool result, const char* label) {
    ++checks;
    if (!result) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label);
    }
}
static bool Near(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main() {
    using namespace CeMuseumExhibition;
    namespace Host = CeMuseumExhibitionHost;
    // Dissimilar machines up to the largest radius the authored hall takes (2.7).
    const DisplayInput measured[] = {{71, 2.0f, 4.0f}, {19, 2.6f, 8.0f},
                                     {63, 1.5f, 6.0f}, {24, 2.3f, 5.0f}};
    Plan plan = {};
    Check(Host::Build(measured, 4, &plan), "host room accepts measured exhibition");
    Check(plan.valid && plan.bayCount == 4, "all measured exhibits remain present");
    if (!plan.valid) {
        std::printf("ce_museum_exhibition_host_driver: %u checks, %u failed\n", checks, failures);
        return 1;
    }
    const RoomSpec room = Host::Room();
    Check(Near(room.portalMinY, CeCinemaRoom::kDoorY - CeCinemaRoom::kDoorHalf), "portal begins at cinema opening");
    Check(Near(room.portalMaxY, CeCinemaRoom::kDoorY + CeCinemaRoom::kDoorHalf), "portal ends at cinema opening");
    const Vec2 cinemaDoor = {CeCinemaRoom::kDoorX, CeCinemaRoom::kDoorY};
    const Walk doorway = WalkAt(plan, cinemaDoor);
    Check(doorway == kWalkOpen || doorway == kWalkThroat, "native cinema doorway remains open in the exhibition plan");
    Check(WalkAt(plan, plan.room.entrance) == kWalkOpen, "ordinary entry stays walkable");
    for (unsigned i = 0; i < 4; ++i) {
        const Bay* bay = Host::InputBay(plan, i);
        Check(bay != 0, "input identity has one placed bay");
        if (!bay) continue;
        Check(bay->stableLabel == measured[i].stableLabel, "stable label survives packing");
        Check(Near(bay->radius, measured[i].radius), "measured footprint is not reduced");
        Check(Near(bay->height, measured[i].height), "measured height is not reduced");
        float x = 0.0f, y = 0.0f;
        Check(Host::Plinth(plan, i, 4, &x, &y), "native placement consumes planned model centre");
        const Vec2 local = Host::Local(x, y);
        Check(Near(local.x, bay->model.x) && Near(local.y, bay->model.y), "placement and rendering use identical axes");
        Check(CeMuseumWorld::Inside(x, y), "placed exhibit remains inside authored map");
        Check(WalkAt(plan, local) == kWalkBlocked, "rendered plinth blocks visitor centre");
        Check(WalkAt(plan, bay->pad) == kWalkOpen, "viewing pad remains usable");
        const CeHallPoint worldPad = Host::World(bay->pad);
        const Vec2 localPad = Host::Local(worldPad.x, worldPad.y);
        Check(Near(localPad.x, bay->pad.x) && Near(localPad.y, bay->pad.y), "visitor coordinates round trip");
        Check(LabelForBay(plan, bay->number) == measured[i].stableLabel, "numbered bay resolves native label identity");
    }
    for (unsigned i = 0; i < plan.routeCount; ++i)
        Check(PathClear(plan, plan.route[i].a, plan.route[i].b), "authored visitor route clears final geometry");
    CeHallTour tour = {};
    Check(Host::Tour(plan, &tour), "cinema host tour is derived from the same plan");
    Check(Near(tour.right.x, 1.0f) && Near(tour.right.y, 0.0f), "cinema lateral axis remains fixed");
    Check(Near(tour.forward.x, 0.0f) && Near(tour.forward.y, 1.0f), "cinema depth axis remains fixed");
    const Vec2 entrance = Host::Local(tour.entrance.x, tour.entrance.y);
    Check(Near(entrance.x, room.entrance.x) && Near(entrance.y, room.entrance.y), "native tour entry shares room origin");
    const Vec2 annex = Host::Local(tour.annexEntry.x, tour.annexEntry.y);
    Check(Near(annex.x, cinemaDoor.x) && Near(annex.y, cinemaDoor.y), "theater entry shares the existing cinema doorway");
    const Vec2 corner = Host::Local(tour.annexCorner.x, tour.annexCorner.y);
    Check(PathClear(plan, corner, cinemaDoor), "tour connects the cinema route to its physical doorway");
    for (unsigned i = 0; i < tour.stops; ++i) {
        const Bay& bay = plan.bays[i];
        const Vec2 stop = Host::Local(tour.stop[i].x, tour.stop[i].y);
        Check(Near(stop.x, bay.pad.x) && Near(stop.y, bay.pad.y), "tour stop is the viewing pad");
        Check(tour.display[i].label == bay.stableLabel, "tour label shares packed bay identity");
    }
    float x = 123.0f, y = 456.0f;
    Check(!Host::Plinth(plan, 0, 3, &x, &y), "mismatched native count is refused");
    Check(Near(x, 123.0f) && Near(y, 456.0f), "refused placement leaves coordinates untouched");
    Check(!Host::Plinth(plan, 4, 4, &x, &y), "unknown input cannot select another exhibit");
    Check(!Host::Plinth(plan, 0, 4, 0, &y), "missing coordinate output refused");
    Plan invalid = {};
    Check(Host::InputBay(invalid, 0) == 0, "invalid plan has no placeable bay");
    Check(!Host::Tour(invalid, &tour), "invalid plan cannot activate native hall");
    Check(!Host::Tour(plan, 0), "missing native tour refused");
    Bay* relabel = Host::InputBay(plan, 2);
    Check(relabel != 0, "native label registration finds original input");
    if (relabel) {
        relabel->stableLabel = 91;
        Check(LabelForBay(plan, relabel->number) == 91, "registered label reaches visitor focus");
        Check(Host::Tour(plan, &tour), "relabelled plan retains native tour");
    }

    // A small object is shown enlarged by a whole factor to about 0.9 m; a machine never is.
    Check(Host::EnlargeFactor(0.14f, 0.3f) == 3, "the trooper is shown three times its size");
    Check(Host::EnlargeFactor(0.14f, 0.29f) == 3 && Host::EnlargeFactor(0.14f, 0.31f) == 3, "a hair either side keeps the factor");
    Check(Host::EnlargeFactor(0.05f, 0.05f) == Host::kEnlargeMax, "a speck is capped");
    Check(Host::EnlargeFactor(0.3f, 0.49f) == Host::kEnlargeMin, "a near-machine is at least doubled");
    Check(Host::EnlargeFactor(1.49f, 0.4f) == 1 && Host::EnlargeFactor(1.29f, 0.7f) == 1 &&
          Host::EnlargeFactor(0.3f, 3.8f) == 1, "machines keep their size");
    {
        const unsigned f = Host::EnlargeFactor(0.14f, 0.3f);
        Check(!IsSmall(0.14f * float(f), 0.3f * float(f)), "enlarged, it plans as a machine on an ordinary plinth");
    }
    Check(Host::EnlargeFactor(0.45f, 0.12f) == 2, "a flat, wide small object grows only as far as the hall takes");
    // Over the whole small domain an enlarged display is never refused: alone among machines, and six together.
    {
        bool planned = true;
        for (float r = 0.05f; r < kSmallRadius; r += 0.04f)
            for (float h = 0.05f; h < kSmallHeight; h += 0.04f) {
                if (!IsSmall(r, h)) continue;
                const float f = float(Host::EnlargeFactor(r, h));
                const DisplayInput one[] = {{0, 1.97f, 2.3f}, {1, 1.29f, 0.7f}, {2, 1.49f, 0.4f},
                                            {3, 1.75f, 1.0f}, {4, r * f, h * f}, {5, 2.22f, 1.4f}};
                DisplayInput six[6];
                for (int i = 0; i < 6; ++i) six[i] = DisplayInput{i, r * f, h * f};
                Plan a = {}, b = {};
                planned = planned && Host::Build(one, 6, &a) && Host::Build(six, 6, &b);
            }
        Check(planned, "every enlarged small display plans, among machines and six together");
    }
    // The render scale acts only inside a visit and only on the visit's own enlarged exhibits.
    int trooper = 0, tank = 0, ordinary = 0;
    const void* enlarged[] = {&trooper};
    const float factors[] = {3.0f};
    Check(Host::ExhibitScale(true, &trooper, enlarged, factors, 1) == 3.0f, "the enlarged exhibit is scaled in a visit");
    Check(Host::ExhibitScale(false, &trooper, enlarged, factors, 1) == 1.0f, "outside a visit nothing is scaled");
    Check(Host::ExhibitScale(true, &tank, enlarged, factors, 1) == 1.0f, "another exhibit keeps its size");
    Check(Host::ExhibitScale(true, &ordinary, enlarged, factors, 0) == 1.0f, "with nothing registered nothing is scaled");
    Check(Host::ExhibitScale(true, 0, enlarged, factors, 1) == 1.0f, "a thing with no definition keeps its size");
    const float shrink[] = {0.5f};
    Check(Host::ExhibitScale(true, &trooper, enlarged, shrink, 1) == 1.0f, "nothing is ever shrunk");

    // A model aircraft's post: inside the plinth's top, from it to the raised model, clear of everything else.
    const DisplayInput hall[] = {{0, 1.97f, 2.3f}, {1, 1.29f, 0.7f}, {2, 1.49f, 0.4f}};
    Plan raised = {};
    Check(Host::Build(hall, 3, &raised), "a three-machine hall plans");
    const Bay* before = Host::InputBay(raised, 2);
    const float top = before ? before->model.z : 0.0f;
    const unsigned boxes = raised.boxCount;
    Check(Host::RaiseOnPost(raised, 2, 0.45f), "the aircraft is raised on a post");
    const Bay* air = Host::InputBay(raised, 2);
    if (air && raised.boxCount == boxes + 1) {
        const Box& post = raised.boxes[boxes];
        Check(Near(air->model.z, top + 0.45f) && Near(post.min.z, top) && Near(post.max.z, air->model.z),
              "the post runs from the plinth's top to the model");
        Check(post.purpose == kSolidStand && post.bay == air->number, "the post is the bay's stand material");
        Check(post.min.x >= air->model.x - air->plinthHalf && post.max.x <= air->model.x + air->plinthHalf &&
              post.min.y >= air->model.y - air->plinthHalf && post.max.y <= air->model.y + air->plinthHalf,
              "the post stands inside the plinth's top");
        bool clear = true;
        for (unsigned i = 0; i < boxes; ++i) clear = clear && !detail::Overlap(post, raised.boxes[i]);
        Check(clear, "the post meets nothing else");
        Check(WalkAt(raised, air->pad) == kWalkOpen, "the pad stays open");
    } else {
        Check(false, "the post was added");
    }
    Check(!Host::RaiseOnPost(raised, 7, 0.45f) && !Host::RaiseOnPost(raised, 2, 0.0f) && !Host::RaiseOnPost(raised, 2, 3.0f),
          "a missing bay, no lift or a tower is refused");
    std::printf("ce_museum_exhibition_host_driver: %u checks, %u failed\n", checks, failures);
    return failures ? 1 : 0;
}
