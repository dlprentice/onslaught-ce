/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher's message box texts as the launcher shows them, for box_text_tests.py. Pure: no SDL, no game, no network.
 *   driver columns                     the wrap width
 *   driver wrap COLS CAP < text        the text wrapped at COLS characters in a buffer of CAP bytes (exit 1: it does not fit)
 *   driver texts                       every box text, with the longest arguments the launcher can give it: NAME TAB original-hex TAB wrapped-hex
 *   driver short                       every text builder given a buffer one byte too small, then exactly big enough (the results, 0 or 1) */
#include "../ce_launch_rules.h"
#include <stdio.h>
#include <stdlib.h>

static void put_hex(const char *s)
{
	for (; *s; ++s) printf("%02x", (unsigned char)*s);
}

static void put(const char *name, const char *text)
{
	char wrapped[4096];
	printf("%s\t", name);
	put_hex(text);
	printf("\t");
	if (ce_box_wrap(text, wrapped, sizeof wrapped, CE_BOX_COLUMNS)) put_hex(wrapped);
	else printf("TOO-LONG");
	printf("\n");
}

static void words(char *out, size_t n, const char *word, size_t count)
{
	size_t used = 0;
	for (size_t i = 0; i < count && used + strlen(word) + 2 < n; ++i) used += (size_t)snprintf(out + used, n - used, "%s%s", i ? " " : "", word);
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "columns"))
	{
		printf("%d\n", CE_BOX_COLUMNS);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "wrap"))
	{
		static char in[65536], out[65536];
		size_t n = fread(in, 1, sizeof in - 1, stdin), cap = (size_t)atol(argv[3]);
		in[n] = 0;
		if (cap > sizeof out) cap = sizeof out;
		if (!ce_box_wrap(in, out, cap, (unsigned)atoi(argv[2]))) return 1;
		fwrite(out, 1, strlen(out), stdout);
		return 0;
	}
	char text[4096], why[256], folder[512], version[32];
	if (argc >= 2 && !strcmp(argv[1], "texts"))
	{
		put("game-missing", CE_BOX_GAME_MISSING);
		put("record", CE_BOX_RECORD);
		put("update-download-failed", CE_BOX_UPDATE_DOWNLOAD_FAILED);
		put("update-not-the-release", CE_BOX_UPDATE_NOT_THE_RELEASE);
		put("update-install-failed", CE_BOX_UPDATE_INSTALL_FAILED);
		memset(version, 'v', sizeof version - 1);
		version[sizeof version - 1] = 0;
		if (ce_box_text_update_offer(text, sizeof text, "0.1.14", "0.1.13", 35.2)) put("update-offer", text);
		if (ce_box_text_update_offer(text, sizeof text, version, version, 999999.9)) put("update-offer-longest-versions", text);
		words(why, sizeof why, "it has no BEA.exe", 20);
		if (ce_box_text_bad_folder(text, sizeof text, "it has no BEA.exe")) put("bad-folder", text);
		if (ce_box_text_bad_folder(text, sizeof text, why)) put("bad-folder-longest-reason", text);
		memset(why, 'x', sizeof why - 1);
		why[sizeof why - 1] = 0;
		if (ce_box_text_bad_folder(text, sizeof text, why)) put("bad-folder-one-long-word", text);
		words(folder, sizeof folder, "C:\\Users\\A Long Name\\AppData\\Roaming\\Onslaught CE", 1);
		if (ce_box_text_report(text, sizeof text, 1, folder)) put("report-opened", text);
		if (ce_box_text_report(text, sizeof text, 0, folder)) put("report-not-opened", text);
		return 0;
	}
	if (argc >= 2 && !strcmp(argv[1], "short"))
	{
		char tiny[4096];
		struct { const char *name; size_t len; } sizes[3];
		/* the exact length of each text, found by building it big, then asked for in a buffer that is one byte short and exactly right */
		ce_box_text_update_offer(tiny, sizeof tiny, "0.1.14", "0.1.13", 35.2);
		sizes[0].name = "update-offer"; sizes[0].len = strlen(tiny);
		ce_box_text_bad_folder(tiny, sizeof tiny, "it has no BEA.exe");
		sizes[1].name = "bad-folder"; sizes[1].len = strlen(tiny);
		ce_box_text_report(tiny, sizeof tiny, 1, "C:\\x");
		sizes[2].name = "report"; sizes[2].len = strlen(tiny);
		for (int i = 0; i < 3; ++i)
			for (int extra = 0; extra < 2; ++extra)
			{
				char buf[4096];
				size_t cap = sizes[i].len + (size_t)extra;	/* extra 0: one byte short (no room for the terminator); extra 1: exactly right */
				int r = i == 0 ? ce_box_text_update_offer(buf, cap, "0.1.14", "0.1.13", 35.2) : i == 1 ? ce_box_text_bad_folder(buf, cap, "it has no BEA.exe") :
					ce_box_text_report(buf, cap, 1, "C:\\x");
				printf("%s %s %d\n", sizes[i].name, extra ? "exact" : "short", r);
			}
		return 0;
	}
	return 2;
}
