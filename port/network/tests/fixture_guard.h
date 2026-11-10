// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_NETWORK_FIXTURE_GUARD_H
#define CE_NETWORK_FIXTURE_GUARD_H
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/prctl.h>
#endif
static bool FixtureGuard()
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    return true;
#else
    // A piped system core collector may ignore RLIMIT_CORE. Disable dumpability after exec, in this owned process.
    return prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) == 0;
#endif
}
#endif
