// vm.cpp: the mission-script VM (reconstructed from the retail object, 005398a0..00539c70; not in Stuart's release;
// the file's name is ours, after vm.h).

#include "..\common.h"
#include "vminternal.h"
#include "asminstruction.h"
#include "..\DebugLog.h"

#define MAX_INSTRUCTIONS_PER_RUN	10000

// FUNCTION: 0x005398a0 init:MISSION_SCRIPT_VM
// FUNCTION: 0x005398d0 ??0CVM@@QAE@XZ
// FUNCTION: 0x005398c0 exit:MISSION_SCRIPT_VM
CVM	MISSION_SCRIPT_VM;

//******************************************************************************************
// back to a suspended VM's state (the Wait natives' resumes); the stack's values move across
// FUNCTION: 0x00539910 ??4CVM@@QAEAAV0@AAV0@@Z
CVM	&CVM::operator=(CVM &other)
{
	mCode = other.mCode;
	mPC = other.mPC;
	mStack = other.mStack;
	mFlags = other.mFlags;
	mStop = other.mStop;
	mStackBase = other.mStackBase;
	mCallDepth = other.mCallDepth;
	mRunning = FALSE;
	return *this;
}

//******************************************************************************************
void	CVM::Clean()	// FUNCTION: 0x00539980
{
	mStack.Clear();
}

//******************************************************************************************
// a script's handler for an event, with its arguments on the stack; the object code's init code runs first, once
void	CVM::CallEvent(CMissionScriptObjectCode *code, int event, CDataType **args, int num_args)	// FUNCTION: 0x00539990
{
	if (mStack.mNumValues)
		LOG.AddMessage("FATAL ERROR: stack not empty on call");

	mCode = code;
	if (!code->mInitRun && code->mUnknown5c)
	{
		mPC = 0;
		mStackBase = mStack.Size();
		Run();
		code->mInitRun = TRUE;
	}

	mPC = mCode->mEventPCs[event];
	if (mPC == -1)
	{
		for (int i = 0; i < num_args; i++)
			delete args[i];
		return;
	}

	mStackBase = mStack.mNumValues;
	for (int i = 0; i < num_args; i++)
		mStack.Push(args[i]);
	Run();
}

//******************************************************************************************
// the code at pc (an event function's entry), with its arguments on the stack
void	CVM::CallFunction(CMissionScriptObjectCode *code, int pc, CDataType **args, int num_args)	// FUNCTION: 0x00539a60
{
	mCode = code;
	if (!code->mInitRun && code->mUnknown5c)
	{
		mPC = 0;
		mStackBase = mStack.Size();
		Run();
		code->mInitRun = TRUE;
	}

	mStackBase = mStack.mNumValues;
	for (int i = 0; i < num_args; i++)
		mStack.Push(args[i]);
	mPC = pc;
	Run();
}

//******************************************************************************************
void	CVM::GotoInstruction(int pc)	// FUNCTION: 0x00539ae0
{
	mPC = pc;
	Run();
}

//******************************************************************************************
// until a return outside any local call, or until an instruction stops the VM (a Wait native suspends it)
void	CVM::Run()	// FUNCTION: 0x00539b00
{
	if (mRunning == TRUE)
	{
		LOG.AddString("ERROR: VM tryin to run VM whilst it was already running.\n");
		return;
	}

	mRunning = TRUE;
	mFlags = 0;
	mCallDepth = 0;
	mStop = FALSE;

	CAsmInstruction	*instruction = mCode->mInstructions.at(mPC);
	CSymbolTable	*symbols = mCode->mSymbols;
	int				count = 0;

	while (!(instruction->GetOpcode() == OP_RETURN && mCallDepth <= 0) && !mStop)
	{
		int	pc = mPC++;
		instruction->Execute(this, &mStack, symbols);
		if (mCode->mUnknown60 == 1)
			LOG.AddMessage("-> %4d stack size = %d flags = %d", pc, mStack.mNumValues, mFlags);

		instruction = mCode->mInstructions.at(mPC);
		if (++count > MAX_INSTRUCTIONS_PER_RUN)
		{
			LOG.AddMessage("FATAL ERROR :Infinite loop in script!");
			mStop = TRUE;
		}
	}

	mRunning = FALSE;
	if (mStop == TRUE)
	{
		// suspended: the values stay with the saved copy
		mStack.mNumValues = 0;
		return;
	}

	if (mStack.mNumValues != mStackBase)
		LOG.AddMessage("FATAL ERROR:  stack was different size when exiting");
	while (mStack.mNumValues)
		mStack.RemoveTop();
}
