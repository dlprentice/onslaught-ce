// ControllerDefinition.h: the controls page's key bindings item (RTTI CControllerDefinition, a CMenuItem; vtable 005db404).
// FEPControls.cpp defines it; PauseMenu.cpp builds one for the pause menu's controls page.

#ifndef CONTROLLERDEFINITION_H
#define CONTROLLERDEFINITION_H

class CControllerDefinition : public CMenuItem
{
public:
	CControllerDefinition();								// 00453970 (CPauseMenu calls it, 004ce3d8)
	virtual			~CControllerDefinition();

	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val);
	virtual BOOL	IsEnabled() { return FALSE; }
	virtual void	Render(float x, float y, BOOL selected);			// 00453ad0
	virtual int		GetWidth();
	virtual int		GetHeight();
	virtual CMenuItem	*Clone() { return NULL; }
	virtual bool	IsExpanded();
	virtual void	Refresh();
	void			BeginRemapCapture();						// 00456080
	void			RenderBindingsList(int column, float x, float y, BOOL selected);	// 00455010

	bool			mExpanded;								// +0x1c
	int				mUnknown20;								// +0x20
	int				mUnknown24;								// +0x24: the binding being remapped
	int				mUnknown28;								// +0x28
	CDXTexture		*mTexture;								// +0x2c
};

#endif
