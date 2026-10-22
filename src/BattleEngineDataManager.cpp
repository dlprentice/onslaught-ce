// BattleEngineDataManager.cpp
#ifdef EDITORBUILD
#include	"stdafx.h"
#endif

#include	"common.h"
#include	"BattleEngineDataManager.h"
#include	"MemBuffer.h"
#include	"Weapon.h"
#include	<string.h>

SPtrSet<CBattleEngineData>	UBattleEngineDataManager::sData;

CBattleEngineData::CBattleEngineData()	// FUNCTION: 0x0040f520
{
	mConfigurationName=NULL;

	for (int n=0; n<kBattleEngineStores; n++)
	{
		mStoreHeat[n]=false;
		mStoreValue[n]=0;
	}

	mAugWeapon=NULL;
	mPrimaryWeapon=NULL;
	mExplosion=NULL;
	mCockpit=NULL;
}

void CBattleEngineData::Initialise()	// FUNCTION: 0x0040f590
{
	mConfigurationName=new( MEMTYPE_BATTLEENGINE ) char[9];
	strcpy(mConfigurationName,"Standard");

	char	**weapon=new( MEMTYPE_BATTLEENGINE ) char*;
	*weapon=new( MEMTYPE_BATTLEENGINE ) char[16];
	strcpy(*weapon,"Vulcan Cannon 1");
	mWalkerWeapons.Add(weapon);
	
	weapon=new( MEMTYPE_BATTLEENGINE ) char*;
	*weapon=new( MEMTYPE_BATTLEENGINE ) char[17];
	strcpy(*weapon,"Pulse Cannon Pod");
	mWalkerWeapons.Add(weapon);

	weapon=new( MEMTYPE_BATTLEENGINE ) char*;
	*weapon=new( MEMTYPE_BATTLEENGINE ) char[16];
	strcpy(*weapon,"Vulcan Cannon 1");
	mJetWeapons.Add(weapon);

	weapon=new( MEMTYPE_BATTLEENGINE ) char*;
	*weapon=new( MEMTYPE_BATTLEENGINE ) char[12];
	strcpy(*weapon,"Missile Pod");
	mJetWeapons.Add(weapon);

	for (int n=0; n<kBattleEngineStores; n++)
	{
		mStoreHeat[n]=false;
		mStoreValue[n]=1000;
	}

	mExplosion=new( MEMTYPE_BATTLEENGINE ) char[29];
	strcpy(mExplosion,"Animated Explosion Emitter 2");

	mAugWeapon=new( MEMTYPE_BATTLEENGINE) char[1];
	mAugWeapon[0]=0;

	mPrimaryWeapon=new( MEMTYPE_BATTLEENGINE) char[1];
	mPrimaryWeapon[0]=0;

	mCockpit=new( MEMTYPE_BATTLEENGINE ) char[13];
	strcpy(mCockpit,"cockpit2.msh");

	mLife=20.0f;

	mEnergy=2.5f;
	mGroundEnergyIncrease=0.01f;
	mMinAirEnergyCost=0.1f;
	mMaxAirEnergyCost=0.3f;
	mMinTransformEnergy=1.0f;

	mMaxAirVelocity=7.5f;
	mMinAirVelocity=5.0f;
	mGroundVelocity=4.0f;

	mAirTurnRate=2.0f;
	mGroundTurnRate=1.5f;

	mShieldEfficiency=90.0f;

	mWalkFriction=0.9f;
	mMaxWalkVelocity=0.15f;

	mRollEnergyCost=1.0f;
	mLoopEnergyCost=1.0f;

	mLanguageName=1;

	mStealth=0;
}

void CBattleEngineData::Shutdown()	// FUNCTION: 0x0040f890
{
	if (mConfigurationName)
	{
		delete [] mConfigurationName;
		mConfigurationName=0L;
	}

	while (char **name=mJetWeapons.First())
	{
		mJetWeapons.Remove(name);
		delete [] *name;
		delete name;
	}

	while (char **name=mWalkerWeapons.First())
	{
		mWalkerWeapons.Remove(name);
		delete [] *name;
		delete name;
	}

	if (mExplosion)
	{
		delete [] mExplosion;
		mExplosion=NULL;
	}

	if (mAugWeapon)
	{
		delete [] mAugWeapon;
		mAugWeapon=NULL;
	}

	if (mPrimaryWeapon)
	{
		delete [] mPrimaryWeapon;
		mPrimaryWeapon=NULL;
	}

	if (mCockpit)
	{
		delete [] mCockpit;
		mCockpit=NULL;
	}
}

void CBattleEngineData::Load(	// FUNCTION: 0x0040f980
	CMEMBUFFER	&inFile)
{
	int		version;
	char	temp[256];

	Shutdown();

	inFile.Read(&version,sizeof(version));

	inFile.Read(&mLife,sizeof(mLife));
	inFile.Read(&mEnergy,sizeof(mEnergy));
	
	inFile.Read(&mGroundEnergyIncrease,sizeof(mGroundEnergyIncrease));
	inFile.Read(&mMaxAirEnergyCost,sizeof(mMaxAirEnergyCost));
	inFile.Read(&mMinTransformEnergy,sizeof(mMinTransformEnergy));

	inFile.Read(&mMaxAirVelocity,sizeof(mMaxAirVelocity));
	inFile.Read(&mGroundVelocity,sizeof(mGroundVelocity));

	inFile.Read(&mAirTurnRate,sizeof(mAirTurnRate));
	inFile.Read(&mGroundTurnRate,sizeof(mGroundTurnRate));

	if (version<8)
	{
		float	temp;

		inFile.Read(&temp,sizeof(temp));
		inFile.Read(&temp,sizeof(temp));
		inFile.Read(&temp,sizeof(temp));
	}

	int n,m;

	if (version<=4)
	{
		n=-1;
		do
		{
			n++;
			inFile.Read(&temp[n],sizeof(temp[n]));
		}
		while (temp[n]);

		char	**weapon=new( MEMTYPE_BATTLEENGINE ) char*;
		*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
		for (m=0; m<=n; m++)
			(*weapon)[m]=temp[m];

		mJetWeapons.Add(weapon);

		n=-1;
		do
		{
			n++;
			inFile.Read(&temp[n],sizeof(temp[n]));
		}
		while (temp[n]);

		weapon=new( MEMTYPE_BATTLEENGINE ) char*;
		*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
		for (m=0; m<=n; m++)
			(*weapon)[m]=temp[m];

		mJetWeapons.Add(weapon);

		n=-1;
		do
		{
			n++;
			inFile.Read(&temp[n],sizeof(temp[n]));
		}
		while (temp[n]);

		weapon=new( MEMTYPE_BATTLEENGINE ) char*;
		*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
		for (m=0; m<=n; m++)
			(*weapon)[m]=temp[m];

		mWalkerWeapons.Add(weapon);

		n=-1;
		do
		{
			n++;
			inFile.Read(&temp[n],sizeof(temp[n]));
		}
		while (temp[n]);

		weapon=new( MEMTYPE_BATTLEENGINE ) char*;
		*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
		for (m=0; m<=n; m++)
			(*weapon)[m]=temp[m];

		mWalkerWeapons.Add(weapon);

		n=-1;
		do
		{
			n++;
			inFile.Read(&temp[n],sizeof(temp[n]));
		}
		while (temp[n]);

		weapon=new( MEMTYPE_BATTLEENGINE ) char*;
		*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
		for (m=0; m<=n; m++)
			(*weapon)[m]=temp[m];

		mWalkerWeapons.Add(weapon);
	}

	n=-1;
	do
	{
		n++;
		inFile.Read(&temp[n],sizeof(temp[n]));
	}
	while (temp[n]);

	mConfigurationName=new( MEMTYPE_BATTLEENGINE ) char[n+1];
	for (m=0; m<=n; m++)
		mConfigurationName[m]=temp[m];

	if (version<=4)
	{
		float	temp;

		inFile.Read(&temp,sizeof(temp));
		inFile.Read(&temp,sizeof(temp));
		inFile.Read(&temp,sizeof(temp));
		inFile.Read(&temp,sizeof(temp));
	}
		
	if (version>1)
		inFile.Read(&mShieldEfficiency,sizeof(mShieldEfficiency));

	if (version>2)
		inFile.Read(&mStealth,sizeof(mStealth));

	if (version>3)
	{
		n=-1;
		do
		{
			n++;
			inFile.Read(&temp[n],sizeof(temp[n]));
		}
		while (temp[n]);

		mExplosion=new( MEMTYPE_BATTLEENGINE ) char[n+1];
		for (m=0; m<=n; m++)
			mExplosion[m]=temp[m];
	}
	else
	{
		mExplosion=new( MEMTYPE_BATTLEENGINE ) char[29];
		strcpy(mExplosion,"Animated Explosion Emitter 2");
	}

	if (version>4)
	{
		// Load the walker weapons
		int		weapons;
		inFile.Read(&weapons,sizeof(weapons));

		while (weapons)
		{
			n=-1;
			do
			{
				n++;
				inFile.Read(&temp[n],sizeof(temp[n]));
			}
			while (temp[n]);

			char	**weapon=new( MEMTYPE_BATTLEENGINE ) char*;
			*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
			for (m=0; m<=n; m++)
				(*weapon)[m]=temp[m];

			mWalkerWeapons.Append(weapon);

			weapons--;
		}

		// Load the jet weapons
		inFile.Read(&weapons,sizeof(weapons));

		while (weapons)
		{
			n=-1;
			do
			{
				n++;
				inFile.Read(&temp[n],sizeof(temp[n]));
			}
			while (temp[n]);

			char	**weapon=new( MEMTYPE_BATTLEENGINE ) char*;
			*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
			for (m=0; m<=n; m++)
				(*weapon)[m]=temp[m];

			mJetWeapons.Append(weapon);

			weapons--;
		}

		// Load the ammo stores
		for (int n=0; n<kBattleEngineStores; n++)
		{
			inFile.Read(&mStoreHeat[n],sizeof(mStoreHeat[n]));
			inFile.Read(&mStoreValue[n],sizeof(mStoreValue[n]));
		}
	}

	if (version>5)
	{
		inFile.Read(&mMinAirVelocity,sizeof(mMinAirVelocity));
	}

	if (version>6)
	{
		inFile.Read(&mMaxWalkVelocity,sizeof(mMaxWalkVelocity));
		inFile.Read(&mWalkFriction,sizeof(mWalkFriction));
	}

	if (version>7)
	{
		inFile.Read(&mMinAirEnergyCost,sizeof(mMinAirEnergyCost));
		inFile.Read(&mRollEnergyCost,sizeof(mRollEnergyCost));
		inFile.Read(&mLoopEnergyCost,sizeof(mLoopEnergyCost));
	}
	
	if (version<8)
	{
		mMinAirVelocity=0.3f;
		mMaxAirVelocity=0.9f;

		mMinAirEnergyCost=0.005f;
		mMaxAirEnergyCost=0.015f;
	}

	if (version>8)
	{
		n=-1;
		do
		{
			n++;
			inFile.Read(&temp[n],sizeof(temp[n]));
		}
		while (temp[n]);

		mAugWeapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
			
		strcpy(mAugWeapon,temp);
	}

	if (version>9)
	{
		n=-1;
		do
		{
			n++;
			inFile.Read(&temp[n],sizeof(temp[n]));
		}
		while (temp[n]);

		mPrimaryWeapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
			
		strcpy(mPrimaryWeapon,temp);
	}

	if (version>10)
	{
		n=-1;
		do
		{
			n++;
			inFile.Read(&temp[n],sizeof(temp[n]));
		}
		while (temp[n]);

		mCockpit=new( MEMTYPE_BATTLEENGINE ) char[n+1];
			
		strcpy(mCockpit,temp);
	}
	else
	{
		mCockpit=new( MEMTYPE_BATTLEENGINE ) char[13];
		strcpy(mCockpit,"cockpit2.msh");
	}

	if (version>11)
		inFile.Read(&mLanguageName,sizeof(mLanguageName));
	else
		mLanguageName=1;
}

#ifdef RESBUILDER
void CBattleEngineData::AccumulateResources(
	CResourceAccumulator		*accumulator)
{
	char	**weaponName;
	char	iconName[256];

	for (weaponName=mWalkerWeapons.First(); weaponName; weaponName=mWalkerWeapons.Next())
	{
		CWeaponData	*weapon=UPhysicsManager::GetWeapon(*weaponName);

		strcpy(iconName,"hud\\");
		strcat(iconName,weapon->mIconName);
		strcat(iconName,".tga");
		
		CTEXTURE		*texture=CTEXTURE::GetTextureByName(iconName);

		accumulator->AddTexture(texture);
	}

	for (weaponName=mJetWeapons.First(); weaponName; weaponName=mJetWeapons.Next())
	{
		CWeaponData	*weapon=UPhysicsManager::GetWeapon(*weaponName);

		strcpy(iconName,"hud\\");
		strcat(iconName,weapon->mIconName);
		strcat(iconName,".tga");
		
		CTEXTURE		*texture=CTEXTURE::GetTextureByName(iconName);

		accumulator->AddTexture(texture);
	}

	// Add the Primary weapon
	CWeaponData	*weapon=UPhysicsManager::GetWeapon(mPrimaryWeapon);

	if( weapon )
	{
		strcpy(iconName,"hud\\");
		strcat(iconName,weapon->mIconName);
		strcat(iconName,".tga");
		
		CTEXTURE		*texture=CTEXTURE::GetTextureByName(iconName);
		
		accumulator->AddTexture(texture);
	}


	// Add the Aug weapon
	weapon=UPhysicsManager::GetWeapon(mAugWeapon);

	if( weapon )
	{
		strcpy(iconName,"hud\\");
		strcat(iconName,weapon->mIconName);
		strcat(iconName,".tga");
		
		CTEXTURE		*texture=CTEXTURE::GetTextureByName(iconName);

		accumulator->AddTexture(texture);
	}

	// Add the cockpit
	CMESH	*mesh=CMESH::GetMesh(mCockpit);

	accumulator->AddMesh(mesh,RES_OPTIONAL | RES_INHIBITPAGING);
}
#endif
#ifdef EDITORBUILD
void CBattleEngineData::Skip(
	CArchive		&ar,
	int				inVersion)
{
	BOOL			boolStore;
	SINT			intStore;
	float			floatStore;
	char			charStore;

	ar >> boolStore;

	ar >> floatStore >> floatStore;
	
	ar >> floatStore >> floatStore >> floatStore;

	ar >> floatStore >> floatStore;

	ar >> floatStore >> floatStore;

	ar >> floatStore >> floatStore >> floatStore;

	do
	{
		ar >> charStore;
	}
	while (charStore);

	do
	{
		ar >> charStore;
	}
	while (charStore);

	do
	{
		ar >> charStore;
	}
	while (charStore);

	do
	{
		ar >> charStore;
	}
	while (charStore);

	do
	{
		ar >> charStore;
	}
	while (charStore);

	if (inVersion>9)
	{
		do
		{
			ar >> charStore;
		}
		while (charStore);
	}

	if (inVersion>10)
	{
		ar >> floatStore >> floatStore >> floatStore >> floatStore;
	}

	if (inVersion>11)
	{
		ar >> intStore;
	}
}

void CBattleEngineData::Load(
	CArchive		&ar)
{
	char		temp[256];
	int			version;

	Shutdown();
	
	ar >> version;

	ar >> mLife >> mEnergy;
	
	ar >> mGroundEnergyIncrease >> mMaxAirEnergyCost >> mMinTransformEnergy;

	ar >> mMaxAirVelocity >> mGroundVelocity;
	ar >> mAirTurnRate >> mGroundTurnRate;
	
	if (version<8)
	{
		float	temp;
		
		ar >> temp >> temp >> temp;
	}

	int n,m;

	if (version<=4)
	{
		n=-1;
		do
		{
			n++;
			ar >> temp[n];
		}
		while (temp[n]);

		char	**weapon=new( MEMTYPE_BATTLEENGINE ) char*;
		*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
		for (m=0; m<=n; m++)
			(*weapon)[m]=temp[m];

		mJetWeapons.Add(weapon);

		n=-1;
		do
		{
			n++;
			ar >> temp[n];
		}
		while (temp[n]);

		weapon=new( MEMTYPE_BATTLEENGINE ) char*;
		*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
		for (m=0; m<=n; m++)
			(*weapon)[m]=temp[m];

		mJetWeapons.Add(weapon);

		n=-1;
		do
		{
			n++;
			ar >> temp[n];
		}
		while (temp[n]);

		weapon=new( MEMTYPE_BATTLEENGINE ) char*;
		*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
		for (m=0; m<=n; m++)
			(*weapon)[m]=temp[m];

		mWalkerWeapons.Add(weapon);

		n=-1;
		do
		{
			n++;
			ar >> temp[n];
		}
		while (temp[n]);

		weapon=new( MEMTYPE_BATTLEENGINE ) char*;
		*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
		for (m=0; m<=n; m++)
			(*weapon)[m]=temp[m];

		mWalkerWeapons.Add(weapon);

		n=-1;
		do
		{
			n++;
			ar >> temp[n];
		}
		while (temp[n]);

		weapon=new( MEMTYPE_BATTLEENGINE ) char*;
		*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
		for (m=0; m<=n; m++)
			(*weapon)[m]=temp[m];

		mWalkerWeapons.Add(weapon);
	}

	n=-1;
	do
	{
		n++;
		ar >> temp[n];
	}
	while (temp[n]);

	mConfigurationName=new( MEMTYPE_BATTLEENGINE ) char[n+1];
	for (m=0; m<=n; m++)
		mConfigurationName[m]=temp[m];

	if (version<=4)
	{
		float	temp;
		ar >> temp >> temp >> temp >> temp;
	}

	if (version>1)
		ar >> mShieldEfficiency;

	if (version>2)
		ar >> mStealth;

	if (version>3)
	{
		n=-1;
		do
		{
			n++;
			ar >> temp[n];
		}
		while (temp[n]);

		mExplosion=new( MEMTYPE_BATTLEENGINE ) char[n+1];
		for (m=0; m<=n; m++)
			mExplosion[m]=temp[m];
	}
	else
	{
		mExplosion=new( MEMTYPE_BATTLEENGINE ) char[29];
		strcpy(mExplosion,"Animated Explosion Emitter 2");
	}

	if (version>4)
	{
		// Load the walker weapons
		int		weapons;
		ar >> weapons;

		while (weapons)
		{
			n=-1;
			do
			{
				n++;
				ar >> temp[n];
			}
			while (temp[n]);

			char	**weapon=new( MEMTYPE_BATTLEENGINE ) char*;
			*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
			for (m=0; m<=n; m++)
				(*weapon)[m]=temp[m];

			mWalkerWeapons.Append(weapon);

			weapons--;
		}

		// Load the jet weapons
		ar >> weapons;

		while (weapons)
		{
			n=-1;
			do
			{
				n++;
				ar >> temp[n];
			}
			while (temp[n]);

			char	**weapon=new( MEMTYPE_BATTLEENGINE ) char*;
			*weapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
			for (m=0; m<=n; m++)
				(*weapon)[m]=temp[m];

			mJetWeapons.Append(weapon);

			weapons--;
		}

		// Load the ammo stores
		for (int n=0; n<kBattleEngineStores; n++)
			ar >> mStoreHeat[n] >> mStoreValue[n];
	}

	if (version>5)
		ar >> mMinAirVelocity;

	if (version>6)
	{
		ar >> mMaxWalkVelocity >> mWalkFriction;
	}

	if (version>7)
	{
		ar >> mMinAirEnergyCost;
		ar >> mRollEnergyCost;
		ar >> mLoopEnergyCost;
	}
	
	if (version<8)
	{
		mMinAirVelocity=0.3f;
		mMaxAirVelocity=0.9f;

		mMinAirEnergyCost=0.005f;
		mMaxAirEnergyCost=0.015f;
	}

	if (version>8)
	{
		n=-1;
		do
		{
			n++;
			ar >> temp[n];
		}
		while (temp[n]);

		mAugWeapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
			
		strcpy(mAugWeapon,temp);
	}

	if (version>9)
	{
		n=-1;
		do
		{
			n++;
			ar >> temp[n];
		}
		while (temp[n]);

		mPrimaryWeapon=new( MEMTYPE_BATTLEENGINE ) char[n+1];
			
		strcpy(mPrimaryWeapon,temp);
	}

	if (version>10)
	{
		n=-1;
		do
		{
			n++;
			ar >> temp[n];
		}
		while (temp[n]);

		mCockpit=new( MEMTYPE_BATTLEENGINE ) char[n+1];
			
		strcpy(mCockpit,temp);
	}
	else
	{
		mCockpit=new( MEMTYPE_BATTLEENGINE ) char[13];
		strcpy(mCockpit,"cockpit2.msh");
	}

	if (version>11)
		ar >> mLanguageName;
	else
		mLanguageName=1;
}

void CBattleEngineData::Save(
	CArchive		&ar)
{
	int		version=kCurrentBattleEngineDataFormat;

	ar << version;

	ar << mLife << mEnergy;
	
	ar << mGroundEnergyIncrease << mMaxAirEnergyCost << mMinTransformEnergy;

	ar << mMaxAirVelocity << mGroundVelocity;
	ar << mAirTurnRate << mGroundTurnRate;

	int n;

	n=-1;
	do
	{
		n++;
		ar << mConfigurationName[n];
	}
	while (mConfigurationName[n]);

	ar << mShieldEfficiency;

	ar << mStealth;

	n=-1;
	do
	{
		n++;
		ar << mExplosion[n];
	}
	while (mExplosion[n]);

	char	**weapon;
	int		size=mWalkerWeapons.Size();
	ar << size;

	for (weapon=mWalkerWeapons.First(); weapon; weapon=mWalkerWeapons.Next())
	{
		if (*weapon)
		{
			n=-1;
			do
			{
				n++;
				ar << (*weapon)[n];
			}
			while ((*weapon)[n]);
		}
	}

	size=mJetWeapons.Size();
	ar << size;

	for (weapon=mJetWeapons.First(); weapon; weapon=mJetWeapons.Next())
	{
		if (*weapon)
		{
			n=-1;
			do
			{
				n++;
				ar << (*weapon)[n];
			}
			while ((*weapon)[n]);
		}
	}

	// Save the ammo stores
	for (n=0; n<kBattleEngineStores; n++)
		ar << mStoreHeat[n] << mStoreValue[n];

	ar << mMinAirVelocity;

	ar << mMaxWalkVelocity << mWalkFriction;

	ar << mMinAirEnergyCost;
	ar << mRollEnergyCost;
	ar << mLoopEnergyCost;

	if (mAugWeapon)
	{
		n=-1;
		do
		{
			n++;
			ar << mAugWeapon[n];
		}
		while (mAugWeapon[n]);
	}
	else
	{
		char	value=0;
		ar << value;
	}

	if (mPrimaryWeapon)
	{
		n=-1;
		do
		{
			n++;
			ar << mPrimaryWeapon[n];
		}
		while (mPrimaryWeapon[n]);
	}
	else
	{
		char	value=0;
		ar << value;
	}

	if (mCockpit)
	{
		n=-1;
		do
		{
			n++;
			ar << mCockpit[n];
		}
		while (mCockpit[n]);
	}
	else
	{
		char	value=0;
		ar << value;
	}

	ar << mLanguageName;
}
#endif

// The file's dynamic initializers (after Configurations.obj's): the maths constants, then sData and its atexit destructor.
// STAND-IN (link layout): retail's 16 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_BattleEngineDataManager[16] = { 0 };
// FUNCTION: 0x0040f3f0 init:ZERO_FVECTOR
// FUNCTION: 0x0040f410 init:ID_FMATRIX
// FUNCTION: 0x0040f4e0 init:sData
// FUNCTION: 0x0040f500 exit:sData
