// SPDX-License-Identifier: GPL-3.0-only
// Minimal authored world writer. It never copies campaign actors, bytecode,
// scripts, navigation, timers or automatic mesh spawns into the exhibition.
#include "ce_network_stl.h"
#include "ce_museum_visit.h"
#include "oids.h"
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
const unsigned kExhibits = 64;
const unsigned kNameLimit = 127;
const unsigned kBodyLimit = 1024 * 1024;

bool nameLength(const char *text, unsigned *length) {
    if (!text || !length) return false;
    unsigned n = 0;
    while (n <= kNameLimit && text[n]) {
        const unsigned char c = static_cast<unsigned char>(text[n]);
        if (c < 32 || c == 127) return false;
        ++n;
    }
    if (!n || n > kNameLimit) return false;
    *length = n;
    return true;
}

bool vectorValid(const float *value, float bound) {
    for (unsigned i = 0; i != 3; ++i)
        if (!(value[i] >= -bound && value[i] <= bound)) return false;
    return true;
}

struct Writer {
    std::vector<unsigned char> bytes;
    void u8(unsigned value) { bytes.push_back(static_cast<unsigned char>(value)); }
    void u16(unsigned value) { u8(value); u8(value >> 8); }
    void u32(std::uint32_t value) {
        u8(value); u8(value >> 8); u8(value >> 16); u8(value >> 24);
    }
    void scalar(float value) {
        static_assert(sizeof(float) == sizeof(std::uint32_t), "world float width");
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        u32(bits);
    }
    void vector(const float *value) { for (unsigned i = 0; i != 3; ++i) scalar(value[i]); }
    void shortName(const char *value) {
        const unsigned n = static_cast<unsigned>(std::strlen(value));
        u8(n);
        bytes.insert(bytes.end(), value, value + n);
    }
    void terminated(const char *value) {
        const unsigned n = static_cast<unsigned>(std::strlen(value));
        bytes.insert(bytes.end(), value, value + n + 1);
    }
    void init(const char *name, const float *position, const float *orientation) {
        vector(position); vector(orientation);
        u32(0); u32(0); u32(0); // mesh slot, allegiance and target
        terminated(""); terminated(name); terminated("");
        u32(1); u32(0); // active; no scripts attached to units
    }
};

struct Reader {
    CeMuseumVisit::SourceReader read;
    void *context;
    unsigned consumed;
    bool take(void *bytes, unsigned length) {
        if (length > 4096 - consumed || !read(bytes, length, context)) return false;
        consumed += length;
        return true;
    }
    bool u16(unsigned *value) {
        unsigned char bytes[2];
        if (!take(bytes, sizeof bytes)) return false;
        *value = bytes[0] | (static_cast<unsigned>(bytes[1]) << 8);
        return true;
    }
    bool u32(std::uint32_t *value) {
        unsigned char bytes[4];
        if (!take(bytes, sizeof bytes)) return false;
        *value = bytes[0] | (static_cast<std::uint32_t>(bytes[1]) << 8) |
                 (static_cast<std::uint32_t>(bytes[2]) << 16) |
                 (static_cast<std::uint32_t>(bytes[3]) << 24);
        return true;
    }
};

bool buildBody(const CeMuseumVisit::RoomPlan &plan, bool real, Writer *out) {
    Writer &w = *out;
    w.bytes.reserve(512 + plan.exhibitCount * 256);
    w.u16(50); // world format used by the standalone authored-world codec
    w.u32(3); w.u32(0); w.u32(0); // header and unused fields
    w.u32(1); w.shortName(plan.playerConfiguration);
    w.u32(0); w.u32(0); w.u32(0); w.u32(0); // header words; ordinary world
    w.u32(real ? static_cast<std::uint32_t>(plan.baseLevel) : UINT32_MAX);
    w.u32(0); // no mission scripts
    w.u32(static_cast<std::uint32_t>(plan.mapNumber));
    w.u32(0); // no named meshes or automatic unused-mesh spawns
    w.u16(real ? 1 + plan.exhibitCount : 0);
    if (real) {
        w.u32(OID_CStart);
        w.init("ce.museum.visitor", plan.visitorPosition, plan.visitorOrientation);
        w.u32(0); w.u32(1); // walker; primary player identity is one-based
        for (unsigned i = 0; i != plan.exhibitCount; ++i) {
            const CeMuseumVisit::ExhibitPlacement &item = plan.exhibits[i];
            char label[48];
            const int n = std::snprintf(label, sizeof label, "ce.museum.exhibit.%u", i + 1);
            if (n < 0 || static_cast<unsigned>(n) >= sizeof label) return false;
            w.u32(OID_CFeature);
            w.init(label, item.position, item.orientation);
            w.shortName(item.definition);
            w.u32(0); // no scheduled death
        }
    }
    w.u16(0); w.u32(0); // no tree groups or tree types
    w.u16(1); w.u32(0); w.u32(0); // empty influence map
    w.u16(0); // no waypoint paths
    for (unsigned i = 0; i != 5; ++i) w.scalar(0);
    w.u32(0); w.u32(0); // no grading thresholds
    for (unsigned i = 0; i != 4; ++i) w.scalar(0);
    for (unsigned i = 0; i != 3; ++i) w.u32(0); // no carried occupancy grids
    w.scalar(0); // no campaign start time
    return !w.bytes.empty() && w.bytes.size() <= kBodyLimit;
}
}

bool CeMuseumVisit::GenerateRoom(const RoomPlan &plan, RoomReceiver receiver, void *context) {
    unsigned length;
    if (!receiver || !nameLength(plan.playerConfiguration, &length) ||
        plan.baseLevel < 0 || plan.baseLevel > 999 || plan.mapNumber < 0 || plan.mapNumber > 999 ||
        !plan.exhibitCount || plan.exhibitCount > kExhibits || !plan.exhibits ||
        !vectorValid(plan.visitorPosition, 4096) || !vectorValid(plan.visitorOrientation, 1000)) return false;
    for (unsigned i = 0; i != plan.exhibitCount; ++i) {
        const ExhibitPlacement &item = plan.exhibits[i];
        if (!nameLength(item.definition, &length) || !vectorValid(item.position, 4096) ||
            !vectorValid(item.orientation, 1000)) return false;
    }
    try {
        Writer base, real;
        if (!buildBody(plan, false, &base) || !buildBody(plan, true, &real)) return false;
        return receiver(base.bytes.data(), static_cast<unsigned>(base.bytes.size()),
                        real.bytes.data(), static_cast<unsigned>(real.bytes.size()), context);
    } catch (...) {
        return false;
    }
}

bool CeMuseumVisit::ReadCarrierPrefix(SourceReader reader, void *context, bool requireMap, CarrierPrefix *prefix) {
    if (!reader || !prefix) return false;
    try {
        Reader source = {reader, context, 0};
        CarrierPrefix result = {};
        result.mapNumber = -1;
        unsigned version;
        std::uint32_t word;
        if (!source.u16(&version) || version != 50 || !source.u32(&word) || word != 3) return false;
        if (!source.u32(&word) || !source.u32(&word)) return false;
        if (!source.u32(&word) || word != 1) return false;
        unsigned char length;
        if (!source.take(&length, 1) || !length || length > kNameLimit ||
            !source.take(result.playerConfiguration, length)) return false;
        result.playerConfiguration[length] = 0;
        unsigned checkedLength;
        if (!nameLength(result.playerConfiguration, &checkedLength) || checkedLength != length) return false;
        for (unsigned i = 0; i != 3; ++i) if (!source.u32(&word)) return false;
        if (!source.u32(&word) || word != 0 || !source.u32(&word)) return false;
        if (word != UINT32_MAX && word > 999) return false;
        result.parentWorld = word == UINT32_MAX ? -1 : static_cast<int>(word);
        if (requireMap) {
            if (!source.u32(&word) || word != 0 || !source.u32(&word) || word > 999) return false;
            result.mapNumber = static_cast<int>(word);
        }
        *prefix = result;
        return true;
    } catch (...) {
        return false;
    }
}

namespace {
const int kSiteMargin = 32;        // grid cells kept clear of the map's edge
const int kSiteStep = 8;           // spacing of the ground samples that judge a site
const int kRingStep = 16;          // spacing of the samples that judge the dry ring around a site
const float kSiteGap = 8.0f;       // clear ground between neighbouring exhibits
const float kSiteStandoff = 24.0f; // from the front of the first row back to the visitor's feet
const float kFlatEnough = 0.5f;    // a site this flat, with every exhibit in view, is taken at once
const float kEyeHeight = 4.0f;     // the visitor's eye above the ground
const float kRayStep = 3.0f;       // spacing of the samples along a line of sight
const int kRings[3] = {48, 24, 0}; // dry ring around the site, widest first

int Cell(float value) { return static_cast<int>(std::floor(value + 0.5f)); }
bool Plausible(float extent) { return extent > 0.0f && extent <= 256.0f; }  // false for NaN as well

struct Probe {
    CeMuseumVisit::GroundHeight ground;
    void *context;
    bool at(float x, float y, float *height) const {
        float value;
        if (!ground(Cell(x), Cell(y), &value, context) || !std::isfinite(value)) return false;
        *height = value;
        return true;
    }
};

// Is the exhibit's upper part in view of the visitor's eye? Z counts downward, so a point is above the ground when its Z is smaller.
bool InView(const Probe &probe, float eyeX, float eyeY, float eyeZ, float x, float y, float topZ) {
    const float dx = x - eyeX, dy = y - eyeY, dz = topZ - eyeZ;
    const float length = std::sqrt(dx * dx + dy * dy);
    const int steps = static_cast<int>(length / kRayStep);
    for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        float ground;
        if (!probe.at(eyeX + dx * t, eyeY + dy * t, &ground)) return false;
        if (eyeZ + dz * t >= ground - 0.3f) return false;
    }
    return true;
}
}

bool CeMuseumVisit::PlanSite(const ExhibitFootprint *footprints, unsigned count, int gridWidth, int gridHeight, GroundHeight ground,
                            void *context, SitePlan *plan) {
    if (!footprints || !ground || !plan || !count || count > kMaxSiteExhibits ||
        gridWidth < 2 * kSiteMargin + 16 || gridHeight < 2 * kSiteMargin + 16 || gridWidth > 4096 || gridHeight > 4096) return false;
    for (unsigned i = 0; i != count; ++i)
        if (!Plausible(footprints[i].width) || !Plausible(footprints[i].depth) || !Plausible(footprints[i].height)) return false;
    const unsigned columns = count < 3 ? count : 3, rows = (count + columns - 1) / columns;
    float columnWidth[3] = {0, 0, 0}, rowDepth[kMaxSiteExhibits] = {};
    for (unsigned i = 0; i != count; ++i) {
        const unsigned column = i % columns, row = i / columns;
        if (footprints[i].width + kSiteGap > columnWidth[column]) columnWidth[column] = footprints[i].width + kSiteGap;
        if (footprints[i].depth + kSiteGap > rowDepth[row]) rowDepth[row] = footprints[i].depth + kSiteGap;
    }
    float blockWidth = 0, blockDepth = 0;
    for (unsigned column = 0; column != columns; ++column) blockWidth += columnWidth[column];
    for (unsigned row = 0; row != rows; ++row) blockDepth += rowDepth[row];
    const float front = kSiteStandoff + kSiteGap, totalDepth = blockDepth + front;
    const int spanX = Cell(blockWidth), spanY = Cell(totalDepth);
    if (spanX + 2 * kSiteMargin > gridWidth || spanY + 2 * kSiteMargin > gridHeight) return false;
    // Cell centres and the visitor's standing point, relative to the site's corner.
    float columnStart[3] = {0, 0, 0}, rowStart[kMaxSiteExhibits] = {};
    for (unsigned column = 1; column != columns; ++column) columnStart[column] = columnStart[column - 1] + columnWidth[column - 1];
    for (unsigned row = 1; row != rows; ++row) rowStart[row] = rowStart[row - 1] + rowDepth[row - 1];
    float relativeX[kMaxSiteExhibits], relativeY[kMaxSiteExhibits];
    for (unsigned i = 0; i != count; ++i) {
        relativeX[i] = columnStart[i % columns] + columnWidth[i % columns] / 2;
        relativeY[i] = front + rowStart[i / columns] + rowDepth[i / columns] / 2;
    }
    const float visitorX = blockWidth / 2, visitorY = kSiteGap / 2;
    const int largest = gridWidth > gridHeight ? gridWidth : gridHeight;
    const int stride = largest / 32 > 16 ? largest / 32 : 16;
    const Probe probe = {ground, context};
    const float infinity = std::numeric_limits<float>::infinity();
    float bestRange = infinity, bestMean = 0;
    unsigned bestVisible = 0, bestRing = 0;
    int bestX = 0, bestY = 0;
    bool found = false;
    for (unsigned tier = 0; tier != 3 && !found; ++tier) {
        const int ring = kRings[tier];
        for (int y0 = kSiteMargin; y0 + spanY + kSiteMargin <= gridHeight; y0 += stride) {
            for (int x0 = kSiteMargin; x0 + spanX + kSiteMargin <= gridWidth; x0 += stride) {
                float low = infinity, high = -infinity, sum = 0;
                unsigned samples = 0;
                bool usable = true;
                for (int dy = 0; usable && dy <= spanY; dy += kSiteStep)
                    for (int dx = 0; dx <= spanX; dx += kSiteStep) {
                        float height;
                        if (!probe.at(static_cast<float>(x0 + dx), static_cast<float>(y0 + dy), &height)) { usable = false; break; }
                        if (height < low) low = height;
                        if (height > high) high = height;
                        sum += height; ++samples;
                    }
                // The ring: the rectangle `ring` cells beyond the site, sampled along its edges.
                for (int edge = 0; usable && ring && edge != 4; ++edge) {
                    const int left = x0 - ring, top = y0 - ring, width = spanX + 2 * ring, height = spanY + 2 * ring;
                    for (int along = 0; along <= (edge < 2 ? width : height); along += kRingStep) {
                        const int x = edge == 0 ? left + along : edge == 1 ? left + along : edge == 2 ? left : left + width;
                        const int y = edge == 0 ? top : edge == 1 ? top + height : top + along;
                        float ignored;
                        if (!probe.at(static_cast<float>(x), static_cast<float>(y), &ignored)) { usable = false; break; }
                    }
                }
                if (!usable) continue;
                const float range = high - low, mean = sum / static_cast<float>(samples);
                // Which exhibits can the visitor see from the standing point?
                unsigned visible = 0;
                float eyeGround;
                const float eyeX = x0 + visitorX, eyeY = y0 + visitorY;
                if (!probe.at(eyeX, eyeY, &eyeGround)) continue;
                for (unsigned i = 0; i != count; ++i) {
                    const float cx = x0 + relativeX[i], cy = y0 + relativeY[i];
                    float base;
                    if (!probe.at(cx, cy, &base)) continue;
                    if (InView(probe, eyeX, eyeY, eyeGround - kEyeHeight, cx, cy, base - footprints[i].height * 0.7f)) ++visible;
                }
                if (visible < bestVisible || (visible == bestVisible && range >= bestRange)) continue;
                found = true;
                bestRange = range; bestMean = mean; bestVisible = visible; bestRing = static_cast<unsigned>(ring);
                bestX = x0; bestY = y0;
                if (visible == count && range <= kFlatEnough) { tier = 3; y0 = gridHeight; break; }
            }
        }
    }
    if (!found) return false;
    SitePlan result = {};
    result.heightRange = bestRange; result.visible = bestVisible; result.dryRing = bestRing;
    for (unsigned i = 0; i != count; ++i) {
        const float x = bestX + relativeX[i], y = bestY + relativeY[i];
        float height;
        result.centres[i][0] = x; result.centres[i][1] = y; result.centres[i][2] = probe.at(x, y, &height) ? height : bestMean;
    }
    float height;
    result.visitor[0] = bestX + visitorX; result.visitor[1] = bestY + visitorY;
    result.visitor[2] = probe.at(result.visitor[0], result.visitor[1], &height) ? height : bestMean;
    *plan = result;
    return true;
}
