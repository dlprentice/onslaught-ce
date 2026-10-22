// WarnOnce.h: a flag that lets a warning through once (the name is ours; its two members are out of line in
// DebugLog.obj). CDXFrontEndVideo::Render keeps one as a function-local static for a failed LockRect.

#ifndef WARNONCE_H
#define WARNONCE_H

class CWarnOnce
{
public:
	CWarnOnce();							// 00441e20: not warned yet
	~CWarnOnce() {}
	bool	Warn();							// 00441e30: whether it had warned before; it has now

	bool	mWarned;
};

#endif
