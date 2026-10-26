// SPDX-License-Identifier: GPL-3.0-only
// Save-specific Windows namespace/approval; resource strings and the game's CRT comparisons are unaffected.
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../common/save_names.h"

extern "C" int PortSaveNamesEqual(const void *a, const void *b)
{
	char scratch[260];
	unsigned error = PortSavePathUtf8(a, scratch, sizeof(scratch));
	if (!error) error = PortSavePathUtf8(b, scratch, sizeof(scratch));
	if (error) { SetLastError(error); return -1; }
	// Windows recommends ordinal comparison for NTFS names: OS case mapping, with no canonical/linguistic aliases.
	int result = CompareStringOrdinal((const WCHAR *)a, -1, (const WCHAR *)b, -1, TRUE);
	return result ? result == CSTR_EQUAL : -1;
}

namespace
{
// Only the main-thread save page uses this state, across its immediate YES -> CreateSave call.
uint16_t approvedName[32];
}

extern "C" void PortSaveClearOverwrite() { memset(approvedName, 0, sizeof(approvedName)); }

extern "C" unsigned PortSaveApproveOverwrite(const void *requested, const void *existing)
{
	PortSaveClearOverwrite();
	int equal = PortSaveNamesEqual(requested, existing);
	if (equal != 1)
	{
		unsigned error = equal < 0 ? GetLastError() : ERROR_INVALID_NAME;
		SetLastError(error); return error;
	}
	for (unsigned i = 0; i < 32; i++)
	{
		memcpy(&approvedName[i], (const char *)requested + i * 2, 2);
		if (!approvedName[i]) break;
	}
	return 0;
}

extern "C" int PortSaveConsumeOverwrite(const void *requested)
{
	uint16_t previous[32];
	memcpy(previous, approvedName, sizeof(previous));
	PortSaveClearOverwrite();
	return previous[0] && PortSaveNamesEqual(previous, requested) == 1;
}
