// FEPCommon.h: the page drawn under every other page (reconstructed; 0xc bytes, FRONTEND + 0x2b0, vtable 005dba68:
// the page's nine slots and RenderAfter).

#ifndef FEPCOMMON_H
#define FEPCOMMON_H

#include "frontendpage.h"

// The help-text lines (CFEPCommon::RenderHelpText's first argument).
enum EFEHelp
{
	FEH_CONTINUE = 4				// CFEPLoadGame::Render (00461e0a)
};

class CFEPCommon : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	Render(float transition, EFrontEndPage dest);

	// the background video, scaled about a point (the default centre is -9999999: the screen's)
	void	RenderVideoBackground(float scale, DWORD colour, float cx = -9999999.f, float cy = -9999999.f);

	virtual void	RenderAfter(float transition, EFrontEndPage dest);		// slot 9 (CFrontEnd::Render 0046842b)

	void	StartVideo(int a);
	void	StopVideo();
	void	RenderSmallBEALogo(float transition);
	void	RenderSmallForsetiText(float transition);
	void	RenderSelectionMarker(float x, float y, float scale, SINT alpha);
	void	RenderHelpText(SINT help, float transition);

	void	*mBuffer;								// +0x04 (freed by Shutdown)
	UBYTE	mUnknown08[0x0c - 0x08];
};

#endif
