// SPDX-License-Identifier: GPL-3.0-only
// The live view's footprint audit, a test build's proof tool (build.py --commander-test; BEA_PORT_COMMANDER_AUDIT=1). The Commander's live view
// draws its own picture over the frame the game made and must leave the game as it found it (ce_cmdview.cpp). Around each frame of that picture
// (ce_cmdmap.cpp's StrategicPictureBegin to StrategicPictureEnd, which takes in the map's own boxes and text) this copies every heap of the
// game's memory manager and the executable's whole data before the picture and compares them after it. Every byte that differs is counted: in a
// heap by the memory type of the block that holds it, in the data by its own offset from the data's start (port/tools/ce_commander_isolation_run.py
// names the symbol it lies in). Left out of the comparison: this file's own records and the Commander's own (ce_cmdmap.cpp and ce_cmdview.cpp put theirs
// in sections of their own in a test build). The map's close note in the probe gives the counts; a proof run passes when no heap byte differs
// and every differing data byte lies where the runner says, with its reason, that the picture did not write it (the dynamic linker's table; a
// record another thread writes while the main thread draws). The comparison cannot tell whose write a byte is: it runs on the main thread around
// the picture, and the audio thread works on.
// BEA_PORT_COMMANDER_AUDIT_LEAK=1 is the audit's own control: one byte of the game's data (the height field's scratch buffer) is then flipped
// inside each audited frame, and the audit must report it.
// It writes nothing else to the game; its copies are the C library's. Nothing here is compiled into a build without the test define, and the
// audit itself is Linux's (it reads the linker's bounds of the data); elsewhere its three calls do nothing.
#if defined(CE_VERSION) && defined(CE_COMMANDER_TEST) && !defined(__linux__)
void CeCmdViewAuditBefore() {}
void CeCmdViewAuditAfter() {}
void CeCmdViewAuditReport() {}
#endif
#if defined(CE_VERSION) && defined(CE_COMMANDER_TEST) && defined(__linux__)

#include "common.h"
#include "MemoryManager.h"
#include "DXMemoryManager.h"
#include "map.h"
#include "ce_commander.h"
#include "ce_cmdview.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The executable's own data and zeroed data (the linker's bounds), and the sections left out.
extern "C" char __data_start[], _end[];
extern "C" char __start_ceaud[], __stop_ceaud[];
extern "C" char __start_cecmdd[] __attribute__((weak)), __stop_cecmdd[] __attribute__((weak));
extern "C" char __start_cecmdb[] __attribute__((weak)), __stop_cecmdb[] __attribute__((weak));
#define AUD __attribute__((section("ceaud")))

namespace
{
struct HeapAccess : CMemoryHeap
{
	static CMemoryHeap *CMemoryHeap::*Next() { return &HeapAccess::mNextHeap; }
	static void *CMemoryHeap::*Mem() { return &HeapAccess::mpMem; }
	static UINT CMemoryHeap::*Size() { return &HeapAccess::mSize; }
	static void *CMemoryHeap::*Tiny() { return &HeapAccess::mTinyHeap; }
	static void *CMemoryHeap::*TinyEnd() { return &HeapAccess::mTinyHeapEnd; }
};
struct ManagerAccess : CMemoryManager
{
	static CMemoryHeap *CMemoryManager::*First() { return &ManagerAccess::mFirstHeap; }
};

struct Span { unsigned char *at; size_t size; unsigned char *copy; size_t cap; };
const unsigned kSpans = 64;
AUD Span gSpans[kSpans] = {{0, 0, 0, 0}};
AUD unsigned gSpanCount = 0, gDataSpan = ~0u;
AUD bool gCopied = false;
AUD int gOn = -1, gLeak = 0;

struct Tally { int kind; unsigned key; unsigned long long bytes; unsigned frames, lastFrame; };	// kind 0: a heap's memory type (key); 1: a data byte's offset (key)
const unsigned kTallies = 8192;
AUD Tally *gTallies = 0;
AUD unsigned gTallyCount = 0, gFrames = 0, gDirtyFrames = 0, gFrame = 0;
AUD unsigned long long gHeapBytes = 0, gDataBytes = 0, gSpanBytes = 0;

bool On()
{
	if (gOn < 0) { gOn = getenv("BEA_PORT_COMMANDER_AUDIT") != NULL; gLeak = getenv("BEA_PORT_COMMANDER_AUDIT_LEAK") != NULL; }
	return gOn != 0;
}

void AddSpan(void *at, size_t size)
{
	if (!at || !size || gSpanCount == kSpans) return;
	Span &s = gSpans[gSpanCount++];
	s.at = (unsigned char *)at; s.size = size;
	if (s.cap < size) { free(s.copy); s.copy = (unsigned char *)malloc(size); s.cap = s.copy ? size : 0; }
}

void Count(int kind, unsigned key)
{
	if (!gTallies && !(gTallies = (Tally *)malloc(kTallies * sizeof *gTallies))) return;
	unsigned i = 0;
	while (i < gTallyCount && !(gTallies[i].kind == kind && gTallies[i].key == key)) ++i;
	if (i == gTallyCount)
	{
		if (gTallyCount == kTallies) return;
		gTallies[i].kind = kind; gTallies[i].key = key; gTallies[i].bytes = 0; gTallies[i].frames = 0; gTallies[i].lastFrame = 0;
		++gTallyCount;
	}
	++gTallies[i].bytes;
	if (gTallies[i].lastFrame != gFrame) { gTallies[i].lastFrame = gFrame; ++gTallies[i].frames; }
}

bool Within(const unsigned char *p, const char *lo, const char *hi) { return lo && hi && p >= (const unsigned char *)lo && p < (const unsigned char *)hi; }

// The memory type of the heap block that holds the byte: 256 a free block, 257 a block's header, 258 outside any block (the walk is from the
// heap's first block, by the headers as they are now).
unsigned TypeAt(const Span &s, const unsigned char *p)
{
	for (CMemoryBlock *b = (CMemoryBlock *)s.at; (unsigned char *)b < s.at + s.size;)
	{
		if (!b->IsValid()) return 258;
		CMemoryBlock *next = b->GetBlockBelow();
		if ((unsigned char *)next <= (unsigned char *)b) return 258;
		if (p < (const unsigned char *)next)
			return !b->IsUsed() ? 256 : (p < (const unsigned char *)b + MEMBLOCK_HEADER_SIZE ? 257 : ((unsigned)b->GetType() & 255));
		b = next;
	}
	return 258;
}
}

void CeCmdViewAuditBefore()
{
	if (!On()) return;
	gSpanCount = 0;
	gCopied = false;
	for (CMemoryHeap *h = MEM_MANAGER.*ManagerAccess::First(); h; h = h->*HeapAccess::Next())
	{
		AddSpan(h->*HeapAccess::Mem(), h->*HeapAccess::Size());
		unsigned char *tiny = (unsigned char *)(h->*HeapAccess::Tiny()), *end = (unsigned char *)(h->*HeapAccess::TinyEnd());
		if (tiny && end > tiny) AddSpan(tiny, (size_t)(end - tiny));
	}
	gDataSpan = gSpanCount;
	AddSpan(__data_start, (size_t)(_end - __data_start));
	gSpanBytes = 0;
	for (unsigned i = 0; i < gSpanCount; ++i)
	{
		if (!gSpans[i].copy) return;
		memcpy(gSpans[i].copy, gSpans[i].at, gSpans[i].size);
		gSpanBytes += gSpans[i].size;
	}
	gCopied = true;
	// The audit's own control: one byte of the game's data changed inside the audited frame.
	if (gLeak) ((unsigned char *)MAP.mUnknown28)[0] ^= 1;
}

void CeCmdViewAuditAfter()
{
	if (!On() || !gCopied) return;
	gCopied = false;
	++gFrames; ++gFrame;
	bool dirty = false;
	for (unsigned i = 0; i < gSpanCount; ++i)
	{
		const Span &s = gSpans[i];
		for (size_t k = 0; k < s.size; ++k)
		{
			if ((k & 4095) == 0 && k + 4096 <= s.size && memcmp(s.at + k, s.copy + k, 4096) == 0) { k += 4095; continue; }
			if (s.at[k] == s.copy[k]) continue;
			const unsigned char *p = s.at + k;
			if (i == gDataSpan)
			{
				if (Within(p, __start_ceaud, __stop_ceaud) || Within(p, __start_cecmdd, __stop_cecmdd) || Within(p, __start_cecmdb, __stop_cecmdb)) continue;
				Count(1, (unsigned)k); ++gDataBytes;
			}
			else { Count(0, TypeAt(s, p)); ++gHeapBytes; }
			dirty = true;
		}
	}
	if (dirty) ++gDirtyFrames;
}

void CeCmdViewAuditReport()
{
	if (!On()) return;
	CeCmdNote("view audit frames %u changed-frames %u heap-bytes %llu data-bytes %llu entries %u spans %u span-bytes %llu own-sections %d", gFrames,
			  gDirtyFrames, gHeapBytes, gDataBytes, gTallyCount, gSpanCount, gSpanBytes, (__start_cecmdd && __start_cecmdb) ? 1 : 0);
	for (unsigned i = 0; i < gTallyCount && i < 2000; ++i)
		CeCmdNote("view audit %s %u frames %u bytes %llu", gTallies[i].kind ? "data-byte" : "heap-type", gTallies[i].key, gTallies[i].frames, gTallies[i].bytes);
}

#endif
