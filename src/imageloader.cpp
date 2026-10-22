// imageloader.cpp: CImageLoader (reconstructed; Stuart's file is not in the release). From the retail object,
// 00488620..004887f0. The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "imageloader.h"

//******************************************************************************************
CImageLoader::CImageLoader(char *name)	// FUNCTION: 0x00488620
	: mWidth(0), mHeight(0), mDepth(0), mData(NULL), mAlpha(NULL)
{
	strcpy(mName, name);
}

//******************************************************************************************
CImageLoader::~CImageLoader()	// FUNCTION: 0x00488700
{
	FreeData();
	FreeAlpha();
}

//******************************************************************************************
void	CImageLoader::FreeData()	// FUNCTION: 0x00488740
{
	if (mData)
	{
		delete [] mData;
		mData = NULL;
	}
}

//******************************************************************************************
void	CImageLoader::FreeAlpha()	// FUNCTION: 0x00488760
{
	if (mAlpha)
	{
		delete [] mAlpha;
		mAlpha = NULL;
	}
}

//******************************************************************************************
BOOL	CImageLoader::AllocData(SINT size)	// FUNCTION: 0x00488780
{
	FreeData();
#line 43
	mData = new (MEMTYPE_UNKNOWN) UBYTE[size];
	return mData != NULL;
}

//******************************************************************************************
BOOL	CImageLoader::AllocAlpha(SINT size)	// FUNCTION: 0x004887c0
{
	FreeAlpha();
#line 50
	mAlpha = new (MEMTYPE_UNKNOWN) UBYTE[size];
	return mAlpha != NULL;
}

// FUNCTION: 0x00488670 ?GetName@CImageLoader@@UAEPADXZ
// FUNCTION: 0x00488680 ?GetHeight@CImageLoader@@UAEHXZ
// FUNCTION: 0x00488690 ?GetDepth@CImageLoader@@UAEHXZ
// FUNCTION: 0x004886a0 ??_GCImageLoader@@UAEPAXI@Z
