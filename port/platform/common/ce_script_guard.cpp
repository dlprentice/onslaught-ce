// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_script_guard.cpp: whether a script notification that nests in the script VM waits a frame
// (ce_script_guard.h; patch 0176 calls it from IScript::StartedDying and IScript::Died).
#include "ce_script_guard.h"
#include "mods.h"
#include "portlog.h"

#include <stdio.h>
#include <string.h>

static int sDeferred;
static bool sLogged[2];

bool PortCeScriptDeferNested(int level, const char *entry)
{
	char path[64];
	snprintf(path, sizeof(path), "data/resources/%03d_res_PC.aya", level);
	bool supplied = PortModHas(path);
	if (!supplied)
	{
		snprintf(path, sizeof(path), "data/worlds/world%03d.wrd", level);
		supplied = PortModHas(path);
	}
	if (!supplied)
		return false;

	sDeferred++;
	int which = strcmp(entry, "died") == 0 ? 1 : 0;
	if (!sLogged[which])
	{
		sLogged[which] = true;
		PortSessionLog(PORT_SESSION_GAME, true, "script %s entered while the script VM was running (level %d is a mod's): it waits for the next frame", entry, level);
	}
	return true;
}

int PortCeScriptDeferred()
{
	return sDeferred;
}
