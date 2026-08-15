#!/bin/bash


set -e

echo "=== VSwitch Scale Test ==="
echo "Testing with multiple VPort connections"
echo ""


pkill vswitch || true
pkill vport || true
sleep 1


for i in {0..9}; do
  sudo ip link delete tap$i 2>/dev/null || true
done


echo "Starting VSwitch on port 8080..."
./build/vswitch 8080 &
VSWITCH_PID=$!
sleep 2


VPORT_COUNT=5

echo "Creating $VPORT_COUNT VPort connections..."


VPORT_PIDS=()
for i in $(seq 0 $((VPORT_COUNT - 1))); do
  echo "  Starting VPort $i on tap$i..."
  sudo ./build/vport 127.0.0.1 8080 tap$i &
  VPORT_PIDS[$i]=$!
  sleep 0.5
  

  echo "  Configuring TAP device tap$i with IP 10.1.1.$((100 + i))..."
  sudo ip addr add 10.1.1.$((100 + i))/24 dev tap$i
  sudo ip link set tap$i up
  
  echo "  VPort $i ready"
done

echo ""
echo "All VPorts connected. System ready."
echo "VSwitch should show $VPORT_COUNT learned MAC addresses."
echo ""
echo "Press Ctrl+C to stop..."


trap "echo ''; echo 'Stopping...'; kill $VSWITCH_PID ${VPORT_PIDS[@]} 2>/dev/null; exit 0" INT TERM

wait $VSWITCH_PID

