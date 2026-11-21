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
extern "C" int PortPublicDeviceBusy(void);	// the public graphics host: a device change is running or waiting to be retried
namespace
{
    int pendingMode = -1;
    enum DisplayResult { DisplayOk, DisplayRefused, DisplayNotSaved, DisplayInputFailed, DisplayRestoreFailed };
    DisplayResult displayResult = DisplayOk;
}

// Menu Apply visits every item before returning. Reset only after that traversal and the frame's Present,
// so device-object callbacks cannot run while an item is being committed or drawn.
// One Apply can also ask for the options reset (Screen mode, anti-aliasing and the other reset rows). That reset
// draws wait screens, and each of their presents ends in this call: the request then stays queued for the present
// that started the reset, so the window is prepared from the mode that reset installed. After a reset that failed
// it stays queued until the shell has recovered the device and a frame is presented again.
void PortCeApplyPendingDisplayMode()
{
    if (pendingMode >= 0 && PortPublicDeviceBusy()) return;
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
    // The label never changes, as the original's never do ("Label:value", no space): a state follows the value in effect.
    WCHAR *GetText() override { return const_cast<WCHAR *>(L"Window mode:"); }
    int GetNumChoices() override { return 2; }
    WCHAR *GetChoiceText(int value) override {
        const bool windowed = value == 1;
        if (value == GetValue())
        {
#if defined(CE_VERSION) && defined(BEA_PUBLIC_GRAPHICS_HOST)
            // Refused: the window is as it was. Not saved: switched for this session only. The two restore failures leave the window
            // or the mouse in a state only a new start puts right (the mouse's repair is retried, but the choice was not saved either).
            if (displayResult == DisplayRefused) return const_cast<WCHAR *>(windowed ? L"Windowed (not changed)" : L"Fullscreen (not changed)");
            if (displayResult == DisplayNotSaved) return const_cast<WCHAR *>(windowed ? L"Windowed (not saved)" : L"Fullscreen (not saved)");
            if (displayResult == DisplayInputFailed || displayResult == DisplayRestoreFailed)
                return const_cast<WCHAR *>(windowed ? L"Windowed (restart the game)" : L"Fullscreen (restart the game)");
#else
            // This build changes the mode at the next start only; a refused save leaves the saved choice as it was.
            if (failed) return const_cast<WCHAR *>(windowed ? L"Windowed (not changed)" : L"Fullscreen (not changed)");
#endif
        }
#if defined(CE_VERSION) && defined(BEA_PUBLIC_GRAPHICS_HOST)
        return const_cast<WCHAR *>(windowed ? L"Windowed" : L"Fullscreen");
#else
        return const_cast<WCHAR *>(windowed ? L"Windowed (next start)" : L"Fullscreen (next start)");
#endif
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
