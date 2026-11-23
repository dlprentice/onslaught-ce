#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Emergency kill switch for Onslaught CE Online Lobby (`CE_ENABLED=false|true`)."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Toggle CE_ENABLED on lobby and/or issuer Workers.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--disable", action="store_true", help="Fail-closed: set CE_ENABLED=false.")
    group.add_argument("--enable", action="store_true", help="Re-enable service: set CE_ENABLED=true.")
    parser.add_argument("--target", choices=("both", "lobby", "issuer"), default="both",
                        help="Which Worker(s) to update.")
    parser.add_argument("--verify-url", default=None,
                        help="Optional origin URL to verify /ce-lobby/v1/health after toggling.")
    parser.add_argument("--allow-loopback-http", action="store_true",
                        help="Allow http://127.0.0.1:<port> for --verify-url.")
    parser.add_argument("--wrangler-wrapper", default=None,
                        help="Explicit path to local-data/cloudflare-operator/wrangler.py.")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print planned Wrangler commands without executing.")
    args = parser.parse_args(argv)

    common.validate_wrangler_templates()
    wrapper = None if args.dry_run else common.find_wrangler_wrapper(args.wrangler_wrapper)
    val = "false" if args.disable else "true"

    # When disabling, disable the private TURN issuer first so TURN egress stops immediately.
    if args.target == "both":
        targets = (common.ISSUER_TOML, common.LOBBY_TOML) if args.disable else (common.LOBBY_TOML, common.ISSUER_TOML)
    elif args.target == "issuer":
        targets = (common.ISSUER_TOML,)
    else:
        targets = (common.LOBBY_TOML,)

    for cfg in targets:
        print(f"Setting CE_ENABLED={val} on {cfg.name}...")
        common.run_wrangler(
            ["deploy", "--config", cfg.name, "--var", f"CE_ENABLED:{val}"],
            wrapper=wrapper,
            dry_run=args.dry_run,
        )

    if args.verify_url and not args.dry_run:
        scheme, host, port = common.parse_service_url(
            args.verify_url, allow_loopback_http=args.allow_loopback_http
        )
        expected = 503 if args.disable else 200
        status, _, payload = common.http_json_request(scheme, host, port, "GET", "/ce-lobby/v1/health")
        if status != expected:
            raise RuntimeError(
                f"Health verification failed: expected HTTP {expected}, got {status} ({payload.get('error', '')})"
            )
        print(f"Verified /ce-lobby/v1/health returned HTTP {status}.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
