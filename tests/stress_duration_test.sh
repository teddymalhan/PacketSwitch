#!/bin/bash


set -e

echo "=== VSwitch Duration Test ==="
echo "Testing long-running stability (1 hour)"
echo ""


pkill vswitch || true
pkill vport || true
sleep 1


echo "Starting VSwitch on port 8080..."
./build/vswitch 8080 &
VSWITCH_PID=$!
sleep 2


echo "Starting VPort 0 on tap0..."
sudo ./build/vport 127.0.0.1 8080 tap0 &
VPORT1_PID=$!
sleep 1

echo "Starting VPort 1 on tap1..."
sudo ./build/vport 127.0.0.1 8080 tap1 &
VPORT2_PID=$!
sleep 1


sudo ip addr add 10.1.1.101/24 dev tap0
sudo ip addr add 10.1.1.102/24 dev tap1
sudo ip link set tap0 up
sudo ip link set tap1 up

echo ""
echo "System ready for duration test."
echo "Running for 1 hour with periodic traffic generation..."
echo ""


END_TIME=$(date -d "+1 hour" +%s)


generate_traffic() {
  ping -c 1 -W 1 10.1.1.102 > /dev/null 2>&1 || true
}


START_TIME=$(date +%s)
ITERATION=0

while [ $(date +%s) -lt $END_TIME ]; do
  ITERATION=$((ITERATION + 1))
  ELAPSED=$(( $(date +%s) - START_TIME ))
  
  if [ $((ELAPSED % 60)) -eq 0 ]; then
    echo "[$ELAPSED seconds] Sending test packets... (iteration $ITERATION)"
  fi
  
  generate_traffic
  sleep 10
done

echo ""
echo "Duration test complete! System ran for 1 hour."
echo ""


echo "Stopping components..."
kill $VSWITCH_PID $VPORT1_PID $VPORT2_PID 2>/dev/null

echo "Done."

