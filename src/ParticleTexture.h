// ParticleTexture.h: particle textures (only what matched files need). The PC build's particle textures rebuild
// and free their surfaces together (0054fde0, 0054fd80), called by the shell's device handlers.

#ifndef PARTICLETEXTURE_H
#define PARTICLETEXTURE_H

class CDXTexture;
class CVBufTexture;

// A particle texture by name and mode, kept on one list (0x1a4 bytes; the head is 009c64d0). Each has its own vertex
// buffer; the particles fill it and RenderAll draws them all, one draw each (DXParticleTexture.cpp).
// The platform-independent part (the PC's destructor keeps an unwind frame for it, 00550110).
class CParticleTexture
{
public:
	~CParticleTexture() {}
};

class CDXParticleTexture : public CParticleTexture
{
public:
	CDXParticleTexture() { mNext = NULL; mName[0] = 0; mVBuf = NULL; mTexture = NULL; mVertex = -1; }
	~CDXParticleTexture();						// 00550110

	static CDXParticleTexture	*Get(char *name, SINT mode);	// 0054fbc0: finds or loads it
	static void		RecreateAllSurfaces();		// 0054fde0
	static void		FreeAllSurfaces();			// 0054fd80
	static void		DestroyAll();				// 0054fee0
	static void		RenderAll();				// 0054ff20

	void			AddTriangle(UWORD a, UWORD b, UWORD c);		// 00550180
	UWORD			*GetIndices(SINT count);	// 005501b0
	SINT			GetNextVertex();			// 005501d0: the index the next vertex gets
	void			AddVertex(void *vertex);	// 005501e0
	void			GetVertices(SINT count, void **verts, SINT *first);	// 00550200: room for count vertices
	void			Render();					// 00550220

	char				mName[0xc8];		// +0x00: the path the archive names
	char				mShortName[0xc8];	// +0xc8: after the last backslash
	SINT				mMode;				// +0x190: the descriptor's blend mode + 1
	CDXTexture			*mTexture;			// +0x194
	CVBufTexture		*mVBuf;				// +0x198
	SINT				mVertex;			// +0x19c: the last vertex added, -1 after a draw
	CDXParticleTexture	*mNext;				// +0x1a0
};
#define CPARTICLETEXTURE CDXParticleTexture

#include "tweak.h"
extern CTweakSLONG	EXTRA_GEFORCE_FX_FEATURES;	// 009c6480 (its initializer 0054fb60; CGame::Render's reflection pass)

#endif
