// PCController.h: interface for the PCController class.
//
//////////////////////////////////////////////////////////////////////

#ifndef PCCONTROLLER_H
#define PCCONTROLLER_H

#include "Controller.h"
#include "DX.h"
#include "Platform.h"

// pc specific controllers


class CPCController : public CController
{
public:
	CPCController(IController* to_control,int padnumber, int configuration = 1) ;
	
protected:
 
	virtual int GetJoyButtonOnce(int pad_number, int button);	// defined in PCController.cpp
	virtual int GetJoyButtonOn(int pad_number, int button);	// defined in PCController.cpp
	virtual int GetJoyButtonRelease(int pad_number, int button);	// defined in PCController.cpp

	virtual BOOL	GetKeyOnce(int key);	// defined in PCController.cpp
	virtual BOOL	GetKeyOn(int key);	// defined in PCController.cpp
	virtual BOOL	GetKeyRelease(int key);	// defined in PCController.cpp
	virtual void	DeviceSetVibration(float){}

	virtual float	GetJoyAnalogueLeftX(int pad_number);
	virtual float	GetJoyAnalogueLeftY(int pad_number);
	virtual	float	GetJoyAnalogueRightX(int pad_number);
	virtual float	GetJoyAnalogueRightY(int pad_number);
	virtual float	GetJoyPovX(int pad_number);
	virtual float	GetJoyPovY(int pad_number);

	virtual void RecordControllerState() ;
	virtual void ReadControllerState() ;
};

#endif // PCCONTROLLER_H
