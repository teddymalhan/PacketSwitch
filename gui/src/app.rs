//! The root view: it owns the session, routes to a workspace, and drives the
//! clock.
//!
//! The core has no clock of its own -- `step_traffic` and `step_report` are
//! explicitly step functions -- so the frontend owns the tick, exactly as the
//! QML frontend did with two `Timer` elements. The intervals are unchanged:
//! 500 ms for a traffic tick, 16 ms for a report slice.

use std::time::Duration;

use gpui::{
    App, AppContext as _, ClickEvent, Context, Entity, IntoElement, ParentElement as _, Render,
    Styled as _, Task, Window, div, prelude::*, px,
};
use gpui_component::button::{Button, ButtonVariants};
use gpui_component::input::InputState;

use crate::ffi::{Dirty, Session};
use crate::pages;
use crate::ui::color;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Page {
    Dashboard,
    Topology,
    Traffic,
    Packets,
    Faults,
    Policies,
    Reports,
}

impl Page {
    pub const ALL: [Page; 7] = [
        Page::Dashboard,
        Page::Topology,
        Page::Traffic,
        Page::Packets,
        Page::Faults,
        Page::Policies,
        Page::Reports,
    ];

    pub fn title(self) -> &'static str {
        match self {
            Page::Dashboard => "Dashboard",
            Page::Topology => "Topology",
            Page::Traffic => "Traffic Lab",
            Page::Packets => "Packets & Security",
            Page::Faults => "Fault Lab",
            Page::Policies => "Policy Lab",
            Page::Reports => "Reports",
        }
    }

    pub fn subtitle(self) -> &'static str {
        match self {
            Page::Dashboard => "Lab state at a glance",
            Page::Topology => "Nodes, links and layout",
            Page::Traffic => "Generate and measure traffic",
            Page::Packets => "Per-packet analysis and anomalies",
            Page::Faults => "Latency, loss and blackholes",
            Page::Policies => "Rules, enforcement and leases",
            Page::Reports => "Backend benchmarks and provenance",
        }
    }
}

/// Traffic-form state. Values are the same defaults the QML form opened with.
pub struct TrafficForm {
    pub scenario: String,
    pub backend: String,
    pub packets_per_tick: i32,
    pub frame_size: i32,
    pub seed: u64,
}

impl Default for TrafficForm {
    fn default() -> Self {
        Self {
            scenario: "mixed-traffic".into(),
            backend: "CPU".into(),
            packets_per_tick: 256,
            frame_size: 64,
            seed: 1,
        }
    }
}

pub struct ReportForm {
    pub scenario: String,
    pub packets: i32,
    pub batch_size: i32,
    pub frame_size: i32,
    pub seed: i32,
}

impl Default for ReportForm {
    fn default() -> Self {
        Self {
            scenario: "mixed-traffic".into(),
            packets: 100_000,
            batch_size: 64,
            frame_size: 64,
            seed: 7,
        }
    }
}

pub struct FaultForm {
    pub latency_ms: i32,
    pub loss_percent: f64,
    pub blackhole: bool,
}

impl Default for FaultForm {
    fn default() -> Self {
        Self {
            latency_ms: 25,
            loss_percent: 0.0,
            blackhole: false,
        }
    }
}

pub struct PolicyForm {
    pub anomaly_type: String,
    pub action: String,
    pub rate_limit: u64,
}

impl Default for PolicyForm {
    fn default() -> Self {
        Self {
            anomaly_type: "Broadcast storm".into(),
            action: "Quarantine".into(),
            rate_limit: 0,
        }
    }
}

/// Text fields, which need entity-backed state of their own.
pub struct Inputs {
    pub node_id: Entity<InputState>,
    pub link_from: Entity<InputState>,
    pub link_to: Entity<InputState>,
    pub link_latency: Entity<InputState>,
    pub policy_name: Entity<InputState>,
}

impl Inputs {
    fn new(window: &mut Window, cx: &mut App) -> Self {
        Self {
            node_id: cx.new(|cx| InputState::new(window, cx).placeholder("node id")),
            link_from: cx.new(|cx| InputState::new(window, cx).placeholder("from")),
            link_to: cx.new(|cx| InputState::new(window, cx).placeholder("to")),
            link_latency: cx.new(|cx| InputState::new(window, cx).default_value("1")),
            policy_name: cx.new(|cx| InputState::new(window, cx).placeholder("rule name")),
        }
    }
}

pub struct WireLab {
    pub session: Session,
    pub page: Page,
    pub traffic: TrafficForm,
    pub report: ReportForm,
    pub fault: FaultForm,
    pub policy: PolicyForm,
    pub inputs: Inputs,
    pub backends: Vec<String>,
    pub scenarios: Vec<String>,
    pub anomaly_types: Vec<String>,
    pub policy_actions: Vec<String>,
    /// Dropping a task cancels it, so the ticks have to be held.
    traffic_tick: Option<Task<()>>,
    report_tick: Option<Task<()>>,
}

impl WireLab {
    pub fn new(window: &mut Window, cx: &mut Context<Self>) -> Self {
        let session = Session::open().expect(
            "the WireLab library speaks a different ABI than this binding was built against",
        );
        let backends = crate::ffi::backend_names();
        let traffic = TrafficForm {
            backend: backends.first().cloned().unwrap_or_else(|| "CPU".into()),
            ..TrafficForm::default()
        };
        Self {
            session,
            page: Page::Dashboard,
            traffic,
            report: ReportForm::default(),
            fault: FaultForm::default(),
            policy: PolicyForm::default(),
            inputs: Inputs::new(window, cx),
            backends,
            scenarios: crate::ffi::scenario_names(),
            anomaly_types: crate::ffi::anomaly_type_names(),
            policy_actions: crate::ffi::policy_action_names(),
            traffic_tick: None,
            report_tick: None,
        }
    }

    /// Drains the session's dirty mask and repaints when anything changed. Every
    /// command goes through here; nothing else calls `notify`.
    pub fn settle(&mut self, cx: &mut Context<Self>) {
        if !self.session.take_dirty().is_empty() {
            cx.notify();
        }
        self.ensure_ticks(cx);
    }

    /// Commands are `&mut Session` calls; this wraps the "run it, then settle"
    /// pattern every button needs.
    pub fn command(&mut self, cx: &mut Context<Self>, body: impl FnOnce(&mut Session)) {
        body(&mut self.session);
        self.settle(cx);
    }

    fn ensure_ticks(&mut self, cx: &mut Context<Self>) {
        if self.session.traffic_running() && self.traffic_tick.is_none() {
            self.traffic_tick = Some(cx.spawn(async move |this, cx| {
                loop {
                    // The executor's timer, not smol's: it is the clock gpui
                    // owns, so a test can advance it.
                    cx.background_executor()
                        .timer(Duration::from_millis(500))
                        .await;
                    let running = this
                        .update(cx, |this, cx| {
                            if !this.session.traffic_running() {
                                return false;
                            }
                            this.session.step_traffic();
                            if !this.session.take_dirty().is_empty() {
                                cx.notify();
                            }
                            true
                        })
                        // An `Err` means the view is gone, which is the only
                        // other reason to stop ticking.
                        .unwrap_or(false);
                    if !running {
                        break;
                    }
                }
                let _ = this.update(cx, |this, _| this.traffic_tick = None);
            }));
        }

        if self.session.report_running() && self.report_tick.is_none() {
            self.report_tick = Some(cx.spawn(async move |this, cx| {
                loop {
                    cx.background_executor()
                        .timer(Duration::from_millis(16))
                        .await;
                    let running = this
                        .update(cx, |this, cx| {
                            if !this.session.report_running() {
                                return false;
                            }
                            this.session.step_report();
                            if !this.session.take_dirty().is_empty() {
                                cx.notify();
                            }
                            true
                        })
                        .unwrap_or(false);
                    if !running {
                        break;
                    }
                }
                let _ = this.update(cx, |this, _| this.report_tick = None);
            }));
        }
    }

    pub fn open_topology_dialog(&mut self, cx: &mut Context<Self>) {
        let paths = cx.prompt_for_paths(gpui::PathPromptOptions {
            files: true,
            directories: false,
            multiple: false,
            prompt: Some("Open".into()),
        });
        cx.spawn(async move |this, cx| {
            let Ok(Ok(Some(selected))) = paths.await else {
                return;
            };
            let Some(path) = selected.first().map(|path| path.display().to_string()) else {
                return;
            };
            let _ = this.update(cx, |this, cx| {
                this.command(cx, |session| session.open_topology(&path));
            });
        })
        .detach();
    }

    pub fn save_topology_dialog(&mut self, cx: &mut Context<Self>) {
        let path = cx.prompt_for_new_path(&std::env::temp_dir(), Some("topology.yaml"));
        cx.spawn(async move |this, cx| {
            let Ok(Ok(Some(selected))) = path.await else {
                return;
            };
            let selected = selected.display().to_string();
            let _ = this.update(cx, |this, cx| {
                this.command(cx, |session| session.save_topology(&selected));
            });
        })
        .detach();
    }

    pub fn export_report_dialog(&mut self, cx: &mut Context<Self>) {
        let path = cx.prompt_for_new_path(&std::env::temp_dir(), Some("wirelab-report.json"));
        cx.spawn(async move |this, cx| {
            let Ok(Ok(Some(selected))) = path.await else {
                return;
            };
            let selected = selected.display().to_string();
            let _ = this.update(cx, |this, cx| {
                this.command(cx, |session| {
                    session.export_report(&selected);
                });
            });
        })
        .detach();
    }

    fn sidebar(&self, cx: &mut Context<Self>) -> impl IntoElement {
        let page = self.page;
        div()
            .flex()
            .flex_col()
            .w(px(212.))
            .h_full()
            .p_2()
            .gap_1()
            .bg(color::surface())
            .border_r_1()
            .border_color(color::border())
            .child(
                div()
                    .px_2()
                    .py_3()
                    .text_lg()
                    .text_color(color::text())
                    .child("WireLab"),
            )
            .children(Page::ALL.into_iter().enumerate().map(move |(index, item)| {
                div()
                    .id(("workspace", index))
                    .flex()
                    .flex_col()
                    .px_2()
                    .py_1p5()
                    .rounded_md()
                    .when(item == page, |element| element.bg(color::raised()))
                    .child(
                        div()
                            .text_sm()
                            .text_color(if item == page {
                                color::text()
                            } else {
                                color::muted()
                            })
                            .child(item.title()),
                    )
                    .on_click(cx.listener(move |this, _: &ClickEvent, _window, cx| {
                        this.page = item;
                        cx.notify();
                    }))
            }))
    }

    fn header(&self, cx: &mut Context<Self>) -> impl IntoElement {
        let has_topology = self.session.has_topology();
        let topology_name = if has_topology {
            self.session.topology_name().to_owned()
        } else {
            "No topology loaded".to_owned()
        };
        div()
            .flex()
            .flex_row()
            .items_center()
            .justify_between()
            .w_full()
            .px_4()
            .py_3()
            .bg(color::surface())
            .border_b_1()
            .border_color(color::border())
            .child(
                div()
                    .flex()
                    .flex_col()
                    .child(
                        div()
                            .text_lg()
                            .text_color(color::text())
                            .child(self.page.title()),
                    )
                    .child(
                        div()
                            .text_xs()
                            .text_color(color::muted())
                            .child(self.page.subtitle()),
                    ),
            )
            .child(
                div()
                    .flex()
                    .flex_row()
                    .items_center()
                    .gap_2()
                    .child(
                        div()
                            .text_sm()
                            .text_color(color::muted())
                            .child(topology_name),
                    )
                    .child(
                        Button::new("open-topology")
                            .label("Open…")
                            .ghost()
                            .on_click(cx.listener(|this, _: &ClickEvent, _window, cx| {
                                this.open_topology_dialog(cx);
                            })),
                    )
                    .child(
                        Button::new("save-topology")
                            .label("Save…")
                            .ghost()
                            .on_click(cx.listener(|this, _: &ClickEvent, _window, cx| {
                                this.save_topology_dialog(cx);
                            })),
                    ),
            )
    }

    fn footer(&self) -> impl IntoElement {
        let status = self.session.status_message();
        let status = if status.is_empty() { "Ready." } else { status };
        div()
            .flex()
            .flex_row()
            .items_center()
            .justify_between()
            .w_full()
            .px_4()
            .py_2()
            .bg(color::surface())
            .border_t_1()
            .border_color(color::border())
            .child(
                div()
                    .text_sm()
                    .text_color(color::muted())
                    .child(status.to_owned()),
            )
            .child(div().text_xs().text_color(color::muted()).child(
                if self.session.traffic_running() {
                    format!("traffic running on {}", self.session.active_backend())
                } else {
                    "traffic stopped".to_owned()
                },
            ))
    }
}

impl Render for WireLab {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        div()
            .flex()
            .flex_row()
            .size_full()
            .bg(color::background())
            .text_color(color::text())
            .child(self.sidebar(cx))
            .child(
                div()
                    .flex()
                    .flex_col()
                    .flex_1()
                    .h_full()
                    .child(self.header(cx))
                    .child(
                        div()
                            .id("workspace-body")
                            .flex()
                            .flex_col()
                            .flex_1()
                            .w_full()
                            .p_4()
                            .gap_3()
                            .overflow_y_scroll()
                            .child(pages::render(self, window, cx)),
                    )
                    .child(self.footer()),
            )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use gpui::TestAppContext;

    fn scenario_path() -> String {
        format!(
            "{}/../scenarios/security-lab.yaml",
            env!("CARGO_MANIFEST_DIR")
        )
    }

    /// Draws every workspace headlessly.
    ///
    /// Screen capture is unavailable in this environment, so this is the
    /// verification that the view tree actually builds and paints. It catches
    /// what a compile cannot: a missing theme global, an element that panics
    /// during prepaint, a page that reads the session while mutating it.
    #[gpui::test]
    fn draws_every_workspace(cx: &mut TestAppContext) {
        cx.update(|cx| gpui_component::init(cx));
        let (lab, cx) = cx.add_window_view(|window, cx| WireLab::new(window, cx));

        // A loaded topology and one traffic tick populate every table, so the
        // pages are drawn with data rather than in their empty state.
        lab.update(cx, |lab, cx| {
            let scenario = scenario_path();
            lab.command(cx, |session| session.open_topology(&scenario));
            lab.command(cx, |session| session.select_node("client-a"));
            lab.command(cx, |session| session.apply_selected_fault(25, 1.0, false));
            lab.command(cx, |session| {
                session.add_policy("storm-guard", "Broadcast storm", "Quarantine", 0)
            });
            lab.command(cx, |session| {
                session.start_traffic("mixed-traffic", 64, 128, 42, "CPU")
            });
            lab.command(cx, |session| session.step_traffic());
            lab.command(cx, |session| session.stop_traffic());

            assert!(
                lab.session.has_topology(),
                "{}",
                lab.session.status_message()
            );
            assert!(
                lab.session.packets().len() > 0,
                "a tick must produce packet rows"
            );
            assert_eq!(lab.session.faults().len(), 1);
            assert_eq!(lab.session.policies().len(), 1);
        });

        for page in Page::ALL {
            lab.update(cx, |lab, cx| {
                lab.page = page;
                cx.notify();
            });
            cx.run_until_parked();
            // Drawing the window exercises the real root view, so every element
            // sees the rendered-entity stack it expects.
            cx.update(|window, cx| {
                let _frame = window.draw(cx);
            });
        }
    }

    /// The traffic tick is the other half of the clock the frontend owns: the
    /// core never advances a simulation on its own, so a stopped tick is a
    /// frozen lab that still claims to be running.
    #[gpui::test]
    fn ticks_traffic_until_it_is_stopped(cx: &mut TestAppContext) {
        cx.update(|cx| gpui_component::init(cx));
        let (lab, cx) = cx.add_window_view(|window, cx| WireLab::new(window, cx));

        lab.update(cx, |lab, cx| {
            let scenario = scenario_path();
            lab.command(cx, |session| session.open_topology(&scenario));
            lab.command(cx, |session| {
                session.start_traffic("mixed-traffic", 32, 64, 7, "CPU")
            });
            assert!(
                lab.session.traffic_running(),
                "{}",
                lab.session.status_message()
            );
        });

        for _ in 0..3 {
            cx.executor()
                .advance_clock(std::time::Duration::from_millis(500));
            cx.run_until_parked();
        }
        let ticked = lab.read_with(cx, |lab, _| lab.session.metrics_history().len());
        assert!(ticked >= 3, "expected at least three ticks, saw {ticked}");

        lab.update(cx, |lab, cx| {
            lab.command(cx, |session| session.stop_traffic())
        });
        for _ in 0..3 {
            cx.executor()
                .advance_clock(std::time::Duration::from_millis(500));
            cx.run_until_parked();
        }
        let after_stop = lab.read_with(cx, |lab, _| lab.session.metrics_history().len());
        assert_eq!(after_stop, ticked, "a stopped run must not keep generating");
    }

    /// The benchmark report is the one long-running flow the frontend drives,
    /// so its in-progress and finished states are drawn too, and the 16 ms tick
    /// task is proven to carry a report to completion on its own.
    #[gpui::test]
    fn draws_the_reports_workspace_while_a_report_runs(cx: &mut TestAppContext) {
        cx.update(|cx| gpui_component::init(cx));
        let (lab, cx) = cx.add_window_view(|window, cx| WireLab::new(window, cx));

        lab.update(cx, |lab, cx| {
            lab.page = Page::Reports;
            lab.command(cx, |session| {
                session.start_report("port-scan", 256, 32, 64, 3)
            });
            assert!(
                lab.session.report_running(),
                "{}",
                lab.session.status_message()
            );
        });
        cx.update(|window, cx| {
            let _frame = window.draw(cx);
        });

        // The 16 ms tick is a real timer, so the test clock has to be moved for
        // it to fire; run_until_parked alone never advances time.
        for _ in 0..200 {
            cx.executor()
                .advance_clock(std::time::Duration::from_millis(20));
            cx.run_until_parked();
            if !lab.read_with(cx, |lab, _| lab.session.report_running()) {
                break;
            }
        }
        lab.update(cx, |lab, _| {
            assert!(
                !lab.session.report_running(),
                "the tick task must finish the report; progress={} stage={} rows={}",
                lab.session.report_progress(),
                lab.session.report_stage(),
                lab.session.report_rows().len()
            );
            assert_eq!(
                lab.session.report_rows().len(),
                crate::ffi::backend_names().len()
            );
        });
        cx.update(|window, cx| {
            let _frame = window.draw(cx);
        });
    }
}
