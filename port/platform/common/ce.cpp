// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce.cpp: whether the CE layer (additions over the faithful port, such as the version text of
// patch 0037) may draw: never in a replay or a recording (the virtual or shim clock, an input tape, probes), so those
// runs stay identical to the faithful build. Public builds only define CE_VERSION; the answer is fixed at start.
#include <stdlib.h>

bool PortCeLayer()
{
	static int on = -1;
	if (on < 0)
	{
		const char *force = getenv("BEA_PORT_CE_LAYER");	// screenshots of the CE layer during a replay: 1 forces it on
		if (force && *force)
			return (on = force[0] == '1');
		const char *names[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"};
		on = 1;
		for (const char *n : names)
		{
			const char *v = getenv(n);
			if (v && *v)
				on = 0;
		}
	}
	return on == 1;
}
