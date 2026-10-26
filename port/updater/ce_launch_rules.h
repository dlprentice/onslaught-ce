/* SPDX-License-Identifier: GPL-3.0-only */
/* Pure admission/command helpers shared with the isolated tests; no network or process side effects. */
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
#endif
