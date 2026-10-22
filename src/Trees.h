// Trees.h: the landscape's trees (only what matched files need so far).

#ifndef TREES_H
#define TREES_H

#ifdef TREES_FULL	// DXTrees.cpp's view; game.cpp keeps the short one (its header-composition tie)
#include "deviceobject.h"
class CVBufTexture;

// The landscape's trees (vtable 005e59d8): a device object with two vertex buffers.
class CDXTrees : public DeviceObject
{
public:
	CDXTrees() {}
	virtual			~CDXTrees() {}
	// Retail table 005e59d8 slot 1 has its own zero-return identity (the non-inline fold 005019c0; base uses 00405930).
	virtual HRESULT	InitDeviceObjects();				// 1 (005019c0)
	virtual HRESULT	DeleteDeviceObjects();				// 4 (0055a3b0)
#else
class CDXTrees
{
public:
#endif
	void	Build();							// 0055a420
	void	Init();								// 0055a390
	void	Shutdown();							// 0055a400
	void	Render();							// 0055aa10 (CDXEngine::Render 0053e7c1)
#ifdef TREES_FULL
	void	HideTree(class CRTTree *rt);		// 0055ae40 (CRTTree's destructor 004de001)

	CVBufTexture	*mVBuf;						// +0x08
	CVBufTexture	*mUnknownVBuf;				// +0x0c: drawn only while the camera is well clear of the ground
	SINT			mNumTrees;					// +0x10: Render draws nothing below 1
#endif
};

extern CDXTrees TREES;							// 009cc148

#endif
