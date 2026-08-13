#!/usr/bin/env bash

set -Eeuo pipefail

readonly IMAGE="packetswitch-demo"
readonly NETWORK="packetswitch-demo-net"
readonly SWITCH="packetswitch-demo-switch"
readonly PORT1="packetswitch-demo-port1"
readonly PORT2="packetswitch-demo-port2"
readonly SWITCH_PORT="8080"
readonly SWITCH_IP="172.30.0.2"
readonly PORT1_IP="10.1.1.101"
readonly PORT2_IP="10.1.1.102"

usage() {
  cat <<'EOF'
Usage: tests/demo_stack.sh <command>

Commands:
  start       Build and start VSwitch plus two configured VPorts
  stop        Remove the demo containers and network
  status      Show the three running demo containers
  ping-1      Send traffic from VPort 1 to VPort 2
  ping-2      Send traffic from VPort 2 to VPort 1
  traffic     Continuously send visible traffic in both directions
EOF
}

require_docker() {
  command -v docker >/dev/null 2>&1 || {
    printf 'Error: Docker is not installed or not on PATH.\n' >&2
    exit 1
  }
  docker info >/dev/null 2>&1 || {
    printf 'Error: Docker is installed, but its daemon is not ready.\n' >&2
    exit 1
  }
}

wait_for_log() {
  local container=$1
  local message=$2
  local attempts=40
  while (( attempts-- > 0 )); do
    if docker logs "$container" 2>&1 | grep -Fq "$message"; then
      return 0
    fi
    sleep 0.25
  done
  printf 'Error: %s did not become ready. Logs follow:\n' "$container" >&2
  docker logs "$container" >&2 || true
  return 1
}

remove_stack() {
  docker rm -f "$PORT1" "$PORT2" "$SWITCH" >/dev/null 2>&1 || true
  docker network rm "$NETWORK" >/dev/null 2>&1 || true
}

start_port() {
  local name=$1
  local tap=$2
  docker run -d --rm \
    --name "$name" \
    --network "$NETWORK" \
    --cap-add NET_ADMIN \
    --device /dev/net/tun \
    "$IMAGE" \
    stdbuf -oL -eL /app/build/vport "$SWITCH_IP" "$SWITCH_PORT" "$tap" >/dev/null
  wait_for_log "$name" 'VPort is running!'
}

start_stack() {
  local status=0
  printf '[setup] Building PacketSwitch demo image...\n'
  docker build --quiet --tag "$IMAGE" --file tests/Dockerfile . >/dev/null

  printf '[setup] Creating isolated demo network...\n'
  remove_stack
  docker network create --subnet 172.30.0.0/24 "$NETWORK" >/dev/null

  printf '[setup] Starting VSwitch...\n'
  docker run -d --rm \
    --name "$SWITCH" \
    --network "$NETWORK" \
    --ip "$SWITCH_IP" \
    "$IMAGE" \
    stdbuf -oL -eL /app/build/vswitch "$SWITCH_PORT" >/dev/null
  wait_for_log "$SWITCH" 'Ready to receive frames from VPorts' || status=$?

  if (( status == 0 )); then
    printf '[setup] Starting VPort 1 and VPort 2...\n'
    start_port "$PORT1" tap0 || status=$?
    start_port "$PORT2" tap1 || status=$?
  fi

  if (( status == 0 )); then
    docker exec "$PORT1" ip address add "$PORT1_IP/24" dev tap0
    docker exec "$PORT1" ip link set tap0 up
    docker exec "$PORT2" ip address add "$PORT2_IP/24" dev tap1
    docker exec "$PORT2" ip link set tap1 up
    printf '[setup] Ready: VPort 1 (%s) <-> VSwitch <-> VPort 2 (%s)\n' "$PORT1_IP" "$PORT2_IP"
    printf '[setup] The stack stays up until: make demo-stop\n'
    return
  fi

  remove_stack
  return "$status"
}

require_running() {
  local container
  for container in "$SWITCH" "$PORT1" "$PORT2"; do
    if [[ $(docker inspect -f '{{.State.Running}}' "$container" 2>/dev/null || true) != true ]]; then
      printf 'Error: demo stack is not running. Run: make demo-start\n' >&2
      exit 1
    fi
  done
}

send_ping() {
  local source=$1
  local destination=$2
  require_running
  docker exec "$source" ping -c 3 -W 2 "$destination"
}

stream_traffic() {
  require_running
  printf 'Continuous traffic: %s <-> %s (Ctrl-C stops traffic only)\n\n' "$PORT1_IP" "$PORT2_IP"
  while true; do
    printf '\n[VPort 1 -> VPort 2] %s -> %s\n' "$PORT1_IP" "$PORT2_IP"
    docker exec "$PORT1" ping -c 1 -W 2 "$PORT2_IP" || true
    sleep 1
    printf '\n[VPort 2 -> VPort 1] %s -> %s\n' "$PORT2_IP" "$PORT1_IP"
    docker exec "$PORT2" ping -c 1 -W 2 "$PORT1_IP" || true
    sleep 1
  done
}

require_docker
case "${1:-}" in
  start) start_stack ;;
  stop)
    remove_stack
    printf 'PacketSwitch demo stack stopped.\n'
    ;;
  status)
    docker ps --filter "name=packetswitch-demo-" --format 'table {{.Names}}\t{{.Status}}'
    ;;
  ping-1) send_ping "$PORT1" "$PORT2_IP" ;;
  ping-2) send_ping "$PORT2" "$PORT1_IP" ;;
  traffic) stream_traffic ;;
  *) usage; exit 2 ;;
esac
