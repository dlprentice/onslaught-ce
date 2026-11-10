#pragma once

// The production owner uses the file wrapper supplied by the real port prefix.
#include <cstdio>
extern "C" FILE *port_fopen(const char *, const char *);
#if defined(CE_NET_TEST_AUTOMATION) && !defined(CE_LOCKSTEP_REAL_ADMISSION)
// Host-only fixture modes keep their standard C file boundary.
extern "C" inline FILE *port_fopen(const char *path, const char *mode) { return std::fopen(path, mode); }
#endif
