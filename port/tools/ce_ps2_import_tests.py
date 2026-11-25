#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for Onslaught CE's native PS2 Level 888 importer (port/platform/common/ce_ps2_import.cpp).

Builds a standalone host C++ driver with AddressSanitizer + UndefinedBehaviorSanitizer linked against the actual
CE mod loader (mods.cpp) and settings store (ce.cpp), plus cross-compiles ce_ps2_import.cpp for Windows i686 via
llvm-mingw when the toolchain is present. All test inputs are synthesized cleanly from format rules with zero
retail or PS2 disc bytes in the repository.
"""
from __future__ import annotations

import atexit
import collections
import hashlib
import json
import math
import os
import resource
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
sys.path.insert(0, str(PORT / "tools"))
import deps  # noqa: E402
sys.path.insert(0, str(ROOT / "tools"))
import export_public  # noqa: E402

PS2_H = PORT / "platform/common/ce_ps2_import.h"
PS2_CPP = PORT / "platform/common/ce_ps2_import.cpp"
MODS_CPP = PORT / "platform/common/mods.cpp"
CE_CPP = PORT / "platform/common/ce.cpp"
SHA256_C = PORT / "updater/sha256.c"
DRIVER_CPP = PORT / "tools/tests/ce_ps2_import_driver.cpp"
NOT_IN_FACE = set("$@[\\]^_`{|}~")

_BUILD_DIR: Path | None = None
_DRIVER_EXE: Path | None = None


def _no_core() -> None:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def _build_driver() -> Path:
    global _BUILD_DIR, _DRIVER_EXE
    if _DRIVER_EXE is not None and _DRIVER_EXE.is_file():
        return _DRIVER_EXE
    scratch_base = ROOT / "local-data/port/ce-ps2-import-tests"
    scratch_base.mkdir(parents=True, exist_ok=True)
    _BUILD_DIR = Path(tempfile.mkdtemp(prefix=f"run-{os.getpid()}-", dir=scratch_base))
    atexit.register(shutil.rmtree, _BUILD_DIR, True)

    # Provide a minimal SDL3/SDL.h stub for ce.cpp's unused report/URL helpers in the standalone host driver.
    sdl_dir = _BUILD_DIR / "stub_inc/SDL3"
    sdl_dir.mkdir(parents=True, exist_ok=True)
    (sdl_dir / "SDL.h").write_text(
        "#ifndef STUB_SDL_H\n#define STUB_SDL_H\n"
        "#include <stdint.h>\n"
        "typedef uint64_t Uint64;\n"
        "#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
        "Uint64 SDL_GetTicks(void);\n"
        "static inline int SDL_OpenURL(const char *u) { (void)u; return 0; }\n"
        "#ifdef __cplusplus\n}\n#endif\n#endif\n",
        encoding="utf-8",
    )

    cc = os.environ.get("CC", "gcc")
    cxx = os.environ.get("CXX", "g++")
    san = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    inc = ["-I", str(_BUILD_DIR / "stub_inc"), "-I", str(PORT / "platform/common"), "-I", str(PORT / "include")]

    sha_o = _BUILD_DIR / "sha256.o"
    subprocess.run([cc, "-std=gnu99", *san, "-c", str(SHA256_C), "-o", str(sha_o)],
                   check=True, capture_output=True, text=True, timeout=120, preexec_fn=_no_core)

    mods_o = _BUILD_DIR / "mods.o"
    ce_o = _BUILD_DIR / "ce.o"
    for src, obj in ((MODS_CPP, mods_o), (CE_CPP, ce_o)):
        built_dep = subprocess.run(
            [cxx, "-std=gnu++17", *san, '-DCE_VERSION="0.2.0"', "-DCE_NETWORK_OFF", *inc, "-c", str(src), "-o", str(obj)],
            capture_output=True, text=True, timeout=180, preexec_fn=_no_core,
        )
        if built_dep.returncode != 0:
            raise RuntimeError(f"dep build failed ({src.name}):\n{built_dep.stdout}\n{built_dep.stderr}")

    exe = _BUILD_DIR / "ce_ps2_import_driver"
    cmd = [
        cxx, "-std=gnu++17", *san,
        "-Wall", "-Wextra", "-Werror",
        '-DCE_VERSION="0.2.0"',
        "-DCE_PS2_IMPORT_NO_SDL",
        *inc,
        str(DRIVER_CPP),
        str(PS2_CPP),
        str(mods_o),
        str(ce_o),
        str(sha_o),
        "-lz", "-lpthread",
        "-o", str(exe),
    ]
    built = subprocess.run(cmd, capture_output=True, text=True, timeout=180, preexec_fn=_no_core)
    if built.returncode != 0:
        raise RuntimeError(f"driver build failed:\n{built.stdout}\n{built.stderr}")
    _DRIVER_EXE = exe
    return exe


# ---- synthetic format builders (clean-room, zero game bytes) --------------------------------------------------------

def _chunk(tag: str, payload: bytes) -> bytes:
    return tag.encode("ascii") + struct.pack("<I", len(payload)) + payload


def _pack_aya(raw: bytes) -> bytes:
    out = bytearray()
    for start in range(0, len(raw), 1 << 20):
        comp = zlib.compress(raw[start:start + (1 << 20)], 9)
        out += struct.pack("<I", len(comp)) + comp
    return bytes(out)


def _unpack_aya(packed: bytes) -> bytes:
    out = bytearray()
    at = 0
    while at < len(packed):
        sz = struct.unpack_from("<I", packed, at)[0]
        at += 4
        out += zlib.decompress(packed[at:at + sz])
        at += sz
    return bytes(out)


def _both_u16(v: int) -> bytes:
    return struct.pack("<H", v) + struct.pack(">H", v)


def _both_u32(v: int) -> bytes:
    return struct.pack("<I", v) + struct.pack(">I", v)


def _pc_text_descriptor(name: str, frames: int = 1) -> bytes:
    # CheckLevelArchive: TEXT size == 360 + 12 * frames; DXTX wraps CTEX; name at payload+24; frames at payload+328.
    total = 360 + 12 * frames
    payload = bytearray(total)
    payload[0:4] = b"DXTX"
    struct.pack_into("<I", payload, 4, total - 8)
    payload[8:12] = b"CTEX"
    struct.pack_into("<I", payload, 12, 344)
    nb = name.encode("ascii") + b"\0"
    payload[24:24 + len(nb)] = nb
    struct.pack_into("<I", payload, 328, frames)
    for f in range(frames):
        off = 360 + 12 * f
        payload[off:off + 4] = b"TFRM"
        struct.pack_into("<II", payload, off + 4, 4, 1)
    return bytes(payload)


def _pc_mesh_descriptor(name: str, runtime_id: int) -> bytes:
    body = bytearray(309)
    nb = name.encode("ascii") + b"\0"
    body[0:len(nb)] = nb
    struct.pack_into("<ii", body, 300, runtime_id, 0)
    body[308] = 1
    pms2 = _chunk("PMS2", bytes(body))
    return _chunk("PMSH", pms2)


def _bbox_chunk() -> bytes:
    return _chunk("BBOX", _chunk("BBOX", b"\0" * 40))


def _cmsp_record(index: int, part_type: int = 1, children: int = 0, parent: bool = False,
                 vframes: int = 2, hframes: int = 2, grid: bool = False, cached: int = 2) -> bytes:
    b = bytearray(316)
    struct.pack_into("<I", b, 0x88, index)
    struct.pack_into("<I", b, 0x8C, part_type)
    struct.pack_into("<I", b, 0x90, children)
    struct.pack_into("<I", b, 0x98, 1 if parent else 0)
    struct.pack_into("<I", b, 0xB4, 1)          # frame-set count
    struct.pack_into("<I", b, 0xB8, vframes)
    struct.pack_into("<I", b, 0xBC, hframes)
    struct.pack_into("<I", b, 0xC8, 1)          # HPOS present
    struct.pack_into("<I", b, 0x100, 1 if grid else 0)
    struct.pack_into("<I", b, 0x104, 1)         # CPOS present (not static)
    struct.pack_into("<I", b, 0x108, 1)         # CORI present (not static)
    struct.pack_into("<I", b, 0x10C, 1)         # HORI present
    struct.pack_into("<I", b, 0x118, cached)
    return bytes(b)


def _collision_grid_chunk() -> bytes:
    cpbt = bytearray(184)
    struct.pack_into("<II", cpbt, 0x68, 1, 1)   # 1x1 cell grid
    sizs = struct.pack("<II", 1, 1)
    pbet = _chunk("PBET", _chunk("PBSQ", struct.pack("<IH", 1, 0)))
    pbcs = struct.pack("<I", 1) + pbet
    plqt = struct.pack("<I", 1)
    pbke = _chunk("PBKE", struct.pack("<IHHHxx", 0, 0, 1, 2))
    pbfk = _chunk("PBFK", pbke)
    pbpn = struct.pack("<I", 3)
    verts = b"".join(_chunk("PBPP", struct.pack("<hhh", i, i + 1, i + 2)) for i in range(3))
    body = _chunk("CPBT", bytes(cpbt)) + _chunk("SIZS", sizs) + _chunk("PBCS", pbcs) + _chunk("PLQT", plqt) + pbfk + _chunk("PBPN", pbpn) + verts
    return _chunk("PBKT", body)


def _loose_pc_cmsh(mesh_name: str, tex_names: list[str]) -> bytes:
    cmsh = bytearray(372)
    struct.pack_into("<I", cmsh, 4, len(tex_names))
    struct.pack_into("<I", cmsh, 0x15C, 1)      # 1 part
    nb = mesh_name.encode("ascii") + b"\0"
    cmsh[0x24:0x24 + len(nb)] = nb
    cmst = b"".join(struct.pack("<9I", 0, 0, 1, 0, 0, 0, 0, 0, 0) for _ in tex_names)
    mshts = b"".join(
        _chunk("MSHT", _chunk("TEXB", b"\0" * 20 + t.encode("ascii").ljust(128, b"\0")))
        for t in tex_names
    )
    # Simple rigid part with 1 triangle
    cmvb = bytearray(296)
    struct.pack_into("<I", cmvb, 8, 0x01000000)
    struct.pack_into("<iI", cmvb, 0x108, 1, 0xDEAD)
    struct.pack_into("<III", cmvb, 0x114, 36, 0x152, 4)
    vbuf = b"".join(struct.pack("<3f3fI2f", float(i), 0.0, 0.0, 0.0, 1.0, 0.0, 0xFFFFFFFF, 0.0, 0.0) for i in range(3))
    mmpt = struct.pack("<6I", len(vbuf), 6, 3, 3, 1, 1)
    ibuf = struct.pack("<3H", 0, 1, 2)
    texr = struct.pack("<6I", 0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF)
    pmvb = _chunk("CMVB", bytes(cmvb)) + _chunk("MMPT", mmpt) + _chunk("IBUF", ibuf) + _chunk("VBUF", vbuf) + _chunk("TEXR", texr)
    mesp_body = (
        _chunk("CMSP", _cmsp_record(0, vframes=1, hframes=1, cached=1))
        + _bbox_chunk()
        + _chunk("VHFM", b"\x00")
        + _chunk("HORI", b"\0" * 48)
        + _chunk("HPOS", b"\0" * 16)
        + _chunk("CPOS", b"\0" * 16)
        + _chunk("CORI", b"\0" * 48)
        + _chunk("PMVB", pmvb)
    )
    return _chunk("CMSH", bytes(cmsh)) + _chunk("CMST", cmst) + mshts + _chunk("MESP", mesp_body) + _bbox_chunk()


def _ps2_vertex_record(pos: tuple[float, float, float], normal: tuple[float, float, float],
                       uv: tuple[float, float], color: int, draws: bool) -> bytes:
    words = [0] * 28
    words[0], words[1], words[2] = (struct.unpack("<I", struct.pack("<f", v))[0] for v in normal)
    words[9], words[10] = (struct.unpack("<I", struct.pack("<f", v))[0] for v in uv)
    words[11] = color
    words[24], words[25], words[26] = (struct.unpack("<I", struct.pack("<f", v))[0] for v in pos)
    words[27] = 0 if draws else 0x8000
    return struct.pack("<28I", *words)


def _ps2_fenrir_pmsh(tex_indices: tuple[int, int] = (1, 2), bad_kicks: bool = False) -> bytes:
    # Build a 2-part PS2 CMSH stream with parent/child hierarchy, collision grid, CAMD, CEMT, and multi-run strips.
    cmsh = bytearray(372)
    struct.pack_into("<I", cmsh, 4, len(tex_indices))
    struct.pack_into("<I", cmsh, 0x14, 1)       # 1 anim mode
    struct.pack_into("<I", cmsh, 0x1C, 1)       # 1 emitter
    struct.pack_into("<I", cmsh, 0x15C, 2)      # 2 parts
    nb = b"fenrir-multiplayer.msh\0"
    cmsh[0x24:0x24 + len(nb)] = nb
    cmst = b"".join(struct.pack("<9I", 0, 0, 1, 0, 0, 0, 0, 0, 0) for _ in tex_indices)
    mshts = b"".join(_chunk("MSHT", _chunk("TEXB", b"\0" * 20 + struct.pack("<I", idx))) for idx in tex_indices)

    # Part 0 (root with child 1 and collision grid): 2 strip runs separated by non-drawing vertices to test parity stitching!
    v_coords = [
        (0.0, 0.0, 0.0),    # 0
        (1.0, 0.0, 0.0),    # 1
        (0.0, 1.0, 0.0),    # 2 (draws at i=2, even parity)
        (1.0, 1.0, 0.0),    # 3 (draws at i=3, odd parity)
        (5.0, 0.0, 0.0),    # 4 (no draw - start of second disconnected strip)
        (6.0, 0.0, 0.0),    # 5 (no draw)
        (5.0, 1.0, 0.0),    # 6 (no draw)
        (6.0, 1.0, 0.0),    # 7 (draws at i=7, odd parity -> tests extra degenerate insert!)
    ]
    draws0 = [False, False, True, True, False, False, False, True]
    vrts0 = b"".join(
        _ps2_vertex_record(p, (0.0, 0.0, 1.0), (0.25 * i, 0.5), 0xFFEEDDCC, d)
        for i, (p, d) in enumerate(zip(v_coords, draws0))
    )
    kicks0 = sum(draws0) + (1 if bad_kicks else 0)
    mmpv0 = (
        struct.pack("<I", 1)
        + _chunk("TEXR", struct.pack("<6I", 0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF))
        + _chunk("NVRT", struct.pack("<IIf", len(v_coords), kicks0, 1.0))
        + _chunk("VRTS", vrts0)
    )
    p2vb0 = _chunk("MSVB", struct.pack("<II", 1, 96)) + _chunk("MMPV", mmpv0)
    part0 = (
        _chunk("CMSP", _cmsp_record(0, children=1, parent=False, vframes=2, hframes=2, grid=True, cached=2))
        + _chunk("CHLD", struct.pack("<I", 1))
        + _bbox_chunk()
        + _chunk("VHFM", b"\x00\x01")
        + _chunk("HORI", b"\0" * 96)
        + _chunk("HPOS", b"\0" * 32)
        + _collision_grid_chunk()
        + _chunk("CPOS", b"\0" * 32)
        + _chunk("CORI", b"\0" * 96)
        + _chunk("P2VB", p2vb0)
    )

    # Part 1 (child of part 0, using texture slot 1)
    vrts1 = b"".join(
        _ps2_vertex_record((float(i), 2.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0), 0xFFFFFFFF, i >= 2)
        for i in range(3)
    )
    mmpv1 = (
        struct.pack("<I", 1)
        + _chunk("TEXR", struct.pack("<6I", 1, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF))
        + _chunk("NVRT", struct.pack("<IIf", 3, 1, 1.0))
        + _chunk("VRTS", vrts1)
    )
    p2vb1 = _chunk("MSVB", struct.pack("<II", 1, 96)) + _chunk("MMPV", mmpv1)
    part1 = (
        _chunk("CMSP", _cmsp_record(1, children=0, parent=True, vframes=2, hframes=2, grid=False, cached=2))
        + _chunk("PRNT", struct.pack("<I", 0))
        + _bbox_chunk()
        + _chunk("VHFM", b"\x00\x01")
        + _chunk("HORI", b"\0" * 96)
        + _chunk("HPOS", b"\0" * 32)
        + _chunk("CPOS", b"\0" * 32)
        + _chunk("CORI", b"\0" * 96)
        + _chunk("P2VB", p2vb1)
    )

    camd = bytearray(36)
    camd[0:5] = b"idle\0"
    struct.pack_into("<iiiI", camd, 0x14, 0, 1, 1, 0x3F000000)  # start=0, last=1, scaler=1, rate=0.5f

    cemt_rec = bytearray(336)
    struct.pack_into("<I", cemt_rec, 0x40, 1)                   # indexed emitter naming part 1
    cemt = struct.pack("<I", 336) + bytes(cemt_rec) + struct.pack("<I", 1)

    stream = (
        _chunk("CMSH", bytes(cmsh))
        + _chunk("CMST", cmst)
        + mshts
        + _chunk("MESP", part0)
        + _chunk("MESP", part1)
        + _chunk("CAMD", bytes(camd))
        + _bbox_chunk()
        + _chunk("CEMT", cemt)
    )
    prefix = b"fenrir-multiplayer.msh\0".ljust(304, b"\0")
    return _chunk("PMSH", prefix + stream)


def _worldheaders_bytes(extra_888_world_type: int = 2) -> tuple[bytes, dict[int, tuple[int, list[str], tuple[int, int, int, int]]]]:
    records_spec = [
        (857, ["Standard Forseti", "Standard Muspell"], (1, 0, 0, 2)),
        (888, ["Standard Forseti", "Standard Muspell"], (1, 0, 0, extra_888_world_type)),
    ]
    out = bytearray(struct.pack("<ii", 1, len(records_spec)))
    meta = {}
    for wid, cfgs, tr in records_spec:
        cfg_bytes = b"".join(bytes([len(s)]) + s.encode("ascii") for s in cfgs)
        psize = 24 + len(cfg_bytes)
        out += struct.pack("<iiii", 3, psize, wid, len(cfgs)) + cfg_bytes + struct.pack("<4i", *tr)
        meta[wid] = (psize, cfgs, tr)
    return bytes(out), meta


def _world_body_bytes(world_id: int, psize: int, cfgs: list[str], trailing: tuple[int, int, int, int],
                      units: tuple[str, ...] = ("Aircarrier Multiplayer", "Fenrir Multiplayer")) -> bytes:
    w = bytearray()
    w += struct.pack("<Hiii", 49, 3, psize, world_id)
    w += struct.pack("<i", len(cfgs))
    for c in cfgs:
        cb = c.encode("ascii")
        w += struct.pack("<b", len(cb)) + cb
    w += struct.pack("<4i", *trailing)
    w += struct.pack("<i", -1)          # baseLevel == -1
    # 1 minimal compiled script
    w += struct.pack("<i", 1)
    sname = b"world888script"
    w += struct.pack("<i", len(sname)) + sname
    w += struct.pack("<i", 1) + struct.pack("<ii", 23, 0)       # 1 instruction: OP_RETURN
    w += struct.pack("<13i", *([-1] * 13))                      # eventPCs
    w += struct.pack("<i", 0)                                   # 0 symbols
    w += struct.pack("<i", 0)                                   # nextSymbolIndex
    w += struct.pack("<i", 0)                                   # 0 eventFunctions
    w += struct.pack("<ii", 0, 0)                               # tail
    w += b"\0" * 10                                             # 10-byte script trailer
    w += struct.pack("<i", world_id)                            # mapNumber
    w += struct.pack("<i", 0)                                   # namedMeshes

    # Things: 2 start points + unit definitions
    total_things = 2 + len(units)
    w += struct.pack("<H", total_things)
    for player_num in (1, 2):
        w += struct.pack("<i", 15)                              # OID_START
        w += struct.pack("<6f3i", 100.0 * player_num, 200.0, -50.0, 0.0, 0.0, 0.0, 0, player_num - 1, -1)
        w += b"\0\0\0"                                          # script, name, spawnScript
        w += struct.pack("<ii", 1, 0)                           # active, attachScriptsToUnits (v49 > 45)
        w += struct.pack("<ii", 0, player_num)                  # planeMode, playerNumber
    for idx, udef in enumerate(units):
        w += struct.pack("<i", 8)                               # OID_UNIT
        w += struct.pack("<6f3i", 256.0, 100.0 + 200.0 * idx, -60.0, 0.0, 0.0, 0.0, 0, idx, -1)
        w += b"\0\0\0"
        w += struct.pack("<ii", 1, 0)
        ub = udef.encode("ascii")
        w += struct.pack("<b", len(ub)) + ub + struct.pack("<i", -1)

    w += struct.pack("<h", 0)                                   # treeGroups
    w += struct.pack("<i", 0)                                   # treeTypes
    w += struct.pack("<Hii", 1, 0, 0)                           # influenceMap v1, 0 nodes, 0 links
    w += struct.pack("<h", 1)                                   # 1 waypoint path
    wp_name = b"patrol"
    w += struct.pack("<b", len(wp_name)) + wp_name + struct.pack("<ih", 1, 0)
    w += struct.pack("<5f2i4f", 0.0, 0.0, 0.0, 1.0, 1.0, 100, 50, 600.0, 300.0, 300.0, 50.0)
    for _ in range(3):
        w += struct.pack("<i", 0)                               # occupancy grid v0 (0 bytes)
    w += struct.pack("<5i", 0, 0, 0, 0, 0)                      # v49 < 50 legacyGridWords
    w += struct.pack("<f", 0.0)                                 # v49 levelStartTime
    return bytes(w)


def _sshd_empty_payload() -> bytes:
    return _chunk("SHDS", b"") + _chunk("SHDC", struct.pack("<i", 0)) + _chunk("SHDL", b"")


def _sshd_one_shadow_payload() -> bytes:
    entry = _chunk("SHDE", struct.pack("<6i", 0, 0, 4, 4, 0, 0))
    return _chunk("SHDS", b"") + _chunk("SHDC", struct.pack("<i", 1)) + _chunk("SHDL", b"") + entry


def _physics_bytes() -> bytes:
    def stmt(kind: int, name: str, nodes: list[tuple[int, bytes]]) -> bytes:
        body = bytearray()
        for idx, (field, val) in enumerate(nodes):
            link = -1 if idx == len(nodes) - 1 else 0
            body += struct.pack("<II", field, len(val)) + val + struct.pack("<i", link)
        nb = name.encode("ascii") + b"\0"
        return struct.pack("<II", kind, len(nb) + len(body)) + nb + bytes(body)

    cstr = lambda s: s.encode("ascii") + b"\0"
    raw = bytearray(struct.pack("<H", 0x12))
    raw += stmt(7, "Deck Turret", [(3, cstr("turret.msh"))])
    raw += stmt(5, "Deck Wing", [(1, cstr("Fighter"))])
    raw += stmt(1, "Fighter", [(9, cstr("fighter.msh"))])
    raw += stmt(1, "Aircarrier Base", [(9, cstr("venturer.msh")), (27, cstr("Deck Turret")), (18, cstr("Deck Wing"))])
    raw += stmt(1, "Aircarrier Multiplayer", [(39, cstr("Aircarrier Base"))])
    raw += stmt(1, "Fenrir Multiplayer", [(9, cstr("fenrir-multiplayer.msh"))])
    raw += struct.pack("<i", -1)
    return bytes(raw)


def _make_pc_game_dir(root: Path, wh_888_type: int = 2) -> dict:
    (root / "data/resources/meshes").mkdir(parents=True, exist_ok=True)
    wh_bytes, wh_meta = _worldheaders_bytes(wh_888_type)
    (root / "data/worldheaders.dat").write_bytes(wh_bytes)
    (root / "data/default physics.dat").write_bytes(_physics_bytes())

    for mname, texs in (("venturer.msh", ["venturer.tga"]), ("turret.msh", ["turret.tga"]), ("fighter.msh", ["fighter.tga"])):
        (root / f"data/resources/meshes/m_{mname}.aya").write_bytes(_pack_aya(_loose_pc_cmsh(mname, texs)))

    # Donor 857 archive
    psize857, cfgs857, tr857 = wh_meta[857]
    wbody857 = _world_body_bytes(857, psize857, cfgs857, tr857, units=("Fighter",))
    wres857 = _chunk("WRLD", _chunk("WDAT", struct.pack("<ii", -1, 857)) + _chunk("RLWD", wbody857))
    guards = struct.pack("<6I", 344, 372, 316, 5084, 92, 0)
    donor_raw = (
        _chunk("LVLR", struct.pack("<I", 103))
        + _chunk("TARG", struct.pack("<I", 1))
        + _chunk("AYAD", guards)
        + _chunk("TEXT", _pc_text_descriptor("sea.tga"))
        + _chunk("TEXT", _pc_text_descriptor("venturer.tga"))
        + _chunk("MESH", _pc_mesh_descriptor("venturer.msh", 5))
        + _chunk("ERES", b"terrain")
        + _chunk("SURF", b"")
        + _chunk("SSHD", _sshd_one_shadow_payload())
        + _chunk("WRES", wres857)
    )
    (root / "data/resources/857_res_PC.aya").write_bytes(_pack_aya(donor_raw))

    # Source archives 731, 732, 741, 742 holding the missing flagship/turret/fighter descriptors
    source_defs = {
        731: ([("turret.tga", 1), ("fenrir_hull.tga", 1)], [("turret.msh", 2)]),
        732: ([("fenrir_trim.tga", 1)], []),
        741: ([("fighter.tga", 1)], [("fighter.msh", 3)]),
        742: ([("unused.tga", 1)], []),
    }
    for lvl, (tex_list, mesh_list) in source_defs.items():
        sraw = (
            _chunk("LVLR", struct.pack("<I", 103))
            + _chunk("TARG", struct.pack("<I", 1))
            + _chunk("AYAD", guards)
            + b"".join(_chunk("TEXT", _pc_text_descriptor(t, f)) for t, f in tex_list)
            + b"".join(_chunk("MESH", _pc_mesh_descriptor(m, mid)) for m, mid in mesh_list)
        )
        (root / f"data/resources/{lvl}_res_PC.aya").write_bytes(_pack_aya(sraw))
    return wh_meta


def _ps2_text_chunk(name: str) -> bytes:
    inner = bytearray(8 + 160)
    nb = name.encode("ascii") + b"\0"
    inner[8:8 + len(nb)] = nb
    return _chunk("TEXT", _chunk("P2TX", bytes(inner)))


def _make_ps2_888_member(wh_meta: dict, *, include_fenrir: bool = True, wdat_level: int = 888,
                         tex_indices: tuple[int, int] = (1, 2), bad_kicks: bool = False) -> bytes:
    psize888, cfgs888, tr888 = wh_meta[888]
    wbody888 = _world_body_bytes(888, psize888, cfgs888, tr888)
    wres = _chunk("WRLD", _chunk("WDAT", struct.pack("<ii", -1, wdat_level)) + _chunk("RLWD", wbody888))
    chunks = [
        _chunk("LVLR", struct.pack("<I", 103)),
        _ps2_text_chunk("sea.tga"),
        _ps2_text_chunk("fenrir_hull.tga"),
        _ps2_text_chunk("fenrir_trim.tga"),
    ]
    if include_fenrir:
        chunks.append(_chunk("MESH", _ps2_fenrir_pmsh(tex_indices=tex_indices, bad_kicks=bad_kicks)))
    chunks.append(_chunk("SSHD", _sshd_empty_payload()))
    chunks.append(_chunk("WRES", wres))
    return b"".join(chunks)


def _make_rcdf_package(rows_spec: list[tuple[str, bytes]], *, magic: bytes = b"RCDF", version: int = 100,
                       overlap: bool = False, bad_offset: bool = False) -> bytes:
    header = bytearray(2048)
    header[0:4] = magic
    struct.pack_into("<I", header, 4, version)
    body = bytearray()
    index_rows = bytearray()
    cur_off = 2048
    for idx, (path, data) in enumerate(rows_spec):
        rec_off = (cur_off - 4) if (overlap and idx == 1) else (100 if bad_offset else cur_off)
        raw_path = path.encode("ascii") + b"\0"
        row = raw_path.ljust(256, b"\0") + struct.pack("<II", rec_off, len(data))
        index_rows += row
        body += data
        cur_off += len(data)
    index_offset = 2048 + len(body)
    struct.pack_into("<I", header, 8, index_offset)
    index = struct.pack("<I", len(rows_spec)) + bytes(index_rows)
    return bytes(header) + bytes(body) + index


def _iso_dir_record(name: bytes, extent: int, size: int, flags: int) -> bytes:
    rec_len = 33 + len(name)
    if rec_len & 1:
        rec_len += 1
    rec = bytearray(rec_len)
    rec[0] = rec_len
    rec[2:10] = _both_u32(extent)
    rec[10:18] = _both_u32(size)
    rec[25] = flags
    rec[32] = len(name)
    rec[33:33 + len(name)] = name
    return bytes(rec)


def _make_iso_image(pkg_bytes: bytes | None, *, pvd_magic: bytes = b"CD001") -> bytes:
    sector = 2048
    root_sector = 18
    pkg_sector = 19
    entries = bytearray()
    entries += _iso_dir_record(b"\x00", root_sector, sector, 0x02)
    entries += _iso_dir_record(b"\x01", root_sector, sector, 0x02)
    if pkg_bytes is not None:
        entries += _iso_dir_record(b"DATA0.NYO;1", pkg_sector, len(pkg_bytes), 0x00)
    root_dir = bytes(entries).ljust(sector, b"\0")

    pkg_sectors = ((len(pkg_bytes) + sector - 1) // sector) if pkg_bytes is not None else 1
    total_sectors = pkg_sector + max(1, pkg_sectors)

    img = bytearray(total_sectors * sector)
    # Sector 16: Primary Volume Descriptor
    pvd = bytearray(sector)
    pvd[0] = 1
    pvd[1:6] = pvd_magic
    pvd[6] = 1
    pvd[80:88] = _both_u32(total_sectors)
    pvd[128:132] = _both_u16(sector)
    root_rec = _iso_dir_record(b"\x00", root_sector, sector, 0x02)
    pvd[156:156 + 34] = root_rec[:34]
    img[16 * sector:17 * sector] = pvd

    # Sector 17: Volume Descriptor Set Terminator
    term = bytearray(sector)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1
    img[17 * sector:18 * sector] = term

    # Sector 18: Root directory
    img[root_sector * sector:(root_sector + 1) * sector] = root_dir

    # Sector 19+: DATA0.NYO
    if pkg_bytes is not None:
        img[pkg_sector * sector:pkg_sector * sector + len(pkg_bytes)] = pkg_bytes
    return bytes(img)


def _make_custom_zip(member_name: str, iso_bytes: bytes, *, method: int = 8, zip64: bool = False,
                     encrypted: bool = False, extra_second: bool = False, corrupt_crc: bool = False,
                     corrupt_deflate: bool = False) -> bytes:
    crc = (zlib.crc32(iso_bytes) & 0xFFFFFFFF) ^ (1 if corrupt_crc else 0)
    if method == 0:
        comp = iso_bytes
    else:
        cobj = zlib.compressobj(6, zlib.DEFLATED, -zlib.MAX_WBITS)
        comp = cobj.compress(iso_bytes) + cobj.flush()
        if corrupt_deflate:
            comp = b"\xff\xff\xff\xff" + comp[4:]

    flags = 0x0001 if encrypted else 0
    nb = member_name.encode("ascii")
    lfh = struct.pack("<IHHHHHIIIHH", 0x04034B50, 45 if zip64 else 20, flags, method, 0, 0,
                      crc, len(comp), len(iso_bytes), len(nb), 0) + nb
    data_blob = lfh + comp

    cd_entries = bytearray()
    entries_count = 1
    if zip64:
        extra64 = struct.pack("<HHQQQ", 0x0001, 24, len(iso_bytes), len(comp), 0)
        cd1 = struct.pack(
            "<IHHHHHHIIIHHHHHII",
            0x02014B50, 45, 45, flags, method, 0, 0, crc,
            0xFFFFFFFF, 0xFFFFFFFF, len(nb), len(extra64), 0, 0, 0, 0, 0xFFFFFFFF,
        ) + nb + extra64
    else:
        cd1 = struct.pack(
            "<IHHHHHHIIIHHHHHII",
            0x02014B50, 20, 20, flags, method, 0, 0, crc,
            len(comp), len(iso_bytes), len(nb), 0, 0, 0, 0, 0, 0,
        ) + nb
    cd_entries += cd1

    if extra_second:
        entries_count = 2
        nb2 = b"README.TXT"
        cd_entries += struct.pack(
            "<IHHHHHHIIIHHHHHII",
            0x02014B50, 20, 20, 0, 0, 0, 0, 0, 0, 0, len(nb2), 0, 0, 0, 0, 0, len(data_blob),
        ) + nb2

    cd_offset = len(data_blob)
    cd_size = len(cd_entries)

    if zip64:
        z64_off = cd_offset + cd_size
        z64_eocd = struct.pack(
            "<IQHHIIQQQQ",
            0x06064B50, 44, 45, 45, 0, 0, entries_count, entries_count, cd_size, cd_offset,
        )
        z64_loc = struct.pack("<IIQI", 0x07064B50, 0, z64_off, 1)
        eocd = struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 0xFFFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0)
        return data_blob + bytes(cd_entries) + z64_eocd + z64_loc + eocd

    eocd = struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, entries_count, entries_count, cd_size, cd_offset, 0)
    return data_blob + bytes(cd_entries) + eocd


def _oriented_triangles_from_pc_cmsh(stream: bytes) -> list[collections.Counter]:
    """Decode each MESP's PMVB vertex pool & index strip into canonical oriented triangle multisets."""
    at = 0
    part_tris = []
    while at < len(stream):
        tag = stream[at:at + 4].decode("ascii")
        sz = struct.unpack_from("<I", stream, at + 4)[0]
        payload = stream[at + 8:at + 8 + sz]
        at += 8 + sz
        if tag != "MESP":
            continue
        pat = 0
        while pat < len(payload):
            ptag = payload[pat:pat + 4].decode("ascii")
            psz = struct.unpack_from("<I", payload, pat + 4)[0]
            ppay = payload[pat + 8:pat + 8 + psz]
            pat += 8 + psz
            if ptag != "PMVB":
                continue
            vat = 0
            pool: list[tuple] = []
            strips: list[tuple[int, ...]] = []
            while vat < len(ppay):
                vtag = ppay[vat:vat + 4].decode("ascii")
                vsz = struct.unpack_from("<I", ppay, vat + 4)[0]
                vpay = ppay[vat + 8:vat + 8 + vsz]
                vat += 8 + vsz
                if vtag == "VBUF" and vpay and not pool:
                    for off in range(0, len(vpay), 36):
                        pool.append(struct.unpack_from("<3f3fI2f", vpay, off))
                elif vtag == "IBUF":
                    strips.append(struct.unpack(f"<{len(vpay) // 2}H", vpay))
            for st in strips:
                cnt: collections.Counter = collections.Counter()
                for i in range(len(st) - 2):
                    a, b, c = pool[st[i]], pool[st[i + 1]], pool[st[i + 2]]
                    if len({a, b, c}) < 3:
                        continue
                    if i & 1:
                        b, c = c, b
                    tri = (a, b, c)
                    m = min(range(3), key=lambda k: tri[k])
                    cnt[tri[m:] + tri[:m]] += 1
                part_tris.append(cnt)
    return part_tris


# ---- test suite ------------------------------------------------------------------------------------------------------

class CePs2ImportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.exe = _build_driver()

    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="ce-ps2-case-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.game = self.tmp / "game"
        self.user = self.tmp / "user"
        self.ext = self.tmp / "external"
        self.user.mkdir()
        self.ext.mkdir()
        self.wh_meta = _make_pc_game_dir(self.game)
        self.member_bytes = _make_ps2_888_member(self.wh_meta)
        self.pkg_bytes = _make_rcdf_package([
            ("\\data\\Resources\\100_res_PS2.aya", b"LVLR" + struct.pack("<I", 103)),
            ("\\data\\Resources\\888_res_PS2.aya", self.member_bytes),
        ])
        self.iso_bytes = _make_iso_image(self.pkg_bytes)

    def _run_driver(self, args: list[str], *, game: Path | None = None, user: Path | None = None) -> str:
        env = dict(
            os.environ,
            BEA_GAME_DIR=str(game or self.game),
            BEA_USER_DIR=str(user or self.user),
            TMPDIR=str(self.tmp),
            ASAN_OPTIONS="detect_leaks=0:abort_on_error=1",
            UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
        )
        done = subprocess.run([str(self.exe), *args], env=env, capture_output=True, text=True, timeout=60, preexec_fn=_no_core)
        self.assertEqual(done.returncode, 0, f"driver failed ({args}):\nstdout={done.stdout}\nstderr={done.stderr}")
        lines = [ln for ln in done.stdout.splitlines() if ln.startswith(("OK ", "ERR ", "CANCELLED ", "REPORT "))]
        self.assertTrue(lines, f"unexpected driver output:\n{done.stdout}\n{done.stderr}")
        return lines[-1]

    def test_plain_iso_and_sparse_over_4gib_extract_convert_stage_and_pass_mods_loader(self) -> None:
        iso_path = self.ext / "disc.iso"
        iso_path.write_bytes(self.iso_bytes)

        out = self._run_driver(["sync", str(iso_path)])
        self.assertEqual(out, "OK enabled=1 line=flagship-duel-ps2")

        mod_dir = self.user / "mods/flagship-duel-ps2"
        manifest = json.loads((mod_dir / "mod.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["schema"], 1)
        self.assertEqual(manifest["id"], "flagship-duel-ps2")
        self.assertEqual(manifest["name"], "Flagship Duel (PS2 Level 888)")
        self.assertEqual(len(manifest["files"]), 1)

        aya_path = mod_dir / "data/resources/888_res_PC.aya"
        aya_bytes = aya_path.read_bytes()
        aya_sha = hashlib.sha256(aya_bytes).hexdigest()
        self.assertEqual(manifest["files"][0], {
            "path": "data/resources/888_res_PC.aya",
            "size": len(aya_bytes),
            "sha256": aya_sha,
        })

        # Verify CE's actual mods.cpp loader accepts the staged mod with 0 refusals!
        rep = self._run_driver(["report"])
        self.assertIn("found=1 id=flagship-duel-ps2 why=0 place=1 replaced=0 worlds=0 archives=1", rep)
        self.assertIn("check_rc=0", rep)

        # Inspect unpacked archive structure, descriptor runtime IDs, SSHD replacement, and triangle winding!
        raw = _unpack_aya(aya_bytes)
        at = 0
        mesh_ids = []
        inline_cmsh = None
        sshd_payload = None
        while at < len(raw):
            tag = raw[at:at + 4].decode("ascii")
            sz = struct.unpack_from("<I", raw, at + 4)[0]
            pay = raw[at + 8:at + 8 + sz]
            at += 8 + sz
            if tag == "MESH":
                rid = struct.unpack_from("<i", pay, 316)[0]
                mesh_ids.append(rid)
                if pay[16] == 0:
                    self.assertEqual(rid, 20000)
                    self.assertEqual(pay[324], 1)
                    inline_cmsh = pay[325:]
            elif tag == "SSHD":
                sshd_payload = pay

        self.assertEqual(mesh_ids, [5, 6, 7, 20000])
        self.assertEqual(sshd_payload, _sshd_empty_payload())
        self.assertIsNotNone(inline_cmsh)

        # Check oriented triangles of part 0 (3 triangles across 2 disconnected strip runs, even & odd parity preserved!)
        part_tris = _oriented_triangles_from_pc_cmsh(inline_cmsh)
        self.assertEqual(len(part_tris), 2)
        self.assertEqual(sum(part_tris[0].values()), 3)
        self.assertEqual(sum(part_tris[1].values()), 1)

        # Verify 64-bit offsets > 4 GiB: sparse dual-layer ISO with DATA0.NYO located past 4 GiB (sector 2,200,000)!
        sparse_iso = self.ext / "dual_layer_sparse.iso"
        sector = 2048
        root_sec = 18
        high_pkg_sec = 2_200_000        # 4,505,600,000 bytes (> 2^32)
        total_secs = 2_441_408          # 5,000,003,584 bytes (exact multiple of 2048, <= 9 GB)
        pvd = bytearray(sector)
        pvd[0], pvd[1:6], pvd[6] = 1, b"CD001", 1
        pvd[80:88] = _both_u32(total_secs)
        pvd[128:132] = _both_u16(sector)
        pvd[156:190] = _iso_dir_record(b"\x00", root_sec, sector, 0x02)[:34]
        term = bytearray(sector)
        term[0], term[1:6], term[6] = 255, b"CD001", 1
        rdir = (
            _iso_dir_record(b"\x00", root_sec, sector, 0x02)
            + _iso_dir_record(b"\x01", root_sec, sector, 0x02)
            + _iso_dir_record(b"DATA0.NYO;1", high_pkg_sec, len(self.pkg_bytes), 0x00)
        ).ljust(sector, b"\0")
        fd = os.open(sparse_iso, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o600)
        try:
            os.ftruncate(fd, total_secs * sector)
            os.pwrite(fd, bytes(pvd), 16 * sector)
            os.pwrite(fd, bytes(term), 17 * sector)
            os.pwrite(fd, rdir, root_sec * sector)
            os.pwrite(fd, self.pkg_bytes, high_pkg_sec * sector)
        finally:
            os.close(fd)
        user2 = self.tmp / "user_sparse"
        user2.mkdir()
        out_sparse = self._run_driver(["sync", str(sparse_iso)], user=user2)
        self.assertEqual(out_sparse, "OK enabled=1 line=flagship-duel-ps2")
        self.assertEqual(
            hashlib.sha256((user2 / "mods/flagship-duel-ps2/data/resources/888_res_PC.aya").read_bytes()).hexdigest(),
            aya_sha,
        )

    def test_streamed_redump_zip_stored_deflated_and_zip64_never_writes_temp_iso(self) -> None:
        iso_path = self.ext / "ref.iso"
        iso_path.write_bytes(self.iso_bytes)
        self._run_driver(["sync", str(iso_path)])
        expected_sha = hashlib.sha256(
            (self.user / "mods/flagship-duel-ps2/data/resources/888_res_PC.aya").read_bytes()
        ).hexdigest()
        iso_path.unlink()

        variants = [
            ("stored.zip", _make_custom_zip("Battle Engine Aquila (USA).iso", self.iso_bytes, method=0, zip64=False)),
            ("deflated.zip", _make_custom_zip("Battle Engine Aquila (Europe).ISO", self.iso_bytes, method=8, zip64=False)),
            ("zip64.zip", _make_custom_zip("Battle Engine Aquila (Redump).iso", self.iso_bytes, method=8, zip64=True)),
        ]
        for fname, blob in variants:
            with self.subTest(zip_variant=fname):
                zpath = self.ext / fname
                zpath.write_bytes(blob)
                u_sub = self.tmp / f"u_{fname}"
                u_sub.mkdir()
                ans = self._run_driver(["sync", str(zpath)], user=u_sub)
                self.assertEqual(ans, "OK enabled=1 line=flagship-duel-ps2")
                got_sha = hashlib.sha256(
                    (u_sub / "mods/flagship-duel-ps2/data/resources/888_res_PC.aya").read_bytes()
                ).hexdigest()
                self.assertEqual(got_sha, expected_sha)
                # Ensure no temporary .iso file was ever extracted to disk
                leaked_isos = list(self.tmp.rglob("*.iso")) + list(self.tmp.rglob("*.ISO"))
                self.assertEqual(leaked_isos, [])

    def test_direct_lvlr_aya_async_worker_cancel_replace_and_mod_cap_preservation(self) -> None:
        aya_src = self.ext / "888_res_PS2.aya"
        aya_src.write_bytes(self.member_bytes)

        # Pre-populate ce.ini with an existing enabled mod and an older copy of flagship-duel-ps2
        (self.user / "ce.ini").write_text("mods=existing-mod\n", encoding="utf-8")
        stale = self.user / "mods/flagship-duel-ps2"
        stale.mkdir(parents=True)
        (stale / "stale.txt").write_text("old", encoding="utf-8")

        # Cancel & restart check
        cancelled = self._run_driver(["cancel", str(aya_src)])
        self.assertEqual(cancelled, "CANCELLED state=0")

        # Async worker run
        ans = self._run_driver(["async", str(aya_src)])
        self.assertTrue(ans.startswith("OK enabled=1 "), ans)
        self.assertTrue(ans.endswith("line=existing-mod,flagship-duel-ps2"), ans)
        self.assertFalse((stale / "stale.txt").exists())
        self.assertTrue((stale / "mod.json").is_file())
        self.assertTrue((stale / "data/resources/888_res_PC.aya").is_file())
        self.assertEqual(list((self.user / "mods").glob(".stage-*")), [])
        self.assertEqual(list((self.user / "mods").glob(".old-*")), [])

        # When 16 other mods are already enabled, existing 16 stay intact and enabled=0 is reported cleanly
        user16 = self.tmp / "user16"
        user16.mkdir()
        sixteen = ",".join(f"mod-{i:02d}" for i in range(16))
        (user16 / "ce.ini").write_text(f"mods={sixteen}\n", encoding="utf-8")
        ans16 = self._run_driver(["async", str(aya_src)], user=user16)
        self.assertTrue(ans16.startswith("OK enabled=0 "), ans16)
        self.assertTrue(ans16.endswith(f"line={sixteen}"), ans16)
        self.assertTrue((user16 / "mods/flagship-duel-ps2/mod.json").is_file())

    def test_adversarial_inputs_refuse_cleanly_with_plain_english_and_zero_leftovers(self) -> None:
        # Prepare adversarial files
        chd = self.ext / "disc.chd"
        chd.write_bytes(b"MComprHD" + b"\0" * 64)

        trunc_iso = self.ext / "trunc.iso"
        trunc_iso.write_bytes(b"\0" * 5000)

        bad_pvd = self.ext / "bad_pvd.iso"
        bad_pvd.write_bytes(_make_iso_image(self.pkg_bytes, pvd_magic=b"XXXXX"))

        no_pkg = self.ext / "no_pkg.iso"
        no_pkg.write_bytes(_make_iso_image(None))

        bad_rcdf_magic = self.ext / "bad_rcdf_magic.iso"
        bad_rcdf_magic.write_bytes(_make_iso_image(_make_rcdf_package([("\\a", b"LVLR0000")], magic=b"XXXX")))

        bad_rcdf_ver = self.ext / "bad_rcdf_ver.iso"
        bad_rcdf_ver.write_bytes(_make_iso_image(_make_rcdf_package([("\\a", b"LVLR0000")], version=99)))

        overlap_rcdf = self.ext / "overlap.iso"
        overlap_rcdf.write_bytes(_make_iso_image(_make_rcdf_package(
            [("\\a", b"LVLR0000"), ("\\data\\Resources\\888_res_PS2.aya", self.member_bytes)], overlap=True
        )))

        oob_rcdf = self.ext / "oob.iso"
        oob_rcdf.write_bytes(_make_iso_image(_make_rcdf_package(
            [("\\data\\Resources\\888_res_PS2.aya", self.member_bytes)], bad_offset=True
        )))

        missing_888 = self.ext / "no888.iso"
        missing_888.write_bytes(_make_iso_image(_make_rcdf_package([("\\data\\Resources\\100_res_PS2.aya", b"LVLR0000")])))

        multi_zip = self.ext / "multi.zip"
        multi_zip.write_bytes(_make_custom_zip("disc.iso", self.iso_bytes, extra_second=True))

        enc_zip = self.ext / "enc.zip"
        enc_zip.write_bytes(_make_custom_zip("disc.iso", self.iso_bytes, encrypted=True))

        corrupt_zip = self.ext / "corrupt.zip"
        corrupt_zip.write_bytes(_make_custom_zip("disc.iso", self.iso_bytes, corrupt_crc=True))

        # Large trailing ISO (>5 MiB past RCDF index) with bad ZIP CRC-32 must still drain to EOF and fail CRC!
        large_tail_iso = self.iso_bytes + b"\x00" * (5 * 1024 * 1024)
        corrupt_large_tail_zip = self.ext / "corrupt_large_tail.zip"
        corrupt_large_tail_zip.write_bytes(_make_custom_zip("disc.iso", large_tail_iso, corrupt_crc=True))

        corrupt_deflate_zip = self.ext / "corrupt_deflate.zip"
        corrupt_deflate_zip.write_bytes(_make_custom_zip("disc.iso", self.iso_bytes, corrupt_deflate=True))

        non_iso_zip = self.ext / "readme.zip"
        non_iso_zip.write_bytes(_make_custom_zip("notes.bin", self.iso_bytes))

        no_fenrir = self.ext / "no_fenrir.aya"
        no_fenrir.write_bytes(_make_ps2_888_member(self.wh_meta, include_fenrir=False))

        bad_kicks = self.ext / "bad_kicks.aya"
        bad_kicks.write_bytes(_make_ps2_888_member(self.wh_meta, bad_kicks=True))

        bad_tex_idx = self.ext / "bad_tex.aya"
        bad_tex_idx.write_bytes(_make_ps2_888_member(self.wh_meta, tex_indices=(1, 99)))

        wrong_wdat = self.ext / "wrong_wdat.aya"
        wrong_wdat.write_bytes(_make_ps2_888_member(self.wh_meta, wdat_level=857))

        symlink_src = self.ext / "link.aya"
        valid_aya = self.ext / "valid.aya"
        valid_aya.write_bytes(self.member_bytes)
        symlink_src.symlink_to(valid_aya)

        inside_game = self.game / "inside.aya"
        inside_game.write_bytes(self.member_bytes)

        cases = [
            ("missing", self.ext / "does_not_exist.iso", "normal file"),
            ("directory", self.ext, "normal file"),
            ("symlink", symlink_src, "normal file"),
            ("inside_game", inside_game, "outside your game"),
            ("chd", chd, "CHD"),
            ("trunc_iso", trunc_iso, "ISO-9660"),
            ("bad_pvd", bad_pvd, "ISO-9660"),
            ("no_pkg", no_pkg, "DATA0.NYO"),
            ("bad_rcdf_magic", bad_rcdf_magic, "DATA0.NYO"),
            ("bad_rcdf_ver", bad_rcdf_ver, "DATA0.NYO"),
            ("overlap_rcdf", overlap_rcdf, "DATA0.NYO"),
            ("oob_rcdf", oob_rcdf, "DATA0.NYO"),
            ("missing_888", missing_888, "Level 888 was not found"),
            ("multi_zip", multi_zip, "single PS2 .iso"),
            ("enc_zip", enc_zip, "Encrypted"),
            ("corrupt_zip", corrupt_zip, "zip archive is damaged"),
            ("corrupt_large_tail_zip", corrupt_large_tail_zip, "zip archive is damaged"),
            ("corrupt_deflate_zip", corrupt_deflate_zip, "zip archive is damaged"),
            ("non_iso_zip", non_iso_zip, "single PS2 .iso"),
            ("no_fenrir", no_fenrir, "Fenrir mesh"),
            ("bad_kicks", bad_kicks, "Fenrir mesh"),
            ("bad_tex_idx", bad_tex_idx, "Fenrir mesh"),
            ("wrong_wdat", wrong_wdat, "world or shadow"),
        ]

        for label, path, substr in cases:
            with self.subTest(adversarial=label):
                u_case = self.tmp / f"u_bad_{label}"
                u_case.mkdir()
                ans = self._run_driver(["sync", str(path)], user=u_case)
                self.assertTrue(ans.startswith("ERR "), f"{label}: {ans}")
                msg = ans[4:]
                self.assertIn(substr, msg, f"{label}: {msg}")
                self.assertTrue(msg.endswith("."), msg)
                self.assertFalse(set(msg) & NOT_IN_FACE, msg)
                self.assertLessEqual(len(msg), 110, msg)
                # Zero partial files or directories in mods/
                mods_dir = u_case / "mods"
                if mods_dir.exists():
                    self.assertEqual(list(mods_dir.iterdir()), [], f"{label} left files in mods/")

        # Also test worldheaders mismatch and pre-existing 888_res_PC.aya in game_dir
        bad_game = self.tmp / "bad_wh_game"
        _make_pc_game_dir(bad_game, wh_888_type=0)
        u_wh = self.tmp / "u_wh"
        u_wh.mkdir()
        ans_wh = self._run_driver(["sync", str(valid_aya)], game=bad_game, user=u_wh)
        self.assertIn("ERR ", ans_wh)
        self.assertIn("world or shadow", ans_wh)

        already_game = self.tmp / "already_game"
        _make_pc_game_dir(already_game)
        (already_game / "data/resources/888_res_PC.aya").write_bytes(b"existing")
        u_al = self.tmp / "u_al"
        u_al.mkdir()
        ans_al = self._run_driver(["sync", str(valid_aya)], game=already_game, user=u_al)
        self.assertIn("already contains a Level 888 archive", ans_al)

        # Reimport over an unmodified prior import leaves zero stale backup folders;
        # reimport over a folder with custom user files preserves the prior folder as a recoverable backup!
        u_reimp = self.tmp / "u_reimport_backup"
        u_reimp.mkdir()
        self.assertEqual(self._run_driver(["sync", str(valid_aya)], user=u_reimp), "OK enabled=1 line=flagship-duel-ps2")
        self.assertEqual(self._run_driver(["sync", str(valid_aya)], user=u_reimp), "OK enabled=1 line=flagship-duel-ps2")
        self.assertEqual([p.name for p in u_reimp.iterdir() if p.name.startswith("flagship-duel-ps2-backup-")], [])
        self.assertEqual([p.name for p in (u_reimp / "mods").iterdir() if p.name.startswith(".old-")], [])

        custom_note = u_reimp / "mods/flagship-duel-ps2/my_custom_notes.txt"
        custom_note.write_text("keep my custom mod work\n", encoding="utf-8")
        self.assertEqual(self._run_driver(["sync", str(valid_aya)], user=u_reimp), "OK enabled=1 line=flagship-duel-ps2")
        self.assertFalse(custom_note.exists())
        backups = [p for p in u_reimp.iterdir() if p.name.startswith("flagship-duel-ps2-backup-")]
        self.assertEqual(len(backups), 1, f"expected 1 preserved backup dir, got {backups}")
        self.assertEqual((backups[0] / "my_custom_notes.txt").read_text(encoding="utf-8"), "keep my custom mod work\n")
        self.assertEqual([p.name for p in (u_reimp / "mods").iterdir() if p.name != "flagship-duel-ps2"], [])

    def test_windows_i686_mingw_cross_compile(self) -> None:
        mingw_cxx = deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang++"
        sdl_inc = deps.PREFIX_WIN / "include"
        if not mingw_cxx.is_file() or not (sdl_inc / "SDL3/SDL.h").is_file():
            self.skipTest("llvm-mingw or Windows SDL3 headers not present in BEA_PORT_DATA")

        out_obj = self.tmp / "ce_ps2_import_win.o"
        cmd = [
            str(mingw_cxx),
            "-std=gnu++17", "-DNOMINMAX", "-fms-extensions", "-fno-autolink",
            "-fno-strict-aliasing", "-fwrapv", "-mms-bitfields", "-fshort-wchar",
            "-O2", "-Wall", "-Wextra", "-Werror",
            '-DCE_VERSION="0.2.0"',
            "-I", str(PORT / "include"),
            "-I", str(PORT / "include/windows"),
            "-I", str(sdl_inc),
            "-c", str(PS2_CPP),
            "-o", str(out_obj),
        ]
        done = subprocess.run(cmd, capture_output=True, text=True, timeout=120, preexec_fn=_no_core)
        self.assertEqual(done.returncode, 0, f"Windows i686 cross-compile failed:\n{done.stdout}\n{done.stderr}")
        self.assertTrue(out_obj.is_file() and out_obj.stat().st_size > 0)

    def test_spdx_and_public_export_hygiene(self) -> None:
        owned = (
            "port/platform/common/ce_ps2_import.h",
            "port/platform/common/ce_ps2_import.cpp",
            "port/tools/tests/ce_ps2_import_driver.cpp",
            "port/tools/ce_ps2_import_tests.py",
        )
        for rel in owned:
            raw = (ROOT / rel).read_bytes()
            head = "\n".join(raw.decode("utf-8").splitlines()[:5])
            self.assertIn("SPDX-License-Identifier: GPL-3.0-only", head, rel)
            problems = export_public.check({rel: raw})
            self.assertEqual(problems, [], f"{rel}: {problems}")

    @unittest.skipUnless(
        os.environ.get("BEA_PS2_IMAGE") and os.environ.get("BEA_GAME"),
        "set BEA_PS2_IMAGE and BEA_GAME for optional local PS2 disc verification",
    )
    def test_optional_real_ps2_disc_image_read_only(self) -> None:
        ps2_img = Path(os.environ["BEA_PS2_IMAGE"])
        real_game = Path(os.environ["BEA_GAME"])
        real_user = self.tmp / "real_user"
        real_user.mkdir()
        ans = self._run_driver(["async", str(ps2_img)], game=real_game, user=real_user)
        self.assertTrue(ans.startswith("OK enabled=1 "), ans)
        rep = self._run_driver(["report"], game=real_game, user=real_user)
        self.assertIn("found=1 id=flagship-duel-ps2 why=0", rep)
        self.assertIn("check_rc=0", rep)


if __name__ == "__main__":
    unittest.main()
