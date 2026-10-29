// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/save_overwrite.cpp (patch 0118): the one-shot approval an overwrite YES gives its retry. The retail save
// page answered YES by deleting the old career and saving again. Here nothing is deleted: YES approves exactly one retry for
// exactly this name, CreateSave then admits the existing career without asking again, and the write replaces it as a whole
// (save_replace.h). The Windows build's equivalent is platform/windows/save_namespace.cpp; this one compares names as the
// Linux build's career list does (the game's _wcsicmp, port_wcsicmp).
#include <stdint.h>
#include <string.h>

extern "C" int port_wcsicmp(const uint16_t *a, const uint16_t *b);	// crt.cpp

namespace
{
const unsigned MaxUnits = 32;	// 31 characters and the terminator, as the virtual keyboard holds
uint16_t approvedName[MaxUnits];

// Reads the name's units without running past the terminator or the limit; false when it is empty or too long.
bool Copy(const void *name, uint16_t (&out)[MaxUnits])
{
	const uint16_t *units = (const uint16_t *)name;
	for (unsigned i = 0; i < MaxUnits; i++)
	{
		out[i] = units[i];
		if (!units[i])
			return i != 0;
	}
	return false;
}
}

extern "C" void PortSaveClearOverwrite()
{
	memset(approvedName, 0, sizeof(approvedName));
}

// 0 when `requested` names the same career as `existing` and the approval is now held for it; otherwise nonzero and none is held.
extern "C" unsigned PortSaveApproveOverwrite(const void *requested, const void *existing)
{
	PortSaveClearOverwrite();
	uint16_t want[MaxUnits], have[MaxUnits];
	if (!requested || !existing || !Copy(requested, want) || !Copy(existing, have) || port_wcsicmp(want, have) != 0)
		return 1;
	memcpy(approvedName, want, sizeof(approvedName));
	return 0;
}

// True once for the name that was approved. Every call clears the approval first, whatever it answers.
extern "C" int PortSaveConsumeOverwrite(const void *requested)
{
	uint16_t previous[MaxUnits], name[MaxUnits];
	memcpy(previous, approvedName, sizeof(previous));
	PortSaveClearOverwrite();
	return previous[0] && requested && Copy(requested, name) && port_wcsicmp(previous, name) == 0;
}
