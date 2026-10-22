// NvTriStrip.h: the NVIDIA triangle stripper the game links (library code at 0056eb50..; not decompiled). Only the
// declarations its callers use: landscapeib.cpp, and CDXMeshVB::BuildStaticVB (0054c372..0054c5d9). PrimitiveGroup's
// destructor is the header's inline (landscapeib.obj 0048e310).

#ifndef NVTRISTRIP_H
#define NVTRISTRIP_H

enum PrimType
{
	PT_LIST,
	PT_STRIP,
	PT_FAN
};

struct PrimitiveGroup
{
	PrimType		type;
	unsigned int	numIndices;
	unsigned short	*indices;

	PrimitiveGroup() : type(PT_STRIP), numIndices(0), indices(NULL) {}
	~PrimitiveGroup()
	{
		if (indices)
			delete [] indices;
		indices = NULL;
	}
};

void	SetCacheSize(const unsigned int cacheSize);			// 0056eb60
void	SetStitchStrips(const bool bStitchStrips);			// 0056eb70
void	SetMinStripSize(const unsigned int minSize);		// 0056eb80
void	SetListsOnly(const bool bListsOnly);				// 0056eb50
void	GenerateStrips(const unsigned short *in_indices, const unsigned int in_numIndices, PrimitiveGroup **primGroups,
			unsigned short *numGroups);						// 0056eb90

#endif
