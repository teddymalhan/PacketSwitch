#!/bin/bash


set -e

echo "========================================="
echo "Building Docker image..."
echo "========================================="

docker build -t vpn-alpine .

echo ""
echo "========================================="
echo "Running Docker test..."
echo "========================================="


docker run --rm --privileged \
    vpn-alpine sh -c '
echo "=== Testing VPort and VSwitch ==="
echo ""

cd /app/build

echo "Starting VSwitch in background..."
timeout 5 ./vswitch 8080 &
VSWITCH_PID=$!
sleep 1

echo "VSwitch PID: $VSWITCH_PID"
echo ""

echo "Testing VPort creation (will timeout)..."
timeout 3 ./vport 127.0.0.1 8080 || true

echo ""
echo "=== Checking executables ==="
ls -lh vport vswitch

echo ""
echo "=== Test complete! ==="
kill $VSWITCH_PID 2>/dev/null || true
'

echo ""
echo "========================================="
echo "✅ Docker test complete!"
echo "========================================="

