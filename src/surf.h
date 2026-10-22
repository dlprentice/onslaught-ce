// surf.h: the surf that breaks along the shore (DXSurf.obj, 00556420..00556b77; SURF is defined with the system's
// globals, 004ef9c0). Stuart's release has no copy.

#ifndef SURF_H
#define SURF_H

#ifdef DXSURF_CPP
class CVBuffer;
class CDXTexture;

// A strip of surf along a stretch of shore (0xc bytes): the wave's leading and trailing faces.
struct SSurfStrip
{
	SSurfStrip() { mVB0 = NULL; mVB1 = NULL; }		// 005565a0
	~SSurfStrip();									// 005565b0
	void		Create(class CChunkReader *c);		// 005565d0

	CVBuffer	*mVB0;								// +0x00
	CVBuffer	*mVB1;								// +0x04
	SINT		mCount;								// +0x08: points along the shore
};
#endif

class CDXSurf
{
public:
#if defined(DXSURF_CPP) || defined(SYSTEM_CPP)
	CDXSurf();										// 00556460 (SURF's initializer calls it, 004ef9c5)
	~CDXSurf();										// empty, folded into 0040c640 (SURF's exit, 004ef9e0)
#endif
#if defined(DXSURF_CPP) || defined(DXWATER_CPP)
	void	Render(bool validate);					// 00556a30 (the water passes, 0055bf37, 0055d520)
#endif
	void	Deserialize(class CChunkReader *c);		// 00556490 (CResourceAccumulator::ReadResources 004d77f7)
	void	LoadWaves();							// 00556470 (CDXLandscape::BuildLevelSpecifics 005453ac)
	void	Destroy();								// 005569e0 (CDXLandscape::ShutDown 00544f8b)
#ifdef DXSURF_CPP
	SSurfStrip	*mStrips;							// +0x00
	SINT		mNumStrips;							// +0x04
	CDXTexture	*mWaves;							// +0x08
	BOOL		mLoaded;							// +0x0c
#endif
};

extern CDXSurf SURF;							// 0083d3d0

#endif
