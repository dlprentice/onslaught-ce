#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Two-step Cloudflare Workers deployment orchestrator for Onslaught CE Online Lobby.

Deployment order breaks the mutual Service Binding cycle safely:
  1. Deploy disabled issuer (`issuer.wrangler.toml --env bootstrap`, no bindings).
  2. Deploy lobby Worker + SQLite Durable Objects (`wrangler.toml`).
  3. Upload secrets via stdin pipes (`CE_SESSION_SECRET`, `CE_ISSUER_PERMIT_KEY`,
     `TURN_KEY_ID`, `TURN_API_TOKEN`) — never on argv or in logs.
  4. Deploy full issuer (`issuer.wrangler.toml`) binding `PERMIT_AUTHORITY`.
  5. Keep `CE_ENABLED=false` unless `--enable` is explicitly passed.
"""
from __future__ import annotations

import argparse
import getpass
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402


def load_secrets(args: argparse.Namespace) -> dict[str, str]:
    values: dict[str, str] = {}
    secrets_dir = Path(args.secrets_dir) if args.secrets_dir else None

    def read_or_prompt(name: str, *, allow_generate: bool = False) -> str:
        if secrets_dir is not None:
            candidate = secrets_dir / name
            if candidate.exists():
                val = common.read_private_secret_file(candidate)
                common.validate_secret_value(name, val)
                return val
            if allow_generate and args.generate_keys:
                val = common.generate_hex_secret()
                common.write_private_secret_file(secrets_dir, name, val)
                return val
            raise FileNotFoundError(f"Missing secret file {name} in --secrets-dir.")
        if allow_generate and args.generate_keys:
            return common.generate_hex_secret()
        if args.dry_run:
            # Syntactically valid placeholder used strictly in dry-run (never sent anywhere).
            return ("a" * 64) if name.endswith(("SECRET", "KEY")) else "dryrun-placeholder"
        val = getpass.getpass(f"Enter {name}: ").strip()
        common.validate_secret_value(name, val)
        return val

    values["CE_SESSION_SECRET"] = read_or_prompt("CE_SESSION_SECRET", allow_generate=True)
    values["CE_ISSUER_PERMIT_KEY"] = read_or_prompt("CE_ISSUER_PERMIT_KEY", allow_generate=True)
    if values["CE_SESSION_SECRET"] == values["CE_ISSUER_PERMIT_KEY"] and not args.dry_run:
        raise ValueError("CE_SESSION_SECRET and CE_ISSUER_PERMIT_KEY must be distinct.")
    values["TURN_KEY_ID"] = read_or_prompt("TURN_KEY_ID", allow_generate=False)
    values["TURN_API_TOKEN"] = read_or_prompt("TURN_API_TOKEN", allow_generate=False)
    return values


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Deploy Onslaught CE Online Lobby Workers.")
    parser.add_argument("--mode", choices=("bootstrap", "full"), default="full",
                        help="bootstrap: deploy disabled workers only; full: bootstrap + secrets + bindings.")
    parser.add_argument("--secrets-dir", default=None,
                        help="Private 0700 directory holding 0600 files named after each secret.")
    parser.add_argument("--generate-keys", action="store_true",
                        help="Generate fresh random 32-byte hex keys for CE_SESSION_SECRET & CE_ISSUER_PERMIT_KEY.")
    parser.add_argument("--skip-secrets", action="store_true",
                        help="Skip secret upload (for code-only redeploys after secrets are already set).")
    parser.add_argument("--enable", action="store_true",
                        help="Set CE_ENABLED=true on deploy (default stays false/fail-closed).")
    parser.add_argument("--wrangler-wrapper", default=None,
                        help="Explicit path to local-data/cloudflare-operator/wrangler.py.")
    parser.add_argument("--dry-run", action="store_true",
                        help="Validate templates and print planned Wrangler steps without executing.")
    args = parser.parse_args(argv)

    common.validate_wrangler_templates()
    wrapper = None if args.dry_run else common.find_wrangler_wrapper(args.wrangler_wrapper)
    enabled_flag = "true" if args.enable else "false"

    # Step 1: Bootstrap disabled issuer so lobby Worker can bind TURN_ISSUER.
    print("Step 1/4: Deploying issuer bootstrap (no service bindings, disabled)...")
    common.run_wrangler(
        ["deploy", "--config", common.ISSUER_TOML.name, "--env", "bootstrap"],
        wrapper=wrapper,
        dry_run=args.dry_run,
    )

    # Step 2: Deploy lobby Worker + SQLite Durable Objects.
    print(f"Step 2/4: Deploying lobby Worker (CE_ENABLED={enabled_flag})...")
    common.run_wrangler(
        ["deploy", "--config", common.LOBBY_TOML.name, "--var", f"CE_ENABLED:{enabled_flag}"],
        wrapper=wrapper,
        dry_run=args.dry_run,
    )

    if args.mode == "bootstrap":
        print("Bootstrap deployment complete (both Workers remain disabled).")
        return 0

    # Step 3: Pipe secrets strictly through stdin.
    if not args.skip_secrets:
        print("Step 3/4: Uploading secrets via stdin pipe...")
        secret_map = load_secrets(args)
        common.put_secret(common.LOBBY_TOML, "CE_SESSION_SECRET", secret_map["CE_SESSION_SECRET"],
                          wrapper=wrapper, dry_run=args.dry_run)
        common.put_secret(common.LOBBY_TOML, "CE_ISSUER_PERMIT_KEY", secret_map["CE_ISSUER_PERMIT_KEY"],
                          wrapper=wrapper, dry_run=args.dry_run)
        common.put_secret(common.ISSUER_TOML, "CE_ISSUER_PERMIT_KEY", secret_map["CE_ISSUER_PERMIT_KEY"],
                          wrapper=wrapper, dry_run=args.dry_run)
        common.put_secret(common.ISSUER_TOML, "TURN_KEY_ID", secret_map["TURN_KEY_ID"],
                          wrapper=wrapper, dry_run=args.dry_run)
        common.put_secret(common.ISSUER_TOML, "TURN_API_TOKEN", secret_map["TURN_API_TOKEN"],
                          wrapper=wrapper, dry_run=args.dry_run)
    else:
        print("Step 3/4: Skipping secret upload (--skip-secrets).")

    # Step 4: Deploy full issuer with PERMIT_AUTHORITY service binding.
    print(f"Step 4/4: Deploying full private TURN issuer (CE_ENABLED={enabled_flag})...")
    common.run_wrangler(
        ["deploy", "--config", common.ISSUER_TOML.name, "--var", f"CE_ENABLED:{enabled_flag}"],
        wrapper=wrapper,
        dry_run=args.dry_run,
    )

    print("Deployment workflow complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
