// SPDX-License-Identifier: GPL-3.0-only
#include "../common/ce_career_restore.h"

#ifndef _WIN32
// Host-only stand-in compiled on Linux by UI seam checks when building with
// -DCE_RESTORE_UI_TEST_WINDOWS_COPY=1.
CeCareerStorageResult CeCareerBackupAll(const CeCareerStorageRequest &) {
  CeCareerStorageResult out;
  out.status = CeCareerStorageStatus::Unsupported;
  return out;
}
CeCareerStorageResult CeCareerTakeFullSet(const CeCareerStorageRequest &) {
  CeCareerStorageResult out;
  out.status = CeCareerStorageStatus::Unsupported;
  return out;
}
CeCareerStorageResult CeCareerRestoreAsNew(const CeCareerRestoreRequest &) {
  CeCareerStorageResult out;
  out.status = CeCareerStorageStatus::Unsupported;
  return out;
}
bool CeCareerRestoreStamp(const CeCareerRestoreRequest &, std::string &stamp) {
  stamp.clear();
  return false;
}
CeCareerStorageStatus CeCareerRestoreSuggest(const CeCareerRestoreRequest &,
                                             std::string &targetName) {
  targetName.clear();
  return CeCareerStorageStatus::Unsupported;
}
#else
#include "ce_backup_paths.h"
#include "../common/ce_backup_name.h"
#include "../common/ce_backup_verify.h"
#include "../common/ce_win_reparse.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {
using Status = CeCareerStorageStatus;
using Bytes = std::vector<unsigned char>;
using namespace ce_backup_paths;

struct Failure {
  Status status;
};

void require(bool ok, Status status = Status::Refused) {
  if (!ok)
    throw Failure{status};
}

constexpr size_t kMemberLimit = 2u * 1024u * 1024u;
constexpr size_t kSetLimit = 64u * 1024u * 1024u;
constexpr size_t kCountLimit = 512;
constexpr size_t kDirectoryEntryLimit = 2048;
const char *const kOptions = "defaultoptions.bea";
const wchar_t *const kOptionsWide = L"defaultoptions.bea";
const wchar_t *const kLockWide = L".ce-career-custody.lock";
// The manifest's Reason, which Backup Preview reads to say what a set holds.
// Older sets keep theirs: "Manual" is the set Restore as New makes first.
const char *const kFullSetReason = "All careers";
const char *const kRestoreReason = "Manual";

constexpr NTSTATUS kStatusInvalidDeviceRequest =
    static_cast<NTSTATUS>(0xc0000010u);
constexpr NTSTATUS kStatusObjectNameNotFound =
    static_cast<NTSTATUS>(0xc0000034u);
constexpr NTSTATUS kStatusObjectNameCollision =
    static_cast<NTSTATUS>(0xc0000035u);
constexpr NTSTATUS kStatusObjectPathNotFound =
    static_cast<NTSTATUS>(0xc000003au);
constexpr NTSTATUS kStatusSharingViolation =
    static_cast<NTSTATUS>(0xc0000043u);
constexpr NTSTATUS kStatusNotSupported = static_cast<NTSTATUS>(0xc00000bbu);

decltype(&NtCreateFile) ntCreateFileFn() {
  static const auto fn = reinterpret_cast<decltype(&NtCreateFile)>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
  return fn;
}

decltype(&NtSetInformationFile) ntSetInformationFileFn() {
  static const auto fn = reinterpret_cast<decltype(&NtSetInformationFile)>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile"));
  return fn;
}

void closeNow(Handle &h) {
  if (h.h != INVALID_HANDLE_VALUE) {
    if (h.discard) {
      FILE_DISPOSITION_INFO d{TRUE};
      SetFileInformationByHandle(h.h, FileDispositionInfo, &d, sizeof(d));
    }
    CloseHandle(h.h);
    h.h = INVALID_HANDLE_VALUE;
    h.discard = false;
  }
}

void assignHandle(Handle &dst, Handle &&src) {
  closeNow(dst);
  dst.h = src.h;
  dst.discard = src.discard;
  src.h = INVALID_HANDLE_VALUE;
  src.discard = false;
}

Handle openChildRaw(HANDLE parent, const Wide &name, ACCESS_MASK access,
                    ULONG fileAttributes, ULONG shareAccess, ULONG disposition,
                    ULONG options, NTSTATUS *statusOut = nullptr) {
  const auto nt = ntCreateFileFn();
  if (!nt || name.empty() || name.size() > 255) {
    if (statusOut)
      *statusOut = static_cast<NTSTATUS>(0xc000000du);
    return Handle();
  }
  UNICODE_STRING leaf{};
  leaf.Length = leaf.MaximumLength =
      static_cast<USHORT>(name.size() * sizeof(wchar_t));
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

NTSTATUS renameNoReplace(HANDLE h, HANDLE targetParent, const Wide &newName) {
  const auto nt = ntSetInformationFileFn();
  if (!nt || newName.empty() || newName.size() > 255)
    return static_cast<NTSTATUS>(0xc000000du);
  const ULONG bytes = static_cast<ULONG>(newName.size() * sizeof(wchar_t));
  const ULONG total = static_cast<ULONG>(
      std::max<size_t>(sizeof(FILE_RENAME_INFORMATION),
                       offsetof(FILE_RENAME_INFORMATION, FileName) + bytes));
  alignas(FILE_RENAME_INFORMATION) unsigned char
      storage[sizeof(FILE_RENAME_INFORMATION) + 256 * sizeof(wchar_t)]{};
  auto *info = reinterpret_cast<FILE_RENAME_INFORMATION *>(storage);
  info->ReplaceIfExists = FALSE;
  info->RootDirectory = targetParent;
  info->FileNameLength = bytes;
  std::memcpy(info->FileName, newName.data(), bytes);
  IO_STATUS_BLOCK io{};
  return nt(h, &io, info, total, FileRenameInformation);
}

bool fillRandom(unsigned char *buf, size_t len) {
  using BCryptGenRandomFn = NTSTATUS(WINAPI *)(PVOID, PUCHAR, ULONG, ULONG);
  using RtlGenRandomFn = BOOLEAN(WINAPI *)(PVOID, ULONG);
  static const BCryptGenRandomFn bcrypt = []() -> BCryptGenRandomFn {
    HMODULE mod = LoadLibraryExW(L"bcrypt.dll", nullptr,
                                 LOAD_LIBRARY_SEARCH_SYSTEM32);
    return mod ? reinterpret_cast<BCryptGenRandomFn>(
                     GetProcAddress(mod, "BCryptGenRandom"))
               : nullptr;
  }();
  if (bcrypt)
    return bcrypt(nullptr, buf, static_cast<ULONG>(len), 0x00000002u) == 0;
  static const RtlGenRandomFn rtl = []() -> RtlGenRandomFn {
    HMODULE mod = LoadLibraryExW(L"advapi32.dll", nullptr,
                                 LOAD_LIBRARY_SEARCH_SYSTEM32);
    return mod ? reinterpret_cast<RtlGenRandomFn>(
                     GetProcAddress(mod, "SystemFunction036"))
               : nullptr;
  }();
  if (rtl)
    return rtl(buf, static_cast<ULONG>(len)) != FALSE;
  return false;
}

std::string digits(size_t count) {
  std::string out;
  while (out.size() < count) {
    unsigned char b[64];
    require(fillRandom(b, sizeof(b)));
    for (size_t i = 0; i < sizeof(b) && out.size() < count; ++i)
      if (b[i] < 250)
        out += char('0' + b[i] % 10);
  }
  return out;
}

struct OrdinalLess {
  bool operator()(const Wide &a, const Wide &b) const {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                b.c_str(), static_cast<int>(b.size()),
                                TRUE) == CSTR_LESS_THAN;
  }
};

using WideNames = std::set<Wide, OrdinalLess>;

bool ordinalEqual(const Wide &a, const Wide &b) {
  return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                              static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::string lower(std::string s) {
  for (char &c : s)
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
  return s;
}

bool career(const std::string &s) {
  return s.size() > 4 && lower(s.substr(s.size() - 4)) == ".bes";
}

bool wideCareer(const Wide &s) {
  if (s.size() <= 4)
    return false;
  return CompareStringOrdinal(s.data() + (s.size() - 4), 4, L".bes", 4, TRUE) ==
         CSTR_EQUAL;
}

bool portable(const std::string &s) {
  if (!career(s) || s.size() >= 256 || s.front() == ' ' ||
      s[s.size() - 5] == ' ' || s[s.size() - 5] == '.' ||
      CeBackupFileHasDirectionMark(s.data(), s.size()))
    return false;
  const size_t stemLen = s.size() - 4;
  size_t units = 0;
  std::vector<unsigned> codepoints;
  for (size_t i = 0; i < stemLen;) {
    unsigned char b0 = static_cast<unsigned char>(s[i++]);
    unsigned cp = 0;
    if (b0 < 0x80) {
      cp = b0;
      if (cp < 32 || cp == 127 || std::strchr("<>:\"/\\|?*", char(cp)))
        return false;
    } else {
      unsigned extra = 0, minCp = 0;
      if (b0 >= 0xc2 && b0 <= 0xdf) {
        cp = b0 & 0x1fu;
        extra = 1;
        minCp = 0x80;
      } else if (b0 >= 0xe0 && b0 <= 0xef) {
        cp = b0 & 0x0fu;
        extra = 2;
        minCp = 0x800;
      } else if (b0 >= 0xf0 && b0 <= 0xf4) {
        cp = b0 & 0x07u;
        extra = 3;
        minCp = 0x10000;
      } else
        return false;
      if (extra > stemLen - i)
        return false;
      while (extra--) {
        unsigned char bx = static_cast<unsigned char>(s[i++]);
        if ((bx & 0xc0u) != 0x80u)
          return false;
        cp = (cp << 6) | (bx & 0x3fu);
      }
      if (cp < minCp || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu) ||
          (cp >= 0x80u && cp <= 0x9fu) || CeBackupDirectionMark(cp))
        return false;
    }
    units += cp >= 0x10000u ? 2u : 1u;
    if (units > 31u)
      return false;
    codepoints.push_back(cp);
  }
  if (codepoints.empty())
    return false;
  size_t headEnd = 0;
  while (headEnd < codepoints.size() && codepoints[headEnd] != '.')
    ++headEnd;
  while (headEnd > 0 && codepoints[headEnd - 1] == ' ')
    --headEnd;
  std::string head;
  for (size_t i = 0; i < headEnd; ++i) {
    unsigned cp = codepoints[i];
    if (cp >= 'A' && cp <= 'Z')
      head.push_back(char(cp + ('a' - 'A')));
    else if (cp < 0x80u)
      head.push_back(char(cp));
    else
      head.push_back('!');
  }
  if (head == "con" || head == "prn" || head == "aux" || head == "nul" ||
      head == "conin$" || head == "conout$")
    return false;
  if (headEnd == 4 &&
      (head.compare(0, 3, "com") == 0 || head.compare(0, 3, "lpt") == 0)) {
    unsigned d = codepoints[3];
    if ((d >= '0' && d <= '9') || d == 0xb9u || d == 0xb2u || d == 0xb3u)
      return false;
  }
  return true;
}

Wide toWide(const std::string &s, Status failStatus = Status::Refused) {
  require(!s.empty() && s.find('\0') == std::string::npos, failStatus);
  Wide out;
  require(wide(s.c_str(), out), failStatus);
  return out;
}

std::string toUtf8(const Wide &s, Status failStatus = Status::Refused) {
  std::string out;
  require(utf8(s, out), failStatus);
  return out;
}

std::string joinPath(const std::string &dir, const std::string &leaf) {
  if (dir.empty())
    return leaf;
  if (dir.back() == '\\' || dir.back() == '/')
    return dir + leaf;
  return dir + "\\" + leaf;
}

bool isLocalNtfs(HANDLE h) {
  wchar_t fsName[64]{};
  DWORD flags = 0;
  if (!GetVolumeInformationByHandleW(h, nullptr, 0, nullptr, nullptr, &flags,
                                     fsName, 64))
    return false;
  return _wcsicmp(fsName, L"NTFS") == 0;
}

Directory openDir(const std::string &path, Status failStatus = Status::Refused) {
  require(!path.empty() && path.find('\0') == std::string::npos, failStatus);
  Directory d;
  require(d.open(path.c_str()), failStatus);
  return d;
}

void bound(const Directory &held, const std::string &path) {
  Directory again = openDir(path, Status::Changed);
  require(sameId(held.ids.back(), again.ids.back()), Status::Changed);
}

struct FileStamp {
  FILE_ID_INFO id{};
  LONGLONG size = 0;
  LONGLONG written = 0;
  LONGLONG changed = 0;
  DWORD links = 0;
};

struct DirStamp {
  FILE_ID_INFO id{};
  LONGLONG written = 0;
  LONGLONG changed = 0;
  DWORD links = 0;
};

bool readFileStamp(HANDLE h, const Wide &leaf, FileStamp &s,
                   bool allowHidden = false) {
  FILE_BASIC_INFO basic{};
  FILE_STANDARD_INFO standard{};
  const DWORD forbidden =
      FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM |
      (allowHidden ? 0u : FILE_ATTRIBUTE_HIDDEN);
  if (GetFileType(h) != FILE_TYPE_DISK ||
      !GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic)) ||
      (basic.FileAttributes & forbidden) ||
      ce_win_handle_is_link(h, basic.FileAttributes) ||
      !GetFileInformationByHandleEx(h, FileStandardInfo, &standard,
                                    sizeof(standard)) ||
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

bool readDirStamp(HANDLE h, DirStamp &s) {
  FILE_BASIC_INFO basic{};
  FILE_STANDARD_INFO standard{};
  if (!dirInfo(h, s.id) ||
      !GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic)) ||
      !(basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
      ce_win_handle_is_link(h, basic.FileAttributes) ||
      !GetFileInformationByHandleEx(h, FileStandardInfo, &standard,
                                    sizeof(standard)) ||
      !standard.Directory || standard.DeletePending)
    return false;
  s.written = basic.LastWriteTime.QuadPart;
  s.changed = basic.ChangeTime.QuadPart;
  s.links = standard.NumberOfLinks;
  return true;
}

bool sameStamp(const FileStamp &a, const FileStamp &b) {
  return sameId(a.id, b.id) && a.size == b.size && a.links == b.links &&
         a.written == b.written && a.changed == b.changed;
}

bool sameDirStamp(const DirStamp &a, const DirStamp &b) {
  return sameId(a.id, b.id) && a.links == b.links && a.written == b.written &&
         a.changed == b.changed;
}

std::string hexFileId(const FILE_ID_INFO &id) {
  static const char hex[] = "0123456789abcdef";
  std::string out = std::to_string(id.VolumeSerialNumber) + ":";
  out.reserve(out.size() + 32);
  for (unsigned char b : id.FileId.Identifier) {
    out += hex[b >> 4];
    out += hex[b & 15];
  }
  return out;
}

std::string digest(const Bytes &bytes) {
  return ce_backup_verify::digest(std::string(bytes.begin(), bytes.end()));
}

std::string quoted(const std::string &s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += char(c);
    } else if (c < 32) {
      char temp[7];
      std::snprintf(temp, sizeof(temp), "\\u%04x", unsigned(c));
      out += temp;
    } else
      out += char(c);
  }
  return out + '"';
}

Bytes readFile(HANDLE parent, const Wide &leaf, FileStamp &observed) {
  require(component(leaf));
  Handle f = child(parent, leaf, false, false);
  require(bool(f));
  require(readFileStamp(f.h, leaf, observed) &&
          static_cast<unsigned long long>(observed.size) <= kMemberLimit);
  Bytes result(static_cast<size_t>(observed.size));
  size_t done = 0;
  while (done < result.size()) {
    DWORD got = 0;
    const DWORD ask =
        static_cast<DWORD>(std::min<size_t>(result.size() - done, 65536));
    require(ReadFile(f.h, result.data() + done, ask, &got, nullptr) && got > 0);
    done += got;
  }
  unsigned char tail = 0;
  DWORD extra = 0;
  FileStamp held{};
  require(ReadFile(f.h, &tail, 1, &extra, nullptr) && extra == 0 &&
              readFileStamp(f.h, leaf, held) && sameStamp(observed, held),
          Status::Changed);
  Handle byName = child(parent, leaf, false, false);
  FileStamp named{};
  require(bool(byName) && readFileStamp(byName.h, leaf, named) &&
              sameStamp(observed, named),
          Status::Changed);
  return result;
}

void writeAll(HANDLE h, const Bytes &data) {
  size_t done = 0;
  while (done < data.size()) {
    DWORD put = 0;
    const DWORD chunk =
        static_cast<DWORD>(std::min<size_t>(data.size() - done, 65536));
    require(WriteFile(h, data.data() + done, chunk, &put, nullptr) && put > 0);
    done += put;
  }
  require(FlushFileBuffers(h) != FALSE);
}

void verifyIdentical(HANDLE h, const Bytes &data) {
  LARGE_INTEGER zero{}, len{};
  require(GetFileSizeEx(h, &len) &&
          len.QuadPart == static_cast<LONGLONG>(data.size()) &&
          SetFilePointerEx(h, zero, nullptr, FILE_BEGIN));
  Bytes bytes(data.size());
  size_t done = 0;
  while (done < bytes.size()) {
    DWORD got = 0;
    const DWORD ask =
        static_cast<DWORD>(std::min<size_t>(bytes.size() - done, 65536));
    require(ReadFile(h, bytes.data() + done, ask, &got, nullptr) && got > 0);
    done += got;
  }
  unsigned char tail = 0;
  DWORD extra = 0;
  require(ReadFile(h, &tail, 1, &extra, nullptr) && extra == 0 &&
          bytes == data);
}

template <class Visit> bool enumerateDir(HANDLE dir, Visit visit) {
  alignas(FILE_ID_BOTH_DIR_INFO) unsigned char buf[16384];
  auto kind = FileIdBothDirectoryRestartInfo;
  size_t count = 0;
  while (GetFileInformationByHandleEx(dir, kind, buf, sizeof(buf))) {
    kind = FileIdBothDirectoryInfo;
    for (size_t pos = 0;;) {
      constexpr size_t head = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
      if (pos > sizeof(buf) - head)
        return false;
      const auto *e =
          reinterpret_cast<const FILE_ID_BOTH_DIR_INFO *>(buf + pos);
      const size_t len = e->FileNameLength;
      if (++count > kDirectoryEntryLimit || !len || len % sizeof(wchar_t) ||
          len > sizeof(buf) - pos - head)
        return false;
      Wide leaf(e->FileName, len / sizeof(wchar_t));
      if (leaf != L"." && leaf != L".." && !visit(leaf, e->FileAttributes))
        return false;
      if (!e->NextEntryOffset)
        break;
      if (e->NextEntryOffset < head + len ||
          e->NextEntryOffset % alignof(FILE_ID_BOTH_DIR_INFO) ||
          e->NextEntryOffset > sizeof(buf) - pos)
        return false;
      pos += e->NextEntryOffset;
    }
  }
  return GetLastError() == ERROR_NO_MORE_FILES;
}

bool existsEntry(HANDLE parent, const Wide &name) {
  if (parent == INVALID_HANDLE_VALUE)
    return false;
  NTSTATUS st = 0;
  Handle probe = openChildRaw(
      parent, name, FILE_READ_ATTRIBUTES | SYNCHRONIZE, 0,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
      FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, &st);
  if (probe)
    return true;
  require(st == kStatusObjectNameNotFound || st == kStatusObjectPathNotFound);
  return false;
}

bool savegamesStillAbsent(HANDLE parent) {
  NTSTATUS st = 0;
  Handle probe = openChildRaw(
      parent, L"savegames", FILE_READ_ATTRIBUTES | SYNCHRONIZE, 0,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
      FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, &st);
  return !probe &&
         (st == kStatusObjectNameNotFound || st == kStatusObjectPathNotFound);
}

void openOptionalSavegames(HANDLE parent, Handle &out, FILE_ID_INFO &idOut,
                           bool &present) {
  closeNow(out);
  idOut = {};
  NTSTATUS st = 0;
  Handle h = child(parent, L"savegames", true, false, &st);
  if (h) {
    require(dirInfo(h.h, idOut));
    assignHandle(out, std::move(h));
    present = true;
    return;
  }
  require((st == kStatusObjectNameNotFound || st == kStatusObjectPathNotFound) &&
          savegamesStillAbsent(parent));
  present = false;
}

struct Member {
  std::string name;
  bool user = false;
  FileStamp identity{};
  Bytes bytes;
};

struct SkippedMember {
  std::string name;
  bool user = false;
  FileStamp identity{};
};

struct Snapshot {
  std::vector<Member> all;
  std::map<std::string, size_t> visible;
  std::vector<SkippedMember> skipped;
};

bool hasCareers(const Snapshot &s) {
  for (const auto &it : s.visible)
    if (career(it.first))
      return true;
  return false;
}

void logSkippedSave(bool fromUser, const std::string &name,
                    const char *reason) {
  std::fprintf(
      stderr,
      "onslaught-ce: career storage skipped unreadable %s save \"%s\" (%s)\n",
      fromUser ? "user" : "game", name.c_str(), reason);
}

bool equal(const Snapshot &a, const Snapshot &b) {
  if (a.all.size() != b.all.size() || a.visible != b.visible ||
      a.skipped.size() != b.skipped.size())
    return false;
  for (size_t i = 0; i < a.all.size(); ++i)
    if (a.all[i].name != b.all[i].name || a.all[i].user != b.all[i].user ||
        !sameStamp(a.all[i].identity, b.all[i].identity) ||
        a.all[i].bytes != b.all[i].bytes)
      return false;
  for (size_t i = 0; i < a.skipped.size(); ++i)
    if (a.skipped[i].name != b.skipped[i].name ||
        a.skipped[i].user != b.skipped[i].user ||
        !sameStamp(a.skipped[i].identity, b.skipped[i].identity))
      return false;
  return true;
}

struct Session {
  const CeCareerStorageRequest &r;
  Directory user, game, package, root;
  Handle slots, original, lock;
  FILE_ID_INFO slotsId{}, originalId{};
  bool slotsPresent = false, originalPresent = false;
  FileStamp lockStamp{};
  bool locked = false;
  Snapshot saved;

  explicit Session(const CeCareerStorageRequest &in) : r(in) {
    permission();
    user = openDir(r.user);
    game = openDir(r.game);
    package = openDir(r.package);
    root = openDir(r.backupRoot);
    require(outside(root, user, game, package));
    require(!user.below(root) && !game.below(root) && !package.below(root));
    require(!user.below(game) && !game.below(user) &&
            !user.differentRootOnSameVolume(game));
    openOptionalSavegames(user.h(), slots, slotsId, slotsPresent);
    openOptionalSavegames(game.h(), original, originalId, originalPresent);
    require(isLocalNtfs(user.h()) && isLocalNtfs(game.h()) &&
                isLocalNtfs(root.h()) &&
                (!slotsPresent || isLocalNtfs(slots.h)) &&
                (!originalPresent || isLocalNtfs(original.h)),
            Status::Unsupported);
    NTSTATUS lockStatus = 0;
    assignHandle(
        lock,
        openChildRaw(
            user.h(), kLockWide,
            GENERIC_READ | GENERIC_WRITE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
            FILE_OPEN_IF,
            FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT |
                FILE_NON_DIRECTORY_FILE,
            &lockStatus));
    if (!lock)
      throw Failure{lockStatus == kStatusSharingViolation ? Status::Busy
                                                          : Status::Refused};
    require(readFileStamp(lock.h, kLockWide, lockStamp, true));
    OVERLAPPED ov{};
    if (!LockFileEx(lock.h,
                    LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1,
                    0, &ov)) {
      const DWORD err = GetLastError();
      throw Failure{err == ERROR_LOCK_VIOLATION ? Status::Busy
                                                : Status::Refused};
    }
    locked = true;
    roots();
    saved = snapshot("", true);
  }

  ~Session() {
    if (locked && lock) {
      OVERLAPPED ov{};
      UnlockFileEx(lock.h, 0, 1, 0, &ov);
    }
  }

  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  void permission(bool includeCancel = true) const {
    require(r.permitted && r.permitted(r.owner));
    require(!(includeCancel && r.cancelled && r.cancelled(r.owner)),
            Status::Cancelled);
  }

  void roots() const {
    bound(user, r.user);
    bound(game, r.game);
    bound(package, r.package);
    bound(root, r.backupRoot);
    if (slotsPresent) {
      Handle a = child(user.h(), L"savegames", true, false);
      FILE_ID_INFO aId{};
      require(bool(a) && dirInfo(a.h, aId) && sameId(aId, slotsId),
              Status::Changed);
    } else {
      require(savegamesStillAbsent(user.h()), Status::Changed);
    }
    if (originalPresent) {
      Handle b = child(game.h(), L"savegames", true, false);
      FILE_ID_INFO bId{};
      require(bool(b) && dirInfo(b.h, bId) && sameId(bId, originalId),
              Status::Changed);
    } else {
      require(savegamesStillAbsent(game.h()), Status::Changed);
    }
    Handle held = openChildRaw(
        user.h(), kLockWide, FILE_READ_ATTRIBUTES | SYNCHRONIZE, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
        FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT |
            FILE_NON_DIRECTORY_FILE);
    FileStamp cur{};
    require(bool(held) && readFileStamp(held.h, kLockWide, cur, true) &&
                sameId(cur.id, lockStamp.id) && cur.links == 1,
            Status::Changed);
  }

  void ensureUserSavegames() {
    if (slotsPresent)
      return;
    roots();
    NTSTATUS createSt = 0;
    Handle created = openChildRaw(
        user.h(), L"savegames",
        FILE_READ_ATTRIBUTES | SYNCHRONIZE | FILE_LIST_DIRECTORY,
        FILE_ATTRIBUTE_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_CREATE,
        FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT |
            FILE_DIRECTORY_FILE,
        &createSt);
    if (!created) {
      if (createSt == kStatusObjectNameCollision)
        throw Failure{Status::Changed};
      if (createSt == kStatusNotSupported ||
          createSt == kStatusInvalidDeviceRequest)
        throw Failure{Status::Unsupported};
      throw Failure{Status::Refused};
    }
    FILE_ID_INFO createdId{};
    require(dirInfo(created.h, createdId) &&
            physicalLeaf(created.h, L"savegames"));
    require(isLocalNtfs(created.h), Status::Unsupported);
    closeNow(created);
    Handle reopened = child(user.h(), L"savegames", true, false);
    DirStamp reopenedStamp{};
    require(bool(reopened) && readDirStamp(reopened.h, reopenedStamp) &&
                sameId(reopenedStamp.id, createdId) &&
                physicalLeaf(reopened.h, L"savegames"),
            Status::Changed);
    assignHandle(slots, std::move(reopened));
    slotsId = createdId;
    slotsPresent = true;
  }

  Snapshot snapshot(const std::string &exclude = "",
                    bool logSkipped = false) const {
    Snapshot s;
    size_t total = 0;
    std::map<Wide, std::string, OrdinalLess> spelling;
    for (bool fromUser : {true, false}) {
      HANDLE rootHandle = fromUser ? user.h() : game.h();
      if (!(fromUser ? slotsPresent : originalPresent)) {
        require(savegamesStillAbsent(rootHandle), Status::Changed);
        continue;
      }
      const FILE_ID_INFO &expectedId = fromUser ? slotsId : originalId;
      Handle scan = child(rootHandle, L"savegames", true, false);
      FILE_ID_INFO scanId{};
      require(bool(scan) && dirInfo(scan.h, scanId) &&
                  sameId(scanId, expectedId),
              Status::Changed);
      std::set<std::string> names;
      require(enumerateDir(scan.h, [&](const Wide &leaf, DWORD) {
        if (!wideCareer(leaf))
          return true;
        std::string utfName;
        if (!utf8(leaf, utfName))
          return false;
        names.insert(utfName);
        return true;
      }));
      for (const std::string &name : names) {
        if (fromUser && name == exclude)
          continue;
        const Wide wideName = toWide(name);
        Handle probe = child(scan.h, wideName, false, false);
        FileStamp fs{};
        require(bool(probe) && readFileStamp(probe.h, wideName, fs));
        closeNow(probe);
        for (const Member &existing : s.all)
          require(!sameId(existing.identity.id, fs.id));
        for (const SkippedMember &existing : s.skipped)
          require(!sameId(existing.identity.id, fs.id));
        if (!portable(name)) {
          if (logSkipped)
            logSkippedSave(fromUser, name, "non-portable or overlong filename");
          s.skipped.push_back(SkippedMember{name, fromUser, fs});
          continue;
        }
        auto was = spelling.find(wideName);
        require(was == spelling.end() || (was->second == name && !fromUser));
        spelling[wideName] = name;
        if (fs.size != CE_CAREER_SIZE) {
          if (logSkipped)
            logSkippedSave(fromUser, name,
                           fs.size == 0 ? "empty 0-byte file"
                                        : "wrong file size");
          s.skipped.push_back(SkippedMember{name, fromUser, fs});
          continue;
        }
        Member m;
        m.name = name;
        m.user = fromUser;
        m.bytes = readFile(scan.h, wideName, m.identity);
        for (const Member &existing : s.all)
          require(!sameId(existing.identity.id, m.identity.id));
        for (const SkippedMember &existing : s.skipped)
          require(!sameId(existing.identity.id, m.identity.id));
        SCeCareerView view{};
        if (!CeCareerViewBuild(m.bytes.data(), int(m.bytes.size()), &view) ||
            !CeCareerViewPlausible(view)) {
          if (logSkipped)
            logSkippedSave(fromUser, name, "corrupt or unreadable career data");
          s.skipped.push_back(SkippedMember{name, fromUser, m.identity});
          continue;
        }
        total += m.bytes.size();
        require(s.all.size() < kCountLimit && total <= kSetLimit);
        if (!s.visible.count(name))
          s.visible[name] = s.all.size();
        s.all.push_back(std::move(m));
      }
    }
    NTSTATUS optStatus = 0;
    Handle optProbe = openChildRaw(
        user.h(), kOptionsWide, FILE_READ_ATTRIBUTES | SYNCHRONIZE, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
        FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, &optStatus);
    if (optProbe) {
      Wide optPath;
      require(physical(optProbe.h, optPath));
      const size_t slash = optPath.rfind(L'\\');
      require(slash != Wide::npos);
      Wide actualLeaf = optPath.substr(slash + 1);
      require(CompareStringOrdinal(actualLeaf.c_str(),
                                   static_cast<int>(actualLeaf.size()),
                                   kOptionsWide, -1, TRUE) == CSTR_EQUAL);
      closeNow(optProbe);
      Member m;
      m.name = toUtf8(actualLeaf);
      m.user = true;
      m.bytes = readFile(user.h(), actualLeaf, m.identity);
      for (const Member &existing : s.all)
        require(!sameId(existing.identity.id, m.identity.id));
      for (const SkippedMember &existing : s.skipped)
        require(!sameId(existing.identity.id, m.identity.id));
      total += m.bytes.size();
      require(s.all.size() < kCountLimit && total <= kSetLimit);
      s.visible[m.name] = s.all.size();
      s.all.push_back(std::move(m));
    } else {
      require(optStatus == kStatusObjectNameNotFound ||
              optStatus == kStatusObjectPathNotFound);
    }
    return s;
  }

  void check(const std::string &exclude = "", bool allowCancel = true) const {
    permission(allowCancel);
    roots();
    require(equal(saved, snapshot(exclude)), Status::Changed);
  }
};

std::string setStamp(const CeCareerStorageRequest &r, const std::string &path,
                     const std::string &expected) {
  Directory dir = openDir(path);
  require(isLocalNtfs(dir.h()), Status::Unsupported);
  Directory user = openDir(r.user);
  Directory game = openDir(r.game);
  Directory package = openDir(r.package);
  require(outside(dir, user, game, package));
  require(!user.below(dir) && !game.below(dir) && !package.below(dir));
  DirStamp opened{};
  require(readDirStamp(dir.h(), opened));
  FileStamp manifestIdentity{};
  const Bytes raw =
      readFile(dir.h(), L"onslaught-backup.json", manifestIdentity);
  const std::string text(raw.begin(), raw.end());
  ce_preview::Manifest parsedManifest;
  ce_preview::Parser parser(text);
  require(parser.parse(parsedManifest) &&
              parsedManifest.files.size() <= kCountLimit,
          Status::Changed);
  size_t total = 0;
  std::string record;
  const auto addFile = [&](const FileStamp &s) {
    record += hexFileId(s.id) + ":" + std::to_string(s.size) + ":" +
              std::to_string(s.links) + ":" + std::to_string(s.written) + ":" +
              std::to_string(s.changed) + ";";
  };
  const auto addDir = [&](const DirStamp &s) {
    record += hexFileId(s.id) + ":" + std::to_string(s.links) + ":" +
              std::to_string(s.written) + ":" + std::to_string(s.changed) + ";";
  };
  addDir(opened);
  addFile(manifestIdentity);
  std::vector<std::pair<Wide, FileStamp>> heldMembers;
  heldMembers.emplace_back(L"onslaught-backup.json", manifestIdentity);
  for (const auto &member : parsedManifest.files) {
    const Wide leaf = toWide(member.name, Status::Changed);
    FileStamp observed{};
    const Bytes bytes = readFile(dir.h(), leaf, observed);
    total += bytes.size();
    require(total <= kSetLimit);
    record += member.name + ":" + digest(bytes) + ";";
    addFile(observed);
    heldMembers.emplace_back(leaf, observed);
  }
  CePreviewSet verified;
  std::string error;
  require(CePreviewRead(path, r.user, r.game, r.package, verified, error) &&
              verified.identity == expected,
          Status::Changed);
  for (const auto &member : heldMembers) {
    Handle again = child(dir.h(), member.first, false, false);
    FileStamp current{};
    require(bool(again) && readFileStamp(again.h, member.first, current) &&
                sameStamp(current, member.second),
            Status::Changed);
  }
  DirStamp after{};
  require(readDirStamp(dir.h(), after) && sameDirStamp(opened, after),
          Status::Changed);
  bound(dir, path);
  return ce_backup_verify::digest(record + expected);
}

WideNames names(const CeCareerRestoreRequest &r) {
  Directory user = openDir(r.user);
  Directory game = openDir(r.game);
  require(!user.below(game) && !game.below(user) &&
          !user.differentRootOnSameVolume(game));
  Handle slots, original;
  FILE_ID_INFO slotsId{}, originalId{};
  bool slotsPresent = false, originalPresent = false;
  openOptionalSavegames(user.h(), slots, slotsId, slotsPresent);
  openOptionalSavegames(game.h(), original, originalId, originalPresent);
  DirStamp beforeUser{}, beforeGame{};
  require((!slotsPresent || readDirStamp(slots.h, beforeUser)) &&
          (!originalPresent || readDirStamp(original.h, beforeGame)));
  require(isLocalNtfs(user.h()) && isLocalNtfs(game.h()) &&
              (!slotsPresent || isLocalNtfs(slots.h)) &&
              (!originalPresent || isLocalNtfs(original.h)),
          Status::Unsupported);
  std::map<Wide, std::string, OrdinalLess> spellings;
  WideNames found;
  for (bool fromUser : {true, false}) {
    if (!(fromUser ? slotsPresent : originalPresent))
      continue;
    HANDLE folder = fromUser ? slots.h : original.h;
    std::set<std::string> entries;
    require(enumerateDir(folder, [&](const Wide &leaf, DWORD) {
      if (!wideCareer(leaf))
        return true;
      std::string utfName;
      if (!utf8(leaf, utfName))
        return false;
      entries.insert(utfName);
      return true;
    }));
    require(entries.size() <= kCountLimit);
    for (const std::string &entry : entries) {
      const Wide wideEntry = toWide(entry);
      Handle f = child(folder, wideEntry, false, false);
      FileStamp st{};
      require(bool(f) && readFileStamp(f.h, wideEntry, st));
      if (!portable(entry)) {
        found.insert(wideEntry);
        continue;
      }
      auto was = spellings.find(wideEntry);
      require(was == spellings.end() || (was->second == entry && !fromUser));
      spellings[wideEntry] = entry;
      found.insert(wideEntry);
    }
  }
  DirStamp afterUser{}, afterGame{};
  require((slotsPresent
               ? (readDirStamp(slots.h, afterUser) &&
                  sameDirStamp(beforeUser, afterUser))
               : savegamesStillAbsent(user.h())) &&
              (originalPresent
                   ? (readDirStamp(original.h, afterGame) &&
                      sameDirStamp(beforeGame, afterGame))
                   : savegamesStillAbsent(game.h())),
          Status::Changed);
  bound(user, r.user);
  bound(game, r.game);
  return found;
}

struct CopyStem {
  std::string name;
  unsigned next = 1;
};

CopyStem copyStem(const std::string &file) {
  CopyStem out{file.substr(0, file.size() - 4), 1};
  const std::string marker = " restored";
  std::string folded = lower(out.name);
  size_t pos = folded.rfind(marker);
  if (pos == std::string::npos || pos == 0)
    return out;
  std::string tail = folded.substr(pos + marker.size());
  unsigned previous = 1;
  if (!tail.empty()) {
    if (tail[0] != ' ' || tail.size() < 2 || tail.size() > 5)
      return out;
    previous = 0;
    for (size_t i = 1; i < tail.size(); ++i) {
      if (tail[i] < '0' || tail[i] > '9')
        return out;
      previous = previous * 10 + unsigned(tail[i] - '0');
    }
    if (previous < 2 || previous > 9999)
      return out;
  }
  out.name.resize(pos);
  out.next = previous + 1;
  return out;
}

std::string proposal(const CopyStem &base, unsigned number) {
  std::string suffix = " restored";
  if (number > 1)
    suffix += " " + std::to_string(number);
  require(suffix.size() < 31);
  const size_t maxStem = 31 - suffix.size();
  size_t cut = std::min(base.name.size(), maxStem);
  if (cut < base.name.size()) {
    while (cut > 0 &&
           (static_cast<unsigned char>(base.name[cut]) & 0xc0u) == 0x80u)
      --cut;
  }
  std::string stem = base.name.substr(0, cut);
  while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.'))
    stem.pop_back();
  require(!stem.empty());
  const std::string out = stem + suffix + ".bes";
  require(portable(out));
  return out;
}

Bytes selected(const CeCareerRestoreRequest &r) {
  require(!r.sourceStamp.empty() &&
              setStamp(r, r.sourceSet, r.sourceIdentity) == r.sourceStamp,
          Status::Changed);
  CePreviewSet set;
  std::string error;
  require(CePreviewRead(r.sourceSet, r.user, r.game, r.package, set, error) &&
              set.identity == r.sourceIdentity,
          Status::Changed);
  for (const auto &entry : set.careers)
    if (entry.name == r.sourceName)
      return entry.bytes;
  throw Failure{Status::Refused};
}

std::string manifest(const Session &s, const char *reason,
                     const SYSTEMTIME &utc) {
  char timestamp[32];
  std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02uZ",
                unsigned(utc.wYear), unsigned(utc.wMonth), unsigned(utc.wDay),
                unsigned(utc.wHour), unsigned(utc.wMinute),
                unsigned(utc.wSecond));
  const std::string userPath = toUtf8(s.user.path);
  const std::string gamePath = toUtf8(s.game.path);
  std::string out = "{\"Folder\":" + quoted(userPath) +
                    ",\"Game\":" + quoted(gamePath) +
                    ",\"Created\":" + quoted(timestamp) +
                    ",\"Reason\":" + quoted(reason) +
                    ",\"Target\":1,\"Files\":[";
  bool first = true;
  for (const auto &it : s.saved.visible) {
    const Member &m = s.saved.all[it.second];
    if (!first)
      out += ',';
    first = false;
    const std::string parent =
        lower(m.name) == kOptions
            ? userPath
            : joinPath(m.user ? userPath : gamePath, "savegames");
    out += "{\"Name\":" + quoted(m.name) +
           ",\"Original\":" + quoted(joinPath(parent, m.name)) +
           ",\"Size\":" + std::to_string(m.bytes.size()) +
           ",\"Sha256\":" + quoted(digest(m.bytes)) + "}";
  }
  return out + "]}\n";
}

// What the preview reader will refuse is refused here, before anything is
// written: a set it cannot read (more members or manifest bytes than it
// accepts, or no career at all) is never staged, so it is never published.
void readable(const Session &s, const std::string &json) {
  ce_preview::Manifest parsed;
  ce_preview::Parser parser(json);
  require(parser.parse(parsed) && parsed.files.size() == s.saved.visible.size());
  bool careerFound = false;
  for (const auto &entry : parsed.files)
    careerFound = careerFound || ce_preview::career(entry.name);
  require(careerFound);
}

// A set is staged under a name the preview never lists, written, flushed and
// verified by the preview reader itself while still staged; only then is it
// renamed, without replacement, to a listed name. A cut at any step before
// the rename leaves only the private staging folder. After the rename the set
// is the one already verified; it is read back again as confirmation, and a
// failed confirmation reports Uncertain and never removes or renames it.
void backup(Session &s, CeCareerStorageResult &out, const char *reason) {
  s.check();
  SYSTEMTIME utc{};
  GetSystemTime(&utc);
  char stampText[32];
  std::snprintf(stampText, sizeof(stampText), "%04u%02u%02u-%02u%02u%02u",
                unsigned(utc.wYear), unsigned(utc.wMonth), unsigned(utc.wDay),
                unsigned(utc.wHour), unsigned(utc.wMinute),
                unsigned(utc.wSecond));
  const std::string finalName =
      "onslaught-backup-" + std::string(stampText) + "-" + digits(18);
  const std::string staging = ".pending-" + finalName;
  const std::string json = manifest(s, reason, utc);
  readable(s, json);
  const Wide wideStaging = toWide(staging);
  const Wide wideFinal = toWide(finalName);
  // Open staging without DELETE first so CePreviewRead (which shares READ|WRITE
  // only) can verify it while still staged without a sharing violation.
  Handle dir = openChildRaw(
      s.root.h(), wideStaging,
      FILE_READ_ATTRIBUTES | SYNCHRONIZE | FILE_LIST_DIRECTORY,
      FILE_ATTRIBUTE_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE,
      FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT |
          FILE_DIRECTORY_FILE);
  FILE_ID_INFO stagingId{};
  require(bool(dir) && dirInfo(dir.h, stagingId));
  for (const auto &it : s.saved.visible) {
    s.permission();
    const Member &m = s.saved.all[it.second];
    const Wide wideMember = toWide(m.name);
    Handle file = child(dir.h, wideMember, false, true);
    require(bool(file));
    writeAll(file.h, m.bytes);
    verifyIdentical(file.h, m.bytes);
  }
  const Bytes jsonBytes(json.begin(), json.end());
  {
    Handle list = child(dir.h, L"onslaught-backup.json", false, true);
    require(bool(list));
    writeAll(list.h, jsonBytes);
    verifyIdentical(list.h, jsonBytes);
  }
  CePreviewSet staged;
  std::string error;
  const std::string rootUtf8 = toUtf8(s.root.path);
  require(CePreviewRead(joinPath(rootUtf8, staging), s.r.user, s.r.game,
                        s.r.package, staged, error) &&
          size_t(staged.members) == s.saved.visible.size() &&
          staged.identity == digest(jsonBytes));
  s.check();
  closeNow(dir);
  Handle renameDir = openChildRaw(
      s.root.h(), wideStaging, DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
      FILE_ATTRIBUTE_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
      FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT |
          FILE_DIRECTORY_FILE);
  FILE_ID_INFO nowStagingId{};
  require(bool(renameDir) && dirInfo(renameDir.h, nowStagingId) &&
              sameId(nowStagingId, stagingId),
          Status::Changed);
  require(renameNoReplace(renameDir.h, s.root.h(), wideFinal) == 0);
  closeNow(renameDir);
  out.backupSet = joinPath(s.r.backupRoot, finalName);
  CePreviewSet parsed;
  require(CePreviewRead(out.backupSet, s.r.user, s.r.game, s.r.package, parsed,
                        error),
          Status::Uncertain);
  require(size_t(parsed.members) == s.saved.visible.size() &&
              parsed.identity == staged.identity,
          Status::Uncertain);
  out.backupIdentity = parsed.identity;
  out.backupStamp = setStamp(s.r, out.backupSet, out.backupIdentity);
  s.check();
}

void checkedBackup(const Session &s, const CeCareerStorageResult &out) {
  require(!out.backupStamp.empty() &&
              setStamp(s.r, out.backupSet, out.backupIdentity) ==
                  out.backupStamp,
          Status::Changed);
  CePreviewSet parsed;
  std::string error;
  require(CePreviewRead(out.backupSet, s.r.user, s.r.game, s.r.package, parsed,
                        error) &&
              parsed.identity == out.backupIdentity,
          Status::Changed);
}
} // namespace

bool CeCareerRestoreStamp(const CeCareerRestoreRequest &request,
                          std::string &stampOut) {
  stampOut.clear();
  try {
    stampOut = setStamp(request, request.sourceSet, request.sourceIdentity);
    return true;
  } catch (...) {
    return false;
  }
}

CeCareerStorageStatus
CeCareerRestoreSuggest(const CeCareerRestoreRequest &request,
                       std::string &targetName) {
  targetName.clear();
  try {
    require(portable(request.sourceName));
    selected(request);
    const auto taken = names(request);
    const auto stem = copyStem(request.sourceName);
    for (unsigned n = stem.next; n < stem.next + 1000 && n <= 9999; ++n) {
      const std::string candidate = proposal(stem, n);
      if (!taken.count(toWide(candidate))) {
        targetName = candidate;
        return Status::Complete;
      }
    }
    return Status::Occupied;
  } catch (const Failure &error) {
    return error.status;
  } catch (...) {
    return Status::Refused;
  }
}

CeCareerStorageResult CeCareerBackupAll(const CeCareerStorageRequest &request) {
  CeCareerStorageResult out;
  try {
    Session session(request);
    backup(session, out, kFullSetReason);
    out.status = Status::Complete;
  } catch (const Failure &error) {
    out.status = out.backupSet.empty() ? error.status : Status::Uncertain;
  } catch (...) {
    out.status = out.backupSet.empty() ? Status::Refused : Status::Uncertain;
  }
  return out;
}

CeCareerStorageResult
CeCareerTakeFullSet(const CeCareerStorageRequest &request) {
  CeCareerStorageResult out = CeCareerBackupAll(request);
  if (out.status == Status::Complete) {
    // Bound again here. The set is published, so a failure from here on is
    // Uncertain: the set is kept and named, but there is nothing to write
    // behind.
    bool stillBound = false;
    try {
      CePreviewSet again;
      std::string error;
      stillBound =
          !out.backupSet.empty() && !out.backupIdentity.empty() &&
          !out.backupStamp.empty() &&
          setStamp(request, out.backupSet, out.backupIdentity) ==
              out.backupStamp &&
          CePreviewRead(out.backupSet, request.user, request.game,
                        request.package, again, error) &&
          again.identity == out.backupIdentity;
    } catch (...) {
      stillBound = false;
    }
    if (stillBound)
      return out;
    out.status = Status::Uncertain;
  }
  // Not a set to write behind: only Uncertain keeps the path, to inspect.
  if (out.status != Status::Uncertain)
    out.backupSet.clear();
  out.backupIdentity.clear();
  out.backupStamp.clear();
  return out;
}

CeCareerStorageResult
CeCareerRestoreAsNew(const CeCareerRestoreRequest &request) {
  CeCareerStorageResult out;
  bool published = false;
  try {
    require(portable(request.targetName) && request.namePermitted &&
            request.namePermitted(request.owner, request.sourceName,
                                  request.targetName));
    Session session(request);
    const Bytes payload = selected(request);
    require(!payload.empty());
    const std::string target = request.targetName;
    const Wide wideTarget = toWide(target);
    for (const Member &member : session.saved.all)
      require(!ordinalEqual(toWide(member.name), wideTarget), Status::Occupied);
    for (const SkippedMember &member : session.saved.skipped)
      require(!ordinalEqual(toWide(member.name), wideTarget), Status::Occupied);
    require(!existsEntry(session.slots.h, wideTarget) &&
                !existsEntry(session.original.h, wideTarget),
            Status::Occupied);
    const bool needBackup = hasCareers(session.saved);
    if (needBackup) {
      backup(session, out, kRestoreReason);
      checkedBackup(session, out);
    }
    require(selected(request) == payload, Status::Changed);
    session.permission();
    require(request.namePermitted(request.owner, request.sourceName, target));
    session.check();
    session.ensureUserSavegames();
    const Wide stageName = toWide(".pending-" + digits(18) + ".tmp");
    NTSTATUS stageStatus = 0;
    Handle stage = openChildRaw(
        session.slots.h, stageName,
        GENERIC_READ | GENERIC_WRITE | FILE_READ_ATTRIBUTES | DELETE |
            SYNCHRONIZE,
        FILE_ATTRIBUTE_NORMAL, 0, FILE_CREATE,
        FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT |
            FILE_NON_DIRECTORY_FILE,
        &stageStatus);
    if (!stage)
      throw Failure{(stageStatus == kStatusNotSupported ||
                     stageStatus == kStatusInvalidDeviceRequest)
                        ? Status::Unsupported
                        : Status::Refused};
    stage.discard = true;
    writeAll(stage.h, payload);
    verifyIdentical(stage.h, payload);
    FileStamp stageStamp{};
    require(readFileStamp(stage.h, stageName, stageStamp, true));
    require(!existsEntry(session.slots.h, wideTarget) &&
                !existsEntry(session.original.h, wideTarget),
            Status::Occupied);
    session.check();
    if (needBackup)
      checkedBackup(session, out);
    require(selected(request) == payload, Status::Changed);
    session.permission();
    require(request.namePermitted(request.owner, request.sourceName, target));
    session.roots();
    require(!existsEntry(session.original.h, wideTarget), Status::Occupied);
    const NTSTATUS publishStatus =
        renameNoReplace(stage.h, session.slots.h, wideTarget);
    if (publishStatus != 0)
      throw Failure{publishStatus == kStatusObjectNameCollision
                        ? Status::Occupied
                        : Status::Refused};
    stage.discard = false;
    published = true;
    out.newFile = target;
    require(FlushFileBuffers(stage.h) != FALSE, Status::Uncertain);
    closeNow(stage);
    {
      Handle finalFile = child(session.slots.h, wideTarget, false, false);
      FileStamp finalStamp{};
      require(bool(finalFile) &&
                  readFileStamp(finalFile.h, wideTarget, finalStamp) &&
                  sameId(finalStamp.id, stageStamp.id) && finalStamp.links == 1,
              Status::Uncertain);
      verifyIdentical(finalFile.h, payload);
    }
    if (needBackup)
      checkedBackup(session, out);
    require(selected(request) == payload, Status::Changed);
    session.check(target, false);
    require(request.namePermitted(request.owner, request.sourceName, target));
    require(!existsEntry(session.original.h, wideTarget), Status::Occupied);
    out.status = Status::Complete;
  } catch (const Failure &error) {
    out.status = published ? Status::Uncertain : error.status;
  } catch (...) {
    out.status = published ? Status::Uncertain : Status::Refused;
  }
  return out;
}
#endif
