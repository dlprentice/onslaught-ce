// deviceobject.h: an object the shell tells when the Direct3D device is created, restored, lost or deleted
// (vtable 005e48c8, 5 slots; 8 bytes). Stuart's header is not in his release; the slots follow his calls.

#ifndef DEVICEOBJECT_H
#define DEVICEOBJECT_H

class DeviceObject
{
public:
	virtual			~DeviceObject();										// 0: leaves the shell's lists
	virtual HRESULT	InitDeviceObjects()			{ return S_OK; }			// 1
	virtual HRESULT	RestoreDeviceObjects()		{ return S_OK; }			// 2
	virtual HRESULT	InvalidateDeviceObjects()	{ return S_OK; }			// 3
	virtual HRESULT	DeleteDeviceObjects()		{ return S_OK; }			// 4

	DeviceObject	*mNext;													// +0x04
};

#endif
