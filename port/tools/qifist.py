#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""VC6's /QIfist for clang: rewrite an LLVM IR file's float-to-integer conversions to use the current rounding mode;
and VC6's division by a constant, a multiplication by its reciprocal.

    python3 port/tools/qifist.py FILE.ll        # in place; prints the number of instructions rewritten

Retail is built with /QIfist (config/flags.txt): a float or double becomes an integer through `fistp`, which rounds in
the x87 unit's current mode (nearest), where C (and clang's `fptosi`/`fptoui`) truncates. port/build.py compiles each
game file to unoptimised IR (-Xclang -disable-llvm-passes), runs this, then optimises and generates code, so the
optimiser sees llvm.lrint/llvm.llrint from the start and never folds a truncation the original code did not have.
Constant expressions are left alone: VC6 folded those at compile time with C's truncation too. cframetimer.cpp is built
without /QIfist in retail (config/file-flags.txt), so build.py does not run this on it.

Division by a constant: VC6 at /O2 compiles `x / C` as `x * (1/C)`, the reciprocal a constant of the operation's type
(CBattleEngineWalkerPart::Forward's mGroundVelocity/50 is `fmuls __real@4@3ff9a3d70a3d70a3d800`, 0.02f, in the decomp's
VC6 object), which can differ from a true division in the last bit (the battle engine's velocity at frame 3301 of a
recorded level-100 tape). Every `fdiv` whose divisor is a constant becomes an `fmul` by 1/C, rounded once from the exact value to
the type (VC6 computes it in extended precision first; its constant names show the 64-bit value). cframetimer.cpp gets this too only if it is ever run through here (it is not).
"""
import struct
from fractions import Fraction
import re
import sys
from pathlib import Path

FP = {"float": "f32", "double": "f64", "x86_fp80": "f80"}
FDIV = re.compile(r"^(\s*)(%[-\w.$]+) = fdiv ((?:[a-z]+ )*)(float|double) ([^,\s]+), (0x[0-9A-Fa-f]{16}|[-+]?\d+\.\d+(?:e[-+]\d+)?)(.*)$")
INSN = re.compile(r"^(\s*)(%[-\w.$]+) = (fptosi|fptoui) (float|double|x86_fp80) ([^,\s]+) to (i\d+)(.*)$")


def constant(text):
    """An LLVM IR float or double literal (decimal, or the 16-digit hex of a double) as a Python float."""
    if text.lower().startswith("0x"):
        return struct.unpack("<d", int(text, 16).to_bytes(8, "little"))[0]
    return float(text)


def nearest(exact, ty):
    """The float or double nearest to an exact rational (ties to even), as a Python float."""
    fmt, ifmt = ("<f", "<I") if ty == "float" else ("<d", "<Q")
    guess = struct.unpack(fmt, struct.pack(fmt, float(exact)))[0]
    bits = struct.unpack(ifmt, struct.pack(fmt, guess))[0]
    best = None
    for b in (bits - 1, bits, bits + 1):
        v = struct.unpack(fmt, struct.pack(ifmt, b))[0]
        d = abs(Fraction(v) - exact)
        if best is None or d < best[0] or d == best[0] and b % 2 == 0:
            best = (d, v, b)
    return best[1]


def literal(value, ty):
    """value (already representable in ty) as LLVM IR's hex literal (floats are written as the double of their value)."""
    return "0x%016X" % int.from_bytes(struct.pack("<d", value), "little")


def reciprocal(line):
    m = FDIV.match(line)
    if not m:
        return None
    indent, dst, flags, ty, val, div, tail = m.groups()
    c = constant(div)
    if c == 0 or c != c or abs(c) == float("inf"):
        return None
    r = 1.0 / c
    if ty == "float" and (abs(r) > 3.4028234663852886e38 or (r != 0 and abs(r) < 1.1754943508222875e-38)):
        return None
    return f"{indent}{dst} = fmul {flags}{ty} {val}, {literal(nearest(1 / Fraction(c), ty), ty)}{tail}"


def rewrite(text):
    out, used, n = [], set(), 0
    for line in text.split("\n"):
        r = reciprocal(line)
        if r is not None:
            n += 1
            out.append(r)
            continue
        m = INSN.match(line)
        if not m:
            out.append(line)
            continue
        indent, dst, op, ty, val, ity, tail = m.groups()
        bits = int(ity[1:])
        f = FP[ty]
        n += 1
        if op == "fptosi" and bits == 32:
            used.add(("lrint", "i32", ty))
            out.append(f"{indent}{dst} = call i32 @llvm.lrint.i32.{f}({ty} {val}){tail}")
        elif op == "fptosi" and bits == 64 or op == "fptoui" and bits >= 32:
            # fistp to 64 bits; an unsigned 32-bit result is its low half, as VC6's __ftol-free /QIfist path gives.
            used.add(("llrint", "i64", ty))
            if bits == 64:
                out.append(f"{indent}{dst} = call i64 @llvm.llrint.i64.{f}({ty} {val}){tail}")
            else:
                out.append(f"{indent}%qifist.{n} = call i64 @llvm.llrint.i64.{f}({ty} {val})")
                out.append(f"{indent}{dst} = trunc i64 %qifist.{n} to {ity}{tail}")
        else:
            # 8 and 16-bit results: fistp to 32 bits, then the low part.
            used.add(("lrint", "i32", ty))
            out.append(f"{indent}%qifist.{n} = call i32 @llvm.lrint.i32.{f}({ty} {val})")
            out.append(f"{indent}{dst} = trunc i32 %qifist.{n} to {ity}{tail}")
    text = "\n".join(out)
    for kind, ity, ty in sorted(used):
        name = f"@llvm.{kind}.{ity}.{FP[ty]}"
        if f"declare {ity} {name}(" not in text:
            text += f"\ndeclare {ity} {name}({ty})\n"
    return text, n


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    path = Path(sys.argv[1])
    text, n = rewrite(path.read_text())
    path.write_text(text)
    print(n)


if __name__ == "__main__":
    main()
