// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/runmode.cpp: runmode.h.
#include "runmode.h"
#include "testhooks.h"
#include <stdio.h>
#include <stdlib.h>

namespace
{
bool	Set(const char *name)
{
	const char *v = getenv(name);
	return v && *v;
}
bool	AnySet(const char *const *names, int count)
{
	for (int i = 0; i < count; ++i)
		if (Set(names[i])) return true;
	return false;
}
PortRunMode	Decide()
{
	static const char *const network[] = {"BEA_NET_HOST", "BEA_NET_JOIN", "BEA_LOCKSTEP_HOST", "BEA_LOCKSTEP_JOIN"};
	static const char *const record[] = {"BEA_PORT_RECORD", "BEA_RECORD"};
	static const char *const replay[] = {"BEA_PORT_TAPE", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT"};
	if (AnySet(network, 4)) return PORT_RUN_NETWORK_TEST;
	if (AnySet(record, 2)) return PORT_RUN_RECORD;
	if (AnySet(replay, 3)) return PORT_RUN_REPLAY;
	if (Set("BEA_PORT_PROBES")) return PORT_RUN_TEST;
	if (gPortTestHooks && gPortTestHooks->journeyActive && gPortTestHooks->journeyActive()) return PORT_RUN_TEST_KIT;
	if (Set("BEA_PORT_CLOCK")) return PORT_RUN_TEST;
	return PORT_RUN_PLAY;
}
}

PortRunMode	PortRunModeNow()
{
	static const PortRunMode mode = Decide();
	static bool told;
	if (!told)
	{
		told = true;
		fprintf(stderr, "onslaught-ce: run mode: %s\n", PortRunModeName(mode));
	}
	return mode;
}

const char	*PortRunModeName(PortRunMode mode)
{
	switch (mode)
	{
	case PORT_RUN_PLAY:			return "play";
	case PORT_RUN_TEST:			return "test";
	case PORT_RUN_TEST_KIT:		return "test-kit";
	case PORT_RUN_REPLAY:		return "replay";
	case PORT_RUN_RECORD:		return "record";
	case PORT_RUN_NETWORK_TEST:	return "network-test";
	}
	return "unknown";
}

bool		PortRunOffersNetworkPages()
{
	const PortRunMode mode = PortRunModeNow();
	return mode == PORT_RUN_PLAY || mode == PORT_RUN_TEST_KIT;
}
