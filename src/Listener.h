// Listener.h: IListener, the root interface (vtable 005e4f2c: one pure slot).

#ifndef LISTENER_INCLUDE
#define LISTENER_INCLUDE

class CEvent;

class IListener
{
public:
	virtual void	HandleEvent(CEvent *event) = 0;
};

#endif
