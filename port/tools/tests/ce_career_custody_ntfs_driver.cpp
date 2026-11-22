// SPDX-License-Identifier: GPL-3.0-only
// port/tools/tests/ce_career_custody_ntfs_driver.cpp: standalone native Windows NTFS acceptance fixture
// linking the production career custody backend (port/platform/windows/ce_career_restore.cpp,
// port/platform/windows/ce_career_publish.cpp, and port/platform/windows/ce_backup_preview.cpp).
//
// CLI:
//   ce-career-custody-ntfs.exe --root NEW_PRIVATE_DIRECTORY [--case NAME] [--allow-wine]
//
// Without --case, spawns one isolated child process per case. Emits pure ASCII JSONL to stdout and
// ROOT\results.jsonl, and writes ROOT\done.txt (PASS\n or FAIL\n) only after ROOT\results.jsonl closes.
//
// Honest native vs Wine contract:
// - Default mode (no --allow-wine) is for the native Windows NTFS gate: requires a genuine non-Wine
//   Windows host, non-elevated token, active UTF-8 ANSI code page (from utf8.manifest), and a local
//   fixed NTFS volume. Junction escape and normalized case-sensitivity probes MUST execute and pass;
//   symbolic links report UNSUPPORTED_OR_DENIED (exit 3, optional_not_exercised) only if the token lacks
//   SeCreateSymbolicLinkPrivilege and Developer Mode is off.
// - --allow-wine is for host/Wine dry-run verification: all six core custody cases MUST pass; NTFS-only
//   probes whose OS primitives Wine does not implement emit UNSUPPORTED_OR_DENIED (exit 3) and never
//   claim native Windows acceptance.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <winioctl.h>
#include <winternl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "ce_backup_preview.h"
#include "ce_career_publish.h"
#include "ce_career_restore.h"
#include "ce_career_view.h"
#include <SDL3/SDL.h>

// ---- standalone SDL stubs (no SDL3.dll dependency) ----------------------------------------------------------------------
namespace {
std::string gPackageBasePath;
} // namespace

extern "C" {
const char *SDL_GetBasePath(void)
{
	return gPackageBasePath.c_str();
}

SDL_Window *SDL_GetKeyboardFocus(void)
{
	return nullptr;
}

void SDL_ShowOpenFileDialog(SDL_DialogFileCallback, void *, SDL_Window *,
							const SDL_DialogFileFilter *, int, const char *, bool)
{
}
} // extern "C"

// ---- NTFS acceptance fixture -------------------------------------------------------------------------------------------
namespace {

struct UnsupportedProbe : public std::exception
{
	std::string probe;
	DWORD error;
	UnsupportedProbe(const std::string &p, DWORD e) : probe(p), error(e) {}
	const char *what() const noexcept override { return probe.c_str(); }
};

struct MountBuf
{
	ULONG tag;
	USHORT dataLength, reserved, substOffset, substLength, printOffset, printLength;
	WCHAR path[1];
};

std::wstring gRoot;
std::wstring gExe;
FILE *gReceipt = NULL;
unsigned gFailures = 0;
unsigned gUnsupported = 0;
bool gAllowWine = false;
bool gPermit = true;
bool gCancel = false;
bool gNamePermit = true;

std::wstring WideFromUtf8(const char *s)
{
	if (!s || !*s)
		return L"";
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
	if (n <= 1)
		return L"";
	std::wstring out((size_t)(n - 1), L'\0');
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, &out[0], n) != n)
		return L"";
	return out;
}

std::string Utf8FromWide(const std::wstring &w)
{
	if (w.empty())
		return "";
	int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
	if (n <= 0)
		return "";
	std::string out((size_t)n, '\0');
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.c_str(), (int)w.size(), &out[0], n, NULL, NULL) != n)
		return "";
	return out;
}

std::wstring Path(const std::wstring &rel) { return gRoot + L"\\" + rel; }

std::wstring Pre(const std::wstring &p)
{
	if (p.size() >= 4 && p[0] == L'\\' && p[1] == L'\\' && p[2] == L'?' && p[3] == L'\\')
		return p;
	return L"\\\\?\\" + p;
}

std::wstring Unpre(const std::wstring &p)
{
	if (p.size() >= 4 && p[0] == L'\\' && p[1] == L'\\' && p[2] == L'?' && p[3] == L'\\')
		return p.substr(4);
	return p;
}

std::string PathUtf8(const std::wstring &rel)
{
	return Utf8FromWide(Unpre(Path(rel)));
}

std::string JsonAscii(const std::string &s)
{
	std::string out = "\"";
	char esc[8];
	for (unsigned char c : s)
	{
		if (c == '"' || c == '\\')
		{
			out += '\\';
			out += (char)c;
		}
		else if (c >= 0x20 && c < 0x7f)
			out += (char)c;
		else
		{
			snprintf(esc, sizeof esc, "\\u%04x", (unsigned)c);
			out += esc;
		}
	}
	return out + "\"";
}

std::string JsonWide(const std::wstring &w)
{
	std::string out = "\"";
	char esc[8];
	for (wchar_t c : w)
	{
		unsigned v = (unsigned)(unsigned short)c;
		if (v == '"' || v == '\\')
		{
			out += '\\';
			out += (char)v;
		}
		else if (v >= 0x20 && v < 0x7f)
			out += (char)v;
		else
		{
			snprintf(esc, sizeof esc, "\\u%04x", v);
			out += esc;
		}
	}
	return out + "\"";
}

void Emit(const std::string &line)
{
	fputs((line + "\n").c_str(), stdout);
	fflush(stdout);
	if (gReceipt)
	{
		fputs((line + "\n").c_str(), gReceipt);
		fflush(gReceipt);
	}
}

void Check(bool ok, const char *name)
{
	DWORD err = ok ? 0 : GetLastError();
	Emit(std::string("{\"check\":") + JsonAscii(name) + ",\"ok\":" + (ok ? "true" : "false") +
		 ",\"winerror\":" + std::to_string((unsigned long)err) + "}");
	if (!ok)
	{
		gFailures++;
		throw std::runtime_error(name);
	}
}

bool PermitCb(void *) { return gPermit; }
bool CancelCb(void *) { return gCancel; }
bool RestoreNamePermitCb(void *, const std::string &, const std::string &) { return gNamePermit; }
bool PublishNamePermitCb(void *, const char *, const char *) { return gNamePermit; }

void Mkdir(const std::wstring &p)
{
	BOOL ok = CreateDirectoryW(Pre(p).c_str(), NULL);
	if (!ok && GetLastError() == ERROR_ALREADY_EXISTS)
		return;
	Check(ok != 0, "create-owned-directory");
}

void WriteBytes(const std::wstring &p, const std::vector<unsigned char> &bytes, DWORD disposition = CREATE_NEW)
{
	HANDLE h = CreateFileW(Pre(p).c_str(), GENERIC_WRITE, 0, NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
	Check(h != INVALID_HANDLE_VALUE, "write-fixture-open");
	DWORD wrote = 0;
	BOOL ok = bytes.empty()
				  ? TRUE
				  : (WriteFile(h, bytes.data(), (DWORD)bytes.size(), &wrote, NULL) && wrote == bytes.size());
	BOOL flushed = FlushFileBuffers(h);
	BOOL closed = CloseHandle(h);
	Check(ok && flushed && closed, "write-fixture-exact");
}

void WriteText(const std::wstring &p, const std::string &text, DWORD disposition = CREATE_NEW)
{
	std::vector<unsigned char> bytes(text.begin(), text.end());
	WriteBytes(p, bytes, disposition);
}

std::vector<unsigned char> ReadAllFile(const std::wstring &p)
{
	HANDLE h = CreateFileW(Pre(p).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
						   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	Check(h != INVALID_HANDLE_VALUE, "read-fixture-open");
	std::vector<unsigned char> out;
	unsigned char buf[4096];
	DWORD got = 0;
	while (ReadFile(h, buf, sizeof buf, &got, NULL) && got > 0)
		out.insert(out.end(), buf, buf + got);
	CloseHandle(h);
	return out;
}

std::string ReadTextFile(const std::wstring &p)
{
	std::vector<unsigned char> b = ReadAllFile(p);
	return std::string(b.begin(), b.end());
}

std::wstring FinalPath(HANDLE h, DWORD flags)
{
	std::vector<wchar_t> buf(32768);
	DWORD n = GetFinalPathNameByHandleW(h, buf.data(), (DWORD)buf.size(), flags);
	if (n == 0 || n >= buf.size())
		return L"";
	return std::wstring(buf.data(), n);
}

bool QueryNumberOfLinks(const std::wstring &p, DWORD &linksOut)
{
	linksOut = 0;
	HANDLE h = CreateFileW(Pre(p).c_str(), FILE_READ_ATTRIBUTES,
						   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
						   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	FILE_STANDARD_INFO info = {};
	BOOL ok = GetFileInformationByHandleEx(h, FileStandardInfo, &info, sizeof info);
	CloseHandle(h);
	if (!ok)
		return false;
	linksOut = info.NumberOfLinks;
	return true;
}

bool HasPendingEntry(const std::wstring &dir)
{
	WIN32_FIND_DATAW fd = {};
	HANDLE h = FindFirstFileW(Pre(dir + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	bool found = false;
	do
	{
		if (wcsncmp(fd.cFileName, L".pending-", 9) == 0)
			found = true;
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return found;
}

void PutU16(std::vector<unsigned char> &b, size_t off, uint16_t v)
{
	b[off] = (unsigned char)(v & 0xffu);
	b[off + 1] = (unsigned char)((v >> 8) & 0xffu);
}

void PutU32(std::vector<unsigned char> &b, size_t off, uint32_t v)
{
	b[off] = (unsigned char)(v & 0xffu);
	b[off + 1] = (unsigned char)((v >> 8) & 0xffu);
	b[off + 2] = (unsigned char)((v >> 16) & 0xffu);
	b[off + 3] = (unsigned char)((v >> 24) & 0xffu);
}

void PutF32(std::vector<unsigned char> &b, size_t off, float v)
{
	uint32_t bits = 0;
	memcpy(&bits, &v, sizeof bits);
	PutU32(b, off, bits);
}

std::vector<unsigned char> MakeCareer(uint32_t killSeed)
{
	using namespace ce_career_view_detail;
	std::vector<unsigned char> buf(CE_CAREER_SIZE, 0);
	PutU16(buf, 0, CE_CAREER_VERSION);
	for (int n = 0; n < CE_CAREER_MISSIONS; ++n)
	{
		size_t at = (size_t)kMissionOffset + (size_t)n * kMissionStride;
		PutU32(buf, at + kMissionLower, 0xffffffffu);
		PutU32(buf, at + kMissionHigher, 0xffffffffu);
		PutF32(buf, at + kMissionRank, -1.0f);
	}
	for (int r = 0; r < CE_CAREER_ROUTES; ++r)
	{
		size_t at = (size_t)kRouteOffset + (size_t)r * kRouteStride;
		PutU32(buf, at, 0);
		PutU32(buf, at + 4, 0xffffffffu);
	}
	const uint32_t worlds[3] = {100u, 110u, 200u};
	const uint32_t nextNode[3] = {1u, 2u, 0xffffffffu};
	for (int idx = 0; idx < 3; ++idx)
	{
		size_t at = (size_t)kMissionOffset + (size_t)idx * kMissionStride;
		PutU32(buf, at + kMissionComplete, idx == 0 ? 1u : 0u);
		PutU32(buf, at + kMissionLower, (uint32_t)(idx * 2));
		PutU32(buf, at + kMissionHigher, (uint32_t)(idx * 2 + 1));
		PutU32(buf, at + kMissionWorld, worlds[idx]);
		PutF32(buf, at + kMissionRank, idx == 0 ? 1.0f : -1.0f);
		size_t rat = (size_t)kRouteOffset + (size_t)(idx * 2) * kRouteStride;
		PutU32(buf, rat, idx == 0 ? 1u : 0u);
		PutU32(buf, rat + 4, nextNode[idx]);
	}
	PutU32(buf, kKillsOffset, killSeed & 0x00ffffffu);
	PutF32(buf, kSoundOffset, 0.8f);
	PutF32(buf, kMusicOffset, 0.7f);
	return buf;
}

DWORD MakeJunction(const std::wstring &link, const std::wstring &target)
{
	if (!CreateDirectoryW(Pre(link).c_str(), NULL))
		return GetLastError();
	HANDLE h = CreateFileW(Pre(link).c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
						   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return GetLastError();
	std::wstring cleanTarget = Unpre(target);
	std::wstring subst = L"\\??\\" + cleanTarget;
	std::wstring print = cleanTarget;
	std::vector<BYTE> buf(sizeof(MountBuf) + (subst.size() + print.size() + 2) * sizeof(WCHAR) + 16, 0);
	MountBuf *m = reinterpret_cast<MountBuf *>(buf.data());
	m->tag = 0xA0000003u; // IO_REPARSE_TAG_MOUNT_POINT
	m->substOffset = 0;
	m->substLength = (USHORT)(subst.size() * sizeof(WCHAR));
	m->printOffset = (USHORT)(m->substLength + sizeof(WCHAR));
	m->printLength = (USHORT)(print.size() * sizeof(WCHAR));
	memcpy(reinterpret_cast<BYTE *>(m->path), subst.c_str(), m->substLength);
	memcpy(reinterpret_cast<BYTE *>(m->path) + m->printOffset, print.c_str(), m->printLength);
	m->dataLength = (USHORT)(8 + m->printOffset + m->printLength + sizeof(WCHAR));
	DWORD got = 0;
	DWORD err = DeviceIoControl(h, 0x000900A4u /* FSCTL_SET_REPARSE_POINT */, buf.data(),
								(DWORD)(8 + m->dataLength), NULL, 0, &got, NULL)
					? 0
					: GetLastError();
	CloseHandle(h);
	if (err)
		RemoveDirectoryW(Pre(link).c_str());
	return err;
}

struct ProfileDirs
{
	std::string user;
	std::string game;
	std::string package;
	std::string backup;
	std::string external;
};

ProfileDirs SetupProfile()
{
	Mkdir(Path(L"user"));
	Mkdir(Path(L"user\\savegames"));
	Mkdir(Path(L"game"));
	Mkdir(Path(L"game\\savegames"));
	Mkdir(Path(L"package"));
	Mkdir(Path(L"backup"));
	Mkdir(Path(L"external"));
	ProfileDirs p;
	p.user = PathUtf8(L"user");
	p.game = PathUtf8(L"game");
	p.package = PathUtf8(L"package");
	p.backup = PathUtf8(L"backup");
	p.external = PathUtf8(L"external");
	gPackageBasePath = p.package + "\\";
	gPermit = true;
	gCancel = false;
	gNamePermit = true;
	return p;
}

void SnapshotDirRecursive(const std::wstring &rootDir, const std::wstring &prefix,
						  std::map<std::wstring, std::vector<unsigned char>> &out)
{
	WIN32_FIND_DATAW fd = {};
	HANDLE h = FindFirstFileW(Pre(rootDir + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return;
	do
	{
		std::wstring name = fd.cFileName;
		if (name == L"." || name == L".." || name == L".ce-career-custody.lock")
			continue;
		std::wstring full = rootDir + L"\\" + name;
		std::wstring rel = prefix.empty() ? name : (prefix + L"\\" + name);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			out[rel + L"\\"] = {};
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				SnapshotDirRecursive(full, rel, out);
		}
		else
		{
			out[rel] = ReadAllFile(full);
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

struct ChildOutput
{
	DWORD exitCode = 1;
	std::string text;
	std::string errText;
};

ChildOutput RunSelf(const std::wstring &args, const std::wstring &outFile, const std::wstring &errFile)
{
	SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
	HANDLE out = CreateFileW(Pre(outFile).c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	Check(out != INVALID_HANDLE_VALUE, "child-output-open");
	HANDLE err = CreateFileW(Pre(errFile).c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	Check(err != INVALID_HANDLE_VALUE, "child-stderr-open");
	HANDLE in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, OPEN_EXISTING, 0, NULL);
	Check(in != INVALID_HANDLE_VALUE, "child-input-open");

	std::wstring cmd = L"\"" + gExe + L"\" " + args;
	std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
	mutableCmd.push_back(L'\0');

	STARTUPINFOW si = {};
	si.cb = sizeof si;
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = in;
	si.hStdOutput = out;
	si.hStdError = err;
	PROCESS_INFORMATION pi = {};
	BOOL started = CreateProcessW(gExe.c_str(), mutableCmd.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	CloseHandle(out);
	CloseHandle(err);
	CloseHandle(in);

	Check(started != 0, "child-process-start");
	DWORD waited = WaitForSingleObject(pi.hProcess, 60000);
	if (waited == WAIT_TIMEOUT)
	{
		TerminateProcess(pi.hProcess, 1);
		WaitForSingleObject(pi.hProcess, 5000);
	}
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	Check(waited == WAIT_OBJECT_0, "child-process-finished");
	return {code, ReadTextFile(outFile), ReadTextFile(errFile)};
}

// ---- Case 1: Atomic backup/restore roundtrip ----------------------------------------------------------------------------
void CaseBackupRestoreRoundtrip()
{
	ProfileDirs p = SetupProfile();
	const std::vector<unsigned char> alpha = MakeCareer(101);
	const std::vector<unsigned char> gameOrig = MakeCareer(202);
	const std::string opts = "authored-default-options-custody-fixture";
	WriteBytes(Path(L"user\\savegames\\Alpha.bes"), alpha);
	WriteText(Path(L"user\\defaultoptions.bea"), opts);
	WriteBytes(Path(L"game\\savegames\\GameOrig.bes"), gameOrig);

	CeCareerStorageRequest backupReq;
	backupReq.user = p.user;
	backupReq.game = p.game;
	backupReq.package = p.package;
	backupReq.backupRoot = p.backup;
	backupReq.permitted = PermitCb;
	backupReq.cancelled = CancelCb;
	CeCareerStorageResult fullSet = CeCareerTakeFullSet(backupReq);
	Check(fullSet.status == CeCareerStorageStatus::Complete && !fullSet.backupSet.empty() &&
			  !fullSet.backupIdentity.empty() && !fullSet.backupStamp.empty() && !HasPendingEntry(Path(L"backup")),
		  "take-full-set-complete");

	std::vector<std::string> sets;
	std::string listErr;
	Check(CePreviewList(p.backup, p.user, p.game, p.package, sets, listErr) && sets.size() == 1,
		  "backup-list-contains-set");

	CePreviewSet preview;
	std::string readErr;
	Check(CePreviewRead(fullSet.backupSet, p.user, p.game, p.package, preview, readErr) &&
			  preview.identity == fullSet.backupIdentity && preview.members == 3 && preview.careers.size() == 2,
		  "backup-preview-verified");

	CeCareerRestoreRequest rreq;
	rreq.user = p.user;
	rreq.game = p.game;
	rreq.package = p.package;
	rreq.backupRoot = p.backup;
	rreq.permitted = PermitCb;
	rreq.cancelled = CancelCb;
	rreq.namePermitted = RestoreNamePermitCb;
	rreq.sourceSet = fullSet.backupSet;
	rreq.sourceIdentity = fullSet.backupIdentity;
	rreq.sourceName = "Alpha.bes";
	std::string stampNow;
	Check(CeCareerRestoreStamp(rreq, stampNow) && stampNow == fullSet.backupStamp, "restore-stamp-matches");
	rreq.sourceStamp = stampNow;

	std::string suggested;
	Check(CeCareerRestoreSuggest(rreq, suggested) == CeCareerStorageStatus::Complete && !suggested.empty() &&
			  suggested != "Alpha.bes" && suggested.size() <= 35,
		  "restore-suggest-clean");

	rreq.targetName = suggested;
	CeCareerStorageResult restored = CeCareerRestoreAsNew(rreq);
	Check(restored.status == CeCareerStorageStatus::Complete && restored.newFile == suggested &&
			  !restored.backupSet.empty() && !HasPendingEntry(Path(L"user\\savegames")) && !HasPendingEntry(Path(L"backup")),
		  "restore-as-new-complete");

	std::wstring restoredPath = Path(L"user\\savegames\\") + WideFromUtf8(suggested.c_str());
	DWORD nlinks = 0;
	Check(ReadAllFile(restoredPath) == alpha && QueryNumberOfLinks(restoredPath, nlinks) && nlinks == 1,
		  "restore-new-bytes-exact");
	Check(ReadAllFile(Path(L"user\\savegames\\Alpha.bes")) == alpha &&
			  ReadAllFile(Path(L"game\\savegames\\GameOrig.bes")) == gameOrig &&
			  ReadTextFile(Path(L"user\\defaultoptions.bea")) == opts,
		  "restore-originals-preserved");

	// Occupied target in user or game savegames must refuse without overwriting.
	rreq.targetName = "Alpha.bes";
	CeCareerStorageResult occUser = CeCareerRestoreAsNew(rreq);
	rreq.targetName = "GameOrig.bes";
	CeCareerStorageResult occGame = CeCareerRestoreAsNew(rreq);
	Check(occUser.status == CeCareerStorageStatus::Occupied && occGame.status == CeCareerStorageStatus::Occupied &&
			  ReadAllFile(Path(L"user\\savegames\\Alpha.bes")) == alpha &&
			  ReadAllFile(Path(L"game\\savegames\\GameOrig.bes")) == gameOrig,
		  "restore-occupied-refused");
}

// ---- Case 2: External .bes publish & collision/sanitization -------------------------------------------------------------
void CaseExternalPublishCollision()
{
	ProfileDirs p = SetupProfile();
	const std::vector<unsigned char> existing = MakeCareer(111);
	const std::vector<unsigned char> gameSlot = MakeCareer(222);
	const std::vector<unsigned char> hero = MakeCareer(333);
	WriteBytes(Path(L"user\\savegames\\Existing.bes"), existing);
	WriteBytes(Path(L"user\\savegames\\Again.bes"), existing);
	WriteBytes(Path(L"user\\savegames\\Again imported.bes"), existing);
	WriteBytes(Path(L"user\\savegames\\Again edited.bes"), existing);
	WriteBytes(Path(L"game\\savegames\\GameSlot.bes"), gameSlot);
	WriteBytes(Path(L"external\\Hero.bes"), hero);

	std::vector<unsigned char> shortBytes(hero.begin(), hero.end() - 1);
	WriteBytes(Path(L"external\\BadSize.bes"), shortBytes);
	std::vector<unsigned char> badVer = hero;
	PutU16(badVer, 0, CE_CAREER_VERSION + 1);
	WriteBytes(Path(L"external\\BadVer.bes"), badVer);

	unsigned char imported[CE_CAREER_SIZE] = {};
	char leaf[256] = {}, err[192] = {};
	Check(CeCareerImportRead(PathUtf8(L"external\\Hero.bes").c_str(), p.user.c_str(), p.game.c_str(), p.package.c_str(),
							 imported, sizeof imported, leaf, sizeof leaf, err, sizeof err) == CE_IMPORT_READ_OK &&
			  strcmp(leaf, "Hero.bes") == 0 && memcmp(imported, hero.data(), CE_CAREER_SIZE) == 0,
		  "import-read-valid-ok");

	Check(CeCareerImportRead(PathUtf8(L"external\\BadSize.bes").c_str(), p.user.c_str(), p.game.c_str(), p.package.c_str(),
							 imported, sizeof imported, leaf, sizeof leaf, err, sizeof err) == CE_IMPORT_READ_BAD_SIZE,
		  "import-read-bad-size-refused");

	Check(CeCareerImportRead(PathUtf8(L"external\\BadVer.bes").c_str(), p.user.c_str(), p.game.c_str(), p.package.c_str(),
							 imported, sizeof imported, leaf, sizeof leaf, err, sizeof err) == CE_IMPORT_READ_BAD_CAREER,
		  "import-read-bad-career-refused");

	Check(CeCareerImportRead(PathUtf8(L"user\\savegames\\Existing.bes").c_str(), p.user.c_str(), p.game.c_str(), p.package.c_str(),
							 imported, sizeof imported, leaf, sizeof leaf, err, sizeof err) != CE_IMPORT_READ_OK,
		  "import-read-inside-user-refused");

	char targetDirect[80] = {}, targetColl1[80] = {}, targetColl2[80] = {}, targetEdit[80] = {}, targetLong[80] = {}, targetDev[80] = {};
	bool suggestOk =
		CeCareerPublishSuggest(CE_PUBLISH_IMPORT, p.user.c_str(), p.game.c_str(), "Hero.bes", PublishNamePermitCb, NULL, targetDirect, sizeof targetDirect) == CE_PUBLISH_COMPLETE &&
		strcmp(targetDirect, "Hero.bes") == 0 &&
		CeCareerPublishSuggest(CE_PUBLISH_IMPORT, p.user.c_str(), p.game.c_str(), "Existing.bes", PublishNamePermitCb, NULL, targetColl1, sizeof targetColl1) == CE_PUBLISH_COMPLETE &&
		strcmp(targetColl1, "Existing imported.bes") == 0 &&
		CeCareerPublishSuggest(CE_PUBLISH_IMPORT, p.user.c_str(), p.game.c_str(), "Again.bes", PublishNamePermitCb, NULL, targetColl2, sizeof targetColl2) == CE_PUBLISH_COMPLETE &&
		strcmp(targetColl2, "Again imported 2.bes") == 0 &&
		CeCareerPublishSuggest(CE_PUBLISH_EDIT, p.user.c_str(), p.game.c_str(), "Existing.bes", PublishNamePermitCb, NULL, targetEdit, sizeof targetEdit) == CE_PUBLISH_COMPLETE &&
		strcmp(targetEdit, "Existing edited.bes") == 0 &&
		CeCareerPublishSuggest(CE_PUBLISH_IMPORT, p.user.c_str(), p.game.c_str(), "VeryLongExternalCareerFilenameExceedingThirtyOneChars.bes", PublishNamePermitCb, NULL, targetLong, sizeof targetLong) == CE_PUBLISH_COMPLETE &&
		strlen(targetLong) <= 35 && strlen(targetLong) > 4 &&
		CeCareerPublishSuggest(CE_PUBLISH_IMPORT, p.user.c_str(), p.game.c_str(), "COM1.bes", PublishNamePermitCb, NULL, targetDev, sizeof targetDev) == CE_PUBLISH_COMPLETE &&
		strcmp(targetDev, "Imported.bes") == 0;
	Check(suggestOk, "publish-suggest-collision-and-sanitize");

	CeCareerPublishRequest req = {};
	req.userDir = p.user.c_str();
	req.gameDir = p.game.c_str();
	req.packageDir = p.package.c_str();
	req.backupRoot = p.backup.c_str();
	req.sourceName = "Hero.bes";
	req.targetName = targetDirect;
	req.bytes = hero.data();
	req.size = hero.size();
	req.permitted = PermitCb;
	req.cancelled = CancelCb;
	req.namePermitted = PublishNamePermitCb;
	CeCareerPublishResult pub = CeCareerPublishNew(&req);
	Check(pub.status == CE_PUBLISH_COMPLETE && pub.possible == 1 && strcmp(pub.newFile, "Hero.bes") == 0 && pub.backupSet[0] != 0,
		  "publish-new-complete");

	DWORD nlinks = 0;
	Check(ReadAllFile(Path(L"user\\savegames\\Hero.bes")) == hero &&
			  QueryNumberOfLinks(Path(L"user\\savegames\\Hero.bes"), nlinks) && nlinks == 1 &&
			  ReadAllFile(Path(L"user\\savegames\\Existing.bes")) == existing,
		  "publish-new-bytes-exact");

	// Occupied targets in user and game folders must refuse before backup or overwrite.
	std::vector<std::string> setsBefore;
	std::string listErr;
	CePreviewList(p.backup, p.user, p.game, p.package, setsBefore, listErr);
	req.targetName = "Existing.bes";
	CeCareerPublishResult occUser = CeCareerPublishNew(&req);
	req.targetName = "GameSlot.bes";
	CeCareerPublishResult occGame = CeCareerPublishNew(&req);
	std::vector<std::string> setsAfter;
	CePreviewList(p.backup, p.user, p.game, p.package, setsAfter, listErr);
	Check(occUser.status == CE_PUBLISH_OCCUPIED && occGame.status == CE_PUBLISH_OCCUPIED &&
			  setsBefore == setsAfter && ReadAllFile(Path(L"user\\savegames\\Existing.bes")) == existing &&
			  ReadAllFile(Path(L"game\\savegames\\GameSlot.bes")) == gameSlot,
		  "publish-occupied-refused-no-overwrite");
}

// ---- Case 3: Backup preview read-only custody ---------------------------------------------------------------------------
void CaseBackupPreviewReadonly()
{
	ProfileDirs p = SetupProfile();
	const std::vector<unsigned char> alpha = MakeCareer(404);
	WriteBytes(Path(L"user\\savegames\\Alpha.bes"), alpha);
	WriteText(Path(L"user\\defaultoptions.bea"), "preview-options");

	CeCareerStorageRequest req;
	req.user = p.user;
	req.game = p.game;
	req.package = p.package;
	req.backupRoot = p.backup;
	req.permitted = PermitCb;
	req.cancelled = CancelCb;
	CeCareerStorageResult good = CeCareerBackupAll(req);
	Check(good.status == CeCareerStorageStatus::Complete, "preview-seed-backup-complete");

	// Build a tampered copy beside the valid backup and a nested copy inside user\.
	std::wstring goodW = WideFromUtf8(good.backupSet.c_str());
	std::wstring tamperedW = Path(L"backup\\onslaught-backup-tampered");
	Mkdir(tamperedW);
	std::vector<unsigned char> broken = alpha;
	broken.back() ^= 0xffu;
	WriteBytes(tamperedW + L"\\Alpha.bes", broken);
	WriteText(tamperedW + L"\\defaultoptions.bea", "preview-options");
	WriteText(tamperedW + L"\\onslaught-backup.json", ReadTextFile(goodW + L"\\onslaught-backup.json"));

	std::wstring nestedW = Path(L"user\\nested");
	Mkdir(nestedW);
	std::wstring insideSetW = nestedW + L"\\onslaught-backup-inside";
	Mkdir(insideSetW);
	WriteBytes(insideSetW + L"\\Alpha.bes", alpha);
	WriteText(insideSetW + L"\\defaultoptions.bea", "preview-options");
	WriteText(insideSetW + L"\\onslaught-backup.json", ReadTextFile(goodW + L"\\onslaught-backup.json"));

	std::map<std::wstring, std::vector<unsigned char>> before;
	SnapshotDirRecursive(Path(L"user"), L"user", before);
	SnapshotDirRecursive(Path(L"game"), L"game", before);
	SnapshotDirRecursive(Path(L"package"), L"package", before);
	SnapshotDirRecursive(Path(L"backup"), L"backup", before);

	std::vector<std::string> listed;
	std::string err;
	CePreviewSet set;
	Check(CePreviewList(p.backup, p.user, p.game, p.package, listed, err) && listed.size() >= 1 &&
			  CePreviewRead(good.backupSet, p.user, p.game, p.package, set, err) &&
			  set.identity == good.backupIdentity && set.careers.size() == 1 && set.careers[0].bytes == alpha,
		  "preview-valid-set-ok");

	CePreviewSet badSet;
	Check(!CePreviewRead(PathUtf8(L"backup\\onslaught-backup-tampered"), p.user, p.game, p.package, badSet, err),
		  "preview-tampered-member-refused");
	Check(!CePreviewRead(PathUtf8(L"user\\nested\\onslaught-backup-inside"), p.user, p.game, p.package, badSet, err),
		  "preview-inside-user-refused");

	std::map<std::wstring, std::vector<unsigned char>> after;
	SnapshotDirRecursive(Path(L"user"), L"user", after);
	SnapshotDirRecursive(Path(L"game"), L"game", after);
	SnapshotDirRecursive(Path(L"package"), L"package", after);
	SnapshotDirRecursive(Path(L"backup"), L"backup", after);
	Check(before == after, "preview-strictly-readonly");
}

// ---- Case 4: Reserved Windows device names & NTFS ADS rejection ---------------------------------------------------------
void CaseReservedNamesAndAds()
{
	ProfileDirs p = SetupProfile();
	const std::vector<unsigned char> alpha = MakeCareer(505);
	WriteBytes(Path(L"user\\savegames\\Alpha.bes"), alpha);

	CeCareerStorageRequest bReq;
	bReq.user = p.user;
	bReq.game = p.game;
	bReq.package = p.package;
	bReq.backupRoot = p.backup;
	bReq.permitted = PermitCb;
	bReq.cancelled = CancelCb;
	CeCareerStorageResult seed = CeCareerTakeFullSet(bReq);
	Check(seed.status == CeCareerStorageStatus::Complete, "reserved-seed-backup-complete");

	const char *kReserved[] = {"CON.bes", "NUL.bes", "AUX.bes", "PRN.bes", "COM1.bes", "LPT1.bes", "COM0.bes", "LPT0.bes", "Bad .bes", "Bad..bes"};
	const char *kAds[] = {"Hero.bes:stream", "Hero.bes::$DATA", "Hero:alt.bes", "Pilot\xe2\x80\xae.bes"};

	bool pubReservedRefused = true;
	for (const char *bad : kReserved)
	{
		CeCareerPublishRequest req = {};
		req.userDir = p.user.c_str();
		req.gameDir = p.game.c_str();
		req.packageDir = p.package.c_str();
		req.backupRoot = p.backup.c_str();
		req.sourceName = "Alpha.bes";
		req.targetName = bad;
		req.bytes = alpha.data();
		req.size = alpha.size();
		req.permitted = PermitCb;
		req.cancelled = CancelCb;
		req.namePermitted = PublishNamePermitCb;
		CeCareerPublishResult res = CeCareerPublishNew(&req);
		if (res.status != CE_PUBLISH_BAD_NAME || res.possible != 0 || res.newFile[0] != 0)
			pubReservedRefused = false;
	}
	Check(pubReservedRefused, "publish-reserved-device-names-refused");

	bool pubAdsRefused = true;
	for (const char *bad : kAds)
	{
		CeCareerPublishRequest req = {};
		req.userDir = p.user.c_str();
		req.gameDir = p.game.c_str();
		req.packageDir = p.package.c_str();
		req.backupRoot = p.backup.c_str();
		req.sourceName = "Alpha.bes";
		req.targetName = bad;
		req.bytes = alpha.data();
		req.size = alpha.size();
		req.permitted = PermitCb;
		req.cancelled = CancelCb;
		req.namePermitted = PublishNamePermitCb;
		CeCareerPublishResult res = CeCareerPublishNew(&req);
		if (res.status != CE_PUBLISH_BAD_NAME || res.possible != 0 || res.newFile[0] != 0)
			pubAdsRefused = false;
	}
	Check(pubAdsRefused, "publish-ads-names-refused");

	bool restoreReservedRefused = true;
	for (const char *bad : kReserved)
	{
		CeCareerRestoreRequest rreq;
		rreq.user = p.user;
		rreq.game = p.game;
		rreq.package = p.package;
		rreq.backupRoot = p.backup;
		rreq.permitted = PermitCb;
		rreq.cancelled = CancelCb;
		rreq.namePermitted = RestoreNamePermitCb;
		rreq.sourceSet = seed.backupSet;
		rreq.sourceIdentity = seed.backupIdentity;
		rreq.sourceName = "Alpha.bes";
		rreq.sourceStamp = seed.backupStamp;
		rreq.targetName = bad;
		CeCareerStorageResult res = CeCareerRestoreAsNew(rreq);
		if (res.status != CeCareerStorageStatus::Refused || !res.newFile.empty())
			restoreReservedRefused = false;
	}
	Check(restoreReservedRefused, "restore-reserved-device-names-refused");

	bool restoreAdsRefused = true;
	for (const char *bad : kAds)
	{
		CeCareerRestoreRequest rreq;
		rreq.user = p.user;
		rreq.game = p.game;
		rreq.package = p.package;
		rreq.backupRoot = p.backup;
		rreq.permitted = PermitCb;
		rreq.cancelled = CancelCb;
		rreq.namePermitted = RestoreNamePermitCb;
		rreq.sourceSet = seed.backupSet;
		rreq.sourceIdentity = seed.backupIdentity;
		rreq.sourceName = "Alpha.bes";
		rreq.sourceStamp = seed.backupStamp;
		rreq.targetName = bad;
		CeCareerStorageResult res = CeCareerRestoreAsNew(rreq);
		if (res.status != CeCareerStorageStatus::Refused || !res.newFile.empty())
			restoreAdsRefused = false;
	}
	Check(restoreAdsRefused, "restore-ads-names-refused");

	// On-disk NTFS ADS probe alongside Alpha.bes (matching mods-windows-ntfs.exe).
	std::wstring saveDir = Path(L"user\\savegames");
	std::wstring adsPath = Pre(saveDir + L"\\Alpha.bes:hidden");
	HANDLE ads = CreateFileW(adsPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (ads != INVALID_HANDLE_VALUE)
	{
		const char extra[] = "alternate-stream-must-not-alter-unnamed-career";
		DWORD wrote = 0;
		BOOL ok = WriteFile(ads, extra, (DWORD)(sizeof(extra) - 1), &wrote, NULL) && wrote == sizeof(extra) - 1;
		CloseHandle(ads);
		Check(ok != 0, "ntfs-ads-attach-write");
		bool sawLiteralColon = false;
		WIN32_FIND_DATAW fd = {};
		HANDLE fh = FindFirstFileW(Pre(saveDir + L"\\*").c_str(), &fd);
		Check(fh != INVALID_HANDLE_VALUE, "ntfs-ads-list-savegames");
		do
		{
			if (wcschr(fd.cFileName, L':') != NULL)
				sawLiteralColon = true;
		} while (FindNextFileW(fh, &fd));
		FindClose(fh);
		if (sawLiteralColon)
		{
			DeleteFileW(adsPath.c_str());
			if (!gAllowWine)
				Check(!sawLiteralColon, "ntfs-ads-not-literal-colon");
			Emit("{\"probe\":\"ntfs-ads-on-disk\",\"status\":\"UNSUPPORTED_OR_DENIED\",\"winerror\":0,\"passed\":false}");
		}
		else
		{
			Emit("{\"probe\":\"ntfs-ads-on-disk\",\"status\":\"EXERCISED\",\"passed\":true}");
		}
	}
	else
	{
		DWORD err = GetLastError();
		if (!gAllowWine)
			Check(ads != INVALID_HANDLE_VALUE, "ntfs-ads-attach-open");
		Emit("{\"probe\":\"ntfs-ads-on-disk\",\"status\":\"UNSUPPORTED_OR_DENIED\",\"winerror\":" +
			 std::to_string((unsigned long)err) + ",\"passed\":false}");
	}

	CeCareerStorageResult afterAds = CeCareerBackupAll(bReq);
	CePreviewSet checked;
	std::string readErr;
	Check(afterAds.status == CeCareerStorageStatus::Complete &&
			  CePreviewRead(afterAds.backupSet, p.user, p.game, p.package, checked, readErr) &&
			  checked.careers.size() == 1 && checked.careers[0].bytes == alpha,
		  "backup-unnamed-stream-exact");
}

// ---- Case 5: Hardlink refusal across ImportRead, PreviewRead, BackupAll & PublishNew -----------------------------------
void CaseHardlinkRefusal()
{
	ProfileDirs p = SetupProfile();
	const std::vector<unsigned char> alpha = MakeCareer(606);
	const std::vector<unsigned char> hero = MakeCareer(607);
	WriteBytes(Path(L"user\\savegames\\Alpha.bes"), alpha);
	WriteBytes(Path(L"external\\Hero.bes"), hero);

	// 1. Hardlink in external import path.
	std::wstring extSrc = Path(L"external\\Hero.bes");
	std::wstring extLink = Path(L"external\\Hardlink.bes");
	Check(CreateHardLinkW(Pre(extLink).c_str(), Pre(extSrc).c_str(), NULL) != 0, "create-hardlink-external");
	DWORD extLinks = 0;
	Check(QueryNumberOfLinks(extLink, extLinks) && extLinks == 2, "hardlink-verified-nlinks-2");

	unsigned char buf[CE_CAREER_SIZE] = {};
	char leaf[256] = {}, err[192] = {};
	Check(CeCareerImportRead(PathUtf8(L"external\\Hardlink.bes").c_str(), p.user.c_str(), p.game.c_str(), p.package.c_str(),
							 buf, sizeof buf, leaf, sizeof leaf, err, sizeof err) == CE_IMPORT_READ_UNSAFE_FILE,
		  "import-read-hardlink-refused");

	// 2. Hardlink inside a backup set refused by CePreviewRead.
	CeCareerStorageRequest bReq;
	bReq.user = p.user;
	bReq.game = p.game;
	bReq.package = p.package;
	bReq.backupRoot = p.backup;
	bReq.permitted = PermitCb;
	bReq.cancelled = CancelCb;
	CeCareerStorageResult good = CeCareerBackupAll(bReq);
	Check(good.status == CeCareerStorageStatus::Complete, "hardlink-seed-backup-complete");
	std::wstring setW = WideFromUtf8(good.backupSet.c_str());
	Check(CreateHardLinkW(Pre(setW + L"\\Alpha-alias.bin").c_str(), Pre(setW + L"\\Alpha.bes").c_str(), NULL) != 0,
		  "create-hardlink-backup-member");
	CePreviewSet preview;
	std::string previewErr;
	Check(!CePreviewRead(good.backupSet, p.user, p.game, p.package, preview, previewErr),
		  "preview-hardlink-refused");

	// 3. Hardlink inside user\savegames refused by CeCareerBackupAll & CeCareerPublishNew.
	std::wstring saveSrc = Path(L"user\\savegames\\Alpha.bes");
	std::wstring saveLink = Path(L"user\\savegames\\Alpha-alias.bin");
	Check(CreateHardLinkW(Pre(saveLink).c_str(), Pre(saveSrc).c_str(), NULL) != 0, "create-hardlink-save");
	CeCareerStorageResult bFail = CeCareerBackupAll(bReq);
	Check(bFail.status == CeCareerStorageStatus::Refused && bFail.backupSet.empty(), "backup-all-hardlink-refused");

	CeCareerPublishRequest pReq = {};
	pReq.userDir = p.user.c_str();
	pReq.gameDir = p.game.c_str();
	pReq.packageDir = p.package.c_str();
	pReq.backupRoot = p.backup.c_str();
	pReq.sourceName = "Hero.bes";
	pReq.targetName = "Fresh.bes";
	pReq.bytes = hero.data();
	pReq.size = hero.size();
	pReq.permitted = PermitCb;
	pReq.cancelled = CancelCb;
	pReq.namePermitted = PublishNamePermitCb;
	CeCareerPublishResult pFail = CeCareerPublishNew(&pReq);
	Check(pFail.status == CE_PUBLISH_REFUSED && pFail.possible == 0 && pFail.newFile[0] == 0 &&
			  GetFileAttributesW(Pre(Path(L"user\\savegames\\Fresh.bes")).c_str()) == INVALID_FILE_ATTRIBUTES,
		  "publish-hardlink-refused");
}

// ---- Case 6: Read-only target & rollback / lock contention preservation ------------------------------------------------
void CaseReadonlyRollbackPreservation()
{
	ProfileDirs p = SetupProfile();
	const std::vector<unsigned char> prot = MakeCareer(707);
	const std::vector<unsigned char> fresh = MakeCareer(708);
	std::wstring protPath = Path(L"user\\savegames\\Protected.bes");
	WriteBytes(protPath, prot);
	Check(SetFileAttributesW(Pre(protPath).c_str(), FILE_ATTRIBUTE_READONLY) != 0, "set-readonly-target");

	// Seed a valid backup (reading a read-only source career must succeed and keep it read-only).
	CeCareerStorageRequest bReq;
	bReq.user = p.user;
	bReq.game = p.game;
	bReq.package = p.package;
	bReq.backupRoot = p.backup;
	bReq.permitted = PermitCb;
	bReq.cancelled = CancelCb;
	CeCareerStorageResult seed = CeCareerTakeFullSet(bReq);
	Check(seed.status == CeCareerStorageStatus::Complete, "readonly-seed-backup-complete");

	// Attempt restore targeting read-only Protected.bes -> Occupied.
	CeCareerRestoreRequest rReq;
	rReq.user = p.user;
	rReq.game = p.game;
	rReq.package = p.package;
	rReq.backupRoot = p.backup;
	rReq.permitted = PermitCb;
	rReq.cancelled = CancelCb;
	rReq.namePermitted = RestoreNamePermitCb;
	rReq.sourceSet = seed.backupSet;
	rReq.sourceIdentity = seed.backupIdentity;
	rReq.sourceName = "Protected.bes";
	rReq.sourceStamp = seed.backupStamp;
	rReq.targetName = "Protected.bes";
	CeCareerStorageResult rOcc = CeCareerRestoreAsNew(rReq);
	Check(rOcc.status == CeCareerStorageStatus::Occupied && rOcc.newFile.empty(), "readonly-target-restore-refused");

	// Attempt publish targeting read-only Protected.bes -> Occupied.
	CeCareerPublishRequest pReq = {};
	pReq.userDir = p.user.c_str();
	pReq.gameDir = p.game.c_str();
	pReq.packageDir = p.package.c_str();
	pReq.backupRoot = p.backup.c_str();
	pReq.sourceName = "Fresh.bes";
	pReq.targetName = "Protected.bes";
	pReq.bytes = fresh.data();
	pReq.size = fresh.size();
	pReq.permitted = PermitCb;
	pReq.cancelled = CancelCb;
	pReq.namePermitted = PublishNamePermitCb;
	CeCareerPublishResult pOcc = CeCareerPublishNew(&pReq);
	Check(pOcc.status == CE_PUBLISH_OCCUPIED && pOcc.possible == 0 && pOcc.newFile[0] == 0,
		  "readonly-target-publish-refused");

	DWORD attrs = GetFileAttributesW(Pre(protPath).c_str());
	Check(attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) != 0 &&
			  ReadAllFile(protPath) == prot,
		  "readonly-attribute-and-bytes-preserved");

	// Cancellation & authority revocation rollback leaves zero new careers and zero new backups.
	std::vector<std::string> setsBefore;
	std::string listErr;
	CePreviewList(p.backup, p.user, p.game, p.package, setsBefore, listErr);

	gCancel = true;
	pReq.targetName = "Cancelled.bes";
	CeCareerPublishResult pCancel = CeCareerPublishNew(&pReq);
	rReq.targetName = "CancelledR.bes";
	CeCareerStorageResult rCancel = CeCareerRestoreAsNew(rReq);
	gCancel = false;

	gPermit = false;
	pReq.targetName = "NoAuth.bes";
	CeCareerPublishResult pNoAuth = CeCareerPublishNew(&pReq);
	rReq.targetName = "NoAuthR.bes";
	CeCareerStorageResult rNoAuth = CeCareerRestoreAsNew(rReq);
	gPermit = true;

	std::vector<std::string> setsAfter;
	CePreviewList(p.backup, p.user, p.game, p.package, setsAfter, listErr);
	Check(pCancel.status == CE_PUBLISH_CANCELLED && rCancel.status == CeCareerStorageStatus::Cancelled &&
			  pNoAuth.status == CE_PUBLISH_AUTHORITY && rNoAuth.status == CeCareerStorageStatus::Refused &&
			  setsBefore == setsAfter &&
			  GetFileAttributesW(Pre(Path(L"user\\savegames\\Cancelled.bes")).c_str()) == INVALID_FILE_ATTRIBUTES &&
			  GetFileAttributesW(Pre(Path(L"user\\savegames\\NoAuth.bes")).c_str()) == INVALID_FILE_ATTRIBUTES,
		  "cancel-and-authority-rollback-clean");

	// Custody lock contention -> Busy, preserving everything cleanly.
	std::wstring lockPath = Path(L"user\\.ce-career-custody.lock");
	HANDLE lockH = CreateFileW(Pre(lockPath).c_str(), GENERIC_READ | GENERIC_WRITE,
							   FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	Check(lockH != INVALID_HANDLE_VALUE, "open-custody-lock");
	OVERLAPPED ov = {};
	Check(LockFileEx(lockH, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov) != 0,
		  "hold-custody-lock");
	CeCareerStorageResult bBusy = CeCareerBackupAll(bReq);
	pReq.targetName = "BusyTarget.bes";
	CeCareerPublishResult pBusy = CeCareerPublishNew(&pReq);
	UnlockFileEx(lockH, 0, 1, 0, &ov);
	CloseHandle(lockH);
	Check(bBusy.status == CeCareerStorageStatus::Busy && pBusy.status == CE_PUBLISH_BUSY &&
			  GetFileAttributesW(Pre(Path(L"user\\savegames\\BusyTarget.bes")).c_str()) == INVALID_FILE_ATTRIBUTES,
		  "custody-lock-busy-refused");

	Check(!HasPendingEntry(Path(L"user\\savegames")) && !HasPendingEntry(Path(L"backup")),
		  "no-pending-staging-leftovers");
}

// ---- Case 7: NTFS directory junction escape refusal ---------------------------------------------------------------------
void CaseJunctionEscape()
{
	ProfileDirs p = SetupProfile();
	const std::vector<unsigned char> alpha = MakeCareer(801);
	const std::vector<unsigned char> esc = MakeCareer(802);
	WriteBytes(Path(L"user\\savegames\\Alpha.bes"), alpha);

	// Seed one legitimate backup set first for preview junction test.
	CeCareerStorageRequest bReq;
	bReq.user = p.user;
	bReq.game = p.game;
	bReq.package = p.package;
	bReq.backupRoot = p.backup;
	bReq.permitted = PermitCb;
	bReq.cancelled = CancelCb;
	CeCareerStorageResult good = CeCareerBackupAll(bReq);
	Check(good.status == CeCareerStorageStatus::Complete, "junction-seed-backup-complete");

	std::wstring outsideTarget = Path(L"outside-target");
	Mkdir(outsideTarget);
	WriteBytes(outsideTarget + L"\\Escape.bes", esc);

	// Create three junctions: backupRoot -> user, external-junction -> outsideTarget, preview-junction -> good.backupSet.
	std::wstring jBackup = Path(L"backup-junction");
	DWORD err = MakeJunction(jBackup, Path(L"user"));
	if (err)
		throw UnsupportedProbe("create-ntfs-junction-backup", err);
	DWORD attrs = GetFileAttributesW(Pre(jBackup).c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT))
		throw UnsupportedProbe("ntfs-junction-reparse-not-visible", GetLastError());

	std::wstring jExternal = Path(L"external-junction");
	err = MakeJunction(jExternal, outsideTarget);
	if (err)
		throw UnsupportedProbe("create-ntfs-junction-external", err);

	std::wstring jPreview = Path(L"preview-junction");
	err = MakeJunction(jPreview, WideFromUtf8(good.backupSet.c_str()));
	if (err)
		throw UnsupportedProbe("create-ntfs-junction-preview", err);

	// Verify junctions are live (not dangling) when opened normally.
	Check(ReadAllFile(jExternal + L"\\Escape.bes") == esc &&
			  !ReadTextFile(jPreview + L"\\onslaught-backup.json").empty(),
		  "junction-reparse-visible");

	// 1. Backup into a junction backupRoot must be refused.
	CeCareerStorageRequest jBackupReq = bReq;
	jBackupReq.backupRoot = PathUtf8(L"backup-junction");
	CeCareerStorageResult jBackupRes = CeCareerBackupAll(jBackupReq);
	Check(jBackupRes.status == CeCareerStorageStatus::Refused && jBackupRes.backupSet.empty(),
		  "backup-junction-root-refused");

	// 2. External import through a directory junction must be refused during parent walk.
	unsigned char buf[CE_CAREER_SIZE] = {};
	char leaf[256] = {}, ierr[192] = {};
	Check(CeCareerImportRead(PathUtf8(L"external-junction\\Escape.bes").c_str(), p.user.c_str(), p.game.c_str(), p.package.c_str(),
							 buf, sizeof buf, leaf, sizeof leaf, ierr, sizeof ierr) == CE_IMPORT_READ_BAD_PATH,
		  "import-read-junction-refused");

	// 3. Preview of a backup set through a directory junction must be refused.
	CePreviewSet set;
	std::string perr;
	Check(!CePreviewRead(PathUtf8(L"preview-junction"), p.user, p.game, p.package, set, perr),
		  "preview-junction-refused");

	// 4. Replacing user\savegames with a junction to outsideTarget must be refused by CeCareerBackupAll.
	std::wstring saveDir = Path(L"user\\savegames");
	std::wstring movedSave = Path(L"moved-savegames");
	Check(MoveFileExW(Pre(saveDir).c_str(), Pre(movedSave).c_str(), 0) != 0, "junction-move-savegames-aside");
	err = MakeJunction(saveDir, movedSave);
	if (err)
		throw UnsupportedProbe("create-ntfs-junction-savegames", err);
	CeCareerStorageResult jSaveRes = CeCareerBackupAll(bReq);
	Check(jSaveRes.status == CeCareerStorageStatus::Refused && jSaveRes.backupSet.empty(),
		  "backup-junction-savegames-refused");
}

// ---- Case 8: Directory & file symlink refusal ---------------------------------------------------------------------------
void CaseSymlinkRefusal()
{
	ProfileDirs p = SetupProfile();
	const std::vector<unsigned char> alpha = MakeCareer(901);
	const std::vector<unsigned char> hero = MakeCareer(902);
	WriteBytes(Path(L"user\\savegames\\Alpha.bes"), alpha);
	WriteBytes(Path(L"external\\Hero.bes"), hero);

	CeCareerStorageRequest bReq;
	bReq.user = p.user;
	bReq.game = p.game;
	bReq.package = p.package;
	bReq.backupRoot = p.backup;
	bReq.permitted = PermitCb;
	bReq.cancelled = CancelCb;
	CeCareerStorageResult good = CeCareerBackupAll(bReq);
	Check(good.status == CeCareerStorageStatus::Complete, "symlink-seed-backup-complete");

	std::wstring fileSym = Path(L"external\\FileSym.bes");
	if (!CreateSymbolicLinkW(Pre(fileSym).c_str(), Unpre(Path(L"external\\Hero.bes")).c_str(),
							 SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
		throw UnsupportedProbe("create-file-symlink", GetLastError());
	DWORD fAttr = GetFileAttributesW(Pre(fileSym).c_str());
	if (fAttr == INVALID_FILE_ATTRIBUTES || !(fAttr & FILE_ATTRIBUTE_REPARSE_POINT))
		throw UnsupportedProbe("file-symlink-reparse-not-visible", GetLastError());

	std::wstring dirSym = Path(L"external-dirsym");
	if (!CreateSymbolicLinkW(Pre(dirSym).c_str(), Unpre(Path(L"external")).c_str(),
							 SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
		throw UnsupportedProbe("create-dir-symlink", GetLastError());
	DWORD dAttr = GetFileAttributesW(Pre(dirSym).c_str());
	if (dAttr == INVALID_FILE_ATTRIBUTES || !(dAttr & FILE_ATTRIBUTE_REPARSE_POINT))
		throw UnsupportedProbe("dir-symlink-reparse-not-visible", GetLastError());

	std::wstring previewDirSym = Path(L"preview-dirsym");
	if (!CreateSymbolicLinkW(Pre(previewDirSym).c_str(), Unpre(WideFromUtf8(good.backupSet.c_str())).c_str(),
							 SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
		throw UnsupportedProbe("create-preview-dir-symlink", GetLastError());

	Check(ReadAllFile(fileSym) == hero && ReadAllFile(dirSym + L"\\Hero.bes") == hero,
		  "symlink-reparse-visible");

	unsigned char buf[CE_CAREER_SIZE] = {};
	char leaf[256] = {}, ierr[192] = {};
	Check(CeCareerImportRead(PathUtf8(L"external\\FileSym.bes").c_str(), p.user.c_str(), p.game.c_str(), p.package.c_str(),
							 buf, sizeof buf, leaf, sizeof leaf, ierr, sizeof ierr) == CE_IMPORT_READ_UNSAFE_FILE,
		  "import-read-file-symlink-refused");
	Check(CeCareerImportRead(PathUtf8(L"external-dirsym\\Hero.bes").c_str(), p.user.c_str(), p.game.c_str(), p.package.c_str(),
							 buf, sizeof buf, leaf, sizeof leaf, ierr, sizeof ierr) == CE_IMPORT_READ_BAD_PATH,
		  "import-read-dir-symlink-refused");

	CePreviewSet set;
	std::string perr;
	Check(!CePreviewRead(PathUtf8(L"preview-dirsym"), p.user, p.game, p.package, set, perr),
		  "preview-symlink-refused");

	// File symlink placed in user\savegames must cause CeCareerBackupAll to refuse cleanly.
	std::wstring saveSym = Path(L"user\\savegames\\Linked.bes");
	if (!CreateSymbolicLinkW(Pre(saveSym).c_str(), Unpre(Path(L"external\\Hero.bes")).c_str(),
							 SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
		throw UnsupportedProbe("create-save-file-symlink", GetLastError());
	CeCareerStorageResult bRes = CeCareerBackupAll(bReq);
	Check(bRes.status == CeCareerStorageStatus::Refused && bRes.backupSet.empty(),
		  "backup-symlink-member-refused");
}

// ---- Case 9: NTFS exact-case & case-sensitivity handling ----------------------------------------------------------------
void CaseCaseSensitivity()
{
	ProfileDirs p = SetupProfile();

	// Probe NTFS normalized case reporting (CaseProbe.txt opened via caseprobe.txt).
	std::wstring probeDir = Path(L"case-probe");
	Mkdir(probeDir);
	WriteText(probeDir + L"\\CaseProbe.txt", "case-probe");
	HANDLE ph = CreateFileW(Pre(probeDir + L"\\caseprobe.txt").c_str(), FILE_READ_ATTRIBUTES, 7, NULL, OPEN_EXISTING,
							FILE_ATTRIBUTE_NORMAL, NULL);
	if (ph == INVALID_HANDLE_VALUE)
		throw UnsupportedProbe("case-insensitive-open-probe", GetLastError());
	std::wstring norm = FinalPath(ph, VOLUME_NAME_NONE | FILE_NAME_NORMALIZED);
	CloseHandle(ph);
	size_t slash = norm.find_last_of(L'\\');
	std::wstring leaf = slash == std::wstring::npos ? norm : norm.substr(slash + 1);
	if (leaf != L"CaseProbe.txt")
		throw UnsupportedProbe("ntfs-normalized-case-name-not-reported", 0);
	Check(true, "ntfs-normalized-case-probe");

	const std::vector<unsigned char> alpha = MakeCareer(951);
	const std::vector<unsigned char> gameSlot = MakeCareer(952);
	const std::vector<unsigned char> other = MakeCareer(953);
	WriteBytes(Path(L"user\\savegames\\Alpha.bes"), alpha);
	WriteBytes(Path(L"game\\savegames\\GameSlot.bes"), gameSlot);

	CeCareerStorageRequest bReq;
	bReq.user = p.user;
	bReq.game = p.game;
	bReq.package = p.package;
	bReq.backupRoot = p.backup;
	bReq.permitted = PermitCb;
	bReq.cancelled = CancelCb;
	CeCareerStorageResult seed = CeCareerTakeFullSet(bReq);
	Check(seed.status == CeCareerStorageStatus::Complete, "case-seed-backup-complete");

	CeCareerPublishRequest pReq = {};
	pReq.userDir = p.user.c_str();
	pReq.gameDir = p.game.c_str();
	pReq.packageDir = p.package.c_str();
	pReq.backupRoot = p.backup.c_str();
	pReq.sourceName = "Other.bes";
	pReq.targetName = "ALPHA.BES";
	pReq.bytes = other.data();
	pReq.size = other.size();
	pReq.permitted = PermitCb;
	pReq.cancelled = CancelCb;
	pReq.namePermitted = PublishNamePermitCb;
	CeCareerPublishResult pubUser = CeCareerPublishNew(&pReq);
	Check(pubUser.status == CE_PUBLISH_OCCUPIED && pubUser.possible == 0, "publish-case-variant-user-refused");

	pReq.targetName = "gameslot.bes";
	CeCareerPublishResult pubGame = CeCareerPublishNew(&pReq);
	Check(pubGame.status == CE_PUBLISH_OCCUPIED && pubGame.possible == 0, "publish-case-variant-game-refused");

	CeCareerRestoreRequest rReq;
	rReq.user = p.user;
	rReq.game = p.game;
	rReq.package = p.package;
	rReq.backupRoot = p.backup;
	rReq.permitted = PermitCb;
	rReq.cancelled = CancelCb;
	rReq.namePermitted = RestoreNamePermitCb;
	rReq.sourceSet = seed.backupSet;
	rReq.sourceIdentity = seed.backupIdentity;
	rReq.sourceName = "Alpha.bes";
	rReq.sourceStamp = seed.backupStamp;
	rReq.targetName = "alpha.bes";
	CeCareerStorageResult resVar = CeCareerRestoreAsNew(rReq);
	Check(resVar.status == CeCareerStorageStatus::Occupied && resVar.newFile.empty(), "restore-case-variant-refused");

	// Verify original casing and bytes are preserved on NTFS.
	HANDLE ah = CreateFileW(Pre(Path(L"user\\savegames\\Alpha.bes")).c_str(), FILE_READ_ATTRIBUTES, 7, NULL,
							OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	Check(ah != INVALID_HANDLE_VALUE, "reopen-alpha-for-case");
	std::wstring alphaNorm = FinalPath(ah, VOLUME_NAME_NONE | FILE_NAME_NORMALIZED);
	CloseHandle(ah);
	size_t aslash = alphaNorm.find_last_of(L'\\');
	std::wstring alphaLeaf = aslash == std::wstring::npos ? alphaNorm : alphaNorm.substr(aslash + 1);
	Check(alphaLeaf == L"Alpha.bes" && ReadAllFile(Path(L"user\\savegames\\Alpha.bes")) == alpha &&
			  ReadAllFile(Path(L"game\\savegames\\GameSlot.bes")) == gameSlot,
		  "originals-case-and-bytes-preserved");

	// Optional sub-probe: NTFS per-directory FileCaseSensitiveInfo (may be denied on limited Windows tokens).
	std::wstring csUser = Path(L"cs-user");
	Mkdir(csUser);
	std::wstring csSaves = csUser + L"\\savegames";
	Mkdir(csSaves);
	HANDLE csDir = CreateFileW(Pre(csSaves).c_str(), FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES | DELETE | SYNCHRONIZE,
							   FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (csDir != INVALID_HANDLE_VALUE)
	{
		struct CaseSensInfo { ULONG Flags; } info = {1u};
		const FILE_INFO_BY_HANDLE_CLASS kFileCaseSensitiveInfo = static_cast<FILE_INFO_BY_HANDLE_CLASS>(23);
		BOOL setOk = SetFileInformationByHandle(csDir, kFileCaseSensitiveInfo, &info, sizeof info);
		DWORD setErr = setOk ? 0 : GetLastError();
		CloseHandle(csDir);
		if (setOk)
		{
			WriteBytes(csSaves + L"\\Twin.bes", alpha);
			HANDLE second = CreateFileW(Pre(csSaves + L"\\twin.bes").c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
										FILE_ATTRIBUTE_NORMAL, NULL);
			if (second != INVALID_HANDLE_VALUE)
			{
				DWORD put = 0;
				WriteFile(second, other.data(), (DWORD)other.size(), &put, NULL);
				FlushFileBuffers(second);
				CloseHandle(second);
				CeCareerStorageRequest csReq = bReq;
				csReq.user = PathUtf8(L"cs-user");
				CeCareerStorageResult csRes = CeCareerBackupAll(csReq);
				Check(csRes.status == CeCareerStorageStatus::Refused && csRes.backupSet.empty(),
					  "case-sensitive-dir-collision-refused");
				Emit("{\"probe\":\"ntfs-per-dir-case-sensitive\",\"status\":\"EXERCISED\",\"passed\":true}");
			}
			else
			{
				Emit("{\"probe\":\"ntfs-per-dir-case-sensitive\",\"status\":\"UNSUPPORTED_OR_DENIED\",\"winerror\":" +
					 std::to_string((unsigned long)GetLastError()) + ",\"passed\":false}");
			}
		}
		else
		{
			Emit("{\"probe\":\"ntfs-per-dir-case-sensitive\",\"status\":\"UNSUPPORTED_OR_DENIED\",\"winerror\":" +
				 std::to_string((unsigned long)setErr) + ",\"passed\":false}");
		}
	}
	else
	{
		Emit("{\"probe\":\"ntfs-per-dir-case-sensitive\",\"status\":\"UNSUPPORTED_OR_DENIED\",\"winerror\":" +
			 std::to_string((unsigned long)GetLastError()) + ",\"passed\":false}");
	}
}

struct CaseSpec
{
	const wchar_t *name;
	void (*fn)();
	bool ntfsOnly;			// may return exit 3 (UNSUPPORTED_OR_DENIED) in --allow-wine mode
	bool privilegeOptional;	// may return exit 3 on non-elevated native Windows without Developer Mode
};

static const CaseSpec kCases[] = {
	{L"backup-restore-roundtrip", CaseBackupRestoreRoundtrip, false, false},
	{L"external-publish-collision", CaseExternalPublishCollision, false, false},
	{L"backup-preview-readonly", CaseBackupPreviewReadonly, false, false},
	{L"reserved-names-ads", CaseReservedNamesAndAds, false, false},
	{L"hardlink-refusal", CaseHardlinkRefusal, false, false},
	{L"readonly-rollback-preservation", CaseReadonlyRollbackPreservation, false, false},
	{L"junction-escape", CaseJunctionEscape, true, false},
	{L"symlink-refusal", CaseSymlinkRefusal, true, true},
	{L"case-sensitivity", CaseCaseSensitivity, true, false},
};

void Preflight()
{
	HANDLE token = NULL;
	TOKEN_ELEVATION elev = {};
	DWORD sz = 0;
	BOOL tokenOk = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
				   GetTokenInformation(token, TokenElevation, &elev, sizeof elev, &sz);
	if (token)
		CloseHandle(token);
	UINT acp = GetACP();
	WCHAR volume[260] = {}, fs[64] = {};
	BOOL volOk = GetVolumePathNameW(gRoot.c_str(), volume, 260) &&
				 GetVolumeInformationW(volume, NULL, 0, NULL, NULL, NULL, fs, 64);
	UINT driveType = volOk ? GetDriveTypeW(volume) : 0;
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	bool wineDetected = ntdll != NULL && GetProcAddress(ntdll, "wine_get_version") != NULL;

	Emit(std::string("{\"runtime_mode\":") + JsonAscii(gAllowWine ? "wine_dry_run" : "native_ntfs") +
		 ",\"filesystem\":" + JsonWide(fs) +
		 ",\"drive_type\":" + std::to_string(driveType) +
		 ",\"token_elevated\":" + std::to_string(elev.TokenIsElevated ? 1 : 0) +
		 ",\"ansi_codepage\":" + std::to_string(acp) +
		 ",\"wine_detected\":" + std::to_string(wineDetected ? 1 : 0) +
		 ",\"root_utf16\":" + JsonWide(gRoot) + "}");

	if (!gAllowWine)
	{
		Check(!wineDetected, "native-windows-not-wine");
		Check(tokenOk && !elev.TokenIsElevated, "least-privilege-required");
		Check(acp == CP_UTF8, "production-utf8-manifest-active");
		Check(volOk && driveType == DRIVE_FIXED && !_wcsicmp(fs, L"NTFS"), "native-gate-requires-local-fixed-ntfs");
	}
}

int RunAllChildren()
{
	Mkdir(Path(L"cases"));
	int requiredFailures = 0;
	for (const CaseSpec &cs : kCases)
	{
		std::wstring childRoot = Path(L"cases\\") + cs.name;
		std::wstring args = L"--root \"" + childRoot + L"\" --case " + cs.name;
		if (gAllowWine)
			args += L" --allow-wine";
		ChildOutput res = RunSelf(args, Path(std::wstring(cs.name) + L".jsonl"), Path(std::wstring(cs.name) + L".stderr.log"));
		bool optional = res.exitCode == 3 && (gAllowWine ? cs.ntfsOnly : cs.privilegeOptional);
		const char *status = res.exitCode == 0 ? "PASS" : (res.exitCode == 3 ? "UNSUPPORTED_OR_DENIED" : "FAIL");
		if (res.exitCode == 3)
			gUnsupported++;
		Emit(std::string("{\"case\":") + JsonWide(cs.name) +
			 ",\"exit\":" + std::to_string((unsigned long)res.exitCode) +
			 ",\"status\":" + JsonAscii(status) +
			 ",\"optional_not_exercised\":" + (optional ? "true" : "false") + "}");
		if (res.exitCode != 0 && !optional)
		{
			gFailures++;
			requiredFailures++;
		}
	}
	return requiredFailures ? 1 : 0;
}

} // namespace

int main()
{
	int wargc = 0;
	LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
	if (!wargv)
		return 2;
	std::wstring requested, which;
	for (int i = 1; i < wargc; i++)
	{
		std::wstring a = wargv[i];
		if (a == L"--root" && i + 1 < wargc)
			requested = wargv[++i];
		else if (a == L"--case" && i + 1 < wargc)
			which = wargv[++i];
		else if (a == L"--allow-wine")
			gAllowWine = true;
		else
		{
			LocalFree(wargv);
			return 2;
		}
	}
	LocalFree(wargv);
	if (requested.empty() || requested.size() > 180 || requested.find(L'"') != std::wstring::npos)
		return 2;
	WCHAR absolute[260] = {};
	DWORD len = GetFullPathNameW(requested.c_str(), 260, absolute, NULL);
	if (!len || len >= 260)
		return 2;
	gRoot = absolute;
	WCHAR exePath[512] = {};
	DWORD exeLen = GetModuleFileNameW(NULL, exePath, 512);
	if (!exeLen || exeLen >= 512)
		return 2;
	gExe = exePath;
	if (GetFileAttributesW(Pre(gRoot).c_str()) != INVALID_FILE_ATTRIBUTES || !CreateDirectoryW(Pre(gRoot).c_str(), NULL))
		return 2;
	gReceipt = _wfopen(Path(L"results.jsonl").c_str(), L"wb");
	if (!gReceipt)
		return 2;

	int result = 1;
	try
	{
		Preflight();
		if (which.empty())
		{
			result = RunAllChildren();
		}
		else
		{
			bool found = false;
			for (const CaseSpec &cs : kCases)
			{
				if (which == cs.name)
				{
					found = true;
					cs.fn();
					result = 0;
					break;
				}
			}
			Check(found, "known-ntfs-case");
		}
	}
	catch (const UnsupportedProbe &u)
	{
		gUnsupported++;
		Emit(std::string("{\"case\":") + JsonWide(which) +
			 ",\"probe\":" + JsonAscii(u.probe) +
			 ",\"status\":\"UNSUPPORTED_OR_DENIED\",\"winerror\":" + std::to_string((unsigned long)u.error) +
			 ",\"passed\":false}");
		result = 3;
	}
	catch (const std::exception &)
	{
		result = 1;
	}

	Emit(std::string("{\"exit\":") + std::to_string(result) +
		 ",\"failures\":" + std::to_string(gFailures) +
		 ",\"unsupported\":" + std::to_string(gUnsupported) +
		 ",\"native_windows_acceptance\":" + (!gAllowWine && result == 0 ? "true" : "false") +
		 ",\"native_executed\":true}");
	if (fclose(gReceipt) != 0)
		result = 1;
	gReceipt = NULL;

	HANDLE done = CreateFileW(Path(L"done.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (done == INVALID_HANDLE_VALUE)
		return 1;
	const char *marker = result == 0 ? "PASS\n" : "FAIL\n";
	DWORD written = 0;
	BOOL ok = WriteFile(done, marker, (DWORD)strlen(marker), &written, NULL) && written == strlen(marker);
	BOOL closed = CloseHandle(done);
	return (ok && closed) ? result : 1;
}
