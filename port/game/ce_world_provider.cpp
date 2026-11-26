// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_world_provider.cpp: Unified CE custom-world provider table, world_source hook handler,
// and pre-SYSTEM.Shutdown() teardown facade (ce_world_provider.h).
#include "ce_world_provider.h"
#include "ce_hooks.h"
#include <stddef.h>

#ifdef CE_VERSION

namespace {

static constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
	return static_cast<uint32_t>(static_cast<unsigned char>(a)) |
	       (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8) |
	       (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16) |
	       (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24);
}

static constexpr uint32_t kTagEres = MakeFourCC('E', 'R', 'E', 'S');
static constexpr uint32_t kTagSurf = MakeFourCC('S', 'U', 'R', 'F');
static constexpr uint32_t kTagSshd = MakeFourCC('S', 'S', 'H', 'D');
static constexpr uint32_t kTagDmkr = MakeFourCC('D', 'M', 'K', 'R');

// Weak references to feature modules so host drivers that compile ce_world_provider.cpp selectively still link;
// full game builds always provide strong definitions (verified by windows_weak_link_tests.py).
extern "C" {
bool        PortCeEditorActive(void) __attribute__((weak));
void        PortCeEditorCancel(void) __attribute__((weak));
bool        PortCeEditorTakeReturn(void) __attribute__((weak));

bool        PortCeMuseumVisitActive(void) __attribute__((weak));
bool        PortCeMuseumAuthoredSource(void) __attribute__((weak));
bool        PortCeMuseumAuthoredMap(void) __attribute__((weak));
void        PortCeMuseumAuthoredInstall(void) __attribute__((weak));
int         PortCeMuseumVisitReadWorld(const char *, const void **, unsigned *) __attribute__((weak));
bool        PortCeMuseumVisitLoadRefused(void) __attribute__((weak));
bool        PortCeMuseumVisitTakeReturn(void) __attribute__((weak));
void        PortCeMuseumVisitCancel(void) __attribute__((weak));
bool        PortCeMuseumCinemaFire(bool, float) __attribute__((weak));
void        PortCeMuseumSprintSignal(void) __attribute__((weak));

bool        PortCeCarrierClashActive(void) __attribute__((weak));
void        PortCeCarrierClashInstallArena(void) __attribute__((weak));
int         PortCeCarrierClashReadWorld(const char *, const void **, unsigned *) __attribute__((weak));
bool        PortCeCarrierClashPreloadMeshes(void) __attribute__((weak));
bool        PortCeCarrierClashLoadRefused(void) __attribute__((weak));
bool        PortCeCarrierClashTakeReturn(void) __attribute__((weak));
int         PortCeCarrierClashBeInStasis(const void *) __attribute__((weak));
void        PortCeCarrierClashCancel(void) __attribute__((weak));

bool        PortCeLivingSandboxActive(void) __attribute__((weak));
void        PortCeLivingSandboxInstallArena(void) __attribute__((weak));
int         PortCeLivingSandboxReadWorld(const char *, const void **, unsigned *) __attribute__((weak));
bool        PortCeLivingSandboxPreloadMeshes(void) __attribute__((weak));
bool        PortCeLivingSandboxLoadRefused(void) __attribute__((weak));
bool        PortCeLivingSandboxTakeReturn(void) __attribute__((weak));
bool        PortCeLivingSandboxRescuePlayer(int) __attribute__((weak));
int         PortCeLivingSandboxBeInStasis(const void *) __attribute__((weak));
void        PortCeLivingSandboxCancel(void) __attribute__((weak));
bool        PortCeSanctuarySkipOutdoorWorld(void) __attribute__((weak));
void        PortCeSanctuaryRenderCinemaPlayer(int) __attribute__((weak));
bool        PortCeSanctuaryFire(bool, float) __attribute__((weak));

bool        PortCeFlagshipDuelActive(void) __attribute__((weak));
const char *PortCeFlagshipDuelDonorArchivePath(void) __attribute__((weak));
int         PortCeFlagshipDuelReadWorld(const char *, const void **, unsigned *) __attribute__((weak));
bool        PortCeFlagshipDuelPreloadMeshes(void) __attribute__((weak));
bool        PortCeFlagshipDuelLoadRefused(void) __attribute__((weak));
bool        PortCeFlagshipDuelTakeReturn(void) __attribute__((weak));
int         PortCeFlagshipDuelBeInStasis(const void *) __attribute__((weak));
void        PortCeFlagshipDuelCancel(void) __attribute__((weak));
}

// ---- Editor provider ----
bool EditorActive() { return PortCeEditorActive && PortCeEditorActive(); }
bool EditorSuppress() { return true; }
bool EditorTakeReturn(int *outPage) {
	if (!PortCeEditorTakeReturn || !PortCeEditorTakeReturn()) return false;
	if (outPage) *outPage = 16; // FEP_LANGUAGE_TEST
	return true;
}
void EditorCancel() { if (PortCeEditorCancel) PortCeEditorCancel(); }

// ---- Museum Visit provider ----
bool MuseumActive() { return PortCeMuseumVisitActive && PortCeMuseumVisitActive(); }
int MuseumReadWorld(const char *wrd, const void **outBytes, unsigned *outLen) {
	return PortCeMuseumVisitReadWorld ? PortCeMuseumVisitReadWorld(wrd, outBytes, outLen) : 0;
}
CeWorldChunkPolicy MuseumChunkPolicy(uint32_t tag) {
	if (!(PortCeMuseumAuthoredSource && PortCeMuseumAuthoredSource()))
		return CE_CHUNK_RETAIL;
	if (tag == kTagEres) return CE_CHUNK_SKIP_AND_INSTALL;
	if (tag == kTagSurf || tag == kTagSshd) return CE_CHUNK_SKIP;
	if (tag == kTagDmkr) return CE_CHUNK_SKIP_IF_DAMAGE_LOADED;
	return CE_CHUNK_RETAIL;
}
void MuseumPostEres() { if (PortCeMuseumAuthoredInstall) PortCeMuseumAuthoredInstall(); }
bool MuseumLoadRefused() { return PortCeMuseumVisitLoadRefused && PortCeMuseumVisitLoadRefused(); }
bool MuseumTakeReturn(int *outPage) {
	if (!PortCeMuseumVisitTakeReturn || !PortCeMuseumVisitTakeReturn()) return false;
	if (outPage) *outPage = 16;
	return true;
}
bool MuseumInterceptButton(int button, float val) {
	// BUTTON_MECH_CHARGE_GUN_POD=27, BUTTON_MECH_FIRE_GUN_POD=28, BUTTON_MECH_LANDING_JETS=31
	if ((button == 27 || button == 28) && PortCeMuseumCinemaFire && PortCeMuseumCinemaFire(button == 28, val))
		return true;
	if (button == 31 && PortCeMuseumSprintSignal) {
		PortCeMuseumSprintSignal();
		return true;
	}
	return false;
}
void MuseumCancel() { if (PortCeMuseumVisitCancel) PortCeMuseumVisitCancel(); }
bool MuseumSkipOutdoor() { return PortCeMuseumAuthoredMap && PortCeMuseumAuthoredMap(); }

// ---- Carrier Clash provider ----
bool CarrierClashActive() { return PortCeCarrierClashActive && PortCeCarrierClashActive(); }
int CarrierClashReadWorld(const char *wrd, const void **outBytes, unsigned *outLen) {
	return PortCeCarrierClashReadWorld ? PortCeCarrierClashReadWorld(wrd, outBytes, outLen) : 0;
}
CeWorldChunkPolicy CarrierClashChunkPolicy(uint32_t tag) {
	if (tag == kTagEres) return CE_CHUNK_DESERIALIZE_AND_INSTALL;
	if (tag == kTagSurf || tag == kTagSshd) return CE_CHUNK_SKIP;
	if (tag == kTagDmkr) return CE_CHUNK_SKIP_IF_DAMAGE_LOADED;
	return CE_CHUNK_RETAIL;
}
void CarrierClashPostEres() { if (PortCeCarrierClashInstallArena) PortCeCarrierClashInstallArena(); }
void CarrierClashPostReadResources() { if (PortCeCarrierClashPreloadMeshes) PortCeCarrierClashPreloadMeshes(); }
bool CarrierClashAlwaysTrue() { return true; }
bool CarrierClashLoadRefused() { return PortCeCarrierClashLoadRefused && PortCeCarrierClashLoadRefused(); }
bool CarrierClashTakeReturn(int *outPage) {
	if (!PortCeCarrierClashTakeReturn || !PortCeCarrierClashTakeReturn()) return false;
	if (outPage) *outPage = 16;
	return true;
}
bool CarrierClashInStasis(const void *be) { return PortCeCarrierClashBeInStasis && PortCeCarrierClashBeInStasis(be) != 0; }
void CarrierClashCancel() { if (PortCeCarrierClashCancel) PortCeCarrierClashCancel(); }

// ---- Living Sandbox provider ----
bool SandboxActive() { return PortCeLivingSandboxActive && PortCeLivingSandboxActive(); }
int SandboxReadWorld(const char *wrd, const void **outBytes, unsigned *outLen) {
	return PortCeLivingSandboxReadWorld ? PortCeLivingSandboxReadWorld(wrd, outBytes, outLen) : 0;
}
CeWorldChunkPolicy SandboxChunkPolicy(uint32_t tag) {
	if (tag == kTagEres) return CE_CHUNK_DESERIALIZE_AND_INSTALL;
	if (tag == kTagSurf || tag == kTagSshd) return CE_CHUNK_SKIP;
	if (tag == kTagDmkr) return CE_CHUNK_SKIP_IF_DAMAGE_LOADED;
	return CE_CHUNK_RETAIL;
}
void SandboxPostEres() { if (PortCeLivingSandboxInstallArena) PortCeLivingSandboxInstallArena(); }
void SandboxPostReadResources() { if (PortCeLivingSandboxPreloadMeshes) PortCeLivingSandboxPreloadMeshes(); }
bool SandboxAlwaysTrue() { return true; }
bool SandboxLoadRefused() { return PortCeLivingSandboxLoadRefused && PortCeLivingSandboxLoadRefused(); }
bool SandboxTakeReturn(int *outPage) {
	if (!PortCeLivingSandboxTakeReturn || !PortCeLivingSandboxTakeReturn()) return false;
	if (outPage) *outPage = 16;
	return true;
}
bool SandboxRescuePlayer(int playerNumber) {
	return PortCeLivingSandboxRescuePlayer && PortCeLivingSandboxRescuePlayer(playerNumber);
}
bool SandboxInStasis(const void *be) { return PortCeLivingSandboxBeInStasis && PortCeLivingSandboxBeInStasis(be) != 0; }
bool SandboxInterceptButton(int button, float val) {
	if ((button == 27 || button == 28) && PortCeSanctuaryFire && PortCeSanctuaryFire(button == 28, val))
		return true;
	return false;
}
void SandboxCancel() { if (PortCeLivingSandboxCancel) PortCeLivingSandboxCancel(); }
bool SandboxSkipOutdoor() { return PortCeSanctuarySkipOutdoorWorld && PortCeSanctuarySkipOutdoorWorld(); }
void SandboxRenderIndoor(int viewpoint) {
	if (PortCeSanctuaryRenderCinemaPlayer) PortCeSanctuaryRenderCinemaPlayer(viewpoint);
}

// ---- Flagship Duel provider ----
bool FlagshipDuelActive() { return PortCeFlagshipDuelActive && PortCeFlagshipDuelActive(); }
const char *FlagshipDuelDonorArchive(int level) {
	if (level < 0 || !PortCeFlagshipDuelDonorArchivePath) return NULL;
	return PortCeFlagshipDuelDonorArchivePath();
}
const char *FlagshipDuelLevelName(int level) {
	return (level == 788) ? "Flagship Duel" : NULL;
}
int FlagshipDuelReadWorld(const char *wrd, const void **outBytes, unsigned *outLen) {
	return PortCeFlagshipDuelReadWorld ? PortCeFlagshipDuelReadWorld(wrd, outBytes, outLen) : 0;
}
void FlagshipDuelPostReadResources() { if (PortCeFlagshipDuelPreloadMeshes) PortCeFlagshipDuelPreloadMeshes(); }
bool FlagshipDuelAlwaysTrue() { return true; }
bool FlagshipDuelLoadRefused() { return PortCeFlagshipDuelLoadRefused && PortCeFlagshipDuelLoadRefused(); }
bool FlagshipDuelTakeReturn(int *outPage) {
	if (!PortCeFlagshipDuelTakeReturn || !PortCeFlagshipDuelTakeReturn()) return false;
	if (outPage) *outPage = 16;
	return true;
}
bool FlagshipDuelInStasis(const void *be) { return PortCeFlagshipDuelBeInStasis && PortCeFlagshipDuelBeInStasis(be) != 0; }
void FlagshipDuelCancel() { if (PortCeFlagshipDuelCancel) PortCeFlagshipDuelCancel(); }

const CeWorldContentProvider kProviders[] = {
	{
		CE_MODE_EDITOR, "editor", EditorActive,
		NULL, NULL, NULL, NULL, NULL, NULL,
		EditorSuppress, NULL, NULL, EditorTakeReturn,
		NULL, NULL, NULL, NULL, EditorCancel, NULL, NULL
	},
	{
		CE_MODE_MUSEUM_VISIT, "museum_visit", MuseumActive,
		NULL, NULL, MuseumReadWorld, MuseumChunkPolicy, MuseumPostEres, NULL,
		EditorSuppress, NULL, MuseumLoadRefused, MuseumTakeReturn,
		NULL, NULL, NULL, MuseumInterceptButton, MuseumCancel, MuseumSkipOutdoor, NULL
	},
	{
		CE_MODE_CARRIER_CLASH, "carrier_clash", CarrierClashActive,
		NULL, NULL, CarrierClashReadWorld, CarrierClashChunkPolicy, CarrierClashPostEres, CarrierClashPostReadResources,
		CarrierClashAlwaysTrue, CarrierClashAlwaysTrue, CarrierClashLoadRefused, CarrierClashTakeReturn,
		NULL, NULL, CarrierClashInStasis, NULL, CarrierClashCancel, NULL, NULL
	},
	{
		CE_MODE_LIVING_SANDBOX, "living_sandbox", SandboxActive,
		NULL, NULL, SandboxReadWorld, SandboxChunkPolicy, SandboxPostEres, SandboxPostReadResources,
		SandboxAlwaysTrue, NULL, SandboxLoadRefused, SandboxTakeReturn,
		SandboxAlwaysTrue, SandboxRescuePlayer, SandboxInStasis, SandboxInterceptButton, SandboxCancel,
		SandboxSkipOutdoor, SandboxRenderIndoor
	},
	{
		CE_MODE_FLAGSHIP_DUEL, "flagship_duel", FlagshipDuelActive,
		FlagshipDuelDonorArchive, FlagshipDuelLevelName, FlagshipDuelReadWorld, NULL, NULL, FlagshipDuelPostReadResources,
		FlagshipDuelAlwaysTrue, NULL, FlagshipDuelLoadRefused, FlagshipDuelTakeReturn,
		NULL, NULL, FlagshipDuelInStasis, NULL, FlagshipDuelCancel, NULL, NULL
	},
};

} // namespace

const CeWorldContentProvider *CeWorldProviderForMode(CeMode mode)
{
	for (const auto &p : kProviders)
		if (p.mode == mode)
			return &p;
	return NULL;
}

const CeWorldContentProvider *CePolicyActiveWorldProvider()
{
	for (const auto &p : kProviders)
		if (p.isActive && p.isActive())
			return &p;
	return NULL;
}

extern "C" void CeWorldProviderCancelAll(void)
{
	// Cancel order preserves exact pre-SYSTEM.Shutdown() teardown safety (Museum -> Editor -> Living Sandbox ->
	// Carrier Clash -> Flagship Duel), strictly BEFORE SYSTEM.Shutdown() and Cleanup3DEnvironment().
	if (PortCeMuseumVisitCancel) PortCeMuseumVisitCancel();
	if (PortCeEditorCancel) PortCeEditorCancel();
	if (PortCeLivingSandboxCancel) PortCeLivingSandboxCancel();
	if (PortCeCarrierClashCancel) PortCeCarrierClashCancel();
	if (PortCeFlagshipDuelCancel) PortCeFlagshipDuelCancel();
}

// Dispatched by ce_hooks.cpp on world_source (FIRST_MATCH, CE_GATE_LAYER).
extern "C" bool PortCeWorldProviderHandleSource(CeWorldSourceReq *req)
{
	if (!req)
		return false;
	const CeWorldContentProvider *p = CePolicyActiveWorldProvider();
	if (!p)
		return false;
	switch (req->stage)
	{
	case CE_WORLD_SRC_DONOR_ARCHIVE:
		if (p->donorArchivePath)
		{
			const char *path = p->donorArchivePath(req->level);
			if (path && path[0])
			{
				req->archive_path = path;
				return true;
			}
		}
		return false;
	case CE_WORLD_SRC_CHUNK_POLICY:
		if (p->chunkPolicy)
		{
			const CeWorldChunkPolicy policy = p->chunkPolicy(req->fourcc);
			if (policy != CE_CHUNK_RETAIL)
			{
				req->chunk_policy = static_cast<int>(policy);
				req->post_eres = p->onPostEres;
				return true;
			}
		}
		return false;
	case CE_WORLD_SRC_READ_BYTES:
		if (p->readWorldBytes)
		{
			const void *bytes = NULL;
			unsigned len = 0;
			const int status = p->readWorldBytes(req->wrd_name, &bytes, &len);
			if (status != 0)
			{
				req->read_status = status;
				req->bytes = bytes;
				req->length = len;
				return true;
			}
		}
		return false;
	default:
		return false;
	}
}

#else

const CeWorldContentProvider *CeWorldProviderForMode(CeMode) { return NULL; }
const CeWorldContentProvider *CePolicyActiveWorldProvider() { return NULL; }
extern "C" void CeWorldProviderCancelAll(void) {}
extern "C" bool PortCeWorldProviderHandleSource(CeWorldSourceReq *) { return false; }

#endif
