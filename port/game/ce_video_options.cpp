// SPDX-License-Identifier: GPL-3.0-only
// The native video list owns this CE-only item. Public CE applies it at the post-Present boundary.
#include "common.h"
#include "MenuItem.h"
#include "ltshell.h"

extern "C" int CE_DisplayModeChoice(int);
extern "C" int CE_DisplayModeStore(int);
extern "C" int CE_DisplayModeAvailable();

#if defined(CE_VERSION) && defined(BEA_PUBLIC_GRAPHICS_HOST)
extern "C" int PortCeDisplayWindowBegin(HWND, int, unsigned, unsigned);
extern "C" int PortCeDisplayWindowReady();
extern "C" int PortCeDisplayInputReady();
extern "C" int PortCeDisplayWindowEnd(int);
namespace
{
    int pendingMode = -1;
    enum DisplayResult { DisplayOk, DisplayRefused, DisplayNotSaved, DisplayInputFailed, DisplayRestoreFailed };
    DisplayResult displayResult = DisplayOk;
}

// Menu Apply visits every item before returning. Reset only after that traversal and the frame's Present,
// so device-object callbacks cannot run while an item is being committed or drawn.
void PortCeApplyPendingDisplayMode()
{
    const int requested = pendingMode;
    pendingMode = -1;
    if (requested < 0 || !CE_DisplayModeAvailable()) return;
    const int previous = LT.m_bWindowed ? 1 : 0;
    if (requested == previous)
    {
        displayResult = !PortCeDisplayInputReady() ? DisplayInputFailed :
                        CE_DisplayModeStore(requested) ? DisplayOk : DisplayNotSaved;
        return;
    }
    const int prepared = PortCeDisplayWindowBegin(LT.GetHWnd(), requested,
                                                 LT.m_d3dsdBackBuffer.Width, LT.m_d3dsdBackBuffer.Height);
    if (prepared <= 0)
    {
        displayResult = prepared < 0 ? DisplayRestoreFailed : DisplayRefused;
        return;
    }
    if (FAILED(LT.ForceWindowed(requested)))
    {
        const bool windowRestored = PortCeDisplayWindowEnd(0) != 0;
        displayResult = LT.m_bReady && windowRestored ? DisplayRefused : DisplayRestoreFailed;
        return;
    }
    // The presentation is already working. An input or persistence refusal must not begin a second graphics
    // transaction: keep the coherent live mode, report the unsaved preference, and let the input owner retry.
    const bool inputReady = PortCeDisplayWindowReady() != 0 && PortCeDisplayInputReady() != 0;
    const bool saved = inputReady && CE_DisplayModeStore(requested);
    displayResult = !PortCeDisplayWindowEnd(1) ? DisplayRestoreFailed :
                    !inputReady ? DisplayInputFailed : saved ? DisplayOk : DisplayNotSaved;
}
#endif

class CCeDisplayModeItem : public CMenuMenuItem
{
    bool failed;
public:
    CCeDisplayModeItem() : CMenuMenuItem((EPCText)0,3,true), failed(false) { Refresh(); }
    WCHAR *GetText() override {
#if defined(CE_VERSION) && defined(BEA_PUBLIC_GRAPHICS_HOST)
        const WCHAR *text = L"Window mode: ";
        if (displayResult == DisplayRefused) text = L"Window mode (not applied): ";
        else if (displayResult == DisplayNotSaved) text = L"Window mode (applied; not saved): ";
        else if (displayResult == DisplayInputFailed) text = L"Window mode (input restore failed): ";
        else if (displayResult == DisplayRestoreFailed) text = L"Window mode (restore failed): ";
        return const_cast<WCHAR *>(text);
#else
        return const_cast<WCHAR *>(failed ? L"Display mode (restart required; not saved): " : L"Display mode (restart required): ");
#endif
    }
    int GetNumChoices() override { return 2; }
    WCHAR *GetChoiceText(int value) override {
        return const_cast<WCHAR *>(value == 1 ? L"Windowed" : L"Fullscreen");
    }
    int GetValue() override {
#if defined(CE_VERSION) && defined(BEA_PUBLIC_GRAPHICS_HOST)
        return pendingMode >= 0 ? pendingMode : (LT.m_bWindowed ? 1 : 0);
#else
        return CE_DisplayModeChoice(LT.m_bWindowed);
#endif
    }
    void SetValue(int value) override {
#if defined(CE_VERSION) && defined(BEA_PUBLIC_GRAPHICS_HOST)
        if (!CE_DisplayModeAvailable() || (value != 0 && value != 1))
        {
            mSelection = mValue = GetValue();
            return;
        }
        pendingMode = value;
        displayResult = DisplayOk;
#else
        failed = !CE_DisplayModeStore(value);
        // Commit's native caller also assigns mValue from mSelection. Restore both on a refused write.
        if (failed) mSelection = mValue = GetValue();
#endif
    }
};

CMenuItem *PortCeDisplayModeItem() { return new((EMemoryType)0x80) CCeDisplayModeItem; }
