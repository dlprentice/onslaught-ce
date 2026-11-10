# Private file exchange for the experimental two-player pilot

Status: experimental Linux operator tool; cross-machine player acceptance remains open
Last updated: 2026-11-02
Summary: finite export/import of existing requests, temporary grants and encrypted ICE records through a separately chosen private channel.

The game currently has no service that delivers an invitation, a grant request,
a temporary TURN grant, or an ICE offer/answer to another machine. The existing
join code is a session capability shared manually. Possession of a code or a
request context is not account authentication or permission to operate a public
credential broker.

`tools/ice_pilot_courier.py` supplies the local ends of an explicit file handoff.
It performs one export or import and exits. It accepts only existing records
with the fixed names and schemas below. It has no listener, peer address, remote
command, provider key option, grant issuance, discovery, or renewal operation.
The separate [operator issuer](RELAY-ACCESS.md) remains responsible for a bounded,
explicit issuance decision and retains the provider's master token locally.

## Before either player starts

The two players and the operator must already agree on a private, authenticated
file channel and the intended counterpart. The courier does not select or
operate that channel. Do not publish parcels in a public link, repository, chat
log, or diagnostic report. Offer parcels contain temporary TURN credentials in
base64, which is an encoding, not encryption. The encrypted ICE record itself
is authenticated by `CeIceExchange` using the existing CE session capability;
the parcel wrapper does not establish a new authenticated identity.

Each player needs separate, existing, fresh private directories for the lobby
and game stages, selected before launch using `BEA_NET_ICE_DIRECTORY` and
`BEA_NET_ICE_GAME_DIRECTORY`. Linux directories must be owned by the current
user with no group/other permissions; files must be private, regular, singly
linked and owned by that user. The courier refuses symlinks, including directory
ancestors. Use a new private transfer directory for each outgoing parcel and a
separate private incoming directory on the receiving machine. The two directories
given to one courier invocation must be distinct.

The receiving process must already be pending with its own role-specific
`relay-host.request.json` or `relay-guest.request.json`. Import never creates
that local identity. Keep clocks synchronized: requests and parcels use UTC
seconds and refuse future issuance times. The actual caller's monotonic deadline
is authoritative if wall time changes.

## One stage, three handoffs

The operator is on the host side for this bounded workflow. The guest sends only
its existing request; the host compares its context with the host's pending
request. The operator then makes its separate explicit decision to issue the two
role-specific temporary grants. The host's grant remains on the host machine.

| Kind | Exporter → importer | Exact payload |
| --- | --- | --- |
| `request` | Guest → host/operator | Existing `relay-guest.request.json` |
| `offer` | Host/operator → guest | Existing `relay-guest.json` and `ce-ice-<context>.offer` |
| `answer` | Guest → host | Existing `ce-ice-<context>.answer` |

For each row, export on the sender, transfer the single resulting
`ice-pilot-transfer.json` through the agreed private channel, then import on the
receiver. The following commands use operator-selected directory placeholders;
they do not perform the intervening cross-machine delivery:

```sh
python3 port/tools/ice_pilot_courier.py export request --session-directory "$guest_stage" --transfer-directory "$guest_request_out"
python3 port/tools/ice_pilot_courier.py import request --session-directory "$host_stage" --transfer-directory "$host_request_in"

# Separate explicit issuance occurs here; the host publishes its encrypted offer.
python3 port/tools/ice_pilot_courier.py export offer --session-directory "$host_stage" --transfer-directory "$host_offer_out"
python3 port/tools/ice_pilot_courier.py import offer --session-directory "$guest_stage" --transfer-directory "$guest_offer_in"

# The guest accepts that offer before publishing its encrypted answer.
python3 port/tools/ice_pilot_courier.py export answer --session-directory "$guest_stage" --transfer-directory "$guest_answer_out"
python3 port/tools/ice_pilot_courier.py import answer --session-directory "$host_stage" --transfer-directory "$host_answer_in"
```

Authenticated lobby START derives a fresh game identity. Repeat all three
handoffs for the game stage using the separately configured game directories.
A retry or new mission needs fresh directories and a relaunch under the current
caller contract. Do not manufacture requests, reuse an earlier stage's grant,
edit records, or replace the game's private handoff file.

## Expiry and refusal

Each role's original connection attempt has **120 seconds total**, including
waiting for a grant, file exchange and ICE readiness. Neither grant arrival nor
an import starts a new timer. Every courier action requires an unexpired local
request, and imports enforce both the parcel deadline and that local deadline.
The encrypted exchange's 300-second ceiling does not enlarge the attempt.

Grant validity is separately bounded by the existing request's `grantExpiresAt`
and the issuer's `MAX_TTL`. The current CE caller requests a two-hour
(7,200-second) budget by default. `BEA_NET_ICE_LIFETIME_SECONDS` selects a canonical
integer from 120 through 14400 before session creation. The operator issuer's
`--ttl` also defaults to 7,200 seconds, caps at 14,400, and is clamped to the
remaining lifetime allowed by both the request and operator session deadlines.
For a session budgeted at four hours, both peers must select 14400 and the issuer
must allow it. The matching caller and issuer contract is described in
[relay access](RELAY-ACCESS.md#preparation-before-transport-creation).

These are credential bounds, not guaranteed durations of connected play. Grant
delivery, waiting for the other peer, admission and play consume the remaining
lifetime. The original 120-second preparation deadline and 300-second exchange
validity stay unchanged. The courier has no independent active-session TTL
override and does not reset issuance times; altering it or editing a request
cannot extend a running allocation. The current caller fails an active session
when its grant expires. There is no in-game refresh or ICE restart mechanism.
[Cloudflare's TURN FAQ](https://developers.cloudflare.com/realtime/turn/faq/)
also says an allocation disconnects shortly after its credentials expire, and
that maintenance or network changes can interrupt it. Cloudflare recommends
ICE restart for such disruptions.
Plan a deliberately bounded pilot and end before expiry; a short transport probe
does not establish that a long mission survives. Future renewal needs
authenticated session ownership, new grant delivery, coordinated ICE restart
and interruption/replay tests.

Only fixed `pilot transfer complete` or `pilot transfer refused` messages are
emitted. They contain no paths, contexts, raw errors, SDP or credential values.
Completion means local file publication, not peer admission, ICE connectivity,
or gameplay success. Invalid private permissions, stale requests, wrong
role/context, malformed grants, unknown parcel records, changed existing files
and expired records all refuse.

Every file is published atomically without replacement. An offer is published
before its grant so a waiting guest cannot start ICE before the offer exists.
This pair is not a filesystem transaction: a failed second publication may leave
the offer present. Reimporting the same valid bytes is idempotent within the
original deadline; it neither replaces files nor renews timestamps. If the
attempt has expired or records conflict, leave the evidence intact and start
fresh. Exporting to an occupied transfer directory also refuses when its parcel
differs; use a new directory instead of replacing it.

The selected dependency is UDP-only. [The pinned libjuice README](https://github.com/paullouisageneau/libjuice/blob/b89c792e3612faf2f12cf35bcc56857313a06be3/README.md)
states that other transport protocols are ignored. Supported provider TCP/TLS
URLs may be recognized in a response, but the loader configures only the fixed
UDP TURN URLs. Networks that block those UDP routes have no demonstrated fallback.
Windows private-directory and actual game acceptance remain separate gates.

## Focused offline checks

The boundary suite uses fabricated grants and envelopes in distinct private
directories. It intercepts socket/DNS, key loading and issuance calls so any use
fails the test. It checks the three handoffs, private file rules, exact bytes,
request/context/role/expiry boundaries, no replacement, fixed output, incomplete
publication and changed-directory refusal.

An optional fixture compiles the exact pinned `CeIceExchange` source with
Monocypher 4.0.2 and drives two local processes through the courier. It checks
that offer delivery is necessary, both encrypted descriptions are accepted,
changed ciphertext is rejected, and an accepted record cannot be replaced.
This runs without an ICE agent, network, provider request, game, real grant or
master token. It establishes file-format interoperability, not cross-machine
channel authentication or gameplay.

```sh
python3 port/tools/ice_pilot_courier_tests.py --output local-data/courier-check
python3 port/tools/ice_pilot_courier_tests.py --output local-data/courier-exchange-check \
  --exchange-source "$pinned_exchange_checkout" --monocypher-archive "$existing_monocypher_archive"
```

The helper verifies source hashes before compiling. It never downloads inputs.
Monocypher's source URL is `https://monocypher.org/download/monocypher-4.0.2.tar.gz`,
SHA-256 `38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c`,
licensed `BSD-2-Clause OR CC0-1.0`. Full player acceptance still requires the exact
composed game callers, an agreed private channel, both stage handoffs inside the
original deadlines, sustained relay-only gameplay, an expiry/disruption control
and the intended platform. No such acceptance is established by this tool.
