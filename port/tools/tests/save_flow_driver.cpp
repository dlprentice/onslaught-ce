// SPDX-License-Identifier: GPL-3.0-only
// Driver for port/tools/save_flow_tests.py. The save page's own functions (ButtonPressed, Process, CreateSave, the overwrite
// question), the PC memory card (PCMemoryCard.cpp, compiled as the game compiles it) and the Linux file layer are the real
// sources, before and after patches 0117 and 0118, over authored folders. Only the surroundings are authored: the front end,
// the message box, the career's bytes and the text table record what the player would see. No game data, no real save.
// Compiled as a game file (port.h forced in) without the standard library's containers.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PC 1
#define XBOX 2
#define PS2 3
#define TARGET PC
#define MEMORYCARD RealMemoryCardDeclaration
#include "MemoryCard.h"
#undef MEMORYCARD

typedef unsigned char BYTE;
typedef int SINT;
typedef int EFEPState;
typedef int EFETextHack;
#include "ui_constants.inc"

#define ASSERT(c) ((void)0)
#define TRACE(...) ((void)0)
#define FO(v) v
#define MEMTYPE_SCRATCHPAD 1
#define SAFE_DELETE(p) delete[] (p)
void *operator new[](size_t n, int) { return ::operator new[](n); }
extern "C" FILE *port_fopen(const char *, const char *);
extern "C" unsigned PortSaveApproveOverwrite(const void *, const void *);		// platform/linux/save_overwrite.cpp
extern "C" int PortSaveConsumeOverwrite(const void *);
extern "C" void PortSaveClearOverwrite();

// --- what the player would have seen ------------------------------------------------------------------------------------
static char events[64][96];
static int eventCount;
static void Event(const char *fmt, const char *a = "", int b = 0)
{
    if (eventCount < 64) snprintf(events[eventCount++], sizeof(events[0]), fmt, a, b);
}
static const char *Narrow(const WCHAR *w)
{
    static char text[96];
    int i = 0;
    for (; w && w[i] && i < 95; i++) text[i] = (char)w[i];
    text[i] = 0;
    return text;
}
static WCHAR *Text(int id)
{
    static WCHAR pool[8][96];
    static int next;
    WCHAR *w = pool[next++ & 7];
    const char *name = NameOf(id);
    int i = 0;
    for (; name[i] && i < 95; i++) w[i] = (WCHAR)name[i];
    w[i] = 0;
    return w;
}
WCHAR *Localization_GetStringById(int id) { return Text(1000 + id); }
#include CONVERTER_SOURCE

struct TextBoundary { WCHAR *GetText(int id, int = 0) { return Text(id); } };

static unsigned char careerBytes[1 << 16];
static int careerSize;
struct CareerBoundary
{
    int SizeOfSaveGame() { return careerSize; }
    int InProgress() { return 1; }
    void SaveAndMarkInProgress(char *dest) { memcpy(dest, careerBytes, careerSize); }
} CAREER;

struct FrontendBoundary
{
    enum { AUTO_SAVE_NOT, AUTO_SAVE_NORMAL, AUTO_SAVE_PRETEND };
    int autosave = AUTO_SAVE_NOT;
    TextBoundary mFrontEndText;
    struct { bool m_Fresh = true; } mFEPVirtualKeyboard;
    int GetMemoryCardNumber() { return 0; }
    int GetAutoSave() { return autosave; }
    void SetAutoSave(int v) { autosave = v; Event("autosave-armed"); }
    void SetSaveMode(BOOL) {}
    int pageChanges = 0;
    void SetPage(int page, int) { pageChanges++; Event("page:%s", PageName(page)); }
    int GetSuccessPage() { return FEP_SUCCESS_PAGE; }
    int GetSuccessTransTime() { return 0; }
    void StartMCOperation(WCHAR *) {}
    void EndMCOperation() {}
    void PlaySound(int) {}
    static const char *PageName(int page) { return page == FEP_SUCCESS_PAGE ? "success" : page == FEP_VIRTUAL_KEYBOARD ? "keyboard" : page == FEP_MAIN ? "main" : "other"; }
} FRONTEND;
typedef FrontendBoundary CFrontEnd;
#define FET FRONTEND.mFrontEndText

struct MessboxBoundary
{
    bool displayed = false;
    int question = 0, choice = 0, created = 0;
    bool BeingDisplayed() { return displayed; }
    int GetQuestion() { return question; }
    int GetFinalChoice() { return choice; }
    void Kill() { displayed = false; question = 0; choice = 0; }
    void SetOnDismiss(int, int) {}
    // (x, y, width, height, text, font, time, speed, ?, options, question)
    void Create(float, float, float, int, WCHAR *text, int, int, int, BOOL, int, int q)
    {
        displayed = true; question = q; choice = 0; created++;
        if (q == FEMB_Q_OVERWRITE) Event("question:overwrite");
        else Event("message:%s", Narrow(text));
    }
} FEMESSBOX;
struct { int Font(int) { return 0; } } PLATFORM;
struct { void SetFogEnabled(bool) {} } RENDERINFO;

char gSaveFromOptions;
struct CFEPSaveGame
{
    WCHAR mSaveGameName[256] = {}, mExistingSaveName[256] = {};
    int mExistingSave = -1, mCX = 0, mCY = 0;
    BOOL mCareerWasInProgress = 0;
    float mStartTime = 0;
    void Process(EFEPState);
    void CreateSave();
    void ButtonPressed(SINT, float);
    static void RemovedMUWhinge(EFETextHack reason);
    void AskIfYouWantToDelete(BOOL career_in_progress, BOOL because_4096, BOOL no_space_for_bea);
};

// --- the real card, counting what the page asks of it ------------------------------------------------------------------
static int createCalls, writeCalls, deleteCalls;
struct CardBoundary : CPCMemoryCard
{
    int CreateSave(int card, WCHAR *name, int *save, BOOL overwrite) { createCalls++; return CPCMemoryCard::CreateSave(card, name, save, overwrite); }
    int WriteSave(int card, int save, WCHAR *name, void *data, int size) { writeCalls++; return CPCMemoryCard::WriteSave(card, save, name, data, size); }
    int DeleteSave(int card, int save, WCHAR *name) { deleteCalls++; return CPCMemoryCard::DeleteSave(card, save, name); }
} MEMORYCARD;

#include UI_SOURCE

// the options file (FEPOptions.cpp's WriteDefaultOptionsFile) and the setup history it reports to
struct { void AddMessage(const char *m) { Event("setup-history:%s", m); } } SETUP_HISTORY;
struct CFEPOptions { static void WriteDefaultOptionsFile(char *data, int size); };
#include OPTIONS_SOURCE

// --- platform boundary ------------------------------------------------------------------------------------------------------
#include "handles.h"
PortHandle *PortHandleNew(PortHandleKind kind, size_t size) { PortHandle *h = (PortHandle *)calloc(1, size); h->kind = kind; return h; }
void PortHandleFree(PortHandle *h) { free(h); }
PortHandle *PortCheck(HANDLE h, int kind) { PortHandle *p = (PortHandle *)h; return p && p->kind == kind ? p : NULL; }
void PortSetLastError(DWORD) {}
extern "C" int PortReplaceUserFile(const char *, const void *, unsigned, const char *);

static bool Slurp(const char *path, unsigned char *out, int *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    *size = (int)fread(out, 1, sizeof(careerBytes), f);
    fclose(f);
    return true;
}
static void Wide(WCHAR *out, const char *s) { int i = 0; for (; s[i]; i++) out[i] = (WCHAR)(unsigned char)s[i]; out[i] = 0; }

// save-page <name> <answers: Y/N letters, e.g. YY> <autosave: not|normal> <career bytes file>
//   runs frames of the save page; every overwrite question is answered with the next letter. Prints the player's view.
// card <op> ...: card operations with their results.
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    PortPathsInit();
    if (!strcmp(argv[1], "save-page") && argc == 6)
    {
        if (!Slurp(argv[5], careerBytes, &careerSize)) return 2;
        CFEPSaveGame page;
        Wide(page.mSaveGameName, argv[2]);
        FRONTEND.autosave = !strcmp(argv[4], "normal") ? FrontendBoundary::AUTO_SAVE_NORMAL : FrontendBoundary::AUTO_SAVE_NOT;
        const char *answers = argv[3];
        for (int frame = 0; frame < 12 && !FRONTEND.pageChanges; frame++)
        {
            int createdBefore = FEMESSBOX.created;
            bool answered = FEMESSBOX.choice != 0;
            page.Process(FEPS_ACTIVE);
            if (answered && FEMESSBOX.created == createdBefore)
                FEMESSBOX.Kill();                      // the answered box fades away; a new one replaces it instead
            if (FEMESSBOX.displayed && FEMESSBOX.question == FEMB_Q_OVERWRITE && !FEMESSBOX.choice && *answers && *answers != '-')
                FEMESSBOX.choice = *answers++ == 'Y' ? FEMB_YES : FEMB_NO;
        }
        for (int i = 0; i < eventCount; i++) printf("EVENT %s\n", events[i]);
        printf("COUNT create=%d write=%d delete=%d\n", createCalls, writeCalls, deleteCalls);
        return 0;
    }
    // resume-like <name> <file>: what CPauseMenu::Resume and the language change do with a career: CreateSave(.., TRUE), then
    // WriteSave, and nothing is shown to the player whatever happens.
    if (!strcmp(argv[1], "resume-like") && argc == 4)
    {
        if (!Slurp(argv[3], careerBytes, &careerSize)) return 2;
        WCHAR name[64];
        Wide(name, argv[2]);
        int save = -1;
        int created = MEMORYCARD.CreateSave(0, name, &save, TRUE);
        int written = created == MCE_SUCCESS ? MEMORYCARD.WriteSave(0, save, name, careerBytes, careerSize) : -1;
        printf("create=%d write=%d\n", created, written);
        return 0;
    }
    if (!strcmp(argv[1], "options") && argc == 3)
    {
        if (!Slurp(argv[2], careerBytes, &careerSize)) return 2;
        CFEPOptions::WriteDefaultOptionsFile((char *)careerBytes, careerSize);
        for (int i = 0; i < eventCount; i++) printf("EVENT %s\n", events[i]);
        return 0;
    }
    // approvals <script>: A:requested:existing approves, C:name consumes, X clears; prints one result per step
    if (!strcmp(argv[1], "approvals") && argc == 3)
    {
        char script[512];
        snprintf(script, sizeof(script), "%s", argv[2]);
        for (char *tok = strtok(script, " "); tok; tok = strtok(NULL, " "))
        {
            WCHAR a[300] = {0}, b[300] = {0};
            if (tok[0] == 'A')
            {
                char *x = strchr(tok + 2, ':');
                if (!x) return 2;
                *x = 0;
                Wide(a, tok + 2);
                Wide(b, x + 1);
                printf("A=%u\n", PortSaveApproveOverwrite(a, b));
            }
            else if (tok[0] == 'C') { Wide(a, tok + 2); printf("C=%d\n", PortSaveConsumeOverwrite(a)); }
            else if (tok[0] == 'X') { PortSaveClearOverwrite(); printf("X\n"); }
        }
        return 0;
    }
    if (!strcmp(argv[1], "card") && argc >= 4)
    {
        WCHAR name[64];
        Wide(name, argv[3]);
        const char *op = argv[2];
        if (!strcmp(op, "delete")) { printf("delete=%d\n", MEMORYCARD.DeleteSave(0, 0, name)); return 0; }
        if (!strcmp(op, "create") && argc == 5)       // create <name> <overwrite 0|1>
        {
            int save = -2, r = MEMORYCARD.CreateSave(0, name, &save, atoi(argv[4]));
            printf("create=%d save=%d\n", r, save);
            return 0;
        }
        if (!strcmp(op, "write") && argc == 5)        // write <name> <file>
        {
            if (!Slurp(argv[4], careerBytes, &careerSize)) return 2;
            printf("write=%d\n", MEMORYCARD.WriteSave(0, 0, name, careerBytes, careerSize));
            return 0;
        }
        if (!strcmp(op, "read") && argc == 5)         // read <name> <size>
        {
            int want = atoi(argv[4]), got = -1;
            int r = MEMORYCARD.ReadSave(0, 0, name, careerBytes, want, &got);
            printf("read=%d bytes=%d\n", r, got);
            if (r == 0) fwrite(careerBytes, 1, want, stderr);
            return 0;
        }
        if (!strcmp(op, "list"))
        {
            int n = -1;
            MEMORYCARD.GetNumSaves(0, &n);
            printf("saves=%d\n", n);
            for (int i = 0; i < n; i++) { WCHAR w[256]; MEMORYCARD.GetSaveName(0, i, w); printf("name=%s\n", Narrow(w)); }
            return 0;
        }
    }
    return 2;
}
