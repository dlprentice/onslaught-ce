// SPDX-License-Identifier: GPL-3.0-only
// Authored data and deterministic test randomness; no sockets or real credentials.
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

extern "C" int CE_NetGenerateSecret(uint8_t secret[CE_NET_SECRET_BYTES]) {
    static unsigned counter=0;
    ++counter;
    for (size_t i=0;i<CE_NET_SECRET_BYTES;++i) secret[i]=uint8_t(counter+i*7);
    return 1;
}
namespace {
using R=CeIceExchangeResult;
int checks=0;
void Check(bool yes,const char* name) {
    ++checks; if (!yes) { std::cerr<<"FAIL: "<<name<<'\n'; std::exit(1); }
}
std::string Dir(const std::string& root,const char* name) {
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
    for (size_t i=0;i<CE_NET_SECRET_BYTES;++i) c.secret[i]=uint8_t(i+1);
    for (size_t i=0;i<CE_NET_DIGEST_BYTES;++i) c.compatibility[i]=uint8_t(i+41);
    return c;
}
std::string File(const std::string& root,const CeIceExchange& exchange,const char* suffix) {
    const char digits[]="0123456789abcdef";
    std::string name=root+"/ce-ice-";
    for (size_t i=0;i<32;++i) { name+=digits[exchange.Context()[i]>>4]; name+=digits[exchange.Context()[i]&15]; }
    return name+suffix;
}
std::vector<char> Read(const std::string& path) {
    std::ifstream f(path,std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
}
void Write(const std::string& path,const std::vector<char>& data) {
    std::ofstream f(path,std::ios::binary); f.write(data.data(),static_cast<std::streamsize>(data.size()));
    Check(f.good(),"authored fixture record written");
}
const char host_sdp[]="a=ice-ufrag:fixture-host\r\na=ice-pwd:invented-host-only\r\na=candidate:test 1 UDP 1 192.0.2.10 5000 typ relay\r\n";
const char guest_sdp[]="a=ice-ufrag:fixture-guest\r\na=ice-pwd:invented-guest-only\r\na=candidate:test 1 UDP 1 192.0.2.20 5001 typ relay\r\n";
R Poll(CeIceExchange& exchange,std::string& text,uint64_t now=1000) {
    char buffer[4096]{}; size_t size=0;
    auto result=exchange.PollRemote(buffer,sizeof(buffer),&size,now);
    text.assign(buffer,size); return result;
}
void PairAndReplay(const std::string& root) {
    auto directory=Dir(root,"pair");
    auto hc=Config(CE_NET_HOST),gc=Config(CE_NET_GUEST);
    auto host=CeIceExchange::Open(hc,directory.c_str(),1000);
    auto guest=CeIceExchange::Open(gc,directory.c_str(),1000);
    Check(host && guest,"host and guest open");
    Check(!std::memcmp(host->Context(),guest->Context(),32),"same CE session has same context");
    Check(host->Role()==CE_NET_HOST && guest->Role()==CE_NET_GUEST,"roles preserved");
    Check(guest->PublishLocal(guest_sdp,sizeof(guest_sdp)-1,1000)==R::Pending,"guest waits for offer");
    std::string decoded;
    Check(Poll(*host,decoded)==R::Pending && Poll(*guest,decoded)==R::Pending,"missing peer stays pending");
    Check(host->PublishLocal(host_sdp,sizeof(host_sdp)-1,1000)==R::Ready,"host publishes offer");
    auto offer=Read(File(directory,*host,".offer"));
    Check(!offer.empty(),"offer file exists");
    Check(std::string(offer.begin(),offer.end()).find("invented-host-only")==std::string::npos,"SDP credential not plaintext in file");
    Check(Poll(*guest,decoded)==R::Ready && decoded==host_sdp,"guest obtains exact authenticated offer");
    Check(guest->PublishLocal(guest_sdp,sizeof(guest_sdp)-1,1001)==R::Ready,"guest publishes bound answer");
    Check(Poll(*host,decoded,1001)==R::Ready && decoded==guest_sdp,"host obtains exact authenticated answer");
    Check(Poll(*host,decoded,1400)==R::Pending,"completed handshake does not expire an active session");
    Check(guest->PublishLocal(guest_sdp,sizeof(guest_sdp)-1,1400)==R::Ready,"published answer not rewritten");
    Check(!CeIceExchange::Open(hc,directory.c_str(),1002),"host refuses same-context old files");
    Check(!CeIceExchange::Open(gc,directory.c_str(),1002),"guest refuses same-context old answer");
    auto next=hc; ++next.secret[0];
    Check(CeIceExchange::Open(next,directory.c_str(),1002)!=nullptr,"fresh stage secret can share directory");

    auto replacement=offer; replacement.back()^=1;
    Write(File(directory,*host,".offer"),replacement);
    Check(Poll(*guest,decoded,1002)==R::Error,"replacement after acceptance is terminal");
    Check(Poll(*guest,decoded,1000)==R::Error,"terminal error cannot be reset by older time");

    auto retry_directory=Dir(root,"fresh-attempt");
    auto retry=CeIceExchange::Open(hc,retry_directory.c_str(),1002);
    Check(retry && retry->PublishLocal(host_sdp,sizeof(host_sdp)-1,1002)==R::Ready,"fresh host attempt publishes");
    auto new_offer=Read(File(retry_directory,*retry,".offer"));
    Check(std::memcmp(new_offer.data()+25,offer.data()+25,32)!=0,"fresh attempt ID despite same CE context");
    Write(File(retry_directory,*retry,".answer"),Read(File(directory,*host,".answer")));
    Check(Poll(*retry,decoded,1003)==R::Error,"prior answer cannot bind a fresh attempt");
}
void Validation(const std::string& root) {
    auto hc=Config(CE_NET_HOST),gc=Config(CE_NET_GUEST);
    std::string decoded;
    auto expiry=Dir(root,"expiry");
    auto h=CeIceExchange::Open(hc,expiry.c_str(),1000);
    auto g=CeIceExchange::Open(gc,expiry.c_str(),1100);
    Check(h && g && h->PublishLocal(host_sdp,sizeof(host_sdp)-1,1000)==R::Ready,"expiry fixture offer");
    Check(Poll(*g,decoded,1300)==R::Error,"expired offer refused");

    auto mismatch=Dir(root,"wrong-context");
    h=CeIceExchange::Open(hc,mismatch.c_str(),1000);
    auto wrong=gc; ++wrong.compatibility[0];
    g=CeIceExchange::Open(wrong,mismatch.c_str(),1000);
    Check(h && g && h->PublishLocal(host_sdp,sizeof(host_sdp)-1,1000)==R::Ready,"context fixture offer");
    Write(File(mismatch,*g,".offer"),Read(File(mismatch,*h,".offer")));
    Check(Poll(*g,decoded)==R::Error,"wrong compatibility cannot authenticate copied record");

    auto damaged=Dir(root,"changed-record");
    h=CeIceExchange::Open(hc,damaged.c_str(),1000); g=CeIceExchange::Open(gc,damaged.c_str(),1000);
    Check(h && g && h->PublishLocal(host_sdp,sizeof(host_sdp)-1,1000)==R::Ready,"changed record fixture");
    auto data=Read(File(damaged,*h,".offer")); data.back()^=1; Write(File(damaged,*h,".offer"),data);
    Check(Poll(*g,decoded)==R::Error,"changed record authentication refused");

    auto bounded=Dir(root,"bounded");
    h=CeIceExchange::Open(hc,bounded.c_str(),1000);
    std::string large(4096,'x');
    Check(h && h->PublishLocal(large.data(),large.size(),1000)==R::Error,"oversized description refused");
    auto blank=hc; std::memset(blank.secret,0,sizeof(blank.secret));
    Check(!CeIceExchange::Open(blank,bounded.c_str(),1000),"missing session capability refused");
    auto waiting=Dir(root,"waiting-expiry");
    h=CeIceExchange::Open(hc,waiting.c_str(),1000);
    Check(h && Poll(*h,decoded,1300)==R::Error,"waiting exchange expires without any file");
}
}
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    PairAndReplay(argv[1]); Validation(argv[1]);
    std::cout<<"PASS "<<checks<<" authored ICE exchange checks\n";
}
