// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/recorder.cpp: the AVI recorder's Video for Windows calls (ltshell.cpp) and the import stand-in
// src/avirecorder.c references (GetSaveFileNameA, ICSendMessage; unreferenced code that only keeps retail's import
// table). The port records nothing: every call fails, as with no codec installed. Linux only.
#include <windows.h>
#include <vfw.h>

extern "C" {
HRESULT WINAPI AVIStreamWrite(PAVISTREAM, LONG, LONG, LPVOID, LONG, DWORD, LONG *, LONG *) { return E_FAIL; }
BOOL WINAPI GetSaveFileNameA(void *) { return FALSE; }
LRESULT WINAPI ICSendMessage(void *, UINT, DWORD_PTR, DWORD_PTR) { return 0; }
}
