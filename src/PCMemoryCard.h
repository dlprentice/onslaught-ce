// PCMemoryCard.h: the PC's save storage, MEMORYCARD (00704858). Stuart's CPCMemoryCard defines these inline as stubs;
// the retail PC build calls them out of line: Update (0041a200, returns false), GetCardInfo (00514960), ReadSave
// (00515080), CardBeenUnpluggedSinceLastTimeIAsked (004d6b20, returns 0). The saves are files in savegames\\
// (PCMemoryCard.cpp): one card, always present and formatted, with unlimited space.

#ifndef PCMEMORYCARD_H
#define PCMEMORYCARD_H

#include "MemoryCard.h"

class CPCMemoryCard : public CMemoryCard
{
public:
	void		InitialiseOnce() {};
	int			GetCardType(int card,int *cardtype)
	{
		*cardtype=MCT_CARD;
		return(MCE_SUCCESS);
	}
	int			Unformat(int card)
	{
		return(MCE_SUCCESS);
	}
	BOOL		IsHDDAvailable();													// 005019c0 (returns FALSE)
	int			GetNumCards(int *num);												// 00514950
	int			GetCardInfo(int card,BOOL *present,BOOL *formatted,int *bytesfree,int *bytestotal);	// 00514960
	int			GetCardName(int card,WCHAR *buffer);									// 005149a0
	int			ReadSave(int card,int save,WCHAR *name,void *data,int datasize,int *bytesread);
	BOOL		CardBeenUnpluggedSinceLastTimeIAsked(int card);
	bool		Update();
	void		Initialise();														// 0040c640 (empty)
	void		Shutdown();															// 0040c640 (empty)
	int			GetNumSaves(int card,int *num);										// 005149c0
	int			GetSaveName(int card,int save,WCHAR *name);							// 00514a80
	int			CreateSave(int card,WCHAR *name,int *save,BOOL allowed_overwrite);	// 00514be0
	int			DeleteSave(int card,int save,WCHAR *name);							// 00514ec0
	int			WriteSave(int card,int save,WCHAR *name,void *data,int datasize);	// 00514f80
	int			GetSaveSize(int datasize,int *savesize);								// 00515190
	int			MakeHumanReadableSize(int bytes,WCHAR *buffer);						// 005151a0
	int			Format(int card);													// 004d6b20 (returns 0)
	void		AccumulateResources(class CResourceAccumulator *ra, DWORD flags=0);	// Stuart's PCMemoryCard.h (no retail copy)
};

#endif
