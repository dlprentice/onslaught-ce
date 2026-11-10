// SPDX-License-Identifier: GPL-3.0-only
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include "common.h"
#include "DX.h"
#include "landscape.h"
#include "LandscapeVB.h"
#include "ce_terrain_reserve.h"
#include "../platform/common/terrain_quality.h"
#include "../platform/common/portlog.h"

// Only this file uses ordinary placement construction in its owned allocation.
// The game's tagged-new macro must not reinterpret a placement address.
#ifdef new
#undef new
#endif

namespace {
// Each extra pool is a multiple of the original tier pool, within one small explicit budget.
constexpr std::size_t reserveMultiplier = 3;
constexpr std::size_t maximumOwnedBytes = 8u * 1024u * 1024u;
constexpr unsigned short unusedSlot = (std::numeric_limits<unsigned short>::max)();

// The original pool as it stood when the level initialised. Only plain numbers are kept:
// the preparation that follows never reads the game's pool objects again.
struct Shape {
    const CDXPatchList *legacy;
    std::size_t legacyCount, payload;
    int cells;
};
struct ReservePool {
    const CDXPatchList *legacy = nullptr;
    CLandscapeVB *buffers = nullptr;
    std::size_t count = 0, constructed = 0, free = 0, cursor = 0;
};
ReservePool pools[3];
Shape shapes[3];
bool shaped = false;     // the original pools were usable and the reserve fits its budget
bool attempted = false;  // a preparation attempt has run and not been renewed by a new request
bool prepared = false;   // every reserved buffer was created
unsigned prepareAttempts = 0;  // attempts of this initialisation, for the diagnostic fault

void Destroy(ReservePool &pool)
{
    while (pool.constructed) pool.buffers[--pool.constructed].~CLandscapeVB();
    std::free(pool.buffers);
    pool = ReservePool{};
}

void DestroyPools()
{
    for (ReservePool &pool : pools) Destroy(pool);
}

ReservePool *Owner(const void *patch)
{
    if (!patch) return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(patch);
    for (ReservePool &pool : pools) {
        if (!pool.buffers) continue;
        const auto first = reinterpret_cast<std::uintptr_t>(pool.buffers);
        const auto bytes = pool.count * sizeof(CLandscapeVB);
        if (address >= first && address - first < bytes &&
            (address - first) % sizeof(CLandscapeVB) == 0) return &pool;
    }
    return nullptr;
}

bool CaptureShape(CDXPatchManager *manager)
{
    if (!manager || !manager->mLists) return false;
    // Extended can exhaust any tier, including the nearest one in split screen. A doubled
    // linear range covers up to four times the area: the reserve adds three original
    // pools' worth, subject to the explicit budget below.
    std::size_t ownedBytes = 0;
    Shape captured[3];
    for (int level = 0; level < 3; ++level) {
        const CDXPatchList &legacy = manager->mLists[level];
        if (legacy.mCount <= 0 || !legacy.mPatches) return false;
        const auto *original = reinterpret_cast<const CLandscapeVB *>(legacy.mPatches);
        if (original[0].mCells <= 0 || original[0].mSize <= 0) return false;
        const auto payload = static_cast<std::size_t>(original[0].mSize);
        const auto unitBytes = sizeof(CLandscapeVB) + payload;
        const auto legacyCount = static_cast<std::size_t>(legacy.mCount);
        if (unitBytes > maximumOwnedBytes || legacyCount > maximumOwnedBytes / unitBytes / reserveMultiplier) return false;
        const auto bytes = legacyCount * reserveMultiplier * unitBytes;
        if (ownedBytes > maximumOwnedBytes - bytes) return false;
        ownedBytes += bytes;
        captured[level] = Shape{&legacy, legacyCount, payload, original[0].mCells};
    }
    for (int level = 0; level < 3; ++level) shapes[level] = captured[level];
    return true;
}

// Diagnostic only, and only with BEA_PORT_TERRAIN_QUALITY_LOG=1 in an admitted CE session:
// BEA_PORT_TERRAIN_RESERVE_FAULT=TIER:N[:ATTEMPTS] (TIER 0, 1 or 2) stops the preparation when the
// pool of that tier is about to create its Nth buffer (0 = before the pool exists), on every
// attempt or, with ATTEMPTS, only on the first ATTEMPTS of an initialisation. It exercises the
// real rollback, fallback and renewed attempt on the actual device without changing any default.
bool ReadFault(int &level, std::size_t &index, unsigned &attempts)
{
    level = -1;
    index = 0;
    attempts = 0;
    if (!CeTerrainQualityDiagnostics()) return false;
    const char *text = std::getenv("BEA_PORT_TERRAIN_RESERVE_FAULT");
    if (!text || (text[0] != '0' && text[0] != '1' && text[0] != '2') || text[1] != ':' || !text[2]) return false;
    char *end = nullptr;
    const unsigned long value = std::strtoul(text + 2, &end, 10);
    if (end == text + 2 || value > 100000ul) return false;
    unsigned long limit = 0;
    if (*end == ':') {
        const char *digits = end + 1;
        limit = std::strtoul(digits, &end, 10);
        if (end == digits || limit < 1 || limit > 1000ul) return false;
    }
    if (*end) return false;
    level = text[0] - '0';
    index = static_cast<std::size_t>(value);
    attempts = static_cast<unsigned>(limit);
    return true;
}

// One attempt per initialisation. It runs at a frame boundary, from the readiness check, and only
// when Extended is requested: a session that stays on Normal never creates a reserved buffer.
void Prepare()
{
    attempted = true;
    const auto started = std::chrono::steady_clock::now();
    std::size_t ownedBytes = 0, vertexBytes = 0, objects = 0;
    bool valid = shaped;
    int faultLevel = -1;
    std::size_t faultIndex = 0;
    unsigned faultAttempts = 0;
    ++prepareAttempts;
    if (valid) ReadFault(faultLevel, faultIndex, faultAttempts);
    if (faultAttempts && prepareAttempts > faultAttempts) faultLevel = -1;
    for (int level = 0; level < 3 && valid; ++level) {
        const Shape &shape = shapes[level];
        const auto count = shape.legacyCount * reserveMultiplier;
        if (faultLevel == level && faultIndex == 0) { valid = false; break; }
        ReservePool &pool = pools[level];
        pool.legacy = shape.legacy;
        pool.count = count;
        pool.buffers = static_cast<CLandscapeVB *>(std::malloc(count * sizeof(CLandscapeVB)));
        if (!pool.buffers) { pool.count = 0; valid = false; break; }
        for (std::size_t i = 0; i < count; ++i) {
            if (faultLevel == level && faultIndex == i) { valid = false; break; }
            CLandscapeVB *buffer = ::new (static_cast<void *>(pool.buffers + i)) CLandscapeVB;
            ++pool.constructed;
            buffer->Create(shape.cells);
            if (!buffer->mVB) { valid = false; break; }
        }
        if (!valid) break;
        pool.free = pool.constructed;
        ownedBytes += count * (sizeof(CLandscapeVB) + shape.payload);
        vertexBytes += count * shape.payload;
        objects += count;
    }
    prepared = valid;
    if (!valid) DestroyPools();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    PortSessionLog(PORT_SESSION_VIDEO, false,
        "terrain-detail reserve ready=%d objects=%llu owned_estimate_bytes=%llu "
        "vertex_payload_bytes=%llu init_us=%lld fault_tier=%d fault_index=%llu attempt=%u "
        "(driver allocation overhead is not measured)",
        prepared ? 1 : 0, static_cast<unsigned long long>(prepared ? objects : 0),
        static_cast<unsigned long long>(prepared ? ownedBytes : 0),
        static_cast<unsigned long long>(prepared ? vertexBytes : 0),
        static_cast<long long>(elapsed), faultLevel, static_cast<unsigned long long>(faultIndex), prepareAttempts);
}

// The readiness the frame policy samples at a frame boundary, while Extended is requested.
bool Usable()
{
    // A refusal holds until the player asks for Extended again (the engine initialises the pools once
    // per process, so a transient failure must not disable Extended for the whole session). A new
    // request earns one more attempt; the same request, frame after frame, never does.
    if (attempted && !prepared && CeTerrainQualityFreshRequest()) attempted = false;
    if (!attempted) Prepare();
    if (!prepared) return false;
    for (const ReservePool &pool : pools)
        for (std::size_t i = 0; i < pool.count; ++i)
            if (!pool.buffers[i].mVB) return false;
    return true;
}
}

void CeTerrainReserveShutdown()
{
    prepared = attempted = shaped = false;
    prepareAttempts = 0;
    for (Shape &shape : shapes) shape = Shape{};
    CeTerrainQualityResourceCheck(nullptr);
    DestroyPools();
}

void CeTerrainReserveInit(CDXPatchManager *manager)
{
    CeTerrainReserveShutdown();
    if (!CeTerrainQualityResourcesAllowed()) return;
    // Nothing is created here. The check is registered even when the original pools are unusable,
    // so a level that cannot host the reserve reports a refusal rather than a missing initialisation.
    shaped = CaptureShape(manager);
    CeTerrainQualityResourceCheck(Usable);
}

void CeTerrainReserveReset(const CDXPatchList *list)
{
    for (ReservePool &pool : pools) {
        if (pool.legacy != list) continue;
        for (std::size_t i = 0; i < pool.constructed; ++i) pool.buffers[i].mSlot = unusedSlot;
        pool.free = pool.constructed;
        pool.cursor = 0;
    }
}

CLandscapeVB *CeTerrainReserveAllocate(int level, unsigned short slot)
{
    if (!CeTerrainQualityExtended() || !prepared || level < 0 || level >= 3) return nullptr;
    ReservePool &pool = pools[level];
    if (!pool.free || !pool.count) {
        CeTerrainQualityReserveUse(level, false);
        return nullptr;
    }
    std::size_t observedFree = 0;
    for (std::size_t tried = 0; tried < pool.count; ++tried) {
        const auto index = pool.cursor;
        pool.cursor = (pool.cursor + 1) % pool.count;
        CLandscapeVB &buffer = pool.buffers[index];
        if (buffer.mSlot != unusedSlot) continue;
        ++observedFree;
        if (!buffer.mVB) continue;
        buffer.mSlot = slot;
        --pool.free;
        CeTerrainQualityReserveUse(level, true);
        return &buffer;
    }
    // A missing device buffer or an unobserved owner change is a refusal.
    // The native allocator's existing coarse-terrain fallback remains valid.
    pool.free = observedFree;
    CeTerrainQualityReserveUse(level, false);
    return nullptr;
}

bool CeTerrainReserveOwns(const void *patch)
{
    return Owner(patch) != nullptr;
}

void CeTerrainReserveReleased(const void *patch)
{
    ReservePool *pool = Owner(patch);
    if (!pool) return;
    const auto *buffer = static_cast<const CLandscapeVB *>(patch);
    if (buffer->mSlot != unusedSlot && pool->free < pool->count) ++pool->free;
}
