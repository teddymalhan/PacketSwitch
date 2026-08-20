//! Traffic Lab: start and stop a generator, watch the metrics it produces, and
//! read the switch state it teaches -- the MAC table and the port counters.
//!
//! The form values live on `lab.traffic` rather than in the session: the core
//! only learns them when Start is pressed, exactly as the QML form behaved.

use gpui::{
    App, BorderStyle, Bounds, ClickEvent, Context, IntoElement, ParentElement as _, PathBuilder,
    Pixels, Styled as _, Window, canvas, div, fill, point, prelude::*, px, quad, size,
};
use gpui_component::button::{Button, ButtonVariants};

use crate::app::WireLab;
use crate::format;
use crate::ui::{Column, Table, card, choices, color, field, stat};

/// Inset of the plot area inside the canvas bounds, in pixels.
const CHART_PADDING: f32 = 8.0;
/// Horizontal grid lines, including the top and bottom rules.
const GRID_LINES: usize = 4;

pub fn render(
    lab: &mut WireLab,
    _window: &mut Window,
    cx: &mut Context<WireLab>,
) -> impl IntoElement {
    let session = &lab.session;

    let running = session.traffic_running();
    let result = session.traffic_result().to_owned();

    // The paint closure outlives this borrow of the session, so the series has
    // to be owned before the canvas is built.
    let history = session.metrics_history();
    let series: Vec<f64> = history
        .iter()
        .map(|sample| sample.throughput_mbps)
        .collect();
    let latest = history.last().copied();
    let throughput = latest.map(|sample| sample.throughput_mbps).unwrap_or(0.0);
    let latency = latest.map(|sample| sample.latency_ms).unwrap_or(0.0);
    let loss = latest.map(|sample| sample.loss_percent).unwrap_or(0.0);

    let mac_rows: Vec<Vec<String>> = session
        .mac_table()
        .map(|entry| vec![entry.mac.to_owned(), entry.port.to_owned()])
        .collect();

    let port_rows: Vec<Vec<String>> = session
        .port_states()
        .map(|port| {
            vec![
                port.id.to_owned(),
                if port.enforced { "ENFORCED" } else { "UP" }.to_owned(),
                format::integer(port.received),
                format::integer(port.forwarded),
                format::integer(port.dropped),
            ]
        })
        .collect();

    let scenarios: Vec<(String, String)> = lab
        .scenarios
        .iter()
        .map(|name| (name.clone(), name.clone()))
        .collect();
    let backends: Vec<(String, String)> = lab
        .backends
        .iter()
        .map(|name| (name.clone(), name.clone()))
        .collect();
    let scenario = lab.traffic.scenario.clone();
    let backend = lab.traffic.backend.clone();
    let packets_per_tick = lab.traffic.packets_per_tick;
    let frame_size = lab.traffic.frame_size;
    let seed = lab.traffic.seed;

    // `choices` hands its callback a plain `&mut App`, so reaching the view
    // needs the entity handle rather than `cx.listener`.
    let scenario_view = cx.entity();
    let backend_view = cx.entity();

    let controls = div()
        .flex()
        .flex_col()
        .gap_3()
        .child(field(
            "Scenario",
            choices(
                "traffic-scenario",
                scenarios,
                scenario,
                move |value, _window, cx| {
                    scenario_view.update(cx, |lab, cx| {
                        lab.traffic.scenario = value;
                        cx.notify();
                    });
                },
            ),
        ))
        .child(field(
            "Analyzer backend",
            choices(
                "traffic-backend",
                backends,
                backend,
                move |value, _window, cx| {
                    backend_view.update(cx, |lab, cx| {
                        lab.traffic.backend = value;
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
                .gap_4()
                .child(stepper(
                    "traffic-packets",
                    "Packets / 500 ms",
                    packets_per_tick.to_string(),
                    cx.listener(|this: &mut WireLab, _: &ClickEvent, _window, cx| {
                        this.traffic.packets_per_tick = (this.traffic.packets_per_tick - 32).max(1);
                        cx.notify();
                    }),
                    cx.listener(|this: &mut WireLab, _: &ClickEvent, _window, cx| {
                        this.traffic.packets_per_tick =
                            this.traffic.packets_per_tick.saturating_add(32);
                        cx.notify();
                    }),
                ))
                .child(stepper(
                    "traffic-frame",
                    "Frame bytes",
                    frame_size.to_string(),
                    cx.listener(|this: &mut WireLab, _: &ClickEvent, _window, cx| {
                        // 14 bytes is an Ethernet header; anything smaller is not a frame.
                        this.traffic.frame_size = (this.traffic.frame_size - 8).clamp(14, 9000);
                        cx.notify();
                    }),
                    cx.listener(|this: &mut WireLab, _: &ClickEvent, _window, cx| {
                        this.traffic.frame_size =
                            this.traffic.frame_size.saturating_add(8).clamp(14, 9000);
                        cx.notify();
                    }),
                ))
                .child(stepper(
                    "traffic-seed",
                    "Seed",
                    seed.to_string(),
                    cx.listener(|this: &mut WireLab, _: &ClickEvent, _window, cx| {
                        this.traffic.seed = this.traffic.seed.saturating_sub(1).max(1);
                        cx.notify();
                    }),
                    cx.listener(|this: &mut WireLab, _: &ClickEvent, _window, cx| {
                        this.traffic.seed = this.traffic.seed.saturating_add(1);
                        cx.notify();
                    }),
                )),
        )
        .child(
            div()
                .flex()
                .flex_row()
                .gap_2()
                .when(!running, |row| {
                    row.child(
                        Button::new("traffic-start")
                            .label("Start")
                            .primary()
                            .on_click(cx.listener(
                                |this: &mut WireLab, _: &ClickEvent, _window, cx| {
                                    // Copied out first: `command` needs `&mut self`.
                                    let scenario = this.traffic.scenario.clone();
                                    let backend = this.traffic.backend.clone();
                                    let packets = this.traffic.packets_per_tick;
                                    let frame = this.traffic.frame_size;
                                    let seed = this.traffic.seed;
                                    this.command(cx, |session| {
                                        session.start_traffic(
                                            &scenario, packets, frame, seed, &backend,
                                        )
                                    });
                                },
                            )),
                    )
                })
                .when(running, |row| {
                    row.child(Button::new("traffic-stop").label("Stop").danger().on_click(
                        cx.listener(|this: &mut WireLab, _: &ClickEvent, _window, cx| {
                            this.command(cx, |session| session.stop_traffic());
                        }),
                    ))
                }),
        );

    div()
        .flex()
        .flex_col()
        .w_full()
        .gap_3()
        .child(card("Generator", controls))
        .child(card(
            "Throughput",
            div().flex().flex_col().gap_2().child(chart(series)).child(
                div()
                    .flex()
                    .flex_row()
                    .flex_wrap()
                    .gap_2()
                    .child(stat(
                        "Throughput",
                        format::megabits(throughput),
                        color::accent(),
                    ))
                    .child(stat(
                        "Latency",
                        format::milliseconds(latency),
                        color::accent(),
                    ))
                    .child(stat(
                        "Loss",
                        format::percent(loss),
                        if loss > 0.0 {
                            color::warn()
                        } else {
                            color::good()
                        },
                    )),
            ),
        ))
        .child(card(
            "Run",
            div().text_sm().text_color(color::text()).child(result),
        ))
        .child(card(
            "Forwarding / MAC table",
            Table::new(vec![
                Column::new("MAC address", 220.),
                Column::new("Learned port", 160.),
            ])
            .empty_message("No learned addresses.")
            .rows(mac_rows),
        ))
        .child(card(
            "Port counters",
            Table::new(vec![
                Column::new("Port", 160.),
                Column::new("State", 100.),
                Column::new("RX", 120.).numeric(),
                Column::new("TX", 120.).numeric(),
                Column::new("Drop", 120.).numeric(),
            ])
            .empty_message("Load a topology to see its ports.")
            .rows(port_rows),
        ))
}

/// Label, current value and a pair of nudge buttons. The numeric form fields
/// have no text input of their own, so this is the whole control.
fn stepper(
    id: &'static str,
    label: &'static str,
    value: String,
    decrease: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static,
    increase: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static,
) -> impl IntoElement {
    field(
        label,
        div()
            .flex()
            .flex_row()
            .items_center()
            .gap_1()
            .child(
                Button::new((id, 0usize))
                    .label("-")
                    .ghost()
                    .on_click(decrease),
            )
            .child(
                div()
                    .w(px(72.))
                    .text_sm()
                    .text_right()
                    .text_color(color::text())
                    .child(value),
            )
            .child(
                Button::new((id, 1usize))
                    .label("+")
                    .ghost()
                    .on_click(increase),
            ),
    )
}

/// The throughput series as a stroked polyline over a ruled plot area.
///
/// The series is owned: the paint closure runs on every frame, long after the
/// `&Session` that produced the samples has been released.
fn chart(series: Vec<f64>) -> impl IntoElement {
    canvas(
        |bounds, _window, _cx| bounds,
        move |bounds: Bounds<Pixels>, _prepainted: Bounds<Pixels>, window, _cx| {
            window.paint_quad(quad(
                bounds,
                px(4.),
                color::surface(),
                px(1.),
                color::border(),
                BorderStyle::Solid,
            ));

            let left = f32::from(bounds.origin.x) + CHART_PADDING;
            let top = f32::from(bounds.origin.y) + CHART_PADDING;
            let width = f32::from(bounds.size.width) - CHART_PADDING * 2.0;
            let height = f32::from(bounds.size.height) - CHART_PADDING * 2.0;
            if width <= 0.0 || height <= 0.0 {
                return;
            }

            for index in 0..GRID_LINES {
                let y = top + height * (index as f32 / (GRID_LINES - 1) as f32);
                window.paint_quad(fill(
                    Bounds {
                        origin: point(px(left), px(y)),
                        size: size(px(width), px(1.)),
                    },
                    color::border(),
                ));
            }

            // A single point has no segment, and `build` rejects an empty path.
            if series.len() < 2 {
                return;
            }
            // A flat all-zero series still needs a finite divisor.
            let peak = series
                .iter()
                .copied()
                .fold(0.0f64, f64::max)
                .max(f64::EPSILON);

            let last = (series.len() - 1) as f32;
            let mut builder = PathBuilder::stroke(px(1.5));
            for (index, value) in series.iter().enumerate() {
                let x = left + width * (index as f32 / last);
                let ratio = (value / peak).clamp(0.0, 1.0) as f32;
                let at = point(px(x), px(top + height * (1.0 - ratio)));
                if index == 0 {
                    builder.move_to(at);
                } else {
                    builder.line_to(at);
                }
            }
            if let Ok(path) = builder.build() {
                window.paint_path(path, color::accent());
            }
        },
    )
    .h(px(200.))
    .w_full()
}
