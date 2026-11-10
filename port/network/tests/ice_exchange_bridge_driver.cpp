// SPDX-License-Identifier: GPL-3.0-only
// Authored records and deterministic test randomness; no sockets or provider.
#include "../ice_exchange.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace { std::array<uint8_t,32> next_attempt{}; bool use_attempt=false; }
extern "C" int CE_NetGenerateSecret(uint8_t secret[CE_NET_SECRET_BYTES]) {
    static unsigned counter=0;
    ++counter;
    if (use_attempt) { std::memcpy(secret,next_attempt.data(),32); use_attempt=false; }
    else for (size_t i=0;i<32;++i) secret[i]=uint8_t(counter+i*7);
    return 1;
}
namespace {
using R=CeIceExchangeResult;
int checks=0;
void Check(bool yes,const char* name) {
    ++checks; if (!yes) { std::cerr<<"FAIL: "<<name<<'\n'; std::exit(1); }
}
std::string Dir(const std::string& root,const std::string& name) {
    std::string path=root+"/"+name;
#ifdef _WIN32
    Check(CreateDirectoryA(path.c_str(),nullptr)!=0,"fixture directory created");
#else
    Check(mkdir(path.c_str(),0700)==0,"fixture directory created");
#endif
    return path;
}
CE_NetConfig Config(CE_NetRole role) {
    CE_NetConfig c{}; c.role=role;
    for (size_t i=0;i<32;++i) { c.secret[i]=uint8_t(i+1); c.compatibility[i]=uint8_t(i+41); }
    return c;
}
std::string File(const std::string& root,const CeIceExchange& exchange,const char* suffix) {
    const char digits[]="0123456789abcdef";
    std::string name=root+"/ce-ice-";
    for (size_t i=0;i<32;++i) { name+=digits[exchange.Context()[i]>>4]; name+=digits[exchange.Context()[i]&15]; }
    return name+suffix;
}
std::vector<uint8_t> Read(const std::string& path) {
    std::ifstream f(path,std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
}
void Write(const std::string& path,const std::vector<uint8_t>& data) {
    std::ofstream f(path,std::ios::binary); f.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    Check(f.good(),"authored fixture written");
}
std::vector<uint8_t> Export(CeIceExchange& exchange,uint64_t now=1000) {
    std::vector<uint8_t> record(CeIceExchange::MaxRecordBytes); size_t size=0;
    Check(exchange.ExportLocalRecord(record.data(),record.size(),&size,now)==R::Ready,"export ready");
    record.resize(size); return record;
}
const char host_sdp[]="a=ice-ufrag:bridge-host\r\na=ice-pwd:authored-host\r\n";
const char guest_sdp[]="a=ice-ufrag:bridge-guest\r\na=ice-pwd:authored-guest\r\n";
R Poll(CeIceExchange& exchange,std::string& text,uint64_t now=1000) {
    char buffer[4096]{}; size_t size=0;
    auto result=exchange.PollRemote(buffer,sizeof(buffer),&size,now);
    text.assign(buffer,size); return result;
}
void Bridge(const std::string& root) {
    auto hc=Config(CE_NET_HOST),gc=Config(CE_NET_GUEST);
    std::array<uint8_t,32> pure{},guest_context{},changed{};
    Check(CeIceExchange::ContextForConfig(&hc,pure.data()),"pure context exists");
    Check(CeIceExchange::ContextForConfig(&gc,guest_context.data()) && pure==guest_context,"context independent of role");
    auto irrelevant=hc; irrelevant.port=999; irrelevant.handshake_ms=999;
    Check(CeIceExchange::ContextForConfig(&irrelevant,changed.data()) && pure==changed,"unhashed config fields do not alter context");
    ++irrelevant.compatibility[0];
    Check(CeIceExchange::ContextForConfig(&irrelevant,changed.data()) && pure!=changed,"compatibility changes context");
    auto blank=hc; std::memset(blank.secret,0,32);
    Check(!CeIceExchange::ContextForConfig(&blank,changed.data()) && !CeIceExchange::ContextForConfig(nullptr,changed.data()),"invalid pure input refused");
    auto hd=Dir(root,"host"),gd=Dir(root,"guest");
    auto h=CeIceExchange::Open(hc,hd.c_str(),1000),g=CeIceExchange::Open(gc,gd.c_str(),1000);
    Check(h && g && !std::memcmp(h->Context(),pure.data(),32),"Open uses pure context");
    uint8_t small=0xa5; size_t size=999;
    Check(h->ExportLocalRecord(&small,1,&size,1000)==R::Pending && !size && small==0xa5,"export waits without changing output");
    Check(h->PublishLocal(host_sdp,sizeof(host_sdp)-1,1000)==R::Ready,"host publishes");
    Check(h->ExportLocalRecord(&small,1,&size,1000)==R::Error && !size && small==0xa5,"short export preserves bytes and owner");
    auto offer=Export(*h); Check(offer==Export(*h),"export immutable retry");
    Check(g->ImportPeerRecord(offer.data(),offer.size(),1000)==R::Ready,"guest authenticates import");
    Check(g->ImportPeerRecord(offer.data(),offer.size(),1000)==R::Ready,"import identical retry");
    Check(Read(File(gd,*g,".offer"))==offer,"exact ciphertext published");
    std::string decoded;
    Check(Poll(*g,decoded)==R::Ready && decoded==host_sdp,"import does not consume SDP");
    Check(Poll(*g,decoded)==R::Pending,"SDP delivered once");
    Check(g->ImportPeerRecord(offer.data(),offer.size(),1400)==R::Ready,"accepted identical retry after exchange expiry");
    Check(g->PublishLocal(guest_sdp,sizeof(guest_sdp)-1,1001)==R::Ready,"guest binds answer");
    auto answer=Export(*g,1001);
    Check(h->ImportPeerRecord(answer.data(),answer.size(),1001)==R::Ready,"host authenticates answer");
    Check(Poll(*h,decoded,1001)==R::Ready && decoded==guest_sdp,"answer delivered once");

    auto fresh=Dir(root,"fresh"); auto other=CeIceExchange::Open(hc,fresh.c_str(),1000);
    Check(other && other->ImportPeerRecord(answer.data(),answer.size(),1000)==R::Pending,"host import waits for own offer");
    Check(other->PublishLocal(host_sdp,sizeof(host_sdp)-1,1000)==R::Ready,"fresh offer");
    Check(other->ImportPeerRecord(answer.data(),answer.size(),1001)==R::Error,"prior answer refused by fresh attempt");
    Check(Read(File(fresh,*other,".answer")).empty(),"wrong attempt never published");

    auto alternate=Dir(root,"alternate");
    std::memcpy(next_attempt.data(),offer.data()+25,32); use_attempt=true;
    auto second=CeIceExchange::Open(hc,alternate.c_str(),1000);
    Check(second && second->PublishLocal(host_sdp,sizeof(host_sdp)-1,1000)==R::Ready,"same-attempt alternate fixture");
    auto replaced=Export(*second); Check(replaced!=offer,"fresh nonce makes different valid record");
    Write(File(hd,*h,".offer"),replaced);
    Check(h->ExportLocalRecord(&small,1,&size,1001)==R::Error,"even valid local replacement refused");
}
void Refusals(const std::string& root) {
    auto hc=Config(CE_NET_HOST),gc=Config(CE_NET_GUEST);
    auto hd=Dir(root,"source");auto h=CeIceExchange::Open(hc,hd.c_str(),1000);
    Check(h && h->PublishLocal(host_sdp,sizeof(host_sdp)-1,1000)==R::Ready,"refusal source");
    const auto offer=Export(*h);
    for (int test=0;test<7;++test) {
        auto gd=Dir(root,"refuse"+std::to_string(test));auto config=gc;
        if (test==3) ++config.compatibility[0];
        auto g=CeIceExchange::Open(config,gd.c_str(),1000); Check(bool(g),"refusal guest opens");
        auto record=offer;
        if (test==0) record.back()^=1;
        if (test==1) record[8]=CE_NET_GUEST;
        if (test==2) record.resize(99);
        if (test==4) record.resize(CeIceExchange::MaxRecordBytes+1);
        if (test==6) Write(File(gd,*g,".offer"),std::vector<uint8_t>(100,0x41));
        Check(g->ImportPeerRecord(record.data(),record.size(),test==5?1300:1000)==R::Error,"invalid/conflicting import refused");
        Check(Read(File(gd,*g,".offer"))==(test==6?std::vector<uint8_t>(100,0x41):std::vector<uint8_t>()),"refusal preserves directory");
    }
    for (size_t length:{size_t(1),size_t(4095)}) {
        auto d=Dir(root,"bound"+std::to_string(length));auto g=CeIceExchange::Open(hc,d.c_str(),1000);
        std::string text(length,'x'); Check(g && g->PublishLocal(text.data(),text.size(),1000)==R::Ready,"boundary SDP accepted");
        Check(Export(*g).size()==length+99,"exact bounded record envelope");
    }
}
}
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    Bridge(argv[1]); Refusals(argv[1]);
    std::cout<<"ICE_EXCHANGE_BRIDGE_PASS "<<checks<<'\n';
}
