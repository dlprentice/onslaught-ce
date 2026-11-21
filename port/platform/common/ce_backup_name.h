// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stddef.h>

// Unicode Bidi_Control characters are formatting, not letters. Keep ordinary
// Arabic, Hebrew and mixed-script names as written.
inline bool CeBackupDirectionMark(unsigned c) {
    return c == 0x061c || c == 0x200e || c == 0x200f ||
           (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069);
}

template<class Unit>
inline bool CeBackupNameHasDirectionMark(const Unit *name) {
    if (name) for (; *name; ++name) if (CeBackupDirectionMark(static_cast<unsigned>(*name))) return true;
    return false;
}

// Match just the UTF-8 forms of those code points. Invalid UTF-8 is rejected
// separately by the existing filename validation.
inline bool CeBackupFileHasDirectionMark(const char *bytes, size_t length) {
    const unsigned char *p = reinterpret_cast<const unsigned char *>(bytes);
    for (size_t i = 0; i < length; ++i) {
        if (p[i] == 0xd8 && length-i >= 2 && (p[i+1] & 0xc0) == 0x80 &&
            CeBackupDirectionMark(((p[i] & 31u) << 6) | (p[i+1] & 63u))) return true;
        if (p[i] == 0xe2 && length-i >= 3 && (p[i+1] & 0xc0) == 0x80 && (p[i+2] & 0xc0) == 0x80 &&
            CeBackupDirectionMark(((p[i] & 15u) << 12) | ((p[i+1] & 63u) << 6) | (p[i+2] & 63u))) return true;
    }
    return false;
}

inline const char *CeBackupDirectionReason() {
    return "This name contains hidden text direction marks.";
}

// display is a buffer of 256 UTF-16 units. file receives the exact UTF-8 leaf.
bool CeBackupSelectedName(const void *selection, char *file, unsigned capacity, void *display);
