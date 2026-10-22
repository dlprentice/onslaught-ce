// maptex.h: the landscape's mixer textures (only what matched files need so far).

#ifndef MAPTEX_H
#define MAPTEX_H

#define MT_NUMTEX	6					// CEngine::LoadMixers pushes 6 (0044a1fc)

// 0x4c bytes; CEngine::Init allocates N_MAPMIPS with a count (00449ae3).
class CMapTex
{
public:
				CMapTex();						// 00491160
				~CMapTex();						// 00491180
	BOOL		LoadTexture(char *name, int size, int n);	// 004911c0
	void		Downsample(UBYTE *dest, UBYTE *src);	// 00491340: each texel the average of a 2 x 2 square
	BOOL		LoadAll(int set, int num, int size);	// 004914b0
	void		LoadAllFromMapTex(CMapTex *from);		// 004915d0
	void		Deserialize(class CChunkReader *c, int n);	// 004916c0

	UBYTE		*mData;							// +0x00: mNum textures of mTexSize bytes, 4 per texel
	SINT		mUnknown04;						// +0x04
	DWORD		*mUnknown08;					// +0x08: mNum * 1024 DWORDs (Deserialize reads a quarter of them)
	SWORD		mSet;							// +0x0c: -1 when empty
	SINT		mTexSize;						// +0x10
	SINT		mNum;							// +0x14
	SINT		mSize;							// +0x18: width in texels
	SINT		mMinAlpha[MT_NUMTEX];			// +0x1c
	SINT		mMaxAlpha[MT_NUMTEX];			// +0x34
};

#endif
