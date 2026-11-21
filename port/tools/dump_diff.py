#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The first difference between two per-update dumps (port/game/updatedump.cpp, BEA_PORT_UPDATE_DUMP).

    python3 port/tools/dump_diff.py A B [--all] [--json OUT]

A and B are dump files, or two journeys' result.json files whose expect.json asked for "update_dump" (port/journeys/README.md).

    python3 port/tools/dump_diff.py --hashes DUMP > BASE.hashes

writes one line per update ("update sha256", the sha256 of the update's block as written), a few KB for a stored base. Either
side of a comparison may be such a file; the report then gives the first differing update, but no field (the field needs both
full dumps).

A dump has one block per Update: a "U" line (update, Presents, event time, random value and seed, draws, thing count) and
one "H" line per thing (number, type, class, x, y, z as float bits). Blocks are matched by update number. The first
difference is reported as the update, the field (event time, rng, draws, thing count, a thing added or gone, or a thing's
class or one axis) and both values, with float bits decoded. --all lists every differing update (first field of each).
The dumps are read as streams, one update's block at a time from each, in constant memory (a dump of 10,000 Presents is
over a gigabyte): blocks are compared by their hash, and only a pair that differs is parsed to name the field.
Exit 0 only when both dumps hold the same updates and agree on all of them; 1 at a difference, including dumps that cover
different updates (one cut short, or a different range: "differ in length"); 2 when they share no update (NOT COMPARABLE,
never a pass)."""
import argparse
import hashlib
import json
import struct
import sys


def fbits(text):
    try:
        return struct.unpack("<f", struct.pack("<I", int(text, 16)))[0]
    except (ValueError, struct.error):
        return text


def blocks(path):
    """(update, sha256 of the block's lines, the block's lines) for each update in a dump, one at a time."""
    lines, update, h = [], None, None
    with open(path, errors="replace") as f:
        for line in f:
            if line.startswith("U "):
                if update is not None:
                    yield update, h.hexdigest(), lines
                try:
                    update = int(line.split()[1])
                except (IndexError, ValueError):
                    update = None
                    continue
                lines, h = [], hashlib.sha256()
            if update is None:
                continue
            text = line.rstrip("\n")
            h.update(text.encode())
            h.update(b"\n")
            lines.append(text)
    if update is not None:
        yield update, h.hexdigest(), lines


def hash_lines(path):
    """(update, hash, None) from a .hashes file (one "update sha256" line per update)."""
    with open(path) as f:
        for line in f:
            w = line.split()
            if len(w) == 2:
                yield int(w[0]), w[1], None


def parse(lines):
    fields, things = {}, {}
    for line in lines:
        w = line.split()
        if not w:
            continue
        if w[0] == "U":
            fields = dict(zip(w[2::2], w[3::2]))
            # "U <update> F <n> E <bits> R <value> <seed> D <n> N <n>": R carries two words
            if "R" in w:
                i = w.index("R")
                fields["R"] = " ".join(w[i + 1:i + 3])
                rest = w[i + 3:]
                fields.update(dict(zip(rest[0::2], rest[1::2])))
        elif w[0] == "H" and len(w) >= 7:
            things[w[1]] = {"type": w[2], "class": w[3], "x": w[4], "y": w[5], "z": w[6]}
    return {"fields": fields, "things": things}


NAMES = {"F": "presents", "E": "event time", "R": "rng (value seed)", "D": "draws since reseed", "N": "thing count"}


def first_difference(a, b):
    for key in ("E", "R", "D", "N"):
        if a["fields"].get(key) != b["fields"].get(key):
            va, vb = a["fields"].get(key), b["fields"].get(key)
            if key == "E":
                va, vb = f"{va} ({fbits(va)})", f"{vb} ({fbits(vb)})"
            return {"field": NAMES[key], "a": va, "b": vb}
    for n in sorted(set(a["things"]) | set(b["things"]), key=lambda s: int(s) if s.isdigit() else 0):
        ta, tb = a["things"].get(n), b["things"].get(n)
        if ta is None or tb is None:
            return {"field": f"thing {n} {'added in B' if ta is None else 'gone in B'}", "a": ta, "b": tb}
        for k in ("class", "type", "x", "y", "z"):
            if ta[k] != tb[k]:
                va, vb = ta[k], tb[k]
                if k in "xyz":
                    va, vb = f"{va} ({fbits(va)})", f"{vb} ({fbits(vb)})"
                return {"field": f"thing {n} ({ta['class']}) {k}", "a": va, "b": vb}
    return {"field": "the update's block differs", "a": None, "b": None}


def dump_path(arg):
    """A dump file, or the update_dump a journey's result.json names."""
    if arg.endswith(".json"):
        with open(arg) as f:
            path = json.load(f).get("update_dump")
        if not path:
            sys.exit(f"{arg}: the run has no update dump (its expect.json did not ask for one)")
        return path
    return arg


def source(path):
    return hash_lines(path) if path.endswith(".hashes") else blocks(path)


def compare(pa, pb, everything):
    """Walk both streams in update order. Returns (count A, count B, common, differences, first update in one only)."""
    ia, ib = source(pa), source(pb)
    na = nb = common = 0
    differences, only = [], None
    a, b = next(ia, None), next(ib, None)
    while a is not None or b is not None:
        if b is None or (a is not None and a[0] < b[0]):
            na += 1
            only = a[0] if only is None else only
            a = next(ia, None)
        elif a is None or b[0] < a[0]:
            nb += 1
            only = b[0] if only is None else only
            b = next(ib, None)
        else:
            na, nb, common = na + 1, nb + 1, common + 1
            if a[1] != b[1] and (everything or not differences):
                if a[2] is not None and b[2] is not None:
                    differences.append(dict(update=a[0], **first_difference(parse(a[2]), parse(b[2]))))
                else:
                    differences.append({"update": a[0], "field": "the update's block differs (compare the full dumps for the field)",
                                        "a": None, "b": None})
            a, b = next(ia, None), next(ib, None)
    return na, nb, common, differences, only


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b", nargs="?")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--json")
    ap.add_argument("--hashes", action="store_true", help="print one sha256 per update of dump A")
    o = ap.parse_args()
    if o.hashes:
        for u, h, _ in blocks(dump_path(o.a)):
            print(u, h)
        return 0
    if not o.b:
        ap.error("two dumps (or a dump and a .hashes file) to compare")
    o.a, o.b = dump_path(o.a), dump_path(o.b)
    na, nb, common, differences, only = compare(o.a, o.b, o.all)
    report = {"a": o.a, "b": o.b, "updates_a": na, "updates_b": nb, "common": common, "differences": differences}
    if not common:
        report["verdict"] = "NOT COMPARABLE: the dumps share no update"
    else:
        if not differences and only is not None:
            # agreement over a shared part is not agreement: a dump cut short must not pass
            differences.append({"update": only, "field": "differ in length", "a": f"{na} updates", "b": f"{nb} updates"})
        report["verdict"] = "same" if not differences else "differ"
    if o.json:
        with open(o.json, "w") as f:
            json.dump(report, f, indent=1)
    print(f"{report['verdict']} ({common} common updates of {na} and {nb})")
    for d in differences[:20]:
        tail = f": A {d['a']}  B {d['b']}" if d["a"] is not None else ""
        print(f"  update {d['update']}: {d['field']}{tail}")
    return 2 if not common else 1 if differences else 0


if __name__ == "__main__":
    sys.exit(main())
