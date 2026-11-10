#!/usr/bin/env python3
"""Check the loader's Windows-disabled branch without Windows or game files."""

from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/platform/common/mods.cpp"
DRIVER = ROOT / "port/tools/tests/mods_windows_disabled_driver.cpp"


WINDOWS_GUARD = r"defined\s*\(\s*_WIN32\s*\)\s*&&\s*!\s*defined\s*\(\s*PORT_MODS_WINDOWS_ON\s*\)"


def switch_function(source):
    """PlatformOff, the one switch: the function whole."""
    match = re.search(r"(?m)^const char \*PlatformOff\(\)\n\{\n.*?^\}\n", source, re.S)
    if not match or not re.search(WINDOWS_GUARD, match.group()):
        raise ValueError("Windows-disabled guard missing")
    return match.group()


def loader_prefix(source):
    """Load from its first line through the block that asks PlatformOff."""
    match = re.search(r"(?m)^[^\n;{}]*\bLoad\s*\([^;{}]*\)\s*\{", source)
    if not match:
        raise ValueError("loader entry missing")
    body = source[match.end():]
    guard = re.search(r"(?m)^\s*if \(const char \*off = PlatformOff\(\)\)\n\s*\{\n.*?\n\s*\}\n", body, re.S)
    if not guard:
        raise ValueError("Windows-disabled guard is not asked")
    return body[:guard.end()]


def main():
    compiler = shutil.which("c++")
    if not compiler:
        print("Windows mod-off control: FAIL; compiler unavailable")
        return 1
    source = SOURCE.read_text()
    switch, prefix = switch_function(source), loader_prefix(source)
    marker, switch_marker = "/* INSERT_LOAD_PREFIX */", "/* INSERT_PLATFORM_OFF */"
    fixture = DRIVER.read_text()
    if fixture.count(marker) != 1 or fixture.count(switch_marker) != 1:
        print("Windows mod-off control: FAIL; fixture marker changed")
        return 1
    output = Path(tempfile.mkdtemp(prefix="ce-mods-win-off-"))

    def run_variant(name, function, defines):
        program = output / (name + ".cpp")
        binary = output / name
        program.write_text(fixture.replace(marker, prefix).replace(switch_marker, function))
        cmd = [compiler, "-std=c++11", "-O0"]
        for define in defines.split():
            cmd.append("-D" + define)
        cmd += [str(program), "-o", str(binary)]
        built = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (output / (name + "-build.log")).write_bytes(built.stdout)
        if built.returncode:
            return None
        result = subprocess.run([str(binary)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=30)
        (output / (name + "-run.log")).write_bytes(result.stdout)
        return result.returncode

    cases = (("windows", "TEST_AS_WINDOWS"), ("windows-test-build", "TEST_AS_WINDOWS PORT_MODS_WINDOWS_ON"),
             ("explicit-off", "PORT_MODS_PLATFORM_OFF"), ("linux", ""))
    for name, define in cases:
        result = run_variant(name, switch, define)
        if result != 0:
            print("Windows mod-off control: FAIL;", name, "case", result,
                  "; private evidence:", output)
            return 1

    mutant, replacements = re.subn(WINDOWS_GUARD, "0", switch, count=1)
    if replacements != 1:
        print("Windows mod-off control: FAIL; mutation anchor changed")
        return 1
    if run_variant("mutant-windows", mutant, "TEST_AS_WINDOWS") != 2:
        print("Windows mod-off control: FAIL; Windows-guard mutant not rejected")
        return 1
    if run_variant("mutant-explicit-off", mutant, "PORT_MODS_PLATFORM_OFF") != 0:
        print("Windows mod-off control: FAIL; mutant did not retain simulated-off behavior")
        return 1

    print("Windows mod-off control: PASS; Windows-guard mutant rejected;")
    print("simulated-off control alone accepted that mutant; private evidence:", output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
