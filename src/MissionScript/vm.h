// vm.h: the mission-script interface a thing holds (only what matched files need).
//
// IScript: vtable 005e4f08 (CMonitor's three slots; Shutdown overridden at 00533810), 0x3c bytes, constructed from
// its thing and a copy of the object code (005333b0; CComplexThing::SetScript 004f4230). The notifications are
// ordinary members, placed by CComplexThing's calls: Init 00533500, Ready 005335a0, StartedDying 00533660,
// Hit 00533690, Died 005337e0, FinishedPlayingAnim 00533840.
// CDataType: vtable 005e4b4c (20 slots); CStringDataType (005e4e4c, 21) returns its string from slot 14
// (CComplexThing::SetVar 004f45e0). The UnknownN slots are placeholders that keep the order.

#ifndef VM_H
#define VM_H

// IScript.cpp defines the full classes (IScript.h, vminternal.h) before anything includes this
#ifndef ISCRIPT_FULL

#include "Monitor.h"

class CComplexThing;
class CMissionScriptObjectCode;

class IScript : public CMonitor
{
public:
	IScript(CComplexThing *thing, CMissionScriptObjectCode *code);
	virtual void	Shutdown();

	void			Init();
	void			Ready();
	void			StartedDying();
	void			Hit(CComplexThing *other);
	void			Died();
	void			FinishedPlayingAnim();

	UBYTE			mUnknown08[0x3c - 0x08];
};

class CDataType
{
public:
	virtual			~CDataType();			// 0
	virtual void	Unknown1();
	virtual void	Unknown2();
	virtual void	Unknown3();
	virtual void	Unknown4();
	virtual void	Unknown5();
	virtual void	Unknown6();
	virtual void	Unknown7();
	virtual void	Unknown8();
	virtual void	Unknown9();
	virtual void	Unknown10();
	virtual void	Unknown11();
	virtual void	Unknown12();
	virtual void	Unknown13();
	virtual char	*GetString();			// 14
};

class CDXMemBuffer;

class CStringDataType : public CDataType
{
public:
	CStringDataType(CDXMemBuffer *file);	// 0052f790 (8 bytes; CWorld::LoadScripts 0050acea)

	char			*mString;				// +0x04
};

// A compiled mission script (0x70 bytes; CWorld::LoadScripts 0050acfd reads one from the world file).
class CMissionScriptObjectCode
{
public:
	CMissionScriptObjectCode(CDXMemBuffer *file);	// 00538ec0
	virtual			~CMissionScriptObjectCode();
	CMissionScriptObjectCode	*Clone();			// 00539040 (CWorld::GetCopyOfScript 0050ac5a)
#ifdef WORLD_CPP
	void			LoadSpawnedMeshes();			// 005392a0 (CWorld::LoadWorld 0050d401)
#endif

	UBYTE			mUnknown04[0x70 - 0x04];
};

// The mission scripts' virtual machine, vminternal.h's CVM (short view: only what matched files need so far).
class CVM
{
public:
	void	Clean();							// 00539980 (CGame::ShutdownRestartLoop 0046caa7)
};

extern CVM		MISSION_SCRIPT_VM;		// 0089c5e0

#endif

#endif
