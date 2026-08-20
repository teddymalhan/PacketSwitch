#!/usr/bin/env bash
#
# Five-minute scripted WireLab demonstration.
#
# The GUI is driven by a human, so this script is the script for that human: it
# starts the frontend, then walks the seven workspaces in an order that tells
# one story - a lab that is healthy, is attacked, defends itself, and is then
# measured - keeping each chapter inside its budget so the whole run fits in
# five minutes. It ends by exporting the same report headless, so the demo
# leaves an artifact behind rather than only a memory.

set -Eeuo pipefail

readonly GUI_DEFAULT="build/gui/bin/WireLab.app/Contents/MacOS/wirelab-desktop"
readonly GUI_LINUX_DEFAULT="build/gui/cargo/debug/wirelab-desktop"

usage() {
  cat <<'EOF'
Usage: scripts/demo_gui.sh [--gui <path>] [--no-wait] [--skip-report]

  --gui <path>    wirelab-desktop binary (default: the build/gui Debug build, or $WIRELAB_GUI)
  --no-wait       Print the whole script at once instead of pausing per chapter
  --skip-report   Do not run the headless report at the end

Build the frontend first:
  cmake -S . -B build/gui -DCMAKE_BUILD_TYPE=Debug -DPROJECT_BUILD_DESKTOP=ON \
        -DWIRELAB_ENABLE_METAL=ON
  cmake --build build/gui --target wirelab-desktop
EOF
}

gui="${WIRELAB_GUI:-}"
wait_between=1
run_report=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --gui) gui="$2"; shift 2 ;;
    --no-wait) wait_between=0; shift ;;
    --skip-report) run_report=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "$gui" ]]; then
  if [[ -x "$GUI_DEFAULT" ]]; then gui="$GUI_DEFAULT"; else gui="$GUI_LINUX_DEFAULT"; fi
fi

if [[ ! -x "$gui" ]]; then
  echo "Error: no wirelab-desktop at '$gui'." >&2
  usage >&2
  exit 1
fi

chapter() {
  local minutes="$1" title="$2"
  shift 2
  echo
  echo "== [$minutes] $title"
  local line
  for line in "$@"; do
    echo "   $line"
  done
  if [[ "$wait_between" -eq 1 ]]; then
    echo -n "   -- press ENTER for the next chapter --"
    read -r _ || true
  fi
}

echo "Starting $gui"
"$gui" &
readonly GUI_PID=$!
# The demo owns the window it opened: closing the terminal should not leave a
# frontend running against a lab that no longer exists.
trap 'kill "$GUI_PID" 2>/dev/null || true' EXIT
sleep 2

chapter "0:00-0:40" "Dashboard - what a healthy lab looks like" \
  "Point at throughput, latency and loss holding steady on the chart." \
  "Point at the port list: every port is forwarding, no fault is active." \
  "Say what the switch is: a real Layer-2 dataplane, not a simulation of one."

chapter "0:40-1:20" "Topology - the lab under test" \
  "Open Topology and load scenarios/security-lab.yaml." \
  "Walk the hosts and links; name the port a suspicious sender will land on." \
  "Say that ports are bound to senders in first-seen order, because a UDP" \
  "dataplane offers no stable identity."

chapter "1:20-2:10" "Traffic - deterministic, repeatable load" \
  "Start mixed-traffic with a fixed seed; watch the counters move." \
  "Say that the same seed reproduces the same frames byte for byte, which is" \
  "what makes any number in this demo checkable." \
  "Switch the scenario to broadcast-storm and let it run."

chapter "2:10-3:00" "Packets & Security - the storm is seen" \
  "Show the anomaly appearing: broadcast_storm, with its ingress port." \
  "Show top talkers and the flow table filling with the storm's sources." \
  "Say the detector counts inside a window, so a burst is not a storm."

chapter "3:00-3:40" "Policies and Faults - the lab defends itself" \
  "Show the quarantine-broadcast-storm rule that matched." \
  "Open Faults: the offending port is quarantined under a lease, not banned." \
  "Wait for the lease to lapse and the port to come back on its own." \
  "Say the enforcement is reversible by construction: it is a fault the" \
  "controller can clear, over the same contract an operator uses."

chapter "3:40-4:40" "Reports - measure it, do not claim it" \
  "Open Reports, run udp-flood, 200000 packets, batch 128, frame 128, seed 42." \
  "Read the comparison table across the backends this machine has." \
  "Point at transfer and kernel timing: a GPU number that hides the copy is" \
  "not a result. In a Debug build the CPU usually wins, and saying so is the" \
  "point of the table." \
  "Export the report and open the JSON next to the CSV."

chapter "4:40-5:00" "Close" \
  "Every number shown came from a run that can be repeated by seed, and every" \
  "policy shown was enforced against real forwarded frames."

if [[ "$run_report" -eq 1 ]]; then
  echo
  echo "== Headless twin of the Reports chapter"
  ./scripts/bench_report.sh --scenario udp-flood --packets 200000 --batch-size 128 \
    --frame-size 128 --seed 42
fi

echo
echo "Demo complete. Closing the frontend."
