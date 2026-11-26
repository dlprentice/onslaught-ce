// SPDX-License-Identifier: GPL-3.0-only
// Decompresses and uploads the Grand Museum's original GPL-3.0-only architectural textures,
// and bakes the 1024x512 3D Portal Lintel Signage Atlas and 512x512 Atrium Directory
// Floor-Plan & Labeled Console Buttons Atlas once at LoadAll() into D3DPOOL_MANAGED textures
// with full mipmap chains (D3D9 Reset-safe, zero external dependencies).
// Plain-colour fallback remains active if any texture fails upload or is forced missing by
// BEA_PORT_MUSEUM_MISSING_TEXTURE.
#if defined(CE_VERSION) || defined(CE_MUSEUM_TEXTURES_HOST_TEST)

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifndef CE_MUSEUM_TEXTURES_HOST_TEST
#include "common.h"
#include "ltshell.h"
#include "d3dx9.h"
#include "ce_museum_textures.h"
#include "ce_museum_textures_data.h"
#include "ce_museum_exhibition.h"
#include "ce_museum_tour.h"
#include "../platform/common/portlog.h"
#else
#include "ce_museum_textures.h"
#include "ce_museum_exhibition.h"
#include "ce_museum_tour.h"
#endif

#ifndef CE_MUSEUM_TEXTURES_HOST_TEST
extern "C" int uncompress(unsigned char *dest, unsigned long *destLen,
                          const unsigned char *source, unsigned long sourceLen);
extern "C" uint64_t PortCeD3dDeviceGeneration(void) __attribute__((weak));
#endif

namespace CeMuseumTextures {

namespace {

static const char *const kPortalSignLabels[CE_MUSEUM_SIGN_COUNT] = {
    "FORSETI FORCES",
    "MUSPELL DOMINION",
    "AVIATION & NAVAL HANGAR",
    "CONCEPT ART GALLERY",
    "EARLY DESIGNS VAULT",
    "SCREENING ROOM",
    "GRAND ATRIUM",
    "COASTAL COURTYARD",
};

static const char *const kDirectoryButtonLabels[CE_MUSEUM_DIR_BTN_COUNT] = {
    "FORSETI",
    "MUSPELL",
    "HANGAR",
    "CONCEPT ART",
    "EARLY DESIGNS",
    "CINEMA",
};

// Directory button wing accent colours matching the color-coded floor-plan map.
static const uint32_t kDirectoryButtonAccents[CE_MUSEUM_DIR_BTN_COUNT] = {
    0xff5cb8e8u, // Forseti steel-cyan
    0xffe88a40u, // Muspell molten amber
    0xff7ec8d8u, // Hangar aerospace teal
    0xffd89e78u, // Concept Art warm gallery rose-gold
    0xffd4b464u, // Early Designs archival gold
    0xffdc6878u, // Screening Room cinema crimson
};

static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static inline uint32_t PackArgb(uint32_t a, uint32_t r, uint32_t g, uint32_t b) {
    return ((a & 255u) << 24) | ((r & 255u) << 16) | ((g & 255u) << 8) | (b & 255u);
}

static inline uint32_t BlendRgb(uint32_t dst, uint32_t src, float t) {
    t = Clamp01(t);
    const float dr = float((dst >> 16) & 255u), dg = float((dst >> 8) & 255u), db = float(dst & 255u);
    const float sr = float((src >> 16) & 255u), sg = float((src >> 8) & 255u), sb = float(src & 255u);
    const uint32_t r = static_cast<uint32_t>(dr + (sr - dr) * t + 0.5f);
    const uint32_t g = static_cast<uint32_t>(dg + (sg - dg) * t + 0.5f);
    const uint32_t b = static_cast<uint32_t>(db + (sb - db) * t + 0.5f);
    return PackArgb(255u, r > 255u ? 255u : r, g > 255u ? 255u : g, b > 255u ? 255u : b);
}

static inline void FillRect(uint32_t *buf, unsigned bw, unsigned bh,
                            int x0, int y0, int x1, int y1, uint32_t col) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > static_cast<int>(bw)) x1 = static_cast<int>(bw);
    if (y1 > static_cast<int>(bh)) y1 = static_cast<int>(bh);
    for (int y = y0; y < y1; ++y) {
        uint32_t *row = buf + static_cast<unsigned>(y) * bw;
        for (int x = x0; x < x1; ++x) row[x] = col;
    }
}

static inline void FillRectAlpha(uint32_t *buf, unsigned bw, unsigned bh,
                                 int x0, int y0, int x1, int y1, uint32_t col, float alpha) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > static_cast<int>(bw)) x1 = static_cast<int>(bw);
    if (y1 > static_cast<int>(bh)) y1 = static_cast<int>(bh);
    for (int y = y0; y < y1; ++y) {
        uint32_t *row = buf + static_cast<unsigned>(y) * bw;
        for (int x = x0; x < x1; ++x) row[x] = BlendRgb(row[x], col, alpha);
    }
}

static inline void DrawRectOutline(uint32_t *buf, unsigned bw, unsigned bh,
                                   int x0, int y0, int x1, int y1, int thick, uint32_t col) {
    if (thick < 1) thick = 1;
    FillRect(buf, bw, bh, x0, y0, x1, y0 + thick, col);
    FillRect(buf, bw, bh, x0, y1 - thick, x1, y1, col);
    FillRect(buf, bw, bh, x0, y0 + thick, x0 + thick, y1 - thick, col);
    FillRect(buf, bw, bh, x1 - thick, y0 + thick, x1, y1 - thick, col);
}

static inline void DrawDiscRgba(uint32_t *buf, unsigned bw, unsigned bh,
                                float cx, float cy, float radius, uint32_t col) {
    const int x0 = static_cast<int>(std::floor(cx - radius - 1.5f));
    const int x1 = static_cast<int>(std::ceil(cx + radius + 1.5f));
    const int y0 = static_cast<int>(std::floor(cy - radius - 1.5f));
    const int y1 = static_cast<int>(std::ceil(cy + radius + 1.5f));
    const int bx0 = x0 < 0 ? 0 : x0;
    const int by0 = y0 < 0 ? 0 : y0;
    const int bx1 = x1 > static_cast<int>(bw) ? static_cast<int>(bw) : x1;
    const int by1 = y1 > static_cast<int>(bh) ? static_cast<int>(bh) : y1;
    for (int y = by0; y < by1; ++y) {
        for (int x = bx0; x < bx1; ++x) {
            const float d = std::hypot(float(x) + 0.5f - cx, float(y) + 0.5f - cy);
            const float cov = Clamp01((radius + 0.65f - d) / 1.3f);
            if (cov > 0.0f) {
                uint32_t &px = buf[static_cast<unsigned>(y) * bw + static_cast<unsigned>(x)];
                px = BlendRgb(px, col, cov);
            }
        }
    }
}

static inline void DrawLineRgba(uint32_t *buf, unsigned bw, unsigned bh,
                                float x0, float y0, float x1, float y1,
                                float radius, uint32_t col) {
    const float minX = (x0 < x1 ? x0 : x1) - radius - 1.5f;
    const float maxX = (x0 > x1 ? x0 : x1) + radius + 1.5f;
    const float minY = (y0 < y1 ? y0 : y1) - radius - 1.5f;
    const float maxY = (y0 > y1 ? y0 : y1) + radius + 1.5f;
    const int bx0 = minX < 0.0f ? 0 : static_cast<int>(std::floor(minX));
    const int by0 = minY < 0.0f ? 0 : static_cast<int>(std::floor(minY));
    const int bx1 = maxX > float(bw) ? static_cast<int>(bw) : static_cast<int>(std::ceil(maxX));
    const int by1 = maxY > float(bh) ? static_cast<int>(bh) : static_cast<int>(std::ceil(maxY));
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;
    for (int y = by0; y < by1; ++y) {
        const float py = float(y) + 0.5f;
        for (int x = bx0; x < bx1; ++x) {
            const float px = float(x) + 0.5f;
            float t = len2 > 1e-6f ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0.0f;
            t = Clamp01(t);
            const float d = std::hypot(px - (x0 + t * dx), py - (y0 + t * dy));
            const float cov = Clamp01((radius + 0.65f - d) / 1.3f);
            if (cov > 0.0f) {
                uint32_t &p = buf[static_cast<unsigned>(y) * bw + static_cast<unsigned>(x)];
                p = BlendRgb(p, col, cov);
            }
        }
    }
}

// =============================================================================
// Self-contained GPL-3.0-only geometric uppercase stroke-caps SDF rasterizer
// Supports A-Z, 0-9, &, -, :, . (and folds lowercase a-z to uppercase).
// Accumulates max capsule-SDF coverage per pixel so joins have zero bright seams.
// =============================================================================

template <typename T>
struct RawBuf {
    T *ptr;
    size_t count;
    size_t cap;

    RawBuf() : ptr(nullptr), count(0), cap(0) {}
    explicit RawBuf(size_t n) : ptr(nullptr), count(0), cap(0) {
        if (n > 0) {
            ptr = static_cast<T *>(std::calloc(n, sizeof(T)));
            if (ptr) { count = n; cap = n; }
        }
    }
    RawBuf(size_t n, T initVal) : ptr(nullptr), count(0), cap(0) {
        if (n > 0) {
            ptr = static_cast<T *>(std::malloc(n * sizeof(T)));
            if (ptr) {
                count = n;
                cap = n;
                for (size_t i = 0; i < n; ++i) ptr[i] = initVal;
            }
        }
    }
    ~RawBuf() { std::free(ptr); }
    RawBuf(const RawBuf &) = delete;
    RawBuf &operator=(const RawBuf &) = delete;

    bool resize(size_t n) {
        if (n == 0) { count = 0; return true; }
        if (n <= cap && ptr) { count = n; return true; }
        T *p = static_cast<T *>(std::malloc(n * sizeof(T)));
        if (!p) return false;
        std::free(ptr);
        ptr = p;
        count = n;
        cap = n;
        return true;
    }

    T *data() { return ptr; }
    const T *data() const { return ptr; }
    size_t size() const { return count; }
    bool empty() const { return !ptr || count == 0; }
    T &operator[](size_t i) { return ptr[i]; }
    const T &operator[](size_t i) const { return ptr[i]; }
};

struct StrokeMask {
    unsigned w, h;
    RawBuf<float> cov;

    StrokeMask(unsigned width, unsigned height)
        : w(width), h(height), cov(static_cast<size_t>(width) * static_cast<size_t>(height)) {}

    void Clear() {
        if (!cov.empty()) std::memset(cov.data(), 0, cov.size() * sizeof(float));
    }

    void Seg(float x0, float y0, float x1, float y1, float radius) {
        if (cov.empty()) return;
        const float minX = (x0 < x1 ? x0 : x1) - radius - 1.5f;
        const float maxX = (x0 > x1 ? x0 : x1) + radius + 1.5f;
        const float minY = (y0 < y1 ? y0 : y1) - radius - 1.5f;
        const float maxY = (y0 > y1 ? y0 : y1) + radius + 1.5f;
        const int bx0 = minX < 0.0f ? 0 : static_cast<int>(std::floor(minX));
        const int by0 = minY < 0.0f ? 0 : static_cast<int>(std::floor(minY));
        const int bx1 = maxX > float(w) ? static_cast<int>(w) : static_cast<int>(std::ceil(maxX));
        const int by1 = maxY > float(h) ? static_cast<int>(h) : static_cast<int>(std::ceil(maxY));
        const float dx = x1 - x0, dy = y1 - y0;
        const float len2 = dx * dx + dy * dy;
        for (int y = by0; y < by1; ++y) {
            const float py = float(y) + 0.5f;
            float *row = &cov[static_cast<unsigned>(y) * w];
            for (int x = bx0; x < bx1; ++x) {
                const float px = float(x) + 0.5f;
                float t = len2 > 1e-6f ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0.0f;
                t = Clamp01(t);
                const float d = std::hypot(px - (x0 + t * dx), py - (y0 + t * dy));
                const float c = Clamp01((radius + 0.65f - d) / 1.3f);
                if (c > row[x]) row[x] = c;
            }
        }
    }

    void Arc(float cx, float cy, float rx, float ry, float deg0, float deg1, float radius) {
        constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
        const float span = std::fabs(deg1 - deg0);
        unsigned steps = static_cast<unsigned>(std::ceil(span / 12.0f));
        if (steps < 8u) steps = 8u;
        float px = cx + std::cos(deg0 * kDeg2Rad) * rx;
        float py = cy + std::sin(deg0 * kDeg2Rad) * ry;
        for (unsigned i = 1u; i <= steps; ++i) {
            const float a = (deg0 + (deg1 - deg0) * (float(i) / float(steps))) * kDeg2Rad;
            const float nx = cx + std::cos(a) * rx;
            const float ny = cy + std::sin(a) * ry;
            Seg(px, py, nx, ny, radius);
            px = nx;
            py = ny;
        }
    }
};

static inline char FoldUpper(char ch) {
    return (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 'a' + 'A') : ch;
}

// Proportional architectural capital widths relative to glyph height (1.0).
static float GlyphWidthRatio(char ch) {
    switch (FoldUpper(ch)) {
    case ' ': return 0.44f;
    case 'I': return 0.36f;
    case 'J': return 0.64f;
    case 'F': case 'L': case 'E': return 0.66f;
    case 'M': return 0.96f;
    case 'W': return 1.04f;
    case 'O': case 'Q': case 'G': return 0.84f;
    case 'A': case 'V': case 'N': case 'H': case 'D': case 'C': case 'U': case 'X': case 'Y': case '&': return 0.80f;
    case '1': return 0.50f;
    case '-': return 0.52f;
    case ':': case '.': return 0.30f;
    default:  return 0.74f;
    }
}

static void RasterizeGlyph(StrokeMask &m, char rawCh, float gx, float gy,
                           float gw, float gh, float strokeRad) {
    const char ch = FoldUpper(rawCh);
    auto seg = [&](float x0, float y0, float x1, float y1) {
        m.Seg(gx + x0 * gw, gy + y0 * gh, gx + x1 * gw, gy + y1 * gh, strokeRad);
    };
    auto arc = [&](float cx, float cy, float rx, float ry, float a0, float a1) {
        m.Arc(gx + cx * gw, gy + cy * gh, rx * gw, ry * gh, a0, a1, strokeRad);
    };
    auto dot = [&](float cx, float cy) {
        m.Seg(gx + cx * gw, gy + cy * gh, gx + cx * gw, gy + cy * gh, strokeRad * 1.25f);
    };

    switch (ch) {
    case 'A':
        seg(0.06f, 1.00f, 0.50f, 0.00f);
        seg(0.50f, 0.00f, 0.94f, 1.00f);
        seg(0.22f, 0.67f, 0.78f, 0.67f);
        break;
    case 'B':
        seg(0.10f, 0.00f, 0.10f, 1.00f);
        seg(0.10f, 0.00f, 0.54f, 0.00f);
        seg(0.10f, 0.48f, 0.56f, 0.48f);
        seg(0.10f, 1.00f, 0.58f, 1.00f);
        arc(0.54f, 0.24f, 0.32f, 0.24f, -90.0f, 90.0f);
        arc(0.58f, 0.74f, 0.34f, 0.26f, -90.0f, 90.0f);
        break;
    case 'C':
        arc(0.54f, 0.50f, 0.46f, 0.50f, 40.0f, 320.0f);
        break;
    case 'D':
        seg(0.10f, 0.00f, 0.10f, 1.00f);
        seg(0.10f, 0.00f, 0.44f, 0.00f);
        seg(0.10f, 1.00f, 0.44f, 1.00f);
        arc(0.44f, 0.50f, 0.48f, 0.50f, -90.0f, 90.0f);
        break;
    case 'E':
        seg(0.12f, 0.00f, 0.12f, 1.00f);
        seg(0.12f, 0.00f, 0.90f, 0.00f);
        seg(0.12f, 0.49f, 0.80f, 0.49f);
        seg(0.12f, 1.00f, 0.90f, 1.00f);
        break;
    case 'F':
        seg(0.12f, 0.00f, 0.12f, 1.00f);
        seg(0.12f, 0.00f, 0.90f, 0.00f);
        seg(0.12f, 0.50f, 0.78f, 0.50f);
        break;
    case 'G':
        arc(0.50f, 0.50f, 0.45f, 0.50f, 18.0f, 322.0f);
        seg(0.54f, 0.52f, 0.93f, 0.52f);
        seg(0.93f, 0.52f, 0.93f, 0.66f);
        break;
    case 'H':
        seg(0.10f, 0.00f, 0.10f, 1.00f);
        seg(0.90f, 0.00f, 0.90f, 1.00f);
        seg(0.10f, 0.50f, 0.90f, 0.50f);
        break;
    case 'I':
        seg(0.50f, 0.00f, 0.50f, 1.00f);
        seg(0.14f, 0.00f, 0.86f, 0.00f);
        seg(0.14f, 1.00f, 0.86f, 1.00f);
        break;
    case 'J':
        seg(0.36f, 0.00f, 0.86f, 0.00f);
        seg(0.76f, 0.00f, 0.76f, 0.72f);
        arc(0.44f, 0.72f, 0.32f, 0.28f, 0.0f, 165.0f);
        break;
    case 'K':
        seg(0.10f, 0.00f, 0.10f, 1.00f);
        seg(0.88f, 0.00f, 0.12f, 0.54f);
        seg(0.34f, 0.39f, 0.92f, 1.00f);
        break;
    case 'L':
        seg(0.12f, 0.00f, 0.12f, 1.00f);
        seg(0.12f, 1.00f, 0.90f, 1.00f);
        break;
    case 'M':
        seg(0.06f, 1.00f, 0.06f, 0.00f);
        seg(0.06f, 0.00f, 0.50f, 0.66f);
        seg(0.50f, 0.66f, 0.94f, 0.00f);
        seg(0.94f, 0.00f, 0.94f, 1.00f);
        break;
    case 'N':
        seg(0.10f, 1.00f, 0.10f, 0.00f);
        seg(0.10f, 0.00f, 0.90f, 1.00f);
        seg(0.90f, 1.00f, 0.90f, 0.00f);
        break;
    case 'O':
        arc(0.50f, 0.50f, 0.45f, 0.50f, 0.0f, 360.0f);
        break;
    case 'P':
        seg(0.10f, 0.00f, 0.10f, 1.00f);
        seg(0.10f, 0.00f, 0.56f, 0.00f);
        seg(0.10f, 0.54f, 0.56f, 0.54f);
        arc(0.56f, 0.27f, 0.34f, 0.27f, -90.0f, 90.0f);
        break;
    case 'Q':
        arc(0.50f, 0.50f, 0.45f, 0.50f, 0.0f, 360.0f);
        seg(0.62f, 0.72f, 0.96f, 1.04f);
        break;
    case 'R':
        seg(0.10f, 0.00f, 0.10f, 1.00f);
        seg(0.10f, 0.00f, 0.54f, 0.00f);
        seg(0.10f, 0.52f, 0.54f, 0.52f);
        arc(0.54f, 0.26f, 0.34f, 0.26f, -90.0f, 90.0f);
        seg(0.52f, 0.52f, 0.90f, 1.00f);
        break;
    case 'S':
        arc(0.50f, 0.25f, 0.37f, 0.25f, 108.0f, 336.0f);
        seg(0.39f, 0.485f, 0.61f, 0.515f);
        arc(0.50f, 0.75f, 0.39f, 0.25f, -72.0f, 154.0f);
        break;
    case 'T':
        seg(0.04f, 0.00f, 0.96f, 0.00f);
        seg(0.50f, 0.00f, 0.50f, 1.00f);
        break;
    case 'U':
        seg(0.10f, 0.00f, 0.10f, 0.64f);
        seg(0.90f, 0.00f, 0.90f, 0.64f);
        arc(0.50f, 0.64f, 0.40f, 0.36f, 0.0f, 180.0f);
        break;
    case 'V':
        seg(0.06f, 0.00f, 0.50f, 1.00f);
        seg(0.50f, 1.00f, 0.94f, 0.00f);
        break;
    case 'W':
        seg(0.04f, 0.00f, 0.27f, 1.00f);
        seg(0.27f, 1.00f, 0.50f, 0.34f);
        seg(0.50f, 0.34f, 0.73f, 1.00f);
        seg(0.73f, 1.00f, 0.96f, 0.00f);
        break;
    case 'X':
        seg(0.08f, 0.00f, 0.92f, 1.00f);
        seg(0.92f, 0.00f, 0.08f, 1.00f);
        break;
    case 'Y':
        seg(0.06f, 0.00f, 0.50f, 0.52f);
        seg(0.94f, 0.00f, 0.50f, 0.52f);
        seg(0.50f, 0.52f, 0.50f, 1.00f);
        break;
    case 'Z':
        seg(0.08f, 0.00f, 0.92f, 0.00f);
        seg(0.92f, 0.00f, 0.08f, 1.00f);
        seg(0.08f, 1.00f, 0.92f, 1.00f);
        break;
    case '0':
        arc(0.50f, 0.50f, 0.40f, 0.50f, 0.0f, 360.0f);
        break;
    case '1':
        seg(0.22f, 0.22f, 0.54f, 0.00f);
        seg(0.54f, 0.00f, 0.54f, 1.00f);
        seg(0.18f, 1.00f, 0.88f, 1.00f);
        break;
    case '2':
        arc(0.50f, 0.26f, 0.38f, 0.26f, 198.0f, 386.0f);
        seg(0.84f, 0.38f, 0.12f, 1.00f);
        seg(0.12f, 1.00f, 0.90f, 1.00f);
        break;
    case '3':
        arc(0.48f, 0.25f, 0.38f, 0.25f, 208.0f, 450.0f);
        arc(0.50f, 0.74f, 0.40f, 0.26f, -90.0f, 155.0f);
        break;
    case '4':
        seg(0.68f, 1.00f, 0.68f, 0.00f);
        seg(0.68f, 0.00f, 0.10f, 0.68f);
        seg(0.10f, 0.68f, 0.92f, 0.68f);
        break;
    case '5':
        seg(0.84f, 0.00f, 0.18f, 0.00f);
        seg(0.18f, 0.00f, 0.14f, 0.45f);
        seg(0.14f, 0.45f, 0.50f, 0.42f);
        arc(0.50f, 0.71f, 0.40f, 0.29f, -90.0f, 156.0f);
        break;
    case '6':
        arc(0.54f, 0.50f, 0.42f, 0.50f, 162.0f, 312.0f);
        arc(0.50f, 0.72f, 0.38f, 0.28f, 0.0f, 360.0f);
        break;
    case '7':
        seg(0.10f, 0.00f, 0.90f, 0.00f);
        seg(0.90f, 0.00f, 0.34f, 1.00f);
        break;
    case '8':
        arc(0.50f, 0.25f, 0.34f, 0.25f, 0.0f, 360.0f);
        arc(0.50f, 0.74f, 0.40f, 0.26f, 0.0f, 360.0f);
        break;
    case '9':
        arc(0.50f, 0.28f, 0.38f, 0.28f, 0.0f, 360.0f);
        arc(0.46f, 0.50f, 0.42f, 0.50f, -18.0f, 132.0f);
        break;
    case '&':
        arc(0.42f, 0.21f, 0.21f, 0.20f, 135.0f, 405.0f);
        seg(0.57f, 0.35f, 0.18f, 0.61f);
        arc(0.44f, 0.73f, 0.31f, 0.27f, 25.0f, 206.0f);
        seg(0.72f, 0.84f, 0.90f, 0.55f);
        seg(0.28f, 0.35f, 0.88f, 1.00f);
        break;
    case '-':
        seg(0.12f, 0.52f, 0.88f, 0.52f);
        break;
    case ':':
        dot(0.50f, 0.30f);
        dot(0.50f, 0.78f);
        break;
    case '.':
        dot(0.50f, 0.90f);
        break;
    default:
        break;
    }
}

static float MeasureTextWidth(const char *text, float height, float tracking) {
    if (!text || !*text) return 0.0f;
    float w = 0.0f;
    for (const char *p = text; *p; ++p) {
        w += GlyphWidthRatio(*p) * height;
        if (p[1]) w += tracking * height;
    }
    return w;
}

static void DrawCenteredText(StrokeMask &mask, const char *text,
                             float centerX, float centerY,
                             float maxWidth, float height,
                             float tracking, float strokeRatio) {
    if (!text || !*text) return;
    float h = height;
    float tw = MeasureTextWidth(text, h, tracking);
    if (maxWidth > 1.0f && tw > maxWidth) {
        const float s = maxWidth / tw;
        h *= s;
        tw = maxWidth;
    }
    const float strokeRad = (h * strokeRatio) < 0.85f ? 0.85f : (h * strokeRatio);
    float x = centerX - tw * 0.5f;
    const float y = centerY - h * 0.5f;
    for (const char *p = text; *p; ++p) {
        const float gw = GlyphWidthRatio(*p) * h;
        if (*p != ' ') RasterizeGlyph(mask, *p, x, y, gw, h, strokeRad);
        x += gw + tracking * h;
    }
}

// Composite an engraved antique-brass text mask (with subtle top-bevel specular & engraved dark shadow)
// directly onto an ARGB8888 canvas.
static void CompositeEngravedMask(uint32_t *canvas, const StrokeMask &mask,
                                  uint32_t mainCol, uint32_t hiCol, uint32_t shadowCol,
                                  int shadowDx, int shadowDy) {
    if (!canvas || mask.cov.empty()) return;
    const unsigned w = mask.w, h = mask.h;
    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; ++x) {
            const float c = mask.cov[y * w + x];
            // Engraved drop shadow offset down-right
            const int sx = static_cast<int>(x) - shadowDx;
            const int sy = static_cast<int>(y) - shadowDy;
            if (sx >= 0 && sy >= 0 && sx < static_cast<int>(w) && sy < static_cast<int>(h)) {
                const float sc = mask.cov[static_cast<unsigned>(sy) * w + static_cast<unsigned>(sx)];
                if (sc > 0.01f && c < 0.99f) {
                    canvas[y * w + x] = BlendRgb(canvas[y * w + x], shadowCol, sc * 0.72f);
                }
            }
            if (c > 0.01f) {
                // Subtle upper-bevel highlight where pixel above has lower coverage
                const float above = y > 0 ? mask.cov[(y - 1u) * w + x] : 0.0f;
                const float bevel = Clamp01((c - above) * 1.15f);
                const uint32_t glyphCol = BlendRgb(mainCol, hiCol, bevel * 0.55f);
                canvas[y * w + x] = BlendRgb(canvas[y * w + x], glyphCol, c);
            }
        }
    }
}

static void BoxDownsample(const uint32_t *src, unsigned sw, unsigned sh,
                          uint32_t *dst, unsigned dw, unsigned dh) {
    const unsigned rx = sw / dw;
    const unsigned ry = sh / dh;
    const unsigned area = rx * ry;
    for (unsigned y = 0; y < dh; ++y) {
        for (unsigned x = 0; x < dw; ++x) {
            unsigned r = 0, g = 0, b = 0;
            for (unsigned dy = 0; dy < ry; ++dy) {
                const uint32_t *srow = src + (y * ry + dy) * sw + x * rx;
                for (unsigned dx = 0; dx < rx; ++dx) {
                    const uint32_t p = srow[dx];
                    r += (p >> 16) & 255u;
                    g += (p >> 8) & 255u;
                    b += p & 255u;
                }
            }
            dst[y * dw + x] = PackArgb(255u, (r + area / 2u) / area,
                                             (g + area / 2u) / area,
                                             (b + area / 2u) / area);
        }
    }
}

static void DownsampleRectClamped(const uint32_t *src, unsigned sw,
                                  unsigned x0, unsigned y0, unsigned x1, unsigned y1,
                                  unsigned cellX0, unsigned cellY0, unsigned cellX1, unsigned cellY1,
                                  uint32_t *outPx) {
    unsigned aSum = 0u, rSum = 0u, gSum = 0u, bSum = 0u, count = 0u;
    for (unsigned y = y0; y < y1; ++y) {
        const unsigned cy = y < cellY0 ? cellY0 : (y >= cellY1 ? cellY1 - 1u : y);
        const uint32_t *srow = src + cy * sw;
        for (unsigned x = x0; x < x1; ++x) {
            const unsigned cx = x < cellX0 ? cellX0 : (x >= cellX1 ? cellX1 - 1u : x);
            const uint32_t p = srow[cx];
            aSum += (p >> 24) & 255u;
            rSum += (p >> 16) & 255u;
            gSum += (p >> 8) & 255u;
            bSum += p & 255u;
            ++count;
        }
    }
    if (!count) { *outPx = 0xff000000u; return; }
    *outPx = PackArgb((aSum + count / 2u) / count,
                      (rSum + count / 2u) / count,
                      (gSum + count / 2u) / count,
                      (bSum + count / 2u) / count);
}

#ifndef CE_MUSEUM_TEXTURES_HOST_TEST
static_assert(static_cast<unsigned>(CE_MUSEUM_TEX_COUNT) == static_cast<unsigned>(CeMuseumTexturesData::kCount),
              "CeMuseumTexId must match CeMuseumTexturesData::kCount");

IDirect3DTexture9 *gTextures[CE_MUSEUM_TEX_COUNT] = {};
IDirect3DTexture9 *gPortalSignAtlas = NULL;
IDirect3DTexture9 *gDirectoryAtlas = NULL;
IDirect3DDevice9 *gDevice = NULL;
uint64_t gDeviceGen = 0u;
bool gLoaded = false;

uint64_t CurrentDeviceGen() {
    return PortCeD3dDeviceGeneration ? PortCeD3dDeviceGeneration() : 0u;
}

bool ForcedMissing(const char *slug) {
    const char *missing = std::getenv("BEA_PORT_MUSEUM_MISSING_TEXTURE");
    return slug && missing && *missing && std::strstr(slug, missing) != NULL;
}

// Upload an atlas into D3DPOOL_MANAGED with capped per-cell software mip levels (kAtlasMipLevels)
// so adjacent strips/buttons never bleed across seams at oblique viewing angles.
IDirect3DTexture9 *UploadManagedArgbAtlas(IDirect3DDevice9 *device, const unsigned int *argb,
                                          unsigned width, unsigned height, bool isPortalSign) {
    if (!device || !argb || !width || !height) return NULL;
    IDirect3DTexture9 *tex = NULL;
    const HRESULT hr = device->CreateTexture(
        static_cast<UINT>(width), static_cast<UINT>(height),
        static_cast<UINT>(kAtlasMipLevels), 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);
    if (FAILED(hr) || !tex) {
        if (tex) tex->Release();
        return NULL;
    }
    RawBuf<unsigned int> mipBuf;
    for (unsigned lvl = 0u; lvl < kAtlasMipLevels; ++lvl) {
        const unsigned lw = width >> lvl;
        const unsigned lh = height >> lvl;
        if (!lw || !lh) break;
        const unsigned int *src = argb;
        if (lvl > 0u) {
            if (!mipBuf.resize(static_cast<size_t>(lw) * static_cast<size_t>(lh))) {
                tex->Release();
                return NULL;
            }
            if (isPortalSign)
                CeMuseumTextures::DownsamplePortalSignAtlasLevel(argb, lvl, mipBuf.data());
            else
                CeMuseumTextures::DownsampleDirectoryAtlasLevel(argb, lvl, mipBuf.data());
            src = mipBuf.data();
        }
        D3DLOCKED_RECT lr = {};
        if (FAILED(tex->LockRect(static_cast<UINT>(lvl), &lr, NULL, 0)) || !lr.pBits) {
            tex->Release();
            return NULL;
        }
        unsigned char *dstBytes = static_cast<unsigned char *>(lr.pBits);
        for (unsigned y = 0u; y < lh; ++y) {
            std::memcpy(dstBytes + static_cast<size_t>(y) * static_cast<size_t>(lr.Pitch),
                        src + static_cast<size_t>(y) * static_cast<size_t>(lw),
                        static_cast<size_t>(lw) * sizeof(uint32_t));
        }
        tex->UnlockRect(static_cast<UINT>(lvl));
    }
    return tex;
}
#endif // !CE_MUSEUM_TEXTURES_HOST_TEST

} // namespace

const char *PortalSignLabel(CeMuseumPortalSignId id) {
    const unsigned idx = static_cast<unsigned>(id);
    return idx < static_cast<unsigned>(CE_MUSEUM_SIGN_COUNT) ? kPortalSignLabels[idx] : "";
}

const char *DirectoryButtonLabel(CeMuseumDirButtonId id) {
    const unsigned idx = static_cast<unsigned>(id);
    return idx < static_cast<unsigned>(CE_MUSEUM_DIR_BTN_COUNT) ? kDirectoryButtonLabels[idx] : "";
}

CeMuseumAtlasUv PortalSignUv(CeMuseumPortalSignId id) {
    const unsigned idx = static_cast<unsigned>(id) < static_cast<unsigned>(CE_MUSEUM_SIGN_COUNT)
        ? static_cast<unsigned>(id) : 0u;
    const float y0 = float(idx * kPortalStripHeight) + 0.5f;
    const float y1 = float((idx + 1u) * kPortalStripHeight) - 0.5f;
    return CeMuseumAtlasUv{
        0.5f / float(kPortalAtlasWidth),
        y0 / float(kPortalAtlasHeight),
        (float(kPortalAtlasWidth) - 0.5f) / float(kPortalAtlasWidth),
        y1 / float(kPortalAtlasHeight),
    };
}

CeMuseumAtlasUv DirectoryMapUv() {
    return CeMuseumAtlasUv{
        0.5f / float(kDirectoryAtlasWidth),
        0.5f / float(kDirectoryAtlasHeight),
        (float(kDirectoryAtlasWidth) - 0.5f) / float(kDirectoryAtlasWidth),
        (float(kDirectoryMapHeight) - 0.5f) / float(kDirectoryAtlasHeight),
    };
}

CeMuseumAtlasUv DirectoryButtonUv(CeMuseumDirButtonId id) {
    const unsigned idx = static_cast<unsigned>(id) < static_cast<unsigned>(CE_MUSEUM_DIR_BTN_COUNT)
        ? static_cast<unsigned>(id) : 0u;
    const unsigned col = idx % 3u;
    const unsigned row = idx / 3u;
    const unsigned cellW = 170u;
    const unsigned cellH = 64u;
    const unsigned x0 = col * cellW;
    const unsigned y0 = kDirectoryMapHeight + row * cellH;
    return CeMuseumAtlasUv{
        (float(x0) + 0.5f) / float(kDirectoryAtlasWidth),
        (float(y0) + 0.5f) / float(kDirectoryAtlasHeight),
        (float(x0 + cellW) - 0.5f) / float(kDirectoryAtlasWidth),
        (float(y0 + cellH) - 0.5f) / float(kDirectoryAtlasHeight),
    };
}

// Bake all 8 engraved brass-on-obsidian 1024x64 strips into the 1024x512 Portal Signage Atlas.
// Each strip is rasterized in an isotropic 2048x256 (exact 8:1 world board aspect!) supersampled buffer
// and box-downsampled 2x horizontally x 4x vertically to 1024x64 so strokes stay crisp and circular
// on every 8:1 architectural portal signboard.
void BakePortalSignAtlasRgba(unsigned int *outArgb) {
    if (!outArgb) return;
    constexpr unsigned kHiW = 2048u;
    constexpr unsigned kHiH = 256u;
    RawBuf<uint32_t> hi(static_cast<size_t>(kHiW) * static_cast<size_t>(kHiH), 0xff161c22u);
    StrokeMask mask(kHiW, kHiH);
    if (hi.empty() || mask.cov.empty()) return;

    for (unsigned s = 0; s < static_cast<unsigned>(CE_MUSEUM_SIGN_COUNT); ++s) {
        // 1. Honed dark obsidian stone plate (#161C22 with subtle top-lit sheen)
        for (unsigned y = 0; y < kHiH; ++y) {
            const float t = float(y) / float(kHiH - 1u);
            const uint32_t rowCol = BlendRgb(0xff1c232bu, 0xff12171du, t);
            uint32_t *row = &hi[y * kHiW];
            for (unsigned x = 0; x < kHiW; ++x) row[x] = rowCol;
        }
        // Subtle chamfered stone inner recess shading
        FillRectAlpha(hi.data(), kHiW, kHiH, 34, 34, int(kHiW) - 34, int(kHiH) - 34, 0xff0d1116u, 0.25f);

        // 2. Engraved antique brass double-hairline architectural border (#E8C46A / #B28E42)
        DrawRectOutline(hi.data(), kHiW, kHiH, 12, 12, int(kHiW) - 12, int(kHiH) - 12, 6, 0xffe8c46au);
        DrawRectOutline(hi.data(), kHiW, kHiH, 26, 26, int(kHiW) - 26, int(kHiH) - 26, 3, 0xffb28e42u);
        // Subtle neoclassical corner square rivets
        const int rx[2] = {38, int(kHiW) - 52};
        const int ry[2] = {38, int(kHiH) - 52};
        for (int ix = 0; ix < 2; ++ix)
            for (int iy = 0; iy < 2; ++iy)
                FillRect(hi.data(), kHiW, kHiH, rx[ix], ry[iy], rx[ix] + 14, ry[iy] + 14, 0xffcca652u);

        // 3. Engraved antique brass capitals centered cleanly inside the border
        mask.Clear();
        DrawCenteredText(mask, kPortalSignLabels[s],
                         float(kHiW) * 0.5f, float(kHiH) * 0.51f,
                         float(kHiW) - 176.0f, 122.0f, 0.24f, 0.068f);
        CompositeEngravedMask(hi.data(), mask, 0xffe8c46au, 0xffffebaeu, 0xff080a0du, 3, 5);

        // 4. Supersampled downsample 2048x256 -> 1024x64 into atlas strip s
        uint32_t *stripOut = reinterpret_cast<uint32_t *>(outArgb) +
            s * kPortalStripHeight * kPortalAtlasWidth;
        BoxDownsample(hi.data(), kHiW, kHiH, stripOut, kPortalAtlasWidth, kPortalStripHeight);
    }
}

// Bake the 512x512 Atrium Directory Atlas (2x supersampled 1024x1024 -> box-downsampled 512x512):
//   - Top 512x384: Color-coded Architectural Floor-Plan generated directly from MakeRoomGraph().rooms
//                  and MakeRoomGraph().portals, with compass rose and "YOU ARE HERE" marker.
//   - Bottom 512x128: 3x2 grid of 170x64 crisp labeled console buttons matching the 6 Directory wings.
void BakeDirectoryAtlasRgba(unsigned int *outArgb) {
    if (!outArgb) return;
    constexpr unsigned kHiW = kDirectoryAtlasWidth * 2u;  // 1024
    constexpr unsigned kHiH = kDirectoryAtlasHeight * 2u; // 1024
    constexpr unsigned kHiMapH = kDirectoryMapHeight * 2u; // 768
    RawBuf<uint32_t> hi(static_cast<size_t>(kHiW) * static_cast<size_t>(kHiH), 0xff161c22u);
    StrokeMask mask(kHiW, kHiH);
    if (hi.empty() || mask.cov.empty()) return;

    // =========================================================================
    // 1. TOP PANEL (1024x768 -> 512x384): ARCHITECTURAL FLOOR-PLAN MAP PLAQUE
    // =========================================================================
    for (unsigned y = 0; y < kHiMapH; ++y) {
        const float t = float(y) / float(kHiMapH - 1u);
        const uint32_t col = BlendRgb(0xff1a222au, 0xff11161cu, t);
        uint32_t *row = &hi[y * kHiW];
        for (unsigned x = 0; x < kHiW; ++x) row[x] = col;
    }
    // Double-hairline antique brass plaque frame
    DrawRectOutline(hi.data(), kHiW, kHiH, 10, 10, int(kHiW) - 10, int(kHiMapH) - 10, 6, 0xffe8c46au);
    DrawRectOutline(hi.data(), kHiW, kHiH, 22, 22, int(kHiW) - 22, int(kHiMapH) - 22, 2, 0xffa8863eu);

    // Header band: "MUSEUM DIRECTORY"
    FillRect(hi.data(), kHiW, kHiH, 24, 24, int(kHiW) - 24, 108, 0xff12181eu);
    FillRect(hi.data(), kHiW, kHiH, 36, 106, int(kHiW) - 36, 110, 0xffc8a250u);
    DrawCenteredText(mask, "MUSEUM DIRECTORY", float(kHiW) * 0.5f, 67.0f, 880.0f, 44.0f, 0.24f, 0.072f);

    // Subtle architectural blueprint grid across map viewport
    for (int gx = 56; gx < int(kHiW) - 40; gx += 48)
        FillRectAlpha(hi.data(), kHiW, kHiH, gx, 118, gx + 1, int(kHiMapH) - 34, 0xff3c4c5cu, 0.22f);
    for (int gy = 136; gy < int(kHiMapH) - 34; gy += 48)
        FillRectAlpha(hi.data(), kHiW, kHiH, 36, gy, int(kHiW) - 36, gy + 1, 0xff3c4c5cu, 0.22f);

    // Project MakeRoomGraph().rooms directly into pixel space (North +Y up, East +X right).
    const CeMuseumExhibition::Grand::RoomGraph graph = CeMuseumExhibition::Grand::MakeRoomGraph();
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (unsigned r = 0; r < CeMuseumExhibition::Grand::kRoomCount; ++r) {
        const auto &b = graph.rooms[r];
        if (b.minX < minX) minX = b.minX;
        if (b.maxX > maxX) maxX = b.maxX;
        if (b.minY < minY) minY = b.minY;
        if (b.maxY > maxY) maxY = b.maxY;
    }
    const float spanX = (maxX - minX) > 1.0f ? (maxX - minX) : 100.0f;
    const float spanY = (maxY - minY) > 1.0f ? (maxY - minY) : 84.0f;
    const float viewL = 62.0f, viewR = float(kHiW) - 62.0f;
    const float viewT = 132.0f, viewB = float(kHiMapH) - 48.0f;
    const float scaleX = (viewR - viewL) / spanX;
    const float scaleY = (viewB - viewT) / spanY;
    const float scale = scaleX < scaleY ? scaleX : scaleY;
    const float padX = ((viewR - viewL) - spanX * scale) * 0.5f;
    const float padY = ((viewB - viewT) - spanY * scale) * 0.5f;
    auto toPxX = [&](float wx) -> float { return viewL + padX + (wx - minX) * scale; };
    auto toPxY = [&](float wy) -> float { return viewB - padY - (wy - minY) * scale; };

    struct RoomStyle {
        uint32_t fill;
        uint32_t border;
        const char *line1;
        const char *line2;
    };
    static const RoomStyle kRoomStyles[CeMuseumExhibition::Grand::kRoomCount] = {
        {0xff4a4334u, 0xffd4b068u, "COURTYARD", ""},      // kRoomCourtyard
        {0xff334250u, 0xffe8c46au, "ATRIUM",    ""},      // kRoomAtrium
        {0xff1f4364u, 0xff66c2eeu, "FORSETI",   ""},      // kRoomForseti
        {0xff5c281du, 0xffe88a40u, "MUSPELL",   ""},      // kRoomMuspell
        {0xff224652u, 0xff7ec8d8u, "HANGAR",    ""},      // kRoomHangar
        {0xff47303fu, 0xffd89e78u, "CONCEPT",   "ART"},   // kRoomConceptArt
        {0xff463c27u, 0xffd4b464u, "EARLY",     "DESIGNS"}, // kRoomEarlyDesigns
        {0xff52222cu, 0xffdc6878u, "CINEMA",    ""},      // kRoomCinema
    };

    // Draw all 8 color-coded wings from MakeRoomGraph().rooms
    for (unsigned r = 0; r < CeMuseumExhibition::Grand::kRoomCount; ++r) {
        const auto &b = graph.rooms[r];
        const int x0 = static_cast<int>(std::lround(toPxX(b.minX)));
        const int x1 = static_cast<int>(std::lround(toPxX(b.maxX)));
        const int y0 = static_cast<int>(std::lround(toPxY(b.maxY)));
        const int y1 = static_cast<int>(std::lround(toPxY(b.minY)));
        FillRect(hi.data(), kHiW, kHiH, x0, y0, x1, y1, kRoomStyles[r].fill);
        DrawRectOutline(hi.data(), kHiW, kHiH, x0, y0, x1, y1, 4, kRoomStyles[r].border);
    }

    // Draw crisp central rotunda compass medallion inside Grand Atrium
    const float atrCx = toPxX(0.0f), atrCy = toPxY(36.0f);
    DrawDiscRgba(hi.data(), kHiW, kHiH, atrCx, atrCy, 4.2f * scale, 0xff445666u);
    DrawDiscRgba(hi.data(), kHiW, kHiH, atrCx, atrCy, 2.0f * scale, 0xffc8a454u);

    // Draw open portal passages directly from MakeRoomGraph().portals
    for (unsigned p = 0; p < CeMuseumExhibition::Grand::kPortalCount; ++p) {
        const auto &pt = graph.portals[p];
        if (pt.vertical) {
            const float px = toPxX(pt.line);
            const float py0 = toPxY(pt.hi);
            const float py1 = toPxY(pt.lo);
            FillRect(hi.data(), kHiW, kHiH,
                     static_cast<int>(px) - 5, static_cast<int>(py0) + 2,
                     static_cast<int>(px) + 5, static_cast<int>(py1) - 2, 0xfff0dba0u);
        } else {
            const float py = toPxY(pt.line);
            const float px0 = toPxX(pt.lo);
            const float px1 = toPxX(pt.hi);
            FillRect(hi.data(), kHiW, kHiH,
                     static_cast<int>(px0) + 2, static_cast<int>(py) - 5,
                     static_cast<int>(px1) - 2, static_cast<int>(py) + 5, 0xfff0dba0u);
        }
    }

    // Room labels centered inside each wing
    for (unsigned r = 0; r < CeMuseumExhibition::Grand::kRoomCount; ++r) {
        const auto &b = graph.rooms[r];
        const float cx = toPxX(0.5f * (b.minX + b.maxX));
        // Shift Atrium label slightly north of the rotunda disc so it stays clear of the You-Are-Here marker
        const float cy = (r == CeMuseumExhibition::Grand::kRoomAtrium)
            ? toPxY(45.5f)
            : toPxY(0.5f * (b.minY + b.maxY));
        const float maxW = (b.maxX - b.minX) * scale - 32.0f;
        const RoomStyle &st = kRoomStyles[r];
        if (st.line2[0]) {
            DrawCenteredText(mask, st.line1, cx, cy - 17.0f, maxW, 24.0f, 0.16f, 0.085f);
            DrawCenteredText(mask, st.line2, cx, cy + 17.0f, maxW, 24.0f, 0.16f, 0.085f);
        } else {
            DrawCenteredText(mask, st.line1, cx, cy, maxW, 27.0f, 0.16f, 0.085f);
        }
    }

    // Compass rose ("N" + north pointer arrow) in the open upper-right bay northeast of Hangar
    {
        const float ncx = float(kHiW) - 118.0f;
        const float ncy = 216.0f;
        DrawLineRgba(hi.data(), kHiW, kHiH, ncx, ncy + 34.0f, ncx, ncy - 30.0f, 3.0f, 0xffe8c46au);
        DrawLineRgba(hi.data(), kHiW, kHiH, ncx - 14.0f, ncy - 10.0f, ncx, ncy - 32.0f, 3.0f, 0xffe8c46au);
        DrawLineRgba(hi.data(), kHiW, kHiH, ncx + 14.0f, ncy - 10.0f, ncx, ncy - 32.0f, 3.0f, 0xffe8c46au);
        DrawLineRgba(hi.data(), kHiW, kHiH, ncx - 18.0f, ncy + 6.0f, ncx + 18.0f, ncy + 6.0f, 2.0f, 0xffa8863eu);
        DrawCenteredText(mask, "N", ncx, ncy - 54.0f, 44.0f, 30.0f, 0.16f, 0.085f);
    }

    // "YOU ARE HERE" beacon plotted directly at (kDirectoryX, kDirectoryY) inside Grand Atrium,
    // with a crisp leader line to a legible obsidian-and-gold callout badge strictly clear of Courtyard!
    {
        const float youX = toPxX(CeMuseumTour::kDirectoryX);
        const float youY = toPxY(CeMuseumTour::kDirectoryY);
        const float badgeX = 192.0f;
        const float badgeY = float(kHiMapH) - 102.0f;
        // Callout leader line
        DrawLineRgba(hi.data(), kHiW, kHiH, badgeX + 112.0f, badgeY - 30.0f, youX, youY, 3.2f, 0xffe8c46au);
        // Beacon bullseye at the Directory Console position
        DrawDiscRgba(hi.data(), kHiW, kHiH, youX, youY, 18.0f, 0xffe8c46au);
        DrawDiscRgba(hi.data(), kHiW, kHiH, youX, youY, 12.5f, 0xffe03c32u);
        DrawDiscRgba(hi.data(), kHiW, kHiH, youX, youY, 5.0f, 0xfffff8e6u);
        // Callout badge box in open SW corner strictly left of Courtyard
        const int bx0 = static_cast<int>(badgeX) - 144;
        const int bx1 = static_cast<int>(badgeX) + 144;
        const int by0 = static_cast<int>(badgeY) - 32;
        const int by1 = static_cast<int>(badgeY) + 32;
        FillRect(hi.data(), kHiW, kHiH, bx0, by0, bx1, by1, 0xff161c22u);
        DrawRectOutline(hi.data(), kHiW, kHiH, bx0, by0, bx1, by1, 4, 0xffe8c46au);
        DrawDiscRgba(hi.data(), kHiW, kHiH, float(bx0) + 26.0f, badgeY, 9.5f, 0xffe03c32u);
        DrawDiscRgba(hi.data(), kHiW, kHiH, float(bx0) + 26.0f, badgeY, 3.8f, 0xfffff8e6u);
        DrawCenteredText(mask, "YOU ARE HERE", badgeX + 16.0f, badgeY, 224.0f, 25.0f, 0.15f, 0.088f);
    }

    CompositeEngravedMask(hi.data(), mask, 0xfff2d482u, 0xfffff4ccu, 0xff090c10u, 2, 3);

    // =========================================================================
    // 2. BOTTOM BAND (1024x256 -> 512x128): 6 LABELED DIRECTORY CONSOLE BUTTONS
    // =========================================================================
    mask.Clear();
    constexpr unsigned kCellW = 340u; // 170px * 2x
    constexpr unsigned kCellH = 128u; // 64px * 2x
    for (unsigned i = 0; i < static_cast<unsigned>(CE_MUSEUM_DIR_BTN_COUNT); ++i) {
        const unsigned col = i % 3u;
        const unsigned row = i / 3u;
        const int x0 = static_cast<int>(col * kCellW);
        const int y0 = static_cast<int>(kHiMapH + row * kCellH);
        const int x1 = x0 + static_cast<int>(kCellW);
        const int y1 = y0 + static_cast<int>(kCellH);

        // Brushed obsidian button face with subtle top-lit sheen
        for (int y = y0; y < y1; ++y) {
            const float t = float(y - y0) / float(kCellH - 1u);
            const uint32_t bg = BlendRgb(0xff202832u, 0xff13181eu, t);
            uint32_t *dstRow = &hi[static_cast<unsigned>(y) * kHiW];
            for (int x = x0; x < x1; ++x) dstRow[x] = bg;
        }
        // Double-hairline antique brass button bezel + color-coded wing bar along lower edge
        DrawRectOutline(hi.data(), kHiW, kHiH, x0 + 6, y0 + 6, x1 - 6, y1 - 6, 5, 0xffe8c46au);
        DrawRectOutline(hi.data(), kHiW, kHiH, x0 + 15, y0 + 15, x1 - 15, y1 - 15, 2, 0xffa6843eu);
        FillRect(hi.data(), kHiW, kHiH, x0 + 22, y1 - 24, x1 - 22, y1 - 17, kDirectoryButtonAccents[i]);

        const float cx = float(x0 + x1) * 0.5f;
        const float cy = float(y0 + y1 - 10) * 0.5f;
        const float maxW = float(kCellW) - 56.0f;
        if (i == CE_MUSEUM_DIR_BTN_CONCEPT_ART) {
            DrawCenteredText(mask, "CONCEPT ART", cx, cy, maxW, 38.0f, 0.16f, 0.082f);
        } else if (i == CE_MUSEUM_DIR_BTN_EARLY_DESIGNS) {
            DrawCenteredText(mask, "EARLY DESIGNS", cx, cy, maxW, 34.0f, 0.14f, 0.084f);
        } else {
            DrawCenteredText(mask, kDirectoryButtonLabels[i], cx, cy, maxW, 44.0f, 0.20f, 0.078f);
        }
    }
    CompositeEngravedMask(hi.data(), mask, 0xfff2d27eu, 0xfffff2c6u, 0xff080b0eu, 2, 3);

    // 2x2 box-filter downsample 1024x1024 -> 512x512
    BoxDownsample(hi.data(), kHiW, kHiH, reinterpret_cast<uint32_t *>(outArgb),
                  kDirectoryAtlasWidth, kDirectoryAtlasHeight);
}

void DownsamplePortalSignAtlasLevel(const unsigned int *baseArgb, unsigned level, unsigned int *outArgb) {
    if (!baseArgb || !outArgb || level >= kAtlasMipLevels) return;
    const unsigned scale = 1u << level;
    const unsigned dw = kPortalAtlasWidth >> level;
    const unsigned dh = kPortalAtlasHeight >> level;
    if (level == 0u) {
        std::memcpy(outArgb, baseArgb, static_cast<size_t>(dw) * static_cast<size_t>(dh) * sizeof(unsigned int));
        return;
    }
    const uint32_t *src = reinterpret_cast<const uint32_t *>(baseArgb);
    uint32_t *dst = reinterpret_cast<uint32_t *>(outArgb);
    const unsigned dstStripH = kPortalStripHeight >> level;
    for (unsigned s = 0u; s < static_cast<unsigned>(CE_MUSEUM_SIGN_COUNT); ++s) {
        const unsigned cellY0 = s * kPortalStripHeight;
        const unsigned cellY1 = cellY0 + kPortalStripHeight;
        for (unsigned ly = 0u; ly < dstStripH; ++ly) {
            const unsigned dy = s * dstStripH + ly;
            const unsigned sy0 = dy * scale;
            const unsigned sy1 = sy0 + scale;
            for (unsigned dx = 0u; dx < dw; ++dx) {
                const unsigned sx0 = dx * scale;
                const unsigned sx1 = sx0 + scale;
                DownsampleRectClamped(src, kPortalAtlasWidth, sx0, sy0, sx1, sy1,
                                      0u, cellY0, kPortalAtlasWidth, cellY1,
                                      &dst[dy * dw + dx]);
            }
        }
    }
}

void DownsampleDirectoryAtlasLevel(const unsigned int *baseArgb, unsigned level, unsigned int *outArgb) {
    if (!baseArgb || !outArgb || level >= kAtlasMipLevels) return;
    const unsigned scale = 1u << level;
    const unsigned dw = kDirectoryAtlasWidth >> level;
    const unsigned dh = kDirectoryAtlasHeight >> level;
    if (level == 0u) {
        std::memcpy(outArgb, baseArgb, static_cast<size_t>(dw) * static_cast<size_t>(dh) * sizeof(unsigned int));
        return;
    }
    const uint32_t *src = reinterpret_cast<const uint32_t *>(baseArgb);
    uint32_t *dst = reinterpret_cast<uint32_t *>(outArgb);
    const unsigned dstMapH = kDirectoryMapHeight >> level;
    // 1. Top floor-plan map panel [0..512) x [0..384): strictly clamped inside map bounds.
    for (unsigned dy = 0u; dy < dstMapH; ++dy) {
        const unsigned sy0 = dy * scale;
        const unsigned sy1 = sy0 + scale;
        for (unsigned dx = 0u; dx < dw; ++dx) {
            const unsigned sx0 = dx * scale;
            const unsigned sx1 = sx0 + scale;
            DownsampleRectClamped(src, kDirectoryAtlasWidth, sx0, sy0, sx1, sy1,
                                  0u, 0u, kDirectoryAtlasWidth, kDirectoryMapHeight,
                                  &dst[dy * dw + dx]);
        }
    }
    // 2. Bottom 3x2 console button grid: each texel samples strictly inside its own 170x64 button cell.
    constexpr unsigned kCellW = 170u;
    constexpr unsigned kCellH = 64u;
    const unsigned dstCellH = kCellH >> level;
    for (unsigned row = 0u; row < 2u; ++row) {
        const unsigned cellY0 = kDirectoryMapHeight + row * kCellH;
        const unsigned cellY1 = cellY0 + kCellH;
        for (unsigned ly = 0u; ly < dstCellH; ++ly) {
            const unsigned dy = dstMapH + row * dstCellH + ly;
            const unsigned sy0 = dy * scale;
            const unsigned sy1 = sy0 + scale;
            for (unsigned dx = 0u; dx < dw; ++dx) {
                const unsigned sx0 = dx * scale;
                const unsigned sx1 = sx0 + scale;
                const unsigned center2 = (dx * 2u + 1u) * scale; // 2x level-0 center X
                unsigned col = center2 / (2u * kCellW);
                if (col > 2u) col = 2u;
                const unsigned cellX0 = col * kCellW;
                const unsigned cellX1 = cellX0 + kCellW;
                DownsampleRectClamped(src, kDirectoryAtlasWidth, sx0, sy0, sx1, sy1,
                                      cellX0, cellY0, cellX1, cellY1,
                                      &dst[dy * dw + dx]);
            }
        }
    }
}

#ifndef CE_MUSEUM_TEXTURES_HOST_TEST

const char *Name(CeMuseumTexId id) {
    const unsigned idx = static_cast<unsigned>(id);
    if (idx >= static_cast<unsigned>(CE_MUSEUM_TEX_COUNT)) return "";
    return CeMuseumTexturesData::kEntries[idx].name;
}

void ReleaseAll() {
    for (unsigned i = 0; i < static_cast<unsigned>(CE_MUSEUM_TEX_COUNT); ++i) {
        if (gTextures[i]) {
            gTextures[i]->Release();
            gTextures[i] = NULL;
        }
    }
    if (gPortalSignAtlas) {
        gPortalSignAtlas->Release();
        gPortalSignAtlas = NULL;
    }
    if (gDirectoryAtlas) {
        gDirectoryAtlas->Release();
        gDirectoryAtlas = NULL;
    }
    gDevice = NULL;
    gDeviceGen = 0u;
    gLoaded = false;
}

void LoadAll(IDirect3DDevice9 *device) {
    if (!device) { ReleaseAll(); return; }
    const uint64_t gen = CurrentDeviceGen();
    if (gLoaded && gDevice == device && gDeviceGen == gen) return;
    ReleaseAll();
    gDevice = device;
    gDeviceGen = gen;
    gLoaded = true;
    unsigned loaded = 0;
    for (unsigned i = 0; i < static_cast<unsigned>(CE_MUSEUM_TEX_COUNT); ++i) {
        const CeMuseumTexturesData::Entry &entry = CeMuseumTexturesData::kEntries[i];
        if (ForcedMissing(entry.name)) {
            PortSessionLog(PORT_SESSION_GAME, false,
                           "museum: architectural texture %s skipped by test override; using plain colour",
                           entry.name);
            continue;
        }
        if (!entry.packed || !entry.packedSize || !entry.rawSize || entry.rawSize > (4u << 20)) continue;
        unsigned char *raw = static_cast<unsigned char *>(std::malloc(entry.rawSize));
        if (!raw) continue;
        unsigned long outLen = entry.rawSize;
        const int rc = uncompress(raw, &outLen, entry.packed, entry.packedSize);
        if (rc == 0 && outLen == entry.rawSize) {
            IDirect3DTexture9 *tex = NULL;
            const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
                device, raw, static_cast<UINT>(outLen),
                static_cast<UINT>(entry.width), static_cast<UINT>(entry.height),
                0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                D3DX_FILTER_TRIANGLE, D3DX_FILTER_BOX, 0, NULL, NULL, &tex);
            if (SUCCEEDED(hr) && tex) {
                gTextures[i] = tex;
                ++loaded;
            } else {
                if (tex) tex->Release();
                PortSessionLog(PORT_SESSION_GAME, false,
                               "museum: architectural texture %s failed upload; using plain colour",
                               entry.name);
            }
        }
        std::free(raw);
    }

    // Bake the 1024x512 Portal Lintel Signage Atlas ONCE into D3DPOOL_MANAGED with per-strip capped mips.
    if (!ForcedMissing("portal_signage")) {
        RawBuf<unsigned int> pixels(static_cast<size_t>(kPortalAtlasWidth) * static_cast<size_t>(kPortalAtlasHeight), 0xff161c22u);
        if (!pixels.empty()) {
            BakePortalSignAtlasRgba(pixels.data());
            gPortalSignAtlas = UploadManagedArgbAtlas(device, pixels.data(), kPortalAtlasWidth, kPortalAtlasHeight, true);
        }
    }

    // Bake the 512x512 Atrium Directory Floor-Plan & Labeled Console Buttons Atlas ONCE with per-cell capped mips.
    if (!ForcedMissing("directory_map")) {
        RawBuf<unsigned int> pixels(static_cast<size_t>(kDirectoryAtlasWidth) * static_cast<size_t>(kDirectoryAtlasHeight), 0xff161c22u);
        if (!pixels.empty()) {
            BakeDirectoryAtlasRgba(pixels.data());
            gDirectoryAtlas = UploadManagedArgbAtlas(device, pixels.data(), kDirectoryAtlasWidth, kDirectoryAtlasHeight, false);
        }
    }

    PortSessionLog(PORT_SESSION_GAME, false,
                   "museum: loaded %u of %u architectural textures (signage=%u, directory=%u)",
                   loaded, static_cast<unsigned>(CE_MUSEUM_TEX_COUNT),
                   gPortalSignAtlas ? 1u : 0u, gDirectoryAtlas ? 1u : 0u);
}

IDirect3DTexture9 *Get(CeMuseumTexId id) {
    const unsigned idx = static_cast<unsigned>(id);
    if (idx >= static_cast<unsigned>(CE_MUSEUM_TEX_COUNT) || !gLoaded) return NULL;
    IDirect3DDevice9 *cur = LT.GetDevice();
    if (!cur) return NULL;
    const uint64_t gen = CurrentDeviceGen();
    if (gDevice != cur || gDeviceGen != gen) LoadAll(cur);
    return (gDevice == cur && gDeviceGen == gen) ? gTextures[idx] : NULL;
}

IDirect3DTexture9 *GetPortalSignAtlas() {
    if (!gLoaded) return NULL;
    IDirect3DDevice9 *cur = LT.GetDevice();
    if (!cur) return NULL;
    const uint64_t gen = CurrentDeviceGen();
    if (gDevice != cur || gDeviceGen != gen) LoadAll(cur);
    return (gDevice == cur && gDeviceGen == gen) ? gPortalSignAtlas : NULL;
}

IDirect3DTexture9 *GetDirectoryAtlas() {
    if (!gLoaded) return NULL;
    IDirect3DDevice9 *cur = LT.GetDevice();
    if (!cur) return NULL;
    const uint64_t gen = CurrentDeviceGen();
    if (gDevice != cur || gDeviceGen != gen) LoadAll(cur);
    return (gDevice == cur && gDeviceGen == gen) ? gDirectoryAtlas : NULL;
}

#else // CE_MUSEUM_TEXTURES_HOST_TEST
void LoadAll(IDirect3DDevice9 *) {}
void ReleaseAll() {}
IDirect3DTexture9 *Get(CeMuseumTexId) { return 0; }
const char *Name(CeMuseumTexId) { return ""; }
IDirect3DTexture9 *GetPortalSignAtlas() { return 0; }
IDirect3DTexture9 *GetDirectoryAtlas() { return 0; }
#endif // !CE_MUSEUM_TEXTURES_HOST_TEST

} // namespace CeMuseumTextures

extern "C" void PortCeMuseumTexturesDeleteDeviceObjects(void) {
    CeMuseumTextures::ReleaseAll();
}
#else
#include "ce_museum_textures.h"
namespace CeMuseumTextures {
void LoadAll(IDirect3DDevice9 *) {}
void ReleaseAll() {}
IDirect3DTexture9 *Get(CeMuseumTexId) { return 0; }
const char *Name(CeMuseumTexId) { return ""; }
IDirect3DTexture9 *GetPortalSignAtlas() { return 0; }
IDirect3DTexture9 *GetDirectoryAtlas() { return 0; }
const char *PortalSignLabel(CeMuseumPortalSignId) { return ""; }
const char *DirectoryButtonLabel(CeMuseumDirButtonId) { return ""; }
CeMuseumAtlasUv PortalSignUv(CeMuseumPortalSignId) { return CeMuseumAtlasUv{0.0f, 0.0f, 1.0f, 1.0f}; }
CeMuseumAtlasUv DirectoryMapUv() { return CeMuseumAtlasUv{0.0f, 0.0f, 1.0f, 1.0f}; }
CeMuseumAtlasUv DirectoryButtonUv(CeMuseumDirButtonId) { return CeMuseumAtlasUv{0.0f, 0.0f, 1.0f, 1.0f}; }
void BakePortalSignAtlasRgba(unsigned int *) {}
void BakeDirectoryAtlasRgba(unsigned int *) {}
void DownsamplePortalSignAtlasLevel(const unsigned int *, unsigned, unsigned int *) {}
void DownsampleDirectoryAtlasLevel(const unsigned int *, unsigned, unsigned int *) {}
} // namespace CeMuseumTextures
#endif
