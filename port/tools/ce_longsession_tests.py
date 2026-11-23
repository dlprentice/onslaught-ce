#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Long-session foundation & gameplay glue test suite (ASan/UBSan + causal mutants).

Covers:
  1. 32-bit monotonic CE entity generation handles (ce_entity_handles.h):
     >50,000 spawns wrap-around immunity (negative short mThingNumber), ABA/pointer reuse,
     world reset invalidation, generation wrap, and causal mutants.
  2. Commander kMaxEntities = 1024 + graceful prioritization (ce_command_panel.h,
     ce_command_selection.h): >256 and >1024 entity picking/ordering without lockout,
     strict selected -> player faction -> nearest ordering, hidden count, and mutants.
  3. Read-only long-session telemetry & inert Sandbox Launch Profile gate
     (ce_sandbox_telemetry.h): faithful/replay/network/demo/campaign fences, opt-in log
     gating, snapshot line formatting (including negative sThingCount), and mutants.
  4. Play Any Unit 6-second mode hint auto-collapse (re-open on camera change or F1)
     and infantry primary weapon preference (rifle over thrown plasma grenade), with mutants.
  5. Source-level invariants across ce_cmdproduction.cpp, ce_cmdmap.cpp, ce_playunit.cpp,
     and ce_sandbox_telemetry.cpp.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port" / "game"
SCRATCH = Path(tempfile.gettempdir())


def compile_and_run(work: Path, name: str, driver_src: str, extra_sources=(), include_dirs=()):
    cpp = work / f"{name}.cpp"
    exe = work / name
    cpp.write_text(driver_src, encoding="utf-8")
    cxx = shutil.which("g++") or "c++"
    cmd = [
        cxx, "-std=c++17", "-O1", "-g",
        "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-parameter",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
    ]
    for inc in include_dirs:
        cmd.extend(["-I", str(inc)])
    cmd.append(str(cpp))
    for src in extra_sources:
        cmd.append(str(src))
    cmd.extend(["-o", str(exe)])
    built = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if built.returncode != 0:
        return built.returncode, "COMPILE_FAIL:\n" + built.stdout[-1500:] + built.stderr[-1500:]
    ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
    return ran.returncode, ran.stdout + ran.stderr


HANDLES_DRIVER = r"""
#include "ce_entity_handles.h"
#include <cstdio>
#include <cstdint>
#include <vector>

static int gFails = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++gFails;
    }
}

int main() {
    static CeEntityHandleTable table;
    table.ClearAll();

    // 1. Null / invalid handle rejection.
    Check(table.Register(0u) == kCeEntityHandleInvalid, "null_register_rejected");
    Check(table.Resolve(kCeEntityHandleInvalid) == 0u, "zero_handle_rejected");
    Check(!CeEntityHandleIsValid(kCeEntityHandleInvalid), "zero_handle_invalid");
    Check(!CeEntityHandleIsValid(0x80000001u), "sign_bit_handle_invalid");

    // 2. 50,000+ spawns across retail signed 16-bit sThingCount wrap-around (-32768..-1).
    constexpr int kPool = 256;
    uintptr_t addrs[kPool];
    CeEntityHandle active[kPool];
    for (int i = 0; i < kPool; ++i) {
        addrs[i] = 0x100000u + static_cast<uintptr_t>(i) * 64u;
        active[i] = kCeEntityHandleInvalid;
    }

    std::vector<CeEntityHandle> retiredSamples;
    int16_t sThingCount = 0;
    int negativeSpawnsObserved = 0;

    for (int cycle = 0; cycle < 50000; ++cycle) {
        const int slot = cycle % kPool;
        if (active[slot] != kCeEntityHandleInvalid) {
            const CeEntityHandle oldH = active[slot];
            Check(table.Resolve(oldH) == addrs[slot], "live_handle_resolves");
            Check(table.Matches(addrs[slot], oldH), "live_handle_matches");
            Check(table.Unregister(addrs[slot]), "unregister_succeeds");
            Check(table.Resolve(oldH) == 0u, "freed_handle_invalid");
            Check(!table.Matches(addrs[slot], oldH), "freed_handle_mismatch");
            if (cycle % 97 == 0) retiredSamples.push_back(oldH);
        }

        const int16_t num = sThingCount++;
        if (num < 0) ++negativeSpawnsObserved;

        const CeEntityHandle h = table.Register(addrs[slot]);
        Check(h != kCeEntityHandleInvalid, "spawn_handle_nonzero");
        Check(static_cast<int32_t>(h) > 0, "spawn_handle_positive_int32");
        Check(table.Register(addrs[slot]) == h, "register_idempotent");
        Check(table.Lookup(addrs[slot]) == h, "lookup_matches");
        Check(table.Resolve(h) == addrs[slot], "resolve_matches");
        if (active[slot] != kCeEntityHandleInvalid) {
            Check(h != active[slot], "aba_pointer_reuse_gets_new_handle");
            Check(table.Resolve(active[slot]) == 0u, "old_aba_handle_dead");
        }
        active[slot] = h;
    }

    Check(negativeSpawnsObserved > 15000, "observed_negative_16bit_wrap");
    Check(table.TotalSpawns() == 50000ull, "total_spawns_exact");
    Check(table.LiveCount() == kPool, "live_count_exact");

    // Ensure none of the sampled retired handles resolve even after 50,000 spawns.
    for (CeEntityHandle stale : retiredSamples) {
        Check(table.Resolve(stale) == 0u, "historical_stale_handle_never_aliases");
    }

    // 3. World reset must invalidate every handle and preserve/advance generations
    // so a new world never re-issues a handle from the prior world (even slot 1, gen 1).
    CeEntityHandleTableBase<8u, 16u> worldTbl;
    const CeEntityHandle w1First = worldTbl.Register(0x9000u);
    worldTbl.ResetWorld();
    Check(worldTbl.LiveCount() == 0u, "reset_world_clears_live");
    Check(worldTbl.Resolve(w1First) == 0u, "reset_world_invalidates_prior");
    const CeEntityHandle w2First = worldTbl.Register(0x9000u);
    Check(w2First != kCeEntityHandleInvalid, "reregister_after_reset_valid");
    Check(w2First != w1First, "reset_world_preserves_and_bumps_generation");
    Check(worldTbl.Resolve(w1First) == 0u, "prior_world_handle_stays_dead_after_reuse");

    // 4. Small table capacity & generation wrap (131071 -> 1, never 0).
    CeEntityHandleTableBase<2u, 4u> tiny;
    const CeEntityHandle a = tiny.Register(0x10u, 100u);
    const CeEntityHandle b = tiny.Register(0x20u, 200u);
    Check(a != 0u && b != 0u, "tiny_fills_to_capacity");
    Check(tiny.Register(0x30u, 300u) == kCeEntityHandleInvalid, "tiny_rejects_over_capacity");
    // 5. ABA protection: RegisterFresh at a STILL-LIVE address (e.g. dying-unit re-query or
    //    untracked squad/non-unit free + heap reuse) MUST invalidate the old handle and issue a
    //    strictly new handle/generation even when the table is at 100% capacity!
    const CeEntityHandle aFresh = tiny.RegisterFresh(0x10u, 100u);
    Check(aFresh != kCeEntityHandleInvalid, "register_fresh_at_capacity_succeeds");
    Check(aFresh != a, "register_fresh_advances_handle_on_live_addr");
    Check(tiny.Resolve(a) == 0u, "register_fresh_invalidates_old_handle");
    Check(!tiny.Matches(0x10u, a), "register_fresh_old_handle_no_longer_matches");
    Check(tiny.Resolve(aFresh) == 0x10u, "register_fresh_new_handle_resolves");
    Check(tiny.Lookup(0x10u) == aFresh, "register_fresh_lookup_returns_new");
    Check(tiny.LiveCount() == 2u, "register_fresh_preserves_live_count");

    // 6. Stamp-mismatch ABA protection: untracked deallocation + heap reuse of a squad/non-unit
    //    at the same address with a new (thingType, mThingNumber) stamp MUST retire the old handle.
    const CeEntityHandle bNewStamp = tiny.Register(0x20u, 201u);
    Check(bNewStamp != kCeEntityHandleInvalid && bNewStamp != b, "stamp_change_issues_new_handle");
    Check(tiny.Resolve(b) == 0u, "stamp_change_invalidates_old_handle");
    Check(tiny.Resolve(bNewStamp) == 0x20u && tiny.StampOf(bNewStamp) == 201u, "stamp_change_stores_new_stamp");

    // Non-full table RegisterFresh (simulating dying-unit re-query then respawn at same address).
    CeEntityHandleTableBase<8u, 16u> respawnTbl;
    const CeEntityHandle dyingStale = respawnTbl.Register(0xBEEFu, 0x0001002Au);
    const CeEntityHandle respawned = respawnTbl.RegisterFresh(0xBEEFu, 0x0001002Au);
    Check(respawned != kCeEntityHandleInvalid && respawned != dyingStale, "respawn_same_addr_same_stamp_new_handle");
    Check(respawnTbl.Resolve(dyingStale) == 0u, "dying_stale_handle_invalidated_on_respawn");
    Check(respawnTbl.Resolve(respawned) == 0xBEEFu, "respawned_handle_resolves");
    Check(respawnTbl.LiveCount() == 1u, "respawn_live_count_stays_one");

    // 7. Generation-checked handle retirement (UnregisterHandle): safe cleanup for non-unit
    //    objects (such as expired CRound shells) that frees the slot when still owned, yet
    //    NEVER unregisters a newer entity that already reused the same address or slot.
    Check(!tiny.UnregisterHandle(a), "unregister_handle_rejects_stale_gen_on_reused_slot");
    Check(tiny.Resolve(aFresh) == 0x10u && tiny.LiveCount() == 2u,
          "unregister_handle_stale_on_reused_slot_leaves_live_entity_intact");
    Check(!respawnTbl.UnregisterHandle(dyingStale), "unregister_handle_rejects_stale_gen");
    Check(respawnTbl.Resolve(respawned) == 0xBEEFu && respawnTbl.LiveCount() == 1u,
          "unregister_handle_stale_leaves_new_entity_intact");
    Check(respawnTbl.UnregisterHandle(respawned), "unregister_handle_retires_live_match");
    Check(respawnTbl.Resolve(respawned) == 0u && respawnTbl.LiveCount() == 0u,
          "unregister_handle_frees_slot");

    tiny.Unregister(0x10u);
    const CeEntityHandle c = tiny.Register(0x30u, 300u);
    Check(c != 0u && c != aFresh, "tiny_reuses_freed_slot_with_new_gen");
    Check(tiny.Resolve(aFresh) == 0u && tiny.Resolve(c) == 0x30u, "tiny_distinguishes_reused_slot");

    if (gFails == 0) std::puts("handles_ok");
    return gFails == 0 ? 0 : 1;
}
"""


COMMANDER_OVERFLOW_DRIVER = r"""
#include "ce_command_panel.h"
#include "ce_command_selection.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cp = ce_command_panel;
namespace cs = ce_command_selection;

static int gFails = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++gFails;
    }
}

static cs::UnitSnapshot MakeUnit(uint64_t world, uint64_t id, bool friendly, float sx, float sy) {
    cs::UnitSnapshot u{};
    u.key = {world, id, 1u};
    u.facts.kind = cp::EntityKind::Unit;
    u.facts.relation = friendly ? cp::TeamRelation::Friendly : cp::TeamRelation::Unknown;
    u.facts.control = cp::ControlOwner::CommandAI;
    u.facts.availability = cp::Availability::Available;
    u.facts.selectable = friendly;
    u.facts.capabilities = cp::ActionBit(cp::Action::Focus) | cp::ActionBit(cp::Action::Move) | cp::ActionBit(cp::Action::Hold);
    u.facts.squad.present = true;
    u.facts.squad.squad_id = 10u;
    u.facts.squad.squad_generation = 1u;
    u.facts.squad.has_member_count = true;
    u.facts.squad.member_count = 4u;
    std::snprintf(u.facts.squad.name, sizeof(u.facts.squad.name), "Squad 1");
    std::snprintf(u.facts.name, sizeof(u.facts.name), "Unit %llu", static_cast<unsigned long long>(id));
    u.screen = {sx, sy, 12.f, true};
    return u;
}

int main() {
    // 1. kMaxEntities raised from 256 to 1024.
    static_assert(cp::kMaxEntities == 1024, "kMaxEntities must be 1024");
    static_assert(cs::kMaxUnits == 1024, "kMaxUnits must be 1024");

    // 2. 400 entities (>256 legacy hard cliff): full picking & order submission work cleanly!
    static cs::SelectionController ctrl;
    static cs::UnitSnapshot units400[400];
    for (int i = 0; i < 400; ++i) {
        units400[i] = MakeUnit(1u, static_cast<uint64_t>(i + 1), true,
                               40.f + float(i % 20) * 30.f,
                               40.f + float(i / 20) * 30.f);
    }
    cs::Frame frame400{};
    frame400.world = 1u;
    frame400.snapshot_generation = 1u;
    frame400.context_generation = 1u;
    frame400.targeted_actions = cp::ActionBit(cp::Action::Move);
    frame400.complete = true;
    frame400.units = units400;
    frame400.unit_count = 400;
    const cs::FrameResult res400 = ctrl.BeginFrame(frame400);
    Check(res400.status == cs::FrameStatus::Valid, "frame_400_valid");
    Check(ctrl.FrameUsable(), "frame_400_usable");

    // Click entity #350 (well past legacy 256 limit) and issue Hold order.
    const float px = units400[350].screen.x;
    const float py = units400[350].screen.y;
    cs::ClickModifiers mods{};
    ctrl.PointerDown(px, py);
    const cs::GestureResult up = ctrl.PointerUp(px, py, mods);
    Check(up.outcome == cs::GestureOutcome::Click, "pick_unit_350_succeeds");
    Check(ctrl.SelectionCount() == 1, "unit_350_selected");
    Check(ctrl.IsSelected(units400[350].key), "unit_350_key_matches");
    const cs::ArmStatus arm = ctrl.ArmAction(cp::Action::Hold);
    Check(arm == cs::ArmStatus::Queued, "hold_order_queued_over_256");
    cs::Request req{};
    Check(ctrl.TakeRequest(&req) && req.key_count == 1u && cs::SameKey(req.keys[0], units400[350].key),
          "hold_request_matches_unit_350");

    // 3. >1024 entities (1500 candidates): PrioritizeUnits keeps selected -> player faction -> nearest.
    constexpr int kTotal = 1500;
    static cs::PrioritizeCandidate cands[kTotal];
    static int keptIdx[cs::kMaxUnits];
    for (int i = 0; i < kTotal; ++i) {
        cands[i].source_index = i;
        cands[i].selected = (i >= 1490);                    // 10 selected units (farthest indices!)
        cands[i].player_faction = (i >= 800 && i < 1490);   // 690 friendly units
        cands[i].dist_sq = float(i * i);                    // enemies 0..799 are CLOSEST!
    }
    // Give selected units huge distance to prove selection ALWAYS beats proximity.
    for (int i = 1490; i < kTotal; ++i) cands[i].dist_sq = 1.0e9f + float(i);

    int hidden = -1;
    const int kept = cs::PrioritizeUnits(cands, kTotal, cs::kMaxUnits, keptIdx, &hidden);
    Check(kept == cs::kMaxUnits, "kept_equals_1024");
    Check(hidden == kTotal - cs::kMaxUnits, "hidden_equals_476");

    // First 10 kept MUST be the 10 selected units (1490..1499).
    for (int i = 0; i < 10; ++i) {
        Check(cands[keptIdx[i]].selected, "tier1_selected_first");
    }
    // Next 690 kept MUST be all player-faction units (800..1489), ordered nearest first.
    for (int i = 10; i < 700; ++i) {
        Check(!cands[keptIdx[i]].selected && cands[keptIdx[i]].player_faction, "tier2_player_faction_second");
        if (i > 10) Check(cands[keptIdx[i - 1]].dist_sq <= cands[keptIdx[i]].dist_sq, "tier2_sorted_nearest");
    }
    // Remaining 324 kept MUST be the 324 nearest enemy units (0..323).
    for (int i = 700; i < cs::kMaxUnits; ++i) {
        Check(!cands[keptIdx[i]].selected && !cands[keptIdx[i]].player_faction, "tier3_enemy_third");
        Check(keptIdx[i] == i - 700, "tier3_nearest_enemies_exact");
    }

    // Feed the prioritized 1024-unit slice into SelectionController: picking & orders still work!
    static cs::UnitSnapshot units1024[cs::kMaxUnits];
    for (int i = 0; i < cs::kMaxUnits; ++i) {
        const int src = keptIdx[i];
        units1024[i] = MakeUnit(2u, static_cast<uint64_t>(src + 1),
                                cands[src].selected || cands[src].player_faction,
                                20.f + float(i % 32) * 20.f,
                                20.f + float(i / 32) * 20.f);
    }
    cs::Frame frame1024{};
    frame1024.world = 2u;
    frame1024.snapshot_generation = 2u;
    frame1024.context_generation = 1u;
    frame1024.targeted_actions = cp::ActionBit(cp::Action::Move);
    frame1024.complete = true;
    frame1024.units = units1024;
    frame1024.unit_count = cs::kMaxUnits;
    const cs::FrameResult res1024 = ctrl.BeginFrame(frame1024);
    Check(res1024.status == cs::FrameStatus::Valid, "frame_1024_valid");
    Check(ctrl.FrameUsable(), "frame_1024_usable");
    ctrl.PointerDown(units1024[0].screen.x, units1024[0].screen.y);
    const cs::GestureResult pick0 = ctrl.PointerUp(units1024[0].screen.x, units1024[0].screen.y, mods);
    Check(pick0.outcome == cs::GestureOutcome::Click && ctrl.IsSelected(units1024[0].key),
          "prioritized_1024_picking_works");

    if (gFails == 0) std::puts("commander_overflow_ok");
    return gFails == 0 ? 0 : 1;
}
"""


TELEMETRY_DRIVER = r"""
#include "ce_sandbox_telemetry.h"
#include <cstdio>
#include <cstring>

static int gFails = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++gFails;
    }
}

int main() {
    CeSandboxProfileGate clean{};
    clean.sandboxOptIn = true;
    clean.ceLayer = true;
    clean.recording = false;
    clean.fencedEnv = false;
    clean.networkOrLockstep = false;
    clean.demo = false;
    clean.replayOrProbe = false;
    clean.retailCampaign = false;
    Check(CeSandboxProfileAllowed(clean), "clean_sandbox_allowed");

    // Default / opt-out must be FALSE.
    { CeSandboxProfileGate g = clean; g.sandboxOptIn = false; Check(!CeSandboxProfileAllowed(g), "default_optin_false"); }
    // Every faithful / replay / network / demo / campaign fence MUST force FALSE.
    { CeSandboxProfileGate g = clean; g.ceLayer = false; Check(!CeSandboxProfileAllowed(g), "ce_layer_fence"); }
    { CeSandboxProfileGate g = clean; g.recording = true; Check(!CeSandboxProfileAllowed(g), "recording_fence"); }
    { CeSandboxProfileGate g = clean; g.fencedEnv = true; Check(!CeSandboxProfileAllowed(g), "env_fence"); }
    { CeSandboxProfileGate g = clean; g.networkOrLockstep = true; Check(!CeSandboxProfileAllowed(g), "network_fence"); }
    { CeSandboxProfileGate g = clean; g.demo = true; Check(!CeSandboxProfileAllowed(g), "demo_fence"); }
    { CeSandboxProfileGate g = clean; g.replayOrProbe = true; Check(!CeSandboxProfileAllowed(g), "replay_probe_fence"); }
    { CeSandboxProfileGate g = clean; g.retailCampaign = true; Check(!CeSandboxProfileAllowed(g), "retail_campaign_fence"); }

    // Diagnostic log emission must be strictly inert when disabled or when ceLayer is off.
    Check(!CeSandboxTelemetryShouldEmit(false, true, 0u, 600u), "diag_off_silent");
    Check(!CeSandboxTelemetryShouldEmit(true, false, 0u, 600u), "ce_layer_off_silent");
    Check(!CeSandboxTelemetryShouldEmit(true, true, 100u, 0u), "zero_interval_silent");
    Check(!CeSandboxTelemetryShouldEmit(true, true, 599u, 600u), "non_multiple_tick_silent");
    Check(CeSandboxTelemetryShouldEmit(true, true, 600u, 600u), "interval_tick_emits");

    // Telemetry snapshot formatting (including wrapped negative sThingCount).
    CeSandboxTelemetrySnapshot snap{};
    snap.thingHeapFreeBytes = 4194304u;
    snap.thingHeapLargestFreeBlock = 2097152u;
    snap.defaultHeapFreeBytes = 8388608u;
    snap.defaultHeapPeakBytes = 12582912u;
    snap.sptrSetEstimatedNodes = 24000u;
    snap.sptrSetOverflowNodes = 17u;
    snap.eventManagerPending = 342;
    snap.eventManagerCapacity = 20000;
    snap.sThingCount = -12345;
    snap.activeLiveUnits = 412u;
    snap.commanderMaxEntities = 1024u;
    snap.entityHandleLiveCount = 420u;
    snap.entityHandleTotalSpawns = 54321ull;
    snap.sandboxProfileActive = false;
    snap.valid = true;

    char buf[320] = {};
    const int n = CeSandboxTelemetryFormatLine(snap, buf, sizeof(buf));
    Check(n > 0, "format_nonzero");
    Check(std::strstr(buf, "thing_free=4194304") != nullptr, "fmt_thing_free");
    Check(std::strstr(buf, "thing_maxblk=2097152") != nullptr, "fmt_thing_maxblk");
    Check(std::strstr(buf, "def_peak=12582912") != nullptr, "fmt_def_peak");
    Check(std::strstr(buf, "sptr_overflow=17") != nullptr, "fmt_sptr_overflow");
    Check(std::strstr(buf, "events=342/20000") != nullptr, "fmt_events");
    Check(std::strstr(buf, "sthing=-12345") != nullptr, "fmt_negative_sthing");
    Check(std::strstr(buf, "units=412/1024") != nullptr, "fmt_units_1024");
    Check(std::strstr(buf, "spawns=54321") != nullptr, "fmt_spawns");
    Check(std::strstr(buf, "sandbox=0") != nullptr, "fmt_sandbox_inert");

    if (gFails == 0) std::puts("telemetry_ok");
    return gFails == 0 ? 0 : 1;
}
"""


PLAYUNIT_GLUE_DRIVER = r"""
#include "ce_playunit_rules.h"
#include <cstdio>

static int gFails = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++gFails;
    }
}

int main() {
    // 1. Mode hint capsule auto-collapse after 6 seconds (120 ticks at 20 Hz).
    CePlayUnitHintState hint{};
    CePlayUnitHintOpen(&hint);
    Check(hint.visible && hint.ticksLeft == kCePlayUnitHintCollapseTicks, "hint_opens_for_6s");
    for (unsigned t = 1u; t < kCePlayUnitHintCollapseTicks; ++t) {
        const bool changed = CePlayUnitHintStep(&hint, false, false);
        Check(!changed && hint.visible, "hint_stays_visible_before_6s");
    }
    Check(CePlayUnitHintStep(&hint, false, false) && !hint.visible, "hint_collapses_at_6s");
    Check(!CePlayUnitHintStep(&hint, false, false) && !hint.visible, "hint_stays_collapsed");

    // Re-open on camera mode change.
    Check(CePlayUnitHintStep(&hint, true, false) && hint.visible && hint.ticksLeft == kCePlayUnitHintCollapseTicks,
          "hint_reopens_on_view_change");
    for (unsigned t = 0u; t < kCePlayUnitHintCollapseTicks; ++t) CePlayUnitHintStep(&hint, false, false);
    Check(!hint.visible, "hint_collapses_again");

    // Re-open on F1 help press.
    Check(CePlayUnitHintStep(&hint, false, true) && hint.visible && hint.ticksLeft == kCePlayUnitHintCollapseTicks,
          "hint_reopens_on_f1");

    // 2. Infantry primary weapon selection (rifle beats thrown plasma grenade).
    {
        const bool hasMode[2] = {true, true};
        const bool ready[2] = {true, true};
        const char* names[2] = {"Plasma Grenade Thrown", "Pulse Rifle"};
        Check(CePlayUnitIsSecondaryThrownWeapon(names[0]), "grenade_detected");
        Check(!CePlayUnitIsSecondaryThrownWeapon(names[1]), "rifle_not_grenade");
        Check(CePlayUnitSelectInfantryPrimaryWeapon(2, 0, false, hasMode, ready, names) == 1,
              "rifle_beats_current_grenade");
    }
    // Even while rifle is recovering and grenade is ready, keep the primary rifle synced.
    {
        const bool hasMode[2] = {true, true};
        const bool ready[2] = {true, false};
        const char* names[2] = {"Plasma Grenade Thrown", "Forseti Assault Rifle"};
        Check(CePlayUnitSelectInfantryPrimaryWeapon(2, 0, false, hasMode, ready, names) == 1,
              "recovering_rifle_still_beats_thrown_grenade");
    }
    // Soldier with ONLY grenades falls back cleanly to grenade.
    {
        const bool hasMode[1] = {true};
        const bool ready[1] = {true};
        const char* names[1] = {"Plasma Grenade Thrown"};
        Check(CePlayUnitSelectInfantryPrimaryWeapon(1, 0, false, hasMode, ready, names) == 0,
              "grenade_only_fallback_works");
    }

    if (gFails == 0) std::puts("playunit_glue_ok");
    return gFails == 0 ? 0 : 1;
}
"""


class LongSessionTests(unittest.TestCase):
    def run_mutated_header(self, header_name: str, driver_src: str, expected_ok: str,
                           mutants: dict, extra_sources=()):
        orig = (GAME / header_name).read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory(prefix="ce-longsession-", dir=SCRATCH) as td:
            work = Path(td)
            rc, out = compile_and_run(work, "clean", driver_src, extra_sources=extra_sources, include_dirs=[GAME])
            self.assertEqual(rc, 0, f"clean run failed ({header_name}):\n{out}")
            self.assertIn(expected_ok, out)

            for mname, (old, new) in mutants.items():
                with self.subTest(mutant=mname):
                    self.assertEqual(orig.count(old), 1, f"mutant anchor not unique in {header_name}: {mname}")
                    mdir = work / f"m_{mname}"
                    mdir.mkdir()
                    (mdir / header_name).write_text(orig.replace(old, new, 1), encoding="utf-8")
                    mrc, mout = compile_and_run(mdir, mname, driver_src, extra_sources=extra_sources,
                                                include_dirs=[mdir, GAME])
                    self.assertNotIn("COMPILE_FAIL:", mout, f"mutant {mname} failed to compile:\n{mout}")
                    self.assertNotEqual(mrc, 0, f"mutant {mname} survived!\n{mout}")

    def test_entity_handles_50000_spawns_and_mutants(self):
        self.run_mutated_header(
            "ce_entity_handles.h",
            HANDLES_DRIVER,
            "handles_ok",
            mutants={
                "no_gen_bump": (
                    "uint32_t next = (current + 1u) & kCeEntityGenMask;",
                    "uint32_t next = 1u;",
                ),
                "reset_zeroes_generations": (
                    "if (s.live) {\n                s.gen = NextGeneration(s.gen);\n            }",
                    "s.gen = 0u;",
                ),
                "skip_gen_check_on_resolve": (
                    "if (!s.live || s.gen != CeEntityHandleGeneration(handle)) return 0u;\n        return s.ptr;",
                    "if (!s.live) return 0u;\n        return s.ptr;",
                ),
                "unregister_handle_ignores_gen": (
                    "if (!s.live || s.gen != CeEntityHandleGeneration(handle)) return false;",
                    "if (!s.live) return false;",
                ),
                "register_fresh_skips_unregister": (
                    "Unregister(ptr);\n        return Register(ptr, stamp);",
                    "return Register(ptr, stamp);",
                ),
                "register_ignores_stamp_change": (
                    "if (slots_[existing].stamp == stamp) {\n                return CeEntityHandlePack(existing, slots_[existing].gen);\n            }\n            Unregister(ptr);",
                    "return CeEntityHandlePack(existing, slots_[existing].gen);",
                ),
            },
        )

    def test_commander_1024_entities_prioritization_and_mutants(self):
        extra = [GAME / "ce_command_panel.cpp", GAME / "ce_command_selection.cpp"]
        self.run_mutated_header(
            "ce_command_selection.h",
            COMMANDER_OVERFLOW_DRIVER,
            "commander_overflow_ok",
            extra_sources=extra,
            mutants={
                "drop_selected_priority": (
                    "if (a.selected != b.selected) return a.selected;",
                    "/* dropped selected tier */",
                ),
                "drop_faction_priority": (
                    "if (a.player_faction != b.player_faction) return a.player_faction;",
                    "/* dropped player_faction tier */",
                ),
                "wrong_hidden_count": (
                    "if (out_hidden) *out_hidden = count > heap_size ? (count - heap_size) : 0;",
                    "if (out_hidden) *out_hidden = 0;",
                ),
            },
        )

    def test_telemetry_and_inert_sandbox_profile_gate_and_mutants(self):
        self.run_mutated_header(
            "ce_sandbox_telemetry.h",
            TELEMETRY_DRIVER,
            "telemetry_ok",
            mutants={
                "ignore_optin": ("return g.sandboxOptIn &&", "return true &&"),
                "ignore_recording_fence": ("!g.recording &&", "true &&"),
                "ignore_network_fence": ("!g.networkOrLockstep &&", "true &&"),
                "ignore_campaign_fence": ("!g.retailCampaign;", "true;"),
                "emit_without_ce_layer": (
                    "if (!diagnosticEnabled || !ceLayer || intervalTicks == 0u) return false;",
                    "if (!diagnosticEnabled || intervalTicks == 0u) return false;",
                ),
            },
        )

    def test_playunit_hint_collapse_and_infantry_weapon_sync_and_mutants(self):
        self.run_mutated_header(
            "ce_playunit_rules.h",
            PLAYUNIT_GLUE_DRIVER,
            "playunit_glue_ok",
            mutants={
                "hint_never_collapses": ("s->ticksLeft = 0u;\n\ts->visible = false;", "s->ticksLeft = 0u;\n\ts->visible = true;"),
                "hint_ignores_f1": ("if (viewChanged || helpPressed)", "if (viewChanged)"),
                "grenade_not_filtered": (
                    'return CePlayUnitAsciiContainsNoCase(name, "grenade") ||\n'
                    '\t       CePlayUnitAsciiContainsNoCase(name, "thrown") ||\n'
                    '\t       CePlayUnitAsciiContainsNoCase(name, "satchel");',
                    "return false;",
                ),
            },
        )

    def test_source_level_invariants(self):
        prod = (GAME / "ce_cmdproduction.cpp").read_text(encoding="utf-8")
        self.assertNotIn("thingNumber < 0", prod, "negative wrapped mThingNumber rejection must be removed")
        self.assertIn("CeEntityHandleFor(", prod)
        self.assertIn("CeEntityHandleResolveUnit(", prod)
        is_veh = prod[prod.index("bool CeFactoryIsVehicle("):prod.index("CUnit *CeFactoryLiveVehicle(")]
        self.assertIn("job.productHandle", is_veh, "CeFactoryIsVehicle must prefer 32-bit job.productHandle")
        self.assertIn("CeEntityHandleMatches(", is_veh)

        play = (GAME / "ce_playunit.cpp").read_text(encoding="utf-8")
        overlay_start = play.index("bool CePlayUnitHudOverlay(")
        overlay_end = play.index("extern \"C\" void PortCePlayUnitMouse(", overlay_start)
        overlay_body = play[overlay_start:overlay_end]
        self.assertNotIn("HUD.mCompass->Render", overlay_body, "300px BE compass ring must be suppressed on non-BE units")
        self.assertIn("SelectInfantryPrimaryWeapon(", play)
        self.assertIn("CePlayUnitHintStep(", play)
        self.assertIn("CePlayUnitPrompt(NULL)", play)
        self.assertIn("const float s = 0.48f;", play)
        self.assertGreaterEqual(play.count("CeEntityHandleUnregisterHandle(gSpotShellHandle)"), 3,
                                "gSpotShellHandle must be unregistered on expire, Release, and Reset")
        self.assertIn("CeEntityHandleUnregisterHandle(gProbeRounds[i].handle)", play,
                      "ProbeAim rounds must unregister their handles when expired")

        cmd = (GAME / "ce_cmdmap.cpp").read_text(encoding="utf-8")
        self.assertIn("FormatSquadCardShortName(", cmd)
        self.assertIn("StrategicRectClearsHud(", cmd)
        self.assertIn("UnitControl::PrioritizeUnits(", cmd)
        self.assertIn("+%d hidden", cmd)
        self.assertIn("Commander   Bases %d to %d", cmd)

        tel = (GAME / "ce_sandbox_telemetry.cpp").read_text(encoding="utf-8")
        self.assertNotIn("BEA_SANDBOX", tel, "no unallowlisted BEA_* env vars in public binaries")
        self.assertNotIn('extern "C" bool PortCeNetActive', tel, "PortCeNetActive must use C++ linkage matching ce_netpages.cpp")
        self.assertIn("bool PortCeNetActive() __attribute__((weak));", tel)
        self.assertIn("CE_LockstepSharing", tel)

        eh = (GAME / "ce_entity_handles.cpp").read_text(encoding="utf-8")
        spawn_fn = eh[eh.index("PortCeEntityHandlesUnitSpawn("):]
        self.assertIn("RegisterFresh(", spawn_fn, "unit spawn hook must unconditionally retire stale slot via RegisterFresh")
        for_fn = eh[eh.index("CeEntityHandleFor("):eh.index("CeEntityHandleLookup(")]
        self.assertIn("IsThingDying(", for_fn, "CeEntityHandleFor must refuse to lazily re-register dying entities")
        self.assertIn("StampOf(", for_fn, "CeEntityHandleFor must detect recycled untracked addresses via ThingStamp")
        self.assertIn("RegisterFresh(", for_fn, "CeEntityHandleFor must retire stale slot on stamp mismatch")
        resolve_fn = eh[eh.index("CeEntityHandleResolve("):eh.index("CeEntityHandleResolveUnit(")]
        self.assertIn("IsThingDying(", resolve_fn)
        self.assertIn("StampOf(", resolve_fn)


if __name__ == "__main__":
    unittest.main(verbosity=2)
