#!/usr/bin/env bash

set -Eeuo pipefail

if ! command -v otty >/dev/null 2>&1; then
  printf 'Error: otty is not installed or not on PATH.\n' >&2
  printf 'Run make demo-start, then open windows using commands from the README.\n' >&2
  exit 1
fi

readonly ROOT=$(cd "$(dirname "$0")/.." && pwd)
readonly OTTY=$(command -v otty)

open_window() {
  local title=$1
  local command=$2
  local script
  script="clear; printf '=== %s ===\\n\\n' '$title'; $command"
  "$OTTY" open "$ROOT" --title "$title" --command "$script" >/dev/null
}

./tests/demo_stack.sh start

open_window "WireLab — VSwitch" \
  "docker logs --follow --tail 20 wirelab-demo-switch 2>&1 | awk -f ./tests/highlight_logs.awk"
open_window "WireLab — VPort 1 (10.1.1.101)" \
  "docker logs --follow --tail 12 wirelab-demo-port1 2>&1 | awk -f ./tests/highlight_logs.awk"
open_window "WireLab — VPort 2 (10.1.1.102)" \
  "docker logs --follow --tail 12 wirelab-demo-port2 2>&1 | awk -f ./tests/highlight_logs.awk"
open_window "WireLab — Live Traffic" \
  "./tests/demo_stack.sh traffic 2>&1 | awk -f ./tests/highlight_logs.awk"

printf '\nOpened four Otty windows:\n'
printf '  1. VSwitch MAC learning and forwarding\n'
printf '  2. VPort 1 frame flow\n'
printf '  3. VPort 2 frame flow\n'
printf '  4. Continuous bidirectional ping traffic\n\n'
printf 'When recording is finished, run: make demo-stop\n'
