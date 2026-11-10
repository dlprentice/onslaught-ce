#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual mission-objective calls through the actual world capture/codec/apply bodies.

The player/actor, lifetime, renderer and transport boundaries are controlled. Objective inputs are
authored fixture values, not observations of any level's mission script. No game or assets are run.
The normal adapter test separately compiles the whole production codec against actual native headers.
"""
import hashlib
import json
from pathlib import Path
import resource
import shutil
import subprocess
import tempfile
import deps
from network_compatibility_admission_tests import golden, FILES
from network_world_tests import actual_body

ROOT = Path(__file__).resolve().parents[2]


def digest(data):
    return hashlib.sha256(data.encode()).hexdigest()


def once(text, before, after):
    if text.count(before) != 1:
        raise ValueError("source seam is missing or ambiguous: " + before)
    return text.replace(before, after, 1)


def handshake(out, receipt):
    # Independent golden transcripts are also checked against the real fresh-process admission in
    # network_compatibility_admission_tests.py. Only the schema differs in these authored contexts.
    source = (ROOT / "port/tools/tests/network_objective_handshake.cpp.in").read_text()
    for schema in (10, 11):
        value = bytes.fromhex(golden(FILES, world_schema=schema))
        source = once(source, "/* WORLD" + str(schema) + "_DIGEST */", ",".join(str(x) for x in value))
    cpp = out / "handshake.cpp"
    cpp.write_text(source)
    mc = deps.SRC / "monocypher-4.0.2/src"
    enet = deps.SRC / "enet-5a9c537fd464b3c6d3c55e1d3bd47588faf71b42/include"
    crypto = out / "monocypher.o"
    cc = shutil.which("clang")
    if not cc or not (mc / "monocypher.c").is_file() or not (enet / "enet/enet.h").is_file():
        raise RuntimeError("existing pinned ENet headers and Monocypher source are required")
    c = subprocess.run([cc, "-std=c99", "-O1", "-fsanitize=address,undefined", "-c", str(mc / "monocypher.c"), "-o", str(crypto)], capture_output=True, text=True, timeout=60)
    cmd = [shutil.which("clang++"), "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-ffunction-sections", "-fdata-sections",
           "-Wl,--gc-sections", "-I" + str(ROOT / "port/network"), "-I" + str(enet), "-I" + str(mc),
           '-DCE_TRANSPORT_SOURCE="' + str(ROOT / "port/network/transport.cpp") + '"',
           str(cpp), str(ROOT / "port/network/codec.cpp"), str(crypto), "-o", str(out / "handshake")]
    built = subprocess.run(cmd, capture_output=True, text=True, timeout=60) if c.returncode == 0 else None
    run = subprocess.run([str(out / "handshake")], capture_output=True, text=True, timeout=10) if built and built.returncode == 0 else None
    passed = bool(run and run.returncode == 0)
    (out / "handshake.build.log").write_text(c.stdout + c.stderr + (built.stdout + built.stderr if built else ""))
    receipt["handshake"] = {"source_sha256": digest(source), "transport_sha256": hashlib.sha256((ROOT / "port/network/transport.cpp").read_bytes()).hexdigest(),
                            "crypto_sha256": hashlib.sha256((mc / "monocypher.c").read_bytes()).hexdigest(),
                            "compile_exit": built.returncode if built else c.returncode, "exit": run.returncode if run else None,
                            "stdout": run.stdout if run else "", "stderr": run.stderr if run else "", "passed": passed}
    print(("PASS " if passed else "FAIL ") + "in-memory schema handshake", flush=True)
    return passed


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    compiler = shutil.which("clang++")
    if not compiler:
        raise SystemExit("clang++ is required")
    world = (ROOT / "port/game/ce_network_world.cpp").read_text()
    native = (ROOT / "src/MissionScript/IScript.cpp").read_text()
    thing = (ROOT / "src/thing.cpp").read_text()
    game = (ROOT / "src/game.h").read_text()
    setters = []
    for name in ("SetPrimaryObjectiveComplete", "SetSecondaryObjectiveComplete", "SetPrimaryObjectiveFailed", "SetSecondaryObjectiveFailed"):
        matches = [line.strip() for line in game.splitlines() if "void" in line and name + "(" in line]
        if len(matches) != 1:
            raise ValueError("native objective setter changed: " + name)
        setters.append(matches[0])
    names = ("PrimaryObjectiveComplete", "SecondaryObjectiveComplete", "PrimaryObjectiveFailed", "SecondaryObjectiveFailed", "SetObjective", "UnsetObjective")
    core_capture = actual_body(world, "    static bool CoreCapture(CThing *thing, Core &out)")
    core_apply = actual_body(world, "    static void CoreApply(CThing *thing, const Core &in)")
    marked_capture = next(line.strip() for line in core_capture.splitlines() if "out.flags =" in line)
    marked_apply = next(line.strip() for line in core_apply.splitlines() if "thing->SetObjective(" in line)
    snippets = {
        "OBJECTIVE_STATE": actual_body(world, "struct ObjectiveState") + ";",
        "NATIVE_GAME_SETTERS": "\n".join(setters),
        "NATIVE_SCRIPT_CALLERS": "\n".join(actual_body(native, "void\tIScript::" + name + "(") for name in names),
        "NATIVE_THING_SET_OBJECTIVE": actual_body(thing, "void\tCThing::SetObjective("),
        "ACTUAL_MARKED_CAPTURE": marked_capture,
        "ACTUAL_MARKED_APPLY": marked_apply,
        "OBJECTIVE_HELPERS": "\n".join(actual_body(world, "    static " + sig) for sig in (
            "void CaptureObjectives(", "void ApplyObjectives(", "bool ObjectivesBound(")),
        "ACTUAL_CAPTURE": actual_body(world, "    static bool Capture(CE_WorldFrame &out)"),
        "ACTUAL_APPLY": actual_body(world, "    static bool Apply(const CE_WorldFrame &f)"),
        "ACTUAL_ENTRYPOINTS": "\n".join(actual_body(world, sig) for sig in (
            "int CE_WorldCapture(", "int CE_WorldApply(", "int CE_WorldEncode(", "int CE_WorldDecode(")),
    }
    capture = snippets["ACTUAL_CAPTURE"]
    apply = snippets["ACTUAL_APPLY"]
    if capture.index("CaptureObjectives(out);") > capture.index("captured_tick = out.tick"):
        raise ValueError("objective capture follows capture completion")
    if not (apply.index("if (!Validate(f))") < apply.index("ApplyScope scope;") < apply.index("ApplyObjectives(f);") < apply.index("scope.committed=true")):
        raise ValueError("objective apply left the validated native mutation scope")
    template = (ROOT / "port/tools/tests/network_objective_driver.cpp.in").read_text()
    rendered = template
    for key, value in snippets.items():
        rendered = once(rendered, "/* " + key + " */", value)
    variants = {
        "actual": (rendered, None),
        "lost-capture": (once(rendered, "        CaptureObjectives(out);", ""), "unsupported native objective refuses capture"),
        "lost-primary-apply": (once(rendered, "GAME.GetPrimaryObjective(i)->Set(in.primary[i].state,in.primary[i].string_id);", ";"), "primary endpoint"),
        "lost-secondary-apply": (once(rendered, "GAME.GetSecondaryObjective(i)->Set(in.secondary[i].state,in.secondary[i].string_id);", ";"), "secondary endpoint"),
        "lost-primary-decode": (once(rendered, "f->primary[i].state=(int32_t)CE_NetReadU32(&r);", "CE_NetReadU32(&r);"), "primary endpoint"),
        "lost-state-validation": (once(rendered, "&&ObjectivesBound(f);", ";"), "invalid state refused"),
        "lost-marker-capture": (once(rendered, "out.flags = (unsigned short)thing->mFlags;", ""), "native arena marker reaches guest"),
        "lost-marker-noticeboard": (once(rendered, "thing->SetObjective((in.flags&TF_MARKED_OBJECTIVE)!=0);", ""), "native arena marker reaches guest"),
    }
    parent = ROOT / "local-data/port/network-objective-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    receipt = {"passed": False, "limits": __doc__, "snippet_sha256": {k: digest(v) for k, v in snippets.items()}, "variants": []}
    for name, (text, expected) in variants.items():
        source = out / (name + ".cpp")
        source.write_text(text)
        binary = out / name
        command = [compiler, "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-I" + str(ROOT / "src"), "-I" + str(ROOT / "port/game"), "-I" + str(ROOT / "port/network"),
                   str(source), str(ROOT / "port/network/codec.cpp"), "-o", str(binary)]
        built = subprocess.run(command, capture_output=True, text=True, timeout=60)
        (out / (name + ".build.log")).write_text(built.stdout + built.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10) if built.returncode == 0 else None
        passed = built.returncode == 0 and run is not None and (run.returncode == 0 if expected is None else run.returncode == 2 and expected in run.stderr)
        receipt["variants"].append({"name": name, "source_sha256": digest(text), "compile_exit": built.returncode,
                                    "exit": run.returncode if run else None, "stdout": run.stdout if run else "",
                                    "stderr": run.stderr if run else "", "expected": expected, "passed": passed})
        print(("PASS " if passed else "FAIL ") + name, flush=True)
    receipt["passed"] = all(row["passed"] for row in receipt["variants"]) and handshake(out, receipt)
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
