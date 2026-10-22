// EventFunction.cpp: a script's event handlers (reconstructed from the retail object, 0052f9a0..0052fe2f; not in
// Stuart's release). The allocations' line numbers are retail's (__FILE__ 0064cce0).

#include "EventFunction.h"
#include "..\DebugLog.h"

//******************************************************************************************
inline void CEventFunction::AddParam(CDataType *name)
{
#line 27
	mParams.Append(new (MT_SCRIPT) SEventParam(name));
}

//******************************************************************************************
CEventFunction::~CEventFunction()	// FUNCTION: 0x0052f9a0
{
	mParams.DeleteAll();
}
// FUNCTION: 0x0052fa50 ??_GCEventFunction@@UAEPAXI@Z

//******************************************************************************************
// FUNCTION: 0x0052fa70 ??0CEventFunction@@QAE@PAVCMissionScriptObjectCode@@PAVCDXMemBuffer@@@Z
CEventFunction::CEventFunction(CMissionScriptObjectCode *code, CDXMemBuffer *file)
{
	mCode = code;
	file->Read(&mEntryPC, 4);

	int	num_params;
	file->Read(&num_params, 4);
	for (int i = 0; i < num_params; i++)
	{
		int	index;
		file->Read(&index, 4);
		CSymbolTable	*symbols = mCode->mSymbols;
		CDataType	*value = symbols->GetSymbol(index)->mValue;
		if (value->GetType() == DT_STRING)
		{
#line 64
			mParams.Append(new (MT_SCRIPT) SEventParam(value));
		}
		else
			LOG.AddMessage("FATAL ERROR: Event Function was expecting a string");
	}
}

//******************************************************************************************
CEventFunction	*CEventFunction::Clone(CMissionScriptObjectCode *code)	// FUNCTION: 0x0052fbb0
{
#line 78
	CEventFunction	*copy = new (MT_SCRIPT) CEventFunction(mEntryPC, code);

	// the clone's event names, found by symbol index in its own object code
	CSymbolTable	*symbols = mCode->mSymbols;
	if (!symbols)
	{
		LOG.AddMessage("FATAL ERROR: Could not find symbol table in clone");
		return copy;
	}

	int				num_symbols = symbols->size();
	SEventParam		*param;

	FOR_ALL_ITEMS_IN(mParams, param)
	{
		int	n = 0;
		CSymbol	*symbol;
		while (TRUE)
		{
			symbol = symbols->GetSymbol(n);
			if (symbol && symbol->mValue && symbol->mValue == param->mName)
				break;
			if (++n >= num_symbols)
			{
				LOG.AddMessage("FATAL ERROR can't find event string in symbol table");
				return copy;
			}
		}

		CDataType	*value = copy->mCode->mSymbols->GetSymbol(n)->mValue;
		if (value && value->GetType() == DT_STRING && strcmp(value->GetString(), symbol->mValue->GetString()) == 0)
			copy->AddParam(value);
		else
			LOG.AddMessage("FATAL ERROR: Data type wrong type in clone for event function");
	}

	return copy;
}

//******************************************************************************************
void	CEventFunction::Execute()	// FUNCTION: 0x0052fda0
{
	CDataType	*args[10];
	int			num_args = 0;

	ListIterator<SEventParam>	it(&mParams);
	for (SEventParam *param = it.First(); param; param = it.Next())
	{
#line 150
		args[num_args++] = new (MT_SCRIPT) CBoolDataType(param->mEvent->mFired);
	}

	MISSION_SCRIPT_VM.CallFunction(mCode, mEntryPC, args, num_args);
}
