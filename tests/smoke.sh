#!/bin/sh
# Non-destructive CLI smoke tests. Does not require root privileges.
set -eu

BIN=${1:-./memreduct}

version="$("$BIN" --version)"
printf '%s\n' "$version" | grep -q '1.3.0'

status="$("$BIN" status)"
printf '%s\n' "$status" | grep -q 'Physical memory'

stats="$("$BIN" stats)"
printf '%s\n' "$stats" | grep -q 'Memory breakdown'
printf '%s\n' "$stats" | grep -q 'Dirty / writeback'
printf '%s\n' "$stats" | grep -q 'Kernel VM counters'

MEMREDUCT_BIN="$BIN" python3 -c '
import json, os, subprocess
binary = os.environ["MEMREDUCT_BIN"]
required = {
    "total_kb", "used_kb", "available_kb", "cache_kb", "percent",
    "active_kb", "inactive_kb", "active_file_kb", "inactive_file_kb",
    "dirty_kb", "writeback_kb", "anon_pages_kb", "slab_kb",
    "slab_unreclaimable_kb", "kernel_stack_kb", "page_tables_kb",
    "commit_limit_kb", "committed_as_kb", "hugepages_total",
    "hugepages_free", "hugepages_size_kb",
    "psi_memory_available", "psi_memory_some_avg10",
    "psi_memory_full_available", "psi_memory_full_avg10",
    "psi_cpu_available", "psi_cpu_some_avg10",
    "psi_io_available", "psi_io_some_avg10",
    "zram_available", "zram_devices", "zram_original_bytes",
    "zram_compressed_bytes", "zram_memory_used_bytes",
    "vmstat_available", "page_faults", "major_page_faults",
    "pswpin_pages", "pswpout_pages", "pgscan_kswapd",
    "pgsteal_kswapd", "pgscan_direct", "pgsteal_direct",
    "compact_stall", "compact_success", "compact_fail", "oom_kill",
    "cgroup_v2_available", "cgroup_memory_current_bytes",
    "cgroup_memory_limit_bytes", "cgroup_memory_limit_unlimited",
    "cgroup_memory_swap_current_bytes", "cgroup_event_high",
    "cgroup_event_max", "cgroup_event_oom", "cgroup_event_oom_kill"
}
for command in ("status", "stats"):
    payload = json.loads(subprocess.check_output(
        [binary, command, "--json"], text=True))
    missing = required.difference(payload)
    if missing:
        raise SystemExit(f"{command} JSON missing fields: {sorted(missing)}")
    for key in ("psi_memory_available", "zram_available",
                "vmstat_available", "cgroup_v2_available"):
        if not isinstance(payload[key], bool):
            raise SystemExit(f"{command} JSON field {key} must be boolean")
top = json.loads(subprocess.check_output(
    [binary, "top", "--json"], text=True))
if not isinstance(top, list):
    raise SystemExit("top JSON must be an array")
if top and not {"pid", "name", "rss_kb"}.issubset(top[0]):
    raise SystemExit("top JSON entries are missing expected fields")
'

help="$("$BIN" --help)"
printf '%s\n' "$help" | grep -q 'stats'
printf '%s\n' "$help" | grep -q -- '--modified-cache'
printf '%s\n' "$help" | grep -q -- '--pressure-type'

"$BIN" status --pressure-type full >/dev/null
if "$BIN" status --pressure-type invalid >/dev/null 2>&1; then
    echo "invalid PSI pressure types must be rejected" >&2
    exit 1
fi

if "$BIN" command-that-does-not-exist >/dev/null 2>&1; then
    echo "unknown commands must return a non-zero status" >&2
    exit 1
fi

echo "Mem Reduct smoke tests passed."
