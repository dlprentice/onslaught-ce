// SPDX-License-Identifier: GPL-3.0-only
#include "common.h"
#include "Career.h"
#include "frontend.h"
#include "ce_museum_visit.h"
#include "ce_museum.h"
#include "ce_project.h"
#include "ce_museum_label.h"
#include "ce_museum_hall.h"
#include "ce_museum_exhibition_host.h"
#include "ce_museum_visit_places.h"
#include "ce_museum_world.h"
#include "ce_museum_label_rules.h"
#include "ce_museum_options.h"
#include "../platform/common/portlog.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(CE_VERSION) && !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
#include "DXMemBuffer.h"
#include "world.h"
#include "map.h"
#include "mesh.h"
#include "WorldPhysicsManager.h"
#include "resourceaccumulator.h"
#include "game.h"
#include "player.h"
#include "BattleEngine.h"
#include "engine.h"
#include "eventmanager.h"
#include "ltshell.h"
#endif

bool PortCeMenus();
// Declared by the shared authoring-session change in ce_project.h as well.
bool PortCeAuthoringSessionAllowed() noexcept;
extern "C" bool PortCeEditorActive(void) __attribute__((weak));
extern "C" bool PortCeLivingSandboxActive(void) __attribute__((weak));
extern "C" bool PortCeCarrierClashActive(void) __attribute__((weak));
extern "C" bool PortCeFlagshipDuelActive(void) __attribute__((weak));

namespace {
unsigned char gMuseumCareer[sizeof(CCareer)];
bool gMuseumCareerSaved = false;

// The level whose resources the hall is built from: level 100 (the one hall; the Museum page's halls choose a carrier from a table).
unsigned Carrier() { return 100u; }

bool Begin(bool preparedCache) {
#ifndef CE_VERSION
    (void)preparedCache;
    CeMuseumVisit::Refuse("");
    return false;
#else
    // Check every mode before reading room files, copying the career or leaving
    // the frontend. In particular an editor visit must never lose its snapshot.
    if (PortCeMuseumVisitActive() || gMuseumCareerSaved || !PortCeMenus() || !FRONTEND.IsRunning() ||
        !PortCeAuthoringSessionAllowed() || PortCeMuseumActive() ||
        (PortCeEditorActive && PortCeEditorActive()) ||
        (PortCeLivingSandboxActive && PortCeLivingSandboxActive()) ||
        (PortCeCarrierClashActive && PortCeCarrierClashActive()) ||
        (PortCeFlagshipDuelActive && PortCeFlagshipDuelActive())) {
        CeMuseumVisit::Refuse("Close mods, online play and the Map Editor, then open the museum.");
        PortSessionLog(PORT_SESSION_GAME, false, "museum refused before loading: another mode is open");
        return false;
    }
    if (!(preparedCache ? CeMuseumVisit::Prepare() : CeMuseumVisit::PrepareInstalled(Carrier()))) {
        const char *why = PortCeMuseumVisitMessage();
        PortSessionLog(PORT_SESSION_GAME, true, "museum refused before loading: %s", why && *why ? why : "a visit is already opening");
        return false;
    }
    std::memcpy(gMuseumCareer, &CAREER, sizeof(CCareer));
    gMuseumCareerSaved = true;
    if (!CeMuseumVisit::Activate()) {
        PortCeMuseumVisitCancel();
        CeMuseumVisit::Refuse("The museum could not be opened. Your careers are unchanged.");
        PortSessionLog(PORT_SESSION_GAME, true, "museum refused before loading: the visit could not be activated");
        return false;
    }
    return true;
#endif
}
}

extern "C" bool PortCeMuseumVisitBegin(void) { return Begin(false); }
bool CeMuseumVisit::BeginPreparedCacheForDevelopment() { return Begin(true); }

#if defined(CE_VERSION) && !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
namespace {
void DropExhibitDefinitions();

bool gHaveStart;
bool gAuthoredMap;
int gCarrierBaseWorldLevel = -1;
float gMuseumSprintUntil = -1.0f;
// The visitor's place in the hall of numbered displays (ce_museum_visit_places.h): it tells the labels when a place's name at the top is left.
CeMuseumVisitPlaces gPlaces;
FVector gStartPosition;
FMatrix gStartOrientation;
}
extern "C" void PortCeMuseumSprintSignal(void) {
    // 20 Hz sim tick signal (50 ms); hold sprint across all render frames for 120 ms of sim time
    // so gamepad/non-Shift sprint never drops out at high framerates (>120 FPS).
    gMuseumSprintUntil = EVENT_MANAGER.GetTime() + 0.12f;
}
#else
extern "C" void PortCeMuseumSprintSignal(void) {}
#endif

#if defined(CE_VERSION) && !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
extern "C" bool PortCeMuseumAuthoredMap() {
    return PortCeMuseumAuthoredSource() && gAuthoredMap;
}
void CeMuseumVisit::ApplyAuthoredIdentity() {
    if (PortCeMuseumAuthoredMap()) {
        if (WORLD.mBaseWorldLevel >= 0 && WORLD.mBaseWorldLevel != CeMuseumWorld::kBase)
            gCarrierBaseWorldLevel = WORLD.mBaseWorldLevel;
        WORLD.mWorldLevel=CeMuseumWorld::kWorld;
        WORLD.mBaseWorldLevel=CeMuseumWorld::kBase;
    }
}
void CeMuseumVisit::ReleaseRuntimeResources() {
    // Release all Museum GPU, mesh, texture, Bink, and audio resources before CGame::Shutdown()
    // or Cleanup3DEnvironment(), while retaining gVisit.active for career/return routing.
    PortCeMuseumLabelsClear();
    CeMuseumHallClear();
    if (gAuthoredMap) { MAP.mGeometryLoaded=FALSE; MAP.mPropertiesLoaded=FALSE; gAuthoredMap=false; }
    if (gCarrierBaseWorldLevel >= 0) WORLD.mBaseWorldLevel = gCarrierBaseWorldLevel;
    gHaveStart = false;
    gMuseumSprintUntil = -1.0f;
}
extern "C" void PortCeMuseumAuthoredInstall() {
    if (!PortCeMuseumAuthoredSource()) return;
    // The native collision sampler uses 8-cell tiles with a ninth seam sample.
    // Release the previous height buffer through its normal owner before installing
    // our independently allocated surface. The packed terrain is never decoded.
    MAP.CHeightField::Shutdown();
    MAP.mHeightData=new(MEMTYPE_HEIGHTFIELD) SWORD[CeMuseumWorld::kHeightSamples];
    if (!MAP.mHeightData) { gAuthoredMap=false; return; }
    std::memset(MAP.mHeightData,0,CeMuseumWorld::kHeightSamples*sizeof(SWORD));
    MAP.mGridShiftX=MAP.mGridShiftY=CeMuseumWorld::kGridShift;
    MAP.mHeightScale=1.0f; MAP.mWaterLevel=CeMuseumWorld::kWaterBelow;
    SExtents ext = {};
    ext.mMinX = 0;
    ext.mMinY = 0;
    ext.mMaxX = 511;
    ext.mMaxY = 511;
    ext.mWaterLevel = MAP.mWaterLevel;
    static_assert(sizeof(ext) == sizeof(MAP.mUnknown000), "SExtents size mismatch");
    std::memcpy(MAP.mUnknown000, &ext, sizeof(ext));
    const UBYTE archiveSkyCube = MAP.mHFP.mUnknown18[0];
    std::memset(&MAP.mHFP,0,sizeof(MAP.mHFP));
    // Crisp sunlit coastal daylight (warm sun key, sky-blue fill, balanced neutral ambient)
    MAP.mHFP.Sun=0xfff6ecda; MAP.mHFP.AntiSun=0xff9ab8d4;
    MAP.mHFP.Ambient=0xff88909a; MAP.mHFP.Fog=CeMuseumWorld::kGrandSkyHorizon;
    MAP.mHFP.SunPosX=56.0f; MAP.mHFP.SunPosY=48.0f; MAP.mHFP.SunPosZ=-95.0f;
    MAP.mHFP.mUnknown18[0] = archiveSkyCube ? archiveSkyCube : 1;
    MAP.Unknown47e8e0();
    MAP.mMapNumber=CeMuseumWorld::kMap; MAP.mGeometryLoaded=TRUE; MAP.mPropertiesLoaded=TRUE;
    MAP.InitQuickCollisionMap(); gAuthoredMap=true;
}
#endif

extern "C" void PortCeMuseumVisitCancel(void) {
    // Retain Active while restoring, so every progression/save guard continues
    // to exclude this visit until the final frontend return decision is done.
    if (gMuseumCareerSaved) {
#if defined(CE_VERSION) && !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
        const CeMuseumKeptOptions kept = CeMuseumKeepOptions(CAREER);
#endif
        std::memcpy(&CAREER, gMuseumCareer, sizeof(CCareer));
#if defined(CE_VERSION) && !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
        CeMuseumRestoreOptions(CAREER, kept);
#endif
        gMuseumCareerSaved = false;
    }
    CeMuseumVisit::Clear();
#if defined(CE_VERSION) && !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
    PortCeMuseumLabelsClear();
    CeMuseumHallClear();
    if (gAuthoredMap) { MAP.mGeometryLoaded=FALSE; MAP.mPropertiesLoaded=FALSE; gAuthoredMap=false; }
    DropExhibitDefinitions();
    gCarrierBaseWorldLevel = -1;
    gHaveStart = false;
    gMuseumSprintUntil = -1.0f;
    CeMuseumVisitPlacesDrop(&gPlaces);
#endif
}

// Dispatched via ce_hooks.cpp save_gate (0313-ce-hook-save-gate.patch, after the bonus and editor filters): while a visit holds the player's career, a save of the live career writes the
// record the visit found, so nothing the visit changes reaches a file (ce_museum_options.h CeMuseumCareerToSave). Keyed on the snapshot, not on Active: Cancel keeps Active while it restores.
extern "C" void PortCeMuseumVisitFilterSave(CCareer *career, void *dest) {
#if defined(CE_VERSION) && !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
    if (career != &CAREER || !dest || !gMuseumCareerSaved) return;
    CCareer saved;
    std::memcpy((void *)&saved, gMuseumCareer, sizeof(saved));
    CeMuseumCareerToSave(saved, *career);
    std::memcpy(dest, (const void *)&saved, sizeof(saved));
#else
    (void)career; (void)dest;
#endif
}

extern "C" bool PortCeMuseumVisitTakeReturn(void) {
    if (!PortCeMuseumVisitActive()) return false;
    PortCeMuseumVisitCancel();
    CeMuseumVisit::MarkReturned();		// the Onslaught CE page opens on the museum's row (PortCeMuseumVisitTakeNotice)
    return true;
}

#if defined(CE_VERSION) && !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
namespace {
const unsigned kNativeExhibits = 6;
FVector gLastViewingPosition;
// A model larger than this does not stand in a room of this size (world units, the model's own bounding box).
const float kLargestFootprint = 48.0f, kLargestHeight = 64.0f;
struct SelectedExhibit {
    CFeatureData *definition;
    CMesh *mesh;
    unsigned ordinal;
    char name[48];        // what the label calls it
    char line[48];        // and the line under the name
    char detail[64];
    float yaw;            // how it stands, radians: Layout turns it three-quarters toward its viewing pad
    float footprint[2];   // the model's width (X) and depth (Y)
    float middle[2];      // the box's centre from the model's origin, on the ground plane
    float height;         // the model's extent along Z
    float lowestZ;        // the model's lowest point, Z counted downward as the world does
    unsigned enlarge;     // the whole factor it is shown at (1: its own size); the box above is already multiplied by it
    bool flies;           // an aircraft: shown on a post, climbing and banked, as a model aircraft is
};
// The visit's enlarged exhibits, by definition, and their factors: the only things PortCeMuseumFeatureScale scales, and only while the visit is open.
const void *gEnlarged[kNativeExhibits];
float gEnlargedBy[kNativeExhibits];
unsigned gEnlargedCount;
// Why the candidates were turned away: the only account of an empty or short exhibition a player's log can carry.
struct SelectionCounts {
    unsigned features, unnamed, repeatedName, notLoaded, noBox, tooLarge, repeatedModel, chosen, units;
};

bool ReadSource(void *bytes, unsigned length, void *context) {
    CDXMemBuffer &source = *static_cast<CDXMemBuffer *>(context);
    const UINT before = source.WhereAmI();
    source.Read(bytes, static_cast<SINT>(length));
    return source.WhereAmI() - before == length;
}

bool SourcePrefix(UINT offset, bool map, CeMuseumVisit::CarrierPrefix *prefix) {
    CDXMemBuffer source;
    if (!source.InitFromFile(CResourceAccumulator::GetResourceFileName(), MEMTYPE_MEMBUFFER, TRUE, offset)) return false;
    const bool ok = CeMuseumVisit::ReadCarrierPrefix(ReadSource, &source, map, prefix);
    source.Close();
    return ok;
}

bool Name(const char *name) {
    if (!name || !*name) return false;
    unsigned n = 0;
    while (n <= 127 && name[n]) {
        const unsigned char c = static_cast<unsigned char>(name[n]);
        if (c < 32 || c == 127) return false;
        ++n;
    }
    return n <= 127;
}

bool Finite(float value, float bound) { return value >= -bound && value <= bound; }

// The model's own bounding box (the engine computes it for every loaded mesh, whatever its parts or frames): a centre and the half
// lengths along the axes. Compact, finite models only; a model that cannot stand in the room is counted, never guessed at.
bool ModelBox(CMesh *mesh, SelectedExhibit *item, SelectionCounts *counts) {
    if (!mesh) { ++counts->notLoaded; return false; }
    const CBoundingBox *box = mesh->mBoundingBox;
    if (!box || !box->mValid) { ++counts->noBox; return false; }
    const float half[3] = {box->mAxes.X, box->mAxes.Y, box->mAxes.Z};
    const float centre[3] = {box->mOrigin.X, box->mOrigin.Y, box->mOrigin.Z};
    for (unsigned axis = 0; axis != 3; ++axis)
        if (!Finite(half[axis], 4096) || !Finite(centre[axis], 4096) || half[axis] <= 0) { ++counts->noBox; return false; }
    if (half[0] * 2 > kLargestFootprint || half[1] * 2 > kLargestFootprint || half[2] * 2 > kLargestHeight) { ++counts->tooLarge; return false; }
    item->footprint[0] = half[0] * 2; item->footprint[1] = half[1] * 2;
    item->middle[0] = centre[0]; item->middle[1] = centre[1];
    item->height = half[2] * 2;
    item->lowestZ = centre[2] + half[2];
    return true;
}

// The feature definitions CE made for a visit's exhibits ("ce.museum.<unit>") are in the game's one physics list, which is read once when the game starts and
// not again per level: they stay for a Retry (the world body asks for them again) and go when the visit ends, so the list is as it was.
void DropExhibitDefinitions() {
    gEnlargedCount = 0;
    if (!UPhysicsManager::mFeatures) return;
    for (SINT i = UPhysicsManager::mFeatures->Size() - 1; i >= 0; --i) {
        CFeatureData *item = static_cast<CFeatureData *>(UPhysicsManager::mFeatures->At(i));
        if (item && item->mName && !std::strncmp(item->mName, "ce.museum.", 10)) {
            UPhysicsManager::mFeatures->Remove(item);
            delete item;
        }
    }
}

// A definition of CE's own for one model ("ce.museum.<name>"), in the game's one physics list: a feature that nothing can damage and that dies of nothing, so a
// hall's exhibits stand whatever the visitor does. The game's own definitions are never changed (the list outlives the visit). NULL when the name is taken.
CFeatureData *MakeExhibitDefinition(const char *name, const char *mesh) {
    char definition[96];
    std::snprintf(definition, sizeof definition, "ce.museum.%s", name);
    if (UPhysicsManager::GetFeatureData(definition)) return NULL;
    UPhysicsManager::AddFeature(definition);
    CFeatureData *feature = static_cast<CFeatureData *>(UPhysicsManager::mFeatures->At(UPhysicsManager::mFeatures->Size() - 1));
    if (!feature) return NULL;
    feature->SetMesh(const_cast<char *>(mesh));
    feature->mInvincible = TRUE;
    feature->mLife = 1.0f;
    return feature;
}

// What kind of thing a unit definition is, in a few words: the game's own behaviour class, never a guess from the name.
const char *ClassLine(int behaviour) {
    switch (behaviour) {
    case UB_MECH: return "Walking machine";
    case UB_AIRCRAFT: return "Aircraft";
    case UB_GROUND_UNIT: return "Ground vehicle";
    case UB_INFANTRY: return "Infantry";
    case UB_TURRET: return "Gun turret";
    case UB_BOAT: return "Ship";
    case UB_CARRIER: return "Carrier ship";
    case UB_BUILDING: case UB_SIMPLE_BUILDING: return "Building";
    case UB_FIGHTER: return "Fighter aircraft";
    case UB_BOMBER: return "Bomber";
    case UB_GROUND_ATTACK_AIRCRAFT: return "Ground attack aircraft";
    case UB_DROPSHIP: return "Dropship";
    case UB_MINE: return "Mine";
    case UB_SUBMARINE: return "Submarine";
    case UB_DIVE_BOMBER: return "Dive bomber";
    default: return "Machine";
    }
}

// The machines in this level's resources that can stand in a hall: a unit definition whose model is loaded and compact. A first pass takes one of each
// kind in the order a visitor would meet them (walkers, vehicles, aircraft, defences, ships), a second fills what is left. Each becomes an exhibit
// of CE's own: a feature definition for the model, that nothing can damage, owned by the definition list and freed with it when the level ends.
unsigned SelectUnits(SelectedExhibit *selected, unsigned count, SelectionCounts *counts) {
    if (!UPhysicsManager::mUnits || !UPhysicsManager::mFeatures) return count;
    const SINT total = UPhysicsManager::mUnits->Size();
    if (total <= 0 || total > 4096) return count;
    static const int kKinds[] = {UB_MECH, UB_GROUND_UNIT, UB_FIGHTER, UB_AIRCRAFT, UB_GROUND_ATTACK_AIRCRAFT, UB_BOMBER, UB_TURRET, UB_BOAT, UB_INFANTRY,
                                 UB_BUILDING, UB_SIMPLE_BUILDING};
    const unsigned kinds = sizeof kKinds / sizeof kKinds[0];
    unsigned taken[4096 / 32 + 1] = {};
    SelectionCounts ignored = {};		// the same unit is looked at once for each kind: only the scenery's turned-away account is logged
    (void)counts;
    for (unsigned pass = 0; pass != 2; ++pass) {
        for (unsigned kind = 0; kind != (pass ? 1u : kinds); ++kind) {
            for (SINT i = 0; i != total && count != kNativeExhibits; ++i) {
                if (taken[i / 32] & (1u << (i % 32))) continue;
                CUnitData *unit = static_cast<CUnitData *>(UPhysicsManager::mUnits->At(i));
                if (!unit || !Name(unit->mName) || !Name(unit->mUnknown2c)) continue;
                if (!pass && static_cast<int>(unit->mBehaviour) != kKinds[kind]) continue;
                CMesh *mesh = CMesh::FindLoadedUnique(unit->mUnknown2c);
                SelectedExhibit candidate = {};
                if (!ModelBox(mesh, &candidate, &ignored)) continue;
                bool repeated = false;
                for (unsigned n = 0; n != count; ++n) if (selected[n].mesh == mesh) repeated = true;
                if (repeated) continue;
                CFeatureData *feature = MakeExhibitDefinition(unit->mName, unit->mUnknown2c);
                if (!feature) continue;
                candidate.definition = feature; candidate.mesh = mesh; candidate.ordinal = static_cast<unsigned>(i);
                CeMuseumPrettyName(candidate.name, sizeof candidate.name, unit->mName);
                std::snprintf(candidate.line, sizeof candidate.line, "%s", ClassLine(static_cast<int>(unit->mBehaviour)));
                const int mounts = unit->mWeapons.Size();
        if (mounts == 0)
            std::snprintf(candidate.detail, sizeof candidate.detail, "Armament: none.");
        else if (mounts == 1)
            std::snprintf(candidate.detail, sizeof candidate.detail, "Armament: one weapon mount.");
        else
            std::snprintf(candidate.detail, sizeof candidate.detail, "Armament: %d weapon mounts.", mounts);
        candidate.yaw = 3.14159265f;
        {
            const int behaviour = static_cast<int>(unit->mBehaviour);
            candidate.flies = behaviour == UB_AIRCRAFT || behaviour == UB_FIGHTER || behaviour == UB_BOMBER ||
                              behaviour == UB_GROUND_ATTACK_AIRCRAFT || behaviour == UB_DIVE_BOMBER || behaviour == UB_DROPSHIP;
        }
                taken[i / 32] |= 1u << (i % 32);
                selected[count++] = candidate;
                if (!pass) break;			// one of each kind in the first pass
            }
        }
    }
    return count;
}

unsigned Select(SelectedExhibit *selected, SelectionCounts *counts) {
    if (!UPhysicsManager::mFeatures) return 0;
    const SINT total = UPhysicsManager::mFeatures->Size();
    if (total <= 0 || total > 4096) return 0;
    counts->features = static_cast<unsigned>(total);
    unsigned count = SelectUnits(selected, 0, counts);
    counts->units = count;
    // Too few machines are loaded to make a hall of them: the level's own structures and scenery fill it.
    for (SINT i = 0; i != total && count != kNativeExhibits && counts->units < 3; ++i) {
        CFeatureData *item = static_cast<CFeatureData *>(UPhysicsManager::mFeatures->At(i));
        if (!item || !Name(item->mName) || !Name(item->mMeshName) || !std::strncmp(item->mName, "ce.museum.", 10)) { ++counts->unnamed; continue; }
        unsigned definitions = 0;
        for (SINT j = 0; j != total; ++j) {
            CFeatureData *other = static_cast<CFeatureData *>(UPhysicsManager::mFeatures->At(j));
            if (other && Name(other->mName) && !std::strcmp(item->mName, other->mName)) ++definitions;
        }
        if (definitions != 1) { ++counts->repeatedName; continue; }
        CMesh *mesh = CMesh::FindLoadedUnique(item->mMeshName);
        SelectedExhibit candidate = {};
        if (!ModelBox(mesh, &candidate, counts)) continue;
        bool repeated = false;
        for (unsigned n = 0; n != count; ++n) if (selected[n].mesh == mesh) repeated = true;
        if (repeated) { ++counts->repeatedModel; continue; }
        // The level's own structure is shown through a definition of CE's own too: the game's definition is not changed, and the exhibit cannot be damaged.
        CFeatureData *feature = MakeExhibitDefinition(item->mName, item->mMeshName);
        if (!feature) continue;
        candidate.definition = feature; candidate.mesh = mesh; candidate.ordinal = static_cast<unsigned>(i);
        CeMuseumPrettyName(candidate.name, sizeof candidate.name, item->mName);
        std::snprintf(candidate.line, sizeof candidate.line, "Structure");
        std::snprintf(candidate.detail, sizeof candidate.detail, "Fixed structure.");
        candidate.yaw = 3.14159265f;
        selected[count++] = candidate;
    }
    counts->chosen = count;
    return count;
}

// A cell is ground the visitor can stand on when the terrain there is finite and clear of the water by a metre. Flatness is not a
// condition: the planner prefers the flattest ground it finds, and each exhibit sits at the height of its own cell.
bool GalleryGround(float x, float y, float *z) {
    if (!z || !std::isfinite(x) || !std::isfinite(y) || x < 1 || y < 1 ||
        x >= MAP.mGridWidth - 2 || y >= MAP.mGridHeight - 2) return false;
    *z = MAP.Collide(FVector(x, y, 0));
    return std::isfinite(*z);
}

// A model aircraft's display: how far it climbs and banks (radians), and what its lowest point keeps above the plinth. Level and
// banked about 29 degrees, its upper side leans toward the pad by about that much (a climb would tip its belly to the visitor
// on a three-quarter heading, undoing the bank), and the flight post keeps both wingtips cleanly above the marble plinth.
const float kClimb = 0.0f, kBank = 0.50f, kPostClear = 0.25f;

// The hall for the selected machines. On failure *why names the cause for the log and *planner is set when the
// exhibition planner itself refused the machines (the player is told the hall has no room, not that files are missing).
static bool Layout(SelectedExhibit *selected, unsigned *count, CeMuseumVisit::RoomPlan *plan,
            CeMuseumVisit::ExhibitPlacement *exhibits, CeMuseumVisit::SitePlan *summary, CeMuseumExhibition::Plan* exhibition,
            char *why, unsigned whySize, bool *planner) {
    if (!why || !whySize || !planner) return false;
    *planner = false;
    std::snprintf(why, whySize, "the layout was given no machines or no output");
    if (!selected || !count || !plan || !exhibits || !summary || !exhibition ||
        !*count || *count > CeMuseumExhibition::kMaxBays) return false;
    CeMuseumExhibition::DisplayInput measured[CeMuseumExhibition::kMaxBays] = {};
    for (unsigned n = 0; n < *count; ++n) {
        // A small object (a soldier) is shown enlarged by a whole factor, its box with it, so it plans and stands as a machine does.
        SelectedExhibit &item = selected[n];
        item.enlarge = CeMuseumExhibitionHost::EnlargeFactor(
            0.5f * std::sqrt(item.footprint[0] * item.footprint[0] + item.footprint[1] * item.footprint[1]), item.height);
        const float by = float(item.enlarge);
        item.footprint[0] *= by; item.footprint[1] *= by; item.middle[0] *= by; item.middle[1] *= by;
        item.height *= by; item.lowestZ *= by;
        measured[n].stableLabel = int(n);
        // the planner's radius bounds the model at any yaw on its plinth: half the footprint's diagonal
        measured[n].radius = 0.5f * std::sqrt(selected[n].footprint[0] * selected[n].footprint[0] +
                                              selected[n].footprint[1] * selected[n].footprint[1]);
        measured[n].height = selected[n].height;
    }
    if (!CeMuseumExhibitionHost::Build(measured, *count, exhibition)) {
        *planner = true;
        std::snprintf(why, whySize, "the exhibition planner refused %u machines: %s", *count,
                      CeMuseumExhibition::ReasonText(exhibition->reason));
        return false;
    }
    // An aircraft stands on a post above its plinth, high enough that its nose, tail and lowered wingtip clear the plinth
    // once banked (using the major horizontal axis so whichever axis is wingspan never clips the plinth).
    float postLifts[CeMuseumExhibition::kMaxBays] = {};
    for (unsigned n = 0; n < *count; ++n) {
        if (!selected[n].flies) continue;
        const float major = selected[n].footprint[0] > selected[n].footprint[1] ? selected[n].footprint[0] : selected[n].footprint[1];
        const float lift = 0.5f * major * std::sin(kBank) + kPostClear;
        if (CeMuseumExhibitionHost::RaiseOnPost(*exhibition, n, lift))
            postLifts[n] = lift;
        else
            selected[n].flies = false;
    }

    std::snprintf(why, whySize, "the authored museum map is not installed");
    if (!gAuthoredMap || !*count || *count>6) return false;
    CeMuseumVisit::SitePlan site = {};
    site.visitor[0]=CeMuseumWorld::kEntranceX; site.visitor[1]=CeMuseumWorld::kEntranceY;
    std::snprintf(why, whySize, "no floor at the entrance %.0f,%.0f", site.visitor[0], site.visitor[1]);
    if (!GalleryGround(site.visitor[0],site.visitor[1],&site.visitor[2])) return false;
    const CeMuseumExhibition::Grand::RoomGraph grandGraph = CeMuseumExhibition::Grand::MakeRoomGraph();
    for (unsigned n=0;n<*count;++n) {
        float *centre=site.centres[n];
        std::snprintf(why, whySize, "no plinth or floor for machine %u (%s)", n + 1, selected[n].name);
        SelectedExhibit &item=selected[n];
        const CeMuseumExhibition::Grand::NativeBaySpot &nb = grandGraph.nativeBays[n];
        const CeHallPoint worldBay = CeMuseumExhibitionHost::World({nb.x, nb.y});
        centre[0] = worldBay.x;
        centre[1] = worldBay.y;
        if (!GalleryGround(centre[0], centre[1], &centre[2])) return false;
        centre[2] -= (nb.topZ + postLifts[n]);
        // Face into the Forseti Wing central nave angled slightly East toward incoming visitors.
        item.yaw = std::atan2(-(nb.facingX + 0.45f), -nb.facingY);
        float pitch=0.0f, roll=0.0f;
        if (item.flies && postLifts[n] > 0.0f) {
            pitch=-kClimb;
            const FMatrix right(item.yaw, pitch, kBank);
            roll=(right.Row[0].Z*nb.facingX+right.Row[1].Z*nb.facingY)>0.0f ? kBank : -kBank;
        }
        const FMatrix turn(item.yaw, pitch, roll);
        const FVector box(item.middle[0], item.middle[1], item.lowestZ-0.5f*item.height);
        const FVector at=turn*box;
        exhibits[n].definition=item.definition->mName;
        exhibits[n].position[0]=centre[0]-at.X;
        exhibits[n].position[1]=centre[1]-at.Y;
        exhibits[n].position[2]=centre[2]-0.5f*item.height-at.Z;
        exhibits[n].orientation[0]=item.yaw;
        exhibits[n].orientation[1]=pitch;
        exhibits[n].orientation[2]=roll;
    }
    for (unsigned axis=0;axis<3;++axis) plan->visitorPosition[axis]=site.visitor[axis];
    *summary=site;
    return true;
}

// The units of this carrier whose model is loaded: one summary line, the account a hall of units is built from.
void UnitCensus() {
    if (!UPhysicsManager::mUnits) return;
    const SINT total = UPhysicsManager::mUnits->Size();
    if (total <= 0 || total > 4096) return;
    unsigned loaded = 0;
    for (SINT i = 0; i != total; ++i) {
        CUnitData *unit = static_cast<CUnitData *>(UPhysicsManager::mUnits->At(i));
        if (!unit || !Name(unit->mName) || !Name(unit->mUnknown2c)) continue;
        CMesh *mesh = CMesh::FindLoadedUnique(unit->mUnknown2c);
        const CBoundingBox *box = mesh ? mesh->mBoundingBox : NULL;
        const bool boxed = box && box->mValid;
        if (boxed) ++loaded;
    }
    PortSessionLog(PORT_SESSION_GAME, false, "museum units: %d definitions, %u with a loaded model", static_cast<int>(total), loaded);
}

bool Receive(const void *base, unsigned baseLength, const void *real, unsigned realLength, void *context) {
    const unsigned baseId = *static_cast<const unsigned *>(context);
    return CeMuseumVisit::AcceptInstalled(baseId, base, baseLength, real, realLength);
}
}

// Once a frame while a visit runs (the HUD's draw hook calls it, ce_museum_label.cpp): the visitor cannot be hurt, and the sea and the open air beyond the
// hall are out of bounds. A visitor that reaches the water (the one thing in a hall that ends a Battle Engine whatever its armour) or flies or walks past the hall's
// edge (kCeMuseumHallRadius) is put back where it started, standing still, facing the exhibits, and the hall says so. Z counts downward, so a position within
// kSea of the water, or below it, is in the sea's reach.
// The heading a matrix faces, as the engine's yaw (forward +Y turns to (-sin yaw, cos yaw)).
static float YawOfOrientation(const FMatrix &orientation)
{
    const FVector forward = orientation.GetColumnY();
    if (forward.X * forward.X + forward.Y * forward.Y < 1.0e-8f) return 0.0f;
    return std::atan2(-forward.X, forward.Y);
}

// Puts the visitor somewhere facing a heading, for good: position, orientation and
// the engine's own pose state (its Euler angles, turn rates and shake) all agree, and
// it stands still. Used by the Directory's quick-jump and the out-of-bounds return.
static void PlaceVisitor(CBattleEngine *engine, const FVector &position, float yaw)
{
    engine->SetVelocity(FVector(0.0f, 0.0f, 0.0f));
    engine->Teleport(position);
    FMatrix facing;
    facing.MakeRotationYawF(yaw);
    engine->TeleportOrientation(facing);
    engine->CeResetPoseForRecall(yaw);
}

extern "C" void PortCeMuseumVisitFrame(void) {
    if (!PortCeMuseumVisitActive()) return;
    CPlayer *player = GAME.GetPlayer(0);
    CBattleEngine *engine = player ? player->GetBattleEngine() : NULL;
    if (!engine || engine->IsDying()) return;
    engine->SetVulnerable(FALSE);
    // The hall is roofed (its walls rise to one roof with the theater), so the visit stays on foot. The game's own switch, the one the tutorial uses,
    // makes the morph button do nothing in walker mode and lands a visitor found in jet mode. The engine is made fresh for each level, so a mission
    // after the visit flies as before.
    if (engine->GetState() == BATTLE_ENGINE_STATE_JET)
        PortSessionLog(PORT_SESSION_GAME, false, "museum visitor was in jet mode: landing as a walker");
    engine->DisableFlightMode();
    if (!gHaveStart) {
        gStartPosition = engine->GetPos();
        gLastViewingPosition = gStartPosition;
        gStartOrientation = engine->GetOrientation();
        gHaveStart = true;
        PortSessionLog(PORT_SESSION_GAME, false, "museum visitor stays on foot: flight mode off for the visit");
        return;
    }
    const float kSea = 1.5f;
    float jumpX = 0.0f, jumpY = 0.0f, jumpYaw = 0.0f;
    if (PortCeMuseumTakeQuickJump(&jumpX, &jumpY, &jumpYaw)) {
        float jz = gStartPosition.Z;
        GalleryGround(jumpX, jumpY, &jz);
        const FVector dest(jumpX, jumpY, jz);
        // A jump is a teleport with the pose reset, as the Sandbox's recall is. Writing
        // the orientation matrix alone left the engine's own yaw (its Euler state and
        // turn rates) where it was, so the next tick turned the visitor straight back.
        PlaceVisitor(engine, dest, jumpYaw);
        gLastViewingPosition = dest;
    }
    FVector here = engine->GetPos();
    const float simNow = EVENT_MANAGER.GetTime();
    if (gMuseumSprintUntil > 0.0f && (simNow > gMuseumSprintUntil || (gMuseumSprintUntil - simNow) > 0.25f))
        gMuseumSprintUntil = -1.0f;
    const bool shiftDown = (LT.KeyDown[DIK_LSHIFT] != 0) || (LT.KeyDown[DIK_RSHIFT] != 0) ||
                           (gMuseumSprintUntil > 0.0f && simNow <= gMuseumSprintUntil);

    // Brisk on-foot gallery stride (1.65x base walk, 2.85x Shift sprint) applied cleanly to the
    // resolved horizontal step without mutating BattleEngine configuration or triggering dashes.
    if (!GAME.IsPaused()) {
        const float dx = here.X - gLastViewingPosition.X;
        const float dy = here.Y - gLastViewingPosition.Y;
        const float stepLen = std::hypot(dx, dy);
        if (stepLen > 1e-4f && stepLen < 0.75f) {
            const float extra = shiftDown ? 1.85f : 0.65f;
            float addX = dx * extra;
            float addY = dy * extra;
            const float addLen = std::hypot(addX, addY);
            const float kMaxExtraStep = 0.42f;
            if (addLen > kMaxExtraStep) {
                const float scale = kMaxExtraStep / addLen;
                addX *= scale;
                addY *= scale;
            }
            FVector boosted = here;
            if (CeMuseumHallAllowsVisitor(here.X + addX, here.Y + addY)) {
                boosted.X = here.X + addX;
                boosted.Y = here.Y + addY;
            } else if (CeMuseumHallAllowsVisitor(here.X + addX, here.Y)) {
                boosted.X = here.X + addX;
            } else if (CeMuseumHallAllowsVisitor(here.X, here.Y + addY)) {
                boosted.Y = here.Y + addY;
            }
            if (boosted.X != here.X || boosted.Y != here.Y) {
                engine->MoveTo(boosted);
                here = boosted;
            }
        }
    }

    if (!CeMuseumHallAllowsVisitor(here.X,here.Y)) {
        // Slide smoothly along walls/plinths instead of snagging dead on diagonal approach.
        FVector resolved = gLastViewingPosition;
        if (CeMuseumHallAllowsVisitor(here.X, gLastViewingPosition.Y)) {
            resolved.X = here.X;
        } else if (CeMuseumHallAllowsVisitor(gLastViewingPosition.X, here.Y)) {
            resolved.Y = here.Y;
        }
        engine->MoveTo(resolved);
        engine->SetVelocity(FVector(0.0f,0.0f,0.0f));
        here = resolved;
    }
    gLastViewingPosition = here;
    CeMuseumHallVisitor(here.X, here.Y);
    CeMuseumVisitPlacesStep(&gPlaces, here.X, here.Y);
    const bool sea = CeMuseumInSeaReach(here.Z, MAP.GetWaterLevel(), kSea);
    const bool edge = CeMuseumPastHallEdge(here.X, here.Y, gStartPosition.X, gStartPosition.Y, kCeMuseumHallRadius);
    if (sea || edge) {
        PlaceVisitor(engine, gStartPosition, YawOfOrientation(gStartOrientation));
        gLastViewingPosition = gStartPosition;
        // A visitor put back in jet mode would drift off again with the jets' own momentum: it lands as a walker.
        if (engine->GetState() == BATTLE_ENGINE_STATE_JET) engine->Morph();
        PortCeMuseumLabelsTitle("Out of bounds", sea ? "The sea is not part of the museum. Back at the start." : "That is the edge of the hall. Back at the start.");
        PortSessionLog(PORT_SESSION_GAME, false, "museum visitor put back at the start (%s), from %.1f,%.1f at height %.1f over water %.1f", sea ? "the sea's reach" : "the hall's edge",
                       here.X, here.Y, here.Z, MAP.GetWaterLevel());
    }
}

bool CeMuseumVisit::GenerateInstalled() {
    if (!PortCeMuseumVisitActive()) return false;
    if (ForcedRefusal("changed")) { Refuse("Your game files changed while the museum was opening. Try again."); return false; }
    if (!CheckInstalledSource()) return false;
    if (ForcedRefusal("binding") || WORLD.mWorldLevel != PortCeMuseumVisitLevel() || WORLD.mBaseWorldLevel < 0 || WORLD.mBaseWorldLevel > 999 ||
        WORLD.mBaseWorldLevel == WORLD.mWorldLevel || WORLD.mBaseWorldResourcePos == WORLD.mWorldResourcePos) {
        PortSessionLog(PORT_SESSION_GAME, true, "museum binding: level %d, base %d, resource positions %u and %u", WORLD.mWorldLevel, WORLD.mBaseWorldLevel,
                       static_cast<unsigned>(WORLD.mWorldResourcePos), static_cast<unsigned>(WORLD.mBaseWorldResourcePos));
        Refuse("The museum could not read your game files. Check that the game is installed, then try again."); return false;
    }
    CarrierPrefix base = {}, real = {};
    if (!SourcePrefix(WORLD.mBaseWorldResourcePos, true, &base) ||
        !SourcePrefix(WORLD.mWorldResourcePos, false, &real) ||
        base.parentWorld != -1 || real.parentWorld != WORLD.mBaseWorldLevel ||
        !PortCeMuseumAuthoredMap() || MAP.mMapNumber != CeMuseumWorld::kMap) {
        PortSessionLog(PORT_SESSION_GAME, true, "museum header: base parent %d map %d, real parent %d (base level %d)", base.parentWorld, base.mapNumber, real.parentWorld,
                       WORLD.mBaseWorldLevel);
        Refuse("The museum could not read your game files. Check that the game is installed, then try again."); return false;
    }
    UnitCensus();

    DropExhibitDefinitions();
    gEnlargedCount = 0;
    SelectedExhibit selected[kNativeExhibits] = {};
    SelectionCounts counts = {};
    const unsigned count = ForcedRefusal("exhibits") ? 0 : Select(selected, &counts);
    PortSessionLog(PORT_SESSION_GAME, count == 0,
                   "museum exhibits: %u definitions, %u chosen (%u machines); turned away: %u unnamed, %u repeated names, %u without a loaded model, "
                   "%u without a box, %u too large, %u repeated models",
                   counts.features, counts.chosen, counts.units, counts.unnamed, counts.repeatedName, counts.notLoaded, counts.noBox,
                   counts.tooLarge, counts.repeatedModel);
    if (!count) { Refuse("The museum could not read your game files. Check that the game is installed, then try again."); return false; }
    RoomPlan plan = {};
    ExhibitPlacement exhibits[kNativeExhibits] = {};
    plan.playerConfiguration = real.playerConfiguration;
    plan.baseLevel = CeMuseumWorld::kBase; plan.mapNumber = CeMuseumWorld::kMap;
    plan.exhibits = exhibits; plan.exhibitCount = count;
    CeMuseumVisit::SitePlan summary = {};
    unsigned shown = count;
    CeMuseumExhibition::Plan exhibition = {};
    char why[160] = "a forced refusal of the site";
    bool planner = false;
    if (ForcedRefusal("site") || !Layout(selected, &shown, &plan, exhibits, &summary, &exhibition, why, sizeof why, &planner)) {
        PortSessionLog(PORT_SESSION_GAME, true, "museum site: cannot place %u exhibits on a %d by %d exhibition floor: %s", count, MAP.mGridWidth,
                       MAP.mGridHeight, why);
        if (planner) Refuse("The museum hall has no room for this game's machines, so it cannot open. Your careers are unchanged.");
        else Refuse("The museum could not read your game files. Check that the game is installed, then try again.");
        return false;
    }
    plan.exhibitCount = shown;
    PortSessionLog(PORT_SESSION_GAME, false, "museum world: %s; %u installed exhibits on authored plinths", CeMuseumWorld::kName,shown);
    for (unsigned n = 0; n != shown; ++n) {
        const CeMuseumExhibition::Bay *bay = CeMuseumExhibitionHost::InputBay(exhibition, n);
        const CeHallPoint pad = bay ? CeMuseumExhibitionHost::World(bay->pad) : CeHallPoint{0.0f, 0.0f};
        PortSessionLog(PORT_SESSION_GAME, false, "museum exhibit %u: %s (%s), footprint %.1f by %.1f, height %.1f, shown %u times, at %.0f,%.0f, pad %.2f,%.2f",
                       n + 1, selected[n].name, selected[n].line, selected[n].footprint[0], selected[n].footprint[1], selected[n].height,
                       selected[n].enlarge, exhibits[n].position[0], exhibits[n].position[1], pad.x, pad.y);
    }
    // The words: the hall's name, and each exhibit's name where it stands (ce_museum_label.h). Dropped with the visit.
    PortCeMuseumLabelsClear();
    CeMuseumVisitPlacesDrop(&gPlaces);
    PortCeMuseumLabelsTitle("Units", "Face a machine. Walk closer to inspect it.");

    for (unsigned n = 0; n != shown; ++n) {
        const SelectedExhibit &item = selected[n];
        const float radius = 0.5f * (item.footprint[0] > item.footprint[1] ? item.footprint[0] : item.footprint[1]);
        const bool labelled = PortCeMuseumLabelsAdd(
            item.name, CeMuseumLineRepeatsName(item.name, item.line) ? "" : item.line,
            item.detail, summary.centres[n][0], summary.centres[n][1],
            summary.centres[n][2] - item.height, radius);
        const int label = labelled ? int(PortCeMuseumLabelsCount()) - 1 : -1;
        if (CeMuseumExhibition::Bay* bay = CeMuseumExhibitionHost::InputBay(exhibition, n))
            bay->stableLabel = label;
        // An enlarged exhibit is drawn at its factor (PortCeMuseumFeatureScale) and its label says so plainly.
        if (item.enlarge > 1 && gEnlargedCount < kNativeExhibits) {
            gEnlarged[gEnlargedCount] = item.definition;
            gEnlargedBy[gEnlargedCount++] = float(item.enlarge);
            char note[48];
            if (label >= 0 && CeMuseumScaleNote(note, sizeof note, item.enlarge))
                PortCeMuseumLabelsNote(unsigned(label), note);
        }
    }
    CeHallTour tour;
    if (CeMuseumExhibitionHost::Tour(exhibition, &tour)) {
        CeMuseumHallSetExhibition(tour, GalleryGround, exhibition);
        CeMuseumVisitPlacesArm(&gPlaces, exhibition);
        PortCeMuseumLabelsTitle("Units", "Face a display at its brass sign to inspect it.");
    }
    const unsigned baseId = CeMuseumWorld::kBase;
    if (ForcedRefusal("generate") || !GenerateRoom(plan, Receive, const_cast<unsigned *>(&baseId))) {
        Refuse("The museum could not read your game files. Check that the game is installed, then try again."); return false;
    }
    return true;
}

// Patch 0257 (CFeature::GetRenderScale): the scale a feature is drawn at. Only a visit's own enlarged exhibit, by its definition, while the
// visit is open, is drawn larger; every other thing in every other mode answers 1.
extern "C" float PortCeMuseumFeatureScale(const void *definition) {
    return CeMuseumExhibitionHost::ExhibitScale(PortCeMuseumVisitActive(), definition, gEnlarged, gEnlargedBy, gEnlargedCount);
}
#elif !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
bool CeMuseumVisit::GenerateInstalled() { return false; }
#endif
