// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_ps2_import.h: import cut multiplayer Level 888 from the player's own PS2 disc image
// (.iso, single-ISO .zip, or extracted LVLR .aya) into <user folder>/mods/flagship-duel-ps2/.
// Plain C/POD types only: included from both port code (pcxx) and game menu code (qcxx).
#ifndef CE_PS2_IMPORT_H
#define CE_PS2_IMPORT_H

#include <stddef.h>

enum CePs2ImportPickState
{
	CE_PS2_PICK_IDLE = 0,
	CE_PS2_PICK_OPEN,
	CE_PS2_PICK_READY,
	CE_PS2_PICK_CANCELLED,
	CE_PS2_PICK_ERROR
};

struct CePs2ImportPickStatus
{
	CePs2ImportPickState	state;
	char					path[4096];
	char					leaf[256];
	char					message[192];
};

enum CePs2ImportState
{
	CE_PS2_IMPORT_IDLE = 0,
	CE_PS2_IMPORT_RUNNING,
	CE_PS2_IMPORT_DONE,
	CE_PS2_IMPORT_FAILED,
	CE_PS2_IMPORT_CANCELLED
};

struct CePs2ImportStatus
{
	CePs2ImportState	state;
	int					enabled;	// 1 when flagship-duel-ps2 is enabled in ce.ini; 0 when 16 mods were already enabled
	char				stage[128];
	char				error[256];
};

void	CePs2ImportPickEnter();
void	CePs2ImportPickLeave();
void	CePs2ImportPickOpen();
void	CePs2ImportPickPoll(CePs2ImportPickStatus *out);
void	CePs2ImportPickAck();

bool	CePs2ImportStart(const char *source_path, const char *game_dir, const char *user_dir);
void	CePs2ImportPoll(CePs2ImportStatus *out);
void	CePs2ImportAck();
void	CePs2ImportCancel();

// Synchronous entry point used by host tests (runs the full read-only import, stages the mod, and updates ce.ini).
bool	CePs2ImportRun(const char *source_path, const char *game_dir, const char *user_dir,
					   int *enabled_out, char *error_out, size_t error_cap);

#endif
