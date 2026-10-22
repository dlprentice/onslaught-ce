// fmvlookup.cpp: which cutscene plays around each level (reconstructed; the object has no strings, so the file name
// is ours). It lies between FEPWingmen.obj and mixermap.obj; its table ends where mixermap.cpp's __FILE__ begins
// (00640030). CGame asks for the intro (0) and the two outros (1, 2).

#include	"Common.h"

#include	"fmv.h"

struct SFMVLookup
{
	SINT	mLevel;
	SINT	mIntro;								// +0x04
	SINT	mOutro;								// +0x08
	SINT	mAltOutro;							// +0x0c
};

static SFMVLookup	fmv_lookup[] =				// 0063fd70
{
	{100, 1, 2, -1},	{110, 3, 4, -1},	{200, 5, 6, -1},	{211, -1, -1, -1},	{212, -1, -1, -1},
	{221, 7, -1, -1},	{222, 7, -1, -1},	{231, 8, 9, -1},	{232, 8, 9, -1},	{300, 10, -1, -1},
	{311, -1, -1, -1},	{312, -1, -1, -1},	{321, -1, -1, -1},	{322, -1, -1, -1},	{331, 11, 12, -1},
	{332, 11, 12, -1},	{400, -1, -1, -1},	{411, -1, -1, -1},	{412, -1, -1, -1},	{421, 13, -1, -1},
	{422, 13, -1, -1},	{431, -1, -1, -1},	{432, -1, -1, -1},	{500, -1, 14, 15},	{511, 16, -1, -1},
	{512, 17, 18, -1},	{521, 19, -1, -1},	{522, 19, -1, -1},	{523, 20, -1, -1},	{524, 20, -1, -1},
	{600, 21, -1, -1},	{611, 22, -1, -1},	{612, 22, -1, -1},	{621, 23, 24, -1},	{622, 23, 24, -1},
	{700, 25, -1, -1},	{710, -1, -1, -1},	{720, 26, 28, 27},	{731, -1, -1, -1},	{732, -1, -1, -1},
	{741, -1, 29, 32},	{742, -1, 30, -1},	{800, 31, 33, -1},
	{-1, 0, 0, 0}
};

//*********************************************************************************
// the cutscene for the level: 0 the intro, 1 the outro, 2 the alternative outro; -1 for none
SINT	lookup_FMV(SINT level, SINT which)	// FUNCTION: 0x00523120
{
	SINT	i = 0;
	while (fmv_lookup[i].mLevel != -1)
	{
		if (fmv_lookup[i].mLevel == level)
		{
			switch (which)
			{
			case 1:		return fmv_lookup[i].mOutro;
			case 2:		return fmv_lookup[i].mAltOutro;
			default:	return fmv_lookup[i].mIntro;
			}
		}
		i++;
	}
	return -1;
}
