// Capture.h: the movie capture (only what matched files need so far).

#ifndef CAPTURE_H
#define CAPTURE_H

#include <vfw.h>				// the AVI file the capture writes (Stuart's capture.h users: d3dapp.cpp, DXEngine.cpp)

class CCapture
{
public:
	static void	Capture();							// 005140e0 (CDXEngine::PostRender 0053ef63): on the PC, appends the back
													// buffer to the AVI capture while one runs (ltshell.cpp)
};
#define CCAPTURE CCapture

#endif
