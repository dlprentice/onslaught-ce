// FEPDemoMain.cpp: the playable demo's main menu (reconstructed from the retail PC executable; not in Stuart's
// release). It is CFEPMain with three actions: play, options and quit.

#include	"Common.h"

#include	"FEPDemoMain.h"
#include	"Frontend.h"

//*********************************************************************************
int		CFEPDemoMain::GetNumActions()	// FUNCTION: 0x00457ec0
{
	return 3;
}

//*********************************************************************************
BOOL	CFEPDemoMain::IsActionAvailable(int action)	// FUNCTION: 0x00457ed0
{
	return TRUE;
}

//*********************************************************************************
void	CFEPDemoMain::DoAction()	// FUNCTION: 0x00457ee0
{
	switch (mSelected)
	{
	case 0:
		FRONTEND.SetQuit(201);
		break;

	case 1:
		FRONTEND.SetPage(FEP_OPTIONS, 70);
		break;

	case 2:
		FRONTEND.SetQuit(-1);
		break;

	default:
		CFEPMain::DoAction();
		break;
	}
}

//*********************************************************************************
WCHAR	*CFEPDemoMain::GetActionText(int action)	// FUNCTION: 0x00457f20
{
	switch (action)
	{
	case 0:		return FET.GetText(FEX_NEW_GAME);
	case 1:		return FET.GetText(FEX_OPTIONS);
	default:	return FET.GetText(FEX_QUIT);
	}
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x00457dd0 init:ZERO_FVECTOR
// FUNCTION: 0x00457df0 init:ID_FMATRIX
