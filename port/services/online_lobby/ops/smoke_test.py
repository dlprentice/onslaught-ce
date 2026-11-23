#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Non-destructive end-to-end smoke test for Onslaught CE Online Lobby.

Uses ephemeral in-memory Ed25519 keys and an unlisted room, and cleans up the room on exit.
TURN credential issuance is skipped unless `--check-turn` is explicitly requested.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import secrets
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402


# Pure-Python RFC 8032 Ed25519 fallback when `cryptography` is not installed.
_ED_P = 2**255 - 19
_ED_L = 2**252 + 27742317777372353535851937790883648493
_ED_D = (-121665 * pow(121666, _ED_P - 2, _ED_P)) % _ED_P
_ED_BY = (4 * pow(5, _ED_P - 2, _ED_P)) % _ED_P


def _ed_xrecover(y: int) -> int:
    xx = (y * y - 1) * pow(_ED_D * y * y + 1, _ED_P - 2, _ED_P) % _ED_P
    x = pow(xx, (_ED_P + 3) // 8, _ED_P)
    if (x * x - xx) % _ED_P != 0:
        x = (x * pow(2, (_ED_P - 1) // 4, _ED_P)) % _ED_P
    if x % 2 != 0:
        x = _ED_P - x
    return x


_ED_B = (_ed_xrecover(_ED_BY), _ED_BY, 1, (_ed_xrecover(_ED_BY) * _ED_BY) % _ED_P)


def _ed_add(p: tuple[int, int, int, int], q: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    x1, y1, z1, t1 = p
    x2, y2, z2, t2 = q
    a = ((y1 - x1) * (y2 - x2)) % _ED_P
    b = ((y1 + x1) * (y2 + x2)) % _ED_P
    c = (t1 * 2 * _ED_D * t2) % _ED_P
    d = (z1 * 2 * z2) % _ED_P
    e = (b - a) % _ED_P
    f = (d - c) % _ED_P
    g = (d + c) % _ED_P
    h = (b + a) % _ED_P
    return (e * f) % _ED_P, (g * h) % _ED_P, (f * g) % _ED_P, (e * h) % _ED_P


def _ed_scalarmult(pt: tuple[int, int, int, int], scalar: int) -> tuple[int, int, int, int]:
    res = (0, 1, 1, 0)
    cur = pt
    n = scalar
    while n > 0:
        if n & 1:
            res = _ed_add(res, cur)
        cur = _ed_add(cur, cur)
        n >>= 1
    return res


def _ed_encode(pt: tuple[int, int, int, int]) -> bytes:
    x, y, z, _ = pt
    zi = pow(z, _ED_P - 2, _ED_P)
    x = (x * zi) % _ED_P
    y = (y * zi) % _ED_P
    return (y | ((x & 1) << 255)).to_bytes(32, "little")


class EphemeralSigner:
    """RFC 8032 Ed25519 signer (uses cryptography if available, else stdlib fallback)."""

    def __init__(self, seed: bytes | None = None):
        self._seed = seed if seed is not None else secrets.token_bytes(32)
        if len(self._seed) != 32:
            raise ValueError("Ed25519 seed must be 32 bytes.")
        try:
            from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
            from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
            self._priv = Ed25519PrivateKey.from_private_bytes(self._seed)
            self.pubkey_bytes = self._priv.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
        except ImportError:
            self._priv = None
            h = hashlib.sha512(self._seed).digest()
            a = int.from_bytes(h[:32], "little")
            a &= (1 << 254) - 8
            a |= 1 << 254
            self._a = a
            self._prefix = h[32:]
            self.pubkey_bytes = _ed_encode(_ed_scalarmult(_ED_B, a))
        self.pubkey_hex = self.pubkey_bytes.hex()
        self.player_id, self.tag = common.derive_player_identity(self.pubkey_bytes)

    def sign(self, message: bytes) -> bytes:
        if self._priv is not None:
            return self._priv.sign(message)
        r = int.from_bytes(hashlib.sha512(self._prefix + message).digest(), "little") % _ED_L
        r_enc = _ed_encode(_ed_scalarmult(_ED_B, r))
        k = int.from_bytes(hashlib.sha512(r_enc + self.pubkey_bytes + message).digest(), "little") % _ED_L
        s = (r + k * self._a) % _ED_L
        return r_enc + s.to_bytes(32, "little")


def authenticate(scheme: str, host: str, port: int, signer: EphemeralSigner, callsign: str, build_id: str) -> str:
    code, _, ch = common.http_json_request(
        scheme, host, port, "POST", "/ce-lobby/v1/auth/challenge",
        body={"pubkey": signer.pubkey_hex, "callsign": callsign, "build_id": build_id, "content_id": ""},
    )
    if code != 200:
        raise RuntimeError(f"Challenge failed with HTTP {code}: {ch}")
    msg = f"onslaught-ce-lobby-auth-v1\n{ch['nonce']}\n{ch['issued_at']}\n{signer.pubkey_hex}\n{callsign}".encode("utf-8")
    sig_hex = signer.sign(msg).hex()
    code, _, sess = common.http_json_request(
        scheme, host, port, "POST", "/ce-lobby/v1/auth/session",
        body={
            "pubkey": signer.pubkey_hex,
            "callsign": callsign,
            "nonce": ch["nonce"],
            "issued_at": ch["issued_at"],
            "signature": sig_hex,
            "build_id": build_id,
            "content_id": "",
        },
    )
    if code != 200:
        raise RuntimeError(f"Session auth failed with HTTP {code}: {sess}")
    if sess["player_id"] != signer.player_id or sess["tag"] != signer.tag:
        raise RuntimeError("Derived player identity mismatch.")
    return sess["session_token"]


def run_smoke(raw_url: str, *, allow_loopback_http: bool = False, check_turn: bool = False, build_id: str = "0.1.14") -> dict:
    scheme, host, port = common.parse_service_url(raw_url, allow_loopback_http=allow_loopback_http)

    # 1. Health check.
    code, _, health = common.http_json_request(scheme, host, port, "GET", "/ce-lobby/v1/health")
    if code != 200 or not health.get("ok"):
        raise RuntimeError(f"Health check failed: HTTP {code} {health}")

    # 2. Authenticate two ephemeral players.
    host_signer = EphemeralSigner()
    guest_signer = EphemeralSigner()
    host_tok = authenticate(scheme, host, port, host_signer, "SmokeHost", build_id)
    guest_tok = authenticate(scheme, host, port, guest_signer, "SmokeGuest", build_id)

    room_id = None
    turn_servers_count = 0
    try:
        # 3. Create an unlisted room so public browsers stay clean.
        code, _, room = common.http_json_request(
            scheme, host, port, "POST", "/ce-lobby/v1/lobbies",
            bearer=host_tok,
            body={
                "name": "Smoke Check",
                "mode": "versus_2v2",
                "map": 850,
                "max_seats": 4,
                "topology": "star_lockstep",
                "visibility": "unlisted",
                "password": "",
                "require_relay": False,
                "ai_fill": False,
                "build_id": build_id,
                "content_id": "",
                "be_cfg": "Aquila",
            },
        )
        if code != 200:
            raise RuntimeError(f"Create room failed: HTTP {code} {room}")
        room_id = room["room_id"]
        room_code = room["code"]

        # 4. Resolve code & join as guest.
        code, _, resolved = common.http_json_request(
            scheme, host, port, "POST", "/ce-lobby/v1/lobbies/resolve",
            bearer=guest_tok, body={"code": room_code},
        )
        if code != 200 or resolved["room_id"] != room_id:
            raise RuntimeError(f"Resolve failed: HTTP {code} {resolved}")

        code, _, joined = common.http_json_request(
            scheme, host, port, "POST", f"/ce-lobby/v1/rooms/{room_id}/join",
            bearer=guest_tok, body={"password": "", "build_id": build_id, "content_id": "", "be_cfg": "Aquila"},
        )
        if code != 200:
            raise RuntimeError(f"Join failed: HTTP {code} {joined}")

        # 5. Guest ready + chat.
        code, _, _ = common.http_json_request(
            scheme, host, port, "POST", f"/ce-lobby/v1/rooms/{room_id}/update",
            bearer=guest_tok, body={"ready": True, "rtt_ms": 25},
        )
        if code != 200:
            raise RuntimeError(f"Ready update failed: HTTP {code}")

        code, _, chatted = common.http_json_request(
            scheme, host, port, "POST", f"/ce-lobby/v1/rooms/{room_id}/chat",
            bearer=host_tok, body={"text": "smoke check ok"},
        )
        if code != 200 or len(chatted["chat"]) != 1:
            raise RuntimeError(f"Chat failed: HTTP {code}")

        # 6. Start match & verify ticket.
        code, _, started = common.http_json_request(
            scheme, host, port, "POST", f"/ce-lobby/v1/rooms/{room_id}/start",
            bearer=host_tok, body={},
        )
        if code != 200 or started["state"] != "starting" or not started.get("start_ticket"):
            raise RuntimeError(f"Start failed: HTTP {code}")
        match_id = started["start_ticket"]["match_id"]

        # 7. Exchange ICE offer/answer envelopes.
        code, _, _ = common.http_json_request(
            scheme, host, port, "POST", f"/ce-lobby/v1/rooms/{room_id}/signal",
            bearer=host_tok,
            body={"match_id": match_id, "from_seat": 0, "to_seat": 1, "kind": "offer", "sdp": "v=0\r\na=ice-ufrag:smoke"},
        )
        if code != 200:
            raise RuntimeError(f"Host signal failed: HTTP {code}")
        code, _, poll = common.http_json_request(
            scheme, host, port, "GET", f"/ce-lobby/v1/rooms/{room_id}/signal?match_id={match_id}&since=0",
            bearer=guest_tok,
        )
        if code != 200 or len(poll["messages"]) != 1:
            raise RuntimeError(f"Guest signal poll failed: HTTP {code} {poll}")

        # 8. Optional TURN check (opt-in only).
        if check_turn:
            code, _, turn_res = common.http_json_request(
                scheme, host, port, "POST", f"/ce-lobby/v1/rooms/{room_id}/turn",
                bearer=guest_tok, body={"match_id": match_id, "guest_seat": 1},
            )
            if code != 200 or not turn_res.get("iceServers"):
                raise RuntimeError(f"TURN check failed: HTTP {code}")
            turn_servers_count = len(turn_res["iceServers"])

        # 9. Finish match.
        code, _, finished = common.http_json_request(
            scheme, host, port, "POST", f"/ce-lobby/v1/rooms/{room_id}/finish",
            bearer=host_tok, body={},
        )
        if code != 200 or finished["state"] != "lobby":
            raise RuntimeError(f"Finish failed: HTTP {code}")
    finally:
        if room_id:
            try:
                common.http_json_request(
                    scheme, host, port, "POST", f"/ce-lobby/v1/rooms/{room_id}/leave",
                    bearer=host_tok, body={},
                )
            except Exception:
                pass

    return {
        "ok": True,
        "proto": health["proto"],
        "checked_turn": check_turn,
        "turn_servers": turn_servers_count,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run end-to-end smoke test against Onslaught CE Online Lobby.")
    parser.add_argument("--url", required=True, help="Lobby service origin (https://... or loopback).")
    parser.add_argument("--allow-loopback-http", action="store_true",
                        help="Allow http://127.0.0.1:<port> for local workerd testing.")
    parser.add_argument("--check-turn", action="store_true",
                        help="Also exercise /turn (consumes 1 permit from daily/monthly budget).")
    parser.add_argument("--build-id", default="0.1.14", help="Build ID to use for smoke check.")
    args = parser.parse_args(argv)
    result = run_smoke(
        args.url,
        allow_loopback_http=args.allow_loopback_http,
        check_turn=args.check_turn,
        build_id=args.build_id,
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
