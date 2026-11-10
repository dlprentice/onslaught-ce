#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A lockstep tick must not draw from a shared random stream because a particle exists.

A particle effect's pool is aged by each peer's render loop, so whether CParticleManager::CreateEffect hands back a particle differs
between peers (a peer that was frozen keeps stale particles, the one that waited expired its own). Code that draws a value only
`if (link.mEffect)`, `if (particle)` or `if (link.Full())` therefore draws a different number of values on the two peers, and the
shared streams, and every hash after them, diverge. Such a draw must sit under a CE_LockstepCrtHold (patches 0129 and 0152), which
keeps it on this peer's own presentation copies of the streams.

The suite reads the patched game source as a build with the network sees it and reports every draw that depends on a particle's
existence and has no hold in scope: a draw (rand, Random, FloatRandom, RandomScaled, a rubble pick) inside a block whose condition
names an effect link or a particle pointer, in the else side of such a condition, after an early exit on one, or in the statement an
`if` of that kind guards. The particle system's own sources (ParticleManager, ParticleDescriptor) are exempt: all they do runs inside
the held creation or the render loop. It also requires the rubble pick (CRTBuilding::Unknown10) to hold in its own body, and checks
the analyser against small programs and against mutants of the real sources that lose a hold. It reads names, not types: a particle
held in a variable called something other than mEffect, mParticle, particle, mp or np needs its name added to COND_WORDS.
No game data, device or network is used.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402

EXEMPT = ("ParticleManager.cpp", "ParticleDescriptor.cpp")
DRAW = re.compile(r"(?:rand|srand)\s*\(|Random\s*\(|FloatRandom\s*\(|RandomScaled\s*\(|Unknown10\s*\(")
HOLD = re.compile(r"\bCE_LockstepCrtHold\s+\w+\s*;")
COND_WORDS = re.compile(r"\bmEffect\b|\bFull\s*\(|\bmParticle\b|\bparticle\b|\bmp\b|\bnp\b")
IF_HEAD = re.compile(r"^(?:else\s+)?(?:if|while)\s*\(")
EARLY_EXIT = re.compile(r"^if\s*\((?P<cond>.*)\)\s*(?:return|continue|break|goto)\b", re.S)


def blank(text):
    """The text with comments, string and character literals and preprocessor lines replaced by spaces (newlines kept), so nothing in
    them counts as a brace, a statement or a draw."""
    out, i, n = [], 0, len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "//":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif two == "/*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif text[i] in "\"'":
            q, j = text[i], i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        else:
            out.append(text[i])
            i += 1
    lines = "".join(out).split("\n")
    k = 0
    while k < len(lines):
        if lines[k].lstrip().startswith("#"):
            while k < len(lines):
                more = lines[k].rstrip().endswith("\\")
                lines[k] = " " * len(lines[k])
                k += 1
                if not more:
                    break
        else:
            k += 1
    return "\n".join(lines)


def particle_condition(header):
    return bool(IF_HEAD.match(header)) and bool(COND_WORDS.search(header))


def analyse(text):
    """[(line, token, why)]: draws that depend on a particle's existence and have no CE_LockstepCrtHold in scope."""
    code = blank(text)
    found = []
    stack = []          # one entry per open brace: its header, whether it is a particle branch, whether a hold is in scope, whether an
                        # early exit on a particle came before this point of the block
    chain = {}          # depth -> the block that closed last there was a particle branch (an else that follows is its other side)
    stmt = paren = 0    # where the current statement starts; open parentheses
    i, n = 0, len(code)
    while i < n:
        c = code[i]
        if c == "(":
            paren += 1
        elif c == ")":
            paren = max(0, paren - 1)
        elif paren == 0 and c == "{":
            header = " ".join(code[stmt:i].split())
            cond = particle_condition(header) or (header.startswith("else") and chain.get(len(stack), False))
            stack.append({"header": header, "cond": cond, "hold": False, "tainted": False})
            stmt = i + 1
        elif paren == 0 and c == "}":
            if stack:
                closed = stack.pop()
                chain[len(stack)] = closed["cond"]
                for depth in [d for d in chain if d > len(stack)]:
                    del chain[depth]
            stmt = i + 1
        elif paren == 0 and c == ";":
            pending = " ".join(code[stmt:i].split())
            early = EARLY_EXIT.match(pending)
            if early and COND_WORDS.search(early.group("cond")) and stack:
                stack[-1]["tainted"] = True
            if HOLD.search(code[stmt:i + 1]) and stack:
                stack[-1]["hold"] = True
            stmt = i + 1
        elif (c.isalpha() or c == "_") and (i == 0 or not (code[i - 1].isalnum() or code[i - 1] == "_")):
            word_end = re.compile(r"[A-Za-z_]\w*").match(code, i).end()
            token = DRAW.match(code, i)
            if token:
                if not any(b["hold"] for b in stack):
                    prefix = " ".join(code[stmt:i].split())
                    why = None
                    for b in stack:
                        if b["cond"]:
                            why = "in a branch on a particle: " + b["header"][:70]
                            break
                        if b["tainted"]:
                            why = "after an early exit on a particle in " + (b["header"][:50] or "the function")
                            break
                    if why is None and particle_condition(prefix):
                        why = "in the statement an if on a particle guards: " + prefix[:70]
                    if why:
                        found.append((code.count("\n", 0, i) + 1, re.sub(r"\s*\($", "", token.group(0)), why))
                i = token.end()
                continue
            i = word_end
            continue
        i += 1
    return found


def check(good, name, results):
    print(("PASS " if good else "FAIL ") + name)
    results.append(bool(good))


def analyser_controls(results):
    """The analyser on small programs: each rule fires where it should and stays quiet where a hold is in scope."""
    def count(code):
        return len(analyse(code))
    check(count("void f(){ CParticleEffectLink link; if (link.mEffect) { CE_LockstepCrtHold hold; int a = rand(); } }") == 0,
          "a draw under a hold in the particle branch is accepted", results)
    check(count("void f(){ CParticleEffectLink link; if (link.mEffect) { int a = rand(); } }") == 1,
          "a draw in the particle branch without a hold is reported", results)
    check(count("void f(){ if (mp) { x = GAME.Random(); } }") == 1, "GAME.Random in a branch on a particle pointer is reported", results)
    check(count("void f(){ if (link.Full()) { x = GAME.FloatRandom(); } }") == 1, "FloatRandom after Full() is reported", results)
    check(count("void f(){ if (a) { x = rand(); } }") == 0, "a draw in an ordinary branch is not reported", results)
    check(count("void f(){ if (particle) { y = 1; } else { x = rand(); } }") == 1, "a draw in the else of a particle test is reported", results)
    check(count("void f(){ if (particle) { y = 1; } else if (b) { x = rand(); } }") == 1, "a draw in an else-if of a particle test is reported", results)
    check(count("void f(){ if (particle) { y = 1; } if (b) { z = 2; } else { x = rand(); } }") == 0,
          "an else of an unrelated test after a particle block is not reported", results)
    check(count("void f(){ if (!particle) return; x = rand(); }") == 1, "a draw after an early exit on a particle is reported", results)
    check(count("void f(){ if (np) x = rand(); }") == 1, "a draw in the statement an if on a particle guards is reported", results)
    check(count("void f(){ CE_LockstepCrtHold h; if (np) x = rand(); }") == 0, "a function-wide hold covers a braceless guarded draw", results)
    check(count('void f(){ if (link.mEffect) { /* rand() */ s = "rand()"; } }') == 0, "a draw named in a comment or a string is not a draw", results)
    check(count("void f(){ if (link.mEffect) { if (b) { CE_LockstepCrtHold h; } x = rand(); } }") == 1,
          "a hold in a closed inner block does not cover the outer block", results)
    check(count("void f(){ for (i = 0; i < 3; i++) { if (particle) { x = rand(); } } }") == 1, "a particle branch inside a loop is found", results)
    check(count("void f(){ if (link.mEffect) { m = rt->Unknown10(); } }") == 1, "a rubble pick in the particle branch is reported", results)
    check(count("#define X(a) if (a.mEffect) { rand(); }\nvoid f(){ g(); }") == 0, "a preprocessor line is not code", results)
    check(count("void f(){ if (link.mEffect) { int randomise = 1; int operand = 2; } }") == 0, "a longer name that contains a draw's name is not a draw", results)


def sources():
    """{name: text}: every game source that handles particles (outside the particle system), patched as a build with the network sees it."""
    names = []
    for path in sorted((ROOT / "src").glob("*.cpp")):
        if path.name in EXEMPT:
            continue
        text = path.read_text(encoding="latin-1")
        if re.search(r"\bPARTICLE_MANAGER\b|\bCParticleEffectLink\b|\bCParticle\b|\bCParticleMesh\b", text) or path.name == "RTBuilding.cpp":
            names.append(path.name)
    return build.patched_text(names, network=True)


def function_span(text, pattern):
    """(start, end) of the function whose definition line matches pattern: to the closing brace at the start of a line."""
    m = re.search(pattern, text, re.M)
    if not m:
        return None
    end = text.find("\n}\n", m.start())
    return (m.start(), end) if end > m.start() else None


def main():
    results = []
    analyser_controls(results)
    texts = sources()
    check(len(texts) >= 25, "the patched game sources that handle particles were read (%d files)" % len(texts), results)
    findings = {name: analyse(text) for name, text in texts.items()}
    for name, rows in sorted(findings.items()):
        for line, token, why in rows:
            print("  %s:%d: %s %s, without a hold" % (name, line, token, why))
    check(not any(findings.values()), "no draw that depends on a particle's existence lacks a hold (%d sources)" % len(texts), results)

    # the rubble pick holds in its own body, before it draws
    span = function_span(texts.get("RTBuilding.cpp", ""), r"^CMESH \*CRTBuilding::Unknown10\(\)")
    body = texts["RTBuilding.cpp"][span[0]:span[1]] if span else ""
    check(bool(body) and "CE_LockstepCrtHold" in body and "rand()" in body and body.index("CE_LockstepCrtHold") < body.index("rand()"),
          "CRTBuilding::Unknown10 holds before it draws its level of detail", results)
    check(bool(body) and not HOLD.search(HOLD.sub("", body)) and bool(HOLD.search(body)),
          "mutant: that hold taken out leaves the body without one", results)

    # mutants of the real sources: each hold the fix relies on, taken out, must be reported
    mutants = (
        ("DestroyableSegment.cpp", r"^void CDestroyableSegment::SpawnRubble\(\)", "the rubble branch's hold in SpawnRubble"),
        ("Mech.cpp", r"^void CMech::Unknown71\(", "the debris branch's hold in CMech::Unknown71"),
        ("DestroyableSegment.cpp", r"^void CDestroyableExtraUseMeshSegment::SpawnRubble\(\)", "the spin hold in the extra-use mesh segment"),
    )
    for name, pattern, label in mutants:
        text = texts.get(name, "")
        span = function_span(text, pattern)
        part = text[span[0]:span[1]] if span else ""
        mutated = text[:span[0]] + HOLD.sub("/* hold removed */", part) + text[span[1]:] if span else text
        check(bool(part) and HOLD.search(part) is not None and len(analyse(mutated)) > len(findings.get(name, [])),
              "mutant: %s taken out is reported" % label, results)

    # the bug itself: the queue without patch 0152 is reported at the rubble pick and at the mech debris draws, and nowhere else
    unfixed = build.patched_text(list(texts), skip=("0152-lockstep-debris-draws.patch",), network=True)
    rows = {(name, row[1]) for name, text in unfixed.items() for row in analyse(text)}
    check(rows == {("DestroyableSegment.cpp", "Unknown10"), ("Mech.cpp", "rand")},
          "control: the queue without patch 0152 is reported at the rubble pick and the mech debris draws only", results)

    ok = all(results)
    print("lockstep particle-draw controls: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
