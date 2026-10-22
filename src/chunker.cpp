#include "common.h"
#include  "stdafx.h"

#include "chunker.h"
#include <string.h>
//======================================================-==--=-- --  -
CChunker::CChunker()
{
	Data = new( MEMTYPE_CHUNKER ) UBYTE[CHUNKPOOLSIZE];
	DataSize = CHUNKPOOLSIZE;
	DataUsed = 0;
	Chunk = 0;
	Position = 0;
}
//======================================================-==--=-- --  -
CChunker::~CChunker()
{
	File.Close();
	delete Data ;
}
//======================================================-==--=-- --  -
CMEMBUFFER * CChunker::Open(char* filename)
{
	if( File.InitFromMem(filename) )
	{
		return &File;
	}
	else
	{
		return 0;
	}
};
//======================================================-==--=-- --  -
int	CChunker::Close()
{
	if( File.Close() )
	{ 
		return 0;
	}
	else
	{
		return -1;
	}
};
//======================================================-==--=-- --  -
void CChunker::ReSize()
{
	int		newsize = DataSize+CHUNKPOOLSIZE;
	UBYTE*	newdata = new( MEMTYPE_CHUNKER ) UBYTE[newsize];
	if(DataUsed>0)	memcpy(newdata,Data,DataUsed);
	delete[] Data;
	Data = newdata;
	DataSize = newsize;
}
//======================================================-==--=-- --  -
BOOL CChunker::Start(ULONG chunkname)
{
	if(Chunk>=(LENGTHSMAX-1)) return FALSE;
	if((DataUsed+8)>=DataSize) ReSize();

	Chunks[Chunk] = DataUsed+4;				// this needs filling in later 
	*(ULONG*)(Data+DataUsed) = chunkname;
	DataUsed+=8;
	Position+=8;
	Chunk++;
	return TRUE;
}
//======================================================-==--=-- --  -
BOOL CChunker::End()
{
	if(Chunk<=0) return FALSE;
	Chunk--;
	int start = Chunks[Chunk];
	int size = DataUsed-(int)start;
	*(int*)(Data+start) = size-4;

	if(Chunk==0)						// we've filled in the last one
	{									// so write it all out
		File.Write(Data, DataUsed);
		DataUsed=0;						// OK to use start of buffer
	}
	return TRUE;
}
//======================================================-==--=-- --  -
BOOL CChunker::Write(void* what, ULONG size, ULONG count)
{
	if(Chunk==0) return FALSE;
	while(int(DataUsed+size*count)>=DataSize) ReSize();
	memcpy(Data+DataUsed, what, size*count);
	DataUsed+=size*count;
	Position+=size*count;
	return TRUE;
}
//======================================================-==--=-- --  -
//======================================================-==--=-- --  -
CChunkReader::CChunkReader()	// FUNCTION: 0x004237d0
{
	File = new (MEMTYPE_MEMBUFFER) CMEMBUFFER;
	mOwnFile=TRUE;
}
//======================================================-==--=-- --  -
CChunkReader::~CChunkReader()	// FUNCTION: 0x00423840
{
	if (mOwnFile)
		SAFE_DELETE(File);
}
//======================================================-==--=-- --  -
CMEMBUFFER* CChunkReader::Open(CMEMBUFFER *existing_buffer)	// FUNCTION: 0x00423870 ?Open@CChunkReader@@QAEPAVCDXMemBuffer@@PAV2@@Z
{
	Size=0;
	ReadSinceChunk=0;
	
	if (mOwnFile)
		SAFE_DELETE(File);

	mOwnFile=FALSE;

	File = existing_buffer;

	return File;
};
//======================================================-==--=-- --  -
CMEMBUFFER* CChunkReader::Open(char* filename)	// FUNCTION: 0x004238c0 ?Open@CChunkReader@@QAEPAVCDXMemBuffer@@PAD@Z
{
	Size=0;
	ReadSinceChunk=0;
	
	if( File->InitFromFile(filename) )
	{
		return File;
	}
	else
	{ 
		return 0;
	}
};
//======================================================-==--=-- --  -
int	CChunkReader::Close()	// FUNCTION: 0x00423900
{
	if( File->Close() )
	{
		return 0;
	}
	else
	{
		return -1;
	}
};

//======================================================-==--=-- --  -
ULONG	CChunkReader::GetNext()	// FUNCTION: 0x00423910
{
	ReadSinceChunk = 0;
	int chunk;
	if(File->Read(&chunk,4)<4) 
	{
//		TRACE("CChunkReader::GetNext() failed\n");
		return 0;
	}
	if(File->Read(&Size,4)<4)
	{
//		TRACE("CChunkReader::GetNext() failed\n");
		return 0;
	}
/*
	char buf[32],cid[5];
	
	cid[0]=chunk & 0xFF;
	cid[1]=(chunk>>8) & 0xFF;
	cid[2]=(chunk>>16) & 0xFF;
	cid[3]=(chunk>>24) & 0xFF;
	cid[4]=0;

	sprintf(buf,"Got chunk ID %s, size %d at 0x%x\n",cid,Size,WhereAmI());
	TRACE(buf);
*/
	return chunk;
}
//======================================================-==--=-- --  -
BOOL CChunkReader::Read(void* what, ULONG size, ULONG count)	// FUNCTION: 0x00423960
{
	int ss = size*count;
	ReadSinceChunk += ss;
	ASSERT(ReadSinceChunk<=Size);
	int s = File->Read(what,size*count);
	return(s==ss);
}
//======================================================-==--=-- --  -
int	CChunkReader::Skip()	// FUNCTION: 0x00423990
{
	ULONG skipsize=Size-ReadSinceChunk;
	ReadSinceChunk=Size;
//	printf("Skipped chunk - %d bytes\n",skipsize);
	return File->Skip(skipsize);
}
//======================================================-==--=-- --  -
UINT CChunkReader::WhereAmI()	// FUNCTION: 0x004239b0
{
	return(File->WhereAmI());
}
