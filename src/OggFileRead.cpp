// OggFileRead.cpp: the .ogg voice reader (not in Stuart's release; rebuilt from the retail object after mouse.obj,
// 00523df0..00524827). The decoding is Xiph.Org's decoder_example.c (libvorbis 1.0, BSD) turned into a class:
// fprintf(stderr, ...) became TRACE, exit(1) a -1 return, stdin a file or memory block, stdout the caller's
// buffer, and the decode loop returns whenever the buffer is full and resumes where it left off.

#include "common.h"
#include "OggFileRead.h"

//******************************************************************************************
int	COggFileRead::ReadData()
{
	// the libvorbis example's buffer step: ask the sync layer for 4k, fill it, tell it how much came (retail's ogg.dll
	// pull order records ogg_sync_wrote and ogg_sync_buffer first, from this helper, before ReadHeaders' own calls)
	char	*buffer = ogg_sync_buffer(&mSync, 4096);
	int bytes;

	if (mFile)
		bytes = fread(buffer, 1, 4096, mFile);
	else
	{
		bytes = mMemSize;
		if (bytes >= 4096)
			bytes = 4096;
		memcpy(buffer, mMemData, bytes);
		mMemSize -= bytes;
		mMemData += bytes;
	}
	ogg_sync_wrote(&mSync, bytes);
	return bytes;
}

//******************************************************************************************
int	COggFileRead::ReadHeaders()	// FUNCTION: 0x00523df0
{
	char	*buffer;
	int		bytes;

	mEOS = 0;

	/* grab some data at the head of the stream.  We want the first page
	   (which is guaranteed to be small and only contain the Vorbis
	   stream initial header) We need the first page to get the stream
	   serialno. */

	/* submit a 4k block to libvorbis' Ogg layer */
	bytes = ReadData();

	/* Get the first page. */
	if (ogg_sync_pageout(&mSync, &mPage) != 1)
	{
		/* have we simply run out of data?  If so, we're done. */
		if (bytes < 4096)
			return 1;

		/* error case.  Must not be Vorbis data */
		TRACE("Input does not appear to be an Ogg bitstream.\n");
		return -1;
	}

	/* Get the serial number and set up the rest of decode. */
	/* serialno first; use it to set up a logical stream */
	ogg_stream_init(&mStream, ogg_page_serialno(&mPage));

	vorbis_info_init(&mInfo);
	vorbis_comment_init(&mComment);
	if (ogg_stream_pagein(&mStream, &mPage) < 0)
	{
		TRACE("Error reading first page of Ogg bitstream data.\n");
		return -1;
	}

	if (ogg_stream_packetout(&mStream, &mPacket) != 1)
	{
		TRACE("Error reading initial header packet.\n");
		return -1;
	}

	if (vorbis_synthesis_headerin(&mInfo, &mComment, &mPacket) < 0)
	{
		TRACE("This Ogg bitstream does not contain Vorbis audio data.\n");
		return -1;
	}

	mHeader = 0;
	while (mHeader < 2)
	{
		while (mHeader < 2)
		{
			mResult = ogg_sync_pageout(&mSync, &mPage);
			if (mResult == 0)
				break; /* Need more data */
			if (mResult == 1)
			{
				ogg_stream_pagein(&mStream, &mPage);
				while (mHeader < 2)
				{
					mResult = ogg_stream_packetout(&mStream, &mPacket);
					if (mResult == 0)
						break;
					if (mResult < 0)
					{
						TRACE("Corrupt secondary header.  Exiting.\n");
						return -1;
					}
					vorbis_synthesis_headerin(&mInfo, &mComment, &mPacket);
					mHeader++;
				}
			}
		}
		bytes = ReadData();
		if (bytes == 0 && mHeader < 2)
		{
			TRACE("End of file before finding all Vorbis headers!\n");
			return -1;
		}
	}

	{
		char **ptr = mComment.user_comments;
		while (*ptr)
		{
			TRACE("%s\n", *ptr);
			++ptr;
		}
		TRACE("\nBitstream is %d channel, %ldHz\n", mInfo.channels, mInfo.rate);
		TRACE("Encoded by: %s\n\n", mComment.vendor);
	}

	mConvSize = 4096 / mInfo.channels;

	vorbis_synthesis_init(&mDSP, &mInfo);
	vorbis_block_init(&mDSP, &mBlock);
	return 0;
}

//******************************************************************************************
int	COggFileRead::Decode(BYTE *dest, UINT size)	// FUNCTION: 0x00524180
{
	int		total = 0;
	int		samples;
	UINT	bout;
	int		result;

	// A guard and then the condition: the nested test Stuart writes (his BattleEngine.cpp:1002 and IsStandingOnAMoveingObject), so
	// natural; a single `if (mState == 2)` loses 00524180. The `goto resume` into the decode loops is the decoder's coroutine step
	// and stays a STAND-IN (layout).
	if (mState)
	{
		if (mState == 2)
			goto resume;
	}

	ogg_sync_init(&mSync);

	while (1)
	{
		result = ReadHeaders();
		if (result == -1)
			return -1;
		if (result == 1)
			break;

		while (!mEOS)
		{
			while (!mEOS)
			{
				mResult = ogg_sync_pageout(&mSync, &mPage);
				if (mResult == 0)
					break; /* need more data */
				if (mResult < 0)
				{ /* missing or corrupt data at this page position */
					TRACE("Corrupt or missing data in bitstream; continuing...\n");
				}
				else
				{
					ogg_stream_pagein(&mStream, &mPage); /* can safely ignore errors at this point */
					while (1)
					{
						mResult = ogg_stream_packetout(&mStream, &mPacket);

						if (mResult == 0)
							break; /* need more data */
						if (mResult < 0)
						{ /* missing or corrupt data at this page position */
							/* no reason to complain; already complained above */
						}
						else
						{
							/* we have a packet.  Decode it */
							if (vorbis_synthesis(&mBlock, &mPacket) == 0) /* test for success! */
								vorbis_synthesis_blockin(&mDSP, &mBlock);

							while ((samples = vorbis_synthesis_pcmout(&mDSP, &mPCMOut)) > 0)
							{
								bout = samples;
								if (samples > mConvSize)
									bout = mConvSize;
								if (mInfo.channels * bout * 2 > size)
								{
									if (size)
										bout = (size / 2) / mInfo.channels;
									else
									{
										mPCMBytes = 0;
										mState = 2;
										goto resume;
									}
								}

								/* convert floats to 16 bit signed ints (host order) and interleave */
								for (mHeader = 0; mHeader < mInfo.channels; mHeader++)
								{
									ogg_int16_t *ptr = mPCM + mHeader;
									float *mono = mPCMOut[mHeader];
									for (UINT j = 0; j < bout; j++)
									{
										int val = mono[j] * 32767.f;
										/* might as well guard against clipping */
										if (val > 32767)
											val = 32767;
										else if (val < -32768)
											val = -32768;
										*ptr = val;
										ptr += mInfo.channels;
									}
								}

								vorbis_synthesis_read(&mDSP, bout); /* tell libvorbis how many samples we actually consumed */
								mPCMBytes = mInfo.channels * bout * 2;
								mState = 2;

							resume:
								if (mPCMBytes >= size)
								{
									memcpy(dest, mPCM, size);
									if (mPCMBytes - size)
										memmove(mPCM, &mPCM[size], mPCMBytes - size);
									mPCMBytes -= size;
									return total + size;
								}
								if (mPCMBytes)
									memcpy(dest, mPCM, mPCMBytes);
								size -= mPCMBytes;
								dest += mPCMBytes;
								total += mPCMBytes;
								mPCMBytes = 0;
							}
						}
					}
					if (ogg_page_eos(&mPage))
						mEOS = 1;
				}
			}
			if (!mEOS)
			{
				int bytes = ReadData();
				if (bytes == 0)
					mEOS = 1;
			}
		}

		/* clean up this logical bitstream; before exit we see if we're followed by another [chained] */
		ogg_stream_clear(&mStream);

		/* ogg_page and ogg_packet structs always point to storage in libvorbis.  They're never freed or manipulated
		   directly */
		vorbis_block_clear(&mBlock);
		vorbis_dsp_clear(&mDSP);
		vorbis_comment_clear(&mComment);
		vorbis_info_clear(&mInfo);  /* must be called last */
	}

	/* OK, clean up the framer */
	ogg_sync_clear(&mSync);

	TRACE("Done.\n");
	mState = 0;
	fclose(mFile);
	mFile = NULL;
	return total;
}

//******************************************************************************************
COggFileRead::COggFileRead()	// FUNCTION: 0x005245a0
{
	mEOS = 0;
	mState = 0;
	mConvSize = 4096;
	mFile = NULL;
	mPCMBytes = 0;
	mResult = 0;
}

//******************************************************************************************
// FUNCTION: 0x005245e0 ??_GCOggFileRead@@UAEPAXI@Z
COggFileRead::~COggFileRead()	// FUNCTION: 0x00524600
{
	if (mFile)
	{
		ogg_stream_clear(&mStream);
		mState = 0;
		vorbis_block_clear(&mBlock);
		vorbis_dsp_clear(&mDSP);
		vorbis_comment_clear(&mComment);
		vorbis_info_clear(&mInfo);
		ogg_sync_clear(&mSync);
		fclose(mFile);
		mFile = NULL;
		if (mMemData)
		{
			mMemData = NULL;
			mMemSize = 0;
		}
	}
}

//******************************************************************************************
HRESULT	COggFileRead::Open(CHAR *name)	// FUNCTION: 0x005246a0
{
	Close();
	mFile = fopen(name, "rb");
	if (mFile)
	{
		if (Decode(NULL, 0) != -1)
			return S_OK;
		ogg_sync_clear(&mSync);
		fclose(mFile);
		mFile = NULL;
	}
	return E_FAIL;
}

//******************************************************************************************
HRESULT	COggFileRead::Read(UINT size, BYTE *data, UINT *size_read)	// FUNCTION: 0x00524710
{
	if (!mFile && !mMemSize)
	{
		*size_read = 0;
		return E_FAIL;
	}

	*size_read = Decode(data, size);
	if (*size_read == -1)
	{
		*size_read = 0;
		return E_FAIL;
	}
	return S_OK;
}

//******************************************************************************************
HRESULT	COggFileRead::Close()	// FUNCTION: 0x00524770
{
	if (mFile)
	{
		ogg_stream_clear(&mStream);
		mState = 0;
		vorbis_block_clear(&mBlock);
		vorbis_dsp_clear(&mDSP);
		vorbis_comment_clear(&mComment);
		vorbis_info_clear(&mInfo);
		ogg_sync_clear(&mSync);
		fclose(mFile);
		mFile = NULL;
	}
	if (mMemData)
	{
		mMemData = NULL;
		mMemSize = 0;
	}
	return S_OK;
}

//******************************************************************************************
BOOL	COggFileRead::HasFormat()	// FUNCTION: 0x00524800
{
	return mFile != NULL;
}

DWORD	COggFileRead::GetSampleRate()	// FUNCTION: 0x00524810
{
	return mInfo.rate;
}

DWORD	COggFileRead::GetChannels()	// FUNCTION: 0x00524820
{
	return mInfo.channels;
}
