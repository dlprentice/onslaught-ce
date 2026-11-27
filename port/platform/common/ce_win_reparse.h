/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CE_WIN_REPARSE_H
#define CE_WIN_REPARSE_H

#ifdef _WIN32
#ifndef INVALID_FILE_ATTRIBUTES
#include <windows.h>
#endif
#include <string.h>

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
