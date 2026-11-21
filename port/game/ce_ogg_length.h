// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_ogg_length.h: how long an Ogg Vorbis file plays, worked out from two pieces of it: its first bytes (the identification
// header, which gives the sample rate) and its last bytes (the last page, whose position is the sample count). Nothing is decoded
// and nothing is written. The Music Player (ce_jukebox.cpp) uses it for the progress bar; a file it cannot read gives 0 and the
// page then shows the elapsed time alone. A host test runs these same functions (port/tools/ce_jukebox_tests.py).
#ifndef CE_OGG_LENGTH_H
#define CE_OGG_LENGTH_H

#include <string.h>

enum { CE_OGG_HEAD = 512, CE_OGG_TAIL = 65536, CE_OGG_START = 131072 };

namespace ce_ogg_detail
{
inline unsigned	U32(const unsigned char *p)
{
	return unsigned(p[0]) | (unsigned(p[1]) << 8) | (unsigned(p[2]) << 16) | (unsigned(p[3]) << 24);
}
}

// The sample rate from a file's first bytes: 0 when they do not begin with an Ogg page holding a Vorbis identification header.
inline unsigned	CeOggSampleRate(const unsigned char *head, int size)
{
	if (!head || (size < 27) || memcmp(head, "OggS", 4) || head[4] != 0)
		return 0;
	const int segments = head[26];
	const int start = 27 + segments;
	if (size < start + 16)
		return 0;
	const unsigned char *packet = head + start;
	if (packet[0] != 1 || memcmp(packet + 1, "vorbis", 6))
		return 0;
	return ce_ogg_detail::U32(packet + 12);
}

// Whether a file's first bytes hold a stream the game's music worker can start: its three header packets (identification, comments,
// setup) and then at least one audio packet, in whole, well-formed pages that follow one another. A file cut inside its headers, or
// with nothing after them, plays nothing and the worker gives no sign. complete says the bytes are the whole file; when they are only
// its beginning and run out before the proof, nothing is wrong yet and the answer is true.
inline bool	CeOggStarts(const unsigned char *bytes, int size, bool complete)
{
	if (!bytes || !CeOggSampleRate(bytes, size))
		return false;
	int at = 0, packets = 0;
	while (packets < 4)
	{
		if (at + 27 > size)
			return !complete;
		const unsigned char *page = bytes + at;
		if (memcmp(page, "OggS", 4) || page[4] != 0)
			return false;
		const int segments = page[26];
		if (at + 27 + segments > size)
			return !complete;
		int body = 0, ended = 0;
		for (int s = 0; s < segments; s++)
		{
			body += page[27 + s];
			if (page[27 + s] < 255)
				ended++;						// a lacing value under 255 ends a packet
		}
		if (at + 27 + segments + body > size)
			return !complete;					// the page runs past the bytes given
		packets += ended;
		at += 27 + segments + body;
	}
	return true;
}

// The sample count from the file's last bytes: the position of the last whole page found there, or 0 when there is none or it
// holds no position (an all-ones position means no packet ended on the page).
inline unsigned long long	CeOggSamples(const unsigned char *tail, int size)
{
	using ce_ogg_detail::U32;
	if (!tail || (size < 27))
		return 0;
	unsigned long long best = 0;
	for (int at = 0; at + 27 <= size; at++)
	{
		if (memcmp(tail + at, "OggS", 4) || tail[at + 4] != 0)
			continue;
		const int segments = tail[at + 26];
		if (at + 27 + segments > size)
			continue;
		int body = 0;
		for (int s = 0; s < segments; s++)
			body += tail[at + 27 + s];
		if (at + 27 + segments + body > size)
			continue;						// a page cut short at the end of the bytes given
		const unsigned low = U32(tail + at + 6), high = U32(tail + at + 10);
		if (low == 0xffffffffu && high == 0xffffffffu)
			continue;
		best = (static_cast<unsigned long long>(high) << 32) | low;
	}
	return best;
}

// Whole seconds from the two pieces; 0 when either is unreadable.
inline int	CeOggSeconds(const unsigned char *head, int headSize, const unsigned char *tail, int tailSize)
{
	const unsigned rate = CeOggSampleRate(head, headSize);
	if (!rate)
		return 0;
	const unsigned long long samples = CeOggSamples(tail, tailSize);
	return int((samples + rate / 2) / rate);
}

#endif
