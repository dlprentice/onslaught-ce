// BattleEngineConfigurations.h: Stuart's, verbatim. ShutDown is 0040f140, Load 0040f180, Skip 0040f260 and
// GetConfiguration 0040f2f0 (CWorld::Clear 0050aee7, CWorld::LoadHeader 0050d4ff).

#ifndef BATTLEENGINECONFIGURATIONS_H
#define BATTLEENGINECONFIGURATIONS_H

#include	"BattleEngineDataManager.h"
#include	"membuffer.h"

static const SINT kMaxConfigurations=20;

class UBattleEngineConfigurations
{
	private:
		static SLONG				sConfigurations;
		static char					*sConfigurationName[kMaxConfigurations];

	public:

		static void					Initialise();
		static void					ShutDown();

		static void					Load( CMEMBUFFER &inFile );
		static void					Skip( CMEMBUFFER &inFile );

		static CBattleEngineData*	GetConfiguration(
										int			inConfigurationId);

		static SLONG				CountConfigurations()				{ return sConfigurations; }
};

#endif
