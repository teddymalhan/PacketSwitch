//! Fault Lab: inject latency, loss and blackholes into the current selection.
//!
//! Every fault here applies to whatever the Topology workspace has selected --
//! there is no target picker on this page, which is the one thing an operator
//! gets wrong. The selection card is therefore first, loudest, and says outright
//! what to do when nothing is selected.

use gpui::{
    App, ClickEvent, Context, Div, Hsla, IntoElement, ParentElement as _, Styled as _, Window, div,
    hsla, prelude::*, px,
};
use gpui_component::button::{Button, ButtonVariants};

use crate::app::WireLab;
use crate::ffi::SelectionKind;
use crate::format;
use crate::ui::{Column, Table, card, color, field, pill};

/// One active fault, owned. The session's `Fault` view borrows from the session,
/// and the Clear button needs its target to outlive this frame's `&mut` calls,
/// so the whole list is copied out before a single element is built.
struct FaultRow {
    target: String,
    kind: String,
    latency: String,
    loss: String,
    blackhole: bool,
    first: String,
    second: String,
}

const TARGET_WIDTH: f32 = 220.;
const KIND_WIDTH: f32 = 80.;
const LATENCY_WIDTH: f32 = 100.;
const LOSS_WIDTH: f32 = 100.;
const BLACKHOLE_WIDTH: f32 = 100.;
const CLEAR_WIDTH: f32 = 90.;

pub fn render(
    lab: &mut WireLab,
    _window: &mut Window,
    cx: &mut Context<WireLab>,
) -> impl IntoElement {
    let session = &lab.session;

    let kind = session.selection_kind();
    let selected = !matches!(kind, SelectionKind::None);
    let selection_kind = match kind {
        SelectionKind::None => "NOTHING SELECTED",
        SelectionKind::Node => "HOST",
        SelectionKind::Link => "LINK",
    };
    let selected_id = session.selected_id().to_owned();
    let selected_summary = session.selected_summary().to_owned();

    let fault_rows: Vec<FaultRow> = session
        .faults()
        .map(|fault| FaultRow {
            target: if fault.is_link {
                format!("{} ↔ {}", fault.first, fault.second)
            } else {
                fault.first.to_owned()
            },
            kind: if fault.is_link { "Link" } else { "Port" }.to_owned(),
            latency: format!("{} ms", fault.latency_ms),
            loss: format::percent(fault.loss_percent),
            blackhole: fault.blackhole,
            first: fault.first.to_owned(),
            second: fault.second.to_owned(),
        })
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

    let latency_ms = lab.fault.latency_ms;
    let loss_percent = lab.fault.loss_percent;
    let blackhole = lab.fault.blackhole;

    // Built eagerly so the per-row listeners never hold `cx` across the rest of
    // the tree.
    let mut fault_elements = Vec::with_capacity(fault_rows.len());
    for (index, row) in fault_rows.into_iter().enumerate() {
        let FaultRow {
            target,
            kind: fault_kind,
            latency,
            loss,
            blackhole: is_blackhole,
            first,
            second,
        } = row;
        fault_elements.push(
            div()
                .flex()
                .flex_row()
                .items_center()
                .w_full()
                .py_1()
                .when(index % 2 == 1, |element| {
                    element.bg(hsla(0.62, 0.08, 0.15, 1.0))
                })
                .child(cell(target, TARGET_WIDTH, color::text()))
                .child(cell(fault_kind, KIND_WIDTH, color::muted()))
                .child(numeric_cell(latency, LATENCY_WIDTH, color::warn()))
                .child(numeric_cell(
                    loss,
                    LOSS_WIDTH,
                    if is_blackhole {
                        color::muted()
                    } else {
                        color::bad()
                    },
                ))
                .child(cell(
                    if is_blackhole { "yes" } else { "no" }.to_owned(),
                    BLACKHOLE_WIDTH,
                    if is_blackhole {
                        color::bad()
                    } else {
                        color::muted()
                    },
                ))
                .child(
                    div().w(px(CLEAR_WIDTH)).child(
                        Button::new(("fault-clear", index))
                            .label("Clear")
                            .ghost()
                            .on_click(cx.listener(
                                move |this: &mut WireLab, _: &ClickEvent, _window, cx| {
                                    this.command(cx, |session| {
                                        session.clear_fault(&first, &second)
                                    });
                                },
                            )),
                    ),
                ),
        );
    }

    let empty_faults = fault_elements.is_empty();

    div()
        .flex()
        .flex_col()
        .w_full()
        .gap_3()
        .child(card(
            "Selection",
            div()
                .flex()
                .flex_col()
                .gap_2()
                .child(
                    div()
                        .flex()
                        .flex_row()
                        .items_center()
                        .gap_2()
                        .child(pill(
                            selection_kind,
                            if selected {
                                color::accent()
                            } else {
                                color::muted()
                            },
                        ))
                        .child(
                            div()
                                .text_lg()
                                .text_color(if selected {
                                    color::accent()
                                } else {
                                    color::muted()
                                })
                                .child(if selected {
                                    selected_id
                                } else {
                                    "no target".to_owned()
                                }),
                        ),
                )
                .when(selected, |element| {
                    element.child(
                        div()
                            .text_sm()
                            .text_color(color::muted())
                            .child(selected_summary),
                    )
                })
                .when(!selected, |element| {
                    element.child(
                        div()
                            .text_sm()
                            .text_color(color::warn())
                            .child("Select a host or link on the Topology workspace first."),
                    )
                }),
        ))
        .child(card(
            "Fault injection",
            div()
                .flex()
                .flex_col()
                .gap_3()
                .child(
                    div()
                        .flex()
                        .flex_row()
                        .flex_wrap()
                        .gap_3()
                        .child(field(
                            "Extra latency",
                            stepper(
                                "fault-latency",
                                format!("{latency_ms} ms"),
                                cx.listener(|this: &mut WireLab, _: &ClickEvent, _window, cx| {
                                    this.fault.latency_ms = (this.fault.latency_ms - 5).max(0);
                                    cx.notify();
                                }),
                                cx.listener(|this: &mut WireLab, _: &ClickEvent, _window, cx| {
                                    this.fault.latency_ms += 5;
                                    cx.notify();
                                }),
                            ),
                        ))
                        .child(field(
                            "Packet loss",
                            stepper(
                                "fault-loss",
                                format::percent(loss_percent),
                                cx.listener(|this: &mut WireLab, _: &ClickEvent, _window, cx| {
                                    this.fault.loss_percent =
                                        (this.fault.loss_percent - 0.5).clamp(0.0, 100.0);
                                    cx.notify();
                                }),
                                cx.listener(|this: &mut WireLab, _: &ClickEvent, _window, cx| {
                                    this.fault.loss_percent =
                                        (this.fault.loss_percent + 0.5).clamp(0.0, 100.0);
                                    cx.notify();
                                }),
                            ),
                        ))
                        .child(field(
                            "Blackhole",
                            Button::new("fault-blackhole")
                                .label(if blackhole {
                                    "Dropping all traffic"
                                } else {
                                    "Forwarding normally"
                                })
                                .when(blackhole, |button| button.danger())
                                .when(!blackhole, |button| button.ghost())
                                .on_click(cx.listener(
                                    |this: &mut WireLab, _: &ClickEvent, _window, cx| {
                                        this.fault.blackhole = !this.fault.blackhole;
                                        cx.notify();
                                    },
                                )),
                        )),
                )
                .child(
                    Button::new("fault-apply")
                        .label("Apply fault")
                        .when(selected, |button| button.primary())
                        .when(!selected, |button| button.ghost())
                        .on_click(cx.listener(
                            |this: &mut WireLab, _: &ClickEvent, _window, cx| {
                                let latency = this.fault.latency_ms;
                                let loss = this.fault.loss_percent;
                                let blackhole = this.fault.blackhole;
                                this.command(cx, |session| {
                                    session.apply_selected_fault(latency, loss, blackhole)
                                });
                            },
                        )),
                )
                .child(div().text_xs().text_color(color::muted()).child(
                    "Faults are evaluated for every generated frame before packet \
                             analysis. A link's base latency stays in effect on top of the \
                             injected latency.",
                )),
        ))
        .child(card(
            "Active faults",
            div()
                .flex()
                .flex_col()
                .w_full()
                .gap_1()
                .child(
                    div()
                        .flex()
                        .flex_row()
                        .w_full()
                        .pb_1()
                        .border_b_1()
                        .border_color(color::border())
                        .child(header_cell("Target", TARGET_WIDTH, false))
                        .child(header_cell("Kind", KIND_WIDTH, false))
                        .child(header_cell("Latency", LATENCY_WIDTH, true))
                        .child(header_cell("Loss", LOSS_WIDTH, true))
                        .child(header_cell("Blackhole", BLACKHOLE_WIDTH, false))
                        .child(header_cell("", CLEAR_WIDTH, false)),
                )
                .when(empty_faults, |element| {
                    element.child(
                        div()
                            .py_2()
                            .text_sm()
                            .text_color(color::muted())
                            .child("No faults are active."),
                    )
                })
                .children(fault_elements),
        ))
        .child(card(
            "Port impact",
            Table::new(vec![
                Column::new("Port", 160.),
                Column::new("State", 100.),
                Column::new("Received", 120.).numeric(),
                Column::new("Forwarded", 120.).numeric(),
                Column::new("Dropped", 120.).numeric(),
            ])
            .empty_message("Load a topology to see its ports.")
            .rows(port_rows),
        ))
}

/// A `-` / value / `+` triple. The two handlers are already-bound listeners so
/// this stays free of the view type's borrow rules.
fn stepper(
    id: &'static str,
    value: String,
    decrement: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static,
    increment: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static,
) -> impl IntoElement {
    div()
        .flex()
        .flex_row()
        .items_center()
        .gap_2()
        .child(
            Button::new((id, 0usize))
                .label("-")
                .ghost()
                .on_click(decrement),
        )
        .child(
            div()
                .w(px(80.))
                .text_sm()
                .text_right()
                .text_color(color::text())
                .child(value),
        )
        .child(
            Button::new((id, 1usize))
                .label("+")
                .ghost()
                .on_click(increment),
        )
}

fn header_cell(title: &'static str, width: f32, numeric: bool) -> Div {
    let cell = div()
        .w(px(width))
        .text_xs()
        .text_color(color::muted())
        .child(title);
    if numeric { cell.text_right() } else { cell }
}

fn cell(text: String, width: f32, tint: Hsla) -> Div {
    div().w(px(width)).text_sm().text_color(tint).child(text)
}

fn numeric_cell(text: String, width: f32, tint: Hsla) -> Div {
    cell(text, width, tint).text_right()
}
