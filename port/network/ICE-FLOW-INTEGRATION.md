# Separate-process offline ICE flow

Status: verified on Linux i686 offline fixture and bounded Cloudflare TURN UDP relay probe; no full game-process or Windows runtime acceptance
Last updated: 2026-11-21
Summary: separate-process checks of the unchanged CE caller with the courier and sealed offline store across distinct private directories and processes, plus a bounded relay-only TURN allocation check.

The separate-process i686 Linux fixture has been built and executed inside a
loopback-only network namespace with a private hosts mapping. All 11 checks
passed (`live-direct-trap-control`, `unavailable-relay-no-direct-fallback`,
`actual-context-mismatch-refused`, `courier-lobby-real-session-traffic-and-leave`,
`courier-game-real-session-traffic-and-leave`, `altered-courier-ciphertext-refused-by-ce`,
`actual-request-authentication-and-role-controls`, `actual-request-store-context-refusal`,
`sealed-store-lobby-real-session-traffic`, `sealed-store-game-real-session-traffic`,
and `runtime-privacy-and-clean-shutdown`), with clean process collection and zero
plaintext credential or exchange canaries in logs or SQLite storage.

A separate bounded single-issuance verification (120-second TTL via
`turn_operator_issue.py`, `Mode::Cloudflare` grant parsing, and patched libjuice
`relay-only` enforcement) also confirmed live UDP relay allocation on
`turn.cloudflare.com:3478` and `turn.cloudflare.com:443`, `CE_NET_ACTIVE`
admission on both peers with `CE_NetIceUsesRelay` true, 20/20 verified
bidirectional authenticated application messages, authenticated `CE_NET_PEER_LEFT`
closure, zero secret leakage in outputs, and immediate shredding of temporary
grant files. Full game-process menu/restart journeys and native Windows
execution remain separate acceptance work.

## Planned runtime

[The peer fixture](tests/ice_flow_peer.cpp) is configured to link the production
codec, transport, ICE bridge, encrypted exchange and game-facing caller. Each
host or guest would own one `CE_NetSession` in its own Linux process, with
physically distinct lobby and game directories. The planned scope excludes
game processes, retail data, saves, input devices, accounts and external
provider connections.

The planned lobby phase would call `CE_NetCreateLobby`; a later fresh pair of
processes would call `CE_NetCreateGame` with a different authored CE identity.
Both directory selections are intended to survive that process change. This
would check the two factories and fresh contexts. The game's authenticated
START, handoff file, launcher restart and mission progression remain outside
this fixture. The store's game-stage flag is a fabricated member attestation.

Each process would publish its actual bounded preparation request. A separate
owned libjuice TURN server would accept invented credentials on loopback. The
planned test namespace's private hosts file would resolve
`turn.cloudflare.com` to `127.0.0.1`, with only a loopback interface and no
public route. The design leaves system hosts and DNS configuration unchanged
and retains the normal fixed-endpoint policies in the courier and CE grant
reader.

The native peer remains Linux-only. The normal Windows ICE refusal is
unchanged, and compiling or running this harness supplies no Windows privacy
or gameplay evidence.

## Planned delivery seams

The [courier path](../tools/ice_flow_integration_tests.py) is intended to move
the actual guest request into the host directory. The fabricated issuer would
stage role- and context-bound grants at the host. The existing courier would
then export the real host offer and guest grant together into a fresh parcel
directory and import them into the guest directory. The guest's real
`CeIceExchange` would receive the offer for authentication and answer
publication; a separate answer parcel would return that record to the host.
The planned CE processes would share no directory.

The [sealed-store path](../tools/signaling_session_store.py) is intended to
consume both actual requests through its fixed in-process routes, with a
fabricated verifier, pair approval, consent and issuer. Each stored grant
would be validated against its original local request before protected
publication. The store would seal the actual offer and answer and return
identical bytes to their intended member, for authentication by the receiving
CE exchange. The planned offer acknowledgement follows actual guest answer
publication; final delivery acknowledgement requires both actual sessions to
pass admission and exchange traffic. HTTP, OAuth and hosted runtimes remain
outside this offline fixture.

Planned acceptance for both paths requires actual `CE_NET_ACTIVE` admission,
selected relay pairs, sustained exact application messages in both directions
and one ICE agent per peer. The courier check would also require authenticated
leave to reach the other process. Service delivery alone would not satisfy
those checks. None of these combined runtime checks has run.

The store and parcel-crypto modules are unchanged copies of their separately
reviewed offline implementation. They are configured to use the pinned
Monocypher primitive; there is no new encryption implementation or real key
custody. The planned fixture would keep temporary grants in protected files
and memory; that boundary has not been checked in this combined run.

## Planned refusal controls

- Require a live direct CE host to admit a direct fixture guest, then use a
  new host as a trap while both ICE peers have fabricated grants but no TURN
  server. Require both actual ICE agents to start and preparation to fail
  without admitting either peer or connecting to the direct host. An explicit
  future poll would advance only the session preparation deadline to bound
  the control.
- Require different actual CE identities to produce different request
  contexts, with courier refusal before cross-context request publication or
  ICE-agent creation.
- Alter an authentication tag that passes the courier's structural checks,
  then require the actual guest exchange to refuse it without producing an
  answer or active session.
- Require unknown, expired, wrong-audience and nonmember fabricated identities
  to make no issuer calls. Also require refusal of another role's request, a
  body identity or a changed staged context.
- Scan runtime logs for invented relay credentials and exchange markers, and
  SQLite files for fabricated credential canaries. The planned run uses the
  normal CE bridge's dependency-log suppression.

## Planned build and run

The following commands are unexecuted instructions for future verification
after the shared hold is cleared. Supply the already pinned local archives
from [ICE-BRIDGE.md](ICE-BRIDGE.md). The builder is configured to verify and
extract them into a new ignored output directory, compile the unchanged
production C++ sources and record exact input and binary hashes in
`build-receipt.json`. It has no download step.

```sh
python3 port/tools/ice_flow_integration.py \
  --juice-archive ARCHIVE --enet-archive ARCHIVE \
  --monocypher-archive ARCHIVE \
  --output local-data/flow-build --target linux32 \
  --toolchain-data EXISTING_PINNED_TOOLCHAIN
```

For a host-width check, select `--target native` and omit the toolchain path.
The builder selects the host width for the Python store's local Monocypher
library, including when configured for i686 CE peers.

Run only inside a fresh owned network namespace, with the synthetic hosts
mapping above, a private `HOME` and temporary directory, no desktop/session
environment, disabled core dumps, and the repository's bounded CPU/memory and
heavy-job coordination. Mount source, archives and existing toolchains read
only; permit writes only to the new ignored evidence directory. With those
conditions in place, add `--run` to the build command or run the frozen build:

```sh
python3 port/tools/ice_flow_integration_tests.py \
  --build local-data/flow-build --output local-data/flow-check
```

The runner refuses an ordinary network namespace before starting any relay and
verifies the recorded source, peer binary and crypto-library hashes before using
a build. It uses fixed milestone labels and an ignored output directory for
private records and logs. Child lifetime, shutdown and collection checks passed
in the Linux i686 offline run summarized above.

## Remaining work

This integration does not supply remote transfer, authenticated confidential
delivery, a real identity verifier, deployment, credential renewal, TURN
TCP/TLS, safe Windows private-file acceptance or a player-facing host/join flow.
The offline fixture validates the join between the real session APIs and the
existing delivery components on Linux i686, and the separate bounded probe
validated single-issuance UDP relay traffic through `turn.cloudflare.com`.
Real game processes, authenticated START and campaign restarts, packages, two
machines and sustained gameplay remain separate acceptance work.
