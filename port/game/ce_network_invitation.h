// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_NETWORK_INVITATION_H
#define CE_NETWORK_INVITATION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    CE_INVITATION_EMPTY = 0,
    CE_INVITATION_CHOOSING,
    CE_INVITATION_PREVIEW_READY,
    CE_INVITATION_SELECTED,
    CE_INVITATION_REFUSED,
    CE_INVITATION_CANCELLED
};
enum {
    CE_INVITATION_ERROR_NONE = 0,
    CE_INVITATION_ERROR_UNAVAILABLE,
    CE_INVITATION_ERROR_ORIGIN,
    CE_INVITATION_ERROR_ROLE,
    CE_INVITATION_ERROR_FOLDER,
    CE_INVITATION_ERROR_METADATA,
    CE_INVITATION_ERROR_EXPIRED,
    CE_INVITATION_ERROR_CHANGED,
    CE_INVITATION_ERROR_CAPABILITY,
    CE_INVITATION_ERROR_PRIVATE_DIRECTORY,
    CE_INVITATION_ERROR_SELECTION
};
enum { CE_INVITATION_ORIGIN_BYTES = 512 };

typedef struct CE_InvitationView {
    uint32_t state, error, role;
    uint64_t expires_at;
    char origin[CE_INVITATION_ORIGIN_BYTES + 1];
} CE_InvitationView;

// Main menu thread only. Begin opens a chooser after strict origin validation.
// expected_role is CE_NET_HOST or CE_NET_GUEST. No token is read by Begin/Poll.
int CE_InvitationBeginImport(const char *origin, uint32_t expected_role);
void CE_InvitationPoll(uint64_t utc_seconds, CE_InvitationView *out);
// Separate explicit confirmation: read the selected role capability, create a
// fresh app-owned private exchange directory, then construct/select authority.
// Construction/selection makes no network request; Continue remains separate.
int CE_InvitationInstall(uint64_t utc_seconds);
// Cancels chooser/preview ownership; it does not clear an installed authority.
void CE_InvitationCancel(void);
// Fixed public text; never includes file contents, paths or capability bytes.
const char *CE_InvitationErrorText(uint32_t error);

#ifdef __cplusplus
}
#endif
#endif
