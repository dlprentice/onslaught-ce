#ifndef CE_MUSEUM_EXHIBITION_HOST_H
#define CE_MUSEUM_EXHIBITION_HOST_H

#include "ce_museum_exhibition.h"
#include "ce_museum_world.h"
#include "ce_museum_cinema.h"
#include "ce_museum_hall.h"

// The authored map and the exhibition use the same horizontal axes. Keep the
// origin conversion here so placement, visible geometry and visitor queries
// cannot drift apart.
namespace CeMuseumExhibitionHost {

inline CeMuseumExhibition::RoomSpec Room() {
    CeMuseumExhibition::RoomSpec room = {};
    room.minX = -CeMuseumWorld::kHalfWidth;
    room.maxX = CeMuseumWorld::kHalfWidth;
    room.minY = CeMuseumWorld::kFront;
    room.maxY = CeMuseumWorld::kBack;
    room.entrance = {0.0f, 0.0f};
    room.portalSide = CeCinemaRoom::kDoorX < 0.0f ? -1 : 1;
    room.portalMinY = CeCinemaRoom::kDoorY - CeCinemaRoom::kDoorHalf;
    room.portalMaxY = CeCinemaRoom::kDoorY + CeCinemaRoom::kDoorHalf;
    return room;
}

inline bool Build(const CeMuseumExhibition::DisplayInput* input, unsigned count,
                  CeMuseumExhibition::Plan* plan) {
    return CeMuseumExhibition::MakePlan(Room(), input, count, plan);
}

inline CeMuseumExhibition::Vec2 Local(float x, float y) {
    return {x - CeMuseumWorld::kEntranceX, y - CeMuseumWorld::kEntranceY};
}

inline CeHallPoint World(CeMuseumExhibition::Vec2 point) {
    return {point.x + CeMuseumWorld::kEntranceX,
            point.y + CeMuseumWorld::kEntranceY};
}

inline const CeMuseumExhibition::Bay* InputBay(const CeMuseumExhibition::Plan& plan,
                                             unsigned input) {
    if (!plan.valid) return 0;
    for (unsigned i = 0; i < plan.bayCount; ++i)
        if (plan.bays[i].input == input) return &plan.bays[i];
    return 0;
}

inline CeMuseumExhibition::Bay* InputBay(CeMuseumExhibition::Plan& plan, unsigned input) {
    if (!plan.valid) return 0;
    for (unsigned i = 0; i < plan.bayCount; ++i)
        if (plan.bays[i].input == input) return &plan.bays[i];
    return 0;
}

inline bool Plinth(const CeMuseumExhibition::Plan& plan, unsigned input, unsigned count,
                   float* x, float* y) {
    const CeMuseumExhibition::Bay* bay = InputBay(plan, input);
    if (!bay || !x || !y || count != plan.bayCount) return false;
    const CeHallPoint point = World({bay->model.x, bay->model.y});
    *x = point.x;
    *y = point.y;
    return true;
}

inline bool Tour(const CeMuseumExhibition::Plan& plan, CeHallTour* tour) {
    if (!tour || !plan.valid || !plan.bayCount) return false;
    *tour = CeHallTour{};
    tour->entrance = World(plan.room.entrance);
    tour->right = {1.0f, 0.0f};
    tour->forward = {0.0f, 1.0f};
    const unsigned capacity = sizeof(tour->stop) / sizeof(tour->stop[0]);
    tour->stops = plan.bayCount < capacity ? plan.bayCount : capacity;
    for (unsigned i = 0; i < tour->stops; ++i) {
        const CeMuseumExhibition::Bay& bay = plan.bays[i];
        const CeHallPoint model = World({bay.model.x, bay.model.y});
        tour->stop[i] = World(bay.pad);
        tour->display[i] = {model.x, model.y, bay.radius, bay.stableLabel};
        tour->exhibit[i] = i;
    }
    tour->annexEntry = World({CeCinemaRoom::kDoorX, CeCinemaRoom::kDoorY});
    tour->annexCorner = tour->annexEntry;
    for (unsigned i = 0; i < plan.routeCount; ++i) {
        if (plan.route[i].kind == CeMuseumExhibition::kRouteCinema) {
            tour->annexCorner = World(plan.route[i].a);
            break;
        }
    }
    return true;
}

// A small object (a soldier) is shown enlarged, by a whole factor that brings it to about
// kShownHeight, so it reads from its sign as a machine does; it then plans as a machine on an
// ordinary plinth. Its enlarged radius stays within kShownRadius, so a flat, wide small object
// never grows past what the hall takes. 1 for anything the planner does not count as small.
constexpr float kShownHeight = 0.90f, kShownRadius = 1.20f;
constexpr unsigned kEnlargeMin = 2, kEnlargeMax = 8;
inline unsigned EnlargeFactor(float radius, float height) {
    if (!(height > 0.0f) || !CeMuseumExhibition::IsSmall(radius, height)) return 1;
    const float want = kShownHeight / height + 0.5f;
    unsigned factor = want >= float(kEnlargeMax) ? kEnlargeMax : unsigned(want);
    while (factor > kEnlargeMin && radius * float(factor) > kShownRadius) --factor;
    return factor < kEnlargeMin ? kEnlargeMin : factor;
}

// The render scale of one exhibit: its factor while a visit is open and the thing shown is one of
// the visit's own enlarged exhibits (by its definition), 1 for anything else, so ordinary play and
// every other thing keep their size.
inline float ExhibitScale(bool visit, const void* definition, const void* const* enlarged,
                          const float* factors, unsigned count) {
    if (!visit || !definition || !enlarged || !factors) return 1.0f;
    for (unsigned i = 0; i < count; ++i)
        if (enlarged[i] == definition) return factors[i] >= 1.0f ? factors[i] : 1.0f;
    return 1.0f;
}

// A model aircraft's display: the machine stands lift above its plinth on a slim post, so it can
// be shown climbing and banked. The post is the stand's material, wholly inside the plinth's top;
// the bay's model and look-at points rise with it. False when the plan has no such bay or no room.
constexpr float kPostHalf = 0.06f;
inline bool RaiseOnPost(CeMuseumExhibition::Plan& plan, unsigned input, float lift) {
    CeMuseumExhibition::Bay* bay = InputBay(plan, input);
    if (!bay || !(lift > 0.0f) || lift > 2.0f || plan.boxCount >= unsigned(CeMuseumExhibition::kMaxBoxes)) return false;
    CeMuseumExhibition::Box& post = plan.boxes[plan.boxCount++];
    post.min = CeMuseumExhibition::Vec3{bay->model.x - kPostHalf, bay->model.y - kPostHalf, bay->model.z};
    post.max = CeMuseumExhibition::Vec3{bay->model.x + kPostHalf, bay->model.y + kPostHalf, bay->model.z + lift};
    post.colour = CeMuseumExhibition::kColourStand;
    post.purpose = CeMuseumExhibition::kSolidStand;
    post.bay = bay->number;
    bay->model.z += lift;
    bay->lookAt.z += lift;
    return true;
}

} // namespace CeMuseumExhibitionHost
#endif
