#include "Common.h"
#include "BattleEngineConfigurations.h"
#include "Console.h"

SLONG	UBattleEngineConfigurations::sConfigurations;
char	*UBattleEngineConfigurations::sConfigurationName[kMaxConfigurations];

void UBattleEngineConfigurations::Initialise()
{
	sConfigurations=0;

	for (int n=0; n<kMaxConfigurations; n++)
		sConfigurationName[n]=0L;
}

void UBattleEngineConfigurations::ShutDown()	// FUNCTION: 0x0040f140
{
	sConfigurations=0;

	for (int n=0; n<kMaxConfigurations; n++)
	{
		if (sConfigurationName[n])
		{
			delete [] sConfigurationName[n];
			sConfigurationName[n]=0L;
		}
	}
}

void UBattleEngineConfigurations::Load( CMEMBUFFER &inFile )	// FUNCTION: 0x0040f180
{
	ShutDown();
	
	CONSOLE.Status("Loading battle engine configurations");

	inFile.Read(&sConfigurations,sizeof(sConfigurations));

	for (SINT n=0; n<sConfigurations; n++)
	{
		char	length;
		inFile.Read(&length,sizeof(length));

		// Load the configuration name
		sConfigurationName[n]=new( MEMTYPE_BATTLEENGINE ) char[length+1];

		int c;
		for (c=0; c<length; c++)
			inFile.Read(&sConfigurationName[n][c],sizeof(sConfigurationName[n][c]));

		sConfigurationName[n][c]=0;
	}

	CONSOLE.StatusDone("Loading battle engine configurations");	
}

void UBattleEngineConfigurations::Skip( CMEMBUFFER &inFile )	// FUNCTION: 0x0040f260
{
	SLONG		configurations;

	inFile.Read(&configurations,sizeof(configurations));

	for (SINT n=0; n<configurations; n++)
	{
		char	length;
		inFile.Read(&length,sizeof(length));

		// Load the configuration name
		char	*name=new( MEMTYPE_BATTLEENGINE ) char[length+1];

		SINT c;
		for (c=0; c<length; c++)
			inFile.Read(&name[c],sizeof(name[c]));

		name[c]=0;

		delete [] name;
	}
}

CBattleEngineData* UBattleEngineConfigurations::GetConfiguration(	// FUNCTION: 0x0040f2f0
	int			inConfigurationId)
{
	if ((inConfigurationId<0) || (inConfigurationId>=sConfigurations))
		inConfigurationId=0;

	CBattleEngineData	*data;
	
	if (data=UBattleEngineDataManager::GetConfiguration(sConfigurationName[inConfigurationId]))
		return data;
#define BATTLEENGINECONFIGURATIONS_CPP
	return UBattleEngineDataManager::GetConfiguration(0);
}
// The object's last initializer, after its code (.CRT$XCU order).
// FUNCTION: 0x0040f3d0 init:ZERO_FVECTOR
#include "mathsconst.h"
