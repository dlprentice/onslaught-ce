#!/usr/bin/env python3
"""Authored identity lifetime checks, with no native addresses or game data."""
from pathlib import Path
import hashlib,json,os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
def main():
    parent=ROOT/"local-data/ce-command-lifetime-tests";parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix="run-",dir=parent))
    header=(ROOT/"port/game/ce_command_lifetime.h").read_text()
    driver=ROOT/"port/tools/tests/ce_command_lifetime_driver.cpp"
    changes={
        "old-world-accepted":("key.world != world_","false"),
        "old-generation-accepted":("e.ref.key.generation == key.generation","true"),
        "destruction-retains-key":("e.live = false;\n            removed = true;","e.live = true;\n            removed = true;"),
        "same-id-no-new-generation":("{world_, id, next_}","{world_, id, 1}"),
        "capacity-overwrites-selection":("if (!available || !next_) return false;","if (!available) available = &entries_[0]; if (!next_) return false;"),
        "epoch-wrap-reuses-world":("if (world_) ++world_;","if (++world_ == 0) world_ = 1;"),
        "generation-wrap-reuses-key":("++next_; // Zero", "if (++next_ == 0) next_ = 1; // Zero"),
        "null-address-registered":("if (!address || !world_) return false;","if (!world_) return false;"),
        "kind-change-keeps-generation":("e.ref.id == id && e.ref.kind == kind","e.ref.id == id"),
        "destruction-forgets-neighbours":("if (e.live && e.ref.address == address) {\n            e.live = false;","if (e.live) {\n            e.live = false;")
    }
    variants={"control":header}
    for name,(old,new) in changes.items():
        assert header.count(old)==1,name
        variants[name]=header.replace(old,new)
    results={}
    for name,text in variants.items():
        folder=out/name;folder.mkdir();(folder/"ce_command_lifetime.h").write_text(text)
        binary=folder/"checks"
        c=subprocess.run(["g++","-std=c++17","-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer","-I",str(folder),str(driver),"-o",str(binary)],capture_output=True,text=True)
        (folder/"compile.private.log").write_text(c.stdout+c.stderr)
        if c.returncode:results[name]={"compiled":False};break
        env=os.environ.copy();env["ASAN_OPTIONS"]="detect_leaks=1:halt_on_error=1";env["UBSAN_OPTIONS"]="halt_on_error=1"
        r=subprocess.run([str(binary)],env=env,capture_output=True,text=True);(folder/"run.private.log").write_text(r.stdout+r.stderr)
        results[name]={"compiled":True,"exit":r.returncode,"sanitizer_error":"Sanitizer" in r.stderr or "runtime error:" in r.stderr}
        if name=="control":
            m=re.search(r"checks=(\d+) failures=(\d+) skips=(\d+)",r.stdout);results[name]["counts"]=list(map(int,m.groups())) if m else None
            if r.returncode:break
    passed=len(results)==len(variants) and results["control"].get("exit")==0 and all(x.get("compiled") and x.get("exit",0)!=0 and not x.get("sanitizer_error") for n,x in results.items() if n!="control")
    receipt={"passed":passed,"header_sha256":hashlib.sha256(header.encode()).hexdigest(),"results":results,"scope":"authored lifetime table; native destruction notification and order integration not yet proved"}
    (out/"RESULT.private.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print(json.dumps({"passed":passed,"checks":results["control"].get("counts"),"mutants":len(changes),"receipt":str(out/"RESULT.private.json")}))
    return 0 if passed else 1
if __name__=="__main__":raise SystemExit(main())
