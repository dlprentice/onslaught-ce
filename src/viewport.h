// viewport.h: an engine viewport (from CPCPlatform::SetViewport 00515ab0, which copies it field by field into a
// D3DVIEWPORT9).

#ifndef VIEWPORT_H
#define VIEWPORT_H

class CViewport
{
public:
	int		Width;				// +0x00 (Stuart's engine.h: signed; CGame::Render halves them with cdq/sar)
	int		Height;				// +0x04
	int		X;					// +0x08
	int		Y;					// +0x0c
	float	MinZ;				// +0x10
	float	MaxZ;				// +0x14
};

#endif
