// SPDX-License-Identifier: GPL-3.0-only
#include "../../platform/common/ce_career_backup.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
namespace {
std::atomic<bool> release(false),entered(false);
SDL_DialogFileCallback finish=nullptr;
void *ticket=nullptr;
void check(bool yes){if(!yes)std::abort();}
CeBackupStatus snapshot(){CeBackupStatus p{};CeBackupPoll(&p);return p;}
}
extern "C" SDL_Window *SDL_GetKeyboardFocus(){return nullptr;}
extern "C" void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback done,void *data,SDL_Window *,const char *,bool){
    finish=done;ticket=data;
}
bool CeBackupStore(const CeBackupInput &,char *location,size_t cap,const char *&error){
    entered=true;
    for(unsigned i=0;i<2000&&!release;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(release);snprintf(location,cap,"/first/set");error=nullptr;return true;
}
int main(){
    CeBackupEnter("/first");CeBackupChoose();check(snapshot().state==CE_BACKUP_PICKING);
    const char *cancel[]={nullptr};finish(ticket,cancel,0);
    check(snapshot().state==CE_BACKUP_IDLE && !strcmp(snapshot().folder,"/first"));
    CeBackupChoose();CeBackupLeave();CeBackupEnter("/second");
    const char *stale[]={"/old-dialog",nullptr};finish(ticket,stale,0);check(!strcmp(snapshot().folder,"/second"));
    unsigned char bytes[10004]={};CeBackupEnter("/first");
    check(CeBackupStart("/u","/g","A.bes",bytes,sizeof(bytes)));
    for(int i=0;i<2000&&!entered;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(entered&&snapshot().state==CE_BACKUP_WORKING);
    CeBackupLeave();CeBackupEnter("/second");check(snapshot().state==CE_BACKUP_OTHER);
    check(!CeBackupStart("/u","/g","B.bes",bytes,sizeof(bytes)));
    release=true;
    for(int i=0;i<2000&&snapshot().state==CE_BACKUP_OTHER;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CeBackupStatus current=snapshot();check(current.state==CE_BACKUP_IDLE&&!current.location[0]&&!strcmp(current.folder,"/second"));
    check(CeBackupStart("/u","/g","B.bes",bytes,sizeof(bytes)));
    for(int i=0;i<2000&&snapshot().state==CE_BACKUP_WORKING;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(snapshot().state==CE_BACKUP_OK);CeBackupLeave();
}
