/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CE_WIN_REPARSE_H
#define CE_WIN_REPARSE_H

#ifdef _WIN32
#ifndef INVALID_FILE_ATTRIBUTES
#include <windows.h>
#endif
#include <string.h>

#ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000u
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000u
#endif

/* Windows 11 OneDrive Files On-Demand (cldflt.sys) tags synced folders and files with non-surrogate
 * IO_REPARSE_TAG_CLOUD..CLOUD_F (0x9000001A..0x9000F01A). All symlinks, junctions, and mount points
 * set the name-surrogate bit (0x20000000) and remain strictly refused. */
static inline int ce_win_reparse_is_cloud_only(DWORD tag)
{
	return (tag & 0x20000000u) == 0 && (tag & ~0x0000F000u) == 0x9000001Au;
}

static inline int ce_win_tag_is_link(DWORD attrs, DWORD reparse_tag)
{
	if (!(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
	return !ce_win_reparse_is_cloud_only(reparse_tag);
}

static inline int ce_win_find_is_link(DWORD attrs, DWORD reserved0)
{
	return ce_win_tag_is_link(attrs, reserved0);
}

#ifndef CE_WIN_REPARSE_NO_HANDLE_EX
static inline int ce_win_handle_is_link(HANDLE h, DWORD attrs)
{
	if (!(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
	FILE_ATTRIBUTE_TAG_INFO tag;
	memset(&tag, 0, sizeof(tag));
	if (GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag)) &&
	    ce_win_reparse_is_cloud_only(tag.ReparseTag))
		return 0;
	return 1;
}

/* True when a verified non-link handle opened with FILE_FLAG_OPEN_REPARSE_POINT / OBJ_DONT_REPARSE
 * is a cloud placeholder (OneDrive Files On-Demand) and must be reopened without the no-reparse flag
 * so cldflt.sys hydrates file data on read/write. Never returns 1 for symlinks/junctions/mount points. */
static inline int ce_win_handle_needs_hydration(HANDLE h, DWORD attrs)
{
	if (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
	{
		FILE_ATTRIBUTE_TAG_INFO tag;
		memset(&tag, 0, sizeof(tag));
		return GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag)) &&
		       ce_win_reparse_is_cloud_only(tag.ReparseTag);
	}
	return (attrs & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN)) != 0;
}

/* Compares two open file handles for the same underlying file identity. Prefers Windows 8+ 128-bit
 * FileIdInfo (class 18) and falls back to 64-bit GetFileInformationByHandle when unsupported (e.g. Wine/FAT). */
static inline int ce_win_same_file_id(HANDLE a, HANDLE b)
{
	struct {
		ULONGLONG VolumeSerialNumber;
		BYTE Identifier[16];
	} fa, fb;
	memset(&fa, 0, sizeof(fa));
	memset(&fb, 0, sizeof(fb));
	BOOL oka = GetFileInformationByHandleEx(a, (FILE_INFO_BY_HANDLE_CLASS)18 /* FileIdInfo */, &fa, sizeof(fa));
	BOOL okb = GetFileInformationByHandleEx(b, (FILE_INFO_BY_HANDLE_CLASS)18 /* FileIdInfo */, &fb, sizeof(fb));
	if (oka && okb)
		return fa.VolumeSerialNumber == fb.VolumeSerialNumber &&
		       memcmp(fa.Identifier, fb.Identifier, sizeof(fa.Identifier)) == 0;
	if (oka != okb)
		return 0;
	BY_HANDLE_FILE_INFORMATION ia, ib;
	memset(&ia, 0, sizeof(ia));
	memset(&ib, 0, sizeof(ib));
	if (!GetFileInformationByHandle(a, &ia) || !GetFileInformationByHandle(b, &ib))
		return 0;
	return ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
	       ia.nFileIndexHigh == ib.nFileIndexHigh &&
	       ia.nFileIndexLow == ib.nFileIndexLow;
}

/* Reopens a verified non-link cloud placeholder file handle WITHOUT FILE_FLAG_OPEN_REPARSE_POINT so
 * cldflt.sys hydrates data. Keeps the original file pinned via either *ph (when share allows concurrent
 * reopen) or a temporary FILE_READ_ATTRIBUTES anchor handle (for exclusive/write opens), verifies
 * identical file ID and non-directory/non-link status on the hydrated handle, and replaces *ph and *info.
 * On failure, closes *ph, sets *ph = INVALID_HANDLE_VALUE, and returns 0.
 * For non-cloud regular handles, returns 1 immediately with zero extra syscalls. */
static inline int ce_win_hydrate_handle_a(const char *path, HANDLE *ph, BY_HANDLE_FILE_INFORMATION *info,
                                          DWORD access, DWORD share, DWORD flags, int exclusive_anchor)
{
	HANDLE probed = *ph;
	if (!ce_win_handle_needs_hydration(probed, info->dwFileAttributes))
		return 1;
	HANDLE anchor = INVALID_HANDLE_VALUE;
	HANDLE pin = probed;
	if (exclusive_anchor)
	{
		anchor = CreateFileA(path, FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (anchor == INVALID_HANDLE_VALUE || !ce_win_same_file_id(probed, anchor))
		{
			if (anchor != INVALID_HANDLE_VALUE) CloseHandle(anchor);
			CloseHandle(probed);
			*ph = INVALID_HANDLE_VALUE;
			return 0;
		}
		CloseHandle(probed);
		*ph = INVALID_HANDLE_VALUE;
		pin = anchor;
	}
	DWORD hflags = flags & ~(DWORD)FILE_FLAG_OPEN_REPARSE_POINT;
	HANDLE hyd = INVALID_HANDLE_VALUE;
	for (int attempt = 0; attempt < 4; ++attempt)
	{
		hyd = CreateFileA(path, access, share, NULL, OPEN_EXISTING, hflags, NULL);
		if (hyd != INVALID_HANDLE_VALUE) break;
		DWORD err = GetLastError();
		if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED) break;
		if (attempt + 1 < 4) Sleep(25);
	}
	BY_HANDLE_FILE_INFORMATION hinfo;
	memset(&hinfo, 0, sizeof(hinfo));
	int ok = hyd != INVALID_HANDLE_VALUE &&
	         GetFileType(hyd) == FILE_TYPE_DISK &&
	         GetFileInformationByHandle(hyd, &hinfo) &&
	         !(hinfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
	         !ce_win_handle_is_link(hyd, hinfo.dwFileAttributes) &&
	         ce_win_same_file_id(pin, hyd);
	CloseHandle(pin);
	if (!ok)
	{
		if (hyd != INVALID_HANDLE_VALUE) CloseHandle(hyd);
		*ph = INVALID_HANDLE_VALUE;
		return 0;
	}
	*ph = hyd;
	*info = hinfo;
	return 1;
}

static inline int ce_win_hydrate_handle_w(const WCHAR *path, HANDLE *ph, BY_HANDLE_FILE_INFORMATION *info,
                                          DWORD access, DWORD share, DWORD flags, int exclusive_anchor)
{
	HANDLE probed = *ph;
	if (!ce_win_handle_needs_hydration(probed, info->dwFileAttributes))
		return 1;
	HANDLE anchor = INVALID_HANDLE_VALUE;
	HANDLE pin = probed;
	if (exclusive_anchor)
	{
		anchor = CreateFileW(path, FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (anchor == INVALID_HANDLE_VALUE || !ce_win_same_file_id(probed, anchor))
		{
			if (anchor != INVALID_HANDLE_VALUE) CloseHandle(anchor);
			CloseHandle(probed);
			*ph = INVALID_HANDLE_VALUE;
			return 0;
		}
		CloseHandle(probed);
		*ph = INVALID_HANDLE_VALUE;
		pin = anchor;
	}
	DWORD hflags = flags & ~(DWORD)FILE_FLAG_OPEN_REPARSE_POINT;
	HANDLE hyd = INVALID_HANDLE_VALUE;
	for (int attempt = 0; attempt < 4; ++attempt)
	{
		hyd = CreateFileW(path, access, share, NULL, OPEN_EXISTING, hflags, NULL);
		if (hyd != INVALID_HANDLE_VALUE) break;
		DWORD err = GetLastError();
		if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED) break;
		if (attempt + 1 < 4) Sleep(25);
	}
	BY_HANDLE_FILE_INFORMATION hinfo;
	memset(&hinfo, 0, sizeof(hinfo));
	int ok = hyd != INVALID_HANDLE_VALUE &&
	         GetFileType(hyd) == FILE_TYPE_DISK &&
	         GetFileInformationByHandle(hyd, &hinfo) &&
	         !(hinfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
	         !ce_win_handle_is_link(hyd, hinfo.dwFileAttributes) &&
	         ce_win_same_file_id(pin, hyd);
	CloseHandle(pin);
	if (!ok)
	{
		if (hyd != INVALID_HANDLE_VALUE) CloseHandle(hyd);
		*ph = INVALID_HANDLE_VALUE;
		return 0;
	}
	*ph = hyd;
	*info = hinfo;
	return 1;
}

static inline int ce_win_path_is_link_w(const WCHAR *wp, DWORD attrs)
{
	if (attrs == INVALID_FILE_ATTRIBUTES) return 1;
	if (!(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
	HANDLE h = INVALID_HANDLE_VALUE;
	for (int attempt = 0; attempt < 4; ++attempt)
	{
		h = CreateFileW(wp, FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (h != INVALID_HANDLE_VALUE) break;
		DWORD err = GetLastError();
		if (err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION) break;
		if (attempt + 1 < 4) Sleep(20);
	}
	if (h == INVALID_HANDLE_VALUE) return 1;
	int is_link = ce_win_handle_is_link(h, attrs);
	CloseHandle(h);
	return is_link;
}

static inline int ce_win_path_is_link_a(const char *path, DWORD attrs)
{
	if (attrs == INVALID_FILE_ATTRIBUTES) return 1;
	if (!(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
	HANDLE h = INVALID_HANDLE_VALUE;
	for (int attempt = 0; attempt < 4; ++attempt)
	{
		h = CreateFileA(path, FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (h != INVALID_HANDLE_VALUE) break;
		DWORD err = GetLastError();
		if (err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION) break;
		if (attempt + 1 < 4) Sleep(20);
	}
	if (h == INVALID_HANDLE_VALUE) return 1;
	int is_link = ce_win_handle_is_link(h, attrs);
	CloseHandle(h);
	return is_link;
}
#endif
#endif

#endif
