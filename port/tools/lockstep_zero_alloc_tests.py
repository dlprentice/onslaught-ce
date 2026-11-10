#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A lockstep session's allocations start as zeros (patch 0156), and nothing else in the game allocates around the memory manager.

Measured on the lockstep build before this patch: two peers of one build whose only difference was glibc's MALLOC_PERTURB_ (new blocks zeroed on one,
0xfe-filled on the other) went out of step at tick 0; the game reads memory it never wrote. A block of the game's own heap holds what
its previous occupant left, which differs between two peers (their draw loops allocate and free on their own schedules), and a tiny
block starts with the free-list pointer, an address. While a lockstep session is requested, CDXMemoryManager::Alloc (the one exit of
every game allocation) and the grown part of CMemoryHeap::ReAlloc / ReallocTiny therefore hand out zeros. Solo play keeps the original's
behaviour: the zeroing is conditional on CE_LockstepRequested(), and a public build (CE_NETWORK_OFF) does not contain it.

The suite reads the patched sources as a build with the network sees them and as a public build sees them. It requires:
  - the zeroing after the allocation and before the return, conditional on CE_LockstepRequested(), in CDXMemoryManager::Alloc, of the whole payload the
    heap hands out (a tiny cell in full, even for a request of none; else the request rounded up to 16 or the block's own size when that is more: the slack a later
    reallocation copies);
  - the same for the grown part of CMemoryHeap::ReAlloc and CMemoryHeap::ReallocTiny;
  - none of it in a public build's text;
  - the session settled before the game's memory manager exists (WinMain's boot runs before MEM_MANAGER.Init, and Alloc asserts mInit), so the zeroing covers the manager's first
    allocation as well;
  - the choke point to be complete: the only allocations in game code outside the manager are the malloc that carves the heap's region
    and the shader file's HEAP_ZERO_MEMORY buffer, and CMemoryHeap::Alloc is called from exactly the places this patch knows (the heap's
    region setup, its own retry after a cleanup, ReallocTiny, ReAlloc) besides the manager;
and runs mutants of the text (no condition, no zeroing, zeroing before the allocation, the requested size only, the payload left out, no rounding, no tiny cell, each grown part)
to show each requirement can fail.
No game data, device or network is used.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import build  # noqa: E402
from lockstep_particle_draw_tests import blank  # noqa: E402

FILES = ("DXMemoryManager.cpp", "MemoryManager.cpp")


def check(good, name, results):
    print(("PASS " if good else "FAIL ") + name)
    results.append(bool(good))


def function_body(text, pattern):
    """The text of the function whose definition line matches pattern: to the closing brace at the start of a line."""
    m = re.search(pattern, text, re.M)
    if not m:
        return ""
    end = text.find("\n}\n", m.start())
    return text[m.start():end] if end > m.start() else ""


ALLOC_DEF = r"^void \* CDXMemoryManager::Alloc\("
# the guarded block in CDXMemoryManager::Alloc: the payload (request rounded to 16, or the block's size when larger and it is not a tiny cell) cleared under the session condition
ZERO_BLOCK = re.compile(
    r"#ifndef CE_NETWORK_OFF\n\tif \(data && CE_LockstepRequested\(\)\)\n\t\{\n"
    r"(?:\t\t//[^\n]*\n)*"
    r"\t\tUINT\tlClear = \(aSize \+ 0xf\) & 0xfffffff0;\n"
    r"\t\tif \(mDefaultHeap\.IsTiny\(data\) \|\| mThingHeap\.IsTiny\(data\)\)\n"
    r"\t\t\tlClear = TINY_BLOCK;\n"
    r"\t\telse if \(CMemoryBlock::GetBlock\(data\)->GetSize\(\) > lClear\)\n"
    r"\t\t\tlClear = CMemoryBlock::GetBlock\(data\)->GetSize\(\);\n"
    r"\t\tmemset\(data, 0, lClear\);[^\n]*\n\t\}\n#endif\n")
REALLOC_DEF = r"^void \* CMemoryHeap::ReAlloc\("
TINY_DEF = r"^bool CMemoryHeap::ReallocTiny\("


def alloc_ok(text):
    body = function_body(text, ALLOC_DEF)
    call = body.find("mTypeHeap[ aType ]->Alloc(")
    zero = ZERO_BLOCK.search(body)
    ret = body.rfind("return data;")
    return call >= 0 and zero is not None and call < zero.start() < ret


def realloc_ok(text):
    body = function_body(text, REALLOC_DEF)
    copy = body.find("memcpy( lpMem, apBlock->GetMem(), lSize );")
    zero = re.search(r"#ifndef CE_NETWORK_OFF\s*\n\s*if \(lSize < \(SINT\)aSize && CE_LockstepRequested\(\)\)\s*\n\s*"
                     r"memset\(\(char \*\)lpMem \+ lSize, 0, aSize - lSize\);[^\n]*\n#endif", body)
    return copy >= 0 and zero is not None and copy < zero.start() < body.find("Free( apBlock )")


def tiny_ok(text):
    body = function_body(text, TINY_DEF)
    move = body.find("memmove(data, temp, copysize);")
    zero = re.search(r"#ifndef CE_NETWORK_OFF\s*\n\s*if \(copysize < newsize && CE_LockstepRequested\(\)\)\s*\n\s*"
                     r"memset\(\(char \*\)data \+ copysize, 0, newsize - copysize\);[^\n]*\n#endif", body)
    return move >= 0 and zero is not None and move < zero.start() < body.find("*retval = data;")


def mutate(text, pattern, replacement):
    mutated, n = re.subn(pattern, replacement, text, count=1, flags=re.S)
    return mutated if n == 1 else None


def main():
    results = []
    guarded = build.patched_text(FILES)	# the patched text with the CE_NETWORK_OFF conditionals left in
    public = build.patched_text(FILES, network=False)
    dx, mm = guarded["DXMemoryManager.cpp"], guarded["MemoryManager.cpp"]

    check(alloc_ok(dx), "CDXMemoryManager::Alloc zeroes the block after the allocation and before the return, under CE_LockstepRequested()", results)
    check(realloc_ok(mm), "CMemoryHeap::ReAlloc zeroes the grown part under CE_LockstepRequested()", results)
    check(tiny_ok(mm), "CMemoryHeap::ReallocTiny zeroes the grown part under CE_LockstepRequested()", results)
    check(all("CE_LockstepRequested" not in public[n] and "ce_network_lockstep.h" not in public[n] for n in FILES),
          "a public build's text of both files has no trace of it", results)

    # mutants: each requirement, lost, must be seen
    no_condition = mutate(dx, r"if \(data && CE_LockstepRequested\(\)\)", "if (data)")
    check(no_condition is not None and not alloc_ok(no_condition), "mutant: zeroing in every build, solo play included, is caught", results)
    block = ZERO_BLOCK.search(dx)
    no_zero = dx.replace(block.group(0), "", 1) if block else None
    check(no_zero is not None and not alloc_ok(no_zero), "mutant: no zeroing is caught", results)
    zero_early = None
    if block:
        zero_early = dx.replace(block.group(0), "", 1).replace("\tdata = mTypeHeap[ aType ]->Alloc(", block.group(0) + "\tdata = mTypeHeap[ aType ]->Alloc(", 1)
    check(zero_early is not None and not alloc_ok(zero_early), "mutant: zeroing before the allocation is caught", results)
    requested_only = mutate(dx, r"memset\(data, 0, lClear\);", "memset(data, 0, aSize);")
    check(requested_only is not None and not alloc_ok(requested_only), "mutant: clearing the requested size only (the heap rounds up and hands out slack) is caught", results)
    no_payload = mutate(dx, r"\t\telse if \(CMemoryBlock::GetBlock\(data\)->GetSize\(\) > lClear\)\n[^\n]*\n", "")
    check(no_payload is not None and not alloc_ok(no_payload), "mutant: leaving out the block's own size (the slack of a block that was not split) is caught", results)
    no_rounding = mutate(dx, r"lClear = \(aSize \+ 0xf\) & 0xfffffff0;", "lClear = aSize;")
    check(no_rounding is not None and not alloc_ok(no_rounding), "mutant: leaving out the rounding to 16 is caught", results)
    no_tiny_cell = mutate(dx, r"\t\tif \(mDefaultHeap.IsTiny\(data\) \|\| mThingHeap\.IsTiny\(data\)\)\n[^\n]*\n", "")
    check(no_tiny_cell is not None and not alloc_ok(no_tiny_cell), "mutant: leaving out the tiny cell (a request of none still gets 16 bytes of free-list pointer and stale data) is caught", results)
    no_realloc = mutate(mm, r"\t\tif \(lSize < \(SINT\)aSize && CE_LockstepRequested\(\)\)\s*\n\s*memset\([^\n]*\n", "")
    check(no_realloc is not None and not realloc_ok(no_realloc), "mutant: ReAlloc's grown part left as it was is caught", results)
    no_tiny = mutate(mm, r"\t\tif \(copysize < newsize && CE_LockstepRequested\(\)\)\s*\n\s*memset\([^\n]*\n", "")
    check(no_tiny is not None and not tiny_ok(no_tiny), "mutant: ReallocTiny's grown part left as it was is caught", results)

    # the session is settled before the game's memory manager exists: WinMain runs the boot (CE_BootStart, whose admission is what CE_LockstepRequested reports in a shipped build)
    # before MEM_MANAGER.Init, and Alloc asserts the manager was initialised, so the zeroing sees the answer from the manager's very first allocation (measured on a frozen build: a
    # breakpoint on CE_BootStart and one on CDXMemoryManager::Alloc, the first stops at the boot with the allocator's count at zero; the first allocation is WinMain's options buffer)
    shell = build.patched_text(("ltshell.cpp",))["ltshell.cpp"]
    boot, manager = shell.find("if (!CE_BootStart()) return 1;"), shell.find("MEM_MANAGER.Init(PLATFORM.GetMemorySize());")
    check(0 <= boot < manager, "WinMain runs the boot (CE_BootStart) before the memory manager's Init, so no allocation of the manager precedes the admission", results)
    check("ASSERT(mInit);" in function_body(dx, ALLOC_DEF)[:200], "CDXMemoryManager::Alloc asserts that the manager was initialised (nothing allocates through it before Init)", results)
    swapped = shell.replace("if (!CE_BootStart()) return 1;", "", 1).replace("MEM_MANAGER.Init(PLATFORM.GetMemorySize());", "MEM_MANAGER.Init(PLATFORM.GetMemorySize());\n\tif (!CE_BootStart()) return 1;", 1)
    check(not (0 <= swapped.find("if (!CE_BootStart()) return 1;") < swapped.find("MEM_MANAGER.Init(PLATFORM.GetMemorySize());")),
          "mutant: the boot moved after the memory manager's Init is caught", results)
    check("ASSERT(mInit);" not in function_body(dx.replace("ASSERT(mInit);", "", 1), ALLOC_DEF)[:200], "mutant: an Alloc without the initialisation assertion is caught", results)

    # the choke point is complete
    mallocs = []
    for path in sorted((ROOT / "src").glob("*.cpp")):
        code = blank(path.read_text(encoding="latin-1"))
        for match in re.finditer(r"(?<![\w.>:])(?:malloc|calloc|realloc|_aligned_malloc|HeapAlloc|VirtualAlloc|GlobalAlloc|LocalAlloc)\s*\(", code):
            mallocs.append((path.name, code.count("\n", 0, match.start()) + 1, match.group(0)))
    # MemoryManager.cpp's malloc carves the heap's region; VertexShader.cpp reads a shader file into a HEAP_ZERO_MEMORY buffer (render only)
    check(sorted((name, call[:-1]) for name, _, call in mallocs) == [("MemoryManager.cpp", "malloc"), ("VertexShader.cpp", "HeapAlloc")],
          "the only allocations outside the memory manager in game code are the malloc that carves the heap's region and the shader file's zeroed buffer (%s)" %
          ", ".join("%s:%d %s" % hit for hit in mallocs), results)
    code = blank(mm)
    sites = []
    for match in re.finditer(r"(?<![\w:])Alloc\s*\(", code):
        line_start = code.rfind("\n", 0, match.start()) + 1
        if "CMemoryHeap::Alloc" in code[line_start:match.start() + 20]:
            continue
        defn = list(re.finditer(r"^[A-Za-z_][^\n]*\bCMemoryHeap::(\w+)\s*\(", code[:match.start()], re.M))
        sites.append(defn[-1].group(1) if defn else "?")
    check(sorted(sites) == ["Alloc", "Init", "ReAlloc", "ReallocTiny"],
          "CMemoryHeap::Alloc is called from the heap's own region setup (Init), its retry after a cleanup (Alloc), ReallocTiny and ReAlloc only (%s)" % ", ".join(sorted(sites)), results)

    # a test pair that perturbs the allocator (the experiment this patch came from) or poisons the stack provokes crashes in the retail code on purpose: its processes are
    # non-dumpable (execve resets the flag, so the game sets it itself), in a test-automation build only
    lockstep = (ROOT / "port/game/ce_network_lockstep.cpp").read_text()
    dumpable = re.compile(r'#ifndef _WIN32\n(?:\t\t//[^\n]*\n)+\t\tif \(requested && \(getenv\("MALLOC_PERTURB_"\) \|\| Env\("BEA_LOCKSTEP_POISON_STACK"\)\)\) prctl\(PR_SET_DUMPABLE, 0, 0, 0, 0\);\n#endif')
    in_test_build = lockstep.find("#ifdef CE_NET_TEST_AUTOMATION", lockstep.index("int CE_LockstepRequested(void) {"))
    spot = dumpable.search(lockstep)
    check(spot is not None and lockstep.index("int CE_LockstepRequested(void) {") < in_test_build < spot.start() < lockstep.index("#else", spot.end()),
          "a test-automation pair that perturbs the allocator or poisons the stack is made non-dumpable (prctl PR_SET_DUMPABLE), in the test-automation branch of CE_LockstepRequested only", results)
    check(not dumpable.search(lockstep.replace("prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);", "", 1)), "mutant: a perturbed pair that stays dumpable is caught", results)
    check(not dumpable.search(lockstep.replace('getenv("MALLOC_PERTURB_") || ', "", 1)), "mutant: a pair perturbing the allocator that is not made non-dumpable is caught", results)

    ok = all(results)
    print("lockstep zeroed-allocation controls: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
