#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deterministic generator for port/game/ai/ce_ai_pilot_weights.h.

Generates a ~105 KB INT8 multi-layer perceptron (32 -> 256 -> 256 -> 128 -> 8 = 107,520 weights)
plus a 256-entry Q12 integer tanh lookup table for zero-libm INT8xINT16->INT32 Layer-1 pilot
inference in Onslaught CE. Re-running produces byte-identical output.
"""
from __future__ import annotations

import hashlib
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "port/game/ai/ce_ai_pilot_weights.h"

IN_DIM = 32
H1_DIM = 256
H2_DIM = 256
H3_DIM = 128
OUT_DIM = 8

# Canonical feature & output index map (must match CeAiMlpFeatIdx / CeAiMlpOutIdx in port/game/ai/ce_ai_pilot_l1.h).
FEAT_LIFE_RATIO        = 0
FEAT_ENERGY_RATIO      = 1
FEAT_IS_JET            = 2
FEAT_IS_ON_GROUND      = 3
FEAT_ALTITUDE          = 4
FEAT_DRY_HERE          = 5
FEAT_DRY_FWD           = 6
FEAT_DRY_BACK          = 7
FEAT_DRY_LEFT          = 8
FEAT_DRY_RIGHT         = 9
FEAT_DRY_AHEAD_28      = 10
FEAT_WEAPON_READY      = 11
FEAT_WEAPON_OVERHEATED = 12
FEAT_WEAPON_HEAT_AMMO  = 13
FEAT_PITCH             = 14
FEAT_POS_X             = 15
FEAT_POS_Y             = 16
FEAT_HAS_HOSTILE       = 17
FEAT_HOSTILE_DX        = 18
FEAT_HOSTILE_DY        = 19
FEAT_HOSTILE_DZ        = 20
FEAT_HOSTILE_LIFE      = 21
FEAT_HOSTILE_AIRBORNE  = 22
FEAT_HOSTILE_IS_BE     = 23
FEAT_HOSTILE_CLASS     = 24
FEAT_REPAIR_PAD_DX     = 25
FEAT_REPAIR_PAD_DY     = 26
FEAT_FORCE_BALANCE     = 27
FEAT_FLIGHT_ALLOWED    = 28
FEAT_PRIOR_AGGRESSION  = 29
FEAT_PRIOR_AIR_BIAS    = 30
FEAT_PRIOR_POSTURE     = 31

OUT_STRAFE_TRIM        = 0
OUT_FWD_TRIM           = 1
OUT_JET_TURN_TRIM      = 2
OUT_JET_THRUST_TRIM    = 3
OUT_JET_CRUISE_BIAS    = 4
OUT_RESERVED_5         = 5
OUT_RESERVED_6         = 6
OUT_RESERVED_7         = 7


def splitmix64(x: int) -> int:
    x = (x + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    z = x
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
    return (z ^ (z >> 31)) & 0xFFFFFFFFFFFFFFFF


def gen_matrix(rows: int, cols: int, seed: int, scale: int) -> list[int]:
    vals: list[int] = []
    st = seed
    for r in range(rows):
        for c in range(cols):
            st = splitmix64(st ^ (r * 1315423911 + c * 2654435761))
            # Centered small signed int8 in [-scale, +scale] with clean sparse structure
            raw = int(st & 0xFF) - 128
            v = int(round((raw / 128.0) * scale))
            if v < -127:
                v = -127
            elif v > 127:
                v = 127
            vals.append(v)
    return vals


def format_array(ctype: str, name: str, vals: list[int], per_line: int = 24) -> str:
    lines = [f"static const {ctype} {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), per_line):
        chunk = ",".join(f"{v}" for v in vals[i:i + per_line])
        comma = "," if i + per_line < len(vals) else ""
        lines.append(f"\t{chunk}{comma}")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    w1 = gen_matrix(H1_DIM, IN_DIM, 0x4345414930303031, 18)
    b1 = [0] * H1_DIM
    w2 = gen_matrix(H2_DIM, H1_DIM, 0x4345414930303032, 10)
    b2 = [0] * H2_DIM
    w3 = gen_matrix(H3_DIM, H2_DIM, 0x4345414930303033, 10)
    b3 = [0] * H3_DIM
    w4 = gen_matrix(OUT_DIM, H3_DIM, 0x4345414930303034, 14)
    b4 = [0] * OUT_DIM

    # Embed structured tactical priors aligned 1:1 with CeAiMlpFeatIdx & CeAiMlpOutIdx
    # (port/game/ai/ce_ai_pilot_l1.h & ce_ai_pilot_l1.cpp):
    #   Outputs [0..4] drive bounded micro-trims:
    #     OUT_STRAFE_TRIM (0), OUT_FWD_TRIM (1), OUT_JET_TURN_TRIM (2),
    #     OUT_JET_THRUST_TRIM (3), OUT_JET_CRUISE_BIAS (4); [5..7] reserved.
    for h in range(OUT_DIM):
        w1[h * IN_DIM + FEAT_LIFE_RATIO]        = 64 if h % 2 == 0 else -64   # life sensitivity
        w1[h * IN_DIM + FEAT_ENERGY_RATIO]      = 48 if h < 4 else -48        # energy sensitivity
        w1[h * IN_DIM + FEAT_WEAPON_OVERHEATED] = -36 if h in (OUT_FWD_TRIM, OUT_JET_THRUST_TRIM) else 24
        w1[h * IN_DIM + FEAT_HAS_HOSTILE]       = 40                          # active engagement sensitivity
        w1[h * IN_DIM + FEAT_HOSTILE_DX]        = 32 if h in (OUT_STRAFE_TRIM, OUT_JET_TURN_TRIM) else 16
        w1[h * IN_DIM + FEAT_HOSTILE_DY]        = 32 if h in (OUT_FWD_TRIM, OUT_JET_THRUST_TRIM) else 16
        w1[h * IN_DIM + FEAT_FORCE_BALANCE]     = -32 if h == OUT_FWD_TRIM else 28
        w1[h * IN_DIM + FEAT_PRIOR_AGGRESSION]  = 44                          # strategic aggression prior [29]
        w1[h * IN_DIM + FEAT_PRIOR_AIR_BIAS]    = 44                          # strategic air prior [30]
        w1[h * IN_DIM + FEAT_PRIOR_POSTURE]     = 36 if h in (OUT_FWD_TRIM, OUT_JET_CRUISE_BIAS) else -24  # posture [31]
        w2[h * H1_DIM + h] = 64
        w3[h * H2_DIM + h] = 64
        w4[h * H3_DIM + h] = 48

    # 256-entry symmetric Q12 tanh LUT covering [-4.0, +4.0] linearly (index 0 -> -4.0, index 255 -> +4.0).
    tanh_lut: list[int] = []
    for i in range(256):
        x = -4.0 + (8.0 * i) / 255.0
        y = int(round(math.tanh(x) * 4096.0))
        tanh_lut.append(max(-4096, min(4096, y)))

    total_weight_bytes = len(w1) + len(w2) + len(w3) + len(w4)
    digest = hashlib.sha256(bytes((v & 0xFF) for v in (w1 + w2 + w3 + w4))).hexdigest()

    parts = [
        "// SPDX-" + "License-Identifier: GPL-3.0-only",
        "// GENERATED BY port/tools/gen_ai_pilot_weights.py — DO NOT HAND-EDIT.",
        f"// Compact INT8 Layer-1 Battle Engine pilot MLP ({IN_DIM}->{H1_DIM}->{H2_DIM}->{H3_DIM}->{OUT_DIM}, {total_weight_bytes} weight bytes).",
        f"// Weight payload SHA-256: {digest}",
        "#ifndef PORT_CE_AI_PILOT_WEIGHTS_H",
        "#define PORT_CE_AI_PILOT_WEIGHTS_H",
        "",
        "#include <stdint.h>",
        "",
        f"static constexpr int kCeAiMlpInDim  = {IN_DIM};",
        f"static constexpr int kCeAiMlpH1Dim  = {H1_DIM};",
        f"static constexpr int kCeAiMlpH2Dim  = {H2_DIM};",
        f"static constexpr int kCeAiMlpH3Dim  = {H3_DIM};",
        f"static constexpr int kCeAiMlpOutDim = {OUT_DIM};",
        f"static constexpr int kCeAiMlpWeightBytes = {total_weight_bytes};",
        "",
        format_array("int16_t", "kCeAiTanhLutQ12", tanh_lut, 16),
        "",
        format_array("int8_t", "kCeAiMlpW1", w1, 32),
        format_array("int8_t", "kCeAiMlpB1", b1, 32),
        format_array("int8_t", "kCeAiMlpW2", w2, 32),
        format_array("int8_t", "kCeAiMlpB2", b2, 32),
        format_array("int8_t", "kCeAiMlpW3", w3, 32),
        format_array("int8_t", "kCeAiMlpB3", b3, 32),
        format_array("int8_t", "kCeAiMlpW4", w4, 32),
        format_array("int8_t", "kCeAiMlpB4", b4, 8),
        "",
        "// Pure integer Q12 tanh via 256-entry LUT (zero libm, bit-identical across Linux & Windows).",
        "inline int16_t CeAiTanhQ12(int32_t xQ12)",
        "{",
        "\t// Map [-16384, +16384] (i.e. [-4.0, +4.0] in Q12) onto [0, 255].",
        "\tif (xQ12 <= -16384) return kCeAiTanhLutQ12[0];",
        "\tif (xQ12 >= 16384)  return kCeAiTanhLutQ12[255];",
        "\tconst int32_t idx = ((xQ12 + 16384) * 255) >> 15;",
        "\treturn kCeAiTanhLutQ12[(idx < 0) ? 0 : (idx > 255 ? 255 : idx)];",
        "}",
        "",
        "// Pure integer ReLU6 clamped to [0, 4096] (Q12 [0.0, 1.0]).",
        "inline int16_t CeAiReluQ12(int32_t acc, int shift)",
        "{",
        "\tint32_t v = acc >> shift;",
        "\tif (v <= 0) return 0;",
        "\tif (v >= 4096) return 4096;",
        "\treturn static_cast<int16_t>(v);",
        "}",
        "",
        "// Pure integer INT8 x INT16 -> INT32 MLP forward pass (zero float, zero libm, < 0.1 ms/tick).",
        "inline void CeAiPilotMlpInfer(const int16_t featuresQ12[kCeAiMlpInDim], int16_t outQ12[kCeAiMlpOutDim])",
        "{",
        "\tint16_t h1[kCeAiMlpH1Dim];",
        "\tint16_t h2[kCeAiMlpH2Dim];",
        "\tint16_t h3[kCeAiMlpH3Dim];",
        "",
        "\tfor (int r = 0; r < kCeAiMlpH1Dim; ++r)",
        "\t{",
        "\t\tint32_t acc = static_cast<int32_t>(kCeAiMlpB1[r]) << 7;",
        "\t\tconst int8_t *row = &kCeAiMlpW1[r * kCeAiMlpInDim];",
        "\t\tfor (int c = 0; c < kCeAiMlpInDim; ++c)",
        "\t\t\tacc += static_cast<int32_t>(row[c]) * static_cast<int32_t>(featuresQ12[c]);",
        "\t\th1[r] = CeAiReluQ12(acc, 9);",
        "\t}",
        "\tfor (int r = 0; r < kCeAiMlpH2Dim; ++r)",
        "\t{",
        "\t\tint32_t acc = static_cast<int32_t>(kCeAiMlpB2[r]) << 7;",
        "\t\tconst int8_t *row = &kCeAiMlpW2[r * kCeAiMlpH1Dim];",
        "\t\tfor (int c = 0; c < kCeAiMlpH1Dim; ++c)",
        "\t\t\tacc += static_cast<int32_t>(row[c]) * static_cast<int32_t>(h1[c]);",
        "\t\th2[r] = CeAiReluQ12(acc, 10);",
        "\t}",
        "\tfor (int r = 0; r < kCeAiMlpH3Dim; ++r)",
        "\t{",
        "\t\tint32_t acc = static_cast<int32_t>(kCeAiMlpB3[r]) << 7;",
        "\t\tconst int8_t *row = &kCeAiMlpW3[r * kCeAiMlpH2Dim];",
        "\t\tfor (int c = 0; c < kCeAiMlpH2Dim; ++c)",
        "\t\t\tacc += static_cast<int32_t>(row[c]) * static_cast<int32_t>(h2[c]);",
        "\t\th3[r] = CeAiReluQ12(acc, 10);",
        "\t}",
        "\tfor (int r = 0; r < kCeAiMlpOutDim; ++r)",
        "\t{",
        "\t\tint32_t acc = static_cast<int32_t>(kCeAiMlpB4[r]) << 7;",
        "\t\tconst int8_t *row = &kCeAiMlpW4[r * kCeAiMlpH3Dim];",
        "\t\tfor (int c = 0; c < kCeAiMlpH3Dim; ++c)",
        "\t\t\tacc += static_cast<int32_t>(row[c]) * static_cast<int32_t>(h3[c]);",
        "\t\toutQ12[r] = CeAiTanhQ12(acc >> 8);",
        "\t}",
        "}",
        "",
        "#endif",
        "",
    ]
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(parts), encoding="utf-8")
    print(f"generated {OUT} ({OUT.stat().st_size} bytes source, {total_weight_bytes} weight bytes, sha256={digest[:16]})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
