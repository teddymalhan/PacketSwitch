# Testing Guide

This guide explains how to test the VPN system using different methods.

## Table of Contents

1. [Unit Tests](#unit-tests)
2. [Integration Tests](#integration-tests)
3. [Manual Testing on macOS](#manual-testing-on-macos)
4. [Docker Testing](#docker-testing)
5. [End-to-End Testing](#end-to-end-testing)

---

## Unit Tests

Test individual components in isolation.

### Run All Unit Tests

```bash
cd build
cmake ..
cmake --build .
ctest -C Debug -VV
```

### Run Specific Test

```bash
cd build
ctest -C Debug -R mac_table_test
```

### Expected Output

```
Test project /path/to/build
    Start 1: tmp_test
1/9 Test #1: tmp_test .........................   Passed    0.01 sec
...
9/9 Test #9: integration_test .................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 9
```

**Total Tests**: 113 (all passing)

---

## Integration Tests

Test components working together.

Same as unit tests - integration tests are included in the test suite.

---

## Manual Testing on macOS

### Prerequisites

- macOS with root/sudo access
- Built executables in `build/` directory

### Step 1: Start VSwitch

```bash
cd /path/to/modern-cpp-template/build
./vswitch 8080
```

Output:
```
=== VSwitch - Virtual Switch for Layer 2 Networking ===
[VSwitch] Started at 0.0.0.0:8080
[VSwitch] Ready to receive frames from VPorts
```

### Step 2: Start VPort 1 (Terminal 2)

```bash
cd /path/to/modern-cpp-template/build
sudo ./vport 127.0.0.1 8080
```

Output:
```
[VPort] Created TAP device: utun6, VSwitch: 127.0.0.1:8080
[VPort] Started forwarder threads
VPort is running! Press Ctrl+C to stop.
```

**Note TAP device name**: `utun6` (or similar)

### Step 3: Start VPort 2 (Terminal 3)

```bash
cd /path/to/modern-cpp-template/build
sudo ./vport 127.0.0.1 8080 tap1
```

**Note second TAP device name**: `utun7` (or similar)

### Step 4: Configure TAP Devices (Terminal 4)

```bash

sudo ifconfig utun6 inet 10.1.1.101 netmask 255.255.255.0
sudo ifconfig utun6 up


sudo ifconfig utun7 inet 10.1.1.102 netmask 255.255.255.0
sudo ifconfig utun7 up
```

### Step 5: Test Connectivity

```bash

ping 10.1.1.102


ping 10.1.1.101
```

### Expected Behavior

**VSwitch output** should show:
```
[VSwitch] Received frame from 127.0.0.1:XXXXX: dst=... src=... size=...
  [Learn] aa:bb:cc:dd:ee:ff → 127.0.0.1:XXXXX
  [Forwarded] ...
```

**VPort output** should show:
```
[VPort] Sent to VSwitch: dst=... src=... size=...
[VPort] Forward to TAP device: dst=... src=... size=...
```

**Ping should succeed!**

### Troubleshooting

#### No frames in logs
- Check firewall settings
- Verify VPort is sending frames (watch logs in real-time with `docker logs -f`)

#### Cannot create TAP device
- Ensure running with sudo
- On macOS, may need: `brew install --cask tuntap`

#### Ping not working
- Check if both TAP devices have IP addresses configured
- Verify network routes are set up
- Try `ping -I utun6 10.1.1.102` to specify interface

---

## Docker Testing

Test in isolated Alpine Linux containers with proper TAP support.

### Quick Test Script

```bash
cd /path/to/modern-cpp-template
chmod +x tests/test_in_docker.sh
./tests/test_in_docker.sh
```

This script will:
1. Build Docker image (if needed)
2. Start VSwitch container
3. Start two VPort containers
4. Configure TAP devices
5. Test connectivity
6. Show logs
7. Clean up

### Manual Docker Testing

#### Build Docker Image

```bash
cd /path/to/modern-cpp-template
docker build -t vpn-alpine -f tests/Dockerfile .
```

#### Start VSwitch

```bash
docker run -it --rm \
  --name vswitch-test \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  --entrypoint /app/build/vswitch \
  vpn-alpine 8080
```

#### Start VPort 1 (Terminal 2)

```bash
docker run -it --rm \
  --name vport1-test \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  --network container:vswitch-test \
  --entrypoint /app/build/vport \
  vpn-alpine 127.0.0.1 8080 tap0
```

#### Start VPort 2 (Terminal 3)

```bash
docker run -it --rm \
  --name vport2-test \
  --cap-add=NET_ADMIN \
  --device=/dev/net/tun \
  --network container:vswitch-test \
  --entrypoint /app/build/vport \
  vpn-alpine 127.0.0.1 8080 tap1
```

#### Configure and Test (Terminal 4)

```bash

docker exec vport1-test ip addr add 10.1.1.101/24 dev tap0
docker exec vport1-test ip link set tap0 up

docker exec vport2-test ip addr add 10.1.1.102/24 dev tap1
docker exec vport2-test ip link set tap1 up


docker exec vport1-test ping -c 3 10.1.1.102
```

#### View Logs

```bash

docker logs vswitch-test


docker logs vport1-test
docker logs vport2-test


docker logs -f vswitch-test
```

#### Cleanup

```bash
docker stop vswitch-test vport1-test vport2-test
```

---

## End-to-End Testing

Full system test with packet capture and verification.

### Wireshark/Tcpdump Monitoring

Monitor traffic on TAP devices:

```bash

sudo tcpdump -i utun6


sudo tcpdump -i tap0
```

### Test Scenarios

#### 1. Unicast Test

Send pings between VPorts:

```bash

ping -c 10 10.1.1.102




```

#### 2. Broadcast Test

```bash

ping -b 10.1.1.255



```

#### 3. MAC Learning Test

```bash



docker exec vswitch-test cat /proc/net/arp

```

### Load Testing

Generate high traffic:

```bash

ping -i 0.1 10.1.1.102


while true; do
    ping -c 1 10.1.1.102
    sleep 0.1
done
```

Monitor CPU and memory:

```bash
top
htop
```

---

## Expected Logs

### VSwitch (Working System)

```
[VSwitch] Started at 0.0.0.0:8080
[VSwitch] Ready to receive frames from VPorts

[VSwitch] Received frame from 127.0.0.1:33460: dst=ff:ff:ff:ff:ff:ff src=46:71:3e:30:78:9c size=42
  [Learn] 46:71:3e:30:78:9c → 127.0.0.1:33460
  [Broadcasted] 1 endpoints

[VSwitch] Received frame from 127.0.0.1:33460: dst=46:71:3e:30:78:9c src=86:69:1e:35:16:bd size=98
  [Forwarded] 46:71:3e:30:78:9c
```

### VPort (Working System)

```
[VPort] TAP → VSwitch forwarder started
[VPort] VSwitch → TAP forwarder started

[VPort] Sent to VSwitch: dst=ff:ff:ff:ff:ff:ff src=46:71:3e:30:78:9c type=0806 size=42

[VPort] Forward to TAP device: dst=46:71:3e:30:78:9c src=86:69:1e:35:16:bd type=0800 size=98
```

---

## Performance Benchmarks

Expected performance:

- **Latency**: < 1ms (local loopback)
- **Throughput**: ~100 Mbps (depends on system)
- **Packet Loss**: 0% (local testing)
- **CPU Usage**: < 10% (idle)
- **Memory**: ~10MB per VPort

---

## Debugging

### Enable Verbose Logging

Add debug output to source files or use environment variables:

```bash
export VPN_DEBUG=1
./vswitch 8080
```

### Check TAP Devices

```bash

ifconfig | grep utun


ip link show | grep tap


docker exec vport1-test ip link show
```

### Packet Inspection

Capture packets:

```bash

sudo tcpdump -i any -n host 10.1.1.0/24


sudo tcpdump -i any -n host 10.1.1.0/24


docker exec vport1-test tcpdump -i tap0 -n
```

---

## Common Issues

### Issue: "Failed to create TAP device"

**Solution**:
- Ensure running with sudo/root
- Check TAP module is loaded: `lsmod | grep tap` (Linux)
- Install tuntap on macOS: `brew install --cask tuntap`

### Issue: "Port already in use"

**Solution**:
```bash

lsof -i :8080


netstat -an | grep 8080


./vswitch 9000
```

### Issue: "Network unreachable"

**Solution**:
- Configure TAP device with IP address
- Bring interface up: `ip link set tap0 up`
- Add route if needed

### Issue: No packets forwarded

**Check**:
- Both VPorts are connected to same VSwitch
- TAP devices are configured and up
- VSwitch logs show received frames
- Firewall is not blocking

---

## Next Steps

After testing:
1. Review logs for any errors
2. Verify MAC address learning
3. Test with multiple VPorts
4. Test across different machines
5. Review performance metrics

For more information, see:
- [VPN Usage Guide](../docs/vpn-usage.md)
- [README](../README.md)

