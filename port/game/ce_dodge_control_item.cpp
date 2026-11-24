// SPDX-License-Identifier: GPL-3.0-only
#ifdef CE_VERSION
#include "common.h"
#include "frontend.h"
#include "MenuItem.h"
#include "MenuOptionsList.h"
#include "Platform.h"
#include "ltshell.h"
#include "ce_dodge_binding.h"
#include <initializer_list>
#include <stdio.h>
#include <string.h>

namespace {
extern "C" int CE_SettingGetBool(const char*,int);
const int bindingId=0x43454447;
enum Prompt { Ready,Waiting,Conflict,Unsupported,SaveFailed };
class DodgeKeyItem;
DodgeKeyItem *owner=nullptr;
void capture(WORD key,KeyEventType event);
bool reverseConflict=false;
CeControlAction reverseOwner=CeControlAction::Dodge;

class DodgeKeyItem : public CMenuItem {
public:
    explicit DodgeKeyItem(CeControlAction which) : CMenuItem((EPCText)0,bindingId + static_cast<int>(which)), action(which) { refreshState(); }
    ~DodgeKeyItem() { end(); }
    WCHAR *GetText() {
        if(reverseConflict && !LT.GetKeytrap())reverseConflict=false;
        char key[32]{},line[112]{};
        PortCeDodgeBindingName(PortCeActionKey(action),key,sizeof(key));
        switch(prompt) {
        case Waiting: snprintf(line,sizeof(line),"%s: press a key (Esc cancels)", PortCeActionLabel(action)); break;
        case Conflict: snprintf(line,sizeof(line),"%s is used by %s. Try another key or Esc.",attempt,conflict); break;
        case Unsupported: snprintf(line,sizeof(line),"%s: choose another key (Esc cancels)", PortCeActionLabel(action)); break;
        case SaveFailed: snprintf(line,sizeof(line),"%s: could not save. Press Enter to retry.", PortCeActionLabel(action)); break;
        default: {
            if (reverseConflict && reverseOwner == action) {
                snprintf(line,sizeof(line),"%s is %s. Choose another key or Esc.",key,PortCeActionLabel(action));
                break;
            }
            const bool occupied=PortCeActionConflict(action, PortCeActionKey(action))!=nullptr;
            snprintf(line,sizeof(line),"%s: %s%s", action == CeControlAction::Dodge ? "Dodge modifier" : PortCeActionLabel(action), key,!assist?" (Off)":occupied?" (in use)":"");
            break;
        }
        }
        size_t i=0; for(;i+1<sizeof(label)/sizeof(*label) && line[i];++i) label[i]=(unsigned char)line[i]; label[i]=0;
        return label;
    }
    int GetHeight() { return 18; }
    void Render(float x,float y,BOOL selected) {
        CMenuItem::Render(x,y,selected);
        if (!selected && owner != this && !(reverseConflict && reverseOwner == action)) return;
        char key[32]{},hint[94]{};
        PortCeDodgeBindingName(PortCeActionKey(action),key,sizeof(key));
        if(owner == this)
            snprintf(hint,sizeof(hint),"Esc keeps %s.",key);
        else if(reverseConflict && reverseOwner == action)
            snprintf(hint,sizeof(hint),"Press Esc to keep your previous key.");
        else if(!assist)
            snprintf(hint,sizeof(hint),"%s",action==CeControlAction::Dodge ? "Turn on Dodge Button in CE Settings to use it." :
				"Turn on this feature in Experimental Features to use it.");
        else if(const char *used=PortCeActionConflict(action, PortCeActionKey(action)))
            snprintf(hint,sizeof(hint),"%s uses this key. Choose another for %s.", used, PortCeActionLabel(action));
        else
            snprintf(hint,sizeof(hint),action==CeControlAction::Dodge ? "Hold %s and press a movement direction." :
				action==CeControlAction::TakeUnit ? "Aim at a friendly unit and press %s." : "Press %s to open or leave Commander.", key);
        WCHAR text[96]{};
        for(size_t i=0;hint[i] && i+1<sizeof(text)/sizeof(*text);++i) text[i]=(unsigned char)hint[i];
        CMenuItem::Render(x,454.f,FALSE,0xffd7dbb0,text);
    }
    bool TakesInput() { return owner==this; }
    bool IsExpanded() { return owner==this; }
    void Refresh() { end();prompt=Ready;refreshState(); }
    void begin() {
        if(owner || LT.GetKeytrap() || !PortCeDodgeBindingAvailable())return;
        PortCeDodgeBindingReload();
        reverseConflict=false;owner=this; down=0;prompt=Waiting;PLATFORM.SetKeytrap(capture);
    }
    void end() {
        if(owner!=this)return;
        if(LT.GetKeytrap()==capture) PLATFORM.SetKeytrap(NULL);
        owner=nullptr; down=0;
    }
    void key(WORD code,KeyEventType event) {
        if(!PortCeDodgeBindingAvailable()) {end(); prompt=Ready;return;}
        if(event==KEY_EVENT_DOWN) {if(!down)down=code;return;}
        if(event!=KEY_EVENT_UP)return;
        if(code==1) {end();prompt=Ready;CFrontEnd::PlaySound(FES_BACK);return;}
        if(!down||code!=down)return;
        down=0;
        if(!PortCeDodgeBindingName(code,attempt,sizeof(attempt))) { prompt=Unsupported;return; }
        const char *used=PortCeActionConflict(action, code);
        if(used){conflict=used; prompt=Conflict;return;}
        if(!PortCeActionStore(action, code)){end();prompt=SaveFailed;return;}
        end();prompt=Ready;CFrontEnd::PlaySound(FES_SELECT);
    }
private:
    void refreshState() { PortCeDodgeBindingReload();assist=PortCeActionEnabled(action); }
    CeControlAction action;
	bool assist=true;
    Prompt prompt=Ready;
    WORD down=0;
    char attempt[32]{};
    const char *conflict="";
    WCHAR label[112]{};
};
void capture(WORD key,KeyEventType event) { if(owner)owner->key(key,event); }
}
bool PortCeDodgeReserveFromControls(int key) {
	if (!PortCeDodgeBindingAvailable()) return false;
	for (auto action : {CeControlAction::Dodge, CeControlAction::TakeUnit, CeControlAction::Commander}) {
		if (PortCeActionKey(action) == key && PortCeActionEnabled(action) && !PortCeActionConflict(action, key)) {
			reverseOwner=action; reverseConflict=true; return true;
		}
	}
	return false;
}
void PortCeDodgeControlsAdd(CMenuOptionsList *list) {
	if (!list || !PortCeDodgeBindingAvailable()) return;
	for (auto action : {CeControlAction::Dodge, CeControlAction::TakeUnit, CeControlAction::Commander})
		if (action == CeControlAction::Dodge || PortCeActionEnabled(action))
			list->AddItem(new ((EMemoryType)0x80) DodgeKeyItem(action));
}
bool PortCeDodgeControlsSelect(CMenuItem *item) {
	if (!item || item->mId < bindingId || item->mId > bindingId+2) return false;
	static_cast<DodgeKeyItem*>(item)->begin(); return true;
}
#endif
