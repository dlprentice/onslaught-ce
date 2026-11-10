#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual fully patched shell's network service hook and owned-window status helper.

Real production function bodies; controlled message/device/session boundaries. No game, assets, sockets, display,
audio or GPU. Complements the actual session-module protocol tests; it does not prove native device recovery.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

import inactive_close_tests as shell
import deps
import window_tests
from window_tests import private_ui_compile_args

ROOT = Path(__file__).resolve().parents[2]


def window_status(out):
    production = shell.definition((ROOT / "port/platform/common/window.cpp").read_text(),
                                  r'extern "C" uint32_t CE_SessionWindowStatus\(\)')
    source = r"""
#include <stdint.h>
#include <cstdio>
#include <cstdlib>
enum {CE_SESSION_UNFOCUSED=1,CE_SESSION_MINIMIZED=2,CE_SESSION_HIDDEN=4,CE_SESSION_NO_WINDOW=8};
using SDL_WindowFlags=uint64_t;
enum {SDL_WINDOW_INPUT_FOCUS=1,SDL_WINDOW_MINIMIZED=2,SDL_WINDOW_HIDDEN=4};
struct SDL_Window { SDL_WindowFlags flags; } owned;
static bool exists;static unsigned reads,checks;
SDL_Window *PortMainSdlWindow(){return exists?&owned:nullptr;}
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *w){if(w!=&owned)std::abort();++reads;return w->flags;}
""" + production + r"""
void Check(bool c){++checks;if(!c)std::exit(2);}
int main(){
 Check(CE_SessionWindowStatus()==8&&reads==0);
 exists=true;
 for(unsigned flags=0;flags<8;++flags){
   owned.flags=flags;
   unsigned expected=(flags&1?0:1)|(flags&2?2:0)|(flags&4?4:0);
   Check(CE_SessionWindowStatus()==expected);
 }
 Check(reads==8);
 std::printf("{\"checks\":%u,\"flag_reads\":%u}\n",checks,reads);
}
"""
    path = out / "window-status.cpp"
    path.write_text(source)
    binary = out / "window-status"
    proc = subprocess.run(["clang++", "-std=c++17", "-O1", "-fsanitize=address,undefined",
                           str(path), "-o", str(binary)], capture_output=True, text=True, timeout=30)
    (out / "window-build.log").write_text(proc.stdout + proc.stderr)
    result = {"compile_exit": proc.returncode, "production_sha256": shell.digest(production.encode())}
    if not proc.returncode:
        proc = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5)
        result.update(exit=proc.returncode, observed=json.loads(proc.stdout) if not proc.returncode else {})
        (out / "window-status.log").write_text(proc.stdout + proc.stderr)
    return result


def offscreen_status(out):
    """Use the pinned real SDL offscreen backend; draw nothing and open no desktop window."""
    source = window_tests.DRIVER.replace("WINDOW_CPP", str(ROOT / "port/platform/common/window.cpp")).replace(
        "PORTLOG_CPP", str(ROOT / "port/platform/common/portlog.cpp")).replace(
        "INVENTORY_CPP", str(ROOT / "port/platform/common/inventory.cpp"))
    # The visibility-only fixture has no authored mouse diagnostic stream. Match the real C++ linkage and fail
    # immediately if that otherwise-unowned boundary becomes reachable; no input-device implementation is replaced.
    # (window_tests.DRIVER carries inert stand-ins for the same two functions: they give way to the failing ones.)
    source = re.sub(r"^void PortDinputDiagnosticMouse(?:Event|End)\([^\n]*\{\}[^\n]*\n", "", source, flags=re.M)
    source = source.replace("void PortMouseKeepRelative() {}", r"""void PortMouseKeepRelative() {}
static unsigned diagnosticMouseCalls;
void PortDinputDiagnosticMouseEvent(SDL_Window *, const SDL_Event *, bool) {
    ++diagnosticMouseCalls; fprintf(stderr,"unexpected diagnostic mouse event in visibility fixture\n"); _Exit(12);
}
void PortDinputDiagnosticMouseEnd(SDL_Window *) {
    ++diagnosticMouseCalls; fprintf(stderr,"unexpected diagnostic mouse end in visibility fixture\n"); _Exit(12);
}
""")
    source = source.replace('if (!strcmp(argv[1], "restore")) {', '''if (!strcmp(argv[1], "unexpected-diagnostic-event")) {
        SDL_Event event{}; event.type=SDL_EVENT_MOUSE_MOTION;
        PortDinputDiagnosticMouseEvent(w,&event,true);
    } else if (!strcmp(argv[1], "unexpected-diagnostic-end")) {
        PortDinputDiagnosticMouseEnd(w);
    } else if (!strcmp(argv[1], "network-status")) {
        unsigned initial=CE_SessionWindowStatus();
        CHECK(initial==0);
        CHECK(SDL_HideWindow(w)); Drain();
        unsigned hidden=CE_SessionWindowStatus();
        CHECK((hidden & (CE_SESSION_HIDDEN|CE_SESSION_UNFOCUSED))==(CE_SESSION_HIDDEN|CE_SESSION_UNFOCUSED));
        CHECK(SDL_ShowWindow(w)); Drain();
        unsigned shown=CE_SessionWindowStatus();
        CHECK(shown==0);
        CHECK(diagnosticMouseCalls==0);
        printf("{\\"driver\\":\\"%s\\",\\"initial\\":%u,\\"hidden\\":%u,\\"shown\\":%u,\\"diagnostic_mouse_calls\\":%u}\\n",
            SDL_GetCurrentVideoDriver(),initial,hidden,shown,diagnosticMouseCalls);
    } else if (!strcmp(argv[1], "restore")) {''')
    path = out / "offscreen-status.cpp"
    path.write_text(source)
    binary = out / "offscreen-status"
    command = [str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17",
               "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar", "-fms-extensions",
               "-fno-autolink", "-w", "-O0", f"-I{deps.PORT / 'include/linux'}",
               f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}", f"-I{deps.PREFIX / 'include'}", str(path),
               f"-L{deps.PREFIX / 'lib'}", "-lSDL3", f"-Wl,-rpath,{deps.PREFIX / 'lib'}", "-o", str(binary)]
    proc = subprocess.run(command + private_ui_compile_args(ROOT / "port"), capture_output=True, text=True, timeout=45)
    (out / "offscreen-build.log").write_text(proc.stdout + proc.stderr)
    result = {"compile_exit": proc.returncode, "compile": command}
    if not proc.returncode:
        env = {k: v for k, v in os.environ.items() if not k.startswith("BEA_")}
        env.update(SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy")
        proc = subprocess.run([str(binary), "network-status"], env=env, capture_output=True, text=True, timeout=10)
        (out / "offscreen-status.log").write_text(proc.stdout + proc.stderr)
        result.update(exit=proc.returncode, observed=json.loads(proc.stdout) if not proc.returncode else {})
        result["diagnostic_boundary_controls"] = []
        for mode, reason in (("unexpected-diagnostic-event", "unexpected diagnostic mouse event"),
                             ("unexpected-diagnostic-end", "unexpected diagnostic mouse end")):
            proc = subprocess.run([str(binary), mode], env=env, capture_output=True, text=True, timeout=10)
            (out / (mode + ".log")).write_text(proc.stdout + proc.stderr)
            result["diagnostic_boundary_controls"].append({"case": mode, "exit": proc.returncode,
                                                           "passed": proc.returncode == 12 and reason in proc.stderr})
    return result


def main():
    folder = ROOT / "local-data/port/network-shell-tests"
    folder.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=folder))
    receipt = {"scope": __doc__, "patches": {}, "cases": []}
    for name in shell.FILES:
        (out / name).write_bytes((ROOT / "src" / name).read_bytes())
    for patch in sorted((ROOT / "port/patches").glob("*.patch")):
        sections = [s for s in re.split(r"(?=^--- a/)", patch.read_text(), flags=re.M)
                    if any(s.startswith("--- a/" + name + "\n") for name in shell.FILES)]
        if sections:
            proc = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1"], cwd=out,
                                  input="".join(sections), text=True, capture_output=True, timeout=10)
            receipt["patches"][patch.name] = shell.digest(patch.read_bytes())
            if proc.returncode:
                raise RuntimeError(proc.stdout + proc.stderr)
    sources = {name: (out / name).read_text() for name in shell.FILES}
    ce_source = (ROOT / "port/platform/common/ce.cpp").read_text()
    bodies = shell.functions(sources, ce_source, [], False)     # the harness declares sPortQuitRequested itself
    source = shell.HARNESS[:shell.HARNESS.index("int main(")]
    source = source.replace("int CE_SessionActive() { return 0; }", "int CE_SessionActive() { return 1; }")
    source = source.replace("void CE_SessionGameSetQuit(int) { throw UnexpectedCall{}; }",
                            "void CE_SessionGameSetQuit(int code) { GAME.SetQuit(code); }")
    source = source.replace("int CE_SessionSystemService(int, int) { throw UnexpectedCall{}; }", """
static unsigned services, not_ready, device_lost, ready_probes, resets, service_limit;
static bool device_case, shared_case;
int CE_SessionSystemService(int ready, int device) {
    ++services; if (!ready) ++not_ready;
    if (device==2) ++device_lost; else if (device==1) ++ready_probes;
    if (services>=service_limit && !device_case && !shared_case) { GAME.SetQuit(6); return CE_SESSION_EXIT; }
    return !ready || device==2 || (shared_case && services<4) ? CE_SESSION_WAIT : 0;
}
""")
    # Device's definition precedes the session boundary; retain the controls ahead of that definition.
    controls = "static bool lose_device; static unsigned reset_calls;\n"
    source = source.replace("struct Device {", controls + "struct Device {")
    source = source.replace("HRESULT TestCooperativeLevel() { ++calls; return S_OK; }",
                            "HRESULT TestCooperativeLevel() { ++calls; return lose_device && calls<=2 ? D3DERR_DEVICELOST : lose_device && calls==3 ? D3DERR_DEVICENOTRESET : S_OK; }")
    source = source.replace("HRESULT Reset3DEnvironment(BOOL, BOOL) { throw UnexpectedCall{}; }",
                            "HRESULT Reset3DEnvironment(BOOL, BOOL) { ++reset_calls; return S_OK; }")
    source = source.replace("void Sleep(unsigned) { ++sleep_calls; throw UnexpectedCall{}; }",
                            "static unsigned slept_ms; void Sleep(unsigned ms) { ++sleep_calls; slept_ms+=ms; }")
    source = source.replace("if (++poll_count > 12)", "if (++poll_count > 4001)")
    source += r"""
static unsigned checks;
void Check(bool value,const char *why) { ++checks; if(!value) { std::fprintf(stderr,"FAIL %s\n",why);std::exit(2); } }
int main(int argc,char **argv) {
    if(argc!=2)return 1;
    setenv("BEA_PORT_CE_LAYER","1",1);
    const char *mode=argv[1];
    Device device;D3D d3d;LT.m_pd3dDevice=&device;LT.m_pD3D=&d3d;
    device_case=!std::strcmp(mode,"device-recovery");shared_case=!std::strcmp(mode,"shared-wait");
    lose_device=device_case;LT.m_bActive=device_case||shared_case;LT.m_bWindowed=FALSE;
    service_limit=3501;
    if(!std::strcmp(mode,"real-quit"))queue={WM_QUIT};
    int quit=CPCPlatform{}.Process();
    Check(get_calls==0,"network inactivity never enters blocking GetMessage");
    if(device_case) {
        Check(quit==QT_NONE&&device.calls==4&&reset_calls==1,"native lost/lost/notreset/ready path resets once");
        Check(services==4&&device_lost==3&&ready_probes==1,"every lost-device iteration services the session");
        Check(sleep_calls==3&&slept_ms==30,"device wait yields exactly once per bounded iteration");
    } else if(shared_case) {
        Check(quit==QT_NONE&&services==4,"native loop waits until shared-session service releases it");
        Check(sleep_calls==3&&slept_ms==30,"shared host wait remains bounded and yielding");
    } else if(!std::strcmp(mode,"real-quit")) {
        Check(GAME.value==QT_QUIT_TO_SYSTEM&&services==0,"real WM_QUIT preserves the system quit before device work");
        Check(device.calls==0&&sleep_calls==0,"real quit does not render or wait on a device");
    } else {
        Check(services==3501&&not_ready==3501&&GAME.value==6,"thirty-five seconds of inactive service exits on its genuine session failure");
        Check(sleep_calls==3500&&slept_ms==35000&&device.calls==0,"inactive loop yields without touching render device");
    }
    Check(!translate_calls||dispatch_calls==translate_calls,"native message dispatch remains paired");
    std::printf("{\"checks\":%u,\"services\":%u,\"get_calls\":%u,\"slept_ms\":%u}\n",checks,services,get_calls,slept_ms);
}
"""
    for key, value in bodies.items():
        source = source.replace("@" + key + "@", value)
    (out / "driver.cpp").write_text(source)
    binary = out / "driver"
    command = ["clang++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
               '-DCE_VERSION="test"', str(out / "driver.cpp"), "-o", str(binary)]
    compiled = subprocess.run(command, capture_output=True, text=True, timeout=30,
                              env=dict(os.environ, TMPDIR=str(out)))
    (out / "build.log").write_text(compiled.stdout + compiled.stderr)
    receipt["compile_exit"] = compiled.returncode
    if not compiled.returncode:
        for mode in ("inactive", "device-recovery", "shared-wait", "real-quit"):
            proc = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=5)
            (out / (mode + ".log")).write_text(proc.stdout + proc.stderr)
            receipt["cases"].append({"case": mode, "exit": proc.returncode,
                                      "observed": json.loads(proc.stdout) if not proc.returncode else {}})
    receipt["driver_sha256"] = shell.digest((out / "driver.cpp").read_bytes())
    receipt["functions_sha256"] = {key: shell.digest(value.encode()) for key, value in bodies.items()}
    receipt["window_status"] = window_status(out)
    receipt["offscreen_status"] = offscreen_status(out)
    receipt["pass"] = not compiled.returncode and len(receipt["cases"]) == 4 and all(
        not row["exit"] and row["observed"].get("checks", 0) for row in receipt["cases"])
    receipt["pass"] = receipt["pass"] and not receipt["window_status"]["compile_exit"] and not receipt[
        "window_status"].get("exit", 1) and receipt["window_status"]["observed"].get("checks", 0) == 10
    receipt["pass"] = receipt["pass"] and not receipt["offscreen_status"]["compile_exit"] and not receipt[
        "offscreen_status"].get("exit", 1) and receipt["offscreen_status"]["observed"].get("driver") == "offscreen" and receipt["offscreen_status"]["observed"].get("diagnostic_mouse_calls", -1) == 0
    boundary_controls = receipt["offscreen_status"].get("diagnostic_boundary_controls", [])
    receipt["pass"] = receipt["pass"] and len(boundary_controls) == 2 and all(row["passed"] for row in boundary_controls)
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if receipt["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
