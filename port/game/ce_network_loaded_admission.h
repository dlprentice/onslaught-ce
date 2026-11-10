// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_NETWORK_LOADED_ADMISSION_H
#define CE_NETWORK_LOADED_ADMISSION_H

#ifdef __cplusplus
extern "C" {
#endif

// Experimental level-852 policy: two native players, genuine loaded configuration zero on each,
// uninverted sides, ordinary clocks and no active cheats/build/demo/input automation. This reads
// actual native objects and already initialized platform state; it never selects or repairs a preset.
// The compatibility reader/history fence must already own the freshly loaded authored definitions.
// Call on the game thread after PostLoadProcess, before WorldBegin, and again after autoexec before
// PreRun/baseline/START. Failure refuses admission; this is not a continuous runtime mutation fence.
// No transport/digest/preset assertions are accepted from the caller. Returns 1 or 0 with a local error.
int CE_NetLoadedAdmissionValidate(void);
const char *CE_NetLoadedAdmissionError(void);

#ifdef __cplusplus
}
#endif
#endif
