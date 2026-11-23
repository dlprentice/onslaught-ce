// SPDX-License-Identifier: GPL-3.0-only
// Shared Windows NTFS owner-only ACL and reparse-point-free directory/file checks for CE relay & signaling.
#ifndef CE_WINDOWS_PRIVATE_ACL_H
#define CE_WINDOWS_PRIVATE_ACL_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace CeWinAcl {

struct ScopedHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ScopedHandle() noexcept = default;
    explicit ScopedHandle(HANDLE h) noexcept : value(h) {}
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept : value(other.value) { other.value = INVALID_HANDLE_VALUE; }
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            value = other.value;
            other.value = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    ~ScopedHandle() { Reset(); }
    void Reset() noexcept {
        if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
    HANDLE Release() noexcept {
        HANDLE h = value;
        value = INVALID_HANDLE_VALUE;
        return h;
    }
    bool Good() const noexcept { return value != INVALID_HANDLE_VALUE && value != nullptr; }
};

inline bool CurrentUserSid(std::vector<BYTE>& out) {
    out.clear();
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw) || !raw) return false;
    ScopedHandle token(raw);
    DWORD needed = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &needed);
    if (needed == 0 || needed > 65536) return false;
    std::vector<BYTE> data(needed);
    if (!GetTokenInformation(token.value, TokenUser, data.data(), needed, &needed)) return false;
    PSID sid = reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;
    if (!sid || !IsValidSid(sid)) return false;
    const DWORD len = GetLengthSid(sid);
    if (len == 0 || len > SECURITY_MAX_SID_SIZE) return false;
    out.resize(len);
    return CopySid(len, out.data(), sid) != 0;
}

inline uint64_t SidHash64(const std::vector<BYTE>& sid) noexcept {
    uint64_t h = 14695981039346656037ULL;
    for (BYTE b : sid) {
        h ^= uint64_t(b);
        h *= 1099511628211ULL;
    }
    return h;
}

inline bool HandleIsPrivate(HANDLE handle, PSID user) {
    if (handle == INVALID_HANDLE_VALUE || !handle || !user || !IsValidSid(user)) return false;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PSID owner = nullptr;
    PACL acl = nullptr;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                        &owner, nullptr, &acl, nullptr, &descriptor) != ERROR_SUCCESS)
        return false;
    struct FreeDesc { PSECURITY_DESCRIPTOR p; ~FreeDesc() { if (p) LocalFree(p); } } guard{descriptor};
    if (!descriptor || !IsValidSecurityDescriptor(descriptor) || !owner || !IsValidSid(owner) ||
        !EqualSid(owner, user) || !acl || !IsValidAcl(acl))
        return false;
    std::array<BYTE, SECURITY_MAX_SID_SIZE> system{}, admins{}, creator{};
    DWORD sysSize = static_cast<DWORD>(system.size());
    DWORD admSize = static_cast<DWORD>(admins.size());
    DWORD creSize = static_cast<DWORD>(creator.size());
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system.data(), &sysSize) ||
        !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins.data(), &admSize) ||
        !CreateWellKnownSid(WinCreatorOwnerSid, nullptr, creator.data(), &creSize))
        return false;
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void* raw = nullptr;
        if (!GetAce(acl, i, &raw) || !raw) return false;
        auto* header = static_cast<ACE_HEADER*>(raw);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || header->AceSize < sizeof(ACCESS_ALLOWED_ACE))
            return false;
        auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        if (allowed->Mask == 0) continue;
        PSID sid = &allowed->SidStart;
        const size_t room = header->AceSize - offsetof(ACCESS_ALLOWED_ACE, SidStart);
        if (room < 8 || GetSidLengthRequired(*GetSidSubAuthorityCount(sid)) > room || !IsValidSid(sid))
            return false;
        if (!EqualSid(sid, user) && !EqualSid(sid, system.data()) &&
            !EqualSid(sid, admins.data()) && !EqualSid(sid, creator.data()))
            return false;
    }
    return true;
}

struct OwnerOnlySecurity {
    SECURITY_DESCRIPTOR sd{};
    SECURITY_ATTRIBUTES sa{};
    std::vector<BYTE> aclStorage;
    std::vector<BYTE> userSid;
    bool valid = false;

    bool Init(bool inheritHandle = false) {
        valid = false;
        if (!CurrentUserSid(userSid)) return false;
        std::array<BYTE, SECURITY_MAX_SID_SIZE> system{}, admins{};
        DWORD sysSize = static_cast<DWORD>(system.size());
        DWORD admSize = static_cast<DWORD>(admins.size());
        if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system.data(), &sysSize) ||
            !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins.data(), &admSize))
            return false;
        const DWORD aclSize = static_cast<DWORD>(
            sizeof(ACL) +
            3 * (sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD)) +
            GetLengthSid(userSid.data()) + sysSize + admSize + 16);
        aclStorage.assign(aclSize, 0);
        PACL acl = reinterpret_cast<PACL>(aclStorage.data());
        if (!InitializeAcl(acl, aclSize, ACL_REVISION)) return false;
        if (!AddAccessAllowedAceEx(acl, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
                                   GENERIC_ALL, userSid.data()) ||
            !AddAccessAllowedAceEx(acl, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
                                   GENERIC_ALL, system.data()) ||
            !AddAccessAllowedAceEx(acl, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
                                   GENERIC_ALL, admins.data()))
            return false;
        if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) ||
            !SetSecurityDescriptorOwner(&sd, userSid.data(), FALSE) ||
            !SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE) ||
            !SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED))
            return false;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.lpSecurityDescriptor = &sd;
        sa.bInheritHandle = inheritHandle ? TRUE : FALSE;
        valid = true;
        return true;
    }
};

inline bool NormalizeAbsolutePath(const char* utf8Path, std::wstring& outWide) {
    outWide.clear();
    if (!utf8Path) return false;
    size_t len = 0;
    while (len <= 4096 && utf8Path[len]) ++len;
    if (len < 3 || len > 4096) return false;
    if (!((utf8Path[0] >= 'A' && utf8Path[0] <= 'Z') || (utf8Path[0] >= 'a' && utf8Path[0] <= 'z')) ||
        utf8Path[1] != ':' || (utf8Path[2] != '\\' && utf8Path[2] != '/'))
        return false;
    for (size_t i = 2; i < len; ++i) {
        if (utf8Path[i] == ':') return false;
    }
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Path, int(len), nullptr, 0);
    if (needed <= 0) return false;
    outWide.resize(size_t(needed));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Path, int(len), &outWide[0], needed) != needed) {
        outWide.clear();
        return false;
    }
    for (wchar_t& ch : outWide) {
        if (ch == L'/') ch = L'\\';
    }
    while (outWide.size() > 3 && outWide.back() == L'\\') outWide.pop_back();
    size_t pos = 3;
    while (pos < outWide.size()) {
        size_t next = outWide.find(L'\\', pos);
        if (next == std::wstring::npos) next = outWide.size();
        const size_t partLen = next - pos;
        if (partLen == 0) return false;
        if ((partLen == 1 && outWide[pos] == L'.') ||
            (partLen == 2 && outWide[pos] == L'.' && outWide[pos + 1] == L'.'))
            return false;
        pos = (next < outWide.size()) ? next + 1 : next;
    }
    return true;
}

inline bool ValidLeafName(const std::wstring& leaf) {
    if (leaf.empty() || leaf.size() > 128 || leaf == L"." || leaf == L"..") return false;
    for (wchar_t c : leaf) {
        if (c < 32 || c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"' || c == L'<' || c == L'>' || c == L'|')
            return false;
    }
    return true;
}

inline bool CheckPathComponentsNoReparse(const std::wstring& path) {
    if (path.size() < 3) return false;
    size_t pos = 3;
    while (pos <= path.size()) {
        size_t next = path.find(L'\\', pos);
        if (next == std::wstring::npos) next = path.size();
        if (next > 3) {
            std::wstring prefix = path.substr(0, next);
            ScopedHandle h(CreateFileW(prefix.c_str(), FILE_READ_ATTRIBUTES,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (!h.Good()) return false;
            BY_HANDLE_FILE_INFORMATION info{};
            if (GetFileType(h.value) != FILE_TYPE_DISK || !GetFileInformationByHandle(h.value, &info))
                return false;
            if ((info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0)
                return false;
            if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                return false;
        }
        if (next == path.size()) break;
        pos = next + 1;
    }
    return true;
}

inline bool OpenPrivateDirectory(const char* utf8Path, ScopedHandle& outHandle,
                                 std::wstring* outWidePath = nullptr,
                                 BY_HANDLE_FILE_INFORMATION* outInfo = nullptr,
                                 uint64_t* outUserHash = nullptr) {
    outHandle.Reset();
    std::wstring wide;
    if (!NormalizeAbsolutePath(utf8Path, wide)) return false;
    if (!CheckPathComponentsNoReparse(wide)) return false;
    std::vector<BYTE> userSid;
    if (!CurrentUserSid(userSid)) return false;
    ScopedHandle h(CreateFileW(wide.c_str(),
                               FILE_TRAVERSE | FILE_READ_ATTRIBUTES | READ_CONTROL,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!h.Good()) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(h.value) != FILE_TYPE_DISK || !GetFileInformationByHandle(h.value, &info))
        return false;
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0)
        return false;
    if (!HandleIsPrivate(h.value, userSid.data())) return false;
    if (outWidePath) *outWidePath = wide;
    if (outInfo) *outInfo = info;
    if (outUserHash) *outUserHash = SidHash64(userSid);
    outHandle = std::move(h);
    return true;
}

inline bool CreatePrivateDirectory(const std::wstring& path, bool* outAlreadyExists = nullptr) {
    if (outAlreadyExists) *outAlreadyExists = false;
    OwnerOnlySecurity sec;
    if (!sec.Init(false)) return false;
    if (!CreateDirectoryW(path.c_str(), &sec.sa)) {
        if (outAlreadyExists && GetLastError() == ERROR_ALREADY_EXISTS) *outAlreadyExists = true;
        return false;
    }
    return true;
}

inline bool ReadPrivateFile(const std::wstring& dirWidePath, const std::string& leafAscii,
                            size_t minBytes, size_t maxBytes,
                            std::vector<uint8_t>& outBytes, bool* outNotFound = nullptr) {
    outBytes.clear();
    if (outNotFound) *outNotFound = false;
    std::wstring leaf(leafAscii.begin(), leafAscii.end());
    if (!ValidLeafName(leaf)) return false;
    std::vector<BYTE> userSid;
    if (!CurrentUserSid(userSid)) return false;
    const std::wstring full = dirWidePath + L"\\" + leaf;
    ScopedHandle file(CreateFileW(full.c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file.Good()) {
        if (outNotFound && GetLastError() == ERROR_FILE_NOT_FOUND) *outNotFound = true;
        return false;
    }
    BY_HANDLE_FILE_INFORMATION before{}, after{};
    if (GetFileType(file.value) != FILE_TYPE_DISK || !GetFileInformationByHandle(file.value, &before))
        return false;
    if ((before.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0 ||
        before.nNumberOfLinks != 1 || before.nFileSizeHigh != 0 ||
        before.nFileSizeLow < minBytes || before.nFileSizeLow > maxBytes)
        return false;
    if (!HandleIsPrivate(file.value, userSid.data())) return false;
    outBytes.resize(before.nFileSizeLow);
    size_t offset = 0;
    while (offset < outBytes.size()) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(outBytes.size() - offset);
        if (!ReadFile(file.value, outBytes.data() + offset, want, &got, nullptr) || got == 0) {
            outBytes.clear();
            return false;
        }
        offset += got;
    }
    BYTE extra = 0;
    DWORD extraGot = 0;
    if (!ReadFile(file.value, &extra, 1, &extraGot, nullptr) || extraGot != 0 ||
        !GetFileInformationByHandle(file.value, &after) ||
        before.nFileSizeLow != after.nFileSizeLow ||
        before.ftLastWriteTime.dwLowDateTime != after.ftLastWriteTime.dwLowDateTime ||
        before.ftLastWriteTime.dwHighDateTime != after.ftLastWriteTime.dwHighDateTime ||
        before.dwVolumeSerialNumber != after.dwVolumeSerialNumber ||
        before.nFileIndexHigh != after.nFileIndexHigh ||
        before.nFileIndexLow != after.nFileIndexLow) {
        outBytes.clear();
        return false;
    }
    return true;
}

inline bool PublishPrivateFileNoReplace(const std::wstring& dirWidePath,
                                        const std::string& tempAscii,
                                        const std::string& finalAscii,
                                        const void* data, size_t size) {
    std::wstring tempLeaf(tempAscii.begin(), tempAscii.end());
    std::wstring finalLeaf(finalAscii.begin(), finalAscii.end());
    if (!ValidLeafName(tempLeaf) || !ValidLeafName(finalLeaf) || (!data && size > 0)) return false;
    OwnerOnlySecurity sec;
    if (!sec.Init(false)) return false;
    const std::wstring tempPath = dirWidePath + L"\\" + tempLeaf;
    const std::wstring finalPath = dirWidePath + L"\\" + finalLeaf;
    ScopedHandle file(CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, &sec.sa, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file.Good()) return false;
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t offset = 0;
    bool ok = true;
    while (offset < size) {
        DWORD wrote = 0;
        const DWORD chunk = static_cast<DWORD>(size - offset);
        if (!WriteFile(file.value, bytes + offset, chunk, &wrote, nullptr) || wrote == 0) {
            ok = false;
            break;
        }
        offset += wrote;
    }
    if (ok) ok = FlushFileBuffers(file.value) != 0;
    file.Reset();
    if (ok) {
        // Omit MOVEFILE_REPLACE_EXISTING to fail closed if the target already exists (matching POSIX RENAME_NOREPLACE / linkat).
        ok = MoveFileExW(tempPath.c_str(), finalPath.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
    }
    if (!ok) DeleteFileW(tempPath.c_str());
    return ok;
}

} // namespace CeWinAcl
#endif
#endif
