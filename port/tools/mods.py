#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Onslaught CE mod tools (experimental): pack and unpack the game's .aya files, write a mod's mod.json, and build the
test texture mods from the player's own install.

    python3 port/tools/mods.py unpack IN.aya OUT.dds          # a packed file's contents
    python3 port/tools/mods.py pack IN.dds OUT.aya            # packed as the game reads it (1 MiB zlib blocks)
    python3 port/tools/mods.py manifest MODDIR --name N --version V [--author A] [--description D]
                                        [--ce-min X.Y.Z] [--ce-max X.Y.Z]   # MODDIR/mod.json from MODDIR/data/**
    python3 port/tools/mods.py test-mod KIND USERDIR [--game GAMEDIR]
    python3 port/tools/mods.py preview IN.aya|IN.dds OUT.png  # the top mip level of a DXT2/DXT3 texture, for a look

A mod is USERDIR/mods/<id>/ with mod.json and the replacement files at their game-relative paths (data/...); ce.ini's
mods=<id>,... line enables mods in order (port/platform/common/mods.h). The id is the folder's name.

test-mod builds one of the test mods from the battle engine's texture in the selected install (never committed):
  recolor  ce-test-recolor: meshtex%BE_texA, its colour channels rotated (the blue hull turns red), same size
  hd2x     ce-test-hd2x: the same texture at twice the resolution (an exact nearest-neighbour upscale in the DXT
           blocks, plus one more mip level), its channels rotated the other way (green) so a capture shows which copy loaded
The tool creates new outputs only and does not enable mods or write ce.ini. Enable a generated mod yourself after
closing the game and backing up your settings. File publication currently requires Linux no-follow directory handles.
"""
import argparse
import hashlib
import json
import os
import re
import stat
import struct
import sys
import zlib
from pathlib import Path

BLOCK = 1 << 20				# the game's read buffer (src/DXMemBuffer.cpp READ_MEM_BUFFER_SIZE)
PACK_BUFFER = 0x102927		# the game's block buffer (PACK_BUFFER_SIZE)
TEXTURE_LIMIT = 0x500000	# LoadFrame's scratch buffer: a texture file must unpack to less
MAX_UNPACKED = 64 << 20	# the runtime mod admission bound
MAX_MANIFEST_NODES = 8192
MAX_MANIFEST_BYTES = 1 << 30
BE_TEXTURE = "data/resources/dxtntextures/meshtex%BE_texA.tga(0)A1R5G5B5.aya"
ID_RE = re.compile(r"[a-z0-9_-]{1,48}")


def default_game():
    env = os.environ.get("BEA_GAME_DIR")
    return Path(env) if env else Path.home() / ".local/share/Steam/steamapps/common/Battle Engine Aquila"


def unpack(data):
    """The contents of a packed .aya file: [u32 length][zlib data] blocks, each at most 1 MiB unpacked."""
    if not data:
        raise ValueError("an empty packed file")
    out, pos = bytearray(), 0
    while pos < len(data):
        if len(data) - pos < 4:
            raise ValueError("a cut block header")
        (n,) = struct.unpack_from("<I", data, pos)
        pos += 4
        if n == 0 or n > PACK_BUFFER or len(data) - pos < n:
            raise ValueError("a bad block length")
        decoder = zlib.decompressobj()
        block = decoder.decompress(data[pos:pos + n], BLOCK + 1)
        if len(block) > BLOCK or decoder.unconsumed_tail:
            raise ValueError("a block over 1 MiB")
        if not decoder.eof or decoder.unused_data:
            raise ValueError("an incomplete block or trailing compressed data")
        if len(out) + len(block) > MAX_UNPACKED:
            raise ValueError("the unpacked file is over 64 MiB")
        if len(block) < BLOCK and pos + n < len(data):
            raise ValueError("a short block before the last block")
        out += block
        pos += n
    return bytes(out)


def pack(data):
    """Packed as the game reads it: 1 MiB blocks (the last one shorter), each zlib-compressed behind its length."""
    if len(data) > MAX_UNPACKED:
        raise ValueError("the source file is over 64 MiB")
    out = bytearray()
    for start in range(0, max(len(data), 1), BLOCK):
        z = zlib.compress(data[start:start + BLOCK], 9)
        if len(z) > PACK_BUFFER:
            raise ValueError("a block does not compress into the game's block buffer")
        out += struct.pack("<I", len(z)) + z
    return bytes(out)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def _read_source(path):
    """A bounded regular source; refuse FIFOs, devices and final links instead of blocking or reading without limit."""
    if sys.platform != "linux":
        raise ValueError("safe mod-tool input is currently available only on Linux")
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or before.st_size > MAX_UNPACKED:
            raise ValueError("source must be a regular file no larger than 64 MiB")
        data = bytearray()
        while len(data) <= MAX_UNPACKED:
            chunk = os.read(fd, min(1 << 20, MAX_UNPACKED + 1 - len(data)))
            if not chunk:
                break
            data += chunk
        after = os.fstat(fd)
        identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
        if len(data) > MAX_UNPACKED or identity(before) != identity(after) or len(data) != before.st_size:
            raise ValueError("source changed while reading or exceeded 64 MiB")
        return bytes(data)
    finally:
        os.close(fd)


def _mount_id(fd):
    with open(f"/proc/self/fdinfo/{fd}", "r", encoding="ascii") as f:
        for line in f:
            if line.startswith("mnt_id:"):
                return int(line.split(":", 1)[1])
    raise ValueError("cannot determine the output filesystem's mount identity")


def _check_output_directory(fd, game_guard, final=False):
    st = os.fstat(fd)
    if (st.st_dev, st.st_ino) == game_guard[:2]:
        raise ValueError("output must be outside the game folder")
    # A bind mount of a game subtree can alias the same filesystem without sharing its lexical path. Refuse an
    # alternate mount of that filesystem; it may also refuse a harmless alternate mount, which is safer than writing.
    if final and st.st_dev == game_guard[0] and _mount_id(fd) != game_guard[2]:
        raise ValueError("output is on an unverified alternate mount of the game's filesystem")


def _open_directory(path, game_guard):
    """Hold every component without following a link. Fail closed on platforms without this API."""
    if sys.platform != "linux" or not hasattr(os, "O_NOFOLLOW") or os.open not in os.supports_dir_fd:
        raise ValueError("safe mod-tool output is currently available only on Linux")
    path = Path(path)
    if ".." in path.parts:
        raise ValueError("parent traversal is not allowed in output paths")
    path = path if path.is_absolute() else Path.cwd() / path
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
    fd = os.open("/", flags)
    try:
        for part in path.parts[1:]:
            if part in ("", "."):
                continue
            child = os.open(part, flags, dir_fd=fd)
            os.close(fd)
            fd = child
            _check_output_directory(fd, game_guard)
        _check_output_directory(fd, game_guard, final=True)
        return fd
    except BaseException:
        os.close(fd)
        raise


def _new_file(dir_fd, name, data):
    """Publish only a fresh name; never follow a link or overwrite an existing file."""
    if not name or name in (".", "..") or "/" in name:
        raise ValueError("invalid output name")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC
    fd = os.open(name, flags, 0o600, dir_fd=dir_fd)
    with os.fdopen(fd, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.fsync(dir_fd)


def _write_new(path, data, game_guard):
    path = Path(path)
    fd = _open_directory(path.parent, game_guard)
    try:
        _new_file(fd, path.name, data)
    finally:
        os.close(fd)


def _reject_game_output(path, game):
    """Never create output in the selected/default game tree, even when the given paths alias each other."""
    output = Path(path).resolve(strict=False)
    root = Path(game).resolve(strict=True)
    if output == root or root in output.parents:
        raise ValueError("output must be outside the game folder")
    game_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        st = os.fstat(game_fd)
        return st.st_dev, st.st_ino, _mount_id(game_fd)
    finally:
        os.close(game_fd)


def _manifest_files(dir_fd, parts, files, budget):
    if len(parts) > 12:
        raise ValueError("a mod path is deeper than 12 components")
    names = []
    with os.scandir(dir_fd) as entries:
        for entry in entries:
            budget[0] += 1
            if budget[0] > MAX_MANIFEST_NODES:
                raise ValueError("a mod contains more than 8192 filesystem entries")
            names.append(entry.name)
    for name in sorted(names):
        st = os.stat(name, dir_fd=dir_fd, follow_symlinks=False)
        if stat.S_ISLNK(st.st_mode):
            raise ValueError("links are refused in a mod")
        if stat.S_ISDIR(st.st_mode):
            child = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=dir_fd)
            try:
                _manifest_files(child, parts + [name], files, budget)
            finally:
                os.close(child)
            continue
        fd = os.open(name, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=dir_fd)
        try:
            before = os.fstat(fd)
            if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or before.st_size > MAX_UNPACKED:
                raise ValueError("mod files must be ordinary single-link files no larger than 64 MiB")
            budget[1] += before.st_size
            if budget[1] > MAX_MANIFEST_BYTES:
                raise ValueError("a mod contains more than 1 GiB of files")
            digest = hashlib.sha256()
            count = 0
            while True:
                chunk = os.read(fd, 1 << 20)
                if not chunk:
                    break
                count += len(chunk)
                if count > MAX_UNPACKED:
                    raise ValueError("a mod file exceeds 64 MiB")
                digest.update(chunk)
            after = os.fstat(fd)
            identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_nlink, s.st_mtime_ns, s.st_ctime_ns)
            if identity(before) != identity(after) or count != before.st_size:
                raise ValueError("a mod file changed while its manifest was read")
            files.append({"path": "/".join(parts + [name]), "size": count, "sha256": digest.hexdigest()})
            if len(files) > 4096:
                raise ValueError("a mod has more than 4096 files")
        finally:
            os.close(fd)


def write_manifest(moddir, name, version, author="", description="", ce_min=None, ce_max=None, game=None):
    moddir = Path(moddir)
    mod_id = moddir.name
    if not ID_RE.fullmatch(mod_id):
        raise SystemExit(f"{mod_id!r}: a mod folder's name is 1 to 48 lowercase letters, digits, '-' or '_'")
    guard = _reject_game_output(moddir / "mod.json", game if game is not None else default_game())
    files = []
    root = _open_directory(moddir, guard)
    try:
        data = os.open("data", os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=root)
        try:
            _manifest_files(data, ["data"], files, [0, 0])
        finally:
            os.close(data)
        if not files:
            raise ValueError(f"{moddir}/data holds no files")
        manifest = {"schema": 1, "id": mod_id, "name": name, "version": version}
        if author:
            manifest["author"] = author
        if description:
            manifest["description"] = description
        if ce_min:
            manifest["ce_min"] = ce_min
        if ce_max:
            manifest["ce_max"] = ce_max
        manifest["files"] = files
        _new_file(root, "mod.json", (json.dumps(manifest, indent=2) + "\n").encode())
        return manifest
    finally:
        os.close(root)


# ---- DXT2/DXT3 (the resources' battle engine textures) --------------------------------------------------------------
def dds_header(dds):
    if len(dds) < 128 or dds[:4] != b"DDS " or struct.unpack_from("<I", dds, 4)[0] != 124:
        raise ValueError("not a DDS image")
    height, width = struct.unpack_from("<II", dds, 12)
    mips = max(1, struct.unpack_from("<I", dds, 28)[0])
    fourcc = dds[84:88]
    if fourcc not in (b"DXT2", b"DXT3"):
        raise ValueError(f"{fourcc!r}: only DXT2 and DXT3 textures are handled here")
    if not width or not height or width > 4096 or height > 4096:
        raise ValueError("DDS dimensions must be between 1 and 4096")
    if mips > max(width, height).bit_length():
        raise ValueError("DDS has too many mip levels")
    expected = 128 + sum(size for _, _, size in level_sizes(width, height, mips))
    if len(dds) != expected:
        raise ValueError("DDS payload size does not match its mip levels")
    return width, height, mips


def level_sizes(width, height, mips):
    out = []
    for _ in range(mips):
        out.append((width, height, max(1, (width + 3) // 4) * max(1, (height + 3) // 4) * 16))
        width, height = max(1, width // 2), max(1, height // 2)
    return out


def rgb565(c):
    return (c >> 11) & 31, (c >> 5) & 63, c & 31


def to565(r, g, b):
    return (min(31, r) << 11) | (min(63, g) << 5) | min(31, b)


def recolor(dds, fn):
    """Applies fn((r5, g6, b5)) -> (r5, g6, b5) to both colour endpoints of every block (exact for DXT2/3: their
    colour blocks always interpolate four colours, whatever the endpoints' order)."""
    width, height, mips = dds_header(dds)
    out = bytearray(dds)
    for i in range(128 + 8, len(out), 16):
        c0, c1 = struct.unpack_from("<HH", out, i)
        struct.pack_into("<HH", out, i, to565(*fn(rgb565(c0))), to565(*fn(rgb565(c1))))
    return bytes(out)


def channels(c):
    """5/6/5-bit endpoint values as 0..255 levels, and back."""
    r, g, b = c
    return r * 255 // 31, g * 255 // 63, b * 255 // 31


def from_levels(r, g, b):
    return (r * 31 + 127) // 255, (g * 63 + 127) // 255, (b * 31 + 127) // 255


def red_rotation(c):
    """Red <- blue, green <- red, blue <- green: the blue hull turns red. A permutation keeps DXT2's premultiplied
    colour within its alpha and leaves greys and black as they were."""
    r, g, b = channels(c)
    return from_levels(b, r, g)


def green_rotation(c):
    """Red <- green, green <- blue, blue <- red: the blue hull turns green."""
    r, g, b = channels(c)
    return from_levels(g, b, r)


def upscale2x(dds):
    """Twice the size in each direction, exactly: each new block takes one quadrant of an old block (same endpoints,
    its 2x2 texels repeated), and the old levels become the new levels 1.. (one more mip level in all)."""
    width, height, mips = dds_header(dds)
    levels = level_sizes(width, height, mips)
    top = dds[128:128 + levels[0][2]]
    bw, bh = max(1, width // 4), max(1, height // 4)
    if width % 4 or height % 4:
        raise ValueError("the top level is not whole blocks")
    if width * 2 > 4096 or height * 2 > 4096 or len(dds) + 4 * len(top) >= TEXTURE_LIMIT:
        raise ValueError("the enlarged texture exceeds the mod loader's bounds")
    new = bytearray(4 * len(top))
    nbw = bw * 2
    for by in range(bh * 2):
        for bx in range(nbw):
            src = top[((by // 2) * bw + bx // 2) * 16:][:16]
            qx, qy = (bx % 2) * 2, (by % 2) * 2
            alpha_rows = struct.unpack_from("<4H", src, 0)
            c0, c1, idx = struct.unpack_from("<HHI", src, 8)
            arows, nidx = [], 0
            for y in range(4):
                row = 0
                for x in range(4):
                    sx, sy = qx + x // 2, qy + y // 2
                    row |= ((alpha_rows[sy] >> (4 * sx)) & 15) << (4 * x)
                    nidx |= ((idx >> (2 * (sy * 4 + sx))) & 3) << (2 * (y * 4 + x))
                arows.append(row)
            struct.pack_into("<4HHHI", new, (by * nbw + bx) * 16, *arows, c0, c1, nidx)
    head = bytearray(dds[:128])
    struct.pack_into("<IIII", head, 12, height * 2, width * 2, len(new), 0)
    struct.pack_into("<I", head, 28, mips + 1)
    return bytes(head) + bytes(new) + dds[128:]


def decode_top(dds):
    """The top level of a DXT2/DXT3 image as (width, height, RGBA bytes); DXT2's premultiplied colour is left as is."""
    width, height, _ = dds_header(dds)
    bw = max(1, (width + 3) // 4)
    rgba = bytearray(width * height * 4)
    data = dds[128:]
    for b in range(bw * max(1, (height + 3) // 4)):
        blk = data[b * 16:(b + 1) * 16]
        alpha_rows = struct.unpack_from("<4H", blk, 0)
        c0, c1, idx = struct.unpack_from("<HHI", blk, 8)
        cols = []
        for c in (c0, c1):
            r, g, bb = rgb565(c)
            cols.append((r * 255 // 31, g * 255 // 63, bb * 255 // 31))
        cols.append(tuple((2 * a + b2) // 3 for a, b2 in zip(cols[0], cols[1])))
        cols.append(tuple((a + 2 * b2) // 3 for a, b2 in zip(cols[0], cols[1])))
        x0, y0 = (b % bw) * 4, (b // bw) * 4
        for y in range(4):
            for x in range(4):
                if x0 + x >= width or y0 + y >= height:
                    continue
                r, g, bb = cols[(idx >> (2 * (y * 4 + x))) & 3]
                a = ((alpha_rows[y] >> (4 * x)) & 15) * 17
                o = ((y0 + y) * width + x0 + x) * 4
                rgba[o:o + 4] = bytes((r, g, bb, a))
    return width, height, bytes(rgba)


def enable(user, mod_id):
    """Automatic settings writes are unavailable until guarded game-running and recovery checks exist."""
    raise ValueError("automatic mod enabling is disabled; close the game and edit a backed-up ce.ini yourself")


def test_mod(kind, user, game):
    if kind not in ("recolor", "hd2x"):
        raise ValueError("unknown test mod")
    mod_id = "ce-test-recolor" if kind == "recolor" else "ce-test-hd2x"
    moddir = Path(user) / "mods" / mod_id
    guard = _reject_game_output(moddir, game)
    source = Path(game) / BE_TEXTURE
    if not source.is_file():
        raise SystemExit(f"{source}: not found (select the install with --game or BEA_GAME_DIR)")
    dds = unpack(_read_source(source))
    if kind == "recolor":
        mod_id, name, out = "ce-test-recolor", "CE test: recoloured battle engine", recolor(dds, red_rotation)
    else:
        mod_id, name, out = "ce-test-hd2x", "CE test: battle engine texture at 2x", upscale2x(recolor(dds, green_rotation))
    if len(out) >= TEXTURE_LIMIT:
        raise SystemExit(f"{kind}: {len(out)} bytes unpacked is over the game's texture limit")
    packed = pack(out)
    manifest = {"schema": 1, "id": mod_id, "name": name, "version": "1", "author": "Onslaught CE tests",
                "description": "Generated at test time from the player's own install; never distributed.",
                "files": [{"path": BE_TEXTURE, "size": len(packed), "sha256": sha256(packed)}]}
    user_fd = _open_directory(user, guard)
    try:
        try:
            os.mkdir("mods", 0o700, dir_fd=user_fd)
        except FileExistsError:
            pass
        mods_fd = os.open("mods", os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=user_fd)
        try:
            _check_output_directory(mods_fd, guard, final=True)
            # Do not reuse a previous generated folder, even if it happens to be a link to the game or another mod.
            os.mkdir(mod_id, 0o700, dir_fd=mods_fd)
            mod_fd = os.open(mod_id, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=mods_fd)
            try:
                _check_output_directory(mod_fd, guard, final=True)
                parent = os.dup(mod_fd)
                try:
                    parts = BE_TEXTURE.split("/")
                    for part in parts[:-1]:
                        os.mkdir(part, 0o700, dir_fd=parent)
                        child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                                        dir_fd=parent)
                        os.close(parent)
                        parent = child
                    _new_file(parent, parts[-1], packed)
                finally:
                    os.close(parent)
                _new_file(mod_fd, "mod.json", (json.dumps(manifest, indent=2) + "\n").encode())
            finally:
                os.close(mod_fd)
        finally:
            os.close(mods_fd)
    finally:
        os.close(user_fd)
    return mod_id, moddir


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for cmd in ("unpack", "pack", "preview"):
        p = sub.add_parser(cmd)
        p.add_argument("src", type=Path)
        p.add_argument("dst", type=Path)
        p.add_argument("--game", type=Path, default=default_game())
    m = sub.add_parser("manifest")
    m.add_argument("moddir", type=Path)
    m.add_argument("--name", required=True)
    m.add_argument("--version", required=True)
    m.add_argument("--author", default="")
    m.add_argument("--description", default="")
    m.add_argument("--ce-min")
    m.add_argument("--ce-max")
    m.add_argument("--game", type=Path, default=default_game())
    t = sub.add_parser("test-mod")
    t.add_argument("kind", choices=("recolor", "hd2x"))
    t.add_argument("user", type=Path)
    t.add_argument("--game", type=Path, default=default_game())
    t.add_argument("--enable", action="store_true")
    a = ap.parse_args()
    if a.cmd == "unpack":
        guard = _reject_game_output(a.dst, a.game)
        _write_new(a.dst, unpack(_read_source(a.src)), guard)
    elif a.cmd == "pack":
        guard = _reject_game_output(a.dst, a.game)
        _write_new(a.dst, pack(_read_source(a.src)), guard)
    elif a.cmd == "preview":
        import io
        from PIL import Image
        guard = _reject_game_output(a.dst, a.game)
        data = _read_source(a.src)
        w, h, rgba = decode_top(unpack(data) if a.src.suffix.lower() == ".aya" else data)
        output = io.BytesIO()
        Image.frombytes("RGBA", (w, h), rgba).convert("RGB").save(output, format="PNG")
        _write_new(a.dst, output.getvalue(), guard)
    elif a.cmd == "manifest":
        m = write_manifest(a.moddir, a.name, a.version, a.author, a.description, a.ce_min, a.ce_max, a.game)
        print(f"{a.moddir / 'mod.json'}: {len(m['files'])} files")
    else:
        if a.enable:
            enable(a.user, "")  # refuse before creating even a new test-mod folder
        mod_id, moddir = test_mod(a.kind, a.user, a.game)
        print(f"{moddir}: {mod_id} (not enabled)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, zlib.error) as error:
        print(f"mods: {error}", file=sys.stderr)
        sys.exit(2)
