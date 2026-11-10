#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A refused network start exits through ExitCode without walking an uninitialized heap.

WinMain registers atexit(ExitCode) before CLIPARAMS and the network boot admission, and MEM_MANAGER.Init comes after
them. On Windows a refusal returns through the CRT's exit, which ran ExitCode's memory-manager cleanup over heaps with
no memory (an access violation writing near 0); Linux's main ends with _exit and never ran ExitCode. The patched mirror
(every patch, as build.py applies them) is checked for that order, and the actual patched ExitCode is compiled against
recording stubs: a refused start still restores the accessibility keys and skips the cleanup; a started game cleans up
as before. This is a host model of the shell's exit, not Windows runtime acceptance (that is a Wine run of bea.exe).
"""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r"""
#include <cstdio>
struct STICKYKEYS { unsigned cbSize, dwFlags; };
struct TOGGLEKEYS { unsigned cbSize, dwFlags; };
struct FILTERKEYS { unsigned cbSize, dwFlags; };
enum { SPI_SETSTICKYKEYS = 1, SPI_SETTOGGLEKEYS, SPI_SETFILTERKEYS };
static int restored, freed, shutdowns, cleanups, heap_shutdowns;
static int SystemParametersInfo(int, int, void *, int) { ++restored; return 1; }
static void TRACE(const char *) {}
struct { void FreeData() { ++freed; } } PATCHMANAGER;
struct Reconnect { void Shutdown() { ++shutdowns; } } RECONNECT_INTERFACE[2];
struct { void SetMerge(int) {} void Cleanup() { ++cleanups; } void Shutdown() { ++heap_shutdowns; } } MEM_MANAGER;
#define TRUE 1
static STICKYKEYS sStartupStickyKeys = {8, 0};
static TOGGLEKEYS sStartupToggleKeys = {8, 0};
static FILTERKEYS sStartupFilterKeys = {8, 0};
%s
int main(int argc, char **argv)
{
	const bool started = argc == 2 && argv[1][0] == 's';
	sMemoryManagerReady = started;
	ExitCode();
	std::printf("restored=%%d freed=%%d shutdowns=%%d cleanups=%%d heap_shutdowns=%%d\n", restored, freed, shutdowns,
	            cleanups, heap_shutdowns);
	const bool keys = restored == 3;
	return keys && (started ? freed == 1 && shutdowns == 2 && cleanups == 1 && heap_shutdowns == 1
	                        : freed == 0 && shutdowns == 0 && cleanups == 0 && heap_shutdowns == 0) ? 0 : 1;
}
"""


def main():
    ok = True
    spec = importlib.util.spec_from_file_location("refused_exit_build", ROOT / "port/build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    with tempfile.TemporaryDirectory(prefix="ce-refused-exit-") as tmp:
        tmp = Path(tmp)
        build.PUBLIC = True
        build.MIRROR_ROOT = tmp / "mirror"
        build.MIRROR = build.MIRROR_ROOT / "src"
        build.build_mirror()
        shell = build.network_view((build.MIRROR / "ltshell.cpp").read_text(), off=False)     # as a build with the network sees it
        # 1. The order that made the refusal reach an uninitialized heap, and the flag set only once it exists.
        winmain = shell[shell.index("int WINAPI WinMain("):]
        marks = ["\tatexit(ExitCode);", "\tif (!CE_BootStart()) return 1;",
                 "\tMEM_MANAGER.Init(PLATFORM.GetMemorySize());", "\tsMemoryManagerReady = true;"]
        at = [winmain.find(m) for m in marks]
        ordered = all(a >= 0 for a in at) and at == sorted(at) and winmain.count("sMemoryManagerReady = true;") == 1
        print(("PASS" if ordered else "FAIL") + " WinMain: atexit(ExitCode) < boot refusal < MEM_MANAGER.Init < ready flag %s" % at)
        ok &= ordered
        # 2. The actual patched ExitCode, both ways.
        start = shell.index("static bool sMemoryManagerReady;")
        end = shell.index("{", shell.index("void ExitCode()"))
        level, cursor = 1, end + 1
        while level:
            level += (shell[cursor] == "{") - (shell[cursor] == "}")
            cursor += 1
        exitcode = shell[start:cursor]
        src = tmp / "driver.cpp"
        src.write_text(DRIVER % exitcode)
        compiler = shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            raise SystemExit("a host C++ compiler is required")
        exe = tmp / "driver"
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wno-unused-function", str(src), "-o", str(exe)], check=True)
        for mode in ("refused", "started"):
            r = subprocess.run([str(exe), mode], capture_output=True, text=True, timeout=30)
            print(("PASS" if r.returncode == 0 else "FAIL") + " ExitCode after a %s start: %s" % (mode, r.stdout.strip()))
            ok &= r.returncode == 0
    print("refused network start exit: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
