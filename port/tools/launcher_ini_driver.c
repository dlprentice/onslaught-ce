/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher's own reader of ce.ini (port/updater/ce_launch.c: read_all and ini_get) and the two tests it makes of the lines it
 * owns, update_check and record_sessions, on the ce.ini a path names. The three texts come from the launcher's and the updater's
 * sources as they stand, taken whole by ce_settings_tests.py: launcher_read.inc holds read_all and ini_get, launcher_off0.inc and
 * launcher_on1.inc the expressions the updater and the launcher test.
 * Usage: launcher_ini_driver INI off0   prints 1 when the updater turns update checking off for that file, else 0
 *        launcher_ini_driver INI on1    prints 1 when the launcher records the session for that file, else 0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATHN 4096
static char sIni[PATHN];

#include "launcher_read.inc"

int main(int argc, char **argv)
{
	char opt[64];
	if (argc != 3)
		return 2;
	snprintf(sIni, sizeof sIni, "%s", argv[1]);
	if (!strcmp(argv[2], "off0"))
	{
		printf("%d\n", (
#include "launcher_off0.inc"
			) ? 1 : 0);
		return 0;
	}
	if (!strcmp(argv[2], "on1"))
	{
		printf("%d\n", (
#include "launcher_on1.inc"
			) ? 1 : 0);
		return 0;
	}
	return 2;
}
