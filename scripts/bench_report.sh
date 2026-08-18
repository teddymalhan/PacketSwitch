#!/usr/bin/env bash
#
# Reproducible headless benchmark report.
#
# Runs the same measurement the GUI Reports workspace runs, from the command
# line, across every analyzer backend and traffic generator this build actually
# has, and writes the report in the schema the GUI exports - so a report mailed
# from CI and a report exported from the GUI can be diffed column for column.

set -Eeuo pipefail

readonly DEFAULT_BENCH="build/dev/bin/Debug/wirelab_bench"

usage() {
  cat <<'EOF'
Usage: scripts/bench_report.sh [options]

Options:
  --out <base>        Report path without extension (default: reports/wirelab-report-<utc>)
  --bench <path>      wirelab_bench binary (default: build/dev/bin/Debug/wirelab_bench,
                      or $WIRELAB_BENCH)
  --scenario <name>   known-unicast|broadcast|unknown-unicast|mixed-traffic|
                      udp-flood|port-scan|broadcast-storm (default: mixed-traffic)
  --packets <count>   Packets per run (default: 200000)
  --batch-size <n>    Packets analysed per batch (default: 256)
  --frame-size <n>    Frame bytes (default: 128)
  --seed <value>      Generator seed (default: 42)
  -h, --help          This text

Every run is pinned to the same scenario, seed, frame size and batch size, so
two reports differ only where the machine or the build differs. The backend and
generator matrix is discovered from the binary rather than assumed: a backend
this build does not have is reported as absent, not silently skipped.

Writes <base>.json and <base>.csv.
EOF
}

scenario="mixed-traffic"
packets=200000
batch_size=256
frame_size=128
seed=42
bench="${WIRELAB_BENCH:-$DEFAULT_BENCH}"
out=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) out="$2"; shift 2 ;;
    --bench) bench="$2"; shift 2 ;;
    --scenario) scenario="$2"; shift 2 ;;
    --packets) packets="$2"; shift 2 ;;
    --batch-size) batch_size="$2"; shift 2 ;;
    --frame-size) frame_size="$2"; shift 2 ;;
    --seed) seed="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ ! -x "$bench" ]]; then
  echo "Error: no wirelab_bench at '$bench'." >&2
  echo "Build it with: cmake --build build/dev --target wirelab_bench" >&2
  exit 1
fi

if [[ -z "$out" ]]; then
  out="reports/wirelab-report-$(date -u +%Y%m%dT%H%M%SZ)"
fi
mkdir -p "$(dirname "$out")"

# A backend or generator counts as present only if a real one-batch run with it
# succeeds. Asking the build what it was configured with would report what the
# operator hoped for; running it reports what the machine can do.
probe() {
  local flag="$1" name="$2"
  "$bench" "$flag" "$name" --scenario known-unicast --packets 64 --batch-size 64 \
    --frame-size 64 --seed 1 >/dev/null 2>&1
}

analyzers=()
for candidate in cpu cuda metal; do
  if probe --analyzer "$candidate"; then analyzers+=("$candidate"); fi
done
generators=()
for candidate in cpu cuda metal; do
  if probe --generator "$candidate"; then generators+=("$candidate"); fi
done

echo "Analyzers present: ${analyzers[*]}"
echo "Generators present: ${generators[*]}"
echo "Running ${#analyzers[@]}x${#generators[@]} combinations of $packets packets each..."

runs_file="$(mktemp)"
trap 'rm -f "$runs_file"' EXIT

for analyzer in "${analyzers[@]}"; do
  for generator in "${generators[@]}"; do
    echo "  analyzer=$analyzer generator=$generator"
    "$bench" --analyzer "$analyzer" --generator "$generator" --scenario "$scenario" \
      --packets "$packets" --batch-size "$batch_size" --frame-size "$frame_size" \
      --seed "$seed" >>"$runs_file"
  done
done

WIRELAB_REPORT_OUT="$out" \
WIRELAB_REPORT_RUNS="$runs_file" \
WIRELAB_REPORT_ANALYZERS="${analyzers[*]}" \
WIRELAB_REPORT_GENERATORS="${generators[*]}" \
python3 - <<'PYTHON'
import csv
import datetime
import json
import os
import platform

out = os.environ["WIRELAB_REPORT_OUT"]

runs = []
with open(os.environ["WIRELAB_REPORT_RUNS"], encoding="utf-8") as handle:
    for line in handle:
        line = line.strip()
        if line:
            runs.append(dict(field.split("=", 1) for field in line.split(" ")))

# Column names and order are the GUI export's, so the two reports are diffable.
COLUMNS = ["backend", "generator", "scenario", "packets", "elapsedNs", "packetsPerSecond",
           "goodputBitsPerSecond", "lossPercent", "latencyP50Ns", "latencyP95Ns",
           "latencyP99Ns", "hostToDeviceNs", "kernelNs", "deviceToHostNs", "speedup"]

cpu_rate = next((float(run["packets_per_second"]) for run in runs
                 if run["backend"] == "cpu" and run["generator"] == "cpu"), None)

rows = []
for run in runs:
    rate = float(run["packets_per_second"])
    rows.append({
        "backend": run["backend"],
        "generator": run["generator"],
        "scenario": run["scenario"],
        "packets": int(run["packets"]),
        "elapsedNs": int(run["elapsed_ns"]),
        "packetsPerSecond": rate,
        "goodputBitsPerSecond": float(run["goodput_bits_per_second"]),
        "lossPercent": float(run["loss_percentage"]),
        "latencyP50Ns": int(run["batch_analysis_latency_p50_ns"]),
        "latencyP95Ns": int(run["batch_analysis_latency_p95_ns"]),
        "latencyP99Ns": int(run["batch_analysis_latency_p99_ns"]),
        "hostToDeviceNs": int(run["host_to_device_ns"]),
        "kernelNs": int(run["kernel_ns"]),
        "deviceToHostNs": int(run["device_to_host_ns"]),
        # Against the all-CPU run, which is the baseline every machine has.
        "speedup": round(rate / cpu_rate, 4) if cpu_rate else 0.0,
    })

first = runs[0]
provenance = {
    "scenario": first["scenario"],
    "seed": int(first["seed"]),
    "packets": int(first["packets"]),
    "batchSize": int(first["batch_size"]),
    "frameSize": int(first["frame_size"]),
    "analyzersPresent": os.environ["WIRELAB_REPORT_ANALYZERS"].split(),
    "generatorsPresent": os.environ["WIRELAB_REPORT_GENERATORS"].split(),
    "host": platform.platform(),
    "generatedAt": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
}

with open(out + ".json", "w", encoding="utf-8") as handle:
    json.dump({"provenance": provenance, "results": rows}, handle, indent=2)
    handle.write("\n")

with open(out + ".csv", "w", encoding="utf-8", newline="") as handle:
    writer = csv.DictWriter(handle, fieldnames=COLUMNS)
    writer.writeheader()
    writer.writerows(rows)

print("Wrote {0}.json and {0}.csv".format(out))
for row in rows:
    print("  {backend:>5} analyzer / {generator:>5} generator: "
          "{packetsPerSecond:12.2f} pkt/s  speedup {speedup}".format(**row))
PYTHON
