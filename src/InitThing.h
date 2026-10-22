#ifndef INIT_THING_H
#define INIT_THING_H

#ifdef EDITORBUILD
#include	"stdafx.h"
#endif
#include	"mathsconst.h"		// ZERO_FVECTOR, ID_FMATRIX (CInitThing's constructor 0048dcf0)
#include	"ThingType.h"
#include	"oids.h"
#include	"membuffer.h"
#include	"AnimalType.h"
#include	"WorldPhysicsManager.h"

class	CUnitData;
class	CCollisionSeekingThing;
class	CGeneralVolume;
class	CWeapon;
class	CSpawner;
class	CComponent;
class	CBuildingData;
class	CBattleEngineData;
class	CExplosionData;
class	CFeatureData;
class	CHazardData;
class	CArchive;
class   CInitCSThing;
class	CThing;
class	CGeneralVolume;
class	CUnit;
class	CRoundData;

enum EOrientationType
{
	EULER_ANGLES,
	DIRECTION_COSINE_MATRIX
};

enum EAllegiance
{
	kForsetiAllegiance=0,
	kMuspellAllegiance,
	kNeutralAllegiance,
	kUndefinedAllegiance,
	kInvalidAllegiance,
	kToggleAllegiance,
	kIndependentAllegiance,
};


enum ECollisionLevel
{
	ECL_OUTER_SPHERE = 0,
	ECL_APPROX_GEOMETRY_SHAPES = 1,
	ECL_MESH = 2 
};


enum ECollisionResponse
{
	ECR_PASSIVE = 0,		// object passes through
	ECR_STATIC = 1,			// object stops at point of impact (e.g. rounds)
	ECR_SLIDE =2,			// object slides along the collision normal
};

enum ECollisionType
{
	kCollideNothing=0,
	kCollideGround,
	kCollideWater,
	kCollideThing,
};

class CInitCSThing
{
public:

	void			Initialise()
					{
						mForThing = NULL; 
						mNotSeekCollisionWithBF = 0;  // we collide with everthing
						mShape = NULL ;			   // use outer sphere
						mDesiredCollision = ECL_APPROX_GEOMETRY_SHAPES ; //default is we do shape / shape collision	
						mMinCollision     = ECL_OUTER_SPHERE ; // minimum detail we can provide for other CST 
						mMaxCollision	   = ECL_MESH ; // maximum detail we can provide for other CST 
						mCollisionResponse	= ECR_SLIDE;
						mStartCollideOnNextFrame = TRUE ;
						mTimeBeforeStart=-1; // force next frame // btw -1 means NEXT_FRAME to event_manager
						mIgnoreThing = NULL ;
						mDoOBBForMeshCol = FALSE ;  // force OBB/ for mesh collision
						mUseFixedMeshTransforms = FALSE ;  // i.e. use on meshs that don't internally move (e.g. a building )
						mIgnoreAnimationCollision = FALSE ;
						mRoundLength = 0.0f;
					}

	CInitCSThing() { Initialise(); }

	CThing* mForThing ;				  // do collision for which object
	CThing* mIgnoreThing;			  // ignore collision with this (ONLY FOR RAYS CURRENTLY)
	ULONG mNotSeekCollisionWithBF ;   // things we don't want to collide with
	CGeneralVolume* mShape ;		  // defines the approx shape of the thing (if null uses outer sphere)
	ECollisionLevel	mDesiredCollision ;
	ECollisionLevel	mMinCollision ;
	ECollisionLevel mMaxCollision ;
	ECollisionResponse mCollisionResponse;
	BOOL			   mStartCollideOnNextFrame ;
	BOOL			   mUseFixedMeshTransforms;
	BOOL			   mDoOBBForMeshCol;
	float			   mTimeBeforeStart;
	BOOL			   mIgnoreAnimationCollision;
	float			   mRoundLength ;// hack only used for CS rounds
};

class CInitThing 
{
public:
	FVector						mPos ;
	FMatrix						mOrientation;
	float						mYaw,mPitch,mRoll;
	FVector						mVelocity ;
	EOrientationType			mOrientationType;
	SINT						mMeshNo;
	CInitCSThing				mInitCST ;
	
	EAllegiance					mAllegiance;
	SINT						mTarget;
	
	float						mForceRadius;

	char						mScript[256],mName[256],mSpawnScript[256];

	BOOL						mActive,mAttachScriptsToUnits;

	CUnit						*mSpawnedBy;
	EEmitterType				mWaypointPath;

								CInitThing();

#ifdef EDITORBUILD
	virtual void				Save(
									CArchive	&ar);
	virtual void				Load(
									CArchive	&ar,
									short		inVersion);
#endif
	virtual void				Copy(
									CInitThing	*inThing)
								{
									mPos=inThing->mPos;
									mOrientationType=inThing->mOrientationType;
									mYaw=inThing->mYaw;
									mPitch=inThing->mPitch;
									mOrientation=inThing->mOrientation;
									mRoll=inThing->mRoll;
									mMeshNo=inThing->mMeshNo;
									mAllegiance=inThing->mAllegiance;
									mTarget=inThing->mTarget;
									
									int	n;

									for (n=0; n<256; n++)
									{
										mScript[n]=inThing->mScript[n];
										mName[n]=inThing->mName[n];
										mSpawnScript[n]=inThing->mSpawnScript[n];
									}

									mActive=inThing->mActive;
									mAttachScriptsToUnits=inThing->mAttachScriptsToUnits;
								}

	virtual void				Load(
									short			inVersion,
									CMEMBUFFER		&inFile)
								{
									if (inVersion<=16)
									{
										void* fred;
										ULONG arse ;

										inFile.Read(&mPos.X,sizeof(mPos.X));
										inFile.Read(&mPos.Y,sizeof(mPos.Y));
										inFile.Read(&mPos.Z,sizeof(mPos.Z));

										inFile.Read(&mOrientation,sizeof(mOrientation));
										inFile.Read(&mYaw,sizeof(mYaw));
										inFile.Read(&mPitch,sizeof(mPitch));
										inFile.Read(&mRoll,sizeof(mRoll));
										inFile.Read(&mVelocity,sizeof(mVelocity));
										inFile.Read(&arse,sizeof(arse));
										inFile.Read(&mOrientationType,sizeof(mOrientationType));
										inFile.Read(&fred,sizeof(fred));
										inFile.Read(&fred,sizeof(fred));
										inFile.Read(&mMeshNo,sizeof(mMeshNo));
										inFile.Read(&mAllegiance,sizeof(mAllegiance));
										inFile.Read(&mTarget,sizeof(mTarget));
									}
									else if (inVersion<=19)
									{
										inFile.Read(&mPos.X,sizeof(mPos.X));
										inFile.Read(&mPos.Y,sizeof(mPos.Y));
										inFile.Read(&mPos.Z,sizeof(mPos.Z));
										inFile.Read(&mYaw,sizeof(mYaw));
										inFile.Read(&mPitch,sizeof(mPitch));
										inFile.Read(&mRoll,sizeof(mRoll));
										inFile.Read(&mMeshNo,sizeof(mMeshNo));
										inFile.Read(&mAllegiance,sizeof(mAllegiance));
										inFile.Read(&mTarget,sizeof(mTarget));
										
										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mScript[n],sizeof(mScript[n]));
										}
										while (mScript[n]);
									}
									else if (inVersion<=27)
									{
										inFile.Read(&mPos.X,sizeof(mPos.X));
										inFile.Read(&mPos.Y,sizeof(mPos.Y));
										inFile.Read(&mPos.Z,sizeof(mPos.Z));
										inFile.Read(&mYaw,sizeof(mYaw));
										inFile.Read(&mPitch,sizeof(mPitch));
										inFile.Read(&mRoll,sizeof(mRoll));
										inFile.Read(&mMeshNo,sizeof(mMeshNo));
										inFile.Read(&mAllegiance,sizeof(mAllegiance));
										inFile.Read(&mTarget,sizeof(mTarget));
										
										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mScript[n],sizeof(mScript[n]));
										}
										while (mScript[n]);

										n=char(-1);
										do
										{
											n++;
											inFile.Read(&mName[n],sizeof(mName[n]));
										}
										while (mName[n]);
									}
									else if (inVersion<=33)
									{
										inFile.Read(&mPos.X,sizeof(mPos.X));
										inFile.Read(&mPos.Y,sizeof(mPos.Y));
										inFile.Read(&mPos.Z,sizeof(mPos.Z));
										inFile.Read(&mYaw,sizeof(mYaw));
										inFile.Read(&mPitch,sizeof(mPitch));
										inFile.Read(&mRoll,sizeof(mRoll));
										inFile.Read(&mMeshNo,sizeof(mMeshNo));
										inFile.Read(&mAllegiance,sizeof(mAllegiance));
										inFile.Read(&mTarget,sizeof(mTarget));
										
										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mScript[n],sizeof(mScript[n]));
										}
										while (mScript[n]);

										n=char(-1);
										do
										{
											n++;
											inFile.Read(&mName[n],sizeof(mName[n]));
										}
										while (mName[n]);

										n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnScript[n],sizeof(mSpawnScript[n]));
										}
										while (mSpawnScript[n]);
									}
									else if (inVersion<=45)
									{
										inFile.Read(&mPos.X,sizeof(mPos.X));
										inFile.Read(&mPos.Y,sizeof(mPos.Y));
										inFile.Read(&mPos.Z,sizeof(mPos.Z));
										inFile.Read(&mYaw,sizeof(mYaw));
										inFile.Read(&mPitch,sizeof(mPitch));
										inFile.Read(&mRoll,sizeof(mRoll));
										inFile.Read(&mMeshNo,sizeof(mMeshNo));
										inFile.Read(&mAllegiance,sizeof(mAllegiance));
										inFile.Read(&mTarget,sizeof(mTarget));
										
										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mScript[n],sizeof(mScript[n]));
										}
										while (mScript[n]);

										n=char(-1);
										do
										{
											n++;
											inFile.Read(&mName[n],sizeof(mName[n]));
										}
										while (mName[n]);

										n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnScript[n],sizeof(mSpawnScript[n]));
										}
										while (mSpawnScript[n]);

										inFile.Read(&mActive,sizeof(mActive));
									}
									else
									{
										inFile.Read(&mPos.X,sizeof(mPos.X));
										inFile.Read(&mPos.Y,sizeof(mPos.Y));
										inFile.Read(&mPos.Z,sizeof(mPos.Z));
										inFile.Read(&mYaw,sizeof(mYaw));
										inFile.Read(&mPitch,sizeof(mPitch));
										inFile.Read(&mRoll,sizeof(mRoll));
										inFile.Read(&mMeshNo,sizeof(mMeshNo));
										inFile.Read(&mAllegiance,sizeof(mAllegiance));
										inFile.Read(&mTarget,sizeof(mTarget));
										
										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mScript[n],sizeof(mScript[n]));
										}
										while (mScript[n]);

										n=char(-1);
										do
										{
											n++;
											inFile.Read(&mName[n],sizeof(mName[n]));
										}
										while (mName[n]);

										n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnScript[n],sizeof(mSpawnScript[n]));
										}
										while (mSpawnScript[n]);

										inFile.Read(&mActive,sizeof(mActive));
										inFile.Read(&mAttachScriptsToUnits,sizeof(mAttachScriptsToUnits));
									}
								}
};

class CTreeInitThing : public CInitThing
{
public:
	BOOL						mTreeTypeComplete;
	char						mTreeType[256];

								CTreeInitThing()
								{
									mTreeTypeComplete=true;
									strcpy(mTreeType,"DefaultTree0");
								}

#ifdef EDITORBUILD
	void						Save(
									CArchive	&ar);
	void						Load(
									CArchive	&ar,
									short		inVersion);
#endif
	virtual void				Copy(
									CInitThing	*inThing)
								{
									CInitThing::Copy(inThing);

									mTreeTypeComplete=((CTreeInitThing*)inThing)->mTreeTypeComplete;

									int n;
									for (n=0; n<256; n++)
										mTreeType[n]=((CTreeInitThing*)inThing)->mTreeType[n];
								}

	virtual void				Load(
									short			inVersion,
									CMEMBUFFER		&inFile)
								{
									CInitThing::Load(inVersion,inFile);

									if (inVersion>17)
									{
										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mTreeType[n],sizeof(mTreeType[n]));
										}
										while (mTreeType[n]);
									}
								}
};

class CSpawnerInitThing : public CInitThing
{
public:
	SINT						mAmount,mSquadSize;
	float						mDelay,mInitialDelay,mSquadDelay;
	char						mSpawnUnit[256],mSpawnerSpawnScript[256];

	CSpawnerInitThing() :
		mAmount(-1),
		mSquadSize(1),
		mDelay(0),
		mInitialDelay(0),
		mSquadDelay(0)		
	{
		mSpawnUnit[0]=0;
		mSpawnerSpawnScript[0]=0;
	}

#ifdef EDITORBUILD
	void						Save(
									CArchive	&ar);
	void						Load(
									CArchive	&ar,
									short		inVersion);
#endif
	virtual void				Copy(
									CInitThing	*inThing)
								{
									CInitThing::Copy(inThing);

									mAmount=((CSpawnerInitThing*)inThing)->mAmount;
									mSquadSize=((CSpawnerInitThing*)inThing)->mSquadSize;
									mDelay=((CSpawnerInitThing*)inThing)->mDelay;
									mSquadDelay=((CSpawnerInitThing*)inThing)->mSquadDelay;
									mInitialDelay=((CSpawnerInitThing*)inThing)->mInitialDelay;
									
									int n;
									for (n=0; n<256; n++)
									{
										mSpawnUnit[n]=((CSpawnerInitThing*)inThing)->mSpawnUnit[n];
										mSpawnerSpawnScript[n]=((CSpawnerInitThing*)inThing)->mSpawnerSpawnScript[n];
									}
								}

	void						Load(
									short		inVersion,
									CMEMBUFFER	&inFile)
								{
									CInitThing::Load(inVersion,inFile);

									if (inVersion<=16)
									{
										inFile.Read(&mAmount,sizeof(mAmount));
										inFile.Read(&mDelay,sizeof(mDelay));
										inFile.Read(&mInitialDelay,sizeof(mInitialDelay));

										for (int n=0; n<256; n++)
											inFile.Read(&mSpawnUnit[n],sizeof(mScript[n]));
									}
									else if (inVersion<=20)
									{
										inFile.Read(&mAmount,sizeof(mAmount));
										inFile.Read(&mDelay,sizeof(mDelay));
										inFile.Read(&mInitialDelay,sizeof(mInitialDelay));

										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnUnit[n],sizeof(mScript[n]));
										}
										while (mSpawnUnit[n]);
									}
									else if (inVersion<=21)
									{
										inFile.Read(&mAmount,sizeof(mAmount));
										inFile.Read(&mDelay,sizeof(mDelay));
										inFile.Read(&mInitialDelay,sizeof(mInitialDelay));
										inFile.Read(&mSquadSize,sizeof(mSquadSize));

										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnUnit[n],sizeof(mScript[n]));
										}
										while (mSpawnUnit[n]);
									}
									else if (inVersion<=22)
									{
										inFile.Read(&mAmount,sizeof(mAmount));
										inFile.Read(&mDelay,sizeof(mDelay));
										inFile.Read(&mInitialDelay,sizeof(mInitialDelay));
										inFile.Read(&mSquadSize,sizeof(mSquadSize));
										inFile.Read(&mActive,sizeof(mActive));

										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnUnit[n],sizeof(mScript[n]));
										}
										while (mSpawnUnit[n]);
									}
									else if (inVersion<=24)
									{
										inFile.Read(&mAmount,sizeof(mAmount));
										inFile.Read(&mDelay,sizeof(mDelay));
										inFile.Read(&mInitialDelay,sizeof(mInitialDelay));
										inFile.Read(&mSquadSize,sizeof(mSquadSize));
										inFile.Read(&mActive,sizeof(mActive));

										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnUnit[n],sizeof(mSpawnUnit[n]));
										}
										while (mSpawnUnit[n]);

										n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnScript[n],sizeof(mSpawnScript[n]));
										}
										while (mSpawnScript[n]);
									}
									else if (inVersion<=27)
									{
										inFile.Read(&mAmount,sizeof(mAmount));
										inFile.Read(&mDelay,sizeof(mDelay));
										inFile.Read(&mSquadDelay,sizeof(mSquadDelay));
										inFile.Read(&mInitialDelay,sizeof(mInitialDelay));
										inFile.Read(&mSquadSize,sizeof(mSquadSize));
										inFile.Read(&mActive,sizeof(mActive));

										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnUnit[n],sizeof(mSpawnUnit[n]));
										}
										while (mSpawnUnit[n]);

										n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnScript[n],sizeof(mSpawnScript[n]));
										}
										while (mSpawnScript[n]);
									}
									else if (inVersion<=33)
									{
										inFile.Read(&mAmount,sizeof(mAmount));
										inFile.Read(&mDelay,sizeof(mDelay));
										inFile.Read(&mSquadDelay,sizeof(mSquadDelay));
										inFile.Read(&mInitialDelay,sizeof(mInitialDelay));
										inFile.Read(&mSquadSize,sizeof(mSquadSize));
										inFile.Read(&mActive,sizeof(mActive));

										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnUnit[n],sizeof(mSpawnUnit[n]));
										}
										while (mSpawnUnit[n]);
									}
									else if (inVersion<=43)
									{
										inFile.Read(&mAmount,sizeof(mAmount));
										inFile.Read(&mDelay,sizeof(mDelay));
										inFile.Read(&mSquadDelay,sizeof(mSquadDelay));
										inFile.Read(&mInitialDelay,sizeof(mInitialDelay));
										inFile.Read(&mSquadSize,sizeof(mSquadSize));

										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnUnit[n],sizeof(mSpawnUnit[n]));
										}
										while (mSpawnUnit[n]);
									}
									else
									{
										inFile.Read(&mAmount,sizeof(mAmount));
										inFile.Read(&mDelay,sizeof(mDelay));
										inFile.Read(&mSquadDelay,sizeof(mSquadDelay));
										inFile.Read(&mInitialDelay,sizeof(mInitialDelay));
										inFile.Read(&mSquadSize,sizeof(mSquadSize));

										char n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnUnit[n],sizeof(mSpawnUnit[n]));
										}
										while (mSpawnUnit[n]);

										n=char(-1);
										do
										{
											n++;
											inFile.Read(&mSpawnerSpawnScript[n],sizeof(mSpawnerSpawnScript[n]));
										}
										while (mSpawnerSpawnScript[n]);
									}
								}
};

class CSquadInitThing : public CInitThing
{
public:
	SINT						mAmount;
	char						mUnitName[256];
	int							mMode;

								CSquadInitThing()
								{
									mAmount=0;
									mUnitName[0]=0;
									mMode=0;
								}

#ifdef EDITORBUILD
	void						Save(
									CArchive	&ar);
	void						Load(
									CArchive	&ar,
									short		inVersion);
#endif

	virtual void				Copy(
									CInitThing	*inThing)
								{
									CInitThing::Copy(inThing);
								}

	virtual void				Copy(
									CSquadInitThing	*inThing)
								{
									CInitThing::Copy(inThing);

									mAmount=((CSquadInitThing*)inThing)->mAmount;

									int n;
									for (n=0; n<256; n++)
										mUnitName[n]=((CSquadInitThing*)inThing)->mUnitName[n];
								
									mMode=((CSquadInitThing*)inThing)->mMode;
								}

	void						Load(
									short		inVersion,
									CMEMBUFFER	&inFile)
								{
									CInitThing::Load(inVersion,inFile);

									inFile.Read(&mAmount,sizeof(mAmount));
								
									if (inVersion>28)
										inFile.Read(&mMode,sizeof(mMode));
								}
};

class CWallInitThing : public CInitThing
{
public:
	float						mLength;
	char						mWallType[256];
	float						mLife;

								CWallInitThing()
								{
									mLength=0;
									mLife=50.0f;
									strcpy(mWallType,"wall1");
								}

#ifdef EDITORBUILD
	void						Save(
									CArchive	&ar);
	void						Load(
									CArchive	&ar,
									short		inVersion);
#endif
	virtual void				Copy(
									CInitThing	*inThing)
								{
									CInitThing::Copy(inThing);

									mLength=((CWallInitThing*)inThing)->mLength;
									mLife=((CWallInitThing*)inThing)->mLife;
									strcpy(mWallType,((CWallInitThing*)inThing)->mWallType);
								}

	void						Load(
									short		inVersion,
									CMEMBUFFER	&inFile)
								{
									CInitThing::Load(inVersion,inFile);

									inFile.Read(&mLength,sizeof(mLength));

									if (inVersion>=31)
									{
										inFile.Read(&mLife,sizeof(mLife));

										int n=-1;

										do
										{
											n++;
											inFile.Read(&mWallType[n],sizeof(mWallType[n]));
										}
										while (mWallType[n]);
									}
								}
};

class CCutsceneInitThing : public CInitThing
{
	public:
		char					mFile[256],mLinkTo[256];

								CCutsceneInitThing()
								{
									mFile[0]=0;
									mLinkTo[0]=0;
								}

#ifdef EDITORBUILD
		void					Save(
									CArchive	&ar);
		void					Load(
									CArchive	&ar,
									short		inVersion);
#endif
		virtual void			Copy(
									CInitThing	*inThing)
								{
									CInitThing::Copy(inThing);

									int n;
									for (n=0; n<256; n++)
									{
										mFile[n]=((CCutsceneInitThing*)inThing)->mFile[n];
										mLinkTo[n]=((CCutsceneInitThing*)inThing)->mLinkTo[n];
									}
								}

		void					Load(
									short		inVersion,
									CMEMBUFFER	&inFile)
								{
									CInitThing::Load(inVersion,inFile);

									char n=char(-1);
									do
									{
										n++;
										inFile.Read(&mFile[n],sizeof(mFile[n]));
									}
									while (mFile[n]);

									if (inVersion>=32)
									{
										n=char(-1);
										do
										{
											n++;
											inFile.Read(&mLinkTo[n],sizeof(mLinkTo[n]));
										}
										while (mLinkTo[n]);
									}
								}
};

class CStartInitThing : public CInitThing
{
	public:
		BOOL				mPlaneMode;
		UINT				mPlayerNumber;

							CStartInitThing()
							{
								mPlaneMode=FALSE;
								mPlayerNumber=1;
							}

#ifdef EDITORBUILD
	void					Save(
								CArchive	&ar);
	void					Load(
								CArchive	&ar,
								short		inVersion);
#endif
	virtual void			Copy(
								CInitThing	*inThing)
							{
								CInitThing::Copy(inThing);

								mPlaneMode=((CStartInitThing*)inThing)->mPlaneMode;
								mPlayerNumber=((CStartInitThing*)inThing)->mPlayerNumber;
							}

	void					Load(
								short		inVersion,
								CMEMBUFFER	&inFile)
							{
								CInitThing::Load(inVersion,inFile);

								if (inVersion>14)
									inFile.Read(&mPlaneMode,sizeof(mPlaneMode));
								
								if (inVersion>25)
									inFile.Read(&mPlayerNumber,sizeof(mPlayerNumber));
							}
};

class CSphereTriggerInitThing : public CInitThing
{
	public:
		float				mRadius;

							CSphereTriggerInitThing()
							{
								mRadius=0;
							}

#ifdef EDITORBUILD
	void					Save(
								CArchive	&ar);
	void					Load(
								CArchive	&ar,
								short		inVersion);
#endif
	virtual void			Copy(
								CInitThing	*inThing)
							{
								CInitThing::Copy(inThing);

								mRadius=((CSphereTriggerInitThing*)inThing)->mRadius;
							}

	void					Load(
								short		inVersion,
								CMEMBUFFER	&inFile)
							{
								CInitThing::Load(inVersion,inFile);

								inFile.Read(&mRadius,sizeof(mRadius));
							}
};

class CUnitInitThing : public CInitThing
{
public:
							CUnitInitThing()
							{
								mStats=NULL;
							}

	virtual void			Copy(
								CInitThing	*inThing)
							{
								CInitThing::Copy(inThing);

								mStats=((CUnitInitThing*)inThing)->mStats;
							}

	CUnitData				*mStats;
};

class CRoundInitThing : public CInitThing
{
	public:
		FVector				mDest;
		SINT				mJumpsPerformed;
		CRoundData			*mRoundData;
		float				mInitialDelay,mLifeSpan;

							CRoundInitThing()
							{
								mDest=FVector(-1,-1,-1);
								mJumpsPerformed=0;
								mRoundData=NULL;
								mInitialDelay=0.0f;
								mLifeSpan=0.0f;
							}
};

class CBattleEngineInitThing : public CUnitInitThing
{
public:
						CBattleEngineInitThing()
						{
							mConfigurationId=0;
							mPlaneMode=FALSE;
						}

	SINT				mConfigurationId;
	BOOL				mPlaneMode;
};

class CExplosionInitThing : public CInitThing
{
	public:
						CExplosionInitThing()
						{
							mBehaviour=NULL;
							mColType=kCollideNothing;
							mAttachedTo=NULL;
							mUseAttachedRadius=FALSE;
							mAllowVolumetric=FALSE;
							mOriginator = NULL ;
							mImportant=FALSE;
						}

	CExplosionData		*mBehaviour;
	ECollisionType		mColType;
	CThing				*mAttachedTo;
	BOOL				mUseAttachedRadius,mAllowVolumetric,mImportant;
	CThing*				mOriginator;		
};

class CAnimalInitThing : public CInitThing
{
public:
	
	CAnimalInitThing() { mType=(AnimalType) 0; }
	AnimalType		mType;
};

class CFeatureInitThing : public CInitThing
{
	public:
		CFeatureData	*mData;

						CFeatureInitThing()
						{
							mData=NULL;
						}
};

class CHazardInitThing : public CInitThing
{
	public:
		CHazardData		*mData;

						CHazardInitThing()
						{
							mData=NULL;
						}
};


CInitThing	*SpawnInitThing(SINT inID, BOOL inReportErrors);	// InitThing.cpp (0048c650)

// Retail addresses of the inline members above (their out-of-line copies are COMDATs: CInitThing's in
// BattleEngine.obj, CStartInitThing's in game.obj, the rest in InitThing.obj after SpawnInitThing):
// FUNCTION: 0x0040e1b0 ?Copy@CInitThing@@UAEXPAV1@@Z
// FUNCTION: 0x0040e280 ?Load@CInitThing@@UAEXFAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0046d270 ?Copy@CStartInitThing@@UAEXPAVCInitThing@@@Z
// FUNCTION: 0x0046d350 ?Load@CStartInitThing@@UAEXFAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0048cba0 ?Copy@CTreeInitThing@@UAEXPAVCInitThing@@@Z
// FUNCTION: 0x0048cc90 ?Load@CTreeInitThing@@UAEXFAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0048ccd0 ?Copy@CSpawnerInitThing@@UAEXPAVCInitThing@@@Z
// FUNCTION: 0x0048ce00 ?Load@CSpawnerInitThing@@UAEXFAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0048d7d0 ?Copy@CSquadInitThing@@UAEXPAV1@@Z
// FUNCTION: 0x0048d8d0 ?Load@CSquadInitThing@@UAEXFAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0048d910 ?Copy@CWallInitThing@@UAEXPAVCInitThing@@@Z
// FUNCTION: 0x0048da20 ?Load@CWallInitThing@@UAEXFAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0048da80 ?Copy@CCutsceneInitThing@@UAEXPAVCInitThing@@@Z
// FUNCTION: 0x0048db80 ?Load@CCutsceneInitThing@@UAEXFAAVCDXMemBuffer@@@Z
// FUNCTION: 0x0048dcc0 ?Load@CSphereTriggerInitThing@@UAEXFAAVCDXMemBuffer@@@Z

#endif