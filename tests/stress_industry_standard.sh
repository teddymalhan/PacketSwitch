#!/bin/bash



set -e

echo "========================================"
echo "  Industry Standard Stress Test Suite"
echo "========================================"
echo ""


VPORT_COUNT=2
TEST_DURATION=60


RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'


log_info() {
  echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
  echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
  echo -e "${RED}[ERROR]${NC} $1"
}


cleanup() {
  log_info "Cleaning up Docker containers..."
  docker stop vswitch-stress vport{1..10} 2>/dev/null || true
  docker rm vswitch-stress vport{1..10} 2>/dev/null || true
}

trap cleanup EXIT INT TERM


log_info "Cleaning up any existing test containers..."
docker stop $(docker ps -q --filter "name=vswitch-stress\|vport.*-stress") 2>/dev/null || true
docker rm $(docker ps -aq --filter "name=vswitch-stress\|vport.*-stress") 2>/dev/null || true


log_info "Building Docker image..."
docker build -t vpn-alpine -f tests/Dockerfile . > /dev/null 2>&1


log_info "Starting VSwitch..."
docker run -d \
  --name vswitch-stress \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  vpn-alpine \
  /app/build/vswitch 8080
sleep 2


log_info "Starting $VPORT_COUNT VPorts..."
for i in $(seq 1 $VPORT_COUNT); do
  docker run -d \
    --name vport$i-stress \
    --cap-add=NET_ADMIN \
    --device=/dev/net/tun \
    --network container:vswitch-stress \
    vpn-alpine \
    /app/build/vport 127.0.0.1 8080 tap$i
  

  docker exec vport$i-stress ip addr add 10.1.1.$((100 + i))/24 dev tap$i
  docker exec vport$i-stress ip link set tap$i up
  
  log_info "  VPort $i configured (10.1.1.$((100 + i)))"
  sleep 1
done


log_info "Installing iperf3..."
docker exec vport1-stress apk add iperf3 > /dev/null 2>&1
docker exec vport2-stress apk add iperf3 > /dev/null 2>&1

log_info "Waiting for system to stabilize..."
sleep 3


log_info "Testing connectivity..."
if docker exec vport1-stress ping -c 5 -W 1 10.1.1.102 > /dev/null 2>&1; then
  log_info "  ✓ Connectivity: OK"
else
  log_error "  ✗ Connectivity: FAILED"
  exit 1
fi

echo ""
echo "========================================"
echo "  Running Stress Tests"
echo "========================================"
echo ""


echo "Test 1: Baseline Latency Measurement"
echo "-------------------------------------"
docker exec vport1-stress ping -c 100 10.1.1.102 | grep -A 2 "packet loss"
echo ""


echo "Test 2: TCP Throughput Test"
echo "-------------------------------------"
log_info "Starting iperf3 server on VPort 2..."
docker exec -d vport2-stress iperf3 -s
sleep 2

log_info "Running iperf3 test for ${TEST_DURATION} seconds..."
docker exec vport1-stress iperf3 -c 10.1.1.102 -t ${TEST_DURATION} -i 10
echo ""


echo "Test 3: UDP Throughput Test"
echo "-------------------------------------"
log_info "Running UDP throughput test..."
docker exec vport1-stress iperf3 -c 10.1.1.102 -u -b 50M -t 30
echo ""


echo "Test 4: Multiple Parallel Streams"
echo "-------------------------------------"
log_info "Testing with 4 parallel streams..."
docker exec vport1-stress iperf3 -c 10.1.1.102 -P 4 -t 30 -i 5
echo ""


echo "Test 5: Latency Jitter Analysis"
echo "-------------------------------------"
log_info "Measuring latency jitter (100 pings)..."
docker exec vport1-stress ping -c 100 -i 0.1 10.1.1.102 | \
  grep -E "min|avg|max" || true
echo ""


echo "Test 6: System Resource Usage"
echo "-------------------------------------"
log_info "Monitoring container resource usage..."
docker stats --no-stream vswitch-stress vport1-stress vport2-stress
echo ""


echo "========================================"
echo "  Test Suite Complete"
echo "========================================"
echo ""
log_info "All tests completed successfully"
echo ""
echo "To analyze results:"
echo "  - Check Docker logs: docker logs vswitch-stress"
echo "  - Monitor resources: docker stats"
echo "  - View container status: docker ps"
echo ""

