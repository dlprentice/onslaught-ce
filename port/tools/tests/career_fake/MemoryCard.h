// SPDX-License-Identifier: GPL-3.0-only
// A stand-in for the game's memory card (see common.h here): one career, held in memory, read for reading only.
#ifndef CE_FAKE_MEMORYCARD_H
#define CE_FAKE_MEMORYCARD_H
#include "common.h"
enum { MCE_SUCCESS = 0, MCE_FAIL = 1 };
class CMemoryCard
{
public:
	int	GetNumSaves(int, int *count) { *count = 1; return MCE_SUCCESS; }
	int	GetSaveName(int, int, WCHAR *name) { name[0] = 'A'; name[1] = 0; return MCE_SUCCESS; }
	int	ReadSave(int, int, WCHAR *, unsigned char *bytes, int size, int *read)
	{
		const int n = (mSize < size) ? mSize : size;
		memcpy(bytes, mBytes, size_t(n));
		*read = n;
		return MCE_SUCCESS;
	}
	unsigned char	mBytes[20000];
	int				mSize;
};
extern CMemoryCard MEMORYCARD;
#endif
