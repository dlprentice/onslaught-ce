#!/usr/bin/env python3
# SPDX-FileCopyrightText: Onslaught CE contributors
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the exact native view provider body with authored device adapters."""
from pathlib import Path
import json
import shutil
import subprocess
import tempfile


def main():
    root=Path(__file__).resolve().parents[2]
    folder=root/"local-data/world-view-tests";folder.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix="run-",dir=folder));compiler=shutil.which("c++")
    if not compiler: raise SystemExit("C++ compiler required; no checks ran")
    source=(root/"port/game/ce_world_view.cpp").read_text()
    if source.count("extern bool gMirrorPass;")!=1:raise SystemExit("typed native mirror-pass declaration missing")
    begin="// CE_WORLD_VIEW_IMPL_BEGIN:";end="// CE_WORLD_VIEW_IMPL_END"
    if source.count(begin)!=1 or source.count(end)!=1:raise SystemExit("provider extraction anchors missing")
    body=source.split(begin,1)[1].split("\n",1)[1].split(end,1)[0]
    cases={"actual":body,"without_editor":body,
           "ignore_session":body.replace("token != gWorldViewToken","false").replace("currentToken != token","false"),
           "ignore_age":body.replace("PortPresentCount() - gWorldView.present > 1","false"),
           "ignore_resize":body.replace("PLATFORM.GetWindowWidth() != gWorldViewWidth","false").replace("PLATFORM.GetWindowHeight() != gWorldViewHeight","false"),
           "retain_failed_capture":body.replace("    gWorldViewValid = false;","    // deliberately retain stale view",1),
           "capture_mirror":body.replace("    if (gMirrorPass) return;","    // deliberately capture reflected view",1),
           "retain_before_mirror":body.replace("    gWorldViewValid = false;","    if (gMirrorPass) return;\n    gWorldViewValid = false;",1),
           "read_in_mirror":body.replace("if (gMirrorPass || !out", "if (!out",1)}
    results={}
    for name,content in cases.items():
        normal=name in ["actual","without_editor"]
        if not normal and content==body:raise SystemExit("provider causal anchor missing")
        case=out/name;case.mkdir();(case/"ce_world_view_fixture.inc").write_text(content);exe=case/"checks"
        command=[compiler,"-std=c++17","-Wall","-Wextra","-Werror","-Wno-address","-O2","-I",str(case),"-I",str(root/"port/game")]
        if name=="without_editor":command.append("-DCE_WORLD_VIEW_NO_EDITOR")
        command += [str(root/"port/tools/ce_world_view_tests.cpp"),"-o",str(exe)]
        build=subprocess.run(command,capture_output=True,timeout=40);(case/"compile.private.log").write_bytes(build.stdout+build.stderr)
        run=subprocess.run([str(exe)],capture_output=True,timeout=15) if build.returncode==0 else None
        if run:(case/"run.private.log").write_bytes(run.stdout+run.stderr)
        results[name]={"compiled":build.returncode==0,"exit":run.returncode if run else None,"expected":0 if normal else "nonzero"}
    passed=all(v["compiled"] and (v["exit"]==0 if k in ["actual","without_editor"] else v["exit"]!=0) for k,v in results.items())
    (out/"RESULT.private.json").write_text(json.dumps({"passed":passed,"cases":results},indent=2)+"\n")
    print(json.dumps({"passed":passed,"evidence":str(out),"scope":"exact provider body, authored native adapters; no actual-game hook acceptance"}))
    raise SystemExit(0 if passed else 1)


if __name__=="__main__":main()
