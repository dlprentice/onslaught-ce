// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_bugclip.h: built-in F9 bug-clip recorder (video + stereo game audio + scrubbed diagnostic report,
// packaged as a single GitHub-ready .zip). Off unless the player toggles it with F9 in ordinary CE play (or CE bug
// recording); disabled in faithful, replay, probe, autopilot, and test-kit runs. Runs JPEG encoding, RIFF AVI writing,
// and ZIP packaging on a dedicated worker thread so gameplay frame pacing and the audio callback never block.
#ifndef PORT_CE_BUGCLIP_H
#define PORT_CE_BUGCLIP_H

#ifdef __cplusplus
extern "C" {
#endif

// Toggle clip recording on/off (bound to F9 in window.cpp). Safe to call from the main thread.
// Returns 1 if F9 was handled (CE mode), 0 if disallowed (faithful/replay/probe/pilot).
int PortCeBugClipToggle(void);

// 1 while a clip is actively recording, 0 otherwise.
int PortCeBugClipActive(void);

// Called on Present() (d3d9hooks.cpp) to ask whether a video frame should be read back now.
// When returning 1, *outMaxW and *outMaxH receive the recommended maximum downscaled dimensions (multiples of 16).
int PortCeBugClipWantFrame(unsigned *outMaxW, unsigned *outMaxH);

// Hand one read-back frame (XRGB8888, A8R8G8B8, RGB565, XRGB1555, A1R5G5B5) to the background encoder.
// Non-blocking: drops cleanly if the worker queue is temporarily full.
void PortCeBugClipSubmitFrame(const void *pixels, int pitch, int width, int height, unsigned d3dFormat);

// Called from DirectSound Mixer::Mix (dsound.cpp) on the audio callback thread.
// Strictly lock-free, allocation-free, and syscall-free: copies 44.1 kHz 16-bit stereo PCM into a fixed ring buffer.
void PortCeBugClipPushAudio(const short *stereoPcm44100, int frames);

// Query HUD/front-end status text for rendering (uses only ASCII characters present in the game's bitmap font).
// Returns 1 if text should be drawn; sets *outRecording (1 = live REC indicator, 0 = fading toast) and *outFade [0..1].
int PortCeBugClipStatus(char *out, int n, int *outRecording, float *outFade);

// Flush, finalize any active clip and companion diagnostic file, and join the worker thread cleanly on exit.
void PortCeBugClipShutdown(void);

#ifdef __cplusplus
}
#endif

#endif
