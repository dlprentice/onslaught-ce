// SPDX-License-Identifier: GPL-3.0-only
// port/tools/script_guard_died_prelude.h: what the game's common header gives the few files that
// port/tools/script_guard_died_tests.py compiles on the host (its driver and the game's own SPtrSet, Monitor, activereader, event and
// scheduledevent sources). Authored stand-ins: the basic types, the two allocation tags those files name, the log, the assert and the profiler's marker. The
// suite puts an empty file in place of each header they include and this prelude replaces.
#ifndef SCRIPT_GUARD_DIED_PRELUDE_H
#define SCRIPT_GUARD_DIED_PRELUDE_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <new>

typedef int BOOL;
typedef unsigned long ULONG;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define PC 1
#define TARGET PC

#define SAFE_DELETE(p) { if (p) { delete (p); (p) = NULL; } }
#define ASSERT(x) do { if (!(x)) { printf("ASSERT FAILED\n"); } } while (0)
#define PROFILE_FN(name)

enum EMemoryTypeStandIn { MEMTYPE_DELETION_CALLBACK_LIST, MEMTYPE_SPTRSET };
inline void *operator new(size_t size, EMemoryTypeStandIn) { return ::operator new(size); }
inline void *operator new[](size_t size, EMemoryTypeStandIn) { return ::operator new[](size); }

struct CLogStandIn
{
	void AddMessage(const char *text, ...) { printf("GAME LOG %s\n", text); }
};
extern CLogStandIn LOG;

#endif
