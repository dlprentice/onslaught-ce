// SPDX-License-Identifier: GPL-3.0-only
#ifndef PORT_SAVE_NAMES_H
#define PORT_SAVE_NAMES_H

// The savecard seam's strings are explicit 16-bit UTF-16 units, independent of the host wchar_t ABI.
// Zero means success; failures are the Win32 error values INVALID_NAME (123), FILENAME_EXCED_RANGE (206),
// or NO_UNICODE_TRANSLATION (1113). Output is empty on failure, never a shortened/replacement name.
// The limit is 31 UTF-16 units: loading a career copies its name into the 32-unit virtual keyboard buffer.
extern "C" unsigned PortSavePathUtf8(const void *name16, char *path, unsigned capacity);
// filename is a basename including .bes. A null destination/capacity zero validates a listing entry without copying.
extern "C" unsigned PortSaveNameUtf16(const char *filename, void *name16, unsigned capacity);

// Windows save namespace only. Equality is ordinal, ignoring case through the OS uppercase table, never linguistic
// normalization. -1 is a refused/failed comparison; 0 differs; 1 is equal. Global CRT comparisons stay unchanged.
extern "C" int PortSaveNamesEqual(const void *a16, const void *b16);
extern "C" unsigned PortSaveLookupUtf16(const void *name16, int *index);
extern "C" unsigned PortSaveReserveUtf8(const char *path);
// A synchronous overwrite YES retry gets one bounded, name-keyed approval. Every consume clears it first.
extern "C" unsigned PortSaveApproveOverwrite(const void *requested16, const void *existing16);
extern "C" int PortSaveConsumeOverwrite(const void *requested16);
extern "C" void PortSaveClearOverwrite();

#endif
