// Exercise the actual native page callbacks with authored controller inputs.
// This checks row/action coherence, not rendering or storage behavior.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../../game/ce_backup_preview.cpp"

namespace {
CePreviewDisplay suppliedPreview{};
CeRestoreDisplay suppliedRestore{};
unsigned chooseCalls, againCalls, setCalls, lastSet, beginCalls, confirmCalls,
         cancelCalls, acknowledgeCalls;
unsigned checks;
void check(bool ok) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "page check %u failed\n", checks);
        std::exit(1);
    }
}
std::string narrow(const WCHAR *s) {
    std::string out;
    while (*s) out += static_cast<char>(*s++);
    return out;
}
void resetCalls() {
    chooseCalls = againCalls = setCalls = beginCalls = confirmCalls =
        cancelCalls = acknowledgeCalls = 0;
}
int findRow(const char *text, bool right = false) {
    for (int i = 0; i < page.mCount(); ++i) {
        SCeListRow r{};
        page.mRow(i, &r);
        if (narrow(right ? r.mRight : r.mLeft) == text) return i;
    }
    return -1;
}
void process() { page.mProcess(); }
}

void CePreviewOpen(const char *, const char *, const char *) {}
void CePreviewLeave() {}
void CePreviewChoose() { ++chooseCalls; }
void CePreviewAgain() { ++againCalls; }
void CePreviewSelectSet(unsigned i) { ++setCalls; lastSet = i; }
void CePreviewDisplayRead(unsigned, CePreviewDisplay *out) { *out = suppliedPreview; }
bool CePreviewDisplaySetName(unsigned i, char *out, unsigned n) {
    if (i >= static_cast<unsigned>(suppliedPreview.choices)) return false;
    std::snprintf(out, n, "Authored backup %u", i + 1);
    return true;
}
// Authored list: a full set first, then a per-career set, then one that did not verify.
bool CePreviewDisplaySetHolds(unsigned i, CePreviewSetHolds *out) {
    std::memset(out, 0, sizeof(*out));
    if (i >= static_cast<unsigned>(suppliedPreview.choices)) return false;
    static const CePreviewSetHolds rows[] = {{1, 3, 1, "All careers"},
                                             {1, 1, 0, "Manual: selected CE career"},
                                             {0, 0, 0, ""}};
    *out = rows[i < 3 ? i : 2];
    return true;
}
std::string hintOf(int row) {
    WCHAR help[200]{}, extra[200]{};
    page.mHint(row, help, extra, 200);
    return narrow(help) + "|" + narrow(extra);
}
void CeRestoreUiOpen() {}
void CeRestoreUiLeave() {}
bool CeRestoreUiBegin(unsigned, const CeRestoreAuthority *) { ++beginCalls; return true; }
bool CeRestoreUiCancel() { ++cancelCalls; return true; }
bool CeRestoreUiConfirm(const CeRestoreAuthority *) { ++confirmCalls; return true; }
void CeRestoreUiDrawn(unsigned) {}
void CeRestoreUiPump(unsigned, const CeRestoreAuthority *) {}
bool CeRestoreUiAcknowledge() { ++acknowledgeCalls; return true; }
void CeRestoreUiRead(unsigned, CeRestoreDisplay *out) { *out = suppliedRestore; }
bool PortCeRestoreNamesCompatible(const char *, const char *) { return true; }
bool PortCeProjectCareerAllowed() noexcept { return true; }
bool PortCeMenus() { return true; }
const char *PortUserDir() { return "authored-user"; }
const char *PortGameDir() { return "authored-game"; }
extern "C" unsigned PortPresentCount() { return 0; }
extern "C" int CE_SettingGetText(const char *, char *out, int n) {
    if (n > 0) out[0] = 0;
    return 0;
}
void PortCeBackupPreviewBack() {}
void PortCeExtrasShow(int, int) {}
void PortCeExtrasInFont(WCHAR *text) {
    if (!text) return;
    for (; *text; ++text) if (*text == L'_') *text = L'-';
}
void PortCeExtrasWiden(WCHAR *out, int n, const char *in) {
    if (n <= 0) return;
    int i = 0;
    while (i + 1 < n && in[i]) { out[i] = static_cast<unsigned char>(in[i]); ++i; }
    out[i] = 0;
}
void PortCeExtrasWidenName(WCHAR *out, int n, const char *in) {
    PortCeExtrasWiden(out, n, in);
}

int main() {
    suppliedPreview.phase = CE_PREVIEW_IDLE;
    suppliedPreview.choices = 3;
    suppliedRestore.phase = CE_RESTORE_IDLE;
    page.mOpen();
    process();
    const int choose = findRow("Choose Backup Set");
    const int again = findRow("Verify Again");
    const int firstSet = findRow("All careers", true);
    check(choose >= 0 && again >= 0 && firstSet >= 0);
    // Each listed set says what it holds: on its row, and in full in its hint.
    check(findRow("One career", true) == 1 && findRow("Not verified", true) == 2);
    check(hintOf(firstSet) == "All careers and options, 3 careers.|Made with Back Up All Careers.");
    check(hintOf(1) == "One career, made with Back Up Career.|Selecting a set restores nothing.");
    check(hintOf(2) == "This set did not verify. Select to see why.|Selecting a set restores nothing.");
    check(firstSet == 0 && findRow("Back") == page.mCount() - 1);	// the sets first, Back last
    resetCalls(); page.mSelect(choose);
    check(chooseCalls == 1 && againCalls == 0 && setCalls == 0);
    resetCalls(); page.mSelect(again);
    check(againCalls == 0 && chooseCalls == 0 && setCalls == 0);
    resetCalls(); page.mSelect(firstSet);
    check(setCalls == 1 && lastSet == 0 && chooseCalls == 0 && againCalls == 0);
    resetCalls(); page.mSelect(firstSet + 1);
    check(setCalls == 1 && lastSet == 1 && chooseCalls == 0 && againCalls == 0);

    suppliedPreview.phase = CE_PREVIEW_VERIFIED;
    suppliedPreview.careers = 1;
    suppliedPreview.members = 1;
    std::strcpy(suppliedPreview.name, "Amber.bes");
    suppliedRestore.offer = CE_RESTORE_OFFER_READY;
    process();
    const int againOnCard = findRow("Verify Again");	// rows move once a card is shown
    resetCalls(); page.mSelect(againOnCard);
    check(againCalls == 1 && chooseCalls == 0 && setCalls == 0);
    const int date = findRow("Backup date");
    const int restore = findRow("Restore as New");
    check(date >= 0 && restore == 0 && findRow("Back") == page.mCount() - 1);
    resetCalls(); page.mSelect(date);
    check(chooseCalls == 0 && againCalls == 0 && setCalls == 0 && beginCalls == 0);
    resetCalls(); page.mSelect(restore);
    check(beginCalls == 1 && chooseCalls == 0 && againCalls == 0 && setCalls == 0);

    suppliedRestore.phase = CE_RESTORE_CONFIRM;
    std::strcpy(suppliedRestore.source, "Amber.bes");
    std::strcpy(suppliedRestore.target, "Amber restored.bes");
    process();
    // Cancel is the last row of every step (as on the game's own pages); the action comes first.
    const int cancel = findRow("Cancel");
    check(cancel == page.mCount() - 1 && findRow("Restore as New") == 0);
    resetCalls(); page.mSelect(cancel);
    check(cancelCalls == 1 && confirmCalls == 0);
    unsigned confirmations = 0;
    for (int i = 0; i < page.mCount(); ++i) {
        if (i == cancel) continue;
        resetCalls(); page.mSelect(i);
        confirmations += confirmCalls;
        check(chooseCalls == 0 && againCalls == 0 && setCalls == 0 && beginCalls == 0);
    }
    check(confirmations == 1);

    suppliedRestore.phase = CE_RESTORE_WORKING;
    process(); resetCalls();
    for (int i = 0; i < page.mCount(); ++i) page.mSelect(i);
    check(chooseCalls == 0 && againCalls == 0 && setCalls == 0 && beginCalls == 0 &&
          confirmCalls == 0 && cancelCalls == 0 && acknowledgeCalls == 0);
    // Esc/B steps back one restore sub-state instead of exiting straight to Extras Hub.
    check(PortCeBackupPreviewStepBack() == -1 && cancelCalls == 0 && acknowledgeCalls == 0);
    suppliedRestore.phase = CE_RESTORE_CONFIRM;
    process(); resetCalls();
    check(PortCeBackupPreviewStepBack() == 1 && cancelCalls == 1);
    suppliedRestore.phase = CE_RESTORE_PROPOSING;
    process(); resetCalls();
    check(PortCeBackupPreviewStepBack() == 1 && cancelCalls == 1);
    suppliedRestore.phase = CE_RESTORE_DONE;
    process(); resetCalls();
    check(PortCeBackupPreviewStepBack() == 1 && acknowledgeCalls == 1);
    suppliedRestore.phase = CE_RESTORE_IDLE;
    suppliedPreview.careers = 2;
    std::strcpy(suppliedPreview.name, "Pilot_One.bes");
    process();
    check(findRow("Pilot-One.bes") >= 0);
    check(PortCeBackupPreviewStepBack() == 0);
    page.mSelect(findRow("Restore as New"));
    check(PortCeBackupPreviewStepBack() == 1);
    check(PortCeBackupPreviewStepBack() == 0);
    std::printf("{\"ok\":true,\"checks\":%u}\n", checks);
}
