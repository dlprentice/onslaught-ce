// SPDX-License-Identifier: GPL-3.0-only
// port/bink's pause and video clock (PortBinkPause, Clock, BinkWait) on the host: the production source is included
// whole (BINK_SOURCE) and driven with a fake DirectSound buffer that ends as port/dsound's does (an ended one-shot
// buffer is halted with its cursor back at 0) and a fake QueryPerformanceCounter. No file is opened and nothing is
// decoded: the Video is built by hand. Exit 2 on the first failed check.
#include BINK_SOURCE

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

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
		Need(PortBinkPause(&idle.pub, 1) == 1, "twice: a film paused before start records the hold");
		Need(PortBinkPause(&idle.pub, 1) == 0, "twice: a second pre-start pause changes nothing");
		Need(PortBinkPause(&idle.pub, 0) == 1, "twice: pre-start resume clears the hold");
		Need(PortBinkPause(&idle.pub, 0) == 0, "twice: a second pre-start resume changes nothing");
	}
	if (run("loop-restart"))
	{
		// A 2-frame film at 25 fps (40 ms/frame): BinkNextFrame wrapping FrameNum 2 -> 1 must keep pacing frame 2
		// until frame 1 of cycle 2 is decoded in BinkDoFrame, which then restarts both wall clock and sound!
		FakeBuffer b(0.08);
		Video *v = new Video;
		v->pub.Frames = 2;
		v->pub.FrameNum = 1;
		v->fps = 25.0;
		v->sound = &b;
		v->soundRate = 44100;
		v->soundBlock = 4;

		BinkDoFrame(&v->pub); // frame 1 of cycle 1
		Need(v->started && b.playing && b.plays == 1, "loop-restart: cycle 1 started sound");
		BinkNextFrame(&v->pub); // FrameNum = 2, frame 1 due at 0.0 s
		Need(BinkWait(&v->pub) == 0, "loop-restart: frame 1 due immediately");

		Pass(&b, 0.01); // 10 ms into cycle 1; frame 2 is due at 40 ms
		BinkDoFrame(&v->pub); // decode frame 2
		BinkNextFrame(&v->pub); // FrameNum wraps to 1! Frame 2 is now waiting (due at 40 ms).
		Need(v->pub.FrameNum == 1, "loop-restart: BinkNextFrame wrapped FrameNum to 1");
		Need(b.plays == 1 && b.stops == 0, "loop-restart: BinkNextFrame wrap did NOT restart sound early");
		Need(BinkWait(&v->pub) == 1, "loop-restart: BinkWait still paces last frame (40 ms) before cycle 2 starts");

		Pass(&b, 0.09); // total 100 ms: one-shot sound ended at 80 ms, frame 2 is now past due
		Need(!b.playing && b.cursor == 0, "loop-restart: cycle 1 one-shot sound finished");
		Need(BinkWait(&v->pub) == 0, "loop-restart: last frame of cycle 1 is now due");

		// Decoding frame 1 of cycle 2 must restart wall clock AND rewind/replay sound!
		BinkDoFrame(&v->pub);
		Need(Near(Clock(v), 0.0), "loop-restart: cycle 2 BinkDoFrame restarted clock to 0", Clock(v));
		Need(b.playing && b.plays == 2 && b.stops == 1 && b.cursor == 0, "loop-restart: cycle 2 restarted sound from 0");
		BinkNextFrame(&v->pub); // FrameNum = 2
		BinkDoFrame(&v->pub);
		BinkNextFrame(&v->pub); // FrameNum = 1 (waiting = 2, due at 40 ms of cycle 2)
		Pass(&b, 0.01);
		Need(BinkWait(&v->pub) == 1, "loop-restart: cycle 2 frame 2 waits until 40 ms of cycle 2 (not 0 ms!)");
		v->sound = nullptr;
		delete v;
	}
	if (run("zero-frames"))
	{
		Video v;
		v.started = true;
		v.pub.Frames = 0;
		v.pub.FrameNum = 1;
		v.fps = 25.0;
		Need(BinkWait(&v.pub) == 0, "zero-frames: BinkWait returns 0 without unsigned underflow");
		BinkNextFrame(&v.pub);
		Need(v.pub.FrameNum == 1, "zero-frames: BinkNextFrame is a no-op when Frames == 0");
		Need(BinkOpen(nullptr, 0) == nullptr && BinkOpen("", 0) == nullptr, "zero-frames: BinkOpen rejects empty/null path");
	}
	if (run("copy-bounds"))
	{
		Video v;
		v.pub.Width = 4;
		v.pub.Height = 4;
		v.decoded = 1;
		v.frame = av_frame_alloc();
		Need(v.frame != nullptr, "copy-bounds: av_frame_alloc");
		v.frame->format = AV_PIX_FMT_YUV420P;
		v.frame->width = 4;
		v.frame->height = 4;
		Need(av_frame_get_buffer(v.frame, 16) == 0, "copy-bounds: av_frame_get_buffer");
		for (int r = 0; r < 4; ++r)
			std::memset(v.frame->data[0] + r * v.frame->linesize[0], 200, 4);
		for (int r = 0; r < 2; ++r)
		{
			std::memset(v.frame->data[1] + r * v.frame->linesize[1], 128, 2);
			std::memset(v.frame->data[2] + r * v.frame->linesize[2], 128, 2);
		}

		// Canary-guarded 4x4 BGRA destination buffer: y = 2, height = 4 must copy ONLY 2 rows (4 - 2), never 4!
		std::vector<unsigned char> guard(64 + 4 * 16 + 64, 0xA5);
		unsigned char *dest = guard.data() + 64;
		BinkCopyToBuffer(&v.pub, dest, 16, 4, 0, 2, 0);
		for (int i = 0; i < 64; ++i)
		{
			Need(guard[i] == 0xA5, "copy-bounds: leading canary intact");
			Need(guard[64 + 4 * 16 + i] == 0xA5, "copy-bounds: trailing canary intact (height - y respected!)");
		}
		for (int i = 0; i < 2 * 16; ++i)
			Need(dest[i] == 0xA5, "copy-bounds: rows above y=2 untouched");
		Need(dest[2 * 16] != 0xA5 && dest[3 * 16] != 0xA5, "copy-bounds: rows 2..3 written");

		// Reject dest == NULL, y >= height, and (x + w) * 4 > abs(pitch).
		std::fill(guard.begin(), guard.end(), 0x5A);
		BinkCopyToBuffer(&v.pub, nullptr, 16, 4, 0, 0, 0);
		BinkCopyToBuffer(&v.pub, dest, 16, 4, 0, 4, 0);
		BinkCopyToBuffer(&v.pub, dest, 16, 4, 1, 0, 0); // (1 + 4) * 4 = 20 > pitch 16
		for (unsigned char c : guard)
			Need(c == 0x5A, "copy-bounds: out-of-bounds x/y/pitch rejected without writing");

		// Negative destination pitch (bottom-up surface) and negative source linesize must work via signed ptrdiff_t!
		std::fill(guard.begin(), guard.end(), 0x3C);
		unsigned char *bottomRow = dest + 3 * 16;
		BinkCopyToBuffer(&v.pub, bottomRow, -16, 4, 0, 0, 0);
		for (int i = 0; i < 64; ++i)
			Need(guard[i] == 0x3C && guard[64 + 4 * 16 + i] == 0x3C, "copy-bounds: negative pitch keeps canaries intact");
		Need(dest[0] != 0x3C && dest[3 * 16] != 0x3C, "copy-bounds: negative pitch wrote all 4 rows in bounds");

		av_frame_free(&v.frame);
	}
	if (run("make-sound-safety"))
	{
		struct SplitLockBuffer : IDirectSoundBuffer
		{
			std::vector<unsigned char> ring;
			int refs = 1;
			bool failLock = false, shortLock = false, failUnlock = false;
			explicit SplitLockBuffer(DWORD n) : ring(n, 0) {}
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOINTERFACE; }
			ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)++refs; }
			ULONG STDMETHODCALLTYPE Release() override { int r = --refs; if (r == 0) delete this; return (ULONG)r; }
			HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS c) override { c->dwBufferBytes = (DWORD)ring.size(); return DS_OK; }
			HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD p, LPDWORD w) override { if (p) *p = 0; if (w) *w = 0; return DS_OK; }
			HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX, DWORD, LPDWORD) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE GetVolume(LPLONG v) override { *v = 0; return DS_OK; }
			HRESULT STDMETHODCALLTYPE GetPan(LPLONG p) override { *p = 0; return DS_OK; }
			HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD f) override { *f = 44100; return DS_OK; }
			HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD s) override { *s = 0; return DS_OK; }
			HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUND, LPCDSBUFFERDESC) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE Lock(DWORD, DWORD bytes, LPVOID *p1, LPDWORD n1, LPVOID *p2, LPDWORD n2, DWORD flags) override
			{
				if (failLock) { if (p1) *p1 = nullptr; if (n1) *n1 = 0; if (p2) *p2 = nullptr; if (n2) *n2 = 0; return DSERR_GENERIC; }
				if (flags & DSBLOCK_ENTIREBUFFER) bytes = (DWORD)ring.size();
				DWORD first = bytes > 4 ? 4 : bytes;
				DWORD second = shortLock ? 0 : (bytes - first);
				*p1 = ring.data(); *n1 = first;
				if (p2) *p2 = second ? ring.data() + first : nullptr;
				if (n2) *n2 = second;
				return DS_OK;
			}
			HRESULT STDMETHODCALLTYPE Play(DWORD, DWORD, DWORD) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE SetVolume(LONG) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE SetPan(LONG) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE SetFrequency(DWORD) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE Stop() override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD, LPVOID, DWORD) override { return failUnlock ? DSERR_GENERIC : DS_OK; }
			HRESULT STDMETHODCALLTYPE Restore() override { return DS_OK; }
		};
		struct MockDS8 : IDirectSound8
		{
			std::atomic<int> refs{1};
			int mode = 0; // 0=ok split lock, 1=failLock, 2=shortLock, 3=failUnlock
			int liveBuffers = 0;
			SplitLockBuffer *lastCreated = nullptr;
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOINTERFACE; }
			ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)++refs; }
			ULONG STDMETHODCALLTYPE Release() override { return (ULONG)--refs; }
			HRESULT STDMETHODCALLTYPE CreateSoundBuffer(LPCDSBUFFERDESC d, LPDIRECTSOUNDBUFFER *out, IUnknown *) override
			{
				auto *b = new SplitLockBuffer(d->dwBufferBytes);
				b->failLock = (mode == 1);
				b->shortLock = (mode == 2);
				b->failUnlock = (mode == 3);
				lastCreated = b;
				*out = b;
				return DS_OK;
			}
			HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER *) override { return DSERR_UNSUPPORTED; }
			HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE Compact() override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE Initialize(LPCGUID) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD) override { return DS_OK; }
		};

		// Build a minimal 4-frame stereo 16-bit 44100 Hz WAV in memory so real FFmpeg DecodeAudio + MakeSound run end to end!
		auto makeWavVideo = [](uint32_t sampleRate) -> Video * {
			std::vector<unsigned char> wav(44 + 16, 0);
			auto put32 = [&](size_t off, uint32_t val) { std::memcpy(&wav[off], &val, 4); };
			auto put16 = [&](size_t off, uint16_t val) { std::memcpy(&wav[off], &val, 2); };
			std::memcpy(&wav[0], "RIFF", 4); put32(4, 36 + 16); std::memcpy(&wav[8], "WAVEfmt ", 8);
			put32(16, 16); put16(20, 1); put16(22, 2); put32(24, sampleRate);
			put32(28, sampleRate * 4); put16(32, 4); put16(34, 16);
			std::memcpy(&wav[36], "data", 4); put32(40, 16);
			const short samples[8] = {1000, -1000, 2000, -2000, 3000, -3000, 4000, -4000};
			std::memcpy(&wav[44], samples, 16);

			Video *v = new (std::malloc(sizeof(Video))) Video;
			v->file = wav;
			v->mem.data = v->file.data();
			v->mem.size = (int64_t)v->file.size();
			v->mem.pos = 0;
			unsigned char *iobuf = (unsigned char *)av_malloc(4096);
			AVIOContext *io = avio_alloc_context(iobuf, 4096, 0, &v->mem, ReadMem, nullptr, SeekMem);
			v->fmt = avformat_alloc_context();
			v->fmt->pb = io;
			if (avformat_open_input(&v->fmt, nullptr, av_find_input_format("wav"), nullptr) < 0 ||
				avformat_find_stream_info(v->fmt, nullptr) < 0)
			{
				if (!v->fmt) { av_freep(&io->buffer); avio_context_free(&io); }
				Free(v);
				return nullptr;
			}
			return v;
		};

		MockDS8 ds;
		BinkOpenDirectSound((U32)(uintptr_t)&ds);
		Need(ds.refs.load() == 2, "make-sound-safety: BinkOpenDirectSound AddRefs device");

		// 1) Split 2-region Lock succeeds and copies all 16 PCM bytes across p1 (4B) + p2 (12B).
		ds.mode = 0;
		Video *vOk = makeWavVideo(44100);
		Need(vOk != nullptr, "make-sound-safety: in-memory WAV opened");
		MakeSound(vOk);
		Need(vOk->sound != nullptr && ds.lastCreated != nullptr, "make-sound-safety: split Lock succeeded");
		short gotFirst = 0, gotLast = 0;
		std::memcpy(&gotFirst, ds.lastCreated->ring.data(), 2);
		std::memcpy(&gotLast, ds.lastCreated->ring.data() + 14, 2);
		Need(gotFirst != 0 && gotLast != 0, "make-sound-safety: both Lock regions populated");
		Free(vOk);

		// 2) Lock failure, short Lock, and Unlock failure must each Release the created buffer and leave v->sound == NULL.
		for (int failMode : {1, 2, 3})
		{
			ds.mode = failMode;
			Video *vf = makeWavVideo(44100);
			Need(vf != nullptr, "make-sound-safety: WAV opened for failure mode");
			MakeSound(vf);
			Need(vf->sound == nullptr, "make-sound-safety: failed Lock/Unlock leaves v->sound NULL");
			Free(vf);
		}

		BinkOpenDirectSound(0);
		Need(ds.refs.load() == 1, "make-sound-safety: device refcount balanced back to 1");
	}
	if (run("dsound-thread-safety"))
	{
		struct ThreadDS8 : IDirectSound8
		{
			std::atomic<int> refs{1};
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOINTERFACE; }
			ULONG STDMETHODCALLTYPE AddRef() override { int r = ++refs; Need(r >= 1, "AddRef positive"); return (ULONG)r; }
			ULONG STDMETHODCALLTYPE Release() override { int r = --refs; Need(r >= 1, "Release never drops below caller's ref"); return (ULONG)r; }
			HRESULT STDMETHODCALLTYPE CreateSoundBuffer(LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER *, IUnknown *) override { return DSERR_GENERIC; }
			HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER *) override { return DSERR_UNSUPPORTED; }
			HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE Compact() override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE Initialize(LPCGUID) override { return DS_OK; }
			HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD) override { return DS_OK; }
		};
		ThreadDS8 a, b;
		std::atomic<bool> stop{false};
		std::thread swapper([&] {
			for (int i = 0; i < 2000; ++i)
			{
				BinkOpenDirectSound((U32)(uintptr_t)((i & 1) ? &a : &b));
				U32 tr = (U32)(i & 1);
				BinkSetSoundTrack(1, &tr);
			}
			stop.store(true);
		});
		std::thread reader([&] {
			AVFormatContext *emptyFmt = avformat_alloc_context();
			while (!stop.load())
			{
				Video v;
				v.fmt = emptyFmt;
				MakeSound(&v);
				v.fmt = nullptr;
			}
			avformat_free_context(emptyFmt);
		});
		swapper.join();
		reader.join();
		BinkOpenDirectSound(0);
		Need(a.refs.load() == 1 && b.refs.load() == 1, "dsound-thread-safety: concurrent swaps and MakeSound preserve exact refcounts");
	}
	std::printf("bink pause checks passed\n");
	return 0;
}
