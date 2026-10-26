/* SPDX-License-Identifier: GPL-3.0-only */
/* Portable calls to the production admission/command rules; never starts a child or opens a network connection. */
#include "../ce_launch_rules.h"
#include <stdlib.h>

int main(int argc, char **argv)
{
	if (argc == 6 && !strcmp(argv[1], "url"))
		return ce_update_url_allowed(argv[2], argv[3], argv[4], argv[5]) ? 0 : 1;
	if (argc >= 5 && !strcmp(argv[1], "cmd"))
	{
		char out[8194];
		size_t capacity = (size_t)strtoul(argv[3], NULL, 10);
		if (!capacity || capacity > 8192) return 2;
		memset(out, 0x5a, sizeof out);
		int ok = ce_windows_command(out, capacity, argv[4], argv + 5, argc - 5, atoi(argv[2]));
		if ((unsigned char)out[capacity] != 0x5a || (unsigned char)out[capacity + 1] != 0x5a) return 3;
		if (!ok && out[0]) return 4;
		puts(out);
		return ok ? 0 : 1;
	}
	return 2;
}
