# Quick Start Guide

Get the VPN system running in 5 minutes!

## Option 1: Docker (Recommended)

### Step 1: Build Docker Image

```bash
cd /path/to/wirelab
docker build -t vpn-alpine -f tests/Dockerfile .
```

### Step 2: Run Automated Test

```bash
chmod +x tests/test_in_docker.sh
./tests/test_in_docker.sh
```

**Done!** The script will start everything, test it, and show logs.

---

## Option 2: Native macOS

### Step 1: Build

```bash
cd /path/to/wirelab
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Step 2: Start VSwitch

**Terminal 1:**
```bash
./vswitch 8080
```

### Step 3: Start VPort

**Terminal 2:**
```bash
sudo ./vport 127.0.0.1 8080
```

Note: You'll see a TAP device created (e.g., `utun6`)

### Step 4: Configure Interface

**Terminal 3:**
```bash

sudo ifconfig utun6 inet 10.1.1.101 netmask 255.255.255.0
sudo ifconfig utun6 up
```

### Step 5: Test

```bash

ping 10.1.1.102
```

---

## Verify It's Working

### Check VSwitch Logs

Should see:
```
[VSwitch] Started at 0.0.0.0:8080
[VSwitch] Received frame from 127.0.0.1:XXXXX...
```

### Check VPort Logs

Should see:
```
[VPort] Created TAP device: utun6
[VPort] Started forwarder threads
```

---

## Next Steps

- **Full testing guide**: [TESTING.md](TESTING.md)
- **Detailed usage**: [../docs/vpn-usage.md](../docs/vpn-usage.md)
- **Architecture**: [../docs/architecture-overview.md](../docs/architecture-overview.md)

---

## Troubleshooting

**"Failed to create TAP device"**
- Run with `sudo`
- On macOS: `brew install --cask tuntap`

**"Port already in use"**
- Choose different port: `./vswitch 9000`
- Or kill existing process: `lsof -i :8080`

**Need help?**
- See [TESTING.md](TESTING.md) for detailed troubleshooting
- Check logs in Terminal 1 and 2

