// DataType.h: the mission-script VM's values (reconstructed from the retail object; not in Stuart's release).
//
// CDataType (vtable 005e4b4c, 20 slots) and its six kinds, by the type id the object code stores (CDataType::Create
// 0052ec60): 1 int (005e4af8), 2 float (005e4ea4), 3 string (005e4e4c, one slot more), 4 bool (005e4d50), 5 thing
// pointer (005e4df8), 6 position (005e4da4). The slot order is the base's; names follow what each slot does. The
// inline members are emitted beside the first function that needs their class's vtable, in the order they are
// defined here (AsmInstruction.obj holds the int and bool ones, DataType.obj the rest). The #line directives are the
// allocations' retail __LINE__ values (the file name is this header's, 0064c628).

#ifndef DATATYPE_H
#define DATATYPE_H

#include "..\common.h"
#include "..\maths.h"
#include "..\activereader.h"
#include "..\membuffer.h"
#include "..\DXMemBuffer.h"

class CThing;
class CDXMemBuffer;

enum EDataType
{
	DT_NONE = 0,
	DT_INT,
	DT_FLOAT,
	DT_STRING,
	DT_BOOL,
	DT_THINGPTR,
	DT_POSITION
};

class CDataType
{
public:
	virtual				~CDataType() {}															// 0

	// arithmetic returns a new value, or NULL where the kind has none
	virtual CDataType	*Add(CDataType *other) { return NULL; }									// 1
	virtual CDataType	*Subtract(CDataType *other) { return NULL; }							// 2
	virtual CDataType	*Multiply(CDataType *other) { return NULL; }							// 3
	virtual CDataType	*Divide(CDataType *other) { return NULL; }								// 4
	virtual void		Assign(CDataType *other) {}												// 5
	virtual bool		Equals(CDataType *other) { return false; }								// 6
	virtual bool		NotEquals(CDataType *other) { return false; }							// 7
	virtual bool		LessThan(CDataType *other) { return false; }							// 8
	virtual bool		GreaterThan(CDataType *other) { return false; }							// 9
	virtual bool		LessOrEqual(CDataType *other) { return false; }							// 10
	virtual bool		GreaterOrEqual(CDataType *other) { return false; }						// 11

	virtual int			GetInt() { return 0; }													// 12
	virtual float		GetFloat() { return 0.f; }												// 13
	virtual char		*GetString() { return NULL; }											// 14
	virtual bool		IsTrue() { return true; }												// 15
	virtual CThing		*GetThing() { return NULL; }											// 16
	virtual FVector		GetPosition() { return FVector(0.f, 0.f, 0.f); }						// 17

	virtual CDataType	*Clone() = 0;															// 18
	virtual int			GetType() = 0;															// 19

	// the object code's values (0052ec60)
	static CDataType	*Create(int type, CDXMemBuffer *file);
};

//******************************************************************************************
class CIntDataType : public CDataType
{
public:
	CIntDataType(int value) { mValue = value; }
	CIntDataType(CDXMemBuffer *file) { file->Read(&mValue, 4); }

#line 107
	virtual CDataType	*Add(CDataType *other) { return new (MEMTYPE_SCRIPT) CIntDataType(mValue + other->GetInt()); }
	virtual CDataType	*Subtract(CDataType *other) { return new (MEMTYPE_SCRIPT) CIntDataType(mValue - other->GetInt()); }
	virtual CDataType	*Multiply(CDataType *other) { return new (MEMTYPE_SCRIPT) CIntDataType(mValue * other->GetInt()); }
	virtual CDataType	*Divide(CDataType *other) { return new (MEMTYPE_SCRIPT) CIntDataType(mValue / other->GetInt()); }
	virtual bool		Equals(CDataType *other) { return mValue == other->GetInt(); }
	virtual bool		NotEquals(CDataType *other) { return mValue != other->GetInt(); }
	virtual void		Assign(CDataType *other) { mValue = other->GetInt(); }
	virtual bool		LessThan(CDataType *other) { return mValue < other->GetInt(); }
	virtual bool		GreaterThan(CDataType *other) { return mValue > other->GetInt(); }
	virtual bool		LessOrEqual(CDataType *other) { return mValue <= other->GetInt(); }
	virtual bool		GreaterOrEqual(CDataType *other) { return mValue >= other->GetInt(); }

#line 123
	virtual CDataType	*Clone() { return new (MEMTYPE_SCRIPT) CIntDataType(mValue); }
	virtual int			GetInt() { return mValue; }
	virtual float		GetFloat() { return float(mValue); }
	virtual bool		IsTrue() { return mValue ? true : false; }
	virtual int			GetType() { return DT_INT; }

	int					mValue;			// +0x04
};

//******************************************************************************************
class CBoolDataType : public CDataType
{
public:
	CBoolDataType(bool value) { mValue = value; }
	CBoolDataType(CDXMemBuffer *file) { file->Read(&mValue, 4); }

	virtual bool		Equals(CDataType *other) { return mValue == other->IsTrue(); }
	virtual bool		NotEquals(CDataType *other) { return mValue != other->IsTrue(); }
	virtual void		Assign(CDataType *other) { mValue = other->IsTrue(); }
	virtual bool		IsTrue() { return mValue; }

#line 149
	virtual CDataType	*Clone() { return new (MEMTYPE_SCRIPT) CBoolDataType(mValue); }
	virtual int			GetType() { return DT_BOOL; }

	bool				mValue;			// +0x04
};

//******************************************************************************************
// Class order Float, String, ThingPtr, Position: DataType.obj emits these classes' inline members in that order
// (retail 0052ef50..0052f670). The ??_R4 locators' order is the allocation-keyed walk, not this order.
class CFloatDataType : public CDataType
{
public:
	CFloatDataType(float value) { mValue = value; }
	CFloatDataType(CDXMemBuffer *file) { file->Read(&mValue, 4); }

#line 163
	virtual CDataType	*Add(CDataType *other) { return new (MEMTYPE_SCRIPT) CFloatDataType(mValue + other->GetFloat()); }
	virtual CDataType	*Subtract(CDataType *other) { return new (MEMTYPE_SCRIPT) CFloatDataType(mValue - other->GetFloat()); }
	virtual CDataType	*Multiply(CDataType *other) { return new (MEMTYPE_SCRIPT) CFloatDataType(mValue * other->GetFloat()); }
	virtual CDataType	*Divide(CDataType *other) { return new (MEMTYPE_SCRIPT) CFloatDataType(mValue / other->GetFloat()); }
	virtual bool		Equals(CDataType *other) { return mValue == other->GetFloat(); }
	virtual bool		NotEquals(CDataType *other) { return mValue != other->GetFloat(); }
	virtual void		Assign(CDataType *other) { mValue = other->GetFloat(); }
	virtual bool		LessThan(CDataType *other) { return mValue < other->GetFloat(); }
	virtual bool		GreaterThan(CDataType *other) { return mValue > other->GetFloat(); }
	virtual bool		LessOrEqual(CDataType *other) { return mValue <= other->GetFloat(); }
	virtual bool		GreaterOrEqual(CDataType *other) { return mValue >= other->GetFloat(); }

#line 180
	virtual CDataType	*Clone() { return new (MEMTYPE_SCRIPT) CFloatDataType(mValue); }
	virtual int			GetInt() { return int(mValue); }
	virtual float		GetFloat() { return mValue; }
	virtual int			GetType() { return DT_FLOAT; }

	float				mValue;			// +0x04
};

//******************************************************************************************
class CStringDataType : public CDataType
{
public:
	CStringDataType(char *text);							// 0052f690
	CStringDataType(CDXMemBuffer *file);					// 0052f790
	virtual				~CStringDataType();					// 0052f740

#line 200
	virtual CDataType	*Clone() { return new (MEMTYPE_SCRIPT) CStringDataType(mString); }
	virtual bool		Equals(CDataType *other) { return strcmp(mString, other->GetString()) == 0; }
	virtual bool		Matches(char *text) { return strcmp(mString, text) == 0; }	// 20
	virtual bool		NotEquals(CDataType *other) { return !Equals(other); }
	virtual char		*GetString() { return mString; }
	virtual int			GetType() { return DT_STRING; }

	char				*mString;		// +0x04
};

//******************************************************************************************
class CThingPtrDataType : public CDataType
{
public:
	CThingPtrDataType(CThing *thing) : mThing(thing) {}
	CThingPtrDataType(CDXMemBuffer *file) { int id; file->Read(&id, 4); }	// the thing is bound later

	virtual bool		NotEquals(CDataType *other) { return !Equals(other); }
	virtual void		Assign(CDataType *other) { mThing.SetReader(other->GetThing()); }
	virtual bool		Equals(CDataType *other) { return mThing.ToRead() == other->GetThing(); }

#line 229
	virtual CDataType	*Clone() { return new (MEMTYPE_SCRIPT) CThingPtrDataType(mThing.ToRead()); }
	virtual CThing		*GetThing() { return mThing.ToRead(); }
	virtual int			GetType();						// DataType.cpp (the shared 004dbc20)

	CActiveReader<CThing>	mThing;		// +0x04
};

//******************************************************************************************
class CPositionDataType : public CDataType
{
public:
	CPositionDataType(FVector &pos) : mValue(pos) {}
	CPositionDataType(CDXMemBuffer *file) { file->Read(&mValue.X, 4); file->Read(&mValue.Y, 4); file->Read(&mValue.Z, 4); }

	virtual CDataType	*Add(CDataType *other);				// 0052f820
	virtual CDataType	*Subtract(CDataType *other);		// 0052f8a0
	virtual CDataType	*Multiply(CDataType *other);		// 0052f920: scaled by a float
	virtual void		Assign(CDataType *other) { mValue = other->GetPosition(); }

#line 252
	virtual CDataType	*Clone() { return new (MEMTYPE_SCRIPT) CPositionDataType(mValue); }
	virtual FVector		GetPosition() { return mValue; }
	virtual int			GetType() { return DT_POSITION; }

	FVector				mValue;			// +0x04
};

#endif
