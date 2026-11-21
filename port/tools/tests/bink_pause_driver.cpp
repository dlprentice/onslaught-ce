// SPDX-License-Identifier: GPL-3.0-only
// port/bink's pause and video clock (PortBinkPause, Clock, BinkWait) on the host: the production source is included
// whole (BINK_SOURCE) and driven with a fake DirectSound buffer that ends as port/dsound's does (an ended one-shot
// buffer is halted with its cursor back at 0) and a fake QueryPerformanceCounter. No file is opened and nothing is
// decoded: the Video is built by hand. Exit 2 on the first failed check.
#include BINK_SOURCE

#include <cmath>
#include <cstdio>
#include <cstdlib>

// ---- the clock and the port functions bink.cpp calls
static double gNow = 100.0;
extern "C" BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *c) { c->QuadPart = (LONGLONG)llround(gNow * 1e6); return TRUE; }
extern "C" BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = 1000000; return TRUE; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}
bool PortSessionEnabled() { return false; }
bool PortAudioMediaRole(const void *, PortSessionCategory) { return false; }
void PortSessionMediaActive(PortSessionCategory, const void *, bool) {}
unsigned long long PortClockFrames() { return 0; }
bool PortClockStandsBetweenPresents() { return false; }
extern "C" HANDLE WINAPI CreateFileA(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) { return INVALID_HANDLE_VALUE; }
extern "C" BOOL WINAPI CloseHandle(HANDLE) { return TRUE; }
extern "C" DWORD WINAPI GetFileSize(HANDLE, LPDWORD) { return 0; }
extern "C" BOOL WINAPI ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) { return FALSE; }

// ---- a sound buffer that plays in fake time and ends as port/dsound's does
struct FakeBuffer : IDirectSoundBuffer
{
	DWORD bytes, block = 4, rate = 44100;
	double cursor = 0;			// bytes
	bool playing = false;
	int plays = 0, stops = 0;
	explicit FakeBuffer(double seconds) : bytes((DWORD)(seconds * 44100) * 4) {}
	void Advance(double seconds)
	{
		if (!playing) return;
		cursor += seconds * rate * block;
		if (cursor >= bytes) { cursor = 0; playing = false; }	// dsound.cpp: an ended buffer is halted, cursor 0
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
	ULONG STDMETHODCALLTYPE Release() override { return 0; }
	HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS c) override { c->dwBufferBytes = bytes; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD p, LPDWORD w) override
	{ if (p) *p = (DWORD)cursor; if (w) *w = (DWORD)cursor; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX, DWORD, LPDWORD) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetVolume(LPLONG v) override { *v = 0; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetPan(LPLONG p) override { *p = 0; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD f) override { *f = rate; return DS_OK; }
	HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD s) override { *s = playing ? DSBSTATUS_PLAYING : 0; return DS_OK; }
	HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUND, LPCDSBUFFERDESC) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE Lock(DWORD, DWORD, LPVOID *, LPDWORD, LPVOID *, LPDWORD, DWORD) override { return DSERR_INVALIDCALL; }
	HRESULT STDMETHODCALLTYPE Play(DWORD, DWORD, DWORD) override { playing = true; ++plays; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD p) override { cursor = p; return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetVolume(LONG) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetPan(LONG) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE SetFrequency(DWORD) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE Stop() override { playing = false; ++stops; return DS_OK; }
	HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD, LPVOID, DWORD) override { return DS_OK; }
	HRESULT STDMETHODCALLTYPE Restore() override { return DS_OK; }
};

static int gFailed;
static void Need(bool ok, const char *why, double got = NAN)
{
	if (ok) return;
	if (std::isnan(got)) std::fprintf(stderr, "FAIL: %s\n", why);
	else std::fprintf(stderr, "FAIL: %s (got %.4f)\n", why, got);
	std::exit(2);
}
static bool Near(double a, double b) { return std::fabs(a - b) < 1e-3; }

// a started video of 'frames' frames at 25 fps, with a sound of 'sound' seconds (0 = none), as BinkDoFrame leaves it
static Video *Started(unsigned frames, FakeBuffer *sound)
{
	Video *v = new Video;
	v->pub.Frames = frames;
	v->pub.FrameNum = 1;
	v->fps = 25.0;
	v->sound = sound;
	v->soundRate = 44100;
	v->soundBlock = 4;
	v->started = true;
	QueryPerformanceCounter(&v->start);
	if (sound) { sound->SetCurrentPosition(0); sound->Play(0, 0, 0); }
	return v;
}
static void Pass(FakeBuffer *b, double seconds) { gNow += seconds; if (b) b->Advance(seconds); }

int main(int argc, char **argv)
{
	const char *only = argc > 1 ? argv[1] : "";
	auto run = [&](const char *name) { return !*only || !std::strcmp(only, name); };
	if (run("mid-film"))
	{
		FakeBuffer b(4.0);
		Video *v = Started(100, &b);
		Pass(&b, 1.0);
		Need(Near(Clock(v), 1.0), "mid-film: the clock follows the sound", Clock(v));
		Need(PortBinkPause(&v->pub, 1) == 1, "mid-film: pause accepted");
		Need(!b.playing && b.stops == 1, "mid-film: pausing stops the playing sound");
		Pass(&b, 5.0);
		Need(Near(Clock(v), 1.0), "mid-film: the clock is held where the sound stopped", Clock(v));
		v->pub.FrameNum = 28;	// frame 27 decoded ahead, due at 1.04 s
		Need(BinkWait(&v->pub) == 1, "mid-film: no frame falls due while held");
		Need(PortBinkPause(&v->pub, 0) == 1, "mid-film: resume accepted");
		Need(b.playing && b.plays == 2, "mid-film: resuming plays the sound on");
		Pass(&b, 0.5);
		Need(Near(Clock(v), 1.5), "mid-film: picture and sound go on together", Clock(v));
		Need(BinkWait(&v->pub) == 0, "mid-film: the next frame falls due after resuming");
	}
	if (run("after-end"))
	{
		FakeBuffer b(2.0);			// the sound ends before the picture does
		Video *v = Started(100, &b);
		Pass(&b, 2.5);
		Need(!b.playing && b.cursor == 0, "after-end: the fake sound ended as port/dsound's does");
		Need(Near(Clock(v), 2.5), "after-end: the clock runs on past the sound's end", Clock(v));
		Need(PortBinkPause(&v->pub, 1) == 1, "after-end: pause accepted");
		Pass(&b, 3.0);
		Need(Near(Clock(v), 2.5), "after-end: the clock is held at the pause, not at the ended cursor (0) nor run on", Clock(v));
		Need(PortBinkPause(&v->pub, 0) == 1, "after-end: resume accepted");
		Need(!b.playing && b.plays == 1, "after-end: resuming does not play the ended sound again");
		Pass(&b, 0.5);
		Need(Near(Clock(v), 3.0), "after-end: the clock goes on from the pause", Clock(v));
		Need(!b.playing, "after-end: the sound stays ended");
	}
	if (run("no-sound"))
	{
		Video *v = Started(100, nullptr);
		Pass(nullptr, 1.2);
		Need(PortBinkPause(&v->pub, 1) == 1, "no-sound: pause accepted");
		Pass(nullptr, 4.0);
		Need(Near(Clock(v), 1.2), "no-sound: a silent film's clock is held", Clock(v));
		PortBinkPause(&v->pub, 0);
		Pass(nullptr, 0.3);
		Need(Near(Clock(v), 1.5), "no-sound: and goes on from where it was", Clock(v));
	}
	if (run("twice"))
	{
		FakeBuffer b(4.0);
		Video *v = Started(100, &b);
		Pass(&b, 0.5);
		PortBinkPause(&v->pub, 1);
		Need(PortBinkPause(&v->pub, 1) == 0 && b.stops == 1, "twice: a second pause changes nothing");
		PortBinkPause(&v->pub, 0);
		Need(PortBinkPause(&v->pub, 0) == 0 && b.plays == 2, "twice: a second resume changes nothing");
		Video idle;
		Need(PortBinkPause(&idle.pub, 1) == 0, "twice: a film not started is not held");
	}
	std::printf("bink pause checks passed\n");
	return 0;
}
