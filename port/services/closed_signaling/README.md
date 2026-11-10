# Closed-pair Worker signaling service

Status: active — authored local runtime validation; no deployed-service acceptance
Last updated: 2026-11-03
Summary: runnable HTTP, SQLite Durable Object and private TURN issuer adapters for the existing closed-pair protocol.

This service implements the seven fixed HTTPS POST routes in
[`SIGNALING-SERVICE.md`](../../network/SIGNALING-SERVICE.md) and uses the existing
CEPAIR1 capability and request/grant/record schemas. It has no public pair creation,
capability minting, arbitrary lookup, proxy, discovery or renewal route. A delivery
acknowledgement establishes service delivery, not authenticated CE connectivity or gameplay.

`worker.mjs` exports the public handler, `ClosedPairs` SQLite Durable Object and
the private `PermitAuthority` entrypoint. `issuer.mjs` is a separate private
Worker: only it receives the provider key/token. It contacts exactly the fixed
Cloudflare TURN credential-generation endpoint to obtain the temporary grant.
The requested origin, provider address and TTL are never selected by a player.

## Durable authority

One fixed-name object owns the closed pilot's pair, global budget and encryption
journal. There is no cross-object transaction claim. The trusted configuration
admits one active pair, at most 24 approvals and 96 reserved issuance permits
per UTC day. A new approval reserves all four permits in one commit and activates
its pair in a second, both inside `initialize()`, which the constructor runs in
`blockConcurrencyWhile`. When `initialize()` fails the object is reset and nothing
it wrote persists, so a failed activation does not burn the reservation: the day's
budget stays as it was before the attempt. (Permits spent by issuance attempts do
stay burned, as below.) The object hashes
subjects with the dedicated identity key and stores request/grant/record payloads
using the unchanged native `CEWGCM01` parcel cipher.

The comment above that second commit in `worker.mjs` ("This second commit never
refunds the already reserved global budget on failure.") is out of date and
contradicts the paragraph above. It stays until the next bundle: comments are part
of the bundle text, so any edit to `worker.mjs` changes the tested bundle's hashes.

Both members explicitly accept their assigned pair. Requests freeze the matching
context and shorten the original setup deadline; they do not issue credentials.
Only an explicit, rate-limited poll can reserve a role/stage attempt. Reservation
and its ten-second lease commit before the private issuer is called. The issuer
verifies a signed internal permit and calls back through `PermitAuthority` to
atomically spend that exact reservation before its single provider request.
Replays, timeouts, provider errors and expired leases never mint again or refund.
Identical requests and durably saved grants can be redelivered after process restart.

The setup deadline, pair/grant expiries, one game stage, consent, role, context,
answer-to-offer and acknowledgement gates remain distinct. A completed server
stage can outlive its setup deadline; the existing client still enforces its own
local deadline. Invalid authorized calls consume the durable call allowance;
closing a pair remains available when that allowance is exhausted.

The per-key encryption counter commits before each encryption. Missing key or
budget configuration, key/journal mismatch, clock regression and storage errors
fail closed. Never reset a journal and reuse its parcel key. Local persistence
does not establish rollback resistance or managed key custody. Encrypted known
revocation identifiers are retained; this adapter, like the current Python
binding, does not call a provider revocation endpoint.

## Local runtime checks and bundles

Use the already prepared workerd binary and complete license pinned by
[`runtime.pin.json`](runtime.pin.json), under an ignored input directory. The
runner verifies both hashes and downloads nothing:

```sh
python port/tools/signaling_worker_http_tests.py \
  --inputs local-data/signaling-workerd \
  --output local-data/signaling-worker-http-check
```

The output directory must be new. The runner generates the exact dashboard
bundles, starts the pinned runtime on loopback with disk-backed SQL objects, and
drives the existing Python client through consent and both stages. Its provider
is an authored private fixture. Other outbound traffic is denied. Restart,
lease/deadline, request immutability, global reservations, body limits, role/record
checks, encryption budget, provider errors and permit replay are checked there.
The fixture's structural CEICE002 envelopes are deliberately not authenticated
game records. Logs, state, receipts and bundles stay in ignored `local-data/`.

## Trusted native invitation export

[`signaling_worker_invitation_export.py`](../../tools/signaling_worker_invitation_export.py)
binds invitations to this Worker's approved pair. It reads an explicit private
copy of the `CE_APPROVED_PAIR` JSON value and the matching admission key, mints
the existing CEPAIR1 role capabilities, and calls only authenticated Current
for each role through the existing fixed HTTPS client. Both replies must match
the configured approval, role and exact expiry before any invitation is staged.
It performs no consent, stage, grant, provider or game-attestation operation.

The [data-only example](approved-pair.example.json) is a template, not an active
approval: replace every placeholder and both timestamps in a private copy.
Unknown/duplicate fields and stale/future times refuse. Supply only the six-field
pair value, not a server configuration or secrets object. The key file contains
exactly 32 raw bytes, rather than its hexadecimal Worker binding. Both input
files must be private, with mode 0600 inside directories with mode 0700.

```sh
python port/tools/signaling_worker_invitation_export.py \
  --pair-config '<PRIVATE_PAIR_JSON>' \
  --admission-key '<PRIVATE_RAW_32_BYTE_ADMISSION_KEY>' \
  --service-origin 'https://<PUBLIC_WORKER_ORIGIN>' \
  --host-output '<PRIVATE_EXPORT_BASE>/<NEW_PAIR>/host' \
  --guest-output '<PRIVATE_EXPORT_BASE>/<NEW_PAIR>/guest' \
  --maximum-seconds 14400
```

`--maximum-seconds` defaults to 7200, cannot exceed 14400, and must match the
Worker's capability policy. It never extends the input's expiry. The alternative
`--admission-key-fd N` reads a private regular file at offset zero or an anonymous
pipe containing exactly 32 bytes followed by EOF; pipe reading has a five-second
limit. Neither key nor capability belongs in argv, environment variables or logs.

`PRIVATE_EXPORT_BASE` must already exist with mode 0700. The common `NEW_PAIR`
parent must not exist; both role outputs must be distinct direct children of it.
The command stages and reads back all four private files, rechecks time and makes
both roles visible in one no-replacement directory rename. It never overwrites
an old invitation. A failure after rename reports unverified publication and
preserves the complete private directory for inspection. Current can activate
the server's configured approval even if a later local write fails; the command
does not refund or reset server state.

Each role gets `identity-token` and `pair-admission.json`. Native metadata is
exactly `schema`, `approval`, `role` and `expiresAt`; `issuedAt` remains in the
signed token. The older Python `export_role` emits five metadata fields for its
prototype, and is not a native export substitute. Its `prepare_pair` also targets
the Python SQLite store, rather than approving this Worker's object.

The ordinary CE join code still comes separately from the actual host session.
Importing CEPAIR1 does not create that code, accept the other participant, prove
connectivity or establish authenticated START. Those actions remain with the
actual players and native session owners.

The focused checks replace HTTPS connections with an authored WSGI fixture,
forbid real sockets, and compile the real native metadata parser with its
existing component-test platform stubs. They exercise cross-format acceptance,
Current mismatches, private inputs, expiry, collisions and whole-pair publication:

```sh
python port/tools/signaling_worker_invitation_export_tests.py \
  --output local-data/worker-invitation-export-check
```

The output must be new; a local C++17 `g++` is required. No game, provider,
Cloudflare account, real admission key or Windows runtime is exercised.

## Runtime bundles

To regenerate standalone dashboard inputs without Node, npm or a build service:

```sh
python port/services/closed_signaling/bundle.py \
  --output local-data/closed-signaling-bundles
```

`worker.mjs` and `issuer.mjs` in that output need only the platform's built-in
`cloudflare:workers` import. `bundles.json` pins source and output hashes. Use the
actual runtime-tested output for deployment; a separately regenerated output
must have the same hashes. No third-party binary or fixture is a deployed dependency.

`workerd.capnp` is a disabled, loopback-only local entrypoint. From the repository
root create `local-data/signaling-worker-local-state`, then run the pinned binary
with that config from `port/services/closed_signaling/`. It exposes no issuer and
returns unavailable until a separate trusted configuration exists. The HTTP test
runner supplies its own enabled configuration and invented keys.

## Deployment configuration

Both Wrangler templates default to disabled, no public/preview URL and disabled
observability. They require no Git integration or hosted build. The deployment
owner verifies the actual Workers Free plan, limits and selected route before
enabling anything; no setting here upgrades a plan. SQLite Durable Objects are
available on Free, and exhausted free allowances fail operations.
[Cloudflare pricing](https://developers.cloudflare.com/durable-objects/platform/pricing/)

| Public Worker binding | Trusted value |
| --- | --- |
| `CLOSED_PAIRS` | SQLite namespace for this Worker's `ClosedPairs` class |
| `TURN_ISSUER` | Private service binding to the issuer Worker |
| `CE_ADMISSION_KEY` | Dedicated 32-byte signing key, encoded as lowercase hexadecimal |
| `CE_IDENTITY_KEY` | Separate 32-byte pseudonym key, encoded the same way |
| `CE_PARCEL_KEY` | Separate 32-byte encryption key, encoded the same way |
| `CE_ISSUER_PERMIT_KEY` | Separate 32-byte private permit key, also supplied to the issuer |
| `CE_APPROVED_PAIR` | Private JSON configuration described below |
| `CE_ENCRYPTION_LIMIT` | Explicit positive journal ceiling, at most 100000 |
| `CE_ENABLED` | `true` only after configuration and review |

The four keys must differ. The parcel key identity is its SHA-256; optional
`CE_PARCEL_KEY_ID` must equal that identity. Do not put keys or pair configuration
in these tracked templates. `CE_CAPABILITY_SECONDS` and `CE_GRANT_SECONDS` default
to 7200 and cannot exceed 14400; the issuer's grant policy must match. Leave
`CE_LOCAL_HTTP` absent in the hosted configuration.

`CE_APPROVED_PAIR` has exactly `schema` (1), `approval` (32 lowercase hexadecimal
characters), `host` and `guest` (distinct `p_` plus 32 lowercase hexadecimal
characters plus their respective `_host`/`_guest` suffix), `issuedAt` and
`expiresAt` (integer UTC seconds). A fresh pair must be current and expire within
14400 seconds. Reusing an approval with changed fields refuses. The deployment
owner validates and activates the intended approval before distributing separate
short-lived capabilities through the existing trusted admission procedure.

The issuer receives only its enable/grant policy, the shared private permit key,
the existing `TURN_KEY_ID`/`TURN_API_TOKEN`, and `PERMIT_AUTHORITY`, bound to the
public Worker's named `PermitAuthority` entrypoint. It has no public route,
workers.dev or preview URL. Its fixed provider call preserves the existing
strict parser, UDP relay selection and master-token echo refusal. It never
accepts a client-supplied provider URL or a public mint request.
[Named service bindings](https://developers.cloudflare.com/workers/runtime-apis/bindings/service-bindings/rpc/)

### Provisioning is a separate step from pasting code

The supported configuration path is the checked-in Wrangler templates, or the
equivalent Workers Scripts API metadata. The public Worker needs the
`new_sqlite_classes = ["ClosedPairs"]` migration and `CLOSED_PAIRS` binding;
exporting the JavaScript class by itself is insufficient. Cloudflare documents
class lifecycle deployment through Wrangler; its Scripts API also exposes the
SQLite migration and named service-entrypoint fields. Dashboard-only class
provisioning and named-entrypoint selection have not been established here.
[Class provisioning](https://developers.cloudflare.com/durable-objects/reference/durable-objects-migrations/),
[Scripts API](https://developers.cloudflare.com/api/resources/workers/subresources/scripts/)

The two private service bindings form a deployment cycle. Bootstrap the issuer
under its final name using its `bootstrap` environment: disabled and without an
authority binding. Then provision the disabled public Worker/SQLite namespace,
then apply the issuer's complete private authority binding. `issuer.wrangler.toml`
contains that same-name bootstrap configuration. All steps use local reviewed
source; there is no Git/cloud build. A binding target must already exist.
[Service binding deployment](https://developers.cloudflare.com/workers/runtime-apis/bindings/service-bindings/)

Only the deployment owner supplies account authorization and server secrets,
registers the namespace/bindings, selects the public HTTPS route, verifies all
alternate routes stay closed and enables the service. No auth flow, account
change, provider call or deployment is performed by the local runner. Before a
hosted pilot, verify actual TLS, real capability rejection, platform persistence
and limits, provider issuance/revocation, and the normal native client on each
selected platform. Local workerd success establishes none of those outcomes.
