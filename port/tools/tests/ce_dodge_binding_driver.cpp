// SPDX-License-Identifier: GPL-3.0-only
#include "../../game/ce_dodge_binding.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace { std::string stored;bool writable=true;int calls=0;
void check(bool v){if(!v)std::abort();}
}
extern "C" int CE_SettingGetText(const char *key,char *out,int n){
    check(!std::strcmp(key,"dodge_key"));if(stored.empty())return 0;
    return std::snprintf(out,n,"%s",stored.c_str())<n;
}
extern "C" int CE_SettingSetText(const char *key,const char *value) {
    ++calls;check(!std::strcmp(key,"dodge_key"));if(!writable)return 0;stored=value;return 1;
}
extern "C" int PortCeTutorialPrintableKey(int key,char *out,int size) {
    if(key!=37||size<2)return 0;
    std::strcpy(out,"K");return 1;
}
int main(){
    char name[32]{};
    check(PortCeDodgeBindingKey()==29 && stored.empty() && !calls);
    check(PortCeDodgeBindingName(29,name,sizeof(name)) && !std::strcmp(name,"Left Ctrl"));
    check(!PortCeDodgeBindingStore(1) && !PortCeDodgeBindingStore(-1) && !PortCeDodgeBindingStore(180) && !calls);
    check(PortCeDodgeBindingStore(37) && stored=="37");PortCeDodgeBindingReload();check(PortCeDodgeBindingKey()==37);
    check(PortCeDodgeBindingName(37,name,sizeof(name))&&!std::strcmp(name,"K"));
    writable=false;check(!PortCeDodgeBindingStore(29)&&stored=="37"&&PortCeDodgeBindingKey()==37);
    for(const char *bad:{"","37oops","256","999999","1","-1","224"}){
        stored=bad;PortCeDodgeBindingReload();check(PortCeDodgeBindingKey()==29 && stored==bad);
    }
    std::puts("Dodge preference: default, layout name, rejection, saved reload and failure passed.");
}
