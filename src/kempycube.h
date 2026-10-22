// kempycube.h: the sky cube (only what matched files need so far).

#ifndef KEMPYCUBE_H
#define KEMPYCUBE_H

// 0xa14 bytes (CEngine::Init 00449c2c); its destructor is not virtual (CEngine::Shutdown 00449975).
class CDXKempyCube
{
public:
				CDXKempyCube();					// 00544040
				~CDXKempyCube();				// 00544060
	void		Change(UBYTE number);			// 005441a0
	void		Render();						// 005441b0 (CDXEngine::Render 0053e629)
	void		Load(UBYTE number);				// 005440a0

	static class CVBuffer	*sVB;			// 008aa908: the five faces' quads

	class CDXTexture	*mTextures[5];				// +0x00: one per face, loaded by name
	char		mNames[10][0x100];				// +0x14: the face textures' names (the first five are used)
};
#define CKEMPYCUBE CDXKempyCube

// The sky's extent (CDXEngine::Render: sqrt of 225,065,536, 0053e5eb). The product of the cell size and radius is
// retail's; how it splits into the two is open.
#define SKY_HEIGHT		256
#define SKY_CELL_SIZE	1500
#define SKY_RAD			10

#endif
