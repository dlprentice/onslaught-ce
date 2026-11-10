#!/usr/bin/env bash
# Onslaught CE network test: host (player 1). Usage: ./host.sh [password]
# Uses this folder's own user folder (./user), never your normal Onslaught CE careers, and never checks for updates.
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
PORT=${PORT:-27652}
pass=${1:-}
[ -n "$pass" ] || read -r -p "Shared password (letters and digits; the other player types the same): " pass
[ -n "$pass" ] || { echo "A password is needed."; exit 1; }
secret=$(printf %s "$pass" | sha256sum | cut -c1-64)
mkdir -p "$HERE/user"
echo "Hosting on UDP port $PORT. Tell the other player one of these addresses:"
ip -4 -o addr show scope global 2>/dev/null | awk '{sub(/\/.*/, "", $4); print "  " $4 "   (this network)"}'
echo "  (over the internet: your public address, with UDP port $PORT forwarded to this computer)"
echo "The other player must start join within 30 seconds of the level loading here."
echo "Log: $HERE/user/onslaught-ce.log"
export BEA_USER_DIR="$HERE/user" BEA_NET_HOST="0.0.0.0:$PORT" BEA_NET_SECRET="$secret" BEA_NO_UPDATE_CHECK=1
exec "$HERE/onslaught-ce" --no-update-check -level 852 -skipfmv
