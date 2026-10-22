// bytesprite.cpp: CByteSprite (reconstructed; Stuart's file is not in the release). From the retail object,
// 00418470..00418bd6. The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "bytesprite.h"
#include "membuffer.h"

//******************************************************************************************
CByteSprite::CByteSprite()	// FUNCTION: 0x00418470
{
	mData = NULL;
	mFrameOffsets = NULL;
	mNumFrames = 0;
}

//******************************************************************************************
CByteSprite::~CByteSprite()	// FUNCTION: 0x00418480
{
	SAFE_DELETE_ARRAY(mData);
	SAFE_DELETE_ARRAY(mFrameOffsets);
}

//******************************************************************************************
SINT	CByteSprite::Load(char *name, SINT width, SINT height, SINT frames, UBYTE key)	// FUNCTION: 0x004184c0
{
	char	fname[32];
	sprintf(fname, "data\\%s.raw", name);

	CDXMemBuffer	mb;
	if (mb.InitFromFile(fname, MEMTYPE_MEMBUFFER, TRUE, 0))
	{
#line 29
		CByteSpriteEncoder	*enc = new (MEMTYPE_SCRATCHPAD) CByteSpriteEncoder(width, height, frames, key);

		SINT	c0;
		for (c0 = 0; c0 < enc->mFrames; c0++)
		{
			// STAND-IN (labelled 2026-10-15; origin not recorded): the encoder is read through a second pointer
			// for the frame read. Plain `enc->`: score 8/8/8; a file-local ReadFrame(enc, mb) helper 6/18/224. Original: unknown.
			CByteSpriteEncoder	*e = enc;
			if (mb.Read(e->mFrame, e->mWidth * e->mHeight) != e->mWidth * e->mHeight)
				break;
			if (!enc->EncodeFrame())
				break;
		}

		mNumFrames = c0;
		if (mNumFrames)
		{
#line 32
			mData = new (MEMTYPE_BYTESPRITE) UBYTE[enc->mOutPtr - enc->mOut];
			mFrameOffsets = new (MEMTYPE_BYTESPRITE) SINT[mNumFrames];
			memcpy(mData, enc->mOut, enc->mOutPtr - enc->mOut);
			memcpy(mFrameOffsets, enc->mOffsets, mNumFrames * sizeof(SINT));
		}

		delete enc;
	}

	mb.Close();
	return mNumFrames;
}

//******************************************************************************************
void	CByteSprite::SetTarget(void *pixels, SINT pitch, SINT width, SINT height, UBYTE wrap)	// FUNCTION: 0x00418720
{
	mTarget = (UBYTE *)pixels;
	mPitch = pitch;
	mTargetWidth = width;
	mTargetHeight = height;
	mWrap = wrap;
}

//******************************************************************************************
void	CByteSprite::DrawRLE_NoClip(char *s, SINT x, SINT y, SINT h)	// FUNCTION: 0x00418750
{
	UBYTE	*row = mTarget + y * mPitch + x;
	char	c;
	for (; h; h--)
	{
		if (y >= mTargetHeight)
			return;

		if (y < 0)
		{
			while ((c = *s++) != 0)
				if (c > 0)
					s += c;
		}
		else
		{
			UBYTE	*d = row;
			while ((c = *s++) != 0)
			{
				if (c > 0)
				{
					do
					{
						*d++ = *s++;
					} while (--c);
				}
				else
					d -= c;
			}
		}
		y++;
		row += mPitch;
	}
}

//******************************************************************************************
void	CByteSprite::DrawRLE_ClipLeft(char *s, SINT x, SINT y, SINT h)	// FUNCTION: 0x004187e0
{
	UBYTE	*row = mTarget + y * mPitch + x;
	char	c;
	for (; h; h--)
	{
		if (y >= mTargetHeight)
			return;

		if (y < 0)
		{
			while ((c = *s++) != 0)
				if (c > 0)
					s += c;
		}
		else
		{
			SINT	px = x;
			UBYTE	*d = row;
			while ((c = *s++) != 0)
			{
				if (c > 0)
				{
					do
					{
						if (px >= 0)
							*d = *s;
						d++;
						s++;
						px++;
					} while (--c);
				}
				else
				{
					px -= c;
					d -= c;
				}
			}
		}
		y++;
		row += mPitch;
	}
}

//******************************************************************************************
void	CByteSprite::DrawRLE_ClipRight(char *s, SINT x, SINT y, SINT h)	// FUNCTION: 0x00418880
{
	UBYTE	*row = mTarget + y * mPitch + x;
	char	c;
	for (; h; h--)
	{
		if (y >= mTargetHeight)
			return;

		if (y < 0)
		{
			while ((c = *s++) != 0)
				if (c > 0)
					s += c;
		}
		else
		{
			SINT	px = x;
			UBYTE	*d = row;
			while ((c = *s++) != 0)
			{
				if (c > 0)
				{
					do
					{
						if (px < mTargetWidth)
							*d = *s;
						d++;
						s++;
						px++;
					} while (--c);
				}
				else
				{
					px -= c;
					d -= c;
				}
			}
		}
		y++;
		row += mPitch;
	}
}

//******************************************************************************************
void	CByteSprite::DrawFrame(SINT frame, SINT x, SINT y)	// FUNCTION: 0x00418920
{
	if (frame >= mNumFrames)
		return;

	char	*s = (char *)mData + mFrameOffsets[frame];
	x += *s++;
	y += *s++;
	SINT	right = x + *s++;
	SINT	h = *s++;

	if (x >= mTargetWidth || y >= mTargetHeight || right < 0 || y + h < 0)
		return;

	if (x >= 0)
	{
		if (right < mTargetWidth)
		{
			DrawRLE_NoClip(s, x, y, h);
			return;
		}
		DrawRLE_ClipRight(s, x, y, h);
		if (mWrap & 1)
			DrawRLE_ClipLeft(s, x - mTargetWidth, y, h);
	}
	else
	{
		DrawRLE_ClipLeft(s, x, y, h);
		if (mWrap & 1)
			DrawRLE_ClipRight(s, x + mTargetWidth, y, h);
	}
}

//******************************************************************************************
inline CByteSpriteEncoder::CByteSpriteEncoder(SINT width, SINT height, SINT frames, UBYTE key)
{
#line 189
	mFrame = new (MEMTYPE_SCRATCHPAD) UBYTE[width * height];
	mOffsets = new (MEMTYPE_SCRATCHPAD) SINT[frames];
	mOut = new (MEMTYPE_SCRATCHPAD) UBYTE[width * height * frames * 2];
	mOffsetPtr = mOffsets;
	mOutPtr = mOut;
	mWidth = width;
	mHeight = height;
	mFrames = frames;
	mKey = key;
}

//******************************************************************************************
inline CByteSpriteEncoder::~CByteSpriteEncoder()
{
	delete [] mOut;
	delete [] mOffsets;
	delete [] mFrame;
}

//******************************************************************************************
BOOL	CByteSpriteEncoder::EncodeFrame()	// FUNCTION: 0x004189f0
{
	// the bounds of the opaque pixels
	SINT	minX = 10000, minY = 10000, maxX = 0, maxY = 0;
	BOOL	any = FALSE;
	SINT	x, y;
	UBYTE	*row = mFrame;
	for (y = 0; y < mHeight; y++)
	{
		BOOL	rowAny = FALSE;
		for (x = 0; x < mWidth; x++)
		{
			if (row[x] < mKey)
			{
				rowAny = TRUE;
				if (x < minX)
					minX = x;
				if (x > maxX)
					maxX = x;
			}
		}
		if (rowAny)
		{
			any = TRUE;
			if (y < minY)
				minY = y;
			maxY = y;
		}
		row += mWidth;
	}
	if (!any)
		return FALSE;

	maxX++;
	maxY++;

	UBYTE	*out = mOutPtr;
	*out++ = minX - mWidth / 2;
	*out++ = minY - mHeight / 2;
	*out++ = maxX - minX;
	*out++ = maxY - minY;

	row = mFrame + mWidth * minY;
	for (y = minY; y < maxY; y++)
	{
		x = minX;
		UBYTE	*p = row + x;
		while (x < maxX)
		{
			char	*count = (char *)out++;
			char	n = 0;
			while (x < maxX && *p < mKey)
			{
				n++;
				*out++ = *p++;
				x++;
			}
			if (n > 0)
			{
				*count = n;
				if (x == maxX)
					*out++ = 0;
			}
			else
			{
				while (x < maxX && *p >= mKey)
				{
					n--;
					p++;
					x++;
				}
				*count = (x == maxX) ? (char)0 : n;
			}
		}
		row += mWidth;
	}

	*mOffsetPtr++ = mOutPtr - mOut;
	mOutPtr = out;
	return TRUE;
}
