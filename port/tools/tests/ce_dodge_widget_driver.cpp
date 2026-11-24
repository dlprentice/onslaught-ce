// SPDX-License-Identifier: GPL-3.0-only
// Real Dodge menu item against a small event loop and disposable settings backend.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>
using WCHAR=wchar_t;using WORD=uint16_t;using BOOL=int;using DWORD=uint32_t;
constexpr BOOL FALSE=0;
enum EPCText { eNone };enum EMemoryType { eOrdinary=0x80 };
enum KeyEventType { KEY_EVENT_DOWN,KEY_EVENT_UP,KEY_EVENT_CHAR };
using Trap=void(*)(WORD,KeyEventType);
void*operator new(std::size_t n,EMemoryType){return ::operator new(n);}
void operator delete(void*p,EMemoryType)noexcept{::operator delete(p);}
struct Loop {Trap trap=nullptr;Trap GetKeytrap(){return trap;}void SetKeytrap(Trap t){trap=t;}} LT;
auto&PLATFORM=LT;
enum { FES_BACK,FES_SELECT };
enum { BUTTON_FRONTEND_MENU_BACK = 46 };
class CController;
struct CFrontEnd {static void PlaySound(int){}};
std::wstring lastHint;
float lastHintY = 0.f;
struct CMenuItem {explicit CMenuItem(EPCText,int id):mId(id){}virtual ~CMenuItem()=default;virtual void ReceiveButtonAction(CController*,int,float){}virtual WCHAR*GetText(){return nullptr;}virtual void Render(float,float,BOOL){}virtual bool IsExpanded(){return false;}virtual bool TakesInput(){return false;}void Render(float,float y,BOOL,DWORD,WCHAR*text){lastHintY=y;lastHint=text?text:L"";}int mId;};
struct CMenuOptionsList {
	CMenuItem *item=nullptr,*rows[3]={}; int count=0;
	~CMenuOptionsList(){for(int i=0;i<count;++i)delete rows[i];}
	void AddItem(CMenuItem*p){assert(count<3); rows[count++]=p;if(!item)item=p;}
};
#include "widget.inc"
namespace {std::string stored;bool enabled=true,ordinary=true,writable=true,mixed=false;int writes=0;}
extern "C" int CE_SettingGetBool(const char*,int){return enabled;}
std::string takeSaved,commanderSaved; bool unitOn=false,commanderOn=false;
std::string &store(const char*key){if(!std::strcmp(key,"take_unit_key"))return takeSaved;if(!std::strcmp(key,"commander_key"))return commanderSaved;assert(!std::strcmp(key,"dodge_key"));return stored;}
extern "C" int CE_SettingGetText(const char*key,char*out,int cap){auto &v=store(key);if(v.empty())return 0;std::snprintf(out,cap,"%s",v.c_str());return 1;}
extern "C" int CE_SettingSetText(const char*key,const char*value){if(!writable)return 0;++writes;store(key)=value;return 1;}
extern "C" int PortCeTutorialPrintableKey(int code,char*out,int cap){const char*n=code==18?"E":code==30?"A":code==22?"U":code==0x32?"M":code==0x25?"K":code==0x26?"L":nullptr;if(!n)return 0;std::snprintf(out,cap,"%s",n);return 1;}
bool PortCeDodgeBindingAvailable(){return ordinary;}
bool PortCeActionEnabled(CeControlAction a){return a==CeControlAction::Dodge ? enabled : a==CeControlAction::TakeUnit ? unitOn : commanderOn;}
const char *PortCeActionConflict(CeControlAction a,int code) {
 if (code==0x1e) return "Strafe Left";
 if(a!=CeControlAction::Dodge && enabled && PortCeActionKey(CeControlAction::Dodge)==code)return "Dodge modifier";
 if(a!=CeControlAction::TakeUnit && (mixed||unitOn) && PortCeActionKey(CeControlAction::TakeUnit)==code)return "Take Unit";
 if(a!=CeControlAction::Commander && commanderOn && PortCeActionKey(CeControlAction::Commander)==code)return "Commander view";
 return nullptr;
}
const char *PortCeDodgeBindingConflict(int code){return PortCeActionConflict(CeControlAction::Dodge,code);}
void send(int code){assert(LT.trap);LT.trap(code,KEY_EVENT_DOWN);if(LT.trap)LT.trap(code,KEY_EVENT_UP);}
bool has(CMenuItem*p,const wchar_t*text){return std::wcsstr(p->GetText(),text)!=nullptr;}
int main(){
    ordinary=false;CMenuOptionsList denied;PortCeDodgeControlsAdd(&denied);assert(!denied.item);
    ordinary=true;CMenuOptionsList list;PortCeDodgeControlsAdd(&list);auto item=list.item;assert(list.count==1&&item&&has(item,L"Dodge modifier: Left Ctrl"));
    assert(PortCeDodgeControlsSelect(item)&&LT.trap);send(30);assert(LT.trap&&has(item,L"is used by Strafe Left. Try another key or Esc.")&&!writes);
    item->Render(0,0,1);assert(lastHint==L"Esc keeps Left Ctrl." && lastHintY >= 450.f && lastHintY <= 462.f);
    item->ReceiveButtonAction(nullptr, BUTTON_FRONTEND_MENU_BACK, 1.f);
    assert(!LT.trap && !item->IsExpanded() && !item->TakesInput() && has(item,L"Left Ctrl") && !writes);
    assert(PortCeDodgeControlsSelect(item)&&LT.trap&&item->IsExpanded()&&item->TakesInput());
    send(1);assert(!LT.trap&&has(item,L"Left Ctrl")&&!writes);
    PortCeDodgeControlsSelect(item);send(18);assert(!LT.trap&&stored=="18"&&has(item,L"Dodge modifier: E"));
    PortCeDodgeControlsSelect(item);send(1);assert(!LT.trap&&has(item,L"E")&&writes==1);
    LT.trap=[](WORD,KeyEventType){};assert(PortCeDodgeReserveFromControls(18)&&has(item,L"E is Dodge"));
    item->Render(0,0,1);assert(lastHint==L"Press Esc to keep your previous key.");
    LT.trap=nullptr;assert(has(item,L"Dodge modifier: E"));
    mixed=true;assert(!PortCeDodgeReserveFromControls(18)&&has(item,L"in use"));PortCeDodgeControlsSelect(item);send(18);assert(LT.trap&&has(item,L"is used by Take Unit. Try another key or Esc."));send(1);
    mixed=false;enabled=false;assert(!PortCeDodgeReserveFromControls(18));
    CMenuOptionsList off;PortCeDodgeControlsAdd(&off);assert(off.count==1&&has(off.item,L"(Off)"));PortCeDodgeControlsSelect(off.item);writable=false;send(22);assert(!LT.trap&&has(off.item,L"could not save")&&stored=="18");
    PortCeDodgeControlsSelect(off.item);writable=true;send(22);assert(!LT.trap&&stored=="22"&&has(off.item,L"U (Off)"));
    PortCeDodgeControlsSelect(off.item);assert(LT.trap);ordinary=false;send(22);assert(!LT.trap&&writes==2);
    ordinary=true; writable=true; enabled=true; mixed=false; unitOn=true; commanderOn=true;
 stored.clear(); takeSaved.clear(); commanderSaved.clear(); PortCeDodgeBindingReload();
 CMenuOptionsList gameplay; PortCeDodgeControlsAdd(&gameplay);assert(gameplay.count==3);
 assert(has(gameplay.rows[1],L"Take Unit: E") && has(gameplay.rows[2],L"Commander: M"));
 assert(PortCeDodgeControlsSelect(gameplay.rows[1]));send(0x32);assert(LT.GetKeytrap() && takeSaved.empty());
 send(1);assert(!LT.GetKeytrap() && takeSaved.empty());assert(PortCeDodgeControlsSelect(gameplay.rows[1]));send(0x25);
 assert(takeSaved=="37" && has(gameplay.rows[1],L"Take Unit: K") && PortCeDodgeReserveFromControls(0x25));
 assert(PortCeDodgeControlsSelect(gameplay.rows[2]));send(0x25);assert(LT.GetKeytrap() && commanderSaved.empty());send(0x26);
 assert(commanderSaved=="38" && has(gameplay.rows[2],L"Commander: L") && PortCeDodgeReserveFromControls(0x26));
 assert(!PortCeDodgeReserveFromControls(0x32));PortCeDodgeBindingReload();assert(PortCeActionKey(CeControlAction::TakeUnit)==0x25 && PortCeActionKey(CeControlAction::Commander)==0x26);
 std::puts("Dodge menu capture: native ownership, conflicting keys, Escape, Off, save failure and recovery passed.");
}
