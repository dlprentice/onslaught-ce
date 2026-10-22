// asminstruction.h: the mission-script VM's instructions (reconstructed from the retail object; not in Stuart's
// release). An instruction is 0x0c bytes: its vtable, the attribute the object code stores after the opcode (+0x04),
// and a word nothing here sets (+0x08). Each class has three slots: Execute, Clone and GetOpcode, whose value is the
// opcode CAsmInstruction::Spawn reads (0052d3d0). The classes keep the order their Clone allocations' retail
// __LINE__ values give (the #line directives; __FILE__ 0064cae4). NOP and LABEL do nothing (0x00453ac0, folded).

#ifndef ASMINSTRUCTION_H
#define ASMINSTRUCTION_H

#include "DataType.h"

class CVM;
class CDataStack;
class CSymbolTable;
class CSymbol;

enum EOpcode
{
	OP_NOP,				// 0
	OP_PLUS,
	OP_MINUS,
	OP_MULTIPLY,
	OP_DIVIDE,
	OP_PUSH,
	OP_POP,
	OP_OR,
	OP_AND,
	OP_GREAT_THAN,
	OP_LESS_THAN,
	OP_GREAT_EQ_THAN,
	OP_LESS_EQ_THAN,
	OP_LABEL,
	OP_REMOVE_TOP,
	OP_CMP,
	OP_CMPB,
	OP_CMPNEB,
	OP_JMPNE,
	OP_JMPFALSE,
	OP_JMP,
	OP_GETTOP,
	OP_POINTER,
	OP_RETURN,
	OP_CALL,
	OP_CALLLOCAL,
	OP_PUSHPC,
	OP_NUM
};

class CAsmInstruction
{
public:
	CAsmInstruction(int attr) { mAttr = attr; }

	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols) = 0;
	virtual CAsmInstruction	*Clone() = 0;
	virtual int				GetOpcode() = 0;

	// the value an instruction's symbol holds (0052d040)
	CDataType				*GetAttributeValue(CSymbol *symbol);

	// the instruction the object code stores next (0052d3d0)
	static CAsmInstruction	*Spawn(int opcode, CDXMemBuffer *file);

	int						mAttr;			// +0x04
	int						mUnknown08;		// +0x08
};

class CInstructionOP_CALL : public CAsmInstruction
{
public:
	CInstructionOP_CALL(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 88
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_CALL(mAttr); }
	virtual int				GetOpcode() { return OP_CALL; }
};

class CInstructionOP_CALLLOCAL : public CAsmInstruction
{
public:
	CInstructionOP_CALLLOCAL(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 103
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_CALLLOCAL(mAttr); }
	virtual int				GetOpcode() { return OP_CALLLOCAL; }
};

class CInstructionOP_RETURN : public CAsmInstruction
{
public:
	CInstructionOP_RETURN(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 114
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_RETURN(mAttr); }
	virtual int				GetOpcode() { return OP_RETURN; }
};

class CInstructionOP_POINTER : public CAsmInstruction
{
public:
	CInstructionOP_POINTER(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 123
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_POINTER(mAttr); }
	virtual int				GetOpcode() { return OP_POINTER; }
};

class CInstructionOP_GETTOP : public CAsmInstruction
{
public:
	CInstructionOP_GETTOP(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 133
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_GETTOP(mAttr); }
	virtual int				GetOpcode() { return OP_GETTOP; }
};

class CInstructionOP_JMP : public CAsmInstruction
{
public:
	CInstructionOP_JMP(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 142
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_JMP(mAttr); }
	virtual int				GetOpcode() { return OP_JMP; }
};

class CInstructionOP_JMPFALSE : public CAsmInstruction
{
public:
	CInstructionOP_JMPFALSE(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 151
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_JMPFALSE(mAttr); }
	virtual int				GetOpcode() { return OP_JMPFALSE; }
};

class CInstructionOP_JMPNE : public CAsmInstruction
{
public:
	CInstructionOP_JMPNE(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 160
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_JMPNE(mAttr); }
	virtual int				GetOpcode() { return OP_JMPNE; }
};

class CInstructionOP_CMPNEB : public CAsmInstruction
{
public:
	CInstructionOP_CMPNEB(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 169
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_CMPNEB(mAttr); }
	virtual int				GetOpcode() { return OP_CMPNEB; }
};

class CInstructionOP_CMPB : public CAsmInstruction
{
public:
	CInstructionOP_CMPB(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 178
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_CMPB(mAttr); }
	virtual int				GetOpcode() { return OP_CMPB; }
};

class CInstructionOP_CMP : public CAsmInstruction
{
public:
	CInstructionOP_CMP(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 187
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_CMP(mAttr); }
	virtual int				GetOpcode() { return OP_CMP; }
};

class CInstructionOP_REMOVE_TOP : public CAsmInstruction
{
public:
	CInstructionOP_REMOVE_TOP(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 196
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_REMOVE_TOP(mAttr); }
	virtual int				GetOpcode() { return OP_REMOVE_TOP; }
};

class CInstructionOP_LABEL : public CAsmInstruction
{
public:
	CInstructionOP_LABEL(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols) {}
#line 205
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_LABEL(mAttr); }
	virtual int				GetOpcode() { return OP_LABEL; }
};

class CInstructionOP_LESS_EQ_THAN : public CAsmInstruction
{
public:
	CInstructionOP_LESS_EQ_THAN(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 214
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_LESS_EQ_THAN(mAttr); }
	virtual int				GetOpcode() { return OP_LESS_EQ_THAN; }
};

class CInstructionOP_GREAT_EQ_THAN : public CAsmInstruction
{
public:
	CInstructionOP_GREAT_EQ_THAN(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 223
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_GREAT_EQ_THAN(mAttr); }
	virtual int				GetOpcode() { return OP_GREAT_EQ_THAN; }
};

class CInstructionOP_LESS_THAN : public CAsmInstruction
{
public:
	CInstructionOP_LESS_THAN(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 232
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_LESS_THAN(mAttr); }
	virtual int				GetOpcode() { return OP_LESS_THAN; }
};

class CInstructionOP_GREAT_THAN : public CAsmInstruction
{
public:
	CInstructionOP_GREAT_THAN(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 241
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_GREAT_THAN(mAttr); }
	virtual int				GetOpcode() { return OP_GREAT_THAN; }
};

class CInstructionOP_AND : public CAsmInstruction
{
public:
	CInstructionOP_AND(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 250
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_AND(mAttr); }
	virtual int				GetOpcode() { return OP_AND; }
};

class CInstructionOP_OR : public CAsmInstruction
{
public:
	CInstructionOP_OR(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 259
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_OR(mAttr); }
	virtual int				GetOpcode() { return OP_OR; }
};

class CInstructionOP_POP : public CAsmInstruction
{
public:
	CInstructionOP_POP(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 268
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_POP(mAttr); }
	virtual int				GetOpcode() { return OP_POP; }
};

class CInstructionOP_PUSH : public CAsmInstruction
{
public:
	CInstructionOP_PUSH(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 277
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_PUSH(mAttr); }
	virtual int				GetOpcode();					// AsmInstruction.cpp (the shared 004dbc20)
};

class CInstructionOP_MINUS : public CAsmInstruction
{
public:
	CInstructionOP_MINUS(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 286
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_MINUS(mAttr); }
	virtual int				GetOpcode() { return OP_MINUS; }
};

class CInstructionOP_PLUS : public CAsmInstruction
{
public:
	CInstructionOP_PLUS(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 295
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_PLUS(mAttr); }
	virtual int				GetOpcode() { return OP_PLUS; }
};

class CInstructionOP_MULTIPLY : public CAsmInstruction
{
public:
	CInstructionOP_MULTIPLY(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 304
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_MULTIPLY(mAttr); }
	virtual int				GetOpcode() { return OP_MULTIPLY; }
};

class CInstructionOP_DIVIDE : public CAsmInstruction
{
public:
	CInstructionOP_DIVIDE(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 313
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_DIVIDE(mAttr); }
	virtual int				GetOpcode() { return OP_DIVIDE; }
};

class CInstructionOP_NOP : public CAsmInstruction
{
public:
	CInstructionOP_NOP(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols) {}
#line 322
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_NOP(mAttr); }
	virtual int				GetOpcode() { return OP_NOP; }
};

class CInstructionOP_PUSHPC : public CAsmInstruction
{
public:
	CInstructionOP_PUSHPC(int attr) : CAsmInstruction(attr) {}
	virtual void			Execute(CVM *vm, CDataStack *stack, CSymbolTable *symbols);
#line 331
	virtual CAsmInstruction	*Clone() { return new (MT_INST_SCRIPT) CInstructionOP_PUSHPC(mAttr); }
	virtual int				GetOpcode() { return OP_PUSHPC; }
};

#endif
