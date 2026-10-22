// text.h: localized text. LANG_ENGLISH and friends come from windows.h (winnt.h).

#ifndef TEXT_H
#define TEXT_H

// One string's record in a version-3 text file: its hashed id, its text and its spoken sample's name, both offsets
// into the file's string pool (CFEPLanguageTest::PlaySample 0051c012..0051c02c).
struct STextEntry
{
	DWORD	mID;
	DWORD	mText;
	SINT	mSound;							// -1: none
};

struct STextFile
{
	DWORD		mHeader[3];
	STextEntry	mEntries[1];				// +0x0c (CText::GetStringById 004f25b2)
};

// One source file of the strings: the first string's index and the file's name (0x104 bytes; the language test
// page walks them, 0051c678..0051c697).
struct STextSourceFile
{
	SINT	mFirst;
	char	mName[256];
};

// The loaded language file's text (TEXT is at 0083d960). Versions 1-3 look strings up by hashed id (004f2580);
// the unversioned format 0 indexes an 8-bit table and widens the string into a static buffer.
class CText
{
public:
	CText();								// 004f2140
	void	Init();							// 004f2150
	void	Load(int language);				// 004f21f0
	void	Shutdown();						// 004f2170
	void	Copy(CText &from);				// 004f2660
	WCHAR	*GetStringById(DWORD id);		// 004f2580
	WCHAR	*GetStringByIdTwin(DWORD id);	// 004f2580: a second symbol with the same body (see text.cpp)
	WCHAR	*GetString(DWORD id) { return GetStringById(id); }	// Stuart's name (FEPGoodies.cpp)
	WCHAR	*GetFollowingString(DWORD id, SINT n);				// 004f2500: the string n entries after id's
	char	*GetSoundName(DWORD id);		// 004f24b0: id's spoken sample, or NULL

	char	*GetLanguageName();				// 004f2190: "english", "french" ... from mLanguage
	SINT	GetLanguage() { return mLanguage; }	// Stuart's (CResourceAccumulator::GetFileName 004d7103)
	SINT	GetSoundOffset(SINT n) { return mFile->mEntries[n].mSound; }

	SINT		mFormat;					// +0x00: 0 (unversioned) or the file's version, 1-3
	STextFile	*mFile;						// +0x04
	WCHAR		*mText;						// +0x08: the text pool; an unknown id gets its first string (004f25e6)
	char		*mStrings;					// +0x0c: the sample-name pool
	SINT		mNumEntries;				// +0x10
	BOOL		mLoaded;					// +0x14
	UINT		mSize;						// +0x18: the file's size
	SINT		mLanguage;					// +0x1c
	BOOL		mHasIDNames;				// +0x20: the version word's top bit
	struct STextSourceFile	*mSourceFiles;	// +0x24: where each run of strings came from (CFEPLanguageTest::Render 0051c661)
	SINT		mNumSourceFiles;			// +0x28
	char		(*mIDNames)[64];			// +0x2c: each string's id, as text (0051c6d9)
};

#define TEXT_DB	TEXT

extern CText TEXT;
#endif
