#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the actual session module with controlled transport/world/platform boundaries, without game/device inputs.

Apply the real mirror patches without fuzz to the real caller files. Compile the actual PreRun method body into the
boundary driver, proving host/offline admission and guest exclusion. These are orchestration checks, not actual game
initialization, transport cryptography, world replication or playable multiplayer acceptance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import sys
import tempfile
import deps

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build
import build as port_build    # main() keeps compiler results in a local named build


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, timeout=kwargs.pop("timeout", 30), **kwargs)
    if result.returncode:
        raise RuntimeError(repr(command) + "\n" + result.stdout + result.stderr)
    return result


def body(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[start:end] + "\n"


def compile_callers(source, out, header, world_patch):
    """Build the complete current public mirror and compile actual callers/modules with both pinned targets."""
    folder = out / "native-callers"
    folder.mkdir()
    if sha(world_patch)!=sha(ROOT/"port/patches/0065-network-world.patch"):
        raise ValueError("fully composed native caller build requires the current World patch")
    # Rebuild the actual current PUBLIC mirror, including every admitted patch and public SDK replacement.
    # The caller-supplied existing mirror supplies only its two hash-pinned data includes; no install/archive reads.
    build.PUBLIC=True;build.GAME=source
    build.MIRROR_ROOT=folder/"mirror";build.MIRROR=build.MIRROR_ROOT/"src"
    build.build_mirror()
    mirror=build.MIRROR
    pins={str(p.relative_to(build.MIRROR_ROOT)):sha(p) for p in build.MIRROR_ROOT.rglob("*") if p.is_file()}
    from session_caller_admission import controls
    admission,_=controls(build.network_view((mirror/"game.cpp").read_text(), off=False))
    (folder/"abort-caller-admission.json").write_text(json.dumps(admission,indent=2)+"\n")
    results = []
    for target in ("linux", "windows"):
        settings = build.TARGETS[target]
        includes = [build.PORT / "d3dx/include", build.PORT / "include", *settings["includes"],
                    mirror, mirror / "MissionScript", ROOT / "port/game"]
        for caller in ("game.cpp", "ltshell.cpp"):
            command = [*settings["cxx"], "-std=gnu++14", *build.MSCXX, *build.COMMON, *build.X87,
                       *build.NO_MATH_BUILTINS, '-DCE_VERSION="test"', "-include", str(build.PORT / "include/port.h"),
                       "-include", str(header), *["-I" + str(p) for p in includes], "-fsyntax-only", str(mirror / caller)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=90,
                                    env=dict(os.environ, TMPDIR=str(folder)))
            (folder / (target + "-" + caller + ".log")).write_text(result.stdout + result.stderr)
            results.append({"target": target, "component": caller, "compile": command, "exit": result.returncode})
        for module in ("ce_network_boot.cpp","ce_network_loaded_admission.cpp"):
            command=[*settings["cxx"],"-std=gnu++14",*build.MSCXX,*build.COMMON,*build.X87,
                     *build.NO_MATH_BUILTINS,'-DCE_VERSION="test"',"-include",str(build.PORT/"include/port.h"),
                     *["-I"+str(p) for p in includes],"-fsyntax-only",str(ROOT/"port/game"/module)]
            result=subprocess.run(command,capture_output=True,text=True,timeout=90,env=dict(os.environ,TMPDIR=str(folder)))
            (folder/(target+"-"+module+".log")).write_text(result.stdout+result.stderr)
            results.append({"target":target,"component":module,"compile":command,"exit":result.returncode})
        command = [*settings["cxx"], "-std=gnu++17", "-DNOMINMAX", "-fms-extensions", "-fshort-wchar", "-mms-bitfields",
                   "-fsyntax-only", str(out / "production/game/ce_network_session.cpp")]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30,
                                env=dict(os.environ, TMPDIR=str(folder)))
        (folder / (target + "-session.log")).write_text(result.stdout + result.stderr)
        results.append({"target": target, "component": "session", "compile": command, "exit": result.returncode})
    return {"inputs": pins, "world_patch_sha256": sha(world_patch), "results": results,
            "abort_caller_admission":admission,"public_composed_patches":{p.name:sha(p) for p in sorted((ROOT/"port/patches").glob("*.patch"))}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world", type=Path, default=ROOT / "port/game/ce_network_world.h")
    parser.add_argument("--compiler", default=str(deps.DATA / "llvm/bin/clang++"))
    parser.add_argument("--session-source", type=Path, default=ROOT / "port/game/ce_network_session.cpp",
                        help="explicit frozen implementation for negative controls; default is production")
    parser.add_argument("--asan", action="store_true")
    parser.add_argument("--case", action="append", help="explicit focused cases; skip mutants with --no-mutants")
    parser.add_argument("--no-mutants", action="store_true")
    parser.add_argument("--mutation", action="append", help="run only named causal mutants; unknown names refuse")
    parser.add_argument("--compat-header", type=Path, default=ROOT / "port/network/compatibility.h")
    parser.add_argument("--boot-header", type=Path, default=ROOT / "port/game/ce_network_boot.h")
    parser.add_argument("--loaded-header", type=Path, default=ROOT / "port/game/ce_network_loaded_admission.h")
    modes=parser.add_mutually_exclusive_group()
    modes.add_argument("--lifecycle", action="store_true", help="focused schema5 lifecycle application/prefix controls")
    modes.add_argument("--delivery", action="store_true", help="focused actual cache/delta/repair orchestration controls")
    modes.add_argument("--callers", action="store_true", help="actual named load/script/pre-run abort guards and bridge")
    parser.add_argument("--target-session", action="store_true",
                        help="syntax-check the selected Session with the installed Linux i686 and Windows i686 targets")
    parser.add_argument("--mirror", type=Path, help="existing internal mirror supplies hash-pinned data includes for a fresh full PUBLIC caller/module mirror")
    parser.add_argument("--world-patch", type=Path, default=ROOT / "port/patches/0065-network-world.patch")
    args = parser.parse_args()
    if args.no_mutants and args.mutation:
        parser.error("--mutation cannot be combined with --no-mutants")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/network-session-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    callers = out / "callers"
    callers.mkdir()
    caller_sources = ("game.cpp", "game.h", "ltshell.cpp", "ltshell.h")
    # The mirror's text with every patch's section for the callers applied in order (the patches' contexts are written against the
    # mirror's normalised include names), as a build with the network sees it; patched_text names a patch that is refused.
    for name, text in port_build.patched_text(caller_sources, network=True).items():
        (callers / name).write_text(text, encoding="latin-1")
    patch_pins = {}
    for patch in sorted((ROOT / "port/patches").glob("*.patch")):
        if any(part.startswith("--- a/" + name + "\n") for name in caller_sources
               for part in re.split(r"(?=^--- a/)", patch.read_text(), flags=re.M)):
            patch_pins[patch.name] = sha(patch)
    prerun = out / "prerun.inc"
    caller_text = (callers / "game.cpp").read_text()
    from session_caller_admission import controls
    abort_admission,abort_fragments=controls(caller_text)
    (out/"abort-caller-admission.json").write_text(json.dumps(abort_admission,indent=2)+"\n")
    prerun.write_text(body(caller_text, "void\tCGame::PreRun()"))
    trigger_quit = out / "trigger-quit.inc"
    trigger_quit.write_text(body((callers / "ltshell.cpp").read_text(), "void PCLTShell::TriggerQuit()"))
    bridge = out / "bridge.inc"
    begin = caller_text.index("class CENetworkSession\n{")
    end = caller_text.index("\n};", begin) + 3
    bridge.write_text(caller_text[begin:end] + "\n" + body(caller_text, 'extern "C" int CE_SessionGamePump()'))
    render_entry = out / "render-entry.inc"
    begin = caller_text.index("void CGame::Render( SINT num_renders )")
    end = caller_text.index("\n\t// retail: with the extra GeForce FX", begin)
    render_entry.write_text(caller_text[begin:end] + "\n    (void)num_renders; ++rendered;\n}\n")
    load_admission=out/"load-admission.inc"
    load_admission.write_text("static EQuitType NativeLoadAdmission() {\n"+
        "\n".join(abort_fragments[name] for name in ("after-load","automatic-script","pre-run"))+"\nreturn QT_NONE;\n}\n")
    boot=(ROOT/"port/game/ce_network_boot.cpp").read_text()
    boot_automatic=out/"boot-automatic.inc"
    boot_automatic.write_text(body(boot,'extern "C" int CE_BootAutomaticScript(void)'))
    boot_refuse=out/"boot-refuse.inc"
    boot_refuse.write_text(body(boot,'int Refuse(const char *message)'))
    init_entry = out / "init-entry.inc"
    run_level = body(caller_text, "EQuitType\tCGame::RunLevel(")
    init_end = run_level.index("\n\tmCurrentlyRunningLevel = aLevel")
    # Only the success suffix is a boundary; the full failed-Init prefix, Init and Shutdown are actual source.
    init_entry.write_text(body(caller_text, "BOOL CGame::Init()") + "\n" +
                          body(caller_text, "void CGame::Shutdown()") + "\n" +
                          run_level[:init_end] + "\n    (void)reloadedSounds; (void)pause_for_showing_controls; return QT_NONE; // controlled successful-prefix boundary\n}\n")
    production = out / "production"
    (production / "game").mkdir(parents=True)
    (production / "network").mkdir()
    (production / "platform/common").mkdir(parents=True)
    for name in ("ce_network_session.cpp", "ce_network_session.h", "ce_network_stl.h", "ce_network_campaign.h"):
        shutil.copyfile(args.session_source if name == "ce_network_session.cpp" else ROOT / "port/game" / name,
                        production / "game" / name)
    shutil.copyfile(args.world, production / "game/ce_network_world.h")
    for path, destination in ((args.compat_header, production / "network/compatibility.h"),
                              (args.boot_header, production / "game/ce_network_boot.h"),
                              (args.loaded_header, production / "game/ce_network_loaded_admission.h"),
                              (ROOT / "port/game/ce_network_lockstep.h", production / "game/ce_network_lockstep.h"),
                              (ROOT / "port/platform/common/ce_netcode.h", production / "platform/common/ce_netcode.h")):
        if not path.is_file():
            raise RuntimeError("explicit owning API header required: " + str(path))
        shutil.copyfile(path, destination)
    for name in ("network.h", "ice_game.h", "ice_signaling.h", "codec.cpp", "state_delivery.h", "state_delivery.cpp", "state_delta.h", "state_delta.cpp"):
        shutil.copyfile(ROOT / "port/network" / name, production / "network" / name)
    crypto = deps.SRC / "monocypher-4.0.2/src"
    for name in ("monocypher.h", "monocypher.c"):
        if not (crypto / name).is_file():
            raise RuntimeError("existing pinned Monocypher required; no download/install")
        shutil.copyfile(crypto / name, production / "network" / name)
    shutil.copyfile(ROOT / "port/tools/tests/network_session_delivery_boundary.inc", out / "network_session_delivery_boundary.inc")
    driver = out / "driver.cpp"
    boundary = ROOT / "port/tools/tests/network_session_driver.cpp"
    shutil.copyfile(ROOT / "port/tools/tests" / ("network_session_caller_driver.cpp" if args.callers else
                   "network_session_delivery_driver.cpp" if args.delivery else
                   "network_session_lifecycle_driver.cpp" if args.lifecycle else boundary.name), driver)
    if args.lifecycle or args.delivery or args.callers:
        shutil.copyfile(boundary, out / boundary.name)
    binary = out / "driver"
    command = [args.compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               '-DSESSION_SOURCE="' + str(production / "game/ce_network_session.cpp") + '"',
               '-DPRERUN_SOURCE="' + str(prerun) + '"', '-DBRIDGE_SOURCE="' + str(bridge) + '"',

               '-DTRIGGER_QUIT_SOURCE="' + str(trigger_quit) + '"',
               '-DRENDER_ENTRY_SOURCE="' + str(render_entry) + '"',
               '-DINIT_ENTRY_SOURCE="' + str(init_entry) + '"',
               '-DLOAD_ADMISSION_SOURCE="' + str(load_admission) + '"',
               '-DBOOT_AUTOMATIC_SOURCE="' + str(boot_automatic) + '"',
               '-DBOOT_REFUSE_SOURCE="' + str(boot_refuse) + '"',
               "-I" + str(production / "network"), "-I" + str(production / "game"),
               str(driver), str(production / "network/codec.cpp"), str(production / "network/state_delta.cpp"),
               str(production / "network/state_delivery.cpp"), str(out / "crypto.o"), "-o", str(binary)]
    if args.asan:
        command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    env = dict(os.environ, TMPDIR=str(out), ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:disable_coredump=1")
    for key in tuple(env):
        if key.startswith("BEA_"):
            env.pop(key)
    receipt = {"scope": __doc__, "patches": patch_pins, "compile": command,
               "source_sha256": sha(production / "game/ce_network_session.cpp"),
               "header_sha256": sha(production / "game/ce_network_session.h"),
               "world_header_sha256": sha(production / "game/ce_network_world.h"),
               "driver_sha256": sha(driver), "boundary_sha256": sha(boundary), "prerun_sha256": sha(prerun), "bridge_sha256": sha(bridge),
               "render_entry_sha256": sha(render_entry), "init_entry_sha256": sha(init_entry), "trigger_quit_sha256": sha(trigger_quit), "cases": [],
               "abort_caller_admission":abort_admission,"load_admission_sha256":sha(load_admission),
               "boot_automatic_sha256":sha(boot_automatic),"boot_refuse_sha256":sha(boot_refuse)}
    receipt["delivery_inputs"] = {str(p.relative_to(production)): sha(p) for p in sorted(production.rglob("*")) if p.is_file()}
    receipt["delivery_boundary_sha256"] = sha(out / "network_session_delivery_boundary.inc")
    crypto_command = [str(deps.DATA / "llvm/bin/clang"), "-std=c99", "-O1", "-g", "-I" + str(production / "network"),
                      "-c", str(production / "network/monocypher.c"), "-o", str(out / "crypto.o")]
    if args.asan:
        crypto_command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    crypto_build = subprocess.run(crypto_command, env=env, capture_output=True, text=True, timeout=60)
    (out / "crypto-build.log").write_text(crypto_build.stdout + crypto_build.stderr)
    receipt["crypto_compile"] = crypto_command; receipt["crypto_compile_exit"] = crypto_build.returncode
    if crypto_build.returncode:
        (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(out / "result.json"); return 1
    build = subprocess.run(command, env=env, capture_output=True, text=True, timeout=30)
    (out / "build.log").write_text(build.stdout + build.stderr)
    receipt["compile_exit"] = build.returncode
    if build.returncode:
        (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(out / "result.json")
        return 1
    cases = ("init-fail-guest-map", "init-fail-guest-engine", "init-fail-guest-imposter", "init-fail-host-map", "init-fail-offline-map",
             "init-ok-guest", "init-ok-host", "init-ok-offline", "offline", "host", "guest", "guest-backpressure", "guest-ack-refused", "guest-render-poison", "guest-epoch-erase", "guest-baseline-first", "guest-both-races",
             "vm-fence-offline", "vm-fence-host", "vm-fence-guest", "vm-fence-invalid-endpoint", "vm-fence-invalid-secret",
             "vm-fence-invalid-disabled", "vm-fence-invalid-role", "vm-fence-invalid-level", "vm-fence-allocation-net",
             "vm-fence-allocation-first", "vm-fence-allocation-second",
             "guest-birth-baseline", "guest-birth-backpressure", "guest-birth-coalesce", "guest-birth-service", "guest-birth-wait-order",
             "guest-birth-unavailable", "guest-birth-poison-pending", "guest-birth-reset", "guest-birth-lease", "guest-birth-decode",
             "guest-birth-apply-fail", "guest-birth-send-invalid", "guest-birth-wrong-role", "guest-birth-bad-epoch", "guest-birth-zero-tick", "guest-birth-zero", "guest-birth-close",
             "host-birth-baselined", "host-birth-accept", "host-birth-refused", "host-birth-wrong-stage", "host-birth-old-schema",
             "host-birth-short", "host-birth-long", "host-birth-envelope", "host-birth-wrong-epoch", "host-birth-session-epoch", "host-birth-wrong-level",
             "local-close-before-update", "local-close-after-flush", "local-close-before-render", "guest-local-close-before-render", "local-close-load", "local-close-prerun", "quiet-notice", "state-pacing", "peer-disconnected", "peer-leaving", "peer-malformed",
             "queue-backpressure", "queue-backpressure-control", "early-input", "publish-positive", "publish-send0", "publish-refused", "publish-baseline-refused", "publish-baseline-send0", "host-wait", "host-wait-noack", "host-wait-backpressure",
             "host-wait-overflow", "host-wait-resume-noack", "guest-wait", "guest-wait-lease", "guest-wait-gap", "guest-wait-resume-first", "both",
             "bad-endpoint", "bad-secret", "wrong-level", "virtual", "timeout", "incompatible", "guest-incompatible", "close-wait", "corrupt", "pump-budget",
             "disabled:BEA_PORT_CLOCK", "disabled:BEA_PORT_TAPE", "disabled:BEA_PORT_RECORD",
             "disabled:BEA_PORT_PROBES", "disabled:BEA_RECORD", "disabled:BEA_PORT_UPDATE_DUMP")
    cases += ("host-availability-basic", "host-availability-or", "host-availability-reblock",
              "host-availability-backpressure", "host-availability-timeout", "host-availability-invalid-send",
              "host-availability-invalid-first", "host-availability-invalid-role", "host-availability-invalid-gap",
              "host-availability-invalid-zero", "host-availability-invalid-tickzero", "host-availability-invalid-future",
              "host-availability-invalid-stale", "host-availability-invalid-conflict", "host-availability-invalid-tickback",
              "host-availability-invalid-schema", "host-availability-invalid-epoch", "host-availability-invalid-level",
              "guest-availability-basic", "guest-availability-device", "guest-availability-backpressure",
              "guest-availability-reblock", "guest-availability-timeout", "guest-availability-backwards",
              "guest-availability-overflow", "guest-availability-invalid-send", "guest-availability-unsent-ack",
              "guest-availability-invalid-role", "guest-availability-no-request", "guest-availability-close")
    cases += ("start-handoff-control", "start-handoff-after", "start-handoff-before", "start-handoff-unqueued",
              "start-handoff-wrong-epoch", "start-handoff-cancel", "start-handoff-quit", "start-handoff-terminal",
              "start-handoff-terminal-before", "start-handoff-leaving-first", "start-handoff-leaving-last")
    if args.lifecycle:
        from network_session_lifecycle_tests import CASES
        cases = CASES
    if args.delivery:
        from network_session_delivery_tests import CASES
        cases = CASES
    if args.callers:
        from network_session_caller_tests import CASES
        cases = CASES
    if args.case:
        unknown=set(args.case)-set(cases)
        if unknown:
            raise ValueError("unknown selected Session case(s): "+", ".join(sorted(unknown)))
        cases = tuple(args.case)
    for case in cases:
        proc = subprocess.run([str(binary), case], env=env, capture_output=True, text=True, timeout=8)
        (out / (case.replace(":", "-") + ".log")).write_text(proc.stdout + proc.stderr)
        observed = json.loads(proc.stdout) if proc.returncode == 0 else {}
        receipt["cases"].append({"case": case, "exit": proc.returncode, "observed": observed,
                                 "stderr": proc.stderr, "passed": proc.returncode == 0 and observed.get("checks", 0) > 0
                                 and "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" not in proc.stderr
                                 and "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789" not in proc.stderr})
        print(case, "PASS" if receipt["cases"][-1]["passed"] else "FAIL", flush=True)
    # Actual orchestrator mutants must fail the corresponding contract; compilation failure is never a pass.
    text = (production / "game/ce_network_session.cpp").read_text()
    mutations = (
        ("no-pending-mission-fence", "s.mission_vm_blocked = !s.host;", "s.mission_vm_blocked = false;", "vm-fence-guest", "validated local role is latched before world load"),
        ("release-mission-on-failure", "s.stage = FAILED;", "s.stage = FAILED; s.mission_vm_blocked = false;", "vm-fence-guest", "failed connection cannot release mission fence around live guest world"),
        ("no-service-proof", " && DriveBirthAck()", "", "guest-birth-service", "Send0 keeps committed proof pending"),
        ("send0-commits-proof", "if (sent == 1) s.birth_ack_sent", "if (sent >= 0) s.birth_ack_sent", "guest-birth-backpressure", "Send0 keeps committed proof pending"),
        ("stale-unavailable-proof", "if (!CE_WorldAppliedBirths(&proof)) { s.birth_ack = {}; return true; }", "if (!CE_WorldAppliedBirths(&proof)) { proof = s.birth_ack; }", "guest-birth-unavailable", "unavailable or poisoned getter cancels stale pending proof"),
        ("no-immediate-proof", "if (!DriveBirthAck()) { RuntimeFailure(); return; }", "/* negative: delay proof beyond Apply */", "guest-birth-decode", "successful Apply sends proof before the gameplay input peek"),
        ("guest-admits-proof", "if (!s.host || (s.stage != BASELINED", "if ((s.stage != BASELINED", "guest-birth-wrong-role", "guest refuses host-originated birth application ACK"),
        ("old-control-schema", "schema != kControlSchema", "(schema != 3 && schema != kControlSchema)", "host-birth-old-schema", "invalid or world-refused birth ACK closes session"),
        ("proof-before-wait", "return DriveWait() && DriveBirthAck() && DriveLifecycleAck() && DriveStateControls();", "return DriveBirthAck() && DriveWait() && DriveLifecycleAck() && DriveStateControls();", "guest-birth-wait-order", "shared wait acknowledgement precedes birth proof"),
        ("wrong-proof-tick", "SendControl(BIRTHS_APPLIED, s.birth_ack.serial, s.birth_ack.tick)", "SendControl(BIRTHS_APPLIED, s.birth_ack.serial, s.birth_ack.tick + 1)", "guest-birth-baseline", "birth proof follows a whole successful Apply"),
        ("teardown-retains-proof", "s.stage = DRAINING; s.have_frame = s.have_baseline = s.have_input = false;\n    s.birth_ack = {}; s.birth_ack_sent = 0;", "s.stage = DRAINING; s.have_frame = s.have_baseline = s.have_input = false; /* negative: retain previous session proof */", "guest-birth-reset", "teardown clears all birth ACK state"),
        ("unchecked-getter-epoch", "if (proof.epoch != s.epoch || !proof.tick)", "if (!proof.tick)", "guest-birth-bad-epoch", "invalid committed getter tuple fails before sending"),
        ("guest-unavailability-ignored", "return s.local_blocked || s.peer_guest_blocked;", "return s.local_blocked;", "host-availability-basic", "guest-only unavailability enters host shared wait"),
        ("ready-bypasses-barrier", "if (!s.guest_desired_blocked && (!s.shared_wait || s.wait_ack_pending)) return true;", "/* negative: READY before WAIT */", "guest-availability-backpressure", "rapid recovery cannot send READY before shared WAIT"),
        ("reblock-releases-resume", "if (kind == HOST_RESUME_ACK && !GuestMayResume()) return true;", "if (kind == HOST_RESUME_ACK && (!GuestMayResume() && false)) return true;", "guest-availability-reblock", "reblock after READY Send1 suppresses RESUME ACK before Poll releases wait"),
        ("host-reblock-releases-resume", "if (HostUnavailable()) return true; // a later admitted BLOCK cannot release this wait", "/* negative: release before the new WAIT */", "host-availability-reblock", "admitted reblock fences old resume ACK before release"),
        ("availability-send0-is-receipt", "if (sent == 1) { s.peer_guest_ack_pending = false;", "if (sent >= 0) { s.peer_guest_ack_pending = false;", "host-availability-backpressure", "host receipt advances only on Send1"),
        ("guest-send0-is-queued", "if (sent == 1) s.guest_request_queued = true;", "if (sent >= 0) s.guest_request_queued = true;", "guest-availability-backpressure", "guest retains BLOCK after Send0"),
        ("block-receipt-before-wait-held", "if (s.peer_guest_blocked && (!s.shared_wait || s.wait_phase != WAIT_HELD)) return true;", "/* negative: attest the old wait */", "host-availability-reblock", "BLOCK receipt cannot attest the previous held wait during RESUME"),
        ("no-start-handoff", "bool start_handoff = s.host", "bool start_handoff = false && s.host", "start-handoff-after", "ordered post-START_APPLIED BLOCK is retained through the full PreparePlay handoff"),
        ("handoff-before-start-receipt", "s.start_queued && s.start_applied;", "s.start_queued;", "start-handoff-before", "BLOCK before queued START and exact START_APPLIED refuses without latching availability"),
        ("handoff-before-start-queued", "s.start_queued && s.start_applied;", "s.start_applied;", "start-handoff-unqueued", "BLOCK before queued START and exact START_APPLIED refuses without latching availability"),
        ("terminal-loses-start-order", "s.start_applied = true; return true;", "if (!terminal) s.start_applied = true; return true;", "start-handoff-terminal", "ordered terminal handoff and peer departure drain gracefully without play admission"),
        ("early-handoff-drives-wait", "bool DriveWait() {\n    if (s.stage != RUNNING) return true;", "bool DriveWait() { /* negative: drive before RUNNING */", "start-handoff-after", "handoff metadata never calls the world input gate before RUNNING"),
        ("handoff-ignores-native-close", "    if (CE_LockstepRequested()) return CE_LockstepBeforeUpdate();\n    if (!s.requested) return 1;\n    if (CE_SessionGameQuitCode()) return 0;", "    if (CE_LockstepRequested()) return CE_LockstepBeforeUpdate();\n    if (!s.requested) return 1;", "start-handoff-cancel", "native close before first update prevents polling or driving retained handoff work"),
        ("terminal-handoff-admits-ready", "if (terminal || s.peer_leaving) return PeerDeparture();", "if ((terminal || s.peer_leaving) && false) return PeerDeparture();", "start-handoff-terminal", "ordered terminal handoff and peer departure drain gracefully without play admission"),
    )
    if args.lifecycle:
        from network_session_lifecycle_tests import MUTATIONS
        mutations = MUTATIONS
    if args.delivery:
        from network_session_delivery_tests import MUTATIONS
        mutations = MUTATIONS
    if args.callers:
        mutations = ()
    if args.no_mutants:
        mutations = ()
    if args.mutation:
        unknown=set(args.mutation)-{row[0] for row in mutations}
        if unknown:
            raise ValueError("unknown selected Session mutant(s): "+", ".join(sorted(unknown)))
        mutations=tuple(row for row in mutations if row[0] in set(args.mutation))
    receipt["mutations"] = []
    for label, old, new, case, reason in mutations:
        if text.count(old) != 1:
            raise RuntimeError("mutation no longer names one production boundary: " + label)
        variant = production / "game" / (label + ".cpp")
        variant.write_text(text.replace(old, new))
        variant_binary = out / label
        variant_command = [('-DSESSION_SOURCE="' + str(variant) + '"') if arg.startswith('-DSESSION_SOURCE=')
                           else str(variant_binary) if arg == str(binary) else arg for arg in command]
        build = subprocess.run(variant_command, env=env, capture_output=True, text=True, timeout=30)
        (out / (label + "-build.log")).write_text(build.stdout + build.stderr)
        proc = subprocess.run([str(variant_binary), case], env=env, capture_output=True, text=True, timeout=8) if not build.returncode else None
        if proc:
            (out / (label + ".log")).write_text(proc.stdout + proc.stderr)
        passed = build.returncode == 0 and proc.returncode == 2 and reason in proc.stderr
        receipt["mutations"].append({"label": label, "case": case, "source_sha256": sha(variant), "compile_exit": build.returncode,
                                     "exit": proc.returncode if proc else None, "reason": reason, "passed": passed})
        print(label, "PASS" if passed else "FAIL", flush=True)
    receipt["caller_mutations"] = []
    init_text = init_entry.read_text()
    fragment = "\t\tShutdown();\n\t\tCE_SessionAfterWorldShutdown();"
    if init_text.count(fragment) != 1:
        raise RuntimeError("failed Init caller no longer has one native-then-session retirement boundary")
    caller_variants = (
        ("init-missing-session-retirement", "\t\tShutdown();", "failed native Init retires pending session after native shutdown"),
        ("init-early-session-retirement", "\t\tCE_SessionAfterWorldShutdown();\n\t\tShutdown();", "native resource boundaries retain admitted mission fence"),
    )
    for label, replacement, reason in (() if args.lifecycle or args.delivery or args.callers or args.no_mutants or args.mutation else caller_variants):
        variant = out / (label + ".inc")
        variant.write_text(init_text.replace(fragment, replacement))
        variant_binary = out / label
        variant_command = [('-DINIT_ENTRY_SOURCE="' + str(variant) + '"') if arg.startswith('-DINIT_ENTRY_SOURCE=')
                           else str(variant_binary) if arg == str(binary) else arg for arg in command]
        build = subprocess.run(variant_command, env=env, capture_output=True, text=True, timeout=30)
        (out / (label + "-build.log")).write_text(build.stdout + build.stderr)
        proc = subprocess.run([str(variant_binary), "init-fail-guest-engine"], env=env, capture_output=True, text=True, timeout=8) if not build.returncode else None
        if proc:
            (out / (label + ".log")).write_text(proc.stdout + proc.stderr)
        passed = build.returncode == 0 and proc.returncode == 2 and reason in proc.stderr
        receipt["caller_mutations"].append({"label": label, "source_sha256": sha(variant), "compile_exit": build.returncode,
                                            "exit": proc.returncode if proc else None, "reason": reason, "passed": passed})
        print(label, "PASS" if passed else "FAIL", flush=True)
    receipt["binary_sha256"] = sha(binary)
    receipt["load_admission_mutations"]=[]
    if args.callers and not args.no_mutants:
        variants=[]
        for site,case in (("after-load","caller-afterload-refuse"),("automatic-script","caller-script-refuse"),
                          ("pre-run","caller-prerun-refuse")):
            original=abort_fragments[site]
            changed=original.replace("(EQuitType)CE_SessionGameAbortLoad()","QT_NONE")
            variants.append((site+"-wrong-return",original,changed,case,"native abort site returns the actual bridge quit result"))
        original=abort_fragments["automatic-script"]
        variants.append(("script-falls-through",original,original.replace("if (ce_script < 0) return (EQuitType)CE_SessionGameAbortLoad();",""),
                         "caller-script-refuse","actual automatic-script refusal invalidates its prepared proof before aborting PreRun"))
        for label,old,new,case,reason in variants:
            actual=load_admission.read_text()
            if actual.count(old)!=1 or old==new:
                raise ValueError("load-admission mutant no longer names one exact native fragment: "+label)
            variant=out/(label+".inc");variant.write_text(actual.replace(old,new,1));target=out/label
            selected=[('-DLOAD_ADMISSION_SOURCE="'+str(variant)+'"') if arg.startswith('-DLOAD_ADMISSION_SOURCE=')
                      else str(target) if arg==str(binary) else arg for arg in command]
            compiled=subprocess.run(selected,env=env,capture_output=True,text=True,timeout=30)
            (out/(label+"-build.log")).write_text(compiled.stdout+compiled.stderr)
            proc=subprocess.run([str(target),case],env=env,capture_output=True,text=True,timeout=8) if not compiled.returncode else None
            if proc:(out/(label+".log")).write_text(proc.stdout+proc.stderr)
            passed=compiled.returncode==0 and proc.returncode==2 and reason in proc.stderr
            receipt["load_admission_mutations"].append({"label":label,"case":case,"compile_exit":compiled.returncode,
                "exit":proc.returncode if proc else None,"reason":reason,"source_sha256":sha(variant),"passed":passed})
            print(label,"PASS" if passed else "FAIL",flush=True)
    if args.target_session:
        receipt["target_session"] = []
        for target in ("linux", "windows"):
            target_command = [*port_build.TARGETS[target]["cxx"], "-std=gnu++17", "-DNOMINMAX", "-fms-extensions",
                              "-fshort-wchar", "-mms-bitfields", "-fsyntax-only",
                              str(production / "game/ce_network_session.cpp")]
            result = subprocess.run(target_command, env=env, capture_output=True, text=True, timeout=30)
            (out / (target + "-session.log")).write_text(result.stdout + result.stderr)
            receipt["target_session"].append({"target": target, "compile": target_command, "exit": result.returncode})
    if args.mirror:
        receipt["native_callers"] = compile_callers(args.mirror, out,
            production / "game/ce_network_world.h", args.world_patch)
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if (all(case["passed"] for case in receipt["cases"]) and all(row["passed"] for row in receipt["mutations"]) and all(row["passed"] for row in receipt["caller_mutations"]) and
                 all(row["passed"] for row in receipt["load_admission_mutations"]) and
                 all(not row["exit"] for row in receipt.get("target_session", [])) and
                 all(not row["exit"] for row in receipt.get("native_callers", {}).get("results", []))) else 1


if __name__ == "__main__":
    raise SystemExit(main())
