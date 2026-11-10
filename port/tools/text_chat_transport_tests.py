#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build actual optional text transport against existing reviewed dependencies.

No downloads, installs, game files, public endpoints or real invites. The
receipt binds the source files actually compiled; it does not authenticate a
caller-supplied dependency tree's origin. All traffic is invented loopback data.
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


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3, 2 * 1024**3))
    resource.setrlimit(resource.RLIMIT_CPU, (45, 45))
    resource.setrlimit(resource.RLIMIT_FSIZE, (32 * 1024**2, 32 * 1024**2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--word-size", choices=("32", "64", "both"), default="both")
    parser.add_argument("--no-controls", action="store_true")
    parser.add_argument("--enet-source", type=Path)
    parser.add_argument("--monocypher-source", type=Path)
    parser.add_argument('--control', action='append', help='run selected compiling controls, plus the baseline')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(repo / "port" / "tools"))
    import deps
    enet = args.enet_source or deps.DATA / "src" / "enet-5a9c537fd464b3c6d3c55e1d3bd47588faf71b42"
    mono = args.monocypher_source or deps.DATA / "src" / "monocypher-4.0.2" / "src"
    if not (enet / "include/enet/enet.h").is_file() or not (mono / "monocypher.c").is_file():
        parser.error("existing ENet and Monocypher source inputs are required")
    base = repo / "local-data" / "port" / "text-chat-transport-tests"
    base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=base)); work.chmod(0o700)
    (work / "home").mkdir(mode=0o700)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(work / "home"),
           "TMPDIR": str(work), "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8"}
    inputs = list(enet.glob("*.c")) + list((enet / "include/enet").glob("*.h"))
    inputs += [mono / "monocypher.c", mono / "monocypher.h"]
    network = repo / "port" / "network"
    owned = [network / "transport.cpp", network / "codec.cpp", network / "network.h", network / "text_chat.h",
             network / "media_profile.h", network / "optional/text_chat.cpp",
             network / "optional/media_profile.cpp", network / "tests/text_chat_transport_tests.cpp",
             Path(__file__).resolve()]
    receipt = {"schema": 1, "source_sha256": {str(p.relative_to(repo)):
                hashlib.sha256(p.read_bytes()).hexdigest() for p in owned},
               "dependency_sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                     for p in sorted(inputs)}, "cases": [], "controls": [],
               "limits": ["invented owned loopback only", "no gameplay or native Windows acceptance",
                          "checksum overhead arithmetic only; CRC wire pairing is not established",
                          "bounded-load controls, not all-network latency isolation"]}

    def command(argv, log, timeout=90):
        with log.open("ab") as output:
            try:
                result = subprocess.run(argv, env=env, stdout=output, stderr=output,
                                        timeout=timeout, preexec_fn=limits, check=False)
            except subprocess.TimeoutExpired:
                output.write(b"bounded command timed out\n")
                return 124
            except OSError:
                output.write(b"bounded command did not launch\n")
                return 127
        return result.returncode

    widths = ("32", "64") if args.word_size == "both" else (args.word_size,)
    artifacts = {}
    for width in widths:
        target = work / ("deps-" + width); target.mkdir()
        log = work / ("deps-" + width + ".log")
        features = ("HAS_FCNTL", "HAS_POLL", "HAS_SOCKLEN_T", "HAS_INET_PTON",
                    "HAS_INET_NTOP", "HAS_GETADDRINFO", "HAS_GETNAMEINFO", "HAS_MSGHDR_FLAGS")
        objects = []
        ok = True
        for name in ("callbacks", "compress", "host", "list", "packet", "peer", "protocol", "unix"):
            obj = target / (name + ".o")
            argv = ["clang", "-std=c99", "-m" + width, "-O0", "-g0", "-D_GNU_SOURCE",
                    "-I" + str(enet / "include"), *("-D" + feature for feature in features),
                    "-c", str(enet / (name + ".c")), "-o", str(obj)]
            if command(argv, log) != 0:
                ok = False
                break
            objects.append(obj)
        library = target / "libenet.a"
        if ok: ok = command(["ar", "rcs", str(library), *(str(obj) for obj in objects)], log) == 0
        monocypher = work / ("monocypher-" + width + ".o")
        if ok: ok = command(["clang", "-std=c99", "-m" + width, "-O1", "-g0", "-c",
                             str(mono / "monocypher.c"), "-o", str(monocypher)], log) == 0
        if not ok or not library.is_file():
            receipt["cases"].append({"label": "dependency-build", "word_size": width,
                                     "passed": False, "log": str(log)})
            continue
        artifacts[width] = (library, monocypher)

    def fixture(label, width, fixture_path, control=False):
        log = work / (label + "-" + width + ".log")
        exe = work / (label + "-" + width)
        library, monocypher = artifacts[width]
        argv = ["clang++", "-std=c++17", "-m" + width, "-Wall", "-Wextra", "-Werror",
                "-O0", "-g0", "-DCE_NETWORK_TEXT=1", "-I" + str(enet / "include"),
                "-I" + str(mono), str(fixture_path), str(network / "codec.cpp"), str(network / "optional/text_chat.cpp"),
                str(network / "optional/media_profile.cpp"), str(library), str(monocypher),
                "-pthread", "-o", str(exe)]
        result = {"label": label, "word_size": width, "compiled": False,
                  "executed": False, "passed": False, "log": str(log)}
        result["compile_exit"] = command(argv, log)
        result["compiled"] = result["compile_exit"] == 0
        if not result["compiled"]: return result
        result["run_exit"] = command([str(exe)], log, timeout=60)
        result["executed"] = True
        match = re.search(r"^checks=(\d+) failures=(\d+)$", log.read_text(), re.MULTILINE)
        if match:
            result["checks"], result["failures"] = map(int, match.groups())
            result["passed"] = (result["run_exit"] == (1 if control else 0) and
                                (result["failures"] > 0 if control else
                                 result["checks"] > 0 and result["failures"] == 0))
        return result

    for width in artifacts:
        receipt["cases"].append(fixture("positive", width, network / "tests/text_chat_transport_tests.cpp"))
    if not args.no_controls and len(artifacts) == len(widths) and all(r["passed"] for r in receipt["cases"]):
        original = (network / "transport.cpp").read_text()
        mutations = {
        'state-steals-text-pending': ('void FreePacket(ENetPacket *p)\n{', 'void FreePacket(ENetPacket *p)\n{\n    auto *text_owner=static_cast<CE_NetSession *>(p->userData);\n    if(text_owner&&(p->flags&ENET_PACKET_FLAG_UNSEQUENCED)&&text_owner->textPending)--text_owner->textPending;'),
        'close-keeps-text-ring': ('void CloseTransport(CE_NetSession *s, bool preserveCompleted)\n{\n    if(!s)return;\n#if defined(CE_NETWORK_TEXT)\n    ClearTextMessages(s);\n#endif', 'void CloseTransport(CE_NetSession *s, bool preserveCompleted)\n{\n    if(!s)return;'),

            "priority-removed": (
                " ||\n        !enet_list_empty(&s->peer->outgoingCommands) ||\n        !enet_list_empty(&s->peer->outgoingSendReliableCommands)", ""),
            "critical-reserve-removed": ("s->pendingPackets + 1 > kPendingPackets - reserve ||\n        ", ""),
            "send-budget-removed": ("if (!s->textSendBudget.Allows(now)) return CE_NET_TEXT_RATE_LIMITED;", ""),
            "mtu-admission-removed": ("if (size > maximum || size > CE_NET_TEXT_MAX_FRAME)",
                                      "(void)maximum; if (size > CE_NET_TEXT_MAX_FRAME)"),
            "generic-send-bypass": ("if (kind == CE_NET_TEXT) return 0;", ""),
            "full-queue-hides-malformation": (
                "    CeTextChat::Decoded decoded{};\n    const auto result = CeTextChat::Decode(plain, n, decoded);",
                "    if (s->textCount == CE_NET_TEXT_RECEIVE_CAPACITY) return;\n"
                "    CeTextChat::Decoded decoded{};\n    const auto result = CeTextChat::Decode(plain, n, decoded);"),
        }
        if args.control:
            unknown = set(args.control) - set(mutations)
            if unknown:
                parser.error('unknown control')
            mutations = {name: value for name, value in mutations.items() if name in args.control}
        for label, (before, after) in mutations.items():
            if original.count(before) != 1:
                receipt["controls"].append({"label": label, "passed": False, "anchor_error": True})
                continue
            variant = work / label / "network"; (variant / "tests").mkdir(parents=True)
            for name in ("network.h", "ice_session.h", "ice_bridge.h", "media_profile.h", "text_chat.h"):
                shutil.copyfile(network / name, variant / name)
            (variant / "transport.cpp").write_text(original.replace(before, after, 1))
            test = variant / "tests/text_chat_transport_tests.cpp"
            shutil.copyfile(network / "tests/text_chat_transport_tests.cpp", test)
            for width in artifacts:
                receipt["controls"].append(fixture(label, width, test, control=True))

    receipt["passed"] = len(artifacts) == len(widths) and all(r["passed"] for section in
                        ("cases", "controls") for r in receipt[section])
    result = work / "result.private.json"; result.write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": receipt["passed"], "positive_cases": len(receipt["cases"]),
                      "control_cases": len(receipt["controls"]), "receipt": str(result)}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
