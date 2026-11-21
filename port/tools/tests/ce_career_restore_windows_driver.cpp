// SPDX-License-Identifier: GPL-3.0-only
// Standalone Windows driver for port/platform/windows/ce_career_restore.cpp.
// Runs only against a disposable directory carrying an AUTHORED sentinel and
// the CE_CAREER_RESTORE_DISPOSABLE=1 guard.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../../platform/common/ce_backup_preview.h"
#include "../../platform/common/ce_career_restore.h"
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::string gFaultMode;
bool gFaultArmed = false;
bool gPermit = true;
bool gCancel = false;
bool gNamePermit = true;
std::string gExpectedSource;
std::string gExpectedTarget;
bool gCheckExactPair = false;
int gPermitCalls = 0;
int gNamePermitCalls = 0;

std::wstring toWide(const std::string &s) {
  if (s.empty())
    return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1,
                              nullptr, 0);
  if (n <= 1)
    std::exit(71);
  std::wstring out(static_cast<size_t>(n), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1,
                          &out[0], n) != n)
    std::exit(71);
  out.pop_back();
  return out;
}

std::string readBytes(const std::string &path) {
  HANDLE h = CreateFileW(toWide(path).c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return std::string();
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > 4 * 1024 * 1024) {
    CloseHandle(h);
    return std::string();
  }
  std::string out(static_cast<size_t>(sz.QuadPart), '\0');
  DWORD got = 0;
  if (!out.empty() &&
      (!ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &got, nullptr) ||
       got != out.size())) {
    CloseHandle(h);
    return std::string();
  }
  CloseHandle(h);
  return out;
}

bool writeBytes(const std::string &path, const std::string &data) {
  HANDLE h = CreateFileW(toWide(path).c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  DWORD put = 0;
  bool ok = (data.empty() ||
             (WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &put,
                        nullptr) &&
              put == data.size())) &&
            FlushFileBuffers(h);
  CloseHandle(h);
  return ok;
}

bool permitCb(void *) {
  ++gPermitCalls;
  return gPermit;
}

bool cancelCb(void *) { return gCancel; }

bool namePermitCb(void *, const std::string &source,
                  const std::string &target) {
  ++gNamePermitCalls;
  if (!gNamePermit)
    return false;
  if (gCheckExactPair)
    return source == gExpectedSource && target == gExpectedTarget;
  return true;
}

HANDLE acquireCustodyLock(const std::string &userDir) {
  std::wstring lockPath = toWide(userDir + "\\.ce-career-custody.lock");
  HANDLE h = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return INVALID_HANDLE_VALUE;
  OVERLAPPED ov{};
  if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1,
                  0, &ov)) {
    CloseHandle(h);
    return INVALID_HANDLE_VALUE;
  }
  return h;
}

void releaseCustodyLock(HANDLE h) {
  if (h != INVALID_HANDLE_VALUE) {
    OVERLAPPED ov{};
    UnlockFileEx(h, 0, 1, 0, &ov);
    CloseHandle(h);
  }
}

void printResult(const CeCareerStorageResult &r) {
  std::printf("STATUS=%d\n", int(r.status));
  std::printf("BACKUP_SET=%s\n", r.backupSet.c_str());
  std::printf("BACKUP_IDENTITY=%s\n", r.backupIdentity.c_str());
  std::printf("BACKUP_STAMP=%s\n", r.backupStamp.c_str());
  std::printf("NEW_FILE=%s\n", r.newFile.c_str());
  std::printf("PERMIT_CALLS=%d\n", gPermitCalls);
  std::printf("NAME_PERMIT_CALLS=%d\n", gNamePermitCalls);
  std::fflush(stdout);
}

bool startsWith(const std::string &s, const char *prefix) {
  const size_t n = std::strlen(prefix);
  return s.size() >= n && s.compare(0, n, prefix) == 0;
}

bool createDirectoryJunction(const std::string &linkPath,
                             const std::string &targetPath) {
  const std::wstring wLink = toWide(linkPath);
  const std::wstring wTarget = toWide(targetPath);
  if (!CreateDirectoryW(wLink.c_str(), nullptr))
    return false;
  HANDLE h = CreateFileW(wLink.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0,
                         nullptr, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS |
                             FILE_FLAG_OPEN_REPARSE_POINT,
                         nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  struct MountPointReparseBuffer {
    DWORD ReparseTag;
    WORD ReparseDataLength;
    WORD Reserved;
    WORD SubstituteNameOffset;
    WORD SubstituteNameLength;
    WORD PrintNameOffset;
    WORD PrintNameLength;
    WCHAR PathBuffer[1];
  };
  const std::wstring subst = L"\\??\\" + wTarget;
  const std::wstring print = wTarget;
  const size_t substBytes = subst.size() * sizeof(WCHAR);
  const size_t printBytes = print.size() * sizeof(WCHAR);
  const size_t pathBytes =
      substBytes + sizeof(WCHAR) + printBytes + sizeof(WCHAR);
  const size_t dataLen = 8 + pathBytes;
  const size_t totalLen = 8 + dataLen;
  std::vector<unsigned char> buf(totalLen, 0);
  auto *rp = reinterpret_cast<MountPointReparseBuffer *>(buf.data());
  rp->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
  rp->ReparseDataLength = static_cast<WORD>(dataLen);
  rp->Reserved = 0;
  rp->SubstituteNameOffset = 0;
  rp->SubstituteNameLength = static_cast<WORD>(substBytes);
  rp->PrintNameOffset = static_cast<WORD>(substBytes + sizeof(WCHAR));
  rp->PrintNameLength = static_cast<WORD>(printBytes);
  unsigned char *dst = reinterpret_cast<unsigned char *>(rp->PathBuffer);
  std::memcpy(dst, subst.data(), substBytes);
  std::memcpy(dst + substBytes + sizeof(WCHAR), print.data(), printBytes);
  DWORD returned = 0;
  BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buf.data(),
                            static_cast<DWORD>(buf.size()), nullptr, 0,
                            &returned, nullptr);
  FILE_ATTRIBUTE_TAG_INFO tag{};
  const bool verified =
      ok &&
      GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag)) &&
      (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
      tag.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT;
  CloseHandle(h);
  return verified;
}
} // namespace

// Hooks invoked only by the instrumented scratch test/mutant builds; the
// unmodified production backend does not call them.
bool CeRestoreTestIsLocalNtfs(bool realResult) {
  if (gFaultArmed && gFaultMode == "non_ntfs")
    return false;
  return realResult;
}

bool CeRestoreTestPostPublishFlush(HANDLE stageHandle) {
  if (gFaultArmed && gFaultMode == "post_publish_flush_fail")
    return false;
  return FlushFileBuffers(stageHandle) != FALSE;
}

void CeRestoreTestAfterPublishClose(HANDLE slotsDir,
                                    const wchar_t *targetName) {
  if (!gFaultArmed || gFaultMode != "post_publish_tamper" || !targetName ||
      !*targetName)
    return;
  auto ntCreate = reinterpret_cast<decltype(&NtCreateFile)>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
  if (!ntCreate)
    std::exit(79);
  UNICODE_STRING leaf{};
  leaf.Length = leaf.MaximumLength =
      static_cast<USHORT>(wcslen(targetName) * sizeof(wchar_t));
  leaf.Buffer = const_cast<wchar_t *>(targetName);
  OBJECT_ATTRIBUTES oa{};
  InitializeObjectAttributes(&oa, &leaf, OBJ_CASE_INSENSITIVE, slotsDir,
                             nullptr);
  IO_STATUS_BLOCK io{};
  HANDLE f = INVALID_HANDLE_VALUE;
  if (ntCreate(&f, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io,
               nullptr, FILE_ATTRIBUTE_NORMAL, 0, FILE_OPEN,
               FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT |
                   FILE_NON_DIRECTORY_FILE,
               nullptr, 0) != 0)
    std::exit(80);
  unsigned char b = 0;
  DWORD got = 0, put = 0;
  LARGE_INTEGER zero{};
  if (!ReadFile(f, &b, 1, &got, nullptr) || got != 1 ||
      !SetFilePointerEx(f, zero, nullptr, FILE_BEGIN)) {
    CloseHandle(f);
    std::exit(81);
  }
  b ^= 0xff;
  if (!WriteFile(f, &b, 1, &put, nullptr) || put != 1 || !FlushFileBuffers(f)) {
    CloseHandle(f);
    std::exit(82);
  }
  CloseHandle(f);
}

void CeRestoreTestAfterBackupRename(const std::string &backupSetPath) {
  if (!gFaultArmed || gFaultMode != "post_backup_tamper" ||
      backupSetPath.empty())
    return;
  const std::string member = backupSetPath + "\\Amber.bes";
  std::string data = readBytes(member);
  if (data.empty())
    std::exit(83);
  data.back() ^= 1;
  if (!writeBytes(member, data))
    std::exit(84);
}

bool CeRestoreTestAllowManifestWrite(HANDLE manifestHandle) {
  if (!gFaultArmed || gFaultMode != "cut_manifest")
    return true;
  const char partial[] = "{\"Folder\":\"partial-cut";
  DWORD put = 0;
  WriteFile(manifestHandle, partial, sizeof(partial) - 1, &put, nullptr);
  FlushFileBuffers(manifestHandle);
  return false;
}

void CeRestoreTestBeforePublishRename(HANDLE slotsDir,
                                      const wchar_t *targetName) {
  if (!gFaultArmed || gFaultMode != "race_publish" || !targetName ||
      !*targetName)
    return;
  auto ntCreate = reinterpret_cast<decltype(&NtCreateFile)>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
  if (!ntCreate)
    std::exit(76);
  UNICODE_STRING leaf{};
  leaf.Length = leaf.MaximumLength =
      static_cast<USHORT>(wcslen(targetName) * sizeof(wchar_t));
  leaf.Buffer = const_cast<wchar_t *>(targetName);
  OBJECT_ATTRIBUTES oa{};
  InitializeObjectAttributes(&oa, &leaf, OBJ_CASE_INSENSITIVE, slotsDir,
                             nullptr);
  IO_STATUS_BLOCK io{};
  HANDLE competing = INVALID_HANDLE_VALUE;
  NTSTATUS st = ntCreate(
      &competing, GENERIC_WRITE | SYNCHRONIZE, &oa, &io, nullptr,
      FILE_ATTRIBUTE_NORMAL, 0, FILE_CREATE,
      FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT |
          FILE_NON_DIRECTORY_FILE,
      nullptr, 0);
  if (st != 0)
    std::exit(77);
  const char sentinel[] = "COMPETING_WRITER_SENTINEL";
  DWORD put = 0;
  if (!WriteFile(competing, sentinel, sizeof(sentinel) - 1, &put, nullptr) ||
      put != sizeof(sentinel) - 1 || !FlushFileBuffers(competing)) {
    CloseHandle(competing);
    std::exit(78);
  }
  CloseHandle(competing);
}

int main(int argc, char **argv) {
  const char *guard = std::getenv("CE_CAREER_RESTORE_DISPOSABLE");
  if (!guard || std::strcmp(guard, "1") != 0)
    return 90;
  if (argc < 3)
    return 91;

  const std::string base = argv[1];
  const std::string cmd = argv[2];

  if (cmd == "fail-closed") {
    CeCareerRestoreRequest empty;
    std::string stamp = "stale";
    const bool stamped = CeCareerRestoreStamp(empty, stamp);
    const auto backup = CeCareerBackupAll(empty);
    const auto take = CeCareerTakeFullSet(empty);
    const auto restore = CeCareerRestoreAsNew(empty);
    std::string target = "stale";
    const auto suggest = CeCareerRestoreSuggest(empty, target);
    const bool ok = !stamped && stamp.empty() &&
                    backup.status == CeCareerStorageStatus::Refused &&
                    backup.backupSet.empty() && backup.backupIdentity.empty() &&
                    backup.backupStamp.empty() &&
                    take.status == CeCareerStorageStatus::Refused &&
                    take.backupSet.empty() && take.backupIdentity.empty() &&
                    take.backupStamp.empty() &&
                    restore.status == CeCareerStorageStatus::Refused &&
                    restore.backupSet.empty() && restore.newFile.empty() &&
                    suggest == CeCareerStorageStatus::Refused && target.empty();
    std::printf("FAIL_CLOSED=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
  }

  if (readBytes(base + "\\AUTHORED") != "disposable windows custody fixture\n")
    return 92;

  CeCareerRestoreRequest req;
  req.user = base + "\\user";
  req.game = base + "\\game";
  req.package = base + "\\package";
  req.backupRoot = base + "\\external";
  req.sourceSet = base + "\\source\\onslaught-backup-20260405-123000";
  req.sourceName = "Amber.bes";
  req.permitted = permitCb;
  req.cancelled = cancelCb;
  req.namePermitted = namePermitCb;

  bool noStamp = false;
  bool wrongIdentity = false;
  bool mutateSourceBytes = false;
  bool replaceSourceSameBytes = false;
  bool holdLock = false;
  std::string explicitTarget;
  std::vector<std::string> hideFiles;
  std::vector<std::string> systemFiles;
  std::vector<std::pair<std::string, std::string>> symlinkFiles;
  std::vector<std::pair<std::string, std::string>> symlinkDirs;
  std::vector<std::pair<std::string, std::string>> junctionDirs;

  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "no-permit")
      gPermit = false;
    else if (arg == "null-permit")
      req.permitted = nullptr;
    else if (arg == "cancel")
      gCancel = true;
    else if (arg == "no-name-permit")
      gNamePermit = false;
    else if (arg == "null-name-permit")
      req.namePermitted = nullptr;
    else if (arg == "no-stamp")
      noStamp = true;
    else if (arg == "wrong-identity")
      wrongIdentity = true;
    else if (arg == "mutate-source-bytes")
      mutateSourceBytes = true;
    else if (arg == "replace-source-same-bytes")
      replaceSourceSameBytes = true;
    else if (arg == "hold-lock")
      holdLock = true;
    else if (startsWith(arg, "user="))
      req.user = arg.substr(5);
    else if (startsWith(arg, "game="))
      req.game = arg.substr(5);
    else if (startsWith(arg, "package="))
      req.package = arg.substr(8);
    else if (startsWith(arg, "root="))
      req.backupRoot = arg.substr(5);
    else if (startsWith(arg, "source-set="))
      req.sourceSet = arg.substr(11);
    else if (startsWith(arg, "source-name="))
      req.sourceName = arg.substr(12);
    else if (startsWith(arg, "target="))
      explicitTarget = arg.substr(7);
    else if (startsWith(arg, "fault="))
      gFaultMode = arg.substr(6);
    else if (startsWith(arg, "expect-pair=")) {
      const std::string pair = arg.substr(12);
      const size_t sep = pair.find('|');
      if (sep != std::string::npos) {
        gExpectedSource = pair.substr(0, sep);
        gExpectedTarget = pair.substr(sep + 1);
        gCheckExactPair = true;
      }
    } else if (startsWith(arg, "hide="))
      hideFiles.push_back(arg.substr(5));
    else if (startsWith(arg, "system="))
      systemFiles.push_back(arg.substr(7));
    else if (startsWith(arg, "symlink-file=")) {
      const std::string spec = arg.substr(13);
      const size_t sep = spec.find('|');
      if (sep == std::string::npos)
        return 93;
      symlinkFiles.emplace_back(spec.substr(0, sep), spec.substr(sep + 1));
    } else if (startsWith(arg, "symlink-dir=")) {
      const std::string spec = arg.substr(12);
      const size_t sep = spec.find('|');
      if (sep == std::string::npos)
        return 93;
      symlinkDirs.emplace_back(spec.substr(0, sep), spec.substr(sep + 1));
    } else if (startsWith(arg, "junction-dir=")) {
      const std::string spec = arg.substr(13);
      const size_t sep = spec.find('|');
      if (sep == std::string::npos)
        return 93;
      junctionDirs.emplace_back(spec.substr(0, sep), spec.substr(sep + 1));
    } else
      return 93;
  }

  for (const auto &p : symlinkFiles) {
    if (!CreateSymbolicLinkW(toWide(p.first).c_str(), toWide(p.second).c_str(),
                             0x2) &&
        !CreateSymbolicLinkW(toWide(p.first).c_str(), toWide(p.second).c_str(),
                             0))
      return 94;
  }
  for (const auto &p : symlinkDirs) {
    if (!CreateSymbolicLinkW(toWide(p.first).c_str(), toWide(p.second).c_str(),
                             SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2) &&
        !CreateSymbolicLinkW(toWide(p.first).c_str(), toWide(p.second).c_str(),
                             SYMBOLIC_LINK_FLAG_DIRECTORY))
      return 94;
  }
  for (const auto &p : junctionDirs) {
    if (!createDirectoryJunction(p.first, p.second))
      return 94;
  }

  for (const std::string &path : hideFiles) {
    DWORD attr = GetFileAttributesW(toWide(path).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES ||
        !SetFileAttributesW(toWide(path).c_str(), attr | FILE_ATTRIBUTE_HIDDEN))
      return 94;
  }
  for (const std::string &path : systemFiles) {
    DWORD attr = GetFileAttributesW(toWide(path).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES ||
        !SetFileAttributesW(toWide(path).c_str(), attr | FILE_ATTRIBUTE_SYSTEM))
      return 95;
  }

  if (cmd == "lock-hold") {
    HANDLE held = acquireCustodyLock(req.user);
    if (held == INVALID_HANDLE_VALUE)
      return 96;
    if (!writeBytes(base + "\\lock-ready", "ready\n")) {
      releaseCustodyLock(held);
      return 97;
    }
    const std::wstring releasePath = toWide(base + "\\lock-release");
    for (int i = 0; i < 3000; ++i) {
      if (GetFileAttributesW(releasePath.c_str()) != INVALID_FILE_ATTRIBUTES)
        break;
      Sleep(5);
    }
    releaseCustodyLock(held);
    std::printf("LOCK_HELD=1\n");
    return 0;
  }

  if (cmd == "list") {
    std::vector<std::string> names;
    std::string error;
    const bool ok = CePreviewList(req.backupRoot, req.user, req.game,
                                  req.package, names, error);
    std::printf("LIST_OK=%d\n", ok ? 1 : 0);
    for (const std::string &name : names) {
      CePreviewSet set;
      std::string why;
      const bool good = CePreviewRead(req.backupRoot + "\\" + name, req.user,
                                      req.game, req.package, set, why);
      std::printf("LIST_ITEM=%s|%d|%s|%d|%u|%s\n", name.c_str(), good ? 1 : 0,
                  good ? set.identity.c_str() : "-", good ? set.members : 0,
                  good ? static_cast<unsigned>(set.careers.size()) : 0u,
                  good ? set.reason.c_str() : "-");
    }
    return 0;
  }

  HANDLE heldLock = INVALID_HANDLE_VALUE;
  if (holdLock) {
    heldLock = acquireCustodyLock(req.user);
    if (heldLock == INVALID_HANDLE_VALUE)
      return 98;
  }

  if (cmd == "backup") {
    gFaultArmed = true;
    const auto out = CeCareerBackupAll(req);
    gFaultArmed = false;
    releaseCustodyLock(heldLock);
    printResult(out);
    return 0;
  }

  if (cmd == "take") {
    gFaultArmed = true;
    const auto out = CeCareerTakeFullSet(req);
    gFaultArmed = false;
    releaseCustodyLock(heldLock);
    printResult(out);
    return 0;
  }

  if (cmd == "stamp") {
    CePreviewSet preview;
    std::string error;
    const bool readOk = CePreviewRead(req.sourceSet, req.user, req.game,
                                      req.package, preview, error);
    req.sourceIdentity =
        wrongIdentity ? std::string(64, 'f') : preview.identity;
    std::string stamp;
    gFaultArmed = true;
    const bool ok = CeCareerRestoreStamp(req, stamp);
    gFaultArmed = false;
    releaseCustodyLock(heldLock);
    std::printf("READ_OK=%d\nSTAMP_OK=%d\nIDENTITY=%s\nSTAMP=%s\n",
                readOk ? 1 : 0, ok ? 1 : 0, preview.identity.c_str(),
                stamp.c_str());
    return 0;
  }

  // suggest / restore both bind source preview + stamp first.
  CePreviewSet preview;
  std::string error;
  if (CePreviewRead(req.sourceSet, req.user, req.game, req.package, preview,
                    error)) {
    req.sourceIdentity = preview.identity;
    CeCareerRestoreStamp(req, req.sourceStamp);
  }
  if (noStamp)
    req.sourceStamp.clear();
  if (wrongIdentity)
    req.sourceIdentity = std::string(64, 'f');

  if (mutateSourceBytes) {
    const std::string memberPath = req.sourceSet + "\\" + req.sourceName;
    std::string data = readBytes(memberPath);
    if (data.empty()) {
      releaseCustodyLock(heldLock);
      return 99;
    }
    data.back() ^= 1;
    if (!writeBytes(memberPath, data)) {
      releaseCustodyLock(heldLock);
      return 100;
    }
  }
  if (replaceSourceSameBytes) {
    const std::string memberPath = req.sourceSet + "\\" + req.sourceName;
    const std::string tmpPath = req.sourceSet + "\\.swap-same.tmp";
    const std::string data = readBytes(memberPath);
    if (data.empty() || !writeBytes(tmpPath, data)) {
      releaseCustodyLock(heldLock);
      return 101;
    }
    if (!MoveFileExW(toWide(tmpPath).c_str(), toWide(memberPath).c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
      releaseCustodyLock(heldLock);
      return 102;
    }
  }

  if (cmd == "suggest") {
    std::string target;
    gFaultArmed = true;
    const auto st = CeCareerRestoreSuggest(req, target);
    gFaultArmed = false;
    releaseCustodyLock(heldLock);
    std::printf("STATUS=%d\nTARGET=%s\nSTAMP=%s\n", int(st), target.c_str(),
                req.sourceStamp.c_str());
    return 0;
  }

  if (cmd == "restore") {
    if (!explicitTarget.empty()) {
      req.targetName = explicitTarget;
    } else {
      std::string suggested;
      if (CeCareerRestoreSuggest(req, suggested) ==
          CeCareerStorageStatus::Complete)
        req.targetName = suggested;
      else
        req.targetName = "Amber restored.bes";
    }
    gFaultArmed = true;
    const auto out = CeCareerRestoreAsNew(req);
    gFaultArmed = false;
    releaseCustodyLock(heldLock);
    printResult(out);
    return 0;
  }

  releaseCustodyLock(heldLock);
  return 103;
}
