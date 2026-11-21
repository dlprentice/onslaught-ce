// SPDX-License-Identifier: GPL-3.0-only
#if defined(CE_VERSION) || !defined(PORT_H)	// a CE layer file: a private game build (port.h forced in, no CE_VERSION) has none of it, as it has none of its callers; host test drivers compile it as it is
#include "ce_dodge_binding.h"
#include <cstdio>
#include <cstring>

extern "C" int CE_SettingGetText(const char*,char*,int);
extern "C" int CE_SettingSetText(const char*,const char*);
extern "C" int PortCeTutorialPrintableKey(int,char*,int);

namespace {
int saved[3] = {-1, -1, -1};
const char *field(CeControlAction a) {
	return a == CeControlAction::TakeUnit ? "take_unit_key" : a == CeControlAction::Commander ? "commander_key" : "dodge_key";
}
int fallback(CeControlAction a) {
	return a == CeControlAction::TakeUnit ? 0x12 : a == CeControlAction::Commander ? 0x32 : 0x1d;
}
}
const char *PortCeActionLabel(CeControlAction a) {
	return a == CeControlAction::TakeUnit ? "Take Unit" : a == CeControlAction::Commander ? "Commander" : "Dodge";
}
void PortCeDodgeBindingReload() { for (int &k : saved) k = -1; }
int PortCeDodgeBindingKey() { return PortCeActionKey(CeControlAction::Dodge); }
int PortCeActionKey(CeControlAction action) {
    int &cached = saved[static_cast<int>(action)];
    if(cached>=0)return cached;
    char value[16]{},name[32]{};
    int k=0;
    if(CE_SettingGetText(field(action),value,sizeof(value)) && value[0]) {
        for(const char *p=value;*p;++p) {
            if(*p<'0'||*p>'9'||k>25){k=0;break;}
            k=k*10+(*p-'0');
        }
    }
    cached=PortCeDodgeBindingName(k,name,sizeof(name))?k:fallback(action);
    return cached;
}
bool PortCeDodgeBindingName(int key,char *out,int cap) {
    if(!out||cap<2)return false;
    out[0]=0;
    if(key<=0||key>255||key==1||key==0xdb||key==0xdc||key==0xdd)return false;
    struct Named{int key;const char *name;};
    static const Named named[]={{0x1d,"Left Ctrl"},{0x9d,"Right Ctrl"},{0x2a,"Left Shift"},{0x36,"Right Shift"},
      {0x38,"Left Alt"},{0xb8,"Right Alt"},{0x39,"Space"},{0x0f,"Tab"},{0x1c,"Enter"},{0x0e,"Backspace"},
      {0xc8,"Up arrow"},{0xcb,"Left arrow"},{0xcd,"Right arrow"},{0xd0,"Down arrow"},
      {0xc7,"Home"},{0xcf,"End"},{0xc9,"Page Up"},{0xd1,"Page Down"},{0xd2,"Insert"},{0xd3,"Delete"}};
    for(const auto &entry:named)if(key==entry.key) {
        if(std::strlen(entry.name)>=static_cast<unsigned>(cap))return false;
        std::strcpy(out,entry.name);return true;
    }
    return PortCeTutorialPrintableKey(key,out,cap)!=0;
}
bool PortCeDodgeBindingStore(int key) { return PortCeActionStore(CeControlAction::Dodge, key); }
bool PortCeActionStore(CeControlAction action, int key) {
    char name[32],value[8];
    if(!PortCeDodgeBindingName(key,name,sizeof(name)))return false;
    std::snprintf(value,sizeof(value),"%d",key);
    if(!CE_SettingSetText(field(action),value))return false;
    saved[static_cast<int>(action)]=key;return true;
}

const char *PortCeActionName(CeControlAction action) {
	static char names[3][32];
	char *out=names[static_cast<int>(action)];
	return PortCeDodgeBindingName(PortCeActionKey(action), out, 32) ? out : "";
}
#endif
