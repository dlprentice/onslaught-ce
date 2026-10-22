// tgaloader.h: CTGALoader, the Targa reader (tgaloader.obj, 004f2c60..004f3219; vtable 005df518). Not in Stuart's
// release: reconstructed from the retail object.

#ifndef TGALOADER_H
#define TGALOADER_H

#include "imageloader.h"

// 0x11c bytes.
class CTGALoader : public CImageLoader
{
public:
					CTGALoader(char *name, SINT *status);	// 004f2c60
					~CTGALoader();							// 004f2cb0
	virtual BOOL	Load();									// 004f2ce0
	// 8: 004f2cc0. Load sets the status to 4 when a 32-bit file gave an alpha channel (004f30c9).
	virtual BOOL	HasAlpha();

	SINT			*mStatus;								// +0x118
	static BOOL		Save24BitWithPitch(char *name, ULONG *data, int width, int height, int pitch);	// 004f3110
};

#endif
