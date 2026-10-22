#include "common.h"
#define PCMEMORYCARD_CPP
#if TARGET==PC

#include "memorycard.h"
#include "Platform.h"
#include <io.h>
#include <direct.h>
#include <stdio.h>

// retail: the PC build keeps each career as a file, savegames\<name>.bes; Stuart's CPCMemoryCard was stubs.

WCHAR	*Localization_GetStringById(int id);			// 00524830

#define SAVE_DIRECTORY		"savegames\\"
#define SAVE_EXTENSION		".bes"
#define SAVE_PATTERN		"savegames\\*.bes"
#define SKIP_ATTRIBUTES		(_A_HIDDEN | _A_SYSTEM | _A_SUBDIR)

static long					sFindHandle;			// 008898d8
static struct _finddata_t	sFindData;				// 008898e0

// the next save from the current find, past folders and hidden or system files
static inline BOOL NextSave()
{
	do
	{
		if (_findnext(sFindHandle, &sFindData) != 0)
		{
			_findclose(sFindHandle);
			return FALSE;
		}
	} while (sFindData.attrib & SKIP_ATTRIBUTES);
	return TRUE;
}

static inline BOOL FirstSave()
{
	sFindHandle = _findfirst(SAVE_PATTERN, &sFindData);
	if (sFindHandle == -1)
		return FALSE;
	if (sFindData.attrib & SKIP_ATTRIBUTES)
		return NextSave();
	return TRUE;
}

int CPCMemoryCard::GetNumCards(int *num)	// FUNCTION: 0x00514950
{
	*num=1;
	return(MCE_SUCCESS);
}

int CPCMemoryCard::GetCardInfo(int card,BOOL *present,BOOL *formatted,int *bytesfree,int *bytestotal)	// FUNCTION: 0x00514960
{
	if (present)
		*present=TRUE;
	if (formatted)
		*formatted=TRUE;
	if (bytesfree)
		*bytesfree=0x7fffffff;
	if (bytestotal)
		*bytestotal=0x7fffffff;
	return(MCE_SUCCESS);
}

int CPCMemoryCard::GetCardName(int card,WCHAR *buffer)	// FUNCTION: 0x005149a0
{
	wcscpy(buffer,Localization_GetStringById(0x28));
	return(MCE_SUCCESS);
}

int CPCMemoryCard::GetNumSaves(int card,int *num)	// FUNCTION: 0x005149c0
{
	*num=0;
	if (FirstSave())
	{
		do
		{
			(*num)++;
		} while (NextSave());
	}
	return(MCE_SUCCESS);
}

int CPCMemoryCard::GetSaveName(int card,int save,WCHAR *name)	// FUNCTION: 0x00514a80
{
	char	buffer[256];

	if (FirstSave())
	{
		while (save)
		{
			save--;
			if (!NextSave())
				return 1;
		}

		strcpy(buffer, sFindData.name);
		buffer[strlen(buffer) - 4] = 0;			// drop the extension
		wcscpy(name, ToWCHAR(buffer));

		if (sFindHandle != -1)
		{
			_findclose(sFindHandle);
			sFindHandle = -1;
		}
		return(MCE_SUCCESS);
	}
	return 1;
}

int CPCMemoryCard::CreateSave(int card,WCHAR *name,int *save,BOOL allowed_overwrite)	// FUNCTION: 0x00514be0
{
	char	buffer[256];
	char	path[MAX_PATH];
	FILE	*f;

	if (save)
		*save = -1;

	_mkdir(SAVE_DIRECTORY);
	strcpy(path, SAVE_DIRECTORY);
	strcat(path, FromWCHAR(name));
	strcat(path, SAVE_EXTENSION);

	if (!allowed_overwrite)
	{
		f = fopen(path, "rb");
		if (f)
		{
			fclose(f);
			return MCE_FILEEXISTS;
		}
	}

	f = fopen(path, "wb");
	if (f)
	{
		fclose(f);

		// find the new file's index among the saves
		int index = 0;
		if (FirstSave())
		{
			strcpy(buffer, sFindData.name);
			buffer[strlen(buffer) - 4] = 0;
			while (_wcsicmp(name, ToWCHAR(buffer)) != 0)
			{
				index++;
				if (!NextSave())
					return 1;
				strcpy(buffer, sFindData.name);
				buffer[strlen(buffer) - 4] = 0;
			}

			if (*save)
				*save = index;

			if (sFindHandle != -1)
			{
				_findclose(sFindHandle);
				sFindHandle = -1;
			}
			return(MCE_SUCCESS);
		}
	}
	return 1;
}

int CPCMemoryCard::DeleteSave(int card,int save,WCHAR *name)	// FUNCTION: 0x00514ec0
{
	char	path[MAX_PATH];

	strcpy(path, SAVE_DIRECTORY);
	strcat(path, FromWCHAR(name));
	strcat(path, SAVE_EXTENSION);
	return DeleteFile(path) ? MCE_SUCCESS : 1;
}

int CPCMemoryCard::WriteSave(int card,int save,WCHAR *name,void *data,int datasize)	// FUNCTION: 0x00514f80
{
	char	path[MAX_PATH];

	strcpy(path, SAVE_DIRECTORY);
	strcat(path, FromWCHAR(name));
	strcat(path, SAVE_EXTENSION);
	FILE *f = fopen(path, "wb");
	if (f)
	{
		if (fwrite(data, datasize, 1, f) == 1)
		{
			fclose(f);
			return(MCE_SUCCESS);
		}
	}
	return 1;
}

// retail: a complete read closes the file twice
int CPCMemoryCard::ReadSave(int card,int save,WCHAR *name,void *data,int datasize,int *bytesread)	// FUNCTION: 0x00515080
{
	char	path[MAX_PATH];

	strcpy(path, SAVE_DIRECTORY);
	strcat(path, FromWCHAR(name));
	strcat(path, SAVE_EXTENSION);
	FILE *f = fopen(path, "rb");
	if (f)
	{
		int read = fread(data, 1, datasize, f);
		fclose(f);
		if (read != datasize)
		{
			*bytesread = read;
		}
		else
		{
			*bytesread = read;
			fclose(f);
			return(MCE_SUCCESS);
		}
	}
	return 1;
}

int CPCMemoryCard::GetSaveSize(int datasize,int *savesize)	// FUNCTION: 0x00515190
{
	*savesize=datasize;
	return(MCE_SUCCESS);
}

int CPCMemoryCard::MakeHumanReadableSize(int bytes,WCHAR *buffer)	// FUNCTION: 0x005151a0
{
	buffer[0]=0;
	return(MCE_SUCCESS);
}

//******************************************************************************************
// The PC memory card's console-only answers that no code above defined. Each compiles to a body another function
// already has, so the linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose
// address each annotation names (the class header notes the slots' survivors too).

// FUNCTION: 0x005019c0
BOOL CPCMemoryCard::IsHDDAvailable()
{
	return FALSE;
}

// FUNCTION: 0x004d6b20
BOOL CPCMemoryCard::CardBeenUnpluggedSinceLastTimeIAsked(int card)
{
	return FALSE;
}

// FUNCTION: 0x0041a200
bool CPCMemoryCard::Update()
{
	return false;
}

// FUNCTION: 0x0040c640
void CPCMemoryCard::Initialise()
{
}

// FUNCTION: 0x0040c640
void CPCMemoryCard::Shutdown()
{
}

// FUNCTION: 0x004d6b20
int CPCMemoryCard::Format(int card)
{
	return 0;
}

#endif

// The object's last initializer, after its code (.CRT$XCU order).
// FUNCTION: 0x005151b0 init:ZERO_FVECTOR
#include "mathsconst.h"
