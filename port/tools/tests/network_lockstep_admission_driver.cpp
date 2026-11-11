// SPDX-License-Identifier: GPL-3.0-only
// Actual retained admission, effective Linux reader and lockstep controller. World, transport and
// boot forwarding are authored seams. This entry point never invokes campaign/return controls.
#include <cstdio>
#include <cstring>
#include <string>
static void AdmissionComplete(const char *mode) {
    std::printf("COMPLETE lockstep-admission %s\n", mode);
}
#ifdef CE_NETWORK_OFF
// Keep the network-disabled reader as a separate compile/run control. Only its entry point is
// wrapped here so the unchanged standalone reader control also has an explicit completion marker.
#define main NetworkDisabledReaderMain
#include "network_disabled_authored_open_driver.cpp"
#undef main
int main(int argc, char **argv) {
    if (argc != 2 || std::strcmp(argv[1], "network-disabled")) return 2;
    int result = NetworkDisabledReaderMain();
    AdmissionComplete("network-disabled");
    return result;
}
#else
#define CE_LOCKSTEP_REAL_ADMISSION 1
#ifndef LOCKSTEP_TERMINAL_DRIVER
#define LOCKSTEP_TERMINAL_DRIVER "network_lockstep_terminal_driver.cpp"
#endif
#include LOCKSTEP_TERMINAL_DRIVER
#include "../../network/tests/fixture_guard.h"
#include "handles.h"
#include <cerrno>
#include <unistd.h>

PortHandle *PortHandleNew(PortHandleKind kind, size_t size) {
    PortHandle *handle = static_cast<PortHandle *>(std::calloc(1, size));
    if (handle) handle->kind = kind; return handle;
}
void PortHandleFree(PortHandle *handle) { std::free(handle); }
PortHandle *PortCheck(HANDLE value, int kind) {
    auto handle = static_cast<PortHandle *>(value);
    return handle && (!kind || handle->kind == kind) ? handle : nullptr;
}
void PortSetLastError(DWORD) {}

int main(int argc, char **argv) {
    if (!FixtureGuard() || argc != 2) return 2;
    const std::string mode = argv[1], game = std::getenv("BEA_GAME_DIR");
    const bool automation = mode == "automation", absent = mode == "absent-voice";
    const char *logical = absent ? "data/dialogue/never-present.ogg" : "data/dialogue/fixture.ogg";
    char why[160] = {}; CE_CompatibilityResult admission = {};
    PortPathsInit();
    check(CE_CompatibilityPrepare(CE_COMPAT_LOCKSTEP_PROTOCOL1, 856, &admission, why, sizeof why), "real retained lockstep admission");
    fake_boot_lockstep = automation ? 0 : 1;
    check(CE_LockstepConfigureLevel(856) && L.admission_required == !automation,
          "real configuration retains genuine admission requirement or explicit automation exemption");

    if(mode=="retired-finished"||mode=="retired-ended"||mode=="retired-cancelled"||
       mode=="retired-failed"||mode=="retired-reconfigure") {
        const auto admitted_config=L.config;
        const auto admitted_level=L.level;
        if(mode=="retired-cancelled"||mode=="retired-failed") {
            init(true); L.config=admitted_config; L.level=admitted_level;
            L.admission_required=true; fake_boot_lockstep=1;
            if(mode=="retired-failed") Fail("authored content failure");
            else { CE_LockstepBeginTeardown(QT_QUIT_TO_FRONTEND); CE_LockstepAfterWorldShutdown(); }
        } else {
            natural_host_ack_and_close();
            check(L.stage==FINISHED&&L.final_confirmed,"genuine final agreement precedes admission retirement");
            L.config=admitted_config; L.level=admitted_level;
            L.admission_required=true; fake_boot_lockstep=1;
            if(mode!="retired-finished") {
                CE_LockstepBeginTeardown(QT_QUIT_TO_FRONTEND); CE_LockstepAfterWorldShutdown();
            }
        }
        const auto update_before_retire=CE_LockstepBeforeUpdate();
        const auto controls_before_retire=CE_LockstepControllerFlush(0);
        CE_CompatibilityRetire();
        const auto retired_stage=L.stage;
        const auto retired_tick=L.tick;
        const auto retired_closes=closes;
        const bool retired_error=L.outcome_error;
        if(mode=="retired-failed") {
            check(!AdmissionCurrent()&&!AdmissionLive()&&!CE_LockstepBeforeUpdate(),
                  "retired failed admission never resumes an update");
            CE_LockstepAfterUpdate();
            check(L.stage==FAILED&&L.outcome_error&&L.tick==retired_tick&&closes==retired_closes,
                  "retired failed admission preserves its failure without another close");
        } else {
            check(AdmissionCurrent()&&AdmissionLive(),"retired terminal admission does not query its destroyed context");
            check(CE_LockstepBeforeUpdate()==update_before_retire&&CE_LockstepControllerFlush(0)==controls_before_retire,
                  "retired terminal admission preserves update and native-control routing");
            CE_LockstepAfterUpdate();
            check(L.stage==retired_stage&&L.outcome_error==retired_error&&L.tick==retired_tick&&closes==retired_closes,
                  "retired terminal admission preserves its existing outcome without another close");
            if(mode=="retired-cancelled")
                check(L.stage==ENDED&&L.outcome_error,"retired cancellation does not become a confirmed success");
            else
                check(L.final_confirmed&&!L.outcome_error&&CE_LockstepExitCode(0,0)==0,
                      "retired confirmed completion preserves its agreed outcome");
        }
        if(mode=="retired-reconfigure") {
            CE_LockstepConfigureLevel(admitted_level);
            check(L.stage==FAILED&&L.outcome_error,
                  "a new admitted session cannot reuse a retired compatibility context");
        }
        AdmissionComplete(mode.c_str());
        return failures?1:0;
    }

    if (mode == "after-load" || mode == "ready-barrier") {
        check(!unlink((game + "/data/dialogue/fixture.ogg").c_str()), "remove only authored admitted dialogue fixture");
        check(!port_fopen(logical, "rb"), "real effective reader reports vanished admitted input");
        if (mode == "after-load") {
            check(!CE_LockstepAfterLoad() && L.stage == FAILED, "lost admission refuses actual after-load boundary");
        } else {
            L.stage = STARTING;
            check(!Block([] { return true; }, kStartMs, "fixture ready barrier") && L.stage == FAILED,
                  "already-ready start barrier cannot skip lost admission");
        }
    } else {
        check(CE_LockstepAfterLoad(), "actual loaded boundary accepts unchanged admission");
        // Only the world/start handshake is omitted. All guards, voice reads, sends, applications
        // and tick completion below execute the actual controller against actual retained state.
        L.stage = RUNNING; L.shown = true; L.net = &mock_net; L.quit_at = 0;
        check(CE_LockstepBeforeUpdate(), "actual admitted update begins while content is valid");
        if (mode == "between-controllers") CE_LockstepControllerFlush(0);
        const int before_applied = applied_count;
        const size_t before_sent = sent.size();
        if (mode != "present-voice" && !absent)
            check(!unlink((game + "/data/dialogue/fixture.ogg").c_str()), "remove only authored admitted dialogue fixture");
        CE_LockstepVoiceFile(logical); // GameOpen is the same effective reader as the production owner.
        if (mode == "present-voice") {
            check(CE_LockstepVoiceSeconds() == 5.f, "authored present voice keeps its parsed timing");
        } else {
            check(CE_LockstepVoiceSeconds() == 0.f, "missing dialogue takes the existing text-timing fallback");
        }
        if (mode == "present-voice" || absent || automation) {
            CE_LockstepAfterUpdate();
            const bool tick_completed = L.stage == RUNNING && L.tick == 1 && !L.outcome_error;
            if (automation) check(tick_completed, "non-admitted automation permits normal tick completion after content loss");
            else if (absent) check(tick_completed, "never-present optional dialogue permits normal admitted tick completion");
            else check(tick_completed, "present dialogue permits normal admitted tick completion");
        } else if (mode == "between-controllers") {
            CE_LockstepControllerFlush(1);
            check(L.stage == FAILED && applied_count == before_applied && sent.size() == before_sent,
                  "content failure between controllers blocks second actual application without a new send");
        } else if (mode == "before-update") {
            check(!CE_LockstepBeforeUpdate() && L.stage == FAILED && L.tick == 0,
                  "lost content stops the next actual update before simulation admission");
        } else if (mode == "after-update") {
            CE_LockstepAfterUpdate();
            check(L.stage == FAILED && L.tick == 0 && sent.size() == before_sent,
                  "real missing dialogue revokes tick completion before input or hash publication");
        } else return 2;
    }
    if (!automation && mode != "present-voice" && !absent) {
        check(L.outcome_error && quit_code == QT_QUIT_TO_FRONTEND && destroys <= 1 && !graceful_leaves,
              "admitted content loss aborts once and reports failure to the front end");
    }
    if(mode=="before-update"||mode=="after-update"||mode=="between-controllers") {
        check(L.stage==FAILED&&L.outcome_error&&!L.net&&destroys==1,
              "active content failure destroys its transport before more simulation");
        const auto stopped_tick=L.tick;
        const auto stopped_closes=closes;
        const auto stopped_destroys=destroys;
        check(!CE_LockstepBeforeUpdate()&&L.stage==FAILED&&L.outcome_error&&L.tick==stopped_tick&&closes==stopped_closes&&destroys==stopped_destroys,
              "active content failure stays sticky without another tick or close");
    }
    AdmissionComplete(argv[1]);
    return failures ? 1 : 0;
}
#endif
