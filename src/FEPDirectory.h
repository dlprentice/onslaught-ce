// FEPDirectory.h: the PC's list of save files, for loading one or deleting one (reconstructed; not in Stuart's
// release). 0x4014 bytes at FRONTEND + 0x4834, vtable 005db808.

#ifndef FEPDIRECTORY_H
#define FEPDIRECTORY_H

#include "frontendpage.h"

#define MAX_DIRECTORY_SAVES		0x1000

class CFEPDirectory : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);

	void	RefreshSaveFileList(EFEPState state);
	WCHAR	*RenderSaveFileList(float transition, EFrontEndPage dest);	// 0051ae70: the clicked save's name, or NULL
									// (CFEPVirtualKeyboard::Render copies it, 0052118f)

	WCHAR	*mSaves[MAX_DIRECTORY_SAVES];		// +0x04
	SINT	mNumSaves;							// +0x4004
	SINT	mCursor;							// +0x4008
	SINT	mUnknown400c;						// +0x400c
	float	mTime;								// +0x4010: FRONTEND's counter when the cursor last moved
};

#endif
