// SPDX-License-Identifier: GPL-3.0-only
// port/include/linux/imagehlp.h: ltshell.cpp includes it, but its symbol-lookup code is compiled out (#if 0) as in
// retail, which imports nothing from IMAGEHLP. Linux only.
#ifndef PORT_IMAGEHLP_H
#define PORT_IMAGEHLP_H
#include <windows.h>
#endif
