// Handle-relative local access shared by Windows backup creation and preview.
// Opening an existing component never follows a reparse point.
#pragma once
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h>
#include "../common/ce_backup_name.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace {
namespace ce_backup_paths {
using Wide = std::wstring;
constexpr DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    bool discard=false;
    Handle()=default;
    explicit Handle(HANDLE value) : h(value) {}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    Handle(Handle&& o) noexcept : h(o.h), discard(o.discard) { o.h=INVALID_HANDLE_VALUE; }
    ~Handle() {
        if (h==INVALID_HANDLE_VALUE) return;
        if (discard) {
            FILE_DISPOSITION_INFO d{TRUE};
            SetFileInformationByHandle(h,FileDispositionInfo,&d,sizeof(d));
        }
        CloseHandle(h);
    }
    explicit operator bool() const { return h!=INVALID_HANDLE_VALUE; }
};

bool wide(const char *s,Wide &result) {
    if (!s || !*s || std::strlen(s)>=4096) return false;
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,nullptr,0);
    if (n<=1 || n>=4096) return false;
    result.resize(n);
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,&result[0],n)) return false;
    result.pop_back(); return true;
}
bool utf8(const Wide &s,std::string &out) {
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.c_str(),-1,nullptr,0,nullptr,nullptr);
    if (n<=1 || n>=4096) return false;
    out.resize(n);
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.c_str(),-1,&out[0],n,nullptr,nullptr)) return false;
    out.pop_back(); return true;
}
// Match CE's relative-handle traversal: do not reopen a child by a mutable absolute path.
Handle child(HANDLE parent,const Wide &name,bool dir,bool create,NTSTATUS *status=nullptr) {
    static const auto nt=reinterpret_cast<decltype(&NtCreateFile)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtCreateFile"));
    if (!nt || name.empty() || name.size()>255) return Handle();
    UNICODE_STRING leaf{};
    leaf.Length=leaf.MaximumLength=static_cast<USHORT>(name.size()*sizeof(wchar_t));
    leaf.Buffer=const_cast<wchar_t*>(name.data());
    OBJECT_ATTRIBUTES oa{};
    InitializeObjectAttributes(&oa,&leaf,OBJ_CASE_INSENSITIVE,parent,nullptr);
    IO_STATUS_BLOCK io{};
    HANDLE h=INVALID_HANDLE_VALUE;
    ACCESS_MASK access=FILE_READ_ATTRIBUTES|SYNCHRONIZE|(dir?FILE_LIST_DIRECTORY:GENERIC_READ);
    if (create) access|=DELETE|(dir?0:GENERIC_WRITE);
    NTSTATUS result=nt(&h,access,&oa,&io,nullptr,dir?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL,
        dir?share:FILE_SHARE_READ,create?FILE_CREATE:FILE_OPEN,FILE_OPEN_REPARSE_POINT|
        FILE_SYNCHRONOUS_IO_NONALERT|(dir?FILE_DIRECTORY_FILE:FILE_NON_DIRECTORY_FILE),nullptr,0);
    if (status) *status=result;
    return Handle(result==0?h:INVALID_HANDLE_VALUE);
}
bool dirInfo(HANDLE h,FILE_ID_INFO &id) {
    FILE_ATTRIBUTE_TAG_INFO tag{};
    return GetFileType(h)==FILE_TYPE_DISK && GetFileInformationByHandleEx(h,FileAttributeTagInfo,&tag,sizeof(tag))
        && (tag.FileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(tag.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        && GetFileInformationByHandleEx(h,FileIdInfo,&id,sizeof(id));
}
bool sameId(const FILE_ID_INFO &a,const FILE_ID_INFO &b) {
    return a.VolumeSerialNumber==b.VolumeSerialNumber && !std::memcmp(a.FileId.Identifier,b.FileId.Identifier,16);
}
bool component(const Wide &s) {
    if (s.empty() || s.size()>255 || s.back()==L'.' || s.back()==L' ') return false;
    for (wchar_t c:s) if (c<32 || c==127 || std::wcschr(L"/\\:\"<>|?*",c) || CeBackupDirectionMark(c)) return false;
    // Full-path Win32 device aliases (including superscript digits) must not name a folder.
    Wide stem=s.substr(0,s.find(L'.'));
    while (!stem.empty() && stem.back()==L' ') stem.pop_back();
    for (auto &c:stem) if (c>=L'a' && c<=L'z') c-=L'a'-L'A';
    if (stem==L"CON" || stem==L"PRN" || stem==L"AUX" || stem==L"NUL" || stem==L"CONIN$" || stem==L"CONOUT$") return false;
    if (stem.size()==4 && (stem.substr(0,3)==L"COM" || stem.substr(0,3)==L"LPT")) {
        wchar_t d=stem[3];
        if ((d>=L'0' && d<=L'9') || d==0xb9 || d==0xb2 || d==0xb3) return false;
    }
    return true;
}
bool physical(HANDLE h,Wide &path) {
    wchar_t buf[4096];
    DWORD n=GetFinalPathNameByHandleW(h,buf,4096,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if (n<7 || n>=4096 || std::wcsncmp(buf,L"\\\\?\\",4) || buf[5]!=L':' || buf[6]!=L'\\') return false;
    path.assign(buf+4,n-4); return true;
}
bool physicalLeaf(HANDLE h,const Wide &name) {
    Wide path; if (!physical(h,path)) return false;
    auto pos=path.rfind(L'\\');
    return pos!=Wide::npos && path.compare(pos+1,Wide::npos,name)==0;
}
struct Directory {
    std::vector<Handle> handles;
    std::vector<FILE_ID_INFO> ids;
    Wide path, volumePath;
    bool open(const char *arg) {
        if (!wide(arg,path)) return false;
        std::replace(path.begin(),path.end(),L'/',L'\\');
        while (path.size()>3 && path.back()==L'\\') path.pop_back();
        if (path.size()<3 || path[1]!=L':' || path[2]!=L'\\' ||
            !((path[0]>=L'a' && path[0]<=L'z')||(path[0]>=L'A' && path[0]<=L'Z'))) return false;
        Wide root=path.substr(0,3);
        UINT type=GetDriveTypeW(root.c_str());
        if (type!=DRIVE_FIXED && type!=DRIVE_REMOVABLE) return false;
        wchar_t mapping[1024]; Wide drive=root.substr(0,2);
        // A SUBST drive starts at an arbitrary directory. Its Win32 and volume-relative
        // names may both hide the protected ancestors. Accept real volume roots only.
        DWORD count=QueryDosDeviceW(drive.c_str(),mapping,sizeof(mapping)/sizeof(*mapping));
        const wchar_t nativePrefix[]=L"\\Device\\";
        if (!count || _wcsnicmp(mapping,nativePrefix,8)!=0 || !mapping[8] || std::wcschr(mapping+8,L'\\'))
            return false;
        Handle first(CreateFileW(root.c_str(),FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,share,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        FILE_ID_INFO id{};
        if (!first || !dirInfo(first.h,id)) return false;
        handles.push_back(std::move(first)); ids.push_back(id);
        for (size_t pos=3;pos<path.size();) {
            size_t end=path.find(L'\\',pos);
            Wide part=path.substr(pos,end==Wide::npos?Wide::npos:end-pos);
            if (!component(part)) return false;
            Handle next=child(handles.back().h,part,true,false);
            if (!next || !dirInfo(next.h,id)) return false;
            handles.push_back(std::move(next)); ids.push_back(id);
            if (end==Wide::npos) break;
            pos=end+1;
        }
        wchar_t buf[4096];
        DWORD n=GetFinalPathNameByHandleW(handles.back().h,buf,4096,VOLUME_NAME_NONE|FILE_NAME_NORMALIZED);
        if (!n || n>=4096 || buf[0]!=L'\\') return false;
        volumePath.assign(buf,n);
        return physical(handles.back().h,path);
    }
    bool below(const Directory &parent) const {
        for (const auto &id:ids) if (sameId(id,parent.ids.back())) return true;
        // A drive may start below a protected directory. Its handle walk omits
        // the rest of the volume, so also compare normalized volume-relative names.
        size_t n=parent.volumePath.size();
        if (ids.back().VolumeSerialNumber!=parent.ids.back().VolumeSerialNumber ||
            volumePath.size()<n || (volumePath.size()>n && parent.volumePath.back()!=L'\\' && volumePath[n]!=L'\\'))
            return false;
        int cmp=CompareStringOrdinal(volumePath.data(),static_cast<int>(n),parent.volumePath.data(),static_cast<int>(n),TRUE);
        return cmp==0 || cmp==CSTR_EQUAL;
    }
    bool differentRootOnSameVolume(const Directory &other) const {
        return ids.front().VolumeSerialNumber==other.ids.front().VolumeSerialNumber &&
            !sameId(ids.front(),other.ids.front());
    }
    HANDLE h() const { return handles.back().h; }
};

inline bool outside(const Directory &folder, const Directory &user,
                    const Directory &game, const Directory &package) {
    return !folder.below(user) && !folder.below(game) && !folder.below(package) &&
           !folder.differentRootOnSameVolume(user) && !folder.differentRootOnSameVolume(game) &&
           !folder.differentRootOnSameVolume(package);
}
} // namespace ce_backup_paths
} // namespace
