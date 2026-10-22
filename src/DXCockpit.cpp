// DXCockpit.cpp: the PC cockpit's drawing (CDXCockpit::Render, 0053bb50). Not in Stuart's release; reconstructed
// from the retail code. The object's maths constants open it (0053ba60, 0053ba80).

#include "common.h"
#include "mathsconst.h"	// FUNCTION: 0x0053ba60 init:ZERO_FVECTOR
						// FUNCTION: 0x0053ba80 init:ID_FMATRIX
#include "Cockpit.h"
#include "BattleEngine.h"
#include "RenderThing.h"
#include "meshrenderer.h"
#include "ltshell.h"
#include "engine.h"

//******************************************************************************************
void	CDXCockpit::Render()	// FUNCTION: 0x0053bb50
{
	if (mShouldRender)
	{
		// the cockpit never draws its debug bounds
		BOOL	debug = FALSE;
		if (ENGINE.mDrawDebugStuff & 4)
		{
			ENGINE.ToggleDebugDraw(4);
			debug = TRUE;
		}

		LT.SRS(D3DRS_NORMALIZENORMALS, TRUE);
		if (mRenderThing)
		{
			// a stealthy battle engine's cockpit fades with it
			DWORD	flags = 0;
			if (mBattleEngine->GetStealth() > 5.f)
			{
				MESH_ALPHA = SINT(255.f-(mBattleEngine->GetStealth()/100.0f*255.0f));	// Stuart's spelling (BattleEngine.cpp): one shared COMDAT constant in retail
				flags = 0x40;
			}
			mRenderThing->Render(flags);
			MESH_ALPHA = 255;
		}
		LT.SRS(D3DRS_NORMALIZENORMALS, FALSE);

		if (debug)
			ENGINE.ToggleDebugDraw(4);
	}
}
