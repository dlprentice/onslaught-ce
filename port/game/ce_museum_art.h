// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_art.h: Curated framed concept art canvases for the Concept Art Gallery
// (Southeast) and Early Designs Vault (Southwest). Pure C++ manifest, aspect-preserving 512px
// upload sizing, wing-bounded VRAM cap, and plaque targeting for host verification.
#ifndef CE_MUSEUM_ART_H
#define CE_MUSEUM_ART_H

#include "ce_museum_tour.h"
#include <cmath>

namespace CeMuseumArt {

constexpr unsigned kMaxTexEdge = 512u;
// At most 10 canvases per art wing resident at once (~10 MB max at 512x512 RGBA).
enum { kMaxResidentCanvases = 12, kManifestCount = 20 };

struct CanvasSpec {
    CeMuseumTour::Wing wing;
    const char *retailPath;   // lookup key in player's installed Goodies textures
    const char *title;
    const char *category;
    const char *caption;
    float localX, localY;     // wall-mounted centre in visitor-local metres
    float centreZ;            // positive upward height of canvas centre
    float maxWidth, maxHeight; // frame opening bounds in metres
    float normalX, normalY;   // unit normal pointing into the gallery room
};

// Strictly factual curator titles & captions (zero internal filenames or build jargon).
static const CanvasSpec kManifest[kManifestCount] = {
    // ---- Concept Art Gallery (Southeast: x [16, 40], y [10, 28]) ----
    {CeMuseumTour::kWingConceptArt, "goodies\\ca_be_aquila\\ca_be_final01.tga",
     "Battle Engine Aquila - Study I", "Production Concept Art",
     "Final walker-mode silhouette and Forseti blue-steel livery study.",
     19.5f, 27.62f, 2.85f, 3.0f, 2.2f, 0.0f, -1.0f},
    {CeMuseumTour::kWingConceptArt, "goodies\\ca_be_aquila\\ca_be_final02.tga",
     "Battle Engine Aquila - Study II", "Production Concept Art",
     "Flight-mode wing geometry and thruster alignment study.",
     24.0f, 27.62f, 2.85f, 3.0f, 2.2f, 0.0f, -1.0f},
    {CeMuseumTour::kWingConceptArt, "goodies\\ca_be_aquila\\ca_bea_battle_pic.tga",
     "Coastal Assault", "Key Production Illustration",
     "Combined-arms beachhead action featuring the Aquila over sea and armour.",
     28.5f, 27.62f, 2.85f, 3.0f, 2.2f, 0.0f, -1.0f},
    {CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_xf24-venom_fighter.tga",
     "XF-24 Venom Fighter", "Forseti Aviation Art",
     "High-speed swept-wing Forseti interceptor design sheet.",
     33.0f, 27.62f, 2.85f, 3.0f, 2.2f, 0.0f, -1.0f},
    {CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_av6_rhino_apc.tga",
     "AV-6 Rhino APC", "Forseti Armour Art",
     "Armoured personnel transport study with troop bay detailing.",
     37.0f, 27.62f, 2.85f, 3.0f, 2.2f, 0.0f, -1.0f},

    {CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_venturer.tga",
     "F.S. Venturer", "Forseti Naval Art",
     "Command flagship and fleet flight-deck exterior study.",
     19.5f, 10.38f, 2.85f, 3.0f, 2.2f, 0.0f, 1.0f},
    {CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu__crab_mech.tga",
     "Muspell Gun Crab", "Muspell Armour Art",
     "Heavy quadrupedal Dominion assault walker study.",
     24.0f, 10.38f, 2.85f, 3.0f, 2.2f, 0.0f, 1.0f},
    {CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_mech.tga",
     "Muspell Walker", "Muspell Armour Art",
     "Angular iron-and-ochre Dominion bipedal mech study.",
     28.5f, 10.38f, 2.85f, 3.0f, 2.2f, 0.0f, 1.0f},
    {CeMuseumTour::kWingConceptArt, "goodies\\ca_bosses\\ca_boss_fenrir.tga",
     "Fenrir Command Fortress", "Boss Design Art",
     "Fortified Muspell super-structure and command tower painting.",
     33.0f, 10.38f, 2.85f, 3.0f, 2.2f, 0.0f, 1.0f},
    {CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_hanger_interior.tga",
     "Forseti Fleet Hangar", "Environment Concept Art",
     "Interior staging deck and launch bay architectural study.",
     37.0f, 10.38f, 2.85f, 3.0f, 2.2f, 0.0f, 1.0f},

    // ---- Early Designs Vault (Southwest: x [-40, -16], y [10, 28]) ----
    {CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v1.tga",
     "Aquila Design Study v1", "Early Walker Revision",
     "Initial bipedal war-machine exploration with angular cockpit canopy.",
     -19.5f, 27.62f, 2.85f, 3.0f, 2.2f, 0.0f, -1.0f},
    {CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v2_bluprint01.tga",
     "Aquila v2 Blueprint - Elevation", "Technical Blueprint",
     "Orthographic mechanical drawing from the second Battle Engine revision.",
     -24.0f, 27.62f, 2.85f, 3.0f, 2.2f, 0.0f, -1.0f},
    {CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v2_bluprint02.tga",
     "Aquila v2 Blueprint - Armament", "Technical Blueprint",
     "Weapon pod articulation and thruster placement sheet for revision two.",
     -28.5f, 27.62f, 2.85f, 3.0f, 2.2f, 0.0f, -1.0f},
    {CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v3.tga",
     "Aquila Design Study v3", "Intermediate Revision",
     "Third-pass chassis balancing walker leg stance with aircraft wings.",
     -33.0f, 27.62f, 2.85f, 3.0f, 2.2f, 0.0f, -1.0f},
    {CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v4&5.tga",
     "Aquila Design Studies v4 and v5", "Iterative Studies",
     "Side-by-side silhouette revisions refining dorsal intakes and cockpit.",
     -37.0f, 27.62f, 2.85f, 3.0f, 2.2f, 0.0f, -1.0f},

    {CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v6.tga",
     "Aquila Design Study v6", "Late Pre-Production",
     "Near-final transformation frame establishing the shipped proportions.",
     -19.5f, 10.38f, 2.85f, 3.0f, 2.2f, 0.0f, 1.0f},
    {CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v7.tga",
     "Aquila Design Study v7", "Final Pre-Production",
     "Final silhouette reference completed prior to game-ready modelling.",
     -24.0f, 10.38f, 2.85f, 3.0f, 2.2f, 0.0f, 1.0f},
    {CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_units\\ca_fu_early_craft01.tga",
     "Early Forseti Aircraft Study", "Unshipped Concept",
     "Pre-production aerial hull exploration predating the shipped fighters.",
     -28.5f, 10.38f, 2.85f, 3.0f, 2.2f, 0.0f, 1.0f},
    {CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_units\\ca_mu_early_craft01.tga",
     "Early Muspell Craft Study", "Unshipped Concept",
     "Early Dominion strike craft silhouette from pre-production archives.",
     -33.0f, 10.38f, 2.85f, 3.0f, 2.2f, 0.0f, 1.0f},
    {CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_bosses\\ca_boss_kraken.tga",
     "Kraken Maritime Study", "Boss Archive",
     "Early large-scale naval war-machine design study.",
     -37.0f, 10.38f, 2.85f, 3.0f, 2.2f, 0.0f, 1.0f},
};

struct UploadSize {
    unsigned width;
    unsigned height;
};

// Bounded <= 512 px longest edge preserving exact source aspect ratio.
inline UploadSize BoundedUpload(unsigned srcW, unsigned srcH, unsigned maxEdge = kMaxTexEdge) {
    if (!srcW || !srcH || !maxEdge) return UploadSize{1u, 1u};
    const unsigned longest = srcW > srcH ? srcW : srcH;
    if (longest <= maxEdge) return UploadSize{srcW, srcH};
    const float scale = float(maxEdge) / float(longest);
    unsigned w = static_cast<unsigned>(float(srcW) * scale + 0.5f);
    unsigned h = static_cast<unsigned>(float(srcH) * scale + 0.5f);
    return UploadSize{w ? w : 1u, h ? h : 1u};
}

struct QuadExtent {
    float halfW;
    float halfH;
};

// Fit a source image inside [maxWidth x maxHeight] preserving exact aspect ratio.
inline QuadExtent FitFrame(unsigned srcW, unsigned srcH, float maxWidth, float maxHeight) {
    if (!srcW || !srcH || !(maxWidth > 0.0f) || !(maxHeight > 0.0f))
        return QuadExtent{maxWidth * 0.5f, maxHeight * 0.5f};
    const float aspect = float(srcW) / float(srcH);
    const float boxAspect = maxWidth / maxHeight;
    float w = maxWidth, h = maxHeight;
    if (aspect > boxAspect) h = maxWidth / aspect;
    else w = maxHeight * aspect;
    return QuadExtent{w * 0.5f, h * 0.5f};
}

inline bool ShouldBeResident(unsigned index, CeMuseumTour::Wing currentWing) {
    if (index >= kManifestCount) return false;
    return kManifest[index].wing == currentWing;
}

inline unsigned CountWantedResident(CeMuseumTour::Wing currentWing) {
    unsigned n = 0;
    for (unsigned i = 0; i < kManifestCount; ++i)
        if (ShouldBeResident(i, currentWing)) ++n;
    return n;
}

// Estimated worst-case RGBA memory bytes for resident wing canvases.
inline unsigned EstimateWingBytes(CeMuseumTour::Wing wing) {
    return CountWantedResident(wing) * kMaxTexEdge * kMaxTexEdge * 4u;
}

// Nearest framed canvas the visitor is standing near and looking toward.
inline int FocusCanvas(float visitorX, float visitorY, float forwardX, float forwardY,
                       CeMuseumTour::Wing currentWing) {
    const float flen = std::sqrt(forwardX * forwardX + forwardY * forwardY);
    if (!(flen > 1e-5f)) return -1;
    const float fx = forwardX / flen, fy = forwardY / flen;
    int best = -1;
    float bestScore = -1.0f;
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (!ShouldBeResident(i, currentWing)) continue;
        const CanvasSpec &sp = kManifest[i];
        const float dx = sp.localX - visitorX, dy = sp.localY - visitorY;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d < 0.3f || d > 7.5f) continue;
        // Visitor must be on the front (gallery) side of the painting.
        if ((-dx * sp.normalX - dy * sp.normalY) <= 0.1f) continue;
        const float cosA = (dx * fx + dy * fy) / d;
        if (cosA < 0.85f) continue;
        const float score = cosA - 0.02f * d;
        if (score > bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// Runtime adapter functions implemented in ce_museum_art.cpp.
void RuntimeInit();
void RuntimeShutdown();
void RuntimeUpdate(CeMuseumTour::Wing wing);
bool RuntimeCue(float visitorX, float visitorY, float fwdX, float fwdY,
                CeMuseumTour::Wing wing,
                char *title, unsigned titleCap,
                char *line, unsigned lineCap,
                char *note, unsigned noteCap);
void RuntimeDraw(float floorZ);

} // namespace CeMuseumArt

#endif
