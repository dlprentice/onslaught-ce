#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The port's mod layer (port/platform/common/mods.cpp, with ce.cpp's ce.ini reader) on the host, through
port/tools/mods_driver.cpp, and port/tools/mods.py's packing. Generated fixtures only (no game, retail file, device or
network), except the one control below. Then mutants: each removes one guard from a copy of mods.cpp and must make a named
test fail.

    python3 port/tools/mods_tests.py            # the tests, then the mutants
    python3 port/tools/mods_tests.py --no-mutants
    python3 port/tools/mods_tests.py --mutants-only   # only the mutants; MODS_TESTS_ONLY="text|text" keeps those whose names contain one of the texts

One control reads the player's install (BEA_GAME_DIR, else the default Steam folder): every level archive the game ships is
offered as an added archive and none may be refused. Without an install that test is skipped, the summary says so, and the
mutants aimed at it are reported as not exercised.
"""
import hashlib
import itertools
import json
import mmap
import os
import errno
import re
import resource
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
MODS_CPP = PORT / "platform/common/mods.cpp"
sys.path.insert(0, str(PORT / "tools"))
import mods  # noqa: E402

FENCES = ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT",
          "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN", "BEA_NET_HANDOFF")
TEX = "data/resources/dxtntextures/meshtex%Test.tga(0)A1R5G5B5.aya"
SND = "data/sounds/test.wav"
BUILD = ROOT / "local-data/port/mods-tests"
DRIVER = {}		# (source text, ce version) -> driver path


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


PLATFORM_OFF = ("PORT_MODS_PLATFORM_OFF",)		# builds the branch of a build that switches the mod layer off (mods refused), on the host
ANY_LEVEL = ("PORT_MODS_TEST_ANY_LEVEL",)		# admits an added archive for every level number, to try the shipped ones
SKIP_INLINE = ("PORT_MODS_TEST_SKIP_INLINE_MESH",)		# skips only the inline-mesh rule of the archive walk, to run the rest of it over archives that hold inline meshes
STALE = 0x41414141		# a pointer value for a field the reader must not leave to the file
# the four refusals of a part's cache (CheckMeshPart), in the order the walk checks them
CACHE_COUNT = "whose cached frame count is neither 1 nor its virtual-frame count"
CACHE_CPOS = "with a CPOS when its position is static, or without one when it is not"
CACHE_CORI = "with a CORI when its orientation is static, or without one when it is not"
CACHE_PARENT = "with no parent whose position or orientation is static"


def driver(source=None, ce_version=None, defines=(), sanitize=False):
    """Builds the driver from mods.cpp (or a mutant's text) once per (text, version, defines, sanitize). With sanitize it is built
    with AddressSanitizer and UndefinedBehaviorSanitizer, to catch a read the verdicts do not show."""
    text = source if source is not None else MODS_CPP.read_text()
    key = (hashlib.sha256(text.encode()).hexdigest()[:16], ce_version, tuple(defines), sanitize)
    if key in DRIVER:
        return DRIVER[key]
    out = BUILD / (f"driver-{key[0]}-{ce_version or 'dev'}" + "".join("-" + d for d in defines) + ("-asan" if sanitize else ""))
    # mods.cpp includes "../../updater/sha256.h": the copy sits at the same depth (a/b) as in port/platform/common.
    (out / "a/b").mkdir(parents=True, exist_ok=True)
    (out / "updater").mkdir(exist_ok=True)
    (out / "a/b/mods.cpp").write_text(text)
    for name in ("mods.h", "portlog.h"):
        shutil.copy(PORT / "platform/common" / name, out / "a/b" / name)
    shutil.copy(PORT / "updater/sha256.h", out / "updater/sha256.h")
    cxx, cc = os.environ.get("CXX", "g++"), os.environ.get("CC", "gcc")
    define = ([f'-DCE_VERSION="{ce_version}"'] if ce_version else []) + [f"-D{d}" for d in defines]
    sanitizers = ["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "-g", "-fno-omit-frame-pointer"] if sanitize else []
    steps = [
        [cc, "-O2", "-c", str(PORT / "updater/sha256.c"), "-o", str(out / "sha256.o")],
        # ce.cpp's ce.ini reader does not depend on the version (with CE_VERSION it would also need SDL).
        [cxx, "-std=gnu++17", "-O1", "-w", "-c", "-I", str(PORT / "platform/common"),
         str(PORT / "platform/common/ce.cpp"), "-o", str(out / "ce.o")],
        [cxx, "-std=gnu++17", "-O1", "-Wall", "-Wextra", "-Werror", *sanitizers, *define, "-I", str(out / "a/b"),
         "-I", str(PORT / "updater"), str(PORT / "tools/mods_driver.cpp"), str(out / "a/b/mods.cpp"),
         str(out / "ce.o"), str(out / "sha256.o"), "-lz", "-o", str(out / "driver")],
    ]
    for step in steps:
        r = subprocess.run(step, capture_output=True, text=True, timeout=120, preexec_fn=no_core)
        if r.returncode:
            raise RuntimeError(r.stderr)
    DRIVER[key] = out / "driver"
    return DRIVER[key]


def dds(width=8, height=8):
    """A small DXT3 image: header + one level."""
    head = bytearray(128)
    head[:4] = b"DDS "
    struct.pack_into("<IIII", head, 4, 124, 0x1007, height, width)
    struct.pack_into("<I", head, 76, 32)
    struct.pack_into("<I", head, 80, 4)
    head[84:88] = b"DXT3"
    return bytes(head) + bytes(range(256)) * ((width // 4) * (height // 4) * 16 // 256 + 1)


class Fixture:
    def __init__(self, tmp):
        self.tmp = Path(tmp)
        self.game = self.tmp / "game"
        self.user = self.tmp / "user"
        self.outside = self.tmp / "outside"
        for d in (self.game, self.user, self.outside):
            d.mkdir()
        for rel in (TEX, SND, "data/other.bin"):
            (self.game / rel).parent.mkdir(parents=True, exist_ok=True)
            (self.game / rel).write_bytes(b"original " + rel.encode())

    def mod(self, mod_id, files, manifest=None, raw=None, extra=None):
        """A mod with files {path: bytes}; manifest overrides fields; raw replaces mod.json's text."""
        d = self.user / "mods" / mod_id
        for rel, data in files.items():
            (d / rel).parent.mkdir(parents=True, exist_ok=True)
            (d / rel).write_bytes(data)
        m = {"schema": 1, "id": mod_id, "name": f"Mod {mod_id}", "version": "1.0", "author": "tests",
             "files": [{"path": rel, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                       for rel, data in files.items()]}
        m.update(manifest or {})
        (d / "mod.json").write_text(raw if raw is not None else json.dumps(m))
        return d

    def enable(self, *ids, raw=None):
        (self.user / "ce.ini").write_text("record_sessions=0\n" + (raw if raw is not None else "mods=" + ",".join(ids)) + "\n")

    def run(self, commands, env=None, exe=None, between=None):
        e = {k: v for k, v in os.environ.items() if k not in FENCES and k != "BEA_PORT_CE_LAYER"}
        e.update(BEA_USER_DIR=str(self.user), BEA_GAME_DIR=str(self.game))
        e.update(env or {})
        p = subprocess.Popen([str(exe or driver())], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, env=e, preexec_fn=no_core)
        out = []
        for c in commands:
            if c == "!between":
                between()
                continue
            p.stdin.write(c + "\n")
            p.stdin.flush()
            out.append(p.stdout.readline().strip())
        p.stdin.close()
        err = p.stderr.read()
        p.wait(timeout=60)
        p.stdout.close()
        p.stderr.close()
        return out, err


    def check(self, *ids, exe=None, env=None):
        """bea --check-mods through the driver: (exit status, the lines of standard output, standard error)."""
        e = {k: v for k, v in os.environ.items() if k not in FENCES and k != "BEA_PORT_CE_LAYER"}
        e.update(BEA_USER_DIR=str(self.user), BEA_GAME_DIR=str(self.game))
        e.update(env or {})
        r = subprocess.run([str(exe or driver()), "--check-mods", *ids], stdin=subprocess.DEVNULL, capture_output=True, text=True, env=e, timeout=120, preexec_fn=no_core)
        return r.returncode, r.stdout.splitlines(), r.stderr

    def tree(self):
        """Every path below the fixture with its kind, size, write and change time and hash: what "writes nothing" is compared on."""
        seen = {}
        for path in sorted(self.tmp.rglob("*")):
            st = path.lstat()
            seen[str(path.relative_to(self.tmp))] = (st.st_mode, st.st_size, st.st_mtime_ns, st.st_ctime_ns,
                                                     hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() and not path.is_symlink() else "")
        return seen


def digest(data):
    return f"OPEN {hashlib.sha256(data).hexdigest()} {len(data)}"


def blind_identity(text):
    """mods.cpp with every identity comparison answering "same" (the Linux and the Windows one): what a change that keeps the identity gets, a second write through a
    shared mapping whose page is already dirty on Linux, the times set back by an opener with FILE_WRITE_ATTRIBUTES on NTFS. Whatever the loader still refuses then,
    it refuses by the hash taken at every open."""
    head = "bool SameIdentity(const Identity &a, const Identity &b)\n{\n"
    assert text.count(head) == 2, "one definition for each platform"
    return text.replace(head, head + "\t(void)a;\n\t(void)b;\n\treturn true;\n")


def identity(path):
    """What the Linux loader compares of a file (mods.cpp, IdentityOf): device, inode, size, write and change time."""
    st = os.stat(path)
    return st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns


# ---- authored level archives: the chunk sequence the loader walks, with no game bytes in it ----------------------------
LEVEL_ARCHIVE = "data/resources/888_res_PC.aya"
GUARDS = (344, 372, 316, 5084, 92, 1)		# the AYAD words of the shipped format


def chunk(tag, payload):
    return tag + struct.pack("<I", len(payload)) + payload


def texture_record(name="meshtex\\Test.tga", frames=1):
    """A TEXT payload as the shipped archives hold it (372 bytes for one frame): DXTX around a CTEX of 344 bytes, whose frame
    count is the word at offset 312 (payload offset 328), and a TFRM of 4 bytes for each frame."""
    ctex = bytearray(344)
    ctex[8:8 + len(name) + 1] = name.encode() + b"\0"
    struct.pack_into("<I", ctex, 312, frames)
    return chunk(b"DXTX", chunk(b"CTEX", bytes(ctex)) + chunk(b"TFRM", struct.pack("<I", 1)) * frames)


def sized_texture(size, frames=1):
    """A TEXT payload of exactly `size` bytes whose DXTX and CTEX framing is right, with `frames` as its frame count; nothing else
    in it is looked at."""
    payload = bytearray(b"DXTX" + struct.pack("<I", size - 8) + b"CTEX" + bytes(size - 12))
    if size >= 332:
        struct.pack_into("<I", payload, 328, frames)
    return bytes(payload)


def mesh_record(size=325, name="m.msh"):
    """A MESH payload as the shipped archives hold it (325 bytes and up): PMSH around a PMS2 that fills it, whose data starts with the
    300-byte name."""
    body = bytearray(size - 16)
    body[:len(name) + 1] = name.encode() + b"\0"
    return b"PMSH" + struct.pack("<I", size - 8) + b"PMS2" + struct.pack("<I", size - 16) + bytes(body)


def named_mesh(size, name_bytes=b"m"):
    """A MESH payload of `size` bytes whose 300-byte name field starts with exactly `name_bytes` (zeros after them), so a name that
    is empty in its first byte but not in its second can be made."""
    record = bytearray(mesh_record(size, name=""))
    record[16:16 + len(name_bytes)] = name_bytes
    return bytes(record)


def inline_part(index=0, parts=1, textures=1, words=None, children=(), parent=None, vframes=1, hframes=1, orientations=True, positions=True,
                grid=None, reference=None, pieces=None, cmvb=None, vbuf_extra=0, box=True, after=b"", cmsp_delta=0, cmvb_delta=0, tail=b"", cached=(1, 16, 48), static=(0, 0),
                no_pmvb=False, vhfm=None, hpos=None, bones=None, bone_chunk=None, fovs=None, fov_chunk=None):
    """The MESP chunk of a part of an inline mesh (cmsh-fields.md): CMSP, then each chunk the part record says is there, then PMVB. `words`
    patches the CMSP record ({offset: u32}); `pieces` is a list of dicts for the vertex buffer's pieces (vertices, indices, texr, ...). `bones` (part numbers) makes
    it a skeletal part as the shipped ones are, with the stale bones pointer each carries and 48-byte vertices; `fovs` (values) makes it a camera part, with no pieces
    unless `pieces` says so. `bone_chunk` and `fov_chunk` replace those chunks whole (b"" leaves one out). `cached` is the cache as CMeshPart::BuildCache leaves it in
    a part that is not static (the count, then the bytes of the CPOS and of the CORI, each with its word set; None leaves a chunk and its word out) and `static` the
    position-static and orientation-static flags."""
    stride = 36 if bones is None else 48
    pieces = pieces if pieces is not None else [] if fovs is not None else [dict()]
    cmsp = bytearray(316)
    struct.pack_into("<IIIII", cmsp, 0x88, index, 3 if bones is not None else 4 if fovs is not None else 1 if reference is None else 6, len(children), 0, 0)
    struct.pack_into("<I", cmsp, 0x98, 0 if parent is None else 0xDEAD)
    struct.pack_into("<I", cmsp, 0xB8, vframes)
    struct.pack_into("<I", cmsp, 0xBC, hframes)
    struct.pack_into("<I", cmsp, 0xC8, 1 if positions else 0)
    struct.pack_into("<I", cmsp, 0x100, 1 if grid is not None else 0)
    struct.pack_into("<I", cmsp, 0x10C, 1 if orientations else 0)
    struct.pack_into("<I", cmsp, 0x124, 0 if reference is None else 1)
    if bones is not None:
        struct.pack_into("<I", cmsp, 0xC0, len(bones))
        struct.pack_into("<I", cmsp, 0xD0, STALE)
    if fovs is not None:
        struct.pack_into("<I", cmsp, 0xCC, 1)
    struct.pack_into("<II", cmsp, 0x104, 0 if cached[1] is None else STALE, 0 if cached[2] is None else STALE)
    struct.pack_into("<III", cmsp, 0x118, cached[0], *static)
    for offset, value in (words or {}).items():
        struct.pack_into("<I", cmsp, offset, value)
    out = chunk(b"CMSP", (bytes(cmsp) + bytes(max(cmsp_delta, 0)))[:316 + cmsp_delta])
    if children:
        out += chunk(b"CHLD", b"".join(struct.pack("<I", c) for c in children))
    if parent is not None:
        out += chunk(b"PRNT", struct.pack("<I", parent))
    if box:
        out += chunk(b"BBOX", chunk(b"BBOX", bytes(40)))
    if vframes:
        out += chunk(b"VHFM", bytes(vframes) if vhfm is None else vhfm)
    if hframes and orientations:
        out += chunk(b"HORI", bytes(48 * hframes))
    if hframes and positions:
        out += chunk(b"HPOS", bytes(16 * hframes) if hpos is None else hpos)
    if fovs is not None or fov_chunk is not None:
        out += chunk(b"HFOV", struct.pack(f"<{len(fovs)}f", *fovs)) if fov_chunk is None else fov_chunk
    if bones is not None or bone_chunk is not None:
        out += chunk(b"BONE", struct.pack(f"<{len(bones)}I", *bones)) if bone_chunk is None else bone_chunk
    if grid is not None:
        out += chunk(b"PBKT", grid)
    if cached[1] is not None:
        out += chunk(b"CPOS", bytes(cached[1]))
    if cached[2] is not None:
        out += chunk(b"CORI", bytes(cached[2]))
    if reference is not None:
        out += chunk(b"REFR", struct.pack("<I", reference))
    if no_pmvb:
        return chunk(b"MESP", out + tail)
    head = bytearray(296)
    struct.pack_into("<i", head, 0x108, len(pieces))
    if pieces:
        struct.pack_into("<III", head, 0x114, stride, 0x152 if stride == 36 else 0, 4)
    for offset, value in (cmvb or {}).items():
        struct.pack_into("<I", head, offset, value)
    vb = chunk(b"CMVB", (bytes(head) + bytes(max(cmvb_delta, 0)))[:296 + cmvb_delta])
    first = pieces[0] if pieces else {}
    vcount = first.get("vertices", 4)
    for n, piece in enumerate(pieces):
        indices = piece.get("indices", [0, 1, 2, 3])
        vc = piece.get("vertex_count", vcount)
        texr = piece.get("texr", [0, -1, -1, -1, -1, -1])
        mmpt = struct.pack("<6I", piece.get("vbytes", stride * vc), piece.get("ibytes", 2 * len(indices)), len(indices) if "icount" not in piece else piece["icount"],
                           vc, piece.get("prims", len(indices) - 2), piece.get("built", 1))
        vb += chunk(b"MMPT", mmpt[:len(mmpt) - piece.get("mmpt_cut", 0)] + bytes(piece.get("mmpt_extra", 0)))
        vb += chunk(b"IBUF", struct.pack(f"<{len(indices)}H", *indices) + bytes(piece.get("ibuf_extra", 0)))
        vb += chunk(b"VBUF", piece.get("vbuf", bytes(stride * vc + vbuf_extra)) if n == 0 else bytes(piece.get("vbuf2", 0)))
        vb += chunk(b"TEXR", struct.pack("<6i", *texr) + bytes(piece.get("texr_extra", 0)))
    out += chunk(b"PMVB", vb + after)
    return chunk(b"MESP", out + tail)


def skinned_vertices(rows):
    """The VBUF of 48-byte vertices, one for each row of three palette offsets (float values, or ints taken as the offset's bits). The position and the normal are not 0, so
    that a reader of the wrong field sees something that is no offset."""
    out = bytearray(48 * len(rows))
    for i, row in enumerate(rows):
        struct.pack_into("<3f", out, 48 * i, 1.5, 2.5, 3.5)
        struct.pack_into("<3f", out, 48 * i + 24, 0.25, 0.5, 0.75)
        for slot, offset in enumerate(row):
            struct.pack_into("<I" if isinstance(offset, int) else "<f", out, 48 * i + 12 + 4 * slot, offset)
    return bytes(out)


def skeletal_mesh(bones=2, rows=None, names=None, **part):
    """A mesh of `bones` null parts and, last, a skeletal part that names each of them (or the part numbers in `names`); `rows` are the vertices' palette offsets (four vertices of offset 0
    by default)."""
    rows = [(0.0, 0.0, 0.0)] * 4 if rows is None else rows
    part.setdefault("pieces", [dict(vertex_count=len(rows), vbuf=skinned_vertices(rows), indices=[i % len(rows) for i in range(4)])])
    part.setdefault("bones", list(range(bones)) if names is None else names)
    return inline_mesh(parts=bones + 1, part_list=[inline_part(i, bones + 1, 1, pieces=[]) for i in range(bones)] + [inline_part(bones, bones + 1, 1, **part)])


def camera_mesh(fovs=(90.0,), **part):
    """A mesh of a normal part and a camera part."""
    part.setdefault("fovs", list(fovs))
    part.setdefault("hframes", len(fovs))
    return inline_mesh(parts=2, part_list=[inline_part(0, 2, 1), inline_part(1, 2, 1, **part)])


def cache_mesh(static=(0, 0), cached=(1, 16, 48), vframes=1, listed=True, children=False, kind=""):
    """A mesh of two parts whose second is the one under test, with the cache words, the static flags and the virtual-frame count of its arguments: the first part lists it and it has that
    part as its parent, as a static part must (or, when `listed` is false, no part lists it and it is a second top part). The first part is as every shipped part is. With `children` the part
    under test lists a third part of its own; `kind` makes it a "null", "emitter", "reference", "skeletal" or "camera" part, one with a "grid", or one of 2 "hand frames" (otherwise it is a
    normal part of one hand frame)."""
    extra = {"": {}, "null": dict(words={0x8C: 2}), "emitter": dict(words={0x8C: 5}), "reference": dict(reference=0), "skeletal": dict(bones=[0]), "camera": dict(fovs=[90.0]),
             "grid": dict(grid=inline_grid()), "hand frames": dict(hframes=2)}[kind]
    parts = 3 if children else 2
    part_list = [inline_part(0, parts, 1, children=[1] if listed else []),
                 inline_part(1, parts, 1, parent=0 if listed else None, children=[2] if children else [], vframes=vframes, static=static, cached=cached, **extra)]
    if children:
        part_list.append(inline_part(2, parts, 1, parent=1))
    return inline_mesh(parts=parts, part_list=part_list)


def cache_reasons(static, cpos, cori, vframes, count, listed):
    """The rules a part's cache is held to, as the walk's comment states them: the refusals it gives, in the order it checks them, for a part with these static flags, a CPOS and a CORI or
    not, this virtual-frame count and cached-frame count, and some part listing it or not. No rule is one a part's other flag, word, count, kind or place may switch off."""
    return [reason for fails, reason in ((count != 1 and count != vframes, CACHE_COUNT), (bool(cpos) == bool(static[0]), CACHE_CPOS), (bool(cori) == bool(static[1]), CACHE_CORI),
                                         (any(static) and not listed, CACHE_PARENT)) if fails]


def anim_mode(name=b"idle", mode=10, start=0, last=0, scaler=0, rate=0.0, rate_bits=None):
    """A CAMD record (cmsh-fields.md, CAnimMode): a 16-byte name, the mode, start, last and frame scaler (signed), and the rate (a float, or its bits)."""
    record = bytearray(36)
    record[:len(name)] = name
    struct.pack_into("<iiii", record, 16, mode, start, last, scaler)
    struct.pack_into("<f" if rate_bits is None else "<I", record, 32, rate if rate_bits is None else rate_bits)
    return bytes(record)


def inline_stream(textures=1, frames=None, tex_name=b"tex.tga", parts=1, cmsh=None, cmst=None, texb_delta=0, part=None, anim=0, emitters=0, cemt=None,
                  tail=b"", name=b"inline.msh", part_list=None, second_texb=False, modes=None):
    """The mesh stream (cmsh-fields.md) of an inline mesh of the profile CheckInlineMesh admits: one texture, one part with one four-vertex piece,
    unless the arguments say otherwise. `cmsh` patches the 372-byte record ({offset: u32}); `modes` are its CAMD records (anim_mode), which are
    otherwise `anim` records of zeros."""
    anim = anim or (len(modes) if modes else 0)
    frames = frames or [1] * textures
    head = bytearray(372)
    struct.pack_into("<i", head, 4, textures)
    struct.pack_into("<i", head, 0x14, anim)
    struct.pack_into("<i", head, 0x1C, emitters)
    head[0x24:0x24 + len(name) + 1] = name + b"\0"
    struct.pack_into("<i", head, 0x15C, parts)
    for offset, value in (cmsh or {}).items():
        struct.pack_into("<I", head, offset, value)
    out = chunk(b"CMSH", bytes(head))
    table = bytearray(36 * textures)
    for i, f in enumerate(frames):
        struct.pack_into("<i", table, 36 * i + 8, f)
    out += chunk(b"CMST", bytes(table) if cmst is None else cmst)
    for i in range(textures):
        texb = bytes(20 * frames[i]) + tex_name.ljust(128, b"\0")
        texb = texb[:len(texb) + texb_delta] if texb_delta <= 0 else texb + bytes(texb_delta)
        out += chunk(b"MSHT", chunk(b"TEXB", texb) + (chunk(b"TEXB", texb) if second_texb else b""))
    for i in range(parts):
        out += part_list[i] if part_list else inline_part(i, parts, textures, **(part or {}))
    if anim:
        out += chunk(b"CAMD", bytes(36 * anim) if modes is None else b"".join(modes))
    out += chunk(b"BBOX", chunk(b"BBOX", bytes(40)))
    if emitters:
        out += cemt if cemt is not None else chunk(b"CEMT", struct.pack("<i", 336) + bytes(336) * emitters)
    return out + tail


def inline_mesh(stream=None, runtime_id=20000, name=b"", optional=0, kept=0, **kw):
    """A MESH payload whose mesh is unnamed: PMSH around a PMS2 that holds the 309-byte reference and then the mesh's stream."""
    stream = inline_stream(**kw) if stream is None else stream
    reference = bytearray(309)
    reference[:len(name)] = name
    struct.pack_into("<ii", reference, 300, runtime_id, optional)
    reference[308] = kept
    body = bytes(reference) + stream
    return b"PMSH" + struct.pack("<I", len(body) + 8) + b"PMS2" + struct.pack("<I", len(body)) + body


def chain_link(stream, runtime_id=20001, name=b"", optional=0, kept=1, pmsh_delta=0, pms2_delta=0, pmsh_tag=b"PMSH", pms2_tag=b"PMS2"):
    """The framing of a chained inline mesh (all 12 shipped chains): a PMSH header and a PMS2 header, each around the rest of the stream, the 309-byte reference, then the mesh's stream."""
    reference = bytearray(309)
    reference[:len(name)] = name
    struct.pack_into("<ii", reference, 300, runtime_id, optional)
    reference[308] = kept
    body = bytes(reference) + stream
    return pmsh_tag + struct.pack("<I", len(body) + 8 + pmsh_delta) + pms2_tag + struct.pack("<I", len(body) + pms2_delta) + body


def inline_chain(count=2, ids=None, links=None, last=None, **kw):
    """The stream of a chain of `count` meshes, each built by inline_stream(**kw): every mesh but the last has its chain word set and is followed by chain_link of the rest; `ids` are the runtime ids
    of the second, third ... mesh (20001, 20002, ...), `links` maps the number of a link (1 is the one before the second mesh) to chain_link's own arguments, `last` is the last mesh's stream."""
    ids = ids or [20001 + i for i in range(count - 1)]
    rest = inline_stream(**kw) if last is None else last
    for i in range(count - 2, -1, -1):
        rest = inline_stream(cmsh={8: 1}, **kw) + chain_link(rest, ids[i], **(links or {}).get(i + 1, {}))
    return rest


def grid_cells(width, height, lists):
    """The PBCS chunks of a collision grid: `lists` maps a cell number to its list of triangle indices."""
    out = b""
    for cell in range(width * height):
        if cell in lists:
            idx = lists[cell]
            out += chunk(b"PBCS", struct.pack("<i", 1) + chunk(b"PBET", chunk(b"PBSQ", struct.pack("<i", len(idx)) + struct.pack(f"<{len(idx)}h", *idx))))
        else:
            out += chunk(b"PBCS", struct.pack("<i", 0))
    return out


def inline_grid(width=2, height=1, lists=None, triangles=2, vertices=4, tri_vertices=None, sizs=None, plqt=None, cpbt=None, cells=None, pbke_size=12,
                cpbt_raw=None, sizs_raw=None, plqt_raw=None, no_pbfk=False, pbpn_raw=None, pbpp_size=6, pbpp_count=None, tail=b""):
    """The payload of a PBKT: CPBT, SIZS, the cells, PLQT, PBFK around the PBKEs, PBPN and the PBPPs. The `*_raw` arguments replace a fixed-size record's payload, `no_pbfk` leaves the PBFK
    out, `pbpp_size` and `pbpp_count` change the vertex records, and `tail` follows the last vertex."""
    lists = {0: [0, 1]} if lists is None else lists
    record = bytearray(184)
    struct.pack_into("<ii", record, 0x68, width, height)
    for offset, value in (cpbt or {}).items():
        struct.pack_into("<I", record, offset, value)
    n_lists, n_indices = len(lists), sum(len(v) for v in lists.values())
    out = chunk(b"CPBT", bytes(record) if cpbt_raw is None else cpbt_raw)
    out += chunk(b"SIZS", (struct.pack("<ii", n_lists, n_indices) if sizs is None else struct.pack("<ii", *sizs)) if sizs_raw is None else sizs_raw)
    out += grid_cells(width, height, lists) if cells is None else cells
    out += chunk(b"PLQT", struct.pack("<i", triangles if plqt is None else plqt) if plqt_raw is None else plqt_raw)
    tri_vertices = tri_vertices or [(i % vertices, (i + 1) % vertices, (i + 2) % vertices) for i in range(triangles)]
    if not no_pbfk:
        out += chunk(b"PBFK", b"".join(chunk(b"PBKE", (struct.pack("<IHHHH", 0x1FFFE000, *v, 0) + bytes(max(pbke_size - 12, 0)))[:pbke_size]) for v in tri_vertices))
    out += chunk(b"PBPN", struct.pack("<i", vertices) if pbpn_raw is None else pbpn_raw)
    out += b"".join(chunk(b"PBPP", bytes(pbpp_size)) for _ in range(vertices if pbpp_count is None else pbpp_count))
    return out + tail


def world_body(version=50, size=32):
    return struct.pack("<H", version) + bytes(size - 2)


def level_wres(level=888, base=-1, bodies=None):
    """A WRES payload: one WRLD holding WDAT and the world bodies (a BSWD first when there is a base world)."""
    if bodies is None:
        bodies = (chunk(b"BSWD", world_body()) if base != -1 else b"") + chunk(b"RLWD", world_body())
    return chunk(b"WRLD", chunk(b"WDAT", struct.pack("<ii", base, level)) + bodies)


def level_chunks(level=888, base=-1, wres=None):
    """The (tag, payload) list of an authored level archive."""
    return ([(b"LVLR", struct.pack("<I", 103)), (b"TARG", struct.pack("<I", 1)), (b"AYAD", struct.pack("<6I", *GUARDS))]
            + [(b"TEXT", texture_record()) for _ in range(3)] + [(b"MESH", mesh_record()) for _ in range(2)]
            + [(b"IMPS", bytes(8)), (b"LNDS", b""), (b"SURF", bytes(12)), (b"ERES", bytes(64)), (b"SSHD", bytes(16)),
               (b"WRES", level_wres(level, base) if wres is None else wres)])


def level_stream(chunks):
    return b"".join(chunk(t, p) for t, p in chunks)


def level_archive(chunks=None, **kw):
    """An authored level archive, packed as the game reads it."""
    return mods.pack(level_stream(level_chunks(**kw) if chunks is None else chunks))


def iter_chunks(raw):
    """The offsets of the top-level chunk headers of an unpacked archive."""
    at = 0
    while at + 8 <= len(raw):
        yield at
        at += 8 + struct.unpack_from("<I", raw, at + 4)[0]


def inline_chunk(raw):
    """(payload offset, size) of the inline MESH chunk of an unpacked archive, or None."""
    for at in iter_chunks(raw):
        if raw[at:at + 4] == b"MESH" and raw[at + 8 + 16] == 0:
            return at + 8, struct.unpack_from("<I", raw, at + 4)[0]
    return None


def move_runtime_ids(raw, first=20000):
    """Gives the inline mesh of an unpacked archive (a bytearray) and the meshes chained to it runtime ids an added archive may use, as a stager would, and touches nothing else. Returns the
    number of meshes: a chain link is the one place where PMSH and PMS2 headers fill the rest of the chunk."""
    at, size = inline_chunk(raw)
    end = at + size
    struct.pack_into("<I", raw, at + 316, first)
    meshes = 1
    head = raw.find(b"PMSH", at + 325, end)
    while head >= 0:
        rest = end - head
        if raw[head + 8:head + 12] == b"PMS2" and struct.unpack_from("<II", raw, head + 4)[0] == rest - 8 and struct.unpack_from("<I", raw, head + 12)[0] == rest - 16:
            struct.pack_into("<I", raw, head + 16 + 300, first + meshes)
            meshes += 1
        head = raw.find(b"PMSH", head + 4, end)
    return meshes


def shipped_archives():
    """The numeric level archives of the installed game (BEA_GAME_DIR, else the default Steam folder), or nothing."""
    folder = mods.default_game() / "data/resources"
    return sorted(folder.glob("[0-9][0-9][0-9]_res_PC.aya")) if folder.is_dir() else []


def shipped_meshes():
    """The loose mesh files of the installed game, or nothing."""
    folder = mods.default_game() / "data/resources/meshes"
    return sorted(folder.glob("*.aya")) if folder.is_dir() else []


class ModsTests(unittest.TestCase):
    exe = None			# a mutant's driver, when set
    ce_exe = None
    link_reason = "can no longer be opened safely"		# what the log says for a file replaced by a link (mods_wine_tests.py: Wine follows the link)
    source = None		# and its text, which the sanitizer test builds its own driver from

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="mods-", dir=BUILD)
        self.f = Fixture(self._tmp.name)
        self.tex = mods.pack(dds())

    def tearDown(self):
        self._tmp.cleanup()

    def run_driver(self, commands, **kw):
        kw.setdefault("exe", self.exe)
        return self.f.run(commands, **kw)

    def assertRefused(self, err, words):
        self.assertIn("refused", err)
        self.assertIn(words, err)

    def check_exe(self):
        return self.exe or driver()

    def bad_files(self):
        """A mod with one good file and three refused ones, each for its own reason."""
        good, other = self.tex, b"other bytes"
        (self.f.game / "data/third.bin").write_bytes(b"original")
        return self.f.mod("bad", {TEX: good, SND: b"new", "data/other.bin": other, "data/third.bin": b"third"}, manifest={"files": [
            {"path": SND, "size": 3, "sha256": "0" * 64},
            {"path": "data/other.bin", "size": len(other) + 1, "sha256": hashlib.sha256(other).hexdigest()},
            {"path": TEX, "size": len(good), "sha256": hashlib.sha256(good).hexdigest()},
            {"path": "data/lacking.bin", "size": 1, "sha256": "0" * 64}]})

    # ---- the check mode (mods.h, PortModsCheck): the same loader, every refusal, nothing written ------------------------
    def test_check_mode_passes_what_the_loader_would_load(self):
        self.f.mod("good", {TEX: self.tex, SND: b"new"})
        self.f.mod("two", {"data/other.bin": b"x"})
        self.f.enable("good", "two")
        status, lines, err = self.f.check(exe=self.check_exe())
        self.assertEqual(status, 0, lines)
        self.assertEqual(lines[-1], "result: 2 passed, 0 refused")
        self.assertIn("2 active, 3 game files replaced (experimental)", lines)
        self.assertEqual(sum(line.startswith("good replaces ") for line in lines), 2, lines)
        self.assertFalse([line for line in lines if line.startswith("refused")], lines)
        self.assertEqual(err, "")
        self.assertNotIn(str(self.f.user), "\n".join(lines), "the output must not name the user folder")

    def test_check_mode_prints_every_refused_file_where_the_game_stops_at_the_first(self):
        self.bad_files()
        self.f.enable("bad")
        status, lines, err = self.f.check(exe=self.check_exe())
        self.assertEqual(status, 1, lines)
        self.assertEqual([line for line in lines if line.startswith("refused")],
                         [f"refused bad: file {SND}: its SHA-256 is not the manifest's", "refused bad: file data/other.bin: its size is not the manifest's",
                          "refused bad: file data/lacking.bin: the game has no such file (a mod can only replace the game's files, or add a world file, data/worlds/worldNNN.wrd, or the "
                          "archive of a multiplayer level that has none, data/resources/NNN_res_PC.aya for NNN 850 to 899)"], lines)
        self.assertEqual(lines[-1], "result: 0 passed, 1 refused")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"], err)
        self.assertEqual(err.count("refused bad"), 1, "the game stops at the first refusal of a mod")

    def test_check_mode_takes_the_named_mods_and_otherwise_the_ce_ini_line(self):
        self.f.mod("first", {SND: b"new"})
        self.f.mod("second", {"data/other.bin": b"x"})
        self.f.enable("first")
        status, lines, _ = self.f.check(exe=self.check_exe())
        self.assertEqual((status, lines[-1]), (0, "result: 1 passed, 0 refused"), lines)
        self.assertTrue(any(line.startswith("first replaces ") for line in lines), lines)
        status, lines, _ = self.f.check("second", exe=self.check_exe())
        self.assertEqual((status, lines[-1]), (0, "result: 1 passed, 0 refused"), lines)
        self.assertTrue(any(line.startswith("second replaces ") for line in lines), lines)
        self.assertFalse(any("first" in line for line in lines), lines)
        status, lines, _ = self.f.check("first", "second", exe=self.check_exe())
        self.assertEqual((status, lines[-1]), (0, "result: 2 passed, 0 refused"), lines)

    def test_check_mode_says_when_nothing_is_enabled(self):
        status, lines, _ = self.f.check(exe=self.check_exe())
        self.assertEqual(status, 0, lines)
        self.assertEqual(lines[-2:], ["no mods enabled: ce.ini has no usable mods= line", "result: 0 passed, 0 refused"])

    def test_check_mode_writes_nothing_and_creates_no_user_folder(self):
        self.bad_files()
        self.f.mod("good", {SND: b"new"})
        self.f.enable("good", "bad")
        before = self.f.tree()
        status, lines, _ = self.f.check(exe=self.check_exe())
        self.assertEqual(status, 1, lines)
        status, lines, _ = self.f.check("good", exe=self.check_exe())
        self.assertEqual(status, 0, lines)
        self.assertEqual(self.f.tree(), before)
        missing = self.f.tmp / "no-such-user-folder"
        status, lines, _ = self.f.check(exe=self.check_exe(), env=self.user_env(missing))
        self.assertEqual(status, 0, lines)
        status, lines, _ = self.f.check("good", exe=self.check_exe(), env=self.user_env(missing))
        self.assertEqual(status, 1, lines)
        self.assertIn("refused good: no such folder in the user folder's mods folder (or the folder is a link)", lines)
        self.assertFalse(missing.exists())
        self.assertEqual(self.f.tree(), before)

    def user_env(self, folder):
        return {"BEA_USER_DIR": str(folder)}

    def test_check_mode_has_no_line_limit(self):
        n = 350		# more than the game's log shows
        entries = [{"path": f"data/none{i}.bin", "size": 1, "sha256": "0" * 64} for i in range(n)]
        self.f.mod("many", {SND: b"new"}, manifest={"files": entries})
        status, lines, _ = self.f.check("many", exe=self.check_exe())
        self.assertEqual(status, 1, lines)
        self.assertEqual(sum(line.startswith("refused many: file data/none") for line in lines), n, lines[-3:])
        self.assertEqual(lines[-1], "result: 0 passed, 1 refused")

    def test_check_mode_reads_on_after_a_refused_manifest_field(self):
        self.f.mod("fields", {SND: b"new"}, manifest={"schema": 2, "id": "other", "name": "", "unknown": 1, "ce_min": "x", "files": [
            {"path": SND, "size": 3, "sha256": "0" * 64, "extra": 1}, "text", {"path": SND}, {"path": SND, "size": 3, "sha256": "ZZ"}, {"path": SND, "size": 1 << 30, "sha256": "0" * 64}]})
        status, lines, _ = self.f.check("fields", exe=self.check_exe())
        self.assertEqual(status, 1, lines)
        reasons = [line.split(": ", 1)[1] for line in lines if line.startswith("refused fields:")]
        self.assertEqual(reasons, ["an unknown key in the manifest (schema 1 knows schema, id, name, version, author, description, ce_min, ce_max and files)", "schema is not 1",
                                   "id is not the mod folder's name", "name is missing, not a string, or not 1 to 64 characters",
                                   "ce_min or ce_max is not a version (numbers separated by dots, such as 0.1.8)", "an unknown key in a files entry (path, size and sha256 only)",
                                   "a files entry is not an object", "a files entry lacks path (text), size (a whole number) or sha256 (text)",
                                   "a sha256 that is not 64 lowercase hexadecimal digits", "a file larger than 64 MiB"])

    def test_check_mode_counts_ids_it_cannot_take(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable(raw="mods=good,Bad Id,missing,good")
        status, lines, _ = self.f.check(exe=self.check_exe())
        self.assertEqual(status, 1, lines)
        self.assertIn("refused Bad Id: a mod id is 1 to 48 lowercase letters, digits, '-' or '_'", lines)
        self.assertIn("refused missing: no such folder in the user folder's mods folder (or the folder is a link)", lines)
        self.assertIn("good is listed twice in ce.ini; its first place counts", lines)
        self.assertEqual(lines[-1], "result: 1 passed, 2 refused")

    def test_a_mod_listed_twice_is_checked_once_whether_it_passes_or_not(self):
        self.bad_files()		# three refused files
        self.f.mod("good", {SND: b"new"})
        self.f.enable("bad", "bad", "good", "good")
        status, lines, _ = self.f.check(exe=self.check_exe())
        self.assertEqual(status, 1, lines)
        self.assertEqual(sum(line.startswith("refused bad: ") for line in lines), 3, lines)
        self.assertIn("bad is listed twice in ce.ini; its first place counts", lines)
        self.assertIn("good is listed twice in ce.ini; its first place counts", lines)
        self.assertEqual(lines[-1], "result: 1 passed, 1 refused")
        status, lines, _ = self.f.check("bad", "bad", exe=self.check_exe())
        self.assertEqual((status, lines[-1]), (1, "result: 0 passed, 1 refused"), lines)
        self.assertIn("bad is listed twice; its first place counts", lines, "named on the command line, not in ce.ini")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 1 -"], err)
        self.assertEqual(err.count("refused bad"), 1, "the game logs the refusal once")

    def test_an_id_that_is_a_windows_device_name_is_refused_on_every_platform(self):
        # No Windows folder can be called con, aux, nul, prn, com0 to com9 or lpt0 to lpt9 (CreateDirectory refuses them), so such a mod cannot be installed there: the one
        # validator refuses it on Linux too. Names that only begin like one are ordinary ids.
        devices, ordinary = ("aux", "con", "nul", "prn", "com0", "com1", "lpt9"), ("com10", "console", "nul-sounds")
        for name in devices + ordinary:
            self.f.mod(name, {"data/other.bin": name.encode()})
        self.f.enable(*devices, *ordinary)
        status, lines, _ = self.f.check(exe=self.check_exe())
        self.assertEqual(status, 1, lines)
        self.assertEqual([line for line in lines if line.startswith("refused")],
                         [f"refused {name}: a Windows device name, which no Windows folder can have (a mod must load on every platform)" for name in devices], lines)
        self.assertEqual(lines[-1], f"result: {len(ordinary)} passed, {len(devices)} refused")
        out, err = self.run_driver(["active", "open data/other.bin"])
        self.assertEqual(out, [f"ACTIVE {len(ordinary)} -", digest(ordinary[-1].encode())], err)

    def test_the_mod_tool_takes_the_ids_the_loader_takes(self):
        # port/tools/mods.py writes a mod.json only for a folder name the loader takes (mods.id_problem): every device name, the names next to them and names the
        # character rule refuses, each judged by the loader in one check (an id is judged before its folder is opened; the refused ones print in their order).
        devices = ["con", "prn", "aux", "nul"] + [f"{p}{d}" for p in ("com", "lpt") for d in range(10)]
        near = ["com", "lpt", "com10", "lpt1x", "console", "conin", "auxx", "nul-sounds", "a", "x" * 48]
        other = ["Upper", "a.b", "x" * 49]
        ids = devices + near + other
        for name in ids:
            self.f.mod(name, {"data/other.bin": name.encode()})
        status, lines, _ = self.f.check(*ids, exe=self.check_exe())
        tool_refuses = [name for name in ids if mods.id_problem(name)]
        self.assertEqual(tool_refuses, devices + other, "the tool's own rule")
        self.assertEqual(status, 1, lines)
        self.assertEqual([line.split(": ", 1)[0] for line in lines if line.startswith("refused ")],
                         ["refused " + (name if len(name) <= 48 else name[:48] + "...") for name in tool_refuses], lines)
        self.assertEqual(lines[-1], f"result: {len(ids) - len(tool_refuses)} passed, {len(tool_refuses)} refused")

    def test_check_mode_is_not_fenced_and_says_what_the_fence_would_do(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable("good")
        status, lines, _ = self.f.check(exe=self.check_exe(), env={"BEA_PORT_TAPE": "x"})
        self.assertEqual(status, 0, lines)
        self.assertIn("note: a game started with BEA_PORT_TAPE set keeps mods off; these checks are not affected", lines)
        self.assertEqual(lines[-1], "result: 1 passed, 0 refused")

    def test_check_mode_stops_a_mod_at_the_limit_of_the_whole_set(self):
        source = self.source or MODS_CPP.read_text()
        old = "kMaxTotal = 1ull << 30"
        self.assertEqual(source.count(old), 1)
        exe = driver(source.replace(old, "kMaxTotal = 4"), ce_version=CHECK_VERSION)
        self.f.mod("big", {SND: b"12345", "data/other.bin": b"12345"})
        status, lines, _ = self.f.check("big", exe=exe)
        self.assertEqual(status, 1, lines)
        self.assertEqual([line for line in lines if line.startswith("refused")], ["refused big: the enabled mods hold more than 1 GiB"], lines)

    # ---- a working mod -----------------------------------------------------------------------------------------------
    def test_enabled_mod_replaces_files(self):
        self.f.mod("good", {TEX: self.tex, SND: b"new sound"})
        self.f.enable("good")
        out, err = self.run_driver(["active", "open " + TEX.replace("/", "\\").upper(), "open " + SND,
                                    "open data/other.bin", "open DATA\\sounds\\..\\sounds\\.\\test.wav"])
        self.assertEqual(out, ["ACTIVE 1 -", digest(self.tex), digest(b"new sound"), "NONE", digest(b"new sound")])
        self.assertIn("1 active, 2 game files replaced", err)
        self.assertIn("good replaces " + TEX, err)
        self.assertNotIn(str(self.f.user), err, "the log must not name the user folder")

    def test_absolute_and_drive_reads_are_never_replaced(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable("good")
        out, _ = self.run_driver(["open /" + SND, "open C:\\" + SND.replace("/", "\\"), "open \\" + SND])
        self.assertEqual(out, ["NONE"] * 3)

    def test_no_mods_line_is_silent(self):
        self.f.mod("good", {SND: b"new"})
        out, err = self.run_driver(["active", "open " + SND])
        self.assertEqual(out, ["ACTIVE 0 -", "NONE"])
        self.assertEqual(err, "")

    def test_ambiguous_or_incomplete_config_never_enables_mods(self):
        self.f.mod("good", {SND: b"new"})
        settings = self.f.user / "ce.ini"
        for raw in (b"mods=good\nmods=other\n", b"mods=good\x00\n", b"mods=good\n" + b"x" * (1 << 20),
                    b"mods=" + b"x" * 2048 + b"\n"):
            with self.subTest(length=len(raw), prefix=raw[:16]):
                settings.write_bytes(raw)
                out, _ = self.run_driver(["active", "open " + SND])
                self.assertEqual(out, ["ACTIVE 0 -", "NONE"])
        settings.write_bytes(b"unknown=\xff\r\nmods=good\r\n")
        out, _ = self.run_driver(["active", "open " + SND])
        self.assertEqual(out, ["ACTIVE 1 -", digest(b"new")])

    def test_disabled_mod_is_not_loaded(self):
        self.f.mod("off", {SND: b"new"})
        self.f.mod("on", {"data/other.bin": b"x"})
        self.f.enable("on")
        out, err = self.run_driver(["active", "open " + SND, "open data/other.bin"])
        self.assertEqual(out, ["ACTIVE 1 -", "NONE", digest(b"x")])
        self.assertNotIn("off", err.replace("off for", ""))

    def test_later_mod_wins_and_conflict_is_logged(self):
        self.f.mod("first", {SND: b"one", "data/other.bin": b"keep"})
        self.f.mod("second", {SND: b"two"})
        self.f.enable("first", "second")
        out, err = self.run_driver(["open " + SND, "open data/other.bin"])
        self.assertEqual(out, [digest(b"two"), digest(b"keep")])
        self.assertIn("second replaces data/sounds/test.wav over first", err)

    def test_duplicate_and_bad_ids_in_ce_ini(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable(raw="mods= good , good,../x,Good,a/b")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 1 -"])
        self.assertIn("listed twice", err)
        self.assertEqual(err.count("a mod id is"), 3)

    # ---- the fences --------------------------------------------------------------------------------------------------
    def test_every_fence_keeps_mods_off(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable("good")
        for name in FENCES:
            with self.subTest(name):
                out, err = self.run_driver(["active", "open " + SND], env={name: "1"})
                self.assertEqual(out, [f"ACTIVE 0 {name}", "NONE"])
                self.assertIn(f"off for this run ({name} is set): 1 enabled in ce.ini not loaded", err)

    def test_ce_layer_force_does_not_lift_a_fence(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable("good")
        out, _ = self.run_driver(["active", "open " + SND], env={"BEA_PORT_TAPE": "t", "BEA_PORT_CE_LAYER": "1"})
        self.assertEqual(out, ["ACTIVE 0 BEA_PORT_TAPE", "NONE"])

    def test_check_mode_stops_at_once_in_a_build_with_the_loader_off(self):
        # PlatformOff is the switch of Load and of the check mode: a default Windows build answers like this (status 3, its one line), and no file below the mods folder is opened.
        exe = self.exe or driver(defines=PLATFORM_OFF)
        self.f.mod("good", {SND: b"new"})
        folder = self.f.user / "mods" / "good"
        self.f.enable("good")
        folder.chmod(0)
        try:
            status, lines, err = self.f.check(exe=exe)
            self.assertEqual((status, lines, err), (3, ["off in this build"], ""))
            status, lines, err = self.f.check("good", exe=exe)
            self.assertEqual((status, lines, err), (3, ["off in this build"], ""))
        finally:
            folder.chmod(0o755)

    def test_empty_fence_variable_is_not_a_fence(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable("good")
        out, _ = self.run_driver(["active"], env={"BEA_PORT_TAPE": ""})
        self.assertEqual(out, ["ACTIVE 1 -"])

    def test_platform_off_build_refuses_listed_mods_and_touches_nothing(self):
        # A build with PORT_MODS_PLATFORM_OFF takes this branch (mods.cpp, Load): a listed mod is not activated, one line says why, and no
        # file below the mods folder is opened (the mod folder is made unreadable: a loader that went in would log a refusal).
        exe = self.exe or driver(defines=PLATFORM_OFF)
        self.f.mod("good", {SND: b"new"})
        folder = self.f.user / "mods" / "good"
        self.f.enable("good")
        folder.chmod(0)
        try:
            out, err = self.f.run(["active", "open " + SND, "open data/other.bin"], exe=exe)
            self.assertEqual(out, ["ACTIVE 0 -", "NONE", "NONE"], err)
            lines = [line for line in err.splitlines() if "mods:" in line]
            self.assertEqual(len(lines), 1, err)
            self.assertIn("off in this build", lines[0])
            self.assertNotIn("refused", err)
            # A fence is checked first, so a fenced run gives the fence's reason, not the platform's.
            out, err = self.f.run(["active"], exe=exe, env={"BEA_PORT_CLOCK": "1"})
            self.assertEqual(out, ["ACTIVE 0 BEA_PORT_CLOCK"], err)
            self.assertIn("off for this run (BEA_PORT_CLOCK is set): 1 enabled in ce.ini not loaded", err)
            self.assertNotIn("off in this build", err)
            # No mods line: silent on every platform.
            (self.f.user / "ce.ini").write_text("record_sessions=0\n")
            out, err = self.f.run(["active"], exe=exe)
            self.assertEqual(out, ["ACTIVE 0 -"], err)
            self.assertNotIn("mods:", err)
        finally:
            folder.chmod(0o755)

    # ---- paths -------------------------------------------------------------------------------------------------------
    def test_path_escapes_are_refused(self):
        bad = {"data/../../outside/x": "'.' or '..'", "../data/x": "'.' or '..'", "/data/x": "absolute",
               "data//x": "empty path component", "data\\sounds\\test.wav": "not allowed",
               "C:/data/x": "not allowed", "data/x:stream": "not allowed", "data/con.wav": "device name",
               "data/COM1": "device name", "data/aux .txt.wav": "device name", "data/x.": "ends with a space or a dot",
               "data/x ": "ends with a space", "sounds/test.wav": "outside data/", "data/./x": "'.' or '..'",
               "data/" + "a/" * 12 + "x": "too many", "data/" + "x" * 129: "over-long", "data/\u00e9": "not allowed",
               "data/sounds/TEST~1.WAV": "short name", "data/PROGRA~1/x": "short name", "data/x~12": "short name", "data/~1": "short name"}
        for path, words in bad.items():
            with self.subTest(path):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                d = self.f.user / "mods/bad"
                d.mkdir(parents=True)
                m = {"schema": 1, "id": "bad", "name": "b", "version": "1",
                     "files": [{"path": path, "size": 1, "sha256": "0" * 64}]}
                (d / "mod.json").write_text(json.dumps(m))
                self.f.enable("bad")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 0 -"])
                self.assertRefused(err, words)
                self.assertNotIn(str(self.f.tmp), err)

    def test_path_escapes_are_refused_but_a_tilde_alone_is_fine(self):
        # only the 8.3 alias shape (a '~' followed by a digit) is refused: a '~' elsewhere in a name is an ordinary character
        self.f.mod("tilde", {"data/sounds/a~b.wav": b"new"})
        (self.f.game / "data/sounds/a~b.wav").write_bytes(b"original")
        self.f.enable("tilde")
        out, err = self.run_driver(["active", "open data/sounds/a~b.wav"])
        self.assertEqual(out, ["ACTIVE 1 -", digest(b"new")], err)

    def test_file_the_game_lacks_is_refused(self):
        self.f.mod("adds", {"data/new.bin": b"x"})
        self.f.enable("adds")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "the game has no such file")

    def test_world_files_can_be_added_and_no_other_missing_file_can(self):
        # CWorld::Load reads data/worlds/worldNNN.wrd in place of the level archive's copy: the one name a mod may add.
        self.f.mod("w", {"data/worlds/world100.wrd": b"world body", "data/worlds/WORLD001.WRD": b"base body"})
        self.f.enable("w")
        out, err = self.run_driver(["active", "open data\\worlds\\world100.wrd", "open data/worlds/world001.wrd",
                                    "has data/worlds/world100.wrd", "has DATA\\Worlds\\world001.wrd",
                                    "has data/worlds/world002.wrd", "has data/other.bin"])
        self.assertEqual(out, ["ACTIVE 1 -", digest(b"world body"), digest(b"base body"), "HAS 1", "HAS 1", "HAS 0", "HAS 0"], err)
        self.assertIn("w adds data/worlds/world100.wrd", err)
        self.assertNotIn("w replaces data/worlds", err)

    def test_only_the_three_digit_world_name_may_be_added(self):
        for bad in ("data/worlds/world10.wrd", "data/worlds/world1000.wrd", "data/worlds/world100.wrd.bak",
                    "data/worlds/sub/world100.wrd", "data/worlds/world10a.wrd", "data/world100.wrd",
                    "data/worlds/world100.wr", "data/worlds/xworld100.wrd", "data/resources/world100.wrd"):
            with self.subTest(bad):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("w", {bad: b"x"})
                self.f.enable("w")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 0 -"], err)
                self.assertRefused(err, "the game has no such file")

    def test_world_file_size_is_bounded(self):
        for size, active in ((4 << 20, 1), ((4 << 20) + 1, 0)):
            with self.subTest(size=size):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("w", {"data/worlds/world100.wrd": b"w" * size})
                self.f.enable("w")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, [f"ACTIVE {active} -"], err)
                if not active:
                    self.assertRefused(err, "a world file larger than 4 MiB")

    # ---- level archives a mod may add -------------------------------------------------------------------------------
    def test_level_archive_can_be_added_for_a_level_the_game_lacks(self):
        # CResourceAccumulator::ReadResources opens data/resources/NNN_res_PC.aya for level NNN: the second name a mod may add.
        archive = level_archive()
        for name in (LEVEL_ARCHIVE, "data/resources/888_res_pc.aya", "DATA/Resources/888_RES_PC.AYA"):
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("duel", {name: archive})
                self.f.enable("duel")
                out, err = self.run_driver(["active", "open data\\Resources\\888_res_PC.aya", "has data/resources/888_res_PC.aya",
                                            "has data/resources/889_res_PC.aya", "has data/resources/857_res_PC.aya"])
                self.assertEqual(out, ["ACTIVE 1 -", digest(archive), "HAS 1", "HAS 0", "HAS 0"], err)
                self.assertIn(f"duel adds {name} ({len(archive)} bytes", err)
                self.assertIn("1 active, 0 game files replaced, 1 added (experimental)", err)
                self.assertNotIn("replaces data/resources", err.lower())
                self.assertIn("duel serves " + name + " to the game", err)

    def test_the_summary_counts_added_and_replaced_files_apart(self):
        # One replaced file and two added (a world and a level archive): the summary says which are which.
        self.f.mod("mix", {TEX: self.tex, LEVEL_ARCHIVE: level_archive(), "data/worlds/world100.wrd": world_body()})
        self.f.enable("mix")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 1 -"], err)
        self.assertIn("1 active, 1 game files replaced, 2 added (experimental)", err)
        self.assertIn("mix replaces " + TEX, err)
        self.assertIn("mix adds " + LEVEL_ARCHIVE, err)
        self.assertIn("mix adds data/worlds/world100.wrd", err)

    def test_only_the_three_digit_level_archive_name_in_the_multiplayer_range_may_be_added(self):
        refused = ["data/resources/849_res_PC.aya", "data/resources/900_res_PC.aya", "data/resources/100_res_PC.aya",
                   "data/resources/000_res_PC.aya", "data/resources/88_res_PC.aya", "data/resources/8888_res_PC.aya",
                   "data/resources/888_res_PC.aya.bak", "data/resources/888_res_PC.ayb", "data/resources/sub/888_res_PC.aya",
                   "data/888_res_PC.aya", "data/resources/888_res_XBOX.aya", "data/resources/88a_res_PC.aya",
                   "data/resources/goodie_88_res_PC.aya", "data/resources/base_res_PC.aya", "data/resources/Frontend_res_PC.aya",
                   "data/resources/888res_PC.aya", "data/resources/x888_res_PC.aya"]
        for name in refused:
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                digits = "".join(c for c in name.rsplit("/", 1)[-1][:3] if c.isdigit())
                self.f.mod("a", {name: level_archive(level=int(digits) if len(digits) == 3 else 888)})
                self.f.enable("a")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 0 -"], err)
                self.assertRefused(err, "the game has no such file")
        for level in (850, 851, 879, 880, 899):
            with self.subTest(level):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                name = f"data/resources/{level}_res_PC.aya"
                self.f.mod("a", {name: level_archive(level=level)})
                self.f.enable("a")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 1 -"], err)
                self.assertIn(f"a adds {name}", err)

    def test_a_level_archive_the_game_has_is_replaced_like_any_file(self):
        # An archive the game has stays a replacement under the rules every file has (block structure, size, hash): only the
        # archives a mod ADDS are walked, because there is no shipped copy to trust.
        (self.f.game / "data/resources").mkdir(parents=True, exist_ok=True)
        (self.f.game / LEVEL_ARCHIVE).write_bytes(b"original")
        plain = mods.pack(b"not a level archive")
        self.f.mod("r", {LEVEL_ARCHIVE: plain})
        self.f.enable("r")
        out, err = self.run_driver(["active", "open " + LEVEL_ARCHIVE])
        self.assertEqual(out, ["ACTIVE 1 -", digest(plain)], err)
        self.assertIn("r replaces " + LEVEL_ARCHIVE, err)
        self.assertNotIn("adds", err)

    def test_added_level_archive_structure_is_checked(self):
        w = level_wres
        no_bodies = lambda **kw: level_chunks(wres=w(bodies=b"", **kw))

        def edit(fn):
            c = level_chunks()
            fn(c)
            return level_stream(c)

        def with_tag(c, tag, payload):
            c[[t for t, _ in c].index(tag)] = (tag, payload)

        def wdat_only(level=888, base=-1):
            return chunk(b"WRLD", chunk(b"WDAT", struct.pack("<ii", base, level)))

        big = bytes(33 << 20)
        TEXT_SHORT = "a level archive TEXT chunk that is not a DXTX/CTEX texture record of 372 bytes or more"
        TEXT_FRAMES = "a level archive TEXT chunk with more than 16 frames, or a size that is not 360 bytes and 12 for each frame"
        MESH_WORDS = "a level archive MESH chunk that is not a PMSH record of 325 bytes or more whose PMS2 record fills it"
        MESH_CHAIN = "a level archive MESH chunk of a named mesh whose size is not 325 bytes and 317 for each chained mesh"
        cases = {
            "LVLR 104": (edit(lambda c: c.__setitem__(0, (b"LVLR", struct.pack("<I", 104)))), "does not start with LVLR 103"),
            "LVLR too long": (edit(lambda c: c.__setitem__(0, (b"LVLR", struct.pack("<II", 103, 0)))), "does not start with LVLR 103"),
            "first chunk is a texture": (edit(lambda c: c.insert(0, (b"TEXT", texture_record()))), "does not start with LVLR 103"),
            "TARG 2": (edit(lambda c: c.__setitem__(1, (b"TARG", struct.pack("<I", 2)))), "without TARG 1 second"),
            "no AYAD third": (edit(lambda c: c.__setitem__(2, (b"IMPS", bytes(24)))), "without its AYAD guards third"),
            "short AYAD": (edit(lambda c: c.__setitem__(2, (b"AYAD", bytes(20)))), "without its AYAD guards third"),
            "AYAD guard changed": (edit(lambda c: c.__setitem__(2, (b"AYAD", struct.pack("<6I", 344, 372, 320, 5084, 92, 1)))),
                                   "other structure sizes"),
            "unknown tag": (edit(lambda c: c.insert(3, (b"XXXX", b"abcd"))), "a tag a level archive does not hold"),
            "a tag only the base archive holds": (edit(lambda c: c.insert(3, (b"PLAT", bytes(16)))), "a tag a level archive does not hold"),
            "TEXT is not a texture": (edit(lambda c: with_tag(c, b"TEXT", b"TGA!" + bytes(40))), "not a DXTX/CTEX texture record"),
            "TEXT wrapper length": (edit(lambda c: with_tag(c, b"TEXT", b"DXTX" + struct.pack("<I", 5) + b"CTEX" + bytes(20))),
                                    "not a DXTX/CTEX texture record"),
            "TEXT without CTEX": (edit(lambda c: with_tag(c, b"TEXT", b"DXTX" + struct.pack("<I", 24) + b"XXXX" + bytes(20))),
                                  "not a DXTX/CTEX texture record"),
            "TEXT too short": (edit(lambda c: with_tag(c, b"TEXT", b"DXTX" + struct.pack("<I", 4) + b"CTEX")),
                               "not a DXTX/CTEX texture record"),
            # the game reads a TEXT as DXTX, CTEX and 344 bytes whatever the headers say, so a shorter one is read into the next chunk
            "TEXT of 25 bytes": (edit(lambda c: with_tag(c, b"TEXT", sized_texture(25))),
                                 TEXT_SHORT),
            "TEXT of 344 bytes with a PLAT chunk inside": (edit(lambda c: with_tag(c, b"TEXT", b"DXTX" + struct.pack("<I", 336) + b"CTEX"
                                                                                  + struct.pack("<I", 0) + chunk(b"PLAT", bytes(8))
                                                                                  + bytes(344 - 36))),
                                                           TEXT_SHORT),
            "TEXT of 359 bytes": (edit(lambda c: with_tag(c, b"TEXT", sized_texture(359))),
                                  TEXT_SHORT),
            # a record that is long enough and has its frames right, with one of the three framing fields wrong: the framing is what these decide
            "TEXT of 372 bytes that is not DXTX": (edit(lambda c: with_tag(c, b"TEXT", b"XXXX" + sized_texture(372)[4:])), TEXT_SHORT),
            "TEXT of 372 bytes with a wrapper size that does not cover it": (edit(lambda c: with_tag(
                c, b"TEXT", sized_texture(372)[:4] + struct.pack("<I", 363) + sized_texture(372)[8:])), TEXT_SHORT),
            "TEXT of 372 bytes with a wrapper size that is too long": (edit(lambda c: with_tag(
                c, b"TEXT", sized_texture(372)[:4] + struct.pack("<I", 365) + sized_texture(372)[8:])), TEXT_SHORT),
            "TEXT of 372 bytes without CTEX": (edit(lambda c: with_tag(c, b"TEXT", sized_texture(372)[:8] + b"XXXX" + sized_texture(372)[12:])), TEXT_SHORT),
            "TEXT of 360 bytes, no frame": (edit(lambda c: with_tag(c, b"TEXT", sized_texture(360, 0))), TEXT_SHORT),
            "TEXT of 371 bytes": (edit(lambda c: with_tag(c, b"TEXT", sized_texture(371))), TEXT_SHORT),
            "TEXT of 373 bytes for one frame": (edit(lambda c: with_tag(c, b"TEXT", sized_texture(373))), TEXT_FRAMES),
            "TEXT of 372 bytes for two frames": (edit(lambda c: with_tag(c, b"TEXT", sized_texture(372, 2))), TEXT_FRAMES),
            "TEXT of 372 bytes for no frame": (edit(lambda c: with_tag(c, b"TEXT", sized_texture(372, 0))), TEXT_FRAMES),
            "TEXT of 384 bytes for one frame": (edit(lambda c: with_tag(c, b"TEXT", sized_texture(384, 1))), TEXT_FRAMES),
            "TEXT of 564 bytes for 17 frames": (edit(lambda c: with_tag(c, b"TEXT", sized_texture(564, 17))), TEXT_FRAMES),
            "TEXT of 996 bytes for 53 frames": (edit(lambda c: with_tag(c, b"TEXT", sized_texture(996, 53))), TEXT_FRAMES),
            "TEXT with a frame count of 2^32-1": (edit(lambda c: with_tag(c, b"TEXT", sized_texture(372, 0xFFFFFFFF))), TEXT_FRAMES),
            "TEXT with a PLAT chunk after the record": (edit(lambda c: with_tag(c, b"TEXT", b"DXTX" + struct.pack("<I", 424) + b"CTEX"
                                                                                + struct.pack("<I", 344) + sized_texture(372)[16:360]
                                                                                + chunk(b"PLAT", bytes(64)))), TEXT_FRAMES),
            "a second LVLR": (edit(lambda c: c.insert(3, (b"LVLR", b""))),
                              "a level archive that repeats LVLR, TARG or AYAD after its first three chunks"),
            "a second TARG": (edit(lambda c: c.insert(3, (b"TARG", b""))),
                              "a level archive that repeats LVLR, TARG or AYAD after its first three chunks"),
            "a second AYAD": (edit(lambda c: c.insert(3, (b"AYAD", b""))),
                              "a level archive that repeats LVLR, TARG or AYAD after its first three chunks"),
            "a second AYAD late": (edit(lambda c: c.insert(len(c) - 1, (b"AYAD", struct.pack("<6I", *GUARDS)))),
                                   "a level archive that repeats LVLR, TARG or AYAD after its first three chunks"),
            "MESH is not a mesh": (edit(lambda c: with_tag(c, b"MESH", b"OBJ!" + bytes(20))), "not a PMSH record"),
            "MESH wrapper length": (edit(lambda c: with_tag(c, b"MESH", b"PMSH" + struct.pack("<I", 3) + bytes(24))),
                                    "not a PMSH record"),
            # CMesh::Deserialize reads 324 bytes before any branch and 325 when it keeps the mesh; a skipped mesh is skipped by PMS2's size
            "MESH of 325 bytes that is not PMSH": (edit(lambda c: with_tag(c, b"MESH", b"XXXX" + mesh_record(325)[4:])), MESH_WORDS),
            "MESH of 8 bytes": (edit(lambda c: with_tag(c, b"MESH", b"PMSH" + struct.pack("<I", 0))), MESH_WORDS),
            "MESH of 9 bytes": (edit(lambda c: with_tag(c, b"MESH", b"PMSH" + struct.pack("<I", 1) + b"x")), MESH_WORDS),
            "MESH of 323 bytes": (edit(lambda c: with_tag(c, b"MESH", mesh_record(323))), MESH_WORDS),
            "MESH of 324 bytes": (edit(lambda c: with_tag(c, b"MESH", mesh_record(324))), MESH_WORDS),
            "MESH whose PMSH does not cover it": (edit(lambda c: with_tag(c, b"MESH", mesh_record(325)[:4] + struct.pack("<I", 316) + mesh_record(325)[8:])),
                                                  MESH_WORDS),
            "MESH whose second header is not PMS2": (edit(lambda c: with_tag(c, b"MESH", mesh_record(325)[:8] + b"PMS3" + mesh_record(325)[12:])),
                                                     MESH_WORDS),
            "MESH whose PMS2 is one byte short": (edit(lambda c: with_tag(c, b"MESH", mesh_record(325)[:12] + struct.pack("<I", 308) + mesh_record(325)[16:])),
                                                  MESH_WORDS),
            "MESH whose PMS2 is one byte long": (edit(lambda c: with_tag(c, b"MESH", mesh_record(325)[:12] + struct.pack("<I", 310) + mesh_record(325)[16:])),
                                                 MESH_WORDS),
            "MESH whose PMS2 is empty": (edit(lambda c: with_tag(c, b"MESH", mesh_record(325)[:12] + struct.pack("<I", 0) + mesh_record(325)[16:])),
                                         MESH_WORDS),
            "a MESH of 642 bytes whose PMS2 fills only the first 325": (edit(lambda c: with_tag(c, b"MESH", mesh_record(642)[:12] + struct.pack("<I", 309) + mesh_record(642)[16:])),
                                                                         MESH_WORDS),
            "no WRES": (edit(lambda c: c.pop()), "without exactly one WRES"),
            "two WRES": (edit(lambda c: c.append(c[-1])), "without exactly one WRES"),
            # A named mesh keeps its data in its own file: its chunk is 325 bytes and 317 for each chained mesh, and nothing between.
            "a named MESH of 326 bytes": (edit(lambda c: with_tag(c, b"MESH", mesh_record(326))), MESH_CHAIN),
            "a named MESH of 641 bytes": (edit(lambda c: with_tag(c, b"MESH", mesh_record(325 + 316))), MESH_CHAIN),
            "a named MESH of 643 bytes": (edit(lambda c: with_tag(c, b"MESH", mesh_record(325 + 318))), MESH_CHAIN),
            "a named MESH of 1909 bytes": (edit(lambda c: with_tag(c, b"MESH", mesh_record(325 + 5 * 317 - 1))), MESH_CHAIN),
            "a named MESH of 1911 bytes": (edit(lambda c: with_tag(c, b"MESH", mesh_record(325 + 5 * 317 + 1))), MESH_CHAIN),
            "a named MESH of 355373 bytes": (edit(lambda c: with_tag(c, b"MESH", mesh_record(355373))), MESH_CHAIN),
            "a MESH named by a single byte of 1": (edit(lambda c: with_tag(c, b"MESH", named_mesh(326, b"\x01"))), MESH_CHAIN),
            "a MESH named by a single byte of 255": (edit(lambda c: with_tag(c, b"MESH", named_mesh(326, b"\xff"))), MESH_CHAIN),
            "the second of two MESH chunks is a named one of 643 bytes": (edit(lambda c: c.__setitem__(
                [i for i, (t, _) in enumerate(c) if t == b"MESH"][1], (b"MESH", mesh_record(642 + 1)))), MESH_CHAIN),
            "a chunk after WRES": (edit(lambda c: c.append((b"TEXT", texture_record()))), "a chunk after its WRES"),
            "WRES holds no WRLD": (edit(lambda c: with_tag(c, b"WRES", chunk(b"XXXX", bytes(40)))), "does not hold one WRLD"),
            "WRLD length wrong": (edit(lambda c: with_tag(c, b"WRES", b"WRLD" + struct.pack("<I", 3) + bytes(40))),
                                  "does not hold one WRLD"),
            "WRES too short": (edit(lambda c: with_tag(c, b"WRES", b"WRLD")), "does not hold one WRLD"),
            "a world chunk past its end": (edit(lambda c: with_tag(c, b"WRES", chunk(
                b"WRLD", chunk(b"WDAT", struct.pack("<ii", -1, 888)) + b"RLWD" + struct.pack("<I", 1000) + world_body()))),
                                           "runs past its end"),
            "no WDAT": (edit(lambda c: with_tag(c, b"WRES", chunk(b"WRLD", chunk(b"RLWD", world_body())))),
                        "a level archive WRLD that does not start with WDAT"),
            "an empty WRLD": (edit(lambda c: with_tag(c, b"WRES", chunk(b"WRLD", b""))), "a level archive WRLD without WDAT"),
            "no real world": (edit(lambda c: with_tag(c, b"WRES", wdat_only())), "a level archive WRLD without a real world body (RLWD)"),
            "RLWD before WDAT": (edit(lambda c: with_tag(c, b"WRES", chunk(b"WRLD", chunk(b"RLWD", world_body())
                                                                           + chunk(b"WDAT", struct.pack("<ii", -1, 888))))),
                                 "a level archive WRLD that does not start with WDAT"),
            "RLWD and BSWD swapped": (edit(lambda c: with_tag(c, b"WRES", w(base=10, bodies=chunk(b"RLWD", world_body())
                                                                             + chunk(b"BSWD", world_body())))),
                                      "a level archive WRLD whose BSWD does not follow its WDAT"),
            "two real worlds": (edit(lambda c: with_tag(c, b"WRES", w(bodies=chunk(b"RLWD", world_body()) * 2))),
                                "a level archive WRLD with chunks after its real world body"),
            "two base worlds": (edit(lambda c: with_tag(c, b"WRES", w(base=10, bodies=chunk(b"BSWD", world_body()) * 2
                                                                      + chunk(b"RLWD", world_body())))),
                                "a level archive WRLD whose RLWD does not follow its WDAT and base world"),
            "a base world and no real world": (edit(lambda c: with_tag(c, b"WRES", w(base=10, bodies=chunk(b"BSWD", world_body())))),
                                               "a level archive WRLD without a real world body (RLWD)"),
            "a base level and an empty WRLD tail": (edit(lambda c: with_tag(c, b"WRES", w(base=10, bodies=b""))),
                                                    "a level archive whose WDAT names a base world but whose WRLD has no BSWD"),
            # the size boundaries: one chunk header or WDAT that is a little off must not be read past the end of what is declared
            "a last chunk cut by one byte": (level_stream(level_chunks())[:-1], "a level archive chunk that runs past the end of the stream"),
            "a header that declares one byte more than follows": (level_stream(level_chunks()) + b"ERES" + struct.pack("<I", 1),
                                                                  "a level archive chunk that runs past the end of the stream"),
            "a header that declares eight bytes more than follow": (level_stream(level_chunks()) + b"ERES" + struct.pack("<I", 8),
                                                                    "a level archive chunk that runs past the end of the stream"),
            "an RLWD that declares one byte more than WRLD holds": (edit(lambda c: with_tag(c, b"WRES", chunk(
                b"WRLD", chunk(b"WDAT", struct.pack("<ii", -1, 888)) + b"RLWD" + struct.pack("<I", len(world_body()) + 1) + world_body()))),
                                                                    "a level archive WRLD with a chunk that runs past its end"),
            "an RLWD that declares eight bytes more than WRLD holds": (edit(lambda c: with_tag(c, b"WRES", chunk(
                b"WRLD", chunk(b"WDAT", struct.pack("<ii", -1, 888)) + b"RLWD" + struct.pack("<I", len(world_body()) + 8) + world_body()))),
                                                                       "a level archive WRLD with a chunk that runs past its end"),
            "a BSWD that declares one byte more than WRLD holds": (edit(lambda c: with_tag(c, b"WRES", chunk(
                b"WRLD", chunk(b"WDAT", struct.pack("<ii", 10, 888)) + b"BSWD" + struct.pack("<I", len(world_body()) + 1) + world_body()))),
                                                                   "a level archive WRLD with a chunk that runs past its end"),
            "a WDAT of 12 bytes": (edit(lambda c: with_tag(c, b"WRES", chunk(b"WRLD", chunk(b"WDAT", struct.pack("<iii", -1, 888, 0))
                                                                              + chunk(b"RLWD", world_body())))),
                                   "a level archive WRLD whose WDAT is not 8 bytes"),
            "a WDAT of 4 bytes": (edit(lambda c: with_tag(c, b"WRES", chunk(b"WRLD", chunk(b"WDAT", struct.pack("<i", -1))
                                                                             + chunk(b"RLWD", world_body())))),
                                  "a level archive WRLD whose WDAT is not 8 bytes"),
            "world version 42": (edit(lambda c: with_tag(c, b"WRES", w(bodies=chunk(b"RLWD", world_body(42))))),
                                 "a version CWorld::LoadWorld refuses"),
            "world version 51": (edit(lambda c: with_tag(c, b"WRES", w(bodies=chunk(b"RLWD", world_body(51))))),
                                 "a version CWorld::LoadWorld refuses"),
            "world body over 4 MiB": (edit(lambda c: with_tag(c, b"WRES", w(bodies=chunk(b"RLWD", world_body(50, (4 << 20) + 1))))),
                                      "a level archive world body of an unusable size (2 bytes to 4 MiB)"),
            "base world body over 4 MiB": (edit(lambda c: with_tag(c, b"WRES", w(base=10, bodies=chunk(b"BSWD", world_body(50, (4 << 20) + 1))
                                                                                  + chunk(b"RLWD", world_body())))),
                                           "a level archive world body of an unusable size (2 bytes to 4 MiB)"),
            "world body of one byte": (edit(lambda c: with_tag(c, b"WRES", w(bodies=chunk(b"RLWD", b"2")))),
                                       "a level archive world body of an unusable size (2 bytes to 4 MiB)"),
            "another chunk in WRLD": (edit(lambda c: with_tag(c, b"WRES", w(bodies=chunk(b"RLWD", world_body()) + chunk(b"JUNK", b"")))),
                                      "a level archive WRLD with chunks after its real world body"),
            "WDAT of another level": (edit(lambda c: with_tag(c, b"WRES", level_wres(level=887))), "another level's number"),
            "a base level without its body": (edit(lambda c: with_tag(c, b"WRES", w(base=10, bodies=chunk(b"RLWD", world_body())))),
                                              "a level archive WRLD whose BSWD does not follow its WDAT"),
            "a base body with no base level": (edit(lambda c: with_tag(c, b"WRES", w(bodies=chunk(b"BSWD", world_body())
                                                                                      + chunk(b"RLWD", world_body())))),
                                               "a level archive WRLD whose RLWD does not follow its WDAT and base world"),
            "a base level that is no level": (edit(lambda c: with_tag(c, b"WRES", level_wres(base=5000))), "is not a level number"),
            "more than 2048 chunks": (edit(lambda c: c.__setitem__(slice(3, 3), [(b"LNDS", b"")] * 2046)), "more than 2048 chunks"),
            "a chunk past the end": (level_stream(level_chunks()) + b"ERES" + struct.pack("<I", 100), "past the end of the stream"),
            "more than 32 MiB unpacked": (edit(lambda c: with_tag(c, b"ERES", big)), "more than 32 MiB"),
        }
        # Fewer than a header's eight bytes left, at the end of the stream and inside WRLD: every length is refused, and the
        # four to seven that a header's size word would reach into are the ones a loose bound lets through.
        for extra in range(1, 8):
            cases[f"{extra} bytes after the last chunk"] = (level_stream(level_chunks()) + b"abcdefg"[:extra],
                                                            "a level archive with a cut chunk header")
            cases[f"{extra} bytes after WDAT in WRLD"] = (edit(lambda c: with_tag(c, b"WRES", chunk(
                b"WRLD", chunk(b"WDAT", struct.pack("<ii", -1, 888)) + b"abcdefg"[:extra]))),
                                                          "a level archive WRLD with a chunk that runs past its end")
            cases[f"{extra} bytes after BSWD in WRLD"] = (edit(lambda c: with_tag(c, b"WRES", chunk(
                b"WRLD", chunk(b"WDAT", struct.pack("<ii", 10, 888)) + chunk(b"BSWD", world_body()) + b"abcdefg"[:extra]))),
                                                          "a level archive WRLD with a chunk that runs past its end")
            cases[f"{extra} bytes after RLWD in WRLD"] = (edit(lambda c: with_tag(c, b"WRES", chunk(
                b"WRLD", chunk(b"WDAT", struct.pack("<ii", -1, 888)) + chunk(b"RLWD", world_body()) + b"abcdefg"[:extra]))),
                                                          "a level archive WRLD with chunks after its real world body")
        for name, (stream, words) in cases.items():
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("a", {LEVEL_ARCHIVE: mods.pack(stream)})
                self.f.enable("a")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 0 -"], err)
                self.assertRefused(err, words)
        # The packed file's own rules still apply first, and its size is bounded before it is read.
        for name, (data, words) in {"not packed at all": (b"abc", "a cut block header"),
                                    "over 8 MiB packed": (mods.pack(os.urandom(9 << 20)), "larger than 8 MiB")}.items():
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("a", {LEVEL_ARCHIVE: data})
                self.f.enable("a")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 0 -"], err)
                self.assertRefused(err, words)

    def test_a_short_texture_is_refused_without_reading_past_the_stream(self):
        # A TEXT's frame count is read only once the record's 372 bytes are known to be there. The verdict is the same either way (the
        # short record is refused), so only a build with the sanitizers shows a count read first: it would run 261 bytes past the
        # end of a 131-byte stream (a 25-byte TEXT, then the smallest world chunk).
        chunks = level_chunks()[:3] + [(b"TEXT", sized_texture(25)), (b"WRES", level_wres(bodies=chunk(b"RLWD", world_body(50, 2))))]
        stream = level_stream(chunks)
        self.assertEqual(131, len(stream))
        self.f.mod("a", {LEVEL_ARCHIVE: mods.pack(stream)})
        self.f.enable("a")
        out, err = self.f.run(["active"], exe=driver(self.source, sanitize=True),
                              env={"ASAN_OPTIONS": "detect_leaks=0:exitcode=86", "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
        self.assertNotIn("Sanitizer", err)
        self.assertEqual(out, ["ACTIVE 0 -"], err)
        self.assertRefused(err, "a level archive TEXT chunk that is not a DXTX/CTEX texture record of 372 bytes or more")

    def test_added_level_archive_limits_are_inclusive(self):
        # A base world, 4 MiB world bodies, texture records of 1 to 16 frames, mesh records of 325 bytes and up and exactly 2048 chunks are
        # what the limits allow; one more of any is refused above.
        w = level_wres
        cases = {
            "base world": level_chunks(base=10),
            "texture records of 16 frames (552 bytes)": [(t, texture_record(frames=16) if t == b"TEXT" else p) for t, p in level_chunks()],
            "texture records of 4 frames (408 bytes), as 19 shipped ones are": [(t, texture_record(frames=4) if t == b"TEXT" else p) for t, p in level_chunks()],
            "named mesh records of 642 and 1910 bytes (chains of 1 and 5, as shipped ones are)": [
                (t, mesh_record(1910 if i % 2 else 642) if t == b"MESH" else p) for i, (t, p) in enumerate(level_chunks())],
            "a named mesh record of 959 bytes (a chain of 2)": [(t, mesh_record(325 + 2 * 317) if t == b"MESH" else p) for t, p in level_chunks()],
            "one inline mesh among named ones (its data is in the chunk, and is walked)": [
                (t, inline_mesh() if t == b"MESH" and i == 3 + 3 else p) for i, (t, p) in enumerate(level_chunks())],
            "world body of 4 MiB": level_chunks(wres=w(bodies=chunk(b"RLWD", world_body(50, 4 << 20)))),
            "base and real world bodies of 4 MiB": level_chunks(wres=w(base=10, bodies=chunk(b"BSWD", world_body(50, 4 << 20))
                                                                      + chunk(b"RLWD", world_body(50, 4 << 20)))),
            "world bodies of 2 bytes": level_chunks(wres=w(base=10, bodies=chunk(b"BSWD", world_body(43, 2)) + chunk(b"RLWD", world_body(43, 2)))),
            "2048 chunks": level_chunks()[:3] + [(b"LNDS", b"")] * (2048 - len(level_chunks())) + level_chunks()[3:],
            "30 MiB unpacked": [(t, bytes(30 << 20) if t == b"ERES" else p) for t, p in level_chunks()],
        }
        for name, chunks in cases.items():
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("a", {LEVEL_ARCHIVE: level_archive(chunks)})
                self.f.enable("a")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 1 -"], err)


    def inline_archive(self, *meshes):
        """The authored level archive with its first MESH chunks replaced by `meshes` (payloads) and the rest kept."""
        chunks = level_chunks()
        at = [n for n, (tag, _) in enumerate(chunks) if tag == b"MESH"]
        for n, mesh in zip(at, meshes):
            chunks[n] = (b"MESH", mesh)
        return chunks

    def verdict(self, chunks):
        shutil.rmtree(self.f.user / "mods", ignore_errors=True)
        self.f.mod("a", {LEVEL_ARCHIVE: level_archive(chunks)})
        self.f.enable("a")
        return self.run_driver(["active"])

    def test_one_inline_mesh_of_the_profile_is_admitted(self):
        # The 20000-to-29999 runtime id, an all-zero name, no optional word and a stream CheckInlineMesh walks; the rest of the archive is the
        # usual one. These are the shapes a converted PS2 mesh has (children, parents, references, shared pieces, a collision grid whose
        # unlisted tail is as the PS2 exporter left it, emitters with part indices, animation modes).
        two = inline_stream(textures=2, tex_name=b"two.tga", parts=3, part_list=[
            inline_part(0, 3, 2, children=[1, 2]), inline_part(1, 3, 2, parent=0, reference=2), inline_part(2, 3, 2, parent=0, pieces=[])])
        emitter = bytearray(336)
        struct.pack_into("<I", emitter, 0x40, 1)
        cemt = chunk(b"CEMT", struct.pack("<i", 336) + bytes(emitter) + struct.pack("<I", 0) + bytes(336))
        cases = {
            "the smallest": inline_mesh(),
            "kept flag 1, last id": inline_mesh(runtime_id=29999, kept=1),
            "three parts with children, parents and a reference, two textures": inline_mesh(two),
            "a part without vertices (its CMVB holds no piece)": inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, pieces=[]), inline_part(1, 2, 1)]),
            "two pieces sharing the first's vertices, the second with a slot of its own": inline_mesh(part=dict(pieces=[dict(), dict(texr=[-1, 0, -1, -1, -1, -1])])),
            "12 pieces": inline_mesh(part=dict(pieces=[dict() for _ in range(12)])),
            "an animation mode": inline_mesh(anim=2),
            "two emitters, one with a part index": inline_mesh(emitters=2, cemt=cemt),
            "16 textures of 16 frames": inline_mesh(textures=16, frames=[16] * 16),
            "32 textures, the most": inline_mesh(textures=32),
            "32 animation modes, the most": inline_mesh(anim=32),
            # skeletal parts, as the shipped ones are: a stale bones pointer, a BONE of part numbers, 48-byte vertices that name the bones
            "a skeletal part of one bone": skeletal_mesh(bones=1),
            "a skeletal part of 19 bones, the most": skeletal_mesh(bones=19),
            "vertices that name each of 19 bones in each slot": skeletal_mesh(bones=19, rows=[(3.0 * i, 3.0 * ((i + 1) % 19), 3.0 * ((i + 2) % 19)) for i in range(19)]),
            "a skeletal part with no bones pointer in the file": skeletal_mesh(words={0xD0: 0}),
            "a skeletal part of 12 pieces sharing its vertices": skeletal_mesh(pieces=[dict(vbuf=bytes(48 * 4))] + [dict() for _ in range(11)]),
            "one bone that is the last part of the mesh": inline_mesh(parts=4, part_list=[inline_part(i, 4, 1, pieces=[]) for i in range(2)] + [inline_part(2, 4, 1, bones=[3]), inline_part(3, 4, 1, pieces=[])]),
            "a skeletal part whose bones name a part twice, itself and a later part": inline_mesh(parts=3, part_list=[inline_part(0, 3, 1, pieces=[]), inline_part(1, 3, 1, bones=[0, 0, 1, 2]), inline_part(2, 3, 1, pieces=[])]),
            "a skeletal part with a collision grid, a cache and children": inline_mesh(parts=3, part_list=[inline_part(0, 3, 1, children=[1, 2], pieces=[]), inline_part(1, 3, 1, parent=0, bones=[0], grid=inline_grid(), cached=(1, 16, 48)),
                                                                                           inline_part(2, 3, 1, parent=0, pieces=[])]),
            "a chained mesh with a skeletal part": inline_mesh(inline_chain(2, last=skeletal_mesh(bones=2)[16 + 309:])),
            # camera parts, as the shipped ones are: a field of view for each hand frame and no vertices
            "a camera part": camera_mesh(),
            "a camera part with a field of view for each of 3 hand frames": camera_mesh(fovs=(90.0, 75.0, 60.0), vframes=4, vhfm=bytes([0, 1, 2, 1])),
            "a camera part with a stale pointer in the slot the reader replaces": camera_mesh(words={0xC4: STALE}),
            "a chained mesh with a camera part": inline_mesh(inline_chain(2, last=camera_mesh()[16 + 309:])),
            "a mesh with a skeletal part and a camera part": inline_mesh(parts=3, part_list=[inline_part(0, 3, 1, pieces=[]), inline_part(1, 3, 1, bones=[0]), inline_part(2, 3, 1, fovs=[90.0])]),
            # the cache as CMeshPart::BuildCache leaves it: a count of 1 or the virtual-frame count, a CPOS and a CORI unless the position or the orientation is static,
            # and a static flag only on a part with a parent
            "cached positions and orientations for 2 frames": inline_mesh(part=dict(vframes=2, cached=(2, 32, 96))),
            "a cached frame for each of 3 virtual frames": inline_mesh(part=dict(vframes=3, cached=(3, 48, 144))),
            "one cached frame for 3 virtual frames": inline_mesh(part=dict(vframes=3)),
            "a listed part whose orientation is static, without a CORI": inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, children=[1]),
                                                                                                         inline_part(1, 2, 1, parent=0, static=(0, 1), cached=(1, 16, None))]),
            "a listed part whose position and orientation are static, without a CPOS or CORI": inline_mesh(parts=2, part_list=[
                inline_part(0, 2, 1, children=[1]), inline_part(1, 2, 1, parent=0, static=(1, 1), cached=(1, None, None))]),
            "a listed part whose position only is static, without a CPOS (no shipped part is)": inline_mesh(parts=2, part_list=[
                inline_part(0, 2, 1, children=[1]), inline_part(1, 2, 1, parent=0, static=(1, 0), cached=(1, None, 48))]),
            "static flags of 2^32 - 1 on a listed part (read as set, as the reader reads them)": inline_mesh(parts=2, part_list=[
                inline_part(0, 2, 1, children=[1]), inline_part(1, 2, 1, parent=0, static=(0xFFFFFFFF, 0xFFFFFFFF), cached=(1, None, None))]),
            "static parts below static parts, up to a part that is not static": inline_mesh(parts=4, part_list=[inline_part(0, 4, 1, children=[1])]
                                                                                        + [inline_part(i, 4, 1, parent=i - 1, children=[i + 1] if i < 3 else [], static=(1, 1),
                                                                                                       cached=(1, None, None)) for i in range(1, 4)]),
            "a static part whose parent has a cached frame for each of its 3 virtual frames": inline_mesh(parts=2, part_list=[
                inline_part(0, 2, 1, children=[1], vframes=3, cached=(3, 48, 144)), inline_part(1, 2, 1, parent=0, static=(1, 1), cached=(1, None, None))]),
            # a part's own count is 1 or its own virtual-frame count whichever of its flags are set, and not the count of a part before it
            # (test_the_cache_rules_hold_for_every_combination_of_static_flags_words_counts_and_links runs the whole product)
            "a listed part whose position only is static, with a cached frame for each of its 3 virtual frames": cache_mesh(static=(1, 0), vframes=3, cached=(3, None, 144)),
            "a listed part whose orientation only is static, with a cached frame for each of its 3 virtual frames": cache_mesh(static=(0, 1), vframes=3, cached=(3, 48, None)),
            "a listed part whose position and orientation are static, with 3 cached frames for 3 virtual frames": cache_mesh(static=(1, 1), vframes=3, cached=(3, None, None)),
            "a part of 3 virtual frames and 3 cached frames after a part of 1 virtual frame": cache_mesh(listed=False, vframes=3, cached=(3, 48, 144)),
            "a skeletal part whose bones are static parts": inline_mesh(parts=4, part_list=[inline_part(0, 4, 1, children=[1, 2, 3], pieces=[])]
                                                                        + [inline_part(i, 4, 1, parent=0, static=(1, 1), cached=(1, None, None), pieces=[]) for i in (1, 2)]
                                                                        + [inline_part(3, 4, 1, parent=0, bones=[1, 2], pieces=[dict(vbuf=skinned_vertices([(0.0, 3.0, 0.0)] * 4))])]),
            "a static part in a chained mesh": inline_mesh(inline_chain(2, last=inline_stream(parts=2, part_list=[
                inline_part(0, 2, 1, children=[1]), inline_part(1, 2, 1, parent=0, static=(1, 1), cached=(1, None, None))]))),
            "a virtual-frame map that names each of 3 hand frames": inline_mesh(part=dict(vframes=4, hframes=3, vhfm=bytes([0, 1, 2, 1]))),
            "a part with both a child and a parent": inline_mesh(parts=3, part_list=[inline_part(0, 3, 1, children=[1]), inline_part(1, 3, 1, parent=0, children=[2]), inline_part(2, 3, 1, parent=1)]),
            "the highest child, two parts after the part that lists it, and a part between them that no part lists": inline_mesh(parts=3, part_list=[
                inline_part(0, 3, 1, children=[2]), inline_part(1, 3, 1), inline_part(2, 3, 1, parent=0)]),
            "valid later second and third children": inline_mesh(parts=6, part_list=[inline_part(0, 6, 1), inline_part(1, 6, 1),
                                                inline_part(2, 6, 1, children=[3, 4, 5]), inline_part(3, 6, 1, parent=2),
                                                inline_part(4, 6, 1, parent=2), inline_part(5, 6, 1, parent=2)]),
            "a tree of seven parts, each listed once as a child": inline_mesh(parts=7, part_list=[inline_part(0, 7, 1, children=[1, 2]), inline_part(1, 7, 1, parent=0, children=[3, 4]),
                                                                                 inline_part(2, 7, 1, parent=0, children=[5, 6])] + [inline_part(i, 7, 1, parent=(i - 1) // 2) for i in range(3, 7)]),
            "a part with 63 children, each listed once": inline_mesh(parts=64, part_list=[inline_part(0, 64, 1, children=list(range(1, 64)), pieces=[])]
                                                         + [inline_part(i, 64, 1, parent=0, pieces=[]) for i in range(1, 64)]),
            "a chain of meshes whose parts list children": inline_mesh(inline_chain(2, parts=2, part_list=[inline_part(0, 2, 1, children=[1]), inline_part(1, 2, 1, parent=0)])),
            "a reference part that names a normal part numbered 256 or more": inline_mesh(parts=258, part_list=[inline_part(0, 258, 1, reference=257, pieces=[]), inline_part(1, 258, 1, words={0x8C: 2}, pieces=[])]
                                                                                   + [inline_part(i, 258, 1, pieces=[]) for i in range(2, 258)]),
            "a reference part that names a later normal part": inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, reference=1, pieces=[]), inline_part(1, 2, 1)]),
            "a reference part that names an earlier normal part": inline_mesh(parts=2, part_list=[inline_part(0, 2, 1), inline_part(1, 2, 1, reference=0, pieces=[])]),
            "a collision grid": inline_mesh(part=dict(grid=inline_grid())),
            "a collision grid with garbage in the triangles no list names": inline_mesh(part=dict(grid=inline_grid(
                triangles=3, lists={0: [0, 1]}, tri_vertices=[(0, 1, 2), (1, 2, 3), (60000, 61000, 62000)]))),
            "a collision grid whose CPBT carries another triangle count (the PS2 exporter's)": inline_mesh(part=dict(grid=inline_grid(cpbt={0x78: 24892}))),
            "a collision grid of absent cells": inline_mesh(part=dict(grid=inline_grid(lists={}, triangles=0))),
            "a valid reference in the final part of a maximum-sized mesh": inline_mesh(parts=512,
                                                part_list=[inline_part(i, 512, 1, pieces=[]) for i in range(511)]
                                                + [inline_part(511, 512, 1, reference=510, pieces=[])]),
            "512 parts": inline_mesh(parts=512, part_list=[inline_part(i, 512, 1, pieces=[]) for i in range(512)]),
            "100000 vertices in all": inline_mesh(parts=2, part_list=[inline_part(i, 2, 1, pieces=[dict(vertex_count=50000)]) for i in range(2)]),
            "600000 indices in all": inline_mesh(parts=10, part_list=[inline_part(i, 10, 1, pieces=[dict(indices=[j % 4 for j in range(60000)])]) for i in range(10)]),
            "1024 pieces in all": inline_mesh(parts=86, part_list=[inline_part(i, 86, 1, pieces=[dict() for _ in range(12 if i < 85 else 4)]) for i in range(86)]),
            # the exporter dumped live objects, so the shipped parts carry stale pointers in every slot the reader replaces
            "stale pointers in the slots the reader replaces": inline_mesh(part=dict(
                words={0x80: STALE, 0x84: STALE, 0x94: STALE, 0xC4: STALE, 0xFC: STALE, 0x128: STALE, 0x134: STALE, 0x138: STALE},
                cmvb={0x08: STALE, 0x10C: STALE, 0x124: STALE})),
            "no position sets": inline_mesh(part=dict(words={0xB4: 0})),
            "two position sets (the most a shipped part has)": inline_mesh(part=dict(words={0xB4: 2})),
            # meshes chained to the first, framed as the shipped chains are (5 meshes each)
            "a chain of 2 meshes": inline_mesh(inline_chain(2)),
            "a chain of 5 meshes, as the shipped chains are": inline_mesh(inline_chain(5)),
            "a chain of 16 meshes, the most": inline_mesh(inline_chain(16)),
            "a chain whose meshes have two textures and two parts each": inline_mesh(inline_chain(3, textures=2, tex_name=b"two.tga", parts=2)),
            "a chain that fills the vertex total (two meshes of 50000 vertices)": inline_mesh(inline_chain(2, part=dict(pieces=[dict(vertex_count=50000)]))),
            "a chain with the first and last runtime ids": inline_mesh(inline_chain(2, ids=[29999]), runtime_id=20000),
            "a chain whose chained meshes have a kept flag of 0": inline_mesh(inline_chain(3, links={1: dict(kept=0), 2: dict(kept=0)})),
            "a chained mesh with the lowest runtime id": inline_mesh(inline_chain(2, ids=[20000]), runtime_id=20005),
            # animation modes as the shipped ones are (all 1532: a terminated name, last = start + scaler or, going forward, one less, a rate from 0 to 1, start and last
            # virtual frames of every part of the chain)
            "an animation mode that goes forward (start 0, last 3, scaler 3) on parts of 4 virtual frames": inline_mesh(part=dict(vframes=4), modes=[anim_mode(b"walk", 1, 0, 3, 3, 1 / 3)]),
            "an animation mode whose last is one less than start + scaler": inline_mesh(part=dict(vframes=4), modes=[anim_mode(b"run", 2, 0, 3, 4, 0.25)]),
            "an animation mode that goes backwards (start 3, last 0, scaler -3)": inline_mesh(part=dict(vframes=4), modes=[anim_mode(b"dead_back", 1000, 3, 0, -3, 0.5)]),
            "an animation mode of one frame at the last virtual frame": inline_mesh(part=dict(vframes=4), modes=[anim_mode(b"stand", 3, 3, 3, 0, 0.0)]),
            "an animation mode of one frame going forward (start 2, last 2, scaler 1), as 127 shipped inline modes are": inline_mesh(part=dict(vframes=3),
                                                                                                                                modes=[anim_mode(b"hold", 4, 2, 2, 1, 0.5)]),
            "an animation mode with a rate of 1, the most": inline_mesh(part=dict(vframes=4), modes=[anim_mode(rate=1.0)]),
            "an animation mode name of 15 characters and its NUL": inline_mesh(modes=[anim_mode(name=b"x" * 15)]),
            "three animation modes as the shipped ones are": inline_mesh(part=dict(vframes=4), modes=[anim_mode(b"walk", 1, 0, 3, 3, 0.25), anim_mode(b"run", 2, 1, 3, 3, 0.5),
                                                                                                        anim_mode(b"dead_back", 1000, 3, 0, -3, 1.0)]),
            "an animation mode at virtual frame 65534 of parts of 65535": inline_mesh(part=dict(vframes=65535), modes=[anim_mode(start=65534, last=65534)]),
            "animation modes in a chain, within every mesh's parts": inline_mesh(inline_chain(2, part=dict(vframes=4), modes=[anim_mode(b"walk", 1, 0, 3, 3, 0.25)])),
            "a chain of three meshes with two animation modes each": inline_mesh(inline_chain(3, part=dict(vframes=4), modes=[anim_mode(b"walk", 1, 0, 3, 3, 0.25), anim_mode(b"stand", 1000)])),
            # links: a PRNT names the part that lists it, a part is at most 32 below one that no part lists, and the high bits of every index count
            "parts linked 32 deep (33 parts, each listing the next)": inline_mesh(parts=33, part_list=[inline_part(i, 33, 1, children=[i + 1] if i < 32 else [], parent=i - 1 if i else None, pieces=[])
                                                                                                   for i in range(33)]),
            "two children 256 apart, listed by two parts, one of them numbered 256": inline_mesh(parts=258, part_list=[inline_part(0, 258, 1, children=[1], pieces=[]), inline_part(1, 258, 1, parent=0, pieces=[])]
                                                                                                + [inline_part(i, 258, 1, pieces=[]) for i in range(2, 256)]
                                                                                                + [inline_part(256, 258, 1, children=[257], pieces=[]), inline_part(257, 258, 1, parent=256, pieces=[])]),
            "a camera of 256 hand frames whose map names frame 255": camera_mesh(fovs=[90.0] * 256, vhfm=bytes([255])),
        }
        for name, mesh in cases.items():
            with self.subTest(name):
                out, err = self.verdict(self.inline_archive(mesh))
                self.assertEqual(out, ["ACTIVE 1 -"], err)

    def test_an_inline_mesh_the_loaders_readers_would_misread_is_refused(self):
        # And every refusal of the walk is some case's answer: a reason that no case reaches is a line no mutant can be killed on.
        source = self.source if self.source is not None else MODS_CPP.read_text()
        walk = source[source.index("// ---- an inline mesh"):source.index("// NULL when an added level archive's unpacked stream")]
        reached = set()
        grid = lambda **kw: dict(grid=inline_grid(**kw))
        cases = {
            # the reference and the archive
            "a second name byte set": (inline_mesh(name=b"\0x"), "whose reference name is not all zero"),
            "runtime id 19999": (inline_mesh(runtime_id=19999), "whose runtime id is outside 20000 to 29999"),
            "runtime id 30000": (inline_mesh(runtime_id=30000), "whose runtime id is outside 20000 to 29999"),
            "runtime id 277, a shipped mesh's": (inline_mesh(runtime_id=277), "whose runtime id is outside 20000 to 29999"),
            "the optional word set": (inline_mesh(optional=1), "whose optional word is set"),
            "kept flag 2": (inline_mesh(kept=2), "whose kept flag is not 0 or 1"),
            "no stream at all (325 bytes)": (inline_mesh(stream=b""), "does not start with a 372-byte CMSH record"),
            "a stream of zeros": (inline_mesh(stream=bytes(500)), "does not start with a 372-byte CMSH record"),
            "a CMST first": (inline_mesh(stream=chunk(b"CMST", b"") + inline_stream()), "does not start with a 372-byte CMSH record"),
            "a CMSH of 371 bytes": (inline_mesh(stream=chunk(b"CMSH", bytes(371)) + inline_stream()[380:]), "does not start with a 372-byte CMSH record"),
            # the mesh record
            "an unterminated name": (inline_mesh(name=b"", stream=inline_stream(name=b"x" * 300)), "whose name is not terminated"),
            "a chain word and nothing after the mesh": (inline_mesh(cmsh={8: 1}), "whose chain word is set and no chained mesh follows"),
            "a cutscene sequence": (inline_mesh(cmsh={0xC: 1}), "with cutscene sequences (not admitted)"),
            "33 textures": (inline_mesh(textures=33), "count out of bounds"),
            "33 animation modes": (inline_mesh(anim=33), "count out of bounds"),
            "513 emitters": (inline_mesh(emitters=513), "count out of bounds"),
            "no parts": (inline_mesh(parts=0), "count out of bounds"),
            "513 parts": (inline_mesh(parts=513, part_list=[inline_part(i, 513, 1, pieces=[]) for i in range(513)]), "count out of bounds"),
            "fewer parts than the record says": (inline_mesh(cmsh={0x15C: 2}), "fewer MESP parts than its part count"),
            # textures
            "a CMST of 35 bytes": (inline_mesh(cmst=bytes(35)), "whose CMST is not its texture count of 36-byte records"),
            "a texture of no frames": (inline_mesh(frames=[0]), "no frames or more than 16"),
            "a texture of 17 frames": (inline_mesh(frames=[17]), "no frames or more than 16"),
            "fewer texture lists than the record says": (inline_mesh(cmsh={4: 2}, cmst=(bytes(8) + struct.pack("<i", 1) + bytes(24)) * 2), "fewer MSHT texture lists"),
            "a TEXB one byte short": (inline_mesh(texb_delta=-1), "whose MSHT is not one TEXB"),
            "a TEXB one byte long": (inline_mesh(texb_delta=1), "whose MSHT is not one TEXB"),
            "two TEXBs in an MSHT": (inline_mesh(second_texb=True), "whose MSHT is not one TEXB"),
            "an empty texture name": (inline_mesh(tex_name=b""), "name is empty or not terminated"),
            "a texture name without its NUL": (inline_mesh(tex_name=b"x" * 128), "name is empty or not terminated"),
            "a texture name with a control character": (inline_mesh(tex_name=b"a\x01b"), "name is not printable ASCII"),
            "a texture name with a byte over 0x7E": (inline_mesh(tex_name=b"a\xe9b"), "name is not printable ASCII"),
            # the parts
            "a CMSP of 315 bytes": (inline_mesh(part=dict(cmsp_delta=-1)), "does not start with a 316-byte CMSP record"),
            "a CMSP of 317 bytes": (inline_mesh(part=dict(cmsp_delta=1)), "does not start with a 316-byte CMSP record"),
            "part type 0": (inline_mesh(part=dict(words={0x8C: 0})), "whose type is not one of the game's six"),
            "part type 7": (inline_mesh(part=dict(words={0x8C: 7})), "whose type is not one of the game's six"),
            "part type 2^32 - 1": (inline_mesh(part=dict(words={0x8C: 0xFFFFFFFF})), "whose type is not one of the game's six"),
            "a chained part": (inline_mesh(part=dict(words={0x9C: 1})), "a chain, older-format vertices or triangles, or bone weights or slots"),
            "older-format vertices": (inline_mesh(part=dict(words={0xA8: 1})), "a chain, older-format vertices or triangles, or bone weights or slots"),
            "older-format pvertices": (inline_mesh(part=dict(words={0xAC: 1})), "a chain, older-format vertices or triangles, or bone weights or slots"),
            "older-format triangles": (inline_mesh(part=dict(words={0xB0: 1})), "a chain, older-format vertices or triangles, or bone weights or slots"),
            "bone weights": (inline_mesh(part=dict(words={0xD4: 1})), "a chain, older-format vertices or triangles, or bone weights or slots"),
            "bone slots": (inline_mesh(part=dict(words={0xD8: 1})), "a chain, older-format vertices or triangles, or bone weights or slots"),
            "bone weights on a skeletal part": (skeletal_mesh(words={0xD4: STALE}), "a chain, older-format vertices or triangles, or bone weights or slots"),
            "bone slots on a skeletal part": (skeletal_mesh(words={0xD8: STALE}), "a chain, older-format vertices or triangles, or bone weights or slots"),
            # the fields the reader keeps from the file and the part's destructor or the draw then uses
            "a chain count": (inline_mesh(part=dict(words={0xA0: 1})), "a chain, older-format vertices or triangles, or bone weights or slots"),
            "a chained flag": (inline_mesh(part=dict(words={0xA4: 1})), "a chain, older-format vertices or triangles, or bone weights or slots"),
            "a bones pointer and no bones": (inline_mesh(part=dict(words={0xD0: STALE})), "that is not skeletal with bones or a bones pointer"),
            "bones on a normal part": (inline_mesh(part=dict(words={0xC0: 1})), "that is not skeletal with bones or a bones pointer"),
            "bones on a camera part": (camera_mesh(words={0xC0: 1}), "that is not skeletal with bones or a bones pointer"),
            "a bones pointer on a camera part": (camera_mesh(words={0xD0: STALE}), "that is not skeletal with bones or a bones pointer"),
            # skeletal parts: the bones
            "a skeletal part with no bones": (inline_mesh(part=dict(words={0x8C: 3})), "skeletal part with no bones or more than 19"),
            "a skeletal part of 20 bones": (skeletal_mesh(bones=20), "skeletal part with no bones or more than 19"),
            "a skeletal part of 2^32 - 1 bones": (skeletal_mesh(words={0xC0: 0xFFFFFFFF}), "skeletal part with no bones or more than 19"),
            "a skeletal part without its BONE": (skeletal_mesh(bone_chunk=b""), "whose BONE is not its bone count"),
            "a BONE one entry short": (skeletal_mesh(bones=2, words={0xC0: 3}), "whose BONE is not its bone count"),
            "a BONE one entry long": (skeletal_mesh(bones=2, words={0xC0: 1}), "whose BONE is not its bone count"),
            "a BONE one byte long": (skeletal_mesh(bones=1, bone_chunk=chunk(b"BONE", bytes(5))), "whose BONE is not its bone count"),
            "a bone that is not a part": (skeletal_mesh(bones=1, names=[9]), "with a bone that is not a part of the mesh"),
            "a bone equal to the part count": (skeletal_mesh(bones=1, names=[2]), "with a bone that is not a part of the mesh"),
            "a bone of 2^32 - 1": (skeletal_mesh(bones=1, names=[0xFFFFFFFF]), "with a bone that is not a part of the mesh"),
            "the last bone not a part": (skeletal_mesh(bones=2, names=[0, 1, 7]), "with a bone that is not a part of the mesh"),
            "a bone past the parts that is below the bone count": (skeletal_mesh(bones=1, names=[0, 2, 2, 2]), "with a bone that is not a part of the mesh"),
            # skeletal parts: the vertices
            "a skeletal part without vertices": (skeletal_mesh(pieces=[]), "skeletal part without vertices, or a camera part with them"),
            "a camera part with vertices": (camera_mesh(pieces=[dict()]), "skeletal part without vertices, or a camera part with them"),
            "48-byte vertices on a normal part": (inline_mesh(part=dict(cmvb={0x114: 48, 0x118: 0}, pieces=[dict(vbytes=48 * 4, vbuf=bytes(48 * 4))])), "not the 36-byte format of a rigid part or the 48-byte format of a skeletal one"),
            "48-byte vertices on a null part": (inline_mesh(part=dict(words={0x8C: 2}, cmvb={0x114: 48, 0x118: 0}, pieces=[dict(vbytes=48 * 4, vbuf=bytes(48 * 4))])),
                                                "not the 36-byte format of a rigid part or the 48-byte format of a skeletal one"),
            "48-byte vertices on an emitter part": (inline_mesh(part=dict(words={0x8C: 5}, cmvb={0x114: 48, 0x118: 0}, pieces=[dict(vbytes=48 * 4, vbuf=bytes(48 * 4))])),
                                                    "not the 36-byte format of a rigid part or the 48-byte format of a skeletal one"),
            "48-byte vertices on a reference part": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, reference=1, cmvb={0x114: 48, 0x118: 0}, pieces=[dict(vbytes=48 * 4, vbuf=bytes(48 * 4))]),
                                                                                     inline_part(1, 2, 1)]), "not the 36-byte format of a rigid part or the 48-byte format of a skeletal one"),
            "36-byte vertices on a skeletal part": (skeletal_mesh(cmvb={0x114: 36, 0x118: 0x152}), "not the 36-byte format of a rigid part or the 48-byte format of a skeletal one"),
            "the rigid vertex format word on a skeletal part": (skeletal_mesh(cmvb={0x118: 0x152}), "not the 36-byte format of a rigid part or the 48-byte format of a skeletal one"),
            "another stride on a skeletal part": (skeletal_mesh(cmvb={0x114: 47}), "not the 36-byte format of a rigid part or the 48-byte format of a skeletal one"),
            "a triangle list on a skeletal part": (skeletal_mesh(cmvb={0x11C: 3}), "not the 36-byte format of a rigid part or the 48-byte format of a skeletal one"),
            "vertex bytes that are not 48 for each": (skeletal_mesh(pieces=[dict(vbytes=191, vbuf=bytes(191))]), "vertex count is zero, over 65535, or not its vertex bytes"),
            "a skeletal VBUF one byte short": (skeletal_mesh(pieces=[dict(vbuf=bytes(191))]), "whose VBUF is not its vertices"),
            "a palette offset that is no multiple of 3": (skeletal_mesh(rows=[(1.0, 0.0, 0.0)] * 4), "whose palette offset is not 3 times the number of one of its bones"),
            "a palette offset with a fraction": (skeletal_mesh(rows=[(1.5, 0.0, 0.0)] * 4), "whose palette offset is not 3 times the number of one of its bones"),
            "a negative palette offset": (skeletal_mesh(rows=[(-3.0, 0.0, 0.0)] * 4), "whose palette offset is not 3 times the number of one of its bones"),
            "a palette offset past the last bone": (skeletal_mesh(bones=2, rows=[(6.0, 0.0, 0.0)] * 4), "whose palette offset is not 3 times the number of one of its bones"),
            "a palette offset past 19 bones": (skeletal_mesh(bones=19, rows=[(57.0, 0.0, 0.0)] * 4), "whose palette offset is not 3 times the number of one of its bones"),
            "a palette offset of negative zero": (skeletal_mesh(rows=[(0x80000000, 0.0, 0.0)] * 4), "whose palette offset is not 3 times the number of one of its bones"),
            "a palette offset that is not a number": (skeletal_mesh(rows=[(0x7FC00000, 0.0, 0.0)] * 4), "whose palette offset is not 3 times the number of one of its bones"),
            "a palette offset of infinity": (skeletal_mesh(rows=[(0x7F800000, 0.0, 0.0)] * 4), "whose palette offset is not 3 times the number of one of its bones"),
            "a palette offset of the integer 3": (skeletal_mesh(bones=2, rows=[(3, 0.0, 0.0)] * 4), "whose palette offset is not 3 times the number of one of its bones"),
            "a bad offset in the second slot": (skeletal_mesh(rows=[(0.0, 1.0, 0.0)] * 4), "whose palette offset is not 3 times the number of one of its bones"),
            "a bad offset in the third slot": (skeletal_mesh(rows=[(0.0, 0.0, 1.0)] * 4), "whose palette offset is not 3 times the number of one of its bones"),
            "a bad offset in the last slot of the last vertex": (skeletal_mesh(rows=[(0.0, 0.0, 0.0)] * 3 + [(0.0, 0.0, 1.0)]), "whose palette offset is not 3 times the number of one of its bones"),
            # camera parts
            "a field of view on a normal part": (inline_mesh(part=dict(words={0xCC: 1})), "with a field of view that is not a camera, or a camera without one"),
            "a field of view on a skeletal part": (skeletal_mesh(words={0xCC: 1}), "with a field of view that is not a camera, or a camera without one"),
            "a camera without a field of view": (camera_mesh(words={0xCC: 0}), "with a field of view that is not a camera, or a camera without one"),
            "a camera part type without its word": (inline_mesh(part=dict(words={0x8C: 4}, pieces=[])), "with a field of view that is not a camera, or a camera without one"),
            "a camera part without its HFOV": (camera_mesh(fov_chunk=b""), "whose HFOV is not its frame count of values"),
            "an HFOV one value short": (camera_mesh(fovs=(90.0, 60.0), fov_chunk=chunk(b"HFOV", bytes(4))), "whose HFOV is not its frame count of values"),
            "an HFOV one value long": (camera_mesh(fovs=(90.0,), fov_chunk=chunk(b"HFOV", bytes(8))), "whose HFOV is not its frame count of values"),
            "an HFOV one byte long": (camera_mesh(fovs=(90.0,), fov_chunk=chunk(b"HFOV", bytes(5))), "whose HFOV is not its frame count of values"),
            "3 position sets": (inline_mesh(part=dict(words={0xB4: 3})), "more position sets than the shipped parts have"),
            "2^24 position sets": (inline_mesh(part=dict(words={0xB4: 1 << 24})), "more position sets than the shipped parts have"),
            "2^31 - 1 position sets": (inline_mesh(part=dict(words={0xB4: 0x7FFFFFFF})), "more position sets than the shipped parts have"),
            "2^32 - 1 position sets": (inline_mesh(part=dict(words={0xB4: 0xFFFFFFFF})), "more position sets than the shipped parts have"),
            "no virtual frames": (inline_mesh(part=dict(vframes=0)), "no virtual frames"),
            "no virtual frames and a stale map pointer": (inline_mesh(part=dict(vframes=0, words={0xC4: STALE})), "no virtual frames"),
            "no hand frames": (inline_mesh(part=dict(hframes=0)), "no hand frames"),
            "no hand frames and stale table pointers": (inline_mesh(part=dict(hframes=0, words={0xC8: STALE, 0x10C: STALE})), "no hand frames"),
            "more children than parts": (inline_mesh(part=dict(words={0x90: 2})), "a child, frame or cache count out of bounds"),
            "65536 virtual frames": (inline_mesh(part=dict(words={0xB8: 65536})), "a child, frame or cache count out of bounds"),
            "65536 frames": (inline_mesh(part=dict(words={0xBC: 65536})), "a child, frame or cache count out of bounds"),
            "65536 cached frames": (inline_mesh(part=dict(words={0x118: 65536})), "a child, frame or cache count out of bounds"),
            # The first child is valid: later slots must independently enforce both bounds.
            "a backward second child": (inline_mesh(parts=4, part_list=[inline_part(0, 4, 1), inline_part(1, 4, 1),
                                                inline_part(2, 4, 1, children=[3, 1]), inline_part(3, 4, 1, parent=2)]),
                                        "a child that is not a later part of the mesh"),
            "a cycle through a second child": (inline_mesh(parts=4, part_list=[inline_part(0, 4, 1), inline_part(1, 4, 1, children=[2]),
                                                inline_part(2, 4, 1, parent=1, children=[3, 1]), inline_part(3, 4, 1, parent=2)]),
                                        "a child that is not a later part of the mesh"),
            "a backward third child": (inline_mesh(parts=6, part_list=[inline_part(0, 6, 1), inline_part(1, 6, 1), inline_part(2, 6, 1),
                                                inline_part(3, 6, 1, children=[4, 5, 2]), inline_part(4, 6, 1, parent=3), inline_part(5, 6, 1, parent=3)]),
                                       "a child that is not a later part of the mesh"),
            "a second child equal to the part count": (inline_mesh(parts=4, part_list=[inline_part(0, 4, 1), inline_part(1, 4, 1),
                                                inline_part(2, 4, 1, children=[3, 4]), inline_part(3, 4, 1, parent=2)]),
                                       "a child that is not a later part of the mesh"),
            "a third child beyond the part count": (inline_mesh(parts=5, part_list=[inline_part(0, 5, 1), inline_part(1, 5, 1),
                                                inline_part(2, 5, 1, children=[3, 4, 6]), inline_part(3, 5, 1, parent=2), inline_part(4, 5, 1, parent=2)]),
                                       "a child that is not a later part of the mesh"),
            "a child that is not a part": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, children=[5]), inline_part(1, 2, 1)]), "a child that is not a later part of the mesh"),
            "a child equal to the part count": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, children=[2]), inline_part(1, 2, 1)]), "a child that is not a later part of the mesh"),
            "a part that is its own child": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, children=[0]), inline_part(1, 2, 1)]), "a child that is not a later part of the mesh"),
            "a child that comes before the part": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1), inline_part(1, 2, 1, children=[0])]), "a child that is not a later part of the mesh"),
            "a CHLD that is not the child count": (inline_mesh(part=dict(words={0x90: 1})), "whose CHLD is not its child count"),
            # a part's number is its place in the mesh, and a part is listed as a child once at most (the drawing and the pose go down every entry)
            "a part number of 0x41414141": (inline_mesh(part=dict(words={0x88: STALE})), "whose part number is not its place in the mesh"),
            "a part number of 2^32 - 1": (inline_mesh(part=dict(words={0x88: 0xFFFFFFFF})), "whose part number is not its place in the mesh"),
            "a part number equal to the part count": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1), inline_part(2, 2, 1)]), "whose part number is not its place in the mesh"),
            "two parts with one part number": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1), inline_part(0, 2, 1)]), "whose part number is not its place in the mesh"),
            "two parts whose numbers are swapped": (inline_mesh(parts=2, part_list=[inline_part(1, 2, 1), inline_part(0, 2, 1)]), "whose part number is not its place in the mesh"),
            "a skeletal part with another part's number": (skeletal_mesh(words={0x88: 0}), "whose part number is not its place in the mesh"),
            "a part number past its place in a chained mesh": (inline_mesh(inline_chain(2, last=inline_stream(parts=2, part_list=[inline_part(0, 2, 1), inline_part(2, 2, 1)]))),
                                                               "whose part number is not its place in the mesh"),
            "a child listed twice": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, children=[1, 1]), inline_part(1, 2, 1, parent=0)]), "a child that it or another part of the mesh lists already"),
            "a child listed by two parts": (inline_mesh(parts=3, part_list=[inline_part(0, 3, 1, children=[1, 2]), inline_part(1, 3, 1, parent=0, children=[2]), inline_part(2, 3, 1, parent=1)]),
                                            "a child that it or another part of the mesh lists already"),
            "40 parts that each list the next twice (2^39 ways down)": (inline_mesh(parts=40, part_list=[inline_part(i, 40, 1, children=[i + 1, i + 1] if i < 39 else [], pieces=[]) for i in range(40)]),
                                                                       "a child that it or another part of the mesh lists already"),
            "64 parts that each list every later part (2^62 ways down)": (inline_mesh(parts=64, part_list=[inline_part(i, 64, 1, children=list(range(i + 1, 64)), pieces=[]) for i in range(64)]),
                                                                         "a child that it or another part of the mesh lists already"),
            "a child listed twice in a chained mesh": (inline_mesh(inline_chain(2, last=inline_stream(parts=2, part_list=[inline_part(0, 2, 1, children=[1, 1]), inline_part(1, 2, 1)]))),
                                                       "a child that it or another part of the mesh lists already"),
            "a parent that is not a part": (inline_mesh(part=dict(parent=7)), "whose PRNT is not one earlier part of the mesh"),
            "a parent pointer and no PRNT": (inline_mesh(part=dict(words={0x98: 1}, box=True)), "whose PRNT is not one earlier part of the mesh"),
            "a part that is its own parent": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1), inline_part(1, 2, 1, parent=1)]), "whose PRNT is not one earlier part of the mesh"),
            "a parent that comes after the part": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, parent=1), inline_part(1, 2, 1)]), "whose PRNT is not one earlier part of the mesh"),
            "two parts that are each other's parent": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, parent=1), inline_part(1, 2, 1, parent=0)]), "whose PRNT is not one earlier part of the mesh"),
            # a part has a PRNT exactly when some part lists it, naming that part; no part is more than 32 below one that no part lists
            "a listed part with no PRNT": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, children=[1]), inline_part(1, 2, 1)]), "whose PRNT is not the part that lists it as a child"),
            "a part whose PRNT is not the part that lists it": (inline_mesh(parts=3, part_list=[inline_part(0, 3, 1, children=[2]), inline_part(1, 3, 1), inline_part(2, 3, 1, parent=1)]),
                                                                "whose PRNT is not the part that lists it as a child"),
            "a part whose PRNT names the part above the one that lists it": (inline_mesh(parts=3, part_list=[inline_part(0, 3, 1, children=[1]), inline_part(1, 3, 1, parent=0, children=[2]),
                                                                                                             inline_part(2, 3, 1, parent=0)]), "whose PRNT is not the part that lists it as a child"),
            "a part that no part lists with a PRNT": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1), inline_part(1, 2, 1, parent=0)]), "whose PRNT is not the part that lists it as a child"),
            "parts linked 33 deep (34 parts, each listing the next)": (inline_mesh(parts=34, part_list=[inline_part(i, 34, 1, children=[i + 1] if i < 33 else [], parent=i - 1 if i else None, pieces=[])
                                                                                                        for i in range(34)]), "with a child more than 32 parts below a part that no part lists"),
            "parts linked 33 deep in a chained mesh": (inline_mesh(inline_chain(2, last=inline_stream(parts=34, part_list=[inline_part(i, 34, 1, children=[i + 1] if i < 33 else [], parent=i - 1 if i else None,
                                                                                                                                      pieces=[]) for i in range(34)]))),
                                                       "with a child more than 32 parts below a part that no part lists"),
            # the high bits of each index the walk compares (the game reads every one of them whole)
            "a part number of its place plus 2^16": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1), inline_part(1, 2, 1, words={0x88: (1 << 16) + 1})]), "whose part number is not its place in the mesh"),
            "a bone of 2^16": (skeletal_mesh(bones=1, names=[1 << 16]), "with a bone that is not a part of the mesh"),
            "a PRNT of 2^16 plus the part that lists it": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, children=[1]), inline_part(1, 2, 1, parent=1 << 16)]), "whose PRNT is not one earlier part of the mesh"),
            "a REFR of 2^16 plus a normal part's place": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, reference=(1 << 16) + 1, pieces=[]), inline_part(1, 2, 1)]), "whose REFR is not one part of the mesh"),
            "no bounding box": (inline_mesh(part=dict(box=False)), "without its bounding box"),
            "a VHFM that is not the virtual-frame count": (inline_mesh(part=dict(words={0xB8: 3})), "whose VHFM is not its virtual-frame count"),
            "a VHFM byte equal to the frame count": (inline_mesh(part=dict(vhfm=bytes([0, 1, 0]), vframes=3)), "whose VHFM names a frame past its frame count"),
            "a VHFM byte of 255": (inline_mesh(part=dict(vhfm=bytes([255]))), "whose VHFM names a frame past its frame count"),
            "a camera's VHFM byte past its fields of view": (camera_mesh(fovs=(90.0, 60.0), vframes=2, vhfm=bytes([0, 2])), "whose VHFM names a frame past its frame count"),
            "a skeletal part's VHFM byte past its frames": (skeletal_mesh(vhfm=bytes([1])), "whose VHFM names a frame past its frame count"),
            "the last VHFM byte past the frame count": (inline_mesh(part=dict(vhfm=bytes([0, 0, 2]), vframes=3, hframes=2)), "whose VHFM names a frame past its frame count"),
            "no orientation frames": (inline_mesh(part=dict(orientations=False)), "without its HORI and HPOS"),
            "no position frames": (inline_mesh(part=dict(positions=False)), "without its HORI and HPOS"),
            "an HORI that is not the frame count": (inline_mesh(part=dict(words={0xBC: 2})), "whose HORI is not its frame count of matrices"),
            "an HPOS that is not the frame count": (inline_mesh(part=dict(hpos=b"x" * 15)), "whose HPOS is not its frame count of vectors"),
            # Require the bounds refusal, not a later target-kind refusal after an invalid lookup.
            "a reference equal to the part count": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1), inline_part(1, 2, 1, reference=2, pieces=[])]),
                                                    "whose REFR is not one part of the mesh"),
            "a reference equal to the maximum part count": (inline_mesh(parts=512, part_list=[inline_part(i, 512, 1, pieces=[]) for i in range(511)]
                                                    + [inline_part(511, 512, 1, reference=512, pieces=[])]),
                                                    "whose REFR is not one part of the mesh"),
            "a reference that is not a part": (inline_mesh(part=dict(reference=9)), "whose REFR is not one part of the mesh"),
            "a reference pointer and no REFR": (inline_mesh(part=dict(words={0x8C: 6, 0x124: 1})), "whose REFR is not one part of the mesh"),
            "a reference part without a REFR": (inline_mesh(part=dict(words={0x8C: 6})), "or a reference part without one"),
            "a REFR on a normal part": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, reference=1, words={0x8C: 1}), inline_part(1, 2, 1)]), "with a REFR that is not a reference part"),
            "a reference part that names a null part": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, reference=1, pieces=[]), inline_part(1, 2, 1, words={0x8C: 2}, pieces=[])]), "that does not name a normal part"),
            "a reference part that names an emitter part": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, reference=1, pieces=[]), inline_part(1, 2, 1, words={0x8C: 5}, pieces=[])]), "that does not name a normal part"),
            "a reference part that names a reference part": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, reference=1, pieces=[]), inline_part(1, 2, 1, reference=0, pieces=[])]), "that does not name a normal part"),
            "a reference part that names itself": (inline_mesh(part=dict(reference=0, pieces=[])), "that does not name a normal part"),
            "a reference part that names a skeletal part": (inline_mesh(parts=3, part_list=[inline_part(0, 3, 1, reference=1, pieces=[]), inline_part(1, 3, 1, bones=[2]), inline_part(2, 3, 1, pieces=[])]), "that does not name a normal part"),
            "a reference part that names a camera part": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, reference=1, pieces=[]), inline_part(1, 2, 1, fovs=[90.0])]), "that does not name a normal part"),
            "the second reference part of a mesh names a null part": (inline_mesh(parts=4, part_list=[inline_part(0, 4, 1, reference=2, pieces=[]), inline_part(1, 4, 1, reference=3, pieces=[]), inline_part(2, 4, 1),
                                                                                      inline_part(3, 4, 1, words={0x8C: 2}, pieces=[])]), "that does not name a normal part"),
            "a reference part that names a null part numbered 256 or more": (inline_mesh(parts=258, part_list=[inline_part(0, 258, 1, reference=257, pieces=[])] + [inline_part(i, 258, 1, pieces=[]) for i in range(1, 257)]
                                                                                       + [inline_part(257, 258, 1, words={0x8C: 2}, pieces=[])]), "that does not name a normal part"),
            "a reference part numbered 256 or more that names a null part": (inline_mesh(parts=258, part_list=[inline_part(0, 258, 1, words={0x8C: 2}, pieces=[])] + [inline_part(i, 258, 1, pieces=[]) for i in range(1, 257)]
                                                                                       + [inline_part(257, 258, 1, reference=0, pieces=[])]), "that does not name a normal part"),
            "a collision grid pointer and no PBKT": (inline_mesh(part=dict(words={0x100: 1})), "has a collision grid but no PBKT"),
            "a CPOS that is not the cached frame count of vectors": (inline_mesh(part=dict(vframes=2, cached=(2, 31, 96))), "whose CPOS is not its cached frame count of vectors"),
            "a CORI that is not the cached frame count of matrices": (inline_mesh(part=dict(vframes=2, cached=(2, 32, 97))), "whose CORI is not its cached frame count of matrices"),
            "a cache pointer and no CPOS": (inline_mesh(part=dict(cached=(1, None, 48), words={0x104: STALE})), "whose CPOS is not its cached frame count of vectors"),
            "a cache pointer and no CORI": (inline_mesh(part=dict(cached=(1, 16, None), words={0x108: STALE})), "whose CORI is not its cached frame count of matrices"),
            # the cache: CMeshPart::GetWorldTransforms goes up the parents while a part is static and reads the array of the part it stops at, bounded from above
            # only by that part's count less one (the review's H1 to H4)
            "no cached frames (an empty CPOS and CORI)": (inline_mesh(part=dict(cached=(0, 0, 0))), "whose cached frame count is neither 1 nor its virtual-frame count"),
            "2 cached frames of 1 virtual frame": (inline_mesh(part=dict(cached=(2, 32, 96))), "whose cached frame count is neither 1 nor its virtual-frame count"),
            "2 cached frames of 3 virtual frames": (inline_mesh(part=dict(vframes=3, cached=(2, 32, 96))), "whose cached frame count is neither 1 nor its virtual-frame count"),
            "4 cached frames of 3 virtual frames": (inline_mesh(part=dict(vframes=3, cached=(4, 64, 192))), "whose cached frame count is neither 1 nor its virtual-frame count"),
            "a static part whose parent has no cached frames (H3)": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, children=[1], cached=(0, 0, 0)),
                                                                                                     inline_part(1, 2, 1, parent=0, static=(1, 1), cached=(1, None, None))]),
                                                                     "whose cached frame count is neither 1 nor its virtual-frame count"),
            "no cached frames in a chained mesh": (inline_mesh(inline_chain(2, last=inline_stream(part=dict(cached=(0, 0, 0))))), "whose cached frame count is neither 1 nor its virtual-frame count"),
            "cached frames and no CPOS, the position not static (H1)": (inline_mesh(part=dict(cached=(1, None, 48))), "with a CPOS when its position is static, or without one when it is not"),
            "a CPOS on a listed part whose position is static": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, children=[1]), inline_part(1, 2, 1, parent=0, static=(1, 0))]),
                                                                 "with a CPOS when its position is static, or without one when it is not"),
            "a skeletal part whose bone has cached frames and no CPOS (H4)": (inline_mesh(parts=3, part_list=[inline_part(0, 3, 1, pieces=[], cached=(1, None, 48)), inline_part(1, 3, 1, pieces=[]),
                                                                                                              inline_part(2, 3, 1, bones=[0, 1], pieces=[dict(vbuf=skinned_vertices([(0.0, 3.0, 0.0)] * 4))])]),
                                                                              "with a CPOS when its position is static, or without one when it is not"),
            "cached frames and no CORI, the orientation not static (H1)": (inline_mesh(part=dict(cached=(1, 16, None))), "with a CORI when its orientation is static, or without one when it is not"),
            "a CORI on a listed part whose orientation is static": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, children=[1]), inline_part(1, 2, 1, parent=0, static=(0, 1))]),
                                                                    "with a CORI when its orientation is static, or without one when it is not"),
            "a static position on a part that no part lists (H2)": (inline_mesh(part=dict(static=(1, 0), cached=(1, None, 48))), "with no parent whose position or orientation is static"),
            "a static orientation on a part that no part lists (H2)": (inline_mesh(part=dict(static=(0, 1), cached=(1, 16, None))), "with no parent whose position or orientation is static"),
            "a static position of 2^31 on a part that no part lists": (inline_mesh(part=dict(static=(1 << 31, 0), cached=(1, None, 48))), "with no parent whose position or orientation is static"),
            "a static orientation of 2^31 on a part that no part lists": (inline_mesh(part=dict(static=(0, 1 << 31), cached=(1, 16, None))), "with no parent whose position or orientation is static"),
            "a static flag on the second of two parts that no part lists": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1), inline_part(1, 2, 1, static=(1, 1), cached=(1, None, None))]),
                                                                            "with no parent whose position or orientation is static"),
            "a static part with no parent in a chained mesh": (inline_mesh(inline_chain(2, last=inline_stream(part=dict(static=(1, 1), cached=(1, None, None))))),
                                                               "with no parent whose position or orientation is static"),
            # the rules hold for a part whichever of its flags are set, not only for one whose other flag is clear: each case breaks one rule on a listed part of one flag combination
            # (test_the_cache_rules_hold_for_every_combination_of_static_flags_words_counts_and_links runs the whole product)
            "no cached frames, the position static (an empty CORI)": (cache_mesh(static=(1, 0), cached=(0, None, 0)), CACHE_COUNT),
            "no cached frames, the orientation static (an empty CPOS)": (cache_mesh(static=(0, 1), cached=(0, 0, None)), CACHE_COUNT),
            "no cached frames, the position and the orientation static": (cache_mesh(static=(1, 1), cached=(0, None, None)), CACHE_COUNT),
            "2 cached frames of 1 virtual frame, the position static": (cache_mesh(static=(1, 0), cached=(2, None, 96)), CACHE_COUNT),
            "2 cached frames of 1 virtual frame, the orientation static": (cache_mesh(static=(0, 1), cached=(2, 32, None)), CACHE_COUNT),
            "2 cached frames of 3 virtual frames, the position and the orientation static": (cache_mesh(static=(1, 1), vframes=3, cached=(2, None, None)), CACHE_COUNT),
            "cached frames and no CORI, the position static and the orientation not": (cache_mesh(static=(1, 0), cached=(1, None, None)), CACHE_CORI),
            "a CORI on a part whose position and orientation are static": (cache_mesh(static=(1, 1), cached=(1, None, 48)), CACHE_CORI),
            "cached frames and no CPOS, the orientation static and the position not": (cache_mesh(static=(0, 1), cached=(1, None, None)), CACHE_CPOS),
            "a CPOS on a part whose position and orientation are static": (cache_mesh(static=(1, 1), cached=(1, 16, None)), CACHE_CPOS),
            "a CMVB of 295 bytes": (inline_mesh(part=dict(cmvb_delta=-1)), "does not start with a 296-byte CMVB"),
            "13 pieces": (inline_mesh(part=dict(pieces=[dict() for _ in range(13)])), "more than 12 vertex-buffer pieces"),
            "48-byte vertices": (inline_mesh(part=dict(cmvb={0x114: 48})), "not the 36-byte format of a rigid part or the 48-byte format of a skeletal one"),
            "another vertex format": (inline_mesh(part=dict(cmvb={0x118: 0})), "not the 36-byte format of a rigid part or the 48-byte format of a skeletal one"),
            "a triangle list": (inline_mesh(part=dict(cmvb={0x11C: 3})), "not the 36-byte format of a rigid part or the 48-byte format of a skeletal one"),
            "a shader pointer": (inline_mesh(part=dict(cmvb={0x110: STALE})), "carries a shader pointer"),
            "a piece pointer in the first unused slot": (inline_mesh(part=dict(cmvb={0x0C: STALE})), "piece pointer beyond its pieces"),
            "a piece pointer in the last slot": (inline_mesh(part=dict(cmvb={0x104: STALE})), "piece pointer beyond its pieces"),
            "a piece pointer in slot 0 of a part without pieces": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, pieces=[], cmvb={0x08: STALE}), inline_part(1, 2, 1)]),
                                                                   "piece pointer beyond its pieces"),
            "a piece pointer in the slot after twelve pieces": (inline_mesh(part=dict(pieces=[dict() for _ in range(12)], cmvb={0x08 + 4 * 12: STALE})), "piece pointer beyond its pieces"),
            "bytes after the last piece": (inline_mesh(part=dict(after=chunk(b"JUNK", b""))), "bytes in its PMVB after its last piece"),
            "bytes after the vertex buffer": (inline_mesh(part=dict(tail=chunk(b"JUNK", b""))), "bytes after its vertex buffer"),
            # the pieces
            "a piece not built": (inline_mesh(part=dict(pieces=[dict(built=0)])), "whose MMPT counts disagree"),
            "a piece of 2 indices": (inline_mesh(part=dict(pieces=[dict(indices=[0, 1])])), "whose MMPT counts disagree"),
            "index bytes that are not twice the count": (inline_mesh(part=dict(pieces=[dict(ibytes=7)])), "whose MMPT counts disagree"),
            "a primitive count that is not the index count less 2": (inline_mesh(part=dict(pieces=[dict(prims=3)])), "whose MMPT counts disagree"),
            "no vertices": (inline_mesh(part=dict(pieces=[dict(vertex_count=0, indices=[0, 0, 0, 0])])), "vertex count is zero, over 65535, or not its vertex bytes"),
            "vertex bytes that are not 36 for each": (inline_mesh(part=dict(pieces=[dict(vbytes=143)])), "vertex count is zero, over 65535, or not its vertex bytes"),
            "a second piece with other vertices": (inline_mesh(part=dict(pieces=[dict(), dict(vertex_count=5)])), "does not share the first piece's vertices"),
            "an index past the vertices": (inline_mesh(part=dict(pieces=[dict(indices=[0, 1, 2, 4])])), "an index past its vertices"),
            "an IBUF longer than its bytes": (inline_mesh(part=dict(pieces=[dict(ibuf_extra=2)])), "whose IBUF is not its index bytes"),
            "a first VBUF longer than its vertices": (inline_mesh(part=dict(vbuf_extra=4)), "whose VBUF is not its vertices"),
            "a second VBUF that is not empty": (inline_mesh(part=dict(pieces=[dict(), dict(vbuf2=36)])), "whose VBUF is not its vertices"),
            "a TEXR of 28 bytes": (inline_mesh(part=dict(pieces=[dict(texr_extra=4)])), "without a 24-byte TEXR"),
            "an MMPT of 28 bytes": (inline_mesh(part=dict(pieces=[dict(mmpt_extra=4)])), "without a 24-byte MMPT"),
            "an MMPT of 20 bytes": (inline_mesh(part=dict(pieces=[dict(mmpt_cut=4)])), "without a 24-byte MMPT"),
            "100001 vertices in all": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, pieces=[dict(vertex_count=50001)]), inline_part(1, 2, 1, pieces=[dict(vertex_count=50000)])]),
                                       "more than 100000 vertices, 600000 indices or 1024 pieces"),
            "two parts of 65535 vertices": (inline_mesh(parts=2, part_list=[inline_part(i, 2, 1, pieces=[dict(vertex_count=65535)]) for i in range(2)]),
                                            "more than 100000 vertices, 600000 indices or 1024 pieces"),
            "600001 indices in all": (inline_mesh(parts=10, part_list=[inline_part(i, 10, 1, pieces=[dict(indices=[j % 4 for j in range(60001 if i == 9 else 60000)])]) for i in range(10)]),
                                      "more than 100000 vertices, 600000 indices or 1024 pieces"),
            "1025 pieces in all": (inline_mesh(parts=86, part_list=[inline_part(i, 86, 1, pieces=[dict() for _ in range(12 if i < 85 else 5)]) for i in range(86)]),
                                   "more than 100000 vertices, 600000 indices or 1024 pieces"),
            "a stream of 16 MiB and one byte": (inline_mesh(stream=bytes((16 << 20) + 1)), "an inline mesh over 16 MiB"),
            "a stream of exactly 16 MiB is not over the cap (it is no mesh either)": (inline_mesh(stream=bytes(16 << 20)), "does not start with a 372-byte CMSH record"),
            "a texture slot past the textures": (inline_mesh(part=dict(pieces=[dict(texr=[1, -1, -1, -1, -1, -1])])), "a texture slot that is not -1 or one of the mesh's textures"),
            "a texture slot of -2": (inline_mesh(part=dict(pieces=[dict(texr=[0, -2, -1, -1, -1, -1])])), "a texture slot that is not -1 or one of the mesh's textures"),
            # the collision grid
            "a grid with fewer cells than its size": (inline_mesh(part=grid(width=3, height=1, lists={0: [0, 1]}, cells=grid_cells(2, 1, {0: [0, 1]}))), "fewer PBCS cells than its width and height"),
            "a grid of no width": (inline_mesh(part=grid(width=0, height=1, lists={}, triangles=0)), "has a size out of bounds"),
            "a grid of 200 by 200 cells": (inline_mesh(part=grid(width=200, height=200, lists={}, triangles=0)), "has a size out of bounds"),
            "a grid of 16385 cells": (inline_mesh(part=grid(width=16385, height=1, lists={}, triangles=0)), "has a size out of bounds"),
            "a cell that is neither present nor absent": (inline_mesh(part=grid(cells=chunk(b"PBCS", struct.pack("<i", 2)) + chunk(b"PBCS", struct.pack("<i", 0)))),
                                                         "neither present nor absent"),
            "an absent cell with data": (inline_mesh(part=grid(cells=chunk(b"PBCS", struct.pack("<ii", 0, 0)) + chunk(b"PBCS", struct.pack("<i", 0)), lists={})),
                                         "an absent cell with data"),
            "a list longer than its size": (inline_mesh(part=grid(cells=chunk(b"PBCS", struct.pack("<i", 1) + chunk(b"PBET", chunk(b"PBSQ", struct.pack("<i", 5) + bytes(4))))
                                                             + chunk(b"PBCS", struct.pack("<i", 0)))), "a list whose size is not its count"),
            "a list with bytes after its indices": (inline_mesh(part=grid(cells=chunk(b"PBCS", struct.pack("<i", 1) + chunk(b"PBET", chunk(b"PBSQ", struct.pack("<i", 1) + bytes(4))))
                                                             + chunk(b"PBCS", struct.pack("<i", 0)), lists={0: [0]})), "a list whose size is not its count"),
            "a list naming a triangle past the count": (inline_mesh(part=grid(lists={0: [0, 5]})), "lists a triangle it does not have"),
            "a list naming triangle 32767": (inline_mesh(part=grid(lists={0: [0, 32767]}, triangles=32768)), "lists a triangle past 32767"),
            "more lists than SIZS declares": (inline_mesh(part=grid(sizs=(0, 2))), "more lists or indices than its SIZS declares"),
            "more indices than SIZS declares": (inline_mesh(part=grid(sizs=(1, 1))), "more lists or indices than its SIZS declares"),
            "a SIZS with more lists than cells": (inline_mesh(part=grid(sizs=(3, 2))), "declares more lists or indices than it can hold"),
            "a PBKE of 14 bytes": (inline_mesh(part=grid(pbke_size=14)), "fewer 12-byte PBKE triangles than its count"),
            "a PBKE of 10 bytes": (inline_mesh(part=grid(pbke_size=10)), "fewer 12-byte PBKE triangles than its count"),
            "a PLQT of more triangles than PBFK holds": (inline_mesh(part=grid(plqt=3)), "fewer 12-byte PBKE triangles than its count"),
            "a PLQT of fewer triangles than PBFK holds": (inline_mesh(part=grid(plqt=1, lists={0: [0]})), "bytes in its PBFK after its triangles"),
            "a listed triangle naming a vertex it lacks": (inline_mesh(part=grid(tri_vertices=[(0, 1, 9), (1, 2, 3)])), "a listed triangle that names a vertex it does not have"),
            "more vertices than 32767": (inline_mesh(part=grid(vertices=32768)), "names a vertex it does not have"),
            # the rules of the grid and of the part that no case above reached: each is a refusal with a mutant that removes it
            "a grid that does not start with a 184-byte CPBT": (inline_mesh(part=grid(cpbt_raw=bytes(183))), "does not start with a 184-byte CPBT"),
            "a SIZS of 7 bytes": (inline_mesh(part=grid(sizs_raw=bytes(7))), "has no 8-byte SIZS after its CPBT"),
            "a present cell that holds more than a PBET": (inline_mesh(part=grid(cells=chunk(b"PBCS", struct.pack("<i", 1) + chunk(b"PBET", chunk(b"PBSQ", struct.pack("<i", 0))) + b"x")
                                                                                  + chunk(b"PBCS", struct.pack("<i", 0)))), "has a present cell that is not one PBET"),
            "a PBET that is not a PBSQ": (inline_mesh(part=grid(cells=chunk(b"PBCS", struct.pack("<i", 1) + chunk(b"PBET", chunk(b"JUNK", bytes(4)))) + chunk(b"PBCS", struct.pack("<i", 0)))),
                                          "has a PBET that is not one PBSQ"),
            "a PBET that holds a PBSQ and another chunk": (inline_mesh(part=grid(cells=chunk(b"PBCS", struct.pack("<i", 1) + chunk(b"PBET", chunk(b"PBSQ", struct.pack("<i", 0)) + chunk(b"JUNK", b"")))
                                                                                 + chunk(b"PBCS", struct.pack("<i", 0)))), "has a PBET that is not one PBSQ"),
            "a PBSQ of 3 bytes": (inline_mesh(part=grid(cells=chunk(b"PBCS", struct.pack("<i", 1) + chunk(b"PBET", chunk(b"PBSQ", bytes(3)))) + chunk(b"PBCS", struct.pack("<i", 0)))),
                                  "has a PBET that is not one PBSQ"),
            "a PLQT of 5 bytes": (inline_mesh(part=grid(plqt_raw=bytes(5))), "has no 4-byte PLQT after its cells"),
            "no PBFK": (inline_mesh(part=grid(no_pbfk=True)), "has no PBFK"),
            "a PBPN of 5 bytes": (inline_mesh(part=grid(pbpn_raw=bytes(5))), "has no 4-byte PBPN"),
            "a PBPP of 7 bytes": (inline_mesh(part=grid(pbpp_size=7)), "fewer 6-byte PBPP vertices than its count"),
            "one PBPP fewer than the count": (inline_mesh(part=grid(pbpp_count=3)), "fewer 6-byte PBPP vertices than its count"),
            "a chunk after the last vertex": (inline_mesh(part=grid(tail=chunk(b"JUNK", b""))), "has bytes after its last vertex"),
            "a part without its PMVB": (inline_mesh(part=dict(no_pmvb=True)), "without its PMVB"),
            # the end of the stream
            "a CAMD that is not the animation-mode count": (inline_mesh(anim=1, stream=inline_stream(anim=1).replace(chunk(b"CAMD", bytes(36)), chunk(b"CAMD", bytes(35)))),
                                                           "whose CAMD is not its animation-mode count"),
            # the animation modes: the drawing indexes a part's frame map with start + scaler * phase, bounded from above only
            "an animation mode name of 16 characters without its NUL": (inline_mesh(modes=[anim_mode(name=b"x" * 16)]), "animation mode whose name is not terminated"),
            "the last of three animation modes with a name without its NUL": (inline_mesh(modes=[anim_mode(), anim_mode(), anim_mode(name=b"y" * 16)]), "animation mode whose name is not terminated"),
            "a scaler that is not last - start": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=1, last=3, scaler=5)]), "whose last frame is not its start plus its frame scaler"),
            "a scaler of 0 with start 0 and last 3": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=0, last=3, scaler=0)]), "whose last frame is not its start plus its frame scaler"),
            "a scaler of the wrong sign (start 0, last 3, scaler -3)": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=0, last=3, scaler=-3)]), "whose last frame is not its start plus its frame scaler"),
            "a scaler of 2^30": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=0, last=3, scaler=1 << 30)]), "whose last frame is not its start plus its frame scaler"),
            "a last of -10 with a scaler of -1": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=2, last=-10, scaler=-1)]), "whose last frame is not its start plus its frame scaler"),
            "a start of -2^31": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=-(1 << 31), last=3, scaler=1)]), "whose last frame is not its start plus its frame scaler"),
            "a backwards mode whose last is one more than start + scaler": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=3, last=0, scaler=-4)]),
                                                                           "whose last frame is not its start plus its frame scaler"),
            "a backwards mode whose last is one less than start + scaler": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=3, last=0, scaler=-2)]),
                                                                            "whose last frame is not its start plus its frame scaler"),
            "a mode of no span whose last is one before its start": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=3, last=2, scaler=0)]), "whose last frame is not its start plus its frame scaler"),
            "a forward mode whose last is one more than start + scaler": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=0, last=3, scaler=2)]), "whose last frame is not its start plus its frame scaler"),
            "a start + scaler that wraps around 32 bits": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=(1 << 31) - 1, last=-(1 << 31), scaler=1)]),
                                                           "whose last frame is not its start plus its frame scaler"),
            "the last of three modes with a scaler that is not last - start": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(), anim_mode(), anim_mode(start=0, last=3, scaler=1)]),
                                                                               "whose last frame is not its start plus its frame scaler"),
            "a rate that is not a number": (inline_mesh(modes=[anim_mode(rate_bits=0x7FC00000)]), "whose rate is not a float from 0 to 1"),
            "a rate of infinity": (inline_mesh(modes=[anim_mode(rate_bits=0x7F800000)]), "whose rate is not a float from 0 to 1"),
            "a negative rate": (inline_mesh(modes=[anim_mode(rate=-1.0)]), "whose rate is not a float from 0 to 1"),
            "a rate of negative zero": (inline_mesh(modes=[anim_mode(rate_bits=0x80000000)]), "whose rate is not a float from 0 to 1"),
            "a rate just over 1": (inline_mesh(modes=[anim_mode(rate_bits=0x3F800001)]), "whose rate is not a float from 0 to 1"),
            "a rate of 2": (inline_mesh(modes=[anim_mode(rate=2.0)]), "whose rate is not a float from 0 to 1"),
            "the last of three modes with a rate that is not a number": (inline_mesh(modes=[anim_mode(), anim_mode(), anim_mode(rate_bits=0x7FC00000)]), "whose rate is not a float from 0 to 1"),
            "a start of -1": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=-1, last=3, scaler=4)]), "whose start or last is not a virtual frame of every part"),
            "a last of -1": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=0, last=-1, scaler=-1)]), "whose start or last is not a virtual frame of every part"),
            "a start and last equal to the virtual-frame count": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=4, last=4)]), "whose start or last is not a virtual frame of every part"),
            "a last equal to the virtual-frame count": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=0, last=4, scaler=4)]), "whose start or last is not a virtual frame of every part"),
            "a backwards mode whose start is the virtual-frame count": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=4, last=0, scaler=-4)]), "whose start or last is not a virtual frame of every part"),
            "a start of 2^16 plus 3": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(start=(1 << 16) + 3, last=(1 << 16) + 3)]), "whose start or last is not a virtual frame of every part"),
            "a start past the frames of a smaller later part": (inline_mesh(parts=2, part_list=[inline_part(0, 2, 1, vframes=8), inline_part(1, 2, 1, vframes=4)], modes=[anim_mode(start=5, last=5)]),
                                                                "whose start or last is not a virtual frame of every part"),
            "the last of three modes past the frames": (inline_mesh(part=dict(vframes=4), modes=[anim_mode(), anim_mode(), anim_mode(start=4, last=4)]), "whose start or last is not a virtual frame of every part"),
            "a mode past the frames of a mesh before it in the chain": (inline_mesh(inline_chain(2, part=dict(vframes=4), modes=[anim_mode()],
                                                                                     last=inline_stream(part=dict(vframes=8), modes=[anim_mode(start=5, last=5)]))),
                                                                        "whose start or last is not a virtual frame of every part"),
            "a mode of the first mesh past the frames of a mesh chained to it": (inline_mesh(inline_chain(2, part=dict(vframes=8), modes=[anim_mode(start=5, last=5)],
                                                                                                      last=inline_stream(part=dict(vframes=4), modes=[anim_mode()]))),
                                                                                 "whose start or last is not a virtual frame of every part"),
            # every mesh of a chain is drawn with the index a mode has in the first mesh's table: each has as many modes as the first
            "a chained mesh with fewer animation modes than the first": (inline_mesh(inline_chain(2, modes=[anim_mode(), anim_mode()], last=inline_stream(modes=[anim_mode()]))),
                                                                         "has another number of animation modes than the first mesh of its chain"),
            "a chained mesh with more animation modes than the first": (inline_mesh(inline_chain(2, modes=[anim_mode()], last=inline_stream(modes=[anim_mode(), anim_mode()]))),
                                                                        "has another number of animation modes than the first mesh of its chain"),
            "a chained mesh with animation modes when the first has none": (inline_mesh(inline_chain(2, last=inline_stream(modes=[anim_mode()]))),
                                                                            "has another number of animation modes than the first mesh of its chain"),
            "a chained mesh without animation modes when the first has some": (inline_mesh(inline_chain(2, modes=[anim_mode()], last=inline_stream())),
                                                                               "has another number of animation modes than the first mesh of its chain"),
            "the third mesh of a chain with fewer animation modes": (inline_mesh(inline_chain(3, modes=[anim_mode(), anim_mode()], last=inline_stream(modes=[anim_mode()]))),
                                                                     "has another number of animation modes than the first mesh of its chain"),
            "a CEMT with another record size": (inline_mesh(emitters=1, cemt=chunk(b"CEMT", struct.pack("<i", 335) + bytes(336))), "336-byte emitter record size"),
            "fewer emitters than the record says": (inline_mesh(emitters=2, cemt=chunk(b"CEMT", struct.pack("<i", 336) + bytes(336))), "holds fewer emitters than its count"),
            "an emitter with a part and no index": (inline_mesh(emitters=1, cemt=chunk(b"CEMT", struct.pack("<i", 336) + bytes(0x40) + struct.pack("<I", 1) + bytes(336 - 0x44))),
                                                    "part index is missing or not a part of the mesh"),
            "an emitter whose index is not a part": (inline_mesh(emitters=1, cemt=chunk(b"CEMT", struct.pack("<i", 336) + bytes(0x40) + struct.pack("<I", 1) + bytes(336 - 0x44)
                                                                                         + struct.pack("<I", 9))), "part index is missing or not a part of the mesh"),
            "bytes after the last emitter": (inline_mesh(emitters=1, cemt=chunk(b"CEMT", struct.pack("<i", 336) + bytes(336) + b"x")), "bytes after its last emitter"),
            "emitters in the record and no CEMT": (inline_mesh(emitters=1, cemt=b""), "whose CEMT does not start with the 336-byte emitter record size"),
            "no bounding box at the end": (inline_mesh(stream=inline_stream()[:-48]), "without its bounding box"),
            "a chunk after the last": (inline_mesh(tail=chunk(b"JUNK", b"")), "bytes after its last chunk"),
            "a byte after the last": (inline_mesh(tail=b"x"), "bytes after its last chunk"),
            # chains: the framing, the reference, the ids, the length, the whole profile for each mesh
            "a chain word and 332 bytes after the mesh": (inline_mesh(stream=inline_stream(cmsh={8: 1}) + bytes(332)), "whose chain word is set and no chained mesh follows"),
            "a chain word and 333 bytes that are not a chained mesh": (inline_mesh(stream=inline_stream(cmsh={8: 1}) + bytes(333)), "is not framed by PMSH and PMS2 headers that fill the rest"),
            "a PMSH header with another tag": (inline_mesh(inline_chain(2, links={1: dict(pmsh_tag=b"PMSX")})), "is not framed by PMSH and PMS2 headers"),
            "a PMS2 header with another tag": (inline_mesh(inline_chain(2, links={1: dict(pms2_tag=b"PMS3")})), "is not framed by PMSH and PMS2 headers"),
            "a PMSH size one short": (inline_mesh(inline_chain(2, links={1: dict(pmsh_delta=-1)})), "is not framed by PMSH and PMS2 headers"),
            "a PMSH size one long": (inline_mesh(inline_chain(2, links={1: dict(pmsh_delta=1)})), "is not framed by PMSH and PMS2 headers"),
            "a PMS2 size one short": (inline_mesh(inline_chain(2, links={1: dict(pms2_delta=-1)})), "is not framed by PMSH and PMS2 headers"),
            "a PMS2 size one long": (inline_mesh(inline_chain(2, links={1: dict(pms2_delta=1)})), "is not framed by PMSH and PMS2 headers"),
            "a chained reference with a name": (inline_mesh(inline_chain(2, links={1: dict(name=b"x")})), "has a reference name that is not all zero"),
            "a chained reference with a second name byte set": (inline_mesh(inline_chain(2, links={1: dict(name=b"\0x")})), "has a reference name that is not all zero"),
            "a chained mesh with runtime id 19999": (inline_mesh(inline_chain(2, ids=[19999])), "has a runtime id outside 20000 to 29999"),
            "a chained mesh with runtime id 30000": (inline_mesh(inline_chain(2, ids=[30000])), "has a runtime id outside 20000 to 29999"),
            "a chained mesh with a shipped mesh's runtime id": (inline_mesh(inline_chain(2, ids=[277])), "has a runtime id outside 20000 to 29999"),
            "a chained mesh with the first mesh's runtime id": (inline_mesh(inline_chain(2, ids=[20000])), "or one that another mesh of the chain has"),
            "two chained meshes with one runtime id": (inline_mesh(inline_chain(3, ids=[20001, 20001])), "or one that another mesh of the chain has"),
            "the last chained mesh with the second one's runtime id": (inline_mesh(inline_chain(4, ids=[20001, 20002, 20001])), "or one that another mesh of the chain has"),
            "a chained reference with an optional word": (inline_mesh(inline_chain(2, links={1: dict(optional=1)})), "has an optional word set or a kept flag that is not 0 or 1"),
            "a chained reference with an optional word whose high byte is set": (inline_mesh(inline_chain(2, links={1: dict(optional=1 << 24)})), "has an optional word set or a kept flag that is not 0 or 1"),
            "a chained reference whose last name byte is set": (inline_mesh(inline_chain(2, links={1: dict(name=bytes(299) + b"x")})), "has a reference name that is not all zero"),
            "a chained reference with a kept flag of 2": (inline_mesh(inline_chain(2, links={1: dict(kept=2)})), "has an optional word set or a kept flag that is not 0 or 1"),
            "a chain of 17 meshes": (inline_mesh(inline_chain(17)), "a chain of more than 16 meshes"),
            "a last mesh whose chain word is set and nothing follows": (inline_mesh(inline_chain(2, last=inline_stream(cmsh={8: 1}))), "whose chain word is set and no chained mesh follows"),
            "a byte after the last chunk of the last mesh of a chain": (inline_mesh(inline_chain(2, last=inline_stream() + b"x")), "bytes after its last chunk"),
            "a byte after a whole chain, so that its headers no longer fill the rest": (inline_mesh(inline_chain(2) + b"x"), "is not framed by PMSH and PMS2 headers that fill the rest"),
            "a skeletal part without bones in a chained mesh": (inline_mesh(inline_chain(2, last=inline_stream(part=dict(words={0x8C: 3})))), "skeletal part with no bones or more than 19"),
            "cutscene sequences in a chained mesh": (inline_mesh(inline_chain(2, last=inline_stream(cmsh={0xC: 1}))), "with cutscene sequences (not admitted)"),
            "33 textures in a chained mesh": (inline_mesh(inline_chain(2, last=inline_stream(textures=33))), "count out of bounds"),
            "a chained mesh without its CMSH": (inline_mesh(inline_stream(cmsh={8: 1}) + chain_link(bytes(400))), "does not start with a 372-byte CMSH record"),
            "the totals of a chain (three meshes of 40000 vertices)": (inline_mesh(inline_chain(3, part=dict(pieces=[dict(vertex_count=40000)]))),
                                                                       "more than 100000 vertices, 600000 indices or 1024 pieces"),
        }
        for name, (mesh, words) in cases.items():
            with self.subTest(name):
                out, err = self.verdict(self.inline_archive(mesh))
                self.assertEqual(out, ["ACTIVE 0 -"], err)
                self.assertRefused(err, words)
                reached.update(line[line.index("an inline mesh"):] for line in err.splitlines() if "refused a:" in line and "an inline mesh" in line)
        self.assertEqual(sorted(set(re.findall(r'"(an inline mesh[^"]*)"', walk)) - reached), [], "refusals of the walk no case reached")

    def test_the_cache_rules_hold_for_every_combination_of_static_flags_words_counts_and_links(self):
        # CheckMeshPart holds every part's cache to four rules: its cached-frame count is 1 or its own virtual-frame count, a CPOS follows exactly when its position is not static, a CORI exactly when its
        # orientation is not, and only a part that some part lists is static. No rule is one a part's other flag, its other word, its count or its place in the mesh may switch off. So this runs the
        # product of everything the four rules read (the two flags, the two words, 8 pairs of virtual-frame and cached-frame counts, listed or not: 256 parts, each otherwise as a shipped part is)
        # against the rules as cache_reasons writes them, in the order the walk checks them. A rule held for the parts with one flag, one word, one count or one link only is a part that differs.
        alone, admitted, cells = set(), set(), 0
        for static, cpos, cori, (vframes, count), listed in itertools.product(itertools.product((0, 1), repeat=2), (False, True), (False, True),
                                                                                ((1, 0), (1, 1), (1, 2), (3, 0), (3, 1), (3, 2), (3, 3), (3, 4)), (True, False)):
            reasons = cache_reasons(static, cpos, cori, vframes, count, listed)
            name = f"flags {static}, {'a' if cpos else 'no'} CPOS, {'a' if cori else 'no'} CORI, {count} cached of {vframes} virtual frames, {'listed' if listed else 'not listed'}"
            with self.subTest(name):
                out, err = self.verdict(self.inline_archive(cache_mesh(static, (count, 16 * count if cpos else None, 48 * count if cori else None), vframes, listed)))
                if reasons:
                    self.assertEqual(out, ["ACTIVE 0 -"], err)
                    self.assertRefused(err, reasons[0])
                    for other in {CACHE_COUNT, CACHE_CPOS, CACHE_CORI, CACHE_PARENT} - {reasons[0]}:
                        self.assertNotIn(other, err)		# the whole answer is the first rule broken
                else:
                    self.assertEqual(out, ["ACTIVE 1 -"], err)
            cells += 1
            if not reasons:
                admitted.add(static)
            elif len(reasons) == 1:
                alone.add((static, reasons[0]))
        # and the product reaches, for each combination of the flags, a part the walk admits and a part that breaks each rule that can be broken for it, that rule only
        self.assertEqual(cells, 256)
        for static in itertools.product((0, 1), repeat=2):
            self.assertIn(static, admitted)
            for reason in (CACHE_COUNT, CACHE_CPOS, CACHE_CORI) + ((CACHE_PARENT,) if any(static) else ()):
                self.assertIn((static, reason), alone)

    def test_the_cache_rules_hold_for_a_part_of_every_kind_with_and_without_a_child(self):
        # The rules are the same for every part the walk admits, and the parts that list children matter most: a static part's walk up its parents stops at the first part that is not static in that
        # respect and reads its cached array, so that part (of any kind, and it lists the static one) is held to the relations like any other. So a part of each type (normal, null, emitter,
        # reference, skeletal, camera) and one with a collision grid or 2 hand frames, with and without a child of its own, is run through each combination of the static flags: a shape the walk
        # admits, and a shape that breaks each rule that can be broken for it, that rule alone (the product above runs the normal leaf part through every word, count and link).
        cells = 0
        for kind, children, static in itertools.product(("", "null", "emitter", "reference", "skeletal", "camera", "grid", "hand frames"), (False, True), itertools.product((0, 1), repeat=2)):
            pos, ori = not static[0], not static[1]
            shapes = {"as BuildCache leaves it": (pos, ori, 3, 3, True), "2 cached frames of 3 virtual frames": (pos, ori, 3, 2, True),
                      "a CPOS the wrong way": (not pos, ori, 3, 3, True), "a CORI the wrong way": (pos, not ori, 3, 3, True)}
            if any(static):
                shapes["no part lists it"] = (pos, ori, 3, 3, False)
            for shape, (cpos, cori, vframes, count, listed) in shapes.items():
                reasons = cache_reasons(static, cpos, cori, vframes, count, listed)
                self.assertEqual(len(reasons), 0 if shape == "as BuildCache leaves it" else 1, (kind, static, shape))
                with self.subTest(f"{kind or 'rigid'} part, {'with' if children else 'without'} a child, flags {static}, {shape}"):
                    out, err = self.verdict(self.inline_archive(cache_mesh(static, (count, 16 * count if cpos else None, 48 * count if cori else None), vframes, listed, children, kind)))
                    if reasons:
                        self.assertEqual(out, ["ACTIVE 0 -"], err)
                        self.assertRefused(err, reasons[0])
                        for other in {CACHE_COUNT, CACHE_CPOS, CACHE_CORI, CACHE_PARENT} - {reasons[0]}:
                            self.assertNotIn(other, err)		# the whole answer is the one rule broken
                    else:
                        self.assertEqual(out, ["ACTIVE 1 -"], err)
                cells += 1
        self.assertEqual(cells, 8 * 2 * (4 * 4 + 3))

    def test_only_one_inline_mesh_is_admitted_and_a_named_one_is_still_the_325_bytes(self):
        out, err = self.verdict(self.inline_archive(inline_mesh(), inline_mesh(runtime_id=20001)))
        self.assertEqual(out, ["ACTIVE 0 -"], err)
        self.assertRefused(err, "more than one inline (unnamed) MESH chunk")
        out, err = self.verdict(self.inline_archive(inline_mesh(), mesh_record(325)))
        self.assertEqual(out, ["ACTIVE 1 -"], err)
        out, err = self.verdict(self.inline_archive(inline_mesh(), mesh_record(326)))
        self.assertEqual(out, ["ACTIVE 0 -"], err)
        self.assertRefused(err, "named mesh whose size is not 325 bytes")

    def test_a_refusal_for_an_added_level_archive_says_the_level_cannot_load(self):
        # An archive the mod added has no copy of the game's to fall back to (a world file has its level's own world).
        archive = level_archive()
        d = self.f.mod("w", {LEVEL_ARCHIVE: archive})
        self.f.enable("w")
        out, err = self.run_driver(["has " + LEVEL_ARCHIVE, "!between", "has " + LEVEL_ARCHIVE, "open " + LEVEL_ARCHIVE],
                                   between=lambda: (d / LEVEL_ARCHIVE).write_bytes(archive + b"x"))
        self.assertEqual(out, ["HAS 1", "HAS 0", "NONE"], err)
        self.assertEqual(err.count("changed after it was checked at startup"), 1, err)
        self.assertEqual(err.count("the game has no such level archive, so the level cannot load"), 1, err)
        self.assertNotIn("the level's own world is read instead", err)
        self.assertNotIn("the game's own file is read", err)

    @unittest.skipUnless(shipped_archives(), "no installed game (BEA_GAME_DIR): the shipped level archives were NOT exercised")
    def test_every_shipped_level_archive_passes_the_walker(self):
        # The control for the walker's assumptions: each archive the game ships is offered, under its own number, as an added
        # archive to a build that admits every level number (PORT_MODS_TEST_ANY_LEVEL), and none may be refused for its structure.
        # The shipped archives that hold unnamed (inline) meshes are the one exception, and only for the inline-mesh rule: an added
        # archive may hold one, with a runtime id of the added range, and the shipped ones hold ids of the game's own, so each is
        # refused by that rule and by no other (test_every_shipped_inline_mesh_is_admitted_with_runtime_ids_an_added_archive_may_use).
        exe = self.exe or driver(defines=ANY_LEVEL)
        rest = None if self.exe else driver(defines=ANY_LEVEL + SKIP_INLINE)		# only the inline-mesh rule off
        archives = shipped_archives()
        passed = inline = rest_passed = 0
        for path in archives:
            level = int(path.name[:3])
            name = f"data/resources/{path.name}"
            with self.subTest(level):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                data = path.read_bytes()
                raw = mods.unpack(data)
                has_inline = any(raw[at:at + 4] == b"MESH" and raw[at + 8 + 16] == 0 for at in iter_chunks(raw))
                self.f.mod("shipped", {name: data})
                self.f.enable("shipped")
                out, err = self.f.run(["active", "has " + name], exe=exe)
                if has_inline:
                    self.assertEqual(out, ["ACTIVE 0 -", "HAS 0"], err)
                    self.assertRegex(err, r"refused shipped: file [^:]+: a level archive inline MESH chunk|refused shipped: file [^:]+: an inline mesh|more than one inline")
                    inline += 1
                    if rest:		# the rest of the walk must still pass such an archive: the inline rule fires first and would hide a fault after it
                        shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                        self.f.mod("shipped", {name: data})
                        self.f.enable("shipped")
                        out, err = self.f.run(["active", "has " + name], exe=rest)
                        self.assertEqual(out, ["ACTIVE 1 -", "HAS 1"], err)
                        rest_passed += 1
                    continue
                self.assertEqual(out, ["ACTIVE 1 -", "HAS 1"], err)
                self.assertIn(f"shipped adds {name}", err)
                passed += 1
        sys.stderr.write(f"\n{passed} of {len(archives)} shipped level archives passed the walker; {inline} hold an inline mesh and were refused by that rule; "
                         f"{rest_passed} of those {inline} pass the rest of the walk with only that rule off\n")
        self.assertGreater(passed, 0)
        if len(archives) == 66:		# the Steam edition's level archives: the split is a fact about the shipped data, so a change in it is noticed
            self.assertEqual((passed, inline), (13, 53))
        # And the same build refuses what the shipped ones are not: one without an inline mesh, offered as another level's archive.
        first = next(path for path in archives if not any(mods.unpack(path.read_bytes())[at:at + 4] == b"MESH" and mods.unpack(path.read_bytes())[at + 8 + 16] == 0
                                                          for at in iter_chunks(mods.unpack(path.read_bytes()))))
        other = "data/resources/%03d_res_PC.aya" % ((int(first.name[:3]) + 1) % 1000)
        shutil.rmtree(self.f.user / "mods", ignore_errors=True)
        self.f.mod("shipped", {other: first.read_bytes()})
        self.f.enable("shipped")
        out, err = self.f.run(["active"], exe=exe)
        self.assertEqual(out, ["ACTIVE 0 -"], err)
        self.assertRefused(err, "another level's number")

    @unittest.skipUnless(shipped_archives(), "no installed game (BEA_GAME_DIR): the shipped level archives were NOT exercised")
    def test_every_shipped_inline_mesh_is_admitted_with_runtime_ids_an_added_archive_may_use(self):
        # The control for the walk's profile, on real data and through the whole archive walk: each shipped archive that holds an inline mesh, changed in nothing but its runtime ids.
        exe = self.exe or driver(defines=ANY_LEVEL)
        archives = meshes = 0
        for path in shipped_archives():
            raw = bytearray(mods.unpack(path.read_bytes()))
            if inline_chunk(raw) is None:
                continue
            meshes += move_runtime_ids(raw)
            name = f"data/resources/{path.name}"
            with self.subTest(path.name[:3]):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("shipped", {name: mods.pack(bytes(raw))})
                self.f.enable("shipped")
                out, err = self.f.run(["active"], exe=exe)
                self.assertEqual(out, ["ACTIVE 1 -"], err)
                archives += 1
        sys.stderr.write(f"\n{archives} shipped archives that hold an inline mesh ({meshes} meshes with their chains) pass the whole walk with runtime ids of the added range\n")
        if len(shipped_archives()) == 66:		# the Steam edition: the numbers are a fact about the shipped data
            self.assertEqual((archives, meshes), (53, 139))

    @unittest.skipUnless(shipped_meshes(), "no installed game (BEA_GAME_DIR): the shipped loose meshes were NOT exercised")
    def test_every_shipped_loose_mesh_is_admitted_as_an_inline_mesh(self):
        # The second control, and the only real data with camera parts, which no shipped inline mesh holds: each loose mesh, as the stream of an inline mesh. A loose file frames a chained mesh
        # with a bare PMS2 header and not as an archive does, and one file has parts chained to each other, which the profile leaves out: those are the only ones refused.
        admitted, chained, parts = 0, 0, 0
        for path in shipped_meshes():
            stream = mods.unpack(path.read_bytes())
            with self.subTest(path.name):
                out, err = self.verdict(self.inline_archive(inline_mesh(stream)))
                if out == ["ACTIVE 1 -"]:
                    admitted += 1
                elif "is not framed by PMSH and PMS2 headers that fill the rest of the stream" in err:
                    chained += 1
                else:
                    self.assertIn("a chain, older-format vertices or triangles, or bone weights or slots", err)
                    parts += 1
        sys.stderr.write(f"\n{admitted} of the {admitted + chained + parts} shipped loose meshes are admitted as inline meshes; {chained} are framed as loose chains, {parts} have chained parts\n")
        if len(shipped_meshes()) == 213:		# the Steam edition
            self.assertEqual((admitted, chained, parts), (205, 7, 1))

    def test_has_answers_by_name_and_never_when_mods_are_off(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable("good")
        out, _ = self.run_driver(["has " + SND, "has data/other.bin", "has /" + SND, "has C:\\" + SND.replace("/", "\\")])
        self.assertEqual(out, ["HAS 1", "HAS 0", "HAS 0", "HAS 0"])
        out, _ = self.run_driver(["has " + SND], env={"BEA_PORT_TAPE": "t"})
        self.assertEqual(out, ["HAS 0"])

    def test_the_first_read_of_each_replaced_file_is_logged_once(self):
        self.f.mod("good", {SND: b"new", "data/other.bin": b"other"})
        self.f.enable("good")
        out, err = self.run_driver(["open " + SND, "open " + SND, "open data/other.bin", "open " + SND])
        self.assertEqual(err.count("good serves " + SND + " to the game"), 1, err)
        self.assertEqual(err.count("good serves data/other.bin to the game"), 1, err)
        out, err = self.run_driver(["active"])
        self.assertNotIn("serves", err, "nothing was read, so nothing was served")

    def test_symlinked_file_is_refused(self):
        secret = self.f.outside / "secret"
        secret.write_bytes(b"secret")
        d = self.f.mod("link", {SND: b"secret"})
        (d / SND).unlink()
        (d / SND).symlink_to(secret)
        self.f.enable("link")
        out, err = self.run_driver(["active", "open " + SND])
        self.assertEqual(out, ["ACTIVE 0 -", "NONE"])
        self.assertRefused(err, "a link")

    def test_symlinked_folder_on_the_path_is_refused(self):
        (self.f.outside / "sounds").mkdir()
        (self.f.outside / "sounds/test.wav").write_bytes(b"secret")
        d = self.f.mod("dirlink", {SND: b"secret"})
        shutil.rmtree(d / "data/sounds")
        (d / "data/sounds").symlink_to(self.f.outside / "sounds")
        self.f.enable("dirlink")
        out, err = self.run_driver(["active", "open " + SND])
        self.assertEqual(out, ["ACTIVE 0 -", "NONE"])
        self.assertRefused(err, "link or not a folder")

    def test_symlinked_mod_folder_and_mods_folder_are_refused(self):
        real = self.f.outside / "realmod"
        self.f.mod("tmp", {SND: b"new"})
        shutil.move(str(self.f.user / "mods/tmp"), real)
        m = json.loads((real / "mod.json").read_text())
        m["id"] = "linked"
        (real / "mod.json").write_text(json.dumps(m))
        (self.f.user / "mods/linked").symlink_to(real)
        self.f.enable("linked")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "no such folder")
        # the mods folder itself
        shutil.rmtree(self.f.user / "mods")
        (self.f.outside / "mods").mkdir()
        shutil.move(str(real), self.f.outside / "mods/linked")
        (self.f.user / "mods").symlink_to(self.f.outside / "mods")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "no such folder")

    def test_hard_link_fifo_and_folder_are_refused(self):
        other = self.f.outside / "other"
        other.write_bytes(b"hard")
        d = self.f.mod("hard", {SND: b"hard"})
        (d / SND).unlink()
        os.link(other, d / SND)
        self.f.enable("hard")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "single name")
        (d / SND).unlink()
        os.mkfifo(d / SND)
        out, err = self.run_driver(["active"])		# must not block on the FIFO
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "not a regular file")

    def test_symlinked_manifest_is_refused(self):
        d = self.f.mod("mlink", {SND: b"new"})
        (self.f.outside / "mod.json").write_text((d / "mod.json").read_text())
        (d / "mod.json").unlink()
        (d / "mod.json").symlink_to(self.f.outside / "mod.json")
        self.f.enable("mlink")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "refused mlink: mod.json is missing, a link, or not a regular file: a link")

    def test_a_refused_mod_json_says_why(self):
        # The open chain's own reason follows the general one (on Windows it is the one that says a Mod.json is spelled in another case).
        (self.f.mod("nomanifest", {SND: b"new"}) / "mod.json").unlink()
        folder = self.f.mod("folder", {SND: b"new"}) / "mod.json"
        folder.unlink()
        folder.mkdir()
        status, lines, _ = self.f.check("nomanifest", "folder", exe=self.check_exe())
        self.assertEqual(status, 1, lines)
        refused = [line for line in lines if line.startswith("refused")]
        self.assertEqual(len(refused), 2, lines)
        self.assertEqual(refused[0], "refused nomanifest: mod.json is missing, a link, or not a regular file: a file that cannot be opened")
        self.assertTrue(refused[1].startswith("refused folder: mod.json is missing, a link, or not a regular file: "), refused)
        self.assertEqual(lines[-1], "result: 0 passed, 2 refused")

    # ---- mod.json ----------------------------------------------------------------------------------------------------
    def test_bad_manifests_are_refused(self):
        good = {"schema": 1, "id": "m", "name": "n", "version": "1",
                "files": [{"path": SND, "size": 3, "sha256": hashlib.sha256(b"new").hexdigest()}]}
        cases = {
            "trailing comma": (json.dumps(good)[:-1] + ",}", "a string was expected"),
            "missing comma": (json.dumps(good).replace(', "name"', ' "name"'), "a closing bracket"),
            "repeated key": ('{"schema":1,"schema":1}', "a repeated key"),
            "unknown key": (json.dumps({**good, "script": "x"}), "an unknown key"),
            "unknown file key": (json.dumps({**good, "files": [{**good["files"][0], "exec": 1}]}), "unknown key in a files"),
            "schema 2": (json.dumps({**good, "schema": 2}), "schema is not 1"),
            "id mismatch": (json.dumps({**good, "id": "other"}), "id is not the mod folder's name"),
            "float size": (json.dumps(good).replace('"size": 3', '"size": 3.0'), "not an integer"),
            "negative size": (json.dumps(good).replace('"size": 3', '"size": -3'), "lacks path"),
            "string size": (json.dumps(good).replace('"size": 3', '"size": "3"'), "lacks path"),
            "no files": (json.dumps({**good, "files": []}), "files is missing"),
            "no name": (json.dumps({k: v for k, v in good.items() if k != "name"}), "name is missing"),
            "long name": (json.dumps({**good, "name": "x" * 65}), "name is missing"),
            "upper hash": (json.dumps(good).replace(good["files"][0]["sha256"], good["files"][0]["sha256"].upper()),
                           "64 lowercase"),
            "huge size": (json.dumps(good).replace('"size": 3', f'"size": {64 * 2**20 + 1}'), "larger than 64 MiB"),
            "not an object": ("[1,2]", "not an object"),
            "text after": (json.dumps(good) + " x", "text after"),
            "deep": ('{"files":' + "[" * 9 + "]" * 9 + "}", "nesting deeper"),
            "control char": (json.dumps(good).replace('"n"', '"a\u0001b"'), "control character"),
            "nul escape": (json.dumps(good).replace('"n"', '"a\\u0000b"'), "NUL"),
            "bad version": (json.dumps({**good, "ce_min": "one"}), "ce_min or ce_max"),
            "empty": ("", "a value was expected"),
            "listed twice": (json.dumps({**good, "files": good["files"] * 2}), "listed twice"),
        }
        for i in range(4):
            bad = list("0000")
            bad[i] = "z"
            for field in ("name", "path"):
                text = json.dumps(good)
                old = '"n"' if field == "name" else json.dumps(SND)
                text = text.replace(old, '"\\u' + ''.join(bad) + '"', 1)
                cases[f"bad unicode {field} {i}"] = (text, "a bad \\u escape")
        cases["escaped duplicate key"] = ('{"schema":1,"sch\\u0065ma":1}', "a repeated key")
        for name, (text, words) in cases.items():
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("m", {SND: b"new"}, raw=text)
                self.f.enable("m")
                out, err = self.run_driver(["active", "open " + SND])
                self.assertEqual(out, ["ACTIVE 0 -", "NONE"], err)
                self.assertRefused(err, words)

    def test_oversized_manifest_is_refused(self):
        self.f.mod("big", {SND: b"new"}, manifest={"description": "x" * 1000})
        d = self.f.user / "mods/big"
        text = (d / "mod.json").read_text()
        (d / "mod.json").write_text(text[:-1] + " " * (256 * 1024) + "}")
        self.f.enable("big")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "larger than 256 KiB")

    def test_hash_and_size_mismatches_are_refused(self):
        d = self.f.mod("hash", {SND: b"new"})
        (d / SND).write_bytes(b"neW")
        self.f.enable("hash")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "SHA-256 is not the manifest's")
        (d / SND).write_bytes(b"newer")
        out, err = self.run_driver(["active"])
        self.assertRefused(err, "size is not the manifest's")

    def test_one_bad_file_refuses_the_whole_mod(self):
        d = self.f.mod("half", {SND: b"new", "data/other.bin": b"ok"})
        (d / SND).write_bytes(b"bad")
        self.f.enable("half")
        out, err = self.run_driver(["active", "open data/other.bin"])
        self.assertEqual(out, ["ACTIVE 0 -", "NONE"])

    def test_names_in_the_log_are_ascii(self):
        self.f.mod("names", {SND: b"new"}, manifest={"name": "Caf\u00e9 \u001b[31m", "author": "x\ny"})
        self.f.enable("names")
        out, err = self.run_driver(["active"])
        # a raw control character is refused; the escaped ones are cleaned
        self.assertEqual(out, ["ACTIVE 1 -"])
        self.assertNotIn("\x1b", err)
        line = [l for l in err.splitlines() if "active 1:" in l][0]
        self.assertTrue(all(32 <= ord(c) < 127 for c in line), line)

    # ---- packed files ------------------------------------------------------------------------------------------------
    def block(self, data):
        z = zlib.compress(data)
        return struct.pack("<I", len(z)) + z

    def test_packed_structure_is_checked(self):
        big = bytes(1 << 20)
        cases = {
            "short block first": (self.block(b"x" * 100) + self.block(big), "under 1 MiB before the last"),
            "block over 1 MiB": (self.block(bytes((1 << 20) + 1)), "more than 1 MiB"),
            "trailing bytes": (mods.pack(b"x" * 10) + b"\0\0", "cut block header"),
            "cut block": (mods.pack(b"x" * 10)[:-1], "cut block"),
            "not zlib": (struct.pack("<I", 4) + b"abcd", "does not unpack"),
            "trailing inside block": (struct.pack("<I", len(zlib.compress(b"ok")) + 4) +
                                      zlib.compress(b"ok") + b"junk", "does not unpack"),
            "zero length": (struct.pack("<I", 0), "larger than the game's block buffer"),
            "huge length": (struct.pack("<I", 0x102928) + bytes(0x102928), "larger than the game's block buffer"),
            "empty": (b"", "an empty packed file"),
            "65 MiB in blocks": (self.block(big) * 65, "unpacks to more than 64 MiB"),
            "64 MiB and one byte": (self.block(big) * 64 + self.block(b"x"), "unpacks to more than 64 MiB"),
        }
        for name, (data, words) in cases.items():
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("aya", {"data/x.aya": data})
                (self.f.game / "data/x.aya").write_bytes(b"orig")
                self.f.enable("aya")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 0 -"], err)
                self.assertRefused(err, words)
        shutil.rmtree(self.f.user / "mods")
        self.f.mod("aya", {"data/x.aya": self.block(big) + self.block(big) + self.block(b"tail")})
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 1 -"], err)
        shutil.rmtree(self.f.user / "mods")
        self.f.mod("aya", {"data/x.aya": self.block(big) * 64})
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 1 -"], "exactly 64 MiB unpacked is the most a packed file may hold: " + err)

    def test_textures_must_be_dds_within_the_game_limit(self):
        cases = {"not DDS": (mods.pack(b"TGA!" + bytes(200)), "not a DDS image"),
                 "too big": (mods.pack(dds() + bytes(0x500000)), "5 MiB or more"),
                 "too wide": (mods.pack(dds(8192, 4)), "over 4096"),
                 "too tall": (mods.pack(dds(4, 8192)), "over 4096"),
                 "zero wide": (mods.pack(dds(0, 4)), "over 4096"),
                 "zero tall": (mods.pack(dds(4, 0)), "over 4096")}
        for name, (data, words) in cases.items():
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("t", {TEX: data})
                self.f.enable("t")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 0 -"], err)
                self.assertRefused(err, words)
        for name, data in {"4096 wide": mods.pack(dds(4096, 4)), "4096 tall": mods.pack(dds(4, 4096))}.items():
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("t", {TEX: data})
                self.f.enable("t")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 1 -"], err)

    # ---- run time ----------------------------------------------------------------------------------------------------
    def test_file_changed_after_startup_is_not_read(self):
        d = self.f.mod("swap", {SND: b"new", "data/other.bin": b"o"})
        self.f.enable("swap")
        secret = self.f.outside / "secret"
        secret.write_bytes(b"new")

        def swap():
            (d / SND).unlink()
            (d / SND).symlink_to(secret)
            p = d / "data/other.bin"
            p.write_bytes(b"O")

        out, err = self.run_driver(["active", "!between", "open " + SND, "open data/other.bin", "open data/other.bin"],
                                   between=swap)
        self.assertEqual(out, ["ACTIVE 1 -", "NONE", "NONE", "NONE"])
        self.assertIn("can no longer be opened safely", err)
        self.assertEqual(err.count("changed after it was checked"), 1, "a refusal is logged once")

    def test_has_follows_the_run_time_check_so_a_changed_world_file_falls_back_to_the_archive(self):
        # CWorld::Load skips the level archive's copy when PortModHas says yes. If the answer came from the startup table
        # alone, a world file that changed afterwards (an editor saving it, a backup hard-linking it, a chmod) would send
        # the game to a file the file layer then refuses, and the level would not load ("data files are corrupt"). The
        # answer follows the identity check the read makes, so the level's own world is read instead.
        world = "data/worlds/world100.wrd"
        secret = self.f.outside / "secret"
        secret.write_bytes(b"world body")	# the same bytes the manifest lists, behind a link
        changed = "changed after it was checked at startup"
        unsafe = "can no longer be opened safely"
        cases = {
            "rewritten": (lambda p: p.write_bytes(b"world body, edited"), changed),
            "same size, other bytes": (lambda p: p.write_bytes(b"WORLD BODY"), changed),
            "chmod only": (lambda p: (time.sleep(0.05), os.chmod(p, 0o600)), changed),
            "hard link added": (lambda p: os.link(p, p.with_name("backup.wrd")), unsafe),
            "replaced by a link": (lambda p: (p.unlink(), p.symlink_to(secret)), self.link_reason),
            "deleted": (lambda p: p.unlink(), unsafe),
        }
        for name, (change, reason) in cases.items():
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                d = self.f.mod("w", {world: b"world body"})
                self.f.enable("w")
                out, err = self.run_driver(["has " + world, "has " + world, "!between", "has " + world, "has " + world,
                                            "open " + world], between=lambda: change(d / world))
                self.assertEqual(out, ["HAS 1", "HAS 1", "HAS 0", "HAS 0", "NONE"], err)
                self.assertEqual(err.count(reason), 1, "a refusal is logged once")
                self.assertEqual(err.count("the level's own world is read instead"), 1, err)
                self.assertNotIn("the game's own file is read", err)
                self.assertNotIn("the read fails", err)

    def test_has_says_yes_while_the_file_is_untouched(self):
        world = "data/worlds/world100.wrd"
        self.f.mod("w", {world: b"world body", SND: b"new sound"})
        self.f.enable("w")
        out, err = self.run_driver(["has " + world, "has " + SND, "has " + world, "open " + world, "has " + world])
        self.assertEqual(out, ["HAS 1", "HAS 1", "HAS 1", digest(b"world body"), "HAS 1"], err)
        self.assertNotIn("changed after", err)
        self.assertNotIn("can no longer", err)

    def test_a_refusal_says_what_the_game_reads_instead(self):
        world = "data/worlds/world100.wrd"
        own_file, own_world, no_copy = ("the game's own file is read", "the level's own world is read instead",
                                        "the game has no file of that name, so the read fails")
        for name, commands, expected, then in (
                ("a replaced file asked by name", ["has " + SND], ["HAS 0"], own_file),
                ("a replaced file read", ["open " + SND], ["NONE"], own_file),
                ("an added world asked by name", ["has " + world], ["HAS 0"], own_world),
                ("an added world read without asking", ["open " + world, "has " + world], ["NONE", "HAS 0"], no_copy)):
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                d = self.f.mod("w", {world: b"world body", SND: b"new sound"})
                self.f.enable("w")

                def change():
                    (d / world).write_bytes(b"world body, edited")
                    (d / SND).write_bytes(b"new sound, edited")

                out, err = self.run_driver(["active", "!between"] + commands, between=change)
                self.assertEqual(out, ["ACTIVE 1 -"] + expected, err)
                self.assertEqual(err.count("changed after it was checked at startup"), 1, err)
                self.assertEqual([w for w in (own_file, own_world, no_copy) if w in err], [then], err)

    def test_opened_mod_is_an_immutable_verified_snapshot(self):
        d = self.f.mod("held", {SND: b"good"})
        self.f.enable("held")
        def mutate():
            (d / SND).write_bytes(b"evil")  # the same inode, after PortModOpenRead returned
        out, err = self.run_driver(["hold " + SND, "held-write", "!between", "held-read", "open " + SND],
                                   between=mutate)
        self.assertEqual(out, ["HELD", f"WRITE -1 {errno.EPERM}", digest(b"good"), "NONE"], err)
        self.assertIn("changed after it was checked", err)

    def test_the_hash_at_open_catches_changed_bytes_even_when_the_identity_is_blind(self):
        # The identity only detects an ordinary change (the next test shows one that keeps it): the bytes are hashed at every open. A driver whose identity comparison
        # always says "same" must still refuse bytes that changed and kept the size, and keep refusing them; asking by name does not hash (PortModHas), the read does.
        data = b"same size " * 20
        self.f.mod("good", {SND: data})
        self.f.enable("good")
        target = self.f.user / "mods/good" / SND
        blind = driver(blind_identity(self.source or MODS_CPP.read_text()))
        out, err = self.run_driver(["open " + SND, "!between", "has " + SND, "open " + SND, "open " + SND], exe=blind,
                                   between=lambda: target.write_bytes(bytes(reversed(data))))
        self.assertEqual(out, [digest(data), "HAS 1", "NONE", "NONE"], err)
        self.assertEqual(err.count("no longer matches its checked SHA-256"), 1, "a refusal is logged once")
        self.assertNotIn("changed after it was checked at startup", err)

    def test_a_change_that_keeps_the_identity_is_refused_by_the_hash_at_open(self):
        # Without any privilege: a second write through a shared writable mapping whose page is already dirty changes the bytes without a page fault, so neither the
        # inode, the size, the write time nor the change time moves, and the identity cannot tell. The hash at open must. If the page was written back between the two
        # writes (then the second one faults and moves the times), the case is made again; a file system that moves them on every write cannot show this, and the test says so.
        # The mutant row of this guard is the blind-identity test's (MUTANTS: "hash at open not compared"); without the guard this test fails too.
        data = b"A" * 4096 + b"B" * 100
        target = self.f.user / "mods/good" / SND
        for attempt in range(5):
            shutil.rmtree(self.f.user / "mods", ignore_errors=True)
            self.f.mod("good", {SND: data})
            self.f.enable("good")
            seen = {}
            fd = os.open(target, os.O_RDWR)
            try:
                with mmap.mmap(fd, len(data)) as m:
                    m[0:1] = m[0:1]		# dirties the page with its own byte: the times move now, before the loader's startup check

                    def change():
                        seen["before"] = identity(target)
                        m[1:2] = b"Z"		# a write to the dirty page: no fault, no time update
                        seen["after"] = identity(target)

                    out, err = self.run_driver(["open " + SND, "!between", "has " + SND, "open " + SND, "open " + SND], between=change)
            finally:
                os.close(fd)
            if seen["before"] == seen["after"]:
                break
        else:
            self.skipTest("the times moved at every try: this file system updates them at each write through a mapping, so the case cannot be made here")
        self.assertNotEqual(target.read_bytes(), data, "precondition: the bytes changed")
        self.assertEqual(out, [digest(data), "HAS 1", "NONE", "NONE"], err)
        self.assertEqual(err.count("no longer matches its checked SHA-256"), 1, err)
        self.assertNotIn("changed after it was checked at startup", err)

    # ---- bounds and versions -----------------------------------------------------------------------------------------
    def test_at_most_sixteen_mods(self):
        ids = [f"m{i:02}" for i in range(17)]
        for i in ids:
            self.f.mod(i, {SND: i.encode()})
        self.f.enable(*ids)
        out, err = self.run_driver(["active", "open " + SND])
        self.assertEqual(out, ["ACTIVE 16 -", digest(b"m15")])
        self.assertIn("refused m16: at most 16", err)

    def test_total_file_bound_counts_overridden_entries(self):
        # Lower the production limit in an authored test build so three overlapping manifests exercise it cheaply.
        source = MODS_CPP.read_text()
        needle = "kMaxFilesTotal = 8192"
        self.assertEqual(source.count(needle), 1)
        exe = driver(source.replace(needle, "kMaxFilesTotal = 4"))
        for mod_id in ("one", "two", "three"):
            self.f.mod(mod_id, {SND: mod_id.encode(), "data/other.bin": b"x"})
        self.f.enable("one", "two", "three")
        out, err = self.f.run(["active", "open " + SND], exe=exe)
        self.assertEqual(out, ["ACTIVE 2 -", digest(b"two")], err)
        self.assertIn("refused three", err)
        weakened = source.replace(needle, "kMaxFilesTotal = 4").replace(
            "admittedEntries + mine.size()", "((void)admittedEntries, files.size() + mine.size())")
        out, _ = self.f.run(["active", "open " + SND], exe=driver(weakened))
        self.assertEqual(out, ["ACTIVE 3 -", digest(b"three")])

    def test_more_than_1_gib_is_refused(self):
        # 17 sparse 64 MiB files: the 17th crosses 1 GiB (hashing ~1 GiB of zeros takes a few seconds).
        files = {}
        for i in range(17):
            rel = f"data/big{i}.bin"
            (self.f.game / rel).write_bytes(b"g")
            files[rel] = None
        d = self.f.user / "mods/huge"
        (d / "data").mkdir(parents=True)
        zero = hashlib.sha256()
        chunk = bytes(1 << 20)
        for _ in range(64):
            zero.update(chunk)
        for rel in files:
            with open(d / rel, "wb") as fh:
                fh.truncate(64 << 20)
        m = {"schema": 1, "id": "huge", "name": "h", "version": "1",
             "files": [{"path": rel, "size": 64 << 20, "sha256": zero.hexdigest()} for rel in files]}
        (d / "mod.json").write_text(json.dumps(m))
        self.f.enable("huge")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "more than 1 GiB")

    def test_ce_version_range(self):
        exe = self.ce_exe or driver(ce_version="0.1.9")
        cases = {("0.1.8", "0.2"): 1, ("0.1.9", "0.1.9"): 1, ("0.2", None): 0, (None, "0.1.8"): 0, ("0.1", None): 1}
        for (lo, hi), active in cases.items():
            with self.subTest(lo=lo, hi=hi):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                extra = {k: v for k, v in (("ce_min", lo), ("ce_max", hi)) if v}
                self.f.mod("v", {SND: b"new"}, manifest=extra)
                self.f.enable("v")
                out, err = self.f.run(["active"], exe=exe)
                self.assertEqual(out, [f"ACTIVE {active} -"], err)
                if not active:
                    self.assertRefused(err, "outside the mod's ce_min..ce_max")
        out, err = self.run_driver(["active"])		# a development build does not check the range
        self.assertIn("development build", err)

    def test_development_version_does_not_compare_uninitialized_numbers(self):
        # A fail-fast instrumented build proves the comparator is not called when no version is available.
        source = MODS_CPP.read_text()
        absent = "\t(void)out;\n\treturn false;"
        compare = "int VersionCompare(const unsigned a[4], const unsigned b[4])\n{"
        self.assertEqual(source.count(absent), 1)
        self.assertEqual(source.count(compare), 1)
        source = source.replace(absent, "\tout[0] = 0xdeadbeefu;\n\treturn false;")
        source = source.replace(compare, compare + "\n\tif (a[0] == 0xdeadbeefu) abort();")
        exe = driver(source)
        self.f.mod("dev", {SND: b"new"}, manifest={"ce_min": "99.0", "ce_max": "100.0"})
        self.f.enable("dev")
        out, err = self.f.run(["active", "open " + SND], exe=exe)
        self.assertEqual(out, ["ACTIVE 1 -", digest(b"new")], err)
        self.assertIn("development build", err)

    def test_every_mutant_makes_a_change_of_its_own(self):
        # Two rows that make one change would count one guard twice in the mutants' tally. Each row's edit is applied as run_mutants applies it, and no two
        # rows may leave the same text of mods.cpp with the same defines, whatever text they anchor on. (Two edits whose texts differ but compile alike are
        # not caught here.)
        source = MODS_CPP.read_text()
        self.assertEqual([m[0] for m in MUTANTS if source.count(m[1]) != 1], [], "rows whose text is not in mods.cpp exactly once")
        seen = {}
        for name, old, new, _, *defines in MUTANTS:
            mutant = source.replace(old, new)
            self.assertNotEqual(mutant, source, f"{name} changes nothing")
            key = (hashlib.sha256(mutant.encode()).hexdigest(), tuple(defines[0]) if defines else ())
            self.assertNotIn(key, seen, f"{name} makes the change that {seen.get(key)} makes")
            seen[key] = name
        self.assertEqual(len({m[0] for m in MUTANTS}), len(MUTANTS), "two mutants share a name")


# ---- mutants ---------------------------------------------------------------------------------------------------------
CHECK_VERSION = "0.2.0"		# the CE version the check mode's Windows driver is built with (its command is compiled into public builds only)


@unittest.skipUnless(os.environ.get("MODS_REAL_EXE"), "set MODS_REAL_EXE to the bea of a Linux public build (in its build folder, beside lib/) to run this")
class RealExeCheckTests(unittest.TestCase):
    """bea --check-mods of a real public build: the program's own entry point answers as the driver does, and writes nothing. Authored fixtures only."""

    def setUp(self):
        BUILD.mkdir(parents=True, exist_ok=True)
        self._tmp = tempfile.TemporaryDirectory(prefix="mods-real-", dir=BUILD)
        self.f = Fixture(self._tmp.name)
        self.exe = Path(os.environ["MODS_REAL_EXE"])

    def tearDown(self):
        self._tmp.cleanup()

    def test_the_real_program_checks_every_mod_and_writes_nothing(self):
        tex = mods.pack(dds())
        self.f.mod("good", {TEX: tex, SND: b"new"})
        other = b"other bytes"
        self.f.mod("bad", {SND: b"new", "data/other.bin": other}, manifest={"files": [
            {"path": SND, "size": 3, "sha256": "0" * 64}, {"path": "data/other.bin", "size": len(other) + 1, "sha256": hashlib.sha256(other).hexdigest()}]})
        self.f.enable("good", "bad")
        before = self.f.tree()
        status, lines, err = self.f.check(exe=self.exe)
        self.assertEqual(status, 1, (lines, err))
        self.assertEqual([line for line in lines if line.startswith("refused")],
                         [f"refused bad: file {SND}: its SHA-256 is not the manifest's", "refused bad: file data/other.bin: its size is not the manifest's"], lines)
        self.assertIn("1 active, 2 game files replaced (experimental)", lines)
        self.assertEqual(lines[-1], "result: 1 passed, 1 refused")
        self.assertNotIn("development build", "\n".join(lines), "a public build checks the CE version ranges")
        status, lines, err = self.f.check("good", exe=self.exe)
        self.assertEqual((status, lines[-1]), (0, "result: 1 passed, 0 refused"), (lines, err))
        self.assertEqual(self.f.tree(), before)

    def test_the_real_program_creates_no_user_folder(self):
        missing = self.f.tmp / "no-such-user-folder"
        status, lines, err = self.f.check(exe=self.exe, env={"BEA_USER_DIR": str(missing)})
        self.assertEqual((status, lines), (0, ["no mods enabled: ce.ini has no usable mods= line", "result: 0 passed, 0 refused"]), err)
        self.assertFalse(missing.exists())
        # a named mod: the loader then asks for the user folder (PortUserDir), which must not make it either
        status, lines, err = self.f.check("good", exe=self.exe, env={"BEA_USER_DIR": str(missing)})
        self.assertEqual(status, 1, (lines, err))
        self.assertIn("refused good: no such folder in the user folder's mods folder (or the folder is a link)", lines)
        self.assertEqual(lines[-1], "result: 0 passed, 1 refused")
        self.assertFalse(missing.exists())


CACHE_PRODUCT = "test_the_cache_rules_hold_for_every_combination_of_static_flags_words_counts_and_links"
CACHE_KINDS = "test_the_cache_rules_hold_for_a_part_of_every_kind_with_and_without_a_child"

MUTANTS = [
    ("check mode stops at a mod's first refused file", "if (!gCheck)\n\t\t\treturn bad;\n\t\tLogRefusal(id, bad);", "return bad;",
     "test_check_mode_prints_every_refused_file_where_the_game_stops_at_the_first"),
    ("check mode stops at a manifest's first refused field", "if (!gCheck) return (why); LogRefusal(id, (why));", "return (why);",
     "test_check_mode_reads_on_after_a_refused_manifest_field"),
    ("the game reads on after a mod's first refused file", "if (!gCheck)\n\t\t\treturn bad;", "if (false)\n\t\t\treturn bad;",
     "test_check_mode_prints_every_refused_file_where_the_game_stops_at_the_first"),
    ("refusals not counted for the status", "\tgCheckRefusals++;\n", "", "test_check_mode_prints_every_refused_file_where_the_game_stops_at_the_first"),
    ("the status ignores the refusals", "return gCheckRefusals ? 1 : 0;", "return 0;", "test_check_mode_prints_every_refused_file_where_the_game_stops_at_the_first"),
    ("a refused mod counts as passed", "			total = before;\n			if (Opened(mod.root))\n				CloseFile(mod.root);\n			continue;",
     "			total = before;\n			(void)0;", "test_check_mode_prints_every_refused_file_where_the_game_stops_at_the_first"),
    ("check mode keeps the game's line limit", "if (gCheck)\n\t{\n\t\tva_start", "if (false)\n\t{\n\t\tva_start", "test_check_mode_has_no_line_limit"),
    ("check mode ignores the named mods", "if (gCheckIds)\n\t\tids = *gCheckIds;\n\telse\n", "", "test_check_mode_takes_the_named_mods_and_otherwise_the_ce_ini_line"),
    ("check mode silent when nothing is enabled", "if (gCheck)\n\t\t\t\tLog(\"no mods enabled: ce.ini has no usable mods= line\");\n", "", "test_check_mode_says_when_nothing_is_enabled"),
    ("a fence stops the check mode", "if (gFence && gCheck)", "if (false)", "test_check_mode_is_not_fenced_and_says_what_the_fence_would_do"),
    ("no result line", "Log(\"result: %u passed, %d refused\", (unsigned)gMods.size(), gCheckRefusedMods);", "", "test_check_mode_passes_what_the_loader_would_load"),
    ("a refused mod not counted in the result", "			gCheckRefusedMods++;\n			total = before;\n			if (Opened(mod.root))", "			total = before;\n			if (Opened(mod.root))",
     "test_check_mode_prints_every_refused_file_where_the_game_stops_at_the_first"),
    ("a refused id not counted in the result", "LogRefusal(Clean(id, 48), \"a mod id is 1 to 48 lowercase letters, digits, '-' or '_'\");\n\t\t\tgCheckRefusedMods++;",
     "LogRefusal(Clean(id, 48), \"a mod id is 1 to 48 lowercase letters, digits, '-' or '_'\");", "test_check_mode_counts_ids_it_cannot_take"),
    ("check mode runs with the loader off", "if (const char *off = PlatformOff())\n\t{\n\t\tLog(\"%s\", off);\n\t\treturn 3;", "if (false)\n\t{\n\t\treturn 3;",
     "test_check_mode_stops_at_once_in_a_build_with_the_loader_off", PLATFORM_OFF),
    ("loader-off status is success", "return 3;", "return 0;", "test_check_mode_stops_at_once_in_a_build_with_the_loader_off", PLATFORM_OFF),
    ("check mode reads on past the limit of the whole set", "if (bad == kTooMuch)\n\t\t\tbreak;", "(void)kTooMuch;", "test_check_mode_stops_a_mod_at_the_limit_of_the_whole_set"),
    ("'..' allowed", 'if (part == "." || part == "..")', 'if (part == ".")', "test_path_escapes_are_refused"),
    ("final link followed", "O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC", "O_RDONLY | O_NONBLOCK | O_CLOEXEC",
     "test_symlinked_file_is_refused"),
    ("folder link followed", "O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);\n\t\tclose(dir);",
     "O_PATH | O_DIRECTORY | O_CLOEXEC);\n\t\tclose(dir);", "test_symlinked_folder_on_the_path_is_refused"),
    ("hard links allowed", "!S_ISREG(st.st_mode) || st.st_nlink != 1", "!S_ISREG(st.st_mode)",
     "test_hard_link_fifo_and_folder_are_refused"),
    ("tape fence dropped", '"BEA_PORT_TAPE", ', "", "test_every_fence_keeps_mods_off"),
    ("network fence dropped", '"BEA_NET_HOST", ', "", "test_every_fence_keeps_mods_off"),
    ("network handoff fence dropped", ', "BEA_NET_HANDOFF"', "", "test_every_fence_keeps_mods_off"),
    ("unicode digit not checked", "if (digit < 0)", "if (false)", "test_bad_manifests_are_refused"),
    ("hash not compared", "if (memcmp(sum, e.sha, 32) != 0)", "if (false)", "test_hash_and_size_mismatches_are_refused"),
    ("repeated keys allowed", 'return Fail("a repeated key");', "{}", "test_bad_manifests_are_refused"),
    ("unknown keys allowed", 'REFUSE("an unknown key in the manifest', 'if (0) REFUSE("an unknown key in the manifest',
     "test_bad_manifests_are_refused"),
    ("short block allowed", 'return "a packed block under 1 MiB before the last block";', "{}",
     "test_packed_structure_is_checked"),
    ("trailing compressed bytes allowed", " || consumed != packed", "", "test_packed_structure_is_checked"),
    ("game file not required", "if (!world && !archive && !hasGameFile)", "if (false)", "test_file_the_game_lacks_is_refused"),
    ("run-time identity unchecked", "if (!SameIdentity(id, f->id))\n\t{\n\t\tclose(fd);",
     "if (false)\n\t{\n\t\tclose(fd);", "test_file_changed_after_startup_is_not_read"),
    # the hash at every open: the only guard when a change keeps the identity. One row for the one guard, killed by the blind-identity test, which never skips;
    # test_a_change_that_keeps_the_identity_is_refused_by_the_hash_at_open shows the same guard against a real change and is a plain test (a second row with
    # this edit would count the guard twice in the tally).
    ("hash at open not compared", "if (memcmp(sum, f->sha, sizeof sum) != 0)\n\t{\n\t\tRefused(f, \"no longer matches its checked SHA-256\", false);\n\t\treturn -1;",
     "if (false)\n\t{\n\t\tRefused(f, \"no longer matches its checked SHA-256\", false);\n\t\treturn -1;", "test_the_hash_at_open_catches_changed_bytes_even_when_the_identity_is_blind"),
    ("an id that is a device name taken", "if (DeviceName(id))\n\t\t{", "if (false)\n\t\t{", "test_an_id_that_is_a_windows_device_name_is_refused_on_every_platform"),
    ("a refused mod checked again when it is listed twice", "if (std::find(listed.begin(), listed.end(), id) != listed.end())",
     "if (std::any_of(gMods.begin(), gMods.end(), [&](const Mod &m) { return m.id == id; }))", "test_a_mod_listed_twice_is_checked_once_whether_it_passes_or_not"),
    ("the mod.json refusal drops the open chain's reason", "(why ? why : \"it cannot be opened\")", "\"it cannot be opened\"", "test_a_refused_mod_json_says_why"),
    ("device names allowed", 'return "a Windows device name";', "{}", "test_path_escapes_are_refused"),
    ("short-name aliases allowed", 'return "a component that looks like a Windows short name (a \'~\' and a digit)";', "{}", "test_path_escapes_are_refused"),
    ("short-name digit not required", "if (part[i] == '~' && part[i + 1] >= '0' && part[i + 1] <= '9')", "if (part[i] == '~')", "test_path_escapes_are_refused_but_a_tilde_alone_is_fine"),
    ("texture limit dropped", "if (total >= kTextureScratch)", "if (false)", "test_textures_must_be_dds_within_the_game_limit"),
    ("mod count unbounded", "if ((int)gMods.size() >= kMaxMods)", "if (false)", "test_at_most_sixteen_mods"),
    ("version range unchecked", "if (released)\n\t\t{\n\t\t\tint order", "if ((void)released, false)\n\t\t{\n\t\t\tint order", "test_ce_version_range"),
    ("snapshot not sealed", "fcntl(snapshot, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)",
     "0", "test_opened_mod_is_an_immutable_verified_snapshot"),
    ("every name is a world file", "const bool world = WorldFile(key);", "const bool world = WorldFile(key) || true;",
     "test_only_the_three_digit_world_name_may_be_added"),
    ("world name loosened",
     'if (key.size() != n + 3 + 4 || key.compare(0, n, head) != 0 || key.compare(n + 3, 4, ".wrd") != 0)',
     "if (key.compare(0, n, head) != 0)", "test_only_the_three_digit_world_name_may_be_added"),
    ("world size bound dropped", "if (world && e.size > kMaxWorld)", "if (false)", "test_world_file_size_is_bounded"),
    ("has answers for everything", "if (!f)\n\t\treturn false;\n\tIdentity id;", "if (!f)\n\t\treturn true;\n\tIdentity id;",
     "test_has_answers_by_name_and_never_when_mods_are_off"),
    ("has trusts the startup table", "bool same = SameIdentity(id, f->id);", "bool same = true;",
     "test_has_follows_the_run_time_check_so_a_changed_world_file_falls_back_to_the_archive"),
    ("has claims a file it cannot open", 'Refused(f, "can no longer be opened safely", true);\n\t\treturn false;',
     'Refused(f, "can no longer be opened safely", true);\n\t\treturn true;',
     "test_has_follows_the_run_time_check_so_a_changed_world_file_falls_back_to_the_archive"),
    ("refusal wording ignores added files", "const char *then = !f->added ?", "const char *then = true ?",
     "test_a_refusal_says_what_the_game_reads_instead"),
    ("refusal wording ignores the fallback", ": fallback ? \"the level's own world is read instead\"",
     ": (fallback || true) ? \"the level's own world is read instead\"", "test_a_refusal_says_what_the_game_reads_instead"),
    ("served logged on every read", "if (!__atomic_exchange_n(&const_cast<File *>(f)->served, 1, __ATOMIC_RELAXED))",
     "if (true)", "test_the_first_read_of_each_replaced_file_is_logged_once"),
    ("platform refusal removed", 'Log("%s", off);\n\t\treturn;',
     'Log("%s", off);',
     "test_platform_off_build_refuses_listed_mods_and_touches_nothing", PLATFORM_OFF),
    ("platform switch ignored", "#if defined(PORT_MODS_PLATFORM_OFF)", "#if 0",
     "test_platform_off_build_refuses_listed_mods_and_touches_nothing", PLATFORM_OFF),
    # ---- added level archives ----
    ("level archive range dropped", "return n >= kArchiveFirst && n <= kArchiveLast;",
     "return true;",
     "test_only_the_three_digit_level_archive_name_in_the_multiplayer_range_may_be_added"),
    ("level archive name loosened", "if (key.size() != h + 3 + t || key.compare(0, h, head) != 0 || key.compare(h + 3, t, tail) != 0)\n\t\treturn false;\n\tint n = 0;",
     "if (key.compare(0, h, head) != 0 || key.compare(h + 3, t, tail) != 0)\n\t\treturn false;\n\tint n = 0;",
     "test_only_the_three_digit_level_archive_name_in_the_multiplayer_range_may_be_added"),
    ("level archive added over a game file", "const bool archive = !hasGameFile && LevelArchiveName(key, &archiveLevel);",
     "const bool archive = LevelArchiveName(key, &archiveLevel);",
     "test_a_level_archive_the_game_has_is_replaced_like_any_file"),
    ("archive size bound dropped", "if (archive && e.size > kMaxArchive)",
     "if (false)",
     "test_added_level_archive_structure_is_checked"),
    ("archive unpacked bound dropped", "if (total > kMaxArchiveUnpacked)",
     "if (false)",
     "test_added_level_archive_structure_is_checked"),
    ("added archive not walked", "if (archive)\n\t\t\tif (const char *bad = CheckLevelArchive(unpacked, archiveLevel))",
     "if (false)\n\t\t\t\tif (const char *bad = CheckLevelArchive(unpacked, archiveLevel))",
     "test_added_level_archive_structure_is_checked"),
    ("LVLR version unchecked", "|| size != 4 || Le32(p) != 103))",
     "|| size != 4))",
     "test_added_level_archive_structure_is_checked"),
    ("TARG value unchecked", "|| size != 4 || Le32(p) != 1))",
     "|| size != 4))",
     "test_added_level_archive_structure_is_checked"),
    ("AYAD position unchecked", "if (memcmp(h, \"AYAD\", 4) || size != 24)",
     "if (false)",
     "test_added_level_archive_structure_is_checked"),
    ("AYAD guards unchecked", "if (Le32(p + 4 * i) != guard[i])",
     "if (Le32(p + 4 * i) != guard[i] && false)",
     "test_added_level_archive_structure_is_checked"),
    ("AYAD guard word wrong", "static const unsigned guard[5] = {344, 372, 316, 5084, 92};",
     "static const unsigned guard[5] = {344, 372, 316, 5085, 92};",
     "test_every_shipped_level_archive_passes_the_walker", ANY_LEVEL),
    ("unknown tags allowed", "if (!known)",
     "if (false)",
     "test_added_level_archive_structure_is_checked"),
    ("base archive tags allowed", "\"LNDS\", \"SURF\", \"ERES\", \"SSHD\", \"WRES\"};",
     "\"LNDS\", \"SURF\", \"ERES\", \"SSHD\", \"WRES\", \"PLAT\"};",
     "test_added_level_archive_structure_is_checked"),
    ("chunk overrun unchecked", "if (size > raw.size() - at - 8)",
     "if (false)",
     "test_added_level_archive_structure_is_checked"),
    ("chunk count unbounded", "if (++count > kMaxLevelChunks)",
     "if (++count < 0)",
     "test_added_level_archive_structure_is_checked"),
    ("TEXT framing unchecked", 'if (size < 372 || memcmp(p, "DXTX", 4) || Le32(p + 4) != size - 8 || memcmp(p + 8, "CTEX", 4))',
     "if (size < 372)",
     "test_added_level_archive_structure_is_checked"),
    ("TEXT DXTX tag unchecked", 'memcmp(p, "DXTX", 4) ||',
     '(memcmp(p, "DXTX", 4) && false) ||',
     "test_added_level_archive_structure_is_checked"),
    ("TEXT wrapper size unchecked", 'memcmp(p, "DXTX", 4) || Le32(p + 4) != size - 8',
     'memcmp(p, "DXTX", 4) || (Le32(p + 4) != size - 8 && false)',
     "test_added_level_archive_structure_is_checked"),
    ("TEXT CTEX tag unchecked", 'Le32(p + 4) != size - 8 || memcmp(p + 8, "CTEX", 4)',
     'Le32(p + 4) != size - 8 || (memcmp(p + 8, "CTEX", 4) && false)',
     "test_added_level_archive_structure_is_checked"),
    ("TEXT minimum too high", "if (size < 372 ||",
     "if (size < 373 ||",
     "test_added_level_archive_structure_is_checked"),
    ("TEXT frame count read from the wrong word", "Le32(p + 328)",
     "Le32(p + 324)",
     "test_added_level_archive_limits_are_inclusive"),
    ("TEXT of more than 16 frames allowed", "frames > 16 ||",
     "frames > 17 ||",
     "test_added_level_archive_structure_is_checked"),
    ("TEXT of 16 frames refused", "frames > 16 ||",
     "frames > 15 ||",
     "test_added_level_archive_limits_are_inclusive"),
    ("TEXT longer than its frames allowed", "size != 360 + 12 * frames",
     "size < 360 + 12 * frames",
     "test_added_level_archive_structure_is_checked"),
    ("TEXT shorter than its frames allowed", "size != 360 + 12 * frames",
     "size > 360 + 12 * frames",
     "test_added_level_archive_structure_is_checked"),
    ("MESH minimum back to 9", '(size < 325 || memcmp(p, "PMSH", 4)',
     '(size < 9 || memcmp(p, "PMSH", 4)',
     "test_added_level_archive_structure_is_checked"),
    ("MESH minimum 324", '(size < 325 || memcmp(p, "PMSH", 4)',
     '(size < 324 || memcmp(p, "PMSH", 4)',
     "test_added_level_archive_structure_is_checked"),
    ("MESH minimum 326", '(size < 325 || memcmp(p, "PMSH", 4)',
     '(size < 326 || memcmp(p, "PMSH", 4)',
     "test_added_level_archive_structure_is_checked"),
    ("MESH PMSH tag unchecked", 'memcmp(p, "PMSH", 4) ||',
     '(memcmp(p, "PMSH", 4) && false) ||',
     "test_added_level_archive_structure_is_checked"),
    ("MESH PMSH size unchecked", 'Le32(p + 4) != size - 8 || memcmp(p + 8, "PMS2", 4)',
     '(Le32(p + 4) != size - 8 && false) || memcmp(p + 8, "PMS2", 4)',
     "test_added_level_archive_structure_is_checked"),
    ("MESH second header tag unchecked", 'memcmp(p + 8, "PMS2", 4) ||',
     '(memcmp(p + 8, "PMS2", 4) && false) ||',
     "test_added_level_archive_structure_is_checked"),
    ("MESH second header does not fill it", "Le32(p + 12) != size - 16",
     "(Le32(p + 12) != size - 16 && false)",
     "test_added_level_archive_structure_is_checked"),
    ("MESH second header may be short", "Le32(p + 12) != size - 16",
     "Le32(p + 12) > size - 16",
     "test_added_level_archive_structure_is_checked"),
    ("MESH second header may be long", "Le32(p + 12) != size - 16",
     "Le32(p + 12) < size - 16",
     "test_added_level_archive_structure_is_checked"),
    ("LVLR, TARG and AYAD may recur", "if (count > 3 && (!memcmp(h, \"LVLR\", 4) || !memcmp(h, \"TARG\", 4) || !memcmp(h, \"AYAD\", 4)))",
     "if (false)",
     "test_added_level_archive_structure_is_checked"),
    ("LVLR may recur", "(!memcmp(h, \"LVLR\", 4) || !memcmp(h, \"TARG\", 4) || !memcmp(h, \"AYAD\", 4)))",
     "(!memcmp(h, \"TARG\", 4) || !memcmp(h, \"AYAD\", 4)))",
     "test_added_level_archive_structure_is_checked"),
    ("TARG may recur", "(!memcmp(h, \"LVLR\", 4) || !memcmp(h, \"TARG\", 4) || !memcmp(h, \"AYAD\", 4)))",
     "(!memcmp(h, \"LVLR\", 4) || !memcmp(h, \"AYAD\", 4)))",
     "test_added_level_archive_structure_is_checked"),
    ("AYAD may recur", "(!memcmp(h, \"LVLR\", 4) || !memcmp(h, \"TARG\", 4) || !memcmp(h, \"AYAD\", 4)))",
     "(!memcmp(h, \"LVLR\", 4) || !memcmp(h, \"TARG\", 4)))",
     "test_added_level_archive_structure_is_checked"),
    ("chunks after WRES allowed", "else if (wres)\n\t\t\treturn \"a level archive with a chunk after its WRES\";",
     "else if (false)\n\t\t\treturn \"a level archive with a chunk after its WRES\";",
     "test_added_level_archive_structure_is_checked"),
    ("second WRES allowed", "if (count < 3 || wres != 1)",
     "if (count < 3 || wres < 1)",
     "test_added_level_archive_structure_is_checked"),
    ("WRLD length unchecked", "if (wresSize < 8 || memcmp(w, \"WRLD\", 4) || Le32(w + 4) != wresSize - 8)",
     "if (wresSize < 8 || memcmp(w, \"WRLD\", 4))",
     "test_added_level_archive_structure_is_checked"),
    # the stream's own boundaries: each of these lets a header or a size reach past the end of what is declared
    ("chunk overrun off by eight", "if (size > raw.size() - at - 8)",
     "if (size > raw.size() - at)",
     "test_added_level_archive_structure_is_checked"),
    ("chunk header cut at four bytes", "if (raw.size() - at < 8)",
     "if (raw.size() - at < 4)",
     "test_added_level_archive_structure_is_checked"),
    # WRLD: WDAT, a BSWD with a base world, an RLWD, then the end
    ("WDAT length not exact", "if (size != 8)",
     "if (size < 8)",
     "test_added_level_archive_structure_is_checked"),
    ("WRLD header cut at four bytes", "if (end - k < 8 || Le32(raw.data() + k + 4) > end - k - 8)",
     "if (end - k < 4 || Le32(raw.data() + k + 4) > end - k - 8)",
     "test_added_level_archive_structure_is_checked"),
    ("WRLD chunk overrun off by eight", "if (end - k < 8 || Le32(raw.data() + k + 4) > end - k - 8)",
     "if (end - k < 8 || Le32(raw.data() + k + 4) > end - k)",
     "test_added_level_archive_structure_is_checked"),
    ("world chunk overrun unchecked", "if (end - k < 8 || Le32(raw.data() + k + 4) > end - k - 8)",
     "if (end - k < 8)",
     "test_added_level_archive_structure_is_checked"),
    ("WRLD chunk tags unchecked", "if (memcmp(raw.data() + k, tag, 4))\n\t\t\treturn misplaced;",
     "if (memcmp(raw.data() + k, tag, 4) && false)\n\t\t\treturn misplaced;",
     "test_added_level_archive_structure_is_checked"),
    ("missing chunk read on", "if (k == end)\n\t\t\treturn absent;",
     "if (false)\n\t\t\treturn absent;",
     "test_added_level_archive_structure_is_checked"),
    ("base world body always read", "for (int pass = base == -1 ? 1 : 0; pass < 2; pass++)",
     "for (int pass = 0; pass < 2; pass++)",
     "test_added_level_archive_limits_are_inclusive"),
    ("base world body never read", "for (int pass = base == -1 ? 1 : 0; pass < 2; pass++)",
     "for (int pass = 1; pass < 2; pass++)",
     "test_added_level_archive_limits_are_inclusive"),
    ("world body bounds unchecked", "if (size < 2 || size > kMaxWorld)",
     "if (false)",
     "test_added_level_archive_structure_is_checked"),
    ("world body size unbounded", "if (size < 2 || size > kMaxWorld)",
     "if (size < 2)",
     "test_added_level_archive_structure_is_checked"),
    ("world body of one byte allowed", "if (size < 2 || size > kMaxWorld)",
     "if (size > kMaxWorld)",
     "test_added_level_archive_structure_is_checked"),
    ("world version unchecked", "if (version < 43 || version > 50)",
     "if ((version < 43 || version > 50) && false)",
     "test_added_level_archive_structure_is_checked"),
    ("chunks after the real world allowed", "if (k != end)",
     "if (false)",
     "test_added_level_archive_structure_is_checked"),
    ("WDAT level unchecked", "if (real != level)",
     "if (real != level && false)",
     "test_added_level_archive_structure_is_checked"),
    ("base level range unchecked", "if (base != -1 && (base < 0 || base > 999))",
     "if (false)",
     "test_added_level_archive_structure_is_checked"),
    ("summary counts added files as replaced", "(unsigned)gFiles.size() - added,\n\t\t\tadded);",
     "(unsigned)gFiles.size(),\n\t\t\tadded);",
     "test_the_summary_counts_added_and_replaced_files_apart"),
    ("summary never says added", "if (added)\n\t\tLog(\"%u active, %u game files replaced, %u added",
     "if (false)\n\t\tLog(\"%u active, %u game files replaced, %u added",
     "test_the_summary_counts_added_and_replaced_files_apart"),
    ("added flag ignores archives", "f.added = (world || archive) && !hasGameFile;",
     "f.added = world && !hasGameFile;",
     "test_level_archive_can_be_added_for_a_level_the_game_lacks"),
    ("archive refusal wording missing", ": f->archive ? \"the game has no such level archive, so the level cannot load\"",
     ": false ? \"the game has no such level archive, so the level cannot load\"",
     "test_a_refusal_for_an_added_level_archive_says_the_level_cannot_load"),
    ("archive flag not kept", "f.archive = archive;",
     "f.archive = 0;",
     "test_a_refusal_for_an_added_level_archive_says_the_level_cannot_load"),
    # a TEXT's frame count is read after the size check: the verdicts are the same when it is read first, which only the sanitizers show
    ("frame count read before the size check",
     '\t\t\tif (size < 372 || memcmp(p, "DXTX", 4) || Le32(p + 4) != size - 8 || memcmp(p + 8, "CTEX", 4))\n'
     '\t\t\t\treturn "a level archive TEXT chunk that is not a DXTX/CTEX texture record of 372 bytes or more";\n'
     '\t\t\t// The frame count (mNumFrames, +0x138 of the texture object) is read only here, where the 372 bytes are known to be\n'
     '\t\t\t// there. 1 to 16 frames and exactly their length: no frame is 360 bytes, which the check above has refused.\n'
     '\t\t\tconst unsigned frames = Le32(p + 328);\n',
     '\t\t\tconst unsigned frames = Le32(p + 328);\n'
     '\t\t\tif (size < 372 || memcmp(p, "DXTX", 4) || Le32(p + 4) != size - 8 || memcmp(p + 8, "CTEX", 4))\n'
     '\t\t\t\treturn "a level archive TEXT chunk that is not a DXTX/CTEX texture record of 372 bytes or more";\n',
     "test_a_short_texture_is_refused_without_reading_past_the_stream"),
    # a named mesh keeps its data in its own file: its chunk is 325 bytes and 317 for each chained mesh
    ("named mesh chain size unchecked", "if (p[16] && (size - 325) % 317)", "if (false)",
     "test_added_level_archive_structure_is_checked"),
    ("named mesh chain step off by one", "if (p[16] && (size - 325) % 317)", "if (p[16] && (size - 325) % 316)",
     "test_added_level_archive_limits_are_inclusive"),
    ("named mesh chain base off by one", "if (p[16] && (size - 325) % 317)", "if (p[16] && (size - 324) % 317)",
     "test_added_level_archive_limits_are_inclusive"),
    ("every mesh held to the chain size", "if (p[16] && (size - 325) % 317)", "if ((size - 325) % 317)",
     "test_added_level_archive_limits_are_inclusive"),
    ("mesh name tested at its second byte", "if (p[16] && (size - 325) % 317)", "if (p[17] && (size - 325) % 317)",
     "test_added_level_archive_structure_is_checked"),
    ("mesh name tested in the size word", "if (p[16] && (size - 325) % 317)", "if (p[15] && (size - 325) % 317)",
     "test_added_level_archive_structure_is_checked"),
    ('inline: a second inline mesh allowed', 'if (++inlineMeshes > 1)', 'if (++inlineMeshes > 2)',
     'test_only_one_inline_mesh_is_admitted_and_a_named_one_is_still_the_325_bytes'),
    ('inline: the reference name unchecked', 'std::any_of(p + 16, p + 316, [](unsigned char c) { return c != 0; })', 'false',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: runtime id 19999 allowed', 'Le32(p + 316) < 20000', 'Le32(p + 316) < 19999',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: runtime id 30000 allowed', 'Le32(p + 316) > 29999', 'Le32(p + 316) > 30000',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the optional word ignored', '|| Le32(p + 320) || p[324] > 1)', '|| p[324] > 1)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: kept flag 2 allowed', 'p[324] > 1)', 'p[324] > 2)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: CMSH size unchecked', '!c.Take("CMSH", p, size) || size != 372', '!c.Take("CMSH", p, size)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: sequences allowed', 'if (sequences)\n\t\treturn "an inline mesh with cutscene sequences (not admitted)"', 'if (sequences == 0xFFFFFFFFu)\n\t\treturn "an inline mesh with cutscene sequences (not admitted)"',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the chain word ignored', 'chained = chain != 0;', 'chained = chain != 0 && false;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the chain word always set', 'chained = chain != 0;', 'chained = chain != 0 || true;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a chain of 17 meshes allowed', 'kMeshMaxChain = 16;', 'kMeshMaxChain = 17;',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a chain of 16 meshes refused', 'kMeshMaxChain = 16;', 'kMeshMaxChain = 15;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a chain word and no mesh allowed', 'if (rest < 16 + 309 + 8)', 'if (rest < 16 + 309)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a PMSH tag unchecked', 'if (memcmp(h, "PMSH", 4) || ', 'if (',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a PMSH size unchecked', 'Le32(h + 4) != rest - 8 || ', '',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a PMS2 tag unchecked', 'memcmp(h + 8, "PMS2", 4) || ', '',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a PMS2 size unchecked', '|| Le32(h + 12) != rest - 16)', ')',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ("inline: a chained reference's name unchecked", 'std::any_of(reference, reference + 300, [](unsigned char x) { return x != 0; })', 'false',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a chained runtime id below 20000 allowed', 'bool refused = id < 20000 || id > 29999;', 'bool refused = id > 29999;',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a chained runtime id over 29999 allowed', 'bool refused = id < 20000 || id > 29999;', 'bool refused = id < 20000;',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a repeated runtime id allowed', 'refused = refused || ids[i] == id;', 'refused = refused || ids[i] == id + 0x10000000u;',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ("inline: the first mesh's runtime id not compared", 'for (unsigned i = 0; i < depth; i++)\n\t\t\trefused', 'for (unsigned i = 1; i < depth; i++)\n\t\t\trefused',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ("inline: the first mesh's runtime id not handed over", 'CheckInlineMesh(p + 325, size - 325, Le32(p + 316))', 'CheckInlineMesh(p + 325, size - 325, 0)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a chained reference name checked for 299 bytes only', 'std::any_of(reference, reference + 300, [](unsigned char x) { return x != 0; })', 'std::any_of(reference, reference + 299, [](unsigned char x) { return x != 0; })',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a chained runtime id of 20000 refused', 'bool refused = id < 20000 || id > 29999;', 'bool refused = id < 20001 || id > 29999;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a chained optional word read a byte early', 'if (Le32(reference + 304) || reference[308] > 1)', 'if (Le32(reference + 303) || reference[308] > 1)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a chained optional word allowed', 'if (Le32(reference + 304) || reference[308] > 1)', 'if (reference[308] > 1)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a chained kept flag of 2 allowed', 'if (Le32(reference + 304) || reference[308] > 1)', 'if (Le32(reference + 304) || reference[308] > 2)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the totals restart for each mesh of a chain', 'bool chained = false;\n\t\tif (const char *bad = CheckOneMesh(', 'bool chained = false;\n\t\tvertices = indices = groups = 0;\n\t\tif (const char *bad = CheckOneMesh(',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the last mesh need not end the stream', 'if (!c.Done())\n\t\t\t\treturn "an inline mesh with bytes after its last chunk";', 'if (false)\n\t\t\t\treturn "an inline mesh with bytes after its last chunk";',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: 33 textures allowed', 'textures > kMeshMaxTextures ||', 'textures > kMeshMaxTextures + 1 ||',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: 32 textures refused', 'textures > kMeshMaxTextures ||', 'textures >= kMeshMaxTextures ||',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the texture cap back to 16', 'kMeshMaxTextures = 32,', 'kMeshMaxTextures = 16,',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: 513 parts allowed', 'parts > kMeshMaxParts)', 'parts > kMeshMaxParts + 1)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: no parts allowed', 'emitters > kMeshMaxEmitters || !parts ||', 'emitters > kMeshMaxEmitters ||',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: 33 animation modes allowed', 'animModes > kMeshMaxAnimModes ||', 'animModes > kMeshMaxAnimModes + 1 ||',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: 32 animation modes refused', 'animModes > kMeshMaxAnimModes ||', 'animModes >= kMeshMaxAnimModes ||',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the animation-mode cap back to 8', 'kMeshMaxAnimModes = 32,', 'kMeshMaxAnimModes = 8,',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: 513 emitters allowed', 'emitters > kMeshMaxEmitters ||', 'emitters > kMeshMaxEmitters + 1 ||',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: an unterminated mesh name allowed', 'if (!memchr(p + 0x24, 0, 300))', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: 17 frames allowed', 'frames[i] > 16)', 'frames[i] > 17)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: no frames allowed', 'if (!frames[i] || frames[i] > 16)', 'if (frames[i] > 16)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a TEXB longer than its frames allowed', 'tsize != 20 * (size_t)frames[i] + 128', 'tsize < 20 * (size_t)frames[i] + 128',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: an empty texture name allowed', 'if (!end || end == name)', 'if (!end)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a non-ASCII texture name allowed', 'if (*q < 0x20 || *q >= 0x7F)', 'if (*q < 0x20)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: part type 0 allowed', 'if (partType < 1 || partType > 6)', 'if (partType > 6)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: part type 7 allowed', 'if (partType < 1 || partType > 6)', 'if (partType < 1)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a part of type 1 refused', 'if (partType < 1 || partType > 6)', 'if (partType < 2 || partType > 6)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a part of type 6 refused', 'if (partType < 1 || partType > 6)', 'if (partType < 1 || partType > 5)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the part number unchecked', 'if (Le32(p + 0x88) != index)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a part number below its place allowed', 'if (Le32(p + 0x88) != index)', 'if (Le32(p + 0x88) > index)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a part number above its place allowed', 'if (Le32(p + 0x88) != index)', 'if (Le32(p + 0x88) < index)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a part number held to the part count only', 'if (Le32(p + 0x88) != index)', 'if (Le32(p + 0x88) >= parts)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the part number read from the word before it', 'if (Le32(p + 0x88) != index)', 'if (Le32(p + 0x84) != index)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: older pvertices allowed', 'Le32(p + 0xAC) || ', '',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: older triangles allowed', 'Le32(p + 0xB0) || ', '',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a chained part allowed', 'if (Le32(p + 0x9C) || ', 'if (',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: bone weights allowed', '|| Le32(p + 0xD4) || Le32(p + 0xD8))', '|| Le32(p + 0xD8))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: bone slots allowed', '|| Le32(p + 0xD4) || Le32(p + 0xD8))', '|| Le32(p + 0xD4))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a skeletal part without bones allowed', 'if (skeletal && (!bones || bones > kMeshMaxBones))', 'if (skeletal && bones > kMeshMaxBones)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: any number of bones allowed', 'if (skeletal && (!bones || bones > kMeshMaxBones))', 'if (skeletal && !bones)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: 20 bones allowed', 'bones > kMeshMaxBones', 'bones > kMeshMaxBones + 1',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: 19 bones refused', 'kMeshMaxBones = 19,', 'kMeshMaxBones = 18,',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: bones on another part allowed', 'if (!skeletal && (bones || Le32(p + 0xD0)))', 'if (!skeletal && Le32(p + 0xD0))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a bones pointer on another part allowed', 'if (!skeletal && (bones || Le32(p + 0xD0)))', 'if (!skeletal && bones)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the stale bones pointer refused', 'if (!skeletal && (bones || Le32(p + 0xD0)))', 'if ((!skeletal && bones) || Le32(p + 0xD0))',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a field of view on another part allowed', 'if (fov != camera)', 'if (camera && !fov)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a camera without a field of view allowed', 'if (fov != camera)', 'if (fov && !camera)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the field of view read from another word', 'fov = Le32(p + 0xCC) != 0', 'fov = Le32(p + 0xD8) != 0',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: HFOV size unchecked', 'if (fov && (!m.Take("HFOV", p, size) || size != 4 * (size_t)hframes))', 'if (fov && !m.Take("HFOV", p, size))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a shorter HFOV allowed', 'size != 4 * (size_t)hframes))', 'size > 4 * (size_t)hframes))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a longer HFOV allowed', 'size != 4 * (size_t)hframes))', 'size < 4 * (size_t)hframes))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the HFOV not walked', 'if (fov && (!m.Take("HFOV"', 'if (false && (!m.Take("HFOV"',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: BONE size unchecked', 'if (!m.Take("BONE", p, size) || size != 4 * (size_t)bones)', 'if (!m.Take("BONE", p, size))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a shorter BONE allowed', 'size != 4 * (size_t)bones)', 'size > 4 * (size_t)bones)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a longer BONE allowed', 'size != 4 * (size_t)bones)', 'size < 4 * (size_t)bones)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the BONE not walked', '\tif (bones)\n\t{\n\t\tif (!m.Take("BONE"', '\tif (false)\n\t{\n\t\tif (!m.Take("BONE"',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a bone index unchecked', 'if (Le32(p + 4 * i) >= parts)\n\t\t\t\treturn "an inline mesh part with a bone', 'if (false)\n\t\t\t\treturn "an inline mesh part with a bone',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a bone equal to the part count allowed', 'if (Le32(p + 4 * i) >= parts)\n\t\t\t\treturn "an inline mesh part with a bone', 'if (Le32(p + 4 * i) > parts)\n\t\t\t\treturn "an inline mesh part with a bone',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a bone held to the bone count', 'if (Le32(p + 4 * i) >= parts)\n\t\t\t\treturn "an inline mesh part with a bone', 'if (Le32(p + 4 * i) >= bones)\n\t\t\t\treturn "an inline mesh part with a bone',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a bone past the parts but below the bone count allowed', 'if (Le32(p + 4 * i) >= parts)\n\t\t\t\treturn "an inline mesh part with a bone', 'if (Le32(p + 4 * i) >= std::max(parts, bones))\n\t\t\t\treturn "an inline mesh part with a bone',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: only the first bone read', 'for (unsigned i = 0; i < bones; i++)\n\t\t{\n\t\t\tif (Le32(p + 4 * i) >= parts)', 'for (unsigned i = 0; i < 1; i++)\n\t\t{\n\t\t\tif (Le32(p + 4 * i) >= parts)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a skeletal part without vertices allowed', 'if (skeletal ? !pieces : camera && pieces)', 'if (!skeletal && camera && pieces)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a camera part with vertices allowed', 'if (skeletal ? !pieces : camera && pieces)', 'if (skeletal && !pieces)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: every other part held to no vertices', 'if (skeletal ? !pieces : camera && pieces)', 'if (skeletal ? !pieces : pieces != 0)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: 36-byte vertices on every part', 'const unsigned stride = skeletal ? 48 : 36,', 'const unsigned stride = 36,',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: 48-byte vertices on every part', 'const unsigned stride = skeletal ? 48 : 36,', 'const unsigned stride = 48,',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the rigid format word on every part', 'format = skeletal ? 0 : 0x152;', 'format = 0x152;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the skinned format word on every part', 'format = skeletal ? 0 : 0x152;', 'format = 0;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: 48-byte vertices on every part that is not a normal one', 'const unsigned stride = skeletal ? 48 : 36, format = skeletal ? 0 : 0x152;',
     'const unsigned stride = partType == 1 ? 36 : 48, format = partType == 1 ? 0x152 : 0;', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: palette offsets unchecked', 'if (skeletal && !piece)\n\t\t\tfor (size_t at = 12;', 'if (false)\n\t\t\tfor (size_t at = 12;',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the offsets read at the position', 'for (size_t at = 12;', 'for (size_t at = 0;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the offsets read at the normal', 'for (size_t at = 12;', 'for (size_t at = 24;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: only the first vertex read', 'at < vertexBytes; at += 48)', 'at < 48; at += 48)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the last vertex not read', 'at < vertexBytes; at += 48)', 'at + 48 < vertexBytes; at += 48)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: two slots read', 'slot < 3; slot++)', 'slot < 2; slot++)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: one slot read', 'slot < 3; slot++)', 'slot < 1; slot++)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the slots read at the wrong stride', 'Le32(p + at + 4 * slot)', 'Le32(p + at + 3 * slot)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a palette offset of the bone number itself', 'const float offset = 3.0f * i;', 'const float offset = 1.0f * i;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the last bone refused as a palette offset', 'std::find(offsets, offsets + bones, Le32(p + at + 4 * slot)) == offsets + bones', 'std::find(offsets, offsets + bones - 1, Le32(p + at + 4 * slot)) == offsets + bones - 1',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the palette offset compared as an integer', 'memcpy(&offsets[i], &offset, 4);', 'offsets[i] = (unsigned)offset;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: older vertices allowed', 'Le32(p + 0xA4) || Le32(p + 0xA8) ||', 'Le32(p + 0xA4) ||',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a child index past the parts allowed', 'if (Le32(p + 4 * i) <= index || Le32(p + 4 * i) >= parts)', 'if (Le32(p + 4 * i) <= index)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a child equal to the part count allowed', '|| Le32(p + 4 * i) >= parts)', '|| Le32(p + 4 * i) > parts)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a child before the part allowed', 'if (Le32(p + 4 * i) <= index || Le32(p + 4 * i) >= parts)', 'if (Le32(p + 4 * i) >= parts)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a part that is its own child allowed', 'Le32(p + 4 * i) <= index', 'Le32(p + 4 * i) < index',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a child read from the wrong word', 'if (Le32(p + 4 * i) <= index || Le32(p + 4 * i) >= parts)', 'if (Le32(p + 4 * i + 1) <= index || Le32(p + 4 * i + 1) >= parts)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a next part refused as a child', 'Le32(p + 4 * i) <= index', 'Le32(p + 4 * i) <= index + 1',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a child listed twice allowed', 'if (links[Le32(p + 4 * i)].listedBy)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the listed children never marked', 'links[Le32(p + 4 * i)] = {(unsigned short)(index + 1), (unsigned char)(links[index].depth + 1)};', '(void)links;',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the part marked instead of its child', 'links[Le32(p + 4 * i)] = {', 'links[index] = {',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the marks kept from one mesh of a chain to the next', 'MeshLink links[kMeshMaxParts] = {};', 'static MeshLink links[kMeshMaxParts] = {};',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the child marks kept in 8 bits', 'if (links[Le32(p + 4 * i)].listedBy)', 'if (links[Le32(p + 4 * i) & 255].listedBy)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the part that lists a child kept in 8 bits', '{(unsigned short)(index + 1),', '{(unsigned char)(index + 1),',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    # a part has a PRNT exactly when some part lists it, naming that part, and is at most 32 parts below one that no part lists
    ('inline: a PRNT unrelated to the child lists allowed', 'if ((parent ? Le32(p) + 1 : 0) != links[index].listedBy)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a listed part without a PRNT allowed', '(parent ? Le32(p) + 1 : 0) != links[index].listedBy', 'parent && Le32(p) + 1 != links[index].listedBy',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a part that no part lists allowed a PRNT', '(parent ? Le32(p) + 1 : 0) != links[index].listedBy', 'links[index].listedBy && (parent ? Le32(p) + 1 : 0) != links[index].listedBy',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: any earlier part allowed as the PRNT of a listed part', '(parent ? Le32(p) + 1 : 0) != links[index].listedBy', '(parent ? 1u : 0u) != (links[index].listedBy ? 1u : 0u)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the depth unbounded', 'if (links[index].depth >= kMeshMaxDepth)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: 33 deep allowed', 'links[index].depth >= kMeshMaxDepth', 'links[index].depth > kMeshMaxDepth',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: 32 deep refused', 'links[index].depth >= kMeshMaxDepth', 'links[index].depth + 1u >= kMeshMaxDepth',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the depth not carried down', '(unsigned char)(links[index].depth + 1)}', '(unsigned char)1}',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a parent index unchecked', 'size != 4 || Le32(p) >= index))\n\t\treturn "an inline mesh part whose PRNT', 'size != 4))\n\t\treturn "an inline mesh part whose PRNT',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a parent after the part allowed', 'Le32(p) >= index))', 'Le32(p) >= parts))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a part that is its own parent allowed', 'Le32(p) >= index))', 'Le32(p) > index))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the part just before refused as a parent', 'Le32(p) >= index))', 'Le32(p) + 1 >= index))',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a reference index unchecked', 'if (!m.Take("REFR", p, size) || size != 4 || Le32(p) >= parts)', 'if (!m.Take("REFR", p, size) || size != 4)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a part without orientation frames allowed', 'if (!orientations || !positions)', 'if ((void)orientations, !positions)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a part without position frames allowed', 'if (!orientations || !positions)', 'if ((void)positions, !orientations)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a reference part without a REFR allowed', 'if (reference != (partType == 6))', 'if (reference && partType != 6)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a REFR on another part allowed', 'if (reference != (partType == 6))', 'if (partType == 6 && !reference)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: what a reference names unchecked', 'if (types[i] == 6 && types[targets[i]] != 1)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: only the first reference part checked', 'for (unsigned i = 0; i < parts; i++)\n\t\tif (types[i] == 6', 'for (unsigned i = 0; i < 1; i++)\n\t\tif (types[i] == 6',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: every part held to a normal target', 'if (types[i] == 6 && types[targets[i]] != 1)', 'if (types[targets[i]] != 1)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a reference may name a null part', 'types[targets[i]] != 1)', 'types[targets[i]] != 1 && types[targets[i]] != 2)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a reference may name an emitter part', 'types[targets[i]] != 1)', 'types[targets[i]] != 1 && types[targets[i]] != 5)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a reference may name a reference part', 'types[targets[i]] != 1)', 'types[targets[i]] != 1 && types[targets[i]] != 6)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a reference may name a skeletal part', 'types[targets[i]] != 1)', 'types[targets[i]] != 1 && types[targets[i]] != 3)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a reference may name a camera part', 'types[targets[i]] != 1)', 'types[targets[i]] != 1 && types[targets[i]] != 4)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the target of a reference lost', 'target = (unsigned short)Le32(p);', 'target = 0;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the target of a reference kept in 8 bits', 'target = (unsigned short)Le32(p);', 'target = (unsigned char)Le32(p);',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: references checked in the first 256 parts only', 'for (unsigned i = 0; i < parts; i++)\n\t\tif (types[i] == 6', 'for (unsigned i = 0; i < parts && i < 256; i++)\n\t\tif (types[i] == 6',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a VHFM past the frames allowed', 'if (p[i] >= hframes)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a VHFM equal to the frames allowed', 'if (p[i] >= hframes)', 'if (p[i] > hframes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the last hand frame refused in a VHFM', 'if (p[i] >= hframes)', 'if (p[i] + 1u >= hframes && p[i])',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the last VHFM byte not read', 'for (unsigned i = 0; i < vframes; i++)\n\t\tif (p[i] >= hframes)', 'for (unsigned i = 0; i + 1 < vframes; i++)\n\t\tif (p[i] >= hframes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: VHFM size unchecked', 'size != vframes))', 'false))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: HORI size unchecked', '|| size != 48 * (size_t)hframes)', ')',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: HPOS size unchecked', '|| size != 16 * (size_t)hframes)', ')',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: CPOS size unchecked', 'size != 16 * (size_t)cached))', 'false))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: CORI size unchecked', 'size != 48 * (size_t)cached))', 'false))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    # the cache: a count of 1 or the virtual-frame count, a CPOS and a CORI exactly where the part is not static, and a static flag only on a part with a parent
    ('inline: the cached frame count unchecked', 'if (cached != 1 && cached != vframes)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: no cached frames allowed', 'if (cached != 1 && cached != vframes)', 'if (cached > 1 && cached != vframes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a cached frame count below the virtual-frame count allowed', 'if (cached != 1 && cached != vframes)', 'if (!cached || cached > vframes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a cached frame count above the virtual-frame count allowed', 'if (cached != 1 && cached != vframes)', 'if (!cached || (cached != 1 && cached < vframes))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the cached frame count held to 1', 'if (cached != 1 && cached != vframes)', 'if (cached != 1)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the cached frame count held to the virtual-frame count', 'if (cached != 1 && cached != vframes)', 'if (cached != vframes)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the cached frame count held to the hand-frame count', 'if (cached != 1 && cached != vframes)', 'if (cached != 1 && cached != hframes)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the CPOS rule dropped', 'if (cachedPositions == positionStatic)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: no CPOS allowed where the position is not static', 'if (cachedPositions == positionStatic)', 'if (cachedPositions && positionStatic)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a CPOS allowed where the position is static', 'if (cachedPositions == positionStatic)', 'if (!cachedPositions && !positionStatic)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the CORI rule dropped', 'if (cachedOrientations == orientationStatic)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: no CORI allowed where the orientation is not static', 'if (cachedOrientations == orientationStatic)', 'if (cachedOrientations && orientationStatic)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a CORI allowed where the orientation is static', 'if (cachedOrientations == orientationStatic)', 'if (!cachedOrientations && !orientationStatic)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a static part with no parent allowed', 'if ((positionStatic || orientationStatic) && !parent)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a static orientation with no parent allowed', 'if ((positionStatic || orientationStatic) && !parent)', 'if (positionStatic && !parent)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a static position with no parent allowed', 'if ((positionStatic || orientationStatic) && !parent)', 'if (orientationStatic && !parent)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: only the first part held to a parent when static', 'if ((positionStatic || orientationStatic) && !parent)', 'if ((positionStatic || orientationStatic) && !parent && !index)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    # a rule held only for the parts whose other flag is clear (or set), or only for a count, a word or a link of one kind, lets the rest through: the cases hold each rule on a part of each
    # combination of the flags, and the product on every combination of what the rules read
    ('inline: the CORI rule held only where the position is not static', 'if (cachedOrientations == orientationStatic)', 'if (!positionStatic && cachedOrientations == orientationStatic)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the CORI rule held only where the position is static', 'if (cachedOrientations == orientationStatic)', 'if (positionStatic && cachedOrientations == orientationStatic)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the CPOS rule held only where the orientation is not static', 'if (cachedPositions == positionStatic)', 'if (!orientationStatic && cachedPositions == positionStatic)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the CPOS rule held only where the orientation is static', 'if (cachedPositions == positionStatic)', 'if (orientationStatic && cachedPositions == positionStatic)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the cached frame count held only where the position is not static', 'if (cached != 1 && cached != vframes)', 'if (!positionStatic && cached != 1 && cached != vframes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the cached frame count held only where the position is static', 'if (cached != 1 && cached != vframes)', 'if (positionStatic && cached != 1 && cached != vframes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the cached frame count held only where the orientation is not static', 'if (cached != 1 && cached != vframes)', 'if (!orientationStatic && cached != 1 && cached != vframes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the cached frame count held only where the orientation is static', 'if (cached != 1 && cached != vframes)', 'if (orientationStatic && cached != 1 && cached != vframes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the cached frame count held only where a CPOS or a CORI follows', 'if (cached != 1 && cached != vframes)',
     'if ((cachedPositions || cachedOrientations) && cached != 1 && cached != vframes)', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the cached frame count held to the smallest virtual-frame count so far', 'if (cached != 1 && cached != vframes)', 'if (cached != 1 && cached != frames)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the cached frame count held only where a part has no parent', 'if (cached != 1 && cached != vframes)', 'if (!parent && cached != 1 && cached != vframes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the cached frame count held only where a CPOS follows', 'if (cached != 1 && cached != vframes)', 'if (cachedPositions && cached != 1 && cached != vframes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the cached frame count held only where a CORI follows', 'if (cached != 1 && cached != vframes)', 'if (cachedOrientations && cached != 1 && cached != vframes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the CPOS rule held only where a CORI follows', 'if (cachedPositions == positionStatic)', 'if (cachedOrientations && cachedPositions == positionStatic)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the CORI rule held only where a CPOS follows', 'if (cachedOrientations == orientationStatic)', 'if (cachedPositions && cachedOrientations == orientationStatic)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the CPOS rule held only for a cache of one frame', 'if (cachedPositions == positionStatic)', 'if (cached == 1 && cachedPositions == positionStatic)', CACHE_PRODUCT),
    ('inline: the CPOS rule held only for a part of one virtual frame', 'if (cachedPositions == positionStatic)', 'if (vframes == 1 && cachedPositions == positionStatic)', CACHE_PRODUCT),
    ('inline: the CPOS rule held only for a cache as long as the virtual frames', 'if (cachedPositions == positionStatic)', 'if (cached == vframes && cachedPositions == positionStatic)', CACHE_PRODUCT),
    ('inline: the CORI rule held only for a cache of one frame', 'if (cachedOrientations == orientationStatic)', 'if (cached == 1 && cachedOrientations == orientationStatic)', CACHE_PRODUCT),
    ('inline: the CORI rule held only for a part of one virtual frame', 'if (cachedOrientations == orientationStatic)', 'if (vframes == 1 && cachedOrientations == orientationStatic)', CACHE_PRODUCT),
    ('inline: the CORI rule held only for a cache as long as the virtual frames', 'if (cachedOrientations == orientationStatic)', 'if (cached == vframes && cachedOrientations == orientationStatic)',
     CACHE_PRODUCT),
    ('inline: a static part held to a parent only for a cache of one frame', 'if ((positionStatic || orientationStatic) && !parent)',
     'if (cached == 1 && (positionStatic || orientationStatic) && !parent)', CACHE_PRODUCT),
    ('inline: a static part held to a parent only for a part of one virtual frame', 'if ((positionStatic || orientationStatic) && !parent)',
     'if (vframes == 1 && (positionStatic || orientationStatic) && !parent)', CACHE_PRODUCT),
    ('inline: a static part held to a parent only for a cache as long as the virtual frames', 'if ((positionStatic || orientationStatic) && !parent)',
     'if (cached == vframes && (positionStatic || orientationStatic) && !parent)', CACHE_PRODUCT),
    ('inline: the position-static flag read from its low byte', 'positionStatic = Le32(p + 0x11C) != 0,', 'positionStatic = p[0x11C] != 0,',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the orientation-static flag read from its low byte', 'orientationStatic = Le32(p + 0x120) != 0;', 'orientationStatic = p[0x120] != 0;',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the position-static flag read from the orientation word', 'positionStatic = Le32(p + 0x11C) != 0,', 'positionStatic = Le32(p + 0x120) != 0,',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the orientation-static flag read from the position word', 'orientationStatic = Le32(p + 0x120) != 0;', 'orientationStatic = Le32(p + 0x11C) != 0;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a position-static flag set only when it is 1', 'positionStatic = Le32(p + 0x11C) != 0,', 'positionStatic = Le32(p + 0x11C) == 1,',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: an orientation-static flag set only when it is 1', 'orientationStatic = Le32(p + 0x120) != 0;', 'orientationStatic = Le32(p + 0x120) == 1;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: 13 pieces allowed', 'pieces > 12', 'pieces > 13',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: another vertex size allowed', 'Le32(p + 0x114) != stride ||', 'false ||',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: another vertex format allowed', 'Le32(p + 0x118) != format ||', '((void)format, false) ||',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a triangle list allowed', 'Le32(p + 0x11C) != 4)', 'false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: an unbuilt piece allowed', 'if (built != 1 ||', 'if (built * 0 != 0 ||',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: two-index pieces allowed', 'ic < 3 ||', '',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: index bytes unchecked', 'ib != 2 * ic ||', '',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: primitive count unchecked', '|| prims != ic - 2)', '|| prims * 0 != 0)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a piece without vertices allowed', 'if (!vc || vc > 65535 || vb != stride * vc)', 'if (vc > 65535 || vb != stride * vc)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: vertex bytes unchecked', '|| vb != stride * vc)', ')',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: later pieces may differ', 'else if (vb != vertexBytes || vc != vertexCount)', 'else if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: an index past the vertices allowed', 'if ((unsigned)(p[2 * i] | p[2 * i + 1] << 8) >= vertexCount)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: an IBUF of another size allowed', 'size != ib)', 'size < ib)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a longer first VBUF allowed', 'size != (piece ? 0 : vertexBytes))', 'size < (piece ? 0 : vertexBytes))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a later VBUF with data allowed', 'size != (piece ? 0 : vertexBytes))', 'size != (piece ? size : vertexBytes))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a TEXR of another size allowed', 'if (!v.Take("TEXR", p, size) || size != 24)', 'if (!v.Take("TEXR", p, size) || size < 24)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a texture slot past the textures allowed', '&& Le32(p + 4 * slot) >= textures', '&& Le32(p + 4 * slot) >= textures + 0xFFFF',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: bytes after the last piece allowed', 'if (!v.Done())\n\t\treturn "an inline mesh part with bytes in its PMVB', 'if (false)\n\t\treturn "an inline mesh part with bytes in its PMVB',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: bytes after the vertex buffer allowed', 'return m.Done() ? NULL : "an inline mesh part with bytes after its vertex buffer"', 'return NULL',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ("inline: the grid's cells unchecked against its size", '(unsigned long long)width * height > kMeshMaxCells)', 'false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a grid cell that is neither present nor absent allowed', 'if (present > 1)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: an absent cell with data allowed', 'if (size != 4)\n\t\t\t\treturn "an inline mesh whose collision grid has an absent cell with data"', 'if (false)\n\t\t\t\treturn "an inline mesh whose collision grid has an absent cell with data"',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a list of another size allowed', 'sSize != 4 + 2 * (size_t)count', 'sSize < 4 + 2 * (size_t)count',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: SIZS not held to', 'if (seenLists > lists || seenIndices > indices)', 'if (seenLists > lists + 0xFFFF || seenIndices > indices + 0xFFFFF)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: listed triangles unchecked', 'listed > triangles', 'false',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a list past 32767 allowed', 'if (triangle >= kMeshMaxGridTriangles)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a PBKE of another size allowed', 'if (!tri.Take("PBKE", k, ksize) || ksize != 12)', 'if (!tri.Take("PBKE", k, ksize) || ksize < 12)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ("inline: a listed triangle's vertex unchecked", 'vertices > kMeshMaxGridVertices || maxVertex > vertices', 'vertices > kMeshMaxGridVertices',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a CPBT of another size allowed', 'if (!g.Take("CPBT", p, size) || size != 184)', 'if (!g.Take("CPBT", p, size) || size > 184)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a SIZS of another size allowed', 'if (!g.Take("SIZS", p, size) || size != 8)', 'if (!g.Take("SIZS", p, size) || size > 8)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a present cell with more than a PBET allowed', 'if (!cellCursor.Take("PBET", e, eSize) || !cellCursor.Done())', 'if (!cellCursor.Take("PBET", e, eSize))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a PBET with more than a PBSQ allowed', 'if (!listCursor.Take("PBSQ", s, sSize) || !listCursor.Done() || sSize < 4)', 'if (!listCursor.Take("PBSQ", s, sSize) || sSize < 4)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a PBSQ without its count allowed', 'if (!listCursor.Take("PBSQ", s, sSize) || !listCursor.Done() || sSize < 4)', 'if (!listCursor.Take("PBSQ", s, sSize) || !listCursor.Done())',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a PLQT of another size allowed', 'if (!g.Take("PLQT", p, size) || size != 4)', 'if (!g.Take("PLQT", p, size) || size < 4)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a grid with no PBFK allowed', 'if (!g.Take("PBFK", p, size))', 'if (false && !g.Take("PBFK", p, size))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a PBPN of another size allowed', 'if (!g.Take("PBPN", p, size) || size != 4)', 'if (!g.Take("PBPN", p, size) || size < 4)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a PBPP of another size allowed', 'if (!g.Take("PBPP", p, size) || size != 6)', 'if (!g.Take("PBPP", p, size) || size < 6)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: bytes after the last vertex allowed', 'return g.Done() ? NULL : "an inline mesh whose collision grid has bytes after its last vertex";', 'return NULL;',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a part without its PMVB allowed', 'if (!m.Take("PMVB", p, size))', 'if (false && !m.Take("PMVB", p, size))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: unlisted triangles checked too', 'v < 3 && used[i]', 'v < 3',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the emitter record size unchecked', 'size < 4 || Le32(p) != 336', 'size < 4',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ("inline: an emitter's part index unchecked", 'if (size - at < 4 || Le32(p + at) >= parts)', 'if (size - at < 4)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: bytes after the last emitter allowed', 'if (at != size)\n\t\t\treturn "an inline mesh whose CEMT has bytes', 'if (false)\n\t\t\treturn "an inline mesh whose CEMT has bytes',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the tail checked after a single mesh only', 'if (!c.Done())\n\t\t\t\treturn "an inline mesh with bytes after its last chunk";',
     'if (!c.Done() && depth == 1)\n\t\t\t\treturn "an inline mesh with bytes after its last chunk";', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    # the fields the reader keeps from the file (pointers a destructor frees or the draw calls, counts it loops over)
    ('inline: a chain count allowed', 'Le32(p + 0x9C) || Le32(p + 0xA0) || ', 'Le32(p + 0x9C) || ',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a chained flag allowed', 'Le32(p + 0xA4) || ', '',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the frame-set count unbounded', 'if (Le32(p + 0xB4) > kMeshMaxFrameSets)', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the frame-set bound off by one', 'Le32(p + 0xB4) > kMeshMaxFrameSets', 'Le32(p + 0xB4) > kMeshMaxFrameSets + 1',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the frame-set bound too strict', 'Le32(p + 0xB4) > kMeshMaxFrameSets', 'Le32(p + 0xB4) > kMeshMaxFrameSets - 1',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a part without virtual frames allowed', 'if (!vframes)\n\t\treturn "an inline mesh part with no virtual frames', 'if (false)\n\t\treturn "an inline mesh part with no virtual frames',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a part without hand frames allowed', 'if (!hframes)\n\t\treturn "an inline mesh part with no hand frames', 'if (false)\n\t\treturn "an inline mesh part with no hand frames',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the shader pointer allowed', 'if (Le32(p + 0x110))', 'if (false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the piece slots unchecked', 'for (unsigned slot = pieces; slot < 64; slot++)', 'for (unsigned slot = pieces; slot < pieces; slot++)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the first unused piece slot skipped', 'for (unsigned slot = pieces; slot < 64; slot++)', 'for (unsigned slot = pieces + 1; slot < 64; slot++)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the last piece slot skipped', 'slot < 64; slot++', 'slot < 63; slot++',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the piece slots the reader replaces required zero', 'for (unsigned slot = pieces; slot < 64; slot++)', 'for (unsigned slot = 0; slot < 64; slot++)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('packed: a cut block header allowed', 'if (n - pos < 4)', 'if (false)', 'test_packed_structure_is_checked'),
    ('packed: a cut block allowed', 'if (n - pos < packed)', 'if (false)', 'test_packed_structure_is_checked'),
    ('packed: the 64 MiB total unbounded', 'if (total > kMaxFile)', 'if (false)', 'test_packed_structure_is_checked'),
    ('packed: 64 MiB itself refused', 'if (total > kMaxFile)', 'if (total >= kMaxFile)', 'test_packed_structure_is_checked'),
    ('packed: a byte over 64 MiB allowed', 'if (total > kMaxFile)', 'if (total > kMaxFile + 1)', 'test_packed_structure_is_checked'),
    ('texture: no width bound', 'w > kMaxTextureSide || h > kMaxTextureSide', 'h > kMaxTextureSide', 'test_textures_must_be_dds_within_the_game_limit'),
    ('texture: no height bound', 'w > kMaxTextureSide || h > kMaxTextureSide', 'w > kMaxTextureSide', 'test_textures_must_be_dds_within_the_game_limit'),
    ('texture: zero sides allowed', 'if (!w || !h || w > kMaxTextureSide', 'if (w > kMaxTextureSide', 'test_textures_must_be_dds_within_the_game_limit'),
    ('texture: 4096 itself refused', 'w > kMaxTextureSide || h > kMaxTextureSide', 'w >= kMaxTextureSide || h >= kMaxTextureSide', 'test_textures_must_be_dds_within_the_game_limit'),
    ('inline: an MMPT longer than 24 bytes allowed', 'if (!v.Take("MMPT", p, size) || size != 24)', 'if (!v.Take("MMPT", p, size) || size < 24)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: an MMPT shorter than 24 bytes allowed', 'if (!v.Take("MMPT", p, size) || size != 24)', 'if (!v.Take("MMPT", p, size) || size > 24)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: no piece ceiling', 'if (++groups > kMeshMaxGroups || vertices > kMeshMaxVertices || indices > kMeshMaxIndices)',
     'if ((++groups, false) || vertices > kMeshMaxVertices || indices > kMeshMaxIndices)', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: no vertex ceiling', 'vertices > kMeshMaxVertices', 'false', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: no index ceiling', 'indices > kMeshMaxIndices', 'false', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the piece ceiling refuses its own number', '++groups > kMeshMaxGroups', '++groups >= kMeshMaxGroups', 'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the vertex ceiling refuses its own number', 'vertices > kMeshMaxVertices', 'vertices >= kMeshMaxVertices', 'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the index ceiling refuses its own number', 'indices > kMeshMaxIndices', 'indices >= kMeshMaxIndices', 'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the vertex ceiling one over', 'vertices > kMeshMaxVertices', 'vertices > kMeshMaxVertices + 1', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the index ceiling one over', 'indices > kMeshMaxIndices', 'indices > kMeshMaxIndices + 1', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the piece ceiling one over', '++groups > kMeshMaxGroups', '++groups > kMeshMaxGroups + 1', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: no 16 MiB cap', 'if (n > kMeshMaxBytes)', 'if (false)', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: 16 MiB itself refused as too big', 'if (n > kMeshMaxBytes)', 'if (n >= kMeshMaxBytes)', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    # the animation modes: each record as the shipped ones are, and start and last virtual frames of every part of the chain
    ('inline: an animation mode name unchecked', 'if (!memchr(mode, 0, 16))', 'if (false)', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: an animation mode name checked for 15 bytes', 'if (!memchr(mode, 0, 16))', 'if (!memchr(mode, 0, 15))', 'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the animation modes not walked', 'for (unsigned i = 0; i < animModes; i++)', 'for (unsigned i = animModes; i < animModes; i++)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: only the first animation mode walked', 'for (unsigned i = 0; i < animModes; i++)', 'for (unsigned i = 0; i < animModes && i < 1; i++)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the animation modes read 32 bytes apart', 'const unsigned char *mode = p + 36 * (size_t)i;', 'const unsigned char *mode = p + 32 * (size_t)i;',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a last frame unrelated to the start and the scaler allowed', 'if (last != start + scaler && !(scaler > 0 && last == start + scaler - 1))', 'if ((void)start, (void)last, (void)scaler, false)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a last one less than start + scaler allowed going backwards', '!(scaler > 0 && last == start + scaler - 1)', '!(last == start + scaler - 1)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a last one less than start + scaler refused going forward', 'if (last != start + scaler && !(scaler > 0 && last == start + scaler - 1))', 'if (last != start + scaler)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: a last one more than start + scaler allowed going forward', '!(scaler > 0 && last == start + scaler - 1)', '!(scaler > 0 && (last == start + scaler - 1 || last == start + scaler + 1))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a forward mode of one frame refused (one less only past the start)', '!(scaler > 0 && last == start + scaler - 1)', '!(scaler > 0 && last > start && last == start + scaler - 1)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the start read from the mode word', 'const long long start = (int32_t)Le32(mode + 0x14)', 'const long long start = (int32_t)Le32(mode + 0x10)',
     'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the rate unchecked', 'if (Le32(mode + 0x20) > 0x3F800000u)', 'if (false)', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a rate over 1 allowed', 'Le32(mode + 0x20) > 0x3F800000u', 'Le32(mode + 0x20) > 0x40000000u', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a rate of 1 refused', 'Le32(mode + 0x20) > 0x3F800000u', 'Le32(mode + 0x20) >= 0x3F800000u', 'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the rate compared as a float', 'if (Le32(mode + 0x20) > 0x3F800000u)', 'float rate;\n\t\t\tmemcpy(&rate, mode + 0x20, 4);\n\t\t\tif (rate > 1.0f)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the frames of the modes unchecked', 'return modeFrames < partFrames ? NULL :', 'return true ? NULL :', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a start or last equal to the frame count allowed', 'modeFrames < partFrames ?', 'modeFrames <= partFrames ?', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the last virtual frame refused to the modes', 'modeFrames < partFrames ?', 'modeFrames + 1 < partFrames ?', 'test_one_inline_mesh_of_the_profile_is_admitted'),
    ('inline: the start of a mode not held to the frames', 'std::max(modeFrames, std::max(Le32(mode + 0x14), Le32(mode + 0x18)))', 'std::max(modeFrames, Le32(mode + 0x18))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the last of a mode not held to the frames', 'std::max(modeFrames, std::max(Le32(mode + 0x14), Le32(mode + 0x18)))', 'std::max(modeFrames, Le32(mode + 0x14))',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the frames of the modes compared signed', 'modeFrames = std::max(modeFrames, std::max(Le32(mode + 0x14), Le32(mode + 0x18)));',
     'modeFrames = (unsigned)std::max((int)modeFrames, std::max((int)Le32(mode + 0x14), (int)Le32(mode + 0x18)));', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the start and last of a mode taken in 16 bits', 'std::max(Le32(mode + 0x14), Le32(mode + 0x18))', 'std::max(Le32(mode + 0x14) & 0xFFFFu, Le32(mode + 0x18) & 0xFFFFu)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ("inline: the frame counts of the parts not taken", 'frames = std::min(frames, vframes);', '(void)frames;', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ("inline: the frame count of each mesh's first part only", 'frames = std::min(frames, vframes);', 'frames = index ? frames : std::min(frames, vframes);',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the frames held to the last mesh of a chain only', 'bool chained = false;\n\t\tif (const char *bad = CheckOneMesh(', 'bool chained = false;\n\t\tpartFrames = ~0u;\n\t\tif (const char *bad = CheckOneMesh(',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the modes of the last mesh of a chain only', 'bool chained = false;\n\t\tif (const char *bad = CheckOneMesh(', 'bool chained = false;\n\t\tmodeFrames = 0;\n\t\tif (const char *bad = CheckOneMesh(',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    # every mesh of a chain has as many animation modes as the first
    ('inline: the animation-mode counts of a chain unchecked', 'else if (modes != firstModes)', 'else if (false && modes != firstModes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a chained mesh with fewer animation modes than the first allowed', 'else if (modes != firstModes)', 'else if (modes > firstModes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a chained mesh with more animation modes than the first allowed', 'else if (modes != firstModes)', 'else if (modes < firstModes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the animation-mode count of the second mesh of a chain only', 'else if (modes != firstModes)', 'else if (depth == 2 && modes != firstModes)',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    # the high bits of the indices the walk compares
    ('inline: the part number compared in 16 bits', 'if (Le32(p + 0x88) != index)', 'if ((Le32(p + 0x88) & 0xFFFFu) != index)', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the part number compared in 8 bits', 'if (Le32(p + 0x88) != index)', 'if ((Le32(p + 0x88) & 0xFFu) != (index & 0xFFu))', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a bone compared in 16 bits', 'if (Le32(p + 4 * i) >= parts)\n\t\t\t\treturn "an inline mesh part with a bone', 'if ((Le32(p + 4 * i) & 0xFFFFu) >= parts)\n\t\t\t\treturn "an inline mesh part with a bone',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a PRNT compared in 16 bits', 'size != 4 || Le32(p) >= index))', 'size != 4 || (Le32(p) & 0xFFFFu) >= index))', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a REFR compared in 16 bits', 'size != 4 || Le32(p) >= parts)\n\t\t\treturn "an inline mesh part whose REFR', 'size != 4 || (Le32(p) & 0xFFFFu) >= parts)\n\t\t\treturn "an inline mesh part whose REFR',
     'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: the VHFM bytes held to the frame count in 8 bits', 'if (p[i] >= hframes)', 'if (p[i] >= (hframes & 0xFFu))', 'test_one_inline_mesh_of_the_profile_is_admitted'),
]

# Later child slots and the exact reference boundary need their own causal controls.
MUTANTS += [
    ('inline: later child lower bound uses a wrong stride', '\t\t\tif (Le32(p + 4 * i) <= index || Le32(p + 4 * i) >= parts)', '\t\t\tif (Le32(p + 3 * i) <= index || Le32(p + 4 * i) >= parts)', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: only the first child has a lower bound', '\t\t\tif (Le32(p + 4 * i) <= index || Le32(p + 4 * i) >= parts)', '\t\t\tif ((i == 0 && Le32(p + 4 * i) <= index) || Le32(p + 4 * i) >= parts)', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: only the first child has an upper bound', '\t\t\tif (Le32(p + 4 * i) <= index || Le32(p + 4 * i) >= parts)', '\t\t\tif (Le32(p + 4 * i) <= index || (i == 0 && Le32(p + 4 * i) >= parts))', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
    ('inline: a reference equal to the part count allowed', '\t\tif (!m.Take("REFR", p, size) || size != 4 || Le32(p) >= parts)', '\t\tif (!m.Take("REFR", p, size) || size != 4 || Le32(p) > parts)', 'test_an_inline_mesh_the_loaders_readers_would_misread_is_refused'),
]


# each cache rule held only for the parts of one kind or place (no child, no collision grid, a normal part, not a reference, skeletal or camera part, one hand frame) lets the parts of the others through
MUTANTS += [(f"inline: the {rule} held only where a part {where}", old, f"if ({condition} && {cond})", CACHE_KINDS)
            for rule, old, cond in (("cached frame count", "if (cached != 1 && cached != vframes)", "cached != 1 && cached != vframes"),
                                    ("CPOS rule", "if (cachedPositions == positionStatic)", "cachedPositions == positionStatic"),
                                    ("CORI rule", "if (cachedOrientations == orientationStatic)", "cachedOrientations == orientationStatic"),
                                    ("rule of a parent for a static part", "if ((positionStatic || orientationStatic) && !parent)", "(positionStatic || orientationStatic) && !parent"))
            for where, condition in (("lists no child", "!children"), ("has no collision grid", "!grid"), ("is a normal part", "partType == 1"), ("is not a reference part", "!reference"),
                                     ("is not a skeletal part", "!skeletal"), ("is not a camera part", "!camera"), ("has 1 hand frame", "hframes == 1"))]


def run_mutants():
    source = MODS_CPP.read_text()
    failures = unexercised = 0
    only = [s for s in os.environ.get("MODS_TESTS_ONLY", "").split("|") if s]	# a rerun of chosen mutants: names containing any of these texts
    for name, old, new, test, *defines in MUTANTS:
        if only and not any(s in name for s in only):
            continue
        if source.count(old) != 1:
            print(f"mutant {name}: its text is not in mods.cpp exactly once")
            failures += 1
            continue
        mutant = source.replace(old, new)
        try:
            exe = driver(mutant, defines=defines[0] if defines else ())
            ce_exe = driver(mutant, ce_version="0.1.9") if test == "test_ce_version_range" else None
        except RuntimeError as e:
            print(f"mutant {name}: does not build: {e}")
            failures += 1
            continue
        ModsTests.exe, ModsTests.ce_exe, ModsTests.source = exe, ce_exe, mutant
        result = unittest.TextTestRunner(stream=open(os.devnull, "w"), verbosity=0).run(ModsTests(test))
        ModsTests.exe = ModsTests.ce_exe = ModsTests.source = None
        killed = not result.wasSuccessful()
        if not killed and result.skipped:
            print(f"mutant {name}: NOT EXERCISED ({test} was skipped: {result.skipped[0][1]})")
            unexercised += 1
            continue
        print(f"mutant {name}: {'killed by ' + test if killed else 'SURVIVED ' + test}")
        failures += not killed
    return failures, unexercised


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    mutants = "--no-mutants" not in sys.argv
    mutants_only = "--mutants-only" in sys.argv
    argv = [a for a in sys.argv if a not in ("--no-mutants", "--mutants-only")]
    if not mutants_only:
        program = unittest.main(argv=argv, exit=False, verbosity=2)
        if not program.result.wasSuccessful():
            return 1
    if mutants:
        failed, unexercised = run_mutants()
        only = [s for s in os.environ.get("MODS_TESTS_ONLY", "").split("|") if s]
        chosen = sum(1 for m in MUTANTS if not only or any(s in m[0] for s in only))
        print(f"mutants: {chosen - failed - unexercised} of {chosen} killed, {unexercised} not exercised")
        return 1 if failed else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
