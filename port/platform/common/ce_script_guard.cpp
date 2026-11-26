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
static bool sWorldLatched;
static bool sWorldModSupplied;
static int sWorldPrimaryLevel = -1;
static int sWorldBaseLevel = -1;
static int sModResLevel = -1;
static int sModWorldLevel = -1;

static int ParseDigits3(const char *p)
{
	if (!p || p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9' || p[2] < '0' || p[2] > '9')
		return -1;
	return (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');
}

static bool EqualsAsciiCase(const char *a, const char *b, size_t n)
{
	for (size_t i = 0; i < n; ++i)
	{
		unsigned char ca = (unsigned char)a[i];
		unsigned char cb = (unsigned char)b[i];
		if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
		if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
		if (ca != cb) return false;
	}
	return true;
}

static bool ProbeLevelModSupplied(int level)
{
	char path[64];
	snprintf(path, sizeof(path), "data/resources/%03d_res_PC.aya", level);
	bool supplied = PortModHas(path);
	if (!supplied)
	{
		snprintf(path, sizeof(path), "data/worlds/world%03d.wrd", level);
		supplied = PortModHas(path);
	}
	return supplied;
}

extern "C" void PortCeScriptGuardClear(void)
{
	sWorldLatched = false;
	sWorldModSupplied = false;
	sWorldPrimaryLevel = -1;
	sWorldBaseLevel = -1;
	sModResLevel = -1;
	sModWorldLevel = -1;
}

extern "C" void PortCeScriptGuardNoteModFile(const char *key)
{
	if (!key)
		return;
	const size_t len = strlen(key);
	// Level archive key (29 chars): data/resources/NNN_res_pc.*
	if (len == 29 && EqualsAsciiCase(key, "data/resources/", 15) && EqualsAsciiCase(key + 18, "_res_pc.aya", 11))
	{
		int n = ParseDigits3(key + 15);
		if (n >= 0)
		{
			sModResLevel = n;
			if (sWorldLatched && (n == sWorldPrimaryLevel || n == sWorldBaseLevel))
				sWorldModSupplied = true;
		}
		return;
	}
	// Level world key (24 chars): data/worlds/worldNNN.*
	if (len == 24 && EqualsAsciiCase(key, "data/worlds/world", 17) && EqualsAsciiCase(key + 20, ".wrd", 4))
	{
		int n = ParseDigits3(key + 17);
		if (n >= 0)
		{
			sModWorldLevel = n;
			if (sWorldLatched && (n == sWorldPrimaryLevel || n == sWorldBaseLevel))
				sWorldModSupplied = true;
		}
	}
}

extern "C" void PortCeScriptGuardRecordWorld(int level, bool base, bool modWorld)
{
	if (!base)
	{
		if (sWorldPrimaryLevel != -1 && sWorldPrimaryLevel != level)
		{
			sWorldBaseLevel = -1;
			sWorldModSupplied = false;
		}
		sWorldLatched = true;
		sWorldPrimaryLevel = level;
	}
	else
	{
		sWorldLatched = true;
		sWorldBaseLevel = level;
	}
	if (modWorld || sModResLevel == level || sModWorldLevel == level || ProbeLevelModSupplied(level))
		sWorldModSupplied = true;
}

bool PortModLevelSupplied(int level)
{
	if (sWorldLatched && (level == sWorldPrimaryLevel || level == sWorldBaseLevel))
		return sWorldModSupplied;
	return ProbeLevelModSupplied(level);
}

bool PortCeScriptDeferNested(int level, const char *entry)
{
	bool supplied = PortModLevelSupplied(level);
	if (!supplied)
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
