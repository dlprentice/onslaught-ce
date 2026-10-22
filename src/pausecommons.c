/* pausecommons.c: PauseMenu's two text buffers (PauseMenu.cpp declares them extern "C"). Retail keeps them as COMMON
   symbols at the start of the .bss tail (009d3060, 009d3200), ahead of the C runtime's commons: C tentative
   definitions in an object of their own, which C++ cannot produce. The file name is inferred, and the names are ours
   (Stuart's release has neither), chosen so LINK's order within the object matches retail's (gJoystickText first;
   sJoystickText/sModeText come out the other way round). */

unsigned short gJoystickText[208];
unsigned short gModeText[80];		/* 160 bytes: retail's gap from it to the CRT's __pioinfo (009d32a0) */
