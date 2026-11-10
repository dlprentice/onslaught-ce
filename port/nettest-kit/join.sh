#!/usr/bin/env bash
# Onslaught CE network test: join (player 2). Usage: ./join.sh HOST-ADDRESS [password]
# Uses this folder's own user folder (./user), never your normal Onslaught CE careers, and never checks for updates.
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
PORT=${PORT:-27652}
host=${1:-}
[ -n "$host" ] || read -r -p "Host's address (for example 192.168.1.20): " host
pass=${2:-}
[ -n "$pass" ] || read -r -p "Shared password (the same as the host's): " pass
[ -n "$pass" ] || { echo "A password is needed."; exit 1; }
secret=$(printf %s "$pass" | sha256sum | cut -c1-64)
mkdir -p "$HERE/user"
echo "Joining $host on UDP port $PORT. Log: $HERE/user/onslaught-ce.log"
export BEA_USER_DIR="$HERE/user" BEA_NET_JOIN="$host:$PORT" BEA_NET_SECRET="$secret" BEA_NO_UPDATE_CHECK=1
exec "$HERE/onslaught-ce" --no-update-check -level 852 -skipfmv
