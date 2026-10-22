// fmv.h: full-motion video (only what matched files need so far).

#ifndef FMV_H
#define FMV_H

#ifdef FMV_CPP	// the full class for fmv.cpp; the other objects keep the short view (their declaration counts)
#include "Controller.h"

// the platform-independent player (vtable 005e5018, built by CDXFMV's constructor 0053f0f0; fmv.obj 004655d0..)
class CFMV : public IController
{
public:
	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val);	// 3 (004656e0)
	virtual BOOL	CanBeControlledWhenInPause() { return TRUE; }	// 4 (004014a0)
	virtual EControlType	GetControlType() { return (EControlType)0; }	// 5 (00405930)
	virtual void	Init();					// 6 (004656b0): registers fmv_play
	// 7..11 are pure in CFMV's vtable (_purecall, 0055df1f)
	virtual void	Unknown7(int a, int b) = 0;	// 7..10: the platform's (CDXFMV 00452da0, 0040c640, 0040c640, 005019c0)
	virtual void	Unknown8() = 0;
	virtual void	Unknown9() = 0;
	virtual BOOL	Unknown10() = 0;
	// the platform's player (CDXFMV 0053f190): the language is 0 unless the clip is localised
	virtual BOOL	Play(char *name, int language, int b, int c, int d, int e) = 0;	// 11

	// retail: three more arguments than Stuart's (CGame::RunIntroFMV and CFEPGoodies::Process 0045db89 pass
	// 0, 0, 0, 1)
	BOOL	PlayFullscreen(char *name, BOOL a, BOOL localise, int b = 0, int c = 0, int d = 0, int e = 1);	// 00465640

	BOOL	mQuit;							// +0x08: a skip button was pressed
	BOOL	mSkippable;						// +0x0c: 0x42 skips too (the attract mode, or asked for)
};
#else
class CFMV
{
public:
	virtual void	Init();						// 6 (004656b0); the player overrides it (CDXFMV 0053f180)
	// retail: three more arguments than Stuart's (CGame::RunIntroFMV and CFEPGoodies::Process 0045db89 pass
	// 0, 0, 0, 1)
	BOOL	PlayFullscreen(char *name, BOOL a, BOOL localise, int b = 0, int c = 0, int d = 0, int e = 1);	// 00465640
};
#endif

#ifdef DXFMV_CPP	// DXFMV.cpp defines CDXFMV itself (it needs the front end's video)
class CDXFMV;
extern CDXFMV FMV;								// 0089d690
#elif defined(SYSTEM_CPP)	// FMV is the PC's player; the system calls its own Init directly (004efc4f)
class CDXFMV : public CFMV
{
public:
	virtual void	Init();						// 0053f180
};
extern CDXFMV FMV;								// 0089d690
#elif defined(FMV_CPP)	// fmv.cpp sees the full CFMV: the player's overrides (DXFMV.cpp) make the view complete
class CDXFMV : public CFMV
{
public:
	virtual void	Unknown7(int a, int b);
	virtual void	Unknown8();
	virtual void	Unknown9();
	virtual BOOL	Unknown10();
	virtual BOOL	Play(char *name, int language, int b, int c, int d, int e);
};
extern CDXFMV FMV;								// 0089d690
#else
class CDXFMV : public CFMV {};					// FMV is the PC's player (DXFMV.cpp); the others call CFMV's members
extern CDXFMV FMV;								// 0089d690
#endif

SINT	lookup_FMV(SINT level, SINT which);

// retail: a cutscene's file name, the small version when the full one is not installed (game.cpp)
void	MakeCutsceneName(char *name, SINT fmv);	// 0046d810	// 00523120: which is 0 intro, 1 outro, 2 alternative outro


#endif
