#!/bin/bash


set -e

echo "=== Building Docker image ==="
docker build -t vpn-test -f Dockerfile.test .

echo ""
echo "=== Starting VSwitch in background container ==="
VSWITCH_CONTAINER=$(docker run -d --cap-add=NET_ADMIN --device=/dev/net/tun vpn-test /app/build-test/vswitch 8080)

VSWITCH_IP=$(docker inspect -f '{{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}' $VSWITCH_CONTAINER)
echo "VSwitch container: $VSWITCH_CONTAINER"
echo "VSwitch IP: $VSWITCH_IP"

sleep 2

echo ""
echo "=== Starting VPort 1 ==="
VPORT1_CONTAINER=$(docker run -d --cap-add=NET_ADMIN --device=/dev/net/tun \
    --network container:$VSWITCH_CONTAINER \
    vpn-test /app/build-test/vport 127.0.0.1 8080 tap0)

echo "VPort 1 container: $VPORT1_CONTAINER"

sleep 2

echo ""
echo "=== Starting VPort 2 ==="
VPORT2_CONTAINER=$(docker run -d --cap-add=NET_ADMIN --device=/dev/net/tun \
    --network container:$VSWITCH_CONTAINER \
    vpn-test /app/build-test/vport 127.0.0.1 8080 tap1)

echo "VPort 2 container: $VPORT2_CONTAINER"

sleep 2

echo ""
echo "=== Configuring VPort 1 TAP device ==="
docker exec $VPORT1_CONTAINER ip addr add 10.1.1.101/24 dev tap0
docker exec $VPORT1_CONTAINER ip link set tap0 up

echo ""
echo "=== Configuring VPort 2 TAP device ==="
docker exec $VPORT2_CONTAINER ip addr add 10.1.1.102/24 dev tap1
docker exec $VPORT2_CONTAINER ip link set tap1 up

echo ""
echo "=== Testing connectivity ==="
sleep 3

echo "Pinging from VPort 1 to VPort 2..."
docker exec $VPORT1_CONTAINER ping -c 3 10.1.1.102 || true

echo ""
echo "=== Logs from VSwitch ==="
docker logs $VSWITCH_CONTAINER | tail -20

echo ""
echo "=== Cleaning up ==="
docker stop $VSWITCH_CONTAINER $VPORT1_CONTAINER $VPORT2_CONTAINER
docker rm $VSWITCH_CONTAINER $VPORT1_CONTAINER $VPORT2_CONTAINER

echo "Test complete!"

