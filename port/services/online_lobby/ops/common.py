#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Shared helpers for Onslaught CE online lobby Cloudflare operator scripts.

Secrets are always piped through stdin and redacted from any captured command output.
"""
from __future__ import annotations

import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import secrets
import stat
import subprocess
import sys
import tomllib
from urllib.parse import urlparse


OPS_DIR = Path(__file__).resolve().parent
SERVICE_DIR = OPS_DIR.parent
REPO_ROOT = SERVICE_DIR.parents[2]
LOBBY_TOML = SERVICE_DIR / "wrangler.toml"
ISSUER_TOML = SERVICE_DIR / "issuer.wrangler.toml"

STRICT_HEADERS = {
    "content-type": "application/json",
    "cache-control": "no-store",
    "pragma": "no-cache",
    "x-content-type-options": "nosniff",
    "content-encoding": "identity",
}

SECRET_PATTERNS = {
    "CE_SESSION_SECRET": re.compile(r"^[0-9a-f]{64}$"),
    "CE_ISSUER_PERMIT_KEY": re.compile(r"^[0-9a-f]{64}$"),
    "TURN_KEY_ID": re.compile(r"^[A-Za-z0-9_-]{1,128}$"),
    "TURN_API_TOKEN": re.compile(r"^[!-~]{1,513}$"),
}

CROCKFORD_LOWER = "0123456789abcdefghjkmnpqrstvwxyz"


def find_git_common_parent(start: Path) -> Path | None:
    """Resolve main checkout root when running inside a secondary git worktree."""
    try:
        proc = subprocess.run(
            ["git", "-C", str(start), "rev-parse", "--git-common-dir"],
            capture_output=True,
            text=True,
            check=False,
            timeout=5,
        )
        if proc.returncode == 0 and proc.stdout.strip():
            common = Path(proc.stdout.strip())
            if not common.is_absolute():
                common = (start / common).resolve()
            return common.parent
    except OSError:
        return None
    return None


def find_wrangler_wrapper(override: str | None = None) -> Path:
    """Locate the ignored local Wrangler wrapper without executing it."""
    candidates: list[Path] = []
    if override:
        candidates.append(Path(override))
    env_override = os.environ.get("CE_WRANGLER_WRAPPER")
    if env_override:
        candidates.append(Path(env_override))
    rel = Path("local-data/cloudflare-operator/wrangler.py")
    candidates.append(REPO_ROOT / rel)
    common_parent = find_git_common_parent(REPO_ROOT)
    if common_parent and common_parent != REPO_ROOT:
        candidates.append(common_parent / rel)
    for path in candidates:
        if path.is_file():
            return path.resolve()
    raise FileNotFoundError(
        "Wrangler wrapper not found at local-data/cloudflare-operator/wrangler.py "
        "(or set CE_WRANGLER_WRAPPER / --wrangler-wrapper)."
    )


def redact(text: str, secret_values: list[str] | tuple[str, ...]) -> str:
    result = text
    for val in secret_values:
        if val:
            result = result.replace(val, "[REDACTED]")
    return result


def validate_secret_value(name: str, value: str) -> None:
    pattern = SECRET_PATTERNS.get(name)
    if pattern is None:
        raise ValueError(f"Unsupported secret name: {name}")
    if not isinstance(value, str) or not pattern.match(value):
        raise ValueError(f"Secret {name} failed format validation.")


def read_private_secret_file(path: Path) -> str:
    """Read a secret from an owner-only regular file (mode 0600 on POSIX)."""
    st = os.lstat(path)
    if stat.S_ISLNK(st.st_mode) or not stat.S_ISREG(st.st_mode):
        raise ValueError(f"Secret file must be a regular non-symlink file: {path.name}")
    if os.name == "posix" and (st.st_mode & 0o077) != 0:
        raise PermissionError(f"Secret file must have owner-only permissions (0600): {path.name}")
    return path.read_text(encoding="utf-8").strip()


def write_private_secret_file(directory: Path, filename: str, value: str) -> Path:
    """Write a secret file with owner-only directory (0700) and file (0600) permissions."""
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    if os.name == "posix":
        os.chmod(directory, 0o700)
    target = directory / filename
    fd = os.open(str(target), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        os.write(fd, (value + "\n").encode("utf-8"))
    finally:
        os.close(fd)
    return target


def generate_hex_secret() -> str:
    return secrets.token_hex(32)


def validate_wrangler_templates() -> tuple[dict, dict]:
    """Ensure checked-in Wrangler templates contain no account IDs, emails, or secrets."""
    lobby = tomllib.loads(LOBBY_TOML.read_text(encoding="utf-8"))
    issuer = tomllib.loads(ISSUER_TOML.read_text(encoding="utf-8"))
    for label, cfg in (("wrangler.toml", lobby), ("issuer.wrangler.toml", issuer)):
        if "account_id" in cfg:
            raise ValueError(f"{label} must not contain account_id.")
        if cfg.get("preview_urls") is not False:
            raise ValueError(f"{label} must set preview_urls = false.")
        if cfg.get("observability", {}).get("enabled") is not False:
            raise ValueError(f"{label} must disable observability.")
        vars_map = cfg.get("vars", {})
        if vars_map.get("CE_ENABLED") != "false":
            raise ValueError(f"{label} must default CE_ENABLED = 'false'.")
        for secret_name in SECRET_PATTERNS:
            if secret_name in vars_map:
                raise ValueError(f"{label} must not embed secret {secret_name} in [vars].")
    if lobby.get("workers_dev") is not True:
        raise ValueError("wrangler.toml must set workers_dev = true.")
    if issuer.get("workers_dev") is not False:
        raise ValueError("issuer.wrangler.toml must set workers_dev = false.")
    if issuer.get("env", {}).get("bootstrap", {}).get("workers_dev") is not False:
        raise ValueError("issuer.wrangler.toml [env.bootstrap] must set workers_dev = false.")
    if issuer.get("env", {}).get("bootstrap", {}).get("services") != []:
        raise ValueError("issuer.wrangler.toml [env.bootstrap] must clear services = [].")
    return lobby, issuer


def run_wrangler(
    args: list[str],
    *,
    wrapper: Path | None = None,
    stdin_secret: str | None = None,
    dry_run: bool = False,
) -> subprocess.CompletedProcess[str]:
    """Invoke local Wrangler wrapper safely. Secrets travel strictly via stdin, never argv."""
    if stdin_secret is not None:
        for arg in args:
            if stdin_secret in arg:
                raise ValueError("Refusing to pass secret in argv.")

    if dry_run:
        display = ["wrangler.py", *args]
        suffix = " < [stdin secret]" if stdin_secret is not None else ""
        print(f"[dry-run] {' '.join(display)}{suffix}")
        return subprocess.CompletedProcess(display, 0, stdout="", stderr="")

    resolved = wrapper or find_wrangler_wrapper()
    cmd = [sys.executable, str(resolved), *args]
    stdin_payload = (stdin_secret + "\n") if stdin_secret is not None else None
    proc = subprocess.run(
        cmd,
        cwd=str(SERVICE_DIR),
        input=stdin_payload,
        capture_output=True,
        text=True,
        check=False,
        timeout=180,
    )
    secrets_to_scrub = [stdin_secret] if stdin_secret else []
    clean_out = redact(proc.stdout, secrets_to_scrub)
    clean_err = redact(proc.stderr, secrets_to_scrub)
    if proc.returncode != 0:
        raise RuntimeError(
            f"Wrangler command failed (exit {proc.returncode}): {' '.join(args)}\n"
            f"{clean_err.strip() or clean_out.strip()}"
        )
    return subprocess.CompletedProcess(cmd, proc.returncode, stdout=clean_out, stderr=clean_err)


def put_secret(
    config_path: Path,
    secret_name: str,
    secret_value: str,
    *,
    wrapper: Path | None = None,
    dry_run: bool = False,
) -> None:
    """Upload one secret via stdin pipe without exposing it in argv or logs."""
    validate_secret_value(secret_name, secret_value)
    args = ["secret", "put", secret_name, "--config", str(config_path.name)]
    run_wrangler(args, wrapper=wrapper, stdin_secret=secret_value, dry_run=dry_run)


def parse_service_url(raw_url: str, *, allow_loopback_http: bool = False) -> tuple[str, str, int]:
    """Validate and split service origin into (scheme, host, port)."""
    parsed = urlparse(raw_url.rstrip("/"))
    if parsed.path not in ("", "/") or parsed.query or parsed.fragment:
        raise ValueError("Service URL must be an origin without path, query, or fragment.")
    if parsed.scheme == "https":
        port = parsed.port or 443
    elif parsed.scheme == "http" and allow_loopback_http and parsed.hostname in ("127.0.0.1", "localhost"):
        port = parsed.port or 80
    else:
        raise ValueError("Service URL must use https:// (or http://127.0.0.1:<port> with --allow-loopback-http).")
    if not parsed.hostname:
        raise ValueError("Service URL is missing a hostname.")
    return parsed.scheme, parsed.hostname, port


def http_json_request(
    scheme: str,
    host: str,
    port: int,
    method: str,
    path: str,
    *,
    body: dict | None = None,
    bearer: str | None = None,
    timeout: float = 15.0,
) -> tuple[int, dict[str, str], dict]:
    """Perform an HTTPS/loopback JSON request and assert strict CeSignalHttps response headers."""
    conn_cls = http.client.HTTPSConnection if scheme == "https" else http.client.HTTPConnection
    conn = conn_cls(host, port, timeout=timeout)
    try:
        headers: dict[str, str] = {"Accept": "application/json", "Accept-Encoding": "identity"}
        if bearer is not None:
            headers["Authorization"] = f"Bearer {bearer}"
        payload = None
        if body is not None:
            headers["Content-Type"] = "application/json"
            payload = json.dumps(body, separators=(",", ":"), sort_keys=True).encode("utf-8")
        conn.request(method, path, body=payload, headers=headers)
        resp = conn.getresponse()
        raw = resp.read(65537)
        hdrs = {k.lower(): v for k, v in resp.getheaders()}
        for req_name, req_val in STRICT_HEADERS.items():
            actual = hdrs.get(req_name, "")
            if actual.lower() != req_val:
                raise RuntimeError(f"Strict header mismatch on {req_name}: expected {req_val!r}, got {actual!r}")
        if "location" in hdrs or "set-cookie" in hdrs:
            raise RuntimeError("Forbidden redirect or Set-Cookie header received.")
        if len(raw) > 65536:
            raise RuntimeError("Response body exceeded 65536 bytes.")
        return resp.status, hdrs, json.loads(raw.decode("utf-8"))
    finally:
        conn.close()


def crockford_base32(data: bytes) -> str:
    out: list[str] = []
    buf = 0
    bits = 0
    for byte in data:
        buf = (buf << 8) | byte
        bits += 8
        while bits >= 5:
            bits -= 5
            out.append(CROCKFORD_LOWER[(buf >> bits) & 31])
    if bits > 0:
        out.append(CROCKFORD_LOWER[(buf << (5 - bits)) & 31])
    return "".join(out)


def derive_player_identity(pubkey_bytes: bytes) -> tuple[str, str]:
    digest = hashlib.sha256(pubkey_bytes).digest()
    return crockford_base32(digest[:16]), digest[:2].hex().upper()
