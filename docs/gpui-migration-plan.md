# Plan: Replace the Qt/QML frontend with GPUI (Rust) over a C ABI

Status: **Complete. All six phases landed; Qt is deleted.**
Target: remove Qt from WireLab entirely; ship `wirelab-desktop`, a Rust/GPUI application
linking the existing C++ core through a hand-written `extern "C"` shim.

### What is built

| Piece | Where | Verified by |
|---|---|---|
| Qt-free orchestration core | `include/wirelab/session.hpp`, `src/session*.cpp`, target `wirelab_session` | `ctest -R session_test` — 12 tests |
| C ABI shim | `include/wirelab/wirelab_ffi.h`, `src/wirelab_ffi.cpp`, target `wirelab_ffi` | `ctest -R wirelab_ffi_test` — 8 tests, also clean under ASan |
| macOS bundle | `apps/resources/Info.plist.in`, bundle assembly in `CMakeLists.txt` | `build/bin/WireLab.app` opens named and iconed |
| Rust binding | `gui/src/ffi/{raw,mod}.rs` | `cargo test` — layout check against the real library, plus five workflow tests |
| GPUI frontend, all 7 workspaces | `gui/src/{main,app,ui,format}.rs`, `gui/src/pages/*` | `cargo test` — headless draw of every page, both tick loops |
| CMake integration | `PROJECT_BUILD_DESKTOP` in `CMakeLists.txt` | `cmake --build build --target wirelab-desktop`, `ctest` 39/39 with Metal on |
| Qt deletion | `apps/wirelab_gui.cpp`, `src/gui/`, `wirelab_view_model.*` and their build blocks, all removed | no `Qt`/`QML`/`Q_OBJECT` outside historical notes in this file and `docs/wirelab-plan.md` |
| CI | `desktop-macos` job in `.github/workflows/ci.yml` | builds with `-DPROJECT_BUILD_DESKTOP=ON`, runs `ctest`, `cargo fmt --check`, `cargo clippy -D warnings` |

### What is not built yet

Nothing in this plan. Two things it deliberately left alone:

- Live `VSwitch` attachment from the desktop app over `control_protocol.hpp` — a non-goal here (§7),
  and orthogonal by construction: the FFI session and the control client do not know about each other.
- A Windows desktop build (D8).

### Deviations from this plan, and why

| Planned | Built | Reason |
|---|---|---|
| `wirelab_status` codes on every entry point (§3) | Commands return `void`; failure arrives via `wirelab_session_status_message()` + `WIRELAB_DIRTY_STATUS` | `Session` has no error returns by design. A second error channel could only disagree with the status message the UI already shows. `wirelab_session_open` (NULL) and `wirelab_report_export` (bool) are the two real exceptions. |
| Borrowed spans over `AnalysisBatch` (D3) | Spans only for `MetricSample`; every other row is a `_count`/`_at` accessor pair | The session materialises display rows, not the raw batch, and those rows carry `std::string`. A span of POD does not apply; 100 accessor calls per frame is free next to a render. |
| Core `to_string` for every label (D2) | Display-name helpers kept, Qt-free, in `session.cpp` | The wire names (`unknown-unicast`) and the labels a person reads (`Unknown unicast`) are genuinely different vocabularies. Both directions are now exported through the FFI. |
| Corrosion drives cargo (D7) | A plain `add_custom_target` invoking cargo, with `add_dependencies(wirelab-desktop wirelab_ffi)` | No configure-time download, no new build dependency; cargo already tracks its own inputs and CMake still expresses the ordering. |
| `rfd` for native dialogs (D9) | `App::prompt_for_paths` / `prompt_for_new_path` | gpui ships them; a dependency for what the framework already does is dead weight. |
| `gpui-component` tables and charts (D9) | `gpui-component` for `Input`/`Button`/theme only; tables and both charts are hand-drawn | Row counts are bounded by the session (60 / 100 / 200), so a virtualized table buys nothing and costs a delegate plus an entity per pane. The topology graph needed `canvas` regardless. |
| `gpui::Timer` for the ticks (§ Phase 3) | `cx.background_executor().timer(..)` | `gpui::Timer` is `smol::Timer` and is not driven by the test executor. The headless tick tests found this; the executor's own timer works in both. |

---

## 1. What exists today (verified)

| Layer | Files | Notes |
|---|---|---|
| Qt entry point | `apps/wirelab_gui.cpp` (27 LOC) | `QGuiApplication` + `QQmlApplicationEngine`; registers the view model as QML context property `wirelab` (`:20-22`) |
| UI | `src/gui/qml/Main.qml` (1509 LOC) | Single QML file. `ApplicationWindow` + sidebar `ListView` + 7-page `StackLayout`: Dashboard `:389`, Topology `:540`, Traffic `:710`, Packets & Security `:858`, Faults `:952`, Policies `:1075`, Reports `:1305` |
| Bridge | `include/wirelab/wirelab_view_model.hpp` (212), `src/wirelab_view_model.cpp` (1268) | 30 `Q_PROPERTY`, 22 `Q_INVOKABLE` (`hpp:95-116`), 8 argument-less signals (`hpp:118-126`) |
| Test | `tests/src/wirelab_view_model_test.cpp` | Links `Qt6::Core` only; hand-drives `runTrafficStep()`/`runReportStep()` — no event loop |
| Build | `CMakeLists.txt:280-330`, `tests/CMakeLists.txt:194-211` | `PROJECT_BUILD_WIRELAB_GUI` defaults **OFF**; Qt6 `Quick` + `QuickControls2` only |
| Core | `WireLab` static lib, 27 TUs (`cmake/SourcesAndHeaders.cmake`) | **100% Qt-free.** Contamination is exactly the 4 files + 1 QML file above |

Four facts that shape the whole plan:

1. **The core has no Qt and no async callbacks.** No observer bus, no signal/slot analogue,
   no thread pool. Every seam is pull-based (`BenchmarkRun::advance(budget)`,
   `ControlServer::poll(timeout)`, `AnalysisPipeline::evaluate(...)`). The Qt signals are the
   view model's own invention over polled state.
2. **The UI owns the clock.** `Main.qml:117-127` has two `Timer`s (500 ms → `runTrafficStep()`,
   16 ms → `runReportStep()`). The core is stepped by the frontend, one-for-one replaceable by
   a GPUI timer task.
3. **The GUI is in-process**, not over the control socket. `docs/wirelab-plan.md:220-241`
   describes an out-of-process JSON control service that was never wired to the GUI; do not
   mistake it for the current architecture.
4. **CI never builds the GUI.** No workflow sets `PROJECT_BUILD_WIRELAB_GUI`; no Qt install
   step exists. The Rust frontend is additive to CI, not a replacement of existing steps.

---

## 2. Decisions

### D1 — Boundary: in-process C ABI shim, not `cxx`, not the control socket

New target `wirelab_ffi` (static lib) exposing `include/wirelab/wirelab_ffi.h` as pure
`extern "C"`. Rust binds it with hand-written `#[repr(C)]` mirrors validated by layout
assertions (see D5).

Rejected alternatives:

- **`cxx` crate.** Handles `std::string`/`std::vector` natively but forces cargo to compile
  the C++ sources (or duplicate the CMake build). WireLab's C++ build is non-trivial: ObjC++
  Metal TUs with `-fobjc-arc`, a Python codegen step (`scripts/embed_metal.py`), optional
  CUDA. Reproducing that in `build.rs` is a permanent tax. A frozen C ABI costs one header.
- **Out-of-process over `control_protocol.hpp`.** Zero new C++ and already has backpressure
  and revision fencing (`control_server.hpp:74,126-186`), but newline-delimited JSON per
  packet row cannot feed the Packets pane, and it would change the app from "desktop tool"
  to "client+daemon" — a scope increase the user did not ask for.
  *Keep it available*: the FFI session and the control client are orthogonal; a later
  "attach to running vswitch" feature reuses protocol types unchanged.

### D2 — The view-model logic moves to C++, Qt-stripped; Rust stays presentation-only

`src/wirelab_view_model.cpp` is ~63% UI glue (enum→`QString`, `QVariantMap` row building,
JSON/CSV export) and ~30% real orchestration (`runTrafficStep` `:731-820`, the report state
machine `:1043-1152`, radial topology layout `:499-541`, YAML writer `:445-497`).

Extract `wirelab::Session` (`include/wirelab/session.hpp`, `src/session.cpp`) = the view model
minus Qt: same member block (`wirelab_view_model.hpp:155-208`) with `std::string` for
`QString`, typed structs for `QVariantMap` rows, and a `uint32_t dirty_` bitmask replacing the
8 signals. `wirelab_ffi.cpp` is then a thin `extern "C"` wrapper with a blanket `catch(...)`.

Rationale: the orchestration is already tested (`tests/src/wirelab_view_model_test.cpp`), is
deterministic, and stamps report provenance. Rewriting it in Rust duplicates risk for no gain.
Rust gets layout, painting, input, and formatting only.

Enum→label strings: the core already has `to_string(Enum) -> const char*` for ~15 enums.
Export those through the FFI; delete the duplicated mappers at `wirelab_view_model.cpp:33-212`.

### D3 — Telemetry crosses as borrowed spans, not copies

`AnalysisBatch` (`packet_analyzer.hpp:109-131`) is 8 `std::vector` + 1 `std::array`, **every
element POD**. It is pinned inside the session for the lifetime of a tick. The FFI hands Rust
9 × `(const T*, size_t)`; Rust reads them as `&[T]` with `#[repr(C)]` mirrors. No per-element
allocation, no copy. Same for `AnomalyEvent`.

String-bearing rows (`EnforcementAction`, `PolicyRule`, `TopologyLink`, `BenchmarkResult`) get
index-addressed accessors returning `wirelab_str { const char*, size_t }` borrowed from
session-owned storage, valid until the next mutating call. Documented as a hard rule in the
header.

`std::chrono` flattens to `int64_t` nanoseconds. `steady_clock::time_point` (e.g.
`EnforcementAction::expires_at`, `policy_enforcer.hpp:34-45`) becomes *remaining* ns, not an
absolute stamp — absolute monotonic stamps are meaningless to the frontend.

`expected<T,E>` (`expected.hpp`, `std::variant`-backed) never crosses: every entry point
returns `int32_t` status + out-param. Same for `std::optional` → `bool` + out-param.

### D4 — Signals become a poll-and-clear dirty bitmask

```c
enum {
  WIRELAB_DIRTY_TOPOLOGY = 1u<<0, WIRELAB_DIRTY_SELECTION = 1u<<1,
  WIRELAB_DIRTY_STATUS   = 1u<<2, WIRELAB_DIRTY_TRAFFIC   = 1u<<3,
  WIRELAB_DIRTY_TELEMETRY= 1u<<4, WIRELAB_DIRTY_FAULTS    = 1u<<5,
  WIRELAB_DIRTY_POLICIES = 1u<<6, WIRELAB_DIRTY_REPORT    = 1u<<7,
};
uint32_t wirelab_session_take_dirty(wirelab_session*);
```

One bit per existing signal (`wirelab_view_model.hpp:118-126`). GPUI calls it after each step,
and calls `cx.notify()` when nonzero. No Rust callback trampoline is ever needed — that falls
straight out of the core being pull-based.

### D5 — ABI safety is enforced, not assumed

- C++ side: `static_assert(sizeof(T)==N && alignof(T)==A)` plus `offsetof` asserts for every
  mirrored struct, in `src/wirelab_ffi.cpp`.
- Rust side: a `layout_check` test comparing against `wirelab_abi_layout()` — a function that
  returns the sizes/offsets the C++ compiler actually produced. Mismatch fails `cargo test`.
- Version fence: `WIRELAB_FFI_ABI_VERSION` constant checked at `wirelab_session_open()`.

`bindgen` is deliberately **not** used: 25 functions and ~15 structs are cheaper to hand-mirror
than to carry a libclang build dependency into CI.

### D6 — Threading

Session is `!Sync`, single-threaded, owned by the GPUI main thread — matching the core's
design and today's QML behaviour. `wirelab_ffi.h` documents "not thread-safe; one thread per
session". Rust wraps the raw pointer in a non-`Send` newtype so this is compile-enforced.

The one place that will eventually want a thread is a live `VSwitch::start()` loop
(`src/vswitch.cpp:83-131`, blocking). It is **out of scope** — today's GUI never runs a live
switch. When added: dedicated thread, `stop()` via the existing `std::atomic<bool>`, and
`SwitchMetrics::snapshot()` is already lock-free (`switch_metrics.hpp:41-49`).

### D7 — Build: CMake stays master, Corrosion drives cargo

```
cmake --build  →  wirelab_ffi (static)  ──┐
                  WireLab, wirelab_backends│
                  wirelab_metal / _cuda    │
                                           ├→ corrosion_link_libraries →  cargo build
                                           │                              wirelab-desktop (bin)
                                           └→ macOS bundle assembly (reuses apps/resources/WireLab.icns)
```

`FetchContent(corrosion)`, `corrosion_import_crate(MANIFEST_PATH gui/Cargo.toml)`,
`corrosion_link_libraries(wirelab-desktop wirelab_ffi wirelab_backends)`. New option
`PROJECT_BUILD_DESKTOP` (default OFF, mirroring today's GUI option) plus `PROJECT_RUST_TOOLCHAIN`.
`cargo build` alone still works for frontend iteration once the static libs exist —
`build.rs` reads `WIRELAB_LIB_DIR` (set by Corrosion, overridable by hand).

### D8 — Platform scope: macOS first, Linux best-effort, no Windows GUI

Today's GUI is already macOS-shaped (`QQuickStyle::setStyle("macOS")`, `.icns` bundle,
`Qt.styleHints.colorScheme` palette). GPUI's macOS backend is its most mature; Linux runs on
Blade/Vulkan; Windows is the weakest. The `.vcxproj`/`.slnx` files are stale and do not know
about the GUI anyway. State this explicitly rather than discovering it in CI.

### D9 — Dependencies (pinned)

| Crate | Version | Use |
|---|---|---|
| `gpui` | `0.2.2` (crates.io) | framework |
| `gpui-component` | `0.5.1` | tables, charts, inputs, docking — covers most of the 7 panes |
| `rfd` | latest | native open/save dialogs, replacing QML `FileDialog` (`Main.qml:90-113`) |

`gpui-component` is the load-bearing choice: it ships data tables and charts, which is what 6
of the 7 panes are. Without it, hand-rolling a virtualized table is a phase of its own.
**Risk:** both crates are young and pre-1.0. Pin exact versions, commit `Cargo.lock`, and treat
an upgrade as a deliberate task.

---

## 3. FFI surface (sketch — this is the contract)

```c
/* include/wirelab/wirelab_ffi.h — extern "C", C99, no C++ types. */
#define WIRELAB_FFI_ABI_VERSION 1

typedef struct wirelab_session wirelab_session;
typedef struct { const char* ptr; size_t len; } wirelab_str;   /* borrowed, UTF-8 */

typedef enum { WIRELAB_OK = 0, WIRELAB_ERR_INVALID_ARG, WIRELAB_ERR_IO,
               WIRELAB_ERR_VALIDATION, WIRELAB_ERR_BACKEND, WIRELAB_ERR_INTERNAL
             } wirelab_status;

wirelab_session* wirelab_session_open(uint32_t abi_version);
void             wirelab_session_close(wirelab_session*);
uint32_t         wirelab_session_take_dirty(wirelab_session*);
wirelab_str      wirelab_session_status_message(const wirelab_session*);

/* --- topology (from Q_INVOKABLE :95-102) --- */
wirelab_status wirelab_topology_open(wirelab_session*, const char* path);
wirelab_status wirelab_topology_save(wirelab_session*, const char* path);
wirelab_status wirelab_topology_add_node(wirelab_session*, const char* id, uint32_t type);
wirelab_status wirelab_topology_add_link(wirelab_session*, const char* from, const char* to,
                                         int32_t latency_ms);
wirelab_status wirelab_topology_remove_selected(wirelab_session*);
void           wirelab_select_node(wirelab_session*, const char* id);
void           wirelab_select_link(wirelab_session*, const char* from, const char* to);
void           wirelab_clear_selection(wirelab_session*);

size_t              wirelab_topology_node_count(const wirelab_session*);
wirelab_node_view   wirelab_topology_node(const wirelab_session*, size_t index); /* id,type,x,y */
size_t              wirelab_topology_link_count(const wirelab_session*);
wirelab_link_view   wirelab_topology_link(const wirelab_session*, size_t index);

/* --- traffic (:103-106) --- */
wirelab_status wirelab_traffic_start(wirelab_session*, uint32_t scenario, int32_t packets_per_tick,
                                     int32_t frame_size, uint64_t seed, const char* backend);
void           wirelab_traffic_stop(wirelab_session*);
void           wirelab_traffic_step(wirelab_session*);

/* --- telemetry: borrowed spans, valid until the next mutating call (D3) --- */
typedef struct {
  const wirelab_packet_analysis* packets;      size_t packet_count;
  const wirelab_flow_record*     flows;        size_t flow_count;
  const wirelab_mac_traffic*     src_macs;     size_t src_mac_count;
  const wirelab_mac_traffic*     dst_macs;     size_t dst_mac_count;
  const wirelab_matrix_entry*    matrix;       size_t matrix_count;
  const wirelab_histogram_entry* ethertypes;   size_t ethertype_count;
  const wirelab_histogram_entry* protocols;    size_t protocol_count;
  const wirelab_histogram_entry* dst_ports;    size_t dst_port_count;
  const wirelab_size_bucket*     frame_sizes;  size_t frame_size_count;
  uint64_t total_packets, total_bytes, total_dropped, /* … */;
} wirelab_telemetry;
wirelab_telemetry wirelab_telemetry_view(const wirelab_session*);

/* --- faults / policies / report (:107-116) --- */
wirelab_status wirelab_fault_apply_selected(wirelab_session*, int32_t latency_ms,
                                            double loss_percent, bool blackhole);
wirelab_status wirelab_fault_clear(wirelab_session*, const char* a, const char* b);
wirelab_status wirelab_policy_add(wirelab_session*, const char* name, uint32_t anomaly_type,
                                  uint32_t action, uint64_t rate_limit_pps);
wirelab_status wirelab_policy_remove(wirelab_session*, const char* name);
wirelab_status wirelab_policy_set_enabled(wirelab_session*, const char* name, bool enabled);
wirelab_status wirelab_enforcement_release(wirelab_session*, const char* port_id);
wirelab_status wirelab_report_start(wirelab_session*, uint32_t scenario, int32_t packets,
                                    int32_t batch_size, int32_t frame_size, int32_t seed);
void           wirelab_report_step(wirelab_session*);
wirelab_report_state wirelab_report_state_view(const wirelab_session*);
wirelab_status wirelab_report_export(wirelab_session*, const char* path);

/* --- static tables (the CONSTANT Q_PROPERTYs :40,51,52,59) --- */
size_t      wirelab_backend_count(void);        wirelab_str wirelab_backend_name(size_t);
size_t      wirelab_anomaly_type_count(void);   wirelab_str wirelab_anomaly_type_name(size_t);
size_t      wirelab_policy_action_count(void);  wirelab_str wirelab_policy_action_name(size_t);
size_t      wirelab_scenario_count(void);       wirelab_str wirelab_scenario_name(size_t);

/* --- ABI self-check (D5) --- */
void wirelab_abi_layout(wirelab_abi_layout_report* out);
```

22 `Q_INVOKABLE` → 25 C functions. 30 `Q_PROPERTY` → 4 view structs + 4 static tables + count/index
accessors.

---

## 4. Rust crate layout

```
gui/
  Cargo.toml            # [[bin]] wirelab-desktop
  build.rs              # link wirelab_ffi + WireLab + wirelab_backends (+ Metal frameworks)
  src/
    main.rs             # Application::new().run(); window, menus (cx.set_menus), theme
    ffi/
      raw.rs            # extern "C" decls + #[repr(C)] mirrors  (hand-written, D5)
      mod.rs            # safe Session wrapper: !Send, RAII close, &str→CString, spans→&[T]
    app.rs              # root view: sidebar + page router + status footer
    tick.rs             # cx.spawn timers: 500 ms traffic, 16 ms report → take_dirty → notify
    theme.rs            # palette from cx.window_appearance() (replaces Qt.styleHints)
    format.rs           # number/unit/latency formatting (was JS helpers Main.qml:32-88)
    pages/
      dashboard.rs  topology.rs  traffic.rs  packets.rs  faults.rs  policies.rs  reports.rs
    widgets/
      chart.rs          # telemetry line chart  (was QML Canvas)
      topology_graph.rs # node/link canvas + click hit-testing (was QML Canvas)
      sidebar.rs        # Finder-style sidebar (was ListView + ListModel, Main.qml:320-380)
```

The two QML `Canvas` painters are the only genuinely bespoke drawing. In GPUI both are a
`canvas()` element: build `Path`s for links/series, `paint_quad` for nodes/grid, and register
mouse handlers against the same computed bounds used for painting — hit-testing becomes a
bounds test rather than QML's manual distance math.

---

## 5. Phases

Each phase ends in something runnable. No phase leaves a stub.

### Phase 0 — De-Qt the view model (C++ only, no Rust yet)
- Add `include/wirelab/session.hpp` + `src/session.cpp`: `WireLabViewModel` with `std::string`,
  typed row structs, and `uint32_t dirty_`. Port `runTrafficStep`, `rebuildTelemetryModels`,
  the report state machine, the radial layout, YAML save, JSON/CSV export (`std::ofstream`,
  hand-rolled JSON — the core already hand-rolls JSON in `control_protocol.cpp` with zero deps).
- Delete the enum↔string mappers at `wirelab_view_model.cpp:33-212`; use core `to_string`.
- Rewrite `tests/src/wirelab_view_model_test.cpp` → `tests/src/session_test.cpp`, GTest only,
  **no Qt**. Add it to `cmake/SourcesAndHeaders.cmake` test list, unguarded.
- `wirelab_gui` keeps building: `WireLabViewModel` becomes a ~200-line Qt adapter over
  `Session`. Both frontends exist simultaneously during the port.
- **Acceptance:** `ctest -R session` green; `cmake -DPROJECT_BUILD_WIRELAB_GUI=ON` still builds
  and the QML app behaves identically.

### Phase 1 — FFI shim + ABI tests
- `include/wirelab/wirelab_ffi.h`, `src/wirelab_ffi.cpp`, CMake target `wirelab_ffi`.
- `static_assert` layout checks; `wirelab_abi_layout()`.
- `tests/src/ffi_smoke_test.cpp`: open session → load `scenarios/security-lab.yaml` → start
  traffic → 10 steps → assert telemetry counts and dirty bits → run a small report to completion
  → export → close. Pure C API, no Rust.
- **Acceptance:** `ctest -R ffi` green under ASan (`WireLab_ENABLE_ASAN=ON`).

### Phase 2 — Rust skeleton + safe wrapper
- `gui/` crate, Corrosion wiring, `PROJECT_BUILD_DESKTOP` option.
- `ffi/raw.rs` + safe `Session`; `cargo test` runs the layout check against the real C++ lib.
- `main.rs`: window, sidebar, dark/light palette, empty pages, status footer.
- **Acceptance:** `cmake --build --target wirelab-desktop` produces a launching window on macOS;
  `cargo test -p wirelab-desktop` layout check green.

### Phase 3 — Data panes (parallelizable, one agent per pane)
Dashboard, Traffic, Packets & Security, Faults, Policies, Reports. All are tables + forms +
counters over `gpui-component`. Wire the tick loop (`tick.rs`) in this phase — Traffic is
meaningless without it.
- **Acceptance per pane:** side-by-side with the QML build showing identical numbers for the
  same seed/scenario (the generator is deterministic, so this is an exact comparison).

### Phase 4 — Custom drawing
Telemetry line chart and topology graph, including node/link selection and the fault overlay.
- **Acceptance:** clicking a node/link selects it and the summary matches QML; applying a fault
  redraws the link.

### Phase 5 — Native shell
Menu bar via `cx.set_menus` (replaces `Main.qml:130-149`), `rfd` dialogs, macOS bundle assembly
reusing `apps/resources/WireLab.icns`, `WIRELAB_BUILD_TYPE` provenance stamp equivalent.
- **Acceptance:** an installed `.app` opens a topology from Finder-style dialogs and exports a
  report whose JSON is byte-identical to the QML build's for the same run.

### Phase 6 — Delete Qt
- Remove `apps/wirelab_gui.cpp`, `src/gui/`, `include/wirelab/wirelab_view_model.hpp`,
  `src/wirelab_view_model.cpp`, `CMakeLists.txt:280-330`, `tests/CMakeLists.txt:194-211`,
  `PROJECT_BUILD_WIRELAB_GUI`.
- Retarget `scripts/demo_gui.sh` and `make demo-gui`.
- Update `docs/wirelab-plan.md:193-207,392-405,470-492,558-566` (the only doc describing the
  frontend) and add the frontend/FFI to `docs/class-diagram.md`, which currently documents
  neither.
- Add a `desktop-macos` CI job: install Rust, configure with `-DPROJECT_BUILD_DESKTOP=ON`,
  build, `cargo test`, `cargo clippy -- -D warnings`, `cargo fmt --check`. Existing jobs are
  untouched — they never built Qt.
- **Acceptance:** repo-wide grep for `Qt|QML|Q_OBJECT|QString` returns only historical doc
  references; full `ctest` green.

---

## 6. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| `gpui` 0.2 / `gpui-component` 0.5 are pre-1.0; breaking changes | High | Pin exact versions, commit `Cargo.lock`, upgrades are deliberate tasks |
| Borrowed spans outlive their backing `AnalysisBatch` → UAF | High | Lifetimes tied to `&Session` in the safe wrapper; mutating calls take `&mut Session`, which the borrow checker makes exclusive. ASan on the FFI smoke test |
| ABI drift between C++ and Rust mirrors | Medium | `wirelab_abi_layout()` + `cargo test` layout check (D5); ABI version fence |
| Topology canvas + chart are hand-drawn; parity is subjective | Medium | Phase 4 acceptance is behavioural (selection, fault overlay), not pixel-exact |
| GPUI on Linux is less mature than macOS | Medium | D8: macOS is the supported target; Linux best-effort, no Windows GUI |
| Phase 0 regresses the working QML app | Medium | `WireLabViewModel` retained as a thin adapter through Phase 5; both frontends buildable |
| Rust toolchain added to a C++-only CI | Low | New isolated job; existing jobs never built the GUI |

## 7. Explicit non-goals

- Live `VSwitch` attachment / the out-of-process control protocol (D1, D6). Orthogonal, later.
- Windows desktop build.
- Behavioural or visual redesign. This is a port; the 7 workspaces and their semantics
  (`docs/wirelab-plan.md:197-207`) are the spec, unchanged.
