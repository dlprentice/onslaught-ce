#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual remote Flush and native mouse-look predicate with controlled receiver boundaries.

Runs authored CPU controls only. The real InputQueue, ValidInput, action order and Flush bodies are extracted
unchanged; the receiver boundary models the native morph state assignment and adverse state changes. Native
actors, games, devices and transport are not instantiated. This is not native numerical or gameplay acceptance.
Counterfactual builds remove each relevant safeguard and must fail the corresponding observation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from network_world_tests import actual_body

ROOT=Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def one(text, old, new):
    if text.count(old)!=1:
        raise ValueError("the selected actual source seam is no longer unique")
    return text.replace(old,new)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,help="explicit alternative adapter source for review")
    parser.add_argument("--compiler",default="clang++",help="host C++ compiler; no game ABI or FP claim")
    args=parser.parse_args()
    compiler=shutil.which(args.compiler)
    if not compiler:
        raise SystemExit("ground-aim tests: an existing host C++ compiler is required")
    source=(args.source or ROOT/"port/game/ce_network_world.cpp").resolve()
    native=ROOT/"src/BattleEngine.cpp"
    world=source.read_text(); battle=native.read_text()
    source_hash=sha(source); native_hash=sha(native)
    header=ROOT/"port/game/ce_network_world.h"; header_hash=sha(header)
    morph=actual_body(battle,"void CBattleEngine::Morph()")
    if "mState=BATTLE_ENGINE_STATE_MORPHING_INTO_JET;" not in morph:
        raise ValueError("native morph no longer supports the selected receiver-boundary transition")
    order=world.index("const uint32_t native_actions[]=")
    order_end=world.index("\nstruct LocalInput",order)
    snippets={
        "action_order":world[order:order_end],
        "valid_input":actual_body(world,"bool ValidInput("),
        "input_queue":actual_body(world,"struct InputQueue"),
        "native_eligibility":actual_body(battle,"static inline BOOL MouseLookActive("),
        "flush":actual_body(world,"    static int Flush(unsigned player)"),
    }
    # These mutations are local authored counterfactuals, never production changes.
    variants={
        "fixed":(snippets["flush"],None),
        "old-pre-dispatch-eligibility":(one(snippets["flush"],"&&e->IsWalkerOnGroundOrObject()&&!GAME.IsPaused()",""),"morphing engine"),
        "missing-post-dispatch-pause":(one(snippets["flush"],"&&e->IsWalkerOnGroundOrObject()&&!GAME.IsPaused()","&&e->IsWalkerOnGroundOrObject()"),"post-dispatch ineligible"),
        "missing-consumption":(one(snippets["flush"],"commands.Consumed();",""),"consumed exactly once"),
        "wrong-player":(one(snippets["flush"],"GAME.GetPlayer(1)","GAME.GetPlayer(0)"),"ordinary grounded P2"),
        "missing-controller-owner":(one(snippets["flush"],"||GAME.GetController(1)->GetToControl()!=p",""),"wrong controller owner"),
        "missing-initial-pause":(one(snippets["flush"],"||GAME.IsPaused()||","||"),"paused input is retained"),
        "missing-replica-fence":(one(snippets["flush"],"if (config.role == CE_WORLD_REPLICA) return 1;",""),"replica flush"),
    }
    parent=ROOT/"local-data/port/network-ground-aim-tests";parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix="run-",dir=parent))
    template=ROOT/"port/tools/tests/network_ground_aim_driver.cpp"
    driver=template.read_text()
    pins={key:hashlib.sha256(value.encode()).hexdigest() for key,value in snippets.items()}
    receipt={"source_sha256":source_hash,"native_source_sha256":native_hash,"world_header_sha256":header_hash,
             "native_morph_body_sha256":hashlib.sha256(morph.encode()).hexdigest(),"driver_sha256":sha(template),
             "compiler_sha256":sha(Path(compiler)),"snippet_sha256":pins,"variants":[],"limits":__doc__,"passed":False}
    for name,(flush,failure) in variants.items():
        folder=out/name;folder.mkdir()
        text=driver
        for key,value in dict(snippets,flush=flush).items():
            text=one(text,"/* ACTUAL_"+key.upper()+" */",value)
        cpp=folder/"driver.cpp";cpp.write_text(text);binary=folder/"driver"
        command=[compiler,"-std=c++14","-O0","-Wall","-Wextra","-I"+str(ROOT/"port/game"),str(cpp),"-o",str(binary)]
        built=subprocess.run(command,capture_output=True,text=True,timeout=30)
        (folder/"build.stdout").write_text(built.stdout);(folder/"build.stderr").write_text(built.stderr)
        row={"name":name,"cpp_sha256":sha(cpp),"compile_exit":built.returncode,"expected_failure":failure}
        receipt["variants"].append(row)
        if built.returncode:
            (out/"result.json").write_text(json.dumps(receipt,indent=2)+"\n")
            raise RuntimeError("ground-aim fixture did not compile: "+name)
        run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
        (folder/"run.stdout").write_text(run.stdout);(folder/"run.stderr").write_text(run.stderr)
        row.update(exit=run.returncode,binary_sha256=sha(binary))
        row["passed"]=(run.returncode==0) if failure is None else (run.returncode==2 and failure in run.stderr)
        if failure is None and row["passed"]:
            row["observations"]=json.loads(run.stdout)
        if not row["passed"]:
            (out/"result.json").write_text(json.dumps(receipt,indent=2)+"\n")
            raise RuntimeError("ground-aim causal control failed: "+name+" "+run.stderr)
        print(name+": PASS",flush=True)
    if sha(source)!=source_hash or sha(native)!=native_hash or sha(header)!=header_hash or sha(template)!=receipt["driver_sha256"]:
        raise RuntimeError("ground-aim fixture inputs changed during execution")
    receipt.update(source_unchanged=True,passed=True)
    (out/"result.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print(out/"result.json",flush=True)
    return 0


if __name__=="__main__":
    raise SystemExit(main())
