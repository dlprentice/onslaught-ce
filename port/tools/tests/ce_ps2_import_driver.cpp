// SPDX-License-Identifier: GPL-3.0-only
// port/tools/tests/ce_ps2_import_driver.cpp: host ASan/UBSan driver for ce_ps2_import_tests.py.
// Links port/platform/common/ce_ps2_import.cpp with the actual mod loader (mods.cpp) and ce.ini settings (ce.cpp).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ce_ps2_import.h"
#include "mods.h"

#include <stdint.h>
extern "C" int CE_SettingGetText(const char *key, char *out, int n);
extern "C" uint64_t SDL_GetTicks(void) { return 0; }

const char *PortUserDir()
{
	const char *u = getenv("BEA_USER_DIR");
	return u ? u : ".";
}

const char *PortGameDir()
{
	const char *g = getenv("BEA_GAME_DIR");
	return g ? g : ".";
}

bool PortGameHasFile(const char *rel)
{
	std::string p = std::string(PortGameDir()) + "/" + rel;
	struct stat st{};
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void PortPathsNamesOnly() {}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: %s <sync|async|cancel|report> [source_path]\n", argv[0]);
		return 2;
	}

	const std::string cmd = argv[1];
	if (cmd == "sync")
	{
		if (argc != 3)
			return 2;
		int enabled = 0;
		char err[256] = "";
		if (!CePs2ImportRun(argv[2], PortGameDir(), PortUserDir(), &enabled, err, sizeof(err)))
		{
			printf("ERR %s\n", err);
			return 0;
		}
		char line[2048] = "";
		const int has = CE_SettingGetText("mods", line, sizeof(line));
		printf("OK enabled=%d line=%s\n", enabled, (has && line[0]) ? line : "-");
		return 0;
	}

	if (cmd == "async")
	{
		if (argc != 3)
			return 2;
		if (!CePs2ImportStart(argv[2], PortGameDir(), PortUserDir()))
		{
			printf("ERR failed to start worker\n");
			return 0;
		}
		std::vector<std::string> stages;
		CePs2ImportStatus st{};
		for (;;)
		{
			CePs2ImportPoll(&st);
			if (st.stage[0] && (stages.empty() || stages.back() != st.stage))
				stages.emplace_back(st.stage);
			if (st.state != CE_PS2_IMPORT_RUNNING)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		CePs2ImportAck();
		if (st.state != CE_PS2_IMPORT_DONE)
		{
			printf("ERR %s\n", st.error);
			return 0;
		}
		char line[2048] = "";
		const int has = CE_SettingGetText("mods", line, sizeof(line));
		std::string joined;
		for (size_t i = 0; i < stages.size(); i++)
			joined += (i ? ";" : "") + stages[i];
		printf("OK enabled=%d stages=%s line=%s\n", st.enabled,
			   joined.empty() ? "-" : joined.c_str(),
			   (has && line[0]) ? line : "-");
		return 0;
	}

	if (cmd == "cancel")
	{
		if (argc != 3)
			return 2;
		if (!CePs2ImportStart(argv[2], PortGameDir(), PortUserDir()))
		{
			printf("ERR failed to start worker\n");
			return 0;
		}
		CePs2ImportCancel();
		CePs2ImportStatus st{};
		CePs2ImportPoll(&st);
		printf("CANCELLED state=%d\n", (int)st.state);
		return 0;
	}

	if (cmd == "report")
	{
		PortModInfo rows[PORT_MODS_ROWS] = {};
		PortModsRun run{};
		const int count = PortModsReport(rows, PORT_MODS_ROWS, &run);
		int targetIdx = -1;
		for (int i = 0; i < count; i++)
			if (strcmp(rows[i].id, "flagship-duel-ps2") == 0)
				targetIdx = i;
		char modId[] = "flagship-duel-ps2";
		char *ids[1] = {modId};
		// Silence PortModsCheck stdout chatter into stderr/log summary, capture return code.
		fflush(stdout);
		const int checkRc = PortModsCheck(1, ids);
		if (targetIdx < 0)
		{
			printf("REPORT count=%d found=0 check_rc=%d\n", count, checkRc);
			return 0;
		}
		const PortModInfo &m = rows[targetIdx];
		printf("REPORT count=%d found=1 id=%s why=%d place=%d replaced=%d worlds=%d archives=%d level0=%d check_rc=%d detail=%s\n",
			   count, m.id, m.why, m.place, m.replaced, m.worlds, m.archives,
			   m.level_count > 0 ? m.levels[0] : 0, checkRc, m.detail[0] ? m.detail : "-");
		return 0;
	}

	return 2;
}
