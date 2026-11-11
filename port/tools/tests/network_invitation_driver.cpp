// SPDX-License-Identifier: GPL-3.0-only
#include "ce_network_invitation.h"
#include "ce_network_boot_authority.h"
#include "../../network/ice_exchange.h"
#include "../../network/signaling_provider.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdarg>
namespace fs=std::filesystem;
namespace {
unsigned checks=0,failures=0,pickers=0,tokenOpens=0,selections=0,requests=0,nonceCount=0;
SDL_DialogFileCallback callback=nullptr;void* callbackData=nullptr;
std::string userRoot;
std::shared_ptr<CeBootSignalAuthority> selected;
std::shared_ptr<CeIceSignalProvider> provider;
bool selectAllowed=true,nativeSelected=false;
constexpr const char* origin="https://fixture.invalid";
constexpr const char* token="CEPAIR1.fixture-session-role";
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
struct FakeHttps final:CeSignalHttps {
    bool Begin(const char* route,const char* bearer,size_t size,const std::string&,uint64_t,uint64_t) noexcept override {
        ++requests;Check(std::strcmp(route,"pair/current")==0,"first-request-is-pair-preflight");
        Check(size==std::strlen(token)&&!std::memcmp(bearer,token,size),"preflight-uses-imported-role-capability");return true;
    }
    CeSignalHttpsState Poll(uint64_t,CeSignalHttpsReply*) noexcept override{return CeSignalHttpsState::Pending;}
    void Cancel() noexcept override{}
};
void Write(const fs::path& path,const std::string& data){
    std::ofstream out(path,std::ios::binary);out.write(data.data(),std::streamsize(data.size()));out.close();
    Check(chmod(path.c_str(),0600)==0,"fixture-private-file");
}
std::string Metadata(const char* role="host",uint64_t expires=1120){
    return "{\"schema\":1,\"approval\":\""+std::string(32,'a')+"\",\"role\":\""+role+"\",\"expiresAt\":"+std::to_string(expires)+"}\n";
}
fs::path Package(const fs::path& root,const char* name,const char* role="host",uint64_t expires=1120){
    const fs::path path=root/name;fs::create_directory(path);chmod(path.c_str(),0700);
    Write(path/"pair-admission.json",Metadata(role,expires));Write(path/"identity-token",token);return path;
}
std::string Read(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
CE_InvitationView View(uint64_t utc=1000){CE_InvitationView view{};CE_InvitationPoll(utc,&view);return view;}
void Complete(const fs::path& path,bool threaded=false){
    auto cb=callback;void* data=callbackData;callback=nullptr;callbackData=nullptr;
    Check(cb!=nullptr,"picker-has-owned-callback");if(!cb)return;
    const std::string value=path.string();const char* files[]={value.c_str(),nullptr};
    if(threaded){std::thread other([&]{cb(data,files,0);});other.join();}else cb(data,files,0);
}
void Begin(const fs::path& path,uint32_t role=CE_NET_HOST,bool threaded=false){
    Check(CE_InvitationBeginImport(origin,role)==1,"explicit-import-starts-picker");Complete(path,threaded);
}
size_t Sessions(){size_t n=0;for(const auto& item:fs::directory_iterator(userRoot))if(item.is_directory())++n;return n;}
void Reset(){CE_InvitationCancel();selected.reset();provider.reset();requests=0;selections=0;tokenOpens=0;selectAllowed=true;nativeSelected=false;}
}
SDL_Window* SDL_GetKeyboardFocus(){return nullptr;}
void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback cb,void* data,SDL_Window*,const char*,bool){++pickers;callback=cb;callbackData=data;}
const char* PortUserDir(){return userRoot.c_str();}
extern "C" int CE_NetGenerateSecret(uint8_t* out){++nonceCount;for(unsigned i=0;i<32;++i)out[i]=uint8_t(nonceCount+i);return 1;}
bool CE_LobbySignalingSelected() noexcept{return nativeSelected;}
bool CE_LobbySignalingSelect(std::shared_ptr<CeBootSignalAuthority> value) noexcept{
    ++selections;nativeSelected=true;selected=std::move(value);
    if(!selected||!selectAllowed)return false;
    provider=CeCreateSignalingProviderWithTransport(std::make_unique<FakeHttps>(),selected);
    return provider!=nullptr;
}
bool CeIceExchange::ContextForConfig(const CE_NetConfig* config,uint8_t context[32]) noexcept{
    if(!config||!context)return false;std::memset(context,1,32);return true;
}
extern "C" int __real_openat(int,const char*,int,...);
extern "C" int __wrap_openat(int fd,const char* name,int flags,...){
    if(name&&!std::strcmp(name,"identity-token"))++tokenOpens;
    if(flags&O_CREAT){va_list args;va_start(args,flags);const mode_t mode=mode_t(va_arg(args,int));va_end(args);return __real_openat(fd,name,flags,mode);}
    return __real_openat(fd,name,flags);
}
int main(int argc,char** argv){
    if(argc!=2)return 2;const fs::path root=argv[1];
    userRoot=(root/"app-owned").string();fs::create_directory(userRoot);chmod(userRoot.c_str(),0700);
    const auto host=Package(root,"host");const auto guest=Package(root,"guest","guest");
    const auto beforeMeta=Read(host/"pair-admission.json"),beforeToken=Read(host/"identity-token");
    Reset();const unsigned pickerBefore=pickers;
    Check(!CE_InvitationBeginImport("http://fixture.invalid",CE_NET_HOST)&&pickers==pickerBefore,"invalid-origin-before-picker");
    Check(!CE_InvitationBeginImport(origin,0)&&pickers==pickerBefore,"invalid-role-before-picker");
    Begin(host,CE_NET_HOST,true);
    Check(View().state==CE_INVITATION_PREVIEW_READY&&View().role==CE_NET_HOST&&View().expires_at==1120,"role-expiry-preview");
    Check(!std::strcmp(View().origin,origin),"visible-consented-recipient");
    Check(tokenOpens==0&&selections==0&&requests==0&&Sessions()==0,"preview-has-no-capability-selection-network-or-session-write");
    Check(CE_InvitationInstall(1000)==1&&View().state==CE_INVITATION_SELECTED,"explicit-install-selects-real-authority");
    Check(tokenOpens==1&&selections==1&&requests==0&&Sessions()==1,"install-has-bounded-read-and-zero-network");
    CeIceSignalPairBinding binding{};
    Check(selected&&selected->ExpectedPair(&binding)&&binding.role==CE_NET_HOST&&binding.expiresAt==1120,"typed-role-binding-reaches-provider");
    Check(selected&&std::string(selected->Directory()).find(userRoot+"/")==0&&std::string(selected->Directory())!=host.string(),"fresh-app-owned-directory-not-import-folder");
    struct stat info{};Check(selected&&stat(selected->Directory(),&info)==0&&(info.st_mode&0777)==0700,"fresh-session-directory-private");
    CeIceSignalRequest game{};game.stage=CeIceSignalStageKind::Game;game.role=CE_NET_HOST;
    Check(selected&&!selected->AllowsGame(game),"import-does-not-invent-authenticated-start");
    Check(Read(host/"pair-admission.json")==beforeMeta&&Read(host/"identity-token")==beforeToken&&std::distance(fs::directory_iterator(host),fs::directory_iterator())==2,"import-inputs-remain-read-only");
    const size_t installed=Sessions();
    Check(!CE_InvitationInstall(1000)&&Sessions()==installed&&selections==1,"install-is-one-shot");
    if(provider)provider->Preflight(1000,100);Check(requests==1,"explicit-continue-first-provider-request");
    const auto retained=selected;const unsigned readsBeforeReplacement=tokenOpens;
    Begin(guest,CE_NET_GUEST);Check(View().state==CE_INVITATION_PREVIEW_READY,"replacement-can-preview-without-changing-source");
    Check(!CE_InvitationInstall(1000)&&selected==retained&&tokenOpens==readsBeforeReplacement&&requests==1,"existing-selected-source-preserved-before-explicit-close");
    Reset();Begin(guest,CE_NET_GUEST);Check(View().state==CE_INVITATION_PREVIEW_READY&&CE_InvitationInstall(1000),"guest-import-is-reachable");
    Check(selected&&selected->ExpectedPair(&binding)&&binding.role==CE_NET_GUEST&&requests==0,"guest-role-authority-zero-network");
    Reset();Begin(guest);Check(View().state==CE_INVITATION_REFUSED&&tokenOpens==0&&selections==0,"wrong-role-refused-before-token");
    Reset();const auto missing=Package(root,"missing-token");fs::remove(missing/"identity-token");Begin(missing);
    Check(View().state==CE_INVITATION_PREVIEW_READY&&tokenOpens==0,"metadata-preview-does-not-need-token");
    Check(!CE_InvitationInstall(1000)&&View().error==CE_INVITATION_ERROR_CAPABILITY&&selections==0&&requests==0,"missing-token-refused-locally");
    Reset();const auto changed=Package(root,"changed");Begin(changed);Check(View().state==CE_INVITATION_PREVIEW_READY,"change-control-preview");
    Write(changed/"pair-admission.json",Metadata("host",1121));
    Check(!CE_InvitationInstall(1000)&&View().error==CE_INVITATION_ERROR_CHANGED&&tokenOpens==0&&selections==0,"changed-binding-refused-before-token");
    Reset();const auto expired=Package(root,"expired","host",1000);Begin(expired);Check(View().error==CE_INVITATION_ERROR_EXPIRED&&tokenOpens==0,"expired-preview-refused");
    Reset();Begin(host);Check(View().state==CE_INVITATION_PREVIEW_READY&&!CE_InvitationInstall(1120)&&View().error==CE_INVITATION_ERROR_EXPIRED&&tokenOpens==0,"expiry-rechecked-on-install");
    Reset();Begin(host);Check(View().state==CE_INVITATION_PREVIEW_READY&&!CE_InvitationInstall(999)&&tokenOpens==0,"clock-regression-refuses-install");
    Reset();const auto malformed=Package(root,"malformed");Write(malformed/"pair-admission.json","{\"schema\":1,\"schema\":1}");Begin(malformed);
    Check(View().error==CE_INVITATION_ERROR_METADATA&&tokenOpens==0,"duplicate-metadata-refused");
    Reset();const auto oversized=Package(root,"oversized");Write(oversized/"identity-token",std::string(4097,'q'));Begin(oversized);
    Check(View().state==CE_INVITATION_PREVIEW_READY&&!CE_InvitationInstall(1000)&&selections==0,"overlong-token-refused-before-select");
    Reset();const auto badToken=Package(root,"bad-token");Write(badToken/"identity-token","fixture\nheader");Begin(badToken);
    Check(View().state==CE_INVITATION_PREVIEW_READY&&!CE_InvitationInstall(1000)&&selections==0,"header-control-byte-refused");
    Reset();const auto tokenLink=Package(root,"token-link");fs::remove(tokenLink/"identity-token");fs::create_symlink(host/"identity-token",tokenLink/"identity-token");Begin(tokenLink);
    Check(View().state==CE_INVITATION_PREVIEW_READY&&!CE_InvitationInstall(1000)&&selections==0,"symlink-token-refused");
    Reset();const auto folderLink=root/"folder-link";fs::create_directory_symlink(host,folderLink);Begin(folderLink);
    Check(View().state==CE_INVITATION_REFUSED&&tokenOpens==0,"symlink-import-folder-refused");
    Reset();const auto privateMode=Package(root,"private-mode");chmod(privateMode.c_str(),0777);Begin(privateMode);
    Check(View().state==CE_INVITATION_REFUSED&&tokenOpens==0,"writable-invitation-folder-refused");
    Reset();Check(CE_InvitationBeginImport(origin,CE_NET_HOST),"cancel-starts-picker");CE_InvitationCancel();
    Check(!CE_InvitationBeginImport(origin,CE_NET_HOST),"at-most-one-outstanding-chooser");Complete(host,true);
    Check(View().state==CE_INVITATION_CANCELLED&&tokenOpens==0&&selections==0,"stale-cancelled-callback-has-no-effect");
    Reset();Check(CE_InvitationBeginImport(origin,CE_NET_HOST),"picker-cancel-start");
    auto cb=callback;void* data=callbackData;callback=nullptr;callbackData=nullptr;const char* empty[]={nullptr};cb(data,empty,0);
    Check(View().state==CE_INVITATION_CANCELLED&&tokenOpens==0,"picker-cancel-has-no-file-read");
    Reset();Begin(host);Check(View().state==CE_INVITATION_PREVIEW_READY,"selection-failure-preview");selectAllowed=false;
    Check(!CE_InvitationInstall(1000)&&View().error==CE_INVITATION_ERROR_SELECTION&&selections==1&&requests==0,"selection-refusal-never-contacts-service");
    for(unsigned i=0;i<=CE_INVITATION_ERROR_SELECTION;++i){const char* text=CE_InvitationErrorText(i);Check(text&&std::strstr(text,token)==nullptr&&std::strstr(text,userRoot.c_str())==nullptr,"fixed-errors-contain-no-private-input");}
    CE_InvitationCancel();provider.reset();selected.reset();
    std::printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
