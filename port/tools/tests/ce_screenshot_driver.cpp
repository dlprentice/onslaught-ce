// SPDX-License-Identifier: GPL-3.0-only
// Execute the production screenshot writer (port/platform/common/ce_screenshot.cpp) on the pinned SDL library, one
// case per process; the folder is BEA_USER_DIR/screenshots, set up by ce_photo_tests.py. Prints "status=N name=NAME".
#include "ce_photo.h"
#include <SDL3/SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <vector>

namespace {
const unsigned kA8R8G8B8 = 21, kX8R8G8B8 = 22, kR5G6B5 = 23, kX1R5G5B5 = 24, kR8G8B8 = 20;
const int W = 7, H = 5;

// The test picture: red x * 36 + 1, green y * 50 + 2, blue (x + y) * 20 + 3, in the given format, rows padded to pitch.
std::vector<unsigned char> Picture(unsigned format, int *pitch)
{
	const int bpp = (format == kR5G6B5 || format == kX1R5G5B5) ? 2 : 4;
	*pitch = W * bpp + 12;
	std::vector<unsigned char> bits(size_t(*pitch) * H, 0xcd);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			unsigned r = x * 36 + 1, g = y * 50 + 2, b = (x + y) * 20 + 3;
			unsigned char *p = &bits[size_t(y) * *pitch + x * bpp];
			if (format == kR5G6B5)
			{
				unsigned v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
				p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
			}
			else if (format == kX1R5G5B5)
			{
				unsigned v = 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
				p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
			}
			else
			{
				// X8R8G8B8 / A8R8G8B8 little-endian: B, G, R, then X or A (0 here: the back buffer's alpha is not the picture's)
				p[0] = (unsigned char)b; p[1] = (unsigned char)g; p[2] = (unsigned char)r; p[3] = 0;
			}
		}
	return bits;
}

int Save(unsigned format, int w = W, int h = H, int pitchCut = 0)
{
	int pitch = 0;
	std::vector<unsigned char> bits = Picture(format == kR8G8B8 ? kX8R8G8B8 : format, &pitch);
	if (w != W || h != H) bits.assign(size_t(w) * 4 * h + 64, 0x40), pitch = w * 4;
	char name[96];
	const int status = CE_ScreenshotSave(bits.data(), pitch - pitchCut, w, h, format, name, sizeof name);
	printf("status=%d name=%s\n", status, name);
	return status;
}

// The names a screenshot taken now or in the next few seconds would have, each made a file holding "sentinel".
void TakeNames(const char *folder, int seconds)
{
	const time_t now = time(NULL);
	for (int s = 0; s <= seconds; ++s)
	{
		const time_t t = now + s;
		struct tm local;
		localtime_r(&t, &local);
		char path[4400];
		snprintf(path, sizeof path, "%s/onslaught-ce-%04d%02d%02d-%02d%02d%02d.png", folder, local.tm_year + 1900,
				 local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
		FILE *f = fopen(path, "wb");
		if (!f) { perror(path); exit(3); }
		fputs("sentinel", f);
		fclose(f);
	}
}
}

int main(int argc, char **argv)
{
	if (argc < 2) return 3;
	const char *c = argv[1];
	char folder[4300];
	snprintf(folder, sizeof folder, "%s/screenshots", getenv("BEA_USER_DIR"));
	if (!strcmp(c, "xrgb")) Save(kX8R8G8B8);
	else if (!strcmp(c, "argb")) Save(kA8R8G8B8);
	else if (!strcmp(c, "rgb565")) Save(kR5G6B5);
	else if (!strcmp(c, "xrgb1555")) Save(kX1R5G5B5);
	else if (!strcmp(c, "many")) for (int i = 0; i < 5; ++i) Save(kX8R8G8B8);
	else if (!strcmp(c, "taken")) { TakeNames(folder, 3); Save(kX8R8G8B8); }
	else if (!strcmp(c, "bounds"))
	{
		Save(kR8G8B8);								// not a back buffer format it reads
		Save(kX8R8G8B8, 0, 5);
		Save(kX8R8G8B8, 7, 0);
		Save(kX8R8G8B8, CE_SHOT_MAX_SIDE + 1, 1);
		Save(kX8R8G8B8, 1, CE_SHOT_MAX_SIDE + 1);
		Save(kX8R8G8B8, W, H, 13 + 4);				// a pitch shorter than a row
		char name[8] = "x";
		printf("status=%d name=%s\n", CE_ScreenshotSave(NULL, 64, 4, 4, kX8R8G8B8, name, sizeof name), name);
	}
	else if (!strcmp(c, "fsize"))
	{
		// A file system that refuses the file part way: what was written is removed.
		signal(SIGXFSZ, SIG_IGN);
		struct rlimit limit = {40, 40};
		setrlimit(RLIMIT_FSIZE, &limit);
		Save(kX8R8G8B8);
	}
	else if (!strcmp(c, "one")) Save(kX8R8G8B8);
	else return 3;
	return 0;
}
