// tgaloader.cpp: CTGALoader (reconstructed; Stuart's file is not in the release). From the retail object,
// 004f2c60..004f3219: uncompressed and run-length (types 2 and 10) 24- and 32-bit Targa files. The allocation's
// __LINE__ is the retail one; the #line places it.

#include "common.h"
#include "tgaloader.h"
#include "membuffer.h"

#pragma pack(push, 1)
struct STGAHeader
{
	UBYTE	mIDLength;			// +0x00
	UBYTE	mColourMapType;		// +0x01
	UBYTE	mImageType;			// +0x02: 2 raw, 10 run-length
	UWORD	mColourMapStart;	// +0x03
	UWORD	mColourMapLength;	// +0x05
	UBYTE	mColourMapDepth;	// +0x07
	UWORD	mXOrigin;			// +0x08
	UWORD	mYOrigin;			// +0x0a: nonzero, the rows run bottom up
	UWORD	mWidth;				// +0x0c
	UWORD	mHeight;			// +0x0e
	UBYTE	mDepth;				// +0x10
	UBYTE	mDescriptor;		// +0x11
};
#pragma pack(pop)

//******************************************************************************************
CTGALoader::CTGALoader(char *name, SINT *status) : CImageLoader(name)	// FUNCTION: 0x004f2c60
{
	mStatus = status;
}

//******************************************************************************************
CTGALoader::~CTGALoader()	// FUNCTION: 0x004f2cb0
{
}

//******************************************************************************************
// Defined here, between the destructor and Load: retail emits it there (004f2cc0), after the scalar deleting destructor
// (an inline body in the class would be emitted with the table, before it).
BOOL	CTGALoader::HasAlpha()	// FUNCTION: 0x004f2cc0
{
	return mStatus && *mStatus;
}

//******************************************************************************************
BOOL	CTGALoader::Load()	// FUNCTION: 0x004f2ce0
{
	CDXMemBuffer	mb;
	if (!mb.InitFromFile(GetName(), MEMTYPE_MEMBUFFER, TRUE, 0))
	{
		mb.Close();
		return FALSE;
	}

	STGAHeader	header;
	mb.Read(&header, sizeof(header));

	mDepth = header.mDepth;
	if ((header.mImageType != 2 && header.mImageType != 10) || (header.mDepth != 24 && header.mDepth != 32))
	{
		mb.Close();
		return FALSE;
	}

	mWidth = header.mWidth;
	mHeight = header.mHeight;
	if (!AllocData(mWidth * mHeight * 3))
	{
		mb.Close();
		return FALSE;
	}

	if (header.mDepth == 24)
	{
		if (header.mImageType == 2)
			mb.Read(mData, mWidth * mHeight * 3);
		else if (header.mImageType == 10)
		{
			SINT	left = mWidth * mHeight;
			UBYTE	*d = mData;
			while (left)
			{
				UBYTE	packet;
				mb.Read(&packet, 1);
				SINT	count = (packet & 0x7f) + 1;
				left -= count;
				if (packet & 0x80)
				{
					UBYTE	b, g, r;
					mb.Read(&b, 1);
					mb.Read(&g, 1);
					mb.Read(&r, 1);
					while (count--)
					{
						*d++ = b;
						*d++ = g;
						*d++ = r;
					}
				}
				else
				{
					mb.Read(d, count * 3);
					d += count * 3;
				}
			}
		}
	}
	else
	{
#line 119
		UBYTE	*pixels = new (MEMTYPE_UNKNOWN) UBYTE[mWidth * mHeight * 4];
		if (header.mImageType == 2)
			mb.Read(pixels, mWidth * mHeight * 4);
		else if (header.mImageType == 10)
		{
			SINT	left = mWidth * mHeight;
			UBYTE	*d = pixels;
			while (left)
			{
				UBYTE	packet;
				mb.Read(&packet, 1);
				SINT	count = (packet & 0x7f) + 1;
				left -= count;
				if (packet & 0x80)
				{
					UBYTE	b, g, r, a;
					mb.Read(&b, 1);
					mb.Read(&g, 1);
					mb.Read(&r, 1);
					mb.Read(&a, 1);
					while (count--)
					{
						*d++ = b;
						*d++ = g;
						*d++ = r;
						*d++ = a;
					}
				}
				else
				{
					mb.Read(d, count * 4);
					d += count * 4;
				}
			}
		}

		FreeAlpha();
		AllocAlpha(mWidth * mHeight);

		UBYTE	*d, *a;
		if (header.mYOrigin == 0)
		{
			d = mData;
			a = mAlpha;
		}
		else
		{
			d = mData + (mHeight - 1) * mWidth * 3;
			a = mAlpha + (mHeight - 1) * mWidth;
		}

		UBYTE	*s = pixels;
		for (SINT y = 0; y < mHeight; y++)
		{
			for (SINT x = 0; x < mWidth; x++)
			{
				d[0] = s[0];
				d[1] = s[1];
				d[2] = s[2];
				*a = s[3];
				s += 4;
				d += 3;
				a++;
			}
			if (header.mYOrigin)
			{
				a -= mWidth * 2;
				d -= mWidth * 3 * 2;
			}
		}

		*mStatus = 4;
		delete [] pixels;
	}

	mb.Close();
	return TRUE;
}

//******************************************************************************************
BOOL	CTGALoader::Save24BitWithPitch(char *name, ULONG *data, int width, int height, int pitch)	// FUNCTION: 0x004f3110
{
	STGAHeader	header;
	header.mIDLength = 0;
	header.mColourMapType = 0;
	header.mImageType = 2;
	header.mXOrigin = 0;
	header.mYOrigin = 0;
	header.mWidth = width;
	header.mHeight = height;
	header.mDepth = 24;
	header.mDescriptor = 0;

	FILE	*f = fopen(name, "wb");
	if (f == NULL)
		return FALSE;

	fwrite(&header, sizeof(header), 1, f);

	pitch /= 4;
	for (SINT y = height; y > 0; y--)
	{
		for (SINT x = 0; x < width; x++)
		{
			UBYTE	*c = (UBYTE *)&data[(y - 1) * pitch + x];
			fwrite(&c[0], 1, 1, f);
			fwrite(&c[1], 1, 1, f);
			fwrite(&c[2], 1, 1, f);
		}
	}

	fclose(f);
	return TRUE;
}

// FUNCTION: 0x004f2c90 ??_GCTGALoader@@UAEPAXI@Z
