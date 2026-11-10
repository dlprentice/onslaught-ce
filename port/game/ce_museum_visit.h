// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_MUSEUM_VISIT_H
#define CE_MUSEUM_VISIT_H

// The explorable room is separate from the read-only Goodies gallery.
// Begin pins installed resources before leaving the frontend. The room is
// generated from their loaded definitions at the world-reader boundary.
// Requested visits remain active through world teardown and the frontend's
// progression decision. Only Cancel/TakeReturn end the career exclusion.
// Begin does not launch: after true, the menu calls FRONTEND.SetQuit(Level())
// once and leaves. After false it shows Message() without altering the session.
extern "C" bool PortCeMuseumVisitBegin(void);
extern "C" bool PortCeMuseumVisitActive(void);
extern "C" int PortCeMuseumVisitLevel(void);
extern "C" const char *PortCeMuseumVisitMessage(void);
extern "C" bool PortCeMuseumVisitAfterLoad(void);
extern "C" void PortCeMuseumVisitLevelEnd(void);
extern "C" void PortCeMuseumVisitCancel(void);
extern "C" bool PortCeMuseumVisitTakeReturn(void);

// Tri-state, at the world-reader boundary: 0 ordinary play, 1 an immutable
// decoded body, -1 a refused visit. Never fall back to a campaign body after
// a refusal. The caller copies a successful span into its owned reader before
// parsing it; this span remains valid until Cancel/TakeReturn.
extern "C" int PortCeMuseumVisitReadWorld(const char *gamePath,
                                        const void **bytes, unsigned *length);

// Internal provider boundary. The engine adapter checks mode exclusivity,
// prepares data, captures the career, then activates. Clear follows restoration.
// The staged proof reads museum/room.cache below the user folder: an exact
// "Onslaught CE museum cache 1" line, then "carrier <id> <sha256>",
// "base <id> <sha256> <decoded-byte-count>" and the corresponding "real" row.
// It pins the installed carrier archive and museum/worldNNN.body files. These
// are opaque decoded world bodies, never packed archives or distributed assets.
// The normal menu entry ignores this cache and generates from installed data.
namespace CeMuseumVisit {
// Explicit development route only; the player menu never selects prepared
// files or falls back to them when installed-resource generation fails.
bool BeginPreparedCacheForDevelopment();
bool Prepare();
bool PrepareInstalled(unsigned carrier);
bool Activate();
void Clear();
void Refuse(const char *message);
bool CheckInstalledSource();
bool AcceptInstalled(unsigned baseId, const void *base, unsigned baseLength,
                     const void *real, unsigned realLength);
bool GenerateInstalled();

// The engine resolves each definition against the installed carrier before
// calling this writer. Display labels are never model identities. This boundary
// carries no game-allocated objects or standard-library containers.
struct ExhibitPlacement {
    const char *definition;
    float position[3];
    float orientation[3];
};
struct RoomPlan {
    const char *playerConfiguration;
    int baseLevel;
    int mapNumber;
    float visitorPosition[3];
    float visitorOrientation[3];
    const ExhibitPlacement *exhibits;
    unsigned exhibitCount;
};
// Called only after both complete bodies have been built and validated. Spans
// live for this call only; the receiver must copy both before publishing either.
typedef bool (*RoomReceiver)(const void *base, unsigned baseLength,
                             const void *real, unsigned realLength, void *context);
bool GenerateRoom(const RoomPlan &plan, RoomReceiver receiver, void *context);

// Site planning: where on existing terrain the exhibition stands. Pure arithmetic over the callback's answers; no engine object, game
// allocation or standard-library object crosses. Footprints are the models' full extents (world units, width along X, depth along Y,
// height above the ground). The answer is the centre of each exhibit's cell on the ground, a standing point for the visitor in front
// of the first row, the height range of the ground the site covers (smaller is flatter), how many exhibits the visitor can see from
// the standing point, and how wide a ring of dry ground surrounds the site. A callback answer of false means the cell is wet or
// unusable. The planner prefers, in this order: a clear ring of dry ground around the site (48, then 24, then no cells), the most
// exhibits in view of the visitor, then the flattest ground; a site that covers a wet or unusable cell is never chosen.
struct ExhibitFootprint {
    float width, depth, height;
};
typedef bool (*GroundHeight)(int x, int y, float *height, void *context);
enum { kMaxSiteExhibits = 16 };
struct SitePlan {
    float visitor[3];
    float centres[kMaxSiteExhibits][3];
    float heightRange;
    unsigned visible;
    unsigned dryRing;
};
bool PlanSite(const ExhibitFootprint *footprints, unsigned count, int gridWidth, int gridHeight, GroundHeight ground, void *context,
              SitePlan *plan);

// Only the installed carrier's small header/context prefix is needed. This is
// deliberately not a campaign script or actor decoder. A base source must have
// no scripts before its map context; a real source stops before that section.
struct CarrierPrefix {
    char playerConfiguration[128];
    int parentWorld;
    int mapNumber;
};
typedef bool (*SourceReader)(void *bytes, unsigned length, void *context);
bool ReadCarrierPrefix(SourceReader reader, void *context, bool requireMap, CarrierPrefix *prefix);
}

// The room writer and the cache provider are host-buffer modules (the port's flags and the host allocator); the engine
// adapter is compiled with the game's flags and allocator renames. Every translation unit on both sides includes this
// header, so the compiler checks the layout the records above and the callbacks rely on under both rules, for both
// 32-bit x86 targets. Nothing that crosses is a game object, a standard-library object or a double (the game rule aligns
// doubles to eight bytes, the port rule does not), and the callbacks carry no calling-convention attribute.
namespace CeMuseumVisit {
namespace Contract {
template <class A, class B> struct SameType { enum { value = 0 }; };
template <class A> struct SameType<A, A> { enum { value = 1 }; };

static_assert(sizeof(char) == 1 && sizeof(bool) == 1 && sizeof(int) == 4 && sizeof(unsigned) == 4 && sizeof(float) == 4,
              "the boundary records assume these scalar widths on both sides");
static_assert(SameType<RoomReceiver, bool (*)(const void *, unsigned, const void *, unsigned, void *)>::value,
              "RoomReceiver changed: update the engine adapter's receiver and the writer together");
static_assert(SameType<GroundHeight, bool (*)(int, int, float *, void *)>::value,
              "GroundHeight changed: update the engine adapter's sampler and the site planner together");
static_assert(SameType<SourceReader, bool (*)(void *, unsigned, void *)>::value,
              "SourceReader changed: update the engine adapter's reader and the carrier-prefix reader together");
#if defined(__i386__) || defined(_M_IX86)
// 32-bit x86: four-byte pointers and alignment, no padding, the same offsets on both sides.
static_assert(sizeof(void *) == 4, "the shipped targets are 32-bit x86");
static_assert(sizeof(ExhibitPlacement) == 28 && alignof(ExhibitPlacement) == 4 &&
              __builtin_offsetof(ExhibitPlacement, definition) == 0 &&
              __builtin_offsetof(ExhibitPlacement, position) == 4 &&
              __builtin_offsetof(ExhibitPlacement, orientation) == 16,
              "ExhibitPlacement layout changed");
static_assert(sizeof(RoomPlan) == 44 && alignof(RoomPlan) == 4 &&
              __builtin_offsetof(RoomPlan, playerConfiguration) == 0 &&
              __builtin_offsetof(RoomPlan, baseLevel) == 4 &&
              __builtin_offsetof(RoomPlan, mapNumber) == 8 &&
              __builtin_offsetof(RoomPlan, visitorPosition) == 12 &&
              __builtin_offsetof(RoomPlan, visitorOrientation) == 24 &&
              __builtin_offsetof(RoomPlan, exhibits) == 36 &&
              __builtin_offsetof(RoomPlan, exhibitCount) == 40,
              "RoomPlan layout changed");
static_assert(sizeof(ExhibitFootprint) == 12 && alignof(ExhibitFootprint) == 4 &&
              __builtin_offsetof(ExhibitFootprint, width) == 0 && __builtin_offsetof(ExhibitFootprint, depth) == 4 &&
              __builtin_offsetof(ExhibitFootprint, height) == 8,
              "ExhibitFootprint layout changed");
static_assert(sizeof(SitePlan) == 216 && alignof(SitePlan) == 4 &&
              __builtin_offsetof(SitePlan, visitor) == 0 && __builtin_offsetof(SitePlan, centres) == 12 &&
              __builtin_offsetof(SitePlan, heightRange) == 204 && __builtin_offsetof(SitePlan, visible) == 208 &&
              __builtin_offsetof(SitePlan, dryRing) == 212,
              "SitePlan layout changed");
static_assert(sizeof(CarrierPrefix) == 136 && alignof(CarrierPrefix) == 4 &&
              __builtin_offsetof(CarrierPrefix, playerConfiguration) == 0 &&
              __builtin_offsetof(CarrierPrefix, parentWorld) == 128 &&
              __builtin_offsetof(CarrierPrefix, mapNumber) == 132,
              "CarrierPrefix layout changed");
#endif
}
}

#endif
