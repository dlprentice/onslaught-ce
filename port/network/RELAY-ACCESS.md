# Relay access policy experiment

Status: offline checks; local grant loader and operator-only client; no public broker or live issuance
Last updated: 2026-11-01
Summary: bounded admission, private short-lived relay grants and an operator-only issuance client, tested with invented data.

`port/tools/relay_access_policy.py` models an admission decision for a future
short-lived TURN credential broker. Its focused checks run with:

```sh
python3 port/tools/relay_access_policy_tests.py
```

There is no network listener, endpoint, provider client, provider key, token
format or deployment here. The tests use invented identities, invitation handles
and opaque fake payloads. This model is not connected to the CE transport.

## Trust boundary

A future server adapter must obtain `VerifiedPrincipal` from a real server-side
authentication verifier. Constructing this object does not authenticate anyone.
The adapter must never accept it, its subject or its validity period as a
client's identity claim. The model checks the trusted context's audience,
subject syntax, bounded validity and expiry against caller-supplied server time.
The fixture `FakeAuthenticator` is solely a test input source.

Session and invitation IDs are server-owned handles. This model neither
generates an unpredictable invitation token nor delivers or verifies one.
Knowing a handle does not grant access: redemption also requires the authenticated
invited subject and the exact session. Invitation lookup failures and membership
failures return the same denial. A future adapter must keep handles and
credential payloads out of logs and error responses.

The long-lived provider key belongs only to a future server-side issuer. It
must never enter CE, invitation data or broker responses. The `Issuer` boundary
accepts a bounded `CredentialGrant`, with no provider-key field, and returns a
matching grant plus an opaque payload. The supplied implementation in the tests
is fake. A real issuer would need separate review of the provider response and
its actual expiry; matching a model object cannot prove what a provider enforces.

## Decisions exercised offline

| Operation | Admission rule |
| --- | --- |
| Create session | A current trusted identity, an unused server handle and available capacity. |
| Invite | The live session's authenticated owner; one owner slot and one peer slot; one invitation per subject. |
| Issue credentials | The invitation's authenticated recipient, its exact live session and an unused, unexpired invitation. |
| Close session | The authenticated owner; subsequent invitations and issuance are denied. |

The owner obtains credentials through its own invitation under the same rules.
An invitation grants one issuer attempt for one subject in one session. It cannot
be retargeted, replaced or renewed, even if it expires or the issuer fails.
Consumption happens before the issuer callback; exceptions and mismatched
responses fail closed. Serialized concurrent redemption makes only one issuer
call. There is no retry path after an ambiguous provider failure.

The authored defaults use a two-minute authentication context and invitation,
a ten-minute session and credentials expiring no later than that session.
These are experimental policy choices, not measured match durations or a service
capacity plan. There is no credential renewal or participant replacement.

Each attempt with valid, nondecreasing server time, including an authentication
failure, spends a bounded global sliding-window budget. Trusted identities also
spend a bounded per-principal budget before session or invitation checks. Denials never reach
the issuer. Full state tables deny new work; expired rate buckets and sessions
are collected on the next attempt. Clock rollback denies work without resetting
budgets. Configuration has fixed upper bounds on lifetimes, attempt budgets and
state-table sizes; the tests check quota exhaustion and the exact retry boundary.

Invitation records and closed sessions remain until session expiry, preventing
handle or member reuse while any modeled grant could still be valid. Closing
a session prevents new issuance; this model cannot revoke credentials already
issued by a provider.

## Local client grant loader

`port/network/ice_credentials.h` provides `CeIceCredentials`. It owns the relay
host, username and credential strings and prepares `juice_config_t` only while
the grant is current. Keep that owner alive through transport destruction.
`IsCurrent(now)` and `Configure(config, now)` use explicit Unix seconds; expiry
is exclusive, and a clock value before the grant was opened is rejected. The
session owner must check expiry while connecting and playing, then end the
session with a fixed diagnostic. There is no automatic refresh, credential
rotation or peer replacement.

`Open(directory, role, context, now)` reads `relay-host.json` for role 1 or
`relay-guest.json` for role 2. The strict JSON wrapper has exactly these fields:

| Field | Meaning |
| --- | --- |
| `schema` | Integer 1. |
| `role` | `host` or `guest`, matching the caller's role. |
| `context` | The lowercase hexadecimal spelling of the exchange's 32-byte context. |
| `issuedAt` | Integer Unix seconds, no later than the caller's current time. |
| `expiresAt` | Integer Unix seconds after issuance, within the request's selected cap and at most 14,400 seconds later. |
| `iceServers` | The documented provider response array. |

The context comes from `CeIceExchange::Context()`. It binds this local file to
the CE secret and compatibility context; it is not account authentication or
permission to spend provider funds. A fresh lobby-to-game session needs a fresh
context-bound grant. The loader does not distribute grants or authorize retries.

The [Cloudflare credential documentation](https://developers.cloudflare.com/realtime/turn/generate-credentials/)
defines `iceServers` entries with a `urls` array and TURN `username` and
`credential` strings. The loader selects only UDP `turn.cloudflare.com` on
3478 or 443. Fixed-host STUN entries on 3478 and 443 are accepted and ignored;
no STUN server is configured. Known TCP/TLS URLs may occur in the response but
are not configured as relay alternatives. Unknown destinations are rejected.
The displayed example's credential lengths are not treated as provider maxima.
The loader accepts up to the pinned libjuice limit of 513 ASCII bytes for each
credential string, rejects larger values and never truncates them.

The input is bounded to 32 KiB with fixed nesting and array limits. Duplicate
or unknown fields, invalid numbers, control characters and unsupported URL
forms are rejected. Errors contain fixed labels and never credential values.
On POSIX, the final directory and file must belong to the current user with no
group/other permissions; symlink components, nonregular files and hardlinks are
refused. Windows file loading currently fails closed because equivalent file
privacy has not been validated; the bounded buffer parser remains portable.

`Mode::AuthoredLoopback` permits only a literal loopback UDP relay for an
explicitly selected authored fixture. It rejects external relay destinations
and is not selected by grant contents. The normal mode rejects loopback relays.
The [focused test driver](../tools/ice_credentials_tests.py) uses generated
credentials only and makes no network connection. Its optional
`--juice-include` argument compiles against an existing pinned libjuice header;
otherwise it checks an explicitly labeled authored interface fixture.

## Preparation before transport creation

The caller first freezes the actual CE configuration and exchange context in
its pending session owner. `TryOpen` returns `LoadState::Ready` for a valid
staged grant, `Pending` only when the final grant is missing from a verified
private directory, and `Error` for invalid, expired or inaccessible present
grants. There is no internal wait loop. Polling must keep one bounded deadline
and preserve the session identity.

On `Pending`, `PrepareRequest(directory, role, context, now, request_seconds, grant_seconds)`
publishes one `relay-host.request.json` or `relay-guest.request.json`. Its exact
fields are `schema`, `role`, `context`, `requestedAt`, `requestExpiresAt` and
`grantExpiresAt`. The request window is at most 180 seconds and must match the
caller's actual preparation deadline. The grant deadline is at most 14,400 seconds
after preparation, defaulting to 7,200. Only the context hash leaves the process;
the CE secret and raw compatibility data remain in the frozen owner.

The CE pilot defaults to two hours. Its optional
`BEA_NET_ICE_LIFETIME_SECONDS` selection is a canonical integer from 120 through
14,400, validated and retained before private I/O. A later environment change
cannot enlarge that session's cap. The operator's `--ttl` also has to permit
the desired duration: a four-hour pilot requires both selections to allow
14,400 seconds. Shorter operator TTLs remain deliberate and are clamped to the
actual request's grant deadline; setup consumes part of the remaining lifetime.
Use the matching issuer and CE consumer changes together for the longer pilot.
An older consumer with the 600-second bound will reject a longer staged grant.

Linux publication writes and fsyncs a new protected temporary file, then uses
an atomic no-replace rename and fsyncs the directory. Readers see a complete
final file or `Pending`. Existing requests are never replaced silently. A new
session needs a fresh identity and private directory; these helpers do not
remove stale files or retained receipts. Windows request publication remains
unavailable until its separately reviewed private writer is integrated.

## Operator-only issuance helper

`port/tools/turn_operator_issue.py` is a separate, explicit operator command,
not a public broker. Choose exactly one key-input form: `--key-file` names a
private JSON file containing only `keyId` and `apiToken`; alternatively,
`--token-id-file` and `--api-token-file` name two separate private text files.
Every argument is a file path, never a key value. Mixed, partial, repeated or
same-file alternatives are rejected before issuance.

Each text file contains exactly one ASCII value, optionally followed by one
LF or CRLF. Leading/trailing whitespace, extra lines and JSON quoting are not
part of that format. The existing bounds remain: token ID at most 128 bytes,
API token at most 2,048 bytes. Every input file and its parent must be owned by
the current user, with no group/other access, no symlink components and no
hardlink aliases. All key inputs stay outside the client grant directory.
No key value is accepted from command arguments, environment or client fields.
Finish updating both separate files before starting the issuer; they are
individually verified snapshots rather than an atomic key-pair rotation.

The command requires `--issue`, the selected key-input form, `--directory` and
`--role`; all paths must be absolute. It reads the role's protected preparation request,
rechecks its role and deadlines before spending, and uses that context. There
is no context, secret or credential argument to copy into a command line.
`--ttl` defaults to 7,200 seconds, cannot exceed 14,400, and is clamped to the
remaining request-approved lifetime. This does not establish remote peer
identity. These tests perform no live provider invocation.

The selected grant lifetime limits the pilot's session length, including
setup time. There is no renewal. The
[Cloudflare TURN FAQ](https://developers.cloudflare.com/realtime/turn/faq/)
describes expiry ending an active allocation after a delay and recommends an
ICE restart for renewed credentials; this client does not implement that flow.
The provider's documented maximum is 48 hours. This pilot deliberately selects
two hours by default and four hours at most. The longer bearer-credential
window increases exposure if a grant is copied or stolen; it does not impose
a bandwidth or spending limit on that credential. Existing issuance-count and
private-file protections remain in force.

The command contacts `rtc.live.cloudflare.com` only to generate short-lived
TURN credentials, using the protected key's authorization header and bounded
TTL. The HTTPS request is fixed to the documented credential-generation endpoint.
Only the locally validated key identifier varies within its path, and the
request JSON contains only the bounded TTL. TLS certificate and hostname
verification and an honestly identifying Onslaught CE user-agent are required.
Successful 200/201 responses still require the strict schema. Redirects,
compression, oversized bodies and
unexpected response shapes are refused. Inherited proxies and TLS key logging
are not used.
There is no automatic retry and no arbitrary endpoint option.

A protected `relay-session.json` binds the directory to one context and a
four-hour maximum session deadline. Before making a request, the helper
atomically publishes and fsyncs `relay-host.attempt.json` or
`relay-guest.attempt.json` without replacement. That receipt survives failure,
crash or an ambiguous response;
the helper will not issue that role again in the directory. The second role
shares the context and remaining session lifetime. Existing grant files are
never overwritten. Successful credentials go only to the fixed protected
role-specific grant file; diagnostics contain fixed labels without values.

This Linux-only helper is prepared for later operator review and use. Its
tests block socket/DNS operations and inject invented provider responses.
The per-directory attempt limit bounds these requests, not provider bandwidth
or allocation charges. Actual provider restrictions, clock behavior, revocation,
operator delivery to the remote peer and real-network acceptance remain open.

For a controlled one-machine pilot, `--role both --watch-seconds 120` watches
only the two fixed request names in one selected private directory. It pins
the directory identity and first accepted context, allows at most one attempt
per role and exits after both grants or a terminal failure. The watch window
cannot exceed 180 seconds; no new request starts after its deadline, while a
request already in flight may finish. The helper has no listener and performs
no automatic retry or renewal. An idle watch with no requests times out without
reading the key.

The pilot caller uses separate `BEA_NET_ICE_DIRECTORY` and
`BEA_NET_ICE_GAME_DIRECTORY` selections for lobby and game preparation. Each
directory belongs to one actual context. Run a separate explicitly approved
bounded operator invocation for each phase; a new campaign retry or restart
needs fresh directories and identity. The watcher does not switch phases,
delete old files or silently make stale grants current. The file protocol is
local operator preparation, not an Internet-ready identity or account service.

## Integration limit and cheapest next falsifier

The optional ICE session interface takes a short-lived transport configuration;
it does not establish an account, invitation-delivery or signaling contract.
The policy fixture's fake payload must not be treated as a usable ICE
configuration. The grant loader is a separate local consumption boundary and
does not supply the missing broker authentication or invitation delivery.

This in-memory authority loses replay and quota state on restart and does not
coordinate multiple workers. It is not a public credential mint. A real broker
needs authenticated identity and invitation delivery, atomic durable consumption
and quotas, bounded transport parsing and a provider adapter that enforces the
grant's expiry. Provider credentials can have uses beyond the intended game
session; the application session binding here does not restrict TURN destinations
or prove provider bandwidth, allocation or spending limits. Those require an
explicit provider contract before any real issuance.

The cheapest next integration falsifier is an offline adapter test with the fake
issuer: submit a client-forged identity object and require rejection before any
issuer call, then submit a real verifier result and race the same invitation
twice. Require one issuer call, the intended subject/session and an expiry no
later than the session. The current focused tests establish the policy half of
that experiment, including concurrent use; they do not establish an authentication
adapter, durable replay prevention, provider restrictions or Internet acceptance.
