#!/usr/bin/env bash

set -Eeuo pipefail

readonly IMAGE="wirelab-demo"
readonly NETWORK="wirelab-demo-net"
readonly SWITCH="wirelab-demo-switch"
readonly PORT1="wirelab-demo-port1"
readonly PORT2="wirelab-demo-port2"
readonly SWITCH_PORT="8080"
readonly SWITCH_IP="172.30.0.2"
readonly PORT1_IP="10.1.1.101"
readonly PORT2_IP="10.1.1.102"

cleanup() {
  local status=$?
  trap - EXIT INT TERM
  printf '\n[cleanup] Removing demo resources...\n'
  docker rm -f "$PORT1" "$PORT2" "$SWITCH" >/dev/null 2>&1 || true
  docker network rm "$NETWORK" >/dev/null 2>&1 || true
  if (( status == 0 )); then
    printf '[cleanup] Done.\n'
  else
    printf '[cleanup] Done after a failed demo (exit %d).\n' "$status" >&2
  fi
  exit "$status"
}
trap cleanup EXIT INT TERM

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
  local attempts=20
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

printf '=============================================\n'
printf ' WireLab: live Layer-2 switching demo\n'
printf '=============================================\n\n'

require_docker

printf '[1/5] Building a reproducible demo image...\n'
docker build --quiet --tag "$IMAGE" --file tests/Dockerfile . >/dev/null
printf '      Image ready: %s\n\n' "$IMAGE"

printf '[2/5] Starting VSwitch and two isolated VPorts...\n'
docker network create --subnet 172.30.0.0/24 "$NETWORK" >/dev/null
docker run -d --rm \
  --name "$SWITCH" \
  --network "$NETWORK" \
  --ip "$SWITCH_IP" \
  "$IMAGE" \
  stdbuf -oL -eL /app/build/vswitch "$SWITCH_PORT" >/dev/null
wait_for_log "$SWITCH" 'Ready to receive frames from VPorts'
start_port "$PORT1" tap0
start_port "$PORT2" tap1
printf '      VSwitch, VPort 1, and VPort 2 are ready.\n\n'

printf '[3/5] Configuring TAP endpoints...\n'
docker exec "$PORT1" ip address add "$PORT1_IP/24" dev tap0
docker exec "$PORT1" ip link set tap0 up
docker exec "$PORT2" ip address add "$PORT2_IP/24" dev tap1
docker exec "$PORT2" ip link set tap1 up
printf '      tap0 = %s, tap1 = %s\n\n' "$PORT1_IP" "$PORT2_IP"

printf '[4/5] Sending Ethernet traffic in both directions...\n'
docker exec "$PORT1" ping -c 3 -W 2 "$PORT2_IP"
docker exec "$PORT2" ping -c 2 -W 2 "$PORT1_IP"
printf '\n      PASS: both virtual endpoints exchanged packets.\n\n'

printf '[5/5] VSwitch forwarding evidence:\n'
printf '%s\n' '---------------------------------------------'
docker logs "$SWITCH" 2>&1 | grep -E '\[VSwitch\] Received|\[Learn\]|\[Forwarded to\]|\[Broadcasted to\]'
printf '%s\n' '---------------------------------------------'

learned_count=$(docker logs "$SWITCH" 2>&1 | grep -c '\[Learn\]' || true)
forwarded_count=$(docker logs "$SWITCH" 2>&1 | grep -c '\[Forwarded to\]' || true)
if (( learned_count < 2 || forwarded_count < 1 )); then
  printf 'Error: traffic passed, but expected switch evidence was missing.\n' >&2
  exit 1
fi

printf '\nDEMO PASS: learned %d MAC addresses and logged %d unicast forwards.\n' \
  "$learned_count" "$forwarded_count"
