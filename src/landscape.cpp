// landscape.cpp: the object between InitThing.obj and landscapeib.obj (0048ddd0..0048df1a), reconstructed; Stuart's
// release has no copy. It has no maths constants and no file-name string, so its name is open: it holds the memory
// buffer's line helpers, the sky cube's face names and the PC landscape's mixer detail texture. Its data (the mixer
// texture, 0067a7d0) follows InitThing.obj's directly.

#include "common.h"
#include "membuffer.h"
#include "landscape.h"
#include "Texture.h"
#include "CLIParams.h"


//******************************************************************************************
BOOL	CDXMemBuffer::OpenFile(const char *fname)	// FUNCTION: 0x0048ddd0
{
	return InitFromFile(fname, MEMTYPE_MEMBUFFER, TRUE, 0);
}

//******************************************************************************************
BOOL	CDXMemBuffer::CloseFile()	// FUNCTION: 0x0048ddf0
{
	return Close();
}

//******************************************************************************************
void	CDXMemBuffer::ReadLine(char *line, SINT maxchars)	// FUNCTION: 0x0048de00
{
	ReadString(line, maxchars);
	SINT	len = strlen(line);
	if (len && line[len - 1] == '\n')
		line[len - 1] = 0;
}

//******************************************************************************************
// the sky cube's face textures (CDXKempyCube::Load 0054409e)
void	GetCubeTextureName(char *out, UBYTE number, SINT face)	// FUNCTION: 0x0048de30
{
	char	*faces[5] = { "cent", "up", "right", "down", "left" };
	sprintf(out, "cubes\\cube%.2d_%s.tga", number, faces[face]);
}

//******************************************************************************************
CDXTexture	*CLandscape::mMixerTexture;

CLandscape::CLandscape()	// FUNCTION: 0x0048de90
{
	mMixerTexture = NULL;
}

//******************************************************************************************
CLandscape::~CLandscape()	// FUNCTION: 0x0048dea0
{
	if (mMixerTexture)
	{
		mMixerTexture->CTexture::Release();
		mMixerTexture = NULL;
	}
}

//******************************************************************************************
void	CLandscape::LoadMixerTexture(SINT n)	// FUNCTION: 0x0048dec0
{
	char	name[24];
	sprintf(name, "mixers\\detail%.2d.tga", n);
	if (CLIPARAMS.mBuildResources)
		mMixerTexture = CDXTexture::FindTexture(name, 5, 0, -1, 1, 1);
	else
		mMixerTexture = CDXTexture::GetTextureByName(name);
}
