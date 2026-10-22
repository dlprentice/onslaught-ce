// DisplayModeKey.h: the display mode the options save (only Career.cpp and d3dapp.cpp use it, so it is kept out of
// d3dapp.h, which most files include).

#ifndef DISPLAYMODEKEY_H
#define DISPLAYMODEKEY_H

#include "d3dapp.h"

// the saved display mode, packed: width in bits 0-15, height in 16-30, and bit 31 set for a 32-bit format
class CDisplayModeKey
{
public:
	void	Set(D3DModeInfo *mode);			// 00420d10
	BOOL	operator==(CDisplayModeKey &k)	// one masked compare (BuildDeviceList 0052a393)
			{ return (mWidth == k.mWidth) && (mHeight == k.mHeight) && (m32Bit == k.m32Bit); }

	DWORD	mWidth : 16;
	DWORD	mHeight : 15;
	DWORD	m32Bit : 1;
};
extern CDisplayModeKey	gDisplayModeKey;	// 0066061c (saved with the options)

#endif
