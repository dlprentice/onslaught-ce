// The runner inserts the real lockstep scope functions and their state.
// All random-state accessors below are local fakes; no game or network runs.
struct Snapshot {
    unsigned calls, hold;
    int value, seed;
};

static Snapshot active_state;
static int accessor_calls;

static void PortCrtRandState(unsigned int *calls, unsigned int *hold) {
    ++accessor_calls;
    *calls = active_state.calls;
    *hold = active_state.hold;
}
static void PortCrtRandSet(unsigned int calls, unsigned int hold) {
    ++accessor_calls;
    active_state.calls = calls;
    active_state.hold = hold;
}
static void CE_WorldLockstepRandom(int save, int *value, int *seed) {
    ++accessor_calls;
    if (save) { *value = active_state.value; *seed = active_state.seed; }
    else { active_state.value = *value; active_state.seed = *seed; }
}

/* INSERT_SCOPE_DEFINITIONS */

static bool same(const Snapshot &a, const Snapshot &b) {
    return a.calls == b.calls && a.hold == b.hold &&
           a.value == b.value && a.seed == b.seed;
}

int main() {
    const Snapshot simulation = {101, 202, 303, 404};
    const Snapshot advanced_auxiliary = {707, 808, 909, 1001};
    unsigned outer[4] = {}, inner[4] = {}, next[4] = {};
    active_state = simulation;

    CE_LockstepCrtEnter(outer);
    const Snapshot initial_auxiliary = active_state;
    if (same(initial_auxiliary, simulation)) return 1;
    const int after_outer_entry = accessor_calls;
    CE_LockstepCrtEnter(inner);
    if (!same(active_state, initial_auxiliary) || accessor_calls != after_outer_entry) return 2;

    active_state = advanced_auxiliary;
    CE_LockstepCrtLeave(inner);
    if (!same(active_state, advanced_auxiliary) || accessor_calls != after_outer_entry) return 3;
    CE_LockstepCrtLeave(outer);
    if (!same(active_state, simulation)) return 4;

    CE_LockstepCrtEnter(next);
    if (!same(active_state, advanced_auxiliary)) return 5;
    CE_LockstepCrtLeave(next);
    if (!same(active_state, simulation)) return 6;
    return 0;
}
