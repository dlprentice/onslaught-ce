#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CE photo mode's draw filter (port/platform/common/d3d9hooks.cpp, ce_photo.h, patch 0133).

Photo mode hides the HUD, the battle line and the cockpit by dropping the draw calls their routines make, never by skipping
the routines (what they compute feeds the simulation). The production text of the filter, of the device-table bookkeeping
and of the two entry points the game calls is cut out of d3d9hooks.cpp and compiled against a scripted device table:
- with the filter off the device's own draw entries are untouched, and asking for it on without a device does nothing;
- on, the four draw entries (DrawPrimitive, DrawIndexedPrimitive and their UP forms) hold the filter and their neighbours
  are left alone; outside a scope every call reaches the entry it replaced with its arguments unchanged and its result
  returned; inside a scope no call reaches it and D3D_OK comes back; scopes nest; the count never goes below zero;
- asking for on twice keeps the real entries (never the filter), off puts back exactly what was there, even another
  hook's, and clears the count;
- a device made anew: a table that is not the old one starts unfiltered and is filtered from its own entries; the same
  table (Windows) stays filtered.
Causal mutants of each of these must be caught. That the engine's three routines open a scope, and where, is
ce_photo_tests.py; what the picture looks like is the offscreen captures.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ce_palette_tests import no_core, SANITIZE  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/platform/common/d3d9hooks.cpp"

HARNESS = r"""
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
typedef long HRESULT;
typedef unsigned UINT;
typedef int INT;
typedef int D3DPRIMITIVETYPE;
typedef int D3DFORMAT;
#define STDMETHODCALLTYPE
#define D3D_OK 0
struct IDirect3DDevice9 { int id; };
@TYPEDEFS@
static int slotWrites;
static void SetSlot(void **table, int slot, void *fn) { ++slotWrites; table[slot] = fn; }
@FILTER@
@ENTRIES@

#define CHECK(c, m) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s\n", m); std::exit(2); } } while (0)

// The device's own draw entries: each records its arguments and returns a mark, 7 + its family's own number.
struct Seen { int calls; long a[8]; };
static Seen seen[2][4];
static HRESULT Real(int set, int k, long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7)
{
    Seen &s = seen[set][k];
    ++s.calls;
    long v[8] = {a0, a1, a2, a3, a4, a5, a6, a7};
    for (int i = 0; i < 8; ++i) s.a[i] = v[i];
    return 7 + set * 10 + k;
}
#define REAL(set, k, ...) Real(set, k, __VA_ARGS__)
static HRESULT STDMETHODCALLTYPE realDraw0(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT s, UINT c) { return REAL(0, 0, (long)d, t, s, c, 0, 0, 0, 0); }
static HRESULT STDMETHODCALLTYPE realIdx0(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, INT b, UINT m, UINT n, UINT s, UINT p) { return REAL(0, 1, (long)d, t, b, m, n, s, p, 0); }
static HRESULT STDMETHODCALLTYPE realUP0(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT c, const void *v, UINT st) { return REAL(0, 2, (long)d, t, c, (long)v, st, 0, 0, 0); }
static HRESULT STDMETHODCALLTYPE realIdxUP0(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT m, UINT n, UINT c, const void *i, D3DFORMAT f, const void *v, UINT st) { return REAL(0, 3, (long)d, t, m, n, c, (long)i, f, (long)v + st); }
static HRESULT STDMETHODCALLTYPE realDraw1(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT s, UINT c) { return REAL(1, 0, (long)d, t, s, c, 0, 0, 0, 0); }
static HRESULT STDMETHODCALLTYPE realIdx1(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, INT b, UINT m, UINT n, UINT s, UINT p) { return REAL(1, 1, (long)d, t, b, m, n, s, p, 0); }
static HRESULT STDMETHODCALLTYPE realUP1(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT c, const void *v, UINT st) { return REAL(1, 2, (long)d, t, c, (long)v, st, 0, 0, 0); }
static HRESULT STDMETHODCALLTYPE realIdxUP1(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT m, UINT n, UINT c, const void *i, D3DFORMAT f, const void *v, UINT st) { return REAL(1, 3, (long)d, t, m, n, c, (long)i, f, (long)v + st); }

static void *tableA[160], *tableB[160];
static void fill(void **table, int set)
{
    for (int i = 0; i < 160; ++i) table[i] = (void *)(uintptr_t)(0x1000 + i);
    table[81] = set ? (void *)&realDraw1 : (void *)&realDraw0;
    table[82] = set ? (void *)&realIdx1 : (void *)&realIdx0;
    table[83] = set ? (void *)&realUP1 : (void *)&realUP0;
    table[84] = set ? (void *)&realIdxUP1 : (void *)&realIdxUP0;
}

static IDirect3DDevice9 device = {1};
static int vertices[4];
// Calls entry `k` of `table` as the game would, returns what it returned.
static HRESULT call(void **table, int k)
{
    switch (k)
    {
    case 0: return ((DrawFn)table[81])(&device, 4, 11, 22);
    case 1: return ((DrawIdxFn)table[82])(&device, 4, -3, 5, 6, 7, 8);
    case 2: return ((DrawUPFn)table[83])(&device, 5, 9, vertices, 16);
    default: return ((DrawIdxUPFn)table[84])(&device, 6, 1, 2, 3, vertices + 1, 101, vertices + 2, 4);
    }
}
static int calls(int set) { int n = 0; for (int k = 0; k < 4; ++k) n += seen[set][k].calls; return n; }
static bool args(int set, int k)
{
    const long *a = seen[set][k].a;
    if (a[0] != (long)&device) return false;
    switch (k)
    {
    case 0: return a[1] == 4 && a[2] == 11 && a[3] == 22;
    case 1: return a[1] == 4 && a[2] == -3 && a[3] == 5 && a[4] == 6 && a[5] == 7 && a[6] == 8;
    case 2: return a[1] == 5 && a[2] == 9 && a[3] == (long)vertices && a[4] == 16;
    default: return a[1] == 6 && a[2] == 1 && a[3] == 2 && a[4] == 3 && a[5] == (long)(vertices + 1) && a[6] == 101 &&
                    a[7] == (long)(vertices + 2) + 4;
    }
}
static bool isFilter(void *entry, int k) { return entry == kDrawFilters[k]; }

int main()
{
    fill(tableA, 0);
    fill(tableB, 1);
    // No device yet: nothing to filter.
    PortCeDrawHooks(true);
    CHECK(!gDrawsFiltered && slotWrites == 0, "asking for the filter without a device does nothing");
    NoteDeviceTable(tableA);
    PortCeDrawHooks(false);
    CHECK(slotWrites == 0 && !gDrawsFiltered, "off without the filter does nothing");
    for (int k = 0; k < 4; ++k) CHECK(call(tableA, k) == 7 + k && args(0, k), "with the filter off the device's own entries are called as they are");
    CHECK(calls(0) == 4, "and each once");

    // On: the four entries replaced, nothing else touched.
    PortCeDrawHooks(true);
    CHECK(gDrawsFiltered, "the filter is on");
    for (int k = 0; k < 4; ++k) CHECK(isFilter(tableA[81 + k], k), "each draw entry holds its filter");
    for (int i = 0; i < 160; ++i)
        if (i < 81 || i > 84) CHECK(tableA[i] == (void *)(uintptr_t)(0x1000 + i), "no other entry is touched");
    // Outside a scope the calls go through unchanged, results included.
    for (int k = 0; k < 4; ++k) CHECK(call(tableA, k) == 7 + k, "outside a scope the result is the device's");
    CHECK(calls(0) == 8, "outside a scope every call reaches the device");
    for (int k = 0; k < 4; ++k) CHECK(args(0, k), "with its arguments unchanged");

    // Inside a scope nothing is drawn, and the call still succeeds.
    PortCeDrawsDropped(1);
    for (int k = 0; k < 4; ++k) CHECK(call(tableA, k) == D3D_OK, "inside a scope a draw call succeeds without drawing");
    CHECK(calls(0) == 8, "inside a scope no call reaches the device");
    PortCeDrawsDropped(1);
    for (int k = 0; k < 4; ++k) CHECK(call(tableA, k) == D3D_OK, "in a nested scope too");
    PortCeDrawsDropped(-1);
    for (int k = 0; k < 4; ++k) CHECK(call(tableA, k) == D3D_OK, "closing an inner scope keeps dropping");
    PortCeDrawsDropped(-1);
    for (int k = 0; k < 4; ++k) CHECK(call(tableA, k) == 7 + k, "closing the last scope draws again");
    CHECK(calls(0) == 12, "and the calls reach the device");

    // The count does not go below zero: an extra close is not a credit against the next scope.
    PortCeDrawsDropped(-3);
    PortCeDrawsDropped(1);
    CHECK(call(tableA, 0) == D3D_OK, "a scope after extra closes drops");
    PortCeDrawsDropped(-1);
    CHECK(call(tableA, 0) == 7, "and one close ends it");

    // Asking again keeps the real entries.
    PortCeDrawHooks(true);
    CHECK(isFilter(tableA[81], 0) && gDrawsReal[0] == (void *)&realDraw0, "on twice keeps the device's entry, not the filter");
    CHECK(call(tableA, 0) == 7, "and still reaches it");

    // Off puts back what was there and clears the count.
    PortCeDrawsDropped(2);
    PortCeDrawHooks(false);
    CHECK(!gDrawsFiltered && tableA[81] == (void *)&realDraw0 && tableA[82] == (void *)&realIdx0 && tableA[83] == (void *)&realUP0 &&
          tableA[84] == (void *)&realIdxUP0, "off puts the device's entries back");
    for (int i = 0; i < 160; ++i)
        if (i < 81 || i > 84) CHECK(tableA[i] == (void *)(uintptr_t)(0x1000 + i), "off touches nothing else");
    PortCeDrawHooks(true);
    CHECK(call(tableA, 0) == 7, "off cleared the count of scopes left open");
    PortCeDrawHooks(false);

    // Another hook already in an entry (the draw log) is what the filter forwards to, and what comes back.
    void *logging = (void *)&realDraw1;
    tableA[81] = logging;
    PortCeDrawHooks(true);
    CHECK(call(tableA, 0) == 17, "the filter forwards to whatever the entry held");
    PortCeDrawHooks(false);
    CHECK(tableA[81] == logging, "and gives it back");
    tableA[81] = (void *)&realDraw0;

    // A device made anew.
    PortCeDrawHooks(true);
    NoteDeviceTable(tableB);
    CHECK(!gDrawsFiltered, "a table made anew is not filtered");
    PortCeDrawHooks(true);
    CHECK(gDrawsFiltered && isFilter(tableB[81], 0) && gDrawsReal[0] == (void *)&realDraw1, "it is filtered from its own entries");
    for (int k = 0; k < 4; ++k) CHECK(call(tableB, k) == 17 + k, "and forwards to them");
    NoteDeviceTable(tableB);
    CHECK(gDrawsFiltered, "the same table (one shared table) stays filtered");
    PortCeDrawHooks(false);
    CHECK(tableB[81] == (void *)&realDraw1 && tableB[84] == (void *)&realIdxUP1, "off restores that table");
    CHECK(isFilter(tableA[81], 0), "the earlier table, no longer the device's, is not touched");
    std::puts("draw filter passed");
    return 0;
}
"""


def blocks(source):
    """The filter's typedefs, its definitions and the two entry points, cut out of d3d9hooks.cpp."""
    typedefs = re.findall(r"^typedef HRESULT \(STDMETHODCALLTYPE \*Draw(?:Idx)?Fn\)[^\n]*\n", source, re.M)
    assert len(typedefs) == 2, "DrawFn and DrawIdxFn"
    start = source.index("// CE photo mode's draw filter (ce_photo.h, patch 0133).")
    filt = source[start:source.index("\n#endif\n", start) + 1]
    start = source.index("// Photo mode's draw filter in the device's draw entries")
    entries = source[start:source.index("\n#endif", start) + 1]
    return "".join(typedefs), filt, entries


class DrawFilterTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "local-data/port/photo-draw-filter-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        self.out = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, name, source=None):
        text = source if source is not None else SOURCE.read_text()
        typedefs, filt, entries = blocks(text)
        program = self.out / f"{name}.cpp"
        program.write_text(HARNESS.replace("@TYPEDEFS@", typedefs).replace("@FILTER@", filt).replace("@ENTRIES@", entries))
        exe = self.out / name
        built = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Werror",
                                *SANITIZE, str(program), "-o", str(exe)], capture_output=True, text=True, timeout=120, preexec_fn=no_core)
        self.assertEqual(built.returncode, 0, built.stderr)
        return exe

    def run_exe(self, exe):
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=0", UBSAN_OPTIONS="halt_on_error=1")
        return subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=30, preexec_fn=no_core)

    def test_filter(self):
        ran = self.run_exe(self.build("actual"))
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("draw filter passed", ran.stdout)

    def test_source_wires_the_filter_into_the_device(self):
        source = SOURCE.read_text()
        uses = [m.start() for m in re.finditer(r"NoteDeviceTable\(table\);", source)]
        self.assertEqual(len(uses), 1, "CreateDevice tells the filter which table is the device's")
        create = source[source.index("HRESULT STDMETHODCALLTYPE CreateDevice("):]
        self.assertLess(create.index("NoteDeviceTable(table);"), create.index("SetSlot(table, 17, (void *)&Present);"))

        def directive(at):
            """The nearest preprocessor line before at."""
            lines = [l for l in source[:at].split("\n") if re.match(r"#\s*(if|ifdef|ifndef|else|elif|endif)\b", l)]
            return lines[-1]

        # Faithful builds do not contain any of it: each piece sits directly under #ifdef CE_VERSION.
        for needle in ("FilterDraw(", "void NoteDeviceTable(", "void PortCeDrawHooks(bool on)", "void PortCeDrawsDropped(int delta)"):
            self.assertEqual(directive(source.index(needle)), "#ifdef CE_VERSION", f"{needle} is compiled only in CE builds")
        self.assertEqual(directive(uses[0]), "#ifdef CE_VERSION", "so is CreateDevice's call")

    def test_mutants_are_caught(self):
        source = SOURCE.read_text()
        mutants = [
            ("draw-never-dropped", "return gDrawsDropped > 0 ? D3D_OK : ((DrawFn)gDrawsReal[0])", "return ((DrawFn)gDrawsReal[0])"),
            ("idx-never-dropped", "return gDrawsDropped > 0 ? D3D_OK : ((DrawIdxFn)gDrawsReal[1])", "return ((DrawIdxFn)gDrawsReal[1])"),
            ("up-never-dropped", "return gDrawsDropped > 0 ? D3D_OK : ((DrawUPFn)gDrawsReal[2])", "return ((DrawUPFn)gDrawsReal[2])"),
            ("idxup-never-dropped", "return gDrawsDropped > 0 ? D3D_OK : ((DrawIdxUPFn)gDrawsReal[3])", "return ((DrawIdxUPFn)gDrawsReal[3])"),
            ("draw-always-dropped", "return gDrawsDropped > 0 ? D3D_OK : ((DrawFn)gDrawsReal[0])(d, t, start, count);", "return D3D_OK;"),
            ("up-always-dropped", "return gDrawsDropped > 0 ? D3D_OK : ((DrawUPFn)gDrawsReal[2])(d, t, count, data, stride);", "return D3D_OK;"),
            ("draw-arguments-swapped", "((DrawFn)gDrawsReal[0])(d, t, start, count)", "((DrawFn)gDrawsReal[0])(d, t, count, start)"),
            ("idxup-stride-lost", "(d, t, minv, nv, count, index, format, data, stride)", "(d, t, minv, nv, count, index, format, data, 0)"),
            ("slots-shifted", "const int kDrawSlots[4] = {81, 82, 83, 84};", "const int kDrawSlots[4] = {80, 81, 82, 83};"),
            ("no-up-slots", "const int kDrawSlots[4] = {81, 82, 83, 84};", "const int kDrawSlots[4] = {81, 82, 82, 82};"),
            ("on-twice-recaptures", "if (on == gDrawsFiltered || !gDeviceTable)", "if (!gDeviceTable)"),
            ("no-device-guard", "if (on == gDrawsFiltered || !gDeviceTable)", "if (on == gDrawsFiltered)"),
            ("off-keeps-filter", "SetSlot(gDeviceTable, kDrawSlots[i], on ? kDrawFilters[i] : gDrawsReal[i]);",
             "SetSlot(gDeviceTable, kDrawSlots[i], kDrawFilters[i]);"),
            ("capture-on-off", "\t\tif (on)\n\t\t\tgDrawsReal[i] = gDeviceTable[kDrawSlots[i]];\n", "\t\tgDrawsReal[i] = gDeviceTable[kDrawSlots[i]];\n"),
            ("off-keeps-count", "\tgDrawsFiltered = on;\n\tif (!on)\n\t\tgDrawsDropped = 0;\n", "\tgDrawsFiltered = on;\n"),
            ("count-below-zero", "\tif (gDrawsDropped < 0)\n\t\tgDrawsDropped = 0;\n", ""),
            ("new-table-stays-filtered", "\tif (gDeviceTable != table)\n\t\tgDrawsFiltered = false;\n", ""),
            ("table-not-kept", "\tgDeviceTable = table;\n}", "}"),
            ("filtered-never-set", "\tgDrawsFiltered = on;\n", ""),
        ]
        for name, old, new in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1, name)
                exe = self.build(name, source.replace(old, new))
                ran = self.run_exe(exe)
                self.assertNotEqual(ran.returncode, 0, f"mutant {name} survived: {ran.stdout}")


if __name__ == "__main__":
    unittest.main()
