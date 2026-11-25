// SPDX-License-Identifier: GPL-3.0-only
// port/platform/windows/ce_career_publish.cpp: Windows external .bes picker/reader, filename
// proposal, and handle-relative non-destructive publish-as-new behind CeCareerTakeFullSet.
#include "ce_backup_paths.h"
#include "../common/ce_backup_name.h"
#include "../common/ce_backup_preview.h"
#include "../common/ce_career_publish.h"
#include "../common/ce_career_restore.h"
#include "../../game/ce_career_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <utility>

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800u
#endif

namespace
{
using namespace ce_backup_paths;

constexpr size_t kDirectoryEntryLimit = 2048;
constexpr size_t kCareerLimit = 512;
const wchar_t *const kLockWide = L".ce-career-custody.lock";

constexpr NTSTATUS kStatusInvalidDeviceRequest = static_cast<NTSTATUS>(0xc0000010u);
constexpr NTSTATUS kStatusNotFound = static_cast<NTSTATUS>(0xc0000034u);
constexpr NTSTATUS kStatusCollision = static_cast<NTSTATUS>(0xc0000035u);
constexpr NTSTATUS kStatusPathNotFound = static_cast<NTSTATUS>(0xc000003au);
constexpr NTSTATUS kStatusNotSupported = static_cast<NTSTATUS>(0xc00000bbu);

struct Failure
{
	CeCareerPublishStatus	status;
};

void	require(bool ok, CeCareerPublishStatus status = CE_PUBLISH_REFUSED)
{
	if (!ok)
		throw Failure{status};
}

void	copyText(char *dst, size_t cap, const char *src)
{
	if (!dst || !cap)
		return;
	std::snprintf(dst, cap, "%s", src ? src : "");
}

std::string	trimmedPath(const char *path)
{
	std::string out = path ? path : "";
	while (out.size() > 1 && (out.back() == '\\' || out.back() == '/') &&
		   !(out.size() == 3 && out[1] == ':'))
		out.pop_back();
	return out;
}

std::string	lowerAscii(std::string s)
{
	for (char &c : s)
		if (c >= 'A' && c <= 'Z')
			c = char(c + ('a' - 'A'));
	return s;
}

bool	hasBesExt(const std::string &s)
{
	return s.size() > 4 && lowerAscii(s.substr(s.size() - 4)) == ".bes";
}

bool	wideHasBesExt(const Wide &s)
{
	if (s.size() <= 4)
		return false;
	return CompareStringOrdinal(s.data() + (s.size() - 4), 4, L".bes", 4, TRUE) == CSTR_EQUAL;
}

bool	reservedDevice(const std::string &stem)
{
	std::string head = lowerAscii(stem);
	const size_t dot = head.find('.');
	if (dot != std::string::npos)
		head.resize(dot);
	while (!head.empty() && head.back() == ' ')
		head.pop_back();
	if (head == "con" || head == "prn" || head == "aux" || head == "nul" ||
		head == "conin$" || head == "conout$")
		return true;
	return head.size() == 4 &&
		   (head.substr(0, 3) == "com" || head.substr(0, 3) == "lpt") &&
		   head[3] >= '0' && head[3] <= '9';
}

bool	portableCareerName(const std::string &s)
{
	if (!hasBesExt(s) || s.size() > 35 || s.front() == ' ' ||
		s[s.size() - 5] == ' ' || s[s.size() - 5] == '.')
		return false;
	if (CeBackupFileHasDirectionMark(s.c_str(), s.size()))
		return false;
	for (unsigned char c : s)
		if (c < 32 || c >= 127 || std::strchr("<>:\"/\\|?*", c))
			return false;
	return !reservedDevice(s.substr(0, s.size() - 4));
}

bool	validWindowsFilePath(const char *path, size_t maxLen)
{
	if (!path || !path[0] || strnlen(path, maxLen) >= maxLen)
		return false;
	const bool drive = (path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z');
	return drive && path[1] == ':' && (path[2] == '\\' || path[2] == '/');
}

void	closeNow(Handle &h)
{
	if (h.h != INVALID_HANDLE_VALUE)
	{
		if (h.discard)
		{
			FILE_DISPOSITION_INFO d{TRUE};
			SetFileInformationByHandle(h.h, FileDispositionInfo, &d, sizeof(d));
		}
		CloseHandle(h.h);
		h.h = INVALID_HANDLE_VALUE;
		h.discard = false;
	}
}

void	assignHandle(Handle &dst, Handle &&src)
{
	closeNow(dst);
	dst.h = src.h;
	dst.discard = src.discard;
	src.h = INVALID_HANDLE_VALUE;
	src.discard = false;
}

decltype(&NtCreateFile)	ntCreateFileFn()
{
	static const auto fn = reinterpret_cast<decltype(&NtCreateFile)>(
		GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
	return fn;
}

decltype(&NtSetInformationFile)	ntSetInformationFileFn()
{
	static const auto fn = reinterpret_cast<decltype(&NtSetInformationFile)>(
		GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile"));
	return fn;
}

Handle	openChildRaw(HANDLE parent, const Wide &name, ACCESS_MASK access,
					 ULONG fileAttributes, ULONG shareAccess, ULONG disposition,
					 ULONG options, NTSTATUS *statusOut = nullptr)
{
	const auto nt = ntCreateFileFn();
	if (!nt || name.empty() || name.size() > 255)
	{
		if (statusOut)
			*statusOut = static_cast<NTSTATUS>(0xc000000du);
		return Handle();
	}
	UNICODE_STRING leaf{};
	leaf.Length = leaf.MaximumLength = static_cast<USHORT>(name.size() * sizeof(wchar_t));
	leaf.Buffer = const_cast<wchar_t *>(name.data());
	OBJECT_ATTRIBUTES oa{};
	InitializeObjectAttributes(&oa, &leaf, OBJ_CASE_INSENSITIVE, parent, nullptr);
	IO_STATUS_BLOCK io{};
	HANDLE h = INVALID_HANDLE_VALUE;
	const NTSTATUS st = nt(&h, access, &oa, &io, nullptr, fileAttributes,
						   shareAccess, disposition, options, nullptr, 0);
	if (statusOut)
		*statusOut = st;
	return Handle(st == 0 ? h : INVALID_HANDLE_VALUE);
}

bool	fillRandom(unsigned char *buf, size_t len)
{
	using BCryptGenRandomFn = NTSTATUS(WINAPI *)(PVOID, PUCHAR, ULONG, ULONG);
	using RtlGenRandomFn = BOOLEAN(WINAPI *)(PVOID, ULONG);
	static const BCryptGenRandomFn bcrypt = []() -> BCryptGenRandomFn
	{
		HMODULE mod = LoadLibraryExW(L"bcrypt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		return mod ? reinterpret_cast<BCryptGenRandomFn>(GetProcAddress(mod, "BCryptGenRandom")) : nullptr;
	}();
	if (bcrypt)
		return bcrypt(nullptr, buf, static_cast<ULONG>(len), 0x00000002u) == 0;
	static const RtlGenRandomFn rtl = []() -> RtlGenRandomFn
	{
		HMODULE mod = LoadLibraryExW(L"advapi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		return mod ? reinterpret_cast<RtlGenRandomFn>(GetProcAddress(mod, "SystemFunction036")) : nullptr;
	}();
	if (rtl)
		return rtl(buf, static_cast<ULONG>(len)) != FALSE;
	return false;
}

std::string	randomDigits(size_t count)
{
	std::string out;
	out.reserve(count);
	while (out.size() < count)
	{
		unsigned char b[64];
		require(fillRandom(b, sizeof(b)));
		for (size_t i = 0; i < sizeof(b) && out.size() < count; ++i)
			if (b[i] < 250)
				out.push_back(char('0' + (b[i] % 10)));
	}
	return out;
}

bool	isLocalNtfs(HANDLE h)
{
	wchar_t fsName[64]{};
	DWORD flags = 0;
	if (!GetVolumeInformationByHandleW(h, nullptr, 0, nullptr, nullptr, &flags, fsName, 64))
		return false;
	return _wcsicmp(fsName, L"NTFS") == 0;
}

bool	rootsDisjoint(const Directory &user, const Directory &game,
					  const Directory &package, const Directory &backupRoot)
{
	return outside(backupRoot, user, game, package) &&
		   !user.below(backupRoot) && !game.below(backupRoot) && !package.below(backupRoot) &&
		   !user.below(game) && !game.below(user) && !user.differentRootOnSameVolume(game);
}

void	boundDir(const Directory &held, const std::string &path, CeCareerPublishStatus failStatus)
{
	Directory again;
	require(again.open(path.c_str()) && sameId(held.ids.back(), again.ids.back()), failStatus);
}

struct OrdinalLess
{
	bool	operator()(const Wide &a, const Wide &b) const
	{
		return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
									b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
	}
};

using WideNames = std::set<Wide, OrdinalLess>;

struct FileStamp
{
	FILE_ID_INFO	id{};
	LONGLONG		size = 0;
	LONGLONG		written = 0;
	LONGLONG		changed = 0;
	DWORD			links = 0;
};

struct DirStamp
{
	FILE_ID_INFO	id{};
	LONGLONG		written = 0;
	LONGLONG		changed = 0;
	DWORD			links = 0;
};

bool	readFileStamp(HANDLE h, const Wide &leaf, FileStamp &s, bool allowHidden = false)
{
	FILE_BASIC_INFO basic{};
	FILE_STANDARD_INFO standard{};
	const DWORD forbidden = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY |
							FILE_ATTRIBUTE_SYSTEM | (allowHidden ? 0u : FILE_ATTRIBUTE_HIDDEN);
	if (GetFileType(h) != FILE_TYPE_DISK ||
		!GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic)) ||
		(basic.FileAttributes & forbidden) ||
		!GetFileInformationByHandleEx(h, FileStandardInfo, &standard, sizeof(standard)) ||
		standard.Directory || standard.DeletePending ||
		standard.NumberOfLinks != 1 || standard.EndOfFile.QuadPart < 0 ||
		!GetFileInformationByHandleEx(h, FileIdInfo, &s.id, sizeof(s.id)) ||
		!physicalLeaf(h, leaf))
		return false;
	s.size = standard.EndOfFile.QuadPart;
	s.written = basic.LastWriteTime.QuadPart;
	s.changed = basic.ChangeTime.QuadPart;
	s.links = standard.NumberOfLinks;
	return true;
}

bool	readDirStamp(HANDLE h, DirStamp &s)
{
	FILE_BASIC_INFO basic{};
	FILE_STANDARD_INFO standard{};
	if (!dirInfo(h, s.id) ||
		!GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic)) ||
		!(basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
		(basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
		!GetFileInformationByHandleEx(h, FileStandardInfo, &standard, sizeof(standard)) ||
		!standard.Directory || standard.DeletePending)
		return false;
	s.written = basic.LastWriteTime.QuadPart;
	s.changed = basic.ChangeTime.QuadPart;
	s.links = standard.NumberOfLinks;
	return true;
}

bool	sameStamp(const FileStamp &a, const FileStamp &b)
{
	return sameId(a.id, b.id) && a.size == b.size && a.links == b.links &&
		   a.written == b.written && a.changed == b.changed;
}

bool	sameDirStamp(const DirStamp &a, const DirStamp &b)
{
	return sameId(a.id, b.id) && a.links == b.links && a.written == b.written && a.changed == b.changed;
}

bool	existsEntry(HANDLE parent, const Wide &name)
{
	if (parent == INVALID_HANDLE_VALUE)
		return false;
	NTSTATUS st = 0;
	Handle probe = openChildRaw(
		parent, name, FILE_READ_ATTRIBUTES | SYNCHRONIZE, 0,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
		FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, &st);
	if (probe)
		return true;
	require(st == kStatusNotFound || st == kStatusPathNotFound);
	return false;
}

bool	savegamesStillAbsent(HANDLE parent)
{
	NTSTATUS st = 0;
	Handle probe = openChildRaw(
		parent, L"savegames", FILE_READ_ATTRIBUTES | SYNCHRONIZE, 0,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
		FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, &st);
	return !probe && (st == kStatusNotFound || st == kStatusPathNotFound);
}

void	openOptionalSavegames(HANDLE parent, Handle &out, FILE_ID_INFO &idOut, bool &present)
{
	closeNow(out);
	idOut = {};
	NTSTATUS st = 0;
	Handle h = child(parent, L"savegames", true, false, &st);
	if (h)
	{
		require(dirInfo(h.h, idOut));
		assignHandle(out, std::move(h));
		present = true;
		return;
	}
	require((st == kStatusNotFound || st == kStatusPathNotFound) && savegamesStillAbsent(parent));
	present = false;
}

template <class Visit>
bool	scanEntries(HANDLE dir, Visit visit)
{
	alignas(FILE_ID_BOTH_DIR_INFO) unsigned char buf[16384];
	auto kind = FileIdBothDirectoryRestartInfo;
	size_t count = 0;
	while (GetFileInformationByHandleEx(dir, kind, buf, sizeof(buf)))
	{
		kind = FileIdBothDirectoryInfo;
		for (size_t pos = 0;;)
		{
			constexpr size_t head = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
			if (pos > sizeof(buf) - head)
				return false;
			const auto *e = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO *>(buf + pos);
			const size_t len = e->FileNameLength;
			if (++count > kDirectoryEntryLimit || !len || (len % sizeof(wchar_t)) != 0 ||
				len > sizeof(buf) - pos - head)
				return false;
			Wide leaf(e->FileName, len / sizeof(wchar_t));
			if (leaf != L"." && leaf != L".." && !visit(leaf, e->FileAttributes))
				return false;
			if (!e->NextEntryOffset)
				break;
			if (e->NextEntryOffset < head + len ||
				(e->NextEntryOffset % alignof(FILE_ID_BOTH_DIR_INFO)) != 0 ||
				e->NextEntryOffset > sizeof(buf) - pos)
				return false;
			pos += e->NextEntryOffset;
		}
	}
	return GetLastError() == ERROR_NO_MORE_FILES;
}

bool	collectSlotNames(HANDLE dir, bool allowMissing, WideNames &out)
{
	NTSTATUS st = 0;
	Handle folder = child(dir, L"savegames", true, false, &st);
	if (!folder)
		return allowMissing && (st == kStatusNotFound || st == kStatusPathNotFound) && savegamesStillAbsent(dir);
	DirStamp before{};
	if (!readDirStamp(folder.h, before))
		return false;
	WideNames inDir;
	size_t careers = 0;
	const bool ok = scanEntries(folder.h, [&](const Wide &leaf, DWORD attrs)
	{
		if (!wideHasBesExt(leaf))
			return true;
		if (++careers > kCareerLimit)
			return false;
		if (attrs & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY |
					 FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
			return false;
		std::string utfName;
		if (!utf8(leaf, utfName))
			return false;
		Handle entry = openChildRaw(
			folder.h, leaf, FILE_READ_ATTRIBUTES | SYNCHRONIZE, 0,
			FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
			FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
		FileStamp fs{};
		if (!entry || !readFileStamp(entry.h, leaf, fs))
			return false;
		if (!inDir.insert(leaf).second)
			return false;
		out.insert(leaf);
		return true;
	});
	DirStamp after{};
	return ok && readDirStamp(folder.h, after) && sameDirStamp(before, after);
}

struct LockGuard
{
	Handle		lock;
	FileStamp	stamp{};
	bool		locked = false;

	void	acquire(HANDLE userDir)
	{
		Handle h = openChildRaw(
			userDir, kLockWide,
			GENERIC_READ | GENERIC_WRITE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
			FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF,
			FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
		require(bool(h));
		require(readFileStamp(h.h, kLockWide, stamp, true));
		OVERLAPPED ov{};
		if (!LockFileEx(h.h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov))
			throw Failure{GetLastError() == ERROR_LOCK_VIOLATION ? CE_PUBLISH_BUSY : CE_PUBLISH_REFUSED};
		assignHandle(lock, std::move(h));
		locked = true;
	}

	~LockGuard()
	{
		if (locked && lock)
		{
			OVERLAPPED ov{};
			UnlockFileEx(lock.h, 0, 1, 0, &ov);
		}
	}

	LockGuard() = default;
	LockGuard(const LockGuard &) = delete;
	LockGuard &operator=(const LockGuard &) = delete;
};

bool	sameBytes(HANDLE h, const unsigned char *data, size_t n)
{
	LARGE_INTEGER zero{}, len{};
	if (!GetFileSizeEx(h, &len) || len.QuadPart != static_cast<LONGLONG>(n) ||
		!SetFilePointerEx(h, zero, nullptr, FILE_BEGIN))
		return false;
	unsigned char buf[4096];
	for (size_t off = 0; off < n;)
	{
		DWORD got = 0;
		const DWORD ask = static_cast<DWORD>(std::min(n - off, sizeof(buf)));
		if (!ReadFile(h, buf, ask, &got, nullptr) || got != ask || std::memcmp(data + off, buf, got) != 0)
			return false;
		off += got;
	}
	unsigned char tail = 0;
	DWORD got = 0;
	return ReadFile(h, &tail, 1, &got, nullptr) && got == 0;
}

bool	writeVerified(HANDLE h, const unsigned char *data, size_t n)
{
	size_t off = 0;
	while (off < n)
	{
		DWORD put = 0;
		const DWORD ask = static_cast<DWORD>(n - off);
		if (!WriteFile(h, data + off, ask, &put, nullptr) || put == 0)
			return false;
		off += put;
	}
	return FlushFileBuffers(h) && sameBytes(h, data, n);
}

NTSTATUS	renameNoReplace(HANDLE file, HANDLE parentDir, const Wide &target)
{
	const auto nt = ntSetInformationFileFn();
	if (!nt || target.empty() || target.size() > 255)
		return static_cast<NTSTATUS>(0xc000000du);
	const ULONG bytes = static_cast<ULONG>(target.size() * sizeof(wchar_t));
	const ULONG total = static_cast<ULONG>(
		std::max<size_t>(sizeof(FILE_RENAME_INFORMATION),
						 offsetof(FILE_RENAME_INFORMATION, FileName) + bytes));
	alignas(FILE_RENAME_INFORMATION) unsigned char data[sizeof(FILE_RENAME_INFORMATION) + 256 * sizeof(wchar_t)]{};
	auto *rename = reinterpret_cast<FILE_RENAME_INFORMATION *>(data);
	rename->ReplaceIfExists = FALSE;
	rename->RootDirectory = parentDir;
	rename->FileNameLength = bytes;
	std::memcpy(rename->FileName, target.data(), bytes);
	IO_STATUS_BLOCK io{};
	return nt(file, &io, rename, total, FileRenameInformation);
}

std::string	cleanBaseStem(const char *sourceName, const char *fallback)
{
	std::string raw = sourceName ? sourceName : "";
	if (hasBesExt(raw))
		raw.resize(raw.size() - 4);
	std::string out;
	out.reserve(raw.size());
	for (unsigned char c : raw)
	{
		if (c < 32 || c >= 127 || std::strchr("<>:\"/\\|?*", c))
			continue;
		if (c == ' ' && (out.empty() || out.back() == ' '))
			continue;
		out.push_back(char(c));
	}
	while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
		out.pop_back();
	if (out.empty() || reservedDevice(out))
		out = fallback;
	return out;
}

struct SuffixBase
{
	std::string	stem;
	unsigned	next = 1;
};

SuffixBase	stripCopySuffix(const std::string &stem, const std::string &tag)
{
	SuffixBase out{stem, 1};
	const std::string marker = " " + tag;
	std::string folded = lowerAscii(out.stem);
	size_t pos = folded.rfind(marker);
	if (pos == std::string::npos || pos == 0)
		return out;
	std::string tail = folded.substr(pos + marker.size());
	unsigned previous = 1;
	if (!tail.empty())
	{
		if (tail[0] != ' ' || tail.size() < 2 || tail.size() > 5)
			return out;
		previous = 0;
		for (size_t i = 1; i < tail.size(); ++i)
		{
			if (tail[i] < '0' || tail[i] > '9')
				return out;
			previous = previous * 10u + unsigned(tail[i] - '0');
		}
		if (previous < 2 || previous > 9999)
			return out;
	}
	std::string trimmed = out.stem.substr(0, pos);
	while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '.'))
		trimmed.pop_back();
	if (trimmed.empty())
		return out;
	out.stem = trimmed;
	out.next = previous + 1;
	return out;
}

std::string	formatCandidate(const std::string &baseStem, const char *tag, unsigned number)
{
	std::string suffix;
	if (tag && *tag)
	{
		suffix = std::string(" ") + tag;
		if (number > 1)
			suffix += " " + std::to_string(number);
	}
	if (suffix.size() >= 31)
		return {};
	std::string stem = baseStem.substr(0, 31 - suffix.size());
	while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.'))
		stem.pop_back();
	if (stem.empty())
		return {};
	std::string full = stem + suffix + ".bes";
	return portableCareerName(full) ? full : std::string();
}

CeCareerPublishStatus	mapStorageStatus(CeCareerStorageStatus s)
{
	switch (s)
	{
	case CeCareerStorageStatus::Complete:		return CE_PUBLISH_COMPLETE;
	case CeCareerStorageStatus::Refused:		return CE_PUBLISH_REFUSED;
	case CeCareerStorageStatus::Busy:			return CE_PUBLISH_BUSY;
	case CeCareerStorageStatus::Cancelled:		return CE_PUBLISH_CANCELLED;
	case CeCareerStorageStatus::Changed:		return CE_PUBLISH_CHANGED;
	case CeCareerStorageStatus::Occupied:		return CE_PUBLISH_OCCUPIED;
	case CeCareerStorageStatus::Uncertain:		return CE_PUBLISH_UNCERTAIN;
	case CeCareerStorageStatus::Unsupported:	return CE_PUBLISH_UNSUPPORTED;
	}
	return CE_PUBLISH_REFUSED;
}

// ---- Async external .bes picker state ----
struct PickerState
{
	std::mutex			lock;
	CeImportPickStatus	view{};
	unsigned long		generation = 0;
	bool				open = false;
};

PickerState	&picker()
{
	static PickerState *s = new PickerState;
	return *s;
}

struct PickerTicket
{
	unsigned long	generation;
};

static const SDL_DialogFileFilter	kBesFilters[] = {{"Career Files", "bes"}};

void	setPickNote(CeImportPickStatus &v, CeImportPickState state, const char *msg)
{
	v.state = state;
	copyText(v.message, sizeof(v.message), msg);
}

void SDLCALL	onFilePicked(void *context, const char *const *files, int)
{
	const unsigned long gen = static_cast<PickerTicket *>(context)->generation;
	delete static_cast<PickerTicket *>(context);
	PickerState &s = picker();
	std::lock_guard<std::mutex> hold(s.lock);
	if (!s.open || s.generation != gen || s.view.state != CE_IMPORT_PICK_OPEN)
		return;
	if (!files)
	{
		setPickNote(s.view, CE_IMPORT_PICK_ERROR, "The file chooser could not open.");
		return;
	}
	if (!files[0])
	{
		setPickNote(s.view, CE_IMPORT_PICK_CANCELLED, "No file chosen.");
		return;
	}
	if (files[1] || !validWindowsFilePath(files[0], sizeof(s.view.path)))
	{
		setPickNote(s.view, CE_IMPORT_PICK_ERROR, "Choose one local career file.");
		return;
	}
	const char *leaf = files[0];
	for (const char *p = files[0]; *p; ++p)
		if (*p == '\\' || *p == '/')
			leaf = p + 1;
	if (!leaf[0] || strnlen(leaf, sizeof(s.view.leaf)) >= sizeof(s.view.leaf))
	{
		setPickNote(s.view, CE_IMPORT_PICK_ERROR, "Choose one local career file.");
		return;
	}
	copyText(s.view.path, sizeof(s.view.path), files[0]);
	copyText(s.view.leaf, sizeof(s.view.leaf), leaf);
	setPickNote(s.view, CE_IMPORT_PICK_READY, "Checking the selected career file.");
}
} // namespace

void	CeCareerImportPickEnter()
{
	PickerState &s = picker();
	std::lock_guard<std::mutex> hold(s.lock);
	s.open = true;
	++s.generation;
	s.view = {};
}

void	CeCareerImportPickLeave()
{
	PickerState &s = picker();
	std::lock_guard<std::mutex> hold(s.lock);
	s.open = false;
	++s.generation;
	s.view.state = CE_IMPORT_PICK_IDLE;
}

void	CeCareerImportPickOpen()
{
	PickerState &s = picker();
	PickerTicket *ticket = nullptr;
	{
		std::lock_guard<std::mutex> hold(s.lock);
		if (!s.open || s.view.state == CE_IMPORT_PICK_OPEN)
			return;
		ticket = new (std::nothrow) PickerTicket{s.generation};
		if (!ticket)
		{
			setPickNote(s.view, CE_IMPORT_PICK_ERROR, "There is not enough memory.");
			return;
		}
		s.view.path[0] = 0;
		s.view.leaf[0] = 0;
		setPickNote(s.view, CE_IMPORT_PICK_OPEN, "Waiting for a career file.");
	}
	SDL_ShowOpenFileDialog(onFilePicked, ticket, SDL_GetKeyboardFocus(), kBesFilters, 1, nullptr, false);
}

void	CeCareerImportPickPoll(CeImportPickStatus *out)
{
	if (!out)
		return;
	PickerState &s = picker();
	std::lock_guard<std::mutex> hold(s.lock);
	*out = s.view;
}

void	CeCareerPublishPackageDir(char *out, size_t cap)
{
	if (!out || !cap)
		return;
	const std::string pkg = trimmedPath(SDL_GetBasePath());
	copyText(out, cap, pkg.c_str());
}

CeImportReadStatus	CeCareerImportRead(const char *filePath, const char *userDir, const char *gameDir,
									   const char *packageDir, unsigned char *bytesOut, size_t bytesCap,
									   char *leafOut, size_t leafCap, char *errorOut, size_t errorCap)
{
	if (leafOut && leafCap)
		leafOut[0] = 0;
	copyText(errorOut, errorCap, "This career file could not be read.");
	if (!bytesOut || bytesCap < CE_CAREER_SIZE || !validWindowsFilePath(filePath, 4096))
	{
		copyText(errorOut, errorCap, "Choose a local career file outside your game and saves.");
		return CE_IMPORT_READ_BAD_PATH;
	}
	std::string full(filePath);
	std::replace(full.begin(), full.end(), '/', '\\');
	const size_t slash = full.rfind('\\');
	if (slash == std::string::npos || slash + 1 >= full.size())
	{
		copyText(errorOut, errorCap, "Choose a local career file outside your game and saves.");
		return CE_IMPORT_READ_BAD_PATH;
	}
	std::string parentPath = slash == 2 ? full.substr(0, 3) : full.substr(0, slash);
	std::string leaf = full.substr(slash + 1);
	Wide wideLeaf;
	if (!hasBesExt(leaf) || CeBackupFileHasDirectionMark(leaf.c_str(), leaf.size()) ||
		!wide(leaf.c_str(), wideLeaf) || !component(wideLeaf))
	{
		copyText(errorOut, errorCap, "Choose a valid .bes career file.");
		return CE_IMPORT_READ_BAD_PATH;
	}

	const std::string pkg = (packageDir && *packageDir) ? trimmedPath(packageDir) : trimmedPath(SDL_GetBasePath());
	Directory parent, user, game, package;
	if (!parent.open(parentPath.c_str()) || !user.open(trimmedPath(userDir).c_str()) ||
		!game.open(trimmedPath(gameDir).c_str()) || !package.open(pkg.c_str()) ||
		!outside(parent, user, game, package))
	{
		copyText(errorOut, errorCap, "Choose a local career file outside your game, package and saves.");
		return CE_IMPORT_READ_BAD_PATH;
	}

	Handle file = child(parent.h(), wideLeaf, false, false);
	if (!file)
	{
		copyText(errorOut, errorCap, "The selected file could not be opened.");
		return CE_IMPORT_READ_IO_ERROR;
	}
	FileStamp initial{};
	if (!readFileStamp(file.h, wideLeaf, initial))
	{
		copyText(errorOut, errorCap, "Choose a regular, unlinked career file.");
		return CE_IMPORT_READ_UNSAFE_FILE;
	}
	if (initial.size != CE_CAREER_SIZE)
	{
		copyText(errorOut, errorCap, "This file is not a valid 10,004-byte career file.");
		return CE_IMPORT_READ_BAD_SIZE;
	}

	unsigned char buf[CE_CAREER_SIZE];
	for (size_t pos = 0; pos < CE_CAREER_SIZE;)
	{
		DWORD got = 0;
		const DWORD ask = static_cast<DWORD>(CE_CAREER_SIZE - pos);
		if (!ReadFile(file.h, buf + pos, ask, &got, nullptr) || got == 0)
		{
			copyText(errorOut, errorCap, "The selected file could not be read completely.");
			return CE_IMPORT_READ_IO_ERROR;
		}
		pos += got;
	}
	unsigned char tail = 0;
	DWORD extra = 0;
	FileStamp held{}, named{};
	Handle again = child(parent.h(), wideLeaf, false, false);
	if (!ReadFile(file.h, &tail, 1, &extra, nullptr) || extra != 0 ||
		!again || !readFileStamp(file.h, wideLeaf, held) || !readFileStamp(again.h, wideLeaf, named) ||
		!sameStamp(initial, held) || !sameStamp(initial, named))
	{
		copyText(errorOut, errorCap, "The selected file changed while it was being read.");
		return CE_IMPORT_READ_IO_ERROR;
	}

	SCeCareerView view{};
	if (!CeCareerViewBuild(buf, CE_CAREER_SIZE, &view) || !CeCareerViewPlausible(view))
	{
		copyText(errorOut, errorCap, "This file is not a valid Onslaught career.");
		return CE_IMPORT_READ_BAD_CAREER;
	}

	std::memcpy(bytesOut, buf, CE_CAREER_SIZE);
	copyText(leafOut, leafCap, leaf.c_str());
	copyText(errorOut, errorCap, "");
	return CE_IMPORT_READ_OK;
}

CeCareerPublishStatus	CeCareerPublishSuggest(CeCareerPublishKind kind, const char *userDir,
											   const char *gameDir, const char *sourceName,
											   CeCareerPublishNamePermitFn namePermitted, void *owner,
											   char *targetOut, size_t targetCap)
{
	if (targetOut && targetCap)
		targetOut[0] = 0;
	if (!targetOut || targetCap < 36 || !sourceName || !*sourceName || !namePermitted)
		return CE_PUBLISH_BAD_NAME;
	if (CeBackupFileHasDirectionMark(sourceName, strnlen(sourceName, 256)))
		return CE_PUBLISH_BAD_NAME;

	Directory user, game;
	if (!user.open(trimmedPath(userDir).c_str()) || !game.open(trimmedPath(gameDir).c_str()))
		return CE_PUBLISH_REFUSED;

	WideNames taken;
	if (!collectSlotNames(user.h(), true, taken) || !collectSlotNames(game.h(), true, taken))
		return CE_PUBLISH_REFUSED;

	bool sawNameRefusal = false;
	const std::string clean = cleanBaseStem(sourceName, kind == CE_PUBLISH_IMPORT ? "Imported" : "Career");
	auto isTaken = [&](const std::string &cand) -> bool
	{
		Wide w(cand.begin(), cand.end());
		return taken.count(w) != 0;
	};

	if (kind == CE_PUBLISH_IMPORT)
	{
		const std::string direct = formatCandidate(clean, nullptr, 1);
		if (!direct.empty() && !isTaken(direct))
		{
			if (namePermitted(owner, sourceName, direct.c_str()))
			{
				copyText(targetOut, targetCap, direct.c_str());
				return CE_PUBLISH_COMPLETE;
			}
			sawNameRefusal = true;
		}
		const SuffixBase base = stripCopySuffix(clean, "imported");
		for (unsigned n = base.next; n < base.next + 1000 && n <= 9999; ++n)
		{
			const std::string cand = formatCandidate(base.stem, "imported", n);
			if (cand.empty() || isTaken(cand))
				continue;
			if (namePermitted(owner, sourceName, cand.c_str()))
			{
				copyText(targetOut, targetCap, cand.c_str());
				return CE_PUBLISH_COMPLETE;
			}
			sawNameRefusal = true;
		}
		return sawNameRefusal ? CE_PUBLISH_BAD_NAME : CE_PUBLISH_OCCUPIED;
	}

	const SuffixBase base = stripCopySuffix(clean, "edited");
	for (unsigned n = base.next; n < base.next + 1000 && n <= 9999; ++n)
	{
		const std::string cand = formatCandidate(base.stem, "edited", n);
		if (cand.empty() || isTaken(cand))
			continue;
		if (namePermitted(owner, sourceName, cand.c_str()))
		{
			copyText(targetOut, targetCap, cand.c_str());
			return CE_PUBLISH_COMPLETE;
		}
		sawNameRefusal = true;
	}
	return sawNameRefusal ? CE_PUBLISH_BAD_NAME : CE_PUBLISH_OCCUPIED;
}

CeCareerPublishResult	CeCareerPublishNew(const CeCareerPublishRequest *request)
{
	CeCareerPublishResult out{};
	out.status = CE_PUBLISH_REFUSED;
	bool published = false;
	try
	{
		require(request != nullptr);
		require(request->permitted && request->permitted(request->owner), CE_PUBLISH_AUTHORITY);
		require(!(request->cancelled && request->cancelled(request->owner)), CE_PUBLISH_CANCELLED);
		require(request->backupRoot && *request->backupRoot, CE_PUBLISH_NO_FOLDER);
		require(request->bytes && request->size == CE_CAREER_SIZE, CE_PUBLISH_BAD_CAREER);
		require(request->sourceName && *request->sourceName && request->targetName && *request->targetName,
				CE_PUBLISH_BAD_NAME);

		const std::string sourceName = request->sourceName;
		const std::string target = request->targetName;
		Wide targetWide;
		require(portableCareerName(target) && wide(target.c_str(), targetWide) && component(targetWide) &&
					request->namePermitted && request->namePermitted(request->owner, sourceName.c_str(), target.c_str()),
				CE_PUBLISH_BAD_NAME);

		SCeCareerView view{};
		require(CeCareerViewBuild(request->bytes, CE_CAREER_SIZE, &view) && CeCareerViewPlausible(view),
				CE_PUBLISH_BAD_CAREER);

		const std::string userPath = trimmedPath(request->userDir);
		const std::string gamePath = trimmedPath(request->gameDir);
		const std::string pkgPath = (request->packageDir && *request->packageDir)
										? trimmedPath(request->packageDir)
										: trimmedPath(SDL_GetBasePath());
		const std::string rootPath = trimmedPath(request->backupRoot);
		require(!userPath.empty() && !gamePath.empty() && !pkgPath.empty() && !rootPath.empty());

		// Pre-flight validation BEFORE CeCareerTakeFullSet: refuse non-local/non-NTFS storage,
		// overlapping/protected roots, unsafe savegames entries, or occupied names without writing a backup.
		{
			Directory user, game, package, backupRoot;
			require(user.open(userPath.c_str()) && game.open(gamePath.c_str()) &&
					package.open(pkgPath.c_str()) && backupRoot.open(rootPath.c_str()));
			Handle saves, orig;
			FILE_ID_INFO savesId{}, origId{};
			bool savesPresent = false, origPresent = false;
			openOptionalSavegames(user.h(), saves, savesId, savesPresent);
			openOptionalSavegames(game.h(), orig, origId, origPresent);
			require(isLocalNtfs(user.h()) && isLocalNtfs(game.h()) && isLocalNtfs(backupRoot.h()) &&
						(!savesPresent || isLocalNtfs(saves.h)) && (!origPresent || isLocalNtfs(orig.h)),
					CE_PUBLISH_UNSUPPORTED);
			require(rootsDisjoint(user, game, package, backupRoot));
			WideNames taken;
			require(collectSlotNames(user.h(), true, taken) && collectSlotNames(game.h(), true, taken));
			require(taken.count(targetWide) == 0 &&
						!existsEntry(saves.h, targetWide) && !existsEntry(orig.h, targetWide),
					CE_PUBLISH_OCCUPIED);
		}

		// Mandatory verified full-set backup FIRST. If CeCareerTakeFullSet does not return Complete,
		// refuse cleanly with zero career files written.
		CeCareerStorageRequest storageReq;
		storageReq.user = userPath;
		storageReq.game = gamePath;
		storageReq.package = pkgPath;
		storageReq.backupRoot = rootPath;
		storageReq.permitted = request->permitted;
		storageReq.cancelled = request->cancelled;
		storageReq.owner = request->owner;
		const CeCareerStorageResult backupRes = CeCareerTakeFullSet(storageReq);
		if (!backupRes.backupSet.empty())
			copyText(out.backupSet, sizeof(out.backupSet), backupRes.backupSet.c_str());
		if (backupRes.status != CeCareerStorageStatus::Complete)
		{
			out.status = mapStorageStatus(backupRes.status);
			return out;
		}
		require(!backupRes.backupSet.empty() && !backupRes.backupIdentity.empty() &&
					!backupRes.backupStamp.empty(),
				CE_PUBLISH_UNCERTAIN);

		// Re-open all roots and acquire the exclusive custody lock for non-replacing publication.
		Directory user, game, package, backupRoot;
		require(user.open(userPath.c_str()) && game.open(gamePath.c_str()) &&
				package.open(pkgPath.c_str()) && backupRoot.open(rootPath.c_str()));
		Handle saves, orig;
		FILE_ID_INFO savesId{}, origId{};
		bool savesPresent = false, origPresent = false;
		openOptionalSavegames(user.h(), saves, savesId, savesPresent);
		openOptionalSavegames(game.h(), orig, origId, origPresent);
		require(isLocalNtfs(user.h()) && isLocalNtfs(game.h()) && isLocalNtfs(backupRoot.h()) &&
					(!savesPresent || isLocalNtfs(saves.h)) && (!origPresent || isLocalNtfs(orig.h)),
				CE_PUBLISH_UNSUPPORTED);
		require(rootsDisjoint(user, game, package, backupRoot));

		LockGuard lockGuard;
		lockGuard.acquire(user.h());

		auto verifyRoots = [&](CeCareerPublishStatus failStatus)
		{
			boundDir(user, userPath, failStatus);
			boundDir(game, gamePath, failStatus);
			boundDir(package, pkgPath, failStatus);
			boundDir(backupRoot, rootPath, failStatus);
			if (savesPresent)
			{
				Handle a = child(user.h(), L"savegames", true, false);
				FILE_ID_INFO aId{};
				require(bool(a) && dirInfo(a.h, aId) && sameId(aId, savesId), failStatus);
			}
			else
				require(savegamesStillAbsent(user.h()), failStatus);
			if (origPresent)
			{
				Handle b = child(game.h(), L"savegames", true, false);
				FILE_ID_INFO bId{};
				require(bool(b) && dirInfo(b.h, bId) && sameId(bId, origId), failStatus);
			}
			else
				require(savegamesStillAbsent(game.h()), failStatus);
			Handle held = openChildRaw(
				user.h(), kLockWide, FILE_READ_ATTRIBUTES | SYNCHRONIZE, 0,
				FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
				FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
			FileStamp cur{};
			require(bool(held) && readFileStamp(held.h, kLockWide, cur, true) &&
						sameId(cur.id, lockGuard.stamp.id) && cur.links == 1,
					failStatus);
		};

		auto verifyBackup = [&](CeCareerPublishStatus failStatus, bool allowCancel)
		{
			CeCareerRestoreRequest stampReq;
			stampReq.user = userPath;
			stampReq.game = gamePath;
			stampReq.package = pkgPath;
			stampReq.backupRoot = rootPath;
			stampReq.permitted = request->permitted;
			stampReq.cancelled = allowCancel ? request->cancelled : nullptr;
			stampReq.owner = request->owner;
			stampReq.sourceSet = backupRes.backupSet;
			stampReq.sourceIdentity = backupRes.backupIdentity;
			std::string stampNow;
			require(CeCareerRestoreStamp(stampReq, stampNow) && stampNow == backupRes.backupStamp,
					failStatus);
			CePreviewSet checked;
			std::string previewErr;
			require(CePreviewRead(backupRes.backupSet, userPath, gamePath, pkgPath, checked, previewErr) &&
						checked.identity == backupRes.backupIdentity,
					failStatus);
		};

		verifyRoots(CE_PUBLISH_CHANGED);
		verifyBackup(CE_PUBLISH_CHANGED, true);
		require(request->permitted(request->owner), CE_PUBLISH_AUTHORITY);
		require(!(request->cancelled && request->cancelled(request->owner)), CE_PUBLISH_CANCELLED);
		require(request->namePermitted(request->owner, sourceName.c_str(), target.c_str()), CE_PUBLISH_BAD_NAME);
		{
			WideNames preStageTaken;
			require(collectSlotNames(user.h(), true, preStageTaken) &&
					collectSlotNames(game.h(), true, preStageTaken));
			require(preStageTaken.count(targetWide) == 0 &&
						!existsEntry(saves.h, targetWide) && !existsEntry(orig.h, targetWide),
				CE_PUBLISH_OCCUPIED);
		}
		if (!savesPresent)
		{
			NTSTATUS createSt = 0;
			Handle created = openChildRaw(
				user.h(), L"savegames",
				FILE_READ_ATTRIBUTES | SYNCHRONIZE | FILE_LIST_DIRECTORY,
				FILE_ATTRIBUTE_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE,
				FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT | FILE_DIRECTORY_FILE,
				&createSt);
			if (!created)
			{
				if (createSt == kStatusCollision)
					throw Failure{CE_PUBLISH_CHANGED};
				if (createSt == kStatusNotSupported || createSt == kStatusInvalidDeviceRequest)
					throw Failure{CE_PUBLISH_UNSUPPORTED};
				throw Failure{CE_PUBLISH_REFUSED};
			}
			FILE_ID_INFO createdId{};
			require(dirInfo(created.h, createdId) && physicalLeaf(created.h, L"savegames"));
			require(isLocalNtfs(created.h), CE_PUBLISH_UNSUPPORTED);
			closeNow(created);
			Handle reopened = child(user.h(), L"savegames", true, false);
			DirStamp reopenedStamp{};
			require(bool(reopened) && readDirStamp(reopened.h, reopenedStamp) &&
						sameId(reopenedStamp.id, createdId) && physicalLeaf(reopened.h, L"savegames"),
					CE_PUBLISH_CHANGED);
			assignHandle(saves, std::move(reopened));
			savesId = createdId;
			savesPresent = true;
		}

		// Stage into an exclusive (shareAccess = 0) CSPRNG .pending-<18digits>.tmp inside user\savegames.
		Wide stageName;
		Handle stage;
		for (int attempt = 0; attempt < 32 && !stage; ++attempt)
		{
			const std::string nameUtf8 = ".pending-" + randomDigits(18) + ".tmp";
			Wide candName;
			require(wide(nameUtf8.c_str(), candName));
			NTSTATUS st = 0;
			Handle cand = openChildRaw(
				saves.h, candName,
				GENERIC_READ | GENERIC_WRITE | FILE_READ_ATTRIBUTES | DELETE | SYNCHRONIZE,
				FILE_ATTRIBUTE_NORMAL, 0, FILE_CREATE,
				FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
				&st);
			if (cand)
			{
				stageName = std::move(candName);
				assignHandle(stage, std::move(cand));
			}
			else if (st == kStatusNotSupported || st == kStatusInvalidDeviceRequest)
				throw Failure{CE_PUBLISH_UNSUPPORTED};
			else if (st != kStatusCollision)
				throw Failure{CE_PUBLISH_REFUSED};
		}
		require(bool(stage));
		stage.discard = true;

		require(writeVerified(stage.h, request->bytes, CE_CAREER_SIZE));
		FileStamp stageStamp{};
		require(readFileStamp(stage.h, stageName, stageStamp, true));

		WideNames taken;
		require(collectSlotNames(user.h(), false, taken) && collectSlotNames(game.h(), true, taken));
		require(taken.count(targetWide) == 0 &&
					!existsEntry(saves.h, targetWide) && !existsEntry(orig.h, targetWide),
				CE_PUBLISH_OCCUPIED);
		verifyRoots(CE_PUBLISH_CHANGED);
		verifyBackup(CE_PUBLISH_CHANGED, true);
		require(request->permitted(request->owner), CE_PUBLISH_AUTHORITY);
		require(!(request->cancelled && request->cancelled(request->owner)), CE_PUBLISH_CANCELLED);
		require(request->namePermitted(request->owner, sourceName.c_str(), target.c_str()), CE_PUBLISH_BAD_NAME);

		// Non-replacing handle-relative rename via NtSetInformationFile(FileRenameInformation, ReplaceIfExists=FALSE).
		const NTSTATUS renameStatus = renameNoReplace(stage.h, saves.h, targetWide);
		if (renameStatus != 0)
			throw Failure{renameStatus == kStatusCollision ? CE_PUBLISH_OCCUPIED : CE_PUBLISH_REFUSED};

		stage.discard = false;
		published = true;
		copyText(out.newFile, sizeof(out.newFile), target.c_str());
		out.possible = 1;

		require(FlushFileBuffers(stage.h) != FALSE, CE_PUBLISH_UNCERTAIN);
		closeNow(stage);

		{
			Handle finalFile = child(saves.h, targetWide, false, false);
			FileStamp finalStamp{};
			require(bool(finalFile) && readFileStamp(finalFile.h, targetWide, finalStamp) &&
						sameId(finalStamp.id, stageStamp.id) && finalStamp.links == 1 &&
						sameBytes(finalFile.h, request->bytes, CE_CAREER_SIZE),
					CE_PUBLISH_UNCERTAIN);
		}
		verifyBackup(CE_PUBLISH_UNCERTAIN, false);
		verifyRoots(CE_PUBLISH_UNCERTAIN);
		{
			WideNames postUser, postGame;
			require(collectSlotNames(user.h(), false, postUser) &&
						collectSlotNames(game.h(), true, postGame),
					CE_PUBLISH_UNCERTAIN);
			auto it = postUser.find(targetWide);
			require(it != postUser.end() && *it == targetWide &&
						postGame.count(targetWide) == 0 && !existsEntry(orig.h, targetWide),
					CE_PUBLISH_UNCERTAIN);
		}
		require(request->permitted(request->owner), CE_PUBLISH_UNCERTAIN);
		require(request->namePermitted(request->owner, sourceName.c_str(), target.c_str()), CE_PUBLISH_UNCERTAIN);
		out.status = CE_PUBLISH_COMPLETE;
	}
	catch (const Failure &error)
	{
		out.status = published ? CE_PUBLISH_UNCERTAIN : error.status;
	}
	catch (...)
	{
		out.status = published ? CE_PUBLISH_UNCERTAIN : CE_PUBLISH_REFUSED;
	}
	return out;
}
