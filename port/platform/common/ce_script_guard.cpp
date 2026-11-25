// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_script_guard.cpp: whether a script notification that nests in the script VM is deferred a frame
// (ce_script_guard.h; patch 0176 calls it from IScript::StartedDying and IScript::Died).
#include "ce_script_guard.h"
#include "mods.h"
#include "portlog.h"

#include <stdio.h>
#include <string.h>

static int sDeferred;
static bool sLogged[2];

bool PortModLevelSupplied(int level)
{
	char path[64];
	snprintf(path, sizeof(path), "data/resources/%03d_res_PC.aya", level);
	if (PortModHas(path))
		return true;
	snprintf(path, sizeof(path), "data/worlds/world%03d.wrd", level);
	return PortModHas(path);
}

bool PortCeScriptDeferNested(int level, const char *entry)
{
	if (!PortModLevelSupplied(level))
		return false;

	sDeferred++;
	int which = strcmp(entry, "died") == 0 ? 1 : 0;
	if (!sLogged[which])
	{
		sLogged[which] = true;
		PortSessionLog(PORT_SESSION_GAME, true, "script %s entered while the script VM was running (level %d is a mod's): %s", entry, level,
					   which ? "it is dropped (its script is deleted before the next frame)" : "it waits for the next frame");
	}
	return true;
}

int PortCeScriptDeferred()
{
	return sDeferred;
}
