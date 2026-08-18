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
vswitch <port> [--verbose] [--topology <file>] [--control-port <port>] [--control-address <address>] [--analyzer <backend>]
  port                       UDP port to listen on (0 = ephemeral)
  --verbose                  Log every forwarding decision
  --topology <file>          Analyse and police forwarded traffic against this topology
  --control-port <port>      Serve the control protocol on this TCP port (requires --topology)
  --control-address <addr>   Bind the control channel somewhere other than 127.0.0.1
  --analyzer <backend>       Parse supervised frames with cpu, cuda, metal or metal-live
                             (requires --topology; default cpu)

vport <vswitch_ip> <vswitch_port> [tap_name]
  vswitch_ip    IP address of the VSwitch host
  vswitch_port  UDP port VSwitch is listening on
  tap_name      Optional TAP device name (default: assigned by OS)
```

### Supervised switching

Started with `--topology`, the switch runs every forwarded frame through the
same `AnalysisPipeline` the GUI and `wirelab_pcap` use. Senders are bound to the
topology's host ports in first-seen order; each frame is recorded for batched
analysis and checked against its port's current fault *before* it is learned or
forwarded.

```bash
./build/vswitch 8080 --verbose --topology scenarios/security-lab.yaml
```

A port whose traffic trips a policy is quarantined for real: the enforcer
installs an `isolated` fault, and the switch stops learning from and forwarding
that port's frames until the lease lapses, at which point the operator's own
fault configuration is restored and traffic resumes on its own.

```
  [Blocked] ingress fault
```

Faults that delay or duplicate rather than drop are honoured too: deferred
copies are queued and sent when they come due, which is why the receive loop
waits with a deadline instead of blocking inside `recvfrom`.

Frame parsing itself is a choice: `--analyzer metal-live` moves it onto the GPU
for live switched traffic.

```bash
./build/vswitch 8080 --topology scenarios/security-lab.yaml --analyzer metal-live
```

Detection does not move with it. The batch a tick recorded is still answered
inside that tick, because a lease measured in seconds and a detection window
measured in seconds have to mean the same seconds; deferring a batch to the next
tick to win latency would make containment arrive late. A backend that is
compiled in but has no device is refused rather than quietly run on the CPU.

### Control channel

`--control-port` puts the switch's control plane on a TCP socket, so a GUI, a
script or an operator can drive a running switch and watch what it decides.
The framing is one control message per line: requests and replies in the JSON
`control_protocol.hpp` already defines, with a reply going to the client that
asked and every event broadcast to all of them.

```bash
./build/vswitch 8080 --topology scenarios/security-lab.yaml --control-port 9090
```

```jsonc
// out: a policy contains a port, and every connected client is told
{"event":"anomaly_detected","anomaly":{"type":"broadcast_storm","ingress_port":0, ...}}
{"event":"policy_action","rule_name":"quarantine-broadcast-storm","action":"quarantine", ...}
{"event":"fault_state_changed","first_endpoint":"client-a","active":true, ...}
{"event":"supervision_state","analysed_frames":302,"blocked_frames":21,
 "bindings":[{"port_id":"client-a","endpoint":"127.0.0.1:53124"}]}

// in: the operator ends the containment early
{"api_version":1,"request_id":"clear-1","command":"clear_port_fault",
 "topology_revision":1,"parameters":{"port_id":"client-a"}}
```

`supervision_state` reports which client the switch decided owns which topology
port, because a UDP dataplane offers no stable identity and the binding is a
guess worth showing rather than hiding. It is republished only when it moves,
so a client that joined late asks for it instead of waiting:

```jsonc
// in
{"api_version":1,"request_id":"supervision-1","command":"get_supervision_state","topology_revision":1}
// out: the reply to the asker, then the state broadcast to everyone
{"api_version":1,"request_id":"supervision-1","accepted":true,"topology_revision":1,
 "operation_id":"supervision-state-7"}
{"event":"supervision_state","analysed_frames":302,"blocked_frames":21, ...}
```

Every reply carries `topology_revision`, including a rejection. That is what a
client that lost its connection uses to come back: `ControlClient::reconnect()`
dials the switch again and `resync()` re-asks for switch state, active faults
and supervision state, adopting the revision the switch answers with. A command
still has to carry the current revision, so a client that missed a topology
reload is refused rather than allowed to fault a port that has moved.

The server is polled from the switch's own receive loop rather than from a
thread of its own, so a control client can never interleave with a frame being
forwarded. Nothing it does blocks: replies to a client that has stopped reading
are queued, and the client is disconnected on the next poll once that queue
passes `ControlServerConfig::max_pending_bytes`, so a stalled operator console
cannot stall forwarding.

The control channel is unauthenticated, and it binds loopback for that reason:
anyone who can reach it can quarantine a port or blackhole a link.
`--control-address` exists for a lab where the GUI is on another machine, and
the switch says so on startup when it is used. Do not put it on a network you
would not hand a root shell to.

### Benchmarks over the control channel

`start_benchmark` runs the same analyzer benchmark `wirelab_bench` runs, on the
running switch, and reports it as events rather than as CLI output. The run is
advanced in slices from the same poll that serves control clients, at most
`ControlServerConfig::benchmark_packets_per_poll` packets per poll, so a
benchmark a client asked for never stops the switch from forwarding.

```jsonc
// in: 200k mixed frames through the CPU analyzer, in batches of 64
{"api_version":1,"request_id":"bench-1","command":"start_benchmark","topology_revision":1,
 "parameters":{"scenario":"mixed-traffic","backend":"cpu","batch_size":64,
               "seed":7,"packets":200000,"frame_size":128,"duration_seconds":60}}

// out: the reply names the operation, then progress until the run finishes
{"api_version":1,"request_id":"bench-1","accepted":true,"topology_revision":1,
 "operation_id":"benchmark-1"}
{"event":"benchmark_progress","operation_id":"benchmark-1",
 "completed_packets":0,"total_packets":200000}
{"event":"benchmark_progress","operation_id":"benchmark-1","completed_packets":4096, ...}
{"event":"benchmark_result","operation_id":"benchmark-1","completed":true,
 "result":{"backend":"cpu","packets_per_second":157746.8,"loss_percentage":0.0,
           "batch_analysis_latency_p99_ns":156292, "timing":{"kernel_ns":0, ...}, ...}}
```

`stop_run` ends an active run early and still publishes a `benchmark_result`,
with `completed:false` and the counters measured so far: partial numbers are
the reason to stop a run rather than abandon it. A second `start_benchmark`
while one is running is refused, because two runs sharing this thread would
each measure the other. The backend names this build accepts are the ones
`wirelab_bench` accepts - `cuda` and `metal` exist only in a build configured
with `-DWIRELAB_ENABLE_CUDA=ON` or `-DWIRELAB_ENABLE_METAL=ON`, and a backend
that is compiled in but has no device is refused rather than run on the CPU.

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

### Traffic scenarios

The generator is a pure function of `(scenario, seed, sequence)`: frame N is
derived from a splitmix64 stream re-seeded per frame, so the same seed
reproduces the same bytes, a run can resume mid-stream, and a GPU can produce
frame N without producing frame N-1.

| `--scenario` | What it sends | What it trips |
|---|---|---|
| `known-unicast` | host to known host | nothing; this is the baseline |
| `broadcast` | broadcast MAC | nothing at benign rates |
| `unknown-unicast` | unlearned destination MACs | nothing at benign rates |
| `mixed-traffic` | the three above, round-robin | nothing |
| `udp-flood` | one source to one destination, UDP port 9000 | `udp_flood` |
| `port-scan` | one source sweeping 1024 destination ports | `port_scan` |
| `broadcast-storm` | broadcast from three sources | `broadcast_storm` |

The last three exist so the detectors and policies can be exercised end to end
rather than only against hand-built test frames: point a `broadcast-storm` run
at a supervised switch and the `quarantine-broadcast-storm` policy contains the
port that sent it.

### GPU traffic generation

`--generator metal` synthesises a whole batch of frames on the GPU, one thread
per frame, using the same per-frame derivation the CPU generator uses. That is
the contract: `traffic_source_test` asserts GPU bytes equal CPU bytes frame for
frame across scenarios, batches, and a non-zero starting sequence, so choosing
a generator changes who did the work and nothing about the workload.

```bash
wirelab_bench --analyzer cpu --generator metal --scenario mixed-traffic \
  --packets 4096 --batch-size 256 --frame-size 128 --seed 5
```

`metal` exists only in a build configured with `-DWIRELAB_ENABLE_METAL=ON`, and
the same wiring accepts `cuda` in a `-DWIRELAB_ENABLE_CUDA=ON` build. A
generator that is compiled in but has no device is refused rather than quietly
run on the CPU. `start_benchmark` accepts `"generator"` over the control
channel with the same rules.

### Pipelined GPU analysis

`--analyzer metal` submits a batch and waits for it. The host is then idle for
the whole kernel, which is affordable offline and not affordable on a live
switch. `--analyzer metal-live` keeps the same kernel and changes when the
caller blocks: buffers are allocated once and reused, and up to three batches
are in flight at a time, so the host fills batch N+1 while the GPU runs batch N.

```bash
wirelab_bench --analyzer metal-live --generator cpu --scenario mixed-traffic \
  --packets 20000 --batch-size 256 --frame-size 128 --seed 5
```

On an M-series Mac at 20k mixed frames, batch 256: 515k pps synchronous,
**2.35M pps pipelined**, with identical counters. The counters matching is the
point — `metal_packet_parser_test` asserts the pipelined path agrees with the
CPU analyzer frame for frame, including under a different batch slicing, so
choosing a backend changes who did the work and nothing about the answer.

Results come back in submission order even though the ring completes them
independently, because the aggregator downstream learns MAC addresses as it
goes: a batch that overtook another would disagree about what was already
known. An empty tick takes a ring slot with no work rather than skipping the
ring, so it cannot arrive ahead of traffic that preceded it.

Two caveats worth stating plainly:

- Apple's unified memory means shared-storage buffers *are* the transfer; there
  is no staging copy to overlap the way pinned host memory overlaps on CUDA.
  What overlaps here is host fill against GPU execution.
- `kernel_ns` is measured from the device clock on this path and from the host
  clock around `waitUntilCompleted` on the synchronous one, so the two
  backends' `kernel_ns` are not directly comparable. Compare
  `packets_per_second`, or `transfer_inclusive_ns`, which spans submit to
  collect for the whole batch.

The CUDA analyzer remains synchronous: it has no pinned-buffer or stream
pipeline, because this project has no CUDA hardware to measure one on, and an
unmeasured optimisation is a guess. The interface it would implement
(`StreamingPacketAnalyzer`) is backend-agnostic and already in place.

### Reports and the demo

```bash
make bench-report    # every backend and generator this build has, one report
make demo-gui        # the five-minute scripted GUI walkthrough
```

`scripts/bench_report.sh` runs the analyzer-by-generator matrix at a pinned
scenario, seed, frame size and batch size, and writes `<base>.json` and
`<base>.csv`. It discovers the matrix by running each combination once rather
than by trusting the build flags, so an absent device is reported as absent.
The columns are the GUI Reports workspace's export columns, so a report from CI
and a report exported from the frontend diff against each other directly.

The Reports workspace runs the same measurement in the GUI, in slices off the
frame tick so the window keeps drawing, and its export carries provenance:
scenario, seed, packet and batch and frame size, WireLab version, build type,
which backends this build compiled in, and which are present on the machine.
Transfer and kernel timing are separate columns because a GPU number that hides
the copy is not a result - in a Debug build the CPU usually wins, and the table
says so. Two further columns, `transferInclusiveNs` and `queueWaitNs`, are
non-zero only for a pipelined backend, where the caller never blocked on a batch
and the host clock around the call is therefore not that batch's latency.

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
| `AnalysisPipeline` | The single detection -> policy -> enforcement seam every consumer runs: the GUI, `wirelab_pcap`, and `ControlService` all evaluate batches through one of these rather than rewiring the three stages themselves |
| `SwitchSupervisor` | Binds live senders to topology ports, batches their frames into the pipeline, and gates the switch's forwarding on the resulting faults |
| `ControlService` | Turns switch state, topology commands, anomalies, policy decisions and enforcement into revisioned events over the versioned control protocol, and answers `get_supervision_state` from whatever supervision source it was given |
| `ControlServer` / `ControlClient` | Carries that protocol over TCP as newline-delimited JSON, polled from the switch's receive loop so the control plane never blocks forwarding; the client reconnects and resynchronises against the revision the switch reports |
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
