#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Who can write, replace or delete a career in a network process: a census of the patched game source, with mutants.

    python3 port/tools/network_career_writers_tests.py

A network process (the lobby's start, or a test co-op request) must leave every career as it was. The port's file layer refuses any open for writing that is not on the admission's
allow-list (options, card id, setup log, exception log), but PortReplaceUserFile, the atomic replacement the careers and the options file are written with (patch 0117), and the direct
delete of a career have no such gate of their own: patch 0185 fences the three career functions (CreateSave, WriteSave, DeleteSave) before their first statement. This suite reads the
patched source as a build with the network compiles it (build.patched_text, every file of src/) and requires:
  - PortReplaceUserFile is called from exactly two functions of the game, the career write (CPCMemoryCard::WriteSave) and the options write (CFEPOptions::WriteDefaultOptionsFile), and from
    nothing else in port/ (its own definitions excepted);
  - the career write is fenced (CE_BootRequested() || CE_LockstepRequested()) before its first call, and so are the career create and delete, before their first write;
  - the options write, the one caller without the fence, names the options file and nothing else;
  - the other calls in the game that delete, move, copy or rename a file are the ones known (each counted by file and function), none names a career, so a new one fails here until it is read;
and has mutants of the patched text: an unfenced career write, a fence after the call, a new unfenced caller of the atomic replacement, and a new deleting function.
No game, save, device or network is used; nothing is printed from the game's source but function names and counts.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402

FENCE = "CE_BootRequested() || CE_LockstepRequested()"
# primitives that remove, move, copy or create a file or a folder by name without passing through the port's open-time admission
BYPASS = re.compile(r"(?<![\w.>:])(PortReplaceUserFile|DeleteFile[AW]?|MoveFile(?:Ex)?[AW]?|CopyFile[AW]?|_?unlink|rename|remove|CreateDirectory[AW]?|_?mkdir)\s*\(")
# the text before a match on its line, when the match is a declaration or a definition (a type in front of the name)
DECLARATION = re.compile(r"\b(?:int|void|unsigned|bool|char|long|extern)\s+(?:\"C\"\s+)?[*&\s]*$")
CAREER_WRITERS = ("CPCMemoryCard::CreateSave", "CPCMemoryCard::WriteSave", "CPCMemoryCard::DeleteSave")
# (file, function, primitive) -> count: every known call; the career functions are fenced, the rest were read (none names a career) when this table was made
KNOWN = {
    ("PCMemoryCard.cpp", "CPCMemoryCard::CreateSave", "mkdir"): 2,            # the savegames folder (the Windows and the Linux branch), inside the fence
    ("PCMemoryCard.cpp", "CPCMemoryCard::DeleteSave", "DeleteFile"): 1,       # the career delete, inside the fence
    ("PCMemoryCard.cpp", "CPCMemoryCard::WriteSave", "PortReplaceUserFile"): 1,  # the career write, inside the fence
    ("FEPOptions.cpp", "CFEPOptions::WriteDefaultOptionsFile", "PortReplaceUserFile"): 1,  # the options file
}
# calls in the game that are not careers: a folder, a literal that is not a career, or a container's own remove (checked below by shape)
OTHER = {("DXTexture.cpp", "CreateDirectory"), ("game.cpp", "CreateDirectory"), ("ltshell.cpp", "DeleteFile"), ("Platform.cpp", "mkdir")}


def functions(text):
    """(start, name) of every function definition with its opening brace at column 0 (the game's style); inline members of a class are not found."""
    defs = []
    for m in re.finditer(r"^[A-Za-z_][^\n;{}=]*?\b((?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*)\s*\([^;{}]*\)[^;{}]*\n\{", text, re.M):
        defs.append((m.start(), m.group(1)))
    return defs


def census(texts):
    """{(file, function, primitive): [offsets...]} over every unqualified call of a BYPASS primitive in code (comments and strings blanked)."""
    found = {}
    for name, text in texts.items():
        code = re.sub(r"//[^\n]*|/\*.*?\*/", lambda m: " " * len(m.group(0)), text, flags=re.S)
        code = re.sub(r'"(?:\\.|[^"\\\n])*"', lambda m: '"' + " " * (len(m.group(0)) - 2) + '"', code)
        defs = functions(code)
        for m in BYPASS.finditer(code):
            if DECLARATION.search(code[code.rfind("\n", 0, m.start()) + 1:m.start()]):
                continue            # a prototype (patch 0117 declares PortReplaceUserFile in the files that call it), not a call
            before = [d for d in defs if d[0] < m.start()]
            fn = before[-1][1] if before else "?"
            prim = re.sub(r"^_", "", m.group(1))
            prim = re.sub(r"(?<=File)[AW]$|(?<=Ex)[AW]$|(?<=Directory)[AW]$", "", prim)
            found.setdefault((name, fn, prim), []).append(m.start())
    return found


def body(text, function):
    start = next((p for p, n in functions(text) if n == function), None)
    if start is None:
        return None
    nxt = [p for p, n in functions(text) if p > start]
    return text[start:nxt[0] if nxt else len(text)]


def problems(texts, port_callers):
    """What is wrong with the census of {file: patched text} and the callers of PortReplaceUserFile found in port/."""
    out = []
    found = census(texts)
    atomic = sorted((f, fn) for (f, fn, prim) in found if prim == "PortReplaceUserFile")
    if atomic != [("FEPOptions.cpp", "CFEPOptions::WriteDefaultOptionsFile"), ("PCMemoryCard.cpp", "CPCMemoryCard::WriteSave")]:
        out.append("PortReplaceUserFile is called from %s, not from exactly the career write and the options write" % atomic)
    if port_callers:
        out.append("PortReplaceUserFile is called from port/ outside its own definitions: %s" % port_callers)
    for fn in CAREER_WRITERS:
        text = texts.get("PCMemoryCard.cpp", "")
        b = body(text, fn)
        if b is None:
            out.append("%s was not found in the patched PCMemoryCard.cpp" % fn)
            continue
        fence = b.find(FENCE)
        calls = [o - text.index(b) for (f, f2, prim), offs in found.items() if f == "PCMemoryCard.cpp" and f2 == fn for o in offs]
        if fence < 0:
            out.append("%s has no fence" % fn)
            continue
        # the fence is the first statement: between the function's opening brace and the condition there is nothing but comments (and the "if (" of the fence itself)
        opening = b.find("\n{\n")
        before = re.sub(r"//[^\n]*|/\*.*?\*/", "", b[opening + 3:fence], flags=re.S)
        before = re.sub(r"\bif\s*\(\s*$", "", before.rstrip())
        if opening < 0 or before.strip():
            out.append("%s runs something before its fence" % fn)
        if calls and fence > min(calls):
            out.append("%s calls a writing primitive before its fence" % fn)
    options = body(texts.get("FEPOptions.cpp", ""), "CFEPOptions::WriteDefaultOptionsFile") or ""
    if FENCE in options:
        out.append("the options write is fenced: a network process must be able to write the options file (the admission's allow-list)")
    if not re.search(r'PortReplaceUserFile\s*\(\s*"defaultoptions\.bea"\s*,', options):
        out.append("the options write no longer names defaultoptions.bea as the file it replaces")
    known = {k for k in KNOWN}
    for key, offs in sorted(found.items()):
        f, fn, prim = key
        if key in known:
            if len(offs) != KNOWN[key]:
                out.append("%s %s %s: %d calls, %d known" % (f, fn, prim, len(offs), KNOWN[key]))
        elif (f, prim) in OTHER:
            pass
        elif prim == "remove" and f in ("eventmanager.cpp", "OPtrSet.cpp", "OPtrSet.h"):
            pass            # a container's own remove, not a file's
        else:
            out.append("a new call that deletes, moves, copies, creates or replaces by name: %s in %s (%s): read it, then add it to the census" % (prim, fn, f))
    # the literal calls that are not careers name neither a career nor the savegames folder
    for fname, prim in sorted(OTHER):
        code = texts.get(fname, "")
        for m in re.finditer(r"(?<![\w.>:])%s[AW]?\s*\(([^;]*)\)\s*;" % prim, code):
            if re.search(r"\.bes|savegames", m.group(1), re.I):
                out.append("%s: a %s call names a career or the savegames folder" % (fname, prim))
    return out


def main():
    names = sorted(p.name for p in (ROOT / "src").iterdir() if p.suffix in (".cpp", ".h"))
    texts = build.patched_text(names, network=True)             # what a build with the network compiles: the CE_NETWORK_OFF conditionals resolved for it
    port_callers = []
    for p in sorted((ROOT / "port").rglob("*")):
        rel = p.relative_to(ROOT).as_posix()
        if p.suffix in (".cpp", ".h", ".inc") and not rel.startswith(("port/tools/", "port/patches/")) and p.name not in ("save_replace.cpp", "save_replace.h"):
            if len(re.findall(r"PortReplaceUserFile\s*\(", p.read_text(errors="replace"))):
                port_callers.append(rel)
    ok = True

    def check(label, good):
        nonlocal ok
        print(("PASS " if good else "FAIL ") + label)
        ok &= bool(good)

    found = census(texts)
    shown = problems(texts, port_callers)
    for line in shown[:12]:
        print("   " + line)
    check("no career is written, replaced or deleted by a function without the 0185 fence; the atomic replacement has two callers (the career write, fenced, and the options write); "
          "every other deleting, moving, copying or creating call is known (%d calls in %d functions)" % (sum(len(v) for v in found.values()), len(found)), not shown)

    pcm = texts["PCMemoryCard.cpp"]
    write = body(pcm, "CPCMemoryCard::WriteSave")
    mutants = {
        "the career write without its fence": {"PCMemoryCard.cpp": pcm.replace(write, write.replace(FENCE, "false", 1), 1)},
        "a new unfenced caller of the atomic replacement": {"game.cpp": texts["game.cpp"] + "\nvoid CGame::MutantSave()\n{\n\tPortReplaceUserFile(\"savegames\\\\x.bes\", 0, 0, \"career\");\n}\n"},
        "a new deleting function": {"Platform.cpp": texts["Platform.cpp"] + "\nvoid CPlatform::MutantDelete()\n{\n\tDeleteFile(\"x\");\n}\n"},
        "a statement before the career write's fence": {"PCMemoryCard.cpp": pcm.replace(write, write.replace("\n{\n", "\n{\n\tint marker = 0;\n", 1), 1)},
        "a fenced options write": {"FEPOptions.cpp": texts["FEPOptions.cpp"].replace("PortReplaceUserFile(", "if (" + FENCE + ") return 0; PortReplaceUserFile(", 1)},
        "a career delete without its fence": {"PCMemoryCard.cpp": pcm.replace(body(pcm, "CPCMemoryCard::DeleteSave"), body(pcm, "CPCMemoryCard::DeleteSave").replace(FENCE, "false", 1), 1)},
    }
    # the fence moved behind the first write: its block is taken out of the career write and put after the line of the atomic call
    # (the text is the network flavour's: the CE_NETWORK_OFF directives are resolved away, the fence is its comment, the if and the statement under it)
    block = re.search(r"(?:[^\n]*//[^\n]*\n)?\tif \(CE_BootRequested\(\) \|\| CE_LockstepRequested\(\)\)\n(?:\t\t[^\n]*\n)+", write)
    check("the mutant 'the fence after the atomic call' can be made from the patched career write", bool(block))
    if block:
        moved = write.replace(block.group(0), "", 1)
        call = re.search(r"[^\n]*PortReplaceUserFile\([^\n]*\n", moved)
        moved = moved.replace(call.group(0), call.group(0) + block.group(0), 1)
        mutants["the fence after the atomic call"] = {"PCMemoryCard.cpp": pcm.replace(write, moved, 1)}
    for label, change in mutants.items():
        mutated = dict(texts)
        mutated.update(change)
        check("mutant: %s is caught" % label, mutated != texts and bool(problems(mutated, port_callers)))
    print("career writers census: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
