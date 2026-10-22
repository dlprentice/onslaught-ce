// BattleEngineDataManager.h
#ifndef BATTLEENGINEDATAMANAGER_H
#define BATTLEENGINEDATAMANAGER_H

#include	"SPtrSet.h"
#include	"MemBuffer.h"
#include	"resourceaccumulator.h"
#include	<string.h>

static const short		kCurrentBattleEngineDataFormat=12;
static const short		kBattleEngineStores=6;

class CBattleEngineData
{
	public:
		float			mMaxAirVelocity,mMinAirVelocity;
		float			mMaxAirEnergyCost,mMinAirEnergyCost;

		float			mGroundVelocity;
		float			mAirTurnRate,mGroundTurnRate;
		float			mLife,mEnergy,mShieldEfficiency;
		float			mGroundEnergyIncrease,mMinTransformEnergy;
		float			mWalkFriction,mMaxWalkVelocity;
		float			mRollEnergyCost,mLoopEnergyCost;

		SPtrSet<char*>	mWalkerWeapons;
		SPtrSet<char*>	mJetWeapons;

		char			*mPrimaryWeapon,*mAugWeapon;
		char			*mExplosion;
		char			*mCockpit;

		BOOL			mStoreHeat[kBattleEngineStores];
		float			mStoreValue[kBattleEngineStores];

		float			mStealth;

		SINT			mLanguageName;

		char			*mConfigurationName;

	public:
						CBattleEngineData();

		void			Initialise();
		void			Shutdown();

		void			Load(CMEMBUFFER &inFile);

		void			AccumulateResources(
							CResourceAccumulator	*accumulator);
#ifdef EDITORBUILD
		void			Skip(CArchive &ar,int inVersion);
		void			Load(CArchive &ar);
		void			Save(CArchive &ar);

		void			SetConfigurationName(
							CString		&inString)
						{
							int		n;

							if (mConfigurationName)
								delete mConfigurationName;

							mConfigurationName=new( MEMTYPE_BATTLEENGINE ) char[inString.GetLength()+1];

							for (n=0; n<inString.GetLength(); n++)
								mConfigurationName[n]=inString[n];

							mConfigurationName[n]=0;
						}

		void			AddJetWeapon(
							CString		&inString)
						{
							char	**weapon=new( MEMTYPE_BATTLEENGINE ) char*;
							*weapon=new( MEMTYPE_BATTLEENGINE ) char[inString.GetLength()+1];
							strcpy(*weapon,inString);
							mJetWeapons.Append(weapon);
						}

		void			RemoveJetWeapon(
							CString		&inString)
						{
							char	**weapon;

							for (weapon=mJetWeapons.First(); weapon; weapon=mJetWeapons.Next())
							{
								if (*weapon)
								{
									if (strcmp(*weapon,inString)==0)
									{
										mJetWeapons.Remove(weapon);
										delete [] *weapon;
										delete weapon;
										return;
									}
								}
							}
						}

		void			RenameJetWeapon(
							CString		&inOldString,
							CString		&inNewString)
						{
							char	**weapon;

							for (weapon=mJetWeapons.First(); weapon; weapon=mJetWeapons.Next())
							{
								if (*weapon)
								{
									if (strcmp(*weapon,inOldString)==0)
									{
										delete [] *weapon;

										*weapon=new( MEMTYPE_BATTLEENGINE ) char[inNewString.GetLength()+1];
										strcpy(*weapon,inNewString);

										return;
									}
								}
							}
						}

		void			AddMechWeapon(
							CString		&inString)
						{
							char	**weapon=new( MEMTYPE_BATTLEENGINE ) char*;
							*weapon=new( MEMTYPE_BATTLEENGINE ) char[inString.GetLength()+1];
							strcpy(*weapon,inString);
							mWalkerWeapons.Append(weapon);
						}

		void			RemoveMechWeapon(
							CString		&inString)
						{
							char	**weapon;

							for (weapon=mWalkerWeapons.First(); weapon; weapon=mWalkerWeapons.Next())
							{
								if (*weapon)
								{
									if (strcmp(*weapon,inString)==0)
									{
										mWalkerWeapons.Remove(weapon);
										delete [] *weapon;
										delete weapon;
										return;
									}
								}
							}
						}

		void			RenameWalkerWeapon(
							CString		&inOldString,
							CString		&inNewString)
						{
							char	**weapon;

							for (weapon=mWalkerWeapons.First(); weapon; weapon=mWalkerWeapons.Next())
							{
								if (*weapon)
								{
									if (strcmp(*weapon,inOldString)==0)
									{
										delete [] *weapon;

										*weapon=new( MEMTYPE_BATTLEENGINE ) char[inNewString.GetLength()+1];
										strcpy(*weapon,inNewString);

										return;
									}
								}
							}
						}

		void			SetExplosion(
							CString		&inString)
						{
							int		n;

							if (mExplosion)
								delete mExplosion;

							mExplosion=new( MEMTYPE_BATTLEENGINE ) char[inString.GetLength()+1];

							for (n=0; n<inString.GetLength(); n++)
								mExplosion[n]=inString[n];

							mExplosion[n]=0;
						}

		void			SetAugWeapon(
							CString		&inString)
						{
							int		n;

							if (mAugWeapon)
								delete mAugWeapon;

							mAugWeapon=new( MEMTYPE_BATTLEENGINE ) char[inString.GetLength()+1];

							for (n=0; n<inString.GetLength(); n++)
								mAugWeapon[n]=inString[n];

							mAugWeapon[n]=0;
						}

		void			SetPrimaryWeapon(
							CString		&inString)
						{
							int		n;

							if (mPrimaryWeapon)
								delete mPrimaryWeapon;

							mPrimaryWeapon=new( MEMTYPE_BATTLEENGINE ) char[inString.GetLength()+1];

							for (n=0; n<inString.GetLength(); n++)
								mPrimaryWeapon[n]=inString[n];

							mPrimaryWeapon[n]=0;
						}

		void			SetCockpit(
							CString		&inString)
						{
							int		n;

							if (mCockpit)
								delete mCockpit;

							mCockpit=new( MEMTYPE_BATTLEENGINE ) char[inString.GetLength()+1];

							for (n=0; n<inString.GetLength(); n++)
								mCockpit[n]=inString[n];

							mCockpit[n]=0;
						}
#endif
};

class UBattleEngineDataManager
{
	private:
		static SPtrSet<CBattleEngineData>	sData;

	public:
		static void					Shutdown()
									{
										while (CBattleEngineData *config=sData.First())
										{
											sData.Remove(config);
											config->Shutdown();
											delete config;
										}
									}

		static void					Initialise()
									{
										Shutdown();

										CBattleEngineData	*config=new( MEMTYPE_BATTLEENGINE ) CBattleEngineData();

										config->Initialise();

										sData.Add(config);
									}

		static int					CountConfigurations()			{ return sData.Size(); }

		static CBattleEngineData*	GetConfiguration(
										int		inIndex)
									{
										if (inIndex<0)
											inIndex=0;

										for (CBattleEngineData *data=sData.First(); data; data=sData.Next())
										{
											if (inIndex==0)
												return data;

											inIndex--;
										}

										return 0L;
									}

		static CBattleEngineData*	GetConfiguration(
										char	*inName)
									{
										for (CBattleEngineData *data=sData.First(); data; data=sData.Next())
										{
											if (strcmp(data->mConfigurationName,inName)==0)
												return data;
										}

										return 0L;
									}
		
		static void					Load(CMEMBUFFER	&inFile)
									{
										Shutdown();

										int			configurations;
										inFile.Read( &configurations, sizeof(configurations) );

										for (int n=0; n<configurations; n++)
										{
											CBattleEngineData	*config=new( MEMTYPE_BATTLEENGINE ) CBattleEngineData();

											config->Initialise();
											config->Load(inFile);

											sData.Add(config);
										}
									}

		static void					AccumulateResources(
										CResourceAccumulator *accumulator)
									{
										for (CBattleEngineData *data=sData.First(); data; data=sData.Next())
											data->AccumulateResources(accumulator);
									}

#ifdef EDITORBUILD
		static void					Skip(CArchive	&ar,
										 int		inVersion)
									{
										int		skip;
										ar >> skip;

										CBattleEngineData	data;
										for (int n=0; n<skip; n++)
											data.Skip(ar,inVersion);
									}
		
		static void					Load(CArchive	&ar)
									{
										Shutdown();

										int			configurations;
										ar >> configurations;

										for (int n=0; n<configurations; n++)
										{
											CBattleEngineData	*config=new( MEMTYPE_BATTLEENGINE ) CBattleEngineData();

											config->Initialise();
											config->Load(ar);

											sData.Add(config);
										}
									}

		static void					Save(CArchive	&ar)
									{
										ar << CountConfigurations();

										for (CBattleEngineData *data=sData.First(); data; data=sData.Next())
											data->Save(ar);
									}

		static bool					AddConfiguration(
										CString		&inString)
									{
										if (!ConfigurationExists(inString))
										{
											CBattleEngineData	*config=new( MEMTYPE_BATTLEENGINE ) CBattleEngineData();

											config->Initialise();
											config->SetConfigurationName(inString);

											sData.Add(config);

											return true;
										}

										return false;
									}

		static void					RemoveConfiguration(
										CString		&inString)
									{
										if (CountConfigurations()>1)
										{
											CBattleEngineData	*data=GetConfiguration(inString);

											if (data)
											{
												sData.Remove(data);
												delete data;
											}
										}
									}

		static void					RenameConfiguration(
										CString		&inRenameThis,
										CString		&toThis)
									{
										CBattleEngineData	*data=GetConfiguration(inRenameThis);

										data->SetConfigurationName(toThis);
									}

		static bool					ConfigurationExists(
										CString		&inString)
									{
										for (CBattleEngineData *data=sData.First(); data; data=sData.Next())
										{
											if (!strcmp(data->mConfigurationName,inString))
												return true;
										}

										return false;
									}

		static CBattleEngineData*	GetConfiguration(
										CString		&inString)
									{
										for (CBattleEngineData *data=sData.First(); data; data=sData.Next())
										{
											if (!strcmp(data->mConfigurationName,inString))
												return data;
										}

										return 0L;
									}
#endif
};

#endif