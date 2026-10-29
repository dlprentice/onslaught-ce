// SPDX-License-Identifier: GPL-3.0-only
// CE photo mode (port/game/ce_photo.cpp, patches 0108 and 0133) and its screenshot writer (port/platform/common/ce_screenshot.cpp).
#ifndef PORT_CE_PHOTO_H
#define PORT_CE_PHOTO_H

// CE_ScreenshotSave's outcome.
enum
{
	CE_SHOT_SAVED = 0,
	CE_SHOT_FORMAT = -1,	// a back buffer format it does not read
	CE_SHOT_SIZE = -2,		// no picture, or larger than CE_SHOT_MAX_SIDE either way
	CE_SHOT_FOLDER = -3,	// the screenshots folder cannot be made or is not a plain folder (a link is refused)
	CE_SHOT_FULL = -4,		// the folder already holds CE_SHOT_MAX_FILES entries
	CE_SHOT_ENCODE = -5,	// PNG encoding failed, or the file would exceed CE_SHOT_MAX_BYTES
	CE_SHOT_WRITE = -6,		// the file could not be written whole (what was written is removed)
	CE_SHOT_NAME = -7,		// every name for this second is taken
	CE_SHOT_READ = -8,		// the back buffer could not be read back (d3d9hooks.cpp)
};
const int CE_SHOT_MAX_FILES = 2000;
const int CE_SHOT_MAX_SIDE = 8192;
const long long CE_SHOT_MAX_BYTES = 192ll << 20;

// Saves a picture (pixels in the Direct3D 9 format d3dFormat: X8R8G8B8, A8R8G8B8 taken as opaque, R5G6B5, X1R5G5B5 or
// A1R5G5B5) as <user folder>/screenshots/onslaught-ce-YYYYMMDD-HHMMSS[-N].png in local time, never replacing a file.
// name (n bytes) receives the file name. Returns CE_SHOT_SAVED or a negative CE_SHOT_* status.
extern "C" int CE_ScreenshotSave(const void *pixels, int pitch, int width, int height, unsigned d3dFormat, char *name, int n);
extern "C" unsigned long long CE_ScreenshotTicks(void);

bool	PortCePhotoOn();

// Photo mode hides the HUD, the battle line and the cockpit by dropping the draw calls their routines make (patch 0133),
// never by skipping the routines: what they compute feeds the simulation (the HUD's warning panel calls
// CBattleEngine::TestDanger, which starts the incoming-warhead warning that draws from rand).
void	PortCeDrawHooks(bool on);			// d3d9hooks.cpp: the device's draw entries filtered (on) or as the device came (off)
void	PortCeDrawsDropped(int delta);		// d3d9hooks.cpp: nests; while positive, filtered draw calls do nothing

class CePhotoDrawScope
{
	bool dropping;
public:
	CePhotoDrawScope() : dropping(PortCePhotoOn()) { if (dropping) PortCeDrawsDropped(1); }
	~CePhotoDrawScope() { if (dropping) PortCeDrawsDropped(-1); }
	CePhotoDrawScope(const CePhotoDrawScope &) = delete;
	CePhotoDrawScope &operator=(const CePhotoDrawScope &) = delete;
};

bool	PortCePhotoSet(bool on);
bool	PortCePhotoKey(unsigned key, bool repeat);
void	PortCePhotoFrame();
void	PortCePhotoLevelEnd();
bool	PortCePhotoShotWanted();
void	PortCePhotoShotDone(int status, const char *name);
void	PortCePhotoRender();
bool	PortCePhotoSavedNext();		// the frame being drawn is saved as a screenshot at the Present that follows (ce_notice.cpp)

#endif
