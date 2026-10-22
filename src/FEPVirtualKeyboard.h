// FEPVirtualKeyboard.h: the name-entry keyboard page (reconstructed; 0x6f8 bytes, FRONTEND + 0x413c, vtable 005db830).
// CFEPLoadGame copies the save name into m_strData (+0x4, 008a1898) and puts the cursor at its end (m_iPos, +0x44,
// 008a18d8). The keys are three layouts (letters, symbols, accents) of five rows of up to 14 keys; a key's token is
// its character, or one of the VKT_ commands below (VK_ is windows.h's).

#ifndef FEPVIRTUALKEYBOARD_H
#define FEPVIRTUALKEYBOARD_H

#include "frontendpage.h"

enum
{
	VKT_LEFT = 1,
	VKT_RIGHT,
	VKT_BACKSPACE,
	VKT_SHIFT,
	VKT_CAPSLOCK,
	VKT_ALPHABET,
	VKT_SYMBOLS,
	VKT_ACCENTS,
	VKT_DONE
};

struct SVKKey
{
	WORD	mToken;							// +0: 0 ends the row
	DWORD	mWidth;							// +4: in pixels
};

class CFEPVirtualKeyboard : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);

	void	SetDefaultName();				// 0051fff0: a save name no existing save uses (name open)
	void	MakeDefaultName();				// its body, inlined into TransitionNotification (00520162..00520271)
	void	InitKeyboardLayout();			// 00520530
	void	HandleKey(WCHAR key);			// 00520cc0
	void	MoveToRow(int row);				// 00520f70: keeps the cursor's x, skipping disabled keys
	BOOL	IsKeyDisabled();				// 005214d0: shift and caps on the symbols, and DONE with no name
	void	DrawPanel(float y, float transition, SINT alpha);	// 00521260: the name box, the name and its cursor

	WCHAR	m_strData[32];					// +0x04
	int		m_iPos;							// +0x44
	bool	m_Fresh;						// +0x48 (CFEPSaveGame::CreateSave clears it, 004650fc)
	BOOL	m_bCapsLock;					// +0x4c
	BOOL	m_bShift;						// +0x50: cleared after each character
	SVKKey	m_Keys[3][5][14];				// +0x54
	int		m_iMode;						// +0x6e4
	int		m_iRow;							// +0x6e8
	int		m_iCol;							// +0x6ec
	int		m_iUnknown6f0;					// +0x6f0
	float	m_fColFraction;					// +0x6f4: where across its key the cursor is, for moving between rows
};

#endif
