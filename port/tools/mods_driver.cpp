// SPDX-License-Identifier: GPL-3.0-only
// port/tools/mods_driver.cpp: drives the port's mod layer (platform/common/mods.cpp, with ce.cpp's ce.ini reader) on
// the host for mods_tests.py; no game, device or network. Commands on stdin, one per line:
//   active        prints "ACTIVE <count> <fence or ->"
//   open <path>   prints "OPEN <sha256> <bytes>" for the mod copy PortModOpenRead returns, or "NONE"
//   has <path>    prints "HAS 1" when PortModHas says an active mod supplies the path, else "HAS 0" (the file is
//                 re-checked against its startup identity; nothing is read)
// BEA_USER_DIR is the user folder; BEA_GAME_DIR the game folder whose files a mod may replace.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "mods.h"
extern "C" {
#include "sha256.h"
}

const char *PortUserDir()
{
	const char *u = getenv("BEA_USER_DIR");
	return u ? u : ".";
}

bool PortGameHasFile(const char *rel)
{
	const char *g = getenv("BEA_GAME_DIR");
	std::string p = std::string(g ? g : ".") + "/" + rel;
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

int main()
{
	char line[4096];
	int held = -1;
	while (fgets(line, sizeof line, stdin))
	{
		line[strcspn(line, "\r\n")] = 0;
		if (!strcmp(line, "active"))
		{
			int n = PortModsActive();
			const char *fence = PortModsFence();
			printf("ACTIVE %d %s\n", n, fence ? fence : "-");
		}
		else if (!strncmp(line, "has ", 4))
			printf("HAS %d\n", PortModHas(line + 4) ? 1 : 0);
		else if (!strncmp(line, "hold ", 5))
		{
			if (held >= 0)
				close(held);
			held = PortModOpenRead(line + 5);
			printf("%s\n", held >= 0 ? "HELD" : "NONE");
		}
		else if (!strcmp(line, "held-write"))
		{
			char b = 'x';
			errno = 0;
			ssize_t n = held < 0 ? -1 : pwrite(held, &b, 1, 0);
			printf("WRITE %zd %d\n", n, errno);
		}
		else if (!strncmp(line, "open ", 5) || !strcmp(line, "held-read"))
		{
			int fd = !strcmp(line, "held-read") ? held : PortModOpenRead(line + 5);
			if (!strcmp(line, "held-read"))
				held = -1;
			if (fd < 0)
				printf("NONE\n");
			else
			{
				SHA256_CTX c;
				sha256_init(&c);
				unsigned char buf[65536], sum[32];
				ssize_t n;
				unsigned long long total = 0;
				while ((n = read(fd, buf, sizeof buf)) > 0)
				{
					sha256_update(&c, buf, (size_t)n);
					total += (unsigned long long)n;
				}
				close(fd);
				sha256_final(&c, sum);
				printf("OPEN ");
				for (unsigned char b : sum)
					printf("%02x", b);
				printf(" %llu\n", total);
			}
		}
		else
			printf("?\n");
		fflush(stdout);
	}
	if (held >= 0)
		close(held);
	return 0;
}
