// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_world_provider.h: Unified content provider descriptor table for CE custom-world modes
// (Map Editor, Grand Museum Visit, Carrier Clash, Living Sandbox, Flagship Duel).
// Collapses per-mode world/archive/chunk/shutdown cascades into one data-driven table indexed by active CeMode.
#ifndef PORT_CE_WORLD_PROVIDER_H
#define PORT_CE_WORLD_PROVIDER_H

#include <stdint.h>
#include "ce_modes.h"

enum CeWorldChunkPolicy
{
	CE_CHUNK_RETAIL = 0,
	CE_CHUNK_SKIP,
	CE_CHUNK_SKIP_IF_DAMAGE_LOADED,   // Preserves 0381 DMKR crash fix (PortCeDamageIsLoaded)
	CE_CHUNK_SKIP_AND_INSTALL,        // c.Skip(); onPostEres()
	CE_CHUNK_DESERIALIZE_AND_INSTALL  // ENGINE.Deserialize(&c); onPostEres()
};

struct CeWorldContentProvider
{
	CeMode      mode;
	const char *name;
	bool        (*isActive)();
	const char *(*donorArchivePath)(int level);
	const char *(*levelDisplayName)(int level);
	int         (*readWorldBytes)(const char *wrdName, const void **outBytes, unsigned *outLen);
	CeWorldChunkPolicy (*chunkPolicy)(uint32_t fourccTag);
	void        (*onPostEres)();
	void        (*onPostReadResources)();
	bool        (*suppressCutsceneAndCareerWin)();
	bool        (*suppressTrainingLevelInstantQuit)();
	bool        (*consumeLoadRefused)();
	bool        (*consumeFrontEndReturn)(int *outTargetPage);
	bool        (*suppressLevelLost)();
	bool        (*rescuePlayerOnDeath)(int playerNumber);
	bool        (*isBeInStasis)(const void *be);
	bool        (*interceptPlayerButton)(int button, float val);
	void        (*onPreSystemShutdownCancel)();
	bool        (*skipOutdoorWorldRender)();
	void        (*renderIndoorReplacement)(int viewpoint);
};

const CeWorldContentProvider *CePolicyActiveWorldProvider();
const CeWorldContentProvider *CeWorldProviderForMode(CeMode mode);

// Called ONCE in ltshell.cpp PCLTShell::MainLoop() strictly BEFORE SYSTEM.Shutdown() and Cleanup3DEnvironment().
extern "C" void CeWorldProviderCancelAll(void);

#endif
