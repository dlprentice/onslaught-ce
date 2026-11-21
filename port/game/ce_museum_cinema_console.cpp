// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_cinema_console.cpp: the screening room's projector console;
// see the header for what each part promises.
#include "ce_museum_cinema_console.h"

#include <cmath>
#include <cstddef>
#include <cstring>

namespace CeMuseumCinemaConsole {

namespace Controls = CeMuseumCinemaControls;

namespace {

// ---------------------------------------------------------------------------
// The console's proportions. Across the panel they are fractions of its half
// width, up it and in depth fractions of its half height. Pick, Build and the
// chips Present draws all read this one table.

const float kControlCentreU = 2.0f / 3.0f;   // Previous at -, Next at +, Play at 0
const float kControlHalfU = 0.27f;
const float kControlCentreV = -0.18f;        // the controls sit low; the lamp is above
const float kControlHalfV = 0.58f;
const float kLampHalfU = 0.12f;
const float kLampMinV = 0.58f;
const float kLampMaxV = 0.84f;
const float kKeyRise = 0.14f;                // plate behind the control plane
const float kCasingDepth = 0.70f;            // back of the casing behind it
const float kDecalLift = 0.02f;              // marks stand off the face they are on
const float kGlyphScale = 0.5f;              // of a control's smaller half extent
const float kFrameScale = 0.12f;

// Aiming limits.
const float kNearFraction = 0.25f;           // of the half height
const float kMinClosing = 0.2f;              // cosine between the aim and -front
const float kAttentionMargin = 0.5f;         // of the half width, around the panel

// What a mount may be.
const float kAxisTolerance = 2.0e-3f;
const float kMinHalfExtent = 1.0e-3f;
const float kMaxExtent = 1.0e4f;
const float kMaxCoordinate = 1.0e6f;
const float kMinAim = 1.0e-30f;

const Region kControls[3] = {Controls::kRegionPrevious, Controls::kRegionPlay,
                             Controls::kRegionNext};

// ---------------------------------------------------------------------------
// Vectors

inline Vec3 MakeVec(float x, float y, float z) {
    Vec3 v;
    v.x = x; v.y = y; v.z = z;
    return v;
}
inline Vec3 Add(const Vec3 &a, const Vec3 &b) { return MakeVec(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 Sub(const Vec3 &a, const Vec3 &b) { return MakeVec(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 Scale(const Vec3 &a, float s) { return MakeVec(a.x * s, a.y * s, a.z * s); }
inline float Dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(const Vec3 &a, const Vec3 &b) {
    return MakeVec(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline bool Finite(float f) { return std::isfinite(f); }
inline bool FiniteVec(const Vec3 &v) { return Finite(v.x) && Finite(v.y) && Finite(v.z); }

bool Normalize(Vec3 *v) {
    const float squared = Dot(*v, *v);
    if (!Finite(squared) || !(squared > 1.0e-12f)) return false;
    *v = Scale(*v, 1.0f / std::sqrt(squared));
    return FiniteVec(*v);
}

// A direction of any finite length, scaled first so that squaring cannot overflow.
bool SafeDirection(const Vec3 &in, Vec3 *out) {
    if (!FiniteVec(in)) return false;
    float largest = std::fabs(in.x);
    if (std::fabs(in.y) > largest) largest = std::fabs(in.y);
    if (std::fabs(in.z) > largest) largest = std::fabs(in.z);
    if (!(largest > kMinAim)) return false;
    Vec3 scaled = MakeVec(in.x / largest, in.y / largest, in.z / largest);
    if (!Normalize(&scaled)) return false;
    *out = scaled;
    return true;
}

bool NearUnit(const Vec3 &v) {
    const float squared = Dot(v, v);
    return Finite(squared) && std::fabs(squared - 1.0f) <= kAxisTolerance;
}
bool NearPerpendicular(const Vec3 &a, const Vec3 &b) {
    const float d = Dot(a, b);
    return Finite(d) && std::fabs(d) <= kAxisTolerance;
}

// ---------------------------------------------------------------------------
// The mount, checked and squared up. Pick and Build both work from this, so the
// faces drawn and the rectangles tested cannot drift apart by an axis that was a
// hair off unit length.

struct Basis {
    Vec3 centre, right, up, front;
    float halfWidth, halfHeight, reach;
};

bool MakeBasis(const Mount &m, Basis *b) {
    if (!FiniteVec(m.centre) || !FiniteVec(m.right) || !FiniteVec(m.up) || !FiniteVec(m.front))
        return false;
    if (std::fabs(m.centre.x) > kMaxCoordinate || std::fabs(m.centre.y) > kMaxCoordinate ||
        std::fabs(m.centre.z) > kMaxCoordinate)
        return false;
    if (!Finite(m.halfWidth) || !Finite(m.halfHeight) || !Finite(m.reach)) return false;
    if (!(m.halfWidth >= kMinHalfExtent) || !(m.halfWidth <= kMaxExtent)) return false;
    if (!(m.halfHeight >= kMinHalfExtent) || !(m.halfHeight <= kMaxExtent)) return false;
    if (!(m.reach >= 2.0f * kNearFraction * m.halfHeight) || !(m.reach <= kMaxExtent)) return false;
    if (!NearUnit(m.right) || !NearUnit(m.up) || !NearUnit(m.front)) return false;
    if (!NearPerpendicular(m.right, m.up) || !NearPerpendicular(m.right, m.front) ||
        !NearPerpendicular(m.up, m.front))
        return false;

    Vec3 front = m.front;
    if (!Normalize(&front)) return false;
    Vec3 right = Sub(m.right, Scale(front, Dot(m.right, front)));
    if (!Normalize(&right)) return false;
    Vec3 up = Sub(m.up, Scale(front, Dot(m.up, front)));
    up = Sub(up, Scale(right, Dot(up, right)));
    if (!Normalize(&up)) return false;

    b->centre = m.centre;
    b->right = right;
    b->up = up;
    b->front = front;
    b->halfWidth = m.halfWidth;
    b->halfHeight = m.halfHeight;
    b->reach = m.reach;
    return true;
}

inline Vec3 PointOf(const Basis &b, float u, float v, float d) {
    return Add(b.centre, Add(Scale(b.right, u), Add(Scale(b.up, v), Scale(b.front, d))));
}

inline bool IsControl(Region region) {
    return region == Controls::kRegionPrevious || region == Controls::kRegionPlay ||
           region == Controls::kRegionNext;
}
inline float CentreFraction(Region region) {
    return region == Controls::kRegionPrevious ? -kControlCentreU :
           region == Controls::kRegionNext ? kControlCentreU : 0.0f;
}
inline Extent MakeExtent(float uMin, float uMax, float vMin, float vMax) {
    Extent e;
    e.uMin = uMin; e.uMax = uMax; e.vMin = vMin; e.vMax = vMax;
    return e;
}
Extent ControlExtentOf(const Basis &b, Region region) {
    const float cu = CentreFraction(region) * b.halfWidth, hu = kControlHalfU * b.halfWidth;
    const float cv = kControlCentreV * b.halfHeight, hv = kControlHalfV * b.halfHeight;
    return MakeExtent(cu - hu, cu + hu, cv - hv, cv + hv);
}
inline bool Inside(const Extent &e, float u, float v) {
    return u >= e.uMin && u <= e.uMax && v >= e.vMin && v <= e.vMax;
}

int ValidPlaying(Decoder decoder, int playing, unsigned films) {
    const bool shown = decoder == Controls::kDecoderPlaying || decoder == Controls::kDecoderEnded;
    return shown && playing >= 0 && static_cast<unsigned>(playing) < films ? playing : -1;
}

inline Rgba MakeRgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    Rgba c;
    c.r = r; c.g = g; c.b = b; c.a = a;
    return c;
}

// ---------------------------------------------------------------------------
// Faces

struct Corner { float u, v, d; };

void PushFace(Geometry *g, const Basis &b, const Corner *corners, unsigned count,
              const Vec3 &normal, Material material, Region region, bool pickable) {
    if (g->faceCount >= kMaxFaces || count < 3u || count > 4u) return;
    Face &face = g->faces[g->faceCount++];
    for (unsigned i = 0; i < 4u; ++i) {
        const Corner &corner = corners[i < count ? i : count - 1u];
        face.vertices[i] = PointOf(b, corner.u, corner.v, corner.d);
    }
    face.vertexCount = count;
    face.normal = normal;
    face.material = material;
    face.region = region;
    face.pickable = pickable;
}

// Facing the visitor, at depth d.
void PushFront(Geometry *g, const Basis &b, const Extent &e, float d, Material material,
               Region region, bool pickable) {
    const Corner corners[4] = {{e.uMin, e.vMin, d}, {e.uMax, e.vMin, d},
                               {e.uMax, e.vMax, d}, {e.uMin, e.vMax, d}};
    PushFace(g, b, corners, 4u, b.front, material, region, pickable);
}

// Facing away from the visitor, at depth d.
void PushBack(Geometry *g, const Basis &b, const Extent &e, float d, Material material) {
    const Corner corners[4] = {{e.uMin, e.vMin, d}, {e.uMin, e.vMax, d},
                               {e.uMax, e.vMax, d}, {e.uMax, e.vMin, d}};
    PushFace(g, b, corners, 4u, Scale(b.front, -1.0f), material, Controls::kRegionNone, false);
}

// The four walls around e between depth back and depth fore, facing outward.
void PushWalls(Geometry *g, const Basis &b, const Extent &e, float back, float fore,
               Material material) {
    const Corner rightWall[4] = {{e.uMax, e.vMin, back}, {e.uMax, e.vMax, back},
                                 {e.uMax, e.vMax, fore}, {e.uMax, e.vMin, fore}};
    PushFace(g, b, rightWall, 4u, b.right, material, Controls::kRegionNone, false);
    const Corner leftWall[4] = {{e.uMin, e.vMin, back}, {e.uMin, e.vMin, fore},
                                {e.uMin, e.vMax, fore}, {e.uMin, e.vMax, back}};
    PushFace(g, b, leftWall, 4u, Scale(b.right, -1.0f), material, Controls::kRegionNone, false);
    const Corner topWall[4] = {{e.uMin, e.vMax, back}, {e.uMin, e.vMax, fore},
                               {e.uMax, e.vMax, fore}, {e.uMax, e.vMax, back}};
    PushFace(g, b, topWall, 4u, b.up, material, Controls::kRegionNone, false);
    const Corner bottomWall[4] = {{e.uMin, e.vMin, back}, {e.uMax, e.vMin, back},
                                  {e.uMax, e.vMin, fore}, {e.uMin, e.vMin, fore}};
    PushFace(g, b, bottomWall, 4u, Scale(b.up, -1.0f), material, Controls::kRegionNone, false);
}

void PushTriangle(Geometry *g, const Basis &b, float d, float u0, float v0, float u1, float v1,
                  float u2, float v2, Material material) {
    const Corner corners[3] = {{u0, v0, d}, {u1, v1, d}, {u2, v2, d}};
    PushFace(g, b, corners, 3u, b.front, material, Controls::kRegionNone, false);
}

// The mark on a control: a bar and an arrow for Previous and Next, an arrow for
// Play, a square for Stop. Each stays well inside the control's face.
void PushGlyph(Geometry *g, const Basis &b, const Extent &e, Region region, bool showStop,
               float d, Material material) {
    const float cu = 0.5f * (e.uMin + e.uMax), cv = 0.5f * (e.vMin + e.vMax);
    const float hu = 0.5f * (e.uMax - e.uMin), hv = 0.5f * (e.vMax - e.vMin);
    const float a = kGlyphScale * (hu < hv ? hu : hv);
    if (region == Controls::kRegionPlay) {
        if (showStop) {
            PushFront(g, b, MakeExtent(cu - 0.5f * a, cu + 0.5f * a, cv - 0.5f * a, cv + 0.5f * a),
                      d, material, Controls::kRegionNone, false);
        } else {
            PushTriangle(g, b, d, cu - 0.5f * a, cv - 0.6f * a, cu + 0.6f * a, cv,
                         cu - 0.5f * a, cv + 0.6f * a, material);
        }
    } else if (region == Controls::kRegionPrevious) {
        PushFront(g, b, MakeExtent(cu - 0.6f * a, cu - 0.42f * a, cv - 0.6f * a, cv + 0.6f * a),
                  d, material, Controls::kRegionNone, false);
        PushTriangle(g, b, d, cu - 0.3f * a, cv, cu + 0.6f * a, cv - 0.6f * a,
                     cu + 0.6f * a, cv + 0.6f * a, material);
    } else {
        PushFront(g, b, MakeExtent(cu + 0.42f * a, cu + 0.6f * a, cv - 0.6f * a, cv + 0.6f * a),
                  d, material, Controls::kRegionNone, false);
        PushTriangle(g, b, d, cu - 0.6f * a, cv - 0.6f * a, cu + 0.3f * a, cv,
                     cu - 0.6f * a, cv + 0.6f * a, material);
    }
}

// A border just inside a control's own face, so the highlight is never anywhere a
// press would not land.
void PushFrame(Geometry *g, const Basis &b, const Extent &e, float d) {
    const float hu = 0.5f * (e.uMax - e.uMin), hv = 0.5f * (e.vMax - e.vMin);
    const float t = kFrameScale * (hu < hv ? hu : hv);
    const Region none = Controls::kRegionNone;
    PushFront(g, b, MakeExtent(e.uMin, e.uMax, e.vMin, e.vMin + t), d, kMaterialFocusFrame, none, false);
    PushFront(g, b, MakeExtent(e.uMin, e.uMax, e.vMax - t, e.vMax), d, kMaterialFocusFrame, none, false);
    PushFront(g, b, MakeExtent(e.uMin, e.uMin + t, e.vMin + t, e.vMax - t), d, kMaterialFocusFrame, none, false);
    PushFront(g, b, MakeExtent(e.uMax - t, e.uMax, e.vMin + t, e.vMax - t), d, kMaterialFocusFrame, none, false);
}

inline bool Enabled(const Display &display, Region region) {
    return display.usable && (region == Controls::kRegionPlay || display.multipleFilms);
}

// ---------------------------------------------------------------------------
// Text. Everything the panel says that is not a film title or the Fire label.

const char kTextHeading[] = "Projector";
const char kTextNoFilms[] = "No films";
const char kTextNoFilmsInstalled[] = "No films are installed";
const char kTextUntitled[] = "Untitled film";
const char kTextFilm[] = "Film ";
const char kTextFilmLower[] = "film ";
const char kTextOf[] = " of ";
const char kTextOneFilm[] = " film";
const char kTextManyFilms[] = " films";
const char kTextNotPlaying[] = "Not playing";
const char kTextNowPlaying[] = "Now playing";
const char kTextFinished[] = "Finished";
const char kTextOnScreen[] = "On screen: ";
const char kTextFinishedOther[] = "Finished: ";
const char kTextUnavailable[] = "Films cannot be shown here";
const char kTextPrevious[] = "Previous";
const char kTextPlay[] = "Play";
const char kTextStop[] = "Stop";
const char kTextNext[] = "Next";
const char kTextActionPrevious[] = "Previous film";
const char kTextActionNext[] = "Next film";
const char kTextActionPlay[] = "Play this film";
const char kTextActionStop[] = "Stop this film";
const char kTextOnlyFilm[] = "This is the only film";
const char kTextAim[] = "Aim at a control";
const char kTextFire[] = "Fire";   // the action's own name, when the caller has no label
const char kTextEllipsis[] = "...";

// Panel metrics, in viewport units.
const float kPad = 5.0f;
const float kAccentWidth = 3.0f;
// The text stands this far in from each side, clear of the HUD capsule's cyan end
// caps that the native painter draws at the panel's ends.
const float kSideInset = 18.0f;
// Between the row groups (what is chosen, the controls, the cue), which the native
// painter draws as separate capsules.
const float kRowGap = 12.0f;
const float kChipPad = 2.0f;
const float kCapPad = 4.0f;
const float kCueGap = 6.0f;
const float kHeaderGap = 4.0f;
const float kMinPanelWidth = 160.0f;
const float kDefaultLine = 12.0f;
const float kMinLine = 8.0f;
const float kMaxLine = 28.0f;
const float kFallbackAdvance = 0.62f;        // of the line height, per byte
const float kBindingShare = 0.4f;            // of the row the Fire label may always have

const std::size_t kLineBytes = kTextCapacity;
const std::size_t kSourceCapacity = 192;
const std::size_t kLabelCapacity = 64;
const std::size_t kScanLimit = 1024;

struct Context {
    MeasureText measure;
    void *user;
    float line;
};

float Measure(const Context &c, const char *text) {
    float width = -1.0f;
    if (c.measure) width = c.measure(text, c.user);
    if (!Finite(width) || width < 0.0f)
        width = static_cast<float>(std::strlen(text)) * c.line * kFallbackAdvance;
    return width;
}

inline unsigned char Byte(char c) { return static_cast<unsigned char>(c); }
inline bool Continuation(char c) { return (Byte(c) & 0xC0u) == 0x80u; }

// The largest cut at or before n that does not fall inside a UTF-8 sequence.
// text[n] must be readable. Text in a one-byte code page loses at most three
// extra bytes to this.
std::size_t Boundary(const char *text, std::size_t n) {
    unsigned back = 0;
    while (n > 0 && back < 3u && Continuation(text[n])) { --n; ++back; }
    return n;
}

// Drops a UTF-8 sequence left unfinished at the end of the first n bytes.
std::size_t TrimPartial(const char *text, std::size_t n) {
    std::size_t k = n;
    std::size_t back = 0;
    while (k > 0 && back < 3u && Continuation(text[k - 1])) { --k; ++back; }
    if (k == 0) return n;
    const unsigned lead = Byte(text[k - 1]);
    const std::size_t need = lead >= 0xF0u ? 4u : lead >= 0xE0u ? 3u : lead >= 0xC0u ? 2u : 1u;
    if (need == 1u) return n;
    return back + 1u < need ? k - 1 : n;
}

// Copies caller text into capacity bytes: control characters become spaces, runs
// of spaces become one, and the ends are trimmed. *clipped reports text that did
// not fit. A null source is empty.
std::size_t Sanitize(const char *source, char *out, std::size_t capacity, bool *clipped) {
    std::size_t n = 0;
    bool gap = true, cut = false;
    if (source) {
        for (std::size_t i = 0; i < kScanLimit && source[i] != '\0'; ++i) {
            unsigned ch = Byte(source[i]);
            if (ch < 0x20u || ch == 0x7Fu) ch = 0x20u;
            if (ch == 0x20u) {
                if (gap) continue;
                gap = true;
            } else {
                gap = false;
            }
            if (n + 1u >= capacity) { cut = true; break; }
            out[n++] = static_cast<char>(static_cast<unsigned char>(ch));
        }
    }
    if (cut) n = TrimPartial(out, n);
    while (n > 0 && out[n - 1] == ' ') --n;
    out[n] = '\0';
    if (clipped) *clipped = cut;
    return n;
}

struct Fitted {
    float width;
    bool truncated;
    std::size_t length;
};

// One line of at most maxWidth as the caller measures it. Text that does not fit,
// or that was already clipped, is cut back a character at a time and ends in
// "..."; if not even that fits the line is empty. out holds kLineBytes.
Fitted Fit(const Context &c, const char *source, std::size_t length, bool clipped,
           float maxWidth, char *out) {
    Fitted fit;
    fit.width = 0.0f;
    fit.truncated = clipped;
    fit.length = 0;
    out[0] = '\0';
    if (length == 0) return fit;
    fit.truncated = true;
    if (!(maxWidth > 0.0f)) return fit;
    if (!clipped && length < kLineBytes) {
        std::memcpy(out, source, length);
        out[length] = '\0';
        const float width = Measure(c, out);
        if (width <= maxWidth) {
            fit.width = width;
            fit.truncated = false;
            fit.length = length;
            return fit;
        }
    }
    const std::size_t room = kLineBytes - sizeof(kTextEllipsis);
    std::size_t n = length < room ? length : room;
    for (;;) {
        n = Boundary(source, n);
        std::size_t kept = n;
        while (kept > 0 && source[kept - 1] == ' ') --kept;
        std::memcpy(out, source, kept);
        std::memcpy(out + kept, kTextEllipsis, sizeof(kTextEllipsis));
        const float width = Measure(c, out);
        if (width <= maxWidth) {
            fit.width = width;
            fit.length = kept + sizeof(kTextEllipsis) - 1u;
            return fit;
        }
        if (n == 0) break;
        --n;
    }
    out[0] = '\0';
    return fit;
}

Fitted FitWords(const Context &c, const char *words, float maxWidth, char *out) {
    return Fit(c, words, std::strlen(words), false, maxWidth, out);
}

struct Wrapped {
    unsigned count;
    char text[2][kLineBytes];
    Fitted fit[2];
};

// Up to maxLines lines, at most two. The first breaks at a space where one is in
// its second half, else in the middle of the word; the last is cut with "..." if
// there is still more.
Wrapped Wrap(const Context &c, const char *source, std::size_t length, bool clipped,
             float maxWidth, unsigned maxLines) {
    Wrapped w;
    w.count = 0;
    w.text[0][0] = '\0';
    w.text[1][0] = '\0';
    w.fit[0] = Fit(c, source, length, clipped, maxWidth, w.text[0]);
    w.fit[1] = w.fit[0];
    w.fit[1].length = 0;
    w.count = w.fit[0].length > 0 ? 1u : 0u;
    if (!w.fit[0].truncated || maxLines < 2u) return w;

    // The longest start of the text that fits the first line whole.
    char probe[kLineBytes];
    std::size_t n = length < kLineBytes - 1u ? length : kLineBytes - 1u;
    while (n > 0) {
        n = Boundary(source, n);
        if (n == 0) break;
        std::memcpy(probe, source, n);
        probe[n] = '\0';
        if (Measure(c, probe) <= maxWidth) break;
        --n;
    }
    if (n == 0 || n >= length) return w;

    std::size_t cut = n, next = n;
    if (source[n] == ' ') {
        next = n + 1u;
    } else {
        std::size_t k = n;
        while (k > 0 && source[k - 1] != ' ') --k;
        if (k > 1u && k - 1u >= n / 2u) { cut = k - 1u; next = k; }
    }
    std::memcpy(probe, source, cut);
    probe[cut] = '\0';
    const float firstWidth = Measure(c, probe);
    if (!(firstWidth <= maxWidth)) return w;

    // If nothing of the rest can be shown, the one cut line above stands: it
    // ends in "...", which a bare first half would not.
    char second[kLineBytes];
    const Fitted rest = Fit(c, source + next, length - next, clipped, maxWidth, second);
    if (rest.length == 0) return w;
    std::memcpy(w.text[0], probe, cut + 1u);
    w.fit[0].width = firstWidth;
    w.fit[0].length = cut;
    w.fit[0].truncated = false;
    std::memcpy(w.text[1], second, rest.length + 1u);
    w.fit[1] = rest;
    w.count = 2u;
    return w;
}

struct Builder {
    char text[kSourceCapacity];
    std::size_t length;
};
void Begin(Builder *b) {
    b->text[0] = '\0';
    b->length = 0;
}
void Append(Builder *b, const char *words) {
    if (!words) return;
    while (*words != '\0' && b->length + 1u < kSourceCapacity) b->text[b->length++] = *words++;
    b->text[b->length] = '\0';
}
void AppendNumber(Builder *b, unsigned value) {
    char reversed[12];
    char digits[12];
    unsigned n = 0;
    do {
        reversed[n++] = static_cast<char>('0' + static_cast<int>(value % 10u));
        value /= 10u;
    } while (value != 0u && n < 11u);
    for (unsigned i = 0; i < n; ++i) digits[i] = reversed[n - 1u - i];
    digits[n] = '\0';
    Append(b, digits);
}

Presentation EmptyPresentation() {
    Presentation p;
    std::memset(&p, 0, sizeof(p));
    p.visible = false;
    p.actionable = false;
    p.focus = Controls::kRegionNone;
    p.pressed = Controls::kRegionNone;
    p.rectCount = 0;
    p.lineCount = 0;
    return p;
}

void AddRect(Presentation *p, float x, float y, float w, float h, Style style, Region region) {
    if (p->rectCount >= kMaxRects) return;
    PanelRect &rect = p->rects[p->rectCount++];
    rect.x = x; rect.y = y; rect.w = w; rect.h = h;
    rect.style = style;
    rect.region = region;
}

void AddLine(Presentation *p, const char *text, float x, float y, const Fitted &fit, float height,
             Style style) {
    if (p->lineCount >= kMaxLines || fit.length == 0 || fit.length >= kLineBytes) return;
    TextLine &line = p->lines[p->lineCount++];
    std::memcpy(line.text, text, fit.length);
    line.text[fit.length] = '\0';
    line.x = x; line.y = y;
    line.width = fit.width;
    line.height = height;
    line.style = style;
    line.truncated = fit.truncated;
}

Frame UsableBounds(const Frame &f, float line) {
    const bool sound = Finite(f.x) && Finite(f.y) && Finite(f.w) && Finite(f.h) &&
                       f.x >= 0.0f && f.y >= 0.0f && f.w >= kMinPanelWidth &&
                       f.h >= line + 2.0f * kPad && f.x + f.w <= kViewportWidth &&
                       f.y + f.h <= kViewportHeight;
    return sound ? f : DefaultBounds();
}

float PanelHeight(float line, unsigned titleLines, bool header, bool status, bool chips, bool cue,
                  bool capped) {
    const float boxed = line + 2.0f * kChipPad;
    float h = 2.0f * kPad + static_cast<float>(titleLines) * line;
    if (header) h += line;
    if (status) h += line;
    if (chips) h += kRowGap + boxed;
    if (cue) h += kRowGap + (capped ? boxed : line);
    return h;
}

} // namespace

// ---------------------------------------------------------------------------
// Placement

Mount MakeMount(const Vec3 &centre, const Vec3 &right, const Vec3 &up, const Vec3 &front) {
    Mount m;
    m.centre = centre;
    m.right = right;
    m.up = up;
    m.front = front;
    m.halfWidth = kDefaultHalfWidth;
    m.halfHeight = kDefaultHalfHeight;
    m.reach = kDefaultReach;
    return m;
}

bool ValidMount(const Mount &mount) {
    Basis b;
    return MakeBasis(mount, &b);
}

int Chirality(const Mount &mount) {
    Basis b;
    if (!MakeBasis(mount, &b)) return 0;
    return Dot(Cross(b.right, b.up), b.front) >= 0.0f ? 1 : -1;
}

bool PanelExtent(const Mount &mount, Extent *extent) {
    Basis b;
    if (!extent || !MakeBasis(mount, &b)) return false;
    *extent = MakeExtent(-b.halfWidth, b.halfWidth, -b.halfHeight, b.halfHeight);
    return true;
}

bool ControlExtent(const Mount &mount, Region region, Extent *extent) {
    Basis b;
    if (!extent || !IsControl(region) || !MakeBasis(mount, &b)) return false;
    *extent = ControlExtentOf(b, region);
    return true;
}

bool PanelPoint(const Mount &mount, float u, float v, Vec3 *point) {
    Basis b;
    if (!point || !Finite(u) || !Finite(v) || !MakeBasis(mount, &b)) return false;
    *point = PointOf(b, u, v, 0.0f);
    return true;
}

bool ControlCentre(const Mount &mount, Region region, Vec3 *point) {
    Basis b;
    if (!point || !IsControl(region) || !MakeBasis(mount, &b)) return false;
    const Extent e = ControlExtentOf(b, region);
    *point = PointOf(b, 0.5f * (e.uMin + e.uMax), 0.5f * (e.vMin + e.vMax), 0.0f);
    return true;
}

float NearLimit(const Mount &mount) {
    Basis b;
    return MakeBasis(mount, &b) ? kNearFraction * b.halfHeight : 0.0f;
}

// ---------------------------------------------------------------------------
// Aiming

Hit NoHit() {
    Hit hit;
    hit.status = kPickAway;
    hit.inRange = false;
    hit.atConsole = false;
    hit.onPanel = false;
    hit.region = Controls::kRegionNone;
    hit.meetsPlane = false;
    hit.distance = 0.0f;
    hit.u = 0.0f;
    hit.v = 0.0f;
    hit.point = MakeVec(0.0f, 0.0f, 0.0f);
    return hit;
}

Hit Pick(const Mount &mount, const View &view) {
    Hit hit = NoHit();
    Basis b;
    if (!MakeBasis(mount, &b)) { hit.status = kPickBadMount; return hit; }
    if (!view.museumActive || !view.inRoom) { hit.status = kPickAway; return hit; }
    if (!view.focused || view.paused || view.photo) { hit.status = kPickSuspended; return hit; }

    Vec3 aim;
    hit.status = kPickBadView;
    if (!FiniteVec(view.origin) || !SafeDirection(view.aim, &aim)) return hit;
    const Vec3 rel = Sub(view.origin, b.centre);
    const float ahead = Dot(rel, b.front);
    const float squared = Dot(rel, rel);
    if (!FiniteVec(rel) || !Finite(ahead)) return hit;

    // Where the visitor stands: in front of the plane, and within reach of it.
    if (!(ahead >= kNearFraction * b.halfHeight)) { hit.status = kPickBehind; return hit; }
    if (!(squared <= b.reach * b.reach)) { hit.status = kPickOutOfReach; return hit; }
    hit.inRange = true;
    hit.status = kPickLookingAway;

    // Where the aim meets the plane. A ray leaving the plane, or sliding along
    // it, meets nothing; so does one that meets it farther off than reach.
    const float closing = -Dot(aim, b.front);
    if (!(closing >= kMinClosing)) return hit;
    const float along = ahead / closing;
    if (!Finite(along) || !(along <= b.reach)) return hit;
    const float u = Dot(rel, b.right) + along * Dot(aim, b.right);
    const float v = Dot(rel, b.up) + along * Dot(aim, b.up);
    if (!Finite(u) || !Finite(v)) return hit;
    hit.meetsPlane = true;
    hit.distance = along;
    hit.u = u;
    hit.v = v;
    hit.point = PointOf(b, u, v, 0.0f);

    const float margin = kAttentionMargin * b.halfWidth;
    if (!(std::fabs(u) <= b.halfWidth + margin) || !(std::fabs(v) <= b.halfHeight + margin))
        return hit;
    hit.atConsole = true;
    hit.status = kPickNear;
    if (!(std::fabs(u) <= b.halfWidth) || !(std::fabs(v) <= b.halfHeight)) return hit;
    hit.onPanel = true;
    hit.status = kPickPanel;
    for (unsigned i = 0; i < 3u; ++i) {
        if (Inside(ControlExtentOf(b, kControls[i]), u, v)) {
            hit.region = kControls[i];
            hit.status = kPickControl;
            break;
        }
    }
    return hit;
}

void Feed(const Hit &hit, ControlsInput *input) {
    if (!input) return;
    input->atConsole = hit.atConsole;
    input->region = hit.atConsole ? hit.region : Controls::kRegionNone;
}

HostAction ResolveCommand(const ControlsOutput &controls, unsigned films) {
    HostAction action;
    action.select = false;
    action.play = false;
    action.stop = false;
    action.hasIndex = false;
    action.index = 0u;
    if (controls.command == Controls::kCommandStop) {
        action.stop = true;
    } else if (controls.command == Controls::kCommandSelect ||
               controls.command == Controls::kCommandPlay) {
        if (controls.index >= 0 && static_cast<unsigned>(controls.index) < films) {
            action.hasIndex = true;
            action.index = static_cast<unsigned>(controls.index);
            action.select = controls.command == Controls::kCommandSelect;
            action.play = controls.command == Controls::kCommandPlay;
        }
    }
    return action;
}

bool ShowsStop(Decoder decoder, int playing, int selected, unsigned films) {
    const int shown = ValidPlaying(decoder, playing, films);
    return decoder == Controls::kDecoderPlaying && shown >= 0 && shown == selected;
}

// ---------------------------------------------------------------------------
// The console to draw

Rgba MaterialColour(Material material) {
    switch (material) {
    case kMaterialPlate: return MakeRgba(178, 138, 72, 255);
    case kMaterialCasing: return MakeRgba(46, 58, 68, 255);
    case kMaterialKeySide: return MakeRgba(20, 26, 32, 255);
    case kMaterialKey: return MakeRgba(30, 40, 50, 255);
    case kMaterialKeyFocus: return MakeRgba(70, 200, 224, 255);
    case kMaterialKeyPressed: return MakeRgba(232, 248, 252, 255);
    case kMaterialKeyDisabled: return MakeRgba(58, 62, 66, 255);
    case kMaterialGlyph: return MakeRgba(214, 226, 232, 255);
    case kMaterialGlyphLit: return MakeRgba(10, 22, 30, 255);
    case kMaterialGlyphDim: return MakeRgba(110, 116, 120, 255);
    case kMaterialFocusFrame: return MakeRgba(244, 252, 255, 255);
    case kMaterialLampIdle: return MakeRgba(74, 62, 40, 255);
    case kMaterialLampPlaying: return MakeRgba(96, 220, 128, 255);
    case kMaterialLampEnded: return MakeRgba(226, 184, 84, 255);
    case kMaterialLampUnavailable: return MakeRgba(176, 70, 58, 255);
    }
    return MakeRgba(255, 0, 255, 255);
}

Display IdleDisplay() {
    Display d;
    d.focus = Controls::kRegionNone;
    d.pressed = Controls::kRegionNone;
    d.usable = true;
    d.multipleFilms = true;
    d.showStop = false;
    d.lamp = kLampIdle;
    return d;
}

Display MakeDisplay(const Hit &hit, const ControlsOutput &controls, unsigned films,
                    Decoder decoder, int playing) {
    Display d = IdleDisplay();
    d.focus = hit.atConsole ? hit.region : Controls::kRegionNone;
    d.pressed = d.focus != Controls::kRegionNone && controls.pressed == d.focus
                    ? d.focus : Controls::kRegionNone;
    d.usable = controls.usable && films > 0u && decoder != Controls::kDecoderUnavailable;
    d.multipleFilms = films > 1u;
    d.showStop = ShowsStop(decoder, playing, controls.selected, films);
    if (decoder == Controls::kDecoderUnavailable) d.lamp = kLampUnavailable;
    else if (ValidPlaying(decoder, playing, films) < 0) d.lamp = kLampIdle;
    else d.lamp = decoder == Controls::kDecoderPlaying ? kLampPlaying : kLampEnded;
    return d;
}

bool Build(const Mount &mount, const Display &display, Geometry *geometry) {
    if (!geometry) return false;
    std::memset(geometry, 0, sizeof(*geometry));
    geometry->faceCount = 0;
    Basis b;
    if (!MakeBasis(mount, &b)) return false;

    const float plate = -kKeyRise * b.halfHeight;
    const float back = -kCasingDepth * b.halfHeight;
    const float lift = kDecalLift * b.halfHeight;
    const Extent panel = MakeExtent(-b.halfWidth, b.halfWidth, -b.halfHeight, b.halfHeight);

    // The plate and the casing behind it.
    PushFront(geometry, b, panel, plate, kMaterialPlate, Controls::kRegionNone, false);
    PushWalls(geometry, b, panel, back, plate, kMaterialCasing);
    PushBack(geometry, b, panel, back, kMaterialCasing);

    // The controls stand out of the plate as far as the control plane.
    for (unsigned i = 0; i < 3u; ++i)
        PushWalls(geometry, b, ControlExtentOf(b, kControls[i]), plate, 0.0f, kMaterialKeySide);

    // Their faces: the three rectangles Pick tests, where Pick tests them.
    for (unsigned i = 0; i < 3u; ++i) {
        const Region region = kControls[i];
        Material material = kMaterialKey;
        if (!Enabled(display, region)) material = kMaterialKeyDisabled;
        else if (display.focus == region && display.pressed == region) material = kMaterialKeyPressed;
        else if (display.focus == region) material = kMaterialKeyFocus;
        PushFront(geometry, b, ControlExtentOf(b, region), 0.0f, material, region, true);
    }

    for (unsigned i = 0; i < 3u; ++i) {
        const Region region = kControls[i];
        Material material = kMaterialGlyph;
        if (!Enabled(display, region)) material = kMaterialGlyphDim;
        else if (display.focus == region) material = kMaterialGlyphLit;
        PushGlyph(geometry, b, ControlExtentOf(b, region), region, display.showStop, lift, material);
    }

    if (IsControl(display.focus)) PushFrame(geometry, b, ControlExtentOf(b, display.focus), lift);

    Material lamp = kMaterialLampIdle;
    if (display.lamp == kLampPlaying) lamp = kMaterialLampPlaying;
    else if (display.lamp == kLampEnded) lamp = kMaterialLampEnded;
    else if (display.lamp == kLampUnavailable) lamp = kMaterialLampUnavailable;
    PushFront(geometry, b,
              MakeExtent(-kLampHalfU * b.halfWidth, kLampHalfU * b.halfWidth,
                         kLampMinV * b.halfHeight, kLampMaxV * b.halfHeight),
              plate + lift, lamp, Controls::kRegionNone, false);
    return true;
}

// ---------------------------------------------------------------------------
// The panel on the screen

Rgba StyleColour(Style style) {
    // The house palette: white is content, yellow is focus and nothing else, grey is
    // a note, green a good state, red a failed one; cyan is the HUD caps' own. The
    // native painter draws text at one times, so these are the colours shown.
    switch (style) {
    case kStylePanel: return MakeRgba(8, 16, 24, 184);
    case kStyleAccent: return MakeRgba(56, 200, 232, 255);
    case kStyleAccentPlaying: return MakeRgba(46, 176, 48, 255);
    case kStyleKey: return MakeRgba(8, 16, 24, 184);
    case kStyleKeyFocus: return MakeRgba(24, 40, 52, 224);
    case kStyleKeyPressed: return MakeRgba(56, 200, 232, 112);
    case kStyleKeyDisabled: return MakeRgba(24, 26, 30, 150);
    case kStyleBindingCap: return MakeRgba(56, 200, 232, 80);
    case kStyleHeading: return MakeRgba(168, 168, 168, 255);
    case kStyleFilmCount: return MakeRgba(168, 168, 168, 255);
    case kStyleTitle: return MakeRgba(246, 246, 246, 255);
    case kStyleStatus: return MakeRgba(168, 168, 168, 255);
    case kStylePlaying: return MakeRgba(46, 176, 48, 255);
    case kStyleWarning: return MakeRgba(220, 60, 60, 255);
    case kStyleKeyLabel: return MakeRgba(232, 232, 232, 255);
    case kStyleKeyLabelLit: return MakeRgba(255, 255, 0, 255);
    case kStyleKeyLabelDim: return MakeRgba(104, 104, 104, 255);
    case kStyleBinding: return MakeRgba(246, 246, 246, 255);
    case kStyleAction: return MakeRgba(246, 246, 246, 255);
    case kStyleHint: return MakeRgba(168, 168, 168, 255);
    }
    return MakeRgba(255, 0, 255, 255);
}

Frame DefaultBounds() {
    Frame f;
    f.x = 130.0f;
    f.y = 34.0f;
    f.w = 380.0f;
    f.h = 124.0f;
    return f;
}

PresentInput MakePresentInput() {
    PresentInput in;
    in.focus = NoHit();
    in.controls.consumeFire = false;
    in.controls.command = Controls::kCommandNone;
    in.controls.index = -1;
    in.controls.selected = -1;
    in.controls.playing = -1;
    in.controls.usable = false;
    in.controls.pressed = Controls::kRegionNone;
    in.controls.prompt = Controls::kPromptNone;
    in.films = 0u;
    in.title = 0;
    in.titleIndex = -1;
    in.playingTitle = 0;
    in.fireLabel = 0;
    in.decoder = Controls::kDecoderStopped;
    in.playing = -1;
    in.lineHeight = kDefaultLine;
    in.bounds = DefaultBounds();
    in.measure = 0;
    in.measureUser = 0;
    return in;
}

Presentation Present(const PresentInput &in) {
    Presentation p = EmptyPresentation();
    if (!in.focus.atConsole) return p;

    Context c;
    c.measure = in.measure;
    c.user = in.measureUser;
    c.line = kDefaultLine;
    if (Finite(in.lineHeight) && in.lineHeight >= kMinLine)
        c.line = in.lineHeight < kMaxLine ? in.lineHeight : kMaxLine;
    const Frame bounds = UsableBounds(in.bounds, c.line);
    const float innerX = bounds.x + kSideInset;
    const float innerW = bounds.w - 2.0f * kSideInset;
    const float boxed = c.line + 2.0f * kChipPad;

    // What there is to say. The selector's result is taken as given; anything in
    // it that does not fit the list is shown as nothing rather than guessed at.
    const unsigned films = in.films;
    const int selected = in.controls.selected >= 0 &&
                         static_cast<unsigned>(in.controls.selected) < films
                             ? in.controls.selected : -1;
    const int playing = ValidPlaying(in.decoder, in.playing, films);
    const bool unavailable = in.decoder == Controls::kDecoderUnavailable;
    const bool usable = in.controls.usable && films > 0u && !unavailable;
    const bool running = playing >= 0 && in.decoder == Controls::kDecoderPlaying;
    const bool stop = ShowsStop(in.decoder, in.playing, selected, films);
    const Region region = in.focus.region;
    const Controls::Prompt prompt = in.controls.prompt;

    // The cue. An action is offered only where this frame's aim and the selector's
    // own prompt name the same control, so the Fire label never appears for a
    // press that would be passed through or do nothing.
    const char *action = 0;
    const char *hint = 0;
    if (usable) {
        if (region == Controls::kRegionPrevious && prompt == Controls::kPromptPrevious)
            action = kTextActionPrevious;
        else if (region == Controls::kRegionNext && prompt == Controls::kPromptNext)
            action = kTextActionNext;
        else if (region == Controls::kRegionPlay && prompt == Controls::kPromptPlay)
            action = kTextActionPlay;
        else if (region == Controls::kRegionPlay && prompt == Controls::kPromptStop)
            action = kTextActionStop;
        else if ((region == Controls::kRegionPrevious || region == Controls::kRegionNext) &&
                 prompt == Controls::kPromptOnlyFilm)
            hint = kTextOnlyFilm;
        else if (region == Controls::kRegionNone && prompt == Controls::kPromptAim)
            hint = kTextAim;
    }

    Builder count;
    Begin(&count);
    if (films == 0u) {
        Append(&count, kTextNoFilms);
    } else if (selected < 0) {
        AppendNumber(&count, films);
        Append(&count, films == 1u ? kTextOneFilm : kTextManyFilms);
    } else {
        Append(&count, kTextFilm);
        AppendNumber(&count, static_cast<unsigned>(selected) + 1u);
        Append(&count, kTextOf);
        AppendNumber(&count, films);
    }

    char titleSource[kSourceCapacity];
    std::size_t titleLength = 0;
    bool titleClipped = false;
    Style titleStyle = kStyleTitle;
    if (films == 0u) {
        titleLength = Sanitize(kTextNoFilmsInstalled, titleSource, kSourceCapacity, &titleClipped);
        titleStyle = kStyleStatus;
    } else {
        if (selected >= 0 && in.titleIndex == selected)
            titleLength = Sanitize(in.title, titleSource, kSourceCapacity, &titleClipped);
        if (titleLength == 0)
            titleLength = Sanitize(kTextUntitled, titleSource, kSourceCapacity, &titleClipped);
    }

    Builder status;
    Begin(&status);
    bool statusClipped = false;
    Style statusStyle = kStyleStatus;
    if (films > 0u) {
        if (unavailable) {
            Append(&status, kTextUnavailable);
            statusStyle = kStyleWarning;
        } else if (playing < 0) {
            Append(&status, kTextNotPlaying);
        } else if (playing == selected) {
            Append(&status, running ? kTextNowPlaying : kTextFinished);
            statusStyle = kStylePlaying;
        } else {
            // Another film holds the screen: say which, so the chosen film above
            // is not mistaken for it.
            char name[kLineBytes];
            const std::size_t nameLength = Sanitize(in.playingTitle, name, kLineBytes, &statusClipped);
            Append(&status, running ? kTextOnScreen : kTextFinishedOther);
            if (nameLength > 0) {
                Append(&status, name);
            } else {
                Append(&status, kTextFilmLower);
                AppendNumber(&status, static_cast<unsigned>(playing) + 1u);
                Append(&status, kTextOf);
                AppendNumber(&status, films);
            }
            statusStyle = kStylePlaying;
        }
    }

    // Rows are left out, least needed first, until what remains fits the bounds.
    Wrapped title = Wrap(c, titleSource, titleLength, titleClipped, innerW, 2u);
    bool showHeader = true, showStatus = status.length > 0, showChips = true;
    bool showCue = action != 0 || hint != 0;
    for (int level = 0; level < 5; ++level) {
        if (PanelHeight(c.line, title.count, showHeader, showStatus, showChips, showCue,
                        action != 0) <= bounds.h)
            break;
        if (level == 0) {
            if (title.count > 1u) title = Wrap(c, titleSource, titleLength, titleClipped, innerW, 1u);
        } else if (level == 1) {
            showChips = false;
        } else if (level == 2) {
            showHeader = false;
        } else if (level == 3) {
            showStatus = false;
        } else {
            showCue = false;
        }
    }
    const float height = PanelHeight(c.line, title.count, showHeader, showStatus, showChips,
                                     showCue, action != 0);

    AddRect(&p, bounds.x, bounds.y, bounds.w, height, kStylePanel, Controls::kRegionNone);
    AddRect(&p, bounds.x, bounds.y, kAccentWidth, height,
            running ? kStyleAccentPlaying : kStyleAccent, Controls::kRegionNone);

    char text[kLineBytes];
    float y = bounds.y + kPad;
    if (showHeader) {
        const float half = 0.5f * innerW - kHeaderGap;
        Fitted fit = FitWords(c, kTextHeading, half, text);
        AddLine(&p, text, innerX, y, fit, c.line, kStyleHeading);
        fit = Fit(c, count.text, count.length, false, half, text);
        AddLine(&p, text, innerX + innerW - fit.width, y, fit, c.line, kStyleFilmCount);
        y += c.line;
    }
    for (unsigned i = 0; i < title.count; ++i) {
        AddLine(&p, title.text[i], innerX, y, title.fit[i], c.line, titleStyle);
        y += c.line;
    }
    if (showStatus) {
        const Fitted fit = Fit(c, status.text, status.length, statusClipped, innerW, text);
        AddLine(&p, text, innerX, y, fit, c.line, statusStyle);
        y += c.line;
    }
    if (showChips) {
        // The three controls again, in the console's own proportions.
        y += kRowGap;
        const float chipHalf = 0.5f * innerW * kControlHalfU;
        for (unsigned i = 0; i < 3u; ++i) {
            const Region chip = kControls[i];
            const float centre = innerX + 0.5f * innerW * (1.0f + CentreFraction(chip));
            const bool enabled = usable && (chip == Controls::kRegionPlay || films > 1u);
            const bool focused = region == chip;
            const bool held = focused && in.controls.pressed == chip;
            const Style rectStyle = !enabled ? kStyleKeyDisabled : held ? kStyleKeyPressed :
                                    focused ? kStyleKeyFocus : kStyleKey;
            const Style textStyle = !enabled ? kStyleKeyLabelDim :
                                    focused ? kStyleKeyLabelLit : kStyleKeyLabel;
            AddRect(&p, centre - chipHalf, y, 2.0f * chipHalf, boxed, rectStyle, chip);
            const char *label = chip == Controls::kRegionPrevious ? kTextPrevious :
                                chip == Controls::kRegionNext ? kTextNext :
                                stop ? kTextStop : kTextPlay;
            const Fitted fit = FitWords(c, label, 2.0f * chipHalf - 2.0f * kChipPad, text);
            AddLine(&p, text, centre - 0.5f * fit.width, y + kChipPad, fit, c.line, textStyle);
        }
        y += boxed;
    }
    if (showCue) {
        y += kRowGap;
        if (action) {
            // The bound Fire label on a cap, then what pressing it does. The
            // label is cut before the action is; it always keeps a share of the
            // row.
            char label[kLabelCapacity];
            bool labelClipped = false;
            std::size_t labelLength = Sanitize(in.fireLabel, label, kLabelCapacity, &labelClipped);
            if (labelLength == 0) labelLength = Sanitize(kTextFire, label, kLabelCapacity, &labelClipped);
            const float actionWidth = Measure(c, action);
            float labelRoom = innerW - kCueGap - actionWidth;
            if (labelRoom < kBindingShare * innerW) labelRoom = kBindingShare * innerW;
            labelRoom -= 2.0f * kCapPad;
            const Fitted labelFit = Fit(c, label, labelLength, labelClipped, labelRoom, text);
            if (labelFit.length > 0) {
                const float capWidth = labelFit.width + 2.0f * kCapPad;
                AddRect(&p, innerX, y, capWidth, boxed, kStyleBindingCap, Controls::kRegionNone);
                AddLine(&p, text, innerX + kCapPad, y + kChipPad, labelFit, c.line, kStyleBinding);
                const Fitted actionFit = FitWords(c, action, innerW - capWidth - kCueGap, text);
                AddLine(&p, text, innerX + capWidth + kCueGap, y + kChipPad, actionFit, c.line,
                        kStyleAction);
                p.actionable = true;
            }
        } else {
            const Fitted fit = FitWords(c, hint, innerW, text);
            AddLine(&p, text, innerX, y, fit, c.line, kStyleHint);
        }
    }

    if (p.lineCount == 0) return EmptyPresentation();
    p.visible = true;
    p.focus = region;
    p.pressed = region != Controls::kRegionNone && in.controls.pressed == region
                    ? region : Controls::kRegionNone;
    p.panel.x = bounds.x;
    p.panel.y = bounds.y;
    p.panel.w = bounds.w;
    p.panel.h = height;
    return p;
}

} // namespace CeMuseumCinemaConsole
