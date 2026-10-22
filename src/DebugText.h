// DebugText.h: the on-screen debug text buffer (DBT at 0066ffc8; the write pointer at +0x2710, 006726d8).

#ifndef DEBUGTEXT_H
#define DEBUGTEXT_H

#include <stdio.h>
#include <stdarg.h>

class CDebugText
{
public:
#ifdef DEBUGMARKER_CPP
	CDebugText() { mPtr = mBuffer; }	// DBT's initializer (004424a0) is debugmarker.obj's
#endif
	// out of line in retail (varargs), emitted with game.obj
	// FUNCTION: 0x00472240 ?Out@CDebugText@@QAAXPADZZ
	void	Out(char *fmt, ...) { va_list args; va_start(args, fmt); mPtr += vsprintf(mPtr, fmt, args); va_end(args); }
	char	*GetPtrAndReset() { mPtr = mBuffer; return mBuffer; }

	char	mBuffer[10000];			// +0x0000
	char	*mPtr;					// +0x2710
};

extern CDebugText DBT;

#endif
