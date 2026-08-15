#!/bin/bash


set -e

echo "========================================="
echo "Docker VPN Test Script"
echo "========================================="
echo ""


echo "Starting VSwitch..."
VSWITCH=$(docker run -d --rm \
  --name vswitch-test \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  --entrypoint /app/build/vswitch \
  vpn-alpine 8080)

sleep 2
echo "VSwitch started (container: $VSWITCH)"
echo ""


echo "Starting VPort 1..."
VPORT1=$(docker run -d --rm \
  --name vport1-test \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  --network container:vswitch-test \
  --entrypoint /app/build/vport \
  vpn-alpine 127.0.0.1 8080 tap0)

sleep 2
echo "VPort 1 started (container: $VPORT1)"
echo ""


echo "Starting VPort 2..."
VPORT2=$(docker run -d --rm \
  --name vport2-test \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  --network container:vswitch-test \
  --entrypoint /app/build/vport \
  vpn-alpine 127.0.0.1 8080 tap1)

sleep 2
echo "VPort 2 started (container: $VPORT2)"
echo ""


echo "Configuring TAP devices..."
docker exec vport1-test ip addr add 10.1.1.101/24 dev tap0 || true
docker exec vport1-test ip link set tap0 up || true

docker exec vport2-test ip addr add 10.1.1.102/24 dev tap1 || true
docker exec vport2-test ip link set tap1 up || true

echo "TAP devices configured"
echo ""


echo "=== Network Interfaces ==="
echo "VPort 1 interfaces:"
docker exec vport1-test ip link show || echo "Could not show interfaces"
echo ""
echo "VPort 2 interfaces:"
docker exec vport2-test ip link show || echo "Could not show interfaces"
echo ""


echo "=== VSwitch Logs ==="
docker logs vswitch-test --tail 10
echo ""

echo "=== VPort 1 Logs ==="
docker logs vport1-test --tail 10
echo ""

echo "=== VPort 2 Logs ==="
docker logs vport2-test --tail 10
echo ""


echo "=== Testing Connectivity ==="
echo "Pinging from VPort 1 to VPort 2..."
docker exec vport1-test ping -c 3 10.1.1.102 || echo "Ping may have failed (expected)"
echo ""


echo "=== Updated Logs ==="
echo "VSwitch:"
docker logs vswitch-test --tail 20
echo ""

echo "VPort 1:"
docker logs vport1-test --tail 20
echo ""

echo "VPort 2:"
docker logs vport2-test --tail 20
echo ""


echo "=== Cleaning Up ==="
docker stop vswitch-test vport1-test vport2-test 2>/dev/null || true
docker rm vswitch-test vport1-test vport2-test 2>/dev/null || true

echo "Test complete!"

