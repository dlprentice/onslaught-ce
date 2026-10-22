// DataType.cpp: the mission-script VM's values (reconstructed from the retail object, 0052ec60..0052f997; not in
// Stuart's release). The allocations' line numbers are retail's (__FILE__ 0064cc80).

#include "DataType.h"
#include "..\DebugLog.h"

//******************************************************************************************
// the value a symbol-table entry holds, by its type id (CScriptObjectCode reads the table)
CDataType	*CDataType::Create(int type, CDXMemBuffer *file)	// FUNCTION: 0x0052ec60
{
	CDataType	*value = NULL;

	switch (type)
	{
#line 22
	case DT_INT:		value = new (MT_SCRIPT) CIntDataType(file); break;
	case DT_FLOAT:		value = new (MT_SCRIPT) CFloatDataType(file); break;
	case DT_STRING:		value = new (MT_SCRIPT) CStringDataType(file); break;
	case DT_BOOL:		value = new (MT_SCRIPT) CBoolDataType(file); break;
	case DT_THINGPTR:	value = new (MT_SCRIPT) CThingPtrDataType(file); break;
	case DT_POSITION:	value = new (MT_SCRIPT) CPositionDataType(file); break;
	default:
		LOG.AddMessage("FATAL ERROR: unknown data type to spawn");
		break;
	}

	return value;
}

//******************************************************************************************
// FUNCTION: 0x0052f690 ??0CStringDataType@@QAE@PAD@Z
CStringDataType::CStringDataType(char *text)
{
	int	len = strlen(text);
#line 43
	mString = new (MT_SCRIPT) char[len + 1];
	strncpy(mString, text, len);
	mString[len] = 0;
}
// FUNCTION: 0x0052f720 ??_GCStringDataType@@UAEPAXI@Z

//******************************************************************************************
CStringDataType::~CStringDataType()	// FUNCTION: 0x0052f740
{
	delete [] mString;
}

//******************************************************************************************
// FUNCTION: 0x0052f790 ??0CStringDataType@@QAE@PAVCDXMemBuffer@@@Z
CStringDataType::CStringDataType(CDXMemBuffer *file)
{
	int	len;
	file->Read(&len, 4);
#line 92
	mString = new (MT_SCRIPT) char[len + 1];
	file->Read(mString, len);
	mString[len] = 0;
}

//******************************************************************************************
CDataType	*CPositionDataType::Add(CDataType *other)	// FUNCTION: 0x0052f820
{
	FVector	sum = mValue + other->GetPosition();
#line 135
	return new (MT_SCRIPT) CPositionDataType(sum);
}

//******************************************************************************************
CDataType	*CPositionDataType::Subtract(CDataType *other)	// FUNCTION: 0x0052f8a0
{
	FVector	diff = mValue - other->GetPosition();
#line 143
	return new (MT_SCRIPT) CPositionDataType(diff);
}

//******************************************************************************************
CDataType	*CPositionDataType::Multiply(CDataType *other)	// FUNCTION: 0x0052f920
{
	float	scale = other->GetFloat();
	FVector	scaled(mValue.X * scale, mValue.Y * scale, mValue.Z * scale);
#line 151
	return new (MT_SCRIPT) CPositionDataType(scaled);
}

//******************************************************************************************
// DataType.h's inline members this object emits first (the int and bool ones are AsmInstruction.obj's)
// FUNCTION: 0x0052ef50 ?Add@CFloatDataType@@UAEPAVCDataType@@PAV2@@Z
// FUNCTION: 0x0052efc0 ?Subtract@CFloatDataType@@UAEPAVCDataType@@PAV2@@Z
// FUNCTION: 0x0052f030 ?Multiply@CFloatDataType@@UAEPAVCDataType@@PAV2@@Z
// FUNCTION: 0x0052f0a0 ?Divide@CFloatDataType@@UAEPAVCDataType@@PAV2@@Z
// FUNCTION: 0x0052f110 ?Equals@CFloatDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052f140 ?NotEquals@CFloatDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052f170 ?Assign@CFloatDataType@@UAEXPAVCDataType@@@Z
// FUNCTION: 0x0052f190 ?LessThan@CFloatDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052f1c0 ?GreaterThan@CFloatDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052f1f0 ?LessOrEqual@CFloatDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052f220 ?GreaterOrEqual@CFloatDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052f250 ?Clone@CFloatDataType@@UAEPAVCDataType@@XZ
// FUNCTION: 0x0052f290 ?GetInt@CFloatDataType@@UAEHXZ
// FUNCTION: 0x0052f2b0 ?GetFloat@CFloatDataType@@UAEMXZ
// FUNCTION: 0x0052f2c0 ?Clone@CStringDataType@@UAEPAVCDataType@@XZ
// FUNCTION: 0x0052f360 ?Equals@CStringDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052f3c0 ?Matches@CStringDataType@@UAE_NPAD@Z
// FUNCTION: 0x0052f410 ?NotEquals@CThingPtrDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052f430 ?Assign@CThingPtrDataType@@UAEXPAVCDataType@@@Z
// FUNCTION: 0x0052f450 ?Equals@CThingPtrDataType@@UAE_NPAVCDataType@@@Z
// FUNCTION: 0x0052f470 ?Clone@CThingPtrDataType@@UAEPAVCDataType@@XZ
// FUNCTION: 0x0052f540 ?GetThing@CThingPtrDataType@@UAEPAVCThing@@XZ
// FUNCTION: 0x0052f550 ??_GCThingPtrDataType@@UAEPAXI@Z
// FUNCTION: 0x0052f5d0 ?Assign@CPositionDataType@@UAEXPAVCDataType@@@Z
// FUNCTION: 0x0052f610 ?Clone@CPositionDataType@@UAEPAVCDataType@@XZ
// FUNCTION: 0x0052f660 ?GetType@CPositionDataType@@UAEHXZ
// FUNCTION: 0x0052f670 ??_GCPositionDataType@@UAEPAXI@Z
// FUNCTION: 0x0052f570 ??1CThingPtrDataType@@UAE@XZ

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x0052d390 ??1CPositionDataType@@UAE@XZ

// Out of line: retail folds this `return 5` body into 004dbc20 with CInfantryUnit::GetImposterFrames and
// CRTCutscene::Unknown27, and LINK folds an out-of-line body only with out-of-line ones.
int CThingPtrDataType::GetType()
{
	return DT_THINGPTR;
}
