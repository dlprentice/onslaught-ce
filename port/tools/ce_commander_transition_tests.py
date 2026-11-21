#!/usr/bin/env python3
"""Pure rendered-pose controls, independent of game or renderer state."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
def main():
    parent=ROOT/"local-data/ce-commander-transition-tests"
    parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix="run-",dir=parent))
    header=(ROOT/"port/game/ce_commander_transition.h").read_text()
    driver=ROOT/"port/tools/tests/ce_commander_transition_driver.cpp"
    variants={"control":header}
    changes={
        "snap-to-goal":("double left = 1.0-weight, right = weight;","weight = 1.0; double left = 1.0-weight, right = weight;"),
        "long-way-round":("if (dot < 0.0)","if (false)"),
        "linear-clock":("return t*t*(3.0-2.0*t);","return t;"),
        "mutate-output-on-refusal":("if (!output || !std::isfinite(weight) || !Valid(from) || !Valid(to)) return false;","if (!output || !std::isfinite(weight) || !Valid(from) || !Valid(to)) { if(output) *output = from; return false; }")
    }
    for name,(old,new) in changes.items():
        assert header.count(old)==1,name
        variants[name]=header.replace(old,new)
    results={}
    for name,text in variants.items():
        folder=out/name;folder.mkdir();(folder/"ce_commander_transition.h").write_text(text)
        binary=folder/"checks"
        command=["g++","-std=c++17","-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer","-I",str(folder),str(driver),"-o",str(binary)]
        c=subprocess.run(command,capture_output=True,text=True);(folder/"compile.private.log").write_text(c.stdout+c.stderr)
        if c.returncode:results[name]={"compiled":False};break
        env=os.environ.copy();env["ASAN_OPTIONS"]="detect_leaks=1:halt_on_error=1";env["UBSAN_OPTIONS"]="halt_on_error=1"
        r=subprocess.run([str(binary)],env=env,capture_output=True,text=True);(folder/"run.private.log").write_text(r.stdout+r.stderr)
        results[name]={"compiled":True,"exit":r.returncode,"sanitizer_error":"Sanitizer" in r.stderr or "runtime error:" in r.stderr}
        if name=="control":
            m=re.search(r"checks=(\d+) failures=(\d+) skips=(\d+)",r.stdout);results[name]["counts"]=list(map(int,m.groups())) if m else None
            if r.returncode:break
    passed=len(results)==len(variants) and results["control"].get("exit")==0 and all(v.get("compiled") and v.get("exit",0)!=0 and not v.get("sanitizer_error") for k,v in results.items() if k!="control")
    receipt={"passed":passed,"header_sha256":hashlib.sha256(header.encode()).hexdigest(),"results":results,"scope":"authored pose arithmetic; no native motion or ownership claim"}
    (out/"RESULT.private.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print(json.dumps({"passed":passed,"checks":results["control"].get("counts"),"mutants":len(changes),"receipt":str(out/"RESULT.private.json")}))
    return 0 if passed else 1
if __name__=="__main__":raise SystemExit(main())
