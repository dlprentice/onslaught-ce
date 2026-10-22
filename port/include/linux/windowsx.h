// SPDX-License-Identifier: GPL-3.0-only
// port/include/linux/windowsx.h: the message-cracker macros the game uses. Linux only.
#ifndef PORT_WINDOWSX_H
#define PORT_WINDOWSX_H
#include <windows.h>
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define ComboBox_GetCurSel(h) ((int)(DWORD)SendMessage((h), CB_GETCURSEL, 0L, 0L))
#define ComboBox_SetCurSel(h, i) ((int)(DWORD)SendMessage((h), CB_SETCURSEL, (WPARAM)(int)(i), 0L))
#define ComboBox_GetCount(h) ((int)(DWORD)SendMessage((h), CB_GETCOUNT, 0L, 0L))
#define ComboBox_ResetContent(h) ((int)(DWORD)SendMessage((h), CB_RESETCONTENT, 0L, 0L))
#define ComboBox_AddString(h, s) ((int)(DWORD)SendMessage((h), CB_ADDSTRING, 0L, (LPARAM)(LPCTSTR)(s)))
#define ComboBox_GetItemData(h, i) ((LRESULT)(ULONG_PTR)SendMessage((h), CB_GETITEMDATA, (WPARAM)(int)(i), 0L))
#define ComboBox_SetItemData(h, i, d) ((int)(DWORD)SendMessage((h), CB_SETITEMDATA, (WPARAM)(int)(i), (LPARAM)(d)))
#define Button_GetCheck(h) ((int)(DWORD)SendMessage((h), BM_GETCHECK, 0L, 0L))
#define Button_SetCheck(h, c) ((void)SendMessage((h), BM_SETCHECK, (WPARAM)(int)(c), 0L))
#endif
