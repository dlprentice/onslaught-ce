#include "../platform/common/private_ui_input.h"
// SPDX-License-Identifier: GPL-3.0-only
#include "ce_network_invitation.h"
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
#include "ce_network_stl.h"
#include "ce_network_boot_authority.h"
#include "../network/network.h"
#include "../network/signaling_https.h"
#include "../platform/common/ce_netlobby.h"
#include "../platform/common/platform.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr size_t kImportFileLimit=4096;
constexpr size_t kMetadataLimit=1024;
constexpr size_t kPathLimit=4096;
constexpr uint64_t kPairLifetimeSeconds=4*60*60;
void Wipe(void* value,size_t size){
    volatile uint8_t* p=static_cast<volatile uint8_t*>(value);
    while(size--)*p++=0;
}
struct ImportBytes {
    std::vector<uint8_t> value;
    ~ImportBytes(){if(!value.empty())Wipe(value.data(),value.size());}
};
// Open/Read retain the existing ICE exchange reader's native protections.
// This import-only copy has no publication operation.
struct InvitationDirectory {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    std::wstring path;
    ~InvitationDirectory() { if (handle!=INVALID_HANDLE_VALUE) CloseHandle(handle); }
    bool Open(const char* name) {
        int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,nullptr,0);
        if (!count) return false;
        std::vector<wchar_t> wide(static_cast<size_t>(count));
        if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,wide.data(),count)) return false;
        path=wide.data();
        handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        BY_HANDLE_FILE_INFORMATION info{};
        return handle!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle,&info) &&
            (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) &&
            !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
    }
    std::wstring Child(const std::string& name) const {
        return path+L"\\"+std::wstring(name.begin(),name.end());
    }
    int Read(const std::string& name, std::vector<uint8_t>& bytes) {
        HANDLE file=CreateFileW(Child(name).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
            OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if (file==INVALID_HANDLE_VALUE) return GetLastError()==ERROR_FILE_NOT_FOUND?0:-1;
        BY_HANDLE_FILE_INFORMATION info{}; DWORD got=0;
        bool ok=GetFileInformationByHandle(file,&info) && !info.nFileSizeHigh &&
            !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
            info.nFileSizeLow>=1 && info.nFileSizeLow<=kImportFileLimit;
        if (ok) {
            bytes.resize(info.nFileSizeLow);
            ok=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr) && got==bytes.size();
        }
        CloseHandle(file); return ok?1:-1;
    }

#else
    int handle=-1;
    ~InvitationDirectory() { if (handle>=0) close(handle); }
    bool Open(const char* name) {
        handle=open(name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        struct stat s{};
        return handle>=0 && fstat(handle,&s)==0 && S_ISDIR(s.st_mode) &&
            s.st_uid==geteuid() && !(s.st_mode&0077);
    }
    int Read(const std::string& name, std::vector<uint8_t>& bytes) {
        int file=openat(handle,name.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        if (file<0) return errno==ENOENT?0:-1;
        struct stat s{};
        bool ok=fstat(file,&s)==0 && S_ISREG(s.st_mode) && s.st_size>=static_cast<off_t>(1) &&
            s.st_size<=static_cast<off_t>(kImportFileLimit);
        if (ok) {
            bytes.resize(static_cast<size_t>(s.st_size)); size_t pos=0;
            while (pos<bytes.size()) {
                ssize_t n=read(file,bytes.data()+pos,bytes.size()-pos);
                if (n<0 && errno==EINTR) continue;
                if (n<=0) { ok=false; break; }
                pos+=static_cast<size_t>(n);
            }
            uint8_t extra=0; if (ok && read(file,&extra,1)!=0) ok=false;
        }
        close(file); return ok?1:-1;
    }

#endif

public:
    bool SameAs(const InvitationDirectory& other) const {
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION a{},b{};
        return GetFileInformationByHandle(handle,&a)&&GetFileInformationByHandle(other.handle,&b)&&
            a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow;
#else
        struct stat a{},b{};
        return fstat(handle,&a)==0&&fstat(other.handle,&b)==0&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_uid==b.st_uid;
#endif
    }
};

bool Bounded(const char* text,size_t maximum,size_t* length){
    if(length)*length=0;if(!text)return false;
    size_t n=0;while(n<=maximum&&text[n])++n;
    if(!n||n>maximum)return false;
    if(length)*length=n;return true;
}
bool AbsoluteLocalFolder(const char* path){
    size_t size=0;if(!Bounded(path,kPathLimit,&size))return false;
#ifdef _WIN32
    if(size<3||!((path[0]>='A'&&path[0]<='Z')||(path[0]>='a'&&path[0]<='z'))||path[1]!=':'||(path[2]!='\\'&&path[2]!='/'))return false;
    for(size_t i=2;i<size;++i)if(path[i]==':')return false;
#else
    if(path[0]!='/')return false;
#endif
    return true;
}
struct MetadataParser {
    const uint8_t* p;const uint8_t* end;
    void Space(){while(p<end&&(*p==' '||*p=='\n'||*p=='\r'||*p=='\t'))++p;}
    bool Take(char c){Space();if(p==end||*p!=uint8_t(c))return false;++p;return true;}
    bool String(char* out,size_t capacity){
        if(!Take('"'))return false;size_t n=0;
        while(p<end&&*p!='"'){
            const uint8_t c=*p++;
            if(c<' '||c>126||c=='\\'||n+1>=capacity)return false;
            out[n++]=char(c);
        }
        if(p==end)return false;++p;out[n]=0;return true;
    }
    bool Number(uint64_t* out){
        Space();if(p==end||*p<'0'||*p>'9')return false;
        if(*p=='0'&&p+1<end&&p[1]>='0'&&p[1]<='9')return false;
        uint64_t n=0;
        while(p<end&&*p>='0'&&*p<='9'){
            const uint64_t digit=*p++-'0';
            if(n>((std::numeric_limits<uint64_t>::max)()-digit)/10)return false;
            n=n*10+digit;
        }
        *out=n;return true;
    }
};
bool ParseMetadata(const uint8_t* bytes,size_t size,uint32_t expected,CeIceSignalPairBinding* out){
    if(!bytes||!size||size>kMetadataLimit||!out)return false;
    MetadataParser parser{bytes,bytes+size};CeIceSignalPairBinding pair{};unsigned seen=0;
    if(!parser.Take('{'))return false;
    for(unsigned field=0;field<4;++field){
        char key[24]{};if(!parser.String(key,sizeof(key))||!parser.Take(':'))return false;
        unsigned bit=0;
        if(!std::strcmp(key,"schema")){
            bit=1;uint64_t schema=0;if(!parser.Number(&schema)||schema!=1)return false;
        }else if(!std::strcmp(key,"approval")){
            bit=2;if(!parser.String(pair.approval,sizeof(pair.approval))||std::strlen(pair.approval)!=32)return false;
            for(unsigned i=0;i<32;++i)if(!((pair.approval[i]>='0'&&pair.approval[i]<='9')||(pair.approval[i]>='a'&&pair.approval[i]<='f')))return false;
        }else if(!std::strcmp(key,"role")){
            bit=4;char role[8]{};if(!parser.String(role,sizeof(role)))return false;
            if(!std::strcmp(role,"host"))pair.role=CE_NET_HOST;
            else if(!std::strcmp(role,"guest"))pair.role=CE_NET_GUEST;
            else return false;
        }else if(!std::strcmp(key,"expiresAt")){
            bit=8;if(!parser.Number(&pair.expiresAt))return false;
        }else return false;
        if(seen&bit)return false;seen|=bit;
        if(field<3&&!parser.Take(','))return false;
    }
    if(seen!=15||!parser.Take('}'))return false;parser.Space();
    if(parser.p!=parser.end||uint32_t(pair.role)!=expected)return false;
    *out=pair;return true;
}
bool Current(uint64_t utc,uint64_t expires){
    return utc&&expires>utc&&expires-utc<=kPairLifetimeSeconds;
}
struct PickerTicket {uint64_t generation;};
struct ImportState {
    std::mutex mutex;
    uint64_t generation=0,previewUtc=0;
    PickerTicket* outstanding=nullptr;
    unsigned picked=0;
    std::array<char,kPathLimit+1> chosen{};
    CE_InvitationView view{};
    std::unique_ptr<InvitationDirectory> source;
    std::array<uint8_t,kMetadataLimit> metadata{};
    size_t metadataSize=0;
    CeIceSignalPairBinding pair{};
};
ImportState& State(){static ImportState value;return value;}
void ClearPreview(ImportState& s){
    s.source.reset();Wipe(s.metadata.data(),s.metadata.size());s.metadataSize=0;
    Wipe(&s.pair,sizeof(s.pair));s.previewUtc=0;
}
void Refuse(ImportState& s,uint32_t reason){
    ClearPreview(s);s.view.state=CE_INVITATION_REFUSED;s.view.error=reason;
}
void SDLCALL Picked(void* opaque,const char* const* files,int){
    std::unique_ptr<PickerTicket> ticket(static_cast<PickerTicket*>(opaque));
    auto& s=State();std::lock_guard<std::mutex> lock(s.mutex);
    if(s.outstanding==ticket.get())s.outstanding=nullptr;
    if(ticket->generation!=s.generation||s.view.state!=CE_INVITATION_CHOOSING)return;
    s.picked=0;
    if(!files){s.picked=3;return;}
    if(!files[0]){s.picked=2;return;}
    size_t size=0;
    if(files[1]||!Bounded(files[0],kPathLimit,&size)||!AbsoluteLocalFolder(files[0])){s.picked=3;return;}
    std::memcpy(s.chosen.data(),files[0],size+1);s.picked=1;
}
bool NewExchangeDirectory(std::string* out){
    const char* base=PortUserDir();size_t length=0;
    if(!out||!Bounded(base,kPathLimit-80,&length)||!AbsoluteLocalFolder(base))return false;
#ifndef _WIN32
    const int parent=open(base,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(parent<0)return false;
    struct Parent {int value;~Parent(){close(value);}} owned{parent};
    struct stat info{};
    if(fstat(parent,&info)!=0||!S_ISDIR(info.st_mode)||info.st_uid!=geteuid()||(info.st_mode&0022))return false;
#else
    InvitationDirectory parent;if(!parent.Open(base))return false;
#endif
    for(unsigned attempt=0;attempt<8;++attempt){
        std::array<uint8_t,CE_NET_SECRET_BYTES> nonce{};
        if(!CE_NetGenerateSecret(nonce.data()))return false;
        char leaf[48]="signal-session-";size_t at=std::strlen(leaf);
        for(unsigned i=0;i<16;++i)for(unsigned shift:{4u,0u}){
            const unsigned value=(nonce[i]>>shift)&15;
            leaf[at++]=char(value<10?'0'+value:'a'+value-10);
        }
        leaf[at]=0;Wipe(nonce.data(),nonce.size());
        std::string path(base);if(path.back()!='/'&&path.back()!='\\')path+='/';path+=leaf;
#ifdef _WIN32
        if(!CreateDirectoryW(parent.Child(leaf).c_str(),nullptr)){
            if(GetLastError()==ERROR_ALREADY_EXISTS)continue;return false;
        }
#else
        if(mkdirat(parent,leaf,0700)!=0){if(errno==EEXIST)continue;return false;}
#endif
        InvitationDirectory check;
        if(!check.Open(path.c_str()))return false;
        *out=std::move(path);return true;
    }
    return false;
}
} // namespace

extern "C" int CE_InvitationBeginImport(const char* origin,uint32_t expected){
    auto& s=State();PickerTicket* ticket=nullptr;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if(s.outstanding)return 0;
        if(s.generation==(std::numeric_limits<uint64_t>::max)()){Refuse(s,CE_INVITATION_ERROR_UNAVAILABLE);return 0;}
        ++s.generation;ClearPreview(s);s.chosen.fill(0);s.picked=0;s.view={};
        if(expected!=CE_NET_HOST&&expected!=CE_NET_GUEST){Refuse(s,CE_INVITATION_ERROR_ROLE);return 0;}
        size_t size=0;
        if(!Bounded(origin,CE_INVITATION_ORIGIN_BYTES,&size)||!CeSignalHttpsOriginValid(origin)){
            Refuse(s,CE_INVITATION_ERROR_ORIGIN);return 0;
        }
        ticket=new(std::nothrow) PickerTicket{s.generation};
        if(!ticket){Refuse(s,CE_INVITATION_ERROR_UNAVAILABLE);return 0;}
        s.outstanding=ticket;s.view.state=CE_INVITATION_CHOOSING;s.view.role=expected;
        std::memcpy(s.view.origin,origin,size+1);
    }
    if (!PortPrivateUiChooseFolder(Picked, ticket))
        SDL_ShowOpenFolderDialog(Picked,ticket,SDL_GetKeyboardFocus(),nullptr,false);
    return 1;
}
extern "C" void CE_InvitationPoll(uint64_t utc,CE_InvitationView* out){
    auto& s=State();std::lock_guard<std::mutex> lock(s.mutex);
    if(s.view.state==CE_INVITATION_CHOOSING&&s.picked){
        const unsigned picked=s.picked;s.picked=0;
        if(picked==2){ClearPreview(s);s.view.state=CE_INVITATION_CANCELLED;s.view.error=CE_INVITATION_ERROR_NONE;}
        else if(picked!=1)Refuse(s,CE_INVITATION_ERROR_FOLDER);
        else{
            auto directory=std::make_unique<InvitationDirectory>();ImportBytes bytes;
            if(!directory->Open(s.chosen.data()))Refuse(s,CE_INVITATION_ERROR_FOLDER);
            else if(directory->Read("pair-admission.json",bytes.value)!=1||!ParseMetadata(bytes.value.data(),bytes.value.size(),s.view.role,&s.pair))
                Refuse(s,CE_INVITATION_ERROR_METADATA);
            else if(!Current(utc,s.pair.expiresAt))Refuse(s,CE_INVITATION_ERROR_EXPIRED);
            else{
                s.metadataSize=bytes.value.size();std::memcpy(s.metadata.data(),bytes.value.data(),s.metadataSize);
                s.source=std::move(directory);s.previewUtc=utc;s.view.expires_at=s.pair.expiresAt;
                s.view.state=CE_INVITATION_PREVIEW_READY;s.view.error=CE_INVITATION_ERROR_NONE;
            }
        }
    }
    if(s.view.state==CE_INVITATION_PREVIEW_READY&&(utc<s.previewUtc||!Current(utc,s.pair.expiresAt)))Refuse(s,CE_INVITATION_ERROR_EXPIRED);
    if(out)*out=s.view;
}
extern "C" int CE_InvitationInstall(uint64_t utc){
    auto& s=State();std::lock_guard<std::mutex> lock(s.mutex);
    if(s.view.state!=CE_INVITATION_PREVIEW_READY||!s.source)return 0;
    if(CE_LobbySignalingSelected()){Refuse(s,CE_INVITATION_ERROR_SELECTION);return 0;}
    if(utc<s.previewUtc||!Current(utc,s.pair.expiresAt)){Refuse(s,CE_INVITATION_ERROR_EXPIRED);return 0;}
    InvitationDirectory current;ImportBytes metadata,capability;
    if(!current.Open(s.chosen.data())||!current.SameAs(*s.source)||current.Read("pair-admission.json",metadata.value)!=1||
       metadata.value.size()!=s.metadataSize||std::memcmp(metadata.value.data(),s.metadata.data(),s.metadataSize)){
        Refuse(s,CE_INVITATION_ERROR_CHANGED);return 0;
    }
    CeIceSignalPairBinding pair{};
    if(!ParseMetadata(metadata.value.data(),metadata.value.size(),s.view.role,&pair)||pair.expiresAt!=s.pair.expiresAt||std::strcmp(pair.approval,s.pair.approval)){
        Refuse(s,CE_INVITATION_ERROR_CHANGED);return 0;
    }
    if(current.Read("identity-token",capability.value)!=1||capability.value.empty()){
        Refuse(s,CE_INVITATION_ERROR_CAPABILITY);return 0;
    }
    for(const uint8_t c:capability.value)if(c<33||c>126){Refuse(s,CE_INVITATION_ERROR_CAPABILITY);return 0;}
    std::string exchange;
    if(!NewExchangeDirectory(&exchange)){Refuse(s,CE_INVITATION_ERROR_PRIVATE_DIRECTORY);return 0;}
    auto authority=CeBootSignalAuthority::Create(s.view.origin,reinterpret_cast<const char*>(capability.value.data()),capability.value.size(),pair,exchange.c_str());
    const bool selected=CE_LobbySignalingSelect(std::move(authority));
    if(!selected){Refuse(s,CE_INVITATION_ERROR_SELECTION);return 0;}
    ClearPreview(s);s.chosen.fill(0);s.view.state=CE_INVITATION_SELECTED;s.view.error=CE_INVITATION_ERROR_NONE;
    return 1;
}
extern "C" void CE_InvitationCancel(){
    PortPrivateUiCancelFolder();
    auto& s=State();std::lock_guard<std::mutex> lock(s.mutex);
    if(s.generation!=(std::numeric_limits<uint64_t>::max)())++s.generation;
    ClearPreview(s);s.chosen.fill(0);s.picked=0;s.view={};s.view.state=CE_INVITATION_CANCELLED;
}
#else
extern "C" int CE_InvitationBeginImport(const char*,uint32_t){return 0;}
extern "C" void CE_InvitationPoll(uint64_t,CE_InvitationView* out){if(out){*out={};out->state=CE_INVITATION_REFUSED;out->error=CE_INVITATION_ERROR_UNAVAILABLE;}}
extern "C" int CE_InvitationInstall(uint64_t){return 0;}
extern "C" void CE_InvitationCancel(){}
#endif

extern "C" const char* CE_InvitationErrorText(uint32_t error){
    switch(error){
    case CE_INVITATION_ERROR_NONE:return "";
    case CE_INVITATION_ERROR_UNAVAILABLE:return "Invitation import is unavailable in this build.";
    case CE_INVITATION_ERROR_ORIGIN:return "Enter a trusted HTTPS service origin.";
    case CE_INVITATION_ERROR_ROLE:return "Choose a Host or Join invitation.";
    case CE_INVITATION_ERROR_FOLDER:return "Choose the private role invitation folder.";
    case CE_INVITATION_ERROR_METADATA:return "The invitation metadata is invalid or has another role.";
    case CE_INVITATION_ERROR_EXPIRED:return "This invitation is expired or outside its session window.";
    case CE_INVITATION_ERROR_CHANGED:return "The selected invitation changed. Import it again.";
    case CE_INVITATION_ERROR_CAPABILITY:return "The private invitation capability could not be loaded.";
    case CE_INVITATION_ERROR_PRIVATE_DIRECTORY:return "A private session folder could not be created.";
    default:return "The invitation could not be selected. No service request was made.";
    }
}
#endif
