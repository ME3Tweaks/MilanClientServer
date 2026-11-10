#!/usr/bin/env bash
# All-time MilanServer statistics. The server's counts start over whenever it starts, so each
# run's latest /v1/stats is kept in a file of its own, and the totals add those files up.
#
#   milan-stats.sh save    Saves the running server's statistics. Cron runs it every minute,
#                          and deploy.sh runs it just before replacing the container.
#   milan-stats.sh total   Prints the all-time totals as JSON.
#
# deploy.sh copies this script into the server folder. Files go in stats/ beside it, named
# <server start time>_<container>.json, so a restart inside the same container (a crash or a
# reboot) gets a new file too. The token is read from milanserver.env (Milan__Stats__Token).
set -euo pipefail

folder=$(cd "$(dirname "$0")" && pwd)
stats_dir=$folder/stats
env_file=$folder/milanserver.env
container=milanserver

save() {
    local token port id body started file
    token=$(sed -n 's/^Milan__Stats__Token=//p' "$env_file" 2>/dev/null | tail -1)
    if [[ -z $token ]]; then
        echo "no Milan__Stats__Token in $env_file" >&2
        exit 1
    fi
    port=$(sed -n 's/^ASPNETCORE_HTTP_PORTS=//p' "$env_file" | tail -1)
    port=${port:-5080}

    # No container (between deploy steps): nothing to save.
    id=$(docker inspect --format '{{.Id}}' "$container" 2>/dev/null) || exit 0
    # The token needs HTTPS. Loopback is a trusted proxy, so this header vouches for a request
    # that never leaves the server.
    if ! body=$(curl -fsS --max-time 10 -H "Authorization: Bearer $token" -H "X-Forwarded-Proto: https" \
        "http://127.0.0.1:$port/v1/stats"); then
        echo "could not read http://127.0.0.1:$port/v1/stats" >&2
        exit 1
    fi
    started=$(grep -o '"startedAt":"[^"]*"' <<<"$body" | head -1 | cut -d'"' -f4)
    if [[ -z $started ]]; then
        echo "the statistics have no details.startedAt; is the token right?" >&2
        exit 1
    fi

    mkdir -p "$stats_dir"
    file=$stats_dir/$(tr -c 'A-Za-z0-9.\n' '-' <<<"$started")_${id:0:12}.json
    printf '%s\n' "$body" > "$file.part"
    mv "$file.part" "$file"
}

total() {
    python3 - "$stats_dir" <<'PY'
import json, pathlib, sys

runs = [json.loads(path.read_text())["details"] for path in sorted(pathlib.Path(sys.argv[1]).glob("*.json"))]
if not runs:
    sys.exit(f"no saved statistics in {sys.argv[1]}")

totals = {}
for run in runs:
    for name, value in run["totals"].items():
        totals[name] = totals.get(name, 0) + value

print(json.dumps({
    "runs": len(runs),
    "since": runs[0]["startedAt"],
    "uptimeSeconds": sum(run["uptimeSeconds"] for run in runs),
    "peakPlayers": max(run["peakPlayers"] for run in runs),
    "peakLobbies": max(run["peakLobbies"] for run in runs),
    "totals": totals,
}, indent=2))
PY
}

case ${1:-} in
    save) save ;;
    total) total ;;
    *)
        echo "usage: $0 save|total" >&2
        exit 2
        ;;
esac
