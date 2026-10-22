// FEPDevelopment.h: the developer's level list (reconstructed; 0x14 bytes, FRONTEND + 0x29c, vtable 005dbabc).

#ifndef FEPDEVELOPMENT_H
#define FEPDEVELOPMENT_H

#include "frontendpage.h"

class CFEPDevelopment : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	Render(float transition, EFrontEndPage dest);

	void	RenderWorldList();

	char	**mWorlds;							// +0x04: the world files' names
	int		mNumWorlds;							// +0x08
	int		mLastFirework;						// +0x0c: the counter at the last one
	int		mSelected;							// +0x10
};

#endif
