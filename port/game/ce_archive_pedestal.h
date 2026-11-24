// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_archive_pedestal.h: Interactive Archive Pedestal for the Grand Museum
// Atrium (and portable world placements via RuntimeSetBasePose). Cycles cleanly across
// all 213 retail 3D meshes, all 45 Goodie 3D models, all 149 Goodie concept art plates
// (illuminated lightbox stele), all 33 Cinema FMVs, and all 5 Bonus Levels using a
// dedicated 8 MiB host-malloc swap arena with zero retail-heap fragmentation.
#ifndef CE_ARCHIVE_PEDESTAL_H
#define CE_ARCHIVE_PEDESTAL_H

#define CE_MUSEUM_HAS_ARCHIVE_PEDESTAL 1

#include "ce_museum_tour.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace CeArchivePedestal {

// Agreed Grand Atrium rotunda centerpiece placement (visitor-local metres, Z up).
constexpr float kPedestalLocalX = 0.0f;
constexpr float kPedestalLocalY = 36.0f;
constexpr float kPedestalPlinthTopZ = 1.05f;
constexpr float kPedestalPlinthHalf = 2.0f;
constexpr float kControlLocalY = 33.65f; // south-facing brass control rail
constexpr float kControlLocalZ = 1.08f;
constexpr float kControlOffsetY = kControlLocalY - kPedestalLocalY; // -2.35 m south of center
constexpr float kButtonRadius = 0.24f;
constexpr float kButtonMaxRange = 5.5f;
constexpr float kCustomWorldButtonMaxRange = 18.0f;
constexpr float kKeyboardNearRange = 9.5f;
constexpr float kFocusMaxRange = 11.0f;
constexpr float kTargetSpan = 3.75f;
constexpr float kDefaultRateRadPerSec = 14.0f * 3.14159265f / 180.0f;
constexpr float kSwapFadeSeconds = 0.22f;
constexpr unsigned kLightboxMaxEdge = 1024u;

// Standard DirectInput key codes (avoiding <dinput.h> in pure header).
constexpr unsigned kDikTab = 0x0Fu;
constexpr unsigned kDikP = 0x19u;
constexpr unsigned kDikLBracket = 0x1Au;
constexpr unsigned kDikRBracket = 0x1Bu;
constexpr unsigned kDikPrior = 0xC9u; // PgUp
constexpr unsigned kDikNext = 0xD1u;  // PgDn

enum ControlButton {
    kBtnPrev = 0,
    kBtnNext = 1,
    kBtnCategory = 2,
    kBtnPause = 3,
    kBtnCount = 4
};

struct ButtonSpec {
    ControlButton id;
    const char *label;
    const char *cue;
    float offsetX;
};

static const ButtonSpec kButtons[kBtnCount] = {
    {kBtnPrev,     "Previous Exhibit", "Step back to the previous item in this archive category.", -0.90f},
    {kBtnNext,     "Next Exhibit",     "Advance to the next item in this archive category.",      -0.30f},
    {kBtnCategory, "Switch Category",  "Cycle Retail Meshes, Goodie Models, Concept Art, Cinema, and Bonus Levels.", 0.30f},
    {kBtnPause,    "Pause or Spin",    "Pause or resume turntable rotation on the display pedestal.", 0.90f},
};

enum Category {
    kCatRetailMeshes = 0,
    kCatGoodieModels = 1,
    kCatConceptArt = 2,
    kCatCinemaFmvs = 3,
    kCatBonusLevels = 4,
    kCatCount = 5
};

inline const char *CategoryName(Category c) {
    switch (c) {
    case kCatRetailMeshes: return "Retail 3D Meshes";
    case kCatGoodieModels: return "Goodie 3D Models";
    case kCatConceptArt:   return "Concept Art Lightbox";
    case kCatCinemaFmvs:   return "Cinema Archive";
    case kCatBonusLevels:  return "Bonus Levels";
    default:               return "Archive";
    }
}

// -----------------------------------------------------------------------------
// Complete 232-Goodie Classification (matches retail FEPGoodies.cpp exact IDs)
//   45 Goodie 3D Models + 149 Art Plates + 33 FMVs + 5 Bonus Levels = 232
// -----------------------------------------------------------------------------
constexpr unsigned kGoodieTotal = 232u;

enum GoodieKind {
    kGoodieImage = 0,
    kGoodieMesh = 1,
    kGoodieFmv = 2,
    kGoodieLevel = 3
};

inline GoodieKind ClassifyGoodie(unsigned id) {
    if (id <= 7u) return kGoodieImage;
    if (id == 12u || id == 13u || id == 24u || id == 33u || id == 34u || id == 35u) return kGoodieImage;
    if (id <= 57u) return kGoodieMesh;
    if (id <= 65u) return kGoodieImage;
    if (id <= 70u) return kGoodieLevel;
    if (id <= 74u) return kGoodieImage;
    if (id == 75u) return kGoodieFmv;
    if (id == 76u) return kGoodieMesh;
    if (id == 77u) return kGoodieFmv;
    if (id > 200u) return kGoodieFmv;
    return kGoodieImage;
}

inline unsigned CountGoodies(GoodieKind kind) {
    unsigned n = 0;
    for (unsigned id = 0; id < kGoodieTotal; ++id)
        if (ClassifyGoodie(id) == kind) ++n;
    return n;
}

inline int GoodieIdAt(GoodieKind kind, unsigned ordinal) {
    unsigned seen = 0;
    for (unsigned id = 0; id < kGoodieTotal; ++id) {
        if (ClassifyGoodie(id) == kind) {
            if (seen == ordinal) return static_cast<int>(id);
            ++seen;
        }
    }
    return -1;
}

inline int BonusLevelNumberForGoodie(int goodieId) {
    switch (goodieId) {
    case 66: return 901;
    case 67: return 902;
    case 68: return 903;
    case 69: return 904;
    case 70: return 905;
    default: return 0;
    }
}

// -----------------------------------------------------------------------------
// Pure Pedestal Placement, Controls, Ray-Picking & State Machine
// -----------------------------------------------------------------------------
struct BasePose {
    float localX;
    float localY;
    float plinthTopZ;
    float plinthHalf;
    bool customWorldPose;
};

inline BasePose DefaultAtriumPose() {
    return BasePose{kPedestalLocalX, kPedestalLocalY, kPedestalPlinthTopZ, kPedestalPlinthHalf, false};
}

struct PedestalState {
    BasePose pose;
    Category category;
    unsigned selected[kCatCount];
    unsigned counts[kCatCount];
    float yawRad;
    float fadeAlpha; // 0 -> 1 fade-in after each swap
    bool paused;
    bool initialized;
    bool fireHeld;
    bool fireCaptured;
    bool visitorNear;
    int hoveredButton; // -1 or 0..kBtnCount-1
    unsigned swapGeneration;
};

inline void ResetState(PedestalState *s) {
    if (!s) return;
    s->pose = DefaultAtriumPose();
    s->category = kCatRetailMeshes;
    for (int i = 0; i < kCatCount; ++i) {
        s->selected[i] = 0;
        s->counts[i] = 0;
    }
    s->counts[kCatGoodieModels] = CountGoodies(kGoodieMesh);
    s->counts[kCatConceptArt] = CountGoodies(kGoodieImage);
    s->counts[kCatCinemaFmvs] = CountGoodies(kGoodieFmv);
    s->counts[kCatBonusLevels] = CountGoodies(kGoodieLevel);
    s->yawRad = 0.45f;
    s->fadeAlpha = 1.0f;
    s->paused = false;
    s->initialized = true;
    s->fireHeld = false;
    s->fireCaptured = false;
    s->visitorNear = false;
    s->hoveredButton = -1;
    s->swapGeneration = 0;
}

inline bool ActiveInWing(const PedestalState &s, CeMuseumTour::Wing wing) {
    if (s.pose.customWorldPose) return true;
    return wing == CeMuseumTour::kWingAtrium || wing == CeMuseumTour::kWingCourtyard;
}

inline bool InteractiveInWing(const PedestalState &s, CeMuseumTour::Wing wing) {
    if (s.pose.customWorldPose) return true;
    return wing == CeMuseumTour::kWingAtrium;
}

inline void ButtonPosition(const BasePose &pose, unsigned buttonIndex,
                           float *x, float *y, float *z) {
    const unsigned idx = buttonIndex < static_cast<unsigned>(kBtnCount) ? buttonIndex : 0u;
    if (x) *x = pose.localX + kButtons[idx].offsetX;
    if (y) *y = pose.localY + kControlOffsetY;
    if (z) *z = kControlLocalZ;
}

inline int PickButton(const BasePose &pose,
                      float eyeX, float eyeY, float eyeZ,
                      float aimX, float aimY, float aimZ) {
    const float alen = std::sqrt(aimX * aimX + aimY * aimY + aimZ * aimZ);
    if (!(alen > 1e-5f)) return -1;
    const float ux = aimX / alen, uy = aimY / alen, uz = aimZ / alen;
    const float maxRange = pose.customWorldPose ? kCustomWorldButtonMaxRange : kButtonMaxRange;
    int best = -1;
    float bestT = 1e9f;
    for (unsigned i = 0; i < static_cast<unsigned>(kBtnCount); ++i) {
        float bx = 0.0f, by = 0.0f, bz = 0.0f;
        ButtonPosition(pose, i, &bx, &by, &bz);
        const float vx = bx - eyeX, vy = by - eyeY, vz = bz - eyeZ;
        const float t = vx * ux + vy * uy + vz * uz;
        if (t <= 0.15f || t > maxRange) continue;
        const float cx = eyeX + t * ux - bx;
        const float cy = eyeY + t * uy - by;
        const float cz = eyeZ + t * uz - bz;
        if (cx * cx + cy * cy + cz * cz <= kButtonRadius * kButtonRadius && t < bestT) {
            bestT = t;
            best = static_cast<int>(i);
        }
    }
    return best;
}

inline bool IsVisitorNearPedestal(const BasePose &pose, float vx, float vy) {
    const float dx = pose.localX - vx;
    const float dy = pose.localY - vy;
    return (dx * dx + dy * dy) <= kKeyboardNearRange * kKeyboardNearRange;
}

inline bool FocusPedestal(const BasePose &pose, float vx, float vy, float fwdX, float fwdY) {
    const float flen = std::sqrt(fwdX * fwdX + fwdY * fwdY);
    if (!(flen > 1e-5f)) return false;
    const float fx = fwdX / flen, fy = fwdY / flen;
    const float dx = pose.localX - vx, dy = pose.localY - vy;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d < 0.25f || d > kFocusMaxRange) return false;
    const float cosA = (dx * fx + dy * fy) / d;
    return cosA >= 0.80f;
}

inline bool SelectNext(PedestalState *s) {
    if (!s || !s->initialized) return false;
    const unsigned n = s->counts[s->category];
    if (n == 0u) return false;
    s->selected[s->category] = (s->selected[s->category] + 1u) % n;
    s->fadeAlpha = 0.0f;
    ++s->swapGeneration;
    return true;
}

inline bool SelectPrev(PedestalState *s) {
    if (!s || !s->initialized) return false;
    const unsigned n = s->counts[s->category];
    if (n == 0u) return false;
    s->selected[s->category] = (s->selected[s->category] + n - 1u) % n;
    s->fadeAlpha = 0.0f;
    ++s->swapGeneration;
    return true;
}

inline bool CycleCategory(PedestalState *s) {
    if (!s || !s->initialized) return false;
    for (int step = 1; step <= kCatCount; ++step) {
        const Category next = static_cast<Category>((int(s->category) + step) % kCatCount);
        if (s->counts[next] > 0u) {
            s->category = next;
            if (s->selected[next] >= s->counts[next]) s->selected[next] = 0;
            s->fadeAlpha = 0.0f;
            ++s->swapGeneration;
            return true;
        }
    }
    return false;
}

inline bool TogglePause(PedestalState *s) {
    if (!s || !s->initialized) return false;
    s->paused = !s->paused;
    return true;
}

inline bool ActivateButton(PedestalState *s, int buttonIndex) {
    if (!s || !s->initialized || buttonIndex < 0 || buttonIndex >= kBtnCount) return false;
    switch (static_cast<ControlButton>(buttonIndex)) {
    case kBtnPrev:     return SelectPrev(s);
    case kBtnNext:     return SelectNext(s);
    case kBtnCategory: return CycleCategory(s);
    case kBtnPause:    return TogglePause(s);
    default:           return false;
    }
}

inline bool HandleKeyCode(PedestalState *s, unsigned dik, bool repeat) {
    if (!s || !s->initialized || !s->visitorNear) return false;
    switch (dik) {
    case kDikLBracket:
    case kDikPrior:
        return SelectPrev(s);
    case kDikRBracket:
    case kDikNext:
        return SelectNext(s);
    case kDikTab:
        return repeat ? true : CycleCategory(s);
    case kDikP:
        return repeat ? true : TogglePause(s);
    default:
        return false;
    }
}

inline void StepState(PedestalState *s, float dtSeconds, bool gamePaused) {
    if (!s || !s->initialized || gamePaused || !(dtSeconds > 0.0f)) return;
    const float dt = dtSeconds > 0.25f ? 0.25f : dtSeconds;
    if (!s->paused) {
        const float twoPi = 6.283185307f;
        float y = s->yawRad + kDefaultRateRadPerSec * dt;
        if (y >= twoPi) y -= twoPi;
        s->yawRad = y;
    }
    if (s->fadeAlpha < 1.0f) {
        s->fadeAlpha += dt / kSwapFadeSeconds;
        if (s->fadeAlpha > 1.0f) s->fadeAlpha = 1.0f;
    }
}

inline float ComputeNormalizedScale(float meshSpan, float targetSpan = kTargetSpan) {
    if (!(meshSpan > 1e-4f) || !(targetSpan > 0.0f)) return 1.0f;
    const float s = targetSpan / meshSpan;
    return s < 0.015f ? 0.015f : s > 8.0f ? 8.0f : s;
}

struct LightboxExtent {
    float halfW;
    float halfH;
};

inline LightboxExtent FitLightbox(unsigned srcW, unsigned srcH, float maxWidth, float maxHeight) {
    if (!srcW || !srcH || !(maxWidth > 0.0f) || !(maxHeight > 0.0f))
        return LightboxExtent{maxWidth * 0.5f, maxHeight * 0.5f};
    const float aspect = float(srcW) / float(srcH);
    const float boxAspect = maxWidth / maxHeight;
    float w = maxWidth, h = maxHeight;
    if (aspect > boxAspect) h = maxWidth / aspect;
    else w = maxHeight * aspect;
    return LightboxExtent{w * 0.5f, h * 0.5f};
}

// Formats a clean ASCII player-facing display title from an asset stem (no internal extensions).
inline void FormatDisplayTitle(const char *raw, char *out, unsigned outCap) {
    if (!out || !outCap) return;
    out[0] = '\0';
    if (!raw || !*raw) {
        std::snprintf(out, outCap, "Archival Item");
        return;
    }
    // Strip directory prefix if present.
    const char *start = raw;
    for (const char *q = raw; *q; ++q)
        if (*q == '/' || *q == '\\') start = q + 1;

    // Determine length without known file suffixes (.tga, .msh, .aya).
    size_t len = std::strlen(start);
    auto stripTail = [&](const char *suf) {
        const size_t sl = std::strlen(suf);
        if (len <= sl) return;
        for (size_t i = 0; i < sl; ++i) {
            unsigned char a = static_cast<unsigned char>(start[len - sl + i]);
            unsigned char b = static_cast<unsigned char>(suf[i]);
            if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b + ('a' - 'A'));
            if (a != b) return;
        }
        len -= sl;
    };
    stripTail(".aya");
    stripTail(".msh");
    stripTail(".tga");

    unsigned used = 0;
    bool newWord = true;
    for (size_t i = 0; i < len && used + 1u < outCap; ++i) {
        const unsigned char c = static_cast<unsigned char>(start[i]);
        if (c == '_' || c == '-' || c == ' ') {
            if (used > 0u && out[used - 1u] != ' ') out[used++] = ' ';
            newWord = true;
            continue;
        }
        if (c < 0x20u || c > 0x7Eu || c == '[' || c == ']' || c == '|') continue;
        char ch = static_cast<char>(c);
        if (newWord && ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - ('a' - 'A'));
        out[used++] = ch;
        newWord = false;
    }
    while (used > 0u && out[used - 1u] == ' ') --used;
    out[used] = '\0';
    if (used == 0u) std::snprintf(out, outCap, "Archival Item");
}

// Exported Runtime Seam Functions (auto-hooked by ce_museum_hall.cpp and portable to other worlds).
void RuntimeInit();
void RuntimeShutdown();
void RuntimeUpdate(CeMuseumTour::Wing wing, float dtSeconds, bool paused,
                   float eyeX, float eyeY, float eyeZ,
                   float aimX, float aimY, float aimZ);
bool RuntimeHandleFire(bool release, float value);
bool RuntimeHandleKey(unsigned dik, bool repeat);
bool RuntimeIsAimingControl();
bool RuntimeCue(float visitorX, float visitorY, float fwdX, float fwdY,
                CeMuseumTour::Wing wing,
                char *title, unsigned titleCap,
                char *line, unsigned lineCap,
                char *note, unsigned noteCap);
void RuntimeDraw(float floorZ, bool ownStateBlock = true);
void RuntimeSetBasePose(float worldCenterX, float worldCenterY, float plinthTopZ);

} // namespace CeArchivePedestal

#endif
