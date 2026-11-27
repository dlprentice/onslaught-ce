// SPDX-License-Identifier: GPL-3.0-only
#ifdef CE_VERSION
#include "common.h"
#include "ce_extras.h"
#include "ce_career_view.h"
#include "ce_name_text.h"
#include "../platform/common/ce_career_backup.h"
#include "../platform/common/ce_backup_name.h"
#include "../platform/common/ce_record_mode.h"
#include "../platform/common/save_names.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" int CE_SettingGetText(const char *, char *, int);
void PortCeBackupWhen(char *out, size_t cap, const char *text, bool dateOnly); // ce_backup_preview.cpp
const char *PortUserDir();
const char *PortGameDir();
bool PortCeMenus();
bool PortCeProjectCareerAllowed() noexcept;
namespace {
CeBackupStatus view;
char selectedFile[256];
WCHAR selectedDisplay[256];
bool hiddenDirection, unavailable;
bool permitted() {
    const char *record=getenv("BEA_PORT_RECORD"), *tape=getenv("BEA_PORT_TAPE");
    return PortCeMenus() && PortCeProjectCareerAllowed() &&
        (ce_record_ce_allowed() || ((!record||!*record) && (!tape||!*tape)));
}
void openPage() {
    char folder[4096]={}; selectedFile[0]=0;selectedDisplay[0]=0;
    const WCHAR *source=PortCeBackupCareerName();
    bool resolved=CeBackupSelectedName(source,selectedFile,sizeof(selectedFile),selectedDisplay);
    hiddenDirection=CeBackupNameHasDirectionMark(source) ||
        (resolved && CeBackupNameHasDirectionMark(selectedDisplay));
    unavailable = !resolved;
#ifdef _WIN32
    unavailable = unavailable || !PortCeBackupWindowsOrigin();
#endif
    if(permitted() && !unavailable && !hiddenDirection)
        CE_SettingGetText("career_backup_folder",folder,sizeof(folder));
    CeBackupEnter(folder);CeBackupPoll(&view);
}
void update() { CeBackupPoll(&view); }
// The rows, in order: Back Up Now first (what the page is for), Choose Folder,
// the folder by its own name on one row (never a full path), and Back last.
enum { R_NOW, R_CHOOSE, R_FOLDER, R_BACK, R_NONE };
int act(int i) {
    if(hiddenDirection || unavailable)return i==0?R_BACK:R_NONE;
    return i>=0&&i<4?i:R_NONE;
}
void widenUtf8(WCHAR *dst, int n, const char *s) { CeDecodeName(dst, n, s, true); }
void leafOf(char *out, size_t cap, const char *path) {
    if (!cap) return;
    out[0] = 0;
    if (!path || !*path) return;
    size_t end = strlen(path);
    while (end > 1 && (path[end - 1] == '/' || path[end - 1] == '\\')) {
        if (end == 3 && path[1] == ':') break;
        --end;
    }
    if ((end == 1 && (path[0] == '/' || path[0] == '\\')) ||
        (end == 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\'))) {
        snprintf(out, cap, "%.*s", int(end), path);
        return;
    }
    size_t start = end;
    while (start > 0 && path[start - 1] != '/' && path[start - 1] != '\\') --start;
    if (start == end) {
        snprintf(out, cap, "%c", path[0]);
        return;
    }
    snprintf(out, cap, "%.*s", int(end - start), path + start);
}
// The folder row's text: the new set's date once a backup is made, else the folder's name.
void folderText(char *out,size_t cap) {
    char leaf[256];
    if(view.state==CE_BACKUP_OK && view.location[0]) {
        char when[96];
        leafOf(leaf,sizeof(leaf),view.location);
        PortCeBackupWhen(when,sizeof(when),leaf,false);
        snprintf(out,cap,"Set %s",when);return;
    }
    if(view.folder[0]) {
        leafOf(leaf,sizeof(leaf),view.folder);
        snprintf(out,cap,"%s",leaf[0]?leaf:view.folder);
        return;
    }
    snprintf(out,cap,"No folder chosen");
}
int rows() { return hiddenDirection || unavailable ? 1 : 4; }
void row(int i,SCeListRow *value) {
    SCeListRow &out=*value;
    const int a=act(i);
    if(a==R_FOLDER) {char text[160];folderText(text,sizeof(text));
        widenUtf8(out.mLeft,80,text);PortCeExtrasInFont(out.mLeft);out.mTone=CE_TONE_DIM;return;}
    const char *label=a==R_BACK?"Back":a==R_CHOOSE?"Choose Folder":a==R_NOW?"Back Up Now":"";
    PortCeExtrasWiden(out.mLeft,80,label);
    if((a==R_NOW||a==R_CHOOSE) && (view.state==CE_BACKUP_WORKING||view.state==CE_BACKUP_OTHER||view.state==CE_BACKUP_PICKING))out.mDim=true;
    if(a==R_NOW && !view.folder[0])out.mDim=true;
}
void hint(int i,WCHAR *help,WCHAR *extra,int n) {
    if(hiddenDirection) {
        PortCeExtrasWiden(help,n,CeBackupDirectionReason());
        PortCeExtrasWiden(extra,n,"A backup cannot be made with this name.");return;
    }
    if(unavailable) {
        PortCeExtrasWiden(help,n,"This career could not be verified in your CE saves.");
        PortCeExtrasWiden(extra,n,"Return to the list and open it again."); return;
    }
    if(view.state==CE_BACKUP_OTHER || view.state==CE_BACKUP_WORKING || view.state==CE_BACKUP_PICKING || view.state==CE_BACKUP_ERROR ||
        view.state==CE_BACKUP_OK || view.message[0]){
        PortCeExtrasWiden(help,n,view.message);
        PortCeExtrasWiden(extra,n,view.state==CE_BACKUP_OK?"Saved as a new set in your backup folder.":
            view.state==CE_BACKUP_ERROR?"Your original career has not changed.":"You can go back at any time.");
        return;
    }
    const int a=act(i);
    if(a==R_FOLDER) {PortCeExtrasWiden(help,n,"Your backups go into this folder.");PortCeExtrasWiden(extra,n,"Choose Folder changes it.");return;}
    PortCeExtrasWiden(help,n,a==R_BACK?"Return to this career without making a backup.":
        a==R_CHOOSE?"Choose a folder outside your game and careers.":"Make a new backup of the selected career.");
    const WCHAR *name=selectedDisplay;
    if(n>0){wcsncpy(extra,name,n-1);extra[n-1]=0;PortCeExtrasInFont(extra);}
}
bool select(int i) {
    const int a=act(i);
    if(a==R_BACK){PortCeRecordBackupBack();return true;}
    if(hiddenDirection || unavailable)return false;
    if(!permitted()){PortCeExtrasMessage("Backups are unavailable in this session.");return false;}
    if(a==R_CHOOSE){CeBackupChoose();update();return true;}
    if(a!=R_NOW || !selectedFile[0])return false;
    bool started=CeBackupStart(PortUserDir(),PortGameDir(),selectedFile,PortCeBackupSnapshot(),CE_CAREER_SIZE
#ifdef _WIN32
        ,PortCeBackupWindowsOrigin()
#endif
        );
    update();return started;
}
const WCHAR *empty() {return L"";}
const SCeListPage page={L"Back Up Career",rows,row,hint,select,empty,openPage,CeBackupLeave,update,NULL,0.78f,0};
}
const SCeListPage *PortCeRecordBackupPage() { return &page; }
// ---- The test kit's state key for Back Up Career (ce_testkit.h): the page's last status ---------------------------------------
// A plain read for the test kit (ce_extras.h). It changes nothing.
int PortCeBackupViewState() { return view.state; }
#endif
