# WireLab


https://github.com/user-attachments/assets/e53b113a-df3d-41a7-ae93-054d985b4217


[![standard-readme compliant](https://img.shields.io/badge/readme%20style-standard-brightgreen.svg?style=flat-square)](https://github.com/RichardLitt/standard-readme)

> A network simulation control plane for fault injection and for policy protection against attack traffic.

WireLab simulates a Layer 2 network. You can cause failures in this network on purpose. You can then see how policies protect the network.

Operate WireLab in five steps:

1. Make a topology of hosts, switches, and links.
2. Inject a fault on a port. A fault can drop, delay, duplicate, or isolate the traffic of that port.
3. Send attack traffic through the network. WireLab makes broadcast storms, UDP floods, and port scans.
4. Look at the detection and policy engine. The engine puts the port that sends the attack traffic in quarantine.
5. Wait for the lease of the quarantine to expire. The port then operates again without operator action.

The frontend is `wirelab-desktop`. This is an application in Rust and [GPUI](https://www.gpui.rs/). The application links the C++ simulation core in the same process. It uses a C ABI with a frozen version number. Packet analysis operates on the CPU, on CUDA, or on Metal. The backend changes the machine that does the work. The backend does not change the result. The tests compare the results of the backends frame by frame.

A dataplane operates below the control plane. This dataplane is not a model. It contains two programs:

1. **VSwitch** is a forwarding server that learns Ethernet addresses.
2. **VPort** is a client. VPort connects to VSwitch with a TAP interface and UDP.

The GUI, the CLI tools, and the tests use the same `Session` object. Thus a value on the screen is equal to the value that `wirelab_bench` calculates with the same seed.

Repository: <https://github.com/teddymalhan/wirelab>

## Table of Contents

- [Security](#security)
- [Background](#background)
- [Install](#install)
- [Usage](#usage)
- [Demo](#demo)
- [Architecture](#architecture)
- [Testing](#testing)
- [API](#api)
- [Maintainers](#maintainers)
- [Contributing](#contributing)
- [License](#license)

## Security

The control channel (`--control-port`) has no authentication. Thus the switch binds the control channel to `127.0.0.1`.

A person who has access to the control channel can put a port in quarantine. That person can also stop all traffic on a link.

Use `--control-address` only in a laboratory where the GUI operates on a different machine. The switch shows a warning at startup when you use this option. Do not make the control channel available on a network that you do not control fully.

The `vport` program must have root permissions to make TAP devices. On Windows, start `vport` from an elevated shell. The `vswitch` program does not need these permissions. Do not start `vswitch` with root permissions.

## Background

Usually you can examine fault injection and policy responses only on hardware that you must not break. The other usual method is a simulator that replaces packets with abstractions. WireLab uses different methods. WireLab moves real Ethernet frames through a real software switch. WireLab also shows the full sequence on one screen: topology, fault, attack traffic, detection, policy, enforcement, and recovery.

Three conditions are necessary at the same time:

- **A real dataplane.** WireLab does Layer 2 frame analysis, MAC address learning, TAP device control, and UDP multiplexing across virtual ports. The data that the forwarding path shares is safe for concurrent threads.
- **Sufficiently fast analysis.** WireLab analyses packets in batches on the CPU, on CUDA, or on Metal. The GPU paths use a pipeline. Thus the host fills batch N+1 while the GPU calculates batch N.
- **A closed enforcement loop.** A policy decision installs a fault that the switch obeys. A policy decision is not only a log entry. The enforcer removes the fault when the lease expires.

WireLab does not use third-party network libraries. Thus you can read and examine each component in the source code: `TapDevice`, `UdpSocket`, `EthernetFrame`, `MacTable`, and `PolicyEnforcer`.

## Install

### Dependencies

| Dependency | Minimum version | Notes |
|---|---|---|
| CMake | 3.15 | Build system |
| GCC or Clang | GCC 10 / Clang 12 | C++17 is necessary |
| Linux kernel | Any recent version | TAP device support. macOS uses `utun` |
| Docker | 20.x | Necessary only for tests in a container |
| Rust toolchain | 1.75 (stable) | Necessary only for the desktop application (`-DPROJECT_BUILD_DESKTOP=ON`) |
| CUDA Toolkit | 12.x | Optional. Makes `--analyzer cuda` available (`-DWIRELAB_ENABLE_CUDA=ON`) |
| Xcode and Metal | macOS 13 or later | Optional. Makes `--analyzer metal` and `metal-live` available (`-DWIRELAB_ENABLE_METAL=ON`) |

CMake gets GoogleTest automatically with `FetchContent`. Do not install GoogleTest manually.

### Build with CMake

```bash
git clone https://github.com/teddymalhan/wirelab.git
cd wirelab
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

CMake writes the programs to `build/vswitch` and `build/vport`.

### Build the desktop application

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPROJECT_BUILD_DESKTOP=ON
cmake --build build --target wirelab-desktop
```

To compile the GPU analyzers into the same program, add one option. On macOS, add `-DWIRELAB_ENABLE_METAL=ON`. On Linux with an NVIDIA GPU, add `-DWIRELAB_ENABLE_CUDA=ON`.

A Rust toolchain is necessary for this build. The `PROJECT_BUILD_DESKTOP` option is off by default. Thus a build without Rust is not different from an earlier C++ build.

CMake is the primary build system. CMake compiles the static libraries. CMake then gives the directories of these libraries to cargo. On macOS, CMake also makes the `WireLab.app` bundle around the program. Thus the application has a correct name and icon.

macOS is the supported platform. Support for Linux is limited.

### Native Windows (no Docker)

These items are necessary:

- Windows 10 or Windows 11, x64.
- CMake 3.21 or a later version.
- Visual Studio with the workload for desktop development in C++.
- An OpenVPN TAP-Windows6 adapter, but only for `vport`.

The `vswitch` program and the loopback test suite do not need the adapter. They also do not need administrator rights.

Start PowerShell. Then run these commands:

```powershell
cmake -S . -B out/build/windows -A x64 `
  -DWireLab_ENABLE_UNIT_TESTING=ON `
  -DWireLab_ENABLE_CCACHE=OFF
cmake --build out/build/windows --config Release --parallel
ctest --test-dir out/build/windows -C Release --output-on-failure
```

CMake writes the programs to `out/build/windows/bin/Release`.

Install the TAP component from the
[OpenVPN Community distribution](https://openvpn.net/community-downloads/).
Then start an elevated PowerShell and make an adapter:

```powershell
& "$env:ProgramFiles\OpenVPN\bin\tapctl.exe" create --name "WireLab TAP"
.\out\build\windows\bin\Release\vport.exe 127.0.0.1 8080 "WireLab TAP"
```

The `vport` program accepts the TAP connection name or the adapter GUID. If the computer has only one TAP-Windows6 adapter, you can omit this argument.

WireLab does not support Wintun. Wintun transmits Layer 3 packets, but WireLab forwards complete Ethernet frames.

### Visual Studio

Open `WireLab.slnx`. Do not open only one source file.

The default startup project is `WireLab`. This project builds `vswitch.exe` and starts it on UDP port 8080. To start it, select **Debug > Start Without Debugging** (`Ctrl+F5`).

The solution also contains `VPort`. Make `VPort` the startup project only after these two conditions are true:

- The `vswitch` program operates.
- A TAP-Windows6 adapter is available.

The default arguments of `VPort` are `127.0.0.1 8080`. If the computer has more than one TAP adapter, set the adapter connection name. Set it in **Project > Properties > Debugging > Command Arguments**.

### Build with Make

```bash
make
make test
make format
make docs
make install
```

### Docker (recommended for the first test)

```bash
docker build -t wirelab -f tests/Dockerfile .
chmod +x tests/test_in_docker.sh
./tests/test_in_docker.sh
```

The script starts VSwitch and two VPort programs in one container. The script also configures the TAP devices, does a connectivity test, and shows the forwarding logs. You do not configure the host.

### Live demo

For a demo with visible data flow between different processes, run this command:

```bash
make demo
```

The command starts a permanent Docker demo stack. The command also opens four Otty windows:

1. **VSwitch** shows the logs for MAC address learning, broadcast, and unicast forwarding.
2. **VPort 1** shows the frames of endpoint `10.1.1.101`.
3. **VPort 2** shows the frames of endpoint `10.1.1.102`.
4. **Live Traffic** sends continuous pings in the two directions.

The stack continues to operate after you close a terminal window. To stop the stack and remove all its resources, run this command:

```bash
make demo-stop
```

Docker must operate. The containers control their own TAP devices. Thus the host does not need `sudo` or a network configuration.

To operate the stack without windows, use `make demo-start`, `make demo-status`, and `make demo-stop`.

## Usage

Use the desktop application for usual operation. The CLI tools give access to the same core for scripts, for CI, and for runs without a display.

### Desktop application

```bash
open build/bin/WireLab.app          # macOS. Other systems: build/cargo/release/wirelab-desktop
```

The application has seven workspaces. All workspaces use one `Session` object.

| Workspace | Function |
|---|---|
| Dashboard | Shows the live counters of the simulation |
| Topology | Makes and changes hosts, switches, and links, and calculates the layout of the graph |
| Traffic | Sends a scenario (`known-unicast`, `broadcast-storm`, `udp-flood`, `port-scan`, and others) with a selected seed |
| Packets & Security | Shows the packet data, the flows, and the anomalies of each batch |
| Faults | Injects a drop, delay, duplicate, or isolate fault on a port, with a lease or without a lease |
| Policies | Sets the sequence of the rules, and shows the rule that operated, the controlled ports, and the enforcement log |
| Reports | Runs the benchmark matrix of analyzers and generators, and exports JSON and CSV data |

To see the primary function of the application, do these steps:

1. Make a topology.
2. Start an attack scenario in **Traffic**.
3. Look at the anomaly in **Packets & Security**.
4. Look at the quarantine of the related port in **Policies**.
5. Wait for the lease to expire. The port then operates again.

A fault from **Faults** and a fault from the enforcer use the same mechanism. Thus a manual failure and an automatic containment have the same behavior.

### Start the switch from the CLI

The `vport` program must have root permissions to make TAP devices.

Open three terminals. Then run one command in each terminal:

```bash
# terminal 1. The switch
./build/vswitch 8080

# terminal 2. The first endpoint
sudo ./build/vport 127.0.0.1 8080

# terminal 3. The second endpoint
sudo ./build/vport 127.0.0.1 8080
```

### Configure the TAP devices

Each VPort program shows the name of its TAP device at startup, for example `tap0` or `tap1`. Set an IP address on each device. Then set each device to the up state:

```bash
# The TAP device of terminal 2
sudo ip addr add 10.1.1.101/24 dev tap0
sudo ip link set tap0 up

# The TAP device of terminal 3
sudo ip addr add 10.1.1.102/24 dev tap1
sudo ip link set tap1 up
```

### Test the connectivity

```bash
ping 10.1.1.102
```

The VSwitch logs show the MAC address learning events. The logs also show the forwarding decision for each frame. The two VPort programs stop correctly when you push `Ctrl-C`.

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

To start the supervised mode, use the `--topology` option:

```bash
./build/vswitch 8080 --verbose --topology scenarios/security-lab.yaml
```

In this mode, the switch sends each forwarded frame through the `AnalysisPipeline` object. The GUI and `wirelab_pcap` use the same object.

The switch binds each sender to a host port of the topology. It uses the sequence of the first frame of each sender. The switch records each frame for the batch analysis. The switch also compares each frame with the current fault of its port. It does this comparison before it learns or forwards the frame.

If the traffic of a port agrees with a policy rule, the enforcer puts the port in quarantine. The enforcer installs an `isolated` fault. The switch then does not learn from the frames of that port. The switch also does not forward these frames.

```
  [Blocked] ingress fault
```

When the lease expires, the switch restores the fault configuration of the operator. The traffic of the port then continues.

The switch also obeys faults that delay or duplicate frames. The switch puts the delayed copies in a queue. The switch sends each copy at its correct time. Thus the receive loop waits with a deadline. The receive loop does not stay in `recvfrom`.

The frame analysis can operate on the GPU. The `--analyzer metal-live` option moves the analysis of live traffic to the GPU:

```bash
./build/vswitch 8080 --topology scenarios/security-lab.yaml --analyzer metal-live
```

The detection stays on the CPU. The switch analyses the batch of a tick in the same tick. A lease and a detection window are both in seconds. Thus the two values must refer to the same seconds. If the switch analysed a batch in the next tick, the containment would occur too late.

The program can contain a backend for a device that is not available. In this condition, the switch refuses the backend. The switch does not use the CPU in place of the backend.

### Control channel

The `--control-port` option puts the control plane of the switch on a TCP socket. A GUI, a script, or an operator can then control a switch that operates. They can also see the decisions of the switch.

```bash
./build/vswitch 8080 --topology scenarios/security-lab.yaml --control-port 9090
```

The protocol has one message on each line. The requests and the replies use the JSON messages of `control_protocol.hpp`. The server sends a reply only to the client that sent the request. The server sends each event to all clients.

```jsonc
// out: a policy quarantines a port, and the server tells all clients
{"event":"anomaly_detected","anomaly":{"type":"broadcast_storm","ingress_port":0, ...}}
{"event":"policy_action","rule_name":"quarantine-broadcast-storm","action":"quarantine", ...}
{"event":"fault_state_changed","first_endpoint":"client-a","active":true, ...}
{"event":"supervision_state","analysed_frames":302,"blocked_frames":21,
 "bindings":[{"port_id":"client-a","endpoint":"127.0.0.1:53124"}]}

// in: the operator stops the containment before the lease expires
{"api_version":1,"request_id":"clear-1","command":"clear_port_fault",
 "topology_revision":1,"parameters":{"port_id":"client-a"}}
```

The `supervision_state` event shows the client that the switch selected for each topology port. A UDP dataplane has no permanent identity. Thus this relation is an estimate, and the switch makes the estimate visible.

The switch sends this event again only after a change. Thus a client that connects later must request the state:

```jsonc
// in
{"api_version":1,"request_id":"supervision-1","command":"get_supervision_state","topology_revision":1}
// out: first the reply to the client, then the state to all clients
{"api_version":1,"request_id":"supervision-1","accepted":true,"topology_revision":1,
 "operation_id":"supervision-state-7"}
{"event":"supervision_state","analysed_frames":302,"blocked_frames":21, ...}
```

Each reply contains `topology_revision`. A rejection also contains this value.

A client that lost its connection uses this value. The `ControlClient::reconnect()` function connects to the switch again. The `resync()` function then requests the switch state, the active faults, and the supervision state. The client uses the revision from the reply of the switch.

Each command must contain the current revision. Thus the switch refuses a command from a client that did not get the last topology change. Such a client cannot cause a fault on a port that moved.

The switch polls the server from its own receive loop. The switch does not use a different thread. Thus a control client cannot operate at the same time as a frame transmission.

No operation of the control channel stops the switch. The server puts the replies for a slow client in a queue. If this queue becomes larger than `ControlServerConfig::max_pending_bytes`, the server disconnects the client at the next poll. Thus a slow operator console cannot stop the forwarding.

The control channel has no authentication. Read [Security](#security) before you use `--control-address`.

### Benchmarks on the control channel

The `start_benchmark` command runs the analyzer benchmark of `wirelab_bench` on a switch that operates. The command reports the results as events, not as CLI output.

The switch advances the run in small parts. It uses the same poll that serves the control clients. The maximum number of packets for each poll is `ControlServerConfig::benchmark_packets_per_poll`. Thus a benchmark does not stop the forwarding.

```jsonc
// in: 200k mixed frames through the CPU analyzer, in batches of 64
{"api_version":1,"request_id":"bench-1","command":"start_benchmark","topology_revision":1,
 "parameters":{"scenario":"mixed-traffic","backend":"cpu","batch_size":64,
               "seed":7,"packets":200000,"frame_size":128,"duration_seconds":60}}

// out: first the reply with the operation name, then the progress of the run
{"api_version":1,"request_id":"bench-1","accepted":true,"topology_revision":1,
 "operation_id":"benchmark-1"}
{"event":"benchmark_progress","operation_id":"benchmark-1",
 "completed_packets":0,"total_packets":200000}
{"event":"benchmark_progress","operation_id":"benchmark-1","completed_packets":4096, ...}
{"event":"benchmark_result","operation_id":"benchmark-1","completed":true,
 "result":{"backend":"cpu","packets_per_second":157746.8,"loss_percentage":0.0,
           "batch_analysis_latency_p99_ns":156292, "timing":{"kernel_ns":0, ...}, ...}}
```

The `stop_run` command stops a run before its end. The server sends a `benchmark_result` event for this run. The event contains `completed:false` and the values of the counters at that time. These partial values are the reason to stop a run correctly.

The server refuses a second `start_benchmark` command during a run. Two runs on one thread would change the results of each other.

This build accepts the same backend names as `wirelab_bench`. The `cuda` and `metal` names are available only in a build with `-DWIRELAB_ENABLE_CUDA=ON` or `-DWIRELAB_ENABLE_METAL=ON`. If the device of a compiled backend is not available, the server refuses the backend. The server does not use the CPU in place of the backend.

### Replay of a capture

The `wirelab_pcap` program sends a packet capture through the analysis, detection, and policy pipeline of the live switch. The program then writes a pcapng file. Each packet in this file has a comment with the result of WireLab.

The comment is a standard pcapng option. Thus Wireshark shows the comment without a plugin and without a second file.

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

The packet comment in Wireshark has this format:

```
WireLab: known-unicast, valid, proto=6, 86.66.0.227:80 -> 10.251.23.139:35383, flow=0x0c2a08860549415c
ANOMALY port-scan: 8 destinations > threshold 5 in 1000 ms, source 86.66.0.227
POLICY mirror-port-scan -> mirror
```

The program reads classic libpcap files and pcapng files. It accepts the two byte orders. It accepts microsecond resolution and nanosecond resolution. Thus the program can read its own output again. The program loads only Ethernet captures.

To export only the packets with a result, add `--only-flagged`. To see all threshold options, run `wirelab_pcap` without arguments.

### Traffic scenarios

The generator is a pure function of the scenario, the seed, and the sequence number. The generator calculates frame N from a splitmix64 stream with a new seed for each frame. Thus the same seed gives the same bytes. A run can also continue from the middle of a stream. A GPU can calculate frame N before frame N-1.

| `--scenario` | Traffic | Effect |
|---|---|---|
| `known-unicast` | Host to known host | No anomaly. This is the reference |
| `broadcast` | Broadcast MAC address | No anomaly at low rates |
| `unknown-unicast` | Unknown destination MAC addresses | No anomaly at low rates |
| `mixed-traffic` | The three scenarios above, in sequence | No anomaly |
| `udp-flood` | One source to one destination, UDP port 9000 | `udp_flood` |
| `port-scan` | One source to 1024 destination ports | `port_scan` |
| `broadcast-storm` | Broadcast from three sources | `broadcast_storm` |

The last three scenarios test the detectors and the policies fully. Manual test frames cannot do this test. For example, send a `broadcast-storm` run to a supervised switch. The `quarantine-broadcast-storm` policy then quarantines the port that sends the traffic.

### Traffic generation on the GPU

The `--generator metal` option calculates a full batch of frames on the GPU with one thread for each frame. The GPU uses the same calculation as the CPU generator.

This condition is a contract. The `traffic_source_test` test compares the GPU bytes and the CPU bytes frame by frame. The test does this comparison for different scenarios, different batches, and a start sequence number that is not zero. Thus the generator changes the machine that does the work. The generator does not change the traffic.

```bash
wirelab_bench --analyzer cpu --generator metal --scenario mixed-traffic \
  --packets 4096 --batch-size 256 --frame-size 128 --seed 5
```

The `metal` generator is available only in a build with `-DWIRELAB_ENABLE_METAL=ON`. The `cuda` generator is available only in a build with `-DWIRELAB_ENABLE_CUDA=ON`. If the device of a compiled generator is not available, the program refuses the generator. The program does not use the CPU in place of the generator. The `start_benchmark` command accepts the `"generator"` parameter with the same conditions.

### GPU analysis with a pipeline

The `--analyzer metal` option sends a batch to the GPU and waits for the result. The host does no work during the kernel. This condition is acceptable for offline analysis. This condition is not acceptable for a live switch.

The `--analyzer metal-live` option uses the same kernel and changes the wait condition. The program allocates the buffers one time and uses them again. A maximum of three batches operate at the same time. Thus the host fills batch N+1 while the GPU calculates batch N.

```bash
wirelab_bench --analyzer metal-live --generator cpu --scenario mixed-traffic \
  --packets 20000 --batch-size 256 --frame-size 128 --seed 5
```

An Apple M series Mac gives these results for 20000 mixed frames with a batch size of 256: 515000 packets each second with the synchronous backend, and **2350000 packets each second with the pipeline**. The counters of the two backends are equal.

The equal counters are important. The `metal_packet_parser_test` test compares the pipeline path and the CPU analyzer frame by frame. The test also does this comparison with a different batch size. Thus the backend changes the machine that does the work. The backend does not change the result.

The program collects the results in the sequence of the submissions. The ring completes the batches independently, but the aggregator after the ring learns MAC addresses continuously. Thus a batch in an incorrect sequence would give incorrect knowledge of the addresses. A tick without traffic also uses a slot in the ring. Thus an empty tick cannot come before earlier traffic.

Two conditions are important:

- Apple systems have unified memory. Thus a buffer with shared storage is the transfer. There is no second copy for an overlap. CUDA systems can overlap a copy from pinned host memory. On this path, the host work and the GPU work overlap.
- The two backends measure `kernel_ns` in different ways. The pipeline path uses the device clock. The synchronous path uses the host clock around `waitUntilCompleted`. Thus you must not compare these two values. Compare `packets_per_second`. You can also compare `transfer_inclusive_ns`, which contains the full time from the submission to the collection of a batch.

The CUDA analyzer is synchronous. It has no pipeline with pinned buffers or streams. This project has no CUDA hardware for the measurement of such a pipeline. An optimisation without a measurement is only an estimate. The necessary interface (`StreamingPacketAnalyzer`) is independent of the backend, and the project contains it already.

### Reports

```bash
make bench-report    # one report for each backend and generator of this build
make demo-gui        # the scripted GUI demonstration of five minutes
```

The `scripts/bench_report.sh` script runs the matrix of analyzers and generators. The script uses the same scenario, seed, frame size, and batch size for each run. The script writes `<base>.json` and `<base>.csv`.

The script runs each combination one time to find the available matrix. The script does not use the build flags for this decision. Thus the report shows a device that is not available.

The columns of the report are the export columns of the Reports workspace. Thus you can compare a report from CI and a report from the frontend directly.

The Reports workspace does the same measurement in the GUI. It does the measurement in small parts between the frame ticks. Thus the window continues to operate.

The export contains this data:

- The scenario and the seed.
- The packet size, the batch size, and the frame size.
- The WireLab version and the build type.
- The backends of this build.
- The backends that are available on this machine.

The transfer time and the kernel time are different columns. A GPU value that does not contain the copy time is not a correct result. In a Debug build, the CPU is usually faster, and the table shows this result.

Two more columns, `transferInclusiveNs` and `queueWaitNs`, have a value only for a backend with a pipeline. On such a backend, the caller does not wait for a batch. Thus the host clock does not give the latency of that batch.

## Demo

This video shows the desktop frontend in Rust and GPUI. The frontend operates the C++ core across the FFI boundary. The video shows the topology editor, the traffic tick, the GPU analysis, and the policy enforcement.

<video src="docs/WireLab-Rust-GPUI-C%2B%2B-FFI.mp4" controls width="900"></video>

If the player does not operate, download
[`docs/WireLab-Rust-GPUI-C++-FFI.mp4`](docs/WireLab-Rust-GPUI-C%2B%2B-FFI.mp4).

To do the demonstration on your machine, use [Live demo](#live-demo) (`make demo`) for the dataplane stack in containers. For the scripted GUI demonstration, use `make demo-gui`.

## Architecture

The three primary components of the dataplane are in `include/wirelab/` and `src/`:

| Component | Function |
|---|---|
| `VSwitch` | Receives UDP frames from all VPort clients. Finds the destination MAC address in `MacTable`. Sends a unicast frame or a broadcast frame |
| `VPort` | Reads Ethernet frames from a TAP device and sends them to VSwitch with UDP. Writes the frames from VSwitch to the TAP device |
| `MacTable` | Keeps the relation between a MAC address and an endpoint. Uses `std::shared_mutex`. Thus concurrent read operations do not stop each other |

The analysis and control plane adds a closed loop above the dataplane:

| Component | Function |
|---|---|
| `PacketAnalyzer` | Analyses a batch of frames and gives packet data, histograms, and flow records. Uses the CPU, CUDA, or Metal backend |
| `AnomalyDetector` | Detects these anomalies in a time window: broadcast storms, MAC address flaps, unknown unicast floods, UDP floods, port scans, hosts with too much traffic, and incorrect frames |
| `PolicyEngine` | Compares the anomalies with the user rules in sequence and gives a decision: `Drop`, `RateLimit`, `Mirror`, or `Quarantine` |
| `PolicyEnforcer` | Applies each decision as a fault with a lease on the related port with `TopologyController`. Removes the fault when the lease expires |
| `AnalysisPipeline` | Contains the sequence detection, policy, enforcement. The GUI, `wirelab_pcap`, and `ControlService` use this component for their batches |
| `SwitchSupervisor` | Binds the live senders to the topology ports. Sends their frames to the pipeline in batches. Controls the forwarding of the switch with the resulting faults |
| `ControlService` | Changes the switch state, topology commands, anomalies, policy decisions, and enforcement into events with a revision number. Answers `get_supervision_state` from its supervision source |
| `ControlServer` and `ControlClient` | Send the protocol over TCP as JSON with one message on each line. The switch polls the server from its receive loop. Thus the control plane does not stop the forwarding. The client connects again and synchronises with the revision of the switch |
| `PcapCapture` and `PcapNgWriter` | Read classic pcap and pcapng captures without a copy. Write pcapng files with the result of WireLab in a packet comment |
| `Session` | Does the orchestration for all frontends: topology changes and layout, the traffic tick, the report state machine, the policy and fault commands, and the export to JSON, CSV, and YAML. The signals are a bitmask that the caller reads and clears. Thus `Session` needs no event loop, and the tests use it without one |
| `wirelab_ffi` | The C ABI with a frozen version that `wirelab-desktop` uses. Strings and telemetry cross the boundary as spans in session memory. These spans are valid until the next command that changes the session |

The enforcement loop is closed. A port in quarantine does not forward frames, because the enforcer installs a `FaultConfiguration` object that the topology controller obeys. The port operates again automatically when the lease expires. You can read and change the rules, the controlled ports, and the enforcement log in the **Policies** workspace.

For the class diagrams and the sequence diagrams, read [`docs/architecture-overview.md`](docs/architecture-overview.md).

## Testing

### Unit tests

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DWireLab_ENABLE_UNIT_TESTING=ON
cmake --build . --parallel
ctest --output-on-failure
```

The project has more than 113 unit tests in 9 suites. The tests examine `EthernetFrame`, `MacTable`, `TapDevice`, `UdpSocket`, `JoiningThread`, `Expected`, and the integration scenarios.

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

### End-to-end tests (Docker)

```bash
./tests/test_in_docker.sh
```

For the full test guide, read [`tests/TESTING.md`](tests/TESTING.md). For a short introduction, read [`tests/QUICK_START.md`](tests/QUICK_START.md).

## API

WireLab has two stable interfaces. All other interfaces are internal.

**C ABI: `include/wirelab/wirelab_ffi.h`.** The `wirelab-desktop` application uses this ABI. The ABI is frozen and has a version number.

The orchestration is in C++ (`wirelab::Session`). The Rust code does only the layout, the drawing, the input, and the text format.

Telemetry crosses the boundary as spans in session memory. These spans are valid until the next command that changes the session. The safe Rust wrapper makes an incorrect lifetime a compile error. The read functions take `&self`. The command functions take `&mut self`. The session type is not `Send` and not `Sync`.

The `cargo test` command compares each mirrored structure with the layout of the C++ compiler. Thus a change in the ABI causes a test failure.

For the design and the alternatives, read [`docs/gpui-migration-plan.md`](docs/gpui-migration-plan.md).

**Control protocol: `include/wirelab/control_protocol.hpp`.** The protocol uses JSON over TCP with one message on each line. The `--control-port` option starts the server.

The server sends a reply for each request. The server sends each event to all clients. Each reply contains `topology_revision`. Thus a client that connects again can synchronise.

The commands control the topology reload, the injection and removal of faults, the supervision state, and the benchmark runs. For examples, read [Control channel](#control-channel) and [Benchmarks on the control channel](#benchmarks-on-the-control-channel).

## Maintainers

[@teddymalhan](https://github.com/teddymalhan)

## Contributing

Send your questions and bug reports to [GitHub Issues](../../issues).

Pull requests are welcome. Obey these rules:

1. Run `make format` before you send the pull request. This command uses clang-format with the Google style.
2. Add or change the unit tests for each change of the behavior.
3. Make sure that `ctest` passes with AddressSanitizer.

For the full guide, read [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE) (c) 2025 Teddy Malhan
