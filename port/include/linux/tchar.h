// SPDX-License-Identifier: GPL-3.0-only
// port/include/linux/tchar.h: the game is built without UNICODE, so TCHAR is char (as in retail). Linux only.
#ifndef PORT_TCHAR_H
#define PORT_TCHAR_H
#include <string.h>
#include <stdio.h>
#include <windows.h>
typedef char _TCHAR;
#define _T(x) x
#define _TEXT(x) x
#define _tcscpy strcpy
#define _tcsncpy strncpy
#define _tcscat strcat
#define _tcslen strlen
#define _tcscmp strcmp
#define _tcsicmp strcasecmp
#define _tcschr strchr
#define _tcsrchr strrchr
#define _tcsstr strstr
#define _stprintf sprintf
#define _sntprintf snprintf
#define _tprintf printf
#define _tfopen fopen
#endif
