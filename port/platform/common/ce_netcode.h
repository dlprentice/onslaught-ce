// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_netcode.h: the network game's join code, the secrets derived from it and the private launch
// handoff between the front end and the fresh game process (port/game/ce_netpages.cpp, ce_netlobby.cpp).
//
// A join code holds a 60-bit random key, the host's IPv4 address and UDP port: 108 bits, two zero pad bits, 22 symbols of
// Crockford's base 32 and one check symbol, shown as XXXX-XXXX-XXXX-XXXX-XXXX-XXX. The check symbol is a weighted sum in
// GF(32) with 22 distinct nonzero weights, so it catches every single wrong symbol and every swap of two different
// symbols. Typing is forgiving: case, dashes and spaces are ignored, I and L read as 1 and O as 0.
//
// Limit (v1): 60 bits resist online guessing (each guess costs a full handshake, and the host makes a new code after five
// failed attempts), but someone on the network path who records a handshake could search the 2^60 keys offline, at real
// cost. The key, the code and every secret derived from it are never written to a log.
#ifndef CE_NETCODE_H
#define CE_NETCODE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CE_CODE_SYMBOLS		23			// 22 data symbols and the check symbol
#define CE_CODE_TEXT		29			// with its five dashes and the terminator
#define CE_CODE_KEY_MASK	0x0fffffffffffffffULL

enum CE_CodeResult {
	CE_CODE_OK = 0,
	CE_CODE_EMPTY,			// nothing typed
	CE_CODE_SHORT,			// fewer than 23 symbols
	CE_CODE_LONG,			// more than 23 symbols
	CE_CODE_BAD_CHAR,		// a character that is in no code (U, punctuation)
	CE_CODE_CHECK,			// the check symbol does not match: a typo
	CE_CODE_BAD_PAD,		// the pad bits are not zero: not a code
	CE_CODE_BAD_ADDRESS		// address 0.0.0.0 or port 0
};

typedef struct CE_JoinCode {
	uint64_t key;			// 60 bits
	uint32_t ipv4;			// host order: 192.168.1.20 is 0xc0a80114
	uint16_t port;
} CE_JoinCode;

// out: CE_CODE_TEXT bytes. Returns 1, or 0 when the key has more than 60 bits, the address is 0 or the port 0.
int CE_CodeEncode(const CE_JoinCode *code, char *out, size_t size);
// Returns a CE_CodeResult; *code is filled only for CE_CODE_OK.
int CE_CodeDecode(const char *text, CE_JoinCode *code);
// The player-facing words for a CE_CodeResult (the game font's characters only).
const char *CE_CodeProblem(int result);
// A new 60-bit key from the system's random source (CE_NetGenerateSecret). Returns 1, or 0 when none is available.
int CE_CodeNewKey(uint64_t *key);

// SHA-256("onslaught-ce join v1" || key, 8 bytes big-endian): the lobby's transport secret.
void CE_CodeLobbySecret(uint64_t key, uint8_t out[32]);
// SHA-256("onslaught-ce start v1" || lobby secret || nonce): the game process's secret for one Start.
void CE_CodeStartSecret(const uint8_t lobby[32], const uint8_t nonce[16], uint8_t out[32]);
// SHA-256("onslaught-ce lobby v1\0" || identity || "\0" || version): the lobby's compatibility digest.
void CE_CodeLobbyDigest(const char *identity, const char *version, uint8_t out[32]);
void CE_CodeSha256(const void *data, size_t size, uint8_t out[32]);
// Wipes memory the compiler may not optimise away.
void CE_CodeWipe(void *p, size_t n);

// The private handoff file. A launch (front end -> game) carries the role, the endpoint, the start secret, the level,
// the Start's nonce and the command line to return with; an outcome (game -> front end) carries what happened. The file
// is created new with mode 0600 (Linux), read once and deleted as it is read, and refused when older than 60 s.
enum CE_HandoffKind { CE_HANDOFF_LAUNCH = 1, CE_HANDOFF_OUTCOME = 2 };
#define CE_HANDOFF_MAX_AGE	60
#define CE_HANDOFF_MAX_BYTES	4096
/* Campaign identity is bound to the authenticated launch nonce.
 * A zero version preserves the ordinary launch format. */
typedef struct CE_CampaignLaunch {
    uint32_t version;
    uint64_t session;
    uint64_t attempt;
    uint32_t mission;
} CE_CampaignLaunch;

typedef struct CE_Handoff {
	int kind;
	int role;					// launch: 1 host, 2 guest
	char endpoint[32];			// launch: "a.b.c.d:port" (the host's bind address, or the guest's host)
	uint8_t secret[32];			// launch
	uint32_t level;				// launch: 852 (the engineering session) or a co-op map (100, 856, 858: compatibility.h)
	uint8_t nonce[16];			// launch
	int64_t created;			// seconds since 1970 (UTC)
	int good;					// outcome: 1 when the game ended normally
	char text[256];				// outcome: one line for the player; launch: unused
	char args[1024];			// launch: the front end's own arguments, for the return
    // Nonzero on nonce-bound launches or confirmed, versioned outcome returns.
    CE_CampaignLaunch campaign;
} CE_Handoff;

// Text form (no secret in error messages). Returns the byte count, or 0 when it does not fit.
size_t CE_HandoffFormat(const CE_Handoff *h, char *out, size_t size);
// Returns 1, or 0 with *error naming the problem. now: seconds since 1970.
int CE_HandoffParse(const char *text, size_t size, int64_t now, CE_Handoff *h, const char **error);
// Writes path new (O_EXCL, 0600; an older file at path is removed first). Returns 1 or 0.
int CE_HandoffWrite(const char *path, const CE_Handoff *h);
// Reads path, deletes it (whatever it holds) and parses it. Refuses a link, another owner's file, a file whose mode is not
// 0600 (Linux), one larger than CE_HANDOFF_MAX_BYTES, or one written more than CE_HANDOFF_MAX_AGE seconds ago.
int CE_HandoffRead(const char *path, int64_t now, CE_Handoff *h, const char **error);
int64_t CE_HandoffNow(void);

/* These validate or derive a campaign descriptor; they never create a secret. */
int CE_HandoffCampaignValid(const CE_Handoff *handoff);
int CE_HandoffCampaignSet(CE_Handoff *handoff);

/* A versioned ownership receipt for one private restart record. It is serialized
 * explicitly, never as a raw structure, and is not a claim about arbitrary paths.
 * Verified consumption clears contents through the opened file and retains its
 * directory entry. Unknown, changed, or replaced records are left untouched. */
enum { CE_HANDOFF_OWNER_VERSION = 1 };
typedef struct CE_HandoffOwner {
    uint32_t version;
    uint64_t volume, file, stamp;
    uint8_t digest[32];
} CE_HandoffOwner;
int CE_HandoffOwnedWrite(const char *path, const CE_Handoff *handoff,
                         CE_HandoffOwner *owner, const char **error);
int CE_HandoffOwnedRead(const char *path, const CE_HandoffOwner *owner, int64_t now,
                        CE_Handoff *handoff, const char **error);
int CE_HandoffOwnedClear(const char *path, const CE_HandoffOwner *owner,
                         const char **error);
size_t CE_HandoffOwnerFormat(const CE_HandoffOwner *owner, char *out, size_t capacity);
int CE_HandoffOwnerParse(const char *text, size_t size, CE_HandoffOwner *owner);


#ifdef __cplusplus
}
#endif
#endif
