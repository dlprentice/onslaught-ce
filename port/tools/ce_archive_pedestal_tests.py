#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host tests, 1200-swap randomized arena soak test, and causal mutants for the
Grand Museum Interactive Archive Pedestal and --archive-scan scanner
(port/game/ce_archive_pedestal.{h,cpp}, port/game/ce_archive_scanner.{h,cpp})."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
PEDESTAL_H = GAME / "ce_archive_pedestal.h"
PEDESTAL_CPP = GAME / "ce_archive_pedestal.cpp"
SCANNER_H = GAME / "ce_archive_scanner.h"
SCANNER_CPP = GAME / "ce_archive_scanner.cpp"
LINUX_MAIN = ROOT / "port/platform/linux/main.cpp"

DRIVER = r"""
#define CE_ARCHIVE_POISON 1
#include "ce_archive_scanner.h"
#include "ce_archive_pedestal.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <zlib.h>

using namespace CeArchiveScanner;
using namespace CeArchivePedestal;

static void Need(bool ok, const char *why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(2);
    }
}

static bool Near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

static void PutU16(std::vector<unsigned char> &buf, uint16_t v) {
    buf.push_back(static_cast<unsigned char>(v & 0xFFu));
    buf.push_back(static_cast<unsigned char>((v >> 8) & 0xFFu));
}

static void PutU32(std::vector<unsigned char> &buf, uint32_t v) {
    buf.push_back(static_cast<unsigned char>(v & 0xFFu));
    buf.push_back(static_cast<unsigned char>((v >> 8) & 0xFFu));
    buf.push_back(static_cast<unsigned char>((v >> 16) & 0xFFu));
    buf.push_back(static_cast<unsigned char>((v >> 24) & 0xFFu));
}

static void PutF32(std::vector<unsigned char> &buf, float f) {
    uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    PutU32(buf, bits);
}

static void PutChunk(std::vector<unsigned char> &dst, const char tag[4], const std::vector<unsigned char> &payload) {
    dst.insert(dst.end(), tag, tag + 4);
    PutU32(dst, static_cast<uint32_t>(payload.size()));
    dst.insert(dst.end(), payload.begin(), payload.end());
}

static std::vector<unsigned char> CompressAya(const std::vector<unsigned char> &raw, size_t blockSize = 16384u) {
    std::vector<unsigned char> out;
    size_t pos = 0;
    while (pos < raw.size()) {
        const size_t take = (raw.size() - pos < blockSize) ? (raw.size() - pos) : blockSize;
        uLongf ccap = compressBound(static_cast<uLong>(take));
        std::vector<unsigned char> cbuf(ccap);
        const int rc = compress2(cbuf.data(), &ccap, raw.data() + pos, static_cast<uLong>(take), Z_DEFAULT_COMPRESSION);
        Need(rc == Z_OK, "zlib compress2 fixture succeeded");
        PutU32(out, static_cast<uint32_t>(ccap));
        out.insert(out.end(), cbuf.begin(), cbuf.begin() + ccap);
        pos += take;
    }
    return out;
}

struct SynthMeshOpts {
    uint32_t gridN = 2;       // produces (gridN+1)^2 verts, 2*gridN^2 tris
    bool skeletal = false;    // 48-byte stride skeletal vertices
    bool withRefPart = false; // add a partType==6 reference part translated in +X
    bool withCycle = false;   // inject child cycle for negative test
    bool badIndex = false;    // inject out-of-bounds IBUF index
    bool nanVertex = false;   // inject NaN coordinate
    bool nanUv = false;       // inject NaN texture coordinate
    bool zeroGeom = false;    // zero pieces
    float scale = 2.0f;
};

static std::vector<unsigned char> BuildSyntheticMeshStream(const SynthMeshOpts &opt) {
    const uint32_t numParts = opt.withRefPart ? 3u : 1u;
    const uint32_t numTex = 2u;

    std::vector<unsigned char> stream;
    // CMSH (372 bytes)
    std::vector<unsigned char> cmsh(372u, 0);
    auto write32 = [](std::vector<unsigned char> &b, size_t off, uint32_t v) {
        b[off + 0] = static_cast<unsigned char>(v & 0xFFu);
        b[off + 1] = static_cast<unsigned char>((v >> 8) & 0xFFu);
        b[off + 2] = static_cast<unsigned char>((v >> 16) & 0xFFu);
        b[off + 3] = static_cast<unsigned char>((v >> 24) & 0xFFu);
    };
    write32(cmsh, 0x04u, numTex);
    std::memcpy(cmsh.data() + 0x24u, "synth_unit", 10u);
    write32(cmsh, 0x15Cu, numParts);
    PutChunk(stream, "CMSH", cmsh);

    // CMST (36 * numTex)
    std::vector<unsigned char> cmst(36u * numTex, 0);
    for (uint32_t ti = 0; ti < numTex; ++ti) write32(cmst, 36u * ti + 8u, 1u);
    PutChunk(stream, "CMST", cmst);

    // MSHT -> TEXB
    const char *texNames[2] = {"meshtex\\unit_hull.tga", "meshtex\\unit_trim.tga"};
    for (uint32_t ti = 0; ti < numTex; ++ti) {
        std::vector<unsigned char> texb;
        PutF32(texb, 1.0f); // blend
        PutF32(texb, 0.0f); PutF32(texb, 0.0f); // u/v offset
        PutF32(texb, 1.0f); PutF32(texb, 1.0f); // u/v scale
        char nameBuf[128] = {};
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", texNames[ti]);
        texb.insert(texb.end(), reinterpret_cast<unsigned char *>(nameBuf),
                    reinterpret_cast<unsigned char *>(nameBuf) + 128u);
        std::vector<unsigned char> msht;
        PutChunk(msht, "TEXB", texb);
        PutChunk(stream, "MSHT", msht);
    }

    auto makePart = [&](uint32_t partIdx, uint32_t ptype, const std::vector<uint32_t> &children,
                        int parentIdx, int refTarget, float tx, bool hasGeom) {
        std::vector<unsigned char> mesp;
        std::vector<unsigned char> cmsp(316u, 0);
        write32(cmsp, 0x88u, partIdx);
        write32(cmsp, 0x8Cu, ptype);
        write32(cmsp, 0x90u, static_cast<uint32_t>(children.size()));
        write32(cmsp, 0x98u, parentIdx >= 0 ? 1u : 0u);
        write32(cmsp, 0xB8u, 1u); // vframes
        write32(cmsp, 0xBCu, 1u); // hframes
        PutChunk(mesp, "CMSP", cmsp);

        if (!children.empty()) {
            std::vector<unsigned char> chld;
            for (uint32_t ch : children) PutU32(chld, ch);
            PutChunk(mesp, "CHLD", chld);
        }
        if (parentIdx >= 0) {
            std::vector<unsigned char> prnt;
            PutU32(prnt, static_cast<uint32_t>(parentIdx));
            PutChunk(mesp, "PRNT", prnt);
        }
        std::vector<unsigned char> vhfm(1u, 0u);
        PutChunk(mesp, "VHFM", vhfm);

        // HORI: identity 3x4 floats (48 bytes)
        std::vector<unsigned char> hori;
        const float id[12] = {1,0,0,0, 0,1,0,0, 0,0,1,0};
        for (float f : id) PutF32(hori, f);
        PutChunk(mesp, "HORI", hori);

        // HPOS: translation (16 bytes)
        std::vector<unsigned char> hpos;
        PutF32(hpos, tx); PutF32(hpos, 0.0f); PutF32(hpos, 0.0f); PutF32(hpos, 0.0f);
        PutChunk(mesp, "HPOS", hpos);

        if (refTarget >= 0) {
            std::vector<unsigned char> refr;
            PutU32(refr, static_cast<uint32_t>(refTarget));
            PutChunk(mesp, "REFR", refr);
        }

        // PMVB
        std::vector<unsigned char> pmvb;
        std::vector<unsigned char> cmvb(296u, 0);
        const uint32_t stride = opt.skeletal ? 48u : 36u;
        const uint32_t fvf = opt.skeletal ? 0u : 0x152u;
        const uint32_t pieces = (hasGeom && !opt.zeroGeom) ? 2u : 0u;
        write32(cmvb, 0x108u, pieces);
        write32(cmvb, 0x114u, stride);
        write32(cmvb, 0x118u, fvf);
        write32(cmvb, 0x11Cu, 4u); // D3DPT_TRIANGLESTRIP
        PutChunk(pmvb, "CMVB", cmvb);

        if (pieces > 0u) {
            const uint32_t n = opt.gridN;
            const uint32_t cols = n + 1u;
            const uint32_t vc = cols * cols;
            std::vector<unsigned char> vbuf;
            vbuf.reserve(vc * stride);
            for (uint32_t y = 0; y < cols; ++y) {
                for (uint32_t x = 0; x < cols; ++x) {
                    float px = (float(x) / float(n) - 0.5f) * opt.scale;
                    if (opt.nanVertex && x == 0 && y == 0) px = std::nanf("");
                    const float py = (float(y) / float(n) - 0.5f) * opt.scale;
                    const float pz = ((x + y) & 1u) ? -0.35f * opt.scale : 0.0f;
                    PutF32(vbuf, px); PutF32(vbuf, py); PutF32(vbuf, pz);
                    if (opt.skeletal) {
                        PutF32(vbuf, 0.0f); PutF32(vbuf, 0.0f); PutF32(vbuf, 0.0f);
                    }
                    PutF32(vbuf, 0.0f); PutF32(vbuf, 0.0f); PutF32(vbuf, -1.0f);
                    PutU32(vbuf, 0xFFFFFFFFu);
                    float uval = float(x) / float(n);
                    if (opt.nanUv && x == 0 && y == 0) uval = std::nanf("");
                    PutF32(vbuf, uval);
                    PutF32(vbuf, float(y) / float(n));
                }
            }

            // Build two stitched triangle strips (half rows on tex 0, half rows on tex 1)
            for (uint32_t pi = 0; pi < pieces; ++pi) {
                std::vector<uint16_t> idx;
                const uint32_t rStart = (pi == 0u) ? 0u : (n / 2u);
                const uint32_t rEnd = (pi == 0u) ? (n / 2u > 0u ? n / 2u : 1u) : n;
                for (uint32_t r = rStart; r < rEnd; ++r) {
                    if (!idx.empty()) {
                        // Degenerate stitch
                        idx.push_back(idx.back());
                        idx.push_back(static_cast<uint16_t>(r * cols));
                    }
                    for (uint32_t c = 0; c < cols; ++c) {
                        idx.push_back(static_cast<uint16_t>(r * cols + c));
                        idx.push_back(static_cast<uint16_t>((r + 1u) * cols + c));
                    }
                }
                if (opt.badIndex && pi == 0u && !idx.empty()) idx[0] = static_cast<uint16_t>(vc + 7u);

                std::vector<unsigned char> mmpt;
                PutU32(mmpt, static_cast<uint32_t>(vbuf.size()));
                PutU32(mmpt, static_cast<uint32_t>(idx.size() * 2u));
                PutU32(mmpt, static_cast<uint32_t>(idx.size()));
                PutU32(mmpt, vc);
                PutU32(mmpt, static_cast<uint32_t>(idx.size() - 2u));
                PutU32(mmpt, 1u);
                PutChunk(pmvb, "MMPT", mmpt);

                std::vector<unsigned char> ibuf;
                for (uint16_t ii : idx) PutU16(ibuf, ii);
                PutChunk(pmvb, "IBUF", ibuf);

                std::vector<unsigned char> emptyVbuf;
                PutChunk(pmvb, "VBUF", pi == 0u ? vbuf : emptyVbuf);

                std::vector<unsigned char> texr;
                PutU32(texr, pi);
                for (int s = 1; s < 6; ++s) PutU32(texr, 0xFFFFFFFFu);
                PutChunk(pmvb, "TEXR", texr);
            }
        }
        PutChunk(mesp, "PMVB", pmvb);
        PutChunk(stream, "MESP", mesp);
    };

    if (!opt.withRefPart) {
        std::vector<uint32_t> ch;
        if (opt.withCycle) ch.push_back(0u);
        makePart(0u, opt.skeletal ? 3u : 1u, ch, -1, -1, 0.0f, true);
    } else {
        // Root null part (0) -> children 1 (rigid geom) & 2 (ref -> 1 shifted +3.0 in X)
        std::vector<uint32_t> rootCh = {1u, 2u};
        std::vector<uint32_t> ch1;
        if (opt.withCycle) ch1.push_back(0u);
        makePart(0u, 2u, rootCh, -1, -1, 0.0f, false);
        makePart(1u, opt.skeletal ? 3u : 1u, ch1, 0, -1, -1.5f, true);
        makePart(2u, 6u, {}, 0, 1, 1.5f, false);
    }
    return stream;
}

int main() {
    // 1. Exact 232 Goodies breakdown: 45 models + 149 art plates + 33 FMVs + 5 levels = 232.
    Need(CountGoodies(kGoodieMesh) == 45u, "45 Goodie 3D models");
    Need(CountGoodies(kGoodieImage) == 149u, "149 Goodie concept art plates");
    Need(CountGoodies(kGoodieFmv) == 33u, "33 Goodie Cinema FMVs");
    Need(CountGoodies(kGoodieLevel) == 5u, "5 Goodie Bonus Levels");
    Need(CountGoodies(kGoodieMesh) + CountGoodies(kGoodieImage) +
         CountGoodies(kGoodieFmv) + CountGoodies(kGoodieLevel) == 232u, "sum equals 232 goodies");
    Need(GoodieIdAt(kGoodieMesh, 0u) == 8, "first 3D goodie is id 8");
    Need(GoodieIdAt(kGoodieMesh, 44u) == 76, "last 3D goodie is id 76");
    Need(GoodieIdAt(kGoodieMesh, 45u) == -1, "out-of-range goodie ordinal returns -1");
    for (unsigned i = 0; i < 5u; ++i) {
        const int gid = GoodieIdAt(kGoodieLevel, i);
        Need(BonusLevelNumberForGoodie(gid) == int(901u + i), "bonus level goodie maps to 901..905");
    }

    // 2. Pedestal placement, ray-pick, controls, categories, keyboard, scale, lightbox, titles.
    PedestalState st = {};
    ResetState(&st);
    Need(st.initialized, "pedestal state initialized");
    Need(Near(st.pose.localX, 0.0f) && Near(st.pose.localY, 36.0f) &&
         Near(st.pose.plinthTopZ, 1.05f) && Near(st.pose.plinthHalf, 2.0f),
         "agreed Grand Atrium rotunda centerpiece placement");
    st.counts[kCatRetailMeshes] = 213u;

    // All 4 south-facing brass buttons along localY = 33.65f pick distinctly without overlap.
    for (unsigned b = 0; b < unsigned(kBtnCount); ++b) {
        float bx, by, bz;
        ButtonPosition(st.pose, b, &bx, &by, &bz);
        Need(Near(by, 33.65f), "control button mounted along localY = 33.65f");
        const float ex = bx, ey = by - 2.0f, ez = 1.65f;
        Need(PickButton(st.pose, ex, ey, ez, bx - ex, by - ey, bz - ez) == int(b),
             "direct ray picks exact button");
        Need(PickButton(st.pose, ex, ey, ez, 0.0f, -1.0f, 0.0f) == -1,
             "looking away misses button");
        Need(PickButton(st.pose, bx, by - 8.0f, 1.65f, 0.0f, 1.0f, -0.07f) == -1,
             "out-of-range ray misses button");
    }
    Need(PickButton(st.pose, 0.0f, 32.0f, 1.65f, 0.0f, 0.0f, 0.0f) == -1, "zero aim vector misses");

    // Prev / Next wrap-around + swap fade reset.
    Need(st.selected[kCatRetailMeshes] == 0u, "starts at index 0");
    Need(ActivateButton(&st, kBtnPrev) && st.selected[kCatRetailMeshes] == 212u, "Prev wraps 0 -> 212");
    Need(Near(st.fadeAlpha, 0.0f), "swap resets fadeAlpha to 0");
    Need(ActivateButton(&st, kBtnNext) && st.selected[kCatRetailMeshes] == 0u, "Next wraps 212 -> 0");

    // Category cycling visits all 5 categories cleanly and wraps back.
    Need(st.category == kCatRetailMeshes, "starts on Retail Meshes");
    Need(ActivateButton(&st, kBtnCategory) && st.category == kCatGoodieModels, "cat -> GoodieModels");
    Need(ActivateButton(&st, kBtnCategory) && st.category == kCatConceptArt, "cat -> ConceptArt");
    Need(ActivateButton(&st, kBtnCategory) && st.category == kCatCinemaFmvs, "cat -> CinemaFmvs");
    Need(ActivateButton(&st, kBtnCategory) && st.category == kCatBonusLevels, "cat -> BonusLevels");
    Need(ActivateButton(&st, kBtnCategory) && st.category == kCatRetailMeshes, "cat wraps -> RetailMeshes");

    // Rotation, pause toggle, fade ramp, and dt clamp.
    const float y0 = st.yawRad;
    StepState(&st, 0.1f, false);
    Need(Near(st.yawRad, y0 + kDefaultRateRadPerSec * 0.1f), "rotates smoothly");
    Need(st.fadeAlpha > 0.3f && st.fadeAlpha < 0.6f, "fadeAlpha ramps during swap");
    StepState(&st, 5.0f, false); // dt clamped to 0.25s
    Need(Near(st.yawRad, y0 + kDefaultRateRadPerSec * 0.35f), "large dt clamped to 0.25s");
    Need(Near(st.fadeAlpha, 1.0f), "fade completes to 1.0");
    const float yFrozen = st.yawRad;
    Need(ActivateButton(&st, kBtnPause) && st.paused, "pause toggle freezes rotation");
    StepState(&st, 0.2f, false);
    Need(Near(st.yawRad, yFrozen), "paused pedestal holds yaw");
    ActivateButton(&st, kBtnPause);
    StepState(&st, 0.2f, true); // game pause
    Need(Near(st.yawRad, yFrozen), "game pause freezes yaw");

    // Keyboard shortcuts gated by visitorNear + repeat policy.
    st.visitorNear = false;
    Need(!HandleKeyCode(&st, kDikRBracket, false), "keys ignored when visitor is far");
    st.visitorNear = true;
    Need(HandleKeyCode(&st, kDikRBracket, false) && st.selected[kCatRetailMeshes] == 1u, "] advances");
    Need(HandleKeyCode(&st, kDikNext, true) && st.selected[kCatRetailMeshes] == 2u, "PgDn advances with repeat");
    Need(HandleKeyCode(&st, kDikLBracket, false) && st.selected[kCatRetailMeshes] == 1u, "[ steps back");
    Need(HandleKeyCode(&st, kDikPrior, false) && st.selected[kCatRetailMeshes] == 0u, "PgUp steps back");
    Need(HandleKeyCode(&st, kDikTab, true) && st.category == kCatRetailMeshes, "Tab repeat does not flicker category");
    Need(HandleKeyCode(&st, kDikTab, false) && st.category == kCatGoodieModels, "Tab switches category");
    Need(HandleKeyCode(&st, kDikP, true) && !st.paused, "P repeat does not flicker pause");
    Need(HandleKeyCode(&st, kDikP, false) && st.paused, "P toggles pause");

    // Scale normalization, lightbox aspect fit, and clean ASCII titles.
    Need(Near(ComputeNormalizedScale(9.2f, kTargetSpan), 0.5f), "scale ratio fits targetSpan 4.6");
    Need(Near(ComputeNormalizedScale(1000.0f, kTargetSpan), 0.015f), "min scale clamped");
    Need(Near(ComputeNormalizedScale(0.01f, kTargetSpan), 8.0f), "max scale clamped");
    const LightboxExtent wide = FitLightbox(1600u, 800u, 3.0f, 3.0f);
    Need(Near(wide.halfW, 1.5f) && Near(wide.halfH, 0.75f), "wide art preserves 2:1 aspect");
    const LightboxExtent tall = FitLightbox(600u, 1200u, 3.0f, 3.0f);
    Need(Near(tall.halfW, 0.75f) && Near(tall.halfH, 1.5f), "tall art preserves 1:2 aspect");
    char titleOut[64];
    FormatDisplayTitle("goodies\\ca_be_aquila\\ca_be_final01.tga", titleOut, sizeof(titleOut));
    Need(std::strcmp(titleOut, "Ca Be Final01") == 0, "strips folders/extensions cleanly");
    FormatDisplayTitle("unit_[bad]|test.msh.aya", titleOut, sizeof(titleOut));
    Need(std::strchr(titleOut, '[') == NULL && std::strchr(titleOut, ']') == NULL &&
         std::strchr(titleOut, '|') == NULL, "strips unsafe font glyphs");

    // 3. Dedicated 8 MiB Swap Arena + 2-Pass Parser + 1,200-Swap Randomized Soak Test!
    SwapArena arena = {};
    Need(arena.Init(true), "8 MiB host-malloc swap arena allocates");
    Need(arena.capacity == 8u * 1024u * 1024u, "exact 8 MiB capacity");
    Need(arena.VerifyPoisonTail(), "fresh arena initialized to 0xDD poison");
    unsigned char *const fixedBase = arena.base;

    // Build 6 diverse valid compressed .msh.aya fixtures (rigid, skeletal, ref-part, large grids).
    std::vector<std::vector<unsigned char>> validFixtures;
    const SynthMeshOpts configs[6] = {
        {2,  false, false, false, false, false, false, false, 2.0f},
        {8,  false, true,  false, false, false, false, false, 4.5f},
        {6,  true,  false, false, false, false, false, false, 1.8f},
        {16, false, false, false, false, false, false, false, 12.0f},
        {12, true,  true,  false, false, false, false, false, 6.0f},
        {24, false, true,  false, false, false, false, false, 20.0f},
    };
    for (int i = 0; i < 6; ++i) {
        const auto stream = BuildSyntheticMeshStream(configs[i]);
        validFixtures.push_back(CompressAya(stream));
    }

    // Verify exact parsed counts & bounds on each fixture first.
    size_t expectedMaxUsed = 0;
    for (int i = 0; i < 6; ++i) {
        arena.Reset();
        Need(arena.used == 0u && arena.VerifyPoisonTail(), "reset zeroes used & restores 0xDD poison");
        ParsedMesh pm = {};
        const ParseError pe = DecompressAndParseAyaMesh(validFixtures[i].data(), validFixtures[i].size(), &arena, &pm);
        Need(pe == kParseOk, "valid fixture parses cleanly");
        Need(pm.vertexCount > 0u && pm.triangleCount > 0u && pm.batchCount == 2u, "valid geometry & 2 texture batches");
        Need(pm.bounds.valid && pm.bounds.span > 1.0f, "computed valid exact bounds");
        Need(arena.VerifyPoisonTail(), "unused arena tail remains 0xDD after parse");
        if (configs[i].skeletal) Need(pm.skeletalPartCount == 1u, "skeletal part counted");
        if (configs[i].withRefPart) Need(pm.referencePartCount == 1u, "reference part counted & instanced");
        if (arena.used > expectedMaxUsed) expectedMaxUsed = arena.used;
    }

    // 1,200-swap deterministic pseudo-random soak test: proves ZERO fragmentation, flat highWater, 0xDD poison!
    uint32_t rng = 0x13579BDFu;
    size_t highWaterAfterWarmup = 0;
    for (unsigned swap = 0; swap < 1200u; ++swap) {
        rng = rng * 1664525u + 1013904223u;
        const size_t pick = (rng >> 16) % validFixtures.size();
        arena.Reset();
        Need(arena.base == fixedBase, "arena base pointer never reallocates or fragments");
        Need(arena.used == 0u, "arena resets to 0 on every swap");
        // Verify full 0xDD poison periodically and tail poison on every single swap.
        if ((swap % 60u) == 0u) Need(arena.VerifyPoisonTail(), "full 0xDD poison intact on reset");
        ParsedMesh pm = {};
        const ParseError pe = DecompressAndParseAyaMesh(validFixtures[pick].data(), validFixtures[pick].size(), &arena, &pm);
        Need(pe == kParseOk && pm.bounds.valid, "soak swap parsed cleanly");
        Need(arena.used <= expectedMaxUsed, "arena usage stays strictly flat across 1200 swaps");
        if (swap == 50u) highWaterAfterWarmup = arena.highWater;
        if (swap > 50u) Need(arena.highWater == highWaterAfterWarmup, "flat high-water mark after warmup");
    }
    Need(arena.swapCount >= 1206u, "recorded all swaps");
    Need(arena.highWater == expectedMaxUsed && arena.highWater < kSwapArenaBytes / 4u, "high-water bounded");

    // 4. Corrupt / Truncated / Adversarial .msh.aya Quarantine Rejection Tests!
    auto expectFail = [&](const std::vector<unsigned char> &badAya, ParseError wantErr, const char *why) {
        arena.Reset();
        ParsedMesh pm = {};
        const ParseError got = DecompressAndParseAyaMesh(badAya.data(), badAya.size(), &arena, &pm);
        Need(got == wantErr, why);
    };
    {
        // Truncated header
        std::vector<unsigned char> tiny = {0x01, 0x02};
        expectFail(tiny, kParseAyaTruncated, "rejects <4 byte AYA");
        // Corrupt member length > file size
        std::vector<unsigned char> badLen = {0xFF, 0x00, 0x00, 0x00, 0x78, 0x9C};
        expectFail(badLen, kParseAyaBadMemberSize, "rejects truncated AYA member");
        // Corrupt zlib stream
        std::vector<unsigned char> badZlib = {0x04, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
        expectFail(badZlib, kParseAyaZlibError, "rejects corrupt zlib bytes");
        // Bad CMSH tag
        std::vector<unsigned char> badTag = {'B','A','D','!', 0,0,0,0};
        expectFail(CompressAya(badTag), kParseBadCmsh, "rejects bad CMSH tag");
        // Hierarchy cycle
        SynthMeshOpts cyc = {}; cyc.withRefPart = true; cyc.withCycle = true;
        expectFail(CompressAya(BuildSyntheticMeshStream(cyc)), kParseBadHierarchy, "rejects hierarchy cycle");
        // Index OOB
        SynthMeshOpts oob = {}; oob.badIndex = true;
        expectFail(CompressAya(BuildSyntheticMeshStream(oob)), kParseIndexOutOfBounds, "rejects OOB index");
        // NaN vertex position rejected; NaN UV sanitized to finite 0.0f (matching retail quirks)
        SynthMeshOpts nanv = {}; nanv.nanVertex = true;
        expectFail(CompressAya(BuildSyntheticMeshStream(nanv)), kParseNonFiniteVertex, "rejects NaN vertex");
        {
            SynthMeshOpts nanu = {}; nanu.nanUv = true;
            const auto uvAya = CompressAya(BuildSyntheticMeshStream(nanu));
            arena.Reset();
            ParsedMesh pmUv = {};
            Need(DecompressAndParseAyaMesh(uvAya.data(), uvAya.size(), &arena, &pmUv) == kParseOk,
                 "sanitizes non-finite UV instead of rejecting healthy geometry");
            for (uint32_t vi = 0; vi < pmUv.vertexCount; ++vi)
                Need(std::isfinite(pmUv.vertices[vi].u) && std::isfinite(pmUv.vertices[vi].v),
                     "all output UVs are finite after sanitization");
        }
        // Zero geometry
        SynthMeshOpts zg = {}; zg.zeroGeom = true;
        expectFail(CompressAya(BuildSyntheticMeshStream(zg)), kParseZeroGeometry, "rejects zero-geometry mesh");
    }

    // 5. Quarantine Cache v1 parsing & fingerprint validation.
    {
        const char *cacheText =
            "# Onslaught CE Archive Scan Cache v1\n"
            "version=1\n"
            "fingerprint=0123456789abcdef\n"
            "total=213\n"
            "ok=212\n"
            "quarantined=1\n"
            "OK\tdata/resources/meshes/m_f_be1.msh.aya\tverts=1200\ttris=800\n"
            "QUARANTINE\tdata/resources/meshes/m_corrupt.msh.aya\treason=index_oob\n";
        QuarantineList ql = {};
        Need(!ParseQuarantineCacheText(cacheText, std::strlen(cacheText), 0x1111111111111111ULL, &ql),
             "mismatched fingerprint invalidates cache");
        Need(ql.count == 0u, "invalidated cache stays empty");
        Need(ParseQuarantineCacheText(cacheText, std::strlen(cacheText), 0x0123456789ABCDEFULL, &ql),
             "matching fingerprint loads quarantine cache");
        Need(ql.count == 1u && ql.IsQuarantined("DATA/RESOURCES/MESHES/M_CORRUPT.MSH.AYA"),
             "case-insensitive quarantine lookup");
        Need(!ql.IsQuarantined("data/resources/meshes/m_f_be1.msh.aya"), "healthy mesh not quarantined");
        Need(std::strcmp(ql.ReasonFor("data/resources/meshes/m_corrupt.msh.aya"), "index_oob") == 0,
             "preserves exact quarantine reason");
    }

    arena.Reset();
    Need(arena.VerifyPoisonTail(), "final poison check passes");
    arena.Shutdown();
    Need(arena.base == NULL && arena.capacity == 0u, "arena shuts down cleanly");

    std::printf("ce_archive_pedestal_tests: all checks + 1200-swap soak passed (peak=%lu bytes)\n",
                static_cast<unsigned long>(expectedMaxUsed));
    return 0;
}
"""

SCANNER_MUTANTS = [
    ("arena-no-poison",
     "if (poisonOnReset && base && used > 0) {",
     "if (false && poisonOnReset && base && used > 0) {"),
    ("skip-index-oob",
     "if (Le16(ibuf + k * 2u) >= partVertCount) {\n                        *outErr = kParseIndexOutOfBounds;",
     "if (false && Le16(ibuf + k * 2u) >= partVertCount) {\n                        *outErr = kParseIndexOutOfBounds;"),
    ("skip-nan-check",
     "inline bool FiniteVec3(const Vec3 &v) {\n    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);\n}",
     "inline bool FiniteVec3(const Vec3 &v) {\n    (void)v; return true;\n}"),
    ("skip-cycle-check",
     "if (pv.visited) return false;",
     "if (false && pv.visited) return false;"),
    ("ignore-cache-fingerprint",
     "if (expectedFingerprint != 0 && fileFp != expectedFingerprint) {",
     "if (false && expectedFingerprint != 0 && fileFp != expectedFingerprint) {"),
]

PEDESTAL_MUTANTS = [
    ("ignore-pause-rotation",
     "if (!s->paused) {",
     "if (true) {"),
    ("ignore-button-range",
     "if (t <= 0.15f || t > kButtonMaxRange) continue;",
     "if (t <= 0.15f) continue;"),
    ("skip-swap-fade-reset",
     "s->selected[s->category] = (s->selected[s->category] + n - 1u) % n;\n    s->fadeAlpha = 0.0f;",
     "s->selected[s->category] = (s->selected[s->category] + n - 1u) % n;\n    s->fadeAlpha = 1.0f;"),
    ("keys-ignore-near-gate",
     "if (!s || !s->initialized || !s->visitorNear) return false;",
     "if (!s || !s->initialized) return false;"),
    ("wrong-goodie-classify",
     "if (id == 76u) return kGoodieMesh;",
     "if (id == 76u) return kGoodieImage;"),
]


def run(cmd, expect=0):
    res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return res.returncode == expect, res.stdout + res.stderr


def main():
    failures = []

    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + ("" if ok else ("\n" + detail if detail else "")))
        if not ok:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="ce-archive-pedestal-", dir=tempfile.gettempdir()) as tmp:
        d = Path(tmp)
        (d / "driver.cpp").write_text(DRIVER)
        asan_flags = ["c++", "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                      "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        ok, detail = run(asan_flags + ["-I" + str(GAME), str(d / "driver.cpp"), "-lz", "-o", str(d / "driver")])
        check("archive pedestal + scanner driver builds with ASan/UBSan", ok, detail)
        if ok:
            ok, detail = run([str(d / "driver")])
            check("232 goodies, 1200-swap arena soak, 0xDD poison, quarantine & controls pass", ok, detail)

        # Causal mutants against ce_archive_scanner.h
        scan_src = SCANNER_H.read_text()
        for label, old, new in SCANNER_MUTANTS:
            if scan_src.count(old) != 1:
                check("scanner mutant anchor " + label, False, "not found once: " + old)
                continue
            m = d / ("scan_" + label)
            m.mkdir()
            (m / SCANNER_H.name).write_text(scan_src.replace(old, new, 1))
            built, detail = run(["c++", "-std=c++14", "-O1", "-I" + str(m), "-I" + str(GAME),
                                 str(d / "driver.cpp"), "-lz", "-o", str(m / "driver")])
            if not built:
                check("mutant " + label + " caught", False, detail)
                continue
            caught, _ = run([str(m / "driver")], 2)
            check("mutant " + label + " caught", caught)

        # Causal mutants against ce_archive_pedestal.h
        ped_src = PEDESTAL_H.read_text()
        for label, old, new in PEDESTAL_MUTANTS:
            if ped_src.count(old) != 1:
                check("pedestal mutant anchor " + label, False, "not found once: " + old)
                continue
            m = d / ("ped_" + label)
            m.mkdir()
            (m / PEDESTAL_H.name).write_text(ped_src.replace(old, new, 1))
            built, detail = run(["c++", "-std=c++14", "-O1", "-I" + str(m), "-I" + str(GAME),
                                 str(d / "driver.cpp"), "-lz", "-o", str(m / "driver")])
            if not built:
                check("mutant " + label + " caught", False, detail)
                continue
            caught, _ = run([str(m / "driver")], 2)
            check("mutant " + label + " caught", caught)

    # Static contract, D3D9 safety, heap isolation, and CLI wiring checks.
    ped_h = PEDESTAL_H.read_text()
    ped_cpp = PEDESTAL_CPP.read_text()
    scan_h = SCANNER_H.read_text()
    scan_cpp = SCANNER_CPP.read_text()
    lmain = LINUX_MAIN.read_text()

    check("exported seam defines CE_MUSEUM_HAS_ARCHIVE_PEDESTAL 1",
          "#define CE_MUSEUM_HAS_ARCHIVE_PEDESTAL 1" in ped_h)
    check("all 9 exported Runtime* seam functions declared and defined",
          all(sig in ped_h and sig in ped_cpp for sig in (
              "void RuntimeInit()",
              "void RuntimeShutdown()",
              "void RuntimeUpdate(",
              "bool RuntimeHandleFire(",
              "bool RuntimeHandleKey(",
              "bool RuntimeIsAimingControl()",
              "bool RuntimeCue(",
              "void RuntimeDraw(",
              "void RuntimeSetBasePose(",
          )))
    check("pedestal renderer guards D3D9 state with StateBlock + LT.InvalidateAllStates + NORMALIZENORMALS",
          "CreateStateBlock(D3DSBT_ALL" in ped_cpp
          and "LT.InvalidateAllStates();" in ped_cpp
          and "D3DRS_NORMALIZENORMALS, TRUE" in ped_cpp
          and "RENDERINFO.mWorldChanged = true;" in ped_cpp
          and "RENDERINFO.mViewChanged = true;" in ped_cpp
          and "RENDERINFO.mProjectionChanged = true;" in ped_cpp)
    def strip_comments(src):
        src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
        return re.sub(r"//[^\n]*", "", src)

    check("zero retail heap operator new / STL / fopen / BEA_* env leaks in archive modules",
          all("BEA_" not in txt for txt in (ped_h, ped_cpp, scan_h, scan_cpp))
          and all(re.search(r"\b(new\s+|delete\s+|std::vector|std::string|fopen\s*\(|fclose\s*\()",
                            strip_comments(txt)) is None
                  for txt in (ped_cpp, scan_cpp)))
    check("Linux main.cpp and Windows static ctor wire --archive-scan pre-WinMain",
          '"--archive-scan"' in lmain and "PortCeArchiveScanCli(" in lmain
          and '"--archive-scan"' in scan_cpp and "ExitProcess(" in scan_cpp)
    check("SPDX GPL-3.0-only header present on all archive files",
          all("SPDX-License-Identifier: GPL-3.0-only" in txt for txt in (ped_h, ped_cpp, scan_h, scan_cpp)))
    check("player-facing strings stay free of forbidden jargon",
          not re.search(r'"[^"]*(?:prototype|experimental|ce\.ini)[^"]*"', ped_h, re.I))

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
