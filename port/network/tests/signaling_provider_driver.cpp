// SPDX-License-Identifier: GPL-3.0-only
// Authored admissions only. No sockets, provider calls or actual credentials.
#include "../signaling_provider.h"
#include "../ice_credentials.h"
#include "../signaling_https.cpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>
extern "C" {
#include "../../updater/sha256.h"
}
namespace {
int checks=0;
void Check(bool yes,const char* name){++checks;if(!yes){std::cerr<<"FAIL: "<<name<<'\n';std::exit(1);}}
struct Authority final:CeIceSignalAuthority {
    CE_NetRole role=CE_NET_HOST;bool allow=false;CeIceSignalRequest game{};
    bool ExpectedPair(CeIceSignalPairBinding* out)const noexcept override{if(!out)return false;std::memset(out->approval,'a',32);out->approval[32]=0;out->role=role;out->expiresAt=6000;return true;}
    bool AllowsGame(const CeIceSignalRequest& r)const noexcept override{return allow&&r.stage==CeIceSignalStageKind::Game&&r.role==game.role&&!std::memcmp(r.context,game.context,32)&&r.requestedAt==game.requestedAt&&r.requestExpiresAt==game.requestExpiresAt&&r.grantExpiresAt==game.grantExpiresAt&&r.setupDeadlineMs==game.setupDeadlineMs&&r.directory&&game.directory&&!std::strcmp(r.directory,game.directory);}
private:
    bool CopyBearer(char* p,size_t cap,size_t* size)const noexcept override{const char value[]="authored.pilot.token";if(!p||!size||cap<sizeof(value)-1)return false;std::memcpy(p,value,sizeof(value)-1);*size=sizeof(value)-1;return true;}
};
struct Step{std::string route,body,contains;unsigned code=200;};
struct Fake final:CeSignalHttps {
    std::vector<Step> steps;size_t next=0;bool active=false,hold=false,broken=false;unsigned cancels=0;
    bool Begin(const char* route,const char* bearer,size_t n,const std::string& body,uint64_t now,uint64_t end)noexcept override{
        if(active||next>=steps.size()||!route||steps[next].route!=route||std::string(bearer,n)!="authored.pilot.token"||end<=now||body.size()>65536||(!steps[next].contains.empty()&&body.find(steps[next].contains)==std::string::npos)){broken=true;return false;}active=true;return true;
    }
    CeSignalHttpsState Poll(uint64_t,CeSignalHttpsReply* out)noexcept override{if(!active||!out)return CeSignalHttpsState::Failed;if(hold)return CeSignalHttpsState::Pending;out->body=steps[next].body;out->code=steps[next].code;++next;active=false;return CeSignalHttpsState::Ready;}
    void Cancel()noexcept override{if(active)++cancels;active=false;}
};
std::string Pair(const char* status,const char* role="host"){return std::string("{\"schema\":1,\"status\":\"")+status+"\",\"role\":\""+role+"\",\"approval\":\""+std::string(32,'a')+"\",\"pairExpiresAt\":6000}";}
std::string Grant(const char* role="host"){return std::string("{\"schema\":1,\"role\":\"")+role+"\",\"context\":\""+std::string(64,'1')+"\",\"issuedAt\":1004,\"expiresAt\":3000,\"iceServers\":[{\"urls\":[\"turn:turn.cloudflare.com:3478?transport=udp\"],\"username\":\"invented-user\",\"credential\":\"invented-password\"}]}";}
std::string StageReply(const char* status,const std::string& grant={},const char* role="host",const std::string& extra={}){auto s=Pair(status,role);s.pop_back();s+=",\"setupExpiresAt\":1118";if(!grant.empty())s+=",\"grantExpiresAt\":3000,\"grant\":"+grant;return s+extra+"}";}
void Tick(CeIceSignalProvider& p,uint64_t u,uint64_t m){p.Preflight(u,m);p.Preflight(u,m);}
void Tick(CeIceSignalStage& p,uint64_t u,uint64_t m){p.Poll(u,m);p.Poll(u,m);}
CeIceSignalRequest Request(){CeIceSignalRequest r{};r.role=CE_NET_HOST;std::memset(r.context,0x11,32);r.directory="authored-private-directory";r.requestedAt=1002;r.requestExpiresAt=1120;r.grantExpiresAt=4000;r.setupDeadlineMs=130000;return r;}
void OriginPreview(){
    for(const char* origin:{"https://example.invalid","https://EXAMPLE.invalid/","https://example.invalid:443","https://example.invalid:65535/"})
        Check(CeSignalHttpsOriginValid(origin),"pure origin preview before platform initialization");
    for(const char* origin:{"http://example.invalid","HTTPS://example.invalid","https://user@example.invalid","https://example.invalid/path","https://example.invalid?token=x","https://example.invalid#fragment","https://bad_host.invalid","https://example..invalid","https://-example.invalid","https://example-.invalid","https://example.invalid.","https://example.invalid:0","https://example.invalid:65536","https://[::1]","https:///"})
        Check(!CeSignalHttpsOriginValid(origin),"pure origin preview shares strict factory refusal");
    Check(!CeSignalHttpsOriginValid(nullptr),"pure origin preview refuses missing input");
    const std::string oversized="https://"+std::string(2048,'a');
    Check(!CeSignalHttpsOriginValid(oversized.c_str()),"pure origin preview bounds input before allocation");
}
void HttpPolicy(){
    for(const char* origin:{"http://example.invalid","https://user@example.invalid","https://example.invalid/path","https://example.invalid?token=x","https://bad_host.invalid","https://example.invalid:0"})Check(!CeCreateSignalHttps(origin),"invalid origin refused without IO");
    Check(bool(CeCreateSignalHttps("https://example.invalid:443")),"explicit origin constructs without IO");
#ifndef _WIN32
    // Adding the handle configures the real pinned transport. No Poll/perform
    // is called, so no transfer or resolver starts in this offline check.
    auto configured=CeCreateSignalHttps("https://example.invalid");const char token[]="authored.pilot.token";
    Check(configured&&!configured->Begin("invented",token,sizeof(token)-1,"{}",1000,2000),"fixed HTTP route allowlist");
    Check(configured->Begin("pair/current",token,sizeof(token)-1,"{}",1000,2000),"pinned TLS/proxy/bounds options configure");
    Check(!configured->Begin("pair/current",token,sizeof(token)-1,"{}",1000,2000),"one real transport request at a time");
    configured->Cancel();Check(configured->Begin("pair/current",token,sizeof(token)-1,"{}",1000,2000),"cancelled unsent handle may configure again");configured->Cancel();
#endif
    auto valid=[](){Headers h;const char* first="HTTP/1.1 200 OK\r\n";Check(h.Line(first,std::strlen(first)),"status accepted");for(const char* line:{"Content-Type: application/json\r\n","Cache-Control: no-store\r\n","Pragma: no-cache\r\n","X-Content-Type-Options: nosniff\r\n"})Check(h.Line(line,std::strlen(line)),"required header accepted");return h;};
    auto line=[](Headers& h,const char* s){return h.Line(s,std::strlen(s));};
    auto h=valid();Check(line(h,"Content-Length: 2\r\n")&&line(h,"\r\n")&&h.Body(2)&&!h.Body(3),"body exact length");
    h=valid();Check(!line(h,"Content-Type: application/json\r\n"),"duplicate header refused");
    h=valid();Check(!line(h,"Location: https://example.invalid/\r\n"),"redirect refused");
    h=valid();Check(line(h,"Content-Encoding: gzip\r\n")&&!line(h,"\r\n"),"compressed body refused");
    h=valid();Check(line(h,"Content-Length: 65537\r\n")&&!line(h,"\r\n"),"oversized body refused");
    Headers missing;Check(line(missing,"HTTP/1.1 200 OK\r\n")&&!line(missing,"\r\n"),"missing response security headers refused");
}
std::string RecordExtra(){
    std::array<uint8_t,100> bytes{};for(size_t i=0;i<bytes.size();++i)bytes[i]=uint8_t(i+1);
    const char digits[]="0123456789abcdef",alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    SHA256_CTX c;uint8_t hash[32];sha256_init(&c);sha256_update(&c,bytes.data(),bytes.size());sha256_final(&c,hash);
    std::string digest,encoded;for(auto b:hash){digest+=digits[b>>4];digest+=digits[b&15];}
    for(size_t i=0;i<bytes.size();i+=3){uint32_t v=uint32_t(bytes[i])<<16;if(i+1<bytes.size())v|=uint32_t(bytes[i+1])<<8;if(i+2<bytes.size())v|=bytes[i+2];encoded+=alphabet[v>>18];encoded+=alphabet[(v>>12)&63];encoded+=i+1<bytes.size()?alphabet[(v>>6)&63]:'=';encoded+=i+2<bytes.size()?alphabet[v&63]:'=';}
    return ",\"record\":\""+encoded+"\",\"recordDigest\":\""+digest+"\"";
}
void HostFlow(){
    auto authority=std::make_shared<Authority>();auto fake=std::make_unique<Fake>();auto* f=fake.get();auto grant=Grant();
    f->steps={{"pair/current",Pair("awaiting_pair_consent"),{}},{"pair/accept",Pair("awaiting_pair_consent"),"\"approval\":"},{"pair/current",Pair("admitted"),{}},
        {"stage/request",StageReply("awaiting_peer_request"),"\"afterAuthenticatedStart\":false"},{"stage/poll",StageReply("exchanging",grant),{}},
        {"stage/record",StageReply("exchanging",grant),"\"record\":"},{"stage/poll",StageReply("exchanging",grant,"host",RecordExtra()),{}},
        {"stage/ack",StageReply("service_delivery_complete",grant),"\"digest\":"},{"pair/close",Pair("closed"),"\"approval\":"}};
    auto p=CeCreateSignalingProviderWithTransport(std::move(fake),authority);Check(bool(p)&&!p->Accept(),"construction does not admit or consent");
    Tick(*p,1000,10000);Check(p->View().status==CeIceSignalStatus::NeedsConsent&&f->next==1,"current selects exact pair without consent");
    p->Preflight(1001,11000);Check(f->next==1&&p->View().status==CeIceSignalStatus::NeedsConsent,"idle consent screen sends no background current calls");
    Check(!p->Prepare(Request()),"prepare before consent refused");Check(p->Accept()&&p->Accept(),"explicit same-pair acceptance idempotent");
    Tick(*p,1001,11000);Check(p->View().status==CeIceSignalStatus::AwaitingPairConsent,"own consent awaiting peer");
    Tick(*p,1002,12000);Check(p->View().status==CeIceSignalStatus::Admitted,"both consents admitted");
    auto r=Request();auto stage=p->Prepare(r);Check(bool(stage),"fixed lobby prepares once");
    Tick(*stage,1003,13000);Check(f->next==4,"exact stage request sent");
    Tick(*stage,1004,14000);char output[32768];size_t size=0;
    Check(stage->TakeOwnGrant(output,1,&size)==CeIceSignalRead::Failed&&!size,"short grant output retains event");
    Check(stage->TakeOwnGrant(output,sizeof(output),&size)==CeIceSignalRead::Ready&&std::string(output,size)==grant,"own grant raw bounded event");
    CeIceCredentials credentials;Check(credentials.Parse(output,size,CE_NET_HOST,r.context,1004),"native credential owner accepts delivered authored grant");
    Check(stage->TakeOwnGrant(output,sizeof(output),&size)==CeIceSignalRead::Pending,"grant delivered once");
    std::array<uint8_t,100> local{};local.fill(0x61);Check(stage->PublishLocal(local.data(),local.size())&&stage->PublishLocal(local.data(),local.size()),"local immutable retry queued");
    ++local[0];Check(!stage->PublishLocal(local.data(),local.size()),"local replacement refused");
    Tick(*stage,1005,15000);Tick(*stage,1006,16000);uint8_t peer[4194],digest[32];
    Check(stage->TakePeerRecord(peer,1,&size,digest)==CeIceSignalRead::Failed,"short peer copy retains event");
    Check(stage->TakePeerRecord(peer,sizeof(peer),&size,digest)==CeIceSignalRead::Ready&&size==100,"peer digest checked bounded event");
    uint8_t wrong[32]{};Check(!stage->AcknowledgePeer(wrong)&&stage->AcknowledgePeer(digest),"ack exact consumed event only");
    Tick(*stage,1007,17000);Check(stage->View().status==CeIceSignalStatus::ServiceDeliveryComplete,"service completion remains service-only status");
    Tick(*stage,1119,128000);Check(stage->View().status==CeIceSignalStatus::ServiceDeliveryComplete&&f->next==8,"finished delivery retires setup timer without another request");
    stage.reset();Check(f->next==8&&!p->Prepare(r),"local destruction does not close or remint lobby");
    auto game=r;game.stage=CeIceSignalStageKind::Game;game.context[0]=0x22;Check(!p->Prepare(game),"game without typed START denied");
    authority->allow=true;authority->game=game;auto altered=game;++altered.setupDeadlineMs;Check(!p->Prepare(altered),"typed START deadline change denied");
    auto gameStage=p->Prepare(game);Check(bool(gameStage),"exact typed game plan accepted");gameStage.reset();
    p->ClosePair();p->ClosePair();Check(f->next==9&&!f->broken&&p->View().status==CeIceSignalStatus::Closed,"explicit close once with original approval");
}
void GuestFlow(){
    auto a=std::make_shared<Authority>();a->role=CE_NET_GUEST;auto fake=std::make_unique<Fake>();auto* f=fake.get();auto grant=Grant("guest");
    f->steps={{"pair/current",Pair("admitted","guest"),{}},{"stage/request",StageReply("exchanging",grant,"guest",RecordExtra()),"\"role\":\"guest\""},{"stage/ack",StageReply("exchanging",grant,"guest"),{}}};
    auto p=CeCreateSignalingProviderWithTransport(std::move(fake),a);Tick(*p,1002,12000);auto r=Request();r.role=CE_NET_GUEST;auto stage=p->Prepare(r);Check(bool(stage),"restored consent pair prepares guest");
    Tick(*stage,1004,14000);char grantBytes[32768];uint8_t record[4194],digest[32];size_t n=0;
    Check(stage->TakeOwnGrant(grantBytes,sizeof(grantBytes),&n)==CeIceSignalRead::Pending,"guest grant waits for owner-authenticated offer ACK");
    Check(!stage->AcknowledgePeer(digest),"ack before peer consumption refused");
    Check(stage->TakePeerRecord(record,sizeof(record),&n,digest)==CeIceSignalRead::Ready&&stage->AcknowledgePeer(digest),"trusted CE owner controls post-import ACK seam");
    Check(stage->TakeOwnGrant(grantBytes,sizeof(grantBytes),&n)==CeIceSignalRead::Ready&&std::string(grantBytes,n)==grant,"guest grant consumable after owner ACK");
    CeIceCredentials credentials;Check(credentials.Parse(grantBytes,n,CE_NET_GUEST,r.context,1004),"native credential owner accepts guest delivered grant");
    Tick(*stage,1005,15000);Check(f->next==3&&!f->broken,"guest ACK uses existing pair");
}
void Refusals(){
    std::vector<std::string> bad={"{\"schema\":1,\"schema\":1,\"status\":\"admitted\"}","{\"schema\":true,\"status\":\"admitted\"}","{\"schema\":1,\"status\":\"invented\"}","{\"schema\":1,\"status\":\"admitted\",\"extra\":1}",std::string(65537,'x'),"{\"schema\":1,\"status\":\"admitted\",\"pairExpiresAt\":18446744073709551615}",Pair("admitted","guest")};
    auto swapped=Pair("admitted");swapped.replace(swapped.find(std::string(32,'a')),32,std::string(32,'b'));bad.push_back(swapped);
    for(const auto& reply:bad){auto fake=std::make_unique<Fake>();fake->steps={{"pair/current",reply,{}}};auto p=CeCreateSignalingProviderWithTransport(std::move(fake),std::make_shared<Authority>());Tick(*p,1000,10000);Check(p->View().status==CeIceSignalStatus::InvalidRequest&&!p->Accept()&&!p->Prepare(Request()),"malformed/rebound admission fails closed");}
    auto extra=RecordExtra();auto digestPos=extra.find("recordDigest\":\"")+15;extra[digestPos]=extra[digestPos]=='a'?'b':'a';
    auto invalidPeer=std::make_unique<Fake>();invalidPeer->steps={{"pair/current",Pair("admitted"),{}},{"stage/request",StageReply("exchanging",Grant(),"host",extra),{}}};
    auto invalid=CeCreateSignalingProviderWithTransport(std::move(invalidPeer),std::make_shared<Authority>());Tick(*invalid,1002,12000);auto invalidStage=invalid->Prepare(Request());Check(bool(invalidStage),"digest refusal fixture prepares");Tick(*invalidStage,1004,14000);Check(invalidStage->View().status==CeIceSignalStatus::InvalidRequest,"peer SHA-256 mismatch refused before owner delivery");
    for(const auto& mutation:std::vector<std::pair<std::string,std::string>>{{"\"role\":\"host\"","\"role\":\"guest\""},{std::string(64,'1'),std::string(64,'2')},{"\"issuedAt\":1004","\"issuedAt\":1001"},{"\"expiresAt\":3000","\"expiresAt\":3001"}}){
        auto grant=Grant();auto at=grant.find(mutation.first);Check(at!=std::string::npos,"binding mutation anchor");grant.replace(at,mutation.first.size(),mutation.second);
        auto transport=std::make_unique<Fake>();transport->steps={{"pair/current",Pair("admitted"),{}},{"stage/request",StageReply("exchanging",grant),{}}};auto provider=CeCreateSignalingProviderWithTransport(std::move(transport),std::make_shared<Authority>());Tick(*provider,1002,12000);auto s=provider->Prepare(Request());Check(bool(s),"grant binding refusal prepares");Tick(*s,1004,14000);Check(s->View().status==CeIceSignalStatus::InvalidRequest,"grant role/context/original lifetime binding refused");
    }
    auto fake=std::make_unique<Fake>();auto* f=fake.get();f->steps={{"pair/current",Pair("admitted"),{}},{"stage/request",StageReply("awaiting_peer_request"),{}}};
    auto p=CeCreateSignalingProviderWithTransport(std::move(fake),std::make_shared<Authority>());Tick(*p,1002,12000);auto stage=p->Prepare(Request());Check(bool(stage),"late fixture prepares");f->hold=true;stage->Poll(1003,13000);stage->Poll(1120,130000);
    Check(stage->View().status==CeIceSignalStatus::SetupExpired&&f->cancels==1,"late operation cancelled at fixed deadline");
    fake=std::make_unique<Fake>();fake->steps={{"pair/current",Pair("admitted"),{}}};p=CeCreateSignalingProviderWithTransport(std::move(fake),std::make_shared<Authority>());Tick(*p,1002,12000);p->Preflight(1001,12001);Check(p->View().status==CeIceSignalStatus::InvalidRequest,"UTC rollback refused");
}
}
// Prepare checks a request against the clock the provider read at its last pump, never a fresher one: a caller stamps
// its request from the readings it last pumped with (ice_game.cpp pumps the lobby's admitted pair with its button's
// readings). The pump of an admitted pair with no stage sends nothing, consents to nothing and moves no setup bound.
void PrepareClock(){
    auto admitted=[](uint64_t utc,uint64_t mono,Fake** wire=nullptr){
        auto fake=std::make_unique<Fake>();if(wire)*wire=fake.get();fake->steps={{"pair/current",Pair("admitted"),{}}};
        auto p=CeCreateSignalingProviderWithTransport(std::move(fake),std::make_shared<Authority>());Tick(*p,utc,mono);
        Check(p->View().status==CeIceSignalStatus::Admitted,"clock fixture admitted");return p;};
    auto lobby=[](uint64_t utc,uint64_t mono){auto r=Request();r.requestedAt=utc;r.requestExpiresAt=utc+120;r.grantExpiresAt=std::min<uint64_t>(utc+180,6000);r.setupDeadlineMs=mono+120000;return r;};
    Check(bool(admitted(1002,12000)->Prepare(lobby(1002,12000))),"request stamped at the last pump's readings prepares");
    Check(!admitted(1002,12000)->Prepare(lobby(1002,12001)),"request stamped 1 ms after the last pump refused");
    Check(!admitted(1002,12000)->Prepare(lobby(1002,12016)),"request stamped a 60 Hz frame after the last pump refused");
    Check(!admitted(1002,12000)->Prepare(lobby(1003,12000)),"request stamped in the next UTC second refused");
    for(const auto& button:std::vector<std::pair<uint64_t,uint64_t>>{{1002,12001},{1002,12016},{1003,12000},{1003,12001},{1007,17000}}){
        Fake* f=nullptr;auto p=admitted(1002,12000,&f);const size_t replies=f->next;
        p->Preflight(button.first,button.second);
        Check(f->next==replies&&!f->active&&!f->broken&&!p->PreflightPending()&&p->View().status==CeIceSignalStatus::Admitted,
              "pump of an admitted pair with the button's readings sends nothing");
        Check(bool(p->Prepare(lobby(button.first,button.second))),"pumping with the button's readings first prepares its request");
    }
    {auto p=admitted(1002,12000);p->Preflight(1003,13000);auto r=lobby(1003,13000);++r.setupDeadlineMs;Check(!p->Prepare(r),"pump moves no 120 s setup bound");}
    {auto p=admitted(1002,12000);p->Preflight(1003,13000);Check(!p->Prepare(lobby(1004,13000)),"request stamped after the pumped clock still refused");}
    {auto fake=std::make_unique<Fake>();auto* f=fake.get();f->steps={{"pair/current",Pair("awaiting_pair_consent"),{}}};
     auto p=CeCreateSignalingProviderWithTransport(std::move(fake),std::make_shared<Authority>());Tick(*p,1002,12000);p->Preflight(1003,13000);
     Check(f->next==1&&!f->active&&p->View().status==CeIceSignalStatus::NeedsConsent&&!p->Prepare(lobby(1003,13000)),"pump never consents for the caller");}
    {auto p=admitted(1002,12000);p->Preflight(6000,13000);Check(p->View().status==CeIceSignalStatus::Expired&&!p->Prepare(lobby(6000,13000)),"pump at the pair's end expires it");}
    {auto p=admitted(1002,12000);p->Preflight(1001,13000);Check(p->View().status==CeIceSignalStatus::InvalidRequest&&!p->Prepare(lobby(1001,13000)),"pump with a UTC rollback fails closed");}
}
void AsyncPreflightReadiness(){
    for(int outcome=0;outcome<3;++outcome){
        auto fake=std::unique_ptr<Fake>(new Fake);
        auto* f=fake.get();
        fake->steps.push_back({"pair/current",Pair(outcome==0?"admitted":outcome==1?"unavailable":"expired"),{},200});
        if(outcome==1)fake->steps[0].code=503;
        fake->hold=true;
        auto p=CeCreateSignalingProviderWithTransport(std::move(fake),std::make_shared<Authority>());
        Check(bool(p),"async provider constructed");
        Check(!p->PreflightPending(),"unstarted provider has no pending preflight");
        p->Preflight(1000,10000);
        Check(p->PreflightPending(),"actual provider reports in-flight Current");
        p->Preflight(1001,11000);
        Check(p->PreflightPending()&&f->next==0,"held Current stays pending without admission or a second operation");
        f->hold=false;
        Tick(*p,1001,12000);
        Check(!p->PreflightPending(),"completed or expired Current is no longer pending");
        const auto expected=outcome==0?CeIceSignalStatus::Admitted:outcome==1?CeIceSignalStatus::Unavailable:CeIceSignalStatus::Expired;
        Check(p->View().status==expected,outcome==0?"actual asynchronous Current settles ready":outcome==1?"actual asynchronous Current settles failed":"actual asynchronous Current settles expired");
    }
}

int main(){
    OriginPreview();
#ifndef _WIN32
    Check(!CeCreateSignalHttps("https://example.invalid"),"factory refuses lazy initialization");
#endif
    Check(CeInitializeSignalHttps()&&CeInitializeSignalHttps(),"explicit idempotent initialization");
    HttpPolicy();HostFlow();AsyncPreflightReadiness();PrepareClock();GuestFlow();Refusals();
#ifndef _WIN32
    auto transport=CeCreateSignalHttps("https://example.invalid");Check(bool(transport)&&!CeShutdownSignalHttps(),"shutdown refuses live transports");transport.reset();
#endif
    Check(CeShutdownSignalHttps(),"explicit shutdown without transports");std::cout<<"NATIVE_SIGNALING_PASS "<<checks<<'\n';
}
