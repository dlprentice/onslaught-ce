#ifndef	CHUNKER_H
#define	CHUNKER_H

#include "MemBuffer.h"

 //======================================================-==--=-- --  -
// CHUNKPOOLSIZE is the start size, it rescales dynamically
#define	CHUNKPOOLSIZE	(256*1024)
#define LENGTHSMAX		256
//======================================================-==--=-- --  -
class	CChunker
{
	int			Chunks[LENGTHSMAX];
	UBYTE*		Data;
	int			DataSize;
	int			DataUsed;
	int			Chunk;
	CMEMBUFFER	File;
	UINT		Position;

	void		ReSize();
public:
				CChunker();
				~CChunker();
	CMEMBUFFER*	Open(char* filename);
	int			Close();
	BOOL		Start(ULONG chunkname);
	BOOL		End();
	BOOL		Write(void* what,ULONG size, ULONG count);
	UINT		WhereAmI() { return(Position); };
};
//======= I guess this could be the same class but I can't quite see why yet... =======-=- - 
class	CChunkReader
{
	ULONG		Size;
	CMEMBUFFER	*File;
	ULONG		ReadSinceChunk;
	BOOL		mOwnFile;
public:
				CChunkReader();
				~CChunkReader();
	CMEMBUFFER*	Open(char* filename);
	CMEMBUFFER* Open(CMEMBUFFER *existing_buffer);
	int			Close();
	ULONG		GetNext();		// 0=dodgy, so don't use it as a 'chunk name..."
	BOOL		Read(void* what, ULONG size, ULONG count);
	int			Skip();	
	UINT		GetSize() { return(Size); };
	UINT		WhereAmI();
	CMEMBUFFER	*GetMemBuffer() { return(File); };
};
#endif