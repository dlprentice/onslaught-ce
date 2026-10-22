// OggFileRead.h: the .ogg reader (reconstructed; RTTI COggFileRead 005e4a44, derived from WavRead.h's
// CBaseWaveFileRead). The PC plays the message-box voices from .ogg files.

#ifndef OGGFILEREAD_H
#define OGGFILEREAD_H

#include "WavRead.h"
#include "oggvorbis.h"

class COggFileRead : public CBaseWaveFileRead
{
public:
	COggFileRead();								// 005245a0
	virtual			~COggFileRead();			// 00524600
	virtual HRESULT	Open(CHAR *name);			// 005246a0
	virtual HRESULT	Read(UINT size, BYTE *data, UINT *size_read);	// 00524710
	virtual HRESULT	Close();					// 00524770
#ifdef PCMUSICSTREAM_CPP	// see WavRead.h
	virtual bool	HasFormat();
#else
	virtual BOOL	HasFormat();				// 00524800
#endif
	virtual DWORD	GetSampleRate();			// 00524810
	virtual DWORD	GetChannels();				// 00524820

	// retail (OggFileRead.obj 00523df0..00524827): Xiph's decoder_example.c turned into a reader that decodes
	// on demand, from a file or (never set in retail) a memory block
	int				ReadHeaders();				// 00523df0: 0 ready, 1 no more streams, -1 error
	int				Decode(BYTE *dest, UINT size);	// 00524180: the bytes written, or -1
	int			ReadData();

	ogg_int16_t		mPCM[4096];					// +0x04: decoded, interleaved samples not yet returned
	int				mConvSize;					// +0x2004: samples per channel that fit mPCM
	FILE			*mFile;						// +0x2008
	char			*mMemData;					// +0x200c
	int				mMemSize;					// +0x2010
	ogg_sync_state	mSync;						// +0x2014
	ogg_stream_state mStream;					// +0x2030
	ogg_page		mPage;						// +0x2198
	ogg_packet		mPacket;					// +0x21a8
	vorbis_info		mInfo;						// +0x21c8
	vorbis_comment	mComment;					// +0x21e8
	vorbis_dsp_state mDSP;						// +0x21f8
	vorbis_block	mBlock;						// +0x2268
	UINT			mPCMBytes;					// +0x22d8: bytes waiting in mPCM
	float			**mPCMOut;					// +0x22dc
	int				mEOS;						// +0x22e0
	int				mHeader;					// +0x22e4: the example's i (headers read, then channels)
	int				mResult;					// +0x22e8
	int				mState;						// +0x22ec: 2 while a decoded block is being returned
};

#endif
