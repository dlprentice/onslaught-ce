// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_archive_scanner.h: Dedicated 8 MiB host-malloc linear swap arena,
// clean-room 2-pass standalone .msh.aya decompressor/parser/validator, deterministic
// quarantine cache (archive-scan-v1.txt), and crash-isolated --archive-scan CLI entry.
// Pure C++14 host-compatible core with zero engine/D3D9 dependencies so both game
// builds and standalone ASan/UBSan test drivers exercise the exact same code.
#ifndef CE_ARCHIVE_SCANNER_H
#define CE_ARCHIVE_SCANNER_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int uncompress(unsigned char *dest, unsigned long *destLen,
                          const unsigned char *source, unsigned long sourceLen);

namespace CeArchiveScanner {

// Dedicated 8 MiB swap arena outside retail mDefaultHeap / mThingHeap.
constexpr size_t kSwapArenaBytes = 8u * 1024u * 1024u;
constexpr unsigned char kArenaPoisonByte = 0xDDu;

// Strict safety bounds preventing zip bombs, OOB reads, or pathological mesh inputs.
constexpr uint32_t kMaxAyaMembers = 512u;
constexpr uint32_t kMaxAyaBlockBytes = 1024u * 1024u;
constexpr uint32_t kMaxAyaPackedMember = 1100000u;
constexpr size_t kMaxMeshStreamBytes = 4u * 1024u * 1024u;
constexpr uint32_t kMaxMeshTextures = 64u;
constexpr uint32_t kMaxMeshParts = 512u;
constexpr uint32_t kMaxPartPieces = 64u;
constexpr uint32_t kMaxMeshVertices = 65535u;
constexpr uint32_t kMaxMeshTriangles = 65535u;
constexpr uint32_t kMaxPartDepth = 32u;
constexpr uint32_t kMaxMeshBatches = 65u; // 64 texture slots + 1 untextured fallback
constexpr size_t kMaxQuarantineEntries = 512u;
constexpr size_t kPathCap = 260u;

inline float MinF(float a, float b) { return a < b ? a : b; }
inline float MaxF(float a, float b) { return a > b ? a : b; }
inline size_t MaxSz(size_t a, size_t b) { return a > b ? a : b; }

inline uint16_t Le16(const unsigned char *p) {
    return static_cast<uint16_t>(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}

inline uint32_t Le32(const unsigned char *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline int32_t LeS32(const unsigned char *p) {
    return static_cast<int32_t>(Le32(p));
}

inline float LeF32(const unsigned char *p) {
    const uint32_t bits = Le32(p);
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

inline unsigned char AsciiLower(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

inline bool EqualsFoldAscii(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (AsciiLower(static_cast<unsigned char>(*a)) !=
            AsciiLower(static_cast<unsigned char>(*b)))
            return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

// -----------------------------------------------------------------------------
// Dedicated 8 MiB Host-Malloc Linear/Bump Swap Arena
// -----------------------------------------------------------------------------
struct SwapArena {
    unsigned char *base;
    size_t capacity;
    size_t used;
    size_t highWater;
    size_t lastSwapPeak;
    uint64_t swapCount;
    bool poisonOnReset;

    bool Init(bool enablePoison =
#if defined(CE_ARCHIVE_POISON) || !defined(NDEBUG)
                  true
#else
                  false
#endif
    ) {
        Shutdown();
        base = static_cast<unsigned char *>(std::malloc(kSwapArenaBytes));
        if (!base) return false;
        capacity = kSwapArenaBytes;
        used = 0;
        highWater = 0;
        lastSwapPeak = 0;
        swapCount = 0;
        poisonOnReset = enablePoison;
        if (poisonOnReset) std::memset(base, kArenaPoisonByte, capacity);
        return true;
    }

    void Shutdown() {
        if (base) {
            std::free(base);
            base = NULL;
        }
        capacity = 0;
        used = 0;
        highWater = 0;
        lastSwapPeak = 0;
        swapCount = 0;
    }

    void Reset() {
        if (used > highWater) highWater = used;
        lastSwapPeak = used;
        if (poisonOnReset && base && used > 0) {
            const size_t wipe = used <= capacity ? used : capacity;
            std::memset(base, kArenaPoisonByte, wipe);
        }
        used = 0;
        ++swapCount;
    }

    void *Alloc(size_t bytes, size_t align = 16u) {
        if (!base || capacity == 0) return NULL;
        if (align == 0 || (align & (align - 1u)) != 0) align = 16u;
        const size_t mask = align - 1u;
        if (used > capacity || (capacity - used) < mask) return NULL;
        const size_t aligned = (used + mask) & ~mask;
        if (bytes > capacity - aligned) return NULL;
        used = aligned + bytes;
        if (used > highWater) highWater = used;
        return base + aligned;
    }

    template <typename T>
    T *AllocArray(size_t count) {
        if (count == 0) return NULL;
        if (count > capacity / sizeof(T)) return NULL;
        const size_t align = alignof(T) < 16u ? 16u : alignof(T);
        return static_cast<T *>(Alloc(count * sizeof(T), align));
    }

    // Rewind a temporary top-of-arena scratch allocation (re-poisoning if enabled).
    void RewindTo(size_t mark) {
        if (mark > used) return;
        if (poisonOnReset && base && used > mark)
            std::memset(base + mark, kArenaPoisonByte, used - mark);
        used = mark;
    }

    // Verifies every unused byte in [used, capacity) equals kArenaPoisonByte.
    bool VerifyPoisonTail() const {
        if (!base || !poisonOnReset || used > capacity) return false;
        for (size_t i = used; i < capacity; ++i) {
            if (base[i] != kArenaPoisonByte) return false;
        }
        return true;
    }
};

// -----------------------------------------------------------------------------
// Parsed Arena-Backed Mesh Representation (FVF 0x152: XYZ | NORMAL | DIFFUSE | TEX1)
// -----------------------------------------------------------------------------
struct ArchiveVertex {
    float x, y, z;
    float nx, ny, nz;
    uint32_t diffuse;
    float u, v;
};
static_assert(sizeof(ArchiveVertex) == 36u, "ArchiveVertex must match FVF 0x152 36-byte stride");

struct ArchiveBatch {
    int32_t textureIndex; // -1 for untextured fallback, else 0..textureCount-1
    uint32_t firstIndex;  // offset into ParsedMesh::indices
    uint32_t triangleCount;
};

struct ArchiveBounds {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    float centerX, centerY, centerZ;
    float span;
    bool valid;
};

struct ParsedMesh {
    char meshName[128];
    ArchiveVertex *vertices;
    uint32_t vertexCount;
    uint16_t *indices;
    uint32_t indexCount;
    uint32_t triangleCount;
    ArchiveBatch *batches;
    uint32_t batchCount;
    char (*textureNames)[128];
    float *textureBlends;
    uint32_t textureCount;
    uint32_t partCount;
    uint32_t skeletalPartCount;
    uint32_t referencePartCount;
    ArchiveBounds bounds;
    size_t arenaBytesUsed;
};

enum ParseError {
    kParseOk = 0,
    kParseNullArg,
    kParseArenaOom,
    kParseAyaTruncated,
    kParseAyaBadMemberSize,
    kParseAyaTooManyMembers,
    kParseAyaZlibError,
    kParseAyaTooLarge,
    kParseBadCmsh,
    kParseBadCmst,
    kParseBadMsht,
    kParseBadMesp,
    kParseBadCmsp,
    kParseBadHierarchy,
    kParseBadPmvb,
    kParseBadVertexFormat,
    kParseIndexOutOfBounds,
    kParseNonFiniteVertex,
    kParseTooManyVertices,
    kParseTooManyTriangles,
    kParseZeroGeometry
};

inline const char *ParseErrorName(ParseError err) {
    switch (err) {
    case kParseOk: return "ok";
    case kParseNullArg: return "null_arg";
    case kParseArenaOom: return "arena_oom";
    case kParseAyaTruncated: return "aya_truncated";
    case kParseAyaBadMemberSize: return "aya_bad_member_size";
    case kParseAyaTooManyMembers: return "aya_too_many_members";
    case kParseAyaZlibError: return "aya_zlib_error";
    case kParseAyaTooLarge: return "aya_too_large";
    case kParseBadCmsh: return "bad_cmsh";
    case kParseBadCmst: return "bad_cmst";
    case kParseBadMsht: return "bad_msht";
    case kParseBadMesp: return "bad_mesp";
    case kParseBadCmsp: return "bad_cmsp";
    case kParseBadHierarchy: return "bad_hierarchy";
    case kParseBadPmvb: return "bad_pmvb";
    case kParseBadVertexFormat: return "bad_vertex_format";
    case kParseIndexOutOfBounds: return "index_oob";
    case kParseNonFiniteVertex: return "nonfinite_vertex";
    case kParseTooManyVertices: return "too_many_vertices";
    case kParseTooManyTriangles: return "too_many_triangles";
    case kParseZeroGeometry: return "zero_geometry";
    default: return "unknown";
    }
}

// -----------------------------------------------------------------------------
// Clean-Room Chunk Cursor & Rest-Pose Matrix Math (matching retail FMatrix/FVector)
// -----------------------------------------------------------------------------
struct ChunkCursor {
    const unsigned char *data;
    size_t at;
    size_t end;

    bool Take(const char tag[4], const unsigned char *&payload, uint32_t &size) {
        if (end - at < 8u) return false;
        if (std::memcmp(data + at, tag, 4) != 0) return false;
        const uint32_t sz = Le32(data + at + 4u);
        if (sz > end - at - 8u) return false;
        payload = data + at + 8u;
        size = sz;
        at += 8u + sz;
        return true;
    }

    bool TakeAny(char tagOut[4], const unsigned char *&payload, uint32_t &size) {
        if (end - at < 8u) return false;
        std::memcpy(tagOut, data + at, 4);
        const uint32_t sz = Le32(data + at + 4u);
        if (sz > end - at - 8u) return false;
        payload = data + at + 8u;
        size = sz;
        at += 8u + sz;
        return true;
    }

    bool Done() const { return at == end; }
};

struct Mat3 {
    float r[3][3];
};

struct Vec3 {
    float x, y, z;
};

inline Mat3 IdentityMat3() {
    Mat3 m = {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    return m;
}

inline Vec3 MulMatVec(const Mat3 &m, const Vec3 &v) {
    return Vec3{
        m.r[0][0] * v.x + m.r[0][1] * v.y + m.r[0][2] * v.z,
        m.r[1][0] * v.x + m.r[1][1] * v.y + m.r[1][2] * v.z,
        m.r[2][0] * v.x + m.r[2][1] * v.y + m.r[2][2] * v.z
    };
}

inline Mat3 MulMatMat(const Mat3 &a, const Mat3 &b) {
    Mat3 out = {};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out.r[i][j] = a.r[i][0] * b.r[0][j] + a.r[i][1] * b.r[1][j] + a.r[i][2] * b.r[2][j];
    return out;
}

inline bool FiniteVec3(const Vec3 &v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline bool FiniteMat3(const Mat3 &m) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!std::isfinite(m.r[i][j])) return false;
    return true;
}

// -----------------------------------------------------------------------------
// Bounded Multi-Member AYA Decompressor -> SwapArena
// -----------------------------------------------------------------------------
inline ParseError DecompressAyaToArena(const unsigned char *ayaBytes, size_t ayaSize,
                                       SwapArena *arena,
                                       unsigned char **outStream, size_t *outStreamSize) {
    if (!ayaBytes || !arena || !arena->base || !outStream || !outStreamSize)
        return kParseNullArg;
    *outStream = NULL;
    *outStreamSize = 0;
    if (ayaSize < 4u) return kParseAyaTruncated;

    // Align stream start to 16 bytes and grow arena->used tightly per decoded block so
    // highWater reflects exact peak footprint and untouched arena tail stays 0xDD poisoned.
    if (arena->used > arena->capacity || (arena->capacity - arena->used) < 16u)
        return kParseArenaOom;
    const size_t streamStartMark = arena->used;
    const size_t alignedStart = (streamStartMark + 15u) & ~size_t(15u);
    if (alignedStart >= arena->capacity) return kParseArenaOom;
    const size_t avail = arena->capacity - alignedStart;
    const size_t maxOut = avail < kMaxMeshStreamBytes ? avail : kMaxMeshStreamBytes;
    unsigned char *outBuf = arena->base + alignedStart;
    arena->used = alignedStart;

    size_t pos = 0;
    size_t totalOut = 0;
    uint32_t members = 0;

    while (pos < ayaSize) {
        if (++members > kMaxAyaMembers) {
            arena->RewindTo(streamStartMark);
            return kParseAyaTooManyMembers;
        }
        if (ayaSize - pos < 4u) {
            arena->RewindTo(streamStartMark);
            return kParseAyaTruncated;
        }
        const uint32_t packedLen = Le32(ayaBytes + pos);
        pos += 4u;
        if (packedLen == 0u || packedLen > kMaxAyaPackedMember || packedLen > ayaSize - pos) {
            arena->RewindTo(streamStartMark);
            return kParseAyaBadMemberSize;
        }
        const size_t remain = maxOut - totalOut;
        if (remain == 0u) {
            arena->RewindTo(streamStartMark);
            return kParseAyaTooLarge;
        }
        unsigned long dstLen = static_cast<unsigned long>(remain < kMaxAyaBlockBytes ? remain : kMaxAyaBlockBytes);
        const int zrc = uncompress(outBuf + totalOut, &dstLen,
                                   ayaBytes + pos, static_cast<unsigned long>(packedLen));
        if (zrc != 0 || dstLen == 0u || dstLen > remain) {
            // Zlib may have partially written into outBuf + totalOut before failing; advance used
            // across the written span so RewindTo restores 0xDD poison cleanly everywhere.
            arena->used = alignedStart + totalOut + (remain < kMaxAyaBlockBytes ? remain : kMaxAyaBlockBytes);
            if (arena->used > arena->capacity) arena->used = arena->capacity;
            arena->RewindTo(streamStartMark);
            return kParseAyaZlibError;
        }
        totalOut += static_cast<size_t>(dstLen);
        arena->used = alignedStart + totalOut;
        if (arena->used > arena->highWater) arena->highWater = arena->used;
        pos += packedLen;
    }

    if (totalOut == 0u) {
        arena->RewindTo(streamStartMark);
        return kParseAyaTruncated;
    }

    // Pad arena->used to 16-byte alignment cleanly.
    const size_t alignedEnd = (alignedStart + totalOut + 15u) & ~size_t(15u);
    if (alignedEnd > arena->capacity) {
        arena->RewindTo(streamStartMark);
        return kParseArenaOom;
    }
    arena->used = alignedEnd;
    if (arena->used > arena->highWater) arena->highWater = arena->used;

    *outStream = outBuf;
    *outStreamSize = totalOut;
    return kParseOk;
}

// -----------------------------------------------------------------------------
// Clean-Room 2-Pass Standalone .msh Parser -> Arena-Backed Indexed Triangle Lists
// -----------------------------------------------------------------------------
namespace Detail {

struct PieceView {
    const unsigned char *ibuf;
    uint32_t indexCount;
    uint32_t triangleCount;
    int32_t textureSlot;
};

struct PartView {
    uint32_t partType;
    uint32_t childCount;
    const unsigned char *childData; // 4 * childCount LE uint32
    bool hasParent;
    uint32_t parentIndex;
    bool hasRef;
    uint32_t refTarget;
    Mat3 localOri;
    Vec3 localPos;
    Mat3 worldOri;
    Vec3 worldPos;
    bool visited;
    uint32_t pieceCount;
    uint32_t vertexCount;
    uint32_t stride;
    uint32_t primType;
    const unsigned char *vbuf;
    PieceView *pieces;
};

inline bool ParsePartChunk(const unsigned char *mesp, uint32_t mespSize,
                           uint32_t partIndex, uint32_t partCount, uint32_t texCount,
                           SwapArena *arena, PartView *pv, ParseError *outErr) {
    ChunkCursor pc = {mesp, 0, mespSize};
    const unsigned char *p = NULL;
    uint32_t sz = 0;
    if (!pc.Take("CMSP", p, sz) || sz != 316u) {
        *outErr = kParseBadCmsp;
        return false;
    }
    pv->partType = Le32(p + 0x8Cu);
    if (pv->partType < 1u || pv->partType > 6u) {
        *outErr = kParseBadCmsp;
        return false;
    }
    const uint32_t declaredChildren = Le32(p + 0x90u);
    const uint32_t declaredVFrames = Le32(p + 0xB8u);
    const uint32_t declaredHFrames = Le32(p + 0xBCu);
    if (declaredChildren > partCount) {
        *outErr = kParseBadHierarchy;
        return false;
    }
    pv->localOri = IdentityMat3();
    pv->localPos = Vec3{0.0f, 0.0f, 0.0f};
    pv->worldOri = IdentityMat3();
    pv->worldPos = Vec3{0.0f, 0.0f, 0.0f};

    uint32_t key0 = 0;
    bool haveVhfm = false;
    const unsigned char *horiData = NULL;
    uint32_t horiFrames = 0;
    const unsigned char *hposData = NULL;
    uint32_t hposFrames = 0;

    while (!pc.Done()) {
        char tag[4] = {};
        if (!pc.TakeAny(tag, p, sz)) {
            *outErr = kParseBadMesp;
            return false;
        }
        if (std::memcmp(tag, "CHLD", 4) == 0) {
            if (sz != declaredChildren * 4u) {
                *outErr = kParseBadHierarchy;
                return false;
            }
            for (uint32_t ci = 0; ci < declaredChildren; ++ci) {
                const uint32_t ch = Le32(p + ci * 4u);
                if (ch >= partCount || ch == partIndex) {
                    *outErr = kParseBadHierarchy;
                    return false;
                }
            }
            pv->childCount = declaredChildren;
            pv->childData = p;
        } else if (std::memcmp(tag, "PRNT", 4) == 0) {
            if (sz != 4u) {
                *outErr = kParseBadHierarchy;
                return false;
            }
            const uint32_t pr = Le32(p);
            if (pr >= partCount || pr == partIndex) {
                *outErr = kParseBadHierarchy;
                return false;
            }
            pv->hasParent = true;
            pv->parentIndex = pr;
        } else if (std::memcmp(tag, "VHFM", 4) == 0) {
            if (sz == 0u || (declaredVFrames && sz != declaredVFrames)) {
                *outErr = kParseBadMesp;
                return false;
            }
            key0 = p[0];
            haveVhfm = true;
        } else if (std::memcmp(tag, "HORI", 4) == 0) {
            if (sz % 48u != 0u || (declaredHFrames && sz != declaredHFrames * 48u)) {
                *outErr = kParseBadMesp;
                return false;
            }
            horiData = p;
            horiFrames = sz / 48u;
        } else if (std::memcmp(tag, "HPOS", 4) == 0) {
            if (sz % 16u != 0u || (declaredHFrames && sz != declaredHFrames * 16u)) {
                *outErr = kParseBadMesp;
                return false;
            }
            hposData = p;
            hposFrames = sz / 16u;
        } else if (std::memcmp(tag, "REFR", 4) == 0) {
            if (sz != 4u) {
                *outErr = kParseBadHierarchy;
                return false;
            }
            const uint32_t target = Le32(p);
            if (target >= partCount || target == partIndex) {
                *outErr = kParseBadHierarchy;
                return false;
            }
            pv->hasRef = true;
            pv->refTarget = target;
        } else if (std::memcmp(tag, "PMVB", 4) == 0) {
            ChunkCursor vc = {p, 0, sz};
            const unsigned char *vp = NULL;
            uint32_t vsz = 0;
            if (!vc.Take("CMVB", vp, vsz) || vsz != 296u) {
                *outErr = kParseBadPmvb;
                return false;
            }
            const uint32_t pieces = Le32(vp + 0x108u);
            const uint32_t stride = Le32(vp + 0x114u);
            const uint32_t primType = Le32(vp + 0x11Cu);
            if (pieces > kMaxPartPieces) {
                *outErr = kParseBadPmvb;
                return false;
            }
            if (pieces > 0u) {
                if ((stride != 36u && stride != 48u) || (primType != 4u && primType != 3u)) {
                    *outErr = kParseBadVertexFormat;
                    return false;
                }
                pv->pieces = arena->AllocArray<PieceView>(pieces);
                if (!pv->pieces) {
                    *outErr = kParseArenaOom;
                    return false;
                }
                std::memset(pv->pieces, 0, pieces * sizeof(PieceView));
            }
            pv->pieceCount = pieces;
            pv->stride = stride;
            pv->primType = primType;

            uint32_t partVertCount = 0;
            uint32_t partVertBytes = 0;
            for (uint32_t pi = 0; pi < pieces; ++pi) {
                const unsigned char *mmpt = NULL, *ibuf = NULL, *vbuf = NULL, *texr = NULL;
                uint32_t mmptSz = 0, ibufSz = 0, vbufSz = 0, texrSz = 0;
                if (!vc.Take("MMPT", mmpt, mmptSz) || mmptSz != 24u ||
                    !vc.Take("IBUF", ibuf, ibufSz) ||
                    !vc.Take("VBUF", vbuf, vbufSz) ||
                    !vc.Take("TEXR", texr, texrSz) || texrSz != 24u) {
                    *outErr = kParseBadPmvb;
                    return false;
                }
                const uint32_t vbBytes = Le32(mmpt + 0u);
                const uint32_t ibBytes = Le32(mmpt + 4u);
                const uint32_t ic = Le32(mmpt + 8u);
                const uint32_t vcCount = Le32(mmpt + 12u);
                if (ic < 3u || ic > 65535u || ibBytes != ic * 2u || ibufSz != ibBytes) {
                    *outErr = kParseBadPmvb;
                    return false;
                }
                if (pi == 0u) {
                    if (vcCount == 0u || vcCount > kMaxMeshVertices ||
                        vbBytes != vcCount * stride || vbufSz != vbBytes) {
                        *outErr = kParseBadPmvb;
                        return false;
                    }
                    partVertCount = vcCount;
                    partVertBytes = vbBytes;
                    pv->vertexCount = vcCount;
                    pv->vbuf = vbuf;
                } else {
                    if (vcCount != partVertCount || vbBytes != partVertBytes || vbufSz != 0u) {
                        *outErr = kParseBadPmvb;
                        return false;
                    }
                }
                // Validate every index against partVertCount before counting non-degenerate triangles.
                for (uint32_t k = 0; k < ic; ++k) {
                    if (Le16(ibuf + k * 2u) >= partVertCount) {
                        *outErr = kParseIndexOutOfBounds;
                        return false;
                    }
                }
                uint32_t triCount = 0;
                if (primType == 4u) {
                    for (uint32_t k = 0; k + 2u < ic; ++k) {
                        const uint16_t a = Le16(ibuf + k * 2u);
                        const uint16_t b = Le16(ibuf + (k + 1u) * 2u);
                        const uint16_t c = Le16(ibuf + (k + 2u) * 2u);
                        if (a != b && b != c && a != c) ++triCount;
                    }
                } else {
                    if (ic % 3u != 0u) {
                        *outErr = kParseBadPmvb;
                        return false;
                    }
                    for (uint32_t k = 0; k + 2u < ic; k += 3u) {
                        const uint16_t a = Le16(ibuf + k * 2u);
                        const uint16_t b = Le16(ibuf + (k + 1u) * 2u);
                        const uint16_t c = Le16(ibuf + (k + 2u) * 2u);
                        if (a != b && b != c && a != c) ++triCount;
                    }
                }

                // Resolve layer 0 texture slot (or first valid slot in 0..5).
                int32_t chosenTex = -1;
                for (int slot = 0; slot < 6; ++slot) {
                    const int32_t ti = LeS32(texr + slot * 4);
                    if (ti != -1) {
                        if (ti < 0 || static_cast<uint32_t>(ti) >= texCount) {
                            *outErr = kParseBadPmvb;
                            return false;
                        }
                        if (chosenTex < 0) chosenTex = ti;
                    }
                }
                pv->pieces[pi].ibuf = ibuf;
                pv->pieces[pi].indexCount = ic;
                pv->pieces[pi].triangleCount = triCount;
                pv->pieces[pi].textureSlot = chosenTex;
            }
            if (!vc.Done()) {
                *outErr = kParseBadPmvb;
                return false;
            }
        }
    }

    // Evaluate local rest-pose orientation & position from key0.
    const uint32_t kIdx = haveVhfm ? key0 : 0u;
    if (horiData && horiFrames > 0u) {
        if (kIdx >= horiFrames) {
            *outErr = kParseBadMesp;
            return false;
        }
        const unsigned char *m = horiData + kIdx * 48u;
        for (int row = 0; row < 3; ++row) {
            pv->localOri.r[row][0] = LeF32(m + row * 16 + 0);
            pv->localOri.r[row][1] = LeF32(m + row * 16 + 4);
            pv->localOri.r[row][2] = LeF32(m + row * 16 + 8);
        }
        if (!FiniteMat3(pv->localOri)) {
            *outErr = kParseNonFiniteVertex;
            return false;
        }
    }
    if (hposData && hposFrames > 0u) {
        if (kIdx >= hposFrames) {
            *outErr = kParseBadMesp;
            return false;
        }
        const unsigned char *vp = hposData + kIdx * 16u;
        pv->localPos.x = LeF32(vp + 0);
        pv->localPos.y = LeF32(vp + 4);
        pv->localPos.z = LeF32(vp + 8);
        if (!FiniteVec3(pv->localPos)) {
            *outErr = kParseNonFiniteVertex;
            return false;
        }
    }
    return true;
}

inline bool WalkPartHierarchy(PartView *parts, uint32_t partCount,
                              uint32_t idx, uint32_t depth,
                              const Vec3 &parentPos, const Mat3 &parentOri,
                              uint32_t *outDrawOrder, uint32_t *outDrawCount) {
    if (depth > kMaxPartDepth || idx >= partCount) return false;
    PartView &pv = parts[idx];
    if (pv.visited) return false; // cycle or duplicate child
    pv.visited = true;

    const Vec3 rotated = MulMatVec(parentOri, pv.localPos);
    pv.worldPos = Vec3{parentPos.x + rotated.x, parentPos.y + rotated.y, parentPos.z + rotated.z};
    pv.worldOri = MulMatMat(parentOri, pv.localOri);
    if (!FiniteVec3(pv.worldPos) || !FiniteMat3(pv.worldOri)) return false;

    outDrawOrder[(*outDrawCount)++] = idx;

    for (uint32_t ci = 0; ci < pv.childCount; ++ci) {
        const uint32_t childIdx = Le32(pv.childData + ci * 4u);
        if (!WalkPartHierarchy(parts, partCount, childIdx, depth + 1u,
                               pv.worldPos, pv.worldOri, outDrawOrder, outDrawCount))
            return false;
    }
    return true;
}

} // namespace Detail

// 2-pass parser: validates every chunk, evaluates rest-pose hierarchy, allocates exact
// arena-backed vertices/indices/batches grouped by texture slot, and computes exact AABB.
inline ParseError ParseMeshFromArenaStream(const unsigned char *stream, size_t streamSize,
                                           SwapArena *arena, ParsedMesh *out) {
    if (!stream || !arena || !arena->base || !out) return kParseNullArg;
    std::memset(out, 0, sizeof(*out));

    ChunkCursor c = {stream, 0, streamSize};
    const unsigned char *p = NULL;
    uint32_t sz = 0;

    if (!c.Take("CMSH", p, sz) || sz != 372u) return kParseBadCmsh;
    const uint32_t numTextures = Le32(p + 0x04u);
    const uint32_t numParts = Le32(p + 0x15Cu);
    if (numTextures > kMaxMeshTextures || numParts == 0u || numParts > kMaxMeshParts)
        return kParseBadCmsh;

    // Copy sanitized null-terminated mesh name from CMSH +0x24.
    {
        size_t len = 0;
        while (len < 127u && len < 300u && p[0x24u + len] != 0) {
            const unsigned char ch = p[0x24u + len];
            out->meshName[len] = (ch >= 0x20u && ch < 0x7Fu) ? static_cast<char>(ch) : ' ';
            ++len;
        }
        out->meshName[len] = '\0';
    }

    if (!c.Take("CMST", p, sz) || sz != 36u * numTextures) return kParseBadCmst;
    uint32_t texFrames[kMaxMeshTextures] = {};
    for (uint32_t ti = 0; ti < numTextures; ++ti) {
        texFrames[ti] = Le32(p + 36u * ti + 8u);
        if (texFrames[ti] == 0u || texFrames[ti] > 64u) return kParseBadCmst;
    }

    if (numTextures > 0u) {
        out->textureNames = static_cast<char (*)[128]>(arena->Alloc(numTextures * 128u, 16u));
        out->textureBlends = arena->AllocArray<float>(numTextures);
        if (!out->textureNames || !out->textureBlends) return kParseArenaOom;
        std::memset(out->textureNames, 0, numTextures * 128u);
        out->textureCount = numTextures;
    }

    for (uint32_t ti = 0; ti < numTextures; ++ti) {
        if (!c.Take("MSHT", p, sz)) return kParseBadMsht;
        ChunkCursor mc = {p, 0, sz};
        const unsigned char *tb = NULL;
        uint32_t tbSz = 0;
        const uint32_t need = 20u * texFrames[ti] + 128u;
        if (!mc.Take("TEXB", tb, tbSz) || !mc.Done() || tbSz != need)
            return kParseBadMsht;
        const float blend0 = LeF32(tb);
        out->textureBlends[ti] = std::isfinite(blend0) ? blend0 : 1.0f;
        const unsigned char *nameBytes = tb + 20u * texFrames[ti];
        size_t n = 0;
        while (n < 127u && nameBytes[n] != 0) {
            const unsigned char ch = nameBytes[n];
            if (ch < 0x20u || ch >= 0x7Fu) return kParseBadMsht;
            out->textureNames[ti][n] = static_cast<char>(ch);
            ++n;
        }
        if (n == 0u || nameBytes[n] != 0) return kParseBadMsht;
        out->textureNames[ti][n] = '\0';
    }

    // Pass 1: parse all MESP chunks into temporary arena scratch arrays.
    const size_t scratchMark = arena->used;
    Detail::PartView *parts = arena->AllocArray<Detail::PartView>(numParts);
    uint32_t *drawOrder = arena->AllocArray<uint32_t>(numParts);
    if (!parts || !drawOrder) return kParseArenaOom;
    std::memset(parts, 0, numParts * sizeof(Detail::PartView));

    for (uint32_t pi = 0; pi < numParts; ++pi) {
        if (!c.Take("MESP", p, sz)) return kParseBadMesp;
        ParseError pe = kParseOk;
        if (!Detail::ParsePartChunk(p, sz, pi, numParts, numTextures, arena, &parts[pi], &pe))
            return pe;
        if (parts[pi].partType == 3u) ++out->skeletalPartCount;
        if (parts[pi].partType == 6u) ++out->referencePartCount;
    }

    // Validate reference targets exist and point to renderable geometry parts.
    for (uint32_t pi = 0; pi < numParts; ++pi) {
        if (parts[pi].partType == 6u) {
            if (!parts[pi].hasRef || parts[pi].refTarget >= numParts) return kParseBadHierarchy;
            const uint32_t tgt = parts[pi].refTarget;
            if (parts[tgt].partType != 1u && parts[tgt].partType != 3u) return kParseBadHierarchy;
        }
    }

    // Walk part hierarchy starting from root part 0 (and any unvisited root if present).
    uint32_t drawCount = 0;
    const Vec3 rootPos = {0.0f, 0.0f, 0.0f};
    const Mat3 rootOri = IdentityMat3();
    if (!Detail::WalkPartHierarchy(parts, numParts, 0u, 0u, rootPos, rootOri, drawOrder, &drawCount))
        return kParseBadHierarchy;
    for (uint32_t pi = 0; pi < numParts; ++pi) {
        if (!parts[pi].visited && !parts[pi].hasParent) {
            if (!Detail::WalkPartHierarchy(parts, numParts, pi, 0u, rootPos, rootOri, drawOrder, &drawCount))
                return kParseBadHierarchy;
        }
    }

    // Measure exact total vertices & triangles per texture bucket (0..numTextures, where numTextures = untextured).
    uint32_t bucketTris[kMaxMeshBatches] = {};
    uint32_t totalVerts = 0;
    uint32_t totalTris = 0;
    const uint32_t untexBucket = numTextures;

    for (uint32_t di = 0; di < drawCount; ++di) {
        const Detail::PartView &inst = parts[drawOrder[di]];
        const Detail::PartView *geom = NULL;
        if (inst.partType == 1u || inst.partType == 3u) geom = &inst;
        else if (inst.partType == 6u && inst.hasRef) geom = &parts[inst.refTarget];
        if (!geom || geom->pieceCount == 0u || geom->vertexCount == 0u) continue;

        if (geom->vertexCount > kMaxMeshVertices - totalVerts) return kParseTooManyVertices;
        totalVerts += geom->vertexCount;

        for (uint32_t pc = 0; pc < geom->pieceCount; ++pc) {
            const Detail::PieceView &pv = geom->pieces[pc];
            if (pv.triangleCount > kMaxMeshTriangles - totalTris) return kParseTooManyTriangles;
            totalTris += pv.triangleCount;
            const uint32_t bucket = (pv.textureSlot >= 0 && static_cast<uint32_t>(pv.textureSlot) < numTextures)
                ? static_cast<uint32_t>(pv.textureSlot)
                : untexBucket;
            bucketTris[bucket] += pv.triangleCount;
        }
    }

    if (totalVerts == 0u || totalTris == 0u) return kParseZeroGeometry;

    uint32_t activeBatches = 0;
    for (uint32_t b = 0; b <= numTextures; ++b)
        if (bucketTris[b] > 0u) ++activeBatches;

    // Pass 2: allocate final output buffers in the swap arena and bake transformed rest-pose geometry.
    // Note: keep scratch intact during pass 2 since parts[] points into stream & scratch pieces[].
    ArchiveVertex *verts = arena->AllocArray<ArchiveVertex>(totalVerts);
    uint16_t *indices = arena->AllocArray<uint16_t>(totalTris * 3u);
    ArchiveBatch *batches = arena->AllocArray<ArchiveBatch>(activeBatches);
    if (!verts || !indices || !batches) return kParseArenaOom;

    uint32_t bucketCursor[kMaxMeshBatches] = {};
    {
        uint32_t batchIdx = 0;
        uint32_t idxCursor = 0;
        for (uint32_t b = 0; b <= numTextures; ++b) {
            if (bucketTris[b] == 0u) continue;
            batches[batchIdx].textureIndex = (b < numTextures) ? static_cast<int32_t>(b) : -1;
            batches[batchIdx].firstIndex = idxCursor;
            batches[batchIdx].triangleCount = bucketTris[b];
            bucketCursor[b] = idxCursor;
            idxCursor += bucketTris[b] * 3u;
            ++batchIdx;
        }
    }

    float minX = 1e30f, minY = 1e30f, minZ = 1e30f;
    float maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
    uint32_t vertBase = 0;

    for (uint32_t di = 0; di < drawCount; ++di) {
        const Detail::PartView &inst = parts[drawOrder[di]];
        const Detail::PartView *geom = NULL;
        if (inst.partType == 1u || inst.partType == 3u) geom = &inst;
        else if (inst.partType == 6u && inst.hasRef) geom = &parts[inst.refTarget];
        if (!geom || geom->pieceCount == 0u || geom->vertexCount == 0u || !geom->vbuf) continue;

        const bool skinned = (geom->stride == 48u);
        const size_t normalOff = skinned ? 24u : 12u;
        const size_t diffOff = skinned ? 36u : 24u;
        const size_t uvOff = skinned ? 40u : 28u;

        for (uint32_t vi = 0; vi < geom->vertexCount; ++vi) {
            const unsigned char *v = geom->vbuf + static_cast<size_t>(vi) * geom->stride;
            const Vec3 lp = {LeF32(v + 0), LeF32(v + 4), LeF32(v + 8)};
            Vec3 ln = {LeF32(v + normalOff + 0), LeF32(v + normalOff + 4), LeF32(v + normalOff + 8)};
            const uint32_t diff = Le32(v + diffOff);
            float u = LeF32(v + uvOff + 0);
            float tv = LeF32(v + uvOff + 4);

            if (!FiniteVec3(lp)) return kParseNonFiniteVertex;
            if (!FiniteVec3(ln)) ln = Vec3{0.0f, 0.0f, -1.0f};
            if (!std::isfinite(u)) u = 0.0f;
            if (!std::isfinite(tv)) tv = 0.0f;

            const Vec3 rp = MulMatVec(inst.worldOri, lp);
            const Vec3 wp = {rp.x + inst.worldPos.x, rp.y + inst.worldPos.y, rp.z + inst.worldPos.z};
            Vec3 wn = MulMatVec(inst.worldOri, ln);
            const float nlen = std::sqrt(wn.x * wn.x + wn.y * wn.y + wn.z * wn.z);
            if (nlen > 1e-6f) {
                wn.x /= nlen;
                wn.y /= nlen;
                wn.z /= nlen;
            } else {
                wn = Vec3{0.0f, 0.0f, -1.0f};
            }
            if (!FiniteVec3(wp) || !FiniteVec3(wn)) return kParseNonFiniteVertex;

            ArchiveVertex &dst = verts[vertBase + vi];
            dst.x = wp.x; dst.y = wp.y; dst.z = wp.z;
            dst.nx = wn.x; dst.ny = wn.y; dst.nz = wn.z;
            dst.diffuse = (diff & 0xFF000000u) ? diff : (diff | 0xFF000000u);
            dst.u = u; dst.v = tv;

            minX = MinF(minX, wp.x); maxX = MaxF(maxX, wp.x);
            minY = MinF(minY, wp.y); maxY = MaxF(maxY, wp.y);
            minZ = MinF(minZ, wp.z); maxZ = MaxF(maxZ, wp.z);
        }

        for (uint32_t pc = 0; pc < geom->pieceCount; ++pc) {
            const Detail::PieceView &pv = geom->pieces[pc];
            const uint32_t bucket = (pv.textureSlot >= 0 && static_cast<uint32_t>(pv.textureSlot) < numTextures)
                ? static_cast<uint32_t>(pv.textureSlot)
                : untexBucket;
            uint32_t &cursor = bucketCursor[bucket];
            if (geom->primType == 4u) {
                for (uint32_t k = 0; k + 2u < pv.indexCount; ++k) {
                    const uint16_t a = Le16(pv.ibuf + k * 2u);
                    const uint16_t b = Le16(pv.ibuf + (k + 1u) * 2u);
                    const uint16_t cc = Le16(pv.ibuf + (k + 2u) * 2u);
                    if (a == b || b == cc || a == cc) continue;
                    if ((k & 1u) == 0u) {
                        indices[cursor++] = static_cast<uint16_t>(vertBase + a);
                        indices[cursor++] = static_cast<uint16_t>(vertBase + b);
                        indices[cursor++] = static_cast<uint16_t>(vertBase + cc);
                    } else {
                        indices[cursor++] = static_cast<uint16_t>(vertBase + a);
                        indices[cursor++] = static_cast<uint16_t>(vertBase + cc);
                        indices[cursor++] = static_cast<uint16_t>(vertBase + b);
                    }
                }
            } else {
                for (uint32_t k = 0; k + 2u < pv.indexCount; k += 3u) {
                    const uint16_t a = Le16(pv.ibuf + k * 2u);
                    const uint16_t b = Le16(pv.ibuf + (k + 1u) * 2u);
                    const uint16_t cc = Le16(pv.ibuf + (k + 2u) * 2u);
                    if (a == b || b == cc || a == cc) continue;
                    indices[cursor++] = static_cast<uint16_t>(vertBase + a);
                    indices[cursor++] = static_cast<uint16_t>(vertBase + b);
                    indices[cursor++] = static_cast<uint16_t>(vertBase + cc);
                }
            }
        }
        vertBase += geom->vertexCount;
    }

    (void)scratchMark;
    out->vertices = verts;
    out->vertexCount = totalVerts;
    out->indices = indices;
    out->indexCount = totalTris * 3u;
    out->triangleCount = totalTris;
    out->batches = batches;
    out->batchCount = activeBatches;
    out->partCount = numParts;

    const float dx = maxX - minX;
    const float dy = maxY - minY;
    const float dz = maxZ - minZ;
    const float diagXY = std::sqrt(dx * dx + dy * dy);
    const float span = MaxF(diagXY, dz);
    out->bounds.minX = minX; out->bounds.minY = minY; out->bounds.minZ = minZ;
    out->bounds.maxX = maxX; out->bounds.maxY = maxY; out->bounds.maxZ = maxZ;
    out->bounds.centerX = 0.5f * (minX + maxX);
    out->bounds.centerY = 0.5f * (minY + maxY);
    out->bounds.centerZ = 0.5f * (minZ + maxZ);
    out->bounds.span = span;
    out->bounds.valid = std::isfinite(span) && span > 1e-5f;
    out->arenaBytesUsed = arena->used;
    return kParseOk;
}

inline ParseError DecompressAndParseAyaMesh(const unsigned char *ayaBytes, size_t ayaSize,
                                            SwapArena *arena, ParsedMesh *out) {
    if (!arena || !out) return kParseNullArg;
    unsigned char *stream = NULL;
    size_t streamSize = 0;
    const ParseError derr = DecompressAyaToArena(ayaBytes, ayaSize, arena, &stream, &streamSize);
    if (derr != kParseOk) return derr;
    return ParseMeshFromArenaStream(stream, streamSize, arena, out);
}

// -----------------------------------------------------------------------------
// Deterministic FNV-1a 64-bit Install Fingerprint & Quarantine Cache (v1)
// -----------------------------------------------------------------------------
constexpr uint64_t kFnv64Basis = 14695981039346656037ULL;
constexpr uint64_t kFnv64Prime = 1099511628211ULL;

inline uint64_t HashBytes(uint64_t h, const void *data, size_t len) {
    const unsigned char *p = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= uint64_t(p[i]);
        h *= kFnv64Prime;
    }
    return h;
}

inline uint64_t HashStringLower(uint64_t h, const char *s) {
    if (!s) return h;
    for (; *s; ++s) {
        const unsigned char c = AsciiLower(static_cast<unsigned char>(*s));
        h ^= uint64_t(c);
        h *= kFnv64Prime;
    }
    h ^= 0xFFULL;
    h *= kFnv64Prime;
    return h;
}

inline uint64_t HashU64(uint64_t h, uint64_t v) {
    unsigned char le[8];
    for (int i = 0; i < 8; ++i) le[i] = static_cast<unsigned char>((v >> (i * 8)) & 0xFFu);
    return HashBytes(h, le, sizeof(le));
}

struct QuarantineEntry {
    char path[kPathCap];
    char reason[64];
};

struct QuarantineList {
    uint64_t fingerprint;
    bool loadedFromCache;
    unsigned count;
    QuarantineEntry entries[kMaxQuarantineEntries];

    void Clear() {
        fingerprint = 0;
        loadedFromCache = false;
        count = 0;
    }

    int FindIndex(const char *path) const {
        if (!path || !*path) return -1;
        for (unsigned i = 0; i < count; ++i) {
            if (EqualsFoldAscii(entries[i].path, path)) return static_cast<int>(i);
        }
        return -1;
    }

    bool IsQuarantined(const char *path) const {
        return FindIndex(path) >= 0;
    }

    const char *ReasonFor(const char *path) const {
        const int idx = FindIndex(path);
        return idx >= 0 ? entries[idx].reason : NULL;
    }

    bool Add(const char *path, const char *reason) {
        if (!path || !*path) return false;
        if (FindIndex(path) >= 0) return true;
        if (count >= kMaxQuarantineEntries) return false;
        QuarantineEntry &e = entries[count++];
        std::snprintf(e.path, sizeof(e.path), "%s", path);
        std::snprintf(e.reason, sizeof(e.reason), "%s", (reason && *reason) ? reason : "quarantined");
        return true;
    }
};

// Parse cache text (archive-scan-v1.txt) into QuarantineList; returns false if version
// mismatches or fingerprint != expectedFingerprint (when expectedFingerprint != 0).
inline bool ParseQuarantineCacheText(const char *text, size_t len,
                                     uint64_t expectedFingerprint, QuarantineList *out) {
    if (!text || !out) return false;
    out->Clear();
    bool sawVersion = false;
    bool sawFingerprint = false;
    uint64_t fileFp = 0;

    size_t pos = 0;
    while (pos < len) {
        size_t eol = pos;
        while (eol < len && text[eol] != '\n' && text[eol] != '\r') ++eol;
        const size_t lineLen = eol - pos;
        if (lineLen > 0u && text[pos] != '#') {
            char line[512];
            const size_t copy = lineLen < sizeof(line) - 1u ? lineLen : sizeof(line) - 1u;
            std::memcpy(line, text + pos, copy);
            line[copy] = '\0';

            if (std::strncmp(line, "version=", 8) == 0) {
                if (std::strcmp(line + 8, "1") != 0) return false;
                sawVersion = true;
            } else if (std::strncmp(line, "fingerprint=", 12) == 0) {
                char *endp = NULL;
                const unsigned long long parsed = std::strtoull(line + 12, &endp, 16);
                if (!endp || *endp != '\0') return false;
                fileFp = static_cast<uint64_t>(parsed);
                sawFingerprint = true;
            } else if (std::strncmp(line, "QUARANTINE\t", 11) == 0) {
                const char *pathStart = line + 11;
                const char *tab = std::strchr(pathStart, '\t');
                char pathBuf[kPathCap] = {};
                char reasonBuf[64] = {};
                if (tab) {
                    const size_t pl = static_cast<size_t>(tab - pathStart);
                    const size_t pc = pl < sizeof(pathBuf) - 1u ? pl : sizeof(pathBuf) - 1u;
                    std::memcpy(pathBuf, pathStart, pc);
                    pathBuf[pc] = '\0';
                    const char *r = tab + 1;
                    if (std::strncmp(r, "reason=", 7) == 0) r += 7;
                    const size_t rl = std::strlen(r);
                    const size_t rc = rl < sizeof(reasonBuf) - 1u ? rl : sizeof(reasonBuf) - 1u;
                    std::memcpy(reasonBuf, r, rc);
                    reasonBuf[rc] = '\0';
                } else {
                    const size_t pl = std::strlen(pathStart);
                    const size_t pc = pl < sizeof(pathBuf) - 1u ? pl : sizeof(pathBuf) - 1u;
                    std::memcpy(pathBuf, pathStart, pc);
                    pathBuf[pc] = '\0';
                    std::memcpy(reasonBuf, "quarantined", 12u);
                }
                out->Add(pathBuf, reasonBuf);
            }
        }
        pos = eol;
        while (pos < len && (text[pos] == '\n' || text[pos] == '\r')) ++pos;
    }

    if (!sawVersion || !sawFingerprint) {
        out->Clear();
        return false;
    }
    if (expectedFingerprint != 0 && fileFp != expectedFingerprint) {
        out->Clear();
        return false;
    }
    out->fingerprint = fileFp;
    out->loadedFromCache = true;
    return true;
}

struct ScanSummary {
    unsigned total;
    unsigned ok;
    unsigned quarantined;
    unsigned texResolved;
    unsigned texTotal;
    size_t maxArenaBytes;
    uint64_t fingerprint;
};

// Runtime scanner & OS file helpers implemented in ce_archive_scanner.cpp.
bool ReadWholeFileIntoArena(const char *fullPath, SwapArena *arena,
                            unsigned char **outData, size_t *outSize, size_t maxBytes);
bool ScanGameDir(const char *gameDir, const char *cacheOutPath,
                 ScanSummary *outSummary, char *errBuf, size_t errCap);
bool LoadQuarantineCache(const char *cachePath, uint64_t expectedFingerprint,
                         QuarantineList *outList);

} // namespace CeArchiveScanner

// Pre-WinMain CLI handler for `bea --archive-scan [--game-dir DIR] [--out CACHE_FILE]`.
extern "C" int PortCeArchiveScanCli(int argc, char **argv);

#endif
