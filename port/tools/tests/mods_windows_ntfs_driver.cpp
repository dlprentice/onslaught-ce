// SPDX-License-Identifier: GPL-3.0-only
// port/tools/tests/mods_windows_ntfs_driver.cpp: standalone native Windows NTFS acceptance fixture for the
// production mod loader (port/platform/common/mods.cpp built with -DPORT_MODS_WINDOWS_ON=1 and CE_VERSION).
//
// CLI:
//   mods-windows-ntfs.exe --root NEW_PRIVATE_DIRECTORY [--case NAME] [--allow-wine]
//   mods-windows-ntfs.exe --check-mods [id ...]   (handled before main by mods.cpp's CheckCommand)
//
// Without --case, spawns one isolated child process per case so mods.cpp's process-global startup table starts
// fresh for every probe. Emits pure ASCII JSONL to stdout and ROOT\results.jsonl, and writes ROOT\done.txt
// (PASS\n or FAIL\n) only after ROOT\results.jsonl is closed.
//
// Honest native vs Wine contract:
// - Default mode (no --allow-wine) is for the native Windows NTFS gate: requires a non-elevated token, active
//   UTF-8 ANSI code page (from utf8.manifest), and a local fixed NTFS volume. Junctions and normalized case
//   checks MUST execute and pass; symbolic links report UNSUPPORTED_OR_DENIED (exit 3, optional_not_exercised)
//   only if the token lacks SeCreateSymbolicLinkPrivilege and Developer Mode is off.
// - --allow-wine is for host/Wine dry-run verification: core loader, snapshot, --check-mods, hard-link,
//   reserved-name/ADS-path, duplicate/precedence and file-swap/identity checks MUST pass; NTFS-only probes
//   whose OS primitives Wine does not implement emit UNSUPPORTED_OR_DENIED (exit 3) and are never reported
//   as native Windows acceptance.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "mods.h"
extern "C" {
#include "sha256.h"
}

// ---- production loader hooks backed strictly by BEA_USER_DIR / BEA_GAME_DIR (never creates folders) --------------------
namespace {

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

std::wstring EnvWide(const wchar_t *name)
{
	DWORD n = GetEnvironmentVariableW(name, NULL, 0);
	if (!n)
		return L"";
	std::wstring out((size_t)n, L'\0');
	DWORD got = GetEnvironmentVariableW(name, &out[0], n);
	if (!got || got >= n)
		return L"";
	out.resize(got);
	return out;
}

} // namespace

const char *PortUserDir()
{
	static std::string cached;
	std::wstring w = EnvWide(L"BEA_USER_DIR");
	cached = w.empty() ? "." : Utf8FromWide(w);
	return cached.c_str();
}

bool PortGameHasFile(const char *rel)
{
	if (!rel || !*rel)
		return false;
	std::wstring base = EnvWide(L"BEA_GAME_DIR");
	if (base.empty())
		base = L".";
	std::wstring tail = WideFromUtf8(rel);
	if (tail.empty())
		return false;
	for (wchar_t &c : tail)
		if (c == L'/')
			c = L'\\';
	std::wstring full = base + L"\\" + tail;
	DWORD attr = GetFileAttributesW(full.c_str());
	return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

void PortPathsNamesOnly() {}

extern "C" int CE_SettingGetText(const char *key, char *out, int n)
{
	if (out && n > 0)
		out[0] = 0;
	if (!key || !*key || !out || n <= 1)
		return 0;
	std::wstring user = EnvWide(L"BEA_USER_DIR");
	if (user.empty())
		return 0;
	std::wstring ini = user + L"\\ce.ini";
	HANDLE h = CreateFileW(ini.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
						   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	std::string text;
	char buf[4096];
	DWORD got = 0;
	while (ReadFile(h, buf, sizeof buf, &got, NULL) && got > 0 && text.size() < (256u << 10))
		text.append(buf, got);
	CloseHandle(h);
	const size_t klen = strlen(key);
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t nl = text.find('\n', pos);
		std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = nl == std::string::npos ? text.size() : nl + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
			line.pop_back();
		size_t first = 0;
		while (first < line.size() && (line[first] == ' ' || line[first] == '\t'))
			first++;
		line.erase(0, first);
		if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[')
			continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string k = line.substr(0, eq);
		while (!k.empty() && (k.back() == ' ' || k.back() == '\t'))
			k.pop_back();
		if (k.size() != klen || _stricmp(k.c_str(), key) != 0)
			continue;
		std::string v = line.substr(eq + 1);
		size_t vfirst = 0;
		while (vfirst < v.size() && (v[vfirst] == ' ' || v[vfirst] == '\t'))
			vfirst++;
		v.erase(0, vfirst);
		if (v.empty() || (int)v.size() >= n)
			return 0;
		memcpy(out, v.data(), v.size() + 1);
		return 1;
	}
	return 0;
}

extern "C" int CE_SettingSetText(const char *, const char *) { return 0; }
extern "C" int CE_SettingSetEmpty(const char *) { return 0; }

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

std::string Sha256Hex(const std::string &bytes)
{
	SHA256_CTX ctx;
	unsigned char digest[32];
	sha256_init(&ctx);
	if (!bytes.empty())
		sha256_update(&ctx, reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
	sha256_final(&ctx, digest);
	static const char hex[] = "0123456789abcdef";
	std::string out;
	out.reserve(64);
	for (unsigned char b : digest)
	{
		out += hex[b >> 4];
		out += hex[b & 15];
	}
	return out;
}

void Mkdir(const std::wstring &p)
{
	BOOL ok = CreateDirectoryW(Pre(p).c_str(), NULL);
	if (!ok && GetLastError() == ERROR_ALREADY_EXISTS)
		return;
	Check(ok != 0, "create-owned-directory");
}

void WriteBytes(const std::wstring &p, const std::string &bytes, DWORD disposition = CREATE_NEW)
{
	HANDLE h = CreateFileW(Pre(p).c_str(), GENERIC_WRITE, 0, NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
	Check(h != INVALID_HANDLE_VALUE, "write-fixture-open");
	DWORD wrote = 0;
	BOOL ok = bytes.empty() ? TRUE : (WriteFile(h, bytes.data(), (DWORD)bytes.size(), &wrote, NULL) && wrote == bytes.size());
	BOOL closed = CloseHandle(h);
	Check(ok && closed, "write-fixture-exact");
}

std::string ReadAllHandle(HANDLE h)
{
	std::string out;
	char buf[4096];
	DWORD got = 0;
	while (ReadFile(h, buf, sizeof buf, &got, NULL) && got > 0)
		out.append(buf, got);
	return out;
}

std::string ReadAllFile(const std::wstring &p)
{
	HANDLE h = CreateFileW(Pre(p).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
						   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	Check(h != INVALID_HANDLE_VALUE, "read-fixture-open");
	std::string out = ReadAllHandle(h);
	CloseHandle(h);
	return out;
}

std::wstring FinalPath(HANDLE h, DWORD flags)
{
	std::vector<wchar_t> buf(32768);
	DWORD n = GetFinalPathNameByHandleW(h, buf.data(), (DWORD)buf.size(), flags);
	if (n == 0 || n >= buf.size())
		return L"";
	return std::wstring(buf.data(), n);
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

struct ModFileSpec
{
	std::string rel;	// '/'-separated manifest path
	std::string bytes;	// on-disk bytes
	std::string manifestPath;	// if non-empty, overrides path written to mod.json
	long long manifestSize = -1;
	std::string manifestSha;
};

void EnsureParentDirs(const std::wstring &rootDir, const std::string &rel)
{
	std::wstring cur = rootDir;
	size_t start = 0;
	while (true)
	{
		size_t slash = rel.find('/', start);
		if (slash == std::string::npos)
			break;
		std::string part = rel.substr(start, slash - start);
		cur += L"\\" + WideFromUtf8(part.c_str());
		Mkdir(cur);
		start = slash + 1;
	}
}

void WriteMod(const std::wstring &userDir, const std::wstring &gameDir, const std::string &id,
			  const std::vector<ModFileSpec> &files, bool createDiskFiles = true, bool createGameFiles = true)
{
	Mkdir(userDir + L"\\mods");
	std::wstring modDir = userDir + L"\\mods\\" + WideFromUtf8(id.c_str());
	Mkdir(modDir);
	std::string json = "{\"schema\":1,\"id\":\"" + id + "\",\"name\":\"NTFS Fixture " + id +
					   "\",\"version\":\"1.0.0\",\"author\":\"Fixture\",\"files\":[";
	for (size_t i = 0; i < files.size(); i++)
	{
		const ModFileSpec &f = files[i];
		if (createDiskFiles && !f.rel.empty())
		{
			EnsureParentDirs(modDir, f.rel);
			std::wstring tail = WideFromUtf8(f.rel.c_str());
			for (wchar_t &c : tail)
				if (c == L'/')
					c = L'\\';
			WriteBytes(modDir + L"\\" + tail, f.bytes, CREATE_ALWAYS);
		}
		if (createGameFiles && !f.rel.empty())
		{
			EnsureParentDirs(gameDir, f.rel);
			std::wstring tail = WideFromUtf8(f.rel.c_str());
			for (wchar_t &c : tail)
				if (c == L'/')
					c = L'\\';
			std::wstring gpath = gameDir + L"\\" + tail;
			if (GetFileAttributesW(Pre(gpath).c_str()) == INVALID_FILE_ATTRIBUTES)
				WriteBytes(gpath, "base-game-content-" + f.rel, CREATE_NEW);
		}
		std::string mpath = f.manifestPath.empty() ? f.rel : f.manifestPath;
		unsigned long long msize = f.manifestSize >= 0 ? (unsigned long long)f.manifestSize : (unsigned long long)f.bytes.size();
		std::string msha = f.manifestSha.empty() ? Sha256Hex(f.bytes) : f.manifestSha;
		if (i)
			json += ",";
		json += "{\"path\":" + JsonAscii(mpath) + ",\"size\":" + std::to_string(msize) +
				",\"sha256\":\"" + msha + "\"}";
	}
	json += "]}";
	WriteBytes(modDir + L"\\mod.json", json, CREATE_ALWAYS);
}

void SetupUserAndGame(const std::string &modsLine)
{
	Mkdir(Path(L"game"));
	Mkdir(Path(L"game\\data"));
	Mkdir(Path(L"user"));
	if (!modsLine.empty())
		WriteBytes(Path(L"user\\ce.ini"), "mods=" + modsLine + "\n", CREATE_ALWAYS);
	Check(SetEnvironmentVariableW(L"BEA_GAME_DIR", Path(L"game").c_str()) != 0, "set-game-dir");
	Check(SetEnvironmentVariableW(L"BEA_USER_DIR", Path(L"user").c_str()) != 0, "set-user-dir");
}

struct ChildOutput
{
	DWORD exitCode = 1;
	std::string text;
	std::string errText;
};

ChildOutput RunSelf(const std::wstring &args, const std::wstring &outFile, const std::wstring &errFile,
					const std::wstring *overrideUser = NULL, const std::wstring *overrideGame = NULL)
{
	std::wstring prevUser = EnvWide(L"BEA_USER_DIR");
	std::wstring prevGame = EnvWide(L"BEA_GAME_DIR");
	if (overrideUser)
		SetEnvironmentVariableW(L"BEA_USER_DIR", overrideUser->c_str());
	if (overrideGame)
		SetEnvironmentVariableW(L"BEA_GAME_DIR", overrideGame->c_str());

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

	if (overrideUser)
		SetEnvironmentVariableW(L"BEA_USER_DIR", prevUser.empty() ? NULL : prevUser.c_str());
	if (overrideGame)
		SetEnvironmentVariableW(L"BEA_GAME_DIR", prevGame.empty() ? NULL : prevGame.c_str());

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
	return {code, ReadAllFile(outFile), ReadAllFile(errFile)};
}

// ---- Case 1: clean mod load + read-only snapshot isolation -------------------------------------------------------------
void CaseCleanSnapshot()
{
	SetupUserAndGame("good");
	const std::string payload = "verified-ntfs-mod-snapshot-payload-0123456789";
	const std::string nested = "nested-mod-payload-abcdef";
	WriteMod(Path(L"user"), Path(L"game"), "good",
			 {{"data/item.bin", payload, "", -1, ""}, {"data/sub/nested.bin", nested, "", -1, ""}});
	WriteBytes(Path(L"game\\data\\base_only.bin"), "base-only-content");

	PortModsInit();
	Check(PortModsActive() == 1, "clean-mod-active");
	Check(PortModHas("data/item.bin"), "clean-mod-has-listed");
	Check(PortModHas("data/sub/nested.bin"), "clean-mod-has-nested");
	Check(!PortModHas("data/base_only.bin"), "clean-mod-skips-unlisted");

	HANDLE snap = static_cast<HANDLE>(PortModOpenRead("data/item.bin"));
	Check(snap != NULL && snap != INVALID_HANDLE_VALUE, "snapshot-open");
	std::wstring snapPath = FinalPath(snap, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	Check(!snapPath.empty(), "snapshot-final-path");

	// Mutate source on disk while snapshot is open: snapshot must remain isolated.
	WriteBytes(Path(L"user\\mods\\good\\data\\item.bin"), "tampered-after-snapshot-open-9876543210!!!!!", CREATE_ALWAYS);
	Check(ReadAllHandle(snap) == payload, "snapshot-isolated-from-source-write");

	// Snapshot handle must be read-only.
	DWORD written = 0;
	SetLastError(0);
	BOOL wroteSnap = WriteFile(snap, "x", 1, &written, NULL);
	Check(!wroteSnap && GetLastError() == ERROR_ACCESS_DENIED, "snapshot-handle-readonly");

	// Snapshot file admits no second opener (share mode 0).
	HANDLE secondRead = CreateFileW(snapPath.c_str(), GENERIC_READ,
									FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	Check(secondRead == INVALID_HANDLE_VALUE, "snapshot-reopen-read-refused");
	HANDLE secondWrite = CreateFileW(snapPath.c_str(), GENERIC_WRITE,
									 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	Check(secondWrite == INVALID_HANDLE_VALUE, "snapshot-reopen-write-refused");

	// Held mod root folder cannot be renamed while loaded.
	BOOL renamedRoot = MoveFileExW(Pre(Path(L"user\\mods\\good")).c_str(), Pre(Path(L"user\\mods\\good-moved")).c_str(), 0);
	Check(!renamedRoot, "held-mod-root-rename-refused");

	// Closing snapshot removes temporary file.
	Check(CloseHandle(snap) != 0, "snapshot-close");
	Check(GetFileAttributesW(snapPath.c_str()) == INVALID_FILE_ATTRIBUTES, "snapshot-deleted-on-close");
}

// ---- Case 2: --check-mods CLI path (pass, refuse, read-only on absent user dir) ---------------------------------------
void CaseCheckMods()
{
	SetupUserAndGame("good,bad");
	const std::string goodBytes = "good-check-mods-bytes";
	const std::string otherBytes = "other-check-mods-bytes";
	WriteMod(Path(L"user"), Path(L"game"), "good", {{"data/item.bin", goodBytes, "", -1, ""}});
	WriteMod(Path(L"user"), Path(L"game"), "bad",
			 {{"data/item.bin", goodBytes, "", -1, std::string(64, '0')},
			  {"data/other.bin", otherBytes, "", (long long)otherBytes.size() + 1, Sha256Hex(otherBytes)}});

	ChildOutput both = RunSelf(L"--check-mods", Path(L"check-both.txt"), Path(L"check-both.err.txt"));
	Check(both.exitCode == 1, "check-mods-refuses-bad-exit-1");
	Check(both.text.find("refused bad: file data/item.bin: its SHA-256 is not the manifest's") != std::string::npos,
		  "check-mods-reports-sha-mismatch");
	Check(both.text.find("refused bad: file data/other.bin: its size is not the manifest's") != std::string::npos,
		  "check-mods-reports-size-mismatch");
	Check(both.text.find("result: 1 passed, 1 refused") != std::string::npos, "check-mods-reports-1-passed-1-refused");

	ChildOutput onlyGood = RunSelf(L"--check-mods good", Path(L"check-good.txt"), Path(L"check-good.err.txt"));
	Check(onlyGood.exitCode == 0, "check-mods-good-exit-0");
	Check(onlyGood.text.find("result: 1 passed, 0 refused") != std::string::npos, "check-mods-good-summary");

	std::wstring absent = Path(L"absent-user-dir");
	ChildOutput missingDefault = RunSelf(L"--check-mods", Path(L"check-absent.txt"), Path(L"check-absent.err.txt"), &absent, NULL);
	Check(missingDefault.exitCode == 0, "check-mods-absent-user-exit-0");
	Check(GetFileAttributesW(Pre(absent).c_str()) == INVALID_FILE_ATTRIBUTES, "check-mods-creates-no-user-dir");

	ChildOutput missingNamed = RunSelf(L"--check-mods good", Path(L"check-absent-named.txt"), Path(L"check-absent-named.err.txt"), &absent, NULL);
	Check(missingNamed.exitCode == 1, "check-mods-absent-named-exit-1");
	Check(missingNamed.text.find("refused good:") != std::string::npos, "check-mods-absent-named-refusal");
	Check(GetFileAttributesW(Pre(absent).c_str()) == INVALID_FILE_ATTRIBUTES, "check-mods-named-creates-no-user-dir");
}

// ---- Case 3: hard-link refusal at startup (NumberOfLinks != 1) ---------------------------------------------------------
void CaseHardlinkStartup()
{
	SetupUserAndGame("linked");
	WriteMod(Path(L"user"), Path(L"game"), "linked", {{"data/item.bin", "hardlink-startup-bytes", "", -1, ""}});
	std::wstring src = Path(L"user\\mods\\linked\\data\\item.bin");
	std::wstring second = Path(L"user\\mods\\linked\\second-name.bin");
	Check(CreateHardLinkW(Pre(second).c_str(), Pre(src).c_str(), NULL) != 0, "create-hardlink-startup");

	HANDLE h = CreateFileW(Pre(src).c_str(), FILE_READ_ATTRIBUTES, 7, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	Check(h != INVALID_HANDLE_VALUE, "hardlink-query-open");
	FILE_STANDARD_INFO stdInfo = {};
	BOOL got = GetFileInformationByHandleEx(h, FileStandardInfo, &stdInfo, sizeof stdInfo);
	CloseHandle(h);
	Check(got && stdInfo.NumberOfLinks == 2, "hardlink-number-of-links-is-2");

	PortModsInit();
	Check(PortModsActive() == 0, "hardlink-startup-refused");
	Check(!PortModHas("data/item.bin"), "hardlink-startup-not-claimed");
	Check(PortModOpenRead("data/item.bin") == NULL, "hardlink-startup-not-served");
}

// ---- Case 4: hard-link added at runtime after startup ------------------------------------------------------------------
void CaseHardlinkRuntime()
{
	SetupUserAndGame("rtlink");
	const std::string payload = "hardlink-runtime-bytes";
	WriteMod(Path(L"user"), Path(L"game"), "rtlink", {{"data/item.bin", payload, "", -1, ""}});

	PortModsInit();
	Check(PortModsActive() == 1, "hardlink-runtime-initial-load");
	HANDLE h = static_cast<HANDLE>(PortModOpenRead("data/item.bin"));
	Check(h != NULL && ReadAllHandle(h) == payload, "hardlink-runtime-initial-read");
	CloseHandle(h);

	std::wstring src = Path(L"user\\mods\\rtlink\\data\\item.bin");
	std::wstring second = Path(L"user\\mods\\rtlink\\second-runtime.bin");
	Check(CreateHardLinkW(Pre(second).c_str(), Pre(src).c_str(), NULL) != 0, "create-hardlink-runtime");
	Check(!PortModHas("data/item.bin"), "hardlink-runtime-not-claimed");
	Check(PortModOpenRead("data/item.bin") == NULL, "hardlink-runtime-not-served");
}

// ---- Case 5: junction refusal (mod data folder and mod root) -----------------------------------------------------------
void CaseJunction()
{
	SetupUserAndGame("jdata,jroot");
	const std::string payload = "junction-mod-bytes";
	WriteMod(Path(L"user"), Path(L"game"), "jdata", {{"data/item.bin", payload, "", -1, ""}});
	WriteMod(Path(L"user"), Path(L"game"), "jroot", {{"data/item.bin", payload, "", -1, ""}});

	// Move jdata\data aside and replace with a junction.
	std::wstring jdataDir = Path(L"user\\mods\\jdata\\data");
	std::wstring jdataTarget = Path(L"outside-jdata");
	Check(MoveFileExW(Pre(jdataDir).c_str(), Pre(jdataTarget).c_str(), 0) != 0, "junction-data-move-aside");
	DWORD err = MakeJunction(jdataDir, jdataTarget);
	if (err)
		throw UnsupportedProbe("create-ntfs-junction-data", err);
	DWORD attrs = GetFileAttributesW(Pre(jdataDir).c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT))
		throw UnsupportedProbe("ntfs-junction-reparse-attribute-not-visible", GetLastError());

	// Also move jroot aside and replace with a junction.
	std::wstring jrootDir = Path(L"user\\mods\\jroot");
	std::wstring jrootTarget = Path(L"outside-jroot");
	Check(MoveFileExW(Pre(jrootDir).c_str(), Pre(jrootTarget).c_str(), 0) != 0, "junction-root-move-aside");
	err = MakeJunction(jrootDir, jrootTarget);
	if (err)
		throw UnsupportedProbe("create-ntfs-junction-root", err);
	attrs = GetFileAttributesW(Pre(jrootDir).c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT))
		throw UnsupportedProbe("ntfs-junction-root-reparse-attribute-not-visible", GetLastError());

	// Prove both junctions resolve cleanly when followed normally (not dangling), so refusal is strictly reparse-point refusal.
	Check(ReadAllFile(Path(L"user\\mods\\jdata\\data\\item.bin")) == payload, "junction-data-live-target-reachable");
	Check(ReadAllFile(Path(L"user\\mods\\jroot\\data\\item.bin")) == payload, "junction-root-live-target-reachable");

	PortModsInit();
	Check(PortModsActive() == 0, "junction-mods-refused");
	Check(!PortModHas("data/item.bin"), "junction-not-claimed");
	Check(PortModOpenRead("data/item.bin") == NULL, "junction-not-served");
}

// ---- Case 6: directory symlink refusal ---------------------------------------------------------------------------------
void CaseDirSymlink()
{
	SetupUserAndGame("dsym");
	const std::string payload = "dir-symlink-bytes";
	WriteMod(Path(L"user"), Path(L"game"), "dsym", {{"data/item.bin", payload, "", -1, ""}});
	std::wstring dataDir = Path(L"user\\mods\\dsym\\data");
	std::wstring target = Path(L"outside-dsym");
	Check(MoveFileExW(Pre(dataDir).c_str(), Pre(target).c_str(), 0) != 0, "dir-symlink-move-aside");
	if (!CreateSymbolicLinkW(Pre(dataDir).c_str(), Unpre(target).c_str(),
							 SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
		throw UnsupportedProbe("create-directory-symlink", GetLastError());
	DWORD attrs = GetFileAttributesW(Pre(dataDir).c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT))
		throw UnsupportedProbe("dir-symlink-reparse-attribute-not-visible", GetLastError());
	Check(ReadAllFile(Path(L"user\\mods\\dsym\\data\\item.bin")) == payload, "dir-symlink-live-target-reachable");

	PortModsInit();
	Check(PortModsActive() == 0, "dir-symlink-refused");
	Check(PortModOpenRead("data/item.bin") == NULL, "dir-symlink-not-served");
}

// ---- Case 7: file symlink refusal --------------------------------------------------------------------------------------
void CaseFileSymlink()
{
	SetupUserAndGame("fsym");
	const std::string payload = "file-symlink-bytes";
	WriteMod(Path(L"user"), Path(L"game"), "fsym", {{"data/item.bin", payload, "", -1, ""}});
	std::wstring item = Path(L"user\\mods\\fsym\\data\\item.bin");
	std::wstring target = Path(L"outside-fsym.bin");
	Check(MoveFileExW(Pre(item).c_str(), Pre(target).c_str(), 0) != 0, "file-symlink-move-aside");
	if (!CreateSymbolicLinkW(Pre(item).c_str(), Unpre(target).c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
		throw UnsupportedProbe("create-file-symlink", GetLastError());
	DWORD attrs = GetFileAttributesW(Pre(item).c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT))
		throw UnsupportedProbe("file-symlink-reparse-attribute-not-visible", GetLastError());
	Check(ReadAllFile(item) == payload, "file-symlink-live-target-reachable");

	PortModsInit();
	Check(PortModsActive() == 0, "file-symlink-refused");
	Check(PortModOpenRead("data/item.bin") == NULL, "file-symlink-not-served");
}

// ---- Case 8: mid-chain junction swap after startup ---------------------------------------------------------------------
void CaseJunctionSwap()
{
	SetupUserAndGame("swapmod");
	const std::string payload = "mid-chain-junction-swap-bytes";
	WriteMod(Path(L"user"), Path(L"game"), "swapmod", {{"data/sub/item.bin", payload, "", -1, ""}});

	PortModsInit();
	Check(PortModsActive() == 1, "junction-swap-initial-load");
	HANDLE h = static_cast<HANDLE>(PortModOpenRead("data/sub/item.bin"));
	Check(h != NULL && ReadAllHandle(h) == payload, "junction-swap-initial-read");
	CloseHandle(h);

	std::wstring sub = Path(L"user\\mods\\swapmod\\data\\sub");
	std::wstring aside = Path(L"outside-sub-real");
	Check(MoveFileExW(Pre(sub).c_str(), Pre(aside).c_str(), 0) != 0, "junction-swap-move-aside");
	DWORD err = MakeJunction(sub, aside);
	if (err)
		throw UnsupportedProbe("create-mid-chain-junction-swap", err);
	DWORD attrs = GetFileAttributesW(Pre(sub).c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT))
		throw UnsupportedProbe("mid-chain-junction-reparse-attribute-not-visible", GetLastError());
	Check(ReadAllFile(Path(L"user\\mods\\swapmod\\data\\sub\\item.bin")) == payload, "junction-swap-live-target-reachable");

	Check(!PortModHas("data/sub/item.bin"), "junction-swap-not-claimed");
	Check(PortModOpenRead("data/sub/item.bin") == NULL, "junction-swap-not-served");
}

// ---- Case 9: Windows reserved device names, trailing dot/space, and Alternate Data Streams (:stream) -------------------
void CaseReservedNamesAndAds()
{
	SetupUserAndGame("good");
	const std::string payload = "unnamed-stream-verified-bytes";
	WriteMod(Path(L"user"), Path(L"game"), "good", {{"data/item.bin", payload, "", -1, ""}});

	// 1. Reserved device names as mod IDs must be refused by --check-mods.
	ChildOutput devIds = RunSelf(L"--check-mods con nul prn aux com1 lpt1", Path(L"check-dev-ids.txt"), Path(L"check-dev-ids.err.txt"));
	Check(devIds.exitCode == 1, "reserved-device-mod-ids-refused-exit-1");
	for (const char *id : {"con", "nul", "prn", "aux", "com1", "lpt1"})
	{
		std::string needle = std::string("refused ") + id + ": a Windows device name";
		Check(devIds.text.find(needle) != std::string::npos, ("reserved-device-mod-id-" + std::string(id)).c_str());
	}
	Check(devIds.text.find("result: 0 passed, 6 refused") != std::string::npos, "reserved-device-mod-ids-count");

	// 2. Reserved device names, trailing dot/space, short names, traversal and :stream in mod.json paths.
	struct BadPathCase
	{
		const char *id;
		const char *path;
		const char *expectSubstr;
	};
	static const BadPathCase kBadPaths[] = {
		{"dev-con", "data/CON", "Windows device name"},
		{"dev-nul", "data/NUL", "Windows device name"},
		{"dev-com1", "data/COM1", "Windows device name"},
		{"dev-lpt1", "data/LPT1", "Windows device name"},
		{"dev-con-ext", "data/con.txt", "Windows device name"},
		{"dev-sub-nul", "data/sub/nul.wav", "Windows device name"},
		{"trail-dot", "data/item.bin.", "ends with a space or a dot"},
		{"trail-space", "data/item.bin ", "ends with a space or a dot"},
		{"lead-space", "data/ item.bin", "starts with a space"},
		{"short-name", "data/PROGRA~1/item.bin", "short name"},
		{"ads-stream", "data/item.bin:stream", "character that is not allowed"},
		{"ads-data", "data/item.bin::$DATA", "character that is not allowed"},
		{"backslash", "data\\item.bin", "character that is not allowed"},
		{"dotdot", "data/../item.bin", "'.' or '..' component"},
	};
	std::wstring checkArgs = L"--check-mods";
	for (const auto &bc : kBadPaths)
	{
		WriteMod(Path(L"user"), Path(L"game"), bc.id,
				 {{"", payload, bc.path, (long long)payload.size(), Sha256Hex(payload)}}, false, false);
		checkArgs += L" " + WideFromUtf8(bc.id);
	}
	ChildOutput badPathsOut = RunSelf(checkArgs, Path(L"check-bad-paths.txt"), Path(L"check-bad-paths.err.txt"));
	Check(badPathsOut.exitCode == 1, "bad-paths-refused-exit-1");
	for (const auto &bc : kBadPaths)
	{
		std::string needle = std::string("refused ") + bc.id + ":";
		size_t pos = std::string::npos;
		for (size_t cur = 0; cur < badPathsOut.text.size();)
		{
			size_t eol = badPathsOut.text.find('\n', cur);
			size_t len = (eol == std::string::npos) ? (badPathsOut.text.size() - cur) : (eol - cur);
			if (len >= needle.size() && badPathsOut.text.compare(cur, needle.size(), needle) == 0)
			{
				pos = cur;
				break;
			}
			if (eol == std::string::npos)
				break;
			cur = eol + 1;
		}
		std::string line;
		if (pos != std::string::npos)
		{
			size_t eol = badPathsOut.text.find('\n', pos);
			line = eol == std::string::npos ? badPathsOut.text.substr(pos) : badPathsOut.text.substr(pos, eol - pos);
		}
		Check(pos != std::string::npos && line.find(bc.expectSubstr) != std::string::npos,
			  ("bad-path-refused-" + std::string(bc.id)).c_str());
	}
	Check(badPathsOut.text.find("result: 0 passed, 14 refused") != std::string::npos, "bad-paths-refused-count");

	// 3. Real NTFS Alternate Data Stream attached on disk alongside valid file: verify it is a true ADS
	// (not a literal colon filename created by a POSIX filesystem under Wine), and unnamed stream remains exact.
	std::wstring dataDir = Path(L"user\\mods\\good\\data");
	std::wstring adsPath = Pre(dataDir + L"\\item.bin:hidden");
	HANDLE ads = CreateFileW(adsPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (ads != INVALID_HANDLE_VALUE)
	{
		const char extra[] = "alternate-data-stream-payload-must-not-affect-unnamed-stream";
		DWORD wrote = 0;
		BOOL ok = WriteFile(ads, extra, (DWORD)(sizeof(extra) - 1), &wrote, NULL) && wrote == sizeof(extra) - 1;
		CloseHandle(ads);
		Check(ok != 0, "ntfs-ads-attach-write");
		// On real NTFS, a directory listing of data\* shows only item.bin (no entry containing ':').
		bool sawLiteralColon = false;
		WIN32_FIND_DATAW fd = {};
		HANDLE fh = FindFirstFileW(Pre(dataDir + L"\\*").c_str(), &fd);
		Check(fh != INVALID_HANDLE_VALUE, "ntfs-ads-list-data-dir");
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

	PortModsInit();
	Check(PortModsActive() == 1, "valid-mod-loads-with-ads-control");
	HANDLE snap = static_cast<HANDLE>(PortModOpenRead("data/item.bin"));
	Check(snap != NULL && ReadAllHandle(snap) == payload, "unnamed-stream-bytes-exact");
	CloseHandle(snap);
}

// ---- Case 10: case-insensitive duplicate/collision handling & file-swap/identity-change refusal ------------------------
void CaseCollisionAndIdentity()
{
	SetupUserAndGame("first,second");
	const std::string firstBytes = "first-mod-item-bytes-1111111111";
	const std::string secondBytes = "second-mod-item-bytes-222222222";
	const std::string otherBytes = "other-mod-file-bytes-3333333333";
	WriteMod(Path(L"user"), Path(L"game"), "first", {{"data/item.bin", firstBytes, "", -1, ""}});
	WriteMod(Path(L"user"), Path(L"game"), "second",
			 {{"data/item.bin", secondBytes, "", -1, ""}, {"data/other.bin", otherBytes, "", -1, ""}});

	// 1. Case-insensitive duplicate in one manifest must be refused ("listed twice").
	WriteMod(Path(L"user"), Path(L"game"), "dupcase",
			 {{"data/item.bin", firstBytes, "data/item.bin", -1, ""},
			  {"", firstBytes, "data/ITEM.BIN", (long long)firstBytes.size(), Sha256Hex(firstBytes)}},
			 true, true);
	ChildOutput dupOut = RunSelf(L"--check-mods dupcase", Path(L"check-dupcase.txt"), Path(L"check-dupcase.err.txt"));
	Check(dupOut.exitCode == 1, "case-insensitive-duplicate-refused");
	Check(dupOut.text.find("listed twice") != std::string::npos, "case-insensitive-duplicate-reason");

	// 2. Two mods replacing the same file: later mod wins regardless of lookup case or slash style.
	PortModsInit();
	Check(PortModsActive() == 2, "two-mods-active");
	for (const char *query : {"data/item.bin", "DATA/ITEM.BIN", "Data\\Item.Bin"})
	{
		HANDLE h = static_cast<HANDLE>(PortModOpenRead(query));
		Check(h != NULL && ReadAllHandle(h) == secondBytes, "case-insensitive-lookup-serves-winner");
		CloseHandle(h);
	}

	// 3. File-swap with IDENTICAL bytes and restored timestamps must be refused by file identity (FileIdInfo / ChangeTime).
	std::wstring victim = Path(L"user\\mods\\second\\data\\item.bin");
	FILETIME created = {}, accessed = {}, written = {};
	HANDLE orig = CreateFileW(Pre(victim).c_str(), FILE_READ_ATTRIBUTES, 7, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	Check(orig != INVALID_HANDLE_VALUE && GetFileTime(orig, &created, &accessed, &written), "capture-original-times");
	CloseHandle(orig);
	Check(DeleteFileW(Pre(victim).c_str()) != 0, "delete-original-for-swap");
	HANDLE swapped = CreateFileW(Pre(victim).c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	Check(swapped != INVALID_HANDLE_VALUE, "create-swapped-file");
	DWORD put = 0;
	Check(WriteFile(swapped, secondBytes.data(), (DWORD)secondBytes.size(), &put, NULL) && put == secondBytes.size(),
		  "write-swapped-identical-bytes");
	Check(SetFileTime(swapped, &created, &accessed, &written) != 0, "restore-swapped-times");
	CloseHandle(swapped);
	Check(!PortModHas("data/item.bin"), "swapped-file-not-claimed");
	Check(PortModOpenRead("data/item.bin") == NULL, "swapped-file-refused");

	// 4. In-place overwrite with equal-length tampered bytes must be refused.
	std::wstring other = Path(L"user\\mods\\second\\data\\other.bin");
	std::string tampered(otherBytes.size(), 'Z');
	WriteBytes(other, tampered, CREATE_ALWAYS);
	Check(!PortModHas("data/other.bin"), "tampered-file-not-claimed");
	Check(PortModOpenRead("data/other.bin") == NULL, "tampered-file-refused");
}

// ---- Case 11: NTFS exact on-disk spelling (NameIsExact) ----------------------------------------------------------------
void CaseExactSpelling()
{
	SetupUserAndGame("good");
	// First verify whether this OS/FS returns the on-disk spelling via GetFinalPathNameByHandleW(FILE_NAME_NORMALIZED).
	std::wstring probeDir = Path(L"case-probe");
	Mkdir(probeDir);
	WriteBytes(probeDir + L"\\CaseProbe.txt", "probe", CREATE_NEW);
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

	// On real NTFS: disk holds lowercase data\item.bin, manifest spells Data/Item.bin -> must be refused.
	const std::string payload = "exact-case-payload";
	WriteMod(Path(L"user"), Path(L"game"), "good",
			 {{"data/item.bin", payload, "Data/Item.bin", (long long)payload.size(), Sha256Hex(payload)}});
	PortModsInit();
	Check(PortModsActive() == 0, "wrong-case-manifest-refused-on-ntfs");
	Check(PortModOpenRead("data/item.bin") == NULL, "wrong-case-file-not-served");
}

struct CaseSpec
{
	const wchar_t *name;
	void (*fn)();
	bool ntfsOnly;			// may return exit 3 (UNSUPPORTED_OR_DENIED) in --allow-wine mode
	bool privilegeOptional;	// may return exit 3 on non-elevated native Windows without Developer Mode
};

static const CaseSpec kCases[] = {
	{L"clean-snapshot", CaseCleanSnapshot, false, false},
	{L"check-mods", CaseCheckMods, false, false},
	{L"hardlink-startup", CaseHardlinkStartup, false, false},
	{L"hardlink-runtime", CaseHardlinkRuntime, false, false},
	{L"junction", CaseJunction, true, false},
	{L"dir-symlink", CaseDirSymlink, true, true},
	{L"file-symlink", CaseFileSymlink, true, true},
	{L"junction-swap", CaseJunctionSwap, true, false},
	{L"reserved-names-ads", CaseReservedNamesAndAds, false, false},
	{L"case-collision-identity", CaseCollisionAndIdentity, false, false},
	{L"case-exact", CaseExactSpelling, true, false},
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

	Emit(std::string("{\"runtime_mode\":") + JsonAscii(gAllowWine ? "wine_dry_run" : "native_ntfs") +
		 ",\"filesystem\":" + JsonWide(fs) +
		 ",\"drive_type\":" + std::to_string(driveType) +
		 ",\"token_elevated\":" + std::to_string(elev.TokenIsElevated ? 1 : 0) +
		 ",\"ansi_codepage\":" + std::to_string(acp) +
		 ",\"root_utf16\":" + JsonWide(gRoot) + "}");

	if (!gAllowWine)
	{
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
