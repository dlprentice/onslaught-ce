// DXLandscape.h: the inline parts of the PC landscape (reconstructed; Stuart's release has no copy). Its only
// allocation is at line 170 (CLandscapeView::CreateMipLevels 00544908).

#ifndef DXLANDSCAPE_H
#define DXLANDSCAPE_H

#include "landscape.h"

#line 167
inline void SLandscapeLOD::Init(SINT count)
{
	mSize = count;
	mData = new(MEMTYPE_LANDSCAPE) UWORD[count];
}

#endif
