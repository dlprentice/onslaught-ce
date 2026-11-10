// SPDX-License-Identifier: GPL-3.0-only
// port/tools/tests/ce_script_guard_driver.cpp: the actual port/platform/common/ce_script_guard.cpp on the host, with fakes for the two
// things it reaches: PortModHas answers yes for the game paths named before "--" on the command line, and PortSessionLog prints
// the line it is given. Usage:
//     driver [GAME_PATH ...] -- LEVEL:ENTRY ...
// Prints "ASK LEVEL ENTRY yes|no" for each question, "LOG category error text" for each log line, then "COUNT n".
#include "ce_script_guard.h"
#include "mods.h"
#include "portlog.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static std::vector<std::string> gSupplied;

bool PortModHas(const char *game_path)
{
	for (const std::string &s : gSupplied)
		if (s == game_path)
			return true;
	return false;
}

void PortSessionLog(PortSessionCategory category, bool error, const char *format, ...)
{
	char text[512];
	va_list ap;
	va_start(ap, format);
	vsnprintf(text, sizeof(text), format, ap);
	va_end(ap);
	printf("LOG %d %d %s\n", (int)category, error ? 1 : 0, text);
}

int main(int argc, char **argv)
{
	int i = 1;
	for (; i < argc && strcmp(argv[i], "--") != 0; i++)
		gSupplied.push_back(argv[i]);
	for (i++; i < argc; i++)
	{
		int level;
		char entry[32];
		if (sscanf(argv[i], "%d:%31s", &level, entry) != 2)
			return 2;
		printf("ASK %d %s %s\n", level, entry, PortCeScriptDeferNested(level, entry) ? "yes" : "no");
	}
	printf("COUNT %d\n", PortCeScriptDeferred());
	return 0;
}
