#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The developer information text's place and size (PortCeDevStatsDraw, port/game/ce_console_game.cpp). Its lines from the window's
size to its two draws are run by an executed driver (devstats_layout_driver.cpp): main's draws, exactly, wherever the window is no
larger than 640 x 480, and hand-worked places and sizes above it. Each one-line weakening below must be caught by that driver."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/game/ce_console_game.cpp"
FUNCTION = "void PortCeDevStatsDraw()"
FIRST = "\tconst float width = (float)PLATFORM.GetWindowWidth()"   # the first of the lines under test; they run to the function's end

# (name, old, new): old must occur once in the lines under test.
MUTANTS = (
    ("the factor may fall below 1", "const float layout = fit > 1.f ? fit : 1.f;", "const float layout = fit;"),
    ("the factor is the larger of the two ratios", "(width / 640.f < height / 480.f)", "(width / 640.f > height / 480.f)"),
    ("the place stays (40, 30)", "const float x = 40.f * layout, y = 30.f * layout;", "const float x = 40.f, y = 30.f;"),
    ("the margin stays 48 pixels", "const float room = width - 48.f * layout;", "const float room = width - 48.f;"),
    ("a block's width is compared at the font's own size", "(widest * layout > room", "(widest > room"),
    ("the block keeps the font's own size", "? room / widest : layout;", "? room / widest : 1.f;"),
    ("the shadow stays one pixel off", "DrawTextScaled(x + layout, y + layout,", "DrawTextScaled(x + 1.f, y + 1.f,"),
)


def lines_under_test():
    source = SOURCE.read_text()
    start = source.index(FUNCTION)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += {"{": 1, "}": -1}.get(source[end], 0)
        end += 1
    body = source[brace + 1:end - 1]
    if body.count(FIRST) != 1 or body.count("font->DrawTextScaled(") != 2:
        raise RuntimeError("%s: the place-and-size lines or the two draws are not where this test expects them" % FUNCTION)
    return body[body.index(FIRST):]


def run(out, lines, name):
    """Compiles the driver around these lines and runs it: its exit status (0 every case holds, 2 a case does not)."""
    (out / "devstats_layout.inc").write_text(lines)
    binary = out / name
    subprocess.run(["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(out),
                    str(ROOT / "port/tools/devstats_layout_driver.cpp"), "-o", str(binary)], check=True, timeout=60)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
    return result.returncode, (result.stdout + result.stderr).strip()


def main():
    lines = lines_under_test()
    parent = ROOT / "local-data/port/devstats-layout-tests"
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="case-", dir=parent) as directory:
        out = Path(directory)
        status, said = run(out, lines, "devstats-layout")
        if status != 0:
            raise RuntimeError("the devstats lines fail the driver (exit %d): %s" % (status, said))
        print(said)
        for number, (name, old, new) in enumerate(MUTANTS):
            if lines.count(old) != 1:
                raise RuntimeError("mutant %r: its text is not in the lines under test exactly once" % name)
            status, said = run(out, lines.replace(old, new), "devstats-mutant-%d" % number)
            if status != 2:    # only the driver's own FAIL counts; a crash or another exit is not a catch
                raise RuntimeError("mutant %r was not caught by the executed driver (exit %d): %s" % (name, status, said))
    print("Devstats place and size: main's draws up to 640 x 480 and the worked sizes above it hold; "
          "%d mutants caught by the executed driver: PASS" % len(MUTANTS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
