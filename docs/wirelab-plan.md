# WireLab Development Plan

## Vision

**WireLab** is a programmable virtual-network digital twin built from the current Layer-2 VPN/virtual-switch project. It combines virtual Ethernet switching, deterministic traffic generation and replay, controlled fault injection, real-time telemetry, and CUDA-accelerated bulk packet analysis.

The finished project should demonstrate one reproducible scenario end to end:

1. Start a YAML-defined virtual topology.
2. Generate or replay normal traffic through TAP-backed virtual ports.
3. Show topology, flow, forwarding, and latency metrics.
4. Inject a broadcast storm, MAC flap, or UDP flood.
5. Detect and explain the anomaly.
6. Apply a policy such as rate limiting, dropping, mirroring, or quarantining a port.
7. Compare CPU and CUDA analysis results and performance using the same trace and benchmark configuration.

## Scope and Design Principles

### Preserve the existing dataplane

The existing components remain the foundation:

- `VPort`: bridges a TAP device and the switch over UDP.
- `VSwitch`: learns source MAC addresses and forwards known unicast or broadcast Ethernet frames.
- `MacTable`: stores MAC-to-endpoint mappings.
- `EthernetFrame`: validates and parses Layer-2 headers.

The initial forwarding path stays CPU-owned:

```text
TAP / UDP receive -> Ethernet parse -> MAC learn / forward decision -> UDP / TAP send
```

### Use CUDA for batched, data-parallel work

CUDA is not appropriate for one-packet-at-a-time forwarding. Host-to-device transfer, kernel launch, and synchronization costs would usually exceed CPU parsing of a small Ethernet frame.

CUDA work must receive a batch of contiguous packet bytes and metadata. It should process tasks where thousands of independent packets can be analyzed in parallel:

- packet header parsing;
- protocol and packet-size histograms;
- flow-key extraction and aggregation;
- top-talker and traffic-matrix calculations;
- rule matching and anomaly-candidate generation;
- synthetic packet generation;
- offline PCAP analysis and trace replay.

The CPU remains responsible for sockets, TAP I/O, MAC-table mutation, ordering, and actual policy enforcement until measurement proves a broader GPU dataplane is beneficial.

### Measure end-to-end behavior

A faster CUDA kernel alone is not a success. Every performance claim must include transfer time, batch wait time, CPU work, packet loss, and latency.

The default backend remains CPU unless the CUDA backend meets the same correctness requirements and improves the selected end-to-end workload without violating its latency budget.

## Target Architecture

```text
                         +-------------------------+
                         | WireLab controller      |
                         | YAML topology / policy  |
                         | fault + replay control  |
                         +------------+------------+
                                      |
       +------------------------------+------------------------------+
       |                                                             |
       v                                                             v
+------------------------+                                  +------------------------+
| Virtual dataplane      |                                  | Telemetry plane        |
| TAP <-> VPort <-> UDP  |                                  | metrics + event stream |
| VSwitch + MacTable     |                                  | dashboard / reports    |
+-----------+------------+                                  +------------------------+
            |
            | contiguous packet batches
            v
+---------------------------------------------------------------------+
| CUDA analysis plane                                                 |
| packet parser | flow aggregation | histograms | anomaly candidates  |
| policy matching | heavy hitters | traffic matrix | packet generator |
+---------------------------------------------------------------------+
```

## User-Facing Features

### Topology definitions

WireLab will load small, reproducible virtual topologies from YAML:

```yaml
network:
  name: security-lab

nodes:
  - { id: client-a, type: host }
  - { id: client-b, type: host }
  - { id: dns-server, type: host }
  - { id: core-switch, type: switch }

links:
  - { from: client-a, to: core-switch, latency_ms: 1 }
  - { from: client-b, to: core-switch, latency_ms: 1 }
  - { from: dns-server, to: core-switch, latency_ms: 2 }

policies:
  - name: contain-broadcast-storm
    match: { broadcast_packets_per_second: ">50000" }
    action: rate_limit
```

Initial topology scope: one switch and multiple ports. Later scope: multiple switches, segmented networks, and firewall nodes.

### Traffic generation and replay

A deterministic generator must create traffic with an explicit seed, packet-rate target, duration, frame-size distribution, source/destination distribution, and traffic pattern.

Supported scenarios:

- known unicast traffic;
- broadcast and ARP-like discovery traffic;
- mixed UDP/TCP/ICMP frame metadata;
- many short flows;
- long-lived high-volume flows;
- high-cardinality MAC and flow-table workloads;
- unknown-unicast floods;
- broadcast storms;
- UDP floods;
- port-scan-like fan-out;
- reproducible trace or PCAP replay.

Example commands:

```bash
wirelab generate --scenario mixed-traffic --rate 500kpps --duration 60s --seed 42
wirelab replay --input traces/broadcast-storm.pcap --topology scenarios/security-lab.yaml --speed 10x
```

### Fault injection

Each virtual link or port can be configured with:

- fixed latency;
- jitter;
- packet-loss percentage;
- duplication percentage;
- bandwidth cap;
- link-down / blackhole state;
- port isolation;
- MAC-table flush.

Fault injection must be deterministic when supplied a fixed seed and configuration so that a regression can be replayed exactly.

### Explainable anomaly detection

Initial anomaly detectors must be rule-based and return the measurements responsible for each alert:

| Detector | Evidence |
|---|---|
| Broadcast storm | Broadcast packets/sec from a source exceeds its configured threshold. |
| MAC flap | A source MAC changes ingress endpoint too often in a time window. |
| Unknown-unicast flood | Unknown destination decisions exceed a configured rate. |
| UDP flood | A source or flow exceeds a packet/byte rate threshold. |
| Port-scan heuristic | A source contacts an unusually high number of destination ports. |
| Hot talker | A source, destination, or flow dominates a time window. |
| Malformed frame | Ethernet/IP/transport parsing fails length or structural checks. |

Policy actions:

- allow;
- drop;
- mirror to the event stream;
- rate limit;
- quarantine a virtual port;
- alert only.

The GPU may classify packets and emit anomaly candidates. The host policy engine makes the final enforceable decision and records why it did so.

### Telemetry and visualization

The Qt desktop frontend is the primary WireLab control and visualization surface. The core processes expose a versioned local control API plus a read-only telemetry/event stream; the frontend must not read or mutate dataplane internals directly.

Required views:

- topology graph and current MAC-to-port mapping;
- ingress, forwarded, dropped, and unknown-unicast packets/sec;
- goodput and per-port throughput;
- p50/p95/p99 forwarding latency and jitter;
- protocol, EtherType, frame-size, and destination-port distributions;
- top talkers and source/destination traffic matrix;
- policy actions and anomaly events;
- CPU utilization, GPU utilization, GPU memory, transfer time, and kernel time;
- active injected faults.

### Qt desktop frontend

Build the frontend with Qt 6, C++ and Qt Quick/QML. Keep the long-lived networking, topology, telemetry, and CUDA ownership in the C++ core; use QML for the interactive workspace, charts, and topology presentation. A QWidget-only implementation is acceptable for administrative forms, but the live topology and high-rate charts should use Qt Quick.

The frontend controls every supported WireLab operation:

| Workspace | User actions | Required output |
|---|---|---|
| Topology | Create/open/save YAML topology; add nodes and links; start, stop, isolate, or reconnect ports | Graph, node/port state, learned MAC mappings, link health |
| Traffic Lab | Select generator or PCAP/trace input; configure rate, duration, size distribution, seed, sources, and destinations; start/stop/replay | Offered rate, actual rate, queue depth, replay progress |
| Fault Lab | Select a link or port; apply latency, jitter, loss, duplication, bandwidth caps, blackhole, and MAC-table flush | Active fault list, configuration, timer, resulting loss/latency |
| Policy Lab | Create, validate, enable, disable, and inspect rule-based policies | Match explanation, action, hit counts, affected ports/flows |
| Live Monitor | Filter flows, packets, ports, anomalies, and top talkers; switch visible time window | Topology traffic animation, charts, flow table, alert timeline |
| CUDA Lab | Select CPU/CUDA backend; configure supported batch target and stream count; start a benchmark or trace analysis | Device details, backend state, GPU memory/utilization, H2D/D2H/kernel timings, CPU comparison |
| Reports | Select a completed run; compare CPU/CUDA runs with identical scenario metadata; export JSON/CSV and screenshots | Reproducible configuration and labeled performance report |

Frontend safety and UX rules:

- All destructive actions require an explicit target and a visible summary before execution.
- Start/stop, backend selection, topology changes, and policy changes are commands acknowledged by the core; a button state is never treated as proof that a command succeeded.
- The frontend displays errors from the core verbatim with operation, target, and recovery context.
- Large telemetry updates are sampled, coalesced, and delivered as immutable snapshots so UI rendering cannot block packet forwarding.
- A run records topology, scenario, random seed, software version, backend, GPU/driver details when applicable, and benchmark configuration.
- The UI labels CPU, CUDA-live, and CUDA-offline results distinctly; it must never imply that GPU analysis is GPU-native packet forwarding.

### Core-to-frontend contract

Use a local loopback control service rather than linking the GUI into `vswitch`. This keeps the dataplane independently testable, lets a headless benchmark run without Qt, and gives the GUI one stable contract.

Initial transport: JSON commands and JSON events over localhost WebSocket or a local domain socket. The interface is versioned from the first message:

```json
{
  "api_version": 1,
  "request_id": "a8c6a989-1b0f-4e10-86fb-50d1d554b179",
  "command": "start_benchmark",
  "parameters": {
    "scenario": "mixed-traffic",
    "backend": "cuda",
    "batch_size": 2048,
    "duration_seconds": 60,
    "seed": 42
  }
}
```

Commands return either an accepted operation identifier or a structured validation/error result. Long-running actions publish progress and a final immutable result. The protocol includes command IDs, topology revision IDs, and monotonic event sequence numbers so the frontend can discard stale telemetry after a restart or topology reload.

The C++ core owns protocol types and validation. Qt uses typed view models that translate only validated API messages into presentation state.

## CUDA Architecture

### Data model

The host prepares contiguous packet and metadata buffers rather than transferring `std::vector` instances or host-owned C++ objects to the device:

```cpp
struct PacketBatch {
  std::byte* packet_bytes;       // contiguous payload arena
  uint32_t* packet_offsets;      // N + 1 offsets into packet_bytes
  uint16_t* packet_lengths;      // N lengths
  uint32_t* sender_ids;          // N ingress-port identifiers
  uint64_t batch_timestamp_ns;
  uint32_t packet_count;
};

struct PacketMetadata {
  uint64_t source_mac;
  uint64_t destination_mac;
  uint32_t source_ipv4;
  uint32_t destination_ipv4;
  uint16_t source_port;
  uint16_t destination_port;
  uint16_t frame_length;
  uint8_t protocol;
  uint8_t tcp_flags;
  uint8_t validity;
  uint8_t forwarding_class;
  uint64_t flow_hash;
};
```

Use a structure-of-arrays layout for high-volume device data when profiling shows coalesced access improves the selected kernel.

### Streams and buffer lifecycle

Use pinned host buffers, device buffers, CUDA events, and two or three CUDA streams:

```text
stream 0: copy batch A to device -> analyze A -> copy result A to host
stream 1: copy batch B to device -> analyze B -> copy result B to host
stream 2: copy batch C to device -> analyze C -> copy result C to host
```

This overlaps transfer, execution, and host result consumption. The CPU must retain packet-order metadata; bounded queue depth and maximum batch wait time are required so throughput optimization cannot create unbounded tail latency.

### CUDA modules

#### 1. Packet parser

One thread initially handles one packet. It validates header bounds and extracts Ethernet, IPv4/IPv6, UDP/TCP/ICMP, port, TCP-flag, and flow-hash metadata.

Implementation requirements:

- grid-stride loop;
- bounds checks before every header access;
- blocks initially tuned at 128 or 256 threads;
- checked kernel launches;
- no host pointers accessed by device code;
- malformed packets return metadata rather than causing out-of-bounds reads.

#### 2. Histogram and protocol statistics

Generate per-batch and time-window statistics:

- packet-size histogram;
- EtherType and transport-protocol histogram;
- destination-port histogram;
- broadcast / unicast / unknown-unicast ratio;
- packets and bytes per ingress port;
- malformed-frame counts.

#### 3. Flow aggregation

Derive a 5-tuple key from parsed metadata:

```text
source IP, destination IP, source port, destination port, protocol
```

Start with parallel flow-hash sorting and reduction using CUB or Thrust. Return compact per-flow records rather than raw packet metadata wherever possible.

Later experiments may include a fixed-capacity device-resident flow table, count-min sketch, Bloom filter, or approximate cardinality estimator. These are optional only after the simple sorted-batch approach has correctness and benchmark evidence.

#### 4. Traffic matrix and heavy hitters

Aggregate traffic by virtual source/destination node, MAC, IP, port, or flow. Results feed the live topology and top-talker panels.

#### 5. Policy matching and anomaly candidates

Match packet metadata against read-only, batch-friendly rules: protocol, ports, MAC prefixes, IPv4 ranges, frame size, TCP flags, and forwarding class. Aggregate threshold detectors on the GPU; return explicit candidate evidence to the host.

#### 6. GPU packet generator

Stretch feature. Generate packet headers and payload batches on the GPU for deterministic high-cardinality workloads. The host transmits those batches through the normal UDP/TAP path.

#### 7. Offline PCAP analyzer

A complete early CUDA deliverable. It consumes a packet capture or WireLab trace in large batches and produces flow, protocol, histogram, top-talker, and anomaly reports. This proves CUDA correctness and throughput before introducing live-batch latency constraints.

### CUDA correctness and safety requirements

- Wrap every CUDA Runtime API call in an error-checking helper.
- Check `cudaGetLastError()` after each kernel launch.
- Synchronize with CUDA events or a stream only where host result consumption or benchmark timing requires it.
- Use RAII for streams, events, device allocations, and pinned host allocations.
- Run Compute Sanitizer for memory, race, initialization, and synchronization defects.
- GPU tests must explicitly skip when no compatible CUDA device is available; they must never report a false pass.

## Repository Evolution

Proposed structure:

```text
apps/
  vswitch_main.cpp
  vport_main.cpp
  wirelab_ctl.cpp
  wirelab_bench.cpp
  wirelab_gui.cpp

include/project/
  control/
  dataplane/
  telemetry/
  traffic/
  cuda/
  gui/

src/
  control/
    control_server.cpp
    control_protocol.cpp
  dataplane/
    switch.cpp
    batch_processor.cpp
    policy_engine.cpp
    fault_engine.cpp
  telemetry/
    metrics.cpp
    event_log.cpp
  traffic/
    generator.cpp
    replay.cpp
  cuda/
    packet_parser.cu
    flow_aggregation.cu
    statistics.cu
    cuda_analyzer.cpp
  gui/
    wirelab_application.cpp
    topology_model.cpp
    telemetry_model.cpp
    benchmark_model.cpp
    qml/
      Main.qml
      TopologyWorkspace.qml
      TrafficLab.qml
      FaultLab.qml
      PolicyLab.qml
      MonitorWorkspace.qml
      CudaLab.qml
      ReportsWorkspace.qml

tests/
  unit/
  integration/
  differential/
  gui/
  performance/
  traces/

scenarios/
  security-lab.yaml
  broadcast-storm.yaml
  multi-switch.yaml

```

The exact layout should follow the repository's current source-discovery CMake conventions when implementation begins.

### Backend interface

Introduce a narrow analysis interface with a CPU reference implementation and optional CUDA implementation:

```cpp
class PacketAnalyzer {
 public:
  virtual ~PacketAnalyzer() = default;
  virtual AnalysisBatch analyze(std::span<const PacketView> packets) = 0;
};
```

Implementations:

- `CpuPacketAnalyzer`: canonical reference behavior; always built.
- `CudaPacketAnalyzer`: optional backend; compiled only when CUDA is enabled and selected explicitly at runtime.

The CPU and CUDA implementations must produce equivalent observable classifications and aggregate results for a shared deterministic input corpus.

### CMake integration

CUDA is optional and CPU-only builds remain the default:

```cmake
option(PROJECT_ENABLE_CUDA "Build the optional WireLab CUDA backend" OFF)

if(PROJECT_ENABLE_CUDA)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  add_library(project_cuda src/cuda/packet_parser.cu)
  target_link_libraries(project_cuda PRIVATE CUDA::cudart)
  set_target_properties(project_cuda PROPERTIES
    CUDA_STANDARD 17
    CUDA_SEPARABLE_COMPILATION ON)
endif()
```

Runtime selection is explicit:

```bash
vswitch --analyzer cpu
vswitch --analyzer cuda
```

CUDA selection must fail clearly when no compatible device is present. It must not silently alter forwarding behavior or silently fall back during benchmarks.

### Qt build integration

The Qt frontend is optional at build time, but is a first-class supported executable when enabled. It links to the control-client and typed view-model layer, not directly to the dataplane or CUDA implementation.

```cmake
option(PROJECT_BUILD_WIRELAB_GUI "Build the Qt 6 WireLab frontend" ON)

if(PROJECT_BUILD_WIRELAB_GUI)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Network)
  qt_add_executable(wirelab_gui apps/wirelab_gui.cpp)
  qt_add_qml_module(wirelab_gui
    URI WireLab
    VERSION 1.0
    QML_FILES src/gui/qml/Main.qml)
  target_link_libraries(wirelab_gui PRIVATE
    wirelab_control_client
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network)
endif()
```

`wirelab_gui` starts and connects to a local WireLab control service. The control service may run in-process only for a dedicated development mode; production and benchmark runs use the same external API contract as the GUI.

## Performance and Test Plan

### CPU baseline before CUDA

First establish release-build CPU behavior. Per-frame console logging must be disabled or moved off the hot path during measurements; otherwise it invalidates high-rate packet benchmarks.

Benchmark dimensions:

| Dimension | Initial values |
|---|---|
| Packet size | 64, 128, 512, 1500, 1518 bytes |
| Traffic pattern | known unicast, broadcast, unknown unicast, mixed |
| MAC-table cardinality | 1K, 10K, 100K entries |
| Batch target | 1, 32, 128, 512, 2048, 8192 packets |
| Offered load | stepped load through saturation and overload |
| Run length | 60-second benchmark and 30-minute soak |

### Required measurements

#### Network behavior

- offered, received, forwarded, discarded, and dropped packets/sec;
- packet loss percentage;
- goodput in bit/sec;
- p50, p95, p99, and p99.9 latency;
- jitter;
- per-port throughput;
- MAC lookup and update rate.

#### CPU behavior

- process and per-core utilization;
- allocations per packet or batch;
- context switches where the platform exposes them;
- batch queue depth;
- CPU analysis duration.

#### CUDA behavior

- GPU utilization and memory use;
- host-to-device and device-to-host transfer duration;
- effective transfer bandwidth;
- kernel duration measured with CUDA events;
- batch wait time;
- achieved packets/sec for parser and flow aggregation;
- occupancy, memory throughput, register pressure, and warp divergence when profiling kernel bottlenecks.

### Correctness tests

Test both CPU and CUDA analyzers against generated frames and saved traces:

- frame shorter than Ethernet header;
- valid minimum and maximum frames;
- malformed/truncated Layer-3 or Layer-4 headers;
- known unicast;
- broadcast excluding ingress endpoint;
- unknown unicast;
- source-MAC learning and endpoint migration;
- mixed batches;
- high-cardinality flow batches;
- every relevant boundary batch size: 0, 1, 31, 32, 33, 127, 128, 129, 255, 256, 257, and configured maximum.

Differential tests compare CPU and CUDA results for exact parsing validity, header values, forwarding class, packet-to-result association, aggregate counters, and policy/anomaly evidence.

### Qt frontend validation

Test the frontend at three boundaries:

1. **View-model unit tests:** control-message decoding, topology revision handling, command-state transitions, error presentation, metric downsampling, and CPU/CUDA result labeling.
2. **Control-contract integration tests:** the real control server accepts valid topology, traffic, fault, policy, and benchmark commands; rejects invalid requests; and emits ordered progress/result events.
3. **GUI smoke tests:** launch `wirelab_gui` against a test control server; open a topology, start a deterministic run, inject a fault, enable a policy, select the CUDA analysis view when available, and confirm the completed report appears. These tests use Qt Test and Qt Quick Test where applicable.

GUI tests must use a deterministic test scenario and mock only the rendering-independent control-server timing. They must not replace core dataplane, CUDA, or benchmark tests.

### CUDA validation

Run CUDA-enabled test binaries under:

```bash
compute-sanitizer ./packet_cuda_tests
compute-sanitizer --tool racecheck ./packet_cuda_tests
compute-sanitizer --tool initcheck ./packet_cuda_tests
compute-sanitizer --tool synccheck ./packet_cuda_tests
```

### Profiling workflow

Use Nsight Systems first to find end-to-end queueing, transfer, host-thread, and stream-overlap problems:

```bash
nsys profile -o wirelab_trace ./wirelab_bench --analyzer cuda ...
nsys stats wirelab_trace.nsys-rep
```

Use Nsight Compute only after a kernel matters to end-to-end execution:

```bash
ncu --set full --kernel-name packet_parser -o parser_report ./wirelab_bench --analyzer cuda ...
```

### Acceptance criteria for CUDA live analysis

For a selected operating point, CUDA must:

1. produce no CPU/CUDA differential-test mismatches;
2. produce no Compute Sanitizer findings;
3. report transfer-inclusive, reproducible benchmark results;
4. avoid increasing loss relative to the CPU reference at equivalent offered load;
5. meet the defined tail-latency budget for the live workload; and
6. improve end-to-end throughput or provide a clearly documented bulk/offline-analysis advantage.

If GPU batching increases p99 latency unacceptably, retain CPU analysis for live forwarding and use CUDA for offline analysis, high-volume replay, or bulk telemetry. This is a valid engineering outcome.

## Delivery Milestones

### Milestone 1: Measurable CPU switch and control service

- Release benchmark configuration.
- Structured counters and configurable logging.
- Deterministic packet generator.
- CPU throughput, loss, and latency report.
- Versioned control server and telemetry/event contract.
- Identified CPU hot path from profiling.

### Milestone 2: Topology and faults

- YAML topology loader.
- Controller to create ports/switches.
- Configurable latency, jitter, loss, bandwidth caps, and link/port faults.
- Deterministic scenario tests.
- Control commands and state-change events for every topology and fault action.

### Milestone 3: Qt control frontend

- Qt 6/QML application shell and typed control client.
- Topology, Traffic Lab, and Fault Lab workspaces.
- Open/save topology, start/stop run, generator/replay configuration, and fault controls.
- Command acknowledgement, validation errors, reconnect handling, and stale-event rejection.
- Qt view-model, contract-integration, and GUI smoke tests.

### Milestone 4: Telemetry, policies, and Qt monitoring

- Metrics endpoint and event stream.
- Top talkers, forwarding counters, traffic matrix, and latency reporting.
- Broadcast storm, MAC flap, unknown-unicast flood, and UDP-flood detectors.
- Allow, drop, mirror, rate-limit, and quarantine actions.
- Qt Monitor and Policy Lab workspaces with live topology, charts, flow table, event timeline, and action explanations.

### Milestone 5: CUDA offline analyzer and Qt CUDA Lab

- Optional CMake CUDA target.
- Batching and GPU packet parsing.
- Histograms and flow aggregation.
- CPU/CUDA differential tests.
- Compute Sanitizer validation.
- CPU-versus-CUDA offline trace performance report.
- Qt CUDA Lab showing device details, backend state, batch configuration, transfer timing, kernel timing, and clearly labeled analysis results.

### Milestone 6: CUDA live analysis

- Pinned buffers and double/triple buffering.
- `cudaMemcpyAsync`, streams, and events.
- Live GPU flow/statistics/anomaly analysis.
- Transfer-inclusive latency and throughput comparison.
- Explicit CPU or CUDA backend selection from the Qt frontend and CLI.

### Milestone 7: GPU traffic generation and polished Qt demo

- GPU-generated synthetic traffic batches.
- Repeatable normal, flood, scan, and broadcast-storm scenarios.
- Qt Reports workspace with CPU/CUDA comparison, configuration provenance, export, and screenshots.
- Five-minute scripted GUI demonstration and reproducible headless benchmark command.

### Milestone 8: GPU-native dataplane (hardware-dependent stretch)

This milestone is separate from normal CUDA development. GPU-native NIC receive/transmit requires Linux, a supported NVIDIA ConnectX/BlueField-class NIC, compatible PCIe topology, and GPUDirect/DOCA support. Do not claim this capability without that hardware and measured implementation.

## References

- [NVIDIA CUDA C++ Programming Guide](https://docs.nvidia.com/cuda/cuda-c-programming-guide/)
- [NVIDIA CUDA Best Practices Guide](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/)
- [NVIDIA Nsight Systems](https://docs.nvidia.com/nsight-systems/)
- [NVIDIA Nsight Compute](https://docs.nvidia.com/nsight-compute/)
- [NVIDIA Compute Sanitizer](https://docs.nvidia.com/compute-sanitizer/)
- [NVIDIA GPUDirect RDMA](https://docs.nvidia.com/cuda/gpudirect-rdma/)
- [NVIDIA DOCA GPUNetIO](https://docs.nvidia.com/doca/sdk/doca-gpunetio/)
- [NVIDIA DOCA GPU Packet Processing Application Guide](https://docs.nvidia.com/doca/sdk/doca-gpu-packet-processing-application-guide/)
- [Cisco TRex traffic generator](https://trex-tgn.cisco.com/)
- [Qt 6 documentation](https://doc.qt.io/qt-6/)
