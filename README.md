# PacketSwitch

<img width="1934" height="907" alt="Untitled-2026-08-13-0218" src="https://github.com/user-attachments/assets/0f61eb0d-bd69-4111-8d11-18adf71003a2" />

## Demo
https://github.com/user-attachments/assets/72ed8775-c435-4837-953a-ee813c81396f

Thread-safe Virtual Network Switch written in C++, briding TAP devices over UDP

PacketSwitch implements a learning Ethernet switch as two cooperating programs: 
1. **VSwitch**, a forwarding server to connect devices to (like a router or a switch in real life), and 
2. **VPort**, a client that attaches to the switch via a TAP interface. 

Frames sent from one VPort's TAP device are forwarded over UDP to the correct VPort on the other side, or broadcast when the destination is unknown, reproducing the behavior of a physical switch in software.

The project prioritizes modern C++ idioms: RAII resource management, `std::shared_mutex` for concurrent MAC table reads, and a C++17 `expected<T,E>` type for explicit error handling without exceptions.

## Table of Contents

- [Background](#background)
- [Install](#install)
- [Usage](#usage)
- [Architecture](#architecture)
- [Testing](#testing)
- [Maintainers](#maintainers)
- [Contributing](#contributing)
- [License](#license)

## Background

Virtual switches are a foundational primitive in containerized and virtualized networking — used extensively in tools like Open vSwitch, Docker bridge networks, and hypervisor networking. This project builds one from scratch to develop a working understanding of:

- Layer-2 Ethernet frame parsing and MAC address learning
- TAP device programming on Linux and macOS
- UDP socket multiplexing across multiple virtual ports
- Thread-safe shared state in a concurrent forwarding path

The design intentionally avoids third-party networking libraries so that every abstraction — `TapDevice`, `UdpSocket`, `EthernetFrame`, `MacTable` — is visible and auditable in the source.

## Install

### Dependencies

| Dependency | Minimum version | Notes |
|---|---|---|
| CMake | 3.15 | Build system |
| GCC or Clang | GCC 10 / Clang 12 | C++17 required |
| Linux kernel | Any modern | TAP device support; macOS works via `utun` |
| Docker | 20.x | Required only for containerized tests |

GoogleTest is fetched automatically by CMake via `FetchContent` — no manual installation needed.

### Build with CMake

```bash
git clone https://github.com/teddymalhan/PacketSwitch.git
cd PacketSwitch
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

Binaries are placed at `build/vswitch` and `build/vport`.

### Build with Make

```bash
make
make test
make format
make docs
make install
```

### Docker (recommended for first-time testing)

```bash
docker build -t packetswitch -f tests/Dockerfile .
chmod +x tests/test_in_docker.sh
./tests/test_in_docker.sh
```

The Docker path spins up VSwitch and two VPorts inside a single container, configures TAP devices, runs a connectivity test, and prints forwarding logs — no host configuration required.

### Live demo

For a recording-friendly demo with data visibly flowing between separate
processes, run:

```bash
make demo
```

This starts a persistent Docker demo stack and opens four Otty windows:

1. **VSwitch** — live MAC learning, broadcast, and unicast forwarding logs.
2. **VPort 1** — frames entering and leaving endpoint `10.1.1.101`.
3. **VPort 2** — frames entering and leaving endpoint `10.1.1.102`.
4. **Live Traffic** — continuous bidirectional pings between both endpoints.

The stack stays running when a terminal window closes. Stop and remove all
demo resources after recording:

```bash
make demo-stop
```

Docker must be running. The containers own their TAP devices, so the host does
not need `sudo` or network configuration. To operate the stack without opening
windows, use `make demo-start`, `make demo-status`, and `make demo-stop`.

## Usage

### Running the switch

Open three terminals. VPort requires root to create TAP devices.

```bash

./build/vswitch 8080


sudo ./build/vport 127.0.0.1 8080


sudo ./build/vport 127.0.0.1 8080
```

### Configure TAP devices

After each VPort starts it prints the name of the TAP device it created (e.g. `tap0`, `tap1`). Assign IP addresses and bring the interfaces up:

```bash

sudo ip addr add 10.1.1.101/24 dev tap0
sudo ip link set tap0 up


sudo ip addr add 10.1.1.102/24 dev tap1
sudo ip link set tap1 up
```

### Test connectivity

```bash
ping 10.1.1.102
```

VSwitch logs show MAC learning events and per-frame forwarding decisions. Both VPorts shut down cleanly on `Ctrl-C`.

### CLI reference

```
vswitch <port>
  port   UDP port to listen on (0 = ephemeral)

vport <vswitch_ip> <vswitch_port> [tap_name]
  vswitch_ip    IP address of the VSwitch host
  vswitch_port  UDP port VSwitch is listening on
  tap_name      Optional TAP device name (default: assigned by OS)
```

## Architecture

The three core components map directly to source files under `include/project/` and `src/`:

| Component | Role |
|---|---|
| `VSwitch` | Receives UDP frames from all VPorts; looks up destination MAC in `MacTable`; unicasts or broadcasts |
| `VPort` | Reads raw Ethernet frames from a TAP device and forwards them to VSwitch over UDP; writes frames received from VSwitch back to the TAP device |
| `MacTable` | Thread-safe MAC-to-endpoint mapping; uses `std::shared_mutex` so concurrent reads do not block each other |

See [`docs/architecture-overview.md`](docs/architecture-overview.md) for Mermaid class and sequence diagrams.

## Testing

### Unit tests

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DProject_ENABLE_UNIT_TESTING=ON
cmake --build . --parallel
ctest --output-on-failure
```

There are 113+ unit tests across 9 suites covering `EthernetFrame`, `MacTable`, `TapDevice`, `UdpSocket`, `JoiningThread`, `Expected`, and integration scenarios.

### AddressSanitizer

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DProject_ENABLE_ASAN=ON
cmake --build . --parallel
ctest
```

### Code coverage

```bash
make coverage
```

### End-to-end (Docker)

```bash
./tests/test_in_docker.sh
```

See [`tests/TESTING.md`](tests/TESTING.md) for the full testing guide and [`tests/QUICK_START.md`](tests/QUICK_START.md) for a five-minute walkthrough.

## Maintainers

[@teddymalhan](https://github.com/teddymalhan)

## Contributing

Questions and bug reports are welcome via [GitHub Issues](../../issues).

Pull requests are accepted. Please:

1. Run `make format` before submitting (clang-format, Google style).
2. Add or update unit tests for any changed behavior.
3. Ensure `ctest` passes with AddressSanitizer enabled.

See [CONTRIBUTING.md](CONTRIBUTING.md) for the full contribution guide.

## License

[MIT](LICENSE) © 2025 Teddy Malhan
