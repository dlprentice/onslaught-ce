// SPDX-License-Identifier: GPL-3.0-only
// port/bink/bink.cpp: RAD's Bink entry points the front end calls (src/binkapi.h), decoding the game's .vid files (Bink 1,
// revision i) with FFmpeg's LGPL Bink demuxer and decoders. Replaces port/platform/common/bink.cpp (the platform's stand-in,
// which could not open a file); same names and calling convention (stdcall on Windows; dxvk-native defines __stdcall
// away on Linux).
//
// How the game drives it (DXFrontEndVideo.cpp): BinkOpen on a worker thread; BinkDoFrame decodes the current frame and
// BinkNextFrame advances FrameNum (1-based); BinkWait answers whether the next frame is not yet due; BinkCopyToBuffer
// converts the decoded frame into the texture (32-bit BGRA or R5G6B5); the video ends when FrameNum reaches Frames.
// Here the whole file is read and demuxed at open. The chosen sound track is decoded entirely into one DirectSound
// buffer on the game's own IDirectSound (BinkSetSoundSystem), so it mixes, and records, with the rest of the game's
// sound, and BinkWait follows that buffer's play position (the wall clock when a video has no sound). Colour conversion
// is BT.601 in the range the decoder reports.
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

// FFmpeg's structures need the layout FFmpeg was built with, so this file is compiled with -mno-ms-bitfields after the
// port's layout flags (build.py; port/bink/README.md): -mms-bitfields would align FFmpeg's 64-bit members to 8 on Linux
// (AVPacket 80 bytes instead of 76) and the decoder would read garbage. BINK, the one structure shared with the game,
// holds only 32-bit fields and lays out the same either way.
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
}

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "../platform/common/portlog.h"

#ifdef _WIN32
extern "C" HANDLE WINAPI PortCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
										 DWORD disposition, DWORD flags, HANDLE templ);	// port/platform/windows/files.cpp
#endif

typedef unsigned long U32;
typedef long S32;

// The front end reads only these four fields; the rest of RAD's structure is not part of this project.
struct BINK
{
	U32 Width;
	U32 Height;
	U32 Frames;
	U32 FrameNum;
};
typedef BINK *HBINK;

struct BINKSUMMARY
{
	U32 Unknown00[9];
	U32 TotalPlayedFrames;
	U32 SkippedFrames;
	U32 Unknown2c[20];
};

typedef void *(__stdcall *RADMEMALLOC)(U32 bytes);
typedef void(__stdcall *RADMEMFREE)(void *ptr);
typedef void *(__stdcall *BINKSNDSYSOPEN)(U32 param);

unsigned long long PortClockFrames();		// port/platform/common/clock.cpp
bool PortClockStandsBetweenPresents();	// likewise: the shim clock, standing still between Presents

namespace
{
const U32 kSurface565 = 10;		// BinkCopyToBuffer's formats: 565, or else 32-bit BGRA (BINKSURFACE32A, 5)

IDirectSound8 *gSound = NULL;		// BinkSetSoundSystem's parameter: the game's IDirectSound
U32 gTrack = 0;						// BinkSetSoundTrack's choice; 0 = the first audio stream
bool gTrackSet = false;
unsigned gFilmSerial;

struct Memory
{
	const unsigned char *data;
	int64_t size, pos;
};

int ReadMem(void *opaque, uint8_t *buf, int n)
{
	Memory *m = (Memory *)opaque;
	int64_t left = m->size - m->pos;
	if (left <= 0)
		return AVERROR_EOF;
	if (n > left)
		n = (int)left;
	memcpy(buf, m->data + m->pos, n);
	m->pos += n;
	return n;
}

int64_t SeekMem(void *opaque, int64_t off, int whence)
{
	Memory *m = (Memory *)opaque;
	if (whence == AVSEEK_SIZE)
		return m->size;
	int64_t p = (whence & ~AVSEEK_FORCE) == SEEK_CUR ? m->pos + off : (whence & ~AVSEEK_FORCE) == SEEK_END ? m->size + off : off;
	if (p < 0 || p > m->size)
		return -1;
	m->pos = p;
	return p;
}

struct Video
{
	BINK pub;						// first: the game reads it through HBINK
	unsigned sessionId = 0;
	char sessionAsset[121] = {};
	std::vector<unsigned char> file;
	Memory mem;
	AVFormatContext *fmt = NULL;
	AVCodecContext *dec = NULL;
	int videoStream = -1;
	std::vector<AVPacket *> packets;	// the video stream's packets, one per frame
	U32 decoded = 0;				// the frame dec last produced (1-based), 0 before any
	AVFrame *frame = NULL;
	double fps = 15.0;
	IDirectSoundBuffer *sound = NULL;
	DWORD soundRate = 0, soundBlock = 0;
	LONG volume = 0;
	LARGE_INTEGER start;
	bool started = false;
	U32 played = 0;

};

double Seconds(const LARGE_INTEGER &a)
{
	LARGE_INTEGER now, f;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&f);
	return (double)(now.QuadPart - a.QuadPart) / (double)f.QuadPart;
}

// The video's clock: the sound's play position, or the time since the first frame was decoded.
double Clock(Video *v)
{
	if (!v->started)
		return 0;
	if (v->sound)
	{
		DWORD play = 0, status = 0;
		v->sound->GetStatus(&status);
		if (status & DSBSTATUS_PLAYING)
		{
			v->sound->GetCurrentPosition(&play, NULL);
			return (double)play / v->soundBlock / v->soundRate;
		}
		// the sound has finished: carry on from its length
		DSBCAPS caps;
		caps.dwSize = sizeof(caps);
		v->sound->GetCaps(&caps);
		double len = (double)caps.dwBufferBytes / v->soundBlock / v->soundRate;
		double wall = Seconds(v->start);
		return wall > len ? wall : len;
	}
	return Seconds(v->start);
}

bool ReadFileAll(const char *name, std::vector<unsigned char> &out)
{
	// The game's relative name (data\video\...) through the port's file layer, as the game's own CreateFileA calls go
	// (port.h): on Windows the process runs in the player's user folder, not the game folder, and kernel32's CreateFileA
	// would look for the video there. (On Linux the port's CreateFileA is the file layer itself.)
#ifdef _WIN32
	HANDLE h = PortCreateFileA(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#else
	HANDLE h = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#endif
	if (h == INVALID_HANDLE_VALUE)
		return false;
	DWORD size = GetFileSize(h, NULL), got = 0;
	out.resize(size);
	bool ok = size != INVALID_FILE_SIZE && ReadFile(h, out.data(), size, &got, NULL) && got == size;
	CloseHandle(h);
	return ok;
}

// Decodes one audio stream entirely to interleaved 16-bit PCM.
bool DecodeAudio(Video *v, int stream, std::vector<short> &pcm, int *rate, int *channels)
{
	AVStream *st = v->fmt->streams[stream];
	const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
	if (!codec)
		return false;
	AVCodecContext *ctx = avcodec_alloc_context3(codec);
	avcodec_parameters_to_context(ctx, st->codecpar);
	if (avcodec_open2(ctx, codec, NULL) < 0)
	{
		avcodec_free_context(&ctx);
		return false;
	}
	*rate = ctx->sample_rate;
	*channels = ctx->ch_layout.nb_channels;
	AVPacket *pkt = av_packet_alloc();
	AVFrame *fr = av_frame_alloc();
	v->mem.pos = 0;
	avformat_seek_file(v->fmt, -1, INT64_MIN, 0, INT64_MAX, 0);
	auto drain = [&]() {
		while (avcodec_receive_frame(ctx, fr) == 0)
		{
			int ch = fr->ch_layout.nb_channels, n = fr->nb_samples;
			size_t base = pcm.size();
			pcm.resize(base + (size_t)n * ch);
			for (int i = 0; i < n; i++)
				for (int c = 0; c < ch; c++)
				{
					float s;
					if (fr->format == AV_SAMPLE_FMT_FLTP)
						s = ((const float *)fr->extended_data[c])[i];
					else if (fr->format == AV_SAMPLE_FMT_FLT)
						s = ((const float *)fr->extended_data[0])[i * ch + c];
					else if (fr->format == AV_SAMPLE_FMT_S16P)
						s = ((const short *)fr->extended_data[c])[i] / 32768.0f;
					else
						s = ((const short *)fr->extended_data[0])[i * ch + c] / 32768.0f;
					s = s > 1.0f ? 1.0f : s < -1.0f ? -1.0f : s;
					pcm[base + (size_t)i * ch + c] = (short)(s * 32767.0f);
				}
			av_frame_unref(fr);
		}
	};
	while (av_read_frame(v->fmt, pkt) >= 0)
	{
		if (pkt->stream_index == stream && avcodec_send_packet(ctx, pkt) == 0)
			drain();
		av_packet_unref(pkt);
	}
	avcodec_send_packet(ctx, NULL);
	drain();
	av_frame_free(&fr);
	av_packet_free(&pkt);
	avcodec_free_context(&ctx);
	return !pcm.empty();
}

void MakeSound(Video *v)
{
	if (!gSound)
	{
		PortSessionLog(PORT_SESSION_FILM, false, "film=%u no DirectSound device (sound disabled or unavailable)", v->sessionId);
		return;
	}
	int audioStreams = 0, stream = -1;
	for (unsigned i = 0; i < v->fmt->nb_streams; i++)
		if (v->fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			if ((U32)audioStreams == (gTrackSet ? gTrack : 0) || stream < 0)
				stream = (int)i;
			audioStreams++;
		}
	if (stream < 0)
	{
		PortSessionLog(PORT_SESSION_FILM, false, "film=%u asset=%s has no audio track", v->sessionId, v->sessionAsset);
		return;
	}
	std::vector<short> pcm;
	int rate = 0, channels = 0;
	if (!DecodeAudio(v, stream, pcm, &rate, &channels) || channels < 1 || channels > 2)
	{
		PortSessionLog(PORT_SESSION_FILM, true, "film=%u sound decode/format failed rate=%d channels=%d", v->sessionId, rate, channels);
		return;
	}
	WAVEFORMATEX wfx;
	memset(&wfx, 0, sizeof(wfx));
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = (WORD)channels;
	wfx.nSamplesPerSec = rate;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = (WORD)(2 * channels);
	wfx.nAvgBytesPerSec = rate * wfx.nBlockAlign;
	DSBUFFERDESC d;
	memset(&d, 0, sizeof(d));
	d.dwSize = sizeof(d);
	d.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCSOFTWARE;
	d.dwBufferBytes = (DWORD)(pcm.size() * 2);
	d.lpwfxFormat = &wfx;
	IDirectSoundBuffer *b = NULL;
	HRESULT create = gSound->CreateSoundBuffer(&d, &b, NULL);
	if (FAILED(create) || !b)
	{
		PortSessionLog(PORT_SESSION_FILM, true, "film=%u sound buffer failed result=%08lx", v->sessionId, (unsigned long)create);
		return;
	}
	void *p1, *p2;
	DWORD n1, n2;
	HRESULT fill = b->Lock(0, 0, &p1, &n1, &p2, &n2, DSBLOCK_ENTIREBUFFER);
	if (SUCCEEDED(fill))
	{
		memcpy(p1, pcm.data(), n1);
		b->Unlock(p1, n1, p2, n2);
	}
	v->sound = b;
	if (PortSessionEnabled() && !PortAudioMediaRole(b, PORT_SESSION_FILM))
		PortSessionLog(PORT_SESSION_FILM, true, "film=%u media role assignment refused", v->sessionId);
	v->soundRate = rate;
	v->soundBlock = wfx.nBlockAlign;
	PortSessionLog(PORT_SESSION_FILM, FAILED(fill), "film=%u sound buffer created fill_result=%08lx rate=%d channels=%d bits=16 decoded_bytes=%lu",
		v->sessionId, (unsigned long)fill, rate, channels, (unsigned long)d.dwBufferBytes);
}

void Free(Video *v)
{
	if (v->sound)
	{
		HRESULT stopped = v->sound->Stop();
		ULONG remaining = v->sound->Release();
		PortSessionLog(PORT_SESSION_FILM, FAILED(stopped), "film=%u sound stop=%08lx release_remaining=%lu",
			v->sessionId, (unsigned long)stopped, (unsigned long)remaining);
		if (SUCCEEDED(stopped) || !remaining) PortSessionMediaActive(PORT_SESSION_FILM, v, false);
	}
	for (AVPacket *p : v->packets)
		av_packet_free(&p);
	av_frame_free(&v->frame);
	avcodec_free_context(&v->dec);
	if (v->fmt)
	{
		AVIOContext *io = v->fmt->pb;
		avformat_close_input(&v->fmt);
		if (io)
		{
			av_freep(&io->buffer);
			avio_context_free(&io);
		}
	}
	v->~Video();
	free(v);
}

// Decodes frame n (1-based), restarting the decoder when playback wrapped round.
bool DecodeFrame(Video *v, U32 n)
{
	if (n < 1 || n > v->packets.size())
		return false;
	if (n <= v->decoded)
	{
		avcodec_flush_buffers(v->dec);
		v->decoded = 0;
	}
	while (v->decoded < n)
	{
		if (avcodec_send_packet(v->dec, v->packets[v->decoded]) < 0)
			return false;
		v->decoded++;
		if (avcodec_receive_frame(v->dec, v->frame) < 0 && v->decoded == n)
			return false;
	}
	return true;
}

inline int Clamp(int x) { return x < 0 ? 0 : x > 255 ? 255 : x; }

// BEA_BINK_LOG=file: the game's Bink calls in the form of the retail replay shim's log ("D <present> <seconds> Bink.<Call>"),
// so which video frame the game copies at each Present can be compared with retail's (A/V sync).
FILE *gBinkLog;
bool gBinkLogChecked;
bool BinkLogging()
{
	if (!gBinkLogChecked)
	{
		gBinkLogChecked = true;
		const char *path = getenv("BEA_BINK_LOG");
		if (path && path[0])
			gBinkLog = fopen(path, "w");
	}
	return gBinkLog != NULL;
}
void BinkLog(const char *fmt, ...)
{
	if (!BinkLogging())
		return;
	unsigned long long f = PortClockFrames();
	fprintf(gBinkLog, "D %llu %d.%03d ", f, (int)(f / 30), (int)(f % 30 * 1000 / 30));
	va_list a;
	va_start(a, fmt);
	vfprintf(gBinkLog, fmt, a);
	va_end(a);
	fputc('\n', gBinkLog);
	fflush(gBinkLog);
}

// Retail binkw32's colour conversion, measured against the install's binkw32.dll (tests/retail_compare.sh, README):
// nearest-neighbour chroma and one table per term, each a 16.16 product truncated toward zero, summed and clamped;
// luma below 16 is 0. The data pins the blue constant only to 132202..132221; 132219 (2.0175) fits.
struct Tables
{
	int y[256], rv[256], gu[256], gv[256], bu[256];
	Tables()
	{
		for (int i = 0; i < 256; i++)
		{
			y[i] = i < 16 ? 0 : ((i - 16) * 76309) >> 16;
			rv[i] = Trunc16(104597 * (i - 128));
			gu[i] = Trunc16(-25675 * (i - 128));
			gv[i] = Trunc16(-53279 * (i - 128));
			bu[i] = Trunc16(132219 * (i - 128));
		}
	}
	static int Trunc16(int product) { return product >= 0 ? product >> 16 : -((-product) >> 16); }
};
const Tables gTables;
} // namespace

extern "C"
{
void __stdcall RADSetMemory(RADMEMALLOC, RADMEMFREE) {}

void *__stdcall BinkOpenDirectSound(U32 param)
{
	gSound = (IDirectSound8 *)param;
	return (void *)1;
}

S32 __stdcall BinkSetSoundSystem(BINKSNDSYSOPEN open, U32 param)
{
	if (open)
		open(param);
	return 1;
}

void __stdcall BinkSetSoundTrack(U32 total, U32 *tracks)
{
	gTrackSet = total > 0 && tracks;
	gTrack = gTrackSet ? tracks[0] : 0;
}

HBINK __stdcall BinkOpen(const char *name, U32 flags)
{
	(void)flags;	// every file is read whole (BINKPRELOADALL's effect) and the sound track is always taken
	Video *v = new (malloc(sizeof(Video))) Video;
	if (PortSessionEnabled())
	{
		v->sessionId = __atomic_add_fetch(&gFilmSerial, 1u, __ATOMIC_RELAXED);
		const char *base = name;
		for (const char *p = name; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
		snprintf(v->sessionAsset, sizeof v->sessionAsset, "%.120s", base);
	}
	PortSessionLog(PORT_SESSION_FILM, false, "film=%u open requested asset=%s", v->sessionId, v->sessionAsset);
	if (!ReadFileAll(name, v->file))
	{
		PortSessionLog(PORT_SESSION_FILM, true, "film=%u open failed: file unavailable", v->sessionId);
		v->~Video();
		free(v);
		return NULL;
	}
	v->mem.data = v->file.data();
	v->mem.size = (int64_t)v->file.size();
	v->mem.pos = 0;
	const int iosize = 65536;
	unsigned char *iobuf = (unsigned char *)av_malloc(iosize);
	AVIOContext *io = avio_alloc_context(iobuf, iosize, 0, &v->mem, ReadMem, NULL, SeekMem);
	v->fmt = avformat_alloc_context();
	v->fmt->pb = io;
	if (avformat_open_input(&v->fmt, NULL, av_find_input_format("bink"), NULL) < 0 ||
		avformat_find_stream_info(v->fmt, NULL) < 0)
	{
		PortSessionLog(PORT_SESSION_FILM, true, "film=%u container open failed", v->sessionId);
		if (!v->fmt)
		{
			av_freep(&io->buffer);
			avio_context_free(&io);
		}
		Free(v);
		return NULL;
	}
	v->videoStream = av_find_best_stream(v->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (v->videoStream < 0)
	{
		PortSessionLog(PORT_SESSION_FILM, true, "film=%u no video stream", v->sessionId);
		Free(v);
		return NULL;
	}
	AVStream *st = v->fmt->streams[v->videoStream];
	const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
	v->dec = codec ? avcodec_alloc_context3(codec) : NULL;
	if (!v->dec || avcodec_parameters_to_context(v->dec, st->codecpar) < 0 || avcodec_open2(v->dec, codec, NULL) < 0)
	{
		PortSessionLog(PORT_SESSION_FILM, true, "film=%u video decoder open failed", v->sessionId);
		Free(v);
		return NULL;
	}
	AVRational r = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
	if (r.num > 0 && r.den > 0)
		v->fps = (double)r.num / r.den;
	AVPacket *pkt = av_packet_alloc();
	while (av_read_frame(v->fmt, pkt) >= 0)
	{
		if (pkt->stream_index == v->videoStream)
		{
			v->packets.push_back(pkt);
			pkt = av_packet_alloc();
		}
		else
			av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
	v->frame = av_frame_alloc();
	MakeSound(v);
	v->pub.Width = st->codecpar->width;
	v->pub.Height = st->codecpar->height;
	v->pub.Frames = (U32)v->packets.size();
	v->pub.FrameNum = 1;
	PortSessionLog(PORT_SESSION_FILM, false, "film=%u opened size=%lux%lu frames=%lu sound=%d", v->sessionId,
		(unsigned long)v->pub.Width, (unsigned long)v->pub.Height, (unsigned long)v->pub.Frames, v->sound ? 1 : 0);
	BinkLog("Bink.Open bink=%08lX frames=%lu size=%lux%lu fps=%d sound=%d name=%s", (unsigned long)(uintptr_t)&v->pub,
			(unsigned long)v->pub.Frames, (unsigned long)v->pub.Width, (unsigned long)v->pub.Height, (int)(v->fps * 1000 + 0.5),
			v->sound ? 1 : 0, name);
	return &v->pub;
}

void __stdcall BinkSetVolume(HBINK bink, U32, S32 volume)
{
	// RAD's scale: 32768 is the recording's level, 0 silence; as DirectSound hundredths of a decibel
	Video *v = (Video *)bink;
	if (!v)
		return;
	LONG db = volume <= 0 ? DSBVOLUME_MIN : (LONG)(2000.0 * log10((double)volume / 32768.0));
	v->volume = db < DSBVOLUME_MIN ? DSBVOLUME_MIN : db > DSBVOLUME_MAX ? DSBVOLUME_MAX : db;
	if (v->sound)
		v->sound->SetVolume(v->volume);
}

S32 __stdcall BinkDoFrame(HBINK bink)
{
	Video *v = (Video *)bink;
	if (!v)
		return 0;
	if (!v->started)
	{
		v->started = true;
		QueryPerformanceCounter(&v->start);
		if (v->sound)
		{
			v->sound->SetCurrentPosition(0);
			HRESULT played = v->sound->Play(0, 0, 0);
			PortSessionLog(PORT_SESSION_FILM, FAILED(played), "film=%u first audio play result=%08lx", v->sessionId, (unsigned long)played);
			if (SUCCEEDED(played)) PortSessionMediaActive(PORT_SESSION_FILM, v, true);
		}
	}
	if (DecodeFrame(v, v->pub.FrameNum))
		v->played++;
	return 0;
}

void __stdcall BinkNextFrame(HBINK bink)
{
	Video *v = (Video *)bink;
	if (!v || !v->pub.Frames)
		return;
	v->pub.FrameNum = v->pub.FrameNum >= v->pub.Frames ? 1 : v->pub.FrameNum + 1;
}

S32 __stdcall BinkWait(HBINK bink)
{
	// The frame waiting to be shown is the one decoded last (FrameNum has moved past it); it is due (n - 1) / fps
	// after the start.
	Video *v = (Video *)bink;
	if (!v || !v->started)
		return 0;
	// Under the shim clock time stands still until the next Present, which the game makes only once a frame is due: every
	// frame is due, one per Present, as retail plays its films under the shim.
	if (PortClockStandsBetweenPresents())
		return 0;
	U32 waiting = v->pub.FrameNum > 1 ? v->pub.FrameNum - 1 : v->pub.Frames;
	return Clock(v) < (waiting - 1) / v->fps ? 1 : 0;
}

S32 __stdcall BinkCopyToBuffer(HBINK bink, void *dest, S32 pitch, U32 height, U32 x, U32 y, U32 flags)
{
	Video *v = (Video *)bink;
	if (v && BinkLogging())
	{
		// the frame copied is the one decoded last: FrameNum has already moved past it
		DWORD play = 0;
		if (v->sound)
			v->sound->GetCurrentPosition(&play, NULL);
		BinkLog("Bink.CopyToBuffer bink=%08lX frame=%lu sound=%lu flags=%lx", (unsigned long)(uintptr_t)bink,
				(unsigned long)(v->pub.FrameNum > 1 ? v->pub.FrameNum - 1 : v->pub.Frames), (unsigned long)play,
				(unsigned long)flags);
	}
	if (!v || !v->decoded || !v->frame->data[0])
		return 0;
	const AVFrame *f = v->frame;
	U32 surface = flags & 0xf;
	const unsigned char *alpha = (f->format == AV_PIX_FMT_YUVA420P) ? f->data[3] : NULL;
	U32 w = v->pub.Width, h = v->pub.Height < height ? v->pub.Height : height;
	for (U32 row = 0; row < h; row++)
	{
		const unsigned char *Y = f->data[0] + row * f->linesize[0];
		const unsigned char *U = f->data[1] + (row / 2) * f->linesize[1];
		const unsigned char *V = f->data[2] + (row / 2) * f->linesize[2];
		const unsigned char *A = alpha ? alpha + row * f->linesize[3] : NULL;
		unsigned char *out = (unsigned char *)dest + (row + y) * pitch;
		for (U32 col = 0; col < w; col++)
		{
			int yy = Y[col], u = U[col / 2], vv = V[col / 2];
			int r = gTables.y[yy] + gTables.rv[vv];
			int g = gTables.y[yy] + gTables.gu[u] + gTables.gv[vv];
			int b = gTables.y[yy] + gTables.bu[u];
			r = Clamp(r);
			g = Clamp(g);
			b = Clamp(b);
			if (surface == kSurface565)
			{
				unsigned short p = (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
				memcpy(out + (x + col) * 2, &p, 2);
			}
			else
			{
				unsigned char *p = out + (x + col) * 4;
				p[0] = (unsigned char)b;
				p[1] = (unsigned char)g;
				p[2] = (unsigned char)r;
				p[3] = A ? A[col] : 0;	// retail writes 0 where a video has no alpha plane
			}
		}
	}
	return 0;
}

void __stdcall BinkGetSummary(HBINK bink, BINKSUMMARY *s)
{
	memset(s, 0, sizeof(*s));
	Video *v = (Video *)bink;
	if (v)
		s->TotalPlayedFrames = v->played;
}

void __stdcall BinkClose(HBINK bink)
{
	BinkLog("Bink.Close bink=%08lX", (unsigned long)(uintptr_t)bink);
	if (bink)
	{
		Video *v = (Video *)bink;
		PortSessionLog(PORT_SESSION_FILM, false, "film=%u close asset=%s frame=%lu/%lu played=%lu", v->sessionId,
			v->sessionAsset, (unsigned long)v->pub.FrameNum, (unsigned long)v->pub.Frames, (unsigned long)v->played);
		Free((Video *)bink);
	}
}
}
