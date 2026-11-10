// SPDX-License-Identifier: GPL-3.0-only
#include "common.h"
#include "Career.h"
#include "frontend.h"
#include "ce_museum_visit.h"
#include "ce_museum.h"
#include "ce_project.h"
#include "../platform/common/portlog.h"
#include <cstring>
#if defined(CE_VERSION) && !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
#include "DXMemBuffer.h"
#include "world.h"
#include "map.h"
#include "mesh.h"
#include "WorldPhysicsManager.h"
#include "resourceaccumulator.h"
#endif

bool PortCeMenus();
// Declared by the shared authoring-session change in ce_project.h as well.
bool PortCeAuthoringSessionAllowed() noexcept;
extern "C" bool PortCeEditorActive(void) __attribute__((weak));

namespace {
unsigned char gMuseumCareer[sizeof(CCareer)];
bool gMuseumCareerSaved = false;

bool Begin(bool preparedCache) {
#ifndef CE_VERSION
    (void)preparedCache;
    CeMuseumVisit::Refuse("The museum room is unavailable in this build.");
    return false;
#else
    // Check every mode before reading room files, copying the career or leaving
    // the frontend. In particular an editor visit must never lose its snapshot.
    if (PortCeMuseumVisitActive() || gMuseumCareerSaved || !PortCeMenus() || !FRONTEND.IsRunning() ||
        !PortCeAuthoringSessionAllowed() || PortCeMuseumActive() ||
        (PortCeEditorActive && PortCeEditorActive())) {
        CeMuseumVisit::Refuse("The museum construction test needs ordinary play with no active mods or online session.");
        return false;
    }
    if (!(preparedCache ? CeMuseumVisit::Prepare() : CeMuseumVisit::PrepareInstalled(100))) return false;
    std::memcpy(gMuseumCareer, &CAREER, sizeof(CCareer));
    gMuseumCareerSaved = true;
    if (!CeMuseumVisit::Activate()) {
        PortCeMuseumVisitCancel();
        CeMuseumVisit::Refuse("The museum room could not be started.");
        return false;
    }
    return true;
#endif
}
}

extern "C" bool PortCeMuseumVisitBegin(void) { return Begin(false); }
bool CeMuseumVisit::BeginPreparedCacheForDevelopment() { return Begin(true); }

extern "C" void PortCeMuseumVisitCancel(void) {
    // Retain Active while restoring, so every progression/save guard continues
    // to exclude this visit until the final frontend return decision is done.
    if (gMuseumCareerSaved) {
        std::memcpy(&CAREER, gMuseumCareer, sizeof(CCareer));
        gMuseumCareerSaved = false;
    }
    CeMuseumVisit::Clear();
}

extern "C" bool PortCeMuseumVisitTakeReturn(void) {
    if (!PortCeMuseumVisitActive()) return false;
    PortCeMuseumVisitCancel();
    return true;
}

#if defined(CE_VERSION) && !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
namespace {
const unsigned kNativeExhibits = 6;
// A model larger than this does not stand in a room of this size (world units, the model's own bounding box).
const float kLargestFootprint = 48.0f, kLargestHeight = 64.0f;
struct SelectedExhibit {
    CFeatureData *definition;
    CMesh *mesh;
    unsigned ordinal;
    float footprint[2];   // the model's width (X) and depth (Y)
    float middle[2];      // the box's centre from the model's origin, on the ground plane
    float height;         // the model's extent along Z
    float lowestZ;        // the model's lowest point, Z counted downward as the world does
};
// Why the candidates were turned away: the only account of an empty or short exhibition a player's log can carry.
struct SelectionCounts {
    unsigned features, unnamed, repeatedName, notLoaded, noBox, tooLarge, repeatedModel, chosen;
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

unsigned Select(SelectedExhibit *selected, SelectionCounts *counts) {
    if (!UPhysicsManager::mFeatures) return 0;
    const SINT total = UPhysicsManager::mFeatures->Size();
    if (total <= 0 || total > 4096) return 0;
    counts->features = static_cast<unsigned>(total);
    unsigned count = 0;
    for (SINT i = 0; i != total && count != kNativeExhibits; ++i) {
        CFeatureData *item = static_cast<CFeatureData *>(UPhysicsManager::mFeatures->At(i));
        if (!item || !Name(item->mName) || !Name(item->mMeshName)) { ++counts->unnamed; continue; }
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
        candidate.definition = item; candidate.mesh = mesh; candidate.ordinal = static_cast<unsigned>(i);
        selected[count++] = candidate;
    }
    counts->chosen = count;
    return count;
}

// A cell is ground the visitor can stand on when the terrain there is finite and clear of the water by a metre. Flatness is not a
// condition: the planner prefers the flattest ground it finds, and each exhibit sits at the height of its own cell.
struct Ground { float water; };
bool GroundCell(int x, int y, float *height, void *context) {
    if (x < 1 || y < 1 || x + 1 >= MAP.mGridWidth || y + 1 >= MAP.mGridHeight) return false;
    const float level = MAP.GetHeight(x, y), water = static_cast<const Ground *>(context)->water;
    if (!Finite(level, 4096) || !Finite(water, 4096) || level >= water - 1.0f) return false;
    *height = level;
    return true;
}

// Smallest models first. When the ground cannot take them all, the largest are left out one at a time until the rest fit.
bool Layout(SelectedExhibit *selected, unsigned *count, CeMuseumVisit::RoomPlan *plan, CeMuseumVisit::ExhibitPlacement *exhibits,
            CeMuseumVisit::SitePlan *summary) {
    const SINT width = MAP.mGridWidth, height = MAP.mGridHeight;
    if (width < 128 || height < 128 || width > 4096 || height > 4096) return false;
    for (unsigned i = 1; i < *count; ++i) {
        SelectedExhibit moving = selected[i];
        unsigned j = i;
        while (j && selected[j - 1].footprint[0] * selected[j - 1].footprint[1] > moving.footprint[0] * moving.footprint[1]) {
            selected[j] = selected[j - 1];
            --j;
        }
        selected[j] = moving;
    }
    Ground ground = {MAP.GetWaterLevel()};
    CeMuseumVisit::SitePlan site;
    for (unsigned use = *count; use != 0; --use) {
        CeMuseumVisit::ExhibitFootprint footprints[kNativeExhibits];
        for (unsigned n = 0; n != use; ++n) {
            footprints[n].width = selected[n].footprint[0]; footprints[n].depth = selected[n].footprint[1];
            footprints[n].height = selected[n].height;
        }
        // No source actor positions, squads or script-owned locations are copied: the site is searched on the existing terrain.
        if (!CeMuseumVisit::PlanSite(footprints, use, width, height, GroundCell, &ground, &site)) continue;
        for (unsigned n = 0; n != use; ++n) {
            exhibits[n].definition = selected[n].definition->mName;
            exhibits[n].position[0] = site.centres[n][0] - selected[n].middle[0];
            exhibits[n].position[1] = site.centres[n][1] - selected[n].middle[1];
            exhibits[n].position[2] = site.centres[n][2] - selected[n].lowestZ;
            for (unsigned axis = 0; axis != 3; ++axis) exhibits[n].orientation[axis] = 0;
        }
        for (unsigned axis = 0; axis != 3; ++axis) { plan->visitorPosition[axis] = site.visitor[axis]; plan->visitorOrientation[axis] = 0; }
        *summary = site;
        *count = use;
        return true;
    }
    return false;
}

bool Receive(const void *base, unsigned baseLength, const void *real, unsigned realLength, void *context) {
    const unsigned baseId = *static_cast<const unsigned *>(context);
    return CeMuseumVisit::AcceptInstalled(baseId, base, baseLength, real, realLength);
}
}

bool CeMuseumVisit::GenerateInstalled() {
    if (!PortCeMuseumVisitActive() || !CheckInstalledSource()) return false;
    if (WORLD.mWorldLevel != PortCeMuseumVisitLevel() || WORLD.mBaseWorldLevel < 0 || WORLD.mBaseWorldLevel > 999 ||
        WORLD.mBaseWorldLevel == WORLD.mWorldLevel || WORLD.mBaseWorldResourcePos == WORLD.mWorldResourcePos) {
        Refuse("The museum carrier has an unsupported world binding."); return false;
    }
    CarrierPrefix base = {}, real = {};
    if (!SourcePrefix(WORLD.mBaseWorldResourcePos, true, &base) ||
        !SourcePrefix(WORLD.mWorldResourcePos, false, &real) ||
        base.parentWorld != -1 || real.parentWorld != WORLD.mBaseWorldLevel ||
        !MAP.AlreadyLoaded(base.mapNumber, TRUE, TRUE)) {
        Refuse("The museum could not bind the installed carrier's header and terrain."); return false;
    }
    SelectedExhibit selected[kNativeExhibits] = {};
    SelectionCounts counts = {};
    const unsigned count = Select(selected, &counts);
    PortSessionLog(PORT_SESSION_GAME, count == 0,
                   "museum exhibits: %u definitions, %u chosen; turned away: %u unnamed, %u repeated names, %u without a loaded model, "
                   "%u without a box, %u too large, %u repeated models",
                   counts.features, counts.chosen, counts.unnamed, counts.repeatedName, counts.notLoaded, counts.noBox,
                   counts.tooLarge, counts.repeatedModel);
    if (!count) { Refuse("The carrier has no unambiguous compact static exhibits."); return false; }
    RoomPlan plan = {};
    ExhibitPlacement exhibits[kNativeExhibits] = {};
    plan.playerConfiguration = real.playerConfiguration;
    plan.baseLevel = WORLD.mBaseWorldLevel; plan.mapNumber = base.mapNumber;
    plan.exhibits = exhibits; plan.exhibitCount = count;
    CeMuseumVisit::SitePlan summary = {};
    unsigned shown = count;
    if (!Layout(selected, &shown, &plan, exhibits, &summary)) {
        PortSessionLog(PORT_SESSION_GAME, true, "museum site: no dry ground for %u exhibits on a %d by %d map", count, MAP.mGridWidth, MAP.mGridHeight);
        Refuse("The museum could not find enough dry ground for the exhibition."); return false;
    }
    plan.exhibitCount = shown;
    PortSessionLog(PORT_SESSION_GAME, false, "museum site: %u of %u exhibits placed, %u in view of the visitor, dry ring %u, ground height range %.1f",
                   shown, count, summary.visible, summary.dryRing, summary.heightRange);
    for (unsigned n = 0; n != shown; ++n)
        PortSessionLog(PORT_SESSION_GAME, false, "museum exhibit %u: footprint %.1f by %.1f, height %.1f, at %.0f,%.0f", n + 1,
                       selected[n].footprint[0], selected[n].footprint[1], selected[n].height, exhibits[n].position[0], exhibits[n].position[1]);
    const unsigned baseId = static_cast<unsigned>(WORLD.mBaseWorldLevel);
    if (!GenerateRoom(plan, Receive, const_cast<unsigned *>(&baseId))) {
        Refuse("The museum could not generate its bounded exhibition worlds."); return false;
    }
    return true;
}
#elif !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
bool CeMuseumVisit::GenerateInstalled() { return false; }
#endif
