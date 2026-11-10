#!/usr/bin/env python3
"""Check the actual lockstep random-state scope with local fake accessors."""

from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/game/ce_network_lockstep.cpp"
DRIVER = ROOT / "port/tools/tests/network_lockstep_rng_scope_driver.cpp"


def extract_function(source, name):
    match = re.search(r"(?m)^[^\n;{}]*\b" + name + r"\s*\([^;{}]*\)\s*\{", source)
    if not match:
        raise ValueError("lockstep random scope function missing")
    start, pos, depth = match.start(), match.end(), 1
    while pos < len(source) and depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    if depth:
        raise ValueError("lockstep random scope function is unterminated")
    return source[start:pos]


def main():
    compiler = shutil.which("c++")
    if not compiler:
        print("lockstep RNG scope: FAIL; compiler unavailable")
        return 1
    source = SOURCE.read_text()
    declarations = [line for line in source.splitlines()
                    if "hold_depth" in line and "static" in line]
    if len(declarations) != 1:
        print("lockstep RNG scope: FAIL; scope state declaration changed")
        return 1
    enter = extract_function(source, "CE_LockstepCrtEnter")
    leave = extract_function(source, "CE_LockstepCrtLeave")
    fixture = DRIVER.read_text()
    marker = "/* INSERT_SCOPE_DEFINITIONS */"
    if fixture.count(marker) != 1:
        print("lockstep RNG scope: FAIL; fixture marker changed")
        return 1
    output = Path(tempfile.mkdtemp(prefix="ce-lockstep-rng-scope-"))

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

    definitions = "\n".join((declarations[0], enter, leave))
    result = run_variant("control", definitions)
    if result != 0:
        print("lockstep RNG scope: FAIL; synthetic case", result,
              "; private evidence:", output)
        return 1
    mutant_enter, count = re.subn(r"if\s*\(\s*hold_depth\s*\+\+\s*\)\s*return\s*;",
                                  "++hold_depth;", enter, count=1)
    if count != 1:
        print("lockstep RNG scope: FAIL; nested-scope mutation anchor changed")
        return 1
    mutant = "\n".join((declarations[0], mutant_enter, leave))
    if run_variant("mutant", mutant) != 2:
        print("lockstep RNG scope: FAIL; nested-scope mutant not rejected")
        return 1

    print("lockstep RNG scope: PASS; nested-scope mutant rejected;")
    print("private evidence:", output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
