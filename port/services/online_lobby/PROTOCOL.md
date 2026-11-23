<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Onslaught CE Online Lobby & Star-Relay Protocol (`v1`)

Status: authoritative contract for `port/services/online_lobby/`, `port/network/` (`online_profile`, `online_lobby_client`, `star_transport`), `port/game/` (`ce_online_match`, `ce_online_ui`), and `port/tools/` tests.
Protocol version: `proto = 1`
Active `0.1.14` match cap: `CE_ONLINE_MAX_MATCH_SLOTS = 4` (2–4 players, `topology = "star_lockstep"`).
Protocol/schema ceiling: `CE_ONLINE_WIRE_MAX_SLOTS = 64` (`uint16` seats, `uint16` bitmasks/arrays, `topology` field ready for future 32v32 `"authoritative"` / `"relay_server"` Assault mode with zero lobby/identity redesign).

---

## 1. Zero-Account Local Player Profile & Authentication

- **Keypair:** Local Ed25519 keypair stored in `<user-dir>/online-profile.key` (`0600` on Linux; `CeWinAcl` owner-only DACL on Windows). Never logged, committed, or sent.
- **Profile settings & local stats:** `<user-dir>/online-profile.ini` (`callsign`, `matches_played`, `matches_won`, `preferred_side`, `lobby_url_override`).
- **Identity derivation:**
  - `pubkey_hex`: 64 lowercase hex chars (32-byte Ed25519 public key).
  - `player_id`: lowercase Crockford base32 of the first 16 bytes of `SHA-256(pubkey_bytes)` (26 chars, canonical internal identity).
  - `tag`: 4 uppercase hex chars from the first 2 bytes of `SHA-256(pubkey_bytes)` (display suffix, always rendered as `Callsign#TAG`).
  - `callsign`: 3–20 printable ASCII chars (`[A-Za-z0-9 _-]`, trimmed, collapsed spaces, no control chars; defaults to `"Pilot"` + 4 hex chars).
- **Challenge-Response Authentication (Anti-Replay):**
  1. `POST /ce-lobby/v1/auth/challenge` (no auth header)
     - Request: `{"pubkey":"<64-hex>","callsign":"<3..20 ascii>","build_id":"<CE_VERSION>","content_id":"<64-hex or empty>"}`
     - Response `200`: `{"nonce":"<32-hex>","issued_at":<unix_s>,"expires_at":<unix_s>,"server_time_ms":<unix_ms>}`
  2. Client signs the canonical UTF-8 domain message using Ed25519 (`crypto_eddsa_sign`):
     `onslaught-ce-lobby-auth-v1\n<nonce>\n<issued_at>\n<pubkey_hex>\n<callsign>`
  3. `POST /ce-lobby/v1/auth/session` (no auth header)
     - Request: `{"pubkey":"<64-hex>","callsign":"<3..20 ascii>","nonce":"<32-hex>","issued_at":<unix_s>,"signature":"<128-hex>","build_id":"<CE_VERSION>","content_id":"<64-hex or empty>"}`
     - Response `200`: `{"session_token":"<opaque base64url>","player_id":"<26-b32>","tag":"<4-HEX>","callsign":"<sanitized>","expires_at":<unix_s>,"server_time_ms":<unix_ms>}`
  - Server marks `nonce` single-use (replay returns `401`), enforces `|now - issued_at| <= 120s`, and rate-limits by BOTH IP and `pubkey`.
  - Authenticated requests send header `Authorization: Bearer <session_token>`.

---

## 2. Common HTTP Rules (Matches `CeSignalHttps` Strictness)

Every Worker response MUST set:
- `Content-Type: application/json`
- `Cache-Control: no-store`
- `Pragma: no-cache`
- `X-Content-Type-Options: nosniff`
- `Content-Encoding: identity` (no compression)
- No redirects (`Location` absent), no `Set-Cookie`.
- Allowed HTTP status codes: `200`, `400`, `401`, `403`, `404`, `409`, `429`, `503`.
- Error body shape: `{"error":"<fixed_code>","message":"<short safe ASCII sentence>"}`.
- All route paths start with `/ce-lobby/v1/`.

---

## 3. Lobby Directory Endpoints (`LobbyDirectory` Durable Object)

- `GET /ce-lobby/v1/health` (no auth required)
  - Response `200`: `{"ok":true,"enabled":true,"proto":1,"max_match_slots":4,"wire_max_slots":64,"server_time_ms":<unix_ms>,"active_rooms":<int>}`
  - Returns `503` when `CE_ENABLED !== "true"`.
- `GET /ce-lobby/v1/lobbies?cursor=<int>&limit=<1..32>&build_id=<str>&content_id=<str>&include_incompatible=<0|1>` (Bearer auth)
  - Prunes stale rooms whose last host heartbeat is `> 30s` old.
  - Response `200`:
    ```json
    {
      "proto": 1,
      "server_time_ms": 1763900000000,
      "next_cursor": 0,
      "rooms": [
        {
          "room_id": "rm_<16-hex>",
          "code": "ABCD12",
          "name": "Forseti Skirmish",
          "host_callsign": "AcePilot",
          "host_tag": "4F9A",
          "topology": "star_lockstep",
          "mode": "versus_2v2",
          "map": 850,
          "used_seats": 2,
          "human_seats": 2,
          "max_seats": 4,
          "has_password": false,
          "require_relay": false,
          "state": "lobby",
          "proto": 1,
          "build_id": "0.1.14",
          "content_id": "",
          "compatible": true,
          "updated_at": 1763900000
        }
      ]
    }
    ```
- `POST /ce-lobby/v1/lobbies` (Bearer auth — creates a new room & puts creator in `seat 0` as host)
  - Request:
    ```json
    {
      "name": "Forseti Skirmish",
      "mode": "versus_2v2",
      "map": 850,
      "max_seats": 4,
      "topology": "star_lockstep",
      "visibility": "public",
      "password": "",
      "require_relay": false,
      "ai_fill": true,
      "build_id": "0.1.14",
      "content_id": "",
      "be_cfg": ""
    }
    ```
  - Valid `mode`: `"coop_defence"`, `"coop_skirmish"`, `"versus_2v2"`, `"tutorial"`.
  - Valid `map`: `100, 850, 851, 852, 853, 854, 855, 856, 857, 858, 859, 860, 861, 862, 863, 864, 865, 866`.
  - `max_seats`: `2..4` when `topology == "star_lockstep"` (schema supports `2..64` for future topologies).
  - `visibility`: `"public"` (listed in directory) or `"unlisted"` (joinable by 6-char `code` only).
  - Response `200`: full `RoomState` object (see Section 4) + `"room_id"` + `"code"`.
- `POST /ce-lobby/v1/lobbies/resolve` (Bearer auth — resolve a 6-char room code to `room_id`)
  - Request: `{"code":"ABCD12"}`
  - Response `200`: `{"room_id":"rm_<16-hex>","has_password":false,"state":"lobby"}` (or `404`).

---

## 4. Per-Room Endpoints (`GameRoom` Durable Object)

### Canonical `RoomState` Shape (64-ready!)
```json
{
  "proto": 1,
  "room_id": "rm_<16-hex>",
  "code": "ABCD12",
  "rev": 7,
  "state": "lobby",
  "name": "Forseti Skirmish",
  "topology": "star_lockstep",
  "mode": "versus_2v2",
  "map": 850,
  "max_seats": 4,
  "host_seat": 0,
  "visibility": "public",
  "has_password": false,
  "require_relay": false,
  "ai_fill": true,
  "build_id": "0.1.14",
  "content_id": "",
  "server_time_ms": 1763900000000,
  "teams": [
    {"team": 0, "side": "forseti", "cap": 2},
    {"team": 1, "side": "muspell", "cap": 2}
  ],
  "seats": [
    {
      "seat": 0,
      "kind": "human",
      "player_id": "<26-b32>",
      "tag": "4F9A",
      "callsign": "AcePilot",
      "team": 0,
      "be_cfg": "Aquila",
      "ready": true,
      "rtt_ms": 28,
      "route": "unknown"
    }
  ],
  "chat": [
    {"seq": 1, "seat": 0, "player_id": "<26-b32>", "callsign": "AcePilot", "tag": "4F9A", "text": "gl hf", "ts": 1763900000}
  ],
  "start_ticket": null
}
```
- `state`: `"lobby"` | `"starting"` | `"in_match"` | `"closed"`.
- `seat`: `uint16` (`0 .. max_seats-1`); `kind`: `"human"` | `"ai"` | `"open"` | `"closed"`.
- `route`: `"unknown"` | `"direct"` | `"relay"`.
- `chat`: bounded ring of last `32` messages; each `text` sanitized printable ASCII `1..160` bytes; rate limited (`<= 5` msgs / `10s` per player).
- `start_ticket`: non-null when `state == "starting"` or `"in_match"`:
  ```json
  {
    "match_id": "mt_<16-hex>",
    "seed": 1234567890,
    "level": 850,
    "mode": "versus_2v2",
    "topology": "star_lockstep",
    "require_relay": false,
    "roster_hash": "<64-hex>",
    "match_secret": "<64-hex>",
    "issued_at": 1763900000,
    "expires_at": 1763907200,
    "slots": [
      {"seat": 0, "sim_slot": 0, "kind": "human", "player_id": "<26-b32>", "callsign": "AcePilot", "tag": "4F9A", "team": 0, "be_cfg": "Aquila"}
    ]
  }
  ```
  *(Note: `match_secret` is returned ONLY to authenticated human members of the room, never in directory listings or logs!)*

### Room Routes (all Bearer auth)
- `GET /ce-lobby/v1/rooms/:id?since=<rev>&rtt_ms=<0..9999>` — heartbeat + poll state (returns `{"unchanged":true,"rev":<rev>,"server_time_ms":<ms>}` when `rev <= since`, else full `RoomState`).
- `POST /ce-lobby/v1/rooms/:id/join` — body `{"password":"","build_id":"0.1.14","content_id":"","be_cfg":""}`; assigns lowest `"open"` (or `"ai"` if `ai_fill`) seat; returns full `RoomState`. Refuses incompatible `build_id`/`content_id` (`409`), wrong password (`403`), full room (`409`), kicked player (`403`).
- `POST /ce-lobby/v1/rooms/:id/leave` — body `{}`; frees seat (or closes room if host leaves).
- `POST /ce-lobby/v1/rooms/:id/update` — body updates caller's seat (`team`, `be_cfg`, `ready`, `rtt_ms`, `route`) OR host-only room fields (`name`, `map`, `mode`, `max_seats`, `visibility`, `require_relay`, `ai_fill`, `set_seat_kind: {seat, kind}`, `kick_seat`). Changing `map`/`mode`/`require_relay` resets all non-host `ready = false`.
- `POST /ce-lobby/v1/rooms/:id/chat` — body `{"text":"..."}`; appends sanitized ASCII message.
- `POST /ce-lobby/v1/rooms/:id/start` — host-only; requires `>= 2` authenticated humans (or `>= 1` human when `allow_solo_test` is false -> strictly requires `>= 2` humans in production!), every human seat `ready == true` (host counts ready on Start), generates `start_ticket` (`match_id`, deterministic 32-bit `seed`, `match_secret`, `roster_hash`, explicit `seat -> sim_slot` map), sets `state = "starting"`.
- `POST /ce-lobby/v1/rooms/:id/finish` — host-only (or returns room to `"lobby"` when match ends, clearing `start_ticket` and resetting `ready = false`).
- **Multi-Guest Star ICE Signaling & Per-Guest TURN Credentials (only in `state == "starting"` or `"in_match"`):**
  - `POST /ce-lobby/v1/rooms/:id/turn` — body `{"match_id":"...","guest_seat":<u16>}` (callable by Host for each human guest seat, or by that Guest for their own seat); verifies room is `"starting"`/`"in_match"` with `>= 2` authenticated humans, mints an atomic single-use permit, calls private `TURN_ISSUER` Service Binding, returns `{"iceServers":[...],"ttl":<s_int>}` (STUN + UDP TURN servers from Cloudflare Realtime TURN; fails closed `503` if `CE_ENABLED !== "true"` or daily/monthly cap reached).
  - `POST /ce-lobby/v1/rooms/:id/signal` — body `{"match_id":"...","from_seat":<u16>,"to_seat":<u16>,"kind":"offer"|"answer","sdp":"<bounded ICE SDP <= 4096 bytes>"}`; stores directed ICE description for `to_seat` (Host `0` <-> Guest `seat`).
  - `GET /ce-lobby/v1/rooms/:id/signal?match_id=<id>&since=<seq>` — returns directed signaling envelopes addressed to caller's `seat` (`{"seq":<int>,"messages":[{"seq":1,"from_seat":0,"to_seat":1,"kind":"offer","sdp":"..."}]}`).

---

## 5. Star Host-Relay Wire Protocol (`star_transport.h` / `npeer_ring.h`)

- **Topology:** Star Host-Relay (`Host = sim_slot 0`, `Guests = sim_slot 1..N-1`).
- **ENet + Multi-Slot `CeIceBridge`:**
  - Host creates ONE `ENetHost` bound to `127.0.0.1:0` (or selected LAN IP for direct mode) with `peerCount = max_guests` (`1..3` in `0.1.14`, protocol ceiling `63`).
  - For each online Guest `i`, Host creates one `CeIceBridge` bound to its own ephemeral `127.0.0.1:<bridge_port_i>` (`require_relay = ticket.require_relay`). ENet naturally demultiplexes every guest by `(127.0.0.1, bridge_port_i)`.
  - Each Guest connects to its own single `CeIceBridge` -> Host.
- **Extensible Wire Frame Header (64-ready, Big-Endian):**
  - `proto (u16) = 1`, `msg_type (u16)`, `payload_len (u16)`, followed by message body / TLV fields (unknown TLVs skipped cleanly).
  - Bitmasks on the wire use `u16 nbits` + `ceil(nbits/8)` bytes (mapped to/from `CeNPeerLockstepRing` `uint8` masks for `<= 8` peers).
  - Message types: `STAR_HELLO (1)`, `STAR_ACCEPT (2)`, `STAR_INPUT_GUEST (10)`, `STAR_INPUT_BUNDLE (11)`, `STAR_HASH (12)`, `STAR_DESYNC (13)`, `STAR_CHAT (20)`, `STAR_PING_REPORT (21)`, `STAR_LEAVE (30)`.
- **Security:** All star packets travel over authenticated Monocypher AEAD encrypted frames keyed from `ticket.match_secret` + per-peer handshake nonces + `compatibility_digest`.
