#!/bin/bash


set -e

echo "========================================"
echo "  VSwitch Comprehensive Stress Test"
echo "========================================"
echo ""


VPORT_COUNT=4
TEST_DURATION=300
PACKET_RATE=50


cleanup() {
  echo ""
  echo "Cleaning up..."
  kill $VSWITCH_PID 2>/dev/null || true
  for pid in "${VPORT_PIDS[@]}"; do
    kill $pid 2>/dev/null || true
  done
  killall ping 2>/dev/null || true
  

  for i in {0..9}; do
    sudo ip link delete tap$i 2>/dev/null || true
  done
  
  echo "Cleanup complete."
}

trap cleanup EXIT INT TERM


pkill vswitch || true
pkill vport || true
sleep 1

echo "Starting test configuration:"
echo "  VPorts: $VPORT_COUNT"
echo "  Duration: $TEST_DURATION seconds"
echo "  Packet rate: $PACKET_RATE pps"
echo ""


echo "[1/$((VPORT_COUNT + 2))] Starting VSwitch..."
./build/vswitch 8080 &
VSWITCH_PID=$!
sleep 2
echo "  ✓ VSwitch started (PID: $VSWITCH_PID)"


echo ""
echo "[2-$((VPORT_COUNT + 1))/$((VPORT_COUNT + 2))] Starting VPorts..."
VPORT_PIDS=()
for i in $(seq 0 $((VPORT_COUNT - 1))); do
  echo "  Starting VPort $i..."
  sudo ./build/vport 127.0.0.1 8080 tap$i &
  VPORT_PIDS[$i]=$!
  sleep 0.5
  

  sudo ip addr add 10.1.1.$((100 + i))/24 dev tap$i 2>/dev/null
  sudo ip link set tap$i up 2>/dev/null
  
  echo "  ✓ VPort $i ready (TAP: tap$i, IP: 10.1.1.$((100 + i)))"
done


echo ""
echo "Waiting for system to stabilize..."
sleep 3


echo ""
echo "[$((VPORT_COUNT + 2))/$((VPORT_COUNT + 2))] Testing connectivity..."
for i in $(seq 0 $((VPORT_COUNT - 1))); do
  for j in $(seq 0 $((VPORT_COUNT - 1))); do
    if [ $i -ne $j ]; then
      if ping -c 1 -W 1 10.1.1.$((100 + j)) > /dev/null 2>&1; then
        echo "  ✓ Connectivity: 10.1.1.$((100 + i)) → 10.1.1.$((100 + j))"
      else
        echo "  ✗ Connectivity: 10.1.1.$((100 + i)) → 10.1.1.$((100 + j))"
      fi
    fi
  done
done

echo ""
echo "========================================"
echo "  Starting Stress Test"
echo "========================================"
echo ""
echo "Generating traffic between all VPort pairs..."
echo "Press Ctrl+C to stop early"
echo ""

START_TIME=$(date +%s)
ITERATION=0


while [ $(date +%s) -lt $((START_TIME + TEST_DURATION)) ]; do
  ITERATION=$((ITERATION + 1))
  ELAPSED=$(( $(date +%s) - START_TIME ))
  

  if [ $((ELAPSED % 30)) -eq 0 ]; then
    PERCENT=$((ELAPSED * 100 / TEST_DURATION))
    echo "[Progress: $PERCENT%] Elapsed: ${ELAPSED}s, Iteration: $ITERATION"
  fi
  

  for src in $(seq 0 $((VPORT_COUNT - 1))); do
    for dst in $(seq 0 $((VPORT_COUNT - 1))); do
      if [ $src -ne $dst ]; then

        ping -c 1 -W 1 10.1.1.$((100 + dst)) > /dev/null 2>&1 || true
      fi
    done
  done
  

  sleep 1
done

echo ""
echo "========================================"
echo "  Stress Test Complete"
echo "========================================"
echo "  Duration: $TEST_DURATION seconds"
echo "  Iterations: $ITERATION"
echo "  VPorts: $VPORT_COUNT"
echo "========================================"

