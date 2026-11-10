#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Production adapter codecs against actual generated game headers; no game or retail inputs.

Snapshot an explicit existing mirror's headers and affected source, apply the proposed mirror hooks without fuzz,
then compile the real adapter with the pinned i686 compiler. Link-time section collection excludes game consumers
the fixture does not call. This checks typed field codecs, ID/reference rules, order and bounded refusal; it does not
instantiate engines, establish replica lifetimes, or demonstrate a playable network route.
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

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build
sys.path.insert(0, str(ROOT / "port/tools"))
import premirror


def view(path):
    """A mirror file's text as a build with the network sees it: the patched hooks' CE_NETWORK_OFF conditionals resolved."""
    return build.network_view(Path(path).read_text(), off=False)


def add_round_sources(mirror):
    """Copy missing selected actor/weapon callers with the ordinary include and pre-network patches."""
    files=list((ROOT/"src").rglob("*"))
    index=build.CaseIndex([mirror/p.relative_to(ROOT/"src") for p in files if p.is_file()])
    for name in ("actor.cpp","weapon.cpp"):
        dest=mirror/name
        if dest.exists():
            continue
        dest.write_text(build.normalise((ROOT/"src"/name).read_text(),mirror,index,[mirror,mirror/"MissionScript"]))
        for patch in sorted((ROOT/"port/patches").glob("*.patch")):
            if patch.name[:4]>="0065": break
            sections=re.split(r"(?=^--- a/)",patch.read_text(),flags=re.M)
            selected="".join(section for section in sections if section.startswith("--- a/"+name+"\n"))
            if selected:
                proc=subprocess.run(["patch","--batch","--fuzz=0","-p1"],cwd=mirror,input=selected,capture_output=True,text=True,timeout=15)
                if proc.returncode: raise RuntimeError(patch.name+": "+proc.stdout+proc.stderr)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def caller_controls(mirror, out, compiler):
    """Compile actual native caller bodies; controlled boundaries expose selected view/inversion, not rendering."""
    sound = view(mirror / "SoundManager.cpp")
    start = sound.index("void CSoundManager::UpdateSoundPosition(")
    sound = sound[start:sound.index("\n//-------", start)]
    compass = view(mirror / "Compass.cpp")
    start = compass.index("float\tcx =", compass.index("void CCompass::Render("))
    compass = compass[start:compass.index("CDXSpriteRenderer::mBatch = TRUE", start)]
    player = view(mirror / "Player.cpp")
    start = player.index("\t// deal with axis inversion")
    pitch = player[start:player.index("\t// if look then", start)]
    body = view(mirror / "BattleEngine.cpp")
    start = body.index("if (mPlayer->GetIsInFPV()")
    condition = body[start:body.index("\n", start)].strip()[3:]
    body = "return " + condition + ";\n"
    alpha = next(line for line in view(mirror / "game.cpp").splitlines()
                 if "if (!CE_WorldReplica()) mWinLoseScreenAlpha+=step;" in line) + "\n"
    render_guard = next(line for line in view(mirror / "game.cpp").splitlines()
                        if "if (!CE_WorldCanRender()) return;" in line) + "\n"
    snippets = {"sound": sound, "compass": compass, "pitch": pitch, "body": body, "alpha": alpha, "render_guard": render_guard}
    # These precise negative controls reinstate the native assumption at the actual reviewed call site.
    sound_guard = "if (GAME.IsMultiplayer() && !CE_WorldActive())"
    assert sound.count(sound_guard) == 2
    before, after = sound.rsplit(sound_guard, 1)
    variants = {
        "fixed": ({}, None),
        "wrong-second-listener": ({"sound": before + "if (GAME.IsMultiplayer())" + after}, "guest listener"),
        "split-compass": ({"compass": compass.replace(" && !CE_WorldActive()", "")}, "guest compass"),
        "wrong-body-view": ({"body": body.replace("CE_WorldViewPlayer(ENGINE.mCurrentViewpoint)", "ENGINE.mCurrentViewpoint")}, "first-person"),
        "host-inversion": ({"pitch": pitch.replace("CE_WorldInvertPitch(mNumber-1,TRUE,CAREER.GetInvertYAxis(mNumber-1))", "CAREER.GetInvertYAxis(mNumber-1)")}, "ground preference"),
        "local-replica-fade": ({"alpha": alpha.replace("if (!CE_WorldReplica()) ", "")}, "replica alpha"),
        "unprepared-replica-render": ({"render_guard": ""}, "unprepared replica"),
    }
    results = []
    for variant, (overrides, failure) in variants.items():
        folder = out / "callers" / variant
        folder.mkdir(parents=True)
        defines = []
        pins = {}
        for name, source in snippets.items():
            path = folder / (name + ".inc")
            path.write_text(overrides.get(name, source))
            pins[name] = sha(path)
            defines.append('-DNATIVE_' + name.upper() + '="' + str(path) + '"')
        binary = folder / "caller-controls"
        command = [compiler, "-std=c++14", "-O0", "-Wall", "-Wextra", *defines,
                   str(ROOT / "port/tools/tests/network_world_callers.cpp"), "-o", str(binary)]
        compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
        (folder / "build.log").write_text(compiled.stdout + compiled.stderr)
        proc = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5) if not compiled.returncode else None
        passed = compiled.returncode == 0 and proc is not None and (
            proc.returncode == 0 if failure is None else proc.returncode == 2 and failure in proc.stderr)
        results.append(dict(variant=variant, passed=passed, snippets=pins, command=command,
                            compile_exit=compiled.returncode, exit=proc.returncode if proc else None,
                            stdout=proc.stdout if proc else "", stderr=proc.stderr if proc else ""))
        print(f"{'PASS' if passed else 'FAIL'} actual native callers: {variant}")
    return results


def windows_syntax(mirror, production, out):
    target = build.TARGETS["windows"]
    includes = [build.PORT / "d3dx/include", build.PORT / "include", *target["includes"], mirror, mirror / "MissionScript"]
    command = [*target["cxx"], "-std=gnu++14", *build.MSCXX, *build.COMMON, *build.X87,
               *build.NO_MATH_BUILTINS, "-fsyntax-only", "-include", str(build.PORT / "include/port.h"),
               *["-I" + str(p) for p in includes], str(production / "game/ce_network_world.cpp")]
    proc = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (out / "windows-syntax.log").write_text(proc.stdout + proc.stderr)
    return dict(command=command, exit=proc.returncode)


def native_typeinfo_bridge(production, out, target, syntax=False):
    """Use the production pcxx class, not the game parser flags, for the frozen standard-library boundary."""
    obj = out / (target + "-typeinfo-bridge.o")
    layout = ["-DNOMINMAX", "-fms-extensions", "-fno-autolink", "-fno-strict-aliasing", "-fwrapv",
              "-mms-bitfields", "-fshort-wchar", "-g", "-O2", "-w"]
    command = [*build.TARGETS[target]["cxx"], "-std=gnu++17", *layout, *build.CE_DEFINES,
               "-ffunction-sections", "-fdata-sections"]
    command += ["-fsyntax-only"] if syntax else ["-c", "-o", str(obj)]
    command.append(str(production / "platform/common/typeinfo_bridge.cpp"))
    proc = subprocess.run(command, capture_output=True, text=True, timeout=30)
    (out / (target + "-typeinfo-bridge.log")).write_text(proc.stdout + proc.stderr)
    return obj, dict(command=command, exit=proc.returncode)


def projectile_controls(mirror, out, compiler):
    battle = view(mirror / "BattleEngine.cpp")
    start = battle.index("void\tCBattleEngine::LockHit(")
    lock = battle[start:battle.index("\n//*****", start)]
    rounds = view(mirror / "Round.cpp")
    start = rounds.index("void CRound::Shutdown()")
    shutdown = rounds[start:rounds.index("\n//*****", start)]
    start = rounds.index("void CRound::Unknown4dac90(")
    guard = rounds[rounds.index("{", start) + 1:].splitlines()[1] + "\n"
    assert "CE_WorldReplica()" in guard
    results = []
    for name, changed, failure in (
        ("fixed", {}, None),
        ("despawn-callback", {"lockhit": lock.replace("\tif (CE_WorldReplica()) return;\n", "")}, "replica despawn"),
        ("replica-seek", {"seek_guard": ""}, "replica init"),
    ):
        folder = out / "projectile-callers" / name
        folder.mkdir(parents=True)
        defines, pins = [], {}
        for label, source in {"lockhit": lock, "round_shutdown": shutdown, "seek_guard": guard}.items():
            path = folder / (label + ".inc")
            path.write_text(changed.get(label, source))
            pins[label] = sha(path)
            defines.append('-DNATIVE_' + label.upper() + '="' + str(path) + '"')
        binary = folder / "projectile-callers"
        command = [compiler, "-std=c++14", "-O0", "-Wall", "-Wextra", *defines,
                   str(ROOT / "port/tools/tests/network_world_projectile_callers.cpp"), "-o", str(binary)]
        build_result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        (folder / "build.log").write_text(build_result.stdout + build_result.stderr)
        proc = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5) if not build_result.returncode else None
        passed = build_result.returncode == 0 and proc is not None and (
            proc.returncode == 0 if failure is None else proc.returncode == 2 and failure in proc.stderr)
        results.append(dict(variant=name, passed=passed, snippets=pins, command=command,
                            compile_exit=build_result.returncode, exit=proc.returncode if proc else None,
                            stdout=proc.stdout if proc else "", stderr=proc.stderr if proc else ""))
        print(f"{'PASS' if passed else 'FAIL'} native projectile callbacks: {name}")
    return results


def actual_body(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for i in range(opening, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if not depth:
                return text[start:i + 1]
    raise ValueError("unbalanced native function: " + signature)


def hud_controls(mirror, production, out, compiler):
    """The render wrapper has native hard-false HUD; use the selected logical game camera for HUD policy."""
    header = view(mirror / "Camera.h")
    camera = view(mirror / "Camera.cpp")
    engine = view(mirror / "engine.cpp")
    world = (production / "game/ce_network_world.cpp").read_text()
    # the HUD view-player hooks are patch 0149, written on main's later HUD and render text: read both files with the whole queue applied
    queue = build.patched_text(["Hud.cpp", "DXEngine.cpp"], network=True)
    overlay, shared = queue["Hud.cpp"], queue["DXEngine.cpp"]
    overlay_start = overlay.index("void\tCHud::Render()")
    shared_start = shared.index("HRESULT\tCDXEngine::PostRender(")
    overlay = overlay[overlay_start:]
    shared = shared[shared_start:]
    def predicate(text, statement):
        """The hook's declaration line and condition line, then the statement the condition guards (later patches wrap it in preprocessor lines)."""
        start = text.index("SINT hud_player = CE_WorldViewPlayer(i);")
        lines = text[start:].splitlines()
        call = next(line for line in lines[2:] if line.strip().startswith(statement))
        return "\n".join([lines[0], lines[1], call]) + "\n"
    snippets = {
        "interpolated_hud": actual_body(header[header.index("class CInterpolatedCamera :"):], "virtual bool GetShowHUD()"),
        "cinematic_hud": actual_body(header[header.index("class CPanCamera:"):], "virtual bool GetShowHUD()"),
        "replica_hud": actual_body(world[world.index("class ReplicaCamera :"):], "bool GetShowHUD()"),
        "interpolated_ctor": actual_body(camera, "CInterpolatedCamera::CInterpolatedCamera(CCamera *cam)"),
        "set_viewpoint": actual_body(engine, "void CEngine::SetViewpoint("),
        "world_active": actual_body(world, "int CE_WorldActive()"),
        "world_view": actual_body(world, "int CE_WorldViewPlayer(int original)"),
        "hud_overlay": predicate(overlay, "RenderOverlayForViewpoint("), "hud_shared": predicate(shared, "render=TRUE;"),
    }
    assert "return false" in snippets["interpolated_hud"]
    assert "new( MEMTYPE_CAMERA ) CInterpolatedCamera(camera)" in snippets["set_viewpoint"]
    bad_overlay = snippets["hud_overlay"].splitlines()
    bad_shared = snippets["hud_shared"].splitlines()
    bad_overlay[1] = "if (player && ENGINE.GetCameraForViewpoint(i)->GetShowHUD())"
    bad_shared[1] = "if (ENGINE.GetCameraForViewpoint(i)->GetShowHUD())"
    variants = {
        "fixed": ({}, None),
        "old-wrapper-overlay": ({"hud_overlay": "\n".join(bad_overlay) + "\n"}, "host logical HUD true"),
        "old-wrapper-shared": ({"hud_shared": "\n".join(bad_shared) + "\n"}, "host shared HUD"),
        "old-wrapper-overlay-guest": ({"hud_overlay": "\n".join(bad_overlay) + "\n"}, "guest logical HUD true"),
        "old-wrapper-shared-guest": ({"hud_shared": "\n".join(bad_shared) + "\n"}, "guest shared HUD"),
        "physical-camera-zero": ({name: snippets[name].replace("GAME.GetCamera(hud_player)", "GAME.GetCamera(0)")
                                  for name in ("hud_overlay", "hud_shared")}, "guest logical HUD true"),
        "ignore-logical-false": ({name: snippets[name].replace("GAME.GetCamera(hud_player)->GetShowHUD()", "true")
                                   for name in ("hud_overlay", "hud_shared")}, "logical HUD false"),
        "missing-mapped-bounds": ({name: snippets[name].replace("hud_player >= 0 && hud_player < 2 && ", "")
                                    for name in ("hud_overlay", "hud_shared")}, "malformed mapped role"),
        "mismatched-player-role": ({name: snippets[name].replace("player->GetNumber() == hud_player + 1 && ", "")
                                     for name in ("hud_overlay", "hud_shared")}, "mismatched canonical role"),
    }
    results = []
    for variant, (overrides, failure) in variants.items():
        folder = out / "hud-callers" / variant
        folder.mkdir(parents=True)
        defines, pins = [], {}
        for name, source in snippets.items():
            path = folder / (name + ".inc")
            path.write_text(overrides.get(name, source))
            pins[name] = sha(path)
            defines.append('-DNATIVE_' + name.upper() + '="' + str(path) + '"')
        binary = folder / "hud-callers"
        guest_first = ["-DHUD_GUEST_FIRST"] if variant.endswith("-guest") else []
        command = [compiler, "-std=c++14", "-O0", "-Wall", "-Wextra", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", *guest_first, *defines,
                   str(ROOT / "port/tools/tests/network_world_hud_callers.cpp"), "-o", str(binary)]
        built = subprocess.run(command, capture_output=True, text=True, timeout=30)
        (folder / "build.log").write_text(built.stdout + built.stderr)
        proc = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10) if not built.returncode else None
        passed = built.returncode == 0 and proc is not None and (
            proc.returncode == 0 if failure is None else proc.returncode == 2 and failure in proc.stderr)
        results.append(dict(variant=variant, passed=passed, snippets=pins, command=command,
                            compile_exit=built.returncode, exit=proc.returncode if proc else None,
                            stdout=proc.stdout if proc else "", stderr=proc.stderr if proc else ""))
        print(f"{'PASS' if passed else 'FAIL'} native HUD/wrapper callers: {variant}")
    return results


def hud_target_syntax(mirror, out):
    results = []
    # the HUD view-player hooks are patch 0149, written on main's later text: check the two files it edits with the whole queue applied
    for name, text in build.patched_text(["Hud.cpp", "DXEngine.cpp"], network=True).items():
        (mirror / name).write_text(text, encoding="latin-1")
    for platform in ("linux", "windows"):
        target = build.TARGETS[platform]
        includes = [build.PORT / "d3dx/include", build.PORT / "include", *target["includes"], mirror, mirror / "MissionScript"]
        for name in ("Hud.cpp", "DXEngine.cpp", "Cockpit.cpp"):
            command = [*target["cxx"], "-std=gnu++14", *build.MSCXX, *build.COMMON, *build.X87, *build.NO_MATH_BUILTINS,
                       "-fsyntax-only", "-include", str(build.PORT / "include/port.h"), *["-I" + str(p) for p in includes], str(mirror / name)]
            proc = subprocess.run(command, capture_output=True, text=True, timeout=60)
            (out / f"hud-syntax-{platform}-{name}.log").write_text(proc.stdout + proc.stderr)
            results.append(dict(platform=platform, source=name, source_sha256=sha(mirror / name), command=command, exit=proc.returncode))
            print(f"{'PASS' if proc.returncode == 0 else 'FAIL'} {platform} native HUD caller syntax: {name}")
    return results


def combat_target_syntax(mirror, out):
    results = []
    for platform in ("linux", "windows"):
        target = build.TARGETS[platform]
        includes = [build.PORT / "d3dx/include", build.PORT / "include", *target["includes"], mirror, mirror / "MissionScript"]
        for name in ("explosion.cpp", "WorldPhysicsManager.cpp", "thing.cpp", "actor.cpp", "weapon.cpp", "Round.cpp"):
            command = [*target["cxx"], "-std=gnu++14", *build.MSCXX, *build.COMMON, *build.X87, *build.NO_MATH_BUILTINS,
                       "-fsyntax-only", "-include", str(build.PORT / "include/port.h"), *["-I" + str(p) for p in includes], str(mirror / name)]
            proc = subprocess.run(command, capture_output=True, text=True, timeout=60)
            (out / f"combat-syntax-{platform}-{name}.log").write_text(proc.stdout + proc.stderr)
            results.append(dict(platform=platform, source=name, source_sha256=sha(mirror / name), command=command, exit=proc.returncode))
            print(f"{'PASS' if proc.returncode == 0 else 'FAIL'} {platform} native combat caller syntax: {name}")
    return results


def birth_ack_controls(production, out, compiler):
    """Actual ACK/queue/commit bodies; clock and full-frame validation are explicit controlled boundaries."""
    world = (production / "game/ce_network_world.cpp").read_text()
    apply = actual_body(world, "    static bool Apply(const CE_WorldFrame &f)")
    assert apply.count("birth_applied=") == 1
    assert apply.index("birth_applied=") > apply.index("GAME.mUpdateFrameNumber=f.tick;") > apply.index("old[i].thing->Shutdown();")
    commit = next(line for line in apply.splitlines() if "birth_applied=next_birth;" in line) + "\n"
    actions = next(line for line in world.splitlines() if "native_actions[]=" in line)
    snippets = {
        "types": "\n".join(actual_body(world, "struct " + name) + ";" for name in ("Vec", "Mat", "ExplosionBirth")),
        "helpers": actual_body(world, "int Fail(const char *message)") + "\n" + actions + "\n" +
                   actual_body(world, "unsigned ActionRank(uint32_t button)") + "\n" + actual_body(world, "bool ValidInput(const CE_WorldInput &in)"),
        "local_input": actual_body(world, "struct LocalInput") + ";",
        "input_queue": actual_body(world, "struct InputQueue") + ";",
        "apply_scope": actual_body(world, "struct ApplyScope") + ";",
        "proof_helpers": actual_body(world, "void LifeInvalidateProof() {") + "\n" + actual_body(world, "bool LifeRecordProof(const CE_WorldFrame &frame) {"),
        "apply_commit": commit,
        "replica": actual_body(world, "int CE_WorldReplica()"),
        "getter": actual_body(world, "int CE_WorldAppliedBirths(CE_WorldBirthAck *out)"),
        "retire": actual_body(world, "void RetireBirths(uint32_t serial)"),
        "input_ack": actual_body(world, "bool ValidBirthAck(const CE_WorldInput &in,bool)"),
        "ack": actual_body(world, "int CE_WorldAcknowledgeBirths("),
        "queue": actual_body(world, "int CE_WorldQueueRemoteInput("),
        "published": actual_body(world, "int CE_WorldFramePublished("),
    }
    changes = [
        ("wrong-epoch", "ack", "epoch!=config.epoch||", "", "wrong epoch"),
        ("unpublished-serial", "ack", "||serial>birth_published", "", "unpublished serial"),
        ("future-published-tick", "ack", "tick>published_tick||", "", "beyond published tick"),
        ("future-authoritative-tick", "ack", "tick>EVENT_MANAGER.GetFrameCount()||", "", "beyond authoritative current tick"),
        ("old-birth-tick", "ack", "tick<birth->tick", "false", "predate its retained birth"),
        ("getter-during-apply", "getter", "applying||", "", "getter while applying"),
        ("getter-after-poison", "getter", "poisoned||", "", "poisoned getter"),
        ("getter-before-commit", "getter", "||!applied_sequence", "", "successful Apply sequence"),
        ("retirement-rollback", "retire", "if(serial<=birth_ack) return;", "", "fresh old-ACK input"),
        ("reject-overtaken-input", "input_ack", "in.last_applied_birth_serial<=birth_published", "in.last_applied_birth_serial<=birth_published&&in.last_applied_birth_serial>=birth_ack", "fresh old-ACK input"),
        ("uncaptured-publication", "published", "frame->tick>captured_tick||", "", "publication beyond captured tick"),
        ("publication-tick-rollback", "published", "if(frame->tick>published_tick) published_tick=frame->tick;", "published_tick=frame->tick;", "older publication"),
    ]
    variants = [("fixed", {}, None)]
    for name, field, before, after, expected in changes:
        assert snippets[field].count(before) == 1, (name, before)
        variants.append((name, {field: snippets[field].replace(before, after)}, expected))
    results = []
    for name, overrides, expected in variants:
        folder = out / "birth-ack-callers" / name
        folder.mkdir(parents=True)
        defines, pins = [], {}
        for label, text in snippets.items():
            path = folder / (label + ".inc")
            path.write_text(overrides.get(label, text))
            pins[label] = sha(path)
            defines.append('-DNATIVE_' + label.upper() + '=\"' + str(path) + '\"')
        binary = folder / "birth-ack-callers"
        command = [compiler, "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   '-DCE_WORLD_HEADER=\"' + str(production / "game/ce_network_world.h") + '\"', *defines,
                   str(ROOT / "port/tools/tests/network_world_birth_ack.cpp"), "-o", str(binary)]
        built = subprocess.run(command, capture_output=True, text=True, timeout=30)
        (folder / "build.log").write_text(built.stdout + built.stderr)
        proc = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10) if not built.returncode else None
        passed = built.returncode == 0 and proc is not None and (
            proc.returncode == 0 if expected is None else proc.returncode == 2 and expected in proc.stderr)
        results.append(dict(variant=name, passed=passed, snippets=pins, command=command,
                            compile_exit=built.returncode, exit=proc.returncode if proc else None,
                            stdout=proc.stdout if proc else "", stderr=proc.stderr if proc else ""))
        print(f"{'PASS' if passed else 'FAIL'} application ACK callers: {name}")
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mirror", type=Path, help="a mirror made without patch 0065 and later (premirror.py --below 65); default: build one in the scratch folder")
    ap.add_argument("--network", type=Path, default=ROOT / "port/network", help="directory containing the transport header and codec.cpp")
    ap.add_argument("--caller-compiler", default="g++")
    args = ap.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    build.check_compiler("linux")
    parent = ROOT / "local-data/port/network-world-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    mirror = out / "mirror"
    mirror.mkdir()
    if args.mirror is None:
        args.mirror = premirror.scratch_mirror(out / "premirror", below=65)
    patch = ROOT / "port/patches/0065-network-world.patch"
    affected = re.findall(r"^--- a/(.+)$", patch.read_text(), re.M)
    sources = set(affected) | {"Monitor.cpp", "activereader.cpp", "SPtrSet.cpp", "PCController.cpp", "engine.cpp", "Camera.cpp"}
    sources.update(str(p.relative_to(args.mirror)) for p in args.mirror.rglob("*")
                   if p.is_file() and p.suffix.lower() in {".h", ".hpp", ".inl"})
    pins = {}
    for name in sorted(sources):
        source, dest = args.mirror / name, mirror / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        pins[name] = sha(dest)
    applied = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)],
                             cwd=mirror, capture_output=True, text=True, timeout=15)
    (out / "patch.log").write_text(applied.stdout + applied.stderr)
    if applied.returncode:
        raise SystemExit(f"Mirror hook patch failed: {out / 'patch.log'}")
    lifecycle_patch = ROOT / "port/patches/0079-network-compact-lifecycle.patch"
    lifecycle = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(lifecycle_patch)],
                               cwd=mirror, capture_output=True, text=True, timeout=15)
    (out / "lifecycle-patch.log").write_text(lifecycle.stdout + lifecycle.stderr)
    if lifecycle.returncode:
        raise SystemExit(f"Lifecycle hook patch failed: {out / 'lifecycle-patch.log'}")
    add_round_sources(mirror)
    round_patch=ROOT/"port/patches/0081-network-pulse-round-lifecycle.patch"
    rounds=subprocess.run(["patch","--batch","--fuzz=0","-p1","-i",str(round_patch)],cwd=mirror,capture_output=True,text=True,timeout=15)
    (out/"rounds-patch.log").write_text(rounds.stdout+rounds.stderr)
    if rounds.returncode: raise SystemExit(f"Round hooks failed: {out/'rounds-patch.log'}")
    # Freeze the adapter and its local/transport headers together before compiling it.
    production = out / "production"
    (production / "game").mkdir(parents=True)
    (production / "network").mkdir()
    (production / "platform/common").mkdir(parents=True)
    for name in ("ce_network_world.cpp", "ce_network_world.h", "ce_network_lockstep.h"):
        shutil.copyfile(ROOT / "port/game" / name, production / "game" / name)
    for name in ("network.h", "codec.cpp"):
        shutil.copyfile(args.network / name, production / "network" / name)
    for name in ("typeinfo_bridge.h", "typeinfo_bridge.cpp"):
        shutil.copyfile(ROOT / "port/platform/common" / name, production / "platform/common" / name)
    driver = out / "network_world_driver.cpp"
    shutil.copyfile(ROOT / "port/tools/tests/network_world_driver.cpp", driver)
    target = build.TARGETS["linux"]
    includes = [build.PORT / "d3dx/include", build.PORT / "include", *target["includes"], mirror, mirror / "MissionScript"]
    binary = out / "world-codecs"
    bridge, bridge_compile = native_typeinfo_bridge(production, out, "linux")
    _, bridge_windows_syntax = native_typeinfo_bridge(production, out, "windows", syntax=True)
    command = [*target["cxx"], "-std=gnu++14", *build.MSCXX, *build.COMMON, *build.X87,
               *build.NO_MATH_BUILTINS, "-ffunction-sections", "-fdata-sections",
               "-include", str(build.PORT / "include/port.h"),
               *["-I" + str(p) for p in includes],
               '-DCE_WORLD_SOURCE="' + str(production / "game/ce_network_world.cpp") + '"', str(driver),
               str(production / "network/codec.cpp"), str(bridge), *build.deps.sysroot_link_flags(), "-Wl,--gc-sections", "-o", str(binary)]
    receipt = {"inputs": pins, "patch_sha256": sha(patch), "lifecycle_patch_sha256":sha(lifecycle_patch), "round_patch_sha256":sha(round_patch),
               "round_source_sha256":{name:sha(mirror/name) for name in ("actor.cpp","weapon.cpp","Round.cpp")}, "adapter_sha256": sha(production / "game/ce_network_world.cpp"),
               "header_sha256": sha(production / "game/ce_network_world.h"), "network_header_sha256": sha(production / "network/network.h"),
               "codec_sha256": sha(production / "network/codec.cpp"), "compile": command,
               "typeinfo_bridge_header_sha256": sha(production / "platform/common/typeinfo_bridge.h"),
               "typeinfo_bridge_source_sha256": sha(production / "platform/common/typeinfo_bridge.cpp"),
               "typeinfo_bridge_compile": bridge_compile, "typeinfo_bridge_windows_syntax": bridge_windows_syntax}
    # Pin the actual configurable PC mapping order; the host admits only strict subsequences of this call shape.
    controls = view(mirror / "Controller.h")
    constants = {name: int(number) for name, number in re.findall(r"^#define\s+(BUTTON_\w+)\s+(\d+)", controls, re.M)}
    labels = re.findall(r"SControlBinding\(1,\s*(BUTTON_\w+)", view(mirror / "PCController.cpp"))
    actual_order = [constants[label] for label in labels]
    admitted = re.search(r"native_actions\[\]=\{([^}]+)\}", (production / "game/ce_network_world.cpp").read_text())
    admitted_order = [int(value) for value in admitted[1].split(",")]
    receipt["native_input_mapping"] = dict(actual_ids=actual_order, admitted_ids=admitted_order,
                                             passed=len(actual_order) == 16 and actual_order == admitted_order)
    cockpit_process = actual_body(view(mirror / "Cockpit.cpp"), "void CCockpit::Process()")
    writes = [cockpit_process.index(text) for text in ("mOldShakePos = mShakePos;", "mOldOrientation = mOrientation;", "CE_WorldCockpitInitialized(this);", "if (!mBattleEngine")]
    receipt["cockpit_marker_source_order"] = dict(passed=writes == sorted(writes), process_sha256=hashlib.sha256(cockpit_process.encode()).hexdigest())
    receipt["windows_syntax"] = windows_syntax(mirror, production, out)
    result = subprocess.run(command, capture_output=True, text=True, timeout=90, env=dict(os.environ, TMPDIR=str(out)))
    (out / "build.log").write_text(result.stdout + result.stderr)
    receipt["compile_exit"] = result.returncode
    if not result.returncode:
        proc = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        receipt.update(exit=proc.returncode, stdout=proc.stdout, stderr=proc.stderr, binary_sha256=sha(binary))
        print(proc.stdout.strip())
        receipt["caller_controls"] = caller_controls(mirror, out, args.caller_compiler)
        receipt["projectile_controls"] = projectile_controls(mirror, out, args.caller_compiler)
        receipt["hud_controls"] = hud_controls(mirror, production, out, args.caller_compiler)
        receipt["hud_target_syntax"] = hud_target_syntax(mirror, out)
        receipt["combat_target_syntax"] = combat_target_syntax(mirror, out)
        receipt["birth_ack_controls"] = birth_ack_controls(production, out, args.caller_compiler)
        reader_binary = out / "native-readers"
        reader_command = [*target["cxx"], "-std=gnu++14", *build.MSCXX, *build.COMMON, *build.X87,
                          "-ffunction-sections", "-fdata-sections", "-include", str(build.PORT / "include/port.h"),
                          *["-I" + str(p) for p in includes],
                          '-DNATIVE_SPTRSET="' + str(mirror / "SPtrSet.cpp") + '"',
                          '-DNATIVE_MONITOR="' + str(mirror / "Monitor.cpp") + '"',
                          '-DNATIVE_READER="' + str(mirror / "activereader.cpp") + '"',
                          str(ROOT / "port/tools/tests/network_world_readers.cpp"),
                          *build.deps.sysroot_link_flags(), "-Wl,--gc-sections", "-o", str(reader_binary)]
        compiled = subprocess.run(reader_command, capture_output=True, text=True, timeout=30)
        (out / "native-readers-build.log").write_text(compiled.stdout + compiled.stderr)
        reader = subprocess.run([str(reader_binary)], capture_output=True, text=True, timeout=10) if not compiled.returncode else None
        receipt["native_readers"] = dict(command=reader_command, compile_exit=compiled.returncode,
                                         exit=reader.returncode if reader else None, stdout=reader.stdout if reader else "",
                                         stderr=reader.stderr if reader else "")
        if reader:
            print(reader.stdout.strip())
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if (receipt.get("exit") == 0 and all(c["passed"] for c in receipt.get("caller_controls", []))
                 and all(c["passed"] for c in receipt.get("projectile_controls", []))
                 and len(receipt.get("hud_controls", [])) == 9 and all(c["passed"] for c in receipt["hud_controls"])
                 and len(receipt.get("hud_target_syntax", [])) == 6 and all(c["exit"] == 0 for c in receipt["hud_target_syntax"])
                 and len(receipt.get("combat_target_syntax", [])) == 12 and all(c["exit"] == 0 for c in receipt["combat_target_syntax"])
                 and len(receipt.get("birth_ack_controls", [])) == 13 and all(c["passed"] for c in receipt["birth_ack_controls"])
                 and receipt.get("native_readers", {}).get("exit") == 0
                 and receipt.get("native_input_mapping", {}).get("passed")
                 and receipt.get("cockpit_marker_source_order", {}).get("passed")
                 and receipt.get("typeinfo_bridge_compile", {}).get("exit") == 0
                 and receipt.get("typeinfo_bridge_windows_syntax", {}).get("exit") == 0
                 and receipt.get("windows_syntax", {}).get("exit") == 0) else 1


if __name__ == "__main__":
    raise SystemExit(main())
