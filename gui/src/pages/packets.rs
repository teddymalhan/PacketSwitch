//! Packets & Security: the parsed frames of the most recent tick, the
//! anomalies the detectors raised from them, and the ports policy is currently
//! holding down.
//!
//! Everything on this page is a view of the session's current rows except the
//! Release buttons, which are the one command this workspace offers.

use gpui::{
    ClickEvent, Context, IntoElement, ParentElement as _, Styled as _, Window, div, prelude::*, px,
};
use gpui_component::button::{Button, ButtonVariants};

use crate::app::WireLab;
use crate::ffi::{self, Validity};
use crate::format;
use crate::ui::{Column, Table, card, color, stat};

pub fn render(
    lab: &mut WireLab,
    _window: &mut Window,
    cx: &mut Context<WireLab>,
) -> impl IntoElement {
    // Every row string is collected up front: the session borrow has to end
    // before `cx.listener` hands out a `&mut WireLab`.
    let session = &lab.session;

    let packet_count = session.packets().len();
    let anomaly_count = session.anomalies().len();
    let mac_count = session.mac_table().len();
    let malformed = session
        .packets()
        .filter(|packet| packet.validity != Validity::Valid)
        .count();

    let packet_rows: Vec<Vec<String>> = session
        .packets()
        .map(|packet| {
            vec![
                packet.source_mac.to_owned(),
                packet.destination_mac.to_owned(),
                packet.source_ip.to_owned(),
                packet.destination_ip.to_owned(),
                format::protocol(packet.protocol),
                packet.destination_port.to_string(),
                format::integer(u64::from(packet.bytes)),
                packet.ingress.to_owned(),
                ffi::classification_label(packet.classification).to_owned(),
                ffi::validity_label(packet.validity).to_owned(),
            ]
        })
        .collect();

    let anomaly_rows: Vec<Vec<String>> = session
        .anomalies()
        .map(|anomaly| {
            vec![
                ffi::anomaly_label(anomaly.anomaly_type).to_owned(),
                anomaly.source_mac.to_owned(),
                anomaly.source_ip.to_owned(),
                anomaly.ingress_port.to_string(),
                format::integer(anomaly.observed),
                format::integer(anomaly.threshold),
            ]
        })
        .collect();

    let enforced: Vec<[String; 4]> = session
        .enforced_ports()
        .map(|port| {
            [
                port.port.to_owned(),
                port.rule.to_owned(),
                ffi::enforcement_kind_label(port.kind).to_owned(),
                port.summary.to_owned(),
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
                    "Packets (tick)",
                    packet_count.to_string(),
                    color::accent(),
                ))
                .child(stat(
                    "Anomalies",
                    anomaly_count.to_string(),
                    if anomaly_count == 0 {
                        color::good()
                    } else {
                        color::warn()
                    },
                ))
                .child(stat("Learned MACs", mac_count.to_string(), color::text()))
                .child(stat(
                    "Malformed",
                    malformed.to_string(),
                    if malformed == 0 {
                        color::good()
                    } else {
                        color::warn()
                    },
                )),
        )
        .child(card(
            "Packet analysis · most recent batch",
            Table::new(vec![
                Column::new("Source MAC", 150.),
                Column::new("Destination MAC", 150.),
                Column::new("Source IP", 120.),
                Column::new("Destination IP", 120.),
                Column::new("Protocol", 100.),
                Column::new("Dst port", 90.).numeric(),
                Column::new("Bytes", 90.).numeric(),
                Column::new("Ingress", 100.),
                Column::new("Classification", 140.),
                Column::new("Validity", 150.),
            ])
            .empty_message("Start traffic to capture packets.")
            .rows(packet_rows),
        ))
        .child(card(
            "Active anomalies",
            Table::new(vec![
                Column::new("Type", 200.),
                Column::new("Source MAC", 150.),
                Column::new("Source IP", 120.),
                Column::new("Ingress port", 110.).numeric(),
                Column::new("Observed", 110.).numeric(),
                Column::new("Threshold", 110.).numeric(),
            ])
            .empty_message("No anomalies in the last tick.")
            .rows(anomaly_rows),
        ))
        .child(card("Enforcement", enforcement_body(enforced, cx)))
}

/// The enforcement list wears `ui::Table`'s look but cannot be one: each row
/// carries a button, and the table only renders strings.
fn enforcement_body(rows: Vec<[String; 4]>, cx: &mut Context<WireLab>) -> impl IntoElement {
    let empty = rows.is_empty();

    let header = div()
        .flex()
        .flex_row()
        .w_full()
        .pb_1()
        .border_b_1()
        .border_color(color::border())
        .child(header_cell("Port", 140.))
        .child(header_cell("Rule", 180.))
        .child(header_cell("Kind", 140.))
        .child(header_cell("Summary", 320.))
        .child(header_cell("", 100.));

    div()
        .flex()
        .flex_col()
        .w_full()
        .gap_1()
        .child(header)
        .when(empty, |element| {
            element.child(
                div()
                    .py_2()
                    .text_sm()
                    .text_color(color::muted())
                    .child("No ports are under enforcement."),
            )
        })
        .children(rows.into_iter().enumerate().map(|(index, row)| {
            let [port, rule, kind, summary] = row;
            let target = port.clone();
            div()
                .flex()
                .flex_row()
                .w_full()
                .items_center()
                .py_1()
                .child(body_cell(port, 140.))
                .child(body_cell(rule, 180.))
                .child(body_cell(kind, 140.))
                .child(body_cell(summary, 320.))
                .child(
                    div().w(px(100.)).child(
                        Button::new(("release-enforcement", index))
                            .label("Release")
                            .danger()
                            .on_click(cx.listener(
                                move |this: &mut WireLab, _: &ClickEvent, _window, cx| {
                                    this.command(cx, |session| {
                                        session.release_enforcement(&target)
                                    });
                                },
                            )),
                    ),
                )
        }))
}

fn header_cell(title: &'static str, width: f32) -> impl IntoElement {
    div()
        .w(px(width))
        .text_xs()
        .text_color(color::muted())
        .child(title)
}

fn body_cell(text: String, width: f32) -> impl IntoElement {
    div()
        .w(px(width))
        .text_sm()
        .text_color(color::text())
        .child(text)
}
