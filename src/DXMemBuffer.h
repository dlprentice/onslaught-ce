#ifndef DXMEMBUFFER_H
#define DXMEMBUFFER_H

#include "MemoryManager.h"

class	CThing;

// 0x134 bytes (the retail allocation in CMemoryManager::DumpMemory).
class	CDXMemBuffer : public IMemBuffer
{
public:
	CDXMemBuffer();
	~CDXMemBuffer();

	BOOL	InitFromMem(const char *fname,EMemoryType memtype=MEMTYPE_MEMBUFFER);
	BOOL	InitFromFile(const char *fname,EMemoryType memtype=MEMTYPE_MEMBUFFER,BOOL mungepath=TRUE,UINT startskip = 0);

	void	LoadNextBlock();

	SINT	ConvertFromWritingToReading();	// returns length

	SINT	Read(void *data, SINT size);
	void	ReadString(char *data, SINT maxchars);
	void	ReadLine(char *line, SINT maxchars);		// 0048de00: ReadString without the trailing newline
	BOOL	OpenFile(const char *fname);			// 0048ddd0: InitFromFile with memory type 17 (the particle set)
	BOOL	CloseFile();						// 0048ddf0: Close
	void	Write(void *data, SINT size);

	SINT	Skip(SINT size);

	BOOL	Close();

	BOOL	IsMoreData() {ASSERT(mReading); return (mPtr < mData + mDataSize);};

	void	DeclareInvalidData(CThing *t);

	char	*GetData() {return mData;}
	UINT	GetFileSize();

	BOOL	EndOfFile();

	UINT	WhereAmI() { return(mPos); };

	static void SetNextReadBufferSize(UINT size); // pass in zero to set back to default.

protected:
	// retail PC: *.aya files are zlib-packed (DXMemBuffer.cpp)
	static BOOL		ReadBlock(const char *name, HANDLE file, char *dest, DWORD size, DWORD *read);
	static BOOL		WriteBlock(const char *name, HANDLE file, char *data, DWORD size, DWORD *written);
	static DWORD	SeekBlock(const char *name, HANDLE file, SINT pos);

protected:
	HANDLE	mFile;
	char	*mData;
	char	*mPtr;

	// OK OK so it's not a CRC but you get the idea; something that we load to check the validity
	// of the data in the file.
	char	*mCRCData;
	char	*mCRCDataUpTo;

	// only when saving, we leave the file open
	FILE	*mCRCFile;

	SINT	mSize, mDataSize;
	BOOL	mReading;
	BOOL	mEOF;
	BOOL	mLastBlock;
public:
	char	mFileName[ 256 ];			// +0x2c (CDXMeshVB::Load 0054e20d)
protected:
	UINT	mPos;
	EMemoryType mMemType;

	static UINT mNextReadBufferSize;
};

#endif
