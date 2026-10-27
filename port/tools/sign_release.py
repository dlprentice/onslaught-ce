#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retired: wrote the old latest.json feed that launchers 0.1.0-0.1.6 fetch (port/updater/UPDATER-V2.md).

A signed latest.json at releases/latest/download/ would make an affected old launcher offer its unsafe in-place update,
so this tool no longer writes anything. The v2 feed is port/tools/sign_update_v2.py.
"""
import sys

if __name__ == "__main__":
    sys.exit("sign_release.py is retired: never publish latest.json. See port/updater/UPDATER-V2.md and sign_update_v2.py.")
