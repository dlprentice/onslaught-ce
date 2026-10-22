// RenderMethod.cpp: CRenderMethod, a console tweak that chooses between a renderer's methods (not in Stuart's
// release; rebuilt from the retail object 00527c90..00527e5b, which has no maths initializers; the file name is
// not known). A renderer tries its methods from the best down; the device's ValidateDevice, or a vertex buffer
// check, rejecting the current one steps down to the next, and the last one is always accepted. Every step is
// logged to setuphistory.txt.

#include "common.h"
#include "tweak.h"
#include "ltshell.h"
#include "DebugLog.h"

extern bool	VBUFTEXTURE_VALIDATE_FAILED;			// 00854dd8 (vbuftexture.cpp)
extern bool	VBUFTEXTURE_VALIDATE;					// 00854dd9

//******************************************************************************************
CRenderMethod::CRenderMethod(char *name, int count) : CTweak(name)	// FUNCTION: 0x00527c90
{
	mName = name;
	mMethod = count - 1;
	mAccepted = FALSE;
}

//******************************************************************************************
bool	CRenderMethod::Try(int method)	// FUNCTION: 0x00527cc0
{
	if (mMethod != method)
		return false;

	if (!mAccepted)
		SETUP_HISTORY.AddMessage("RM: First-time attempt at '%s:%d' starting", mName, method);
	return true;
}

//******************************************************************************************
void	CRenderMethod::Set(float value)	// FUNCTION: 0x00527d00
{
	mMethod = int(value);
	mAccepted = TRUE;
}

//******************************************************************************************
bool	CRenderMethod::Validate()	// FUNCTION: 0x00527d20
{
	if (!mAccepted)
	{
		HRESULT hr;
		if (mMethod == 0)
		{
			hr = LT.D3D_ValidateDevice();
			if (FAILED(hr))
				SETUP_HISTORY.AddMessage("RM: Failed ValidSoFar on '%s:%d' because %s, accepting anyway.", mName, mMethod, DXGetErrorString9(hr));
		}
		else
		{
			hr = LT.D3D_ValidateDevice();
			if (FAILED(hr))
			{
				SETUP_HISTORY.AddMessage("RM: Failed ValidSoFar on '%s:%d' because %s", mName, mMethod, DXGetErrorString9(hr));
				mMethod--;
				return false;
			}
		}
	}
	return true;
}

//******************************************************************************************
void	CRenderMethod::Accept()	// FUNCTION: 0x00527da0
{
	if (!mAccepted)
		SETUP_HISTORY.AddMessage("RM: Accepting '%s:%d'", mName, mMethod);
	mAccepted = TRUE;
}

//******************************************************************************************
int		CRenderMethod::GetMethod()	// FUNCTION: 0x00527dd0
{
	return mMethod;
}

//******************************************************************************************
void	CRenderMethod::BeginVBufCheck()	// FUNCTION: 0x00527de0
{
	VBUFTEXTURE_VALIDATE = !mAccepted;
	VBUFTEXTURE_VALIDATE_FAILED = false;
}

//******************************************************************************************
bool	CRenderMethod::CheckVBufValid()	// FUNCTION: 0x00527e00
{
	if (!mAccepted)
	{
		if (VBUFTEXTURE_VALIDATE_FAILED)
		{
			SETUP_HISTORY.AddMessage("RM: Failed CheckVBufValid on '%s:%d'", mName, mMethod);
			if (mMethod == 0)
			{
				mAccepted = TRUE;
				return true;
			}
			mAccepted = FALSE;
			mMethod--;
		}
		return !VBUFTEXTURE_VALIDATE_FAILED;
	}
	return true;
}

// The integer selector is distinct from the console float Set: it does not accept the method.
// The original spelling/object home is unknown; retail folds this body with CTweakFLOAT::Set.
void CRenderMethod::SetMethod(int method)	// FUNCTION: 0x00528b50
{
	mMethod = method;
}
