// FEPBEConfig.h: the Battle Engine configuration page (reconstructed from the retail PC executable; not in Stuart's
// release). 0x30 bytes at FRONTEND + 0x2bc, vtable 005dba3c; the layout is from CFEPBEConfig::Init (0044fa90),
// Process (00450010), ButtonPressed (00450090), TransitionNotification (004519c0) and Load (0044fe70).

#ifndef FEPBECONFIG_H
#define FEPBECONFIG_H

#include "frontendpage.h"
#include "SPtrSet.h"
#include "membuffer.h"

class CFrontEndThing;
class CBattleEngineData;

// One level's choice of configurations and wingmen (0x24 bytes; CFEPBEConfig::Load 0044fe70 and CFEPWingmen::Load
// 00521ae0 read each from data\WorldHeaders.dat in this order after the level, the file's version gating the later
// fields).
struct SBEConfigLevel
{
	~SBEConfigLevel();								// 0044fdf0

	SINT			mLevel;							// +0x00: the level number (compared with FRONTEND.mSelectedLevel)
	BOOL			mTara;							// +0x04: the wingmen the level offers (CFEPWingmen::ButtonPressed
	BOOL			mBilly;							// +0x08: 00521dc0..00521f3b picks wingman 0, 1, 2 from them); from
	BOOL			mJason;							// +0x0c: version 2 on, before which only Tara
	SINT			mUnknown10;						// +0x10: version 3 on; 0 before
	SPtrSet<char *>	mConfigs;						// +0x14: the configuration names (CBattleEngineData's
													// mConfigurationName), each a counted string
};

class CFEPBEConfig : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);
	void	ActiveNotification(EFrontEndPage from) {}		// folded (00452b60)
	void	DeActiveNotification() {}						// folded (0040c640)
	virtual void	Load(CMEMBUFFER &file);			// 0044fe70: one level's entry (Init calls it for each)

	SBEConfigLevel	*GetCurrentLevel();				// 00451a40: the selected level's entry, or NULL

	// the selected level's configurations, without a page (FEPMultiplayerStart.obj and FEPWingmen.obj call them)
	static char		*GetConfigName(SINT n);			// 0044eab0: the n'th name, or NULL
	static WCHAR	*GetConfigDescription(SINT n);	// 0044eb30
	static SINT		GetNumConfigs();				// 0044ecf0
	static SINT		GetNumJetWeapons(SINT n);		// 0044ed40
	static SINT		GetNumWalkerWeapons(SINT n);	// 0044eea0: the primary weapon first, then the walker weapons
	static SINT		GetWalkerWeaponRating(SINT n, SINT weapon, SINT rating);	// 0044f030
	static SINT		GetJetWeaponRating(SINT n, SINT weapon, SINT rating);		// 0044f300
	static WCHAR	*GetWalkerWeaponName(SINT n, SINT weapon);					// 0044f530
	static WCHAR	*GetJetWeaponName(SINT n, SINT weapon);						// 0044f830

	static void		PushProjection();				// 00450400: the page's own projection for its things
	static void		PopProjection();				// 00450440
	static void		RenderRatingPips(float x, float y, float value, DWORD colour);	// 00450460

	float			mStartTime;						// +0x04: when the page was entered
	CFrontEndThing	*mThing;						// +0x08
	float			mTime;							// +0x0c: +0.01 a frame
	SINT			mConfig;						// +0x10: the chosen configuration, an index into the level's
	float			mUpFlash;						// +0x14: 1 when the configuration goes up, fading by 0.1 a frame
	float			mDownFlash;						// +0x18
	float			mChangeTime;					// +0x1c: when it last changed; -1000 after Init
	SPtrSet<SBEConfigLevel>	mLevels;				// +0x20
};

#endif
