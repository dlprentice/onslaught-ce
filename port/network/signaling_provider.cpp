// SPDX-License-Identifier: GPL-3.0-only
#if defined(CE_NETWORK_ICE)
#include "signaling_provider.h"
#include "monocypher.h"
extern "C" {
#include "../updater/sha256.h"
}
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

class CeIceSignalAuthorityAccess {
public:
    static bool Copy(const CeIceSignalAuthority& a,char* p,size_t cap,size_t* size) noexcept { return a.CopyBearer(p,cap,size); }
};
namespace {
using Status=CeIceSignalStatus;
using Read=CeIceSignalRead;
constexpr uint64_t kIntegerMax=uint64_t(INT64_MAX)-14400;
void Wipe(std::string& v) { if(!v.empty())crypto_wipe(&v[0],v.size());v.clear(); }
void Wipe(std::vector<uint8_t>& v) { if(!v.empty())crypto_wipe(v.data(),v.size());v.clear(); }
std::string Hex(const uint8_t* p,size_t n) {
    const char digits[]="0123456789abcdef";std::string s;
    for(size_t i=0;i<n;++i){s+=digits[p[i]>>4];s+=digits[p[i]&15];}return s;
}
bool Unhex(const std::string& s,uint8_t* p,size_t n) {
    if(s.size()!=n*2)return false;
    auto digit=[](char c)->int { if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1; };
    for(size_t i=0;i<n;++i){int a=digit(s[i*2]),b=digit(s[i*2+1]);if(a<0||b<0)return false;p[i]=uint8_t(a*16+b);}return true;
}
std::array<uint8_t,32> Digest(const void* p,size_t n) {
    SHA256_CTX c;std::array<uint8_t,32> out{};sha256_init(&c);sha256_update(&c,p,n);sha256_final(&c,out.data());crypto_wipe(&c,sizeof(c));return out;
}
const char kBase64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string Base64(const uint8_t* p,size_t n) {
    std::string s;for(size_t i=0;i<n;i+=3){uint32_t v=uint32_t(p[i])<<16;if(i+1<n)v|=uint32_t(p[i+1])<<8;if(i+2<n)v|=p[i+2];
        s+=kBase64[v>>18];s+=kBase64[(v>>12)&63];s+=i+1<n?kBase64[(v>>6)&63]:'=';s+=i+2<n?kBase64[v&63]:'=';}return s;
}
bool Unbase64(const std::string& s,std::vector<uint8_t>& p) {
    if(s.empty()||s.size()>5600||s.size()%4)return false;
    auto digit=[](char c)->int { const char* p=c?std::strchr(kBase64,c):nullptr;return p?int(p-kBase64):-1; };
    for(size_t i=0;i<s.size();i+=4){int a=digit(s[i]),b=digit(s[i+1]),c=s[i+2]=='='?0:digit(s[i+2]),d=s[i+3]=='='?0:digit(s[i+3]);
        if(a<0||b<0||c<0||d<0||(s[i+2]=='='&&s[i+3]!='=')||((s[i+2]=='='||s[i+3]=='=')&&i+4!=s.size()))return false;
        uint32_t v=uint32_t(a<<18|b<<12|c<<6|d);p.push_back(uint8_t(v>>16));
        if(s[i+2]!='=')p.push_back(uint8_t(v>>8));
        if(s[i+3]!='=')p.push_back(uint8_t(v));}
    return p.size()>=100&&p.size()<=CE_ICE_SIGNAL_RECORD_MAX&&Base64(p.data(),p.size())==s;
}
// This parser checks the service envelope and immutable grant bindings. The
// existing native credential parser validates the full grant/relay policy;
// the exchange owner remains the sole record authenticator.
struct Json {
    enum Kind { Object,Array,String,Integer,Boolean,Null } kind=Null;
    std::string text;uint64_t number=0;size_t start=0,end=0;
    std::vector<std::pair<std::string,Json>> object;std::vector<Json> array;
    Json()=default;Json(Json&&)=default;Json& operator=(Json&&)=default;
    Json(const Json&)=delete;Json& operator=(const Json&)=delete;
    ~Json(){Wipe(text);}
    const Json* Get(const char* name)const{for(const auto& p:object)if(p.first==name)return &p.second;return nullptr;}
};
class Parser {
    const std::string& in_;size_t pos_=0,tokens_=0;
    void Space(){while(pos_<in_.size()&&(in_[pos_]==' '||in_[pos_]=='\n'||in_[pos_]=='\r'||in_[pos_]=='\t'))++pos_;}
    bool String(std::string& s){
        if(pos_>=in_.size()||in_[pos_++]!='"')return false;
        while(pos_<in_.size()){
            unsigned char c=static_cast<unsigned char>(in_[pos_++]);if(c=='"')return true;if(c<32||c>126)return false;
            if(c=='\\'){
                if(pos_>=in_.size())return false;
                c=static_cast<unsigned char>(in_[pos_++]);
                if(c=='u'){uint8_t v[2];if(pos_+4>in_.size()||!Unhex(in_.substr(pos_,4),v,2)||v[0]||v[1]>127)return false;pos_+=4;c=v[1];}
                else if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c=='b')c='\b';else if(c=='f')c='\f';
                else if(c!='"'&&c!='\\'&&c!='/')return false;
            }
            s+=char(c);if(s.size()>32768)return false;
        }return false;
    }
    bool Value(Json& v,unsigned depth){
        Space();if(pos_>=in_.size()||depth>8||++tokens_>2048)return false;
        v.start=pos_;char c=in_[pos_];
        if(c=='"'){v.kind=Json::String;if(!String(v.text))return false;}
        else if(c=='{'||c=='['){
            v.kind=c=='{'?Json::Object:Json::Array;char end=c=='{'?'}':']';++pos_;Space();
            if(pos_<in_.size()&&in_[pos_]==end)++pos_;
            else for(size_t count=0;;++count){
                if(count>=32)return false;
                if(c=='{'){
                    std::string key;if(!String(key)||key.size()>64)return false;
                    for(const auto& p:v.object)if(p.first==key)return false;
                    Space();if(pos_>=in_.size()||in_[pos_++]!=':')return false;
                    Json child;if(!Value(child,depth+1))return false;v.object.emplace_back(std::move(key),std::move(child));
                }else{Json child;if(!Value(child,depth+1))return false;v.array.push_back(std::move(child));}
                Space();if(pos_>=in_.size())return false;char next=in_[pos_++];if(next==end)break;if(next!=',')return false;Space();
            }
        }else if(c>='0'&&c<='9'){
            v.kind=Json::Integer;size_t first=pos_;
            do{unsigned d=unsigned(in_[pos_++]-'0');if(v.number>(kIntegerMax-d)/10)return false;v.number=v.number*10+d;}while(pos_<in_.size()&&in_[pos_]>='0'&&in_[pos_]<='9');
            if(pos_-first>1&&c=='0')return false;
        }else{
            bool found=false;for(const char* lit:{"true","false","null"}){size_t n=std::strlen(lit);if(in_.compare(pos_,n,lit)==0){v.kind=lit[0]=='n'?Json::Null:Json::Boolean;pos_+=n;found=true;break;}}if(!found)return false;
        }v.end=pos_;return true;
    }
public:
    explicit Parser(const std::string& in):in_(in){}
    bool Parse(Json& v){if(in_.empty()||in_.size()>65536||!Value(v,0))return false;Space();return pos_==in_.size();}
};
struct Reply {
    Status status=Status::Unavailable;CE_NetRole role{};std::string approval,grant;uint64_t setup=0,expiry=0,pair=0;
    std::string grantRole,grantContext;uint64_t grantIssued=0,grantExpiry=0;
    std::vector<uint8_t> record;std::array<uint8_t,32> digest{};bool hasRole=false,hasSetup=false,hasExpiry=false,hasPair=false,hasDigest=false;
    ~Reply(){Wipe(grant);Wipe(record);}
};
struct SensitiveWireReply:CeSignalHttpsReply { ~SensitiveWireReply(){Wipe(body);} };
bool Decode(const CeSignalHttpsReply& wire,Reply& r){
    Json root;if(!Parser(wire.body).Parse(root)||root.kind!=Json::Object)return false;
    for(const auto& field:root.object){bool known=false;for(const char* name:{"schema","status","role","setupExpiresAt","grantExpiresAt","pairExpiresAt","approval","grant","record","recordDigest"})if(field.first==name)known=true;if(!known)return false;}
    const auto* schema=root.Get("schema");const auto* status=root.Get("status");
    if(!schema||schema->kind!=Json::Integer||schema->number!=1||!status||status->kind!=Json::String)return false;
    static const std::pair<const char*,Status> statuses[]={
        {"authentication_required",Status::AuthenticationRequired},{"not_admitted",Status::NotAdmitted},{"invalid_request",Status::InvalidRequest},
        {"rate_limited",Status::RateLimited},{"awaiting_pair_consent",Status::AwaitingPairConsent},{"admitted",Status::Admitted},
        {"awaiting_peer_request",Status::AwaitingPeerRequest},{"exchanging",Status::Exchanging},{"service_delivery_complete",Status::ServiceDeliveryComplete},
        {"issuing",Status::Issuing},{"issuance_unknown",Status::IssuanceUnknown},{"setup_expired",Status::SetupExpired},
        {"grant_expired",Status::GrantExpired},{"closed",Status::Closed},{"expired",Status::Expired},{"unavailable",Status::Unavailable}};
    bool found=false;for(const auto& item:statuses)if(status->text==item.first){r.status=item.second;found=true;break;}if(!found)return false;
    unsigned code=r.status==Status::AuthenticationRequired?401:r.status==Status::NotAdmitted?403:r.status==Status::InvalidRequest?400:r.status==Status::RateLimited?429:r.status==Status::Unavailable?503:200;
    if(code!=wire.code)return false;
    if(const auto* role=root.Get("role")){if(role->kind!=Json::String||(role->text!="host"&&role->text!="guest"))return false;r.role=role->text=="host"?CE_NET_HOST:CE_NET_GUEST;r.hasRole=true;}
    auto number=[&](const char* name,uint64_t& value,bool& present){if(const auto* item=root.Get(name)){if(item->kind!=Json::Integer)return false;value=item->number;present=true;}return true;};
    if(!number("setupExpiresAt",r.setup,r.hasSetup)||!number("grantExpiresAt",r.expiry,r.hasExpiry)||!number("pairExpiresAt",r.pair,r.hasPair))return false;
    if(const auto* a=root.Get("approval")){uint8_t bytes[16];if(a->kind!=Json::String||!Unhex(a->text,bytes,16))return false;r.approval=a->text;}
    if(const auto* d=root.Get("recordDigest")){if(d->kind!=Json::String||!Unhex(d->text,r.digest.data(),32))return false;r.hasDigest=true;}
    if(const auto* g=root.Get("grant")){
        if(g->kind!=Json::Object||g->end-g->start>CE_ICE_SIGNAL_GRANT_MAX)return false;
        const auto* gs=g->Get("schema");const auto* gr=g->Get("role");const auto* gc=g->Get("context");const auto* gi=g->Get("issuedAt");const auto* ge=g->Get("expiresAt");uint8_t binding[32];
        if(!gs||gs->kind!=Json::Integer||gs->number!=1||!gr||gr->kind!=Json::String||!gc||gc->kind!=Json::String||!Unhex(gc->text,binding,32)||!gi||gi->kind!=Json::Integer||!ge||ge->kind!=Json::Integer)return false;
        r.grantRole=gr->text;r.grantContext=gc->text;r.grantIssued=gi->number;r.grantExpiry=ge->number;r.grant=wire.body.substr(g->start,g->end-g->start);
    }
    if(const auto* record=root.Get("record")){if(record->kind!=Json::String||!Unbase64(record->text,r.record)||!r.hasDigest||Digest(r.record.data(),r.record.size())!=r.digest)return false;}
    if((!r.grant.empty()||!r.record.empty())&&((r.status!=Status::Exchanging&&r.status!=Status::ServiceDeliveryComplete)||!r.hasRole||!r.hasSetup||!r.hasExpiry))return false;
    return true;
}
bool Fatal(Status s){return s==Status::AuthenticationRequired||s==Status::NotAdmitted||s==Status::InvalidRequest||s==Status::Unavailable||s==Status::IssuanceUnknown||s==Status::SetupExpired||s==Status::GrantExpired||s==Status::Closed||s==Status::Expired;}
struct StageData {
    CeIceSignalRequest request{};std::string directory;CeIceSignalView view{};std::string grant;
    std::vector<uint8_t> peer,local;std::array<uint8_t,32> grantHash{},peerHash{},localHash{};
    bool cancelled=false,requested=false,hasGrant=false,grantTaken=false,hasPeer=false,peerTaken=false,ackQueued=false,peerAcked=false,hasLocal=false,localSent=false;
    uint64_t lastPollUtc=0;
    ~StageData(){Clear();}void Clear(){Wipe(grant);Wipe(peer);Wipe(local);}
};
enum class Op { None,Current,Accept,Close,Request,Poll,Record,Ack };
struct Core {
    std::unique_ptr<CeSignalHttps> https;std::shared_ptr<const CeIceSignalAuthority> authority;
    CeIceSignalPairBinding pair{};CeIceSignalView view{};std::shared_ptr<StageData> stage,operationStage;
    Op operation=Op::None;bool bound=false,accepted=false,wantsAccept=false,wantsClose=false,closed=false,fatal=false,haveClock=false,lobbyUsed=false,gameUsed=false;
    std::array<uint8_t,32> lobbyContext{};uint64_t utc=0,mono=0,nextStart=0;
    ~Core(){https->Cancel();}
    void Fail(Status s){fatal=true;view.status=s;https->Cancel();operation=Op::None;operationStage.reset();if(stage){stage->view.status=s;stage->Clear();}}
    bool Clock(uint64_t u,uint64_t m){
        if(u>kIntegerMax||(haveClock&&(u<utc||m<mono))){Fail(Status::InvalidRequest);return false;}
        haveClock=true;utc=u;mono=m;if(!closed&&utc>=pair.expiresAt){Fail(Status::Expired);return false;}return true;
    }
    bool Live(const std::shared_ptr<StageData>& s){
        if(!s||s->cancelled)return false;
        if(s->view.grantExpiresAt&&utc>=s->view.grantExpiresAt){Fail(Status::GrantExpired);return false;}
        const bool delivered=s->view.status==Status::ServiceDeliveryComplete&&s->grantTaken&&s->peerAcked&&s->localSent;
        if(!delivered&&(mono>=s->request.setupDeadlineMs||utc>=s->request.requestExpiresAt||(s->view.setupExpiresAt&&utc>=s->view.setupExpiresAt))){Fail(Status::SetupExpired);return false;}
        return !fatal&&!closed;
    }
    std::string Prefix()const{return std::string("{\"approval\":\"")+pair.approval+"\"";}
    bool Start(Op op,const std::shared_ptr<StageData>& s={}){
        std::string body,route;
        if(op==Op::Current){route="pair/current";body="{}";}
        else if(op==Op::Accept||op==Op::Close){route=op==Op::Accept?"pair/accept":"pair/close";body=Prefix()+"}";}
        else{
            if(!Live(s))return false;
            const char* name=s->request.stage==CeIceSignalStageKind::Lobby?"lobby":"game";body=Prefix()+",\"stage\":\""+name+"\"";
            if(op==Op::Request){
                route="stage/request";const auto& r=s->request;
                body+=",\"afterAuthenticatedStart\":";body+=r.stage==CeIceSignalStageKind::Game?"true":"false";
                body+=",\"request\":{\"schema\":1,\"role\":\"";body+=r.role==CE_NET_HOST?"host":"guest";
                body+="\",\"context\":\""+Hex(r.context,32)+"\",\"requestedAt\":"+std::to_string(r.requestedAt)+",\"requestExpiresAt\":"+std::to_string(r.requestExpiresAt)+",\"grantExpiresAt\":"+std::to_string(r.grantExpiresAt)+"}";
            }else if(op==Op::Poll)route="stage/poll";
            else if(op==Op::Record){route="stage/record";body+=",\"record\":\""+Base64(s->local.data(),s->local.size())+"\"";}
            else{route="stage/ack";body+=",\"digest\":\""+Hex(s->peerHash.data(),32)+"\"";}
            body+="}";
        }
        std::array<char,4096> bearer{};size_t size=0;const bool copied=CeIceSignalAuthorityAccess::Copy(*authority,bearer.data(),bearer.size(),&size);
        const uint64_t timeout=std::min<uint64_t>(10000,UINT64_MAX-mono),deadline=s?std::min(s->request.setupDeadlineMs,mono+timeout):mono+timeout;
        const bool ok=copied&&size&&size<=bearer.size()&&deadline>mono&&https->Begin(route.c_str(),bearer.data(),size,body,mono,deadline);
        crypto_wipe(bearer.data(),bearer.size());Wipe(body);if(!ok){Fail(Status::Unavailable);return false;}
        operation=op;operationStage=s;nextStart=mono>UINT64_MAX-1000?UINT64_MAX:mono+1000;if(op==Op::Poll)s->lastPollUtc=utc;return true;
    }
    bool Apply(const Reply& r,Op op,const std::shared_ptr<StageData>& s){
        if((r.hasRole&&r.role!=pair.role)||(!r.approval.empty()&&r.approval!=pair.approval)||(r.hasPair&&r.pair!=pair.expiresAt))return false;
        if(op==Op::Current||op==Op::Accept){
            if(!r.grant.empty()||!r.record.empty()||r.hasDigest)return false;
            if(r.status!=Status::Admitted&&r.status!=Status::AwaitingPairConsent&&r.status!=Status::RateLimited&&!Fatal(r.status))return false;
            if(r.status==Status::Admitted||r.status==Status::AwaitingPairConsent){
                if(!r.hasRole||r.approval!=pair.approval||!r.hasPair||utc>=r.pair)return false;
                bound=true;view.status=r.status==Status::AwaitingPairConsent&&!accepted?Status::NeedsConsent:r.status;
                if(op==Op::Accept){accepted=true;wantsAccept=false;view.status=r.status;}
            }else view.status=r.status;
            if(Fatal(r.status))Fail(r.status);
            return true;
        }
        if(op==Op::Close){if(r.status!=Status::Closed&&r.status!=Status::Expired&&r.status!=Status::AuthenticationRequired&&r.status!=Status::NotAdmitted)return false;view.status=Status::Closed;return true;}
        if(!s||s!=stage||s->cancelled)return true;
        if(!Live(s))return true;
        if(Fatal(r.status)){Fail(r.status);return true;}
        if(r.status==Status::Admitted||r.status==Status::AwaitingPairConsent)return false;
        if(r.hasSetup){if(r.setup>s->request.requestExpiresAt||r.setup<=utc||(s->view.setupExpiresAt&&r.setup>s->view.setupExpiresAt))return false;s->view.setupExpiresAt=r.setup;}
        if(r.hasExpiry){if(r.expiry<=utc||r.expiry>s->request.grantExpiresAt||r.expiry>pair.expiresAt||(s->view.grantExpiresAt&&r.expiry!=s->view.grantExpiresAt))return false;s->view.grantExpiresAt=r.expiry;}
        if(!r.grant.empty()){
            if(r.grantRole!=(s->request.role==CE_NET_HOST?"host":"guest")||r.grantContext!=Hex(s->request.context,32)||r.grantIssued<s->request.requestedAt||r.grantIssued>utc||r.grantExpiry!=r.expiry||r.grantExpiry<=r.grantIssued||r.grantExpiry-r.grantIssued>14400)return false;
            auto hash=Digest(r.grant.data(),r.grant.size());if(s->hasGrant&&hash!=s->grantHash)return false;if(!s->hasGrant){s->grant=r.grant;s->grantHash=hash;s->hasGrant=true;}
        }
        if(!r.record.empty()){if(s->hasPeer&&r.digest!=s->peerHash)return false;if(!s->hasPeer){s->peer=r.record;s->peerHash=r.digest;s->hasPeer=true;}}
        else if(r.hasDigest&&s->hasPeer&&r.digest!=s->peerHash)return false;
        if(op==Op::Request&&!Fatal(r.status)&&r.status!=Status::RateLimited)s->requested=true;
        if(op==Op::Record&&!Fatal(r.status)&&r.status!=Status::RateLimited){s->localSent=true;Wipe(s->local);}
        if(op==Op::Ack&&!Fatal(r.status)&&r.status!=Status::RateLimited)s->peerAcked=true;
        s->view.status=r.status;view=s->view;if(Fatal(r.status))Fail(r.status);return true;
    }
    void Drive(uint64_t u,uint64_t m)noexcept{
        try{
            if(!Clock(u,m))return;
            if(wantsClose){
                if(operation!=Op::None&&operation!=Op::Close){https->Cancel();operation=Op::None;operationStage.reset();}
                if(operation==Op::None){wantsClose=false;Start(Op::Close);}
            }
            if(operation!=Op::None){
                SensitiveWireReply wire;auto state=https->Poll(mono,&wire);
                if(state==CeSignalHttpsState::Failed){Wipe(wire.body);Fail(Status::Unavailable);return;}
                if(state==CeSignalHttpsState::Ready){const auto op=operation;auto s=operationStage;operation=Op::None;operationStage.reset();Reply reply;bool decoded=Decode(wire,reply);Wipe(wire.body);if(!decoded||!Apply(reply,op,s)){Fail(Status::InvalidRequest);return;}}
            }
            if(closed||fatal)return;
            if(stage&&!Live(stage))return;
            if(operation!=Op::None||mono<nextStart)return;
            if(!bound){Start(Op::Current);return;}
            if(wantsAccept){Start(Op::Accept);return;}
            // The local consent gate needs no background traffic while idle.
            if(!stage){if(view.status!=Status::Admitted&&view.status!=Status::NeedsConsent)Start(Op::Current);return;}
            auto s=stage;
            if(!s->requested){Start(Op::Request,s);return;}
            if(s->ackQueued&&!s->peerAcked){Start(Op::Ack,s);return;}
            if(s->hasLocal&&!s->localSent){Start(Op::Record,s);return;}
            if(s->view.status==Status::ServiceDeliveryComplete&&s->grantTaken&&s->peerAcked&&s->localSent)return;
            if(utc>s->lastPollUtc)Start(Op::Poll,s);
        }catch(...){Fail(Status::Unavailable);}
    }
    bool Accept()noexcept{
        if(!bound||closed||fatal)return false;
        if(view.status==Status::Admitted||accepted)return true;
        if(view.status!=Status::NeedsConsent&&view.status!=Status::AwaitingPairConsent)return false;
        wantsAccept=true;return true;
    }
    void Cancel(const std::shared_ptr<StageData>& s)noexcept{
        if(!s||s->cancelled)return;
        s->cancelled=true;s->Clear();
        if(operationStage==s){https->Cancel();operation=Op::None;operationStage.reset();}
        if(stage==s){stage.reset();view.status=fatal?view.status:Status::Admitted;view.setupExpiresAt=0;view.grantExpiresAt=0;}
    }
};
class Stage final:public CeIceSignalStage {
    std::shared_ptr<Core> core_;std::shared_ptr<StageData> data_;
public:
    Stage(std::shared_ptr<Core> c,std::shared_ptr<StageData> d):core_(std::move(c)),data_(std::move(d)){}
    ~Stage()override{CancelLocal();}
    bool Accept()noexcept override{return core_->Accept();}
    void Poll(uint64_t u,uint64_t m)noexcept override{if(!data_->cancelled)core_->Drive(u,m);}
    bool PublishLocal(const uint8_t* bytes,size_t size)noexcept override{
        try{if(!core_->Live(data_)||!bytes||size<100||size>CE_ICE_SIGNAL_RECORD_MAX)return false;
            auto hash=Digest(bytes,size);if(data_->hasLocal)return hash==data_->localHash;
            data_->local.assign(bytes,bytes+size);data_->localHash=hash;data_->hasLocal=true;return true;
        }catch(...){core_->Fail(Status::Unavailable);return false;}
    }
    Read TakeOwnGrant(char* bytes,size_t cap,size_t* size)noexcept override{
        if(size)*size=0;
        if(!core_->Live(data_))return Read::Failed;
        if(!data_->hasGrant||data_->grantTaken||(data_->request.role==CE_NET_GUEST&&!data_->ackQueued))return Read::Pending;
        if(!bytes||!size||cap<data_->grant.size())return Read::Failed;
        std::memcpy(bytes,data_->grant.data(),data_->grant.size());*size=data_->grant.size();data_->grantTaken=true;Wipe(data_->grant);return Read::Ready;
    }
    Read TakePeerRecord(uint8_t* bytes,size_t cap,size_t* size,uint8_t digest[32])noexcept override{
        if(size)*size=0;
        if(!core_->Live(data_))return Read::Failed;
        if(!data_->hasPeer||data_->peerTaken)return Read::Pending;
        if(!bytes||!size||!digest||cap<data_->peer.size())return Read::Failed;
        std::memcpy(bytes,data_->peer.data(),data_->peer.size());*size=data_->peer.size();std::memcpy(digest,data_->peerHash.data(),32);data_->peerTaken=true;Wipe(data_->peer);return Read::Ready;
    }
    bool AcknowledgePeer(const uint8_t digest[32])noexcept override{
        if(!core_->Live(data_)||!digest||!data_->peerTaken||crypto_verify32(digest,data_->peerHash.data()))return false;
        data_->ackQueued=true;return true;
    }
    CeIceSignalView View()const noexcept override{return data_->view;}
    void CancelLocal()noexcept override{core_->Cancel(data_);}
};
class Provider final:public CeIceSignalProvider {
    std::shared_ptr<Core> core_;
public:
    explicit Provider(std::shared_ptr<Core> c):core_(std::move(c)){}
    void Preflight(uint64_t u,uint64_t m)noexcept override{core_->Drive(u,m);}
    CeIceSignalView View()const noexcept override{return core_->stage?core_->stage->view:core_->view;}
    bool PreflightPending()const noexcept override{return core_->operation==Op::Current;}
    bool Accept()noexcept override{return core_->Accept();}
    std::unique_ptr<CeIceSignalStage> Prepare(const CeIceSignalRequest& r)noexcept override{
        try{
            auto& c=*core_;if(!c.haveClock||!c.bound||c.fatal||c.closed||c.stage||c.operation!=Op::None||c.view.status!=Status::Admitted)return nullptr;
            if(r.role!=c.pair.role||!r.directory||!*r.directory||r.requestedAt>c.utc||c.utc>=r.requestExpiresAt||r.requestExpiresAt>r.grantExpiresAt||r.grantExpiresAt>c.pair.expiresAt||r.grantExpiresAt>kIntegerMax||r.requestExpiresAt-r.requestedAt>120||r.grantExpiresAt-r.requestedAt>14400||r.setupDeadlineMs<=c.mono||r.setupDeadlineMs-c.mono>120000)return nullptr;
            uint8_t any=0;for(auto byte:r.context)any|=byte;if(!any)return nullptr;
            if(r.stage==CeIceSignalStageKind::Lobby){if(c.lobbyUsed)return nullptr;}
            else if(r.stage!=CeIceSignalStageKind::Game||c.gameUsed||!c.authority->AllowsGame(r)||(c.lobbyUsed&&!crypto_verify32(r.context,c.lobbyContext.data())))return nullptr;
            size_t directorySize=0;while(directorySize<=4096&&r.directory[directorySize])++directorySize;
            if(directorySize>4096)return nullptr;
            auto s=std::make_shared<StageData>();s->directory.assign(r.directory,directorySize);s->request=r;s->request.directory=s->directory.c_str();s->view.status=Status::AwaitingPeerRequest;s->view.role=r.role;s->view.pairExpiresAt=c.pair.expiresAt;
            auto stage=std::make_unique<Stage>(core_,s);c.stage=s;
            if(r.stage==CeIceSignalStageKind::Lobby){c.lobbyUsed=true;std::memcpy(c.lobbyContext.data(),r.context,32);}else c.gameUsed=true;
            return stage;
        }catch(...){core_->Fail(Status::Unavailable);return nullptr;}
    }
    void ClosePair()noexcept override{
        auto& c=*core_;if(c.closed)return;
        if(c.stage)c.Cancel(c.stage);
        c.https->Cancel();c.operation=Op::None;c.operationStage.reset();c.closed=true;c.wantsClose=true;c.view.status=Status::Closed;
        if(c.haveClock)c.Drive(c.utc,c.mono);
    }
};
}
std::shared_ptr<CeIceSignalProvider> CeCreateSignalingProviderWithTransport(std::unique_ptr<CeSignalHttps> transport,std::shared_ptr<const CeIceSignalAuthority> authority)noexcept{
    try{
        CeIceSignalPairBinding pair{};uint8_t approval[16];
        if(!transport||!authority||!authority->ExpectedPair(&pair)||pair.approval[32]||!Unhex(std::string(pair.approval,32),approval,16)||(pair.role!=CE_NET_HOST&&pair.role!=CE_NET_GUEST)||!pair.expiresAt||pair.expiresAt>kIntegerMax)return nullptr;
        auto core=std::make_shared<Core>();core->https=std::move(transport);core->authority=std::move(authority);core->pair=pair;core->view.role=pair.role;core->view.pairExpiresAt=pair.expiresAt;return std::make_shared<Provider>(std::move(core));
    }catch(...){return nullptr;}
}
std::shared_ptr<CeIceSignalProvider> CeCreateSignalingProvider(const char* origin,std::shared_ptr<const CeIceSignalAuthority> authority)noexcept{
    return CeCreateSignalingProviderWithTransport(CeCreateSignalHttps(origin),std::move(authority));
}
#endif
