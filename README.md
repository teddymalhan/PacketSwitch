# WireLab

<img width="1934" height="907" alt="Untitled-2026-08-13-0218" src="https://github.com/user-attachments/assets/0f61eb0d-bd69-4111-8d11-18adf71003a2" />

## Demo
https://github.com/user-attachments/assets/72ed8775-c435-4837-953a-ee813c81396f

Thread-safe Virtual Network Switch written in C++, briding TAP devices over UDP

WireLab implements a learning Ethernet switch as two cooperating programs: 
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
git clone https://github.com/teddymalhan/WireLab.git
cd WireLab
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

Binaries are placed at `build/vswitch` and `build/vport`.

### Native Windows (no Docker)

Requirements: Windows 10/11 x64, CMake 3.21 or newer, Visual Studio with the
Desktop development with C++ workload, and (for `vport`) an OpenVPN
TAP-Windows6 adapter. `vswitch` and the loopback test suite do not require the
adapter or administrator rights.

From PowerShell:

```powershell
cmake -S . -B out/build/windows -A x64 `
  -DWireLab_ENABLE_UNIT_TESTING=ON `
  -DWireLab_ENABLE_CCACHE=OFF
cmake --build out/build/windows --config Release --parallel
ctest --test-dir out/build/windows -C Release --output-on-failure
```

The executables are written to `out/build/windows/bin/Release`.

Install the TAP component from the
[OpenVPN Community distribution](https://openvpn.net/community-downloads/),
then create a dedicated adapter from an elevated PowerShell:

```powershell
& "$env:ProgramFiles\OpenVPN\bin\tapctl.exe" create --name "WireLab TAP"
.\out\build\windows\bin\Release\vport.exe 127.0.0.1 8080 "WireLab TAP"
```

`vport` accepts the TAP connection name or adapter GUID. If exactly one
TAP-Windows6 adapter is installed, the selector may be omitted. Wintun is not
supported because it transports Layer-3 packets; WireLab forwards complete
Ethernet frames.

### Visual Studio

Open `WireLab.slnx` rather than opening an individual source file. The
default `WireLab` startup project builds and runs `vswitch.exe` on UDP
port 8080 when you select **Debug > Start Without Debugging** (`Ctrl+F5`).

The solution also contains `VPort`. Set it as the startup project only after
`vswitch` is running and a TAP-Windows6 adapter is available. Its default
arguments are `127.0.0.1 8080`; set the adapter connection name under
**Project > Properties > Debugging > Command Arguments** when more than one
TAP adapter is installed.

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
docker build -t wirelab -f tests/Dockerfile .
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

### Replaying a capture

`wirelab_pcap` runs a packet capture through the same analysis, detection, and
policy pipeline the live switch uses, then writes a pcapng whose per-packet
comments carry WireLab's verdict. Because the comment is a standard pcapng
option, Wireshark shows it with no plugin and no sidecar file.

```bash
wirelab_pcap capture.pcap --out annotated.pcapng --broadcast 20 --port-scan 5
```

```
Read 531 packets from capture.pcap (microsecond timestamps, snaplen 32767)

Analysis
  received       531 packets, 78623 bytes
  broadcast      17
  ...

Detection
  1 anomaly events, 1 policy decisions
  port-scan 1
  rule mirror-port-scan 1

Wrote 531 packets to annotated.pcapng (50 carry an anomaly verdict)
```

Each annotated packet reads, in Wireshark's packet comment:

```
WireLab: known-unicast, valid, proto=6, 86.66.0.227:80 -> 10.251.23.139:35383, flow=0x0c2a08860549415c
ANOMALY port-scan: 8 destinations > threshold 5 in 1000 ms, source 86.66.0.227
POLICY mirror-port-scan -> mirror
```

Both classic libpcap and pcapng inputs are accepted, in either byte order and
at microsecond or nanosecond resolution, so the tool can reread its own output.
Only Ethernet captures load. Add `--only-flagged` to export just the packets
that carry a verdict; run `wirelab_pcap` with no arguments for every threshold
flag.

## Architecture

The three core components map directly to source files under `include/wirelab/` and `src/`:

| Component | Role |
|---|---|
| `VSwitch` | Receives UDP frames from all VPorts; looks up destination MAC in `MacTable`; unicasts or broadcasts |
| `VPort` | Reads raw Ethernet frames from a TAP device and forwards them to VSwitch over UDP; writes frames received from VSwitch back to the TAP device |
| `MacTable` | Thread-safe MAC-to-endpoint mapping; uses `std::shared_mutex` so concurrent reads do not block each other |

The WireLab analysis and control plane adds a closed loop on top of that dataplane:

| Component | Role |
|---|---|
| `PacketAnalyzer` | Parses a batch of frames into per-packet metadata, histograms, and flow records (CPU, CUDA, or Metal backend) |
| `AnomalyDetector` | Windowed detection of broadcast storms, MAC flaps, unknown-unicast floods, UDP floods, port scans, hot talkers, and malformed frames |
| `PolicyEngine` | Matches detected anomalies against ordered user rules and produces decisions (`Drop`, `RateLimit`, `Mirror`, `Quarantine`) |
| `PolicyEnforcer` | Applies each decision as a *leased, reversible* fault on the offending port via `TopologyController`, and releases it when the lease expires |
| `ControlService` | Publishes anomalies, policy decisions, and enforcement as revisioned events over the versioned control protocol |
| `PcapCapture` / `PcapNgWriter` | Reads classic pcap and pcapng captures zero-copy, and writes pcapng carrying WireLab's verdict as a per-packet comment |

Enforcement is genuinely closed-loop: a quarantined port stops forwarding frames because the enforcer installs a real `FaultConfiguration` the topology controller already honours, and the port recovers automatically once the lease lapses. Rules, enforced ports, and the enforcement log are editable and observable from the GUI's **Policies** workspace.

See [`docs/architecture-overview.md`](docs/architecture-overview.md) for Mermaid class and sequence diagrams.

## Testing

### Unit tests

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DWireLab_ENABLE_UNIT_TESTING=ON
cmake --build . --parallel
ctest --output-on-failure
```

There are 113+ unit tests across 9 suites covering `EthernetFrame`, `MacTable`, `TapDevice`, `UdpSocket`, `JoiningThread`, `Expected`, and integration scenarios.

### AddressSanitizer

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DWireLab_ENABLE_ASAN=ON
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
