<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Onslaught CE Online Lobby Service (Cloudflare Free Tier)

Status: active — local `workerd` runtime validation; deployment requires operator secrets and review.
Protocol contract: [`PROTOCOL.md`](PROTOCOL.md) (`proto = 1`, `max_match_slots = 4`, `wire_max_slots = 64`).

## Architecture

Two Cloudflare Workers keep public lobby traffic isolated from TURN credentials:

1. **`onslaught-ce-lobby`** ([`worker.mjs`](worker.mjs), [`wrangler.toml`](wrangler.toml)):
   - Public HTTPS API under `/ce-lobby/v1/` (`workers_dev = true`, `preview_urls = false`, `[observability] enabled = false`, `CE_ENABLED = "false"` by default).
   - **`LobbyDirectory` (SQLite Durable Object):** single-use Ed25519 challenge nonces, per-IP (HMAC-hashed) and per-pubkey auth rate limits, public/unlisted room index with 30-second host heartbeat pruning, compatibility filtering (`build_id` + `content_id`), cursor paging, 6-character code resolution, and global UTC daily/monthly TURN permit caps.
   - **`GameRoom` (SQLite Durable Object, one per room):** 64-ready `RoomState` (`star_lockstep` capped at 4 seats), seat management (`human` / `ai` / `open` / `closed`), password hashing with per-room random salt, bounded 32-message printable-ASCII chat ring with rate limiting, host kick list, match start tickets (`match_id`, `seed`, `roster_hash`, `match_secret`, `seat -> sim_slot` map), directed Host-to-Guest ICE signaling, and one-time TURN permit reservations.
   - **`PermitAuthority` (named Worker entrypoint):** private Service Binding target called only by `issuer.mjs` to verify room state (`starting`/`in_match`, `>= 2` authenticated humans), atomically spend the one-time permit, and enforce `CE_DAILY_TURN_LIMIT` (default `200`) and `CE_MONTHLY_TURN_LIMIT` (default `2000`) before any upstream TURN request.

2. **`onslaught-ce-turn-issuer`** ([`issuer.mjs`](issuer.mjs), [`issuer.wrangler.toml`](issuer.wrangler.toml)):
   - Private non-public Worker (`workers_dev = false`, `preview_urls = false`).
   - Only this Worker holds `TURN_KEY_ID` and `TURN_API_TOKEN`.
   - Verifies HMAC-signed permits, consumes them atomically via `PERMIT_AUTHORITY`, contacts the fixed Cloudflare Realtime TURN credential endpoint, filters STUN + UDP TURN (when `require_relay = false`) or UDP TURN only (when `require_relay = true`), rejects redirects/untrusted URLs/master-token echoes, and fails closed (`503`) on any error.

## Secrets (Never Committed)

Secrets are uploaded exclusively over standard input via `wrangler secret put` (never placed in TOML files, command-line arguments, or logs):

| Worker | Secret | Format |
| --- | --- | --- |
| `onslaught-ce-lobby` | `CE_SESSION_SECRET` | 64 lowercase hex chars (32 random bytes) |
| `onslaught-ce-lobby` | `CE_ISSUER_PERMIT_KEY` | 64 lowercase hex chars (32 random bytes, shared with issuer) |
| `onslaught-ce-turn-issuer` | `CE_ISSUER_PERMIT_KEY` | Same 64 lowercase hex chars as above |
| `onslaught-ce-turn-issuer` | `TURN_KEY_ID` | Cloudflare Realtime TURN key ID |
| `onslaught-ce-turn-issuer` | `TURN_API_TOKEN` | Cloudflare Realtime TURN API token |

`CE_SESSION_SECRET` and `CE_ISSUER_PERMIT_KEY` must be distinct random keys.

## Operator Toolkit (`ops/`)

All deployment scripts invoke the repository's ignored local Wrangler wrapper (`local-data/cloudflare-operator/wrangler.py`) and pipe secrets through `stdin`:

- [`ops/deploy.py`](ops/deploy.py): runs the two-step service-binding bootstrap (`issuer --env bootstrap` -> `lobby` -> stdin-piped secret upload -> full `issuer`), defaulting to `CE_ENABLED=false` unless `--enable` is explicitly passed. Supports `--dry-run`.
- [`ops/status.py`](ops/status.py): queries `/ce-lobby/v1/health`, verifies strict HTTP response headers, and optionally inspects recent Wrangler deployments (`--wrangler`).
- [`ops/killswitch.py`](ops/killswitch.py): toggles `CE_ENABLED=false` (`--disable`) or `CE_ENABLED=true` (`--enable`) across both Workers with optional health verification.
- [`ops/smoke_test.py`](ops/smoke_test.py): exercises Ed25519 challenge-response auth, unlisted room creation/resolution, join, ready, chat, start ticket, ICE signaling, finish, and leave against a target URL (never burns TURN quota unless `--check-turn` is passed).

Example dry-run review:

```sh
python3 port/services/online_lobby/ops/deploy.py --dry-run
```

## Local Testing (`workerd`)

Run the full local `workerd` integration suite (real `worker.mjs` + `issuer.mjs` + loopback mock TURN upstream + real WebCrypto Ed25519 verification + SQLite Durable Objects):

```sh
python3 -B port/tools/online_lobby_worker_tests.py
```

[`workerd.capnp`](workerd.capnp) provides a disabled loopback entrypoint (`127.0.0.1:8788`, `CE_ENABLED=false`) for manual inspection.
