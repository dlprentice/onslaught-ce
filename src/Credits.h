// Credits.h: the scrolling credits (Stuart's header name; CGame::RollCredits and CFEPCredits draw them).

#ifndef CREDITS_H
#define CREDITS_H

// The scrolling credits (CREDITS, 00897a88): RenderCredits (0051a030) draws them at a time and alpha and returns
// FALSE once they have finished.
class CCredits
{
public:
	BOOL	RenderCredits(float time, int alpha);
};

extern CCredits CREDITS;

#endif
