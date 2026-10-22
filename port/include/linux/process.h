// SPDX-License-Identifier: GPL-3.0-only
// port/include/linux/process.h: MSVC process/thread declarations the game's headers include. Linux only.
#ifndef PORT_PROCESS_H
#define PORT_PROCESS_H
#include <stdlib.h>
#include <unistd.h>
#define _P_WAIT 0
#define _P_NOWAIT 1
#define P_WAIT _P_WAIT
#define P_NOWAIT _P_NOWAIT
#ifdef __cplusplus
extern "C" {
#endif
intptr_t _spawnl(int mode, const char *cmdname, const char *arg0, ...);
#define spawnl _spawnl
#ifdef __cplusplus
}
#endif
#endif
