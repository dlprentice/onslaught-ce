// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_PLAYER_CONNECTION_ENTRY_STUBS_H
#define CE_PLAYER_CONNECTION_ENTRY_STUBS_H

// Authored menu boundaries only. No game headers, data, rendering or windowing.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>
#include <vector>

using WCHAR = wchar_t;
using WORD = unsigned short;
using DWORD = unsigned int;
using SINT = int;
using BOOL = int;
enum { FALSE = 0, TRUE = 1 };
enum EMemoryType { FIXTURE_MEMORY };
enum EPCText { FIXTURE_TEXT };
enum EFontType { FONT_SMALL, FONT_NORMAL };
enum KeyEventType { KEY_EVENT_DOWN, KEY_EVENT_UP, KEY_EVENT_CHAR };
using pKeyTrapper = void (*)(WORD, KeyEventType);
enum {
    PORT_CE_NO_LISTS, FEP_LANGUAGE_TEST, FES_BACK, FES_SELECT,
    BUTTON_PAUSE_MENU_SELECT, BUTTON_FRONTEND_MENU_BACK,
    BUTTON_FRONTEND_MENU_UP, BUTTON_FRONTEND_MENU_DOWN,
    DIK_LCONTROL, DIK_RCONTROL, DIK_UP, DIK_DOWN, DIK_TAB, DIK_ESCAPE,
    DIK_RETURN, DIK_NUMPADENTER, DIK_BACK, DIK_V,
    D3DRS_SRCBLEND, D3DBLEND_SRCALPHA, D3DRS_DESTBLEND,
    D3DBLEND_INVSRCALPHA, TSS_ADDRESSU, TADDRESS_CLAMP, TSS_ADDRESSV,
    D3DRS_ZFUNC, D3DCMP_LESSEQUAL, D3DTSS_COLORARG1, D3DTA_TEXTURE,
    D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_COLOROP, D3DTOP_MODULATE,
    D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHABLENDENABLE,
    D3DRS_ZWRITEENABLE, D3DRS_ZENABLE, WM_CLOSE
};

inline void* operator new(std::size_t size, EMemoryType) { return ::operator new(size); }
inline void operator delete(void* value, EMemoryType) noexcept { ::operator delete(value); }
#define SAFE_DELETE(value) do { delete (value); (value) = nullptr; } while (0)
#define CE_NETWORK_ENABLED 1

inline float fixture_time = 5.f;
inline int fixture_window_closes = 0;
inline std::vector<std::wstring> fixture_drawn;
struct SIZE { int cx = 0, cy = 0; };
struct SWordWrapFill { WCHAR c[1][600] = {}; };
struct CDXBitmapFont {
    bool CanDisplay(WCHAR c) { return c >= 32 && c <= 126; }
    int GetHeight() { return 18; }
    int WordWrap(SWordWrapFill* fill, const WCHAR* text, float) {
        std::wcsncpy(fill->c[0], text, 599); fill->c[0][599] = 0; return 1;
    }
    void GetTextExtent(const WCHAR* text, SIZE* size) {
        size->cx = int(std::wcslen(text)) * 8; size->cy = 18;
    }
    void DrawTextWithShadow(float, float, DWORD, const WCHAR* text, int, int, float, float) {
        fixture_drawn.emplace_back(text);
    }
};
struct FixturePlatform {
    CDXBitmapFont font;
    CDXBitmapFont* Font(EFontType) { return &font; }
    float GetSysTimeFloat() { return fixture_time; }
};
inline FixturePlatform PLATFORM;
struct CController {};
template<class T> struct FixtureList {
    std::vector<T> values;
    int Size() const { return int(values.size()); }
    T At(int index) { return values.at(std::size_t(index)); }
    void Append(T value) { values.push_back(value); }
};
struct CMenuItem {
    int mId;
    BOOL mEnabled = TRUE;
    CMenuItem(EPCText, int id) : mId(id) {}
    virtual ~CMenuItem() = default;
    virtual WCHAR* GetText() { return nullptr; }
    virtual int GetHeight() { return 18; }
    virtual void Render(float, float, BOOL) {}
    bool IsEnabled() const { return mEnabled != FALSE; }
};
struct CMenuOptionsList {
    FixtureList<CMenuItem*> mItems;
    float mY;
    int mSelected = 0;
    CMenuOptionsList(WCHAR*, float, float y, BOOL, DWORD) : mY(y) {}
    ~CMenuOptionsList() { for (auto item : mItems.values) delete item; }
    void AddItem(CMenuItem* item) { mItems.Append(item); }
    void Reset() {}
    bool PassToSelected(CController*, int, float) { return false; }
    void ReceiveButtonAction(CController*, int button, float) {
        if (button == BUTTON_FRONTEND_MENU_DOWN) mSelected = (mSelected + 1) % mItems.Size();
        if (button == BUTTON_FRONTEND_MENU_UP) mSelected = (mSelected + mItems.Size() - 1) % mItems.Size();
    }
    void Render(void*) {}
};
struct CPauseMenu {
    inline static bool mOptionsPage = false;
    FixtureList<CMenuOptionsList*> mLists;
    int mPage = 0;
    bool mActive = false;
    float mOpenTime = 0.f;
    explicit CPauseMenu(int) {}
    virtual ~CPauseMenu() { for (auto list : mLists.values) delete list; }
    virtual void ReceiveButtonAction(CController*, int, float) {}
    void Activate(int) { mActive = true; mOpenTime = 0.f; }
    void DeActivate(BOOL) { mActive = false; }
};
struct FixtureShell {
    pKeyTrapper trap = nullptr;
    void SetKeytrap(pKeyTrapper value) { trap = value; }
    pKeyTrapper GetKeytrap() { return trap; }
    void* GetHWnd() { return nullptr; }
    void SRS(int, int) {}
    void STS(int, int, int) {}
};
inline FixtureShell LT;
struct FixtureRenderInfo {
    void SetFogEnabled(bool) {}
    void SetUnknown2ed(bool) {}
    void STS(int, int, int) {}
    void Apply() {}
};
inline FixtureRenderInfo RENDERINFO;
struct CFrontEnd {
    static void PlaySound(int) {}
    void SetPage(int, int) {}
};
inline CFrontEnd FRONTEND;
struct FixtureCli {
    BOOL mRecordDemo = FALSE, mPlaybackDemo = FALSE, mAutoConfigTest = FALSE, mAttractMode = FALSE;
};
inline FixtureCli CLIPARAMS;
inline void PostMessage(void*, int, int, int) { ++fixture_window_closes; }

#endif
