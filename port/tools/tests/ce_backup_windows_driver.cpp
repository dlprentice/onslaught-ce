// Synthetic integration fixture. Run only in a disposable Windows user/prefix.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../../platform/common/ce_backup_name.h"
#include "../../platform/common/ce_career_backup.h"
#include "../../platform/common/save_names.h"

// The real enumerator is linked below. Its unrelated game-load, mod, and network
// entry points must never be called by this isolated Service Record fixture.
extern "C" int CE_CompatibilityObserveAuthoredRead(const char*,int,char*,size_t){ std::abort(); }
extern "C" int CE_CompatibilityAuthoredOpenFailed(const char*,char*,size_t){ std::abort(); }
extern "C" int CE_CompatibilityUnsupportedReader(char*,size_t){ std::abort(); }
extern "C" int CE_CompatibilityValidateOpenedFile(const char*,FILE*,char*,size_t){ std::abort(); }
extern "C" int CE_CompatibilityValidateOpenedNative(const char*,intptr_t,char*,size_t){ std::abort(); }
extern "C" int CE_BootRequested(){ std::abort(); }
extern "C" int CE_BootLocalFileAllowed(unsigned,const char*,const char*){ std::abort(); }
int PortModsActive(){ std::abort(); }
void *PortModOpenRead(const char*){ std::abort(); }

namespace {
const char *root="C:\\ce-backup-authored", *user="C:\\ce-backup-authored\\user",
    *game="C:\\ce-backup-authored\\game",*package="C:\\ce-backup-authored\\package",
    *folder="C:\\ce-backup-authored\\backups",*saves="C:\\ce-backup-authored\\user\\savegames";
std::wstring w(const char *s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,nullptr,0); if (n<1) std::exit(86);
    std::wstring out(n,0); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,&out[0],n); out.pop_back(); return out;
}
std::wstring file(const char *base,const char *leaf) { return w(base)+L"\\"+w(leaf); }
void need(bool v,const char *label) { if (!v) { std::printf("FAIL %s\nCODE %lu\n",label,static_cast<unsigned long>(GetLastError())); std::exit(3); } }
void result(const char *label) { std::printf("PASS %s\n",label); }
void mkdirNew(const char *p) { need(CreateDirectoryW(w(p).c_str(),nullptr)!=0,"fresh disposable directory"); }
void put(const std::wstring &path,const std::vector<unsigned char> &v,bool newOnly=true) {
    HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,newOnly?CREATE_NEW:OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    need(h!=INVALID_HANDLE_VALUE,"open authored fixture"); DWORD n=0;
    need(WriteFile(h,v.data(),static_cast<DWORD>(v.size()),&n,nullptr)&&n==v.size()&&FlushFileBuffers(h)&&CloseHandle(h),"write authored fixture");
}
bool exact(const std::wstring &path,const std::vector<unsigned char> &expected) {
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if (h==INVALID_HANDLE_VALUE) return false;
    std::vector<unsigned char> v(expected.size()+1); DWORD n=0; bool ok=ReadFile(h,v.data(),static_cast<DWORD>(v.size()),&n,nullptr)&&
        n==expected.size()&&!std::memcmp(v.data(),expected.data(),n); CloseHandle(h); return ok;
}
size_t sets() {
    WIN32_FIND_DATAW data{};
    HANDLE it=FindFirstFileW((w(folder)+L"\\onslaught-backup-*").c_str(),&data); if (it==INVALID_HANDLE_VALUE) return 0;
    size_t n=0; do { if (data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ++n; } while (FindNextFileW(it,&data)); FindClose(it); return n;
}
bool remember(const char *name,CeBackupOrigin &o) {
    std::wstring typed=w(name); return CeBackupRememberWindows(user,typed.c_str(),o);
}
bool store(const char *name,CeBackupOrigin &o,const std::vector<unsigned char> &data,
        std::string &location,const char *where=folder) {
    char loc[4352]{}; const char *err=nullptr;
    bool ok=CeBackupStore({user,game,package,where,name,data.data(),data.size(),&o},loc,sizeof(loc),err);
    if (!ok && err) std::printf("REASON %s\n",err);
    location=loc; return ok;
}
}
int main() {
    // Never reuse a preexisting path or operate against the host profile.
    if (!std::getenv("CE_BACKUP_DISPOSABLE") || std::strcmp(std::getenv("CE_BACKUP_DISPOSABLE"),"1")) return 85;
    mkdirNew(root); for (auto p:{user,game,package,folder,saves}) mkdirNew(p);
    mkdirNew("C:\\ce-backup-authored\\game\\savegames");
    _putenv_s("BEA_USER_DIR",user); _putenv_s("BEA_GAME_DIR",game);
    std::vector<unsigned char> data(10004),changed(10004);
    for (size_t i=0;i<data.size();++i) data[i]=static_cast<unsigned char>((i*37+11)&255);
    changed=data; changed.back()^=1;
    const char *regular="Pilot",*accent=u8"Mélodie",*mixed=u8"Pilotשלום",*direction=u8"Mark\u202e";
    for (const char *leaf:{"Pilot.bes","Twin.bes",u8"Mélodie.BES",u8"Pilotשלום.bes"}) put(file(saves,leaf),data);
    put(file("C:\\ce-backup-authored\\game\\savegames","GameOnly.bes"),data);
    CeBackupOrigin key{}; std::string loc,one,two;
    need(remember(regular,key)&&!std::strcmp(key.file,"Pilot.bes"),"selected ordinary entry");
    need(sets()==0 && exact(file(saves,key.file),data),"cancel without a write"); result("selection_without_publication");
    need(store("Pilot.bes",key,data,one)&&exact(file(one.c_str(),key.file),data),"ordinary publish");
    need(store("Pilot.bes",key,data,two)&&one!=two&&sets()==2&&exact(file(one.c_str(),key.file),data),"repeat without replacement");
    result("ordinary_repeat_and_retained_first");
    need(!store("Twin.bes",key,data,loc)&&loc.empty()&&sets()==2,"wrong identical slot refused");
    result("distinct_equal_byte_slot_refused");
    for (const auto &p:std::vector<std::pair<const char*,const char*>>{{accent,u8"Mélodie.BES"},{mixed,u8"Pilotשלום.bes"}}) {
        need(remember(p.first,key)&&!std::strcmp(key.file,p.second),"name backed by actual entry");
        char canonical[256]; wchar_t display[32]{};
        need(CeBackupSelectedName(w(p.first).c_str(),canonical,sizeof(canonical),display)&&display==w(p.first),"wide name display");
        need(store(canonical,key,data,loc)&&exact(file(loc.c_str(),p.second),data),"unicode publication");
    }
    result("canonical_case_accent_and_natural_rtl");
    const char *badLeaf=u8"Mark\u202e.bes"; put(file(saves,badLeaf),data);
    need(!remember(direction,key)&&exact(file(saves,badLeaf),data)&&sets()==4,"direction refusal"); result("direction_refusal");
    need(!remember("GameOnly",key)&&!remember("Missing",key)&&sets()==4,"no fallback"); result("missing_or_game_only_refused");
    need(remember(regular,key),"reselect for protected roots");
    for (const char *dest:{user,game,package,saves,"C:\\ce-backup-authored\\absent","C:\\ce-backup-authored\\backups\\.."})
        need(!store("Pilot.bes",key,data,loc,dest)&&loc.empty()&&sets()==4,"unsafe folder");
    // Map S: directly to a nested CE user directory. The drive root
    // itself does not visit the protected user-folder handle during traversal.
    mkdirNew("C:\\ce-backup-authored\\user\\nested");
    need(GetFileAttributesW(L"S:\\")!=INVALID_FILE_ATTRIBUTES,"private drive alias exists");
    need(!store("Pilot.bes",key,data,loc,"S:\\")&&loc.empty()&&sets()==4,"nested protected drive alias");
    result("nested_protected_drive_alias_refused");
    result("protected_or_ambiguous_destination_refused");
    const auto original=file(saves,"Pilot.bes"), held=file(saves,"held.old");
    need(MoveFileW(original.c_str(),held.c_str())!=0,"keep selected original");
    need(!store("Pilot.bes",key,data,loc)&&exact(held,data)&&sets()==4,"missing selected file");
    put(original,data);
    need(!store("Pilot.bes",key,data,loc)&&exact(held,data)&&exact(original,data)&&sets()==4,"same-byte replacement");
    result("missing_and_same_byte_replacement_refused");
    need(remember(regular,key),"select replacement"); put(original,changed,false);
    need(!store("Pilot.bes",key,data,loc)&&exact(original,changed)&&exact(held,data)&&sets()==4,"changed contents");
    result("changed_bytes_preserved");
    const auto twin=file(saves,"Twin.bes"),alias=file(saves,"Alternative.bes");
    need(remember("Twin",key)&&CreateHardLinkW(alias.c_str(),twin.c_str(),nullptr),"seed two names");
    need(!remember("Twin",key)&&exact(alias,data)&&exact(twin,data)&&sets()==4,"hardlink refused");
    result("multi_link_selection_refused");
    need(exact(file(one.c_str(),"Pilot.bes"),data)&&exact(file(two.c_str(),"Pilot.bes"),data),"prior sets retained");
    result("all_prior_sets_intact");
}
