# SPDX-License-Identifier: GPL-3.0-only
"""The menu markers of the shared input driver (port/tools/tests/input_lifetime_driver.cpp), expanded as input_lifetime_tests.main() expands them.

Main's pause-menu click fix (patch 0186) put REAL_CE_LAYER_FUNCTION and REAL_WIDE_FUNCTIONS into that driver; input_lifetime_tests.py replaces them with the real PortCeLayer body (ce.cpp) and the real
widescreen menu-pointer bodies (widescreen.cpp) under test names. The mouse activity and diagnostic mouse tests of the co-op line build their own driver from the same file and so need the same
expansion; this module does it with input_lifetime_tests.definition() and the same renames, so that main's test stays untouched and a change to the real bodies reaches all three.
"""
import re

import input_lifetime_tests as lifetime

WIDE_SIGNATURES = (r"void PortWideBackBuffer\(unsigned w, unsigned h\)", r"void PortUiMouse\(int \*x, int \*y\)",
                   r"void PortUiMouseMenuPoint\(int \*x, int \*y\)", r"int PortUiMouseMenu\(int mouseInputActive\)")
RENAMES = {"PortWideBackBuffer": "TestWideBackBuffer", "PortUiMouse": "TestUiMouse",
           "PortUiMouseMenuPoint": "TestUiMouseMenuPoint", "PortUiMouseMenu": "TestUiMouseMenu",
           "PortCeLayer": "TestRealCeLayer", "PortCeCmdMapOpen": "TestMenuMapOpen", "PortCeEditorMousePixels": "TestMenuEditorPixels",
           "gW": "testMapW", "gH": "testMapH", "gActive": "testMapActive", "gX": "testMapX",
           "gY": "testMapY", "gScale": "testMapScale", "gLeft": "testMapLeft", "gTop": "testMapTop"}


def expand(driver, root):
    """The driver text with REAL_CE_LAYER_FUNCTION and REAL_WIDE_FUNCTIONS replaced by the real bodies read from the checkout at ROOT."""
    ce_layer = lifetime.definition((root / "port/platform/common/ce.cpp").read_text(), r"bool PortCeLayer\(\)")
    driver = driver.replace("REAL_CE_LAYER_FUNCTION", ce_layer.replace("bool PortCeLayer()", "bool TestRealCeLayer()", 1))
    wide = (root / "port/platform/common/widescreen.cpp").read_text()
    bodies = [lifetime.definition(wide, signature) for signature in WIDE_SIGNATURES]
    mapped = "\n".join(re.sub(r"\b(" + "|".join(RENAMES) + r")\b", lambda m: RENAMES[m[0]], body) for body in bodies)
    return driver.replace("REAL_WIDE_FUNCTIONS", mapped)
