// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce.cpp: whether the CE layer (additions over the faithful port, such as the version text of
// patch 0037) may draw: never in a replay or a recording (the virtual or shim clock, an input tape, probes), so those
// runs stay identical to the faithful build. Public builds only define CE_VERSION; the answer is fixed at start. Also the
// console's developer-chatter switch (portlog.h) and the public build's routing of the game's printf (port.h).
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portlog.h"

bool PortCeLayer()
{
	static int on = -1;
	if (on < 0)
	{
		const char *force = getenv("BEA_PORT_CE_LAYER");	// screenshots of the CE layer during a replay: 1 forces it on
		if (force && *force)
			return (on = force[0] == '1');
		const char *names[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"};
		on = 1;
		for (const char *n : names)
		{
			const char *v = getenv(n);
			if (v && *v)
				on = 0;
		}
	}
	return on == 1;
}

// Developer chatter on the console (portlog.h): private builds always; public builds only with BEA_PORT_VERBOSE=1.
bool PortDevLog()
{
#if defined(CE_VERSION)
	static int on = -1;
	if (on < 0)
	{
		const char *v = getenv("BEA_PORT_VERBOSE");
		on = v && *v && *v != '0';
	}
	return on == 1;
#else
	return true;
#endif
}

// The game's printf in public builds (port.h): its debugging lines are developer chatter; everything else prints.
int PortGamePrintf(const char *fmt, ...)
{
	char line[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	static const char *const chatter[] = {"Pause() called\n"};
	if (!PortDevLog())
		for (const char *c : chatter)
			if (strcmp(line, c) == 0)
				return n;
	fputs(line, stdout);
	return n;
}

// "Report a problem" for the CE layer's menu: opens the pre-filled GitHub issue page in the player's browser (the
// launcher's --report, port/updater/ce_report.h). Nothing is sent; the player submits the page or not. Returns 1 when the
// browser was asked to open it, 0 otherwise, and always 0 where the CE layer is off (replays, recordings, private builds).
#include <stdio.h>
#include "../../updater/ce_report.h"
#ifndef _WIN32
#include <sys/stat.h>
#endif

extern "C" int CE_OpenReportPage(void)
{
#ifdef CE_VERSION
	if (!PortCeLayer())
		return 0;
	static char url[32768];
	char user[4096];
	ce_user_dir(user, sizeof(user));
	int opened = ce_open_report(CE_VERSION, getenv("BEA_CE_BUILD"), user, url, sizeof(url));
	fprintf(stderr, PORT_LOG_PREFIX "report page %s: %s\n", opened ? "opened" : "not opened", url);
	return opened;
#else
	return 0;
#endif
}

// ce.ini, the launcher's key=value file in the per-user folder (port/updater/README.md), as the CE menus read and write
// it: record_sessions (the launcher reads it at the next start) and the Cheats page's switches, cheat_<code>=1. The
// game reads the file once and keeps what it writes; other lines are kept as they are.
static const char *const kCheatKeys[6] = {"cheat_malloy", "cheat_turkey", "cheat_v3r5iof", "cheat_maladim", "cheat_aurore",
										  "cheat_latete"};

static void ce_ini_path(char *out, size_t n)
{
	char user[4096];
	ce_user_dir(user, sizeof(user));
#ifdef _WIN32
	snprintf(out, n, "%s\\ce.ini", user);
#else
	snprintf(out, n, "%s/ce.ini", user);
#endif
}

static char *ce_ini_read()
{
	char path[4200];
	ce_ini_path(path, sizeof(path));
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	char *t = NULL;
	size_t len = 0, cap = 0, got;
	char buf[4096];
	while ((got = fread(buf, 1, sizeof(buf), f)) > 0 && len < (1u << 20))
	{
		if (len + got + 1 > cap)
		{
			cap = (len + got + 1) * 2;
			char *m = (char *)realloc(t, cap);
			if (!m)
				break;
			t = m;
		}
		memcpy(t + len, buf, got);
		len += got;
		t[len] = 0;
	}
	fclose(f);
	return t;
}

// The value of key in ce.ini as an integer, or def when it is missing.
extern "C" int CE_SettingGet(const char *key, int def)
{
	char *t = ce_ini_read();
	if (!t)
		return def;
	size_t k = strlen(key);
	int v = def;
	for (char *l = t; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL)
		if (!strncmp(l, key, k) && l[k] == '=')
		{
			v = atoi(l + k + 1);
			break;
		}
	free(t);
	return v;
}

// Sets key=value in ce.ini (replacing its line, or adding one) through ce_ini_write's write-and-rename, so a failed write
// leaves the old file whole; 1 when the file was written, 0 (and a log line) when it was not.
extern "C" int CE_SettingSet(const char *key, int value)
{
	char path[4200];
	ce_ini_path(path, sizeof(path));
	char user[4096];
	ce_user_dir(user, sizeof(user));
#ifdef _WIN32
	CreateDirectoryA(user, NULL);
#else
	char mk[4200];
	snprintf(mk, sizeof(mk), "%s", user);
	for (char *p = mk + 1; *p; p++)		// the folder and its parents, as the launcher makes them
		if (*p == '/')
		{
			*p = 0;
			mkdir(mk, 0755);
			*p = '/';
		}
	mkdir(mk, 0755);
#endif
	char v[16];
	snprintf(v, sizeof(v), "%d", value);
	if (!ce_ini_write(path, key, v))
	{
		fprintf(stderr, PORT_LOG_PREFIX "could not write ce.ini (%s=%d); it is unchanged\n", key, value);
		return 0;
	}
	return 1;
}

static int sCheats[6] = {-1, -1, -1, -1, -1, -1};	// -1 until read from ce.ini

// Whether the Cheats page has cheat n (IsCheatActive's number, 0..5) on: always 0 where the CE layer is off, so replays,
// recordings and the harness see only the career's name, as retail does.
extern "C" int CE_CheatOn(int n)
{
#ifdef CE_VERSION
	if (n < 0 || n >= 6 || !PortCeLayer())
		return 0;
	if (sCheats[n] < 0)
		for (int i = 0; i < 6; i++)
			sCheats[i] = CE_SettingGet(kCheatKeys[i], 0) ? 1 : 0;
	return sCheats[n];
#else
	return 0;
#endif
}

// Switches cheat n and saves it in ce.ini; 1 when saved, 0 (the switch as it was) when ce.ini could not be written.
extern "C" int CE_CheatSet(int n, int on)
{
#ifdef CE_VERSION
	if (n < 0 || n >= 6 || !PortCeLayer())
		return 0;
	CE_CheatOn(n);
	if (!CE_SettingSet(kCheatKeys[n], on ? 1 : 0))
		return 0;
	sCheats[n] = on ? 1 : 0;
	fprintf(stderr, PORT_LOG_PREFIX "cheat %s %s\n", kCheatKeys[n] + 6, on ? "on" : "off");
	return 1;
#else
	return 0;
#endif
}

// The per-user folder for an in-game message, in characters the game's font has (shown(c) is CDXBitmapFont::CanDisplay;
// bytes above 0x7f count as missing, since the message turns bytes into Latin-1 characters). The font has no "~", "\\",
// "_", "$", "@", "[", "]", "^", "{", "|" or "}" (src/Font.cpp, FONT_UNMAPPED_CHARACTERS): the full path, with "/" for
// Windows' "\\"; else the home part in words (Linux "your home folder/...") or %APPDATA% (Windows); else the folder in
// words a player can follow without a command line (ce_user_dir, port/updater/ce_report.h, says which folder it is).
// Returns 1 when out is a folder that "/tapes" can follow, 0 when it is a description.
static int ce_all_shown(const char *s, int (*shown)(int))
{
	for (const unsigned char *c = (const unsigned char *)s; *c; c++)
		if (*c > 0x7f || !shown(*c))
			return 0;
	return 1;
}

static void ce_forward_slashes(char *s)
{
	for (; *s; s++)
		if (*s == '\\')
			*s = '/';
}

extern "C" int CE_UserDirForDialog(char *out, int n, int (*shown)(int))
{
	char user[4096], base[4096];
	ce_user_dir(user, sizeof(user));
	ce_forward_slashes(user);
	if (ce_all_shown(user, shown))
	{
		snprintf(out, (size_t)n, "%s", user);
		return 1;
	}
#ifdef _WIN32
	const char *env = getenv("APPDATA"), *name = "%APPDATA%";
#else
	const char *env = getenv("HOME"), *name = "your home folder";
#endif
	snprintf(base, sizeof(base), "%s", env ? env : "");
	ce_forward_slashes(base);
	size_t l = strlen(base);
	while (l > 1 && base[l - 1] == '/')
		base[--l] = 0;
	const char *custom = getenv("BEA_USER_DIR");
	if (l && !(custom && *custom) && !strncmp(user, base, l) && user[l] == '/' && ce_all_shown(user + l, shown))
	{
		snprintf(out, (size_t)n, "%s%s", name, user + l);
		return 1;
	}
	const char *xdg = getenv("XDG_DATA_HOME");
	if (custom && *custom)
		snprintf(out, (size_t)n, "the folder that was chosen for Onslaught CE's careers and settings");
#ifdef _WIN32
	else
		snprintf(out, (size_t)n, "the Onslaught CE folder in your AppData Roaming folder (to open it, type %%APPDATA%% "
				 "into File Explorer's address bar)");
#else
	else if (xdg && *xdg)
		snprintf(out, (size_t)n, "the onslaught-ce folder in your data folder (the folder your XDG data home setting "
				 "names)");
	else
		snprintf(out, (size_t)n, "the onslaught-ce folder in .local/share in your home folder (.local is a hidden folder: in "
				 "your file manager, press Ctrl+H to show it)");
#endif
	return 0;
}

// The build commit the launcher passed (BEA_CE_BUILD), or "".
extern "C" const char *CE_BuildName(void)
{
	const char *b = getenv("BEA_CE_BUILD");
	return b ? b : "";
}
