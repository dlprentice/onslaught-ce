/* SPDX-License-Identifier: GPL-3.0-only */
/* port/platform/common/ce_record_mode.h: opt-in CE bug-recording admission helper (BEA_PORT_RECORD_CE=1).
 * Plain BEA_PORT_RECORD / BEA_PORT_TAPE / BEA_PORT_PROBES / BEA_PORT_CLOCK runs (including BEA_PORT_CE_LAYER=1
 * screenshot captures) always return 0 so every faithful/parity fence and regression test stays untouched. */
#ifndef PORT_CE_RECORD_MODE_H
#define PORT_CE_RECORD_MODE_H

#include <stddef.h>
#include <stdlib.h>

static inline int ce_record_ce_allowed(void)
{
#ifdef CE_VERSION
	const char *ceRec = getenv("BEA_PORT_RECORD_CE");
	if (!ceRec || ceRec[0] != '1' || ceRec[1] != '\0')
		return 0;
	const char *rec = getenv("BEA_PORT_RECORD");
	const char *tape = getenv("BEA_PORT_TAPE");
	if (!((rec && *rec) || (tape && *tape)))
		return 0;
	static const char *const kHard[] = {
		"BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT",
		"BEA_NET_HOST", "BEA_NET_JOIN", "BEA_LOCKSTEP_HOST", "BEA_LOCKSTEP_JOIN"
	};
	for (size_t i = 0; i < sizeof(kHard) / sizeof(kHard[0]); ++i)
	{
		const char *v = getenv(kHard[i]);
		if (v && *v)
			return 0;
	}
	return 1;
#else
	return 0;
#endif
}

#ifdef __cplusplus
static inline bool PortCeRecordFeaturesEval()
{
	return ce_record_ce_allowed() != 0;
}
#endif

#endif
