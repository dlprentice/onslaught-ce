// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#ifndef CE_ICE_GAME_H
#define CE_ICE_GAME_H
#include "network.h"
#include "ice_signaling.h"
#include <stdlib.h>

// The private directory selects this route. Credentials and descriptions never
// belong in arguments/environment. An explicit request never falls back to direct-IP.
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
#ifdef __cplusplus
extern "C" {
#endif
int CE_NetSignalingDirectorySelected(void);
#ifdef __cplusplus
}
#endif
#endif
#endif
static inline int CE_NetGameIceRequested(void)
{
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
    if (CE_NetSignalingDirectorySelected()) return 1;
#endif
#endif
    const char *directory = getenv("BEA_NET_ICE_DIRECTORY");
    const char *game = getenv("BEA_NET_ICE_GAME_DIRECTORY");
    return (directory && directory[0]) || (game && game[0]) ? 1 : 0;
}
#ifdef __cplusplus
extern "C" {
#endif
#if defined(CE_NETWORK_ICE)
CE_NetSession *CE_NetCreateGame(const CE_NetConfig *);
// Menu pilot: both distinct private directories must be selected before launch.
// Lobby uses BEA_NET_ICE_DIRECTORY, restarted game uses BEA_NET_ICE_GAME_DIRECTORY.
CE_NetSession *CE_NetCreateLobby(const CE_NetConfig *);
// A fixed diagnostic; nullptr asks for the last creation failure.
const char *CE_NetGameProblem(const CE_NetSession *);
#else
static inline CE_NetSession *CE_NetCreateGame(const CE_NetConfig *config)
{
    return CE_NetGameIceRequested() ? NULL : CE_NetCreate(config);
}
static inline CE_NetSession *CE_NetCreateLobby(const CE_NetConfig *config)
{
    return CE_NetCreateGame(config);
}
static inline const char *CE_NetGameProblem(const CE_NetSession *)
{
    return CE_NetGameIceRequested() ? "experimental relay transport is unavailable in this build" : "";
}
#endif
#ifdef __cplusplus
}
#endif
#endif

// Explicit startup selection. Call before creating a session and keep the
// provider selected until every session is destroyed. A refused native setup
// never falls back to manual exchange or another transport.
#if defined(CE_NETWORK_ICE)
// Native selection is memory-only and fixed while a session or restored Game plan owns it.
bool CE_NetSelectSignalingDirectory(const char* directory) noexcept;
bool CE_NetSelectSignaling(std::shared_ptr<CeIceSignalProvider> provider) noexcept;
// One restore per selected provider, before creating the native Game session.
// Expired, mismatched or absent plans are refused without refreshing deadlines.
bool CE_NetRestoreSignalingGamePlan(const CeIceSignalGamePlan& plan) noexcept;
void CE_NetSignalingPreflight(uint64_t utcSeconds, uint64_t monotonicMs) noexcept;
CeIceSignalView CE_NetSignalingView() noexcept;
bool CE_NetSignalingAccept() noexcept;
void CE_NetSignalingClosePair() noexcept;
#else
inline bool CE_NetSelectSignaling(std::shared_ptr<CeIceSignalProvider>) noexcept { return false; }
inline bool CE_NetRestoreSignalingGamePlan(const CeIceSignalGamePlan&) noexcept { return false; }
inline void CE_NetSignalingPreflight(uint64_t, uint64_t) noexcept {}
inline CeIceSignalView CE_NetSignalingView() noexcept { return {}; }
inline bool CE_NetSignalingAccept() noexcept { return false; }
inline void CE_NetSignalingClosePair() noexcept {}
#endif
