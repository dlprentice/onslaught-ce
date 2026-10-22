// symtab.h: the mission-script object code's symbol table (Symtab.obj; reconstructed, not in Stuart's release).

#ifndef SYMTAB_H
#define SYMTAB_H

#include "DataType.h"
#include "..\OPtrSet.h"

// a named variable (0x18 bytes; Symtab.cpp defines the members): its name, its value, the type the compiler gave it,
// its index in the table and one more flag
class CSymbol
{
public:
	CSymbol(CStringDataType &name, CDataType *value, int type);	// Symtab.cpp; CSymbol::Clone inlines it
	CSymbol(CDXMemBuffer *file);						// Symtab.cpp; CSymbolTable's constructor inlines it
	~CSymbol() { delete mValue; }
	CSymbol			*Clone();							// Symtab.cpp; CSymbolTable::Clone inlines it

	CStringDataType	mName;					// +0x00
	CDataType		*mValue;				// +0x08: NULL until the script first sets it
	int				mType;					// +0x0c
	int				mIndex;					// +0x10
	bool			mUnknown14;				// +0x14: read as a dword; a clone clears its byte
};

// the object code's variables (0x14 bytes; Symtab.obj)
class CSymbolTable : public OPtrSet<CSymbol>
{
public:
	CSymbolTable() { mNextIndex = 0; }		// Clone inlines it
	CSymbolTable(CDXMemBuffer *file);		// 00539770
	~CSymbolTable();						// 00539510
	CSymbolTable	*Clone();				// 005395b0
	CSymbol			*GetSymbol(int n);		// 00539760
	CSymbol			*Find(char *name);		// Symtab.cpp; Clone inlines it
	void			Add(CSymbol *symbol);	// Symtab.cpp; Clone inlines it

	int				mNextIndex;				// +0x10
};

#endif
