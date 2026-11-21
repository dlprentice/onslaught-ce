#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The order in which each of the 18 cells of SpawnRound's two orientation products adds its three terms (patch 0110).

CWeapon::SpawnRound forms `(orientation * launchAngle) * inaccuracy` from the two file-local operators FSpawnOri::operator* and
FSpawnTurn::operator* of src/weapon.cpp. A float sum is not associative: for a cell `t0 + t1 + t2` of one row of the left matrix
and one column of the right, which two terms are added first changes the last bit of the result. Patch 0110 writes every cell
as `((a + b) + c)` in the order the original program adds its terms. PINNED below is that order, cell by cell; nothing else in
the repository states all 18 cells in one place, and until this test a wrong cell was caught by nothing but a replay of a level.

What it does, with no game data and no original program (port/tools/tests/weapon_spawn_order_driver.cpp):

- takes FSpawnOri and FSpawnTurn exactly as the build's patched weapon.cpp holds them, and the build's maths.h;
- compiles them for Linux i686 with the game's own pipeline (build.py's COMMON, X87 and NO_MATH_BUILTINS flags, the /QIfist
  rewrite, then the optimised object), links a driver, and runs it at the game's x87 control word, 007f;
- feeds each product a pair of matrices chosen so that, in every cell, the three orders of its three terms give three different
  floats (the choice is searched here, deterministically, with an exact software model of the single-precision x87 unit);
- checks each cell's bits against the pinned order evaluated two independent ways: explicit x87 instructions in the driver, and
  this file's own exact arithmetic (every multiply and add rounded once to 24 bits). The two must agree before either counts.

Mutants: each of the 18 cells rewritten, one at a time, to each of the other two orders (36), and the four cells as patch 0110
first had them, are compiled and must each be caught, the failing cell named. The baseline runs first and must pass.

    python3 -I port/tools/weapon_spawn_order_tests.py [--no-mutants] [--only-mutants NAME,...] [--jobs N] [--keep]

The compiled drivers go to a run folder under local-data/port/weapon-spawn-order-tests; it is removed when every check
passes (--keep keeps it, with its results.json) and kept when a check fails or a compile stops the run.

Linux only: nothing here is compiled or run for the Windows target. That target is built from the same patched source for
the same x87 instructions, so the same cell orders are expected there; that is an inference, not something this test measures.
"""
import argparse
import json
import random
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402

DRIVER = ROOT / "port/tools/tests/weapon_spawn_order_driver.cpp"
STRUCTS = ("FSpawnOri", "FSpawnTurn")
COLS = "XYZ"

# Cell (row i, column j): term k is Row[i].k * m.Row[k].j (k = 0, 1, 2 for X, Y, Z). Each entry is ((first, second), last): the
# two terms added first (either way round: one add is commutative) and the term added to that sum. Rows top to bottom, columns
# X, Y, Z. FSpawnOri forms orientation * launch angle; FSpawnTurn forms that product * the inaccuracy matrix.
PINNED = {
    "FSpawnOri": [
        [((1, 2), 0), ((0, 2), 1), ((0, 2), 1)],
        [((1, 2), 0), ((0, 2), 1), ((1, 2), 0)],
        [((1, 2), 0), ((0, 2), 1), ((1, 2), 0)],
    ],
    "FSpawnTurn": [
        [((1, 2), 0), ((1, 2), 0), ((1, 2), 0)],
        [((0, 1), 2), ((1, 2), 0), ((1, 2), 0)],
        [((0, 2), 1), ((1, 2), 0), ((1, 2), 0)],
    ],
}
# The cells as patch 0110 first wrote them, where they differed from PINNED (the report on onslaught-decomp issue 1).
FIRST_WRITTEN = {("FSpawnOri", 0, 2): ((1, 2), 0), ("FSpawnOri", 2, 2): ((0, 2), 1),
                 ("FSpawnTurn", 0, 0): ((0, 2), 1), ("FSpawnTurn", 2, 0): ((1, 2), 0)}


def cells():
    return [(name, r, c) for name in STRUCTS for r in range(3) for c in range(3)]


# ---------------------------------------------------------------------------------------------------------------------------
# The single-precision x87 unit, modelled exactly: every multiply and add is rounded once to a 24-bit mantissa, nearest even.
# (The extended exponent range never matters here: the chosen values are of ordinary size.)


def round24(q):
    q = Fraction(q)
    if q == 0:
        return q
    sign, q = (-1 if q < 0 else 1), abs(q)
    e = q.numerator.bit_length() - q.denominator.bit_length()
    if Fraction(2) ** e > q:
        e -= 1
    elif Fraction(2) ** (e + 1) <= q:
        e += 1
    scale = Fraction(2) ** (e - 23)
    m = q / scale
    n = m.numerator // m.denominator
    rest = m - n
    if rest > Fraction(1, 2) or (rest == Fraction(1, 2) and n % 2):
        n += 1
    return sign * n * scale


def cell_value(left, right, order):
    """((t[a] + t[b]) + t[c]) with t[k] = left[k] * right[k], each operation rounded once: an exact Fraction."""
    (a, b), c = order
    t = lambda k: round24(Fraction(left[k]) * Fraction(right[k]))
    return round24(round24(t(a) + t(b)) + t(c))


def float_bits(q):
    import struct
    return struct.unpack("<I", struct.pack("<f", float(q)))[0]


def orders_of(order):
    """The pinned order and the other two orders of the same three terms (the last term is each other term in turn)."""
    (a, b), c = order
    return [order, ((a, c), b), ((b, c), a)]


def separates(left_row, right_column):
    values = [float_bits(cell_value(left_row, right_column, o)) for o in orders_of(((0, 1), 2))]
    return len(set(values)) == 3


def float32(rng):
    return float(round24(Fraction(rng.uniform(0.5, 2.0)))) * rng.choice((-1, 1))


def choose_matrices(seed):
    """Left rows and right columns, each as float values, such that all 9 (row, column) pairs separate the three orders.
    Cell values depend only on one row and one column, so three columns are drawn and then three rows that each separate with all three."""
    rng = random.Random(seed)
    columns = [[float32(rng) for _ in range(3)] for _ in range(3)]
    rows = []
    while len(rows) < 3:
        row = [float32(rng) for _ in range(3)]
        if all(separates(row, column) for column in columns):
            rows.append(row)
    return rows, columns          # left[i] = Row[i]; right[k][j] = columns[j][k]


def header(data):
    def lit(x):
        return float(x).hex() + "f"
    out = ["// generated by weapon_spawn_order_tests.py: the matrices and the pinned order of every cell\n"]
    for key, name in (("LEFT", "left"), ("RIGHT", "right")):
        out.append("static const float %s[2][3][3] = {\n" % key)
        for p in range(2):
            rows = data[p][name]
            out.append("\t{" + ", ".join("{" + ", ".join(lit(x) for x in row) + "}" for row in rows) + "},\n")
        out.append("};\n")
    out.append("static const int ORDER[2][3][3][3] = {\n")
    for name in STRUCTS:
        out.append("\t{" + ", ".join("{" + ", ".join("{%d, %d, %d}" % (o[0][0], o[0][1], o[1]) for o in row) + "}"
                                     for row in PINNED[name]) + "},\n")
    out.append("};\n")
    return "".join(out)


# ---------------------------------------------------------------------------------------------------------------------------
# The source: the two structs as the build holds them, and a cell rewritten.


def struct_text(weapon):
    parts = []
    for name in STRUCTS:
        m = re.search(r"^struct %s\b.*?\n};\n" % name, weapon, re.M | re.S)
        assert m, "no struct " + name
        parts.append(m.group(0))
    return "\n".join(parts)


def top_level(text):
    """Split on the commas outside any bracket."""
    pieces, depth, current = [], 0, ""
    for ch in text:
        if ch == "," and depth == 0:
            pieces.append(current)
            current = ""
            continue
        depth += ch == "("
        depth -= ch == ")"
        current += ch
    pieces.append(current)
    return pieces


def cell_text(row, col, order):
    (a, b), c = order
    term = lambda k: "Row[%d].%s * m.Row[%d].%s" % (row, COLS[k], k, COLS[col])
    return "((%s + %s) + %s)" % (term(a), term(b), term(c))


def rewrite(source, edits):
    """The structs' text with the cells in edits {(struct, row, col): order} written as ((a + b) + c) in that order."""
    out = source
    for name in STRUCTS:
        start = re.search(r"^struct %s\b" % name, out, re.M).start()
        end = out.index("\n};\n", start)
        body = out[start:end]
        ret = body.index("return FMatrix(") + len("return FMatrix")
        close = len(body)
        depth = 0
        for i in range(ret, len(body)):
            depth += body[i] == "("
            depth -= body[i] == ")"
            if depth == 0:
                close = i
                break
        rows = top_level(body[ret + 1:close])
        assert len(rows) == 3, name
        new_rows = []
        for r, vector in enumerate(rows):
            head = vector[:vector.index("FVector(") + len("FVector(")]
            inner = vector[len(head):vector.rindex(")")]
            parts = top_level(inner)
            assert len(parts) == 3, (name, r)
            for c in range(3):
                if (name, r, c) in edits:
                    lead = re.match(r"\s*", parts[c]).group(0)
                    parts[c] = lead + cell_text(r, c, edits[(name, r, c)])
            new_rows.append(head + ",".join(parts) + ")")
        out = out[:start] + body[:ret + 1] + ",".join(new_rows) + body[close:] + out[end:]
    return out


# ---------------------------------------------------------------------------------------------------------------------------
# Compiling and running.


class Fixture:
    def __init__(self, parent, maths, data):
        self.parent, self.maths, self.data = parent, maths, data
        self.target = build.TARGETS["linux"]

    def make(self, name, source):
        folder = self.parent / name
        folder.mkdir()
        (folder / "maths.h").write_text(self.maths)
        (folder / "spawn_products.inc").write_text(source)
        (folder / "spawn_case.h").write_text(header(self.data))
        ir, obj, binary = folder / "driver.ll", folder / "driver.o", folder / "driver"
        flags = [*self.target["cxx"], "-std=gnu++14", *build.MSCXX, *build.COMMON, *build.X87, *build.NO_MATH_BUILTINS]
        commands = [
            [*flags, "-I" + str(folder), "-S", "-emit-llvm", "-Xclang", "-disable-llvm-passes", "-o", str(ir), str(DRIVER)],
            [sys.executable, str(build.PORT / "tools/qifist.py"), str(ir)],
            [*self.target["cxx"], *build.COMMON, *build.X87, "-c", "-o", str(obj), str(ir)],
            [*self.target["cxx"], *build.X87, str(obj), "-no-pie", *build.deps.sysroot_link_flags(), "-lm", "-o", str(binary)],
        ]
        log = ""
        for cmd in commands:
            done = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
            log += done.stdout + done.stderr
            if done.returncode:
                (folder / "build.log").write_text(log)
                raise RuntimeError("%s: %s failed:\n%s" % (name, Path(cmd[0]).name, log[-1500:]))
        (folder / "build.log").write_text(log)
        return binary

    def run(self, binary):
        done = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        rows = {}
        for line in done.stdout.splitlines():
            m = re.fullmatch(r"CELL (\w+) (\d) ([XYZ]) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) (\w+)", line)
            if m:
                rows[(m.group(1), int(m.group(2)), COLS.index(m.group(3)))] = dict(
                    expected=int(m.group(4), 16), actual=int(m.group(5), 16), alternatives=[int(m.group(6), 16), int(m.group(7), 16)],
                    verdict=m.group(8))
        control = re.search(r"^CONTROL ([0-9a-f]{4})$", done.stdout, re.M)
        return done.returncode, rows, control.group(1) if control else None, done.stdout


def model_bits(data, name, row, col):
    """Each cell's pinned bits and its two alternatives from this file's own arithmetic."""
    p = STRUCTS.index(name)
    left = data[p]["left"][row]
    right = [data[p]["right"][k][col] for k in range(3)]
    return [float_bits(cell_value(left, right, o)) for o in orders_of(PINNED[name][row][col])]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--no-mutants", action="store_true")
    ap.add_argument("--only-mutants", help="comma-separated mutant names: run only those after the baseline")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--keep", action="store_true", help="keep the run folder even when every check passes")
    args = ap.parse_args()
    parent = ROOT / "local-data/port/weapon-spawn-order-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    # The run folder holds every compiled driver (megabytes with the mutants): it stays only as evidence of a failure.
    passed = False
    try:
        passed = run_checks(args, out) == 0
    finally:
        if passed and not args.keep:
            shutil.rmtree(out)
            print("every check passed: run folder removed (--keep keeps it)")
        else:
            print("run folder kept: %s" % out)
    return 0 if passed else 1


def run_checks(args, out):
    texts = build.patched_text(["maths.h", "weapon.cpp"], network=False)
    source = struct_text(texts["weapon.cpp"])

    # Matrices: one pair for each product, searched with the exact model.
    data = []
    for p in range(2):
        left, columns = choose_matrices(1100 + p)
        data.append({"left": left, "right": [[columns[j][k] for j in range(3)] for k in range(3)]})
    results, failed = [], []

    def record(name, ok, detail=""):
        results.append({"check": name, "passed": ok, "detail": detail})
        print(("PASS " if ok else "FAIL ") + name + (": " + detail if detail else ""), flush=True)
        if not ok:
            failed.append(name)

    # Controls on the data and on the model itself, before anything is compiled.
    for name, r, c in cells():
        bits = model_bits(data, name, r, c)
        record("the matrices tell %s row %d column %s's three orders apart" % (name, r, COLS[c]), len(set(bits)) == 3)
    assert round24(Fraction(1) + Fraction(1, 2 ** 24)) == 1 and round24(Fraction(1) + Fraction(3, 2 ** 24)) == 1 + Fraction(1, 2 ** 22), \
        "the model's ties do not go to even"

    fixture = Fixture(out, texts["maths.h"], data)
    baseline = fixture.make("baseline", source)
    code, rows, control, _ = fixture.run(baseline)
    record("baseline: the game's x87 control word is set", control == "007f", str(control))
    record("baseline: 18 cells reported", len(rows) == 18, str(len(rows)))
    for name, r, c in cells():
        row = rows.get((name, r, c))
        if not row:
            record("baseline: %s row %d column %s" % (name, r, COLS[c]), False, "missing")
            continue
        model = model_bits(data, name, r, c)
        record("baseline: %s row %d column %s equals its pinned order (x87 evaluation and exact model agree)" % (name, r, COLS[c]),
               row["verdict"] == "ok" and row["actual"] == row["expected"] == model[0] and row["alternatives"] == model[1:],
               "actual %08x expected %08x model %08x" % (row["actual"], row["expected"], model[0]))
    record("baseline: the driver exits 0", code == 0, str(code))
    if failed:
        print("the baseline failed: no mutants are run")
        (out / "results.json").write_text(json.dumps(results, indent=1))
        return 1

    mutants = {}      # name -> (edits, the cells that must be reported wrong)
    for name, r, c in cells():
        for k, order in enumerate(orders_of(PINNED[name][r][c])[1:], 1):
            mutants["%s-r%d%s-order%d" % (name, r, COLS[c], k)] = ({(name, r, c): order}, {(name, r, c)})
    mutants["as-first-written-in-0110"] = ({key: order for key, order in FIRST_WRITTEN.items()}, set(FIRST_WRITTEN))
    if args.only_mutants:
        wanted = set(x for x in args.only_mutants.split(",") if x)
        assert wanted <= set(mutants), "no such mutant: " + ", ".join(sorted(wanted - set(mutants)))
        mutants = {k: v for k, v in mutants.items() if k in wanted}
    elif args.no_mutants:
        mutants = {}

    def one(item):
        name, (edits, wrong) = item
        text = rewrite(source, edits)
        assert text != source, name
        binary = fixture.make("mutant-" + name, text)
        code, rows, control, _ = fixture.run(binary)
        reported = {key for key, row in rows.items() if row["verdict"] == "WRONG"}
        return name, code, reported, wrong

    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for name, code, reported, wrong in pool.map(one, sorted(mutants.items())):
            record("mutant %s is caught" % name, code == 1 and reported == wrong,
                   "exit %d, wrong cells %s" % (code, sorted("%s r%d %s" % (n, r, COLS[c]) for n, r, c in reported)))
    (out / "results.json").write_text(json.dumps(results, indent=1))
    print("%d checks, %d failed" % (len(results), len(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
