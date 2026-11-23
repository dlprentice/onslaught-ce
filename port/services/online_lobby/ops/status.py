#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check live health, strict HTTP headers, and optional Wrangler deployment status."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Inspect Onslaught CE Online Lobby status.")
    parser.add_argument("--url", default=None, help="Lobby service origin (https://... or loopback).")
    parser.add_argument("--allow-loopback-http", action="store_true",
                        help="Allow http://127.0.0.1:<port> for local workerd inspection.")
    parser.add_argument("--wrangler", action="store_true",
                        help="Also list recent Cloudflare deployments via local Wrangler wrapper (read-only).")
    parser.add_argument("--wrangler-wrapper", default=None,
                        help="Explicit path to local-data/cloudflare-operator/wrangler.py.")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show read-only Wrangler commands without executing.")
    args = parser.parse_args(argv)

    if not args.url and not args.wrangler:
        parser.error("Specify --url and/or --wrangler.")

    exit_code = 0

    if args.url:
        scheme, host, port = common.parse_service_url(args.url, allow_loopback_http=args.allow_loopback_http)
        t0 = time.monotonic()
        status, _, payload = common.http_json_request(scheme, host, port, "GET", "/ce-lobby/v1/health")
        rtt_ms = int((time.monotonic() - t0) * 1000)
        summary = {
            "http_status": status,
            "rtt_ms": rtt_ms,
            "ok": payload.get("ok"),
            "enabled": payload.get("enabled"),
            "proto": payload.get("proto"),
            "max_match_slots": payload.get("max_match_slots"),
            "wire_max_slots": payload.get("wire_max_slots"),
            "active_rooms": payload.get("active_rooms"),
        }
        print(json.dumps(summary, indent=2, sort_keys=True))
        if status == 503:
            exit_code = 2
        elif status != 200 or not payload.get("ok"):
            exit_code = 1

    if args.wrangler:
        wrapper = None if args.dry_run else common.find_wrangler_wrapper(args.wrangler_wrapper)
        for cfg in (common.LOBBY_TOML, common.ISSUER_TOML):
            print(f"--- Deployments: {cfg.name} ---")
            proc = common.run_wrangler(
                ["deployments", "list", "--config", cfg.name],
                wrapper=wrapper,
                dry_run=args.dry_run,
            )
            if proc.stdout.strip():
                print(proc.stdout.strip())

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
