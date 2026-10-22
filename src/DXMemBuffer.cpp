#include	"Common.h"
#include	"stdafx.h"
#include	"cliparams.h"
#include	"console.h"

// do we use xbox internal buffering or try to dma directly into our structures?
#define MB_BUFFERING 1	// retail: FILE_ATTRIBUTE_NORMAL only (00547f33)

#ifdef _DIRECTX

#include	"MemBuffer.h"  //(sic)

#ifndef SIMPLE_MESHES
#include    "debuglog.h"
#endif

//#include	"Globals.h"

//******************************************************************************************
//#define		MEM_BUFFER_SIZE		1024000
#define		READ_MEM_BUFFER_SIZE		(1024 * 1024)	// retail: 1MB (00547d48)
#if TARGET==PC
#define		WRITE_MEM_BUFFER_SIZE		(1024 * 1024)	// retail: 1MB (00547ddd)
#else
#define		WRITE_MEM_BUFFER_SIZE		(10 * 1024)
#endif

UINT CDXMemBuffer::mNextReadBufferSize = READ_MEM_BUFFER_SIZE;

//******************************************************************************************

void CDXMemBuffer::SetNextReadBufferSize(UINT size)	// FUNCTION: 0x00547d40
{
	// has to be a cluster size. Well, it doesn't at the moment but will one day, maybe.
	ASSERT(!(size & 0x7fff));

	if (size == 0)
	{
		// default
		mNextReadBufferSize = READ_MEM_BUFFER_SIZE;
	}
	else
	{
		// retail: rounded up to whole buffers (00547d53)
		mNextReadBufferSize = (size + READ_MEM_BUFFER_SIZE - 1) & ~(READ_MEM_BUFFER_SIZE - 1);
	}
}

//******************************************************************************************
CDXMemBuffer::CDXMemBuffer()	// FUNCTION: 0x00547d70
{
	mData = NULL;
	mCRCData = NULL;
	mCRCDataUpTo = NULL;
	mCRCFile = NULL;
}
//******************************************************************************************
CDXMemBuffer::~CDXMemBuffer()	// FUNCTION: 0x00547d90
{
//	ASSERT(!(mData)); // should be closed by now...
	delete [] mData;
	delete [] mCRCData;
};

//******************************************************************************************
BOOL	CDXMemBuffer::InitFromMem(const char *fname,EMemoryType memtype)	// FUNCTION: 0x00547dc0
{
#line 227
	mData =	new( memtype ) char[WRITE_MEM_BUFFER_SIZE];
	mMemType = memtype;

	if (!mData)
	{
		TRACE("Could not allocate memory for write buffer\n");
		return FALSE;
	}

	mPtr = mData;
	mSize = WRITE_MEM_BUFFER_SIZE;
	mReading = FALSE;
	mEOF = FALSE;
	mDataSize = 0;
	mPos=0;

	// cache file name (for close):
#if TARGET == PC
	strncpy( mFileName, fname, 256 );
#elif TARGET == XBOX
	sprintf(mFileName, "T:\\%s", fname);
	LOG.AddMessage("*CDXMemBuffer* Trying to open file %s\n", mFileName);
#endif


//	mFile = fopen(mFileName, "wb");
	mFile = CreateFile(mFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

//	if (!mFile)
	if (mFile == INVALID_HANDLE_VALUE)
	{
		TRACE("Membuffer could not open file for writing\n");
#if TARGET==XBOX
		LOG.AddMessage("*CDXMemBuffer* Membuffer could not open file for writing");
#endif
		SAFE_DELETE(mData);
		return(FALSE);
	}

	char crcname[MAX_PATH];
	sprintf(crcname, "%s.crc", mFileName);
	mCRCFile = NULL;	// retail: no CRC file (00547ea8)

	return TRUE;
}

// retail: a fatal error naming the file (005480c5, 0054822e)
static void data_bad(char *name)
{
	CONSOLE.FatalFileError(name, -1);
}

//******************************************************************************************
//******************************************************************************************
// Retail PC addition: files named *.aya are stored as zlib blocks, each a 4-byte packed
// length and packed data; reads can accumulate several chunks per buffer (005486e8).
extern "C" int __cdecl compress(unsigned char *dest, unsigned long *destLen, const unsigned char *source, unsigned long sourceLen);
extern "C" int __cdecl uncompress(unsigned char *dest, unsigned long *destLen, const unsigned char *source, unsigned long sourceLen);

#define		PACK_BUFFER_SIZE		0x102927	// retail (005481a4)

static unsigned char gPackBuffer[PACK_BUFFER_SIZE];	// 008c029c

static bool IsPacked(const char *name)
{
	if (strlen(name) < 4)
		return false;

	return _stricmp(name + strlen(name) - 4, ".aya") == 0;
}

inline BOOL	CDXMemBuffer::ReadBlock(const char *name, HANDLE file, char *dest, DWORD size, DWORD *read)
{
	if (IsPacked(name))
	{
		*read = 0;
		BOOL ok = FALSE;
		DWORD got, packed;
		while (ReadFile(file, &packed, 4, &got, NULL) && got == 4)
		{
			if (!ReadFile(file, gPackBuffer, packed, &got, NULL) || got != packed)
				return ok;

			DWORD len = PACK_BUFFER_SIZE;
			if (uncompress((unsigned char *)dest, &len, gPackBuffer, packed) != 0)
				return FALSE;

			*read += len;
			dest += len;
			ok = TRUE;
			if (*read >= mNextReadBufferSize)
				return ok;
		}
		return ok;
	}
	return ReadFile(file, dest, size, read, NULL);
}

inline BOOL	CDXMemBuffer::WriteBlock(const char *name, HANDLE file, char *data, DWORD size, DWORD *written)
{
	if (IsPacked(name))
	{
		DWORD packed = PACK_BUFFER_SIZE;
		compress(gPackBuffer, &packed, (unsigned char *)data, size);
		data = (char *)gPackBuffer;
		size = packed;
		if (!WriteFile(file, &packed, 4, written, NULL))
			return FALSE;
	}
	return WriteFile(file, data, size, written, NULL);
}

// Packed files cannot seek: whole blocks are read and dropped instead.
inline DWORD	CDXMemBuffer::SeekBlock(const char *name, HANDLE file, SINT pos)
{
	if (IsPacked(name))
	{
		while (pos >= READ_MEM_BUFFER_SIZE)
		{
			DWORD packed, got;
			if (!ReadFile(file, &packed, 4, &got, NULL) || got != 4)
				return 0;
			if (!ReadFile(file, gPackBuffer, packed, &got, NULL) || got != packed)
				return 0;
			pos -= READ_MEM_BUFFER_SIZE;
		}
		return pos;
	}
	return SetFilePointer(file, pos, NULL, FILE_BEGIN);
}

//******************************************************************************************
// The frame's slots (amount_moved, SeekBlock's packed and ReadBlock's ok sharing one) depend on each local's reference count when
// VC6 packs the frame, which counts every `return ok` of ReadBlock as a tail of its own (the cross-jump pass merges them after
// the count): ReadBlock returns at each exit, and SeekBlock tests its two reads in two ifs.
BOOL	CDXMemBuffer::InitFromFile(const char *fname,EMemoryType memtype,BOOL mungepath,UINT startskip)	// FUNCTION: 0x00547ec0
{
	strncpy( mFileName, fname, 256 );

#line 287
	mData =	new( memtype ) char[mNextReadBufferSize];
	mMemType = memtype;

	if (!mData)
	{
		TRACE("Could not allocate memory for read buffer\n");
		return FALSE;
	}

	mPtr = mData;
	mSize = mNextReadBufferSize;
	mReading = TRUE;
	mEOF = FALSE;
	mLastBlock = FALSE;
	mDataSize = 0;
	mPos=0;

	DWORD flags;

	if (MB_BUFFERING) flags = FILE_ATTRIBUTE_NORMAL                         ;
	else			  flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING;

	char crcdataname[256];
	mFile = CreateFile(fname, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, flags, NULL);
	sprintf(crcdataname, "%s.crc", fname);

	if (mFile == INVALID_HANDLE_VALUE)
	{
		delete mData;
		mData = NULL;
		return FALSE;
	}

	// Sometimes we don't want the first bit of the file, why load it?
	DWORD amount_moved;
	if (startskip && !IsPacked(mFileName))
	{
		amount_moved = SeekBlock(mFileName, mFile, startskip / mNextReadBufferSize * mNextReadBufferSize);
	}
	else
	{
		amount_moved = 0;
	}

	if (amount_moved == 0xffffffff) data_bad(mFileName);

	// and do the move in our counter too.
	mPos += amount_moved;

	// read first block:
	LoadNextBlock();

	mPtr = mData;

	if (startskip - amount_moved > 0)
	{
		// There's a bit more skipping to do.
		Skip(startskip - amount_moved);
	}

	return TRUE;
}

//******************************************************************************************
SINT	CDXMemBuffer::ConvertFromWritingToReading()
{
	mReading	= TRUE;
	mPtr		= mData;

	return mDataSize;
}
//******************************************************************************************

static char eval_crc(const char *data, int num_bytes)
{
	char retval = 0;
	
	while (num_bytes)
	{
		retval += *data++;

		num_bytes--;
	}

	return retval;
}

// if it's a really bad winerror we'll just hang with an error message.
static void how_bad_is_that(char *name)
{
	switch(GetLastError())
	{
	case ERROR_HANDLE_EOF:
		// That's fine.
		return;

	default:
		// It's pointless having all these errors listed because of the "default" above
		// but now I've searched for them I feel like leaving them
	case ERROR_ARENA_TRASHED:
	case ERROR_INVALID_BLOCK:
	case ERROR_BAD_FORMAT:
	case ERROR_INVALID_DATA:
	case ERROR_BAD_UNIT:
	case ERROR_NOT_READY:
	case ERROR_CRC:
	case ERROR_NOT_DOS_DISK:
	case ERROR_READ_FAULT:
	case ERROR_GEN_FAILURE:
	case ERROR_OPEN_FAILED:
	case ERROR_DISK_OPERATION_FAILED:
		data_bad(name);
	}
}

//******************************************************************************************
void	CDXMemBuffer::LoadNextBlock()
{
	if (!ReadBlock(mFileName, mFile, mData, mNextReadBufferSize, (DWORD *)&mDataSize))
	{
		// failed.
		how_bad_is_that(mFileName);

		mDataSize = 0;
	}

	if( mDataSize != mSize )
	{
		mLastBlock = TRUE;
	}
	mPtr = mData;

	// Either way, we now have some data, and we have to check it for CRC.
	if (mCRCData && eval_crc(mData, mDataSize) != *mCRCDataUpTo)
	{
		// the crc is wrong!
		data_bad(mFileName);
	}

	// go to next block of CRC data.
	mCRCDataUpTo++;
}

//******************************************************************************************
UINT	CDXMemBuffer::GetFileSize()	// FUNCTION: 0x005482c0
{
	ASSERT(mReading);

	return ::GetFileSize(mFile, NULL);

	/*
	UINT retval;


	UINT current=ftell(mFile);
	fseek(mFile,0,SEEK_END);
	UINT l=ftell(mFile);
	fseek(mFile,current,SEEK_SET);

	mThread.ReleaseMutex();

	return(l);*/
}

//******************************************************************************************
// very very much like reading because most of the Skip's are very tiny
SINT	CDXMemBuffer::Skip(SINT size)	// FUNCTION: 0x005482d0
{
	if (!size) return 0;

	ASSERT(mReading);
	
	int bytesread=0;

	if( mLastBlock && mPtr + size > mData + mDataSize )
	{
		mEOF = TRUE;
		size = ( mData + mDataSize - mPtr );
	}

	if( size > 0 )
	{
		while( mPtr + size > mData + mDataSize )
		{
			UINT lBytesToRead = mData + mDataSize - mPtr;
			bytesread+=lBytesToRead;
			size -= lBytesToRead;

			LoadNextBlock();

			if( mLastBlock && mPtr + size > mData + mDataSize )
			{
				mEOF = TRUE;
				size = ( mData + mDataSize - mPtr );
			}
		}
		bytesread+=size;
		mPtr += size;
	}

	ASSERT(mPtr <= mData + mDataSize);

	mPos+=bytesread;

	return(bytesread);
}

//******************************************************************************************
SINT	CDXMemBuffer::Read(void *data, SINT size)	// FUNCTION: 0x00548570
{
	ASSERT(mReading);
	
	char * lpData = (char *)data;

	int bytesread=0;
	if( mLastBlock && mPtr + size > mData + mDataSize )
	{
		mEOF = TRUE;
		size = ( mData + mDataSize - mPtr );
	}

	if( size > 0 )
	{
		while( mPtr + size > mData + mDataSize )
		{
			// We've haven't loaded enough. copy what we have...
			UINT lBytesToRead = mData + mDataSize - mPtr;
			bytesread+=lBytesToRead;
			memcpy( lpData, mPtr, lBytesToRead );
			lpData += lBytesToRead;
			size -= lBytesToRead;

			// Refill the internal buffer before continuing the copy (retail 0054860a).
			LoadNextBlock();

			if( mLastBlock && mPtr + size > mData + mDataSize )
			{
				mEOF = TRUE;
				size = ( mData + mDataSize - mPtr );
			}
		}
		bytesread+=size;
		memcpy( lpData, mPtr, size );
		mPtr += size;
	}

	ASSERT(mPtr <= mData + mDataSize);

	mPos+=bytesread;

	return(bytesread);
}

//******************************************************************************************
void	CDXMemBuffer::ReadString(char *data, SINT maxchars)	// FUNCTION: 0x00548820
{
	ASSERT(mReading);

	// calculate number of bytes to read:
	UINT lBytesRead = 0;
	for( SINT i=0; i<maxchars-1; ++i )
	{
		if( mPtr >= mData + mDataSize )
		{
			if( mLastBlock )
			{
				mEOF = TRUE;
				break;
			}
			else
			{
				LoadNextBlock();
			}
		}

		data[lBytesRead++] = *(mPtr++);

		if (data[lBytesRead-1] == 10)
		{
			break;
		}
	}

	if( lBytesRead > 1 )

	{
		if( data[lBytesRead-2] == 0x0d )
		{
			data[lBytesRead-2] = '\n';
			data[lBytesRead-1] = '\0';
		}
	}
	data[lBytesRead] = '\0';

	mPos+=lBytesRead;

	ASSERT(mPtr <= mData + mDataSize);
}

//******************************************************************************************
static void write_crc_data(FILE *file, const char *data, SINT size, SINT buffersize)
{
	if (file)
	{
		for (; size > 0; size -= buffersize, data += buffersize)
		{
			char crc;

			if (size >= buffersize) crc = eval_crc(data, buffersize);
			else					crc = eval_crc(data, size      );

			if (fwrite(&crc, 1, 1, file) != 1)
			{
				SASSERT(0, "Can't write CRC!");
			}
		}
	}
}

//******************************************************************************************
void	CDXMemBuffer::Write(void *data, SINT size)	// FUNCTION: 0x00548a70
{
	ASSERT(!mReading);
	DWORD bytes_written;
	
	while(size > 0 && mPtr+size>mData+mSize)
	{
		// Would run off end of buffer - write out buffer

		// But because of the CRC we have to write out precise buffer-size blocks.
		SINT bytes_to_write = mSize - (mPtr - mData);
		memcpy(mPtr, data, bytes_to_write);
		
		// and then the actual data (retail: no CRC data, 00548ac1).
		if (!WriteBlock(mFileName, mFile, mData, mSize, &bytes_written))
		{
			TRACE("Write failed\n");
			DWORD i = GetLastError();
		}
		
		mDataSize=0;
		mPtr=mData;
		size -= bytes_to_write;
		mPos += bytes_to_write;
		data = (void *)(((char *)data) + bytes_to_write);
	}
	
	if (size > 0)
	{
		// Buffer the last bit.
		memcpy(mPtr, data, size);
		mPtr += size;
		mDataSize += size;
		mPos+=size;		
	}
}

//******************************************************************************************
BOOL	CDXMemBuffer::Close()	// FUNCTION: 0x00548c00
{
	if( mData )
	{
		if (mReading)
		{
			// just free up - easy
			CloseHandle(mFile);
//			fclose( mFile );
			delete [] mData;
			mData = NULL;
			delete [] mCRCData;
			mCRCData = NULL;
			return TRUE;
		}

		// otherwise write out the stream.

//		fwrite(mData, mDataSize, 1, mFile);
		DWORD bytes_written;
		if (!WriteBlock(mFileName, mFile, mData, mDataSize, &bytes_written))
		{
			TRACE("Write failed");
		}

		// retail: no CRC file (00548cfb)
		CloseHandle(mFile);
//		fclose(mFile);

		// free up
		delete [] mData;
		mData = NULL;

		return TRUE;
	}

	return FALSE;
}

//******************************************************************************************
void	CDXMemBuffer::DeclareInvalidData(CThing *t)
{
	char	text[300];

	sprintf(text, "Invalid data in Thing");  // can't link _GetClassName() without editor...
//	CONSOLE.AddString(text);
}

//******************************************************************************************

BOOL	CDXMemBuffer::EndOfFile()	// FUNCTION: 0x00548d30
{
	return( mEOF );
}

//******************************************************************************************

#endif
