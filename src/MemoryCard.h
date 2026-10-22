// MemoryCard.h: the generic memory card access (Stuart's MemoryCard.h); the PC's class is CPCMemoryCard (PCMemoryCard.h).

#ifndef MEMORYCARD_H
#define MEMORYCARD_H

// Stuff

#define MAX_MEMORY_CARD_SAVES 4096

// Memory card errors

#define MCE_SUCCESS			0
#define MCE_FAILURE			1
#define MCE_NOCARD			2
#define MCE_UNFORMATTED		3
#define MCE_CORRUPT			4
#define MCE_CARDFULL		5
#define MCE_FILEEXISTS		6
#define MCE_NOFILE			7
#define MCE_TOO_MANY_SAVES	8

// Memory card types

#define MCT_CARD			0
#define MCT_HDD				1

class CMemoryCard
{
public:
	void		InitialiseOnce();
	void		Initialise();
	void		Shutdown();
	
	BOOL		IsHDDAvailable();
	
	int			GetNumCards(int *num);
	int			GetCardInfo(int card,BOOL *present,BOOL *formatted,int *bytesfree,int *bytestotal);
	int			GetCardName(int card,WCHAR *buffer);
	int			GetCardType(int card,int *cardtype);
	
	int			Format(int card);
	int			Unformat(int card);
	
	int			GetNumSaves(int card,int *num);
	int			GetSaveName(int card,int save,WCHAR *name);
	
	int			CreateSave(int card,WCHAR *name,int *save, BOOL allowed_overwrite);
	int			DeleteSave(int card,int save,WCHAR *name);
	
	int			WriteSave(int card,int save,WCHAR *name,void *data,int datasize);
	int			ReadSave(int card,int save,WCHAR *name,void *data,int datasize,int *bytesread);
	
	int			GetSaveSize(int datasize,int *savesize);
	int			MakeHumanReadableSize(int bytes,WCHAR *buffer);

	void		AccumulateResources(class CResourceAccumulator *ra, DWORD flags=0);

	bool		TooManySavesHere(int card) {return false;}
	
	bool		Update();

	BOOL		CardBeenUnpluggedSinceLastTimeIAsked(int card);

protected:
	void	GetHumanReadableError(int res,char *buffer)
	{
		if (res==MCE_SUCCESS)
			strcpy(buffer,"Success");
		else if (res==MCE_FAILURE)
			strcpy(buffer,"Generic failure");
		else if (res==MCE_NOCARD)
			strcpy(buffer,"No card");
		else if (res==MCE_UNFORMATTED)
			strcpy(buffer,"Card unformatted");
		else if (res==MCE_CORRUPT)
			strcpy(buffer,"Save corrupt");
		else if (res==MCE_CARDFULL)
			strcpy(buffer,"Card full");
		else if (res==MCE_FILEEXISTS)
			strcpy(buffer,"File already exists");
		else if (res==MCE_NOFILE)
			strcpy(buffer,"File does not exist");
		else
			strcpy(buffer,"Unknown MCE code");
	}
};

#define CMEMORYCARD CPCMemoryCard
#include "PCMemoryCard.h"

extern CMEMORYCARD MEMORYCARD;

#endif
