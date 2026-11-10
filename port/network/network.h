// SPDX-License-Identifier: GPL-3.0-only
// Direct-IP networking. An authenticated transport only; game rules and object serialization belong to the game.
#ifndef CE_NETWORK_H
#define CE_NETWORK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CE_NET_SECRET_BYTES 32
#define CE_NET_DIGEST_BYTES 32
#define CE_NET_MAX_MESSAGE 65536u
#define CE_NET_MAX_DATAGRAM 1200u
#define CE_NET_MAX_INPUT 1000u
#define CE_NET_MAX_LIFECYCLE 1000u
#define CE_NET_STATE_CONTROL_BYTES 184u
#define CE_NET_VERSION_BYTES 32u

typedef struct CE_NetSession CE_NetSession;
enum CE_NetRole { CE_NET_HOST = 1, CE_NET_GUEST = 2 };
enum CE_NetPhase {
    CE_NET_WAITING = 1, CE_NET_CONNECTING, CE_NET_AUTHENTICATING,
    CE_NET_ACTIVE, CE_NET_SYNCING, CE_NET_CLOSED, CE_NET_FAILED
};
enum CE_NetKind {
    CE_NET_CONTROL = 16, CE_NET_INPUT = 17, CE_NET_STATE = 18, CE_NET_BASELINE = 19,
    CE_NET_LIFECYCLE = 20, CE_NET_STATE_CONTROL = 21, CE_NET_STATE_REPAIR = 22
};
enum CE_NetError {
    CE_NET_OK = 0, CE_NET_BAD_CONFIG, CE_NET_IO, CE_NET_AUTH, CE_NET_INCOMPATIBLE,
    CE_NET_PROTOCOL, CE_NET_TIMEOUT, CE_NET_CAPACITY, CE_NET_PEER_LEFT
};
typedef struct CE_NetConfig {
    uint32_t role;
    const char *ipv4;                 // numeric IPv4 only; host binds exactly this address (no DNS/discovery)
    uint16_t port;                    // host may choose zero; status reports the selected port
    uint8_t secret[CE_NET_SECRET_BYTES]; // randomly generated invite secret, supplied privately to the other player
    uint8_t compatibility[CE_NET_DIGEST_BYTES]; // caller's build/schema/mission/retail-input digest
    uint32_t handshake_ms;            // zero: 5000; all timeouts use real monotonic time, never the game clock
    uint32_t silence_ms;              // zero: 10000
    char version[CE_NET_VERSION_BYTES]; // display label (for example "0.1.7"), sent so a refusal can name both versions
} CE_NetConfig;
typedef struct CE_NetStatus {
    uint32_t phase, error, role, local_player, epoch;
    uint16_t port;
    uint32_t rejected_packets, sent_messages, received_messages;
} CE_NetStatus;
typedef struct CE_NetMessage {
    uint32_t kind, epoch, tick, size;
} CE_NetMessage;

// No networking occurs before Create. There is no implicit configuration, global session or telemetry.
// All session operations run on one caller thread; multiple sessions may coexist, never call them concurrently.
int CE_NetGenerateSecret(uint8_t out[CE_NET_SECRET_BYTES]);
uint64_t CE_NetNowMs(void);
CE_NetSession *CE_NetCreate(const CE_NetConfig *config);
void CE_NetDestroy(CE_NetSession *session); // closes sockets and wipes session/invite keys
void CE_NetPoll(CE_NetSession *session, uint64_t now_ms, uint32_t event_budget);
void CE_NetGetStatus(const CE_NetSession *session, CE_NetStatus *out);
// The other player's version label from its HELLO (host) or authenticated refusal (guest), as printable ASCII.
// Return 1 with a NUL-terminated label, 0 when none is known. It is a display string, never an admission input.
int CE_NetPeerVersion(const CE_NetSession *session, char *out, uint32_t capacity);
// Milliseconds since the last authenticated packet from the other player while connected (pings arrive at least
// once a second), else 0. The transport stops with CE_NET_TIMEOUT when this reaches the configured silence.
uint32_t CE_NetQuietMs(const CE_NetSession *session, uint64_t now_ms);
// CONTROL/STATE_CONTROL are bidirectional; INPUT is guest -> host. All other application kinds are host -> guest.
// Payloads are opaque. STATE_CONTROL is exactly CE_NET_STATE_CONTROL_BYTES bytes, reliable on channel 0.
// INPUT is reliable and ordered; a zero return leaves the batch with the caller. STATE supersedes older queued state.
// LIFECYCLE is reliable and ordered with BASELINE on channel 3, and fits one <=CE_NET_MAX_LIFECYCLE-byte packet.
// STATE_REPAIR is 1..CE_NET_MAX_MESSAGE bytes, reliable and ordered with BASELINE/LIFECYCLE on channel 3.
// It uses the existing bounded reliable assembly deadline, not STATE expiry, and causes no baseline/epoch reset.
// Separate-channel repair announcement/READY ordering and genuine whole-Apply acknowledgement belong to the caller.
// The game's command sequence/consumed acknowledgement determines execution, separately from transport receipt.
// Return 1: queued only, never application/Apply proof; 0: not active/backpressure; -1: invalid argument.
// INPUT/CONTROL/LIFECYCLE/STATE_CONTROL fit one bounded application packet.
int CE_NetSend(CE_NetSession *session, uint32_t kind, uint32_t tick, const uint8_t *data, uint32_t size);
// Return 1: received, 0: none, -1: output too small (message remains queued).
int CE_NetReceive(CE_NetSession *session, CE_NetMessage *message, uint8_t *out, uint32_t capacity);
// Host-only epoch barrier: drops old queued/reassembly state and blocks application sends until peer acknowledges.
int CE_NetBeginEpoch(CE_NetSession *session, uint32_t epoch);
void CE_NetClose(CE_NetSession *session);

/* Optional authenticated, unfragmented best-effort text; no delivery receipt. */
enum { CE_NET_TEXT = 23, CE_NET_TEXT_CHANNEL = 4,
       CE_NET_TEXT_MAX_FRAME = 528, CE_NET_TEXT_RECEIVE_CAPACITY = 16 };
enum { CE_NET_TEXT_OK = 0, CE_NET_TEXT_DISABLED, CE_NET_TEXT_CLOSED,
       CE_NET_TEXT_WOULD_BLOCK, CE_NET_TEXT_TOO_LARGE, CE_NET_TEXT_INVALID,
       CE_NET_TEXT_CLOCK_REGRESSION, CE_NET_TEXT_EMPTY, CE_NET_TEXT_RATE_LIMITED };
typedef struct CE_NetTextInfo {
    uint32_t phase, max_frame, queued, pending;
    uint64_t sent, received, dropped_rate, dropped_full, dropped_duplicate;
} CE_NetTextInfo;
uint32_t CE_NetTextMaxFrame(const CE_NetSession *session);
uint32_t CE_NetTextSend(CE_NetSession *session, uint32_t epoch,
                       const void *frame, uint32_t size, uint64_t monotonic_ms);
uint32_t CE_NetTextReceive(CE_NetSession *session, uint32_t *epoch,
                          void *frame, uint32_t capacity, uint32_t *size);
void CE_NetTextGetInfo(const CE_NetSession *session, CE_NetTextInfo *info);


// Explicit, checked big-endian codecs. These structs describe local buffers; never transmit their memory image.
typedef struct CE_NetWriter { uint8_t *data; uint32_t capacity, size; int error; } CE_NetWriter;
typedef struct CE_NetReader { const uint8_t *data; uint32_t size, position; int error; } CE_NetReader;
void CE_NetWriterInit(CE_NetWriter *, uint8_t *, uint32_t);
void CE_NetWriteU8(CE_NetWriter *, uint8_t);
void CE_NetWriteU16(CE_NetWriter *, uint16_t);
void CE_NetWriteU32(CE_NetWriter *, uint32_t);
void CE_NetWriteU64(CE_NetWriter *, uint64_t);
void CE_NetWriteF32(CE_NetWriter *, float); // refuses NaN/Inf; game adapter checks field-specific ranges
void CE_NetWriteBytes(CE_NetWriter *, const uint8_t *, uint32_t);
void CE_NetReaderInit(CE_NetReader *, const uint8_t *, uint32_t);
uint8_t CE_NetReadU8(CE_NetReader *);
uint16_t CE_NetReadU16(CE_NetReader *);
uint32_t CE_NetReadU32(CE_NetReader *);
uint64_t CE_NetReadU64(CE_NetReader *);
float CE_NetReadF32(CE_NetReader *);
void CE_NetReadBytes(CE_NetReader *, uint8_t *, uint32_t);
int CE_NetReadDone(const CE_NetReader *);
#ifdef __cplusplus
}
#endif
#endif
