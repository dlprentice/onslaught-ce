#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for port/tools/qifist.py: VC6 /QIfist and constant-divisor reciprocal rewrites."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import qifist  # noqa: E402


def f32_hex_bits(double_hex: str) -> int:
    val = struct.unpack("<d", int(double_hex, 16).to_bytes(8, "little"))[0]
    return struct.unpack("<I", struct.pack("<f", val))[0]


class QifistTests(unittest.TestCase):
    def test_decimal_float_divisor_matches_hex_float_divisor_bit_for_bit(self):
        # LLVM 22 prints float 1.15f as 0x3FF2666660000000; LLVM 23 prints 1.150000e+00.
        # Both represent the same IEEE-754 float32 divisor and must produce VC6's 0x3F5E9BD4 reciprocal.
        hex_ir = "  %1 = fdiv float %0, 0x3FF2666660000000\n"
        dec_ir = "  %1 = fdiv float %0, 1.150000e+00\n"
        hex_out, n1 = qifist.rewrite(hex_ir)
        dec_out, n2 = qifist.rewrite(dec_ir)
        self.assertEqual((n1, n2), (1, 1))
        self.assertEqual(hex_out, dec_out)
        self.assertIn("fmul float %0, 0x3FEBD37A80000000", dec_out)
        self.assertEqual(f32_hex_bits("0x3FEBD37A80000000"), 0x3F5E9BD4)

    def test_unfixed_double_parsing_would_lose_one_ulp_on_1_15f(self):
        # Control proving the float32 narrowing in constant() is load-bearing.
        narrowed = qifist.constant("1.150000e+00", "float")
        wide = qifist.constant("1.150000e+00", "double")
        self.assertNotEqual(narrowed, wide)
        from fractions import Fraction
        bad_lit = qifist.literal(qifist.nearest(1 / Fraction(wide), "float"), "float")
        good_lit = qifist.literal(qifist.nearest(1 / Fraction(narrowed), "float"), "float")
        self.assertEqual(f32_hex_bits(bad_lit), 0x3F5E9BD3)
        self.assertEqual(f32_hex_bits(good_lit), 0x3F5E9BD4)

    def test_double_divisor_keeps_full_double_precision(self):
        ir = "  %1 = fdiv double %0, 1.150000e+00\n"
        out, n = qifist.rewrite(ir)
        self.assertEqual(n, 1)
        from fractions import Fraction
        expected = qifist.literal(qifist.nearest(1 / Fraction(float("1.150000e+00")), "double"), "double")
        self.assertIn(f"fmul double %0, {expected}", out)

    def test_zero_and_non_constant_divisors_are_untouched(self):
        ir = "  %1 = fdiv float %0, 0.000000e+00\n  %2 = fdiv float %0, %x\n"
        out, n = qifist.rewrite(ir)
        self.assertEqual(n, 0)
        self.assertEqual(out, ir)

    def test_fptosi_and_fptoui_rewrite_to_lrint_and_llrint(self):
        ir = (
            "  %a = fptosi float %x to i32\n"
            "  %b = fptoui double %y to i32\n"
            "  %c = fptosi float %x to i16\n"
        )
        out, n = qifist.rewrite(ir)
        self.assertEqual(n, 3)
        self.assertIn("call i32 @llvm.lrint.i32.f32(float %x)", out)
        self.assertIn("call i64 @llvm.llrint.i64.f64(double %y)", out)
        self.assertIn("trunc i64 %qifist.2 to i32", out)
        self.assertIn("trunc i32 %qifist.3 to i16", out)
        self.assertEqual(out.count("declare i32 @llvm.lrint.i32.f32(float)"), 1)
        self.assertEqual(out.count("declare i64 @llvm.llrint.i64.f64(double)"), 1)


if __name__ == "__main__":
    unittest.main()
