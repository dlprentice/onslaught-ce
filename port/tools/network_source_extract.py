#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Small source-extraction helper shared by network test suites."""


def actual_body(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for i in range(opening, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if not depth:
                return text[start:i + 1]
    raise ValueError("unbalanced native function: " + signature)
