// System.h: the system object that runs the game (only what matched files need). SYSTEM is at 00896ca4;
// WinMain (00512130) calls its Init (004efb10), Run (004f0330) and Shutdown (004f00e0).

#ifndef SYSTEM_H
#define SYSTEM_H

class CSystem
{
public:
	SINT	mNextLevel;							// +0x00 (con_map 0046be4f)

	BOOL	Init();							// 004efb10
	SINT	Run();							// 004f0330: the front end's last result
	void	Shutdown();						// 004f00e0
};

extern CSystem SYSTEM;

extern BOOL	PLAYABLE_DEMO;					// 0083d448: the playable-demo build's switch

#endif
