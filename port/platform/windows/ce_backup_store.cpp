// Selected ordinary Windows careers: read the verified CE entry and publish a new Companion set.
#include "ce_backup_paths.h"
#include "../common/ce_career_backup.h"
#include "../common/ce_win_reparse.h"
#include "../common/save_names.h"
namespace digest {
#include "../../updater/sha256.c"
}
namespace {
using namespace ce_backup_paths;
constexpr unsigned careerBytes = 10004;
constexpr NTSTATUS collision = static_cast<NTSTATUS>(0xc0000035u);
bool fileStamp(HANDLE h,const Wide &leaf,CeBackupOrigin &stamp) {
    FILE_BASIC_INFO basic{}; FILE_STANDARD_INFO info{}; FILE_ID_INFO id{};
    if (GetFileType(h)!=FILE_TYPE_DISK || !GetFileInformationByHandleEx(h,FileBasicInfo,&basic,sizeof(basic))
        || (basic.FileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM))
        || ce_win_handle_is_link(h,basic.FileAttributes)
        || !GetFileInformationByHandleEx(h,FileStandardInfo,&info,sizeof(info)) || info.Directory || info.DeletePending
        || info.NumberOfLinks!=1 || info.EndOfFile.QuadPart!=careerBytes
        || !GetFileInformationByHandleEx(h,FileIdInfo,&id,sizeof(id)) || !physicalLeaf(h,leaf)) return false;
    stamp.volume=id.VolumeSerialNumber; std::memcpy(stamp.id,id.FileId.Identifier,16);
    stamp.written=basic.LastWriteTime.QuadPart; stamp.changed=basic.ChangeTime.QuadPart; return true;
}
bool match(const CeBackupOrigin &a,const CeBackupOrigin &b) {
    return a.volume==b.volume && a.written==b.written && a.changed==b.changed && !std::memcmp(a.id,b.id,16);
}
bool sameBytes(HANDLE h,const void *data,size_t n) {
    LARGE_INTEGER zero{},len{};
    if (!GetFileSizeEx(h,&len) || len.QuadPart!=static_cast<LONGLONG>(n) || !SetFilePointerEx(h,zero,nullptr,FILE_BEGIN)) return false;
    const auto *p=static_cast<const unsigned char*>(data); unsigned char buf[4096];
    for (size_t off=0;off<n;) {
        DWORD got=0,ask=static_cast<DWORD>(std::min(n-off,sizeof(buf)));
        if (!ReadFile(h,buf,ask,&got,nullptr) || got!=ask || std::memcmp(p+off,buf,got)) return false;
        off+=got;
    }
    return true;
}
bool writeVerified(HANDLE h,const void *data,size_t n) {
    DWORD put=0;
    return WriteFile(h,data,static_cast<DWORD>(n),&put,nullptr) && put==n && FlushFileBuffers(h) && sameBytes(h,data,n);
}
std::string quote(const std::string &s) {
    std::string q="\""; for (char c:s) { if (c=='"' || c=='\\') q+='\\'; q+=c; } return q+'"';
}
std::string hash(const void *data,size_t n) {
    digest::SHA256_CTX c; unsigned char sum[32]; char out[65];
    digest::sha256_init(&c); digest::sha256_update(&c,data,n); digest::sha256_final(&c,sum);
    for (size_t i=0;i<32;++i) std::snprintf(out+2*i,3,"%02x",sum[i]); return out;
}
bool manifestRename(HANDLE file,HANDLE set) {
    static const auto nt=reinterpret_cast<decltype(&NtSetInformationFile)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtSetInformationFile"));
    if (!nt) return false;
    const wchar_t target[]=L"onslaught-backup.json";
    constexpr ULONG bytes=sizeof(target)-sizeof(wchar_t);
    alignas(FILE_RENAME_INFORMATION) unsigned char data[sizeof(FILE_RENAME_INFORMATION)+sizeof(target)]{};
    auto *rename=reinterpret_cast<FILE_RENAME_INFORMATION*>(data);
    rename->ReplaceIfExists=FALSE; rename->RootDirectory=set; rename->FileNameLength=bytes;
    std::memcpy(rename->FileName,target,bytes);
    IO_STATUS_BLOCK io{};
    return nt(file,&io,rename,offsetof(FILE_RENAME_INFORMATION,FileName)+bytes,FileRenameInformation)==0;
}
}
bool CeBackupRememberWindows(const char *user,const void *typed,CeBackupOrigin &origin) {
    origin={};
    try {
        char leaf[sizeof(origin.file)]{};
        if (!typed || CeBackupNameHasDirectionMark(static_cast<const wchar_t*>(typed))
            || PortSaveLookupFileUtf16(typed,leaf,sizeof(leaf)) || CeBackupFileHasDirectionMark(leaf,std::strlen(leaf))) return false;
        Directory root; Wide name;
        if (!root.open(user) || !wide(leaf,name) || !component(name)) return false;
        Handle saves=child(root.h(),L"savegames",true,false); FILE_ID_INFO id{};
        if (!saves || !dirInfo(saves.h,id)) return false;
        Handle file=child(saves.h,name,false,false);
        if (!file || !fileStamp(file.h,name,origin)) return false;
        std::memcpy(origin.file,leaf,std::strlen(leaf)+1); return true;
    } catch (...) { return false; }
}
bool CeBackupStore(const CeBackupInput &in,char *location,size_t room,const char *&error) {
    error="Choose an existing local folder outside the game, its package and your careers.";
    if (!location || !room) return false; *location=0;
    if (!in.filename || !in.origin || !in.origin->file[0] || !in.selected || in.length!=careerBytes) return false;
    if (CeBackupFileHasDirectionMark(in.filename,std::strlen(in.filename))) { error=CeBackupDirectionReason(); return false; }
    Directory user,game,package,folder;
    if (!user.open(in.user) || !game.open(in.game) || !package.open(in.package) || !folder.open(in.folder)
        || !outside(folder,user,game,package)) return false;
    error="This career is not a separate, unchanged file in your CE saves.";
    wchar_t requested[32]{},listed[32]{}; Wide name;
    if (PortSaveNameUtf16(in.filename,requested,32) || PortSaveNameUtf16(in.origin->file,listed,32)
        || PortSaveNamesEqual(requested,listed)!=1 || !wide(in.origin->file,name) || !component(name)) return false;
    char current[sizeof(in.origin->file)]{};
    if (PortSaveLookupFileUtf16(requested,current,sizeof(current)) || std::strcmp(current,in.origin->file)) return false;
    Handle saves=child(user.h(),L"savegames",true,false); FILE_ID_INFO id{};
    if (!saves || !dirInfo(saves.h,id)) return false;
    Handle original=child(saves.h,name,false,false);
    auto unchanged=[&]() {
        CeBackupOrigin stamp{};
        return original && fileStamp(original.h,name,stamp) && match(stamp,*in.origin) && sameBytes(original.h,in.selected,in.length);
    };
    if (!unchanged()) return false;
    SYSTEMTIME t{}; GetSystemTime(&t);
    char date[32]; std::snprintf(date,sizeof(date),"%04u-%02u-%02uT%02u:%02u:%02uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    wchar_t base[64]; swprintf(base,64,L"onslaught-backup-%04u%02u%02u-%02u%02u%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    error="A new backup folder could not be created. Choose another folder.";
    for (unsigned i=0;i<1000;++i) {
        Wide entry=base; if (i) entry+=L"-"+std::to_wstring(i);
        NTSTATUS why=0; Handle set=child(folder.h(),entry,true,true,&why);
        if (!set) { if (why==collision) continue; return false; }
        set.discard=true;
        Wide where,was; std::string published,originalPath,userPath;
        if (!dirInfo(set.h,id) || !physical(set.h,where) || !physical(original.h,was) ||
            !utf8(where,published) || !utf8(was,originalPath) || !utf8(user.path,userPath) || published.size()>=room) return false;
        error="The copy could not be verified. No backup was published.";
        Handle copy=child(set.h,name,false,true); if (!copy) return false; copy.discard=true;
        if (!writeVerified(copy.h,in.selected,in.length) || !unchanged()) return false;
        std::string json="{\"Folder\":"+quote(published)+",\"Created\":"+quote(date)+",\"Game\":"+quote(userPath)+
            ",\"Files\":[{\"Name\":"+quote(current)+",\"Original\":"+quote(originalPath)+",\"Size\":10004,\"Sha256\":"+
            quote(hash(in.selected,in.length))+"}],\"Reason\":\"Manual: selected CE career\",\"Target\":1}\n";
        Handle manifest=child(set.h,L".manifest-pending",false,true); if (!manifest) return false; manifest.discard=true;
        if (!writeVerified(manifest.h,json.data(),json.size()) || !sameBytes(copy.h,in.selected,in.length) || !unchanged()) return false;
        error="The backup manifest could not be published. No earlier backup was changed.";
        if (!manifestRename(manifest.h,set.h)) return false;
        manifest.discard=copy.discard=set.discard=false;
        std::memcpy(location,published.c_str(),published.size()+1); return true;
    }
    return false;
}
