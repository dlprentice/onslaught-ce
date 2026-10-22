// imageloader.h: CImageLoader, the base of the image-file readers (imageloader.obj, 00488620..004887f0; vtable
// 005dbedc). Not in Stuart's release: reconstructed from the retail object. 0x118 bytes. The pixel buffers are the
// ones CMapTex::LoadTexture reads (004911ef..00491276).

#ifndef IMAGELOADER_H
#define IMAGELOADER_H

class CImageLoader
{
public:
					CImageLoader(char *name);						// 00488620
	virtual			~CImageLoader();								// 00488700

	virtual BOOL	Load() = 0;										// 1

	virtual char	*GetName()		{ return mName; }				// 2: 00488670
	virtual SINT	GetWidth()		{ return mWidth; }				// 3: the shared 0052f540
	virtual SINT	GetHeight()		{ return mHeight; }				// 4: 00488680
	virtual SINT	GetDepth()		{ return mDepth; }				// 5: 00488690
	virtual UBYTE	*GetData()		{ return mData; }				// 6: the shared 00453a60
	virtual UBYTE	*GetAlpha()		{ return mAlpha; }				// 7: the shared 004de070
	virtual BOOL	HasAlpha()		{ return FALSE; }				// 8: the shared 00405930

	virtual void	FreeData();										// 9: 00488740
	virtual void	FreeAlpha();									// 10: 00488760
	virtual BOOL	AllocData(SINT size);							// 11: 00488780
	virtual BOOL	AllocAlpha(SINT size);							// 12: 004887c0

	SINT			mWidth;							// +0x04: CDamageTexture::Load's switch reads it (00440cbd)
	SINT			mHeight;						// +0x08
	SINT			mDepth;							// +0x0c: bits per pixel
	UBYTE			*mData;							// +0x10: 24-bit texels
	UBYTE			*mAlpha;						// +0x14: 8-bit alpha, NULL without
	char			mName[256];						// +0x18: the constructor copies the name (00488643)
};

#endif
