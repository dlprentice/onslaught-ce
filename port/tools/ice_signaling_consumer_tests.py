#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded production-consumer controls with authored backend stand-ins.

No engine, networking, credentials or retail data are used. The authored checks
use an authored provider. The real-provider checks link the real signaling
provider behind an authored HTTPS transport (no socket or HTTP) under the same
engine stand-ins: the menu's lobby button after the status page's last pump
(tests/ice_lobby_clock_driver.cpp). The authored checks also drive the native
relay exchange's private trace (tests/ice_relay_trace_driver.cpp): the lines it
prints per step, its beats and bounds, and the one sentence it prints when it
stops, with mutants of the trace itself. Native transport, real owner
validation and composed player acceptance are separate.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

import deps
from ice_exchange_tests import MONOCYPHER_SHA256

# The lobby's pump of an admitted pair before its fixed request (ice_game.cpp CreateGameSession), and its guard.
LOBBY_PUMP = "signalChoice->Preflight(now, monotonic);"
LOBBY_PUMP_GUARD = "if (!exchangeOwners && signalChoice->View().status == CeIceSignalStatus::Admitted)"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--exchange-header", type=Path)
    parser.add_argument("--cxx")
    parser.add_argument("--monocypher-archive", type=Path, default=deps.DOWNLOADS / "monocypher-4.0.2.tar.gz",
                        help="the pinned Monocypher archive the real provider links (default: the dependency cache's)")
    parser.add_argument("--no-controls", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    network = repo / "port/network"
    exchange_header = (args.exchange_header or network / "ice_exchange.h").resolve()
    exchange_bytes = exchange_header.read_bytes()
    if not all(name in exchange_bytes for name in (b"ExportLocalRecord", b"ImportPeerRecord")):
        parser.error("the upstream exchange bridge header is required; compose it or select --exchange-header")
    cxx = shutil.which(args.cxx or "clang++")
    if not cxx:
        parser.error("the selected C++ compiler is unavailable")
    archive = args.monocypher_archive.resolve()
    if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest() != MONOCYPHER_SHA256:
        parser.error("the pinned Monocypher archive is required for the real-provider checks")
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = (args.output or repo / "local-data/ice-signaling-consumer" / stamp).resolve()
    output.mkdir(parents=True, exist_ok=False)
    output.chmod(0o700)
    environment = output / "environment"
    for name in ("home", "config", "cache", "data", "state", "runtime", "tmp"):
        (environment / name).mkdir(parents=True, mode=0o700)
    env = {
        "PATH": str(Path(cxx).parent) + os.pathsep + os.defpath,
        "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8", "CCACHE_DISABLE": "1",
        "HOME": str(environment / "home"), "TMPDIR": str(environment / "tmp"),
        **{"XDG_" + key + "_HOME": str(environment / value) for key, value in
           (("CONFIG", "config"), ("CACHE", "cache"), ("DATA", "data"), ("STATE", "state"))},
        "XDG_RUNTIME_DIR": str(environment / "runtime"),
    }
    fixture = (network / "tests/ice_signaling_consumer_tests.cpp").read_text()
    driver = (network / "tests/ice_lobby_clock_driver.cpp").read_text()
    trace_driver = (network / "tests/ice_relay_trace_driver.cpp").read_text()
    production = (network / "ice_game.cpp").read_text()
    headers = {name: (network / name).read_bytes() for name in
               ("network.h", "ice_game.h", "ice_signaling.h", "ice_session.h")}
    headers["ice_exchange.h"] = exchange_bytes
    provider_headers = {name: (network / name).read_bytes() for name in ("signaling_provider.h", "signaling_https.h")}
    provider_sources = {name: repo / name for name in ("port/network/signaling_provider.cpp", "port/updater/sha256.c",
                                                       "port/updater/sha256.h")}
    result = {
        "scope": "production consumer control flow with authored backend implementations, and with the real "
                 "signaling provider behind an authored HTTPS transport",
        "limits": ["no native HTTP or socket", "no real credential or record validator", "no libjuice",
                   "no connection", "no game", "no platform or player acceptance"],
        "inputs": {name: hashlib.sha256(data).hexdigest() for name, data in {**headers, **provider_headers}.items()},
        "provider_sha256": {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in provider_sources.items()},
        "monocypher_archive_sha256": MONOCYPHER_SHA256,
        "production_sha256": hashlib.sha256(production.encode()).hexdigest(),
        "fixture_sha256": hashlib.sha256(fixture.encode()).hexdigest(),
        "driver_sha256": hashlib.sha256(driver.encode()).hexdigest(),
        "trace_driver_sha256": hashlib.sha256(trace_driver.encode()).hexdigest(),
        "exchange_header": str(exchange_header), "compiler": cxx, "checks": [], "passed": False,
    }

    def compile_step(command: list[str], log: Path) -> bool:
        built = subprocess.run(command, cwd=output, env=env, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90, check=False)
        log.write_bytes(built.stdout)
        return built.returncode == 0

    provider_objects: list[str] = []

    def real_provider() -> list[str]:
        """The real provider, Monocypher and SHA-256 objects, built once from the pinned and checked-out sources."""
        if not provider_objects:
            objects = output / "provider"
            objects.mkdir(mode=0o700)
            with tarfile.open(archive, "r:gz") as stream:
                stream.extractall(objects, filter="data")
            mono = objects / "monocypher-4.0.2/src"
            steps = [
                ([cxx, "-x", "c", "-std=gnu11", "-O1", "-c", str(mono / "monocypher.c"), "-o", str(objects / "monocypher.o")],
                 objects / "monocypher.o"),
                ([cxx, "-x", "c", "-std=gnu11", "-O1", "-c", str(repo / "port/updater/sha256.c"), "-o", str(objects / "sha256.o")],
                 objects / "sha256.o"),
                ([cxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-DCE_NETWORK_ICE=1", "-I", str(mono), "-c",
                  str(network / "signaling_provider.cpp"), "-o", str(objects / "signaling_provider.o")],
                 objects / "signaling_provider.o"),
            ]
            for command, target in steps:
                if not compile_step(command, objects / (target.stem + ".compile.private.log")):
                    raise ValueError("real provider objects did not build")
            provider_objects.extend(str(target) for _, target in steps)
        return provider_objects

    def check(label: str, source: str, enabled: bool, negative: bool = False, real: bool = False,
              trace: bool = False) -> bool:
        root = output / label
        root.mkdir()
        for name, data in headers.items():
            (root / name).write_bytes(data)
        for name, selector in (("fixture_state.h", "STATE"), ("ice_credentials.h", "CREDENTIALS"),
                               ("juice/juice.h", "JUICE")):
            target = root / name
            target.parent.mkdir(exist_ok=True)
            macro = "CE_SIGNAL_FIXTURE_" + selector
            target.write_text("#define " + macro + "\n" + fixture + "\n#undef " + macro + "\n")
        (root / "ice_game.cpp").write_text(source)
        if trace:
            # The trace driver includes the whole consumer fixture (its main renamed) for the engine stand-ins and
            # needs no provider: the fixture's authored provider and stage are the exchange's peers.
            (root / "signaling_consumer_fixture.inc").write_text(fixture)
            (root / "relay_trace_driver.cpp").write_text(trace_driver)
            entry_source, extra = root / "relay_trace_driver.cpp", []
        elif real:
            # The driver includes the whole consumer fixture (its main renamed) for the engine stand-ins.
            for name, data in provider_headers.items():
                (root / name).write_bytes(data)
            (root / "signaling_consumer_fixture.inc").write_text(fixture)
            (root / "lobby_clock_driver.cpp").write_text(driver)
            entry_source, extra = root / "lobby_clock_driver.cpp", real_provider()
        else:
            (root / "fixture.cpp").write_text(fixture)
            entry_source, extra = root / "fixture.cpp", []
        binary = root / "consumer-check"
        command = [cxx, "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                   "-I", str(root), *(["-DCE_NETWORK_ICE=1"] if enabled else []),
                   str(root / "ice_game.cpp"), str(entry_source), *extra, "-o", str(binary)]
        entry = {"name": label, "negative_control": negative, "real_provider": real, "relay_trace": trace,
                 "compiled": False, "passed": False}
        result["checks"].append(entry)
        built = subprocess.run(command, cwd=root, env=env, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45, check=False)
        (root / "compile.private.log").write_bytes(built.stdout)
        entry["compiled"] = built.returncode == 0
        if built.returncode:
            return False
        ran = subprocess.run([str(binary), str(root / "selected")], cwd=root, env=env,
                             stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             timeout=10, check=False)
        (root / "run.private.log").write_bytes(ran.stdout)
        match = re.search(rb"(\d+) assertions, (\d+) failures", ran.stdout)
        if match:
            entry["assertions"], entry["failures"] = map(int, match.groups())
        entry["passed"] = bool(match) and ((ran.returncode != 0 and entry["failures"] > 0)
                                          if negative else (ran.returncode == 0 and entry["failures"] == 0))
        return entry["passed"]

    try:
        passed = check("enabled", production, True)
        passed = check("disabled", production, False) and passed
        passed = check("real-provider", production, True, real=True) and passed
        passed = check("relay-trace", production, True, trace=True) and passed
        controls = [
            ("guest-order", "if (exchange->Role() == CE_NET_GUEST && !signalPeerImported)", "if (false)"),
            ("manual-bypass", "if (signalProvider) return ReadSignalGrant(now);", "if (false) return ReadSignalGrant(now);"),
            ("premature-ack", "if (imported == CeIceExchangeResult::Ready) {", "if (imported != CeIceExchangeResult::Error) {"),
            ("pair-close", "if (signaling) signaling->CancelLocal();",
             "if (signaling) { signaling->CancelLocal(); signalProvider->ClosePair(); }"),
            ("missing-publication", "!signaling->PublishLocal(record.data(), size)", "false"),
            ("game-no-refresh", "request.requestedAt = plan.requestedAt;", "request.requestedAt = now;"),
            ("game-context-binding", "std::memcmp(plan.context, request.context, sizeof request.context) != 0", "false"),
            ("lobby-unpumped", LOBBY_PUMP, "{}"),
            ("lobby-pumps-unadmitted", LOBBY_PUMP_GUARD, "if (!exchangeOwners)"),
        ]
        # The lobby's pump, without which the real provider refuses a button pressed after the status page's last
        # pump (main before this fix), and its guard, without which a pair still waiting for consent is polled.
        real_controls = [("real-provider-unpumped", LOBBY_PUMP, "{}"),
                         ("real-provider-pumps-unadmitted", LOBBY_PUMP_GUARD, "if (!exchangeOwners)")]
        # The exchange's private trace (ice_game.cpp GameExchange::Trace): each control removes, bounds wrongly, mislabels or
        # leaks one thing the trace driver requires.
        trace_controls = [
            ("trace-removed", "        Trace(session, monotonic, result);\n", ""),
            ("trace-prints-every-poll", "if ((!traceWordKnown || std::memcmp(word, traceWord, sizeof word) != 0) && traceLines < kTraceLines) {",
             "if (traceLines < kTraceLines) {"),
            ("trace-line-bound-dropped", "std::memcmp(word, traceWord, sizeof word) != 0) && traceLines < kTraceLines) {",
             "std::memcmp(word, traceWord, sizeof word) != 0)) {"),
            ("beat-missing", "} else if (monotonic >= traceBeatAt && traceBeats < kBeats) {", "} else if (false) {"),
            ("beats-unbounded", "} else if (monotonic >= traceBeatAt && traceBeats < kBeats) {", "} else if (monotonic >= traceBeatAt) {"),
            ("beat-too-early", "constexpr uint64_t kBeatMs = 5000,", "constexpr uint64_t kBeatMs = 1000,"),
            ("beat-not-rearmed-by-progress", "            traceWordKnown = true; ++traceLines; printed = true;\n",
             "            traceWordKnown = true; ++traceLines;\n"),
            ("stopped-removed", "if (result != CE_NET_OK && !traceStopped) {", "if (false) {"),
            ("stopped-repeats", "if (result != CE_NET_OK && !traceStopped) {", "if (result != CE_NET_OK) {"),
            ("stopped-without-sentence", "problem && problem[0] ? problem : kTraceStoppedFallback);", "kTraceStoppedFallback);"),
            ("stopped-without-fallback", "problem && problem[0] ? problem : kTraceStoppedFallback);", "problem ? problem : kTraceStoppedFallback);"),
            ("stopped-leaks-directory",
             "stopped: %s\\n\",\n                         exchange->Role() == CE_NET_HOST ? \"host\" : \"guest\",\n"
             "                         problem && problem[0] ? problem : kTraceStoppedFallback);",
             "stopped: %s %s\\n\",\n                         exchange->Role() == CE_NET_HOST ? \"host\" : \"guest\",\n"
             "                         problem && problem[0] ? problem : kTraceStoppedFallback, directory.c_str());"),
            ("step-grant-bit-missing", "(grantParsed ? 0x02u : 0u) | (iceStarted ? 0x04u : 0u)", "(iceStarted ? 0x04u : 0u)"),
            ("step-grant-bit-before-parse",
             "        if (!credentials.Parse(bytes.value.data(), size, exchange->Role(), exchange->Context(), now, mode)) {",
             "        grantParsed = true;\n        if (!credentials.Parse(bytes.value.data(), size, exchange->Role(), exchange->Context(), now, mode)) {"),
            ("step-bits-swapped", "(accepted ? 0x08u : 0u) | (published ? 0x10u : 0u)", "(accepted ? 0x10u : 0u) | (published ? 0x08u : 0u)"),
            ("step-record-bit-early", "(signalPublished ? 0x20u : 0u)", "(published ? 0x20u : 0u)"),
            ("role-swapped", "exchange->Role() == CE_NET_HOST ? \"host\" : \"guest\", label,",
             "exchange->Role() == CE_NET_HOST ? \"guest\" : \"host\", label,"),
            ("signaling-index-dropped", "static_cast<uint32_t>(view.status),", "static_cast<uint32_t>(view.status) * 0u,"),
            ("gathered-relay-swapped", "traceIce.gathered,\n                                  traceIce.relay_candidates, traceIce.state};",
             "traceIce.relay_candidates,\n                                  traceIce.gathered, traceIce.state};"),
            ("progress-read-every-poll", "monotonic >= traceSampleAt && (traceLines < kTraceLines || traceBeats < kBeats)", "true"),
            ("progress-stale-at-stop", "if (iceStarted && (result != CE_NET_OK ||", "if (iceStarted && (false ||"),
            ("manual-route-traced", "        if (!signalProvider) return;\n        if (!traceBeatAt)", "        if (!traceBeatAt)"),
            ("after-ms-absolute", "monotonic > createdMs ? monotonic - createdMs : 0", "monotonic"),
            ("created-at-zero", "owner->createdMs = CE_NetNowMs();", "owner->createdMs = 0;"),
        ]
        if passed and not args.no_controls:
            for label, before, after in trace_controls:
                if production.count(before) != 1:
                    raise ValueError("negative-control anchor changed: " + label)
                passed = check("control-" + label, production.replace(before, after, 1), True, True, trace=True) and passed
            for label, before, after in controls:
                if production.count(before) != 1:
                    raise ValueError("negative-control anchor changed: " + label)
                passed = check("control-" + label, production.replace(before, after, 1), True, True) and passed
            for label, before, after in real_controls:
                if production.count(before) != 1:
                    raise ValueError("negative-control anchor changed: " + label)
                passed = check("control-" + label, production.replace(before, after, 1), True, True, real=True) and passed
        result["passed"] = passed
    except (OSError, subprocess.TimeoutExpired, ValueError, tarfile.TarError) as error:
        result["error_type"] = type(error).__name__
    finally:
        (output / "result.private.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed": result["passed"], "checks": result["checks"], "output": str(output)}))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
