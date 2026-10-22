// Symtab.cpp: the object code's symbol table (reconstructed from the retail object, 00539510..00539890; not in
// Stuart's release). The allocations' line numbers are retail's (__FILE__ 00650134). The helpers defined first are
// inlined where they are used; the linker drops their own copies.

#include "..\common.h"
#include "symtab.h"

//******************************************************************************************
// The name is taken from the source's string object: reading it here, not as a char * argument, leaves the inlined
// constructor without a bound name parameter (one counted node fewer in its allocator block), which CSymbolTable::Clone's
// register order needs (005395b0).
CSymbol::CSymbol(CStringDataType &name, CDataType *value, int type) : mName(name.GetString())
{
	mType = type;
	mValue = value;
	mIndex = 0;
	mUnknown14 = false;
}

//******************************************************************************************
CSymbol	*CSymbol::Clone()
{
	CDataType	*value = NULL;
	if (mValue)
		value = mValue->Clone();
#line 45
	return new (MT_SCRIPT) CSymbol(mName, value, mType);
}

//******************************************************************************************
CSymbol	*CSymbolTable::Find(char *name)
{
	for (int i = 0; i < mNextIndex; i++)
	{
		char	*s = at(i)->mName.GetString();
		if (strcmp(s, name) == 0)
			return at(i);
	}
	return NULL;
}

//******************************************************************************************
void	CSymbolTable::Add(CSymbol *symbol)
{
	symbol->mIndex = mNextIndex;
	append(symbol);
	mNextIndex++;
}

//******************************************************************************************
CSymbolTable::~CSymbolTable()	// FUNCTION: 0x00539510
{
	for (int i = 0; i < size(); i++)
	{
		CSymbol	*symbol = at(i);
		if (symbol)
		{
			delete symbol;
			mData[i] = NULL;
		}
	}
}

//******************************************************************************************
// a copy for a cloned script: every symbol and its value cloned, a name only once
// The new symbol is built in esi and kept there as `symbol`, the inlined Find loop's counter in edi: the new object's
// web (two allocator blocks at weight 2) must pop after Find's two strcmp pointer copies (216). The constructor that
// reads the name from the source's string object leaves its block one counted node shorter, which does it.
// Find's name local `s` puts the GetString call before the copy of `name`, as retail schedules it.
CSymbolTable	*CSymbolTable::Clone()	// FUNCTION: 0x005395b0
{
#line 180
	CSymbolTable	*copy = new (MT_SCRIPT) CSymbolTable;

	int	num_symbols = size();
	for (int i = 0; i < num_symbols; i++)
	{
		CSymbol	*symbol = at(i)->Clone();
		if (!copy->Find(symbol->mName.GetString()))
			copy->Add(symbol);
	}

	return copy;
}

//******************************************************************************************
CSymbol	*CSymbolTable::GetSymbol(int n)	// FUNCTION: 0x00539760
{
	return at(n);
}

//******************************************************************************************
CSymbol::CSymbol(CDXMemBuffer *file) : mName(file)
{
	int	type;
	file->Read(&type, 4);
	if (!type)
		mValue = NULL;
	else
		mValue = CDataType::Create(type, file);
	file->Read(&mType, 4);
	file->Read(&mIndex, 4);
	file->Read(&mUnknown14, 4);
}

//******************************************************************************************
// FUNCTION: 0x00539770 ??0CSymbolTable@@QAE@PAVCDXMemBuffer@@@Z
CSymbolTable::CSymbolTable(CDXMemBuffer *file)
{
	int	num_symbols;
	file->Read(&num_symbols, 4);
	for (int i = 0; i < num_symbols; i++)
	{
#line 289
		append(new (MT_SCRIPT) CSymbol(file));
	}
	file->Read(&mNextIndex, 4);
}
