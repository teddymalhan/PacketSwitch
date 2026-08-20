# Class Diagram - VPN Virtual Switch Architecture

## Overview
This diagram shows the structure and relationships of the virtual switch components.

> The dataplane diagram below documents the original C/Python prototype the
> project started from. The shipped dataplane is C++ (`src/vswitch.cpp`,
> `src/vport.cpp`); see `docs/architecture-overview.md`. The frontend section at
> the end of this file documents the current desktop stack.

```mermaid
classDiagram
    class vport_t {
        -int tapfd
        -int vport_sockfd
        -sockaddr_in vswitch_addr
        +vport_init(server_ip, server_port)
    }
    
    class TAPUtils {
        <<utility>>
        +tap_alloc(dev: char*) int
    }
    
    class SysUtils {
        <<utility>>
        +ERROR_PRINT_THEN_EXIT(msg)
    }
    
    class VPortApplication {
        -vport_t vport
        -pthread_t up_forwarder
        -pthread_t down_forwarder
        +main(argc, argv) int
        +forward_ether_data_to_vswitch(vport) void*
        +forward_ether_data_to_tap(vport) void*
    }
    
    class VSwitch {
        <<Python>>
        -socket vserver_sock
        -dict mac_table
        -tuple server_addr
        +recvfrom() bytes
        +sendto(data, addr)
        +parse_ethernet_frame(data) tuple
        +update_mac_table(src_mac, vport_addr)
        +forward_frame(dst_mac, data)
    }
    
    class EthernetFrame {
        <<struct>>
        +byte[6] ether_dhost
        +byte[6] ether_shost
        +uint16 ether_type
        +byte[] payload
    }
    
    class LinuxKernel {
        <<external>>
        +/dev/net/tun
        +network_stack
    }
    
    VPortApplication --> vport_t : contains
    VPortApplication --> TAPUtils : uses
    VPortApplication --> SysUtils : uses
    vport_t --> TAPUtils : allocates TAP device
    vport_t --> VSwitch : communicates via UDP
    VPortApplication --> EthernetFrame : processes
    vport_t --> LinuxKernel : interacts with
    VSwitch --> EthernetFrame : processes
    VSwitch --> vport_t : routes frames to
```

## Component Descriptions

### VPort Components (C)

#### vport_t
The main VPort structure that maintains:
- **tapfd**: File descriptor for the TAP device (connection to Linux kernel)
- **vport_sockfd**: UDP socket for communication with VSwitch
- **vswitch_addr**: Address information for the VSwitch server

#### VPortApplication
The main application that:
- Creates and initializes a VPort instance
- Spawns two forwarding threads:
  - **up_forwarder**: Forwards ethernet frames from TAP device to VSwitch
  - **down_forwarder**: Forwards ethernet frames from VSwitch to TAP device

#### TAPUtils
Utility module for TAP device management:
- **tap_alloc()**: Allocates and configures a TAP device (IFF_TAP | IFF_NO_PI)

#### SysUtils
System utility macros:
- **ERROR_PRINT_THEN_EXIT**: Error handling macro for fatal errors

### VSwitch Component (Python)

#### VSwitch
A learning switch implementation that:
- Listens on UDP socket for ethernet frames from VPorts
- Maintains a MAC address table mapping MAC addresses to VPort addresses
- Forwards frames based on destination MAC:
  - Unicast: Forward to specific VPort if MAC is in table
  - Broadcast (ff:ff:ff:ff:ff:ff): Forward to all VPorts except source
  - Unknown: Discard frame

### Data Structures

#### EthernetFrame
Standard Ethernet II frame structure:
- 6-byte destination MAC address
- 6-byte source MAC address
- 2-byte EtherType field
- Variable-length payload

## Architecture Notes

1. **TAP Device**: Virtual network interface that appears as a real network device to the Linux kernel
2. **UDP Communication**: VPorts communicate with VSwitch using UDP sockets for low-latency frame forwarding
3. **MAC Learning**: VSwitch learns MAC-to-VPort mappings dynamically
4. **Threading**: Each VPort uses two threads for bidirectional forwarding

## Desktop Frontend and the FFI Boundary

The frontend is a separate process image in a separate language, so the boundary
between them is a contract rather than a call graph. Three layers, one direction:
the C++ side never calls into Rust, and Rust never touches a dataplane type.

```mermaid
classDiagram
    class Session {
        <<C++, Qt-free>>
        -uint32_t dirty_
        -AnalysisBatch batch_
        -vector~TopologyNodeRow~ nodes_
        +open_topology(path) bool
        +start_traffic(scenario, seed, backend)
        +step_traffic()
        +step_report()
        +take_dirty() uint32_t
        +status_message() string
    }

    class wirelab_ffi {
        <<extern "C">>
        +wirelab_session_open(abi_version) wirelab_session*
        +wirelab_session_take_dirty(s) uint32_t
        +wirelab_telemetry_view(s) wirelab_telemetry
        +wirelab_topology_node(s, index) wirelab_node_view
        +wirelab_report_export(s, path) bool
        +wirelab_abi_layout(out) void
    }

    class raw {
        <<Rust, unsafe>>
        +WirelabStr
        +WirelabTelemetry
        +extern declarations
    }

    class RustSession {
        <<Rust, safe, !Send>>
        -NonNull~wirelab_session~ ptr
        +open() Option~Session~
        +take_dirty() Dirty
        +telemetry() Telemetry~'_~
        +step_traffic(&mut self)
    }

    class WireLab {
        <<gpui::Render>>
        -Session session
        -Page page
        -Task traffic_tick
        -Task report_tick
        +render(window, cx) impl IntoElement
    }

    wirelab_ffi ..> Session : wraps, catch(...)
    raw ..> wirelab_ffi : hand-mirrored, layout-asserted
    RustSession ..> raw : makes safe
    WireLab --> RustSession : owns, single-threaded
```

### Session (`include/wirelab/session.hpp`)

The orchestration every frontend drives: topology editing and radial layout, the
traffic tick, the report state machine, policy and fault commands, YAML/JSON/CSV
export. It has no error returns by design — a failure is a status message the UI
shows, not a code the UI must invent prose for — and no callbacks: every seam is
pull-based, matching the core beneath it.

### wirelab_ffi (`include/wirelab/wirelab_ffi.h`)

A frozen C ABI, versioned by `WIRELAB_FFI_ABI_VERSION` and checked at
`wirelab_session_open`. Two rules the header states and the Rust wrapper
enforces:

- **Thread affinity.** A session belongs to the thread that opened it.
- **Borrow lifetime.** Every string and every span is borrowed from
  session-owned storage and dies at the next mutating call.

Change notification is a poll-and-clear dirty bitmask, so there is no callback
trampoline into Rust in either direction.

### The Rust binding (`gui/src/ffi/`)

`raw.rs` mirrors the header by hand — no `bindgen`, no libclang in CI — and
`wirelab_abi_layout()` reports what the C++ compiler actually produced so
`cargo test` fails on drift rather than reading plausible garbage. `mod.rs`
turns the two header rules into type-system facts: `Session` is neither `Send`
nor `Sync`, getters take `&self` and return values tied to that borrow, and
commands take `&mut self` — so holding a telemetry row across a mutation does
not compile.

### The frontend (`gui/src/`)

GPUI. `WireLab` owns the session and the clock: a 500 ms task steps traffic, a
16 ms task advances a running report, and each drains `take_dirty()` and calls
`cx.notify()` when something moved. The core never advances a simulation on its
own, so the frontend owning the tick is the design, not a convenience.

