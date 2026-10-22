// ScriptObjectCode.cpp: a compiled mission script and the VM's value stack (reconstructed from the retail object,
// 00538db0..0053950c; not in Stuart's release). The allocations' line numbers are retail's (__FILE__ 00650040).

#include "..\common.h"
#include "..\monitor.h"
#include "IScript.h"
#include "asminstruction.h"
#include "EventFunction.h"
#include "..\WorldMeshList.h"
#include "..\mathsconst.h"
#include "..\DebugLog.h"

// the maths constants' initializers come first (mathsconst.h)
// FUNCTION: 0x00538db0 init:ZERO_FVECTOR
// FUNCTION: 0x00538dd0 init:ID_FMATRIX
// The events scripts can name, with their numbers and a flag: retail's object starts its .data with this table
// (0064fef8, its strings after it) and nothing in the executable reads it. The names are ours; "hit" shares
// component.obj's literal.
struct SScriptEventName
{
	int		mNumber;
	char	*mName;
	int		mFlag;
};

SScriptEventName	SCRIPT_EVENT_NAMES[] =				// 0064fef8 (names ours)
{
	{ 0, "init", 0 },
	{ 1, "arrived", 1 },
	{ 2, "timer", 0 },
	{ 3, "died", 0 },
	{ 4, "hit", 1 },
	{ 5, "started_dying", 0 },
	{ 6, "ready", 0 },
	{ 7, "shutdown", 0 },
	{ 8, "notdefined4", 0 },
	{ 9, "notdefined5", 0 },
	{ 10, "notdefined6", 0 },
	{ 11, "notdefined7", 0 },
	{ 12, "notdefined8", 0 },
};

#line 16

//******************************************************************************************
// The empty script, for Clone (which expands it). Defined out of line ahead of the file constructor: its vtable store
// makes the compiler emit ??_G (00538ea0) here, before the file constructor, as retail has it; nothing calls this copy,
// so the linker drops it (/OPT:REF).
CMissionScriptObjectCode::CMissionScriptObjectCode()
{
	mNumInstructions = 0;
	mInitRun = FALSE;
	mUnknown5c = 0;
	mUnknown60 = 0;
	for (int i = 0; i < NUM_SCRIPT_EVENTS; i++)
		mEventPCs[i] = -1;
}

//******************************************************************************************
// FUNCTION: 0x00538ec0 ??0CMissionScriptObjectCode@@QAE@PAVCDXMemBuffer@@@Z
CMissionScriptObjectCode::CMissionScriptObjectCode(CDXMemBuffer *file)
{
	mInitRun = FALSE;

	int	num_instructions;
	file->Read(&num_instructions, 4);
	int i; for (i = 0; i < num_instructions; i++)
	{
		int	opcode;
		file->Read(&opcode, 4);
		mInstructions.append(CAsmInstruction::Spawn(opcode, file));
	}

	for (i = 0; i < NUM_SCRIPT_EVENTS; i++)
		file->Read(&mEventPCs[i], 4);

#line 57
	mSymbols = new (MEMTYPE_SCRIPT) CSymbolTable(file);

	int	num_functions;
	file->Read(&num_functions, 4);
	for (i = 0; i < num_functions; i++)
	{
#line 67
		mEventFunctions.Append(new (MEMTYPE_SCRIPT) CEventFunction(this, file));
	}

	file->Read(&mUnknown60, 4);
	file->Read(&mUnknown5c, 4);
	mNumInstructions = mInstructions.size();
}
// FUNCTION: 0x00538ea0 ??_GCMissionScriptObjectCode@@UAEPAXI@Z

//******************************************************************************************
// a copy for another thing: the instructions and the event functions are cloned, the symbols copied
CMissionScriptObjectCode	*CMissionScriptObjectCode::Clone()	// FUNCTION: 0x00539040
{
#line 83
	CMissionScriptObjectCode	*copy = new (MEMTYPE_SCRIPT) CMissionScriptObjectCode;

	int i; for (i = 0; i < mInstructions.size(); i++)
	{
		CAsmInstruction	*instruction = mInstructions.at(i)->Clone();
		copy->mInstructions.insert_at(copy->mNumInstructions, instruction);
		instruction->mUnknown08 = copy->mNumInstructions;
		copy->mNumInstructions++;
	}

	for (i = 0; i < NUM_SCRIPT_EVENTS; i++)
		copy->mEventPCs[i] = mEventPCs[i];

	copy->mSymbols = mSymbols->Clone();

	CEventFunction	*function;
	FOR_ALL_ITEMS_IN(mEventFunctions, function)
		copy->mEventFunctions.Append(function->Clone(copy));

	copy->mInitRun = mInitRun;
	copy->mUnknown60 = mUnknown60;
	copy->mUnknown5c = mUnknown5c;
	copy->mNumInstructions = mNumInstructions;
	return copy;
}

//******************************************************************************************
CMissionScriptObjectCode::~CMissionScriptObjectCode()	// FUNCTION: 0x005391a0
{
	for (int i = 0; i < mNumInstructions; i++)
		delete mInstructions.at(i);

	mEventFunctions.DeleteAll();
	mInstructions.reset();
	mNumInstructions = 0;

	delete mSymbols;
	mSymbols = NULL;
	mUnknown5c = 0;
}

//******************************************************************************************
// the world loads the meshes of every unit a SpawnThing call names (its first argument, a string symbol)
void	CMissionScriptObjectCode::LoadSpawnedMeshes()	// FUNCTION: 0x005392a0
{
	for (int i = 0; i < mInstructions.size(); i++)
	{
		CAsmInstruction	*instruction = mInstructions.at(i);
		if (instruction->GetOpcode() == OP_CALL &&
			strcmp("SpawnThing", gScriptFunctions[instruction->mAttr & 0xff].mName) == 0)
		{
			int	index = mInstructions.at(i - ((instruction->mAttr >> 8) & 0xff))->mAttr;
			CSymbol	*symbol = mSymbols->GetSymbol(index);
			CWorldMeshList::Add(symbol->mValue->GetString());
		}
	}
}

//******************************************************************************************
// the values move across: from is left empty
// FUNCTION: 0x00539350 ??4CDataStack@@QAEAAV0@AAV0@@Z
CDataStack	&CDataStack::operator=(CDataStack &from)
{
	Clear();

	mNumValues = from.mNumValues;
	for (int i = 0; i < mNumValues; i++)
		mValues[i] = from.mValues[i];
	from.mNumValues = 0;
	return *this;
}

//******************************************************************************************
void	CDataStack::Clear()	// FUNCTION: 0x005393e0
{
	while (mNumValues)
	{
		delete mValues[mNumValues - 1];
		mNumValues--;
	}
}

//******************************************************************************************
void	CDataStack::Push(CDataType *value)	// FUNCTION: 0x00539420
{
	mValues[mNumValues] = value;
	if (++mNumValues > MAX_STACK_SIZE)
	{
		LOG.AddMessage("FATAL ERROR: Stack out of memory");
		mNumValues--;
	}
}

//******************************************************************************************
CDataType	*CDataStack::Pop()	// FUNCTION: 0x00539470
{
	if (!mNumValues)
	{
		LOG.AddMessage("FATAL ERROR: Pop called on empty stack");
		return NULL;
	}
	return mValues[--mNumValues];
}

//******************************************************************************************
void	CDataStack::RemoveTop()	// FUNCTION: 0x005394a0
{
	if (!mNumValues)
	{
		LOG.AddMessage("FATAL ERROR: RemoveTop called on empty stack");
		return;
	}
	delete mValues[--mNumValues];
}

//******************************************************************************************
// the value depth places below the top (0 is the top)
CDataType	*CDataStack::GetTop(int depth)	// FUNCTION: 0x005394e0
{
	if (mNumValues - depth <= 0)
	{
		LOG.AddMessage("FATAL ERROR: Stack item does not exist in call to GetTop - %d", depth);
		return NULL;
	}
	return mValues[mNumValues - depth - 1];
}
