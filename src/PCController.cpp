// PCController.cpp: implementation of the PCController class.
//
//////////////////////////////////////////////////////////////////////

#include "common.h"
#define PCCONTROLLER_CPP
#if TARGET == PC

#include "PCController.h"
#include "console.h"
#include "Platform.h"
#include "mathsconst.h"
//#include "DX.h"
  
// retail: the default bindings, which the player can remap and the career saves (Stuart's fixed
// ControllerMaping table is gone). The first sixteen are the configurable game controls.
// FUNCTION: 0x005141f0 init:ZERO_FVECTOR
bool	gAviCapturing;			// 008892d5: a film capture is running (ltshell.cpp)

// FUNCTION: 0x00514210 init:CONTROL_BINDINGS
SControlBinding CONTROL_BINDINGS[] = {
	SControlBinding(1, BUTTON_MECH_YAW_LEFT, MOUSE_AXIS_MINUS, 0, 0),
	SControlBinding(1, BUTTON_MECH_YAW_RIGHT, MOUSE_AXIS_PLUS, 0, 0),
	SControlBinding(1, BUTTON_MECH_PITCH_UP, MOUSE_AXIS_PLUS, 1, 0),
	SControlBinding(1, BUTTON_MECH_PITCH_DOWN, MOUSE_AXIS_MINUS, 1, 0),
	SControlBinding(1, BUTTON_MECH_STRAFE_LEFT, KEY_ON, DIK_A, 'A'),
	SControlBinding(1, BUTTON_MECH_STRAFE_RIGHT, KEY_ON, DIK_D, 'D'),
	SControlBinding(1, BUTTON_MECH_FORWARD, KEY_ON, DIK_W, 'W'),
	SControlBinding(1, BUTTON_MECH_BACKWARD, KEY_ON, DIK_S, 'S'),
	SControlBinding(1, BUTTON_MECH_CLOAK, KEY_ONCE, DIK_TAB, 0),
	SControlBinding(1, BUTTON_MECH_MORPH, KEY_ONCE, DIK_SPACE, 0),
	SControlBinding(1, BUTTON_MECH_CHARGE_GUN_POD, MOUSE_BUTTON_ON, 0, 0),
	SControlBinding(1, BUTTON_MECH_FIRE_GUN_POD, MOUSE_BUTTON_RELEASE, 0, 0),
	SControlBinding(1, BUTTON_MECH_CHANGE_WEAPON, MOUSE_BUTTON_ONCE, 2, 0),
	SControlBinding(1, BUTTON_MECH_CHANGE_ZOOM_IN, MOUSE_BUTTON_ONCE, 4, 0),
	SControlBinding(1, BUTTON_MECH_CHANGE_ZOOM_OUT, MOUSE_BUTTON_ONCE, 3, 0),
	SControlBinding(1, BUTTON_MECH_LANDING_JETS, KEY_ON, DIK_LSHIFT, 0),
	SControlBinding(0, BUTTON_FRONTEND_MENU_BACK, KEY_ONCE, DIK_BACK, 0),
	SControlBinding(0, BUTTON_FRONTEND_MENU_BACK, KEY_ONCE, DIK_ESCAPE, 0),
	SControlBinding(0, BUTTON_SKIP_CUTSCENE, KEY_ONCE, DIK_SPACE, 0),
	SControlBinding(0, BUTTON_SKIP_CUTSCENE, KEY_ONCE, DIK_RETURN, 0),
	SControlBinding(0, BUTTON_SKIP_CUTSCENE, KEY_ONCE, DIK_ESCAPE, 0),
	SControlBinding(0, BUTTON_SKIP_CUTSCENE, KEY_ONCE, DIK_NUMPADENTER, 0),
	SControlBinding(0, BUTTON_SKIP_PANNING, KEY_ONCE, DIK_SPACE, 0),
	SControlBinding(0, BUTTON_SKIP_PANNING, KEY_ONCE, DIK_RETURN, 0),
	SControlBinding(0, BUTTON_SKIP_PANNING, KEY_ONCE, DIK_ESCAPE, 0),
	SControlBinding(0, BUTTON_SKIP_PANNING, KEY_ONCE, DIK_NUMPADENTER, 0),
	SControlBinding(0, BUTTON_CONSOLE_MENU_UP, KEY_ONCE, DIK_NUMPAD8, 0),
	SControlBinding(0, BUTTON_CONSOLE_MENU_DOWN, KEY_ONCE, DIK_NUMPAD2, 0),
	SControlBinding(0, BUTTON_FRONTEND_MENU_SELECT, KEY_ONCE, DIK_RETURN, 0),
	SControlBinding(0, BUTTON_FRONTEND_MENU_SELECT, KEY_ONCE, DIK_SPACE, 0),
	SControlBinding(0, BUTTON_FRONTEND_MENU_DOWN, KEY_ONCE, DIK_DOWN, 0),
	SControlBinding(0, BUTTON_FRONTEND_MENU_UP, KEY_ONCE, DIK_UP, 0),
	SControlBinding(0, BUTTON_FRONTEND_MENU_LEFT, KEY_ONCE, DIK_LEFT, 0),
	SControlBinding(0, BUTTON_FRONTEND_MENU_RIGHT, KEY_ONCE, DIK_RIGHT, 0),
	SControlBinding(0, BUTTON_PAUSE, KEY_ONCE, DIK_ESCAPE, 0),
	SControlBinding(0, BUTTON_PAUSE_MENU_UP, KEY_ONCE, DIK_UP, 0),
	SControlBinding(0, BUTTON_PAUSE_MENU_DOWN, KEY_ONCE, DIK_DOWN, 0),
	SControlBinding(0, BUTTON_PAUSE_MENU_SELECT, KEY_ONCE, DIK_RETURN, 0),
	SControlBinding(0, BUTTON_TOGGLE_FREE_CAMERA, KEY_ONCE, DIK_F, 0),
	SControlBinding(0, BUTTON_CAMERA_YAW_LEFT, KEY_ON, DIK_LEFT, 0),
	SControlBinding(0, BUTTON_CAMERA_YAW_RIGHT, KEY_ON, DIK_RIGHT, 0),
	SControlBinding(0, BUTTON_CAMERA_PITCH_UP, KEY_ON, DIK_UP, 0),
	SControlBinding(0, BUTTON_CAMERA_PITCH_DOWN, KEY_ON, DIK_DOWN, 0),
	SControlBinding(0, BUTTON_CAMERA_STRAFE_LEFT, KEY_ON, DIK_A, 0),
	SControlBinding(0, BUTTON_CAMERA_STRAFE_RIGHT, KEY_ON, DIK_D, 0),
	SControlBinding(0, BUTTON_CAMERA_MOVE_FORAWRD, KEY_ON, DIK_W, 0),
	SControlBinding(0, BUTTON_CAMERA_MOVE_BACKWARDS, KEY_ON, DIK_S, 0),
	SControlBinding()
};


//******************************************************************************************
CPCController::CPCController(IController* to_control, int padnumber,int configuration) :	// FUNCTION: 0x005145f0
  CController(to_control, padnumber,configuration)
{
}

// Compiler-generated: the deleting destructor.
// FUNCTION: 0x00514620 ??_GCPCController@@UAEPAXI@Z

//******************************************************************************************
// ok get raw values out from LT and convert them to values which is platform
// indepedent. values must range from -1.0 to 1.0
float	CPCController::GetJoyAnalogueLeftX(int pad_number)	// FUNCTION: 0x00514640
{
	if (LT.mJoypads <= pad_number) return 0.0f;
	float lx = (float)(LT.JoyState(pad_number)->lX);
	lx = lx /1000.0f ;
	return lx;
}

//******************************************************************************************
// ok get raw values out from LT and convert them to values which is platform
// indepedent. values must range from -1.0 to 1.0
float	CPCController::GetJoyAnalogueLeftY(int pad_number)	// FUNCTION: 0x00514670
{
	if (LT.mJoypads <= pad_number) return 0.0f;

	float ly = (float)(LT.JoyState(pad_number)->lY);
	ly = ly /1000.0f ;
	return ly;
}


//******************************************************************************************
// ok get raw values out from LT and convert them to values which is platform
// indepedent. values must range from -1.0 to 1.0
float	CPCController::GetJoyAnalogueRightX(int pad_number)	// FUNCTION: 0x005146a0
{
	if (LT.mJoypads <= pad_number) return 0.0f;
	float vx = (float)(LT.JoyState(pad_number)->lZ);
	vx = vx / 1000.0f;
	return vx ;
}

//******************************************************************************************
// ok get raw values out from LT and convert them to values which is platform
// indepedent. values must range from -1.0 to 1.0
float	CPCController::GetJoyAnalogueRightY(int pad_number)	// FUNCTION: 0x005146d0
{
	if (!LT.mJoypadHasRz[pad_number]) return 0.0f;
	if (LT.mJoypads <= pad_number) return 0.0f;
	float vy = (float)(LT.JoyState(pad_number)->lRz);
//	LOG.AddMessage("ray vy = %2.8f", vy) ;
	vy = vy - 32768.0f ;
	vy = vy / 32768.0f ;
	return vy ;
}



//******************************************************************************************
void	CPCController::RecordControllerState()	// FUNCTION: 0x00514720
{
	mDataFile.Write(&mButtons1,sizeof(mButtons1));
	mDataFile.Write(&mButtons2,sizeof(mButtons2));
	mDataFile.Write(&mButtons3,sizeof(mButtons3));
}


//******************************************************************************************
void CPCController::ReadControllerState()	// FUNCTION: 0x00514760
{
	mDataFile.Read(&mButtons1,sizeof(mButtons1));
	mDataFile.Read(&mButtons2,sizeof(mButtons2));
	mDataFile.Read(&mButtons3,sizeof(mButtons3));
	if (mDataFile.EndOfFile())
	{
		mDataFile.Close();
		mPlaying=false;
	}
}

//******************************************************************************************
// Declared in the class, defined here: retail emits these six after ReadControllerState, in this order (GetKeyRelease
// before GetKeyOn), where VC6 puts inline functions defined after it; in the class (Stuart's later header) they come out
// with the constructor's vtable instead. Placement evidence. Lands with the WorldPhysicsManager and BattleEngine moves:
// alone it shifts the dds and bdl witnesses.
inline int CPCController::GetJoyButtonOnce(int pad_number, int button) { return LT.JoyButtonOnce(pad_number, button) ; }	// FUNCTION: 0x005147b0
inline int CPCController::GetJoyButtonOn(int pad_number, int button) { return LT.JoyButtonOn(pad_number, button) ; }	// FUNCTION: 0x005147f0
inline int CPCController::GetJoyButtonRelease(int pad_number, int button) { return LT.JoyButtonRelease(pad_number, button) ; }	// FUNCTION: 0x00514810
inline BOOL CPCController::GetKeyOnce(int key) { return LT.xKeyOnceThisFrame(key) ; }	// FUNCTION: 0x00514850
inline BOOL CPCController::GetKeyRelease(int key) { return LT.xKeyUp(key) ; }	// FUNCTION: 0x00514870
inline BOOL CPCController::GetKeyOn(int key) { return PLATFORM.KeyOn(key) ; }	// FUNCTION: 0x00514890

//******************************************************************************************
// retail: the hat switch, in hundredths of a degree (0xffff in the low word when centred)
float CPCController::GetJoyPovX(int pad_number)	// FUNCTION: 0x005148b0
{
	DWORD pov = LT.JoyState(pad_number)->rgdwPOV[0];
	if (LOWORD(pov) == 0xffff) return 0.0f;
	return (float)sin(pov * (PI / 18000.0f));
}

float CPCController::GetJoyPovY(int pad_number)	// FUNCTION: 0x00514900
{
	DWORD pov = LT.JoyState(pad_number)->rgdwPOV[0];
	if (LOWORD(pov) == 0xffff) return 0.0f;
	return -(float)cos(pov * (PI / 18000.0f));
}

#endif

// Inline functions from headers that this object emits; its vtables and matched code place them.
// STAND-IN (link layout): retail's 40 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_PCController[40] = { 0 };
// FUNCTION: 0x004f00d0 ??1CPCController@@UAE@XZ
