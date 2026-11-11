// SPDX-License-Identifier: GPL-3.0-only
// Authored 0/1 flags only; this fixture never reads a career or a game file.
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" int CE_NetPreRunFlagsDigest(const uint8_t *,uint32_t,uint8_t *);
static void emit(const char *name,const uint8_t *flags) {
    uint8_t out[32];if(!CE_NetPreRunFlagsDigest(flags,256,out)){std::printf("FAIL %s\n",name);return;}
    std::printf("DIGEST %s ",name);for(uint8_t byte:out)std::printf("%02x",byte);std::puts("");
}
int main() {
    uint8_t flags[257]={},out[32];
    emit("zeros",flags);
    for(int i=0;i<256;++i)flags[i]=1;emit("ones",flags);
    for(int i=0;i<256;++i)flags[i]=i&1;emit("alternating",flags);
    for(int i=0;i<256;++i)flags[i]=0;
    for(int i=0;i<256;++i){char name[24];std::snprintf(name,sizeof name,"single-%d",i);flags[i]=1;emit(name,flags);flags[i]=0;}
    auto refuse=[&](const char *name,const uint8_t *p,uint32_t count,uint8_t *result){
        std::memset(out,0xa5,sizeof out);bool ok=!CE_NetPreRunFlagsDigest(p,count,result);
        for(uint8_t byte:out)ok=ok&&byte==0xa5;
        std::printf("%s %s\n",ok?"PASS":"FAIL",name);
    };
    refuse("null flags",nullptr,256,out);refuse("null output",flags,256,nullptr);
    refuse("short",flags,255,out);refuse("long",flags,257,out);
    flags[127]=2;refuse("noncanonical",flags,256,out);flags[127]=0;
    return 0;
}
