#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual bounded delivery/cache/codec controls with a Session-shaped admission caller; no game/device/VM runs.

Optional frozen authored World9 fixtures execute actual Encode/Decode and pure LifeReadiness. Their native resource
preflight and complete WorldApply do not execute. Real Session integration/application ACK proof remains a later gate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import re
import shutil
import struct
import subprocess
import tempfile

import deps

ROOT = Path(__file__).resolve().parents[2]
CASES = ("basic", "slow-ack", "lost-offer", "coalesced-offer", "deferred", "preserve", "generation-send0", "repair",
         "repair-lifecycle-overtake", "bad-terminal", "wire", "loss-schedules", "terminal-pairs", "terminal-prepared", "leave-orders", "cadence")
TERMINAL_CASES = ("terminal-pairs", "terminal-prepared", "leave-orders", "repair")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_span(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 0
    for i in range(opening, len(text)):
        depth += (text[i] == "{") - (text[i] == "}")
        if not depth:
            return text[start:i + 1] + (";" if marker.startswith("struct ") else "")
    raise RuntimeError("unclosed selected production function")


def carrier_fixture(text):
    """Exact selected transport assembly functions; authored authenticated-header/OS-failure boundaries, not ENet."""
    constants = {name: int(re.search(r"\b" + name + r"\s*=\s*(\d+)", text).group(1))
                 for name in ("kChannels", "kChunk", "kQueue", "kStateAssemblyMs")}
    pending = re.search(r"kPendingBytes\s*=\s*(\d+)\s*\*\s*(\d+)", text)
    silence = int(re.search(r"handshakeMs\s*=\s*\d+,\s*silenceMs\s*=\s*(\d+)", text).group(1))
    definitions = "\n".join(source_span(text, marker) for marker in (
        "struct Header {", "struct Assembly {", "struct Queued {", "uint32_t Channel("))
    functions = "\n".join(source_span(text, marker) for marker in (
        "void RetireState(", "bool AssemblyExpired(", "bool Assemble("))
    return '''// SPDX-License-Identifier: GPL-3.0-only
#include "network.h"
#include <algorithm>
#include <array>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
''' + "\n".join(f"constexpr uint32_t {k}={v};" for k, v in constants.items()) + f'''
constexpr uint32_t kParts=(CE_NET_MAX_MESSAGE+kChunk-1)/kChunk,kPendingBytes={int(pending[1])*int(pending[2])};
''' + definitions + f'''
struct CE_NetSession {{
    uint32_t silenceMs={silence},retiredState=0,head=0,count=0,queuedBytes=0;
    CE_NetStatus status={{}};bool failed=false;std::array<Assembly,kChannels> assembly;
    std::array<uint32_t,kChannels> complete{{}};std::array<Queued,kQueue> queue;
    CE_NetSession() {{status.epoch=7;}}
}};
void Reject(CE_NetSession *s) {{++s->status.rejected_packets;}}
void Fail(CE_NetSession *s,uint32_t why) {{s->failed=true;s->status.error=why;}}
''' + functions + r'''
unsigned checks=0;
void Require(bool ok) {++checks;if(!ok){std::fprintf(stderr,"selected transport assembly assertion failed\n");std::exit(2);}}
Header H(uint32_t kind,uint32_t id,uint32_t part,uint32_t total) {
    Header h={};h.kind=kind;h.channel=Channel(kind);h.epoch=7;h.tick=140;h.sequence=part+1;h.message=id;h.total=total;
    h.part=part;h.parts=(total+kChunk-1)/kChunk;return h;
}
int main() {
    std::array<uint8_t,16081> raw{};for(unsigned i=0;i<raw.size();++i)raw[i]=uint8_t(i);
    CE_NetSession superseded;
    Require(Assemble(&superseded,H(CE_NET_STATE,10,0,raw.size()),raw.data(),1000,0));
    Require(Assemble(&superseded,H(CE_NET_STATE,11,0,2501),raw.data(),1000,50));
    Require(superseded.retiredState==10&&!superseded.count);
    Require(!Assemble(&superseded,H(CE_NET_STATE,10,1,raw.size()),raw.data()+1000,1000,60));
    CE_NetSession expired;
    for(unsigned part=0;part<17;++part) {
        const unsigned offset=part*1000,n=std::min(1000u,unsigned(raw.size())-offset);
        const bool accepted=Assemble(&expired,H(CE_NET_STATE,10,part,raw.size()),raw.data()+offset,n,part*20);
        Require(part<13?accepted:!accepted);
    }
    Require(expired.retiredState==10&&!expired.count&&!expired.failed);
    CE_NetSession reliable;
    for(unsigned part=0;part<17;++part) {
        const unsigned offset=part*1000,n=std::min(1000u,unsigned(raw.size())-offset);
        Require(Assemble(&reliable,H(CE_NET_BASELINE,10,part,raw.size()),raw.data()+offset,n,part*20));
    }
    Require(reliable.count==1&&!reliable.failed&&reliable.queue[0].meta.kind==CE_NET_BASELINE&&
        reliable.queue[0].data.size()==raw.size()&&!std::memcmp(reliable.queue[0].data.data(),raw.data(),raw.size()));
    CE_NetSession timeout;
    Require(Assemble(&timeout,H(CE_NET_BASELINE,10,0,raw.size()),raw.data(),1000,0));
    Require(!Assemble(&timeout,H(CE_NET_BASELINE,10,1,raw.size()),raw.data()+1000,1000,timeout.silenceMs));
    Require(timeout.failed&&timeout.status.error==CE_NET_TIMEOUT&&!timeout.count);
    std::printf("{\"checks\":%u,\"state_supersession\":true,\"state_250ms_expiry\":true,\"reliable_320ms_complete\":true,\"reliable_deadline_bounded\":true}\n",checks);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--syntax-targets", action="store_true")
    parser.add_argument("--world-fixture", type=Path, help="existing frozen authored World9 closure, read only")
    parser.add_argument("--transport-source", type=Path, help="frozen transport.cpp assembly functions, read-only source admission")
    parser.add_argument("--no-mutants", action="store_true")
    parser.add_argument("--terminal-only", action="store_true", help="focused terminal semantic/no-mutation controls")
    parser.add_argument("--terminal-before-ref", help="frozen source for the terminal-identity negative")
    parser.add_argument("--terminal-witness", type=Path, help="preserved immutable review witness, compiled unchanged before/current")
    args = parser.parse_args()
    if bool(args.terminal_before_ref) != bool(args.terminal_witness):
        parser.error("--terminal-before-ref and --terminal-witness are selected together")
    if args.terminal_only and (args.world_fixture or args.transport_source):
        parser.error("--terminal-only cannot select the broader World/carrier fixture gates")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/state-delivery-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    stage = out / "production"
    stage.mkdir()
    inputs = {}
    for rel in ("network/state_delivery.h", "network/state_delivery.cpp", "network/state_delta.h", "network/state_delta.cpp",
                "network/network.h", "tools/tests/state_delivery_driver.cpp"):
        src = ROOT / "port" / rel
        inputs[str(src)] = sha(src)
        shutil.copyfile(src, stage / src.name)
    inputs[str(Path(__file__))] = sha(Path(__file__))
    for name in ("monocypher.c", "monocypher.h"):
        src = deps.SRC / "monocypher-4.0.2/src" / name
        if not src.is_file():
            raise RuntimeError("pinned existing Monocypher required; no download/install")
        inputs[str(src)] = sha(src)
        shutil.copyfile(src, stage / name)
    cc, cxx = (deps.DATA / "llvm/bin" / n for n in ("clang", "clang++"))
    inputs[str(cxx)] = sha(cxx)
    env = dict(os.environ, TMPDIR=str(out), ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:disable_coredump=1")
    for key in tuple(env):
        if key.startswith("BEA_"):
            env.pop(key)
    result = {"scope": __doc__, "inputs": inputs, "commands": [], "stages": [], "cases": [], "mutants": [],
              "limits": ["Whole-Apply test callback is controlled; not real native WorldApply/Session integration.",
                         "Repair quiet-WAIT/lifecycle proof is an explicit caller obligation; no World clocks or lifecycle reset.",
                         "No real remote peer, ENet/game/VM/GPU/native Windows run; no cap increase or shared-module edit."]}

    def save():
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")

    def run(label, command, expected=0):
        result["commands"].append([str(x) for x in command])
        p = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
        (out / (label + ".stdout")).write_text(p.stdout)
        (out / (label + ".stderr")).write_text(p.stderr)
        result["stages"].append({"name": label, "exit": p.returncode, "passed": p.returncode == expected})
        save()
        if p.returncode != expected:
            raise RuntimeError(label + " refused; see " + str(out))
        return p

    flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    run("crypto-build", [str(cc), "-std=c99", *flags, "-I" + str(stage), "-c", str(stage / "monocypher.c"), "-o", str(out / "crypto.o")])

    def build(label, source, driver=None):
        binary = out / (label + "-driver")
        run(label + "-build", [str(cxx), "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags, "-I" + str(stage),
                              str(stage / "state_delta.cpp"), str(source), str(driver or stage / "state_delivery_driver.cpp"),
                              str(out / "crypto.o"), "-o", str(binary)])
        return binary

    binary = build("production", stage / "state_delivery.cpp")
    if args.terminal_before_ref:
        witness = args.terminal_witness.resolve()
        inputs[str(witness)] = sha(witness)
        retained = []
        for include in re.findall(r'^#include "([^"]+)"', witness.read_text(), flags=re.M):
            path = Path(include)
            if path.is_absolute():
                inputs[str(path)] = sha(path)
                retained.append(path)
                for name in ("state_delivery.h", "state_delta.h"):
                    header = path.parent / name
                    if header.is_file():
                        inputs[str(header)] = sha(header)
        before_source = out / "terminal-before.cpp"
        p = subprocess.run(["git", "show", args.terminal_before_ref + ":port/network/state_delivery.cpp"], cwd=ROOT,
                           capture_output=True, text=True, check=True, timeout=10)
        before_source.write_text(p.stdout)
        old_receipt = witness.parent / "terminal-result.json"
        if old_receipt.is_file():
            inputs[str(old_receipt)] = sha(old_receipt)
            recorded = json.loads(old_receipt.read_text())
            if recorded["source_sha256"] != sha(before_source) or recorded["driver_sha256"] != sha(witness):
                raise RuntimeError("preserved reviewer witness must use its exact recorded source/driver")
        before = build("terminal-before", before_source, witness)
        p = run("terminal-before-witness", [str(before)], expected=2)
        observed_before = json.loads(p.stdout)
        current = build("terminal-current", stage / "state_delivery.cpp", witness)
        p = run("terminal-current-witness", [str(current)])
        observed_current = json.loads(p.stdout)
        if (observed_before["terminal_bad_ack"], observed_before["terminal_bad_status"]) != (2, 2) or any(
                observed_current[name] != -1 for name in ("normal_bad_ack", "terminal_bad_ack", "normal_bad_status", "terminal_bad_status")):
            raise RuntimeError("preserved witness must discriminate old terminal bypass from corrected admission")
        result["terminal_witness"] = {"ref": args.terminal_before_ref, "source_sha256": sha(before_source),
                                      "witness_sha256": sha(witness), "before": observed_before, "current": observed_current}
    for case in TERMINAL_CASES if args.terminal_only else CASES:
        p = run(case, [str(binary), case])
        result["cases"].append(json.loads(p.stdout))
    p = run("independent-key-wire-vector", [str(binary), "key-vector"])
    vector = json.loads(p.stdout)
    context = bytes((1, 2, 3, 4)) + bytes(28)
    image = b"AUT9" + struct.pack(">IIIII", 7, 9, 151, 0, 1) + bytes(i % 251 + 1 for i in range(24, 512))
    image_digest = hashlib.blake2b(b"CE-state-image-v1\0" + context + struct.pack(">III", 7, 9, 512) + image, digest_size=32).digest()
    delivery_digest = hashlib.blake2b(b"CE-state-applied-v1\0" + context + struct.pack(">IIII", 7, 9, 151, 512) + image_digest, digest_size=32).digest()
    key = context + struct.pack(">IIII", 7, 9, 151, 512) + delivery_digest
    control = b"CDA1\x01\x01\0\0" + struct.pack(">I", 3) + bytes(12) + key + bytes(80)
    if vector != {"image_digest": image_digest.hex(), "delivery_digest": delivery_digest.hex(), "wire": control.hex()}:
        raise RuntimeError("independent image/tick identity or complete control wire vector mismatch")
    result["independent_vector"] = vector

    if args.transport_source:
        source = args.transport_source.resolve()
        inputs[str(source)] = sha(source)
        fixture_source = out / "selected-carrier.cpp"
        fixture_source.write_text(carrier_fixture(source.read_text()))
        result["carrier_extraction_sha256"] = sha(fixture_source)
        carrier = out / "selected-carrier"
        run("selected-carrier-build", [str(cxx), "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
                                      "-I" + str(stage), str(fixture_source), "-o", str(carrier)])
        p = run("selected-carrier", [str(carrier)])
        result["selected_carrier"] = json.loads(p.stdout)
        result["limits"].append("Selected carrier assembly functions run behind authored authenticated-header and Fail/Reject boundaries; no AEAD/ENet/socket execution.")

    if not args.no_mutants:
        source = (stage / "state_delivery.cpp").read_text()
        mutants = (
            ("one-second-offer-cadence", (("now-s->last_offer_at>=CE_STATE_DELIVERY_OFFER_MS", "now-s->last_offer_at>=1000"),), "cadence"),
            ("one-second-status-cadence", (("now-s->status_at<CE_STATE_DELIVERY_STATUS_MS", "now-s->status_at<1000"),), "cadence"),
            ("abandon-pending-offer", (("const bool offer=!s->pending &&", "const bool offer="),), "slow-ack"),
            ("ack-before-world-apply", (("if(offer>r->highest_offer) r->highest_offer=offer;*out=r->ticket;", "if(offer>r->highest_offer) r->highest_offer=offer;*out=r->ticket; CE_StateReceiverApplied(r,&r->ticket,r->epoch,target.sequence,target.tick,now);"),), "deferred"),
            ("ignore-retirement-fence", (("||(offer&&offer<=r->retired_offer)", ""), ("if(offer&&offer<=r->highest_offer) return CE_DELIVERY_INVALID;", "")), "lost-offer"),
            ("emit-state-during-repair", (("if(s->repair_required||s->repair_waiting)", "if(false)"),), "repair"),
            ("forget-raw-active-base", (("const bool used_candidate=r->slots[r->c].valid&&Equal(r->prepared_base,r->slots[r->c].key);", "const bool used_candidate=false;"),), "repair"),
            ("staged-old-basis-commit", (("++s->revision; Invalidate(s);", "++s->revision;"), ("s->prepared_revision==s->revision", "true")), "generation-send0"),
            ("blind-old-channel-coalescing", (("(r->prepared&&target.sequence<=r->ticket.key.sequence)||", ""), ("if(r->expect_repair) return CE_DELIVERY_RETRY;", "")), "repair"),
            ("sender-terminal-bypass", (("const int admission=SenderControlAdmission(s,*c);", "if(terminal) return CE_DELIVERY_STALE;const int admission=SenderControlAdmission(s,*c);"),), "terminal-pairs"),
            ("receiver-terminal-bypass", (("uint32_t kind=0;const int admission=ReceiverControlAdmission(r,*c,kind);", "if(terminal) return CE_DELIVERY_STALE;uint32_t kind=0;const int admission=ReceiverControlAdmission(r,*c,kind);"),), "terminal-pairs"),
            ("sender-terminal-clock", (("if(!terminal) s->now=now;", "s->now=now;"),), "terminal-pairs"),
            ("receiver-terminal-clock", (("if(!terminal) r->now=now;", "r->now=now;"),), "terminal-pairs"),
        )
        for label, changes, case in mutants:
            if args.terminal_only and label not in ("sender-terminal-bypass", "receiver-terminal-bypass", "sender-terminal-clock", "receiver-terminal-clock"):
                continue
            changed = source
            for old, new in changes:
                if changed.count(old) != 1:
                    raise RuntimeError("mutation seam changed: " + label)
                changed = changed.replace(old, new)
            path = out / (label + ".cpp")
            path.write_text(changed)
            mutant = build(label, path)
            p = run(label, [str(mutant), case], expected=2)
            if not p.stderr.startswith("FAIL:"):
                raise RuntimeError("mutant must be killed by a causal assertion")
            result["mutants"].append({"name": label, "case": case, "reason": p.stderr.strip(), "killed": True})

    if args.syntax_targets:
        for target in ("linux", "windows"):
            compiler = cxx if target == "linux" else deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang++"
            opts = ["--target=i686-pc-linux-gnu", "--sysroot=" + str(deps.SYSROOT)] if target == "linux" else []
            run(target + "-syntax", [str(compiler), *opts, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                                     "-I" + str(stage), str(stage / "state_delivery.cpp")])

    if args.world_fixture:
        fixture = args.world_fixture.resolve()
        recorded = json.loads((fixture / "result.json").read_text())
        command = recorded["compile"].copy()
        for rel, expected in recorded["inputs"].items():
            source = fixture / rel
            if not source.is_file() or sha(source) != expected:
                raise RuntimeError("frozen authored World closure changed: " + rel)
            inputs[str(source)] = expected
        for name in ("network_world_driver.cpp", "network_world_lifecycle.cpp", "production/game/ce_network_world.cpp",
                     "production/game/ce_network_world.h", "production/network/codec.cpp", "linux-typeinfo-bridge.o", "result.json"):
            inputs[str(fixture / name)] = sha(fixture / name)
        native_flags = ["--target=i686-pc-linux-gnu", "--sysroot=" + str(deps.SYSROOT), "-std=c++17", "-O1", "-g", "-I" + str(stage)]
        objects = []
        for name in ("state_delta.cpp", "state_delivery.cpp", "monocypher.c"):
            obj = out / ("world-" + name + ".o")
            tool = cc if name.endswith(".c") else cxx
            opts = [x for x in native_flags if not x.startswith("-std=")] + ["-std=c99"] if name.endswith(".c") else native_flags
            run("world-native-" + name, [str(tool), *opts, "-c", str(stage / name), "-o", str(obj)])
            objects.append(str(obj))
        original_driver = str(fixture / "network_world_lifecycle.cpp")
        if command.count(original_driver) != 1:
            raise RuntimeError("frozen World fixture compile closure changed")
        command[command.index(original_driver)] = str(stage / "state_delivery_driver.cpp")
        command[command.index("-o") + 1] = str(out / "world-delivery-driver")
        command += ['-DCE_DELIVERY_WORLD_FIXTURE="' + str(fixture / "network_world_driver.cpp") + '"', "-I" + str(stage), *objects]
        run("actual-world-build", command)
        p = run("actual-world-admission", [str(out / "world-delivery-driver"), "actual-world-admission"])
        result["actual_world"] = json.loads(p.stdout)

    result["inputs_after"] = {path: sha(Path(path)) for path in inputs}
    result["passed"] = result["inputs_after"] == inputs and all(x["passed"] for x in result["stages"])
    save()
    print(json.dumps({"receipt": str(out / "result.json"), "passed": result["passed"], "cases": result["cases"],
                      "mutants": len(result["mutants"]), "actual_world": result.get("actual_world")}, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
