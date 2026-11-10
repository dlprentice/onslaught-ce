// SPDX-License-Identifier: GPL-3.0-only
// Actual lockstep controller; authored world, transport status and clock only.
// Reuse the terminal fixture's boundary stubs, not its scenarios or entry point.
#define CE_NETWORK_ICE 1
#define main ce_terminal_reference_main
#include RESILIENCE_HARNESS_INC
#undef main

static const char *relay_problem = "";
// These cases start after admission with a scripted transport, without a lobby
// directory or launch secret. Do not silently turn an unexpected setup call
// into evidence about the real signaling route.
extern "C" int CE_NetSignalingDirectorySelected(void) { return 0; }
extern "C" int CE_LobbySignalingGameSecret(uint32_t, uint32_t, uint8_t[32]) {
    std::fprintf(stderr, "resilience fixture unexpectedly requested a lobby secret\n");
    std::exit(3);
}
extern "C" CE_NetSession *CE_NetCreateGame(const CE_NetConfig *config) {
    return CE_NetCreate(config);
}
extern "C" CE_NetSession *CE_NetCreateLobby(const CE_NetConfig *config) {
    return CE_NetCreate(config);
}
extern "C" const char *CE_NetGameProblem(const CE_NetSession *) {
    return relay_problem;
}

static void running(bool host) {
    init(host);
    last_error[0] = connect_why[0] = 0;
    fake_boot_lockstep = 1;
    relay_problem = "";
    L.tick = 12;
    L.quit_at = 0;
    L.hash_every = 20;
    L.ran = L.sampled = false;
    L.connection_established = true;
    L.crt_calls = 17; L.crt_hold = 19;
    L.rng_value = 23; L.rng_seed = 29;
    fake_calls = 31; fake_hold = 37;
    fake_rng_value = 41; fake_rng_seed = 43;
    GAME.slot_words[0] = 1;
    fake_career_word = 47;
}

static bool world_untouched() {
    return fake_world_value == 0x12345678 && GAME.slot_words[0] == 1 &&
           fake_career_word == 47 && fake_prerun_calls == 0;
}

static void suspended(uint64_t at) {
    now_ms = at;
    check(CE_LockstepBeforeUpdate() == 0, "missing controls keep the update gate closed");
    CE_LockstepAfterUpdate(); // A caller cannot complete a tick that was not admitted.
    CE_LockstepBeforeRender();
    check(L.tick == 12 && !L.ran && sent.empty() && applied_count == 0 &&
          fake_calls == 31 && fake_hold == 37 && fake_rng_value == 41 && fake_rng_seed == 43 &&
          world_untouched(), "waiting preserves tick, random streams and fabricated world state");
}

static void notice_and_recovery(bool host) {
    running(host);
    suspended(100);
    suspended(599);
    check(!CE_LockstepNotice()[0], "waiting notice stays hidden before half a second");
    suspended(600);
    check(!std::strcmp(CE_LockstepNotice(), "Waiting for the other player... (30 s)"),
          "waiting notice appears at half a second");
    suspended(30099);
    check(!std::strcmp(CE_LockstepNotice(), "Waiting for the other player... (1 s)"),
          "waiting countdown preserves its original deadline");

    // An authored empty control sample exercises the decoder and input ring.
    // No device or game input is read or injected.
    CE_WorldInput sample{};
    input(12, sample);
    check(CE_LockstepBeforeUpdate() == 1 && L.tick == 12 && L.ran &&
          L.waiting_since == 0 && !CE_LockstepNotice()[0] && !quit_code &&
          fake_calls == 17 && fake_hold == 19 && fake_rng_value == 23 && fake_rng_seed == 29,
          "controls before failure resume the same tick and restore simulation random state");
    CE_LockstepAfterUpdate();
    CE_LockstepAfterUpdate();
    check(L.stage == RUNNING && L.tick == 13 && sent.size() == 1 &&
          sent[0].type == MSG_INPUT && sent[0].tick == 15 && world_untouched(),
          "recovery completes one tick and one delayed control sample");
    now_ms = 30100;
    check(CE_LockstepBeforeUpdate() == 0 && L.waiting_since == 30100 && !quit_code &&
          !CE_LockstepNotice()[0], "a subsequent missing tick gets a new wait only after recovery");
}

static void terminal_wait(bool host) {
    running(host);
    suspended(100);
    suspended(30099);
    now_ms = 30100;
    check(CE_LockstepBeforeUpdate() == 0 && L.stage == FAILED && L.tick == 12 &&
          quit_code == QT_QUIT_TO_FRONTEND && L.outcome_error && !L.final_confirmed &&
          !std::strcmp(CE_LockstepError(), "the other player stopped sending controls") &&
          destroys == 1 && closes == 0 && !L.net && world_untouched(),
          "missing controls at thirty seconds abort without a voluntary leave or result");
    // Clear the shell quit flag too: the terminal stage itself must fence updates.
    const unsigned created = fake_created;
    mock_status.phase = CE_NET_ACTIVE;
    mock_status.error = CE_NET_OK;
    CE_WorldInput sample{};
    input(12, sample);
    quit_code = 0;
    now_ms = 60100;
    check(CE_LockstepBeforeUpdate() == 0, "late controls cannot reopen a failed update gate");
    CE_LockstepAfterUpdate();
    CE_LockstepBeforeRender();
    check(L.stage == FAILED && L.tick == 12 && !L.net && !L.final_confirmed &&
          destroys == 1 && fake_created == created && sent.empty() && inbound.size() == 1 &&
          CE_LockstepExitCode(0, 1) != 0 && world_untouched(),
          "network restoration after failure cannot recreate or advance the old session");
}

static void transport_failure(bool host, const char *problem, const char *expected) {
    running(host);
    suspended(100);
    relay_problem = problem;
    mock_status.phase = CE_NET_FAILED;
    mock_status.error = CE_NET_TIMEOUT;
    now_ms = 200;
    check(CE_LockstepBeforeUpdate() == 0 && L.stage == FAILED && L.tick == 12 &&
          !std::strcmp(CE_LockstepError(), expected) && !CE_LockstepNotice()[0] &&
          quit_code == QT_QUIT_TO_FRONTEND && destroys == 1 && closes == 0 && !L.net &&
          !L.final_confirmed && world_untouched(),
          problem[0] ? "relay expiry reason survives to the frontend before another tick" :
                       "direct timeout has its own failure reason before another tick");
    relay_problem = "";
    CE_LockstepAfterWorldShutdown();
    check(!std::strcmp(CE_LockstepError(), expected) && CE_LockstepExitCode(0, 1) != 0,
          "first failure reason and unsuccessful outcome survive world shutdown");
}

int main() {
    for (bool host : {true, false}) {
        notice_and_recovery(host);
        terminal_wait(host);
        transport_failure(host, "", "the other player stopped responding");
        const char *expiry = "relay credentials expired; begin a fresh network session";
        transport_failure(host, expiry, expiry);
    }
    std::printf("RESILIENCE_COMPLETE failures=%d\n", failures);
    return failures ? 1 : 0;
}
