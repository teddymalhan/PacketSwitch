# VPN Usage Guide

## Overview

This project implements a Layer 2 virtual private network (VPN) using modern C++17. It consists of:

- **VSwitch**: Central switching fabric that learns MAC addresses and forwards Ethernet frames
- **VPort**: Virtual port that bridges a TAP device with the VSwitch via UDP

## Architecture

```
┌─────────────────────────────────────┐
│          VSwitch (Server)           │
│  • Learns MAC addresses            │
│  • Forwards frames to known hosts   │
│  • Handles broadcast frames         │
└──────────────┬──────────────────────┘
               │ UDP
               │
    ┌──────────┴──────────┬──────────────┐
    │                     │              │
  VPort 1              VPort 2        VPort N
  (Client)            (Client)       (Client)
    │                     │              │
    └─── TAP ─────────────┴────── TAP ───┘
            │                │              │
      Linux Kernel    Linux Kernel   Linux Kernel
```

## Building

```bash

cmake -B build -DCMAKE_BUILD_TYPE=Release


cmake --build build


cmake -B build -DCMAKE_BUILD_TYPE=Debug -DWireLab_ENABLE_UNIT_TESTING=ON
cmake --build build


cd build && ctest -C Debug -VV
```

## Running

### Start VSwitch

```bash
./build/vswitch 8080
```

The VSwitch will bind to port 8080 and start listening for VPort connections.

### Start VPort

```bash

sudo ./build/vport <VSWITCH_IP> <PORT> [TAP_DEVICE_NAME]
```

Example:
```bash
sudo ./build/vport 192.168.1.100 8080 tap0
```

### Configure TAP Device

After VPort creates the TAP device:

```bash

sudo ip addr add 10.1.1.101/24 dev tap0
sudo ip link set tap0 up


sudo ifconfig utun6 inet 10.1.1.101 netmask 255.255.255.0
sudo ifconfig utun6 up
```

## Testing

### Manual Test

1. **Terminal 1**: Start VSwitch
   ```bash
   ./build/vswitch 8080
   ```

2. **Terminal 2**: Start VPort 1
   ```bash
   sudo ./build/vport 127.0.0.1 8080

   ```

3. **Terminal 3**: Start VPort 2
   ```bash
   sudo ./build/vport 127.0.0.1 8080

   ```

4. **Terminal 4**: Configure interfaces
   ```bash

   sudo ip addr add 10.1.1.101/24 dev tap0
   sudo ip addr add 10.1.1.102/24 dev tap1
   

   sudo ip link set tap0 up
   sudo ip link set tap1 up
   ```

5. **Test connectivity**
   ```bash
   ping 10.1.1.102
   ```

You should see:
- VSwitch learning MAC addresses from VPorts
- Frame forwarding logs in VSwitch
- Successful ping between VPorts

## Components

### VSwitch

- Listens on UDP port for incoming frames
- Learns MAC addresses from source MACs
- Forwards unicast frames to known destinations
- Broadcasts frames to all known endpoints (except source)
- Discards unknown unicast frames

### VPort

- Creates TAP device (virtual network interface)
- Connects to VSwitch via UDP
- Two forwarder threads:
  - TAP → VSwitch: Reads frames from TAP, sends to VSwitch
  - VSwitch → TAP: Receives frames from VSwitch, writes to TAP

## Troubleshooting

### TAP device creation fails

**Error**: `Failed to create TAP device`

**Solution**: 
- Ensure running with root/sudo privileges
- On macOS, install tuntap: `brew install --cask tuntap`
- Check TAP module is loaded: `lsmod | grep tap`

### Port already in use

**Error**: `Failed to bind socket`

**Solution**: Choose a different port or check what's using the port:
```bash

lsof -i :8080

lsof -i :8080
```

### No frames forwarded

**Check**:
- VSwitch and VPort logs show frames being sent/received
- TAP device is configured with IP address
- Network routes are set up correctly

## Project Structure

```
├── include/wirelab/
│   ├── expected.hpp          # std::expected implementation
│   ├── joining_thread.hpp    # RAII thread wrapper
│   ├── sys_utils.hpp          # System utilities
│   ├── tap_device.hpp         # TAP device management
│   ├── ethernet_frame.hpp     # Ethernet frame parsing
│   ├── udp_socket.hpp         # UDP socket wrapper
│   ├── vport.hpp              # Virtual Port
│   ├── mac_table.hpp          # MAC address table
│   └── vswitch.hpp            # Virtual Switch
├── src/
│   └── [implementation files]
├── test/src/
│   └── [test files - 113 tests total]
├── build/
│   ├── vport                  # VPort executable
│   └── vswitch                # VSwitch executable
```

## Modern C++ Features

- **C++17**: All code uses C++17 standard
- **RAII**: Automatic resource management (no memory leaks)
- **std::expected**: Type-safe error handling
- **joining_thread**: RAII thread wrapper (auto-join)
- **Move-only semantics**: Prevents resource duplication
- **Thread-safe**: Uses std::shared_mutex for concurrent access

## Performance

- **Zero-copy**: Frame data moved efficiently
- **Lock-free reads**: std::shared_mutex allows concurrent reads
- **Efficient forwarding**: O(1) MAC lookup with std::unordered_map

## License

[Your License Here]

## Contributing

[Contributing Guidelines Here]

