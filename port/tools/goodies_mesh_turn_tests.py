#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The arithmetic of the goodies mesh viewer's turn: the order of its nine product cells and the precision of its sines and cosines (patch 0130).

In the Goodies screen's model viewer a direction button turns the mesh: CFEPGoodies::ButtonPressed builds a rotation from
(yaw, pitch, roll) and multiplies the mesh's orientation by it. Patch 0130 writes that as GoodiesMeshTurn in src/FEPGoodies.cpp,
in the arithmetic the original program has at that place. Two things decide the last bits of the result:

- the order in which each of the nine cells of the product adds its three terms (a float sum is not associative);
- which sines and cosines are rounded to a float before use. The x87 unit's fsin and fcos return a 64-bit mantissa whatever the
  precision setting; a value stored to a float is rounded to 24 bits, a value used straight from the x87 stack is not. Here the
  three cosines, sin(yaw) and sin(roll) are stored, and sin(pitch) is used unrounded: in sp * sr, in sp * cr, and as the middle
  value of the rotation's third row, which the three cells of the product's third row multiply.

PINNED below states both. What the test does, with no game data and no original program (port/tools/tests/goodies_mesh_turn_driver.cpp):

- takes GoodiesMeshTurn exactly as the build's patched FEPGoodies.cpp holds it, and the build's maths.h;
- compiles them for Linux i686 with the game's own pipeline (build.py's COMMON, X87 and NO_MATH_BUILTINS flags, the /QIfist
  rewrite, the optimised object, the game's renaming of sinf and cosf to the port's x87 routines), links a driver with the port's
  x87math.cpp, and runs it at the game's x87 control word, 007f;
- runs these cases: three "general" ones (three non-zero angles and, for each row of the product, a matrix searched here,
  deterministically, so that the three orders of every cell of that row give three different floats, and for the third row
  also so that every cell differs when the sine is rounded first), "pitch-only" (yaw and roll zero, as the viewer calls it for
  the up and down buttons; the third-row cells differ when the sine is rounded first), "yaw-only" (as it calls it for left
  and right: a control, where no pinned choice can show), and further "angles-N" cases, each another set of three non-zero
  angles with its own matrix. One set of angles cannot tell every single use of a sine or cosine apart (rounding a value
  at one use changes the entry it feeds for some angles and not for others), so sets are drawn, deterministically, until
  every single-use choice listed below changes at least one cell of at least one case;
- checks each cell's bits against the pinned evaluation done two independent ways: one explicit x87 instruction per operation
  in the driver, and this file's own exact arithmetic on the values fsin and fcos returned in that run. The two must agree
  before either counts;
- checks, on the patched file's text, that CFEPGoodies::ButtonPressed turns the mesh by this function and by nothing else:
  one statement, mMeshOri = GoodiesMeshTurn(dx*0.1f, dy*0.1f, 0, mMeshOri), the function called nowhere else, and no other
  mention of mMeshOri in ButtonPressed. The build compiles without warnings, so an unused GoodiesMeshTurn would be silent.

Mutants, each compiled and run after the baseline has passed, each to be caught with exactly the cells this file's arithmetic
says it changes:

- every cell rewritten to each of its other two orders (18);
- the sine of the pitch rounded to a float at each of its five uses (the three third-row cells, sp * sr, sp * cr) and
  everywhere (6);
- each stored sine or cosine left unrounded everywhere (5) and at each single use (24: its uses in the eight entries of
  the rotation that are computed, a value inside sp * sr or sp * cr counted once for each entry that product feeds);
- each of the rotation's four triple products regrouped, sp * (sr * sy) for (sp * sr) * sy and the like (4), and each of its
  two subtractions turned round (2);
- the function as patch 0130 first had it, built on MakeRotationF with the third row read back as floats (1).

And mutants of the call site, made on the file's text and not compiled, each to be reported for exactly the stated reasons:
the two angles exchanged, the mesh orientation written a second time, a second call elsewhere, and the turn as patch 0130
first had it in ButtonPressed.

What this does not reach: the call site is checked as text, not as compiled code (ButtonPressed needs the whole game), and
the comparison with the original program's instructions is the private check's, not this test's.

    python3 -I port/tools/goodies_mesh_turn_tests.py [--no-mutants] [--only-mutants NAME,...] [--jobs N] [--keep]
                                                     [--helper-file FILE]

--helper-file tests another text of the function instead of the build's (to show what a different version does); the call
site is then not checked, and the run says so.
The compiled drivers go to a run folder under local-data/port/goodies-mesh-turn-tests; it is removed when every check passes
(--keep keeps it, with its results.json) and kept when a check fails or a compile stops the run.

Linux only: nothing here is compiled or run for the Windows target. The values fsin and fcos return are read from the run, not
assumed, so a processor whose last bit differs does not fail the test by itself; it could leave a case unable to tell two
choices apart, which the test reports as a failure of its data, never as a pass.
"""
import argparse
import json
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402

DRIVER = ROOT / "port/tools/tests/goodies_mesh_turn_driver.cpp"
FUNCTION = "GoodiesMeshTurn"
COLS = "XYZ"
TRIG = ("cy", "sy", "cr", "sr", "cp", "sp")       # the order of the driver's TRIG line

# Cell (row i, column j) of rotation * mesh: term k is rotation[i][k] * mesh.Row[k].j (k = 0, 1, 2 for X, Y, Z). Each entry is
# ((first, second), last): the two terms added first (either way round: one add is commutative) and the term added to that sum.
PINNED_ORDER = [
    [((0, 2), 1), ((1, 2), 0), ((0, 2), 1)],
    [((0, 2), 1), ((0, 1), 2), ((0, 2), 1)],
    [((1, 2), 0), ((0, 2), 1), ((1, 2), 0)],
]
# The one value used as fsin left it; the other five are stored as floats first.
PINNED_UNROUNDED = frozenset({"sp"})
# The names GoodiesMeshTurn gives the rotation's rows (row 2 is two floats and the unrounded sine).
ROW_NAMES = [("A.X", "A.Y", "A.Z"), ("B.X", "B.Y", "B.Z"), ("cx", "sp", "cz")]

# The eight entries of the rotation that are computed (the ninth, the middle of row 2, is the sine itself).
ENTRY_NAMES = ("A.X", "A.Y", "A.Z", "B.X", "B.Y", "B.Z", "cx", "cz")
# The single uses of the five stored values: (entry, value). In A.X and B.X the roll's sine is inside sp * sr, and in A.Z and
# B.Z the roll's cosine is inside sp * cr; each is listed once for each entry the product feeds.
SINGLE_USES = (
    ("A.X", "cr"), ("A.X", "cy"), ("A.X", "sy"), ("A.X", "sr"),
    ("A.Y", "cp"), ("A.Y", "sy"),
    ("A.Z", "sr"), ("A.Z", "cy"), ("A.Z", "sy"), ("A.Z", "cr"),
    ("B.X", "cr"), ("B.X", "sy"), ("B.X", "cy"), ("B.X", "sr"),
    ("B.Y", "cp"), ("B.Y", "cy"),
    ("B.Z", "sr"), ("B.Z", "sy"), ("B.Z", "cy"), ("B.Z", "cr"),
    ("cx", "cp"), ("cx", "sr"),
    ("cz", "cp"), ("cz", "cr"),
)
TRIPLES = ("A.X", "A.Z", "B.X", "B.Z")      # the entries with a triple product: (sp * sr) * sy, (sp * cr) * sy, (sp * sr) * cy, (sp * cr) * cy
SUBTRACTIONS = ("A.X", "B.Z")               # the entries that subtract: cr * cy - (sp * sr) * sy and sr * sy - (sp * cr) * cy
SINE_PRODUCTS = ("spsr", "spcr")            # sp * sr and sp * cr

REQUIRE_SINE = 8        # a case's `require`: bit r = the three orders of every cell of row r must differ; 8 = the third row's sine


# ---------------------------------------------------------------------------------------------------------------------------
# The x87 unit at control word 007f, modelled exactly: every multiply, add and subtract is rounded once to a 24-bit mantissa,
# nearest even; a store to a float rounds the same way; fsin and fcos are not rounded (their values come from the run).


def round_to(q, bits):
    q = Fraction(q)
    if q == 0:
        return q
    sign, q = (-1 if q < 0 else 1), abs(q)
    e = q.numerator.bit_length() - q.denominator.bit_length()
    if Fraction(2) ** e > q:
        e -= 1
    elif Fraction(2) ** (e + 1) <= q:
        e += 1
    scale = Fraction(2) ** (e - (bits - 1))
    m = q / scale
    n = m.numerator // m.denominator
    rest = m - n
    if rest > Fraction(1, 2) or (rest == Fraction(1, 2) and n % 2):
        n += 1
    return sign * n * scale


def round24(q):
    return round_to(q, 24)


def float_bits(q):
    return struct.unpack("<I", struct.pack("<f", float(q)))[0]


def orders_of(order):
    """The order and the other two orders of the same three terms (the last term is each other term in turn)."""
    (a, b), c = order
    return [order, ((a, c), b), ((b, c), a)]


class Variant:
    """One way of doing the arithmetic: which of the six values stay unrounded, each cell's order, the third-row cells
    (by column) that round the sine to a float before multiplying, and, for the rotation's own entries: the single uses
    (entry, value) that take a stored value unrounded, the products of SINE_PRODUCTS that round the sine first, the entries
    of TRIPLES whose triple product is regrouped, and the entries of SUBTRACTIONS whose subtraction is turned round."""

    def __init__(self, unrounded=PINNED_UNROUNDED, order=None, float_sine=(), raw_uses=(), sine_rounded_in=(), regrouped=(),
                 reversed_in=()):
        self.unrounded = frozenset(unrounded)
        self.order = [list(row) for row in (order or PINNED_ORDER)]
        self.float_sine = frozenset(float_sine)
        self.raw_uses = frozenset(raw_uses)
        self.sine_rounded_in = frozenset(sine_rounded_in)
        self.regrouped = frozenset(regrouped)
        self.reversed_in = frozenset(reversed_in)
        assert self.raw_uses <= set(SINGLE_USES) and self.sine_rounded_in <= set(SINE_PRODUCTS)
        assert self.regrouped <= set(TRIPLES) and self.reversed_in <= set(SUBTRACTIONS)

    def with_order(self, row, col, order):
        v = Variant(self.unrounded, self.order, self.float_sine, self.raw_uses, self.sine_rounded_in, self.regrouped, self.reversed_in)
        v.order[row][col] = order
        return v


PINNED = Variant()


def rotation_rows(trig, variant):
    """The rotation's three rows as exact values: rows 0 and 1 and the outer values of row 2 are results of rounded operations;
    the middle of row 2 is the sine as the variant holds it."""
    raw = {name: Fraction(trig[name]) for name in TRIG}
    held = {name: (raw[name] if name in variant.unrounded else round24(raw[name])) for name in TRIG}
    sp = held["sp"]
    mul = lambda a, b: round24(a * b)

    def entry(name):
        at = lambda value: raw[value] if (name, value) in variant.raw_uses else held[value]

        def triple(product, inner, outer):      # (sp * inner) * outer
            sine = round24(sp) if product in variant.sine_rounded_in else sp
            if name in variant.regrouped:
                return mul(sine, mul(at(inner), at(outer)))
            return mul(mul(sine, at(inner)), at(outer))

        def minus(a, b):
            return round24(b - a) if name in variant.reversed_in else round24(a - b)

        if name == "A.X":
            return minus(mul(at("cr"), at("cy")), triple("spsr", "sr", "sy"))
        if name == "A.Y":
            return -mul(at("cp"), at("sy"))
        if name == "A.Z":
            return round24(mul(at("sr"), at("cy")) + triple("spcr", "cr", "sy"))
        if name == "B.X":
            return round24(mul(at("cr"), at("sy")) + triple("spsr", "sr", "cy"))
        if name == "B.Y":
            return mul(at("cp"), at("cy"))
        if name == "B.Z":
            return minus(mul(at("sr"), at("sy")), triple("spcr", "cr", "cy"))
        if name == "cx":
            return -mul(at("cp"), at("sr"))
        assert name == "cz"
        return mul(at("cp"), at("cr"))

    return [
        [entry("A.X"), entry("A.Y"), entry("A.Z")],
        [entry("B.X"), entry("B.Y"), entry("B.Z")],
        [entry("cx"), sp, entry("cz")],
    ]


def cell_value(left, column, order):
    (a, b), c = order
    t = lambda k: round24(Fraction(left[k]) * Fraction(column[k]))
    return round24(round24(t(a) + t(b)) + t(c))


def cells_of(trig, mesh, variant):
    """{(row, col): bits} for the nine cells."""
    rows = rotation_rows(trig, variant)
    out = {}
    for r in range(3):
        for c in range(3):
            left = list(rows[r])
            if r == 2 and c in variant.float_sine:
                left[1] = round24(left[1])
            out[(r, c)] = float_bits(cell_value(left, [mesh[k][c] for k in range(3)], variant.order[r][c]))
    return out


# ---------------------------------------------------------------------------------------------------------------------------
# Choosing the cases. The search needs sines and cosines before anything runs: exact series, rounded to the x87's 64 bits.
# (The checks after the run use the values the run reports, not these.)


def series(x, sine):
    x = Fraction(x)
    term = x if sine else Fraction(1)
    total, n = term, (1 if sine else 0)
    while abs(term) > Fraction(1, 2 ** 110):
        n += 2
        term = -term * x * x / (n * (n - 1))
        total += term
    return total


def trig_of(yaw, pitch, roll, sine_step=0):
    """The six values to 64 bits; sine_step moves sin(pitch) by that many units in its last place."""
    exact = {"cy": series(yaw, False), "sy": series(yaw, True), "cr": series(roll, False), "sr": series(roll, True),
             "cp": series(pitch, False), "sp": series(pitch, True)}
    out = {name: round_to(value, 64) for name, value in exact.items()}
    if sine_step and out["sp"]:
        sp = abs(out["sp"])
        e = sp.numerator.bit_length() - sp.denominator.bit_length()
        if Fraction(2) ** e > sp:
            e -= 1
        out["sp"] += sine_step * Fraction(2) ** (e - 63)
    return out


def float32(rng, lo=0.5, hi=2.0):
    return float(round24(Fraction(rng.uniform(lo, hi)))) * rng.choice((-1, 1))


def column_fits(rotations, column, order_rows, need_sine):
    """Whether one column of the mesh matrix tells apart what a case requires, for each given rotation (the pinned arithmetic's
    rows for one set of sines and cosines): the three orders of the cell of each row in order_rows, and the third row's cell
    with the sine rounded first."""
    for rows in rotations:
        for r in order_rows:
            if len({float_bits(cell_value(rows[r], column, o)) for o in orders_of(((0, 1), 2))}) != 3:
                return False
        if need_sine:
            rounded = [rows[2][0], round24(rows[2][1]), rows[2][2]]
            for order in {PINNED_ORDER[2][c] for c in range(3)}:
                if float_bits(cell_value(rows[2], column, order)) == float_bits(cell_value(rounded, column, order)):
                    return False
    return True


def choose_mesh(rng, yaw, pitch, roll, order_rows, need_sine):
    """A 3x3 mesh matrix whose every column fits, with sin(pitch) as computed and one unit either side of it in its last place
    (the x87's fsin need not be correctly rounded), or None when 4000 draws find no column."""
    rotations = [rotation_rows(trig_of(yaw, pitch, roll, step), PINNED) for step in (0, -1, 1)]
    columns = []
    for _ in range(3):
        for _ in range(4000):
            column = [float32(rng) for _ in range(3)]
            if column_fits(rotations, column, order_rows, need_sine):
                columns.append(column)
                break
        else:
            return None
    return [[columns[j][k] for j in range(3)] for k in range(3)]       # mesh[k][j]


def changes(trig, mesh, variant, pinned=None):
    """Whether the variant's arithmetic gives a different float in at least one of the nine cells."""
    return cells_of(trig, mesh, variant) != (pinned or cells_of(trig, mesh, PINNED))


def changes_rotation(trig, variant, pinned_rows):
    """Whether a variant that differs only in the rotation's entries changes one of them (if not, no cell can change)."""
    return rotation_rows(trig, variant) != pinned_rows


def choose_cases(variants, single_choices):
    """The cases. The general ones (one set of angles, one matrix for each row of the product) are redrawn (fixed seed, so
    the same numbers every run) until every variant in `variants` changes at least one of their cells. Then angle sets are
    added, the best of eight draws at a time, until every variant in `single_choices` (a choice at one use, one product or
    one entry of the rotation) changes at least one cell of at least one case."""
    rng = random.Random(130)
    tenth = float(round24(Fraction(1, 10)))        # the viewer's step for a full button press: 1.0f * 0.1f
    while True:
        yaw, pitch, roll = (float32(rng, 0.3, 1.2) for _ in range(3))
        meshes = [choose_mesh(rng, yaw, pitch, roll, (r,), r == 2) for r in range(3)]
        if None in meshes:
            continue
        trig = trig_of(yaw, pitch, roll)
        pinned = [cells_of(trig, mesh, PINNED) for mesh in meshes]
        if all(any(cells_of(trig, mesh, v) != p for mesh, p in zip(meshes, pinned)) for v in variants):
            break
    cases = [dict(name="general-row-%d" % r, yaw=yaw, pitch=pitch, roll=roll, m=meshes[r], require=(1 << r) | (REQUIRE_SINE if r == 2 else 0))
             for r in range(3)]
    flat = choose_mesh(rng, 0.0, tenth, 0.0, (), True)
    assert flat is not None, "no matrix found for the pitch-only case"
    cases += [dict(name="pitch-only", yaw=0.0, pitch=tenth, roll=0.0, m=flat, require=REQUIRE_SINE),
              dict(name="yaw-only", yaw=tenth, pitch=0.0, roll=0.0, m=flat, require=0)]

    open_choices = [v for v in single_choices if not any(changes(trig, mesh, v, p) for mesh, p in zip(meshes, pinned))]
    rng = random.Random(1300)
    for _ in range(60):
        if not open_choices:
            break
        best = None
        for _ in range(8):
            angles = [float32(rng, 0.3, 1.2) for _ in range(3)]
            mesh = [[float32(rng) for _ in range(3)] for _ in range(3)]
            t = trig_of(*angles)
            p, rows = cells_of(t, mesh, PINNED), rotation_rows(t, PINNED)
            told = [v for v in open_choices if changes_rotation(t, v, rows) and changes(t, mesh, v, p)]
            if best is None or len(told) > len(best[2]):
                best = (angles, mesh, told)
        if best[2]:
            cases.append(dict(name="angles-%d" % (len(cases) - 4), yaw=best[0][0], pitch=best[0][1], roll=best[0][2], m=best[1], require=0))
            open_choices = [v for v in open_choices if all(v is not w for w in best[2])]
    assert not open_choices, "no angles found that tell %d single choices apart" % len(open_choices)
    return cases


def header(cases):
    def lit(x):
        return float(x).hex() + "f"
    out = ["// generated by goodies_mesh_turn_tests.py: the cases and the pinned order of every cell\n",
           "enum { REQUIRE_SINE = %d };\t// require: bit r = row r's three orders must differ; this bit = the third row's sine\n" % REQUIRE_SINE,
           "struct TurnCase { const char *name; float yaw, pitch, roll; float m[3][3]; int require; };\n",
           "static const int CASE_COUNT = %d;\n" % len(cases),
           "static const TurnCase CASES[] = {\n"]
    for case in cases:
        rows = ", ".join("{" + ", ".join(lit(x) for x in row) + "}" for row in case["m"])
        out.append('\t{"%s", %s, %s, %s, {%s}, %d},\n' % (case["name"], lit(case["yaw"]), lit(case["pitch"]), lit(case["roll"]), rows,
                                                         case["require"]))
    out.append("};\n")
    out.append("static const int ORDER[3][3][3] = {\n")
    for row in PINNED_ORDER:
        out.append("\t{" + ", ".join("{%d, %d, %d}" % (o[0][0], o[0][1], o[1]) for o in row) + "},\n")
    out.append("};\n")
    return "".join(out)


# ---------------------------------------------------------------------------------------------------------------------------
# The source: the function as the build holds it, and its mutants.


def function_text(source):
    m = re.search(r"^static FMatrix\s+%s\(.*?\n}\n" % FUNCTION, source, re.M | re.S)
    assert m, "no %s in the patched FEPGoodies.cpp" % FUNCTION
    return m.group(0)


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


def cell_text(row, col, order, sine="sp"):
    (a, b), c = order
    names = list(ROW_NAMES[row])
    if row == 2:
        names[1] = sine
    term = lambda k: "(%s * m.Row[%d].%s)" % (names[k], k, COLS[col])
    text = "(%s + %s) + %s" % (term(a), term(b), term(c))
    return "(float)(%s)" % text if row == 2 else text


def rewrite_cells(source, edits):
    """The function's text with the cells in edits {(row, col): cell text} replaced."""
    ret = source.index("return FMatrix(") + len("return FMatrix")
    depth, close = 0, None
    for i in range(ret, len(source)):
        depth += source[i] == "("
        depth -= source[i] == ")"
        if depth == 0:
            close = i
            break
    rows = top_level(source[ret + 1:close])
    assert len(rows) == 3, "the function does not return FMatrix(row, row, row)"
    new_rows = []
    for r, vector in enumerate(rows):
        head = vector[:vector.index("FVector(") + len("FVector(")]
        inner = vector[len(head):vector.rindex(")")]
        parts = top_level(inner)
        assert len(parts) == 3, "row %d does not have three cells" % r
        for c in range(3):
            if (r, c) in edits:
                lead = re.match(r"\s*", parts[c]).group(0)
                parts[c] = lead + edits[(r, c)]
        new_rows.append(head + ",".join(parts) + ")")
    return source[:ret + 1] + ",".join(new_rows) + source[close:]


def replace_once(source, old, new):
    assert source.count(old) == 1, "the function's text has changed: %r occurs %d times" % (old, source.count(old))
    return source.replace(old, new)


# The declarations as GoodiesMeshTurn writes them, and each with one value left unrounded (fcos or fsin's result held as a
# long double) or, for the sine of the pitch, stored as a float.
UNROUNDED_EDITS = {
    "cy": ("volatile float cy = cosf(yaw), sy = sinf(yaw);", "long double cy = bea_x87_fcosl(yaw); volatile float sy = sinf(yaw);"),
    "sy": ("volatile float cy = cosf(yaw), sy = sinf(yaw);", "volatile float cy = cosf(yaw); long double sy = bea_x87_fsinl(yaw);"),
    "cr": ("volatile float cr = cosf(roll), sr = sinf(roll);", "long double cr = bea_x87_fcosl(roll); volatile float sr = sinf(roll);"),
    "sr": ("volatile float cr = cosf(roll), sr = sinf(roll);", "volatile float cr = cosf(roll); long double sr = bea_x87_fsinl(roll);"),
    "cp": ("volatile float cp = cosf(pitch);", "long double cp = bea_x87_fcosl(pitch);"),
}
SINE_STORED_EDIT = ("long double sp = bea_x87_fsinl(pitch);", "volatile float sp_stored = sinf(pitch); long double sp = sp_stored;")
# sp * sr and sp * cr as GoodiesMeshTurn writes them, and each with the sine rounded to a float first.
SINE_PRODUCT_EDITS = {
    "spsr": ("long double spsr = sp * sr;", "long double spsr = (long double)(float)sp * sr;"),
    "spcr": ("volatile float spcr = (float)(sp * cr);", "volatile float spcr = (float)((long double)(float)sp * cr);"),
}
# The rotation's eight computed entries as GoodiesMeshTurn writes them.
ENTRY_TEXT = {
    "A.X": "(float)((long double)cr * cy - spsr * sy)",
    "A.Y": "(float)(-((long double)cp * sy))",
    "A.Z": "(float)((long double)sr * cy + (long double)spcr * sy)",
    "B.X": "(float)((long double)cr * sy + spsr * cy)",
    "B.Y": "(float)((long double)cp * cy)",
    "B.Z": "(float)((long double)sr * sy - (long double)spcr * cy)",
    "cx": "(float)(-((long double)cp * sr))",
    "cz": "(float)((long double)cp * cr)",
}
# Each stored value as fcos or fsin left it, and the value that sp * sr and sp * cr hold besides the sine.
UNROUNDED_VALUE = {"cy": "bea_x87_fcosl(yaw)", "sy": "bea_x87_fsinl(yaw)", "cr": "bea_x87_fcosl(roll)", "sr": "bea_x87_fsinl(roll)",
                   "cp": "bea_x87_fcosl(pitch)"}
INSIDE = {"spsr": "sr", "spcr": "cr"}
# How an entry reads sp * sr and sp * cr (the second is a stored float), and the same product made there from another value.
PRODUCT_AS_USED = {"spsr": "spsr", "spcr": "(long double)spcr"}
PRODUCT_REMADE = {"spsr": "(sp * %s)", "spcr": "(long double)(float)(sp * %s)"}


def sub_once(pattern, new, text):
    out, count = re.subn(pattern, lambda m: new(m) if callable(new) else new, text)
    assert count == 1, "the entry's text has changed: %r matches %d times in %r" % (pattern, count, text)
    return out


def single_use_text(entry, value):
    """The entry with one stored value taken unrounded: named there directly, or inside the entry's sp * sr or sp * cr."""
    text = ENTRY_TEXT[entry]
    if re.search(r"\b%s\b" % value, text):
        return sub_once(r"\b%s\b" % value, UNROUNDED_VALUE[value], text)
    product = next(name for name, inside in INSIDE.items() if inside == value)
    return sub_once(re.escape(PRODUCT_AS_USED[product]), PRODUCT_REMADE[product] % UNROUNDED_VALUE[value], text)


def regrouped_text(entry):
    """(sp * inner) * outer written as sp * (inner * outer)."""
    text = ENTRY_TEXT[entry]
    product = next(name for name in SINE_PRODUCTS if PRODUCT_AS_USED[name] + " * " in text)
    return sub_once(re.escape(PRODUCT_AS_USED[product]) + r" \* (\w+)", lambda m: "sp * ((long double)%s * %s)" % (INSIDE[product], m.group(1)), text)


def reversed_text(entry):
    """a - b written as b - a."""
    text = ENTRY_TEXT[entry]
    assert text.startswith("(float)(") and text.endswith(")") and text.count(" - ") == 1, text
    a, b = text[len("(float)("):-1].split(" - ")
    return "(float)(%s - %s)" % (b, a)

# The function as patch 0130 first had it: the rotation from MakeRotationF (patch 0007's, which keeps cos(yaw) unrounded) and
# the product's third row read from the rotation's stored floats.
FIRST_WRITTEN = '''static FMatrix	GoodiesMeshTurn(float yaw, float pitch, float roll, const FMatrix &mesh)
{
	FMatrix delta;
	delta.MakeRotationF(yaw, pitch, roll);
	const FVector &A = delta.Row[0], &B = delta.Row[1], &C = delta.Row[2];
	const FMatrix m = mesh;
	return FMatrix(FVector(((A.X * m.Row[0].X) + (A.Z * m.Row[2].X)) + (A.Y * m.Row[1].X),
								 ((A.Y * m.Row[1].Y) + (A.Z * m.Row[2].Y)) + (A.X * m.Row[0].Y),
								 ((A.X * m.Row[0].Z) + (A.Z * m.Row[2].Z)) + (A.Y * m.Row[1].Z)),
						FVector(((B.Z * m.Row[2].X) + (B.X * m.Row[0].X)) + (B.Y * m.Row[1].X),
								 ((B.X * m.Row[0].Y) + (B.Y * m.Row[1].Y)) + (B.Z * m.Row[2].Y),
								 ((B.Z * m.Row[2].Z) + (B.X * m.Row[0].Z)) + (B.Y * m.Row[1].Z)),
						FVector(((C.Y * m.Row[1].X) + (C.Z * m.Row[2].X)) + (C.X * m.Row[0].X),
								 ((C.X * m.Row[0].Y) + (C.Z * m.Row[2].Y)) + (C.Y * m.Row[1].Y),
								 ((C.Y * m.Row[1].Z) + (C.Z * m.Row[2].Z)) + (C.X * m.Row[0].Z)));
}
'''
FIRST_WRITTEN_VARIANT = Variant(unrounded={"sp", "cy"}, float_sine=(0, 1, 2))


def mutants_of(source):
    """{name: (text or a function making it, the arithmetic it should have)}. Texts are made lazily: a helper given with
    --helper-file may not have the shape the edits expect, and then only the baseline is of interest."""
    out = {}
    for r in range(3):
        for c in range(3):
            for k, order in enumerate(orders_of(PINNED_ORDER[r][c])[1:], 1):
                out["r%d%s-order%d" % (r, COLS[c], k)] = (
                    lambda r=r, c=c, order=order: rewrite_cells(source, {(r, c): cell_text(r, c, order)}), PINNED.with_order(r, c, order))
    for c in range(3):
        out["r2%s-sine-rounded" % COLS[c]] = (
            lambda c=c: rewrite_cells(source, {(2, c): cell_text(2, c, PINNED_ORDER[2][c], sine="(float)sp")}), Variant(float_sine=(c,)))
    out["sine-stored"] = (lambda: replace_once(source, *SINE_STORED_EDIT), Variant(unrounded=()))
    for name, edit in UNROUNDED_EDITS.items():
        out["%s-unrounded" % name] = (lambda edit=edit: replace_once(source, *edit), Variant(unrounded={"sp", name}))
    out["as-first-written-in-0130"] = (lambda: FIRST_WRITTEN, FIRST_WRITTEN_VARIANT)
    return out


def single_choice_mutants_of(source):
    """The same for the choices made at one place in the rotation: a stored value unrounded at one use, the sine rounded in
    one of its two products, a triple product regrouped, a subtraction turned round."""
    out = {}
    for entry, value in SINGLE_USES:
        out["%s-%s-unrounded" % (entry, value)] = (
            lambda entry=entry, value=value: replace_once(source, ENTRY_TEXT[entry], single_use_text(entry, value)),
            Variant(raw_uses={(entry, value)}))
    for product, edit in SINE_PRODUCT_EDITS.items():
        out["%s-sine-rounded" % product] = (lambda edit=edit: replace_once(source, *edit), Variant(sine_rounded_in={product}))
    for entry in TRIPLES:
        out["%s-regrouped" % entry] = (lambda entry=entry: replace_once(source, ENTRY_TEXT[entry], regrouped_text(entry)),
                                       Variant(regrouped={entry}))
    for entry in SUBTRACTIONS:
        out["%s-subtraction-reversed" % entry] = (lambda entry=entry: replace_once(source, ENTRY_TEXT[entry], reversed_text(entry)),
                                                  Variant(reversed_in={entry}))
    return out


# ---------------------------------------------------------------------------------------------------------------------------
# The call site, read from the patched file's text.

CALL_SITE = re.compile(r"mMeshOri\s*=\s*%s\(\s*dx\s*\*\s*0\.1f\s*,\s*dy\s*\*\s*0\.1f\s*,\s*0\s*,\s*mMeshOri\s*\)\s*;" % FUNCTION)
CALL_REASONS = {
    "call": "ButtonPressed does not hold exactly one mMeshOri = %s(dx*0.1f, dy*0.1f, 0, mMeshOri);" % FUNCTION,
    "other-mention": "ButtonPressed mentions mMeshOri outside a statement mMeshOri = %s(..., mMeshOri);" % FUNCTION,
    "calls": "%s is not named exactly twice in the file (its definition and that call)" % FUNCTION,
}


def strip_comments(text):
    """Comments replaced by a space, whichever kind starts first (a line of slashes and stars must not open a block comment)."""
    return re.sub(r"//[^\n]*|/\*.*?\*/", " ", text, flags=re.S)


def call_site_problems(text):
    """The reasons, keys of CALL_REASONS, why the patched FEPGoodies.cpp does not turn the mesh by GoodiesMeshTurn alone."""
    code = strip_comments(text)
    body = re.search(r"^void\s+CFEPGoodies::ButtonPressed\(.*?^}", code, re.M | re.S)
    if not body:
        return {"call"}
    problems = set()
    if len(CALL_SITE.findall(body.group(0))) != 1:
        problems.add("call")
    # mMeshOri anywhere but in a statement of that shape, whatever its other arguments
    if re.search(r"\bmMeshOri\b", re.sub(r"mMeshOri\s*=\s*%s\([^;]*\bmMeshOri\s*\)\s*;" % FUNCTION, " ", body.group(0))):
        problems.add("other-mention")
    if len(re.findall(r"\b%s\b" % FUNCTION, code)) != 2:
        problems.add("calls")
    return problems


def call_site_mutants(text):
    """{name: (the file's text changed, the reasons that must be reported)}; the first is no change in meaning."""
    statement = re.compile(r"mMeshOri = %s\(dx\*0\.1f[^,]*, dy\*0\.1f[^,]*, 0, mMeshOri\);" % FUNCTION)
    assert len(statement.findall(text)) == 1, "the call site's text has changed"
    whole = statement.search(text).group(0)
    return {
        "call-respaced-same-meaning": (statement.sub(lambda m: "mMeshOri=%s( dx * 0.1f, dy * 0.1f, 0, mMeshOri ) ;" % FUNCTION, text), set()),
        "call-angles-exchanged": (statement.sub(lambda m: "mMeshOri = %s(dy*0.1f, dx*0.1f, 0, mMeshOri);" % FUNCTION, text), {"call"}),
        "call-then-written-again": (text.replace(whole, whole + " mMeshOri = mMeshOri * ID_FMATRIX;"), {"other-mention"}),
        "call-twice": (text.replace(whole, whole + " FMatrix again = %s(dx*0.1f, dy*0.1f, 0, ID_FMATRIX);" % FUNCTION), {"calls"}),
        "call-as-first-written-in-0130": (
            text.replace(whole, "FMatrix delta; delta.MakeRotationF(dx*0.1f, dy*0.1f, 0); mMeshOri = delta * mMeshOri;"),
            {"call", "other-mention", "calls"}),
    }


# ---------------------------------------------------------------------------------------------------------------------------
# Compiling and running.


class Fixture:
    def __init__(self, parent, maths, cases):
        self.parent, self.maths, self.cases = parent, maths, cases
        self.target = build.TARGETS["linux"]
        self.cxx = self.target["cxx"]
        # The game's objects have their sinf and cosf calls renamed to the port's x87 routines (build.py GAME_MATH).
        self.syms = parent / "game-math.syms"
        self.syms.write_text("".join("%s %s\n" % pair for pair in build.GAME_MATH.items()))
        self.x87 = parent / "x87math.o"
        self.command([*self.cxx, "-std=gnu++17", "-O2", *build.X87, "-c", "-o", str(self.x87),
                      str(build.PORT / "platform/common/x87math.cpp")], "x87math")

    @staticmethod
    def command(cmd, name):
        done = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        if done.returncode:
            raise RuntimeError("%s: %s failed:\n%s" % (name, Path(cmd[0]).name, (done.stdout + done.stderr)[-1500:]))
        return done.stdout + done.stderr

    def make(self, name, source):
        folder = self.parent / name
        folder.mkdir()
        (folder / "maths.h").write_text(self.maths)
        (folder / "mesh_turn.inc").write_text(source)
        (folder / "mesh_turn_case.h").write_text(header(self.cases))
        ir, obj, binary = folder / "driver.ll", folder / "driver.o", folder / "driver"
        flags = [*self.cxx, "-std=gnu++14", *build.MSCXX, *build.COMMON, *build.X87, *build.NO_MATH_BUILTINS]
        log = ""
        try:
            for cmd in (
                [*flags, "-I" + str(folder), "-S", "-emit-llvm", "-Xclang", "-disable-llvm-passes", "-o", str(ir), str(DRIVER)],
                [sys.executable, str(build.PORT / "tools/qifist.py"), str(ir)],
                [*self.cxx, *build.COMMON, *build.X87, "-c", "-o", str(obj), str(ir)],
                [build.objcopy("linux"), "--redefine-syms=" + str(self.syms), str(obj)],
                [*self.cxx, *build.X87, str(obj), str(self.x87), "-no-pie", *build.deps.sysroot_link_flags(), "-lm", "-o", str(binary)],
            ):
                log += self.command(cmd, name)
        finally:
            (folder / "build.log").write_text(log)
        return binary

    @staticmethod
    def run(binary):
        done = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        cells, trig = {}, {}
        for line in done.stdout.splitlines():
            m = re.fullmatch(r"CELL (\S+) (\d) ([XYZ]) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}|-{8}) (\w+)", line)
            if m:
                cells[(m.group(1), int(m.group(2)), COLS.index(m.group(3)))] = dict(
                    expected=int(m.group(4), 16), actual=int(m.group(5), 16), alternatives=[int(m.group(6), 16), int(m.group(7), 16)],
                    float_sine=None if m.group(8).startswith("-") else int(m.group(8), 16), verdict=m.group(9))
            m = re.fullmatch(r"TRIG (\S+) " + " ".join(["([0-9a-f]{20})"] * 6), line)
            if m:
                trig[m.group(1)] = {name: extended(m.group(2 + i)) for i, name in enumerate(TRIG)}
        control = re.search(r"^CONTROL ([0-9a-f]{4})$", done.stdout, re.M)
        return done.returncode, cells, trig, control.group(1) if control else None, done.stdout


def extended(text):
    """An 80-bit x87 value, 20 hex digits most significant first, as an exact Fraction."""
    word = int(text, 16)
    sign, exponent, mantissa = word >> 79, (word >> 64) & 0x7FFF, word & ((1 << 64) - 1)
    if exponent == 0 and mantissa == 0:
        return Fraction(0)
    assert 0 < exponent < 0x7FFF, "not a finite normal 80-bit value: " + text
    return (-1 if sign else 1) * Fraction(mantissa) * Fraction(2) ** (exponent - 16383 - 63)


def cell_name(key):
    return "%s r%d %s" % (key[0], key[1], COLS[key[2]])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--no-mutants", action="store_true")
    ap.add_argument("--only-mutants", help="comma-separated mutant names: run only those after the baseline")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--keep", action="store_true", help="keep the run folder even when every check passes")
    ap.add_argument("--helper-file", type=Path, help="test this text of the function instead of the build's")
    args = ap.parse_args()
    parent = ROOT / "local-data/port/goodies-mesh-turn-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    # The run folder holds every compiled driver: it stays only as evidence of a failure.
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
    texts = build.patched_text(["maths.h"] + ([] if args.helper_file else ["FEPGoodies.cpp"]), network=False)
    source = args.helper_file.read_text() if args.helper_file else function_text(texts["FEPGoodies.cpp"])
    mutants, single_choices = mutants_of(source), single_choice_mutants_of(source)
    cases = choose_cases([variant for _, variant in mutants.values()], [variant for _, variant in single_choices.values()])
    mutants.update(single_choices)
    results, failed = [], []

    def record(name, ok, detail=""):
        results.append({"check": name, "passed": bool(ok), "detail": detail})
        print(("PASS " if ok else "FAIL ") + name + (": " + detail if detail else ""), flush=True)
        if not ok:
            failed.append(name)

    def finish():
        (out / "results.json").write_text(json.dumps(results, indent=1))
        print("%d checks, %d failed" % (len(results), len(failed)))
        return 1 if failed else 0

    assert round24(Fraction(1) + Fraction(1, 2 ** 24)) == 1 and round24(Fraction(1) + Fraction(3, 2 ** 24)) == 1 + Fraction(1, 2 ** 22), \
        "the model's ties do not go to even"

    fixture = Fixture(out, texts["maths.h"], cases)
    code, cells, trig, control, _ = fixture.run(fixture.make("baseline", source))
    record("baseline: the game's x87 control word is set", control == "007f", str(control))
    record("baseline: %d cells and %d sets of sines and cosines reported" % (9 * len(cases), len(cases)),
           len(cells) == 9 * len(cases) and set(trig) == {case["name"] for case in cases}, "%d cells, %d sets" % (len(cells), len(trig)))
    if failed:
        print("the driver's report is incomplete: nothing more is checked")
        return finish()

    # Controls on the data, from this run's own sines and cosines: each case tells apart what it is there to tell apart.
    pinned = {}
    for case in cases:
        name, t = case["name"], trig[case["name"]]
        pinned[name] = cells_of(t, case["m"], PINNED)
        for r in range(3):
            for c in range(3):
                if case["require"] & (1 << r):
                    values = {cells_of(t, case["m"], PINNED.with_order(r, c, o))[(r, c)] for o in orders_of(PINNED_ORDER[r][c])}
                    record("%s: the numbers tell row %d column %s's three orders apart" % (name, r, COLS[c]), len(values) == 3)
                if r == 2 and case["require"] & REQUIRE_SINE:
                    rounded = cells_of(t, case["m"], Variant(float_sine=(c,)))[(r, c)]
                    record("%s: row 2 column %s differs when the sine is rounded to a float first" % (name, COLS[c]),
                           rounded != pinned[name][(r, c)], "%08x unrounded, %08x rounded" % (pinned[name][(r, c)], rounded))
    for choice, (_, variant) in sorted(single_choices.items()):
        telling = [case["name"] for case in cases if changes(trig[case["name"]], case["m"], variant, pinned[case["name"]])]
        record("the numbers tell the single choice %s apart" % choice, telling, "in " + ", ".join(telling) if telling else "in no case")
    for case in cases:
        name, t = case["name"], trig[case["name"]]
        for r in range(3):
            for c in range(3):
                row = cells[(name, r, c)]
                alternatives = [cells_of(t, case["m"], PINNED.with_order(r, c, o))[(r, c)] for o in orders_of(PINNED_ORDER[r][c])[1:]]
                float_sine = cells_of(t, case["m"], Variant(float_sine=(c,)))[(r, c)] if r == 2 else None
                record("baseline: %s row %d column %s equals the pinned arithmetic (x87 evaluation and exact model agree)" % (name, r, COLS[c]),
                       row["verdict"] == "ok" and row["actual"] == row["expected"] == pinned[name][(r, c)]
                       and row["alternatives"] == alternatives and row["float_sine"] == float_sine,
                       "actual %08x expected %08x model %08x" % (row["actual"], row["expected"], pinned[name][(r, c)]))
    record("baseline: the driver exits 0", code == 0, str(code))
    if args.helper_file:
        print("NOT CHECKED: the call site (--helper-file gives the function's text only; this is not a pass of that check)")
        site_mutants = {}
    else:
        problems = call_site_problems(texts["FEPGoodies.cpp"])
        record("baseline: ButtonPressed turns the mesh by one %s(dx*0.1f, dy*0.1f, 0, mMeshOri) and nothing else" % FUNCTION,
               not problems, "; ".join(CALL_REASONS[reason] for reason in sorted(problems)))
        site_mutants = {} if problems else call_site_mutants(texts["FEPGoodies.cpp"])
    if failed:
        print("the baseline failed: no mutants are run")
        return finish()

    if args.only_mutants:
        wanted = set(x for x in args.only_mutants.split(",") if x)
        assert wanted <= set(mutants) | set(site_mutants), "no such mutant: " + ", ".join(sorted(wanted - set(mutants) - set(site_mutants)))
        mutants = {k: v for k, v in mutants.items() if k in wanted}
        site_mutants = {k: v for k, v in site_mutants.items() if k in wanted}
    elif args.no_mutants:
        mutants, site_mutants = {}, {}

    for name, (text, reasons) in sorted(site_mutants.items()):
        assert text != texts["FEPGoodies.cpp"], name
        reported = call_site_problems(text)
        record("call-site mutant %s is %s" % (name, "reported, for exactly its reasons" if reasons else "accepted: the same call"),
               reported == reasons, "reported %s%s" % (sorted(reported) or "nothing", "" if reported == reasons else "; expected %s" % sorted(reasons)))

    def one(item):
        name, (make_text, variant) = item
        text = make_text()
        assert text != source, name
        code, cells, _, _, _ = fixture.run(fixture.make("mutant-" + name, text))
        reported = {key for key, row in cells.items() if row["verdict"] == "WRONG"}
        predicted = set()
        for case in cases:
            changed = cells_of(trig[case["name"]], case["m"], variant)
            predicted |= {(case["name"], r, c) for (r, c), value in changed.items() if value != pinned[case["name"]][(r, c)]}
        return name, code, reported, predicted

    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for name, code, reported, predicted in pool.map(one, sorted(mutants.items())):
            record("mutant %s is caught, in exactly the cells its arithmetic changes" % name,
                   code == 1 and reported and reported == predicted,
                   "exit %d, wrong cells %s%s" % (code, sorted(cell_name(k) for k in reported),
                                                 "" if reported == predicted else "; predicted %s" % sorted(cell_name(k) for k in predicted)))
    return finish()


if __name__ == "__main__":
    sys.exit(main())
