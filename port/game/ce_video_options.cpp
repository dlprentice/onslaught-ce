// SPDX-License-Identifier: GPL-3.0-only
// The native video list owns this CE-only item. It changes next-start ce.ini preference, never live device state.
#include "common.h"
#include "MenuItem.h"
#include "ltshell.h"

extern "C" int CE_DisplayModeChoice(int);
extern "C" int CE_DisplayModeStore(int);

class CCeDisplayModeItem : public CMenuMenuItem
{
    bool failed;
public:
    CCeDisplayModeItem() : CMenuMenuItem((EPCText)0,3,true), failed(false) { Refresh(); }
    WCHAR *GetText() override {
        return const_cast<WCHAR *>(failed ? L"Display mode (restart required; not saved): " : L"Display mode (restart required): ");
    }
    int GetNumChoices() override { return 2; }
    WCHAR *GetChoiceText(int value) override {
        return const_cast<WCHAR *>(value == 1 ? L"Windowed" : L"Fullscreen");
    }
    int GetValue() override { return CE_DisplayModeChoice(LT.m_bWindowed); }
    void SetValue(int value) override {
        failed = !CE_DisplayModeStore(value);
        // Commit's native caller also assigns mValue from mSelection. Restore both on a refused write.
        if (failed) mSelection = mValue = GetValue();
    }
};

CMenuItem *PortCeDisplayModeItem() { return new((EMemoryType)0x80) CCeDisplayModeItem; }
