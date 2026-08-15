# Running Stress Tests Manually in Docker

Quick copy-paste commands for running stress tests with Docker.

## Prerequisites

```bash

cd /path/to/wirelab


cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

## Option 1: Automated Docker Test (Recommended)

```bash

chmod +x tests/test_in_docker.sh
./tests/test_in_docker.sh
```

This handles everything automatically!

---

## Option 2: Manual Docker Commands

### Step 1: Build Docker Image

```bash
docker build -t vpn-alpine -f tests/Dockerfile .
```

### Step 2: Start VSwitch

```bash
docker run -d \
  --name vswitch-test \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  vpn-alpine \
  /app/build/vswitch 8080
```

Check logs:
```bash
docker logs vswitch-test
```

### Step 3: Start VPort 1

```bash
docker run -d \
  --name vport1-test \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  --network container:vswitch-test \
  vpn-alpine \
  /app/build/vport 127.0.0.1 8080 tap0
```

### Step 4: Start VPort 2

```bash
docker run -d \
  --name vport2-test \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  --network container:vswitch-test \
  vpn-alpine \
  /app/build/vport 127.0.0.1 8080 tap1
```

### Step 5: Configure TAP Devices

```bash

docker exec vport1-test ip addr add 10.1.1.101/24 dev tap0
docker exec vport1-test ip link set tap0 up


docker exec vport2-test ip addr add 10.1.1.102/24 dev tap1
docker exec vport2-test ip link set tap1 up
```

### Step 6: Test Connectivity

```bash

docker exec vport1-test ping -c 10 10.1.1.102


docker exec vport2-test ping -c 10 10.1.1.101
```

### Step 7: Check Logs

```bash

docker logs vswitch-test


docker logs vport1-test


docker logs vport2-test


docker logs -f vswitch-test
```

### Step 8: Cleanup

```bash
docker stop vswitch-test vport1-test vport2-test
docker rm vswitch-test vport1-test vport2-test
```

---

## Option 3: Stress Test - Multiple VPorts

### Start 5 VPorts for Scale Test

```bash

docker run -d \
  --name vswitch-test \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  vpn-alpine \
  /app/build/vswitch 8080


for i in {1..5}; do
  docker run -d \
    --name vport${i}-test \
    --cap-add=NET_ADMIN \
    --device=/dev/net/tun \
    --network container:vswitch-test \
    vpn-alpine \
    /app/build/vport 127.0.0.1 8080 tap$i
  

  docker exec vport${i}-test ip addr add 10.1.1.$((100 + i))/24 dev tap$i
  docker exec vport${i}-test ip link set tap$i up
  
  echo "VPort $i configured"
done
```

### Test Connectivity Between All Pairs

```bash

for src in {1..5}; do
  for dst in {1..5}; do
    if [ $src -ne $dst ]; then
      echo "Testing: VPort $src → VPort $dst"
      docker exec vport${src}-test ping -c 3 -W 1 10.1.1.$((100 + dst))
    fi
  done
done
```

### Generate High-Rate Traffic

```bash

docker exec vport1-test ping -i 0.1 -c 100 10.1.1.102


docker exec vport1-test ping -f 10.1.1.102


docker exec vport1-test bash -c "for i in {1..600}; do ping -c 1 -W 1 10.1.1.102; sleep 1; done"
```

---

## Option 4: Stress Test - Throughput Test

### Start Two VPorts and Generate High Traffic

```bash

docker run -d \
  --name vswitch-throughput \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  vpn-alpine \
  /app/build/vswitch 8080


docker run -d \
  --name vport-throughput1 \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  --network container:vswitch-throughput \
  vpn-alpine \
  /app/build/vport 127.0.0.1 8080 tap0


docker run -d \
  --name vport-throughput2 \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  --network container:vswitch-throughput \
  vpn-alpine \
  /app/build/vport 127.0.0.1 8080 tap1


docker exec vport-throughput1 ip addr add 10.1.1.101/24 dev tap0
docker exec vport-throughput1 ip link set tap0 up

docker exec vport-throughput2 ip addr add 10.1.1.102/24 dev tap1
docker exec vport-throughput2 ip link set tap1 up


docker exec vport-throughput1 ping -i 0.01 -c 1000 10.1.1.102


docker exec vport-throughput1 ping -f 10.1.1.102

```

---

## Option 5: Stress Test - Duration Test (Long-Running)

### Run for 1 Hour with Periodic Traffic

```bash



docker exec vport1-test bash -c "
  for i in {1..360}; do
    ping -c 1 -W 1 10.1.1.102
    sleep 10
    echo \"Minute \$((i / 6)): Check \$i complete\"
  done
"


docker exec -d vport1-test bash -c "
  for i in {1..360}; do
    ping -c 1 -W 1 10.1.1.102 > /dev/null 2>&1
    sleep 10
  done
"


docker logs -f vswitch-throughput
```

---

## Option 6: Comprehensive Stress Test

### Run All Stress Scenarios

```bash

docker run -d --name vswitch-stress --cap-add=NET_ADMIN --device=/dev/net/tun \
  vpn-alpine /app/build/vswitch 8080

for i in {1..4}; do
  docker run -d \
    --name vport-stress${i} \
    --cap-add=NET_ADMIN \
    --device=/dev/net/tun \
    --network container:vswitch-stress \
    vpn-alpine \
    /app/build/vport 127.0.0.1 8080 tap$i
  
  docker exec vport-stress${i} ip addr add 10.1.1.$((100 + i))/24 dev tap$i
  docker exec vport-stress${i} ip link set tap$i up
done


for src in {1..4}; do
  for dst in {1..4}; do
    if [ $src -ne $dst ]; then
      docker exec vport-stress${src} ping -c 1 -W 1 10.1.1.$((100 + dst)) > /dev/null 2>&1 && \
        echo "✓ VPort $src → VPort $dst: OK"
    fi
  done
done


for i in {1..300}; do
  echo "Iteration $i"
  for src in {1..4}; do
    for dst in {1..4}; do
      if [ $src -ne $dst ]; then
        docker exec vport-stress${src} ping -c 1 -W 1 10.1.1.$((100 + dst)) > /dev/null 2>&1
      fi
    done
  done
  sleep 1
done


docker stop vswitch-stress vport-stress{1..4}
docker rm vswitch-stress vport-stress{1..4}
```

---

## Monitoring During Tests

### Watch Logs in Real-Time

```bash

docker logs -f vswitch-test


docker logs -f vswitch-test vport1-test vport2-test
```

### Check System Resources

```bash

docker stats vswitch-test vport1-test vport2-test


docker exec vswitch-test top -b -n 1
docker exec vport1-test ifconfig
```

### Monitor Network Traffic

```bash

docker exec vport1-test tcpdump -i tap0 -c 50


docker exec vport1-test ip link show
docker exec vport1-test ip addr show
```

---

## Troubleshooting

### Check if VSwitch is running

```bash
docker ps | grep vswitch
docker logs vswitch-test
```

### Restart a container

```bash
docker restart vport1-test
```

### Clean up everything

```bash
docker stop $(docker ps -a -q --filter "name=vswitch\|vport")
docker rm $(docker ps -a -q --filter "name=vswitch\|vport")
```

### Check TAP devices

```bash
docker exec vport1-test ip link show type tun
docker exec vport1-test ls -l /dev/net/tun
```

### Force cleanup of stuck containers

```bash
docker kill $(docker ps -a -q --filter "name=vswitch\|vport")
docker rm $(docker ps -a -q --filter "name=vswitch\|vport")
```

---

## Quick Reference

```bash

docker run -d --name vswitch --cap-add=NET_ADMIN --device=/dev/net/tun \
  vpn-alpine /app/build/vswitch 8080

docker run -d --name vport1 --cap-add=NET_ADMIN --device=/dev/net/tun \
  --network container:vswitch vpn-alpine /app/build/vport 127.0.0.1 8080 tap0

docker run -d --name vport2 --cap-add=NET_ADMIN --device=/dev/net/tun \
  --network container:vswitch vpn-alpine /app/build/vport 127.0.0.1 8080 tap1


docker exec vport1 ip addr add 10.1.1.101/24 dev tap0 && \
docker exec vport1 ip link set tap0 up && \
docker exec vport2 ip addr add 10.1.1.102/24 dev tap1 && \
docker exec vport2 ip link set tap1 up


docker exec vport1 ping -c 5 10.1.1.102


docker stop vswitch vport1 vport2 && docker rm vswitch vport1 vport2
```

---

## Next Steps

- See [STRESS_TESTING.md](STRESS_TESTING.md) for detailed analysis
- Check [TESTING.md](TESTING.md) for debugging help
- Monitor logs for errors during stress tests

