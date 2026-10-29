// SPDX-License-Identifier: GPL-3.0-only
// Actual CE settings and video item, with authored files and instrumented OS failure boundaries only.
#include <cerrno>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" int CE_SettingGetBool(const char *, int);
extern "C" int CE_SettingGet(const char *, int);
extern "C" int CE_TrySettingGet(const char *, int, int *);
extern "C" int CE_SettingSet(const char *, int);
extern "C" int CE_CheatOn(int);
extern "C" int CE_CheatSet(int, int);
extern "C" int CE_CheatReady(int);
extern "C" int CE_CheatRestartRequired(int);
extern "C" void CE_MaladimRequireRestart();
extern "C" int CE_DisplayModeAvailable();
extern "C" int CE_DisplayModeChoice(int);
extern "C" int CE_DisplayModeStartup(int, int);
extern "C" int CE_DisplayModeStore(int);
extern "C" int CE_DisplayModeUseSavedSize();
int CE_DisplayModeAvailableBridge();
extern "C" void ActualCLIProbe(const char *, int, unsigned *);

static const char *fault = "";
static int reads, partial;
static uint64_t fixture_ticks = 10000;
extern "C" uint64_t SDL_GetTicks(void) { return fixture_ticks; }
extern "C" FILE *__real_fopen(const char *, const char *);
extern "C" size_t __real_fread(void *, size_t, size_t, FILE *);
extern "C" size_t __real_fwrite(const void *, size_t, size_t, FILE *);
extern "C" int __real_ferror(FILE *);
extern "C" int __real_fclose(FILE *);
extern "C" int __real_fflush(FILE *);
extern "C" int __real_fsync(int);
extern "C" int __real_renameat(int, const char *, int, const char *);
extern "C" int __real_openat(int, const char *, int, ...);
extern "C" int __wrap_openat(int dir, const char *path, int flags, ...)
{
    if (!strcmp(path, "ce.ini") && (flags & O_ACCMODE) == O_RDONLY && !(flags & O_DIRECTORY)) ++reads;
    if (!strcmp(path, "ce.ini") && !strcmp(fault, "open-read") && (flags & O_ACCMODE) == O_RDONLY && !(flags & O_DIRECTORY))
    { errno = EACCES; return -1; }
    if (flags & O_CREAT)
    {
        va_list ap; va_start(ap, flags); int mode = va_arg(ap, int); va_end(ap);
        return __real_openat(dir, path, flags, mode);
    }
    return __real_openat(dir, path, flags);
}
extern "C" FILE *__wrap_fopen(const char *path, const char *mode)
{
    if (strstr(path, "ce.ini")) ++reads;
    return __real_fopen(path, mode);
}
extern "C" size_t __wrap_fread(void *p, size_t size, size_t count, FILE *f)
{
    if (!strcmp(fault, "read") || !strcmp(fault, "short-read"))
    {
        if (partial++) return 0;
        if (size == 1 && count > 4) return __real_fread(p, 1, 4, f);
    }
    size_t result = __real_fread(p, size, count, f);
    if ((!strcmp(fault, "truncate-read") || !strcmp(fault, "change-read")) && partial++ == 0)
    {
        char path[4300]; snprintf(path, sizeof path, "%s/ce.ini", getenv("BEA_USER_DIR"));
        int fd = open(path, O_WRONLY|O_NOFOLLOW);
        if (fd < 0) abort();
        if (!strcmp(fault, "truncate-read")) { if (ftruncate(fd, 4) != 0) abort(); }
        else {
            if (pwrite(fd, "X", 1, 0) != 1) abort();
            const struct timespec times[2] = {{0, UTIME_OMIT}, {500, 0}};
            if (futimens(fd, times) != 0) abort();
        }
        close(fd);
    }
    return result;
}
extern "C" int __wrap_ferror(FILE *f)
{
    return !strcmp(fault, "read") && partial > 1 ? 1 : __real_ferror(f);
}
extern "C" int __wrap_fclose(FILE *f)
{
    int result = __real_fclose(f);
    if (!strcmp(fault, "close-read")) { errno = EIO; return EOF; }
    return result;
}
extern "C" size_t __wrap_fwrite(const void *p, size_t size, size_t count, FILE *f)
{
    if (!strcmp(fault, "short") && size == 1 && count > 1)
        return __real_fwrite(p, 1, count - 1, f);
    return __real_fwrite(p, size, count, f);
}
extern "C" int __wrap_fprintf(FILE *f, const char *fmt, ...)
{
    if (!strcmp(fault, "format")) { errno = EIO; return -1; }
    va_list ap; va_start(ap, fmt); int result = vfprintf(f, fmt, ap); va_end(ap); return result;
}
extern "C" int __wrap_fflush(FILE *f)
{
    if (!strcmp(fault, "flush")) { errno = EIO; return EOF; }
    return __real_fflush(f);
}
extern "C" int __wrap_fsync(int fd)
{
    if (!strcmp(fault, "sync")) { errno = EIO; return -1; }
    return __real_fsync(fd);
}
extern "C" int __wrap_renameat(int olddir, const char *oldname, int newdir, const char *newname)
{
    if (!strcmp(fault, "rename")) { errno = EIO; return -1; }
    int result=__real_renameat(olddir, oldname, newdir, newname);
    if (!result && !strcmp(fault,"post-switch-read")) { fault="read"; partial=0; }
    else if (!result && !strcmp(fault,"post-switch-clear")) fault="short";
    return result;
}

using WCHAR = wchar_t;
using EPCText = int;
struct CController {};
enum { BUTTON_FRONTEND_MENU_UP=1, BUTTON_FRONTEND_MENU_DOWN=2, BUTTON_FRONTEND_MENU_LEFT=3,
       BUTTON_FRONTEND_MENU_RIGHT=4, BUTTON_FRONTEND_MENU_SELECT=5, BUTTON_FRONTEND_MENU_BACK=6,
       FES_MOVE=7, FES_SELECT=8, FES_BACK=9 };
struct CFrontEnd { static void PlaySound(int) {} static bool MouseClickedInRect(float,float,float,float) { return true; } };
enum EMemoryType { MenuMemory = 0x80 };
void *operator new(size_t n, EMemoryType) { return ::operator new(n); }
struct CMenuItem { CMenuItem(EPCText, int) {} virtual ~CMenuItem() {} virtual void Commit() {} };
struct CMenuMenuItem : CMenuItem {
    int mSelection = -1, mValue = -1;
    bool mDeferred = false, mExpanded = false;
    CMenuMenuItem(EPCText, int, bool);
    virtual WCHAR *GetText() = 0;
    virtual int GetNumChoices() = 0;
    virtual WCHAR *GetChoiceText(int) = 0;
    virtual int GetValue() = 0;
    virtual void SetValue(int) = 0;
    virtual void ReceiveButtonAction(CController *, int, float);
    void Refresh();
    void Commit() override;
    bool HasPendingChange();
    void FixtureRenderSync();
    void FixtureMouseSelect(int i);
    void SetSelection(int n) { mSelection = n; }
};
struct Shell { int m_bWindowed = 0; } LT;
#include "native_menu.inc"
#include "native_cheat_action.inc"
void CMenuMenuItem::FixtureRenderSync() {
#include "native_cheat_sync.inc"
}
void CMenuMenuItem::FixtureMouseSelect(int i) {
    float lx=0.f, ly=0.f, cx2=1.f, cy2=1.f;
#include "native_cheat_mouse.inc"
}
#include VIDEO_SOURCE

// Exact CE cheat menu item, with its game globals and dialog at authored boundaries.
class CCeSwitch : public CMenuMenuItem { public: const WCHAR *mCeLabel;
    explicit CCeSwitch(const WCHAR *label) : CMenuMenuItem(0,3,false), mCeLabel(label) {}
    virtual WCHAR *GetText() { return (WCHAR *)mCeLabel; }
    virtual int GetNumChoices() { return 2; }
    virtual void SetValue(int) {} virtual int GetValue() { return 0; } virtual WCHAR *GetChoiceText(int) { return nullptr; } };
static const int PORT_CE_GOD_CHEAT = 3, FALSE = 0;
static const int TRUE = 1;
static const char *PORT_CE_GOD_KEY = "god_mode";
#define PORT_LOG_PREFIX "ce-test: "
using BOOL = int;
struct FakeCareer { int god[2]={0,0}; void SetIsGod(int p, int value) { god[p]=value; }
    int GetIsGod(int p) { return god[p]; } void SaveFixture(char *dest); } CAREER;
using CCareer = FakeCareer;
bool gPortCheatNameOnly=false;
char gAllCheatsEnabled=0, cheatkey[]="HELP ME!!", cheatname[6][256]{};
struct { int mAutoConfigTest=0; } CLIPARAMS;
static char *FromWCHAR(WCHAR *s) { static char out[100]; size_t i=0; while(s[i] && i<99) {out[i]=(char)s[i]; ++i;} out[i]=0; return out; }
struct CFEPSaveGame { WCHAR mSaveGameName[100]{}; BOOL IsCheatActive(int); };
#include "native_name_cheat.inc"
struct FakeFrontEnd { CFEPSaveGame mFEPSaveGame; } FRONTEND;
bool PortCeLayer();
#include "native_ce_god.inc"
void FakeCareer::SaveFixture(char *dest) {
#include "native_ce_save_block.inc"
}
static void SetFixtureCheatName()
{
    const char code[10]={'M','a','l','a','d','i','m',0,0,0};
    for (size_t i=0;i<strlen(cheatkey);++i) cheatname[3][i]=code[i]^cheatkey[i];
    wcscpy(FRONTEND.mFEPSaveGame.mSaveGameName,L"fixture Maladim");
}
static void SyncGodItem() {}
static const char *message;
static void ShowMessage(const char *s, float) { message = s; }
#include "native_cheat_switch.inc"

static bool menus = true;
bool PortCeMenus() { return menus; }
struct List { std::vector<CMenuItem *> mItems; void AddItem(CMenuItem *p) { mItems.push_back(p); } };
using CMenuOptionsList = List;
int gMenuApplyCount;
#define FOR_ALL_ITEMS_IN(container, item) for (auto it = (container).begin(); it != (container).end() && ((item) = *it, true); ++it)
struct CApplyMenuItem {
    CMenuOptionsList *mList;
    void ReceiveButtonAction(CController *, int, float);
};
#include "native_apply.inc"
static void NativeVideoRow(List *list)
{
#include "native_row.inc"
}
static void check(bool ok, const char *reason)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", reason); exit(2); }
}

int main(int argc, char **argv)
{
    if (argc < 2) return 3;
    const char *what = argv[1];
    if (!strcmp(what, "get"))
    {
        if (argc != 4) return 3;
        fault = argv[3];
        int value = CE_SettingGetBool("windowed", atoi(argv[2]));
        printf("%d\n", value);
    }
    else if (!strcmp(what, "get-int"))
    {
        if (argc != 3 && argc != 4) return 3;
        if (argc == 4) fault = argv[3];
        printf("%d\n", CE_SettingGet("wide_mode_w", atoi(argv[2])));
    }
    else if (!strcmp(what, "cheat"))
        printf("%d\n", CE_CheatOn(0));
    else if (!strcmp(what, "cheat-state"))
    {
        int ready = CE_CheatReady(0), value = CE_CheatOn(0);
        printf("%d %d %d\n", ready, value, reads);
    }
    else if (!strcmp(what, "cheat-all"))
    {
        int ready = CE_CheatReady(0), values[6];
        for (int i = 0; i < 6; ++i) values[i] = CE_CheatOn(i);
        printf("%d %d%d%d%d%d%d %d\n", ready, values[0], values[1], values[2], values[3], values[4], values[5], reads);
    }
    else if (!strcmp(what, "cheat-retry"))
    {
        if (argc != 3) return 3;
        fault = argv[2];
        CCeCheatSwitch item(L"fixture", 0);
        check(item.GetValue() == -1 && CE_CheatOn(0) == 0, "failed read is unknown to UI, off to game");
        check(!wcscmp(item.GetChoiceText(-1), L"Unavailable"), "failed read is labelled unavailable");
        item.SetValue(0);
        check(message && !strcmp(message, "Your settings could not be read or saved, so this was not changed."),
              "actual cheat switch reports refused change");
        char path[4300], bytes[128] = {};
        snprintf(path, sizeof path, "%s/ce.ini", getenv("BEA_USER_DIR"));
        int fd = open(path, O_RDONLY|O_NOFOLLOW);
        const char *original = "cheat_malloy=1\nunknown=preserved\n";
        check(fd >= 0 && read(fd, bytes, sizeof bytes) == (ssize_t)strlen(original) && !strcmp(bytes, original),
              "failed read did not alter persisted settings");
        close(fd);
        fault = ""; partial = 0; message = nullptr; fixture_ticks += 1000;
        check(item.GetValue() == 1, "successful retry loads the persisted switch");
        check(!wcscmp(item.GetChoiceText(1), L"On"), "successful retry restores the ordinary choice");
        item.SetValue(0);
        check(!message && item.GetValue() == 0, "retry applies the switch without a refusal dialog");
        puts("cheat-retry pass");
    }
    else if (!strcmp(what, "cheat-folder-absent"))
    {
        // A user folder that was never created holds no settings yet: an empty file, not a failed read.
        const char *folder = getenv("BEA_USER_DIR");
        char ini[4300];
        snprintf(ini, sizeof ini, "%s/ce.ini", folder);
        check(access(folder, F_OK) != 0, "fixture folder starts absent");
        CCeCheatSwitch item(L"fixture", 0);
        check(CE_CheatReady(0) == 1 && CE_CheatOn(0) == 0, "absent folder is a ready, off setting");
        check(item.GetValue() == 0 && !wcscmp(item.GetChoiceText(0), L"Off"), "absent folder shows the ordinary Off choice");
        int god = -1;
        check(CE_TrySettingGet("god_mode", 3, &god) && god == 3, "absent folder reads another key's default");
        check(!CE_CheatRestartRequired(PORT_CE_GOD_CHEAT) && CE_CheatReady(PORT_CE_GOD_CHEAT) == 1,
              "absent folder does not hold the Maladim switch for a restart");
        check(access(folder, F_OK) != 0, "reading creates no folder");
        item.SetValue(1);
        check(!message && item.GetValue() == 1, "the first write is accepted");
        check(access(ini, F_OK) == 0, "the first write created the folder and ce.ini");
        puts("cheat-folder-absent pass");
    }
    else if (!strcmp(what, "cheat-folder-refused"))
    {
        // A user folder that is refused (a link, a file in the way, no access) is a failed read, not an empty one.
        CCeCheatSwitch item(L"fixture", 0);
        check(CE_CheatReady(0) == 0 && CE_CheatOn(0) == 0, "refused folder is unavailable to the UI, off to the game");
        check(item.GetValue() == -1 && !wcscmp(item.GetChoiceText(-1), L"Unavailable"), "refused folder is labelled unavailable");
        int god = -1;
        check(!CE_TrySettingGet("god_mode", 3, &god), "refused folder is a failed god_mode read");
        check(CE_CheatRestartRequired(PORT_CE_GOD_CHEAT) == 1, "refused folder holds the Maladim switch for a restart");
        item.SetValue(1);
        check(message && !strcmp(message, "Your settings could not be read or saved, so this was not changed."),
              "refused folder reports a refused change");
        puts("cheat-folder-refused pass");
    }
    else if (!strcmp(what, "cheat-poll"))
    {
        if (argc != 3 && argc != 4) return 3;
        fault = argv[2];
        if (argc == 4 && !strcmp(argv[3], "wrap")) fixture_ticks = UINT64_MAX - 500;
        CCeCheatSwitch item(L"fixture", 0);
        check(item.GetValue() == -1, "failed first read shows unavailable");
        int attempts = reads;
        for (int i = 0; i < 1000; ++i) check(item.GetValue() == -1, "unreadable setting stays unavailable");
        check(reads == attempts, "same-tick automatic retries are throttled");
        if (argc == 4 && !strcmp(argv[3], "backward"))
        {
            fixture_ticks -= 100;
            check(item.GetValue() == -1 && reads == attempts + 1, "backward clock permits one retry");
            attempts = reads;
            for (int i = 0; i < 1000; ++i) check(item.GetValue() == -1, "backward clock returns to backoff");
            check(reads == attempts, "backward clock does not cause repeated reads");
        }
        fixture_ticks += 999;
        check(item.GetValue() == -1 && reads == attempts, "automatic retry waits a full second");
        fixture_ticks += 1;
        check(item.GetValue() == -1 && reads == attempts + 1, "automatic retry happens after one second");
        fault = ""; partial = 0;
        item.SetValue(0);
        check(!message && item.GetValue() == 0 && reads > attempts + 1,
              "player action retries immediately during backoff");
        attempts = reads;
        for (int i = 0; i < 1000; ++i) { fixture_ticks += 50; check(item.GetValue() == 0, "cached value stable"); }
        check(reads == attempts, "successful cache adds no further reads");
        puts("cheat-poll pass");
    }
    else if (!strcmp(what, "cheat-forced-capture"))
    {
        fault = "read";
        CCeCheatSwitch item(L"fixture", 0);
        check(item.GetValue() == -1 && CE_CheatOn(0) == 0, "forced capture game fallback remains off");
        int attempts = reads;
        fault = ""; partial = 0; fixture_ticks += 10000000;
        for (int i = 0; i < 1000; ++i)
            check(item.GetValue() == -1, "forced capture holds its failed automatic fallback");
        check(reads == attempts, "forced capture does not retry automatically");
        check(!wcscmp(item.GetChoiceText(-1), L"Unavailable"), "forced capture labels unknown state");
        item.SetValue(0);
        check(!message && item.GetValue() == 0 && reads > attempts, "explicit action may still retry");
        puts("cheat-forced-capture pass");
    }
    else if (!strcmp(what, "cheat-dispatch"))
    {
        if (argc != 3) return 3;
        const char *scenario=argv[2]; fault="read";
        CCeCheatSwitch item(L"fixture",0); item.Refresh();
        check(item.mValue == -1 && item.mSelection == -1 && !wcscmp(item.GetChoiceText(-1),L"Unavailable"),
              "actual menu starts unavailable");
        int attempts=reads;
        item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
        check(item.mExpanded, "actual select opens choices");
        if (!strcmp(scenario,"no-choice"))
        {
            item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
            check(!item.mExpanded && reads==attempts && !message, "closing without a choice does not write");
        }
        else if (!strcmp(scenario,"cancel"))
        {
            item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_DOWN,1.f);
            item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_BACK,1.f);
            check(!item.mExpanded && item.mSelection==-1, "cancel restores unavailable selection");
            fault=""; partial=0; fixture_ticks+=1000; item.FixtureRenderSync();
            check(item.mValue==1 && item.mSelection==1 && !message, "closed menu shows recovered value");
        }
        else
        {
            bool mouse=!strncmp(scenario,"mouse-",6), failure=strstr(scenario,"fail")!=nullptr;
            int target=strstr(scenario,"-on") ? 1 : 0;
            if (!mouse)
            {
                item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_DOWN,1.f);
                if (target) item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_RIGHT,1.f);
                check(item.mSelection==target, "keyboard chose real value");
            }
            if (!failure) { fault=""; partial=0; }
            if (!strcmp(scenario,"keyboard-shared"))
            {
                fixture_ticks+=1000;
                check(CE_CheatOn(0)==1 && CE_CheatReady(0)==1, "another caller recovered shared cache");
                item.FixtureRenderSync();
                check(item.mValue==-1 && item.mSelection==0, "open menu preserves pending selection");
            }
            if (mouse) item.FixtureMouseSelect(target);
            else item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
            check(!item.mExpanded, "actual commit closes choices");
            if (failure)
            {
                check(message && strstr(message,"could not be read or saved"), "failed actual commit reports error");
                item.FixtureRenderSync();
                check(item.mValue==-1 && item.mSelection==-1, "failed commit returns to unavailable");
            }
            else check(!message && item.GetValue()==target, "actual commit applies the chosen value");
        }
        puts("cheat-dispatch pass");
    }
    else if (!strncmp(what, "career-late-", 12) || !strcmp(what, "career-forced-recovery") || !strcmp(what,"career-name"))
    {
        fault="read"; PortCeCareerLoaded(&CAREER);
        check(!CAREER.GetIsGod(0) && CE_CheatRestartRequired(3), "failed career load marked restart");
        fault=""; partial=0; fixture_ticks+=1000;
        if (!strcmp(what,"career-forced-recovery")) check(CE_CheatSet(0,1), "forced capture explicit retry for other cheat");
        check(CE_CheatOn(0)==1 && CE_CheatReady(0)==1, "unrelated cheat can recover");
        check(CE_CheatOn(3)==0 && !CE_CheatReady(3) && CE_CheatRestartRequired(3),
              "Maladim remains unavailable after failed load");
        if (!strcmp(what,"career-name"))
        {
            SetFixtureCheatName();
            check(FRONTEND.mFEPSaveGame.IsCheatActive(3), "original name-based cheat still works");
        }
        check(!PortCeCareerSaving(&CAREER), "late save does not replace unrestored god setting");
        CCeCheatSwitch item(L"fixture",3); item.Refresh();
        check(item.mValue==-1 && !wcscmp(item.GetChoiceText(-1),L"Restart to retry"), "restart status is visible");
        item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
        int target=strstr(what,"-on") ? 1 : 0;
        if (strstr(what,"mouse")) item.FixtureMouseSelect(target);
        else {
            item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_DOWN,1.f);
            if (target) item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_RIGHT,1.f);
            item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
        }
        check(message && strstr(message,"Restart Onslaught CE"), "actual menu tells player to restart");
        printf("%s pass\n",what);
    }
    else if (!strcmp(what,"career-probe"))
    {
        fault="read"; PortCeCareerLoaded(&CAREER);
        fault=""; partial=0; fixture_ticks+=1000;
        CE_CheatOn(0);
        int enabled=CE_CheatOn(3), saving=PortCeCareerSaving(&CAREER);
        printf("%d %d\n",enabled,saving);
    }
    else if (!strcmp(what,"career-other-write"))
    {
        fault="read"; PortCeCareerLoaded(&CAREER);
        fault=""; partial=0; fixture_ticks+=1000;
        check(CE_CheatOn(0)==1 && !CE_CheatReady(3), "other switch recovers while Maladim remains held");
        check(CE_CheatSet(0,0), "other switch can be changed");
        check(!PortCeCareerSaving(&CAREER), "held Maladim cannot update unrestored god mode");
        puts("career-other-write pass");
    }
    else if (!strcmp(what,"career-godread-fail") || !strcmp(what,"career-godread-later") ||
             !strcmp(what,"career-godread-name") || !strcmp(what,"career-godread-probe"))
    {
        check(CE_CheatOn(3)==1, "first cheat read has Maladim on");
        if (!strcmp(what,"career-godread-later"))
        {
            PortCeCareerLoaded(&CAREER); check(CAREER.GetIsGod(0)==1, "first career restores god mode");
            CAREER.SetIsGod(0,0);
        }
        if (!strcmp(what,"career-godread-name")) SetFixtureCheatName();
        fault="read"; partial=0; PortCeCareerLoaded(&CAREER);
        fault=""; partial=0; fixture_ticks+=1000;
        if (!strcmp(what,"career-godread-probe"))
        {
            int enabled=CE_CheatOn(3), saving=PortCeCareerSaving(&CAREER);
            printf("%d %d\n",enabled,saving);
        }
        else if (!strcmp(what,"career-godread-name"))
        {
            check(!CE_CheatRestartRequired(3) && CE_CheatReady(3) && FRONTEND.mFEPSaveGame.IsCheatActive(3),
                  "name cheat does not need CE god setting");
            check(!PortCeCareerSaving(&CAREER), "name cheat uses original save path");
            printf("%s pass\n",what);
        }
        else
        {
            check(CE_CheatRestartRequired(3) && !CE_CheatReady(3) && !CE_CheatOn(3),
                  "failed god setting read latches Maladim unavailable");
            check(CE_CheatOn(0)==1 && !PortCeCareerSaving(&CAREER), "other cheat still works and save leaves setting");
            printf("%s pass\n",what);
        }
    }
    else if (!strcmp(what,"career-godmissing"))
    {
        PortCeCareerLoaded(&CAREER);
        check(!CAREER.GetIsGod(0) && CE_CheatReady(3) && !CE_CheatRestartRequired(3),
              "missing god key is a valid off default");
        check(PortCeCareerSaving(&CAREER), "missing god key can be saved normally");
        puts("career-godmissing pass");
    }
    else if (!strcmp(what,"career-save-read-error") || !strcmp(what,"career-save-read-retry"))
    {
        PortCeCareerLoaded(&CAREER); check(CAREER.GetIsGod(0)==1, "save fault starts from a restored career");
        CAREER.SetIsGod(0,0); fault="read"; partial=0;
        check(PortCeCareerSaving(&CAREER), "save still reports its existing mask decision");
        if (!strcmp(what,"career-save-read-retry"))
        {
            fault=""; partial=0; check(PortCeCareerSaving(&CAREER), "save retry reaches actual settings writer");
        }
        printf("%s pass\n",what);
    }
    else if (!strcmp(what,"career-save-active-failure"))
    {
        PortCeCareerLoaded(&CAREER); check(CAREER.GetIsGod(0)==1, "active-failure starts with live God");
        fault="read";
        for (int i=0;i<2;++i)
        {
            partial=0; char bytes[sizeof(CCareer)]{}; CAREER.SaveFixture(bytes);
            CCareer serialized; memcpy(&serialized,bytes,sizeof(serialized));
            check(!serialized.GetIsGod(0) && CAREER.GetIsGod(0)==1, "serialized career masks God and restores live state");
        }
        puts("career-save-active-failure pass");
    }
    else if (!strcmp(what,"maladim-off"))
    {
        if (argc!=3) return 3;
        PortCeCareerLoaded(&CAREER); check(CAREER.GetIsGod(0)==1, "Maladim off starts from restored God mode");
        CCeCheatSwitch item(L"fixture",3); item.Refresh();
        check(item.mValue==1 && item.mSelection==1, "Maladim switch initially on");
        item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
        item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_UP,1.f);
        fault=argv[2]; item.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
        check(CE_CheatOn(3)==0 && CAREER.GetIsGod(0)==0, "Maladim and live God mode switched off");
        bool failed=strcmp(argv[2],"none")!=0;
        check((message && strstr(message,"Maladim switch is off, but the God setting could not be cleared")) == failed,
              "partial result is reported honestly");
        puts("maladim-off pass");
    }
    else if (!strcmp(what, "career-normal") || !strcmp(what, "career-deliberate-off"))
    {
        PortCeCareerLoaded(&CAREER);
        check(CAREER.GetIsGod(0)==1 && CE_CheatOn(3)==1 && CE_CheatReady(3), "normal load restores god mode");
        if (!strcmp(what,"career-deliberate-off")) CAREER.SetIsGod(0,0);
        check(PortCeCareerSaving(&CAREER), "normal CE save masks cheat-only god flag");
        puts(!strcmp(what,"career-normal") ? "career-normal pass" : "career-deliberate-off pass");
    }
    else if (!strcmp(what, "cheat-write"))
    {
        CCeCheatSwitch item(L"fixture", 0);
        check(item.GetValue() == 0, "empty or absent setting starts off");
        item.SetValue(1);
        check(!message && item.GetValue() == 1, "empty or absent setting can be enabled");
        puts("cheat-write pass");
    }
    else if (!strcmp(what, "cheat-clear"))
    {
        CCeCheatSwitch item(L"fixture", 0);
        check(item.GetValue() == 1, "cheat after unrelated binary is read on");
        item.SetValue(0);
        check(!message && item.GetValue() == 0, "cheat after unrelated binary is cleared");
        puts("cheat-clear pass");
    }
    else if (!strcmp(what, "cli"))
    {
        if (argc != 4) return 3;
        unsigned v[15]; ActualCLIProbe(argv[3], atoi(argv[2]), v);
        printf("%u %u %u %u %u %u %d %d %d\n", v[0], v[1], v[2], v[3], v[4], v[5],
               CE_DisplayModeUseSavedSize(), CE_DisplayModeAvailable(), reads);
    }
    else if (!strcmp(what, "row"))
    {
        if (argc != 3) return 3;
        menus = atoi(argv[2]) != 0;
        List list; NativeVideoRow(&list);
        printf("%zu %d\n", list.mItems.size(), reads);
        for (CMenuItem *p : list.mItems) delete p;
    }
    else if (!strcmp(what, "store"))
    {
        if (argc != 4) return 3;
        int before = CE_DisplayModeChoice(0);
        fault = argv[3];
        int ok = CE_DisplayModeStore(atoi(argv[2]));
        fault = "";
        printf("%d %d %d\n", before, ok, CE_DisplayModeChoice(0));
    }
    else if (!strcmp(what, "menu"))
    {
        if (argc != 3) return 3;
        List list; NativeVideoRow(&list);
        check(list.mItems.size() == 1, "row admitted");
        CMenuMenuItem *item = static_cast<CMenuMenuItem *>(list.mItems[0]);
        check(item->GetNumChoices() == 2 && !wcscmp(item->GetChoiceText(0), L"Fullscreen") &&
              !wcscmp(item->GetChoiceText(1), L"Windowed"), "choice labels");
        check(wcsstr(item->GetText(), L"restart required") != nullptr, "restart label");
        int before = item->GetValue(), live = LT.m_bWindowed;
        int next = 1 - before;
        item->SetSelection(next);
        check(item->HasPendingChange(), "selection deferred");
        CApplyMenuItem apply{&list};
        apply.ReceiveButtonAction(nullptr, 0, 1.f);
        check(item->HasPendingChange() && gMenuApplyCount == 0, "non-apply action leaves selection pending");
        fault = argv[2]; apply.ReceiveButtonAction(nullptr, BUTTON_FRONTEND_MENU_SELECT, 1.f); fault = "";
        check(gMenuApplyCount == 1, "native Apply traversed the list once");
        bool failed = strcmp(argv[2], "none") != 0;
        check(item->GetValue() == (failed ? before : next), "persisted value after commit");
        check(item->mValue == item->GetValue() && item->mSelection == item->GetValue() && !item->HasPendingChange(),
              "native commit and refusal retain consistent selection");
        check(LT.m_bWindowed == live, "live window unchanged");
        check((wcsstr(item->GetText(), L"not saved") != nullptr) == failed, "failure label");
        if (failed)
        {
            item->SetSelection(next); apply.ReceiveButtonAction(nullptr, BUTTON_FRONTEND_MENU_SELECT, 1.f);
            check(item->GetValue() == next && !item->HasPendingChange() &&
                  !wcsstr(item->GetText(), L"not saved"), "retry persists and clears failure label");
        }
        delete item;
        puts("menu pass");
    }
    else return 3;
}
