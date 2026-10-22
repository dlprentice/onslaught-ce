// AsmInstruction.cpp: the mission-script VM's instructions (reconstructed from the retail object, 0052d040..0052ec5c;
// not in Stuart's release). The allocations' line numbers are retail's (__FILE__ 0064c5c4).

#include "asminstruction.h"
#include "vminternal.h"
#include "..\DebugLog.h"

// The opcodes' names, in opcode order: retail's object starts its .data with this table (0064c3ec, its strings after
// it) and nothing in the executable reads it. The name is ours.
char	*OPCODE_NAMES[] =									// 0064c3ec (name ours)
{
	"OP_NOP", "OP_PLUS", "OP_MINUS", "OP_MULTIPLY", "OP_DIVIDE", "OP_PUSH",
	"OP_POP", "OP_OR", "OP_AND", "OP_GREAT_THAN", "OP_LESS_THAN", "OP_GREAT_EQ_THAN",
	"OP_LESS_EQ_THAN", "OP_LABEL", "OP_REMOVE_TOP", "OP_CMP", "OP_CMPB", "OP_CMPNEB",
	"OP_JMPNE", "OP_JMPFALSE", "OP_JMP", "OP_GETTOP", "OP_POINTER", "OP_RETURN",
	"OP_CALL", "OP_CALLLOCAL", "OP_PUSHPC",
};

#line 8
// A native call's arguments, popped off the stack (0089c300): at most 7, as SScriptFunction::mArgTypes holds.
CDataType	*gScriptFunctionArgs[7];

//******************************************************************************************
CDataType	*CAsmInstruction::GetAttributeValue(CSymbol *symbol)	// FUNCTION: 0x0052d040
{
	if (symbol->mValue)
		return symbol->mValue->Clone();

	LOG.AddMessage("FATAL ERROR:  no data set for attribute '%s'", symbol->mName.GetString());
#line 56
	return new (MT_SCRIPT) CIntDataType(0);
}

//******************************************************************************************
CAsmInstruction	*CAsmInstruction::Spawn(int opcode, CDXMemBuffer *file)	// FUNCTION: 0x0052d3d0
{
	CAsmInstruction	*instruction = NULL;
	int				attr;

	file->Read(&attr, 4);

	switch (opcode)
	{
#line 87
	case OP_NOP:			instruction = new (MT_INST_SCRIPT) CInstructionOP_NOP(attr); break;
	case OP_PLUS:			instruction = new (MT_INST_SCRIPT) CInstructionOP_PLUS(attr); break;
	case OP_MINUS:			instruction = new (MT_INST_SCRIPT) CInstructionOP_MINUS(attr); break;
	case OP_MULTIPLY:		instruction = new (MT_INST_SCRIPT) CInstructionOP_MULTIPLY(attr); break;
	case OP_DIVIDE:			instruction = new (MT_INST_SCRIPT) CInstructionOP_DIVIDE(attr); break;
	case OP_PUSH:			instruction = new (MT_INST_SCRIPT) CInstructionOP_PUSH(attr); break;
	case OP_POP:			instruction = new (MT_INST_SCRIPT) CInstructionOP_POP(attr); break;
	case OP_OR:				instruction = new (MT_INST_SCRIPT) CInstructionOP_OR(attr); break;
	case OP_AND:			instruction = new (MT_INST_SCRIPT) CInstructionOP_AND(attr); break;
	case OP_GREAT_THAN:		instruction = new (MT_INST_SCRIPT) CInstructionOP_GREAT_THAN(attr); break;
	case OP_LESS_THAN:		instruction = new (MT_INST_SCRIPT) CInstructionOP_LESS_THAN(attr); break;
	case OP_GREAT_EQ_THAN:	instruction = new (MT_INST_SCRIPT) CInstructionOP_GREAT_EQ_THAN(attr); break;
	case OP_LESS_EQ_THAN:	instruction = new (MT_INST_SCRIPT) CInstructionOP_LESS_EQ_THAN(attr); break;
	case OP_LABEL:			instruction = new (MT_INST_SCRIPT) CInstructionOP_LABEL(attr); break;
	case OP_REMOVE_TOP:		instruction = new (MT_INST_SCRIPT) CInstructionOP_REMOVE_TOP(attr); break;
	case OP_CMP:			instruction = new (MT_INST_SCRIPT) CInstructionOP_CMP(attr); break;
	case OP_CMPB:			instruction = new (MT_INST_SCRIPT) CInstructionOP_CMPB(attr); break;
	case OP_CMPNEB:			instruction = new (MT_INST_SCRIPT) CInstructionOP_CMPNEB(attr); break;
	case OP_JMPNE:			instruction = new (MT_INST_SCRIPT) CInstructionOP_JMPNE(attr); break;
	case OP_JMPFALSE:		instruction = new (MT_INST_SCRIPT) CInstructionOP_JMPFALSE(attr); break;
	case OP_JMP:			instruction = new (MT_INST_SCRIPT) CInstructionOP_JMP(attr); break;
	case OP_GETTOP:			instruction = new (MT_INST_SCRIPT) CInstructionOP_GETTOP(attr); break;
	case OP_POINTER:		instruction = new (MT_INST_SCRIPT) CInstructionOP_POINTER(attr); break;
	case OP_RETURN:			instruction = new (MT_INST_SCRIPT) CInstructionOP_RETURN(attr); break;
	case OP_CALL:			instruction = new (MT_INST_SCRIPT) CInstructionOP_CALL(attr); break;
	case OP_CALLLOCAL:		instruction = new (MT_INST_SCRIPT) CInstructionOP_CALLLOCAL(attr); break;
	case OP_PUSHPC:			instruction = new (MT_INST_SCRIPT) CInstructionOP_PUSHPC(attr); break;
	default:
		LOG.AddMessage("FATAL ERROR: uknown instruction in spawn");
		break;
	}

	return instruction;
}

//******************************************************************************************
// the return address a local call jumps back to
void	CInstructionOP_PUSHPC::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e0a0
{
#line 143
	stack->Push(new (MT_SCRIPT) CIntDataType(mAttr));
}

//******************************************************************************************
void	CInstructionOP_RETURN::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e0f0
{
	if (vm->mCallDepth > 0)
	{
		vm->mCallDepth--;

		CDataType	*address = stack->Pop();
		vm->mPC = address->GetInt();
		delete address;

#line 158
		stack->Push(new (MT_SCRIPT) CIntDataType(1));
	}
}

//******************************************************************************************
void	CInstructionOP_PLUS::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e180
{
	CDataType	*b = stack->Pop();
	CDataType	*a = stack->Pop();
	CDataType	*result = a->Add(b);
	delete b;
	delete a;
	stack->Push(result);
}

//******************************************************************************************
void	CInstructionOP_MINUS::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e1d0
{
	CDataType	*b = stack->Pop();
	CDataType	*a = stack->Pop();
	CDataType	*result = a->Subtract(b);
	delete b;
	delete a;
	stack->Push(result);
}

//******************************************************************************************
void	CInstructionOP_MULTIPLY::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e220
{
	CDataType	*b = stack->Pop();
	CDataType	*a = stack->Pop();
	CDataType	*result = a->Multiply(b);
	delete b;
	delete a;
	stack->Push(result);
}

//******************************************************************************************
void	CInstructionOP_DIVIDE::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e270
{
	CDataType	*b = stack->Pop();
	CDataType	*a = stack->Pop();
	CDataType	*result = a->Divide(b);
	delete b;
	delete a;
	stack->Push(result);
}

//******************************************************************************************
void	CInstructionOP_PUSH::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e2c0
{
	stack->Push(GetAttributeValue(symbols->GetSymbol(mAttr)));
}

//******************************************************************************************
void	CInstructionOP_POP::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e2f0
{
	CSymbol	*symbol = symbols->GetSymbol(mAttr);
	delete symbol->mValue;
	symbol->mValue = stack->Pop();
}

//******************************************************************************************
void	CInstructionOP_REMOVE_TOP::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e320
{
	stack->RemoveTop();
}

//******************************************************************************************
// compares the top two values without removing them, for JMPNE
void	CInstructionOP_CMP::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e330
{
	if (stack->GetTop(0)->Equals(stack->GetTop(1)))
		vm->mFlags |= 1;
	else
		vm->mFlags &= ~1;
}

//******************************************************************************************
void	CInstructionOP_CMPB::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e380
{
	CDataType	*a = stack->Pop();
	CDataType	*b = stack->Pop();
	CDataType	*result;

	if (a->Equals(b))
	{
#line 262
		result = new (MT_SCRIPT) CBoolDataType(true);
	}
	else
	{
		result = new (MT_SCRIPT) CBoolDataType(false);
	}

	stack->Push(result);
	delete a;
	delete b;
}

//******************************************************************************************
void	CInstructionOP_OR::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e4d0
{
	CDataType	*a = stack->Pop();
	CDataType	*b = stack->Pop();
	CDataType	*result;

	if (a->IsTrue() || b->IsTrue())
	{
#line 280
		result = new (MT_SCRIPT) CBoolDataType(true);
	}
	else
	{
		result = new (MT_SCRIPT) CBoolDataType(false);
	}

	stack->Push(result);
	delete a;
	delete b;
}

//******************************************************************************************
void	CInstructionOP_AND::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e580
{
	CDataType	*a = stack->Pop();
	CDataType	*b = stack->Pop();
	CDataType	*result;

	if (a->IsTrue() && b->IsTrue())
	{
#line 299
		result = new (MT_SCRIPT) CBoolDataType(true);
	}
	else
	{
		result = new (MT_SCRIPT) CBoolDataType(false);
	}

	stack->Push(result);
	delete a;
	delete b;
}

//******************************************************************************************
void	CInstructionOP_GREAT_THAN::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e630
{
	CDataType	*b = stack->Pop();
	CDataType	*a = stack->Pop();

	if (a->GreaterThan(b))
	{
#line 317
		stack->Push(new (MT_SCRIPT) CBoolDataType(true));
	}
	else
	{
		stack->Push(new (MT_SCRIPT) CBoolDataType(false));
	}

	delete b;
	delete a;
}

//******************************************************************************************
void	CInstructionOP_LESS_THAN::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e6d0
{
	CDataType	*b = stack->Pop();
	CDataType	*a = stack->Pop();

	if (a->LessThan(b))
	{
#line 336
		stack->Push(new (MT_SCRIPT) CBoolDataType(true));
	}
	else
	{
		stack->Push(new (MT_SCRIPT) CBoolDataType(false));
	}

	delete b;
	delete a;
}

//******************************************************************************************
void	CInstructionOP_GREAT_EQ_THAN::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e770
{
	CDataType	*b = stack->Pop();
	CDataType	*a = stack->Pop();

	if (a->GreaterOrEqual(b))
	{
#line 354
		stack->Push(new (MT_SCRIPT) CBoolDataType(true));
	}
	else
	{
		stack->Push(new (MT_SCRIPT) CBoolDataType(false));
	}

	delete b;
	delete a;
}

//******************************************************************************************
void	CInstructionOP_LESS_EQ_THAN::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e810
{
	CDataType	*b = stack->Pop();
	CDataType	*a = stack->Pop();

	if (a->LessOrEqual(b))
	{
#line 372
		stack->Push(new (MT_SCRIPT) CBoolDataType(true));
	}
	else
	{
		stack->Push(new (MT_SCRIPT) CBoolDataType(false));
	}

	delete b;
	delete a;
}

//******************************************************************************************
void	CInstructionOP_CMPNEB::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e8b0
{
	CDataType	*a = stack->Pop();
	CDataType	*b = stack->Pop();
	CDataType	*result;

	if (a->NotEquals(b))
	{
#line 390
		result = new (MT_SCRIPT) CBoolDataType(true);
	}
	else
	{
		result = new (MT_SCRIPT) CBoolDataType(false);
	}

	stack->Push(result);
	delete a;
	delete b;
}

//******************************************************************************************
void	CInstructionOP_JMPFALSE::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e950
{
	CDataType	*value = stack->Pop();
	if (!value->IsTrue())
		vm->mPC = mAttr;
	delete value;
}

//******************************************************************************************
void	CInstructionOP_JMPNE::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e990
{
	if (!(vm->mFlags & 1))
		vm->mPC = mAttr;
}

//******************************************************************************************
void	CInstructionOP_JMP::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e9b0
{
	vm->mPC = mAttr;
}

//******************************************************************************************
// sets a variable from the top of the stack, leaving the value there
void	CInstructionOP_GETTOP::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052e9c0
{
	CSymbol	*symbol = symbols->GetSymbol(mAttr);
	if (!symbol->mValue)
		symbol->mValue = stack->GetTop(0)->Clone();
	else
		symbol->mValue->Assign(stack->GetTop(0));
}

//******************************************************************************************
// the thing the next function call acts on
void	CInstructionOP_POINTER::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052ea10
{
	CDataType	*value = stack->Pop();
	vm->mCode->mScript->mThing = value->GetThing();
	delete value;
}

//******************************************************************************************
// a native function: the attribute's low byte picks it, the next byte counts its arguments and the high half says
// whether it returns a value
void	CInstructionOP_CALL::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052ea40
{
	IScript	*script = vm->mCode->mScript;
	int		num_args = (mAttr >> 8) & 0xff;
	int		c0;

	for (c0 = num_args; c0 > 0; c0--)
		gScriptFunctionArgs[c0 - 1] = stack->Pop();

	if (!script->mThing)
	{
		LOG.AddMessage("FATAL ERROR: Can't call function '%s' on NULL object", gScriptFunctions[mAttr & 0xff].mName);
#line 480
		stack->Push(new (MT_SCRIPT) CIntDataType(0));
		script->mThing = script->mDefaultThing;
		for (c0 = 0; c0 < num_args; c0++)
			delete gScriptFunctionArgs[c0];
		return;
	}

	CDataType	*result = NULL;
	void		(IScript::*function)(CDataType **args, int num_args, CDataType **result) = gScriptFunctions[mAttr & 0xff].mFunction;
	(script->*function)(gScriptFunctionArgs, num_args, &result);

	for (c0 = 0; c0 < num_args; c0++)
		delete gScriptFunctionArgs[c0];

	// the function may have started another script
	CDataStack	*results = stack;
	if (vm->mStop)
		results = &(*script->mVM)->mStack;

	if ((mAttr & ~0xffff) > 0)
	{
		if (!result)
			LOG.AddMessage("ERROR: function '%s' was expecting return value!", gScriptFunctions[mAttr & 0xff].mName);
		else
			results->Push(result);
	}
	else
	{
		if (result)
			LOG.AddMessage("ERROR: script function '%s' returned unexpected value", gScriptFunctions[mAttr & 0xff].mName);
#line 526
		results->Push(new (MT_SCRIPT) CIntDataType(1));
	}

	script->mThing = script->mDefaultThing;
}

//******************************************************************************************
void	CInstructionOP_CALLLOCAL::Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols)	// FUNCTION: 0x0052ec40
{
	vm->mPC = mAttr;
	vm->mCallDepth++;
}

//******************************************************************************************
// DataType.h's int and bool members and asminstruction.h's, which this object emits first
// FUNCTION: 0x0052d0a0 ?Add@CIntDataType@@UAEPAVCDataType@@PAV2@@Z
// FUNCTION: 0x0052d110 ?Subtract@CIntDataType@@UAEPAVCDataType@@PAV2@@Z
// FUNCTION: 0x0052d180 ?Multiply@CIntDataType@@UAEPAVCDataType@@PAV2@@Z
// FUNCTION: 0x0052d1f0 ?Divide@CIntDataType@@UAEPAVCDataType@@PAV2@@Z
// FUNCTION: 0x0052d260 ?Equals@CIntDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052d280 ?NotEquals@CIntDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052d2a0 ?Assign@CIntDataType@@UAEXPAVCDataType@@@Z
// FUNCTION: 0x0052d2c0 ?LessThan@CIntDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052d2e0 ?GreaterThan@CIntDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052d300 ?LessOrEqual@CIntDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052d320 ?GreaterOrEqual@CIntDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052d340 ?Clone@CIntDataType@@UAEPAVCDataType@@XZ
// FUNCTION: 0x0052d370 ?GetFloat@CIntDataType@@UAEMXZ
// FUNCTION: 0x0052d380 ?IsTrue@CIntDataType@@UAE_NXZ
// FUNCTION: 0x0052d3a0 ??_GCDataType@@UAEPAXI@Z
// FUNCTION: 0x0052d950 ?GetOpcode@CInstructionOP_CALL@@UAEHXZ
// FUNCTION: 0x0052d960 ?Clone@CInstructionOP_CALL@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052d990 ?GetOpcode@CInstructionOP_CALLLOCAL@@UAEHXZ
// FUNCTION: 0x0052d9a0 ?Clone@CInstructionOP_CALLLOCAL@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052d9d0 ?Clone@CInstructionOP_RETURN@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052da00 ?GetOpcode@CInstructionOP_RETURN@@UAEHXZ
// FUNCTION: 0x0052da10 ?Clone@CInstructionOP_POINTER@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052da40 ?Clone@CInstructionOP_GETTOP@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052da80 ?Clone@CInstructionOP_JMP@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052dac0 ?GetOpcode@CInstructionOP_JMP@@UAEHXZ
// FUNCTION: 0x0052dad0 ?Clone@CInstructionOP_JMPFALSE@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052db10 ?GetOpcode@CInstructionOP_JMPFALSE@@UAEHXZ
// FUNCTION: 0x0052db20 ?Clone@CInstructionOP_JMPNE@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052db60 ?GetOpcode@CInstructionOP_JMPNE@@UAEHXZ
// FUNCTION: 0x0052db70 ?Clone@CInstructionOP_CMPNEB@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052dbb0 ?Clone@CInstructionOP_CMPB@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052dbf0 ?GetOpcode@CInstructionOP_CMPB@@UAEHXZ
// FUNCTION: 0x0052dc00 ?Clone@CInstructionOP_CMP@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052dc40 ?Clone@CInstructionOP_REMOVE_TOP@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052dc80 ?GetOpcode@CInstructionOP_REMOVE_TOP@@UAEHXZ
// FUNCTION: 0x0052dc90 ?Clone@CInstructionOP_LABEL@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052dcd0 ?GetOpcode@CInstructionOP_LABEL@@UAEHXZ
// FUNCTION: 0x0052dce0 ?Clone@CInstructionOP_LESS_EQ_THAN@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052dd20 ?GetOpcode@CInstructionOP_LESS_EQ_THAN@@UAEHXZ
// FUNCTION: 0x0052dd30 ?Clone@CInstructionOP_GREAT_EQ_THAN@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052dd70 ?Clone@CInstructionOP_LESS_THAN@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052ddb0 ?GetOpcode@CInstructionOP_LESS_THAN@@UAEHXZ
// FUNCTION: 0x0052ddc0 ?Clone@CInstructionOP_GREAT_THAN@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052de00 ?GetOpcode@CInstructionOP_GREAT_THAN@@UAEHXZ
// FUNCTION: 0x0052de10 ?Clone@CInstructionOP_AND@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052de50 ?Clone@CInstructionOP_OR@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052de90 ?GetOpcode@CInstructionOP_OR@@UAEHXZ
// FUNCTION: 0x0052dea0 ?Clone@CInstructionOP_POP@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052dee0 ?Clone@CInstructionOP_PUSH@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052df20 ?Clone@CInstructionOP_MINUS@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052df60 ?Clone@CInstructionOP_PLUS@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052dfa0 ?Clone@CInstructionOP_MULTIPLY@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052dfe0 ?Clone@CInstructionOP_DIVIDE@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052e020 ?Clone@CInstructionOP_NOP@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052e060 ?Clone@CInstructionOP_PUSHPC@@UAEPAVCAsmInstruction@@XZ
// FUNCTION: 0x0052e420 ?Equals@CBoolDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052e440 ?NotEquals@CBoolDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052e460 ?Assign@CBoolDataType@@UAEXPAVCDataType@@@Z
// FUNCTION: 0x0052e480 ?IsTrue@CBoolDataType@@UAE_NXZ
// FUNCTION: 0x0052e490 ?Clone@CBoolDataType@@UAEPAVCDataType@@XZ

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004dbc00 ?Equals@CDataType@@UAE_NPAV1@@Z

// Out of line: retail folds this `return 5` body into 004dbc20 with CInfantryUnit::GetImposterFrames and
// CRTCutscene::Unknown27, and LINK folds an out-of-line body only with out-of-line ones.
int CInstructionOP_PUSH::GetOpcode()
{
	return OP_PUSH;
}
