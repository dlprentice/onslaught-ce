// Platform.h: the platform object and its fonts (only what matched files need so far).

#ifndef PLATFORM_H
#define PLATFORM_H

#include <d3d9.h>
#include "keytrap.h"

// Stuart's Platform.h
enum EQuitType
{
	QT_NONE	= 0,
	QT_QUIT_TO_FRONTEND,
	QT_QUIT_TO_SYSTEM,
	QT_LOAD_ERROR,
	QT_RESTART_LEVEL,
	QT_QUIT_TIMEOUT,
	QT_USER_QUIT_TO_FRONTEND,
	QT_USER_QUIT_TO_TITLE_SCREEN
};

enum EFontType
{
	FONT_NORMAL,
	FONT_SMALL,
	FONT_DEBUG,
	FONT_TITLE,
};

class CBitmapFont
{
public:
	virtual int		RemapChar(WCHAR c) = 0;			// retail: slot 0 (returns an int: mov eax,imm at 005409c3) (Stuart's EnableCharSwapHack's swap)
};

// Stuart's word-wrap result: lines of 100 characters (CGame::DrawGameStuff steps 0xc8 bytes a line, 00471e9b)
struct SWordWrapFill
{
	WCHAR	c[40][100];			// 40 lines: CGame::DrawGameStuff's frame (0x24a0, 004714c6)
};

// Stuart's name (game.cpp): the shadowed draw at scale 1, a direct call (CGame::DrawGameStuff pushes the arguments
// before it fetches the font, 004717f4..00471818)
#define DrawTextShadowed(x, y, colour, text)	DrawTextWithShadow(x, y, colour, text, 0, 0, 1.f, 1.f)

// RTTI: CDXBitmapFont (vtable 005e504c), 0x1180 bytes; its destructor is not virtual (0053f770). DXFont.cpp: the DirectX
// sample framework's CD3DFont reworked to draw from a font texture or from a GDI font drawn into a texture.
class CDXBitmapFont : public CBitmapFont
{
public:
					CDXBitmapFont();					// 0053f730
					~CDXBitmapFont();					// 0053f770
	virtual int		RemapChar(WCHAR c);					// 00465cf0
	static void		InitCharMap();						// 00465c10: the remap tables (the constructor calls it)
	void	InitialiseAsBitmapFont(char *name, int size);				// 0053f830
	void	InitialiseAsSysFont(TCHAR *name, int size, int flags);		// 0053f7d0
	void	Deserialize(class CChunkReader *c);						// 00540840
	void	EnableCharSwapHack() { mCharSwapHack = TRUE; }
	HRESULT	DrawText(float x, float y, DWORD colour, WCHAR *text, int a = 0, int b = 0, float z = 1.0f);	// 00540640
	int		GetHeight() { return mHeight; }
	// the positioned, scaled, wrapped draw (00465710) and the text's width in pixels (00540680)
	void	DrawTextDynamic(float x, float y, float z, float scalex, float scaley, DWORD colour, WCHAR *text, float wrap_width, int a = 0, int b = 0);
	BOOL	GetTextExtent(WCHAR *text, SIZE *size);
	HRESULT	DrawTextScaled(float x, float y, float z, float scalex, float scaley, DWORD colour, WCHAR *text, int flags, DWORD *colours);	// 00540010 (name open; CFEPIntro::Render)
	BOOL	CanDisplay(WCHAR c);											// 00465dd0 (CFEPVirtualKeyboard)
	void	DrawTextWithShadow(float x, float y, DWORD colour, WCHAR *text, int a = 0, float b = 1.f, float c = 1.f, float d = 1.f);	// 004659a0
	int		WordWrap(struct SWordWrapFill *fill, WCHAR *text, float width);	// 00465a20 (Stuart's name): lines of 100 characters, returns the count
	void	InitFromTexture();						// 0053f880: measures each character's cell in the font texture
	void	InitFromSysFont();						// 0053fb00: draws the GDI font into a new texture
	BOOL	IsValid();								// 00540970

	char			mFontName[0x50];		// +0x04 (Deserialize reads 0x50 bytes)
	DWORD			mHeight;				// +0x54
	DWORD			mFlags;					// +0x58: 1 bold, 2 italic
	char			mTextureName[0x100];	// +0x5c
	bool			mSysFont;				// +0x15c
	DWORD			mTexWidth;				// +0x160
	DWORD			mTexHeight;				// +0x164
	BOOL			mCharSwapHack;			// +0x168
	DWORD			mUnknown16c;			// +0x16c: set while CHelpTextDisplay::Render draws for controller configurations 3 and 4 (0047fc5f)
	class CDXTexture		*mTexture;			// +0x170
	class CVBufTexture	*mVBufTexture;		// +0x174
	class CVBufTexture	*mVBufTexture2;		// +0x178
	float			mTextScale;				// +0x17c
	float			mTexCoords[256][4];		// +0x180
};

// RTTI: the debug font (vtable 005e4980)
class CDXBitmapDebugFont : public CDXBitmapFont
{
public:
	virtual int		RemapChar(WCHAR c);					// 00540990
};
#define CBITMAPFONT CDXBitmapFont

class	CFrameTimer;
class	CViewport;

class	CPlatform
{
public:
	// Platform.cpp (Stuart's CPlatform::Flip)
	void		Flip(BOOL a = FALSE);				// 004d2580: presents, then resets the display (and saves the options) when asked
	// 004d2600: copies the path, cuts it after its last '\\' when told to, and makes the folders it names
	// (called by CCLIParams::GetParams for -autoconfigtest's folder).
	void		CreateDirectoryPath(char *path, BOOL strip_file_name);
};

class CPCPlatform : public CPlatform
{
public:
	void	TriggerRumble(int pad);			// Stuart's (BattleEngine.cpp); not placed yet

	CPCPlatform()
	{
		mFrameTimer = NULL;
		mFont=NULL;
		mDebugFont=NULL;
		mSmallFont=NULL;
		mTitleFont=NULL;
		mXboxFont=NULL;
		mSmallXboxFont = NULL;
	}
	BOOL		Init();								// 005154e0
	void		Shutdown();							// 005157b0
	EQuitType	Process();							// 00515880
	BOOL		BeginScene();						// 005158c0
	void		EndScene();							// 005158e0
	void		DeviceFlip(BOOL in_game);			// 005158f0
	void		ClearScreen(DWORD col);				// 00515910
	SINT		GetScreenWidth();
	float		GetFPS();							// 00515950
	float		GetSysTimeFloat();					// 005159e0
	BOOL		KeyOn(SINT c);						// 00515970
	BOOL		KeyOnce(SINT c);					// 00515980
	void		FlushInputBuffers();				// 005159b0
	void		SetKeytrap(pKeyTrapper trap);		// 005159c0
	void		SetRumbleEnabled(BOOL enabled);
	void		SetMemorySize(long size) { mMemorySize=size; };
	long		GetMemorySize() { return(mMemorySize);	};
	CBITMAPFONT	*Font( EFontType aFontType );		// 00515a70
	void		MakeD3DViewport(D3DVIEWPORT9 *out,CViewport *in);	// retail: inline (viewport.h)
	void		SetVertexShadersEnabled( BOOL aShaders );
	void		SetViewport(CViewport *vp);			// 00515ab0
	int			GetWindowWidth();					// 00515940
	int			GetWindowHeight();					// 00515b00
	CBITMAPFONT*			Font() { return mFont; };
	CBITMAPFONT*			DebugFont() { return mDebugFont; };
	CBITMAPFONT*			SmallFont() { return mSmallFont ; };
	CBITMAPFONT*			TitleFont() { return mTitleFont ; };
	void		Deserialize(class CChunkReader *c);	// 00515b10
	void		InitFonts();						// 005155e0
	void		SetRegKey(char *keyname,char *value);	// 00515db0

protected:
	CFrameTimer		*mFrameTimer;					// +0x00
	LARGE_INTEGER	mFrequency;						// +0x08
	float			mClockDivisor;					// +0x10
public:
	long			mMemorySize;					// +0x14 (0088a0bc; retail has no mGeforce3)
protected:
	CBITMAPFONT		*mFont;							// +0x18
	CBITMAPFONT		*mDebugFont;					// +0x1c
	CBITMAPFONT		*mSmallFont;					// +0x20
	CBITMAPFONT		*mTitleFont;					// +0x24
	CBITMAPFONT		*mXboxFont;						// +0x28
	CBITMAPFONT		*mSmallXboxFont;				// +0x2c
};

extern CPCPlatform PLATFORM;

WCHAR	*ToWCHAR(char *text);
TCHAR	*ToTCHAR(char *text);				// 004f7c70
char	*FromWCHAR(WCHAR *text);			// 004f7d30
char	*FromTCHAR(TCHAR *text);			// 004f7cd0

#endif
