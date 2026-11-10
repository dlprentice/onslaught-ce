/* SPDX-License-Identifier: GPL-3.0-only */
/* Pure admission/command helpers and the text of the launcher's message boxes, shared with the isolated tests; no network or process side effects. */
#ifndef CE_LAUNCH_RULES_H
#define CE_LAUNCH_RULES_H
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define CE_RELEASE_ASSETS "https://github.com/dlprentice/onslaught-ce/releases/download/"

static inline int ce_release_component(const char *s, size_t n)
{
	if (!n || (n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.')) return 0;
	for (size_t i = 0; i < n; ++i)
		if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'Z') ||
			  (s[i] >= 'a' && s[i] <= 'z') || s[i] == '.' || s[i] == '-' || s[i] == '+')) return 0;
	return 1;
}

static inline int ce_update_url_allowed(const char *url, const char *version, const char *platform, const char *extension)
{
	const size_t base = sizeof CE_RELEASE_ASSETS - 1;
	char name[256];
	if (!ce_release_component(version, strlen(version)) || strncmp(url, CE_RELEASE_ASSETS, base)) return 0;
	const char *tag = url + base, *slash = strchr(tag, '/');
	if (!slash || !ce_release_component(tag, (size_t)(slash - tag))) return 0;
	int n = snprintf(name, sizeof name, "onslaught-ce-%s-%s.%s", version, platform, extension);
	/* Only one safe tag component and this platform's exact versioned filename: no percent escapes, dot segments,
	 * alternate host/repository, query, fragment or extra path component can reach curl's URL normalization. */
	return n > 0 && (size_t)n < sizeof name && !strcmp(slash + 1, name);
}

static inline int ce_command_put(char *out, size_t capacity, size_t *used, char value)
{
	if (*used + 1 >= capacity) return 0;
	out[(*used)++] = value;
	out[*used] = 0;
	return 1;
}

static inline int ce_command_argument(char *out, size_t capacity, size_t *used, const char *arg, int quoted)
{
	if (quoted && !ce_command_put(out, capacity, used, '"')) return 0;
	while (*arg)
	{
		size_t slashes = 0;
		while (*arg == '\\') { ++slashes; ++arg; }
		/* Windows CRT command-line rules: backslashes before a quote (or a closing quote) are doubled. */
		size_t count = quoted && (!*arg || *arg == '"') ? slashes * 2 : slashes;
		while (count--) if (!ce_command_put(out, capacity, used, '\\')) return 0;
		if (!*arg) break;
		if (quoted && *arg == '"' && !ce_command_put(out, capacity, used, '\\')) return 0;
		if (!ce_command_put(out, capacity, used, *arg++)) return 0;
	}
	return !quoted || ce_command_put(out, capacity, used, '"');
}

static inline int ce_windows_command(char *out, size_t capacity, const char *exe, char *const args[], int nargs, int raw)
{
	size_t used = 0;
	if (!capacity) return 0;
	out[0] = 0;
	if (!ce_command_argument(out, capacity, &used, exe, 1)) goto fail;
	for (int i = 0; i < nargs; ++i)
		if (!ce_command_put(out, capacity, &used, ' ') ||
			!ce_command_argument(out, capacity, &used, args[i], !raw)) goto fail;
	return 1;
fail:
	out[0] = 0; /* A caller must never spawn a truncated command. */
	return 0;
}

/* ---- the text of the launcher's message boxes ----------------------------------------------------------------------------- */
/* SDL's dialogs size themselves to the longest line and never wrap (its X11 one and its Windows one; it tells zenity --no-wrap): the session
 * recording question's 221-character line made a dialog 1,698 pixels wide on a 1,280-pixel screen. Every box is wrapped at this many characters
 * first, which stays under 800 pixels in the widest font those dialogs use (about 10 pixels a character). */
#define CE_BOX_COLUMNS 76

/* Copies `in` into `out` (capacity n) and turns spaces into line feeds so that no line is longer than `cols` characters. A line feed that
 * is already there stays, a word longer than a line stays whole, and a multi-byte UTF-8 character counts once. 0 when it does not fit. */
static inline int ce_box_wrap(const char *in, char *out, size_t n, unsigned cols)
{
	size_t len = strlen(in);
	if (!cols || len + 1 > n) return 0;
	memcpy(out, in, len + 1);
	unsigned col = 0, since = 0;	/* characters on this line; characters after the last space on it */
	size_t space = (size_t)-1;	/* where that space is, or none */
	for (size_t i = 0; i < len; ++i)
	{
		unsigned char c = (unsigned char)out[i];
		if (c == '\n') { col = since = 0; space = (size_t)-1; continue; }
		if ((c & 0xC0) == 0x80) continue;	/* the rest of a multi-byte character */
		++col;
		if (c == ' ')
		{
			if (col > cols) { out[i] = '\n'; col = since = 0; space = (size_t)-1; }	/* the line is full: this space is the break */
			else { space = i; since = 0; }
			continue;
		}
		++since;
		if (col > cols && space != (size_t)-1)
		{
			out[space] = '\n';	/* a word crossed the edge: it starts the next line */
			col = since;
			space = (size_t)-1;
		}
	}
	return 1;
}

/* The questions and notices themselves, in one place so that one test can look at all of them (box_text_tests.py). The "(preview)" style
 * lines of the game's own pages are not here: these are the launcher's. */
#define CE_BOX_GAME_MISSING \
	"Onslaught CE plays the game files of your own copy of Battle Engine Aquila, and could not find it in your Steam libraries.\n\n" \
	"Choose the folder that contains BEA.exe (the game's Steam or disc install folder). Onslaught CE only reads from it."

#define CE_BOX_RECORD \
	"Session recording is on. A recording keeps your keyboard and mouse input and copies some files from your CE user folder, " \
	"which can help reproduce a reported problem.\n\n" \
	"Direct gamepad and flight-stick controls are unavailable while recording; use the keyboard and mouse. " \
	"By default the game runs at 30 frames a second with the original menus, so the Onslaught CE menu and cheats are hidden.\n\n" \
	"Record: record this session.\n" \
	"Not this time: play without recording; you will be asked again next time.\n" \
	"Stop recording: play without recording, and turn session recording off."

#define CE_BOX_UPDATE_AGAIN "Nothing was changed, and this version is starting now. You will be offered the update again next time."
#define CE_BOX_UPDATE_DOWNLOAD_FAILED "The update could not be downloaded.\n\n" CE_BOX_UPDATE_AGAIN
#define CE_BOX_UPDATE_NOT_THE_RELEASE \
	"The update was not installed.\n\nThe file that was downloaded is not the one the signed release lists, so it was thrown away. " CE_BOX_UPDATE_AGAIN
#define CE_BOX_UPDATE_INSTALL_FAILED "The update could not be installed.\n\n" CE_BOX_UPDATE_AGAIN

static inline int ce_box_text_update_offer(char *out, size_t n, const char *offered, const char *running, double megabytes)
{
	int k = snprintf(out, n, "Onslaught CE V%s is available (you have V%s).\n\n"
		"Update: download it (%.1f MB), check its signature and install it inside this Onslaught CE folder, beside "
		"this version, which is kept. Your careers and settings are not changed.\n"
		"Later: start this version; you will be asked again next time.\n"
		"Skip this version: start this version and do not ask about V%s again.", offered, running, megabytes, offered);
	return k > 0 && (size_t)k < n;
}

static inline int ce_box_text_bad_folder(char *out, size_t n, const char *why)
{
	int k = snprintf(out, n, "That folder is not a supported Battle Engine Aquila install: %s.\n\n"
		"Choose the folder that contains BEA.exe. If it is a copy of the game that should work, please report it as a problem.", why);
	return k > 0 && (size_t)k < n;
}

static inline int ce_box_text_report(char *out, size_t n, int opened, const char *folder)
{
	int k = snprintf(out, n, "%s\n\nPlease attach the logs that exist: onslaught-ce.log, onslaught-ce.old.log, "
		"onslaught-ce.concurrent-1.log and onslaught-ce.concurrent-2.log from this folder:\n%s",
		opened ? "The problem report page is open in your browser." : "Your browser could not be opened.", folder);
	return k > 0 && (size_t)k < n;
}
#endif
