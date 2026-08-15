# Industry Standard Stress Testing for VPN/Virtual Switch

This document outlines industry-standard methodologies for stress testing VPN and virtual network systems, following practices used by OpenVPN, WireGuard, and networking equipment manufacturers.

## Industry Testing Methodology

### 1. Define Scope and Success Criteria

**Metrics to Test:**
- **Throughput**: Maximum bandwidth (Mbps/Gbps)
- **Latency**: Round-trip time under load (ms)
- **Packet Loss**: Percentage of dropped packets
- **Concurrent Connections**: Number of simultaneous VPorts
- **CPU/Memory Usage**: Resource utilization under load
- **Stability**: Long-running operation (hours/days)

**Success Criteria:**
- Latency: < 10ms (local), < 50ms (remote)
- Packet Loss: < 0.1%
- Throughput: > 50% of theoretical max
- CPU Usage: < 80% under maximum load
- No memory leaks over 24+ hours

### 2. Establish Baseline Performance

Before stress testing, measure normal operation:

```bash

ping -c 100 10.1.1.102 | tail -1



iperf3 -c 10.1.1.102 -t 30

```

### 3. Controlled Ramp-Up Testing

Gradually increase load to find breaking points:

```bash

rates=(10 50 100 200 500 1000)

for rate in "${rates[@]}"; do
  echo "Testing at ${rate} pps..."
  ping -i $(awk "BEGIN {print 1/$rate}") -c 1000 10.1.1.102
done
```

### 4. Soak Testing (Endurance)

Run system under load for extended periods:

```bash

timeout 86400 bash -c 'while true; do ping -c 1 10.1.1.102; sleep 1; done'
```

### 5. Push-to-Fail Testing

Test system limits:

```bash

for i in {1..20}; do
  sudo ./build/vport 127.0.0.1 8080 tap$i &
done


iperf3 -c 10.1.1.102 -u -b 1G
```

---

## Industry Standard Tools

### 1. iPerf3 - Throughput Testing

**Install:**
```bash

brew install iperf3


sudo apt-get install iperf3


apk add iperf3
```

**Basic Throughput Test:**

```bash

docker exec vport2 iperf3 -s


docker exec vport1 iperf3 -c 10.1.1.102 -t 30 -i 5


docker exec vport1 iperf3 -c 10.1.1.102 -u -b 100M -t 60
```

**Parallel Streams (Multiple Connections):**

```bash

docker exec vport1 iperf3 -c 10.1.1.102 -P 10 -t 60


docker exec vport1 iperf3 -c 10.1.1.102 -d -t 60
```

### 2. MTR - Latency and Packet Loss

**Install:**
```bash

brew install mtr


sudo apt-get install mtr
```

**Continuous Monitoring:**

```bash

mtr --report --report-cycles 100 10.1.1.102


docker exec vport1 mtr 10.1.1.102
```

### 3. Flent - Advanced Network Testing

**Install:**
```bash
pip3 install flent
```

**Netperf Reverb Test (Load Testing):**

```bash
flent rrul -H 10.1.1.102 -l 300
```

### 4. tcpreplay - Replay Captured Traffic

**Install:**
```bash

brew install tcpreplay


sudo apt-get install tcpreplay
```

**Replay Traffic:**

```bash

tcpdump -i tap0 -w test_traffic.pcap


tcpreplay -i tap0 -l 100 test_traffic.pcap
```

### 5. NetEm - Simulate Network Conditions

**Install:**
```bash


```

**Simulate Latency/Delay:**

```bash

docker exec vport1 tc qdisc add dev tap0 root netem delay 50ms


docker exec vport1 tc qdisc add dev tap0 root netem loss 1%


docker exec vport1 tc qdisc add dev tap0 root tbf rate 10mbit
```

### 6. sar - System Resource Monitoring

**Install:**
```bash


```

**Monitor During Tests:**

```bash

sar 1


sar 1 -o sar_output
```

---

## Complete Stress Test Suite

### Test 1: Throughput (Ramp-Up)

```bash




./tests/test_in_docker.sh --background


for bw in 1M 10M 50M 100M 500M; do
  echo "Testing $bw throughput..."
  iperf3 -c 10.1.1.102 -b $bw -t 30 -i 5
  sleep 5
done


./tests/cleanup_docker.sh
```

### Test 2: Latency Under Load

```bash




./tests/test_in_docker.sh --background


iperf3 -c 10.1.1.102 -b 100M -t 300 &


ping -c 100 10.1.1.102 | grep avg


mtr --report --report-cycles 100 10.1.1.102
```

### Test 3: Concurrent Connections (Scale)

```bash




docker run -d --name vswitch-stress --cap-add=NET_ADMIN --device=/dev/net/tun \
  vpn-alpine /app/build/vswitch 8080


MAX_PORTS=10
for i in $(seq 1 $MAX_PORTS); do
  docker run -d --name vport$i --cap-add=NET_ADMIN --device=/dev/net/tun \
    --network container:vswitch-stress \
    vpn-alpine /app/build/vport 127.0.0.1 8080 tap$i
  
  docker exec vport$i ip addr add 10.1.1.$((100 + i))/24 dev tap$i
  docker exec vport$i ip link set tap$i up
done


for i in $(seq 1 $MAX_PORTS); do
  for j in $(seq 1 $MAX_PORTS); do
    if [ $i -ne $j ]; then
      docker exec vport$i ping -c 5 10.1.1.$((100 + j))
    fi
  done
done
```

### Test 4: Long-Running Stability (Soak Test)

```bash




./tests/test_in_docker.sh --background


DURATION=86400
START=$(date +%s)

while [ $(($(date +%s) - START)) -lt $DURATION ]; do

  iperf3 -c 10.1.1.102 -b 10M -t 1
  

  docker stats --no-stream
  
  sleep 60
done


valgrind --leak-check=full ./build/vport 127.0.0.1 8080 tap0
```

### Test 5: Jitter and Packet Loss

```bash




mtr --report --report-cycles 1000 --raw 10.1.1.102 > jitter_raw.txt


awk '{print $8}' jitter_raw.txt | sort -n | awk '
{
  sum+=$1; sumsq+=$1*$1
}
END {
  mean=sum/NR
  printf "Mean: %.2fms\n", mean
  printf "StdDev: %.2fms\n", sqrt(sumsq/NR - mean*mean)
  printf "Min: %s\n", $0
}' <(sort -n)
```

---

## Performance Benchmarks (Industry Standards)

### Layer 2 VPN Performance Targets

| Metric | Acceptable | Good | Excellent |
|--------|-----------|------|-----------|
| **Local Latency** | < 5ms | < 2ms | < 1ms |
| **Remote Latency** | < 50ms | < 30ms | < 20ms |
| **Throughput** | > 50% | > 80% | > 95% |
| **Packet Loss** | < 1% | < 0.1% | < 0.01% |
| **Jitter** | < 20ms | < 10ms | < 5ms |
| **Concurrent VPorts** | 10+ | 50+ | 100+ |
| **CPU (idle)** | < 5% | < 3% | < 1% |
| **CPU (max load)** | < 80% | < 60% | < 40% |
| **Memory Growth** | < 10MB/h | < 5MB/h | < 1MB/h |

---

## Automated Test Framework

### Using iPerf3 in Docker

Create a comprehensive test suite:

```bash



set -e

echo "=== Industry Standard Stress Test Suite ==="
echo ""


if [ ! -f build/vswitch ]; then
  cmake -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build
fi


docker build -t vpn-alpine -f tests/Dockerfile .


docker run -d --name vswitch --cap-add=NET_ADMIN --device=/dev/net/tun \
  vpn-alpine /app/build/vswitch 8080
sleep 2


for i in {1..2}; do
  docker run -d --name vport$i --cap-add=NET_ADMIN --device=/dev/net/tun \
    --network container:vswitch \
    vpn-alpine /app/build/vport 127.0.0.1 8080 tap$i
  
  docker exec vport$i ip addr add 10.1.1.$((100 + i))/24 dev tap$i
  docker exec vport$i ip link set tap$i up
  sleep 1
done


docker exec vport1 apk add iperf3
docker exec vport2 apk add iperf3

echo "=== Test 1: Baseline Latency ==="
docker exec vport1 ping -c 100 10.1.1.102 | grep avg

echo "=== Test 2: Throughput Test ==="
docker exec vport2 iperf3 -s &
sleep 1
docker exec vport1 iperf3 -c 10.1.1.102 -t 30 -i 5

echo "=== Test 3: UDP Flood Test ==="
docker exec vport1 iperf3 -c 10.1.1.102 -u -b 100M -t 60

echo "=== Test 4: Multiple Parallel Streams ==="
docker exec vport1 iperf3 -c 10.1.1.102 -P 10 -t 60

echo "=== Tests Complete ==="


docker stop vswitch vport1 vport2
docker rm vswitch vport1 vport2
```

---

## Monitoring and Analysis

### Real-Time Monitoring

```bash

docker logs -f vswitch


docker stats --no-stream vswitch vport1 vport2


ifconfig | grep -A 5 tap
```

### Performance Analysis

```bash

iperf3 -c 10.1.1.102 -t 30 | grep -E "sender|receiver"


ping -c 100 10.1.1.102 | tail -1 | awk -F'/' '{print $5}'


valgrind --leak-check=full --show-leak-kinds=all ./build/vport 127.0.0.1 8080 tap0
```

---

## CI/CD Integration

Add to your GitHub Actions:

```yaml

name: Stress Tests
on:
  schedule:
    - cron: '0 2 * * 0'

jobs:
  stress-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y iperf3 mtr docker.io
      
      - name: Build
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Release
          cmake --build build
      
      - name: Run stress tests
        run: |
          chmod +x tests/stress_industry_standard.sh
          ./tests/stress_industry_standard.sh
      
      - name: Upload results
        uses: actions/upload-artifact@v2
        with:
          name: stress-test-results
          path: |
            *.txt
            *.log
```

---

## References

- [RFC 2544](https://datatracker.ietf.org/doc/html/rfc2544) - Network Benchmarking Methodology
- [RFC 5180](https://datatracker.ietf.org/doc/html/rfc5180) - IPv6 Benchmarking
- [iPerf3 Documentation](https://iperf.fr/iperf-doc.php)
- [ITU-T Y.1564](https://www.itu.int/rec/T-REC-Y.1564/) - Ethernet service activation test methodology

---

## Quick Start

```bash

docker build -t vpn-alpine -f tests/Dockerfile .


chmod +x tests/stress_industry_standard.sh
./tests/stress_industry_standard.sh


cat stress-test-results.txt
```

