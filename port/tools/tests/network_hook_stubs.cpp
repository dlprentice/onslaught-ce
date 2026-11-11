// SPDX-License-Identifier: GPL-3.0-only
// Stand-ins for the network layer's content-admission and local-file hooks (network/compatibility.h, game/ce_network_boot.h) for the
// fixtures that compile crtfile.cpp, a platform's files.cpp or ce.cpp without the network layer. Offline behaviour: every read is
// admitted and recorded nowhere, no network boot route is requested, local files open as ordinary files, and the cheat gate is open.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern "C" {
int CE_CompatibilityObserveAuthoredRead(const char *, int, char *, size_t) { return 1; }
int CE_CompatibilityAuthoredOpenFailed(const char *, char *, size_t) { return 1; }
int CE_CompatibilityUnsupportedReader(char *, size_t) { return 1; }
int CE_CompatibilityValidateOpenedFile(const char *, FILE *, char *, size_t) { return 1; }
int CE_CompatibilityValidateOpenedNative(const char *, intptr_t, char *, size_t) { return 1; }
int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }
int CE_BootRequested(void) { return 0; }
int CE_LockstepRequested(void) { return 0; }		// patch 0185's career fence asks both
int CE_BootLocalFileAllowed(unsigned, const char *, const char *) { return 1; }
}
#ifndef NETWORK_HOOK_STUBS_NO_LOCAL_FILE	// a fixture that links a platform files.cpp gets PortOpenLocalFile from it
extern "C" FILE *PortOpenLocalFile(unsigned, const char *name, const char *mode) { return fopen(name, mode); }
#endif
