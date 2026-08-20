//! Dashboard: the lab's state at a glance.
//!
//! Everything here is derived from the session's current rows; the page keeps
//! no state of its own, so a tick that changes nothing renders identically.

use gpui::{Context, IntoElement, ParentElement as _, Styled as _, Window, div, prelude::*};

use crate::app::WireLab;
use crate::ffi::{self, SelectionKind};
use crate::format;
use crate::ui::{Column, Table, card, color, stat};

pub fn render(
    lab: &mut WireLab,
    _window: &mut Window,
    _cx: &mut Context<WireLab>,
) -> impl IntoElement {
    let session = &lab.session;

    let latest = session.metrics_history().last().copied();
    let throughput = latest.map(|sample| sample.throughput_mbps).unwrap_or(0.0);
    let latency = latest.map(|sample| sample.latency_ms).unwrap_or(0.0);
    let loss = latest.map(|sample| sample.loss_percent).unwrap_or(0.0);

    let hosts = session
        .nodes()
        .filter(|node| node.node_type == ffi::NodeType::Host)
        .count();
    let links = session.links().len();
    let faults = session.faults().len();
    let enforced = session.enforced_ports().len();
    let anomalies = session.anomalies().len();

    let selection = match session.selection_kind() {
        SelectionKind::None => "nothing selected".to_owned(),
        _ => format!("{} · {}", session.selected_id(), session.selected_summary()),
    };

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

    let fault_rows: Vec<Vec<String>> = session
        .faults()
        .map(|fault| {
            vec![
                if fault.is_link {
                    format!("{} ↔ {}", fault.first, fault.second)
                } else {
                    fault.first.to_owned()
                },
                if fault.is_link { "Link" } else { "Port" }.to_owned(),
                format!("{} ms", fault.latency_ms),
                format::percent(fault.loss_percent),
                if fault.blackhole { "yes" } else { "no" }.to_owned(),
            ]
        })
        .collect();

    div()
        .flex()
        .flex_col()
        .w_full()
        .gap_3()
        .child(
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
                ))
                .child(stat("Hosts", hosts.to_string(), color::text()))
                .child(stat("Links", links.to_string(), color::text()))
                .child(stat(
                    "Active faults",
                    faults.to_string(),
                    if faults == 0 {
                        color::good()
                    } else {
                        color::warn()
                    },
                ))
                .child(stat(
                    "Enforced ports",
                    enforced.to_string(),
                    if enforced == 0 {
                        color::good()
                    } else {
                        color::bad()
                    },
                ))
                .child(stat(
                    "Anomalies (tick)",
                    anomalies.to_string(),
                    if anomalies == 0 {
                        color::good()
                    } else {
                        color::warn()
                    },
                )),
        )
        .child(card(
            "Run",
            div()
                .flex()
                .flex_col()
                .gap_1()
                .child(
                    div()
                        .text_sm()
                        .text_color(color::text())
                        .child(session.traffic_result().to_owned()),
                )
                .child(
                    div()
                        .text_xs()
                        .text_color(color::muted())
                        .child(format!("Selection: {selection}")),
                ),
        ))
        .child(card(
            "Ports",
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
        .child(card(
            "Active faults",
            Table::new(vec![
                Column::new("Target", 220.),
                Column::new("Kind", 80.),
                Column::new("Latency", 100.).numeric(),
                Column::new("Loss", 100.).numeric(),
                Column::new("Blackhole", 100.),
            ])
            .empty_message("No faults are active.")
            .rows(fault_rows),
        ))
}
