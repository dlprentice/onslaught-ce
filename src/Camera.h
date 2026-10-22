// Camera.h: interface for the CCamera class.
//
//////////////////////////////////////////////////////////////////////

#ifndef CAMERA_INCLUDE
#define CAMERA_INCLUDE

#include "fcoords.h"
#include "activereader.h"
#include "Controller.h"

class CThingCamera;
class CBSpline;
class CBattleEngine;


class CThing ;

class CCamera 
{
public:
	virtual FVector GetPos() =0;
	virtual FMatrix GetOrientation() =0;
	virtual FVector GetOldPos() {return GetPos();};
	virtual FMatrix GetOldOrientation() {return GetOrientation();};
	virtual const float   GetZoom() = 0 ;
	virtual const float   GetOldZoom() {return GetZoom();};
	virtual const float	  GetAspectRatio();
	virtual bool GetShowHUD() = 0;
	virtual ~CCamera() {} 

	virtual	void PrepareForInterpolation() {};
	
	float	GetYaw();
};




class CThingCamera : public CCamera
{
public:
	CThingCamera(CThing* for_thing) : mForThing(for_thing) {}
	virtual FVector GetPos() ;
	virtual FMatrix GetOrientation() ;
	virtual FVector GetOldPos();
	virtual FMatrix GetOldOrientation();
	virtual const float	   GetZoom() ;
	virtual const float	   GetOldZoom() ;
	virtual bool GetShowHUD();
	CThing*	GetThing() { return mForThing.ToRead() ; }

private:
	CActiveReader<CThing> mForThing ;
};



class CThing3rdPersonCamera : public CCamera
{
public:
	CThing3rdPersonCamera(CThing* for_thing) ;
	~CThing3rdPersonCamera() ;
	virtual FVector GetPos() ;
	virtual FMatrix GetOrientation() ;
	virtual FVector GetOldPos();
	virtual FMatrix GetOldOrientation();
	virtual CThing3rdPersonCamera* GetIsThingCamera() { return this ; }
	virtual const float	   GetZoom() ;
	virtual const float	   GetOldZoom() ;
	virtual bool GetShowHUD();
	CThing*	GetThing() { return(CThing*)  mForThing.ToRead() ; }

private:
	CActiveReader<CThing> mForThing ;
	CBSpline*			  mCurve;
};



class CViewPointCamera : public CCamera
{
public:
	CViewPointCamera(const FVector& point,
				     const float& rotate_speed = 0.0f,
				     const float& start_distance = 4.0f,
	                 const float& end_distance = 4.0f,
				     const float& time_between_distance = 1.0f) ;
	virtual FVector GetPos() ;
	virtual FMatrix GetOrientation() ;
	virtual const float	   GetZoom() ;
	virtual bool GetShowHUD();
	virtual FVector GetOldPos();
	virtual FMatrix GetOldOrientation();
private:
	FVector mLookAtPoint;
	float   mRotateYawSpeed;
	float   mStartDistance;
	float   mEndDistance;
	float   mTimeBetweenDistances;
	float   mStartTime;

	// led

	float mLastCalcPosTime;
	FVector mLastCalcPos;
	float mLastCalcOriTime;
	FMatrix mLastCalcOri ;

	FMatrix	mOldOrientation;
	FVector	mOldPos;
};

enum ECameraEvent
{
	UPDATE_CAMERA= 2000
};

class CPanCamera: public CCamera, public CMonitor
{
public:
	CPanCamera(CThing* for_thing, CBSpline* curve, float length) ;
	~CPanCamera();
	virtual FVector GetPos() ;
	virtual FMatrix GetOrientation() ;
	virtual const float	   GetZoom() { return 1.0f;}
	virtual bool GetShowHUD() { return FALSE ; }
	virtual FVector GetOldPos();
	virtual FMatrix GetOldOrientation();
	virtual void HandleEvent(CEvent* event);
private:
	void	Update();
	CActiveReader<CThing> mForThing ;
	CBSpline*			  mCurve;
	float				  mStartTime;
	float				  mLength;
	FVector				  mPos ;
	FVector 			  mOldPos ;
	FMatrix				  mOrientation ;
	FMatrix				  mOldOrientation ;
};


class CMovieCamera : public CCamera
{
public:
	CMovieCamera(CThing* for_thing);
	virtual FVector GetPos() ;
	virtual FMatrix GetOrientation() ;
	virtual const float	   GetZoom() ;
	virtual const float	   GetOldZoom() ;
	virtual bool GetShowHUD();
	virtual FVector GetOldPos();
	virtual FMatrix GetOldOrientation();
	int GetCameraNumber();
	
private:
	CActiveReader<CThing> mForThing ;

	float mLastCalcPosTime;
	FVector mLastCalcPos;
	float mLastCalcOriTime;
	FMatrix mLastCalcOri ;
	float mLastCalcZoomTime;
	float mLastCalcZoom;

	FMatrix	mOldOrientation;
	FVector	mOldPos;
	float	mOldZoom;
};

class CControllableCamera : public CCamera, public IController
{
public:
	CControllableCamera(FVector pos, FMatrix orientation) ;
	virtual FVector GetPos() { return mPos ; }
	virtual FMatrix GetOrientation() { return mOrientation ; }
	virtual const float	   GetZoom()  { return 1.0f ; };
	virtual bool GetShowHUD() { return false ;}
	virtual BOOL CanBeControlledWhenInPause() { return TRUE ; }
	virtual EControlType  GetControlType() { return CONTROL_CAMERA ; }
	virtual void ReceiveButtonAction(CController* from_controller, int button, float val) ;

	virtual FVector GetOldPos() {return mOldPos;};
	virtual FMatrix GetOldOrientation() {return mOldOrientation;};

	virtual	void PrepareForInterpolation();

	static	void UpdateFrameCount() {mFrameCount ++;};

private:
	FMatrix	mOrientation ;
	FVector mPos ;

	FVector mOldPos;
	FMatrix	mOldOrientation;

	FVector	mTempPos;
	FMatrix mTempOrientation;

	static	SINT	mFrameCount;

	SINT	mLastFrameCount;
};

// JCL
class CGenericCamera : public CCamera
{
public:
	CGenericCamera() {};
	virtual FVector GetPos() { return mPos ; }
	virtual FMatrix GetOrientation() { return mOrientation ; }
	virtual const float	   GetZoom()  { return mZoom; };
	virtual bool GetShowHUD() { return false ;}

	FMatrix	mOrientation;
	FVector mPos;
	float	mZoom;
};

// JCL
class CInterpolatedCamera : public CCamera
{
public:
	CInterpolatedCamera(CCamera *cam);
	virtual FVector GetPos() ;				// inline, Camera.cpp (after GetAspectRatio)
	virtual FMatrix GetOrientation() { return mOrientation ; }
	virtual const float	   GetZoom()  { return mZoom; };
	virtual bool GetShowHUD() { return false ;}
			void Mirror(float aroundz);	

	FMatrix	mOrientation;
	FVector mPos;
	float	mZoom;
};

#endif 



