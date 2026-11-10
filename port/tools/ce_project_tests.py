#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded authored-only project loader and source-bound integration controls."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "port"), str(ROOT / "port/tools")]
import build  # noqa: E402
TARGETS = ("System.cpp", "world.cpp", "Career.cpp", "Career.h", "PCMemoryCard.cpp")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


# A build with the network puts the network career guard (patch 0185) first in each card writer, as the network suites require.
# It only refuses (and reports no slot): the project's storage guard may follow it, and nothing else may come between them.
NETWORK_CAREER_GUARD = re.compile(
    r"\s*#ifndef CE_NETWORK_OFF\n(?:[ \t]*//[^\n]*\n)?[ \t]*if \(CE_BootRequested\(\) \|\| CE_LockstepRequested\(\)\)\n"
    r"(?:[ \t]*return 1;\n|[ \t]*\{\n[ \t]*if \(save\) \*save = -1;\n[ \t]*return 1;\n[ \t]*\}\n)#endif\n")
STORAGE_GUARD = re.compile(r"\s*#ifdef CE_VERSION\s+(if \(!PortCeProjectCareerAllowed\(\)\) return MCE_FAILURE;)\s*#endif")


def storage_guard(entry):
    """The project's storage guard at a card method's entry (after the network career guard in a network build), else None."""
    lead = NETWORK_CAREER_GUARD.match(entry)
    return STORAGE_GUARD.match(entry[lead.end():] if lead else entry)


def body(source, symbol):
    match = re.search(r"\b" + re.escape(symbol) + r"\s*\(", source)
    if not match:
        raise ValueError("Missing source-bound function " + symbol)
    first = source.index("{", match.end()) + 1
    # Only entry guards are used; never print or publish generated game bodies.
    return source[first:]


def prepare(output):
    """The integration files as the build compiles them: the mirror's text of src/ (include names normalised as build_mirror does),
    every patch's section for them applied in the build's order at fuzz 0, which the build also requires (build.patched_text)."""
    mirror = output / "source.private"
    mirror.mkdir()
    try:
        texts = build.patched_text(TARGETS)
    except RuntimeError as error:
        (output / "patch-error.private.txt").write_text(str(error))
        raise ValueError("A selected integration patch did not apply; private details retained.") from None
    for name, text in texts.items():
        (mirror / name).write_bytes(text.encode("latin-1"))
    return texts


def hook(source, expression):
    matches = re.findall(r"if\s*\(!" + expression + r"\s*\([^;]+?\)\)\s*return FALSE;", source)
    if len(matches) != 1:
        raise ValueError("Expected one production hook: " + expression)
    return matches[0]


def integration(source, output):
    world = source["world.cpp"]
    begin = hook(world, "PortCeProjectBeginWorld")
    apply = hook(world, "PortCeProjectApplyPlacement")
    end = hook(world, "PortCeProjectEndWorld")
    loop = body(world, "CWorld::LoadWorld")
    if not loop.index(apply) < loop.index("things[i]->Init(init)") < loop.index(end):
        raise ValueError("Placement must precede the actual object Init and completion check.")
    startup = body(source["System.cpp"], "CSystem::Run")
    prefix = startup[:startup.index("if (CLIPARAMS.mStressTest")]
    for required in ("PortCeProjectRequested()", "PortCeProjectLaunchLevel()", "if (level < 0) return -1;", "GAME.RunLevel(level);", "return PortCeProjectLoaded() ? 0 : -1;"):
        if required not in prefix:
            raise ValueError("The isolated project launch is missing an early boundary.")
    parts = ["bool LoadAuthoredWorld(int level,CInitThing** inits,CThing** things,int num,BOOL base) {", begin,
             "for (int i=0;i<num;++i) { CInitThing* init=inits[i];", apply, "things[i]->Init(init); }", end, "return TRUE; }"]
    mutators = [("Career.cpp", name) for name in ("CCareerNode::SetBaseThingExistTo", "CCareer::Update", "CCareer::UpdateThingsKilled", "CCareer::UpdateGoodieStates", "CCareer::SetSlot")]
    mutators += [("Career.h", "SetGoodieState"), ("Career.h", "SetInProgress")]
    for index, (path, name) in enumerate(mutators):
        entry = body(source[path], name)
        match = re.match(r"\s*#ifdef CE_VERSION\s+(if \(!PortCeProjectCareerAllowed\(\)\) return;)\s*#endif", entry)
        if not match:
            raise ValueError("A career progression guard is missing or late: " + name)
        parts.append("void Mutator%d() { %s ++careerMutations; }" % (index, match[1]))
    parts.append("void CareerMutators() { " + " ".join("Mutator%d();" % index for index in range(len(mutators))) + " }")
    writers = ("CreateSave", "DeleteSave", "WriteSave", "ReadSave")
    for index, name in enumerate(writers):
        entry = body(source["PCMemoryCard.cpp"], "CPCMemoryCard::" + name)
        match = storage_guard(entry)
        if not match:
            raise ValueError("A career storage guard is missing or late: " + name)
        lead = NETWORK_CAREER_GUARD.match(entry)
        if lead:
            # controls: a statement between the two guards, or a network block that does more than refuse, is late
            between = entry[:lead.end()] + "\tCountStorage();\n" + entry[lead.end():]
            widened = entry[:lead.end()].replace("return 1;", "CountStorage(); return 1;", 1) + entry[lead.end():]
            if storage_guard(between) or storage_guard(widened):
                raise ValueError("The storage guard check admits a statement before the guard: " + name)
        parts.append("int Storage%d() { %s ++%s; return 0; }" % (index, match[1], "careerReads" if name == "ReadSave" else "careerWrites"))
    parts.append("void CareerStorage() { " + " ".join("Storage%d();" % index for index in range(len(writers))) + " }")
    (output / "ce_project_hooks.private.h").write_text("\n".join(parts) + "\n")


def authored():
    return dict(kind="onslaught.ce.mission-project", version=2, coordinates="aquila-xyz-z-down", source_language="bea-msl",
                id="linked_fixture", name="Authored linked placement", entry_script="arrival",
                map=dict(mode="existing-object-placement", level=777, bindings=[dict(definition="authored.beacon", source_object="AuthoredAnchor")]),
                placements=[dict(id="beacon", name="Authored beacon", definition="authored.beacon", position=[12.5, -7.25, -3], script="arrival")],
                scripts=[dict(id="arrival", name="Preserved source", source="// Authored source Ω\r\n\t// Preserved; not executed.\n")])



def profile_checks(output, compiler, project, causal_controls):
    source = (ROOT/"port/platform/linux/main.cpp").read_text()
    main = source[source.index("int main("):]
    guard = "if (!PortPathsProjectPreflight()) return 1;"
    if main.count(guard) != 1:
        raise ValueError("The actual Linux startup lacks its single early project guard.")
    status = "if (PortCeProjectRequested()) rc = PortCeProjectExitStatus();"
    if main.count(status) != 1 or not main.index("int rc = WinMain(") < main.index(status) < main.index("_exit(rc);"):
        raise ValueError("The actual Linux entry does not report the project's own outcome.")
    for marker in ("PortPathsInit(", "PortAudioGuardCheck(", "SDL_Init(", "CeInitializeSignalHttps("):
        if marker in main and main.index(guard) >= main.index(marker):
            raise ValueError("Project profile preflight is late: " + marker)
    paths = (ROOT/"port/platform/linux/paths.cpp").read_text()
    def initialization(text, name):
        first=text.index("{",text.index("void "+name+"()")); end=text.index("\n}",first)+2
        return text[first:end]
    # PortPathsInit delegates name selection; pin both halves of the ordinary path contract.
    ordinary_pins = {
        "PortPathsNamesOnly": "a6fafc5c68e8f4503322a8e99f4ace85986e48e5351cbaa32e2deb51e452af40",
        "PortPathsInit": "0393a807e18801f0b64a4020684e51b037a61f494501a89cb5cc7abc88bc5e55",
    }
    for name, expected in ordinary_pins.items():
        if hashlib.sha256(initialization(paths, name).encode()).hexdigest() != expected:
            raise ValueError("The ordinary path initializer changed: " + name)
    header=output/"ce_project_profile_start.private.h"
    header.write_text("#ifndef CE_PROJECT_OMIT_PREFLIGHT\n"+guard+"\n#endif\nPortPathsInit();\n")
    def mask(match):
        return "".join("\n" if c=="\n" else " " for c in match[0])
    masked=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',mask,paths,flags=re.S)
    def exact_function(name):
        found=re.search(r"\b"+name+r"\s*\(",masked)
        if not found:raise ValueError("Missing exact path function: "+name)
        first=masked.index("{",found.end());level=1;last=first+1
        while level:
            if masked[last]=="{":level+=1
            elif masked[last]=="}":level-=1
            last+=1
        first=paths.rfind("\n",0,found.start())+1
        return paths[first:last]
    names=("PortGameDir","PortUserDir","GameDirectory","WalkDirectories","OpenUserRoot","PortPathsProjectPreflight","PortPathsNamesOnly","PortPathsInit")
    functions={name:exact_function(name) for name in names}
    declarations="".join(line for line in paths.splitlines(keepends=True) if re.match(r"\s*(?:static\s+)?char\s+s(?:Game|User)Dir\[",line))
    if "sGameDir[" not in declarations or "sUserDir[" not in declarations:raise ValueError("Path cache declaration shape changed.")
    extracted=output/"paths-profile.private.cpp"
    extracted.write_text('#include "ce_project.h"\n#include <cstdio>\n#include <cstdlib>\n#include <cstring>\n#include <cerrno>\n#include <fcntl.h>\n#include <sys/stat.h>\n#include <unistd.h>\n'+declarations+"\n"+"\n".join(functions.values())+"\n")
    (output/"profile-source-pins.private.json").write_text(json.dumps({"paths_source_sha256":digest(ROOT/"port/platform/linux/paths.cpp"),"main_source_sha256":digest(ROOT/"port/platform/linux/main.cpp"),"exact_functions":{name:hashlib.sha256(text.encode()).hexdigest() for name,text in functions.items()},"declarations_sha256":hashlib.sha256(declarations.encode()).hexdigest(),"ordinary_initializer_unchanged":True},indent=2)+"\n")
    binary=output/"profile-driver"
    command=[compiler,"-std=c++14","-O0","-Wall","-Wextra","-Werror",'-DCE_VERSION="authored-project-profile"',
             "-I"+str(ROOT/"port/game"),"-I"+str(output),str(ROOT/"port/tools/tests/ce_project_profile_driver.cpp"),
             str(extracted),str(ROOT/"port/game/ce_project.cpp")]
    def build(target,extra=()):
        compiled=subprocess.run(command+list(extra)+["-o",str(target)],capture_output=True,text=True)
        (output/(target.name+".compile.private.log")).write_text(compiled.stdout+compiled.stderr)
        if compiled.returncode:raise ValueError("Real startup/path preflight compile failed; private log retained.")
    build(binary)
    results=[]
    def run(name, selected=True, user_kind="valid", game_kind="valid", target=binary):
        case=output/("profile-"+name);case.mkdir();home=case/"home";home.mkdir(mode=0o700)
        game=case/"game";game.mkdir();user=case/"user";user.mkdir(mode=0o700)
        requested_user=str(user);requested_game=str(game)
        if user_kind=="missing":requested_user=None
        elif user_kind=="relative":requested_user="relative-user"
        elif user_kind=="absent":requested_user=str(case/"absent-user")
        elif user_kind=="unsafe":user.chmod(0o777)
        elif user_kind=="game":requested_user=str(game)
        elif user_kind=="inside-game":user=game/"user";user.mkdir(mode=0o700);requested_user=str(user)
        elif user_kind=="parent-game":requested_user=str(case)
        elif user_kind=="symlink":alias=case/"user-link";alias.symlink_to(user,target_is_directory=True);requested_user=str(alias)
        elif user_kind=="long":requested_user=str(user)+"/."*2600
        if game_kind=="missing":requested_game=None
        elif game_kind=="absent":requested_game=str(case/"absent-game")
        elif game_kind=="long":requested_game=str(game)+"/."*2600
        environment={k:v for k,v in os.environ.items() if k not in ("HOME","BEA_CE_PROJECT","BEA_USER_DIR","BEA_GAME_DIR","DISPLAY","WAYLAND_DISPLAY") and not k.startswith(("XDG_","BEA_NET_","BEA_LOCKSTEP_"))}
        environment["HOME"]=str(home)
        if name=="ordinary-xdg":environment["XDG_DATA_HOME"]=str(home/"private-xdg")
        if selected:environment["BEA_CE_PROJECT"]=str(project)
        if requested_user is not None:environment["BEA_USER_DIR"]=requested_user
        if requested_game is not None:environment["BEA_GAME_DIR"]=requested_game
        if name=="network":environment["BEA_NET_HOST"]="authored-network-intent"
        completed=subprocess.run([str(target)],cwd=case,env=environment,capture_output=True,text=True,timeout=3)
        (case/"driver.private.log").write_text(completed.stdout+completed.stderr)
        entries=sum(1 for _ in home.rglob("*"))
        return dict(case=name,exit=completed.returncode,private_home_entries=entries,absent_user_created=(case/"absent-user").exists(),absent_game_created=(case/"absent-game").exists())
    for name,selected,user_kind,game_kind in (
        ("valid",True,"valid","valid"),("missing-user",True,"missing","valid"),("relative-user",True,"relative","valid"),
        ("absent-user",True,"absent","valid"),("unsafe-user",True,"unsafe","valid"),("same-game",True,"game","valid"),
        ("inside-game",True,"inside-game","valid"),("parent-game",True,"parent-game","valid"),("symlink-user",True,"symlink","valid"),
        ("long-user",True,"long","valid"),("long-game",True,"valid","long"),("missing-game",True,"valid","missing"),
        ("absent-game",True,"valid","absent"),("network",True,"valid","valid"),("ordinary-default",False,"missing","valid"),("ordinary-xdg",False,"missing","valid"),
    ):
        row=run(name,selected,user_kind,game_kind)
        expected=0 if name in ("valid","ordinary-default","ordinary-xdg") else 1
        if row["exit"]!=expected or row["absent_user_created"] or row["absent_game_created"] or (row["private_home_entries"]!=0 and name not in ("ordinary-default","ordinary-xdg")) or (name in ("ordinary-default","ordinary-xdg") and row["private_home_entries"]==0):
            raise ValueError("Private-HOME startup profile control failed: "+name)
        results.append(row)
    if causal_controls:
        mutant=output/"profile-driver-no-guard";build(mutant,["-DCE_PROJECT_OMIT_PREFLIGHT"])
        row=run("guard-removed",True,"missing","valid",mutant)
        if row["exit"]!=0 or row["private_home_entries"]==0:raise ValueError("Removed startup guard did not expose the intended private-HOME creation.")
        row["intended_policy_failure"]=True;results.append(row)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, help="Optional exact authored editor fixture for the connected save/load check.")
    parser.add_argument("--causal-controls", action="store_true")
    args = parser.parse_args()
    parent = ROOT / "local-data/port/qa"; parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="ce-project-", dir=parent))
    game = output / "installed-copy.authored"; user = output / "user.authored"
    game.mkdir(); user.mkdir(mode=0o700)
    (game / "asset.authored").write_text("Authored immutable installed-map stand-in.\n")
    (user / "career.authored").write_text("Authored career sentinel; the loader must not read or write it.\n")
    source = prepare(output); integration(source, output)
    project = output / "linked.ceproject"
    expected = authored()
    if args.project:
        incoming = json.loads(args.project.read_text())
        if incoming != expected:
            raise ValueError("--project accepts only the exact authored cross-language fixture, not private game projects.")
        shutil.copyfile(args.project, project)
    else:
        project.write_text(json.dumps(expected, ensure_ascii=False, indent=2) + "\n")
    pins = {str(path.relative_to(ROOT)): digest(path) for path in (ROOT / "port/game/ce_project.cpp", ROOT / "port/game/ce_project.h", ROOT / "port/patches/0139-ce-project-launch-and-storage-hooks.patch", ROOT / "port/patches/0202-ce-project-linked-placement.patch", ROOT / "port/tools/ce_project_tests.py", ROOT / "port/tools/tests/ce_project_driver.cpp", ROOT / "port/tools/tests/ce_project_profile_driver.cpp", ROOT / "port/platform/linux/paths.cpp", ROOT / "port/platform/linux/main.cpp", ROOT / "port/platform/linux/handles.h") }
    before = {str(path): digest(path) for path in (project, game / "asset.authored", user / "career.authored")}
    compiler = os.environ.get("CXX", "c++")
    common = [compiler, "-std=c++14", "-O0", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "port/game"), "-I" + str(output), str(ROOT / "port/tools/tests/ce_project_driver.cpp")]
    results = []
    def compile_owner(owner, target):
        result = subprocess.run(common + [str(owner), "-o", str(target)], capture_output=True, text=True)
        (output / (target.name + ".compile.private.log")).write_text(result.stdout + result.stderr)
        if result.returncode: raise ValueError("Project owner C++14 compile failed; private log retained.")
    def run(binary, path, scenario, expect=0, timeout=10, expect_timeout=False):
        command = [str(binary), str(path), str(game), str(user), scenario]
        environment = {k:v for k,v in os.environ.items() if k not in ("BEA_CE_PROJECT", "BEA_USER_DIR") and not k.startswith(("BEA_NET_", "BEA_LOCKSTEP_"))}
        try:
            result = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=timeout)
        except subprocess.TimeoutExpired:
            # subprocess.run kills and collects this owned driver before raising.
            if not expect_timeout:
                raise ValueError("Project input validation exceeded its bounded timeout: " + path.stem) from None
            results.append(dict(case=path.stem+":"+scenario, binary=binary.name, timeout=timeout, owned_child_collected=True))
            return
        (output / (binary.name + "-" + path.stem + "-" + scenario + ".private.log")).write_text(result.stdout + result.stderr)
        if expect_timeout or result.returncode != expect: raise ValueError("Authored project check did not reach its expected result: " + scenario)
        results.append(dict(case=path.stem+":"+scenario, binary=binary.name, exit=result.returncode))
    binary = output / "project-driver"; compile_owner(ROOT / "port/game/ce_project.cpp", binary)
    for scenario in ("off", "load", "missing", "duplicate", "near-miss", "base-only", "base-and-level", "wrong-map", "network", "no-map"):
        run(binary, project, scenario)
    # A binding that no placement uses is accepted and moves nothing: its object is never looked up, absent or present.
    for name, unplaced in (("unplaced-absent", "UnplacedObject"), ("unplaced-present", "UnrelatedObject")):
        value = json.loads(json.dumps(expected)); value["map"]["bindings"].append(dict(definition="authored.unplaced", source_object=unplaced))
        path = output / (name + ".ceproject"); path.write_text(json.dumps(value)); run(binary, path, "load")
    variants = []
    def changed(name, edit):
        value = json.loads(json.dumps(expected)); edit(value); path = output / (name + ".ceproject"); path.write_text(json.dumps(value)); variants.append(path)
    changed("missing-binding", lambda v: v["map"].update(bindings=[]))
    changed("unknown-mode", lambda v: v["map"].update(mode="new-object-spawning"))
    changed("duplicate-target", lambda v: v["map"]["bindings"].append(dict(definition="authored.other",source_object="AuthoredAnchor")))
    changed("duplicate-placement", lambda v: v["placements"].append(dict(v["placements"][0],id="other")))
    changed("bad-coordinate", lambda v: v["placements"][0].update(position=[1048577,0,0]))
    changed("missing-source", lambda v: v.update(entry_script="absent"))
    changed("unknown-field", lambda v: v.update(unreviewed=True))
    changed("legacy-project", lambda v: (v.update(version=1),v.pop("map")))
    changed("invalid-map", lambda v: v["map"].update(level=0))
    changed("unicode-control", lambda v: v.update(name="bad\u0085name"))
    for name, data in (("duplicate-field", project.read_text().replace('"version": 2','"version": 2, "version": 2')), ("invalid-utf8", b"\xff"), ("trailing-nul", project.read_bytes()+b"\0"), ("unpaired-surrogate", project.read_text().replace("AuthoredAnchor", "\\ud800")), ("oversize", b" "*((8<<20)+1))):
        path=output/(name+".ceproject");path.write_bytes(data if isinstance(data,bytes) else data.encode());variants.append(path)
    for path in variants: run(binary,path,"invalid")
    symlink=output/"symlink.ceproject";symlink.symlink_to(project);run(binary,symlink,"invalid")
    fifo=output/"fifo.ceproject";os.mkfifo(fifo,0o600);run(binary,fifo,"invalid",timeout=1)
    if args.causal_controls:
        owner=(ROOT/"port/game/ce_project.cpp").read_text()
        controls=(
            ("career-fence", "return !PortCeProjectRequested() &&", "return true &&", "load"),
            ("placement-application", "*x = p.x; *y = p.y; *z = p.z;", "(void)x; (void)y; (void)z;", "load"),
            ("missing-object", "if (!p.seen) return Fail(", "if (false && !p.seen) return Fail(", "missing"),
            ("duplicate-object", "if (p.seen) return Fail(", "if (false && p.seen) return Fail(", "duplicate"),
            ("bounded-name", "p.source.size() == length && std::memcmp(", "std::memcmp(", "near-miss"),
            ("base-world", "if (!PortCeProjectRequested() || base) return true;\n    State& state = Current();\n    if (!state.began || state.failed || state.complete || !name",
             "if (!PortCeProjectRequested()) return true;\n    State& state = Current();\n    if (!state.began || state.failed || state.complete || !name", "base-and-level"),
            ("map-binding", "if (level != state.project.level) return Fail(", "if (false && level != state.project.level) return Fail(", "wrong-map"),
            ("exit-status", "if (state.complete && !state.failed) return 0;", "return 0;", "no-map"),
        )
        for name, old, new, scenario in controls:
            if owner.count(old)!=1:raise ValueError("Causal control anchor drifted: "+name)
            path=output/(name+".cpp");path.write_text(owner.replace(old,new));mutant=output/("mutant-"+name);compile_owner(path,mutant);run(mutant,project,scenario,expect=1)
        if owner.count("|O_NONBLOCK") != 1: raise ValueError("Nonblocking-open control anchor drifted.")
        nonblocking = output/"blocking-open.cpp";nonblocking.write_text(owner.replace("|O_NONBLOCK", ""))
        mutant = output/"mutant-blocking-open";compile_owner(nonblocking,mutant)
        run(mutant,fifo,"invalid",timeout=1,expect_timeout=True)
    if any(digest(Path(path))!=sha for path,sha in before.items()):raise ValueError("An authored input or career sentinel changed.")
    profiles=profile_checks(output,compiler,project,args.causal_controls)
    receipt=dict(profile_cases=profiles,source_head=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),inputs=pins,project_sha256=digest(project),source_hooks={name:digest(output/"source.private"/name) for name in TARGETS},cases=results,inputs_unchanged=True,limits="Authored hook/loader controls only; no installed assets, game process, GPU, provider, VM or Windows acceptance.")
    (output/"result.private.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print("CE project authored loader checks passed:",len(results),"loader process cases +",len(profiles),"private-HOME startup cases; inputs unchanged.")
    print("Receipt:",output/"result.private.json")


if __name__ == "__main__":
    main()
