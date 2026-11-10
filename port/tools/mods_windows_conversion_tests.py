#!/usr/bin/env python3
"""Check Windows user-folder conversion reporting with fake Win32 calls."""

from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/platform/common/mods.cpp"
DRIVER = ROOT / "port/tools/tests/mods_windows_conversion_driver.cpp"


def conversion_guard(source):
    match = re.search(r"\bif\s*\(\s*!\s*MultiByteToWideChar\s*\(", source)
    if not match:
        raise ValueError("Windows user-folder conversion guard missing")
    start = match.start()
    pos = source.index("(", start)
    depth = 1
    pos += 1
    while pos < len(source) and depth:
        depth += (source[pos] == "(") - (source[pos] == ")")
        pos += 1
    if depth:
        raise ValueError("conversion condition is unterminated")
    while pos < len(source) and source[pos].isspace():
        pos += 1
    if pos >= len(source):
        raise ValueError("conversion failure branch missing")
    if source[pos] == "{":
        depth = 1
        pos += 1
        while pos < len(source) and depth:
            depth += (source[pos] == "{") - (source[pos] == "}")
            pos += 1
        if depth:
            raise ValueError("conversion failure block is unterminated")
    else:
        pos = source.index(";", pos) + 1
    return source[start:pos]


def main():
    compiler = shutil.which("c++")
    if not compiler:
        print("Windows mod conversion: FAIL; compiler unavailable")
        return 1
    guard = conversion_guard(SOURCE.read_text())
    fixture = DRIVER.read_text()
    marker = "/* INSERT_CONVERSION_GUARD */"
    if fixture.count(marker) != 1:
        print("Windows mod conversion: FAIL; fixture marker changed")
        return 1
    output = Path(tempfile.mkdtemp(prefix="ce-mod-win-conversion-"))

    def run_variant(name, body):
        program = output / (name + ".cpp")
        binary = output / name
        program.write_text(fixture.replace(marker, body))
        built = subprocess.run([compiler, "-std=c++11", "-O0", str(program), "-o", str(binary)],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (output / (name + "-build.log")).write_bytes(built.stdout)
        if built.returncode:
            return None
        result = subprocess.run([str(binary)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=30)
        (output / (name + "-run.log")).write_bytes(result.stdout)
        return result.returncode

    result = run_variant("control", guard)
    if result != 0:
        print("Windows mod conversion: FAIL; synthetic case", result,
              "; private evidence:", output)
        return 1
    needle = "error == ERROR_INSUFFICIENT_BUFFER"
    if guard.count(needle) != 1:
        print("Windows mod conversion: FAIL; mutant anchor changed")
        return 1
    mutant = guard.replace(needle, "false", 1)
    if run_variant("mutant", mutant) != 2:
        print("Windows mod conversion: FAIL; wrong-diagnostic mutant not rejected")
        return 1
    invalid_reason = '"the user folder path is not valid UTF-8"'
    if guard.count(invalid_reason) != 1:
        print("Windows mod conversion: FAIL; invalid-encoding reason changed")
        return 1
    ambiguous = guard.replace(invalid_reason,
                              '"the user folder path is not valid UTF-8 or too long"', 1)
    if run_variant("ambiguous", ambiguous) != 3:
        print("Windows mod conversion: FAIL; ambiguous-reason mutant not rejected")
        return 1

    print("Windows mod conversion: PASS; both diagnostic mutants rejected;")
    print("private evidence:", output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
