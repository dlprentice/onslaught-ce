// vminternal.h: the mission-script VM's runtime as the instructions see it (only what matched files need; the names are
// ours). vm.h keeps the small view the rest of the game uses, so that its includers' declaration counts stay put.
//
// CVM (MISSION_SCRIPT_VM, 0089c5e0) runs one script at a time (Run 00539b00): its object code at +0x08,
// the data stack at +0x0c, the program counter at +0x214 (each instruction executes with it already past itself),
// the compare flag at +0x218 and the call depth at +0x224. An instruction's Execute gets the VM, its stack and the
// object code's symbol table.

#ifndef VMINTERNAL_H
#define VMINTERNAL_H

#include "DataType.h"
#include "..\Monitor.h"
#include "..\OPtrSet.h"

#define MAX_STACK_SIZE	128

// the value stack (ScriptObjectCode.obj)
class CDataStack
{
public:
	CDataStack() : mNumValues(0) {}
	~CDataStack() { Clear(); }
	CDataStack		&operator=(CDataStack &from);	// 00539350: takes from's values
	void			Clear();				// 005393e0: deletes the values
	void			Push(CDataType *value);	// 00539420
	CDataType		*Pop();					// 00539470
	void			RemoveTop();			// 005394a0
	CDataType		*GetTop(int depth);		// 005394e0
	int				Size() { return mNumValues; }

	CDataType		*mValues[128];			// +0x000
	int				mNumValues;				// +0x200
};

#include "symtab.h"

class IScript;

// the native functions a script calls (0x40 bytes each, the table at 0064ce20: FollowWaypoint..IsOverWater; IScript.cpp
// fills it in with a dynamic initializer, 0052ff30). The handler is a pointer to a member of IScript, declared while
// IScript is incomplete so that it takes the general 16-byte form; it is aligned to 8, after a gap at +0x2c.
struct SScriptFunction
{
	char			*mName;					// +0x00
	int				mNumArgs;				// +0x04
	BOOL			mReturnsValue;			// +0x08
	int				mArgTypes[7];			// +0x0c: EDataType, or 7 for any
	int				mReturnType;			// +0x28
	void			(IScript::*mFunction)(CDataType **args, int num_args, CDataType **result);	// +0x30
};

extern SScriptFunction	gScriptFunctions[];			// 0064ce20
extern CDataType		*gScriptFunctionArgs[];		// 0089c300

#ifndef ISCRIPT_FULL
// the script object the functions run on, as the call instruction reaches it (vm.h has the rest)
class IScript
{
public:
	UBYTE			mUnknown00[0x08];
	CThing			*mDefaultThing;			// +0x08
	UBYTE			mUnknown0c[0x10 - 0x0c];
	CThing			*mThing;				// +0x10: the thing a function acts on; POINTER sets it
	UBYTE			mUnknown14[0x2c - 0x14];
	class CVM		**mVM;					// +0x2c
};
#endif

// a compiled mission script (vtable 005e4f54, 0x70 bytes; ScriptObjectCode.obj): its instructions, the program
// counter of each event's handler, the symbol table and the event functions
#define NUM_SCRIPT_EVENTS	13

class CAsmInstruction;

class CMissionScriptObjectCode
{
public:
	CMissionScriptObjectCode();					// out of line in ScriptObjectCode.cpp; Clone inlines it, the linker drops the copy
	CMissionScriptObjectCode(CDXMemBuffer *file);	// 00538ec0
	virtual			~CMissionScriptObjectCode();	// 005391a0
	CMissionScriptObjectCode	*Clone();			// 00539040
	void			LoadSpawnedMeshes();			// 005392a0: the meshes of the things SpawnThing calls name

	OPtrSet<CAsmInstruction>	mInstructions;		// +0x04
	int				mEventPCs[NUM_SCRIPT_EVENTS];	// +0x14: -1 for an event without a handler
	SPtrSet<class CEventFunction>	mEventFunctions;	// +0x48
	CSymbolTable	*mSymbols;				// +0x58
	int				mUnknown5c;				// +0x5c: CVM::CallEvent runs the init code once when it is set
	int				mUnknown60;
	int				mNumInstructions;		// +0x64
	IScript			*mScript;				// +0x68
	BOOL			mInitRun;				// +0x6c
};

// the VM itself (RTTI .?AVCVM@@, vtable 005e4f1c: CMonitor's slots, 0x228 bytes). The Wait functions keep a copy of the
// running VM (the implicit copy constructor) and assignment restores one.
class CVM : public CMonitor
{
public:
	CVM()									// 005398d0 (MISSION_SCRIPT_VM's initializer calls it)
	{
		mCode = NULL;
		mPC = 0;
		mFlags = 0;
		mStackBase = 0;
		mCallDepth = 0;
		mRunning = FALSE;
	}
	CVM				&operator=(CVM &other);	// 00539910
	void			Clean();				// 00539980
	// runs the handler for one of a script's events, if it has one (00539990)
	void			CallEvent(CMissionScriptObjectCode *code, int event, CDataType **args, int num_args);
	// runs a script's code from pc with the arguments pushed (00539a60)
	void			CallFunction(CMissionScriptObjectCode *code, int pc, CDataType **args, int num_args);
	void			GotoInstruction(int pc);	// 00539ae0
	void			Run();					// 00539b00: from mPC to the return

	CMissionScriptObjectCode	*mCode;		// +0x08
	CDataStack		mStack;					// +0x0c
	int				mRunning;				// +0x210
	int				mPC;					// +0x214
	DWORD			mFlags;					// +0x218: bit 0, the last compare
	int				mStackBase;				// +0x21c: the stack's size when a call began; Run checks it at the end
	int				mStop;					// +0x220
	int				mCallDepth;				// +0x224
};

extern CVM		MISSION_SCRIPT_VM;	// 0089c5e0

#endif
