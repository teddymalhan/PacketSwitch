//! Reports: the benchmark workspace.
//!
//! A report is a claim about how fast a backend is, so this page shows the run
//! form, the comparison table and the provenance of the numbers side by side --
//! a figure without its scenario, seed and build type is not quotable.

use gpui::{
    App, ClickEvent, Context, IntoElement, ParentElement as _, Styled as _, Window, div,
    prelude::*, px, relative,
};
use gpui_component::Disableable as _;
use gpui_component::button::{Button, ButtonVariants};

use crate::app::WireLab;
use crate::format;
use crate::ui::{Column, Table, card, choices, color, field};

pub fn render(
    lab: &mut WireLab,
    _window: &mut Window,
    cx: &mut Context<WireLab>,
) -> impl IntoElement {
    let session = &lab.session;

    let running = session.report_running();
    let progress = session.report_progress().clamp(0.0, 1.0);
    let stage = session.report_stage().to_owned();
    let export_path = session.report_export_path().to_owned();
    let provenance = session.report_provenance();

    // Owned before anything takes `&mut lab`: every row borrows session storage
    // that the next command invalidates.
    let rows: Vec<Vec<String>> = session
        .report_rows()
        .map(|row| {
            vec![
                row.backend_label.to_owned(),
                row.scenario.to_owned(),
                format::integer(row.packets),
                format::duration_ns(row.elapsed_ns),
                format::packets_per_second(row.packets_per_second),
                format::bits_per_second(row.goodput_bits_per_second),
                format::percent(row.loss_percent),
                format::duration_ns(row.latency_p50_ns),
                format::duration_ns(row.latency_p95_ns),
                format::duration_ns(row.latency_p99_ns),
                format::duration_ns(row.host_to_device_ns),
                format::duration_ns(row.kernel_ns),
                format::duration_ns(row.device_to_host_ns),
                format::duration_ns(row.transfer_inclusive_ns),
                format::duration_ns(row.queue_wait_ns),
                format!("{:.2}x", row.speedup),
            ]
        })
        .collect();
    let has_rows = !rows.is_empty();

    let scenario = lab.report.scenario.clone();
    let scenarios: Vec<(String, String)> = lab
        .scenarios
        .iter()
        .map(|name| (name.clone(), name.clone()))
        .collect();
    let packets = lab.report.packets;
    let batch_size = lab.report.batch_size;
    let frame_size = lab.report.frame_size;
    let seed = lab.report.seed;

    let view = cx.entity();

    div()
        .flex()
        .flex_col()
        .w_full()
        .gap_3()
        .child(card(
            "Run",
            div()
                .flex()
                .flex_col()
                .gap_3()
                .child(field(
                    "Scenario",
                    choices(
                        "report-scenario",
                        scenarios,
                        scenario,
                        move |picked, _window, cx| {
                            view.update(cx, |this, cx| {
                                this.report.scenario = picked;
                                cx.notify();
                            });
                        },
                    ),
                ))
                .child(
                    div()
                        .flex()
                        .flex_row()
                        .flex_wrap()
                        .gap_3()
                        .child(field(
                            "Packets",
                            stepper(
                                "report-packets",
                                format::integer(packets.max(0) as u64),
                                cx.listener(|this, _: &ClickEvent, _window, cx| {
                                    this.report.packets = (this.report.packets / 2).max(256);
                                    cx.notify();
                                }),
                                cx.listener(|this, _: &ClickEvent, _window, cx| {
                                    this.report.packets =
                                        this.report.packets.saturating_mul(2).min(10_000_000);
                                    cx.notify();
                                }),
                            ),
                        ))
                        .child(field(
                            "Batch size",
                            stepper(
                                "report-batch",
                                format::integer(batch_size.max(0) as u64),
                                cx.listener(|this, _: &ClickEvent, _window, cx| {
                                    this.report.batch_size = (this.report.batch_size / 2).max(1);
                                    cx.notify();
                                }),
                                cx.listener(|this, _: &ClickEvent, _window, cx| {
                                    this.report.batch_size =
                                        this.report.batch_size.saturating_mul(2).min(8192);
                                    cx.notify();
                                }),
                            ),
                        ))
                        .child(field(
                            "Frame bytes",
                            stepper(
                                "report-frame",
                                format::integer(frame_size.max(0) as u64),
                                cx.listener(|this, _: &ClickEvent, _window, cx| {
                                    this.report.frame_size =
                                        (this.report.frame_size - 8).clamp(14, 9000);
                                    cx.notify();
                                }),
                                cx.listener(|this, _: &ClickEvent, _window, cx| {
                                    this.report.frame_size =
                                        (this.report.frame_size + 8).clamp(14, 9000);
                                    cx.notify();
                                }),
                            ),
                        ))
                        .child(field(
                            "Seed",
                            stepper(
                                "report-seed",
                                seed.to_string(),
                                cx.listener(|this, _: &ClickEvent, _window, cx| {
                                    this.report.seed = (this.report.seed - 1).max(0);
                                    cx.notify();
                                }),
                                cx.listener(|this, _: &ClickEvent, _window, cx| {
                                    this.report.seed = this.report.seed.saturating_add(1);
                                    cx.notify();
                                }),
                            ),
                        )),
                )
                // A second run while one is in flight would interleave two
                // measurements, so the button leaves rather than greys out.
                .when(!running, |element| {
                    element.child(
                        div().flex().flex_row().child(
                            Button::new("report-run")
                                .label("Run report")
                                .primary()
                                .on_click(cx.listener(|this, _: &ClickEvent, _window, cx| {
                                    let scenario = this.report.scenario.clone();
                                    let packets = this.report.packets;
                                    let batch_size = this.report.batch_size;
                                    let frame_size = this.report.frame_size;
                                    let seed = this.report.seed;
                                    this.command(cx, move |session| {
                                        session.start_report(
                                            &scenario, packets, batch_size, frame_size, seed,
                                        );
                                    });
                                })),
                        ),
                    )
                }),
        ))
        .when(running, |element| {
            element.child(card(
                "Progress",
                div()
                    .flex()
                    .flex_col()
                    .gap_2()
                    .child(
                        div()
                            .text_sm()
                            .text_color(color::text())
                            .child(stage.clone()),
                    )
                    .child(
                        div()
                            .w_full()
                            .h(px(8.))
                            .rounded_md()
                            .bg(color::raised())
                            .child(
                                div()
                                    .h_full()
                                    .w(relative(progress as f32))
                                    .rounded_md()
                                    .bg(color::accent()),
                            ),
                    )
                    .child(
                        div()
                            .text_xs()
                            .text_color(color::muted())
                            .child(format::percent(progress * 100.0)),
                    ),
            ))
        })
        .when(!running && has_rows, |element| {
            element.child(card(
                "Progress",
                div()
                    .text_sm()
                    .text_color(color::text())
                    .child(stage.clone()),
            ))
        })
        .child(card(
            "CPU / GPU comparison",
            Table::new(vec![
                Column::new("Backend", 110.),
                Column::new("Scenario", 130.),
                Column::new("Packets", 100.).numeric(),
                Column::new("Elapsed", 100.).numeric(),
                Column::new("Packets/s", 120.).numeric(),
                Column::new("Goodput", 120.).numeric(),
                Column::new("Loss", 90.).numeric(),
                Column::new("p50", 90.).numeric(),
                Column::new("p95", 90.).numeric(),
                Column::new("p99", 90.).numeric(),
                Column::new("H2D", 90.).numeric(),
                Column::new("Kernel", 90.).numeric(),
                Column::new("D2H", 90.).numeric(),
                Column::new("Transfer", 90.).numeric(),
                Column::new("Queue", 90.).numeric(),
                Column::new("Speedup", 90.).numeric(),
            ])
            .empty_message("Run a report to compare the backends.")
            .rows(rows),
        ))
        .child(card(
            "Configuration provenance",
            div()
                .flex()
                .flex_col()
                .gap_1()
                .child(entry("Scenario", provenance.scenario))
                .child(entry("Seed", provenance.seed.to_string()))
                .child(entry("Packets", format::integer(provenance.packets)))
                .child(entry("Batch size", format::integer(provenance.batch_size)))
                .child(entry("Frame bytes", format::integer(provenance.frame_size)))
                .child(entry("Hosts", format::integer(provenance.host_count)))
                .child(entry("Generator", provenance.generator))
                .child(entry("WireLab", provenance.version))
                .child(entry("Build", provenance.build_type))
                .child(entry("Generated", provenance.generated_at))
                .child(entry(
                    "Compiled in",
                    provenance.backends_compiled_in.join(", "),
                ))
                .child(entry(
                    "On this machine",
                    provenance.backends_present.join(", "),
                )),
        ))
        .child(card(
            "Export",
            div()
                .flex()
                .flex_col()
                .gap_2()
                .child(
                    div().flex().flex_row().child(
                        Button::new("report-export")
                            .label("Export…")
                            .outline()
                            // Exporting mid-run would write a half-measured
                            // table; exporting nothing would write an empty one.
                            .disabled(running || !has_rows)
                            .on_click(cx.listener(|this, _: &ClickEvent, _window, cx| {
                                this.export_report_dialog(cx);
                            })),
                    ),
                )
                .when(!export_path.is_empty(), |element| {
                    element.child(
                        div()
                            .text_xs()
                            .text_color(color::muted())
                            .child(export_path.clone()),
                    )
                }),
        ))
}

/// A number with a `-`/`+` pair. The steps differ per field, so the arithmetic
/// stays in the caller's listeners and only the shape lives here.
fn stepper(
    id: &'static str,
    value: String,
    on_down: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static,
    on_up: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static,
) -> impl IntoElement {
    div()
        .flex()
        .flex_row()
        .items_center()
        .gap_1()
        .child(
            Button::new((id, 0usize))
                .label("-")
                .ghost()
                .on_click(on_down),
        )
        .child(
            div()
                .w(px(96.))
                .text_sm()
                .text_right()
                .text_color(color::text())
                .child(value),
        )
        .child(Button::new((id, 1usize)).label("+").ghost().on_click(on_up))
}

/// One provenance label/value pair.
fn entry(label: &'static str, value: String) -> impl IntoElement {
    div()
        .flex()
        .flex_row()
        .gap_2()
        .child(
            div()
                .w(px(140.))
                .text_xs()
                .text_color(color::muted())
                .child(label),
        )
        .child(
            div()
                .flex_1()
                .text_sm()
                .text_color(color::text())
                .child(value),
        )
}
