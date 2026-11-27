// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_bugclip.cpp: built-in F9 bug-clip recorder.
// Writes a self-contained RIFF AVI 1.0 file (Baseline MJPEG 4:2:0 video at 20 FPS + 44.1 kHz 16-bit stereo PCM audio)
// and a companion scrubbed diagnostic snapshot (.txt with version, OS, ce.ini, and recent onslaught-ce.log tail),
// then packages both into a single GitHub-ready ZIP archive (STORE + IEEE CRC32) at
// <user folder>/clips/onslaught-ce-clip-YYYYMMDD-HHMMSS[-N].zip (removing loose temp files on success).
//
// Design constraints:
// - Zero external codec/archive dependencies; 100% clean-room Baseline JPEG + RIFF AVI 1.0 + ZIP in standard C++17.
// - Audio callback path (PortCeBugClipPushAudio) is strictly lock-free, allocation-free, and syscall-free.
// - Video encoding, ZIP packaging, and disk I/O run strictly on a single worker thread; Present() only copies pixels
//   when a slot is free.
// - Bounded: max 60 seconds, max 64 MiB per clip, max 128 files in <user>/clips/; never follows symlinks/reparse points
//   and never overwrites an existing file (ce_settings_subdir_open_ex + O_EXCL / CREATE_NEW).
// - Never reads online-profile.key, online-profile.ini, or savegames; scrubs user account paths in the .txt bundle.
#ifdef CE_VERSION

#include "ce_bugclip.h"
#include "ce_win_reparse.h"
#include "portlog.h"
#include "../../updater/ce_report.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

bool PortCeLayer();
extern "C" const char *CE_BuildName(void);

namespace
{
// Direct3D 9 D3DFORMAT values (matching ce_screenshot.cpp).
const unsigned kA8R8G8B8 = 21, kX8R8G8B8 = 22, kR5G6B5 = 23, kX1R5G5B5 = 24, kA1R5G5B5 = 25;

// Bounded clip parameters (compile-time overridable for standalone unit tests only).
#ifndef CE_BUGCLIP_MAX_SECONDS
#define CE_BUGCLIP_MAX_SECONDS 60u
#endif
#ifndef CE_BUGCLIP_MAX_BYTES
#define CE_BUGCLIP_MAX_BYTES (64ull << 20)
#endif
const unsigned kClipFps = 20;
const uint64_t kFramePeriodNs = 1000000000ull / kClipFps;	// 50 ms
const unsigned kMaxClipSeconds = CE_BUGCLIP_MAX_SECONDS;
const unsigned kMaxClipFrames = kClipFps * kMaxClipSeconds;
const uint64_t kMaxClipBytes = CE_BUGCLIP_MAX_BYTES;		// 64 MiB default
const unsigned kMaxClipWidth = 854;							// 16:9 480p friendly (1920x1080 -> 848x480 after mod-16)
const unsigned kMaxClipHeight = 480;
const int kMaxClipFiles = 128;								// bounded entry count in clips/
const unsigned kAudioRate = 44100;
const unsigned kAudioChannels = 2;
const unsigned kAudioBlockAlign = 4;						// 2ch * 16-bit
const unsigned kFrameQueueSlots = 3;
const uint64_t kToastMs = 4000;

// Lock-free SPSC audio ring buffer (power-of-two size in stereo frames: 131072 frames ~= 2.97 s @ 44.1 kHz).
const uint32_t kAudioRingFrames = 131072u;
const uint32_t kAudioRingMask = kAudioRingFrames - 1u;
int16_t gAudioRing[kAudioRingFrames * 2u];
uint32_t gAudioHead = 0;	// written only by audio producer
uint32_t gAudioTail = 0;	// written only by worker consumer
int gAudioActive = 0;		// atomic flag read by audio callback

// ---- Clean-room Baseline JPEG (YCbCr 4:2:0, Annex K tables, quality 80) -----------------------------------------

const uint8_t kZigZag[64] = {
	 0,  1,  8, 16,  9,  2,  3, 10,
	17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63
};

const uint8_t kBaseLumaQ[64] = {
	16, 11, 10, 16, 24,  40,  51,  61,
	12, 12, 14, 19, 26,  58,  60,  55,
	14, 13, 16, 24, 40,  57,  69,  56,
	14, 17, 22, 29, 51,  87,  80,  62,
	18, 22, 37, 56, 68, 109, 103,  77,
	24, 35, 55, 64, 81, 104, 113,  92,
	49, 64, 78, 87, 103, 121, 120, 101,
	72, 92, 95, 98, 112, 100, 103,  99
};

const uint8_t kBaseChromaQ[64] = {
	17, 18, 24, 47, 99, 99, 99, 99,
	18, 21, 26, 66, 99, 99, 99, 99,
	24, 26, 56, 99, 99, 99, 99, 99,
	47, 66, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99
};

// ITU-T T.81 Annex K standard Huffman tables.
const uint8_t kDcLumaBits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
const uint8_t kDcLumaVals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

const uint8_t kDcChromaBits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
const uint8_t kDcChromaVals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

const uint8_t kAcLumaBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 125};
const uint8_t kAcLumaVals[162] = {
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
	0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
	0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa
};

const uint8_t kAcChromaBits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 119};
const uint8_t kAcChromaVals[162] = {
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
	0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
	0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
	0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
	0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
	0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
	0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa
};

struct HuffCode { uint16_t code; uint8_t len; };

struct JpegTables
{
	bool ready = false;
	float cosT[8][8];
	uint8_t qLuma[64];
	uint8_t qChroma[64];
	float invQLuma[64];
	float invQChroma[64];
	HuffCode dcLuma[16];
	HuffCode dcChroma[16];
	HuffCode acLuma[256];
	HuffCode acChroma[256];

	static void BuildHuff(const uint8_t bits[16], const uint8_t *vals, HuffCode *out, size_t outCount)
	{
		memset(out, 0, sizeof(HuffCode) * outCount);
		uint32_t code = 0;
		size_t idx = 0;
		for (int len = 1; len <= 16; ++len)
		{
			for (int k = 0; k < bits[len - 1]; ++k)
			{
				uint8_t sym = vals[idx++];
				if (sym < outCount)
				{
					out[sym].code = (uint16_t)code;
					out[sym].len = (uint8_t)len;
				}
				++code;
			}
			code <<= 1;
		}
	}

	void Init(int quality)
	{
		if (ready) return;
		const double kPi = 3.14159265358979323846;
		const double kInvSqrt2 = 0.70710678118654752440;
		for (int u = 0; u < 8; ++u)
		{
			const double au = (u == 0) ? kInvSqrt2 : 1.0;
			for (int x = 0; x < 8; ++x)
				cosT[u][x] = (float)(0.5 * au * std::cos((2.0 * x + 1.0) * u * kPi / 16.0));
		}
		const int q = std::max(1, std::min(100, quality));
		const int scale = (q < 50) ? (5000 / q) : (200 - 2 * q);
		for (int i = 0; i < 64; ++i)
		{
			int yl = (kBaseLumaQ[i] * scale + 50) / 100;
			int yc = (kBaseChromaQ[i] * scale + 50) / 100;
			qLuma[i] = (uint8_t)std::max(1, std::min(255, yl));
			qChroma[i] = (uint8_t)std::max(1, std::min(255, yc));
			invQLuma[i] = 1.0f / (float)qLuma[i];
			invQChroma[i] = 1.0f / (float)qChroma[i];
		}
		BuildHuff(kDcLumaBits, kDcLumaVals, dcLuma, 16);
		BuildHuff(kDcChromaBits, kDcChromaVals, dcChroma, 16);
		BuildHuff(kAcLumaBits, kAcLumaVals, acLuma, 256);
		BuildHuff(kAcChromaBits, kAcChromaVals, acChroma, 256);
		ready = true;
	}
};

struct BitWriter
{
	std::vector<uint8_t> *buf;
	uint32_t acc = 0;
	int bits = 0;

	void PutByte(uint8_t b) { buf->push_back(b); }
	void PutU16BE(uint16_t v) { buf->push_back((uint8_t)(v >> 8)); buf->push_back((uint8_t)(v & 0xFF)); }

	void PutBits(uint32_t value, int count)
	{
		if (count <= 0 || count > 16) return;
		acc = (acc << count) | (value & ((1u << count) - 1u));
		bits += count;
		while (bits >= 8)
		{
			bits -= 8;
			uint8_t out = (uint8_t)((acc >> bits) & 0xFFu);
			buf->push_back(out);
			if (out == 0xFFu)
				buf->push_back(0x00u);
		}
	}

	void FlushBits()
	{
		if (bits > 0)
			PutBits((1u << (8 - bits)) - 1u, 8 - bits);
	}
};

void WriteDht(BitWriter &w, uint8_t tcTh, const uint8_t bits[16], const uint8_t *vals, size_t valCount)
{
	w.PutU16BE(0xFFC4);
	w.PutU16BE((uint16_t)(2 + 1 + 16 + valCount));
	w.PutByte(tcTh);
	for (int i = 0; i < 16; ++i) w.PutByte(bits[i]);
	for (size_t i = 0; i < valCount; ++i) w.PutByte(vals[i]);
}

void EncodeBlock(const float block[64], const JpegTables &t, const float invQ[64],
                 const HuffCode *dcTab, const HuffCode *acTab, int &prevDc, BitWriter &w)
{
	float tmp[64];
	for (int y = 0; y < 8; ++y)
	{
		const float *row = block + y * 8;
		float *dst = tmp + y * 8;
		for (int u = 0; u < 8; ++u)
		{
			const float *c = t.cosT[u];
			dst[u] = row[0] * c[0] + row[1] * c[1] + row[2] * c[2] + row[3] * c[3] +
			         row[4] * c[4] + row[5] * c[5] + row[6] * c[6] + row[7] * c[7];
		}
	}
	int qcoef[64];
	for (int v = 0; v < 8; ++v)
	{
		const float *c = t.cosT[v];
		for (int u = 0; u < 8; ++u)
		{
			const float sum = tmp[0 * 8 + u] * c[0] + tmp[1 * 8 + u] * c[1] + tmp[2 * 8 + u] * c[2] + tmp[3 * 8 + u] * c[3] +
			                  tmp[4 * 8 + u] * c[4] + tmp[5 * 8 + u] * c[5] + tmp[6 * 8 + u] * c[6] + tmp[7 * 8 + u] * c[7];
			const int idx = v * 8 + u;
			const float scaled = sum * invQ[idx];
			qcoef[idx] = (int)(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));
		}
	}
	auto emitVal = [&](int val, int maxCat, int &cat, uint32_t &bits) {
		const int maxMag = (1 << maxCat) - 1;
		if (val > maxMag) val = maxMag;
		else if (val < -maxMag) val = -maxMag;
		int a = val < 0 ? -val : val;
		cat = 0;
		while (a > 0) { ++cat; a >>= 1; }
		bits = (uint32_t)(val < 0 ? (val + (1 << cat) - 1) : val);
	};

	// Clamp DC into [-1023, 1023] so diff stays in [-2046, 2046] (category <= 11, standard Annex K DC table).
	const int dc = std::max(-1023, std::min(1023, qcoef[0]));
	const int diff = dc - prevDc;
	prevDc = dc;
	if (diff == 0)
	{
		w.PutBits(dcTab[0].code, dcTab[0].len);
	}
	else
	{
		int cat = 0;
		uint32_t bits = 0;
		emitVal(diff, 11, cat, bits);
		w.PutBits(dcTab[cat].code, dcTab[cat].len);
		w.PutBits(bits, cat);
	}

	int lastNz = 63;
	while (lastNz > 0 && qcoef[kZigZag[lastNz]] == 0) --lastNz;
	int zeroRun = 0;
	for (int i = 1; i <= lastNz; ++i)
	{
		// Clamp AC into [-1023, 1023] (category 1..10, guaranteed present in Annex K AC Huffman tables).
		const int val = std::max(-1023, std::min(1023, qcoef[kZigZag[i]]));
		if (val == 0)
		{
			++zeroRun;
			if (zeroRun == 16)
			{
				w.PutBits(acTab[0xF0].code, acTab[0xF0].len);
				zeroRun = 0;
			}
			continue;
		}
		int cat = 0;
		uint32_t bits = 0;
		emitVal(val, 10, cat, bits);
		const uint8_t sym = (uint8_t)((zeroRun << 4) | cat);
		w.PutBits(acTab[sym].code, acTab[sym].len);
		w.PutBits(bits, cat);
		zeroRun = 0;
	}
	if (lastNz < 63)
		w.PutBits(acTab[0x00].code, acTab[0x00].len);	// EOB
}

void EncodeJpegRgb24(const uint8_t *rgb, int width, int height, const JpegTables &t, std::vector<uint8_t> &out)
{
	out.clear();
	out.reserve((size_t)width * (size_t)height / 3 + 4096);
	BitWriter w{&out};

	// SOI + JFIF APP0
	w.PutU16BE(0xFFD8);
	w.PutU16BE(0xFFE0);
	w.PutU16BE(16);
	w.PutByte('J'); w.PutByte('F'); w.PutByte('I'); w.PutByte('F'); w.PutByte(0);
	w.PutByte(1); w.PutByte(1); w.PutByte(0);
	w.PutU16BE(1); w.PutU16BE(1);
	w.PutByte(0); w.PutByte(0);

	// DQT 0 (Luma) & DQT 1 (Chroma), in zigzag order
	w.PutU16BE(0xFFDB);
	w.PutU16BE(67);
	w.PutByte(0);
	for (int i = 0; i < 64; ++i) w.PutByte(t.qLuma[kZigZag[i]]);
	w.PutU16BE(0xFFDB);
	w.PutU16BE(67);
	w.PutByte(1);
	for (int i = 0; i < 64; ++i) w.PutByte(t.qChroma[kZigZag[i]]);

	// SOF0: 8-bit baseline, 3 components Y(2x2), Cb(1x1), Cr(1x1)
	w.PutU16BE(0xFFC0);
	w.PutU16BE(17);
	w.PutByte(8);
	w.PutU16BE((uint16_t)height);
	w.PutU16BE((uint16_t)width);
	w.PutByte(3);
	w.PutByte(1); w.PutByte(0x22); w.PutByte(0);
	w.PutByte(2); w.PutByte(0x11); w.PutByte(1);
	w.PutByte(3); w.PutByte(0x11); w.PutByte(1);

	// DHTs
	WriteDht(w, 0x00, kDcLumaBits, kDcLumaVals, sizeof(kDcLumaVals));
	WriteDht(w, 0x10, kAcLumaBits, kAcLumaVals, sizeof(kAcLumaVals));
	WriteDht(w, 0x01, kDcChromaBits, kDcChromaVals, sizeof(kDcChromaVals));
	WriteDht(w, 0x11, kAcChromaBits, kAcChromaVals, sizeof(kAcChromaVals));

	// SOS
	w.PutU16BE(0xFFDA);
	w.PutU16BE(12);
	w.PutByte(3);
	w.PutByte(1); w.PutByte(0x00);
	w.PutByte(2); w.PutByte(0x11);
	w.PutByte(3); w.PutByte(0x11);
	w.PutByte(0); w.PutByte(63); w.PutByte(0);

	int prevY = 0, prevCb = 0, prevCr = 0;
	float yBlk[4][64], cbBlk[64], crBlk[64];
	for (int my = 0; my < height; my += 16)
	{
		for (int mx = 0; mx < width; mx += 16)
		{
			for (int by = 0; by < 8; ++by)
			{
				for (int bx = 0; bx < 8; ++bx)
				{
					float cbSum = 0.0f, crSum = 0.0f;
					for (int dy = 0; dy < 2; ++dy)
					{
						const int py = std::min(height - 1, my + by * 2 + dy);
						const uint8_t *row = rgb + (size_t)py * (size_t)width * 3u;
						for (int dx = 0; dx < 2; ++dx)
						{
							const int px = std::min(width - 1, mx + bx * 2 + dx);
							const uint8_t *p = row + (size_t)px * 3u;
							const float r = (float)p[0], g = (float)p[1], b = (float)p[2];
							const float yv = 0.299f * r + 0.587f * g + 0.114f * b - 128.0f;
							cbSum += -0.168736f * r - 0.331264f * g + 0.5f * b;
							crSum +=  0.5f * r - 0.418688f * g - 0.081312f * b;
							const int lx = bx * 2 + dx;
							const int ly = by * 2 + dy;
							const int sub = ((ly >> 3) << 1) | (lx >> 3);
							yBlk[sub][(ly & 7) * 8 + (lx & 7)] = yv;
						}
					}
					cbBlk[by * 8 + bx] = cbSum * 0.25f;
					crBlk[by * 8 + bx] = crSum * 0.25f;
				}
			}
			for (int sub = 0; sub < 4; ++sub)
				EncodeBlock(yBlk[sub], t, t.invQLuma, t.dcLuma, t.acLuma, prevY, w);
			EncodeBlock(cbBlk, t, t.invQChroma, t.dcChroma, t.acChroma, prevCb, w);
			EncodeBlock(crBlk, t, t.invQChroma, t.dcChroma, t.acChroma, prevCr, w);
		}
	}
	w.FlushBits();
	w.PutU16BE(0xFFD9);	// EOI
}

// ---- Clean-room RIFF AVI 1.0 Writer (MJPG + 44.1 kHz 16-bit stereo PCM) -----------------------------------------

struct AviIdxEntry { char id[4]; uint32_t flags; uint32_t offset; uint32_t size; };

struct AviWriter
{
	FILE *f = NULL;
	int width = 0, height = 0;
	uint64_t bytesWritten = 0;
	uint32_t totalFrames = 0;
	uint32_t totalAudioFrames = 0;
	uint32_t maxVideoChunk = 0;
	long riffSizePos = 0;
	long avihTotalFramesPos = 0;
	long avihSuggestedPos = 0;
	long strhVLenPos = 0;
	long strhVSuggestedPos = 0;
	long strhALenPos = 0;
	long moviSizePos = 0;
	long moviFourccPos = 0;
	std::vector<AviIdxEntry> idx;
	bool failed = false;

	void WriteRaw(const void *data, size_t n)
	{
		if (failed || !f) return;
		if (fwrite(data, 1, n, f) != n) { failed = true; return; }
		bytesWritten += n;
	}
	void WriteFourCC(const char cc[4]) { WriteRaw(cc, 4); }
	void WriteU16(uint16_t v)
	{
		uint8_t b[2] = {(uint8_t)(v & 0xFF), (uint8_t)(v >> 8)};
		WriteRaw(b, 2);
	}
	void WriteU32(uint32_t v)
	{
		uint8_t b[4] = {(uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF), (uint8_t)((v >> 16) & 0xFF), (uint8_t)(v >> 24)};
		WriteRaw(b, 4);
	}
	void PatchU32(long pos, uint32_t v)
	{
		if (failed || !f) return;
		if (fseek(f, pos, SEEK_SET) != 0) { failed = true; return; }
		uint8_t b[4] = {(uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF), (uint8_t)((v >> 16) & 0xFF), (uint8_t)(v >> 24)};
		if (fwrite(b, 1, 4, f) != 4) failed = true;
	}

	bool Begin(FILE *stream, int w, int h)
	{
		f = stream; width = w; height = h;
		bytesWritten = 0; totalFrames = 0; totalAudioFrames = 0; maxVideoChunk = 0;
		idx.clear(); idx.reserve(kMaxClipFrames * 2u);
		failed = false;

		WriteFourCC("RIFF");
		riffSizePos = ftell(f);
		WriteU32(0);
		WriteFourCC("AVI ");

		// LIST hdrl (294 bytes = 4 + 64 + 124 + 102)
		WriteFourCC("LIST"); WriteU32(294); WriteFourCC("hdrl");

		// avih (56 bytes)
		WriteFourCC("avih"); WriteU32(56);
		WriteU32(1000000u / kClipFps);		// dwMicroSecPerFrame (50000)
		WriteU32(2000000u);					// dwMaxBytesPerSec
		WriteU32(0);						// dwPaddingGranularity
		WriteU32(0x00000110u);				// AVIF_HASINDEX | AVIF_ISINTERLEAVED
		avihTotalFramesPos = ftell(f);
		WriteU32(0);						// dwTotalFrames
		WriteU32(0);						// dwInitialFrames
		WriteU32(2);						// dwStreams (video + audio)
		avihSuggestedPos = ftell(f);
		WriteU32((uint32_t)w * (uint32_t)h * 3u);	// dwSuggestedBufferSize
		WriteU32((uint32_t)w);				// dwWidth
		WriteU32((uint32_t)h);				// dwHeight
		WriteU32(0); WriteU32(0); WriteU32(0); WriteU32(0);

		// LIST strl (video: 116 bytes = 4 + 64 + 48)
		WriteFourCC("LIST"); WriteU32(116); WriteFourCC("strl");
		WriteFourCC("strh"); WriteU32(56);
		WriteFourCC("vids"); WriteFourCC("MJPG");
		WriteU32(0);						// dwFlags
		WriteU16(0); WriteU16(0);			// wPriority, wLanguage
		WriteU32(0);						// dwInitialFrames
		WriteU32(1);						// dwScale
		WriteU32(kClipFps);					// dwRate
		WriteU32(0);						// dwStart
		strhVLenPos = ftell(f);
		WriteU32(0);						// dwLength
		strhVSuggestedPos = ftell(f);
		WriteU32((uint32_t)w * (uint32_t)h * 3u);
		WriteU32(0xFFFFFFFFu);				// dwQuality (default)
		WriteU32(0);						// dwSampleSize (variable)
		WriteU16(0); WriteU16(0); WriteU16((uint16_t)w); WriteU16((uint16_t)h);

		WriteFourCC("strf"); WriteU32(40);
		WriteU32(40);						// biSize
		WriteU32((uint32_t)w);				// biWidth
		WriteU32((uint32_t)h);				// biHeight
		WriteU16(1);						// biPlanes
		WriteU16(24);						// biBitCount
		WriteFourCC("MJPG");				// biCompression
		WriteU32((uint32_t)w * (uint32_t)h * 3u);	// biSizeImage
		WriteU32(0); WriteU32(0); WriteU32(0); WriteU32(0);

		// LIST strl (audio: 94 bytes = 4 + 64 + 26)
		WriteFourCC("LIST"); WriteU32(94); WriteFourCC("strl");
		WriteFourCC("strh"); WriteU32(56);
		WriteFourCC("auds"); WriteU32(0);
		WriteU32(0);
		WriteU16(0); WriteU16(0);
		WriteU32(0);
		WriteU32(1);						// dwScale = 1 sample
		WriteU32(kAudioRate);				// dwRate = 44100 samples/sec
		WriteU32(0);
		strhALenPos = ftell(f);
		WriteU32(0);						// dwLength (total samples)
		WriteU32(kAudioRate * kAudioBlockAlign);
		WriteU32(0xFFFFFFFFu);
		WriteU32(kAudioBlockAlign);			// dwSampleSize = 4 bytes/sample
		WriteU16(0); WriteU16(0); WriteU16(0); WriteU16(0);

		WriteFourCC("strf"); WriteU32(18);
		WriteU16(1);						// WAVE_FORMAT_PCM
		WriteU16((uint16_t)kAudioChannels);
		WriteU32(kAudioRate);
		WriteU32(kAudioRate * kAudioBlockAlign);
		WriteU16((uint16_t)kAudioBlockAlign);
		WriteU16(16);
		WriteU16(0);

		// LIST movi
		WriteFourCC("LIST");
		moviSizePos = ftell(f);
		WriteU32(0);
		moviFourccPos = ftell(f);
		WriteFourCC("movi");
		return !failed;
	}

	void WriteChunk(const char id[4], const void *data, uint32_t size, uint32_t flags)
	{
		if (failed || !f) return;
		long chunkPos = ftell(f);
		if (chunkPos < moviFourccPos) { failed = true; return; }
		AviIdxEntry e;
		memcpy(e.id, id, 4);
		e.flags = flags;
		e.offset = (uint32_t)(chunkPos - moviFourccPos);
		e.size = size;
		idx.push_back(e);
		WriteFourCC(id);
		WriteU32(size);
		if (size) WriteRaw(data, size);
		if (size & 1u) { uint8_t pad = 0; WriteRaw(&pad, 1); }
	}

	void WriteVideo(const uint8_t *jpeg, size_t size)
	{
		if (size > 0x7FFFFFFFu) { failed = true; return; }
		WriteChunk("00dc", jpeg, (uint32_t)size, 0x00000010u);	// AVIIF_KEYFRAME
		if (!failed)
		{
			++totalFrames;
			if ((uint32_t)size > maxVideoChunk) maxVideoChunk = (uint32_t)size;
		}
	}

	void WriteAudio(const int16_t *pcm, uint32_t frames)
	{
		if (!frames) return;
		const uint32_t bytes = frames * kAudioBlockAlign;
		WriteChunk("01wb", pcm, bytes, 0x00000010u);
		if (!failed) totalAudioFrames += frames;
	}

	bool Finish()
	{
		if (!f) return false;
		if (!failed)
		{
			const long moviEnd = ftell(f);
			if (moviEnd < moviFourccPos + 4) failed = true;
			const uint32_t moviSize = failed ? 0u : (uint32_t)(moviEnd - moviFourccPos);
			const uint32_t idxBytes = (uint32_t)(idx.size() * 16u);
			WriteFourCC("idx1");
			WriteU32(idxBytes);
			for (const AviIdxEntry &e : idx)
			{
				WriteFourCC(e.id);
				WriteU32(e.flags);
				WriteU32(e.offset);
				WriteU32(e.size);
			}
			const long fileEnd = ftell(f);
			if (!failed && fileEnd > 8)
			{
				const uint32_t riffSize = (uint32_t)(fileEnd - 8);
				const uint32_t suggested = maxVideoChunk ? maxVideoChunk : ((uint32_t)width * (uint32_t)height);
				PatchU32(riffSizePos, riffSize);
				PatchU32(avihTotalFramesPos, totalFrames);
				PatchU32(avihSuggestedPos, suggested);
				PatchU32(strhVLenPos, totalFrames);
				PatchU32(strhVSuggestedPos, suggested);
				PatchU32(strhALenPos, totalAudioFrames);
				PatchU32(moviSizePos, moviSize);
			}
			if (fflush(f) != 0 || ferror(f)) failed = true;
		}
		if (fclose(f) != 0) failed = true;
		f = NULL;
		return !failed && totalFrames > 0;
	}
};

// ---- Safe Directory & File Helpers (mirroring ce_screenshot.cpp security boundaries) ----------------------------

int CountEntries(CeSettingsDir *dir, int limit)
{
	int count = 0;
#ifdef _WIN32
	char pattern[4300];
	snprintf(pattern, sizeof pattern, "%s\\*", dir->path);
	WIN32_FIND_DATAA found;
	HANDLE h = FindFirstFileA(pattern, &found);
	if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : limit;
	do
		if (strcmp(found.cFileName, ".") && strcmp(found.cFileName, "..")) ++count;
	while (count < limit && FindNextFileA(h, &found));
	FindClose(h);
#else
	int fd = openat(dir->fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
	if (!d)
	{
		if (fd >= 0) close(fd);
		return limit;
	}
	for (struct dirent *e; count < limit && (e = readdir(d));)
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) ++count;
	closedir(d);
#endif
	return count;
}

FILE *OpenNewExclusive(CeSettingsDir *dir, const char *name, int *alreadyExists)
{
	if (alreadyExists) *alreadyExists = 0;
#ifdef _WIN32
	char full[4500];
	snprintf(full, sizeof full, "%s\\%s", dir->path, name);
	HANDLE h = CreateFileA(full, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		if (alreadyExists && GetLastError() == ERROR_FILE_EXISTS) *alreadyExists = 1;
		return NULL;
	}
	int fd = _open_osfhandle((intptr_t)h, _O_WRONLY | _O_BINARY);
	if (fd < 0) { CloseHandle(h); DeleteFileA(full); return NULL; }
	FILE *f = _fdopen(fd, "wb");
	if (!f) { _close(fd); DeleteFileA(full); return NULL; }
	return f;
#else
	int fd = openat(dir->fd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (fd < 0)
	{
		if (alreadyExists && errno == EEXIST) *alreadyExists = 1;
		return NULL;
	}
	FILE *f = fdopen(fd, "wb");
	if (!f) { close(fd); unlinkat(dir->fd, name, 0); return NULL; }
	return f;
#endif
}

void RemoveClipFile(CeSettingsDir *dir, const char *name)
{
	if (!name || !*name) return;
#ifdef _WIN32
	char full[4500];
	snprintf(full, sizeof full, "%s\\%s", dir->path, name);
	DeleteFileA(full);
#else
	unlinkat(dir->fd, name, 0);
#endif
}

bool ClipFileExists(CeSettingsDir *dir, const char *name)
{
	if (!name || !*name) return true;
#ifdef _WIN32
	char full[4500];
	snprintf(full, sizeof full, "%s\\%s", dir->path, name);
	if (GetFileAttributesA(full) != INVALID_FILE_ATTRIBUTES) return true;
	DWORD err = GetLastError();
	return err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND;
#else
	struct stat st;
	return fstatat(dir->fd, name, &st, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT;
#endif
}

FILE *OpenExistingRegular(CeSettingsDir *dir, const char *name)
{
	if (!name || !*name) return NULL;
#ifdef _WIN32
	char full[4500];
	snprintf(full, sizeof full, "%s\\%s", dir->path, name);
	HANDLE h = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
	                       OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (h == INVALID_HANDLE_VALUE) return NULL;
	BY_HANDLE_FILE_INFORMATION info;
	if (GetFileType(h) != FILE_TYPE_DISK || !GetFileInformationByHandle(h, &info) ||
	    (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || ce_win_handle_is_link(h, info.dwFileAttributes))
	{
		CloseHandle(h);
		return NULL;
	}
	if (!ce_win_hydrate_handle_a(full, &h, &info, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, 0, 0))
		return NULL;
	int fd = _open_osfhandle((intptr_t)h, _O_RDONLY | _O_BINARY);
	if (fd < 0) { CloseHandle(h); return NULL; }
	FILE *f = _fdopen(fd, "rb");
	if (!f) { _close(fd); return NULL; }
	return f;
#else
	int fd = openat(dir->fd, name, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return NULL;
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); return NULL; }
	FILE *f = fdopen(fd, "rb");
	if (!f) { close(fd); return NULL; }
	return f;
#endif
}

// ---- Clean-room Standard ZIP Writer (PKZIP 2.0 STORE + IEEE 802.3 CRC32, zero external deps) -------------------

void InitCrc32Table(uint32_t tab[256])
{
	for (uint32_t i = 0; i < 256u; ++i)
	{
		uint32_t c = i;
		for (int b = 0; b < 8; ++b)
			c = (c & 1u) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
		tab[i] = c;
	}
}

uint32_t UpdateCrc32(uint32_t crc, const uint8_t *data, size_t n, const uint32_t tab[256])
{
	for (size_t i = 0; i < n; ++i)
		crc = tab[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
	return crc;
}

bool ZipWriteU16(FILE *f, uint16_t v)
{
	uint8_t b[2] = {(uint8_t)(v & 0xFFu), (uint8_t)(v >> 8)};
	return fwrite(b, 1, 2, f) == 2;
}

bool ZipWriteU32(FILE *f, uint32_t v)
{
	uint8_t b[4] = {(uint8_t)(v & 0xFFu), (uint8_t)((v >> 8) & 0xFFu),
	                (uint8_t)((v >> 16) & 0xFFu), (uint8_t)(v >> 24)};
	return fwrite(b, 1, 4, f) == 4;
}

bool PackageClipZip(CeSettingsDir *dir, const char *zipName, const char *const *members, int memberCount,
                    uint16_t dosTime, uint16_t dosDate)
{
	if (!dir || !zipName || !*zipName || !members || memberCount <= 0) return false;
	uint32_t crcTab[256];
	InitCrc32Table(crcTab);

	struct ZipEntryInfo
	{
		const char *name;
		uint16_t nameLen;
		uint32_t crc;
		uint32_t size;
		uint32_t localOffset;
	};
	std::vector<ZipEntryInfo> entries;
	entries.reserve((size_t)memberCount);
	std::vector<uint8_t> ioBuf(65536u);

	uint64_t estTotal = 22u;
	for (int i = 0; i < memberCount; ++i)
	{
		const char *mname = members[i];
		if (!mname || !*mname) return false;
		const size_t nlen = strlen(mname);
		if (nlen == 0 || nlen > 0xFFFFu) return false;
		FILE *in = OpenExistingRegular(dir, mname);
		if (!in) return false;
		uint32_t crc = 0xFFFFFFFFu;
		uint64_t total = 0;
		bool readErr = false;
		for (;;)
		{
			size_t got = fread(ioBuf.data(), 1, ioBuf.size(), in);
			if (got > 0)
			{
				if (total + got > 0x7FFFFFFFull) { readErr = true; break; }
				crc = UpdateCrc32(crc, ioBuf.data(), got, crcTab);
				total += got;
			}
			if (got < ioBuf.size())
			{
				if (ferror(in)) readErr = true;
				break;
			}
		}
		fclose(in);
		if (readErr) return false;
		estTotal += 30u + nlen + total + 46u + nlen;
		if (estTotal > 0x7FFFFFFFull) return false;
		entries.push_back({mname, (uint16_t)nlen, crc ^ 0xFFFFFFFFu, (uint32_t)total, 0u});
	}

	FILE *zf = OpenNewExclusive(dir, zipName, NULL);
	if (!zf) return false;

	bool ok = true;
	uint32_t offset = 0;
	for (ZipEntryInfo &e : entries)
	{
		e.localOffset = offset;
		ok = ok &&
		     ZipWriteU32(zf, 0x04034B50u) &&		// Local file header signature
		     ZipWriteU16(zf, 20u) &&				// Version needed to extract (2.0)
		     ZipWriteU16(zf, 0u) &&					// General purpose bit flag
		     ZipWriteU16(zf, 0u) &&					// Compression method (0 = STORE)
		     ZipWriteU16(zf, dosTime) &&			// Last mod file time
		     ZipWriteU16(zf, dosDate) &&			// Last mod file date
		     ZipWriteU32(zf, e.crc) &&				// CRC-32
		     ZipWriteU32(zf, e.size) &&				// Compressed size
		     ZipWriteU32(zf, e.size) &&				// Uncompressed size
		     ZipWriteU16(zf, e.nameLen) &&			// File name length
		     ZipWriteU16(zf, 0u) &&					// Extra field length
		     fwrite(e.name, 1, e.nameLen, zf) == e.nameLen;
		offset += 30u + e.nameLen;
		if (!ok) break;

		FILE *in = OpenExistingRegular(dir, e.name);
		if (!in) { ok = false; break; }
		uint32_t copied = 0;
		for (;;)
		{
			size_t got = fread(ioBuf.data(), 1, ioBuf.size(), in);
			if (got > 0)
			{
				if (got > e.size - copied || fwrite(ioBuf.data(), 1, got, zf) != got) { ok = false; break; }
				copied += (uint32_t)got;
			}
			if (got < ioBuf.size())
			{
				if (ferror(in)) ok = false;
				break;
			}
		}
		fclose(in);
		if (!ok || copied != e.size) { ok = false; break; }
		offset += e.size;
	}

	const uint32_t cdOffset = offset;
	uint32_t cdSize = 0;
	for (const ZipEntryInfo &e : entries)
	{
		if (!ok) break;
		ok = ok &&
		     ZipWriteU32(zf, 0x02014B50u) &&		// Central directory file header signature
		     ZipWriteU16(zf, 20u) &&				// Version made by (2.0)
		     ZipWriteU16(zf, 20u) &&				// Version needed to extract (2.0)
		     ZipWriteU16(zf, 0u) &&					// General purpose bit flag
		     ZipWriteU16(zf, 0u) &&					// Compression method (0 = STORE)
		     ZipWriteU16(zf, dosTime) &&			// Last mod file time
		     ZipWriteU16(zf, dosDate) &&			// Last mod file date
		     ZipWriteU32(zf, e.crc) &&				// CRC-32
		     ZipWriteU32(zf, e.size) &&				// Compressed size
		     ZipWriteU32(zf, e.size) &&				// Uncompressed size
		     ZipWriteU16(zf, e.nameLen) &&			// File name length
		     ZipWriteU16(zf, 0u) &&					// Extra field length
		     ZipWriteU16(zf, 0u) &&					// File comment length
		     ZipWriteU16(zf, 0u) &&					// Disk number start
		     ZipWriteU16(zf, 0u) &&					// Internal file attributes
		     ZipWriteU32(zf, 0u) &&					// External file attributes
		     ZipWriteU32(zf, e.localOffset) &&		// Relative offset of local header
		     fwrite(e.name, 1, e.nameLen, zf) == e.nameLen;
		cdSize += 46u + e.nameLen;
	}

	if (ok)
	{
		ok = ZipWriteU32(zf, 0x06054B50u) &&				// End of central directory signature
		     ZipWriteU16(zf, 0u) &&							// Number of this disk
		     ZipWriteU16(zf, 0u) &&							// Disk where central directory starts
		     ZipWriteU16(zf, (uint16_t)entries.size()) &&	// Entries on this disk
		     ZipWriteU16(zf, (uint16_t)entries.size()) &&	// Total entries
		     ZipWriteU32(zf, cdSize) &&						// Size of central directory
		     ZipWriteU32(zf, cdOffset) &&					// Offset of central directory
		     ZipWriteU16(zf, 0u);							// Comment length
	}

	if (ok && (fflush(zf) != 0 || ferror(zf))) ok = false;
	if (fclose(zf) != 0) ok = false;
	if (!ok)
	{
		RemoveClipFile(dir, zipName);
		return false;
	}
	return true;
}

// Read tail (up to maxBytes) of a regular user-folder file without following symlinks/reparse points.
std::string ReadRegularTail(const char *userDir, const char *fileName, size_t maxBytes)
{
	CeSettingsDir dir;
	if (!ce_settings_dir_open_ex(userDir, 0, &dir, NULL)) return "";
	FILE *f = NULL;
#ifdef _WIN32
	char path[4500];
	snprintf(path, sizeof path, "%s\\%s", dir.path, fileName);
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                       OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (h != INVALID_HANDLE_VALUE)
	{
		BY_HANDLE_FILE_INFORMATION info;
		if (GetFileType(h) == FILE_TYPE_DISK && GetFileInformationByHandle(h, &info) &&
		    !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !ce_win_handle_is_link(h, info.dwFileAttributes) &&
		    ce_win_hydrate_handle_a(path, &h, &info, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, 0))
		{
			int fd = _open_osfhandle((intptr_t)h, _O_RDONLY | _O_BINARY);
			if (fd >= 0) { f = _fdopen(fd, "rb"); if (!f) _close(fd); }
			else CloseHandle(h);
		}
		else if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	}
#else
	int fd = openat(dir.fd, fileName, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
	if (fd >= 0)
	{
		struct stat st;
		if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode))
		{
			f = fdopen(fd, "rb");
			if (!f) close(fd);
		}
		else close(fd);
	}
#endif
	std::string out;
	if (f)
	{
		if (fseek(f, 0, SEEK_END) == 0)
		{
			long total = ftell(f);
			if (total > 0)
			{
				long start = (long)((size_t)total > maxBytes ? ((size_t)total - maxBytes) : 0u);
				if (fseek(f, start, SEEK_SET) == 0)
				{
					size_t want = (size_t)(total - start);
					out.resize(want);
					size_t got = fread(&out[0], 1, want, f);
					out.resize(got);
				}
			}
		}
		fclose(f);
	}
	ce_settings_dir_close(&dir);
	return out;
}

// Replace any occurrence of user home/account path prefixes in a diagnostic text line.
static bool AsciiLowerEq(char a, char b)
{
	if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
	if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
	return a == b;
}

static size_t FindInsensitive(const std::string &hay, const std::string &needle, size_t from)
{
	if (needle.empty() || from + needle.size() > hay.size()) return std::string::npos;
	for (size_t i = from; i + needle.size() <= hay.size(); ++i)
	{
		bool match = true;
		for (size_t j = 0; j < needle.size(); ++j)
		{
			if (!AsciiLowerEq(hay[i + j], needle[j])) { match = false; break; }
		}
		if (match) return i;
	}
	return std::string::npos;
}

static bool AsciiEqInsensitive(const std::string &a, const char *b)
{
	const size_t blen = strlen(b);
	if (a.size() != blen) return false;
	for (size_t i = 0; i < blen; ++i)
		if (!AsciiLowerEq(a[i], b[i])) return false;
	return true;
}

static void ScrubAccountSegment(std::string &out, const char *prefix, bool caseInsensitive)
{
	const size_t plen = strlen(prefix);
	size_t pos = 0;
	while (pos + plen < out.size())
	{
		size_t hit = caseInsensitive ? FindInsensitive(out, prefix, pos) : out.find(prefix, pos);
		if (hit == std::string::npos) break;
		size_t start = hit + plen;
		size_t end = start;
		// Stop at path separators, control/newline chars, quotes, or trailing log punctuation; allow spaces inside
		// multi-word Windows profile folder names (both backslash and forward-slash forms).
		while (end < out.size())
		{
			unsigned char ch = (unsigned char)out[end];
			if (ch == '/' || ch == '\\' || ch < 32 || ch == '"' || ch == '\'' ||
			    ch == ')' || ch == ']' || ch == ',' || ch == ';' || ch == ':')
				break;
			++end;
		}
		while (end > start && out[end - 1] == ' ') --end;
		if (end > start)
		{
			std::string seg = out.substr(start, end - start);
			bool keep = (seg == "<user>");
			if (!keep)
			{
				auto eq = [&](const char *w) { return caseInsensitive ? AsciiEqInsensitive(seg, w) : (seg == w); };
				if (eq("Public") || eq("Default") || eq("Default User") || eq("All Users"))
					keep = true;
			}
			if (!keep)
			{
				out.replace(start, end - start, "<user>");
				pos = start + 6;
				continue;
			}
		}
		pos = start;
	}
}

std::string ScrubText(const std::string &in)
{
	std::vector<std::pair<std::string, std::string>> pathReps;
	std::vector<std::string> accts;
#ifdef _WIN32
	const char *envs[][2] = {{"LOCALAPPDATA", "%LOCALAPPDATA%"}, {"APPDATA", "%APPDATA%"}, {"USERPROFILE", "%USERPROFILE%"}};
	const char *acctEnvs[] = {"USERNAME"};
	const bool ci = true;
#else
	const char *envs[][2] = {{"HOME", "~"}};
	const char *acctEnvs[] = {"USER", "LOGNAME"};
	const bool ci = false;
#endif
	for (const auto &e : envs)
	{
		const char *v = getenv(e[0]);
		if (v && strlen(v) >= 3)
		{
			std::string val(v);
			pathReps.push_back({val, e[1]});
#ifdef _WIN32
			if (val.find('\\') != std::string::npos)
			{
				std::string fwd = val;
				std::replace(fwd.begin(), fwd.end(), '\\', '/');
				pathReps.push_back({fwd, e[1]});
			}
#endif
		}
	}
	for (const char *envName : acctEnvs)
	{
		const char *a = getenv(envName);
		// Only run unanchored global replacement for usernames >= 4 chars so short names like "ed" do not mangle
		// words like "windowed"/"extended"/"gamedir"; path-anchored scrubbing below always scrubs any length.
		if (a && strlen(a) >= 4)
		{
			std::string s(a);
			if (std::find(accts.begin(), accts.end(), s) == accts.end())
				accts.push_back(std::move(s));
		}
	}

	std::string out = in;
	for (const auto &r : pathReps)
	{
		size_t pos = 0;
		while ((pos = (ci ? FindInsensitive(out, r.first, pos) : out.find(r.first, pos))) != std::string::npos)
		{
			out.replace(pos, r.first.size(), r.second);
			pos += r.second.size();
		}
	}
	// Scrub path-anchored home/Users account segments of any length (including multi-word Windows profile folders).
	ScrubAccountSegment(out, "\\Users\\", true);
	ScrubAccountSegment(out, "/Users/", true);
	ScrubAccountSegment(out, "/home/", false);
	for (const auto &a : accts)
	{
		size_t pos = 0;
		while ((pos = (ci ? FindInsensitive(out, a, pos) : out.find(a, pos))) != std::string::npos)
		{
			out.replace(pos, a.size(), "<user>");
			pos += 6;
		}
	}
	return out;
}

// Strict allowlist filter for ce.ini in shareable companion bug-clip reports (matching ce_launch.c's
// filter_ce_ini_for_tape): keeps only gameplay/UI/graphics/experimental switches; drops paths (gamedir,
// music_dir, career_backup_folder), endpoints/network keys (lobby_url, signaling_url, turn_*, online_*,
// net_port), mods, unknown keys, and any value containing '/', '\', '://', or drive-letter paths.
static bool CeIniKeySafeForReport(const char *key, size_t len)
{
	static const char *const kSafeKeys[] = {
		"aniso", "anisotropic_filtering", "catalogue",
		"cheat_aurore", "cheat_latete", "cheat_maladim", "cheat_malloy", "cheat_turkey", "cheat_v3r5iof", "cheats",
		"commander", "commander_key", "commander_tactical_map", "console_theme",
		"debrief_stats", "dev_stats", "dodge_button", "dodge_key", "dynamic_shadow_res",
		"extra_graphics", "extras", "factory_exercise", "full_models", "full_tree_models", "full_unit_models",
		"god_mode", "hud_anchor", "hud_scale",
		"map_editor", "mods_menu", "msaa", "multi_be", "multi_be_cam", "multi_be_campaign", "multi_be_hud_slot",
		"museum", "music_playtype", "muspell",
		"network_game", "play_side", "play_unit", "rumble",
		"sandbox_telemetry", "sharp_ui", "solo_skirmish", "take_unit_key",
		"terrain_detail", "terrain_detail_distance", "vsync",
		"water_reflection_res", "wide_mode_h", "wide_mode_w", "widescreen", "windowed"
	};
	for (size_t i = 0; i < sizeof(kSafeKeys) / sizeof(kSafeKeys[0]); ++i)
		if (strlen(kSafeKeys[i]) == len && memcmp(kSafeKeys[i], key, len) == 0)
			return true;
	return false;
}

static std::string FilterCeIniForReport(const std::string &raw)
{
	if (raw.empty() || raw.find('\0') != std::string::npos) return "";
	size_t at = (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
	             (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) ? 3u : 0u;
	static const char kExpHeader[] = "[experimental]";
	std::string out;
	out.reserve(raw.size());
	while (at < raw.size())
	{
		size_t nl = raw.find('\n', at);
		size_t n = (nl == std::string::npos) ? (raw.size() - at) : (nl - at);
		const char *line = raw.data() + at;
		at = (nl == std::string::npos) ? raw.size() : (nl + 1);
		size_t s = 0, e = n;
		while (s < e && (line[s] == ' ' || line[s] == '\t')) ++s;
		while (e > s && (line[e - 1] == '\r' || line[e - 1] == ' ' || line[e - 1] == '\t')) --e;
		if (s == e || line[s] == ';' || line[s] == '#') continue;
		if (e - s == sizeof(kExpHeader) - 1 && memcmp(line + s, kExpHeader, sizeof(kExpHeader) - 1) == 0)
		{
			out.append(kExpHeader);
			out.push_back('\n');
			continue;
		}
		const char *eq = (const char *)memchr(line + s, '=', e - s);
		if (!eq) continue;
		size_t ke = (size_t)(eq - line);
		while (ke > s && (line[ke - 1] == ' ' || line[ke - 1] == '\t')) --ke;
		size_t klen = ke - s;
		if (!klen || !CeIniKeySafeForReport(line + s, klen)) continue;
		size_t vs = (size_t)(eq - line) + 1;
		while (vs < e && (line[vs] == ' ' || line[vs] == '\t')) ++vs;
		size_t vlen = e - vs;
		if (vlen == 0 || vlen > 64) continue;
		bool badVal = false;
		if (vlen >= 2 &&
		    (((unsigned char)line[vs] >= 'A' && (unsigned char)line[vs] <= 'Z') ||
		     ((unsigned char)line[vs] >= 'a' && (unsigned char)line[vs] <= 'z')) &&
		    line[vs + 1] == ':')
			badVal = true;
		for (size_t i = 0; !badVal && i < vlen; ++i)
		{
			unsigned char c = (unsigned char)line[vs + i];
			if (c < 0x20 || c == 0x7F || c == '/' || c == '\\') { badVal = true; break; }
			if (i + 2 < vlen && line[vs + i] == ':' && line[vs + i + 1] == '/' && line[vs + i + 2] == '/')
				{ badVal = true; break; }
		}
		if (badVal) continue;
		out.append(line + s, klen);
		out.push_back('=');
		out.append(line + vs, vlen);
		out.push_back('\n');
	}
	return out;
}

// Bilinear RGB24 resize fallback when the game changes resolution mid-clip so AVI dimensions stay fixed with zero dropped frames.
void ResizeRgb24Bilinear(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh)
{
	if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
	for (int y = 0; y < dh; ++y)
	{
		const float fy = ((float)y + 0.5f) * (float)sh / (float)dh - 0.5f;
		const int y0 = std::max(0, std::min(sh - 1, (int)std::floor(fy)));
		const int y1 = std::min(sh - 1, y0 + 1);
		const float wy = std::max(0.0f, std::min(1.0f, fy - (float)y0));
		const uint8_t *r0 = src + (size_t)y0 * (size_t)sw * 3u;
		const uint8_t *r1 = src + (size_t)y1 * (size_t)sw * 3u;
		uint8_t *out = dst + (size_t)y * (size_t)dw * 3u;
		for (int x = 0; x < dw; ++x)
		{
			const float fx = ((float)x + 0.5f) * (float)sw / (float)dw - 0.5f;
			const int x0 = std::max(0, std::min(sw - 1, (int)std::floor(fx)));
			const int x1 = std::min(sw - 1, x0 + 1);
			const float wx = std::max(0.0f, std::min(1.0f, fx - (float)x0));
			const uint8_t *p00 = r0 + (size_t)x0 * 3u;
			const uint8_t *p10 = r0 + (size_t)x1 * 3u;
			const uint8_t *p01 = r1 + (size_t)x0 * 3u;
			const uint8_t *p11 = r1 + (size_t)x1 * 3u;
			for (int c = 0; c < 3; ++c)
			{
				const float top = (float)p00[c] * (1.0f - wx) + (float)p10[c] * wx;
				const float bot = (float)p01[c] * (1.0f - wx) + (float)p11[c] * wx;
				const int v = (int)(top * (1.0f - wy) + bot * wy + 0.5f);
				out[x * 3 + c] = (uint8_t)std::max(0, std::min(255, v));
			}
		}
	}
}

// Convert D3D9 locked surface pixels to tightly packed RGB24.
bool ConvertToRgb24(const void *pixels, int pitch, int width, int height, unsigned d3dFormat, std::vector<uint8_t> &rgb)
{
	if (!pixels || width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
	const int bpp = (d3dFormat == kA8R8G8B8 || d3dFormat == kX8R8G8B8) ? 4 :
	                (d3dFormat == kR5G6B5 || d3dFormat == kX1R5G5B5 || d3dFormat == kA1R5G5B5) ? 2 : 0;
	if (!bpp || pitch < width * bpp) return false;
	rgb.resize((size_t)width * (size_t)height * 3u);
	for (int y = 0; y < height; ++y)
	{
		const uint8_t *src = (const uint8_t *)pixels + (size_t)y * (size_t)pitch;
		uint8_t *dst = &rgb[(size_t)y * (size_t)width * 3u];
		if (bpp == 4)
		{
			for (int x = 0; x < width; ++x)
			{
				// D3D9 X8R8G8B8 / A8R8G8B8 is BGRA in little-endian memory.
				dst[x * 3 + 0] = src[x * 4 + 2];
				dst[x * 3 + 1] = src[x * 4 + 1];
				dst[x * 3 + 2] = src[x * 4 + 0];
			}
		}
		else if (d3dFormat == kR5G6B5)
		{
			const uint16_t *s16 = (const uint16_t *)src;
			for (int x = 0; x < width; ++x)
			{
				const uint16_t v = s16[x];
				const uint8_t r5 = (uint8_t)((v >> 11) & 0x1F);
				const uint8_t g6 = (uint8_t)((v >> 5) & 0x3F);
				const uint8_t b5 = (uint8_t)(v & 0x1F);
				dst[x * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
				dst[x * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
				dst[x * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
			}
		}
		else
		{
			const uint16_t *s16 = (const uint16_t *)src;
			for (int x = 0; x < width; ++x)
			{
				const uint16_t v = s16[x];
				const uint8_t r5 = (uint8_t)((v >> 10) & 0x1F);
				const uint8_t g5 = (uint8_t)((v >> 5) & 0x1F);
				const uint8_t b5 = (uint8_t)(v & 0x1F);
				dst[x * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
				dst[x * 3 + 1] = (uint8_t)((g5 << 3) | (g5 >> 2));
				dst[x * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
			}
		}
	}
	return true;
}

// ---- Recorder Runtime State -------------------------------------------------------------------------------------

struct QueuedFrame
{
	std::vector<uint8_t> rgb;
	int width = 0;
	int height = 0;
	uint64_t stampNs = 0;
	bool ready = false;
};

struct MonotonicCond
{
#ifndef _WIN32
	// Avoid std::condition_variable::wait_for on Linux: libstdc++ under the Sniper sysroot emits
	// pthread_cond_clockwait@GLIBC_2.30, breaking the glibc 2.29 compatibility floor.
	pthread_cond_t cond;
	clockid_t clk = CLOCK_REALTIME;

	MonotonicCond()
	{
		pthread_condattr_t attr;
		if (pthread_condattr_init(&attr) == 0)
		{
			if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0)
				clk = CLOCK_MONOTONIC;
			if (pthread_cond_init(&cond, &attr) != 0)
			{
				clk = CLOCK_REALTIME;
				pthread_cond_init(&cond, NULL);
			}
			pthread_condattr_destroy(&attr);
		}
		else
		{
			pthread_cond_init(&cond, NULL);
		}
	}

	~MonotonicCond()
	{
		pthread_cond_destroy(&cond);
	}

	void notify_one()
	{
		pthread_cond_signal(&cond);
	}

	template <class Pred>
	void wait_ms(std::unique_lock<std::mutex> &lk, unsigned ms, Pred pred)
	{
		if (pred()) return;
		struct timespec ts;
		if (clock_gettime(clk, &ts) != 0) return;
		uint64_t nsec = (uint64_t)ts.tv_nsec + (uint64_t)ms * 1000000ull;
		ts.tv_sec += (time_t)(nsec / 1000000000ull);
		ts.tv_nsec = (long)(nsec % 1000000000ull);
		while (!pred())
		{
			int rc = pthread_cond_timedwait(&cond, lk.mutex()->native_handle(), &ts);
			if (rc == ETIMEDOUT || (rc != 0 && rc != EINTR))
				break;
		}
	}
#else
	std::condition_variable cv;

	void notify_one() { cv.notify_one(); }

	template <class Pred>
	void wait_ms(std::unique_lock<std::mutex> &lk, unsigned ms, Pred pred)
	{
		cv.wait_for(lk, std::chrono::milliseconds(ms), pred);
	}
#endif
};

struct BugClipRuntime
{
	std::mutex mu;
	MonotonicCond cv;
	std::thread worker;
	bool active = false;
	bool stopRequested = false;
	bool atExitRegistered = false;

	uint64_t startNs = 0;
	uint64_t stopNs = 0;
	uint64_t lastCaptureNs = 0;

	QueuedFrame ring[kFrameQueueSlots];
	unsigned qRead = 0, qWrite = 0, qCount = 0;

	// Toast / status message protected by mu.
	char toast[128] = "";
	uint64_t toastFromMs = 0;
	uint64_t toastUntilMs = 0;

	void SetToastLocked(const char *msg)
	{
		snprintf(toast, sizeof toast, "%s", msg ? msg : "");
		toastFromMs = SDL_GetTicks();
		toastUntilMs = toastFromMs + kToastMs;
	}

	void DrainAudioLocked(AviWriter &avi, std::vector<int16_t> &scratch, uint64_t byteCap, bool &capHit)
	{
		uint32_t head = __atomic_load_n(&gAudioHead, __ATOMIC_ACQUIRE);
		uint32_t tail = __atomic_load_n(&gAudioTail, __ATOMIC_RELAXED);
		uint32_t avail = head - tail;
		if (avail > kAudioRingFrames)
		{
			tail = head - kAudioRingFrames;
			avail = kAudioRingFrames;
		}
		if (!avail) return;
		const uint64_t idxReserve = (uint64_t)(avi.idx.size() + 2u) * 16u + 8u;
		if (avi.bytesWritten + (uint64_t)avail * kAudioBlockAlign + 16u + idxReserve > byteCap)
		{
			__atomic_store_n(&gAudioTail, head, __ATOMIC_RELEASE);
			capHit = true;
			return;
		}
		scratch.resize((size_t)avail * 2u);
		uint32_t idx = tail & kAudioRingMask;
		uint32_t first = std::min(avail, kAudioRingFrames - idx);
		memcpy(scratch.data(), gAudioRing + (size_t)idx * 2u, (size_t)first * kAudioBlockAlign);
		if (first < avail)
			memcpy(scratch.data() + (size_t)first * 2u, gAudioRing, (size_t)(avail - first) * kAudioBlockAlign);
		__atomic_store_n(&gAudioTail, head, __ATOMIC_RELEASE);
		avi.WriteAudio(scratch.data(), avail);
	}

	bool WriteCompanionReport(CeSettingsDir *dir, const char *txtName, const char *aviName,
	                          int width, int height, uint32_t frames, uint32_t audioFrames)
	{
		FILE *tf = OpenNewExclusive(dir, txtName, NULL);
		if (!tf) return false;
		char user[4096], os[256];
		ce_user_dir(user, sizeof user);
		ce_os_name(os, sizeof os);
		const char *build = CE_BuildName();
		const double sec = frames ? ((double)frames / (double)kClipFps) : 0.0;
		fprintf(tf,
		        "Onslaught CE Bug Clip Report\n"
		        "Version: V%s (build %s)\n"
		        "OS: %s\n"
		        "Clip: %s (%dx%d, %u frames, %.2f s @ %u fps, %u audio samples @ %u Hz stereo)\n\n",
		        CE_VERSION, (build && *build) ? build : "unknown", os, aviName,
		        width, height, frames, sec, kClipFps, audioFrames, kAudioRate);

		std::string ini = ReadRegularTail(user, "ce.ini", 32768);
		if (!ini.empty())
		{
			std::string cleanIni = ScrubText(FilterCeIniForReport(ini));
			if (!cleanIni.empty())
				fprintf(tf, "---- ce.ini ----\n%s\n", cleanIni.c_str());
		}
		fflush(stderr);
		std::string logTail = ReadRegularTail(user, "onslaught-ce.log", 65536);
		if (!logTail.empty())
		{
			std::string cleanLog = ScrubText(logTail);
			fprintf(tf, "---- onslaught-ce.log (tail) ----\n%s\n", cleanLog.c_str());
		}
		if (fflush(tf) != 0 || ferror(tf))
		{
			fclose(tf);
			RemoveClipFile(dir, txtName);
			return false;
		}
		if (fclose(tf) != 0)
		{
			RemoveClipFile(dir, txtName);
			return false;
		}
		return true;
	}

	void WorkerMain()
	{
		JpegTables tables;
		tables.Init(80);
		AviWriter avi;
		CeSettingsDir dir;
		bool dirOpen = false;
		char zipName[96] = "", aviName[96] = "", txtName[96] = "";
		bool aviStarted = false;
		int clipW = 0, clipH = 0;
		uint64_t firstFrameNs = 0;
		uint64_t clipStartNs = 0;
		{
			std::lock_guard<std::mutex> lk(mu);
			clipStartNs = startNs;
		}
		const char *clkEnv = getenv("BEA_PORT_CLOCK");
		const bool virtualClock = (clkEnv && *clkEnv);
		std::vector<uint8_t> jpegBuf, lastJpeg, resizedRgb;
		std::vector<int16_t> audioScratch;
		bool capHit = false;
		const char *errMsg = NULL;
		uint16_t dosTime = 0, dosDate = (uint16_t)((1u << 5) | 1u);	// 1980-01-01 00:00:00 fallback

		char user[4096];
		ce_user_dir(user, sizeof user);
		if (!ce_settings_subdir_open_ex(user, "clips", 1, &dir, NULL))
		{
			errMsg = "Bug clip failed: could not open clips folder.";
		}
		else
		{
			dirOpen = true;
			if (CountEntries(&dir, kMaxClipFiles) >= kMaxClipFiles)
			{
				errMsg = "Bug clip failed: the clips folder is full.";
			}
			else
			{
				time_t now = time(NULL);
				struct tm local;
#ifdef _WIN32
				const bool dated = localtime_s(&local, &now) == 0;
#else
				const bool dated = localtime_r(&now, &local) != NULL;
#endif
				char stem[64];
				if (dated)
				{
					snprintf(stem, sizeof stem, "onslaught-ce-clip-%04d%02d%02d-%02d%02d%02d",
					         local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
					         local.tm_hour, local.tm_min, local.tm_sec);
					int yr = local.tm_year + 1900;
					if (yr < 1980) yr = 1980;
					if (yr > 2107) yr = 2107;
					dosTime = (uint16_t)(((local.tm_hour & 0x1F) << 11) |
					                     ((local.tm_min & 0x3F) << 5) |
					                     ((local.tm_sec / 2) & 0x1F));
					dosDate = (uint16_t)((((yr - 1980) & 0x7F) << 9) |
					                     (((local.tm_mon + 1) & 0x0F) << 5) |
					                     (local.tm_mday & 0x1F));
				}
				else
				{
					snprintf(stem, sizeof stem, "onslaught-ce-clip-%llu", (unsigned long long)SDL_GetTicks());
				}

				FILE *f = NULL;
				for (int i = 1; i <= 99 && !f; ++i)
				{
					if (i == 1)
					{
						snprintf(zipName, sizeof zipName, "%s.zip", stem);
						snprintf(aviName, sizeof aviName, "%s.avi", stem);
						snprintf(txtName, sizeof txtName, "%s.txt", stem);
					}
					else
					{
						snprintf(zipName, sizeof zipName, "%s-%d.zip", stem, i);
						snprintf(aviName, sizeof aviName, "%s-%d.avi", stem, i);
						snprintf(txtName, sizeof txtName, "%s-%d.txt", stem, i);
					}
					// Avoid colliding with any pre-existing .zip, .txt, or .avi for this stem.
					if (ClipFileExists(&dir, zipName) || ClipFileExists(&dir, txtName))
						continue;
					int exists = 0;
					f = OpenNewExclusive(&dir, aviName, &exists);
					if (!f && !exists) break;
				}
				if (!f)
					errMsg = "Bug clip failed: could not create clip file.";
				else
					avi.f = f;
			}
		}

		if (errMsg)
		{
			__atomic_store_n(&gAudioActive, 0, __ATOMIC_RELEASE);
			std::lock_guard<std::mutex> lk(mu);
			active = false;
			stopRequested = false;
			qRead = qWrite = qCount = 0;
			SetToastLocked(errMsg);
			if (dirOpen) ce_settings_dir_close(&dir);
			fprintf(stderr, PORT_LOG_PREFIX "%s\n", errMsg);
			return;
		}

		// Main worker encoding loop.
		for (;;)
		{
			QueuedFrame frame;
			bool gotFrame = false;
			bool stopping = false;
			{
				std::unique_lock<std::mutex> lk(mu);
				cv.wait_ms(lk, 40u, [&] { return qCount > 0 || stopRequested; });
				if (qCount > 0)
				{
					frame = std::move(ring[qRead]);
					ring[qRead].ready = false;
					qRead = (qRead + 1u) % kFrameQueueSlots;
					--qCount;
					gotFrame = true;
				}
				stopping = stopRequested;
			}

			// Enforce the 60-second wall-clock cap here too so minimized or stalled runs always auto-stop.
			const uint64_t loopNowNs = SDL_GetTicksNS();
			if (!capHit && clipStartNs && loopNowNs > clipStartNs &&
			    (loopNowNs - clipStartNs) >= (uint64_t)kMaxClipSeconds * 1000000000ull)
			{
				capHit = true;
			}

			if (gotFrame && !capHit && !avi.failed)
			{
				if (!aviStarted)
				{
					clipW = frame.width;
					clipH = frame.height;
					firstFrameNs = frame.stampNs;
					// Discard stale pre-frame-1 audio older than one video frame so A/V start aligned.
					const uint32_t maxLead = kAudioRate / kClipFps;
					const uint32_t head = __atomic_load_n(&gAudioHead, __ATOMIC_ACQUIRE);
					const uint32_t tail = __atomic_load_n(&gAudioTail, __ATOMIC_RELAXED);
					if ((uint32_t)(head - tail) > maxLead)
						__atomic_store_n(&gAudioTail, head - maxLead, __ATOMIC_RELEASE);
					aviStarted = avi.Begin(avi.f, clipW, clipH);
				}
				if (aviStarted)
				{
					const uint8_t *rgbPtr = frame.rgb.data();
					if (frame.width != clipW || frame.height != clipH)
					{
						resizedRgb.resize((size_t)clipW * (size_t)clipH * 3u);
						ResizeRgb24Bilinear(frame.rgb.data(), frame.width, frame.height, resizedRgb.data(), clipW, clipH);
						rgbPtr = resizedRgb.data();
					}
					EncodeJpegRgb24(rgbPtr, clipW, clipH, tables, jpegBuf);
					// Keep constant 20 FPS timeline aligned to wall clock across loading stalls in ordinary play.
					// Under a virtual clock (BEA_PORT_CLOCK, e.g. --record), virtual audio pauses during stalls so
					// suppress wall-clock frame duplication to keep video and virtual audio in sync.
					if (!virtualClock && !lastJpeg.empty())
					{
						uint64_t elapsedNs = frame.stampNs >= firstFrameNs ? (frame.stampNs - firstFrameNs) : 0;
						uint32_t targetIndex = (uint32_t)(elapsedNs / kFramePeriodNs);
						if (targetIndex > avi.totalFrames)
						{
							uint32_t dup = std::min<uint32_t>(targetIndex - avi.totalFrames, kMaxClipFrames - avi.totalFrames);
							for (uint32_t d = 0; d < dup && avi.totalFrames < kMaxClipFrames; ++d)
							{
								const uint64_t idxReserve = (uint64_t)(avi.idx.size() + 2u) * 16u + 8u;
								if (avi.bytesWritten + lastJpeg.size() + 16u + idxReserve > kMaxClipBytes) { capHit = true; break; }
								avi.WriteVideo(lastJpeg.data(), lastJpeg.size());
							}
						}
					}
					if (!capHit && avi.totalFrames < kMaxClipFrames)
					{
						const uint64_t idxReserve = (uint64_t)(avi.idx.size() + 2u) * 16u + 8u;
						if (avi.bytesWritten + jpegBuf.size() + 16u + idxReserve > kMaxClipBytes)
							capHit = true;
						else
						{
							avi.WriteVideo(jpegBuf.data(), jpegBuf.size());
							lastJpeg = jpegBuf;
						}
					}
					if (avi.totalFrames >= kMaxClipFrames)
						capHit = true;
				}
			}

			if (aviStarted && !capHit && !avi.failed)
				DrainAudioLocked(avi, audioScratch, kMaxClipBytes, capHit);

			if (capHit || avi.failed)
			{
				__atomic_store_n(&gAudioActive, 0, __ATOMIC_RELEASE);
				std::lock_guard<std::mutex> lk(mu);
				if (!stopNs) stopNs = loopNowNs;
				active = false;
				stopRequested = true;
				stopping = true;
			}

			if (stopping)
			{
				bool more = false;
				{
					std::lock_guard<std::mutex> lk(mu);
					more = (qCount > 0) && !capHit && !avi.failed;
				}
				if (!more) break;
			}
		}

		__atomic_store_n(&gAudioActive, 0, __ATOMIC_RELEASE);

		uint64_t latchedStopNs = 0;
		{
			std::lock_guard<std::mutex> lk(mu);
			latchedStopNs = stopNs;
		}

		bool ok = false;
		uint32_t savedFrames = 0, savedAudio = 0;
		if (aviStarted)
		{
			if (!capHit && !avi.failed)
				DrainAudioLocked(avi, audioScratch, kMaxClipBytes, capHit);
			if (!virtualClock && !capHit && !avi.failed && !lastJpeg.empty())
			{
				const uint64_t endNs = latchedStopNs ? latchedStopNs : SDL_GetTicksNS();
				if (endNs > firstFrameNs)
				{
					const uint32_t targetEnd = (uint32_t)((endNs - firstFrameNs) / kFramePeriodNs);
					if (targetEnd > avi.totalFrames)
					{
						const uint32_t pad = std::min<uint32_t>(targetEnd - avi.totalFrames, kMaxClipFrames - avi.totalFrames);
						for (uint32_t d = 0; d < pad; ++d)
						{
							const uint64_t idxReserve = (uint64_t)(avi.idx.size() + 2u) * 16u + 8u;
							if (avi.bytesWritten + lastJpeg.size() + 16u + idxReserve > kMaxClipBytes) break;
							avi.WriteVideo(lastJpeg.data(), lastJpeg.size());
						}
					}
				}
			}
			savedFrames = avi.totalFrames;
			savedAudio = avi.totalAudioFrames;
			ok = avi.Finish();
		}
		else if (avi.f)
		{
			fclose(avi.f);
			avi.f = NULL;
		}

		char doneMsg[128];
		if (ok)
		{
			const bool txtOk = WriteCompanionReport(&dir, txtName, aviName, clipW, clipH, savedFrames, savedAudio);
			const char *members[2] = {aviName, txtName};
			const int memberCount = txtOk ? 2 : 1;
			if (PackageClipZip(&dir, zipName, members, memberCount, dosTime, dosDate))
			{
				RemoveClipFile(&dir, aviName);
				if (txtOk) RemoveClipFile(&dir, txtName);
				snprintf(doneMsg, sizeof doneMsg, "Bug clip saved: %s (clips folder)", zipName);
				fprintf(stderr, PORT_LOG_PREFIX "bug clip saved: clips/%s (%dx%d, %u frames, %u audio samples)\n",
				        zipName, clipW, clipH, savedFrames, savedAudio);
			}
			else
			{
				snprintf(doneMsg, sizeof doneMsg, "Bug clip saved: %s (clips folder)", aviName);
				fprintf(stderr, PORT_LOG_PREFIX "bug clip zip packaging failed; kept loose clips/%s (%dx%d, %u frames, %u audio samples)\n",
				        aviName, clipW, clipH, savedFrames, savedAudio);
			}
		}
		else
		{
			RemoveClipFile(&dir, aviName);
			snprintf(doneMsg, sizeof doneMsg, "Bug clip not saved: %s",
			         savedFrames == 0 ? "no frames captured." : "could not write file.");
			fprintf(stderr, PORT_LOG_PREFIX "%s\n", doneMsg);
		}
		ce_settings_dir_close(&dir);

		std::lock_guard<std::mutex> lk(mu);
		active = false;
		stopRequested = false;
		qRead = qWrite = qCount = 0;
		SetToastLocked(doneMsg);
	}
};

BugClipRuntime gClip;

bool BugClipAllowed()
{
	if (!PortCeLayer()) return false;
	static const char *const kHardFences[] = {
		"BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT"
	};
	for (const char *name : kHardFences)
	{
		const char *v = getenv(name);
		if (v && *v) return false;
	}
	return true;
}
}	// namespace

extern "C" int PortCeBugClipToggle(void)
{
	if (!BugClipAllowed()) return 0;
	std::thread prevWorker;
	bool starting = false;
	{
		std::lock_guard<std::mutex> lk(gClip.mu);
		if (gClip.active)
		{
			gClip.active = false;
			gClip.stopRequested = true;
			gClip.stopNs = SDL_GetTicksNS();
			__atomic_store_n(&gAudioActive, 0, __ATOMIC_RELEASE);
			gClip.SetToastLocked("Saving bug clip...");
			gClip.cv.notify_one();
			return 1;
		}
		if (gClip.worker.joinable())
			prevWorker = std::move(gClip.worker);
		starting = true;
	}
	if (prevWorker.joinable())
		prevWorker.join();

	if (!starting) return 1;
	std::lock_guard<std::mutex> lk(gClip.mu);
	if (!gClip.atExitRegistered)
	{
		gClip.atExitRegistered = true;
		atexit(PortCeBugClipShutdown);
	}
	// Advance consumer tail to producer's current head (main thread never mutates gAudioHead).
	__atomic_store_n(&gAudioTail, __atomic_load_n(&gAudioHead, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
	gClip.qRead = gClip.qWrite = gClip.qCount = 0;
	gClip.startNs = SDL_GetTicksNS();
	gClip.stopNs = 0;
	gClip.lastCaptureNs = 0;
	gClip.stopRequested = false;
	gClip.active = true;
	__atomic_store_n(&gAudioActive, 1, __ATOMIC_RELEASE);
	gClip.SetToastLocked("Recording bug clip (F9 to stop)");
	fprintf(stderr, PORT_LOG_PREFIX "bug clip recording started (F9 to stop, max %us)\n", kMaxClipSeconds);
	gClip.worker = std::thread([] { gClip.WorkerMain(); });
	return 1;
}

extern "C" int PortCeBugClipActive(void)
{
	std::lock_guard<std::mutex> lk(gClip.mu);
	return gClip.active ? 1 : 0;
}

extern "C" int PortCeBugClipWantFrame(unsigned *outMaxW, unsigned *outMaxH)
{
	if (!__atomic_load_n(&gAudioActive, __ATOMIC_RELAXED))
		return 0;
	std::lock_guard<std::mutex> lk(gClip.mu);
	if (!gClip.active || gClip.qCount >= kFrameQueueSlots)
		return 0;
	const uint64_t now = SDL_GetTicksNS();
	if (gClip.startNs && now > gClip.startNs && (now - gClip.startNs) >= (uint64_t)kMaxClipSeconds * 1000000000ull)
	{
		gClip.active = false;
		gClip.stopRequested = true;
		if (!gClip.stopNs) gClip.stopNs = now;
		__atomic_store_n(&gAudioActive, 0, __ATOMIC_RELEASE);
		gClip.cv.notify_one();
		return 0;
	}
	if (gClip.lastCaptureNs && now >= gClip.lastCaptureNs && (now - gClip.lastCaptureNs) < kFramePeriodNs)
		return 0;
	if (outMaxW) *outMaxW = kMaxClipWidth;
	if (outMaxH) *outMaxH = kMaxClipHeight;
	return 1;
}

extern "C" void PortCeBugClipSubmitFrame(const void *pixels, int pitch, int width, int height, unsigned d3dFormat)
{
	std::vector<uint8_t> rgb;
	if (!ConvertToRgb24(pixels, pitch, width, height, d3dFormat, rgb))
		return;
	const uint64_t now = SDL_GetTicksNS();
	{
		std::lock_guard<std::mutex> lk(gClip.mu);
		if (!gClip.active || gClip.qCount >= kFrameQueueSlots)
			return;
		QueuedFrame &slot = gClip.ring[gClip.qWrite];
		slot.rgb = std::move(rgb);
		slot.width = width;
		slot.height = height;
		slot.stampNs = now;
		slot.ready = true;
		gClip.qWrite = (gClip.qWrite + 1u) % kFrameQueueSlots;
		++gClip.qCount;
		gClip.lastCaptureNs = now;
	}
	gClip.cv.notify_one();
}

extern "C" void PortCeBugClipPushAudio(const short *stereoPcm44100, int frames)
{
	if (!stereoPcm44100 || frames <= 0) return;
	if (!__atomic_load_n(&gAudioActive, __ATOMIC_ACQUIRE)) return;
	uint32_t head = __atomic_load_n(&gAudioHead, __ATOMIC_RELAXED);
	uint32_t tail = __atomic_load_n(&gAudioTail, __ATOMIC_ACQUIRE);
	uint32_t used = head - tail;
	if (used >= kAudioRingFrames) return;
	uint32_t room = kAudioRingFrames - used;
	uint32_t count = (uint32_t)frames < room ? (uint32_t)frames : room;
	uint32_t idx = head & kAudioRingMask;
	uint32_t first = count < (kAudioRingFrames - idx) ? count : (kAudioRingFrames - idx);
	memcpy(gAudioRing + (size_t)idx * 2u, stereoPcm44100, (size_t)first * kAudioBlockAlign);
	if (first < count)
		memcpy(gAudioRing, stereoPcm44100 + (size_t)first * 2u, (size_t)(count - first) * kAudioBlockAlign);
	__atomic_store_n(&gAudioHead, head + count, __ATOMIC_RELEASE);
}

extern "C" int PortCeBugClipStatus(char *out, int n, int *outRecording, float *outFade)
{
	if (!out || n <= 0) return 0;
	out[0] = 0;
	if (outRecording) *outRecording = 0;
	if (outFade) *outFade = 0.0f;
	std::lock_guard<std::mutex> lk(gClip.mu);
	if (gClip.active)
	{
		const uint64_t now = SDL_GetTicksNS();
		const unsigned sec = (now >= gClip.startNs) ? (unsigned)((now - gClip.startNs) / 1000000000ull) : 0u;
		snprintf(out, (size_t)n, "REC %02u:%02u (F9 stop)", sec / 60u, sec % 60u);
		if (outRecording) *outRecording = 1;
		if (outFade) *outFade = 1.0f;
		return 1;
	}
	if (!gClip.toast[0]) return 0;
	const uint64_t nowMs = SDL_GetTicks();
	if (nowMs >= gClip.toastUntilMs)
	{
		gClip.toast[0] = 0;
		return 0;
	}
	float fade = 1.0f;
	const uint64_t rem = gClip.toastUntilMs - nowMs;
	if (rem < 500u) fade = (float)rem / 500.0f;
	snprintf(out, (size_t)n, "%s", gClip.toast);
	if (outFade) *outFade = fade;
	return 1;
}

extern "C" void PortCeBugClipShutdown(void)
{
	std::thread t;
	{
		std::lock_guard<std::mutex> lk(gClip.mu);
		if (gClip.active && !gClip.stopNs)
			gClip.stopNs = SDL_GetTicksNS();
		gClip.active = false;
		gClip.stopRequested = true;
		__atomic_store_n(&gAudioActive, 0, __ATOMIC_RELEASE);
		gClip.cv.notify_one();
		if (gClip.worker.joinable())
			t = std::move(gClip.worker);
	}
	if (t.joinable())
		t.join();
}

#endif
