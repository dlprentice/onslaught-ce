#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build game-facing ICE caller fixtures from pinned, caller-supplied archives.

This is not a game launch. Run only in an isolated network namespace, with a
private HOME and bounded resources. No downloads, external service or account
credentials are used. Windows is compile/link only. Private fixtures remain in
the selected new ignored output directory; logs contain fixed diagnostics only.

The namespace needs loopback up (`unshare -rn`, then `ip link set lo up`); a second
address is not needed (a dummy interface, `ip link add dummy0 type dummy` with a
documentation-range address, gives the same results). The --causal control of the
relay-only policy is meant to fail at its traffic check, but about one run in
thirteen (6 of 80 reruns of its retained binary, in both kinds of namespace) stalls
earlier: the fixture's 6 s wait for "private offer/answer and grants admit both CE
peers" runs out, the run takes about 7 s instead of 2, and the tool stops with
"caller check failed: relay-policy". Rerun before reading that as a regression; the
unmutated caller did not stall in 15 runs. Pass --toolchain-data, the pinned
toolchain, for a native run too: the host compiler's stricter -Wextra stops the
session sources.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

import ice_bridge_build as bridge

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--juice-archive", type=Path, required=True)
    parser.add_argument("--enet-archive", type=Path, required=True)
    parser.add_argument("--monocypher-archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target", choices=("native", "linux32", "windows32"), default="native")
    parser.add_argument("--toolchain-data", type=Path)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--causal", action="store_true", help="run bounded authored fault controls")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to((ROOT / "local-data").resolve()):
        parser.error("output must be new and below this checkout's local-data")
    if args.target != "native" and not args.toolchain_data:
        parser.error("cross builds require existing pinned toolchain data")
    windows = args.target == "windows32"
    if (args.run or args.causal) and windows:
        parser.error("Windows execution is a separate acceptance step")
    if args.causal and not args.run:
        parser.error("causal controls require --run")
    output.mkdir(mode=0o700, parents=True)
    session = output / "session"
    command = [sys.executable, str(ROOT / "port/tools/ice_session_build.py"),
               "--output", str(session), "--target", args.target]
    for name in ("juice_archive", "enet_archive", "monocypher_archive", "toolchain_data"):
        value = getattr(args, name)
        if value:
            command += ["--" + name.replace("_", "-"), str(value.resolve())]
    subprocess.run(command, check=True, timeout=180)
    enet = session / "bridge" / ("enet-" + bridge.ENET_COMMIT)
    juice = session / "bridge" / ("libjuice-" + bridge.JUICE_COMMIT)
    data = args.toolchain_data.resolve() if args.toolchain_data else None
    if windows:
        cxx = str(data / "llvm-mingw/bin/i686-w64-mingw32-clang++")
        flags, libraries = [], ["-lws2_32", "-lwinmm", "-lbcrypt"]
    else:
        cxx = str(data / "llvm/bin/clang++") if data else shutil.which("c++")
        flags = ["-m32", "--sysroot=" + str(data / "sysroot-sniper")] if args.target == "linux32" else []
        libraries = ["-pthread"]
    if not cxx:
        parser.error("C++ compiler unavailable")
    network = ROOT / "port/network"
    common = [*flags, "-O2", "-pthread", "-std=c++17", "-Wall", "-Wextra", "-Werror",
              "-I" + str(enet / "include"), "-I" + str(session / "monocypher-4.0.2/src"), "-I" + str(network)]
    optional = ["-DCE_NETWORK_ICE=1", "-DJUICE_STATIC", "-I" + str(juice / "include")]
    wrappers = [] if windows else ["-Wl,--wrap=time", "-Wl,--wrap=juice_create", "-Wl,--wrap=juice_send", "-Wl,--wrap=enet_host_service"]
    suffix = ".exe" if windows else ""
    results = {"target": args.target, "game_process": False, "runs": [], "causal": []}
    with (output / "build.log").open("w") as log:
        def build(command: list[str | Path]) -> None:
            subprocess.run(list(map(str, command)), check=True, stdout=log, stderr=subprocess.STDOUT, timeout=90)

        objects = [*sorted((session / "bridge").glob("enet-*.o")), session / "monocypher.o"]
        enabled_objects = [*objects, *sorted((session / "bridge").glob("juice-*.o")),
                           *[session / ("ice-" + name + ".o") for name in ("codec", "transport", "ice_bridge")]]
        for source in ("ice_exchange", "ice_game"):
            obj = output / (source + ".o")
            build([cxx, *common, *optional, "-c", network / (source + ".cpp"), "-o", obj])
            enabled_objects.append(obj)
        # Both public caller seams compile without any optional include or link.
        build([cxx, *common, "-c", network / "ice_game.cpp", "-o", output / "disabled-game.o"])
        disabled = output / ("direct-game-tests" + suffix)
        build([cxx, *common, network / "tests/ice_game_tests.cpp", *objects,
               session / "direct-codec.o", session / "direct-transport.o", *libraries, "-o", disabled])
        binary = output / ("ice-game-tests" + suffix)

        def link(target: Path, selected: list[Path]) -> None:
            build([cxx, *common, *optional, network / "tests/ice_game_tests.cpp", *selected,
                   *libraries, *wrappers, "-o", target])

        link(binary, enabled_objects)
        credentials = output / ("credential-tests" + suffix)
        build([cxx, *common, *optional, network / "tests/ice_credentials_tests.cpp", *libraries, "-o", credentials])

        def run(target: Path, argument: str, name: str, expected: int = 0, contains: str = "") -> None:
            completed = subprocess.run([str(target), argument], capture_output=True, text=True, timeout=40)
            # The fixture never prints grants, SDP, canaries, paths or account data.
            text = completed.stdout + completed.stderr
            (output / (name + ".log")).write_text(text)
            okay = completed.returncode == expected and (not contains or contains in text)
            results["causal" if expected else "runs"].append({"name": name, "exit": completed.returncode, "passed": okay})
            (output / "result.json").write_text(json.dumps(results, indent=2) + "\n")
            if not okay:
                raise SystemExit("caller check failed: " + name + "; private diagnostics retained")
            print("PASS", name)

        if args.run:
            credential_root = output / "credential-fixtures"
            credential_root.mkdir(mode=0o700)
            run(credentials, str(credential_root), "credentials-and-directory-identity")
            run(disabled, str(output / "direct-fixtures"), "disabled-caller")
            run(binary, str(output / "ice-fixtures"), "relay-caller")
            run(binary, "log-control", "credential-log-control")
            run(session / "bridge/ice_bridge_tests", "", "bridge-relay-policy")
            for mode in ("direct", "ice"):
                for suite in ("session", "protocol"):
                    target = session / (mode + "-" + suite + "-tests")
                    run(target, "", mode + "-" + suite)
        if args.causal:
            mutations = [
                ("directory-alias", "ice_game", "owner->credentials.RequireDistinctDirectories(base, game)",
                 "(std::strcmp(base, game) != 0 || owner->credentials.RequireDistinctDirectories(base, game))",
                 "menu pilot refuses a trailing-slash alias before publication"),
                ("relay-policy", "ice_bridge", "juice_set_relay_only(bridge->agent_, require_relay)",
                 "juice_set_relay_only(bridge->agent_, false)",
                 "selected relay remains active throughout sustained bidirectional traffic"),
                ("request", "ice_game", "owner->credentials.PrepareRequest(directory, config->role, owner->exchange->Context(), now, kPreparationMs / 1000, lifetime)", "true",
                 "private preparation request is present and bounded"),
                ("requested-lifetime", "ice_game", "kPreparationMs / 1000, lifetime)", "kPreparationMs / 1000, 600)",
                 "lobby requests the default two-hour lifetime"),
                ("lifetime-selection", "ice_game", "return seconds >= kPreparationMs / 1000;",
                 "seconds = CeIceCredentials::DefaultLifetimeSeconds; return true;",
                 "explicit lifetime is requested rather than only permitted"),
                ("request-grant-cap", "ice_game", "grantLimit && credentials.ExpiresAt() > grantLimit", "false",
                 "staged grant cannot exceed the explicitly selected lifetime"),
                ("start-resets-deadline", "transport", "s->ice = CeIceBridge::Create(s->host, *ice);",
                 "s->started = CE_NetNowMs(); s->ice = CeIceBridge::Create(s->host, *ice);",
                 "late grant does not restart the original preparation deadline"),
                ("expiry", "ice_game", "!now || !credentials.IsCurrent(now)", "!now",
                 "active session ends clearly when its grant expires"),
                ("expiry-message", "ice_game", "relay access or local clock is invalid", "relay session ended",
                 "active session ends clearly when its grant expires"),
                ("expiry-instruction", "ice_game", "; relaunch both games with fresh private lobby and game folders and repeat the private code and file exchange\";\n            return CE_NET_TIMEOUT;",
                 "\";\n            return CE_NET_TIMEOUT;", "active session ends clearly when its grant expires"),
                ("exchange", "transport", "s->iceCallbacks.poll(s->iceExchange, s, now)", "CE_NET_OK",
                 "private offer/answer and grants admit both CE peers"),
                ("close-pump", "transport", "if (session->ice && !session->ice->Pump()) break;", "",
                 "bounded close drain delivers authenticated leave after initial loss"),
                ("deadline", "transport", "now - s->started >= s->iceTimeoutMs", "false",
                 "missing grant cannot outlive the preparation deadline"),
                ("dependency-log", "ice_bridge", "juice_set_log_level(JUICE_LOG_LEVEL_NONE);", "",
                 "credential_exposed=yes"),
                # The progress the exchange's private trace reads: each field comes from the agent.
                ("progress-relay-count", "ice_bridge", "RelayCandidate(line) && count != std::numeric_limits<uint32_t>::max()) ++count;",
                 "false) ++count;", "an admitted pair reports finished gathering, relay candidates and a connected agent"),
                ("progress-gathered", "transport", "out->gathered = s->ice->GatheringDone() ? 1 : 0;", "out->gathered = 0;",
                 "an admitted pair reports finished gathering, relay candidates and a connected agent"),
                ("progress-state", "transport", "out->state = s->ice->StateIndex();", "out->state = 0;",
                 "an admitted pair reports finished gathering, relay candidates and a connected agent"),
            ]
            for name, source, before, after, failure in mutations:
                content = (network / (source + ".cpp")).read_text()
                if content.count(before) != 1:
                    raise SystemExit("causal anchor changed: " + name)
                changed, obj, mutant = (output / (name + ext) for ext in (".cpp", ".o", suffix or ".bin"))
                changed.write_text(content.replace(before, after))
                build([cxx, *common, *optional, "-c", changed, "-o", obj])
                replaced = output / (source + ".o") if source == "ice_game" else session / ("ice-" + source + ".o")
                link(mutant, [obj if item == replaced else item for item in enabled_objects])
                run(mutant, "log-control" if name == "dependency-log" else str(output / (name + "-fixtures")),
                    name, expected=1, contains=failure)
    (output / "result.json").write_text(json.dumps(results, indent=2) + "\n")
    print("Built game-facing caller fixtures:", args.target, "(compile/link only)" if not args.run else "(authored loopback only)")


if __name__ == "__main__":
    main()
