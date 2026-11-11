#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""No patch puts a new statement where it takes the body of an unbraced if, else, for or while.

Patch 0208 once added a counter call between an unchanged `if (...)` and its unchanged one-line body. The call became
the condition's body and the old body ran every time. The compiler gives no error, and the game's own warnings are
off, so this scans every hunk of port/patches/*.patch for the shape. The condition line (one or several lines) and the
old body are both unchanged context, and the first code line after the condition in the patched text is an added
statement: not an added `{`, not an added nested condition. An added preprocessor conditional is read both ways
(its first branch, and its `#else` branch or nothing), so a guarded alternative body is not reported; a guarded
statement inserted there is. Two nearby shapes are also reported, by indentation: a one-statement body that the patch
replaced by two or more statements indented alike without braces, and an added statement straight after an unchanged
one-statement body, indented as that body is (it looks guarded and is not). Known findings owned elsewhere are listed in
KNOWN with their reason; one that is no longer found fails as stale.

    python3 -I port/tools/patch_control_body_tests.py            # the queue, then the controls
    python3 -I port/tools/patch_control_body_tests.py --scan DIR # report the patches in DIR; exit 1 on a finding

A finding names the patch, the file and the hunk line; no source text is printed. No game data, build or device is used.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATCHES = ROOT / "port/patches"

CONTROL = re.compile(r"^\s*(?:\}\s*)?(?:(?:else\s+)?if|while|for)\s*\(")
ELSE = re.compile(r"^\s*(?:\}\s*)?else\s*$")
PREPROCESSOR = re.compile(r"^\s*#\s*(\w+)")


def code(text):
    """The line without comments and trailing space (strings are not parsed: a control line rarely holds one)."""
    text = re.sub(r"/\*.*?\*/", " ", text)
    return re.sub(r"//.*$", "", text).rstrip()


def is_code(text):
    """Not blank, not a preprocessor line, not a comment line (a block comment's middle starts with a lone '*')."""
    t = text.strip()
    return bool(t) and not t.startswith(("#", "//", "/*")) and not re.match(r"\*(?:$|[\s/])", t)


def indent(text):
    expanded = text.expandtabs(4)
    return len(expanded) - len(expanded.lstrip())


def hunks(text):
    """Yields (file, first line number of the hunk in the patch, [(mark, text, line number in the patch)])."""
    name, body, start = None, None, 0
    for number, line in enumerate(text.split("\n"), 1):
        if line.startswith("+++ "):
            if body:
                yield name, start, body
            name, body = line[4:].split("\t")[0].removeprefix("b/"), None
        elif line.startswith("--- "):
            if body:
                yield name, start, body
            body = None
        elif line.startswith("@@"):
            if body:
                yield name, start, body
            body, start = [], number
        elif body is not None and line[:1] in (" ", "+", "-"):
            body.append((line[0], line[1:], number))
        elif body is not None and line.startswith("\\"):
            continue
    if body:
        yield name, start, body


def views(body):
    """The new text of a hunk as two lists of hunk indices: added preprocessor conditionals read as their first branch,
    and as their #else branch (or as nothing). Unchanged preprocessor lines are kept as they are."""
    result = []
    for take_first in (True, False):
        kept, stack = [], []   # one entry per open ADDED conditional: whether its current branch is kept
        for i, (mark, text, _) in enumerate(body):
            if mark == "-":
                continue
            directive = PREPROCESSOR.match(text)
            word = directive[1] if directive else None
            if mark == "+" and word in ("if", "ifdef", "ifndef"):
                stack.append(take_first)
                continue
            if mark == "+" and word in ("elif", "else") and stack:
                stack[-1] = (not take_first) and word == "else"
                continue
            if mark == "+" and word == "endif" and stack:
                stack.pop()
                continue
            if all(stack):
                kept.append(i)
        result.append(kept)
    return result


def old_view(body):
    return [i for i, (mark, _, _) in enumerate(body) if mark != "+"]


def header_end(body, order, at):
    """The position in `order` of the last line of the control header that starts at order[at], or None when that
    header ends in a brace or a semicolon (then it guards a block, or is a do-while's tail or a one-line statement)."""
    text = code(body[order[at]][1])
    end = at
    while text.count("(") > text.count(")") and end + 1 < len(order):
        end += 1
        text += " " + code(body[order[end]][1])
    if text.endswith(("{", ";", "}")) or text.count("(") != text.count(")"):
        return None
    return end


def next_code(body, order, at):
    at += 1
    while at < len(order) and not is_code(body[order[at]][1]):
        at += 1
    return at if at < len(order) else None


def statement_end(body, order, at):
    depth = 0
    for k in range(at, len(order)):
        text = code(body[order[k]][1])
        depth += text.count("(") - text.count(")")
        if depth <= 0 and text.endswith(";"):
            return k
    return None


def is_control(text):
    return bool(CONTROL.match(text) or ELSE.match(code(text)))


BETWEEN = "an added statement takes the body of an unchanged control line"
SEVERAL = "a one-statement body was replaced by several statements without braces"
AFTER = "an added statement after an unchanged body is indented as part of it"


def plain_statement(text):
    t = text.strip()
    return not t.startswith(("{", "}")) and not is_control(text)


def findings_of_hunk(body):
    """[(line in the patch of the control line, reason)] for one hunk."""
    found = set()
    old = old_view(body)
    old_pos = {index: pos for pos, index in enumerate(old)}
    for order in views(body):
        for at, index in enumerate(order):
            mark, text, _ = body[index]
            if mark != " " or not is_control(text):
                continue
            end = at if ELSE.match(code(text)) else header_end(body, order, at)
            if end is None or any(body[order[k]][0] != " " for k in range(at, end + 1)):
                continue
            first = next_code(body, order, end)
            if first is None or not plain_statement(body[order[first]][1]):
                continue
            last = statement_end(body, order, first)
            after = next_code(body, order, last) if last is not None else None
            inner = indent(body[order[first]][1])
            follows = (after is not None and body[order[after]][0] == "+" and plain_statement(body[order[after]][1])
                       and inner > indent(text) and indent(body[order[after]][1]) == inner)
            if body[order[first]][0] == " ":
                # The old body is unchanged: a statement added straight after it must not look like part of it.
                if follows:
                    found.add((body[index][2], AFTER))
                continue
            # The first statement after the control line is added. The old body: the first code line after the
            # same control line in the old text.
            old_body = next_code(body, old, old_pos[order[end]])
            if old_body is None:
                continue
            old_index = old[old_body]
            if body[old_index][0] == " " and old_index in order[first:]:
                found.add((body[index][2], BETWEEN))
            elif body[old_index][0] == "-" and follows:
                found.add((body[index][2], SEVERAL))
    return sorted(found)


# Findings that are known and owned elsewhere, by (patch, file, reason), each with why it stays. A fixed one fails as stale.
KNOWN = {}


def scan(folder):
    """[(patch name, file, line in the patch, reason)] for every patch in folder."""
    out = []
    for patch in sorted(Path(folder).glob("*.patch")):
        for name, _, body in hunks(patch.read_text(encoding="latin-1")):
            for line, reason in findings_of_hunk(body):
                out.append((patch.name, name, line, reason))
    return out


def hunk(text):
    """A hunk body from authored lines (mark and text)."""
    return [(line[0], line[1:], n) for n, line in enumerate(text.strip("\n").split("\n"), 1)]


# Authored controls (made-up code): each must give the expected number of findings.
CONTROLS = [
    ("an added statement between a condition and its body", 1, """
 void f()
 {
 \tif (a)
+\t\tcount();
 \t\tdraw();
 \tdone();
"""),
    ("the same, with the condition on two lines", 1, """
 \tif (a &&
 \t\tb)
+\t\tcount();
 \t\tdraw();
"""),
    ("the same after else", 1, """
 \telse
+\t\tcount();
 \t\tdraw();
"""),
    ("the same before a braced body on its own line", 1, """
 \tfor (i = 0; i < n; i++)
+\t\tcount();
 \t{
 \t\tdraw(i);
 \t}
"""),
    ("a guarded statement inserted there", 1, """
 \twhile (a)
+#ifndef OPTION
+\t\tcount();
+#endif
 \t\tdraw();
"""),
    ("a body replaced by two statements", 1, """
 \tif (a)
-\t\tdraw();
+\t\tcount();
+\t\tdraw();
 \tdone();
"""),
    ("the counter and the body braced together", 0, """
 \tif (a)
+\t{
+\t\tcount();
 \t\tdraw();
+\t}
 \tdone();
"""),
    ("a nested condition added before the body", 0, """
 \tif (a)
+\t\tif (b)
 \t\t\tdraw();
"""),
    ("a guarded nested condition", 0, """
 \tif (a)
+#ifdef OPTION
+\t\tif (b)
+#endif
 \t\tdraw();
"""),
    ("a guarded alternative body", 0, """
 \tif (a)
+#ifdef OPTION
+\t\tdraw_other();
+#else
 \t\tdraw();
+#endif
"""),
    ("a changed body", 0, """
 \tif (a)
-\t\tdraw(1);
+\t\tdraw(2);
 \tdone();
"""),
    ("a statement added after the body", 0, """
 \tif (a)
 \t\tdraw();
+\tcount();
"""),
    ("a statement added after the body, indented as the body", 1, """
 \tif (a)
 \t\tdraw();
+\t\tcount();
 \tdone();
"""),
    ("a body replaced by a statement, then a statement after it", 0, """
 \tif (a)
-\t\tdraw(1);
+\t\tdraw(2);
+\tcount();
 \tdone();
"""),
    ("a body that starts with a dereference, then a statement after it", 0, """
 \tfor (n = 4; n; n--)
 \t\t*dst++ = *src++;
 
+\tcount();
"""),
    ("a statement added after a condition with a block", 0, """
 \tif (a) {
+\t\tcount();
 \t\tdraw();
 \t}
"""),
    ("a do-while tail", 0, """
 \t} while (a);
+\tcount();
 \tdraw();
"""),
    ("a guarded rewrite of the condition", 0, """
+#ifdef OPTION
 \tif (a)
+#else
+\tif (a || b)
+#endif
 \t\tdraw();
"""),
]


def main():
    if "--scan" in sys.argv:
        found = scan(sys.argv[sys.argv.index("--scan") + 1])
        for patch, name, line, reason in found:
            print(f"FINDING {patch} {name} patch line {line}: {reason}")
        print(f"{len(found)} findings")
        return 1 if found else 0
    failures = 0
    found = scan(PATCHES)
    seen = set()
    for patch, name, line, reason in found:
        if (patch, name, reason) in KNOWN:
            seen.add((patch, name, reason))
            print(f"KNOWN {patch} {name} patch line {line}: {reason} ({KNOWN[(patch, name, reason)]})")
            continue
        print(f"FAIL {patch} {name} patch line {line}: {reason}")
        failures += 1
    for key in sorted(set(KNOWN) - seen):
        print(f"FAIL stale exception: {' '.join(key)} is no longer found; remove it from KNOWN")
        failures += 1
    patches = sorted(PATCHES.glob("*.patch"))
    count = sum(1 for p in patches for _ in hunks(p.read_text(encoding="latin-1")))
    if not patches or count < 100:
        print(f"FAIL the queue looks empty ({len(patches)} patches, {count} hunks)")
        failures += 1
    elif not failures:
        print(f"PASS {len(patches)} patches, {count} hunks: no added statement takes a body or hides behind one")
    for label, expected, text in CONTROLS:
        got = len(findings_of_hunk(hunk(text)))
        ok = got == expected
        failures += not ok
        print(("PASS " if ok else "FAIL ") + f"control: {label} ({got} findings, {expected} expected)")
    # The defects this check exists for: patch 0208 with the braces round its tree imposter counter taken away, and patch
    # 0206 with the braces round the commander map and editor overlay calls taken away (each as it once was).
    for name, marker, label in (("0208-ce-full-models.patch", "CE_MODEL_TREE_IMPOSTOR", "the tree imposter counter"),
                                ("0206-native-editor-lifecycle.patch", "PortCeEditorRender();", "the editor overlay call")):
        lines = (PATCHES / name).read_text(encoding="latin-1").split("\n")
        at = [i for i, line in enumerate(lines) if line.startswith("+") and marker in line]
        def added(i, text):
            return 0 <= i < len(lines) and lines[i][:1] == "+" and lines[i][1:].strip() == text
        opening = next((i for i in range(at[0] - 1, at[0] - 4, -1) if added(i, "{")), None) if len(at) == 1 else None
        closing = next((i for i in range(at[0] + 1, at[0] + 4) if added(i, "}")), None) if len(at) == 1 else None
        if opening is not None and closing is not None:
            mutant = lines[:opening] + lines[opening + 1:closing] + lines[closing + 1:]
            got = [f for _, _, body in hunks("\n".join(mutant)) for f in findings_of_hunk(body)]
            ok = len(got) == 1
            print(("PASS " if ok else "FAIL ") + f"control: patch {name[:4]} without the braces round {label} "
                  f"({len(got)} findings, 1 expected)")
        else:
            ok = False
            print(f"FAIL control: patch {name[:4]}'s {label} is not inside an added braced body")
        failures += not ok
    print("patch control-body check: " + ("OK" if not failures else f"{failures} FAILED"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
