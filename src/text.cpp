// text.cpp: the localized text database (reconstructed; Stuart's file is not in the release). Layout and behaviour
// from the retail object, 004f2120..004f2710. The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "text.h"
#include "console.h"
#include "debuglog.h"
#include "membuffer.h"

int		gAmericanText;						// 0083d990

CText	TEXT;								// 0083d960	// FUNCTION: 0x004f2120 init:TEXT
WCHAR	gWideText[512];				// 0083d560: format 0's widened string; external, not static: LINK never folds code that refers to a static, and retail folds GetStringById with its twin

//******************************************************************************************
CText::CText()	// FUNCTION: 0x004f2140
{
	mFile = NULL;
	mLoaded = FALSE;
	mFormat = 0;
	mSize = 0;
}

//******************************************************************************************
void	CText::Init()	// FUNCTION: 0x004f2150
{
	mFile = NULL;
	mLoaded = FALSE;
	mFormat = 0;
	mSize = 0;
	mLanguage = -1;
}

//******************************************************************************************
void	CText::Shutdown()	// FUNCTION: 0x004f2170
{
	if (mFile)
	{
		MEM_MANAGER.Free(mFile);
		mFile = NULL;
	}
}

//******************************************************************************************
char	*CText::GetLanguageName()	// FUNCTION: 0x004f2190
{
	switch (mLanguage)
	{
	case 0:	return "english";
	case 1:	return "french";
	case 2:	return "german";
	case 3:	return "spanish";
	case 4:	return "italian";
	}

	LOG.AddMessage("ERROR: unkown language %d", mLanguage);
	return "english";
}

//******************************************************************************************
// Retail reads the pool lengths a byte at a time: they need not be aligned.
#define READ_LENGTH(p)	((((((p)[3] << 8) | (p)[2]) << 8) | (p)[1]) << 8 | (p)[0])

void	CText::Load(int language)	// FUNCTION: 0x004f21f0
{
	if (mLoaded || mFile)
		return;

	mLanguage = language;

	char	name[256];
	if (gAmericanText)
		sprintf(name, "data\\LANGUAGE\\american.DAT");
	else
		sprintf(name, "data\\LANGUAGE\\%s.DAT", GetLanguageName());

	CDXMemBuffer	mb;
	if (!mb.InitFromFile(name, MEMTYPE_MEMBUFFER, TRUE, 0))
	{
		CONSOLE.FatalFileError(name, -1);
		LOG.AddMessage("ERROR: Can't open text file %s", name);
		mb.Close();
		return;
	}

	UINT	size = mb.GetFileSize();
#line 128
	mFile = (STextFile *)new (MEMTYPE_TEXT) UBYTE[size];
	mb.Read(mFile, size);
	mSize = size;

	DWORD	version = ((DWORD *)mFile)[0];
	if (version != 0xffffffbb)
	{
		mNumEntries = version;
		mText = (WCHAR *)&((DWORD *)mFile)[version + 1];
		mFormat = 0;
	}
	else
	{
		version = ((DWORD *)mFile)[1];
		if (version & 0x80000000)
			mHasIDNames = TRUE;
		else
			mHasIDNames = FALSE;
		version &= 0x7fffffff;

		if (version == 1)
		{
			mFormat = version;
			mNumEntries = ((DWORD *)mFile)[2];
			mText = (WCHAR *)&((DWORD *)mFile)[mNumEntries * 2 + 3];
		}
		else if (version == 2 || version == 3)
		{
			mFormat = version;
			mNumEntries = ((DWORD *)mFile)[2];

			UBYTE	*p = (UBYTE *)&((DWORD *)mFile)[mNumEntries * 3 + 3];
			SINT	textSize = READ_LENGTH(p);
			mText = (WCHAR *)&((DWORD *)mFile)[mNumEntries * 3 + 4];
			SINT	stringSize = READ_LENGTH((UBYTE *)&((DWORD *)mFile)[mNumEntries * 3 + 4] + textSize);
			mStrings = (char *)((UBYTE *)&((DWORD *)mFile)[mNumEntries * 3 + 4] + textSize + 4);
			if (mHasIDNames)
			{
				mIDNames = (char (*)[64])((DWORD)(mStrings + stringSize + 3) & ~3);
				SINT	*files = (SINT *)(mIDNames + mNumEntries);
				mNumSourceFiles = *files;
				mSourceFiles = (STextSourceFile *)(files + 1);
			}
		}
		else
			LOG.AddMessage("unknown version in text file");
	}

	mb.Close();
	mLoaded = TRUE;
}

//******************************************************************************************
char	*CText::GetSoundName(DWORD id)	// FUNCTION: 0x004f24b0
{
	if (mFormat == 2 || mFormat == 3)
	{
		DWORD	*ids = &mFile->mEntries[0].mID;
		SINT	*sounds = &mFile->mEntries[0].mSound;
		for (SINT c0 = 0; c0 < mNumEntries; c0++)
		{
			if (id == *ids)
			{
				if (*sounds == -1)
					return NULL;
				return mStrings + *sounds;
			}
			ids += 3;
			sounds += 3;
		}
	}
	return NULL;
}

//******************************************************************************************
WCHAR	*CText::GetFollowingString(DWORD id, SINT n)	// FUNCTION: 0x004f2500
{
	if (mFormat > 3)
	{
		LOG.AddMessage("FATAL ERROR: TextDB unsupported version");
		return NULL;
	}

	DWORD	*ids = &mFile->mEntries[0].mID;
	DWORD	*texts = &mFile->mEntries[0].mText;
	for (SINT c0 = 0; c0 < mNumEntries; c0++)
	{
		if (id == *ids)
			return mText + texts[n * 3];
		ids += 3;
		texts += 3;
	}

	LOG.AddMessage("ERROR: No string after for id: %d (%d)", id, n);
	return mText;
}

//******************************************************************************************
WCHAR	*CText::GetStringById(DWORD id)	// FUNCTION: 0x004f2580
{
	SINT	stride = 2;
	if (mFormat == 1)
		stride = 2;
	if (mFormat == 2)
		stride = 3;
	if (mFormat == 3)
		stride = 3;

	if (mFormat >= 1)
	{
		DWORD	*ids = &mFile->mEntries[0].mID;
		DWORD	*texts = &mFile->mEntries[0].mText;
		for (SINT c0 = 0; c0 < mNumEntries; c0++)
		{
			if (id == *ids)
				return mText + *texts;
			ids += stride;
			texts += stride;
		}

		LOG.AddMessage("ERROR: No string for id: %d", id);
		return mText;
	}

	if (mFormat == 0)
	{
		char	*s;
		if (SINT(id) >= 0 && SINT(id) < mNumEntries)
			s = (char *)mText + mFile->mHeader[id + 1];
		else
			s = (char *)mText;

		MultiByteToWideChar(CP_ACP, 0, s, -1, gWideText, strlen(s) + 1);
		return gWideText;
	}

	return mText;
}

//******************************************************************************************
// Retail has two symbols with this body and the linker folded them into 004f2580 (identical COMDAT folding). The
// evidence is CFrontEndText::GetMultiplayerLevelDescription (0046a220), which sends types 0 and 3 to the same
// string through separate blocks: VC6 merges two identical case blocks (a probe with one callee does), and keeps
// them apart when the callees are different symbols. Their names are not known; type 3 calls this one. Same body
// as GetStringById, kept in step with it.
WCHAR	*CText::GetStringByIdTwin(DWORD id)	// FUNCTION: 0x004f2580
{
	SINT	stride = 2;
	if (mFormat == 1)
		stride = 2;
	if (mFormat == 2)
		stride = 3;
	if (mFormat == 3)
		stride = 3;

	if (mFormat >= 1)
	{
		DWORD	*ids = &mFile->mEntries[0].mID;
		DWORD	*texts = &mFile->mEntries[0].mText;
		for (SINT c0 = 0; c0 < mNumEntries; c0++)
		{
			if (id == *ids)
				return mText + *texts;
			ids += stride;
			texts += stride;
		}

		LOG.AddMessage("ERROR: No string for id: %d", id);
		return mText;
	}

	if (mFormat == 0)
	{
		char	*s;
		if (SINT(id) >= 0 && SINT(id) < mNumEntries)
			s = (char *)mText + mFile->mHeader[id + 1];
		else
			s = (char *)mText;

		MultiByteToWideChar(CP_ACP, 0, s, -1, gWideText, strlen(s) + 1);
		return gWideText;
	}

	return mText;
}

//******************************************************************************************
void	CText::Copy(CText &from)	// FUNCTION: 0x004f2660
{
	Shutdown();
	Init();

	mFormat = from.mFormat;
	mNumEntries = from.mNumEntries;
	mLoaded = from.mLoaded;
	mSize = from.mSize;
	mLanguage = from.mLanguage;

#line 341
	mFile = (STextFile *)new (MEMTYPE_TEXT) UBYTE[mSize];
	memcpy(mFile, from.mFile, mSize);
	mText = (WCHAR *)((char *)from.mText - (char *)from.mFile + (char *)mFile);
	mStrings = from.mStrings - (char *)from.mFile + (char *)mFile;
}
