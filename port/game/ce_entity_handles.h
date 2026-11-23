// SPDX-License-Identifier: GPL-3.0-only
// 32-bit monotonic generation handle table for CE entity identity.
//
// Retail CThing::sThingCount (src/thing.h) is a signed 16-bit short that wraps
// negative after 32,767 spawns (~15-30 min of persistent combat), breaking any
// caller that rejects negative numbers or aliases wrapped 16-bit IDs.
//
// This module maintains a pure host-memory side table mapping live engine
// pointers (CThing*) <-> non-zero 32-bit CeEntityHandle values with zero src/
// struct growth, zero game-heap allocations, and zero faithful-replay impact.
//
// Bit layout (32-bit unsigned, bit 31 always 0 so every valid handle is also
// strictly positive when carried through signed 32-bit int API boundaries):
//   bits  0..13 (14 bits): slot index in [1 .. 16383]  (0 = invalid)
//   bits 14..30 (17 bits): generation in [1 .. 131071] (0 = invalid, wraps 131071 -> 1)
//   bit  31      (1 bit):  always 0

#ifndef CE_ENTITY_HANDLES_H
#define CE_ENTITY_HANDLES_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t CeEntityHandle;

static constexpr CeEntityHandle kCeEntityHandleInvalid = 0u;
static constexpr uint32_t kCeEntitySlotBits = 14u;
static constexpr uint32_t kCeEntityGenBits = 17u;
static constexpr uint32_t kCeEntitySlotMask = (1u << kCeEntitySlotBits) - 1u;  // 16383
static constexpr uint32_t kCeEntityGenMask = (1u << kCeEntityGenBits) - 1u;    // 131071
static constexpr uint32_t kCeEntityHandleMaxSlots = kCeEntitySlotMask;         // 16383

inline CeEntityHandle CeEntityHandlePack(uint32_t slot, uint32_t generation) {
    if (slot == 0u || slot > kCeEntitySlotMask) return kCeEntityHandleInvalid;
    if (generation == 0u || generation > kCeEntityGenMask) return kCeEntityHandleInvalid;
    return (generation << kCeEntitySlotBits) | slot;
}

inline uint32_t CeEntityHandleSlot(CeEntityHandle handle) {
    return handle & kCeEntitySlotMask;
}

inline uint32_t CeEntityHandleGeneration(CeEntityHandle handle) {
    return (handle >> kCeEntitySlotBits) & kCeEntityGenMask;
}

inline bool CeEntityHandleIsValid(CeEntityHandle handle) {
    return (handle & 0x80000000u) == 0u &&
           CeEntityHandleSlot(handle) != 0u &&
           CeEntityHandleGeneration(handle) != 0u;
}

// Fixed-capacity, zero-allocation O(1) bidirectional handle table.
// Freed slots enter a FIFO queue and advance their 17-bit generation on reuse,
// preventing ABA aliasing across tens of thousands of spawn/destroy cycles.
template <uint32_t MaxSlots, uint32_t HashBuckets>
class CeEntityHandleTableBase {
    static_assert(MaxSlots >= 1u && MaxSlots <= kCeEntityHandleMaxSlots,
                  "MaxSlots must fit in 14-bit non-zero slot range");
    static_assert(HashBuckets >= 2u && (HashBuckets & (HashBuckets - 1u)) == 0u,
                  "HashBuckets must be a power of two");

    struct Slot {
        uintptr_t ptr;
        uint64_t stamp;
        uint32_t gen;
        uint16_t hashNext;
        uint16_t freeNext;
        uint8_t live;
    };

    Slot slots_[MaxSlots + 1u];
    uint16_t buckets_[HashBuckets];
    uint32_t highWater_;
    uint32_t liveCount_;
    uint32_t worldEpoch_;
    uint64_t totalSpawns_;
    uint64_t worldSpawns_;
    uint16_t freeHead_;
    uint16_t freeTail_;

    static uint32_t HashPointer(uintptr_t ptr) {
        uint64_t x = static_cast<uint64_t>(ptr);
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdull;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ull;
        x ^= x >> 33;
        return static_cast<uint32_t>(x) & (HashBuckets - 1u);
    }

    static uint32_t NextGeneration(uint32_t current) {
        uint32_t next = (current + 1u) & kCeEntityGenMask;
        return next == 0u ? 1u : next;
    }

    uint16_t FindSlotByPtr(uintptr_t ptr) const {
        if (ptr == 0u) return 0u;
        const uint32_t bucket = HashPointer(ptr);
        uint16_t idx = buckets_[bucket];
        while (idx != 0u) {
            if (slots_[idx].live && slots_[idx].ptr == ptr) return idx;
            idx = slots_[idx].hashNext;
        }
        return 0u;
    }

    void HashInsert(uint16_t idx, uintptr_t ptr) {
        const uint32_t bucket = HashPointer(ptr);
        slots_[idx].hashNext = buckets_[bucket];
        buckets_[bucket] = idx;
    }

    void HashRemove(uint16_t idx, uintptr_t ptr) {
        const uint32_t bucket = HashPointer(ptr);
        uint16_t cur = buckets_[bucket];
        uint16_t prev = 0u;
        while (cur != 0u) {
            if (cur == idx) {
                if (prev != 0u) slots_[prev].hashNext = slots_[cur].hashNext;
                else buckets_[bucket] = slots_[cur].hashNext;
                slots_[cur].hashNext = 0u;
                return;
            }
            prev = cur;
            cur = slots_[cur].hashNext;
        }
    }

public:
    CeEntityHandleTableBase() { ClearAll(); }

    CeEntityHandleTableBase(const CeEntityHandleTableBase&) = delete;
    CeEntityHandleTableBase& operator=(const CeEntityHandleTableBase&) = delete;

    void ClearAll() {
        memset(slots_, 0, sizeof(slots_));
        memset(buckets_, 0, sizeof(buckets_));
        highWater_ = 0u;
        liveCount_ = 0u;
        worldEpoch_ = 1u;
        totalSpawns_ = 0u;
        worldSpawns_ = 0u;
        freeHead_ = 0u;
        freeTail_ = 0u;
    }

    // Registers a live entity pointer (or returns its existing handle if already
    // live with the same identity stamp). If `ptr` is already live with a different
    // stamp (untracked deallocation + heap address reuse), the stale slot is retired
    // and a strictly new handle/generation is issued.
    CeEntityHandle Register(uintptr_t ptr, uint64_t stamp = 0u) {
        if (ptr == 0u) return kCeEntityHandleInvalid;
        const uint16_t existing = FindSlotByPtr(ptr);
        if (existing != 0u) {
            if (slots_[existing].stamp == stamp) {
                return CeEntityHandlePack(existing, slots_[existing].gen);
            }
            Unregister(ptr);
        }

        uint16_t idx = 0u;
        if (highWater_ < MaxSlots) {
            idx = static_cast<uint16_t>(++highWater_);
        } else if (freeHead_ != 0u) {
            idx = freeHead_;
            freeHead_ = slots_[idx].freeNext;
            if (freeHead_ == 0u) freeTail_ = 0u;
            slots_[idx].freeNext = 0u;
        } else {
            return kCeEntityHandleInvalid;
        }

        Slot& s = slots_[idx];
        s.ptr = ptr;
        s.stamp = stamp;
        s.gen = NextGeneration(s.gen);
        s.live = 1u;
        s.hashNext = 0u;
        s.freeNext = 0u;
        HashInsert(idx, ptr);
        ++liveCount_;
        ++totalSpawns_;
        ++worldSpawns_;
        return CeEntityHandlePack(idx, s.gen);
    }

    // Unconditionally retires any stale slot for `ptr` before registering a fresh
    // generation handle, guaranteeing strict ABA safety on spawn when the engine
    // heap reuses a previously seen address.
    CeEntityHandle RegisterFresh(uintptr_t ptr, uint64_t stamp = 0u) {
        if (ptr == 0u) return kCeEntityHandleInvalid;
        Unregister(ptr);
        return Register(ptr, stamp);
    }

    // Removes a pointer from the live table. Safe to call repeatedly or for
    // unknown pointers.
    bool Unregister(uintptr_t ptr) {
        const uint16_t idx = FindSlotByPtr(ptr);
        if (idx == 0u) return false;
        HashRemove(idx, ptr);
        Slot& s = slots_[idx];
        s.live = 0u;
        s.ptr = 0u;
        s.stamp = 0u;
        s.freeNext = 0u;
        if (freeTail_ != 0u) slots_[freeTail_].freeNext = idx;
        else freeHead_ = idx;
        freeTail_ = idx;
        if (liveCount_ > 0u) --liveCount_;
        return true;
    }

    // Removes an entry only if `handle` still matches the slot's live generation.
    // Safe for retiring non-unit handles (e.g. expired CRound shells) without any
    // risk of unregistering a newer entity that has already reused the same address.
    bool UnregisterHandle(CeEntityHandle handle) {
        if (!CeEntityHandleIsValid(handle)) return false;
        const uint32_t idx = CeEntityHandleSlot(handle);
        if (idx == 0u || idx > MaxSlots) return false;
        Slot& s = slots_[idx];
        if (!s.live || s.gen != CeEntityHandleGeneration(handle)) return false;
        const uintptr_t ptr = s.ptr;
        HashRemove(static_cast<uint16_t>(idx), ptr);
        s.live = 0u;
        s.ptr = 0u;
        s.stamp = 0u;
        s.freeNext = 0u;
        if (freeTail_ != 0u) slots_[freeTail_].freeNext = static_cast<uint16_t>(idx);
        else freeHead_ = static_cast<uint16_t>(idx);
        freeTail_ = static_cast<uint16_t>(idx);
        if (liveCount_ > 0u) --liveCount_;
        return true;
    }

    // O(1) lookup without registering.
    CeEntityHandle Lookup(uintptr_t ptr) const {
        const uint16_t idx = FindSlotByPtr(ptr);
        if (idx == 0u) return kCeEntityHandleInvalid;
        return CeEntityHandlePack(idx, slots_[idx].gen);
    }

    // O(1) identity stamp read for a currently live handle (0 if stale/invalid).
    uint64_t StampOf(CeEntityHandle handle) const {
        if (!CeEntityHandleIsValid(handle)) return 0u;
        const uint32_t idx = CeEntityHandleSlot(handle);
        if (idx == 0u || idx > MaxSlots) return 0u;
        const Slot& s = slots_[idx];
        if (!s.live || s.gen != CeEntityHandleGeneration(handle)) return 0u;
        return s.stamp;
    }

    // O(1) handle -> pointer resolution. Returns 0 if stale, invalid, or freed.
    uintptr_t Resolve(CeEntityHandle handle) const {
        if (!CeEntityHandleIsValid(handle)) return 0u;
        const uint32_t idx = CeEntityHandleSlot(handle);
        if (idx == 0u || idx > MaxSlots) return 0u;
        const Slot& s = slots_[idx];
        if (!s.live || s.gen != CeEntityHandleGeneration(handle)) return 0u;
        return s.ptr;
    }

    bool Matches(uintptr_t ptr, CeEntityHandle handle) const {
        return ptr != 0u && handle != kCeEntityHandleInvalid && Resolve(handle) == ptr;
    }

    // Invalidates all live handles on level teardown while advancing generations
    // on every previously touched slot so no stale handle from a prior level can
    // ever resolve in the next level.
    void ResetWorld() {
        for (uint32_t i = 1u; i <= highWater_; ++i) {
            Slot& s = slots_[i];
            if (s.live) {
                s.gen = NextGeneration(s.gen);
            }
            s.live = 0u;
            s.ptr = 0u;
            s.stamp = 0u;
            s.hashNext = 0u;
            s.freeNext = 0u;
        }
        memset(buckets_, 0, sizeof(buckets_));
        highWater_ = 0u;
        liveCount_ = 0u;
        worldSpawns_ = 0u;
        freeHead_ = 0u;
        freeTail_ = 0u;
        if (++worldEpoch_ == 0u) worldEpoch_ = 1u;
    }

    uint32_t LiveCount() const { return liveCount_; }
    uint64_t TotalSpawns() const { return totalSpawns_; }
    uint64_t WorldSpawns() const { return worldSpawns_; }
    uint32_t WorldEpoch() const { return worldEpoch_; }
    uint32_t Capacity() const { return MaxSlots; }
};

using CeEntityHandleTable = CeEntityHandleTableBase<kCeEntityHandleMaxSlots, 32768u>;

// Engine-integrated handle API (implemented in port/game/ce_entity_handles.cpp).
class CThing;
class CUnit;
class CSquad;
class CInitThing;

CeEntityHandle CeEntityHandleFor(const CThing* thing);
CeEntityHandle CeEntityHandleLookup(const CThing* thing);
CThing* CeEntityHandleResolve(CeEntityHandle handle);
CUnit* CeEntityHandleResolveUnit(CeEntityHandle handle);
CSquad* CeEntityHandleResolveSquad(CeEntityHandle handle);
bool CeEntityHandleMatches(const CThing* thing, CeEntityHandle handle);
CeEntityHandle CeEntityHandleRegister(CThing* thing);
void CeEntityHandleUnregister(const CThing* thing);
void CeEntityHandleUnregisterHandle(CeEntityHandle handle);
void CeEntityHandleResetWorld();
uint32_t CeEntityHandleLiveCount();
uint64_t CeEntityHandleTotalSpawns();
uint32_t CeEntityHandleWorldEpoch();

extern "C" void PortCeEntityHandlesUnitSpawn(CUnit* unit, CInitThing* init);
extern "C" void PortCeEntityHandlesUnitDestroyed(CUnit* unit);
extern "C" void PortCeEntityHandlesLevelEnd(int level, int quit);

#endif
