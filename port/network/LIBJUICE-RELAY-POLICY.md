# Explicit relay policy for the ICE bridge

Status: experimental dependency patch; provider and gameplay acceptance remain separate
Last updated: 2026-11-02
Summary: preserve a selected relay path through nomination and enforce the same policy inside application egress.

[libjuice-relay-only.patch](libjuice-relay-only.patch) adds
`juice_set_relay_only(agent, enabled)` to the pinned dependency. The bridge sets
this option before gathering whenever its existing `require_relay` argument is
true. Calls after gathering or remote candidate setup are refused. As with the
upstream pre-gathering setters, configuration must finish before concurrent use.
The new flag starts false; callers that do not enable it retain upstream behavior.

SDP filtering alone is insufficient: libjuice can discover reflexive candidates
during checks and replace a provisional relayed pair with a direct pair. The patch
excludes a candidate pair when neither endpoint is a relay. It accepts host-to-relay,
relay-to-host, and relay-to-relay paths. It also holds the connection lock while
loading, checking, and using the exact selected entry for an application send. A
local relay requires an actual relay entry; a remote relay uses the selected peer
candidate's type. There is no interval between a CE-side check and this library-side
send in which a newly selected direct entry can bypass the policy.

The policy does not authenticate a relay provider independently of the peer's
authenticated ICE description. Existing CE session admission and endpoint ownership
remain necessary. It does not alter the TURN server implementation, create a relay
service, add target arguments, or change the default direct transport. The separate
owned-client interoperability probe's socket wrapper is not used by the game.

## Exact source and license

The patch applies only to libjuice v1.7.4 commit
`b89c792e3612faf2f12cf35bcc56857313a06be3`, from the
[pinned archive](https://codeload.github.com/paullouisageneau/libjuice/tar.gz/b89c792e3612faf2f12cf35bcc56857313a06be3).
The archive SHA-256 is
`713b22174e8cb2e06538fa9bac2650bda21b83c86f5128813ab4dcf8f5bea9ed`.
The patch SHA-256 is
`21d2dbecacfff42e61fc16589e26795335600354ca82c45199ba8f26235341e2`.
The original archive's `LICENSE` SHA-256 is
`fab3dd6bdab226f1c08630b1dd917e11fcb4ec5e1e020e2c16f83a0a13863e85`.

The dependency modification is MPL-2.0, explicitly marked in the patch. All upstream
copyright and license headers remain in place. The bridge and authored fixtures
remain GPL-3.0-only. A binary built with this patch must not be described as the
unmodified upstream dependency. Its corresponding dependency source includes the
exact original archive, exact patch, application instructions, and original license
and notices. Distribution and public export must carry the MPL license text too.

Apply to a fresh extraction after checking both hashes:

```sh
patch --batch --fuzz=0 -p1 -i /path/to/libjuice-relay-only.patch
```

The build and package receipt contract must bind the patch bytes and the resulting
source tree, not merely the original archive hash. The isolated fixture helper
checks the fixed patch hash and records each changed upstream file hash and binary
hash. Production dependency caching, package source inclusion, and export license
routing are separate integration checks; this document does not assert they have
run merely because the fixture passes.

## Focused controls

The authored fixture uses an in-process TURN server bound to `127.0.0.1`, invented
credentials, and two CE-owned loopback ENet endpoints. All library logging is disabled
before the first library operation. It preserves the existing fragmented/reliable
message, endpoint ownership, and recoverable/fatal receive controls, and adds:

- A three-second selected-relay window with repeated bidirectional application
  delivery extending beyond 2.5 seconds. The earlier provisional-pair defect failed
  well before this interval in an independent caller experiment.
- Destruction of the owned TURN server followed by newly queued messages and a
  2.2-second observation window with no post-removal delivery through a direct path.
- A working direct connection and payload with the option disabled.
- A C fixture that injects authored selected-entry structures independently of the
  checklist. Every attempted application socket send is intercepted locally. It
  verifies default direct behavior, required-relay rejection before egress, allowed
  remote-relay egress, refusal of a claimed local relay lacking a relay entry, blocked
  non-relay reflexive pairs, and refusal to change policy after a pair exists.

The removal control does not promise immediate consent-loss notification. The patch
does not change libjuice's existing consent timers; application/session timeout
behavior needs its own evidence. It proves no new post-removal payload delivery in
the stated interval.

Run the helper with verified existing libjuice and ENet archives, a fresh ignored
output directory, and the project's normal offline isolation and resource limits.
`--target linux32 --run` builds and runs the authored fixture with the pinned Linux
toolchain. `--target windows32` compiles and links only; Windows execution remains a
separate acceptance step. Both cross targets require `--toolchain-data`. No provider,
account, real credential, game data, or player endpoint is needed for these controls.
