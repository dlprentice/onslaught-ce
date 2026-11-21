#ifndef CE_COMMAND_LIFETIME_H
#define CE_COMMAND_LIFETIME_H
#include <stddef.h>
#include <stdint.h>

// UI snapshots retain this value, never an address. The native adapter resolves
// it again against both this table and the current world before dispatch.
struct CeCommandLifetimeKey {
    uint64_t world = 0;
    uint64_t id = 0;
    uint64_t generation = 0;
};
struct CeCommandLifetimeRef {
    uintptr_t address = 0;
    uint64_t id = 0;
    uint32_t kind = 0;
    CeCommandLifetimeKey key{};
};

template<size_t Capacity> class CeCommandLifetimeTable {
    static_assert(Capacity > 0, "lifetime table needs storage");
    struct Entry {
        bool live = false;
        CeCommandLifetimeRef ref{};
    };
    uint64_t world_;
    uint64_t next_;
    Entry entries_[Capacity]{};
public:
    // Constant initialization and a trivial destructor keep late destruction
    // notifications independent of UI construction/destruction order.
    constexpr explicit CeCommandLifetimeTable(uint64_t world = 1,
        uint64_t firstGeneration = 1) : world_(world), next_(firstGeneration) {}
    CeCommandLifetimeTable(const CeCommandLifetimeTable&) = delete;
    CeCommandLifetimeTable& operator=(const CeCommandLifetimeTable&) = delete;
    uint64_t World() const { return world_; }
    size_t Size() const {
        size_t size = 0;
        for (const Entry& e : entries_) if (e.live) ++size;
        return size;
    }
    bool Observe(uintptr_t address, uint64_t id, uint32_t kind,
        CeCommandLifetimeKey* output) {
        if (output) *output = {};
        if (!address || !world_) return false;
        Entry* available = nullptr;
        for (Entry& e : entries_) {
            if (e.live && e.ref.address == address) {
                if (e.ref.id == id && e.ref.kind == kind) {
                    if (output) *output = e.ref.key;
                    return true;
                }
                // Defensive identity change even if a caller missed removal.
                e.live = false;
            }
            if (!e.live && !available) available = &e;
        }
        if (!available || !next_) return false;
        available->ref.address = address;
        available->ref.id = id;
        available->ref.kind = kind;
        available->ref.key = {world_, id, next_};
        available->live = true;
        ++next_; // Zero means exhausted; it is never issued as a generation.
        if (output) *output = available->ref.key;
        return true;
    }
    bool Resolve(const CeCommandLifetimeKey& key, CeCommandLifetimeRef* output) const {
        if (output) *output = {};
        if (!world_ || key.world != world_ || !key.generation) return false;
        for (const Entry& e : entries_) {
            if (e.live && e.ref.id == key.id && e.ref.key.generation == key.generation) {
                if (output) *output = e.ref;
                return true;
            }
        }
        return false;
    }
    // No dereference, engine/UI call, allocation or callback occurs here. It is
    // safe to repeat a notification or receive it after world teardown cleared us.
    bool Forget(uintptr_t address) {
        if (!address) return false;
        bool removed = false;
        for (Entry& e : entries_) if (e.live && e.ref.address == address) {
            e.live = false;
            removed = true;
        }
        return removed;
    }
    void EndWorld() {
        for (Entry& e : entries_) e = {};
        if (world_) ++world_; // Exhaustion stays inert instead of reusing epoch 1.
        next_ = 1;
    }
};
#endif
