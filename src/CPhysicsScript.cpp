// CPhysicsScript.cpp: reconstructed from CPhysicsScript.obj (0042e790..0042ea9f); Stuart's release has no copy.
// The allocation's __LINE__ is the retail one (16).
#include "common.h"
#include "CPhysicsScript.h"
#include "CPhysicsScriptStatements.h"
#include "mathsconst.h"

// FUNCTION: 0x0042e790 init:ZERO_FVECTOR
// FUNCTION: 0x0042e7b0 init:ID_FMATRIX
SPtrSet<CPhysicsScriptStatement>	*CPhysicsScript::mStatements = NULL;

#line 13
//******************************************************************************************
void CPhysicsScript::Create()	// FUNCTION: 0x0042e880
{
	mStatements = new(MT_SCRIPT) SPtrSet<CPhysicsScriptStatement>;
}

//******************************************************************************************
void CPhysicsScript::Shutdown()	// FUNCTION: 0x0042e8f0
{
	if (mStatements)
	{
		CPhysicsScriptStatement	*statement;

		while (statement = mStatements->First())
		{
			mStatements->Remove(statement);
			delete statement;
		}
		if (mStatements)
		{
			delete mStatements;
			mStatements = NULL;
		}
	}
}

//******************************************************************************************
BOOL CPhysicsScript::Load(CMEMBUFFER &file)	// FUNCTION: 0x0042e950
{
	Shutdown();
	Create();

	SWORD	version;
	file.Read(&version, 2);
	if (version == 18)
	{
		SINT	type, size;

		file.Read(&type, 4);
		while (type != -1)
		{
			file.Read(&size, 4);

			CPhysicsScriptStatement	*statement = CPhysicsScriptStatement::Create(type);
			if (statement)
			{
				statement->Load(file);
				mStatements->Append(statement);
			}
			else
				SkipStatement(file, size);

			file.Read(&type, 4);
		}
		return TRUE;
	}
	return FALSE;
}

//******************************************************************************************
void CPhysicsScript::Process()	// FUNCTION: 0x0042ea60
{
	CPhysicsScriptStatement	*statement;

	for (statement = mStatements->First(); statement; statement = mStatements->Next())
		statement->Execute();
}
