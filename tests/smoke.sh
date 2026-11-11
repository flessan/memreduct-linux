#!/bin/sh
# Non-destructive CLI smoke tests. Does not require root privileges.
set -eu

BIN=${1:-./memreduct}

version="$("$BIN" --version)"
printf '%s\n' "$version" | grep -q '1.2.0'

status="$("$BIN" status)"
printf '%s\n' "$status" | grep -q 'Physical memory'

stats="$("$BIN" stats)"
printf '%s\n' "$stats" | grep -q 'Memory breakdown'
printf '%s\n' "$stats" | grep -q 'Dirty / writeback'

python3 -c '
import json, subprocess, sys
binary = sys.argv[1]
required = {
    "total_kb", "used_kb", "available_kb", "cache_kb", "percent",
    "active_kb", "inactive_kb", "active_file_kb", "inactive_file_kb",
    "dirty_kb", "writeback_kb", "anon_pages_kb", "slab_kb",
    "slab_unreclaimable_kb", "kernel_stack_kb", "page_tables_kb",
    "commit_limit_kb", "committed_as_kb", "hugepages_total",
    "hugepages_free", "hugepages_size_kb"
}
for command in ("status", "stats"):
    payload = json.loads(subprocess.check_output([binary, command, "--json"], text=True))
    missing = required.difference(payload)
    if missing:
        raise SystemExit(f"{command} JSON missing fields: {sorted(missing)}")
top = json.loads(subprocess.check_output([binary, "top", "--json"], text=True))
if not isinstance(top, list):
    raise SystemExit("top JSON must be an array")
if top and not {"pid", "name", "rss_kb"}.issubset(top[0]):
    raise SystemExit("top JSON entries are missing expected fields")
' "$BIN"

help="$("$BIN" --help)"
printf '%s\n' "$help" | grep -q 'stats'
printf '%s\n' "$help" | grep -q -- '--modified-cache'
printf '%s\n' "$help" | grep -q -- '--pressure'

if "$BIN" command-that-does-not-exist >/dev/null 2>&1; then
    echo "unknown commands must return a non-zero status" >&2
    exit 1
fi

echo "Mem Reduct smoke tests passed."
