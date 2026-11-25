// SPDX-License-Identifier: GPL-3.0-only
#include "ce_career_backup.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <vector>

namespace {
struct State { std::mutex lock; CeBackupStatus view{}; unsigned long page=0,job=0,owner=0; bool open=false,busy=false; };
// SDL may finish a picker after the page closes; a slow removable drive must not block quitting.
State &state() { static State *s=new State; return *s; }
struct Ticket { unsigned long serial; };
void note(CeBackupStatus &v,CeBackupState kind,const char *s){v.state=kind;snprintf(v.message,sizeof(v.message),"%s",s);}
bool validFolderPath(const char *path) {
    if (!path || !path[0]) return false;
#ifdef _WIN32
    const bool drive=((path[0]>='A' && path[0]<='Z') || (path[0]>='a' && path[0]<='z'));
    return drive && path[1]==':' && (path[2]=='\\' || path[2]=='/');
#else
    return path[0]=='/';
#endif
}
size_t trimmedFolderLen(const char *path, size_t cap) {
    if (!path) return 0;
    size_t len = strnlen(path, cap);
    if (len >= cap) return len;
#ifdef _WIN32
    const bool driveRoot = len >= 3 &&
        ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' && (path[2] == '\\' || path[2] == '/');
    const size_t minLen = driveRoot ? 3 : 1;
    while (len > minLen && (path[len - 1] == '/' || path[len - 1] == '\\'))
        --len;
#else
    while (len > 1 && path[len - 1] == '/')
        --len;
#endif
    return len;
}
void SDLCALL picked(void *context,const char *const *files,int) {
    const unsigned long id=static_cast<Ticket*>(context)->serial;delete static_cast<Ticket*>(context);
    State &s=state();std::lock_guard<std::mutex> hold(s.lock);
    if(!s.open||s.page!=id||s.view.state!=CE_BACKUP_PICKING)return;
    if(!files){note(s.view,CE_BACKUP_ERROR,"The folder chooser could not open.");return;}
    if(!files[0]){note(s.view,CE_BACKUP_IDLE,"No new folder chosen.");return;}
    const size_t len = trimmedFolderLen(files[0], sizeof(s.view.folder));
    if(files[1] || !validFolderPath(files[0]) || len>=sizeof(s.view.folder)){
        note(s.view,CE_BACKUP_ERROR,"Choose one local folder.");return;
    }
    snprintf(s.view.folder,sizeof(s.view.folder),"%.*s",int(len),files[0]);note(s.view,CE_BACKUP_IDLE,"Ready when you are.");
}
}
void CeBackupEnter(const char *configured) {
    State &s=state();std::lock_guard<std::mutex> hold(s.lock);s.open=true;
    ++s.page;s.view={};
    if(configured && *configured) {
        const size_t len = trimmedFolderLen(configured, sizeof(s.view.folder));
        if(len<sizeof(s.view.folder))
            snprintf(s.view.folder,sizeof(s.view.folder),"%.*s",int(len),configured);
    }
}
void CeBackupLeave() {
    State &s=state();std::lock_guard<std::mutex> hold(s.lock);s.open=false;
    ++s.page;s.view.state=CE_BACKUP_IDLE;
}
void CeBackupChoose() {
    State &s=state();Ticket *ticket;
    {std::lock_guard<std::mutex> hold(s.lock);
     if(!s.open||s.busy||s.view.state==CE_BACKUP_PICKING)return;
     ticket=new(std::nothrow) Ticket{s.page};
     if(!ticket){note(s.view,CE_BACKUP_ERROR,"There is not enough memory.");return;}
     note(s.view,CE_BACKUP_PICKING,"Waiting for a folder.");}
    SDL_ShowOpenFolderDialog(picked,ticket,SDL_GetKeyboardFocus(),nullptr,false);
}
void CeBackupPoll(CeBackupStatus *out) {
    if(!out)return;
    State &s=state();std::lock_guard<std::mutex> hold(s.lock);*out=s.view;
    if(s.busy && s.owner!=s.page)
        note(*out,CE_BACKUP_OTHER,"The previous backup is still running.");
}
bool CeBackupStart(const char *user,const char *game,const char *filename,const unsigned char *selected,size_t n, const CeBackupOrigin *origin) {
    State &s=state();std::lock_guard<std::mutex> hold(s.lock);
    if(!s.open||s.busy||s.view.state==CE_BACKUP_PICKING)return false;
    if(!s.view.folder[0]){note(s.view,CE_BACKUP_ERROR,"Choose a backup folder first.");return false;}
    if(!selected||n!=10004||!filename||!user||!game){note(s.view,CE_BACKUP_ERROR,"This career is no longer available.");return false;}
    char exe[4096];
#ifdef _WIN32
    wchar_t wide[4096];
    DWORD count=GetModuleFileNameW(nullptr,wide,sizeof(wide)/sizeof(*wide));
    wchar_t *slash=count && count<sizeof(wide)/sizeof(*wide) ? wcsrchr(wide,L'\\') : nullptr;
    if (slash && slash-wide==2 && wide[1]==L':') ++slash;
    if (slash) *slash=0;
    if (!slash || !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,-1,exe,sizeof(exe),nullptr,nullptr)) {
        note(s.view,CE_BACKUP_ERROR,"The game folder could not be checked."); return false;
    }
#else
    ssize_t k=readlink("/proc/self/exe",exe,sizeof(exe)-1);
    if(k<=0||k>=ssize_t(sizeof(exe)-1)){note(s.view,CE_BACKUP_ERROR,"The game folder could not be checked.");return false;}
    exe[k]=0;char *slash=strrchr(exe,'/');
    if(!slash||slash==exe){note(s.view,CE_BACKUP_ERROR,"The game folder could not be checked.");return false;}
    *slash=0;
#endif
    CeBackupOrigin key = origin ? *origin : CeBackupOrigin{};
    const unsigned long page=s.page, job=++s.job;
    try {
        std::string u=user,g=game,p=exe,f=s.view.folder,name=filename;
        std::vector<unsigned char> raw(selected,selected+n);
        s.busy=true;s.owner=page;note(s.view,CE_BACKUP_WORKING,"Checking and copying your career.");
        std::thread([page,job,u,g,p,f,name,raw,key](){
            char location[4352]{};const char *err="The backup could not be completed.";bool ok=false;
            try {ok=CeBackupStore({u.c_str(),g.c_str(),p.c_str(),f.c_str(),name.c_str(),raw.data(),raw.size(),&key},
                                  location,sizeof(location),err);}catch(...){err="The backup could not be completed.";}
            State &s=state();std::lock_guard<std::mutex> hold(s.lock);
            if(s.job!=job)return;
            s.busy=false;
            if(!s.open||s.page!=page)return;
            snprintf(s.view.location,sizeof(s.view.location),"%s",location);
            note(s.view,ok?CE_BACKUP_OK:CE_BACKUP_ERROR,ok?"Backup verified. Your career is unchanged.":err);
        }).detach();
    }catch(...){s.busy=false;note(s.view,CE_BACKUP_ERROR,"The backup could not be started.");return false;}
    return true;
}
