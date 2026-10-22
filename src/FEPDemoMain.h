// FEPDemoMain.h: the playable demo's main menu (reconstructed; a CFEPMain, FRONTEND + 0xbde0, vtable 005db7a8).

#ifndef FEPDEMOMAIN_H
#define FEPDEMOMAIN_H

#include "FEPMain.h"

class CFEPDemoMain : public CFEPMain
{
public:
	BOOL	IsActionAvailable(int action);
	int		GetNumActions();
	void	DoAction();
	WCHAR	*GetActionText(int action);
};

#endif
