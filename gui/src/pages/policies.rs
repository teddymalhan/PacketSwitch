//! Policy Lab: the rules that turn a detected anomaly into port enforcement.
//!
//! Four panels, in the order a rule travels: the form that defines a rule, the
//! rules themselves, the ports currently held under enforcement, and the log of
//! what the enforcer did on each tick.

use gpui::{
    ClickEvent, Context, IntoElement, ParentElement as _, Styled as _, Window, div, hsla,
    prelude::*, px,
};
use gpui_component::button::{Button, ButtonVariants};
use gpui_component::input::Input;

use crate::app::WireLab;
use crate::ffi;
use crate::format;
use crate::ui::{Column, Table, card, choices, color, field};

/// The rate limit spinner moves in whole hundreds of packets per second; the
/// core's own step in the QML form was 1000, but 100 keeps the small limits a
/// lab actually wants reachable without a text field.
const RATE_STEP: u64 = 100;

/// Owned snapshots. Everything the session lends out is borrowed from
/// `&lab.session`, so a page that also issues commands has to copy first.
struct RuleRow {
    name: String,
    anomaly: &'static str,
    action: &'static str,
    enabled: bool,
    rate_limit: u64,
    hits: u64,
}

struct PortRow {
    port: String,
    rule: String,
    kind: &'static str,
    summary: String,
}

pub fn render(
    lab: &mut WireLab,
    _window: &mut Window,
    cx: &mut Context<WireLab>,
) -> impl IntoElement {
    let handle = cx.entity();

    let rules: Vec<RuleRow> = lab
        .session
        .policies()
        .map(|policy| RuleRow {
            name: policy.name.to_owned(),
            anomaly: ffi::anomaly_label(policy.anomaly_type),
            action: ffi::policy_action_label(policy.action),
            enabled: policy.enabled,
            rate_limit: policy.rate_limit_packets_per_second,
            hits: policy.hits,
        })
        .collect();

    let ports: Vec<PortRow> = lab
        .session
        .enforced_ports()
        .map(|port| PortRow {
            port: port.port.to_owned(),
            rule: port.rule.to_owned(),
            kind: ffi::enforcement_kind_label(port.kind),
            summary: port.summary.to_owned(),
        })
        .collect();

    // The session appends, so the newest action is last; the log reads newest
    // first, exactly as the QML list did with BottomToTop.
    let mut activity_rows: Vec<Vec<String>> = lab
        .session
        .policy_actions()
        .map(|row| {
            vec![
                row.sequence.to_string(),
                row.rule.to_owned(),
                ffi::anomaly_label(row.anomaly_type).to_owned(),
                ffi::policy_action_label(row.action).to_owned(),
                if row.port.is_empty() {
                    "—".to_owned()
                } else {
                    row.port.to_owned()
                },
                ffi::enforcement_outcome_label(row.outcome).to_owned(),
                row.detail.to_owned(),
            ]
        })
        .collect();
    activity_rows.reverse();

    let anomaly_options: Vec<(String, String)> = lab
        .anomaly_types
        .iter()
        .map(|name| (name.clone(), name.clone()))
        .collect();
    let action_options: Vec<(String, String)> = lab
        .policy_actions
        .iter()
        .map(|name| (name.clone(), name.clone()))
        .collect();
    let selected_anomaly = lab.policy.anomaly_type.clone();
    let selected_action = lab.policy.action.clone();
    let rate_limit = lab.policy.rate_limit;

    // The pickers carry the core's own display names, which is what
    // `add_policy` parses; nothing is translated on the way in or out.
    let anomaly_target = handle.clone();
    let anomaly_picker = choices(
        "policy-anomaly",
        anomaly_options,
        selected_anomaly,
        move |value: String, _window, cx| {
            anomaly_target.update(cx, |this, cx| {
                this.policy.anomaly_type = value;
                cx.notify();
            });
        },
    );

    let action_target = handle.clone();
    let action_picker = choices(
        "policy-action",
        action_options,
        selected_action,
        move |value: String, _window, cx| {
            action_target.update(cx, |this, cx| {
                this.policy.action = value;
                cx.notify();
            });
        },
    );

    let new_rule =
        div()
            .flex()
            .flex_col()
            .gap_3()
            .child(field(
                "Rule name",
                div().w(px(260.)).child(Input::new(&lab.inputs.policy_name)),
            ))
            .child(field("When", anomaly_picker))
            .child(field("Then", action_picker))
            .child(field(
                "Rate limit",
                div()
                    .flex()
                    .flex_row()
                    .items_center()
                    .gap_2()
                    .child(Button::new("policy-rate-down").label("−").ghost().on_click(
                        cx.listener(|this, _: &ClickEvent, _window, cx| {
                            this.policy.rate_limit =
                                this.policy.rate_limit.saturating_sub(RATE_STEP);
                            cx.notify();
                        }),
                    ))
                    .child(
                        div()
                            .w(px(120.))
                            .text_sm()
                            .text_color(color::text())
                            .child(format!("{} pps", format::integer(rate_limit))),
                    )
                    .child(
                        Button::new("policy-rate-up")
                            .label("+")
                            .ghost()
                            .on_click(cx.listener(|this, _: &ClickEvent, _window, cx| {
                                this.policy.rate_limit += RATE_STEP;
                                cx.notify();
                            })),
                    ),
            ))
            .child(
                div().child(
                    Button::new("policy-add")
                        .label("Add rule")
                        .primary()
                        .on_click(cx.listener(|this, _: &ClickEvent, window, cx| {
                            let name = this.inputs.policy_name.read(cx).value().to_string();
                            let name = name.trim().to_owned();
                            if name.is_empty() {
                                return;
                            }
                            let anomaly_type = this.policy.anomaly_type.clone();
                            let action = this.policy.action.clone();
                            let rate_limit = this.policy.rate_limit;
                            this.command(cx, |session| {
                                session.add_policy(&name, &anomaly_type, &action, rate_limit)
                            });
                            this.inputs
                                .policy_name
                                .update(cx, |state, cx| state.set_value("", window, cx));
                        })),
                ),
            )
            .child(
                // The core refuses a Rate limit rule with a zero limit and reports
                // it only through the status bar, which is easy to miss down there.
                div().text_xs().text_color(color::muted()).child(
                    "A “Rate limit” action needs a non-zero rate limit, or the rule is rejected.",
                ),
            );

    let rules_empty = rules.is_empty();
    let rules_panel = div()
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
                .child(head("Rule", 180.))
                .child(head("Anomaly", 170.))
                .child(head("Action", 130.))
                .child(head("Enabled", 90.))
                .child(head("Limit", 130.))
                .child(head("Hits", 80.))
                .child(head("", 180.)),
        )
        .when(rules_empty, |element| {
            element.child(empty("No policy rules yet."))
        })
        .children(rules.into_iter().enumerate().map(|(index, rule)| {
            let toggle_name = rule.name.clone();
            let toggle_to = !rule.enabled;
            let remove_name = rule.name.clone();
            div()
                .flex()
                .flex_row()
                .w_full()
                .items_center()
                .py_1()
                .when(index % 2 == 1, |row| row.bg(hsla(0.62, 0.08, 0.15, 1.0)))
                .child(cell(rule.name, 180., color::text()))
                .child(cell(rule.anomaly, 170., color::muted()))
                .child(cell(rule.action, 130., color::text()))
                .child(cell(
                    if rule.enabled { "yes" } else { "no" },
                    90.,
                    if rule.enabled {
                        color::good()
                    } else {
                        color::muted()
                    },
                ))
                .child(cell(
                    if rule.rate_limit > 0 {
                        format!("{} pps", format::integer(rule.rate_limit))
                    } else {
                        "—".to_owned()
                    },
                    130.,
                    color::muted(),
                ))
                .child(cell(
                    format::integer(rule.hits),
                    80.,
                    if rule.hits > 0 {
                        color::warn()
                    } else {
                        color::muted()
                    },
                ))
                .child(
                    div()
                        .flex()
                        .flex_row()
                        .gap_2()
                        .w(px(180.))
                        .child(
                            Button::new(("policy-toggle", index))
                                .label(if rule.enabled { "Disable" } else { "Enable" })
                                .ghost()
                                .on_click(cx.listener(move |this, _: &ClickEvent, _window, cx| {
                                    let name = toggle_name.clone();
                                    this.command(cx, |session| {
                                        session.set_policy_enabled(&name, toggle_to)
                                    });
                                })),
                        )
                        .child(
                            Button::new(("policy-remove", index))
                                .label("Remove")
                                .danger()
                                .on_click(cx.listener(move |this, _: &ClickEvent, _window, cx| {
                                    let name = remove_name.clone();
                                    this.command(cx, |session| session.remove_policy(&name));
                                })),
                        ),
                )
        }));

    let ports_empty = ports.is_empty();
    let ports_panel = div()
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
                .child(head("Port", 140.))
                .child(head("Rule", 180.))
                .child(head("Kind", 140.))
                .child(head("Summary", 320.))
                .child(head("", 100.)),
        )
        .when(ports_empty, |element| {
            element.child(empty("No ports are under enforcement."))
        })
        .children(ports.into_iter().enumerate().map(|(index, port)| {
            let release_port = port.port.clone();
            div()
                .flex()
                .flex_row()
                .w_full()
                .items_center()
                .py_1()
                .when(index % 2 == 1, |row| row.bg(hsla(0.62, 0.08, 0.15, 1.0)))
                .child(cell(port.port, 140., color::bad()))
                .child(cell(port.rule, 180., color::muted()))
                .child(cell(port.kind, 140., color::text()))
                .child(cell(port.summary, 320., color::text()))
                .child(
                    div().w(px(100.)).child(
                        Button::new(("policy-release", index))
                            .label("Release")
                            .outline()
                            .on_click(cx.listener(move |this, _: &ClickEvent, _window, cx| {
                                let port = release_port.clone();
                                this.command(cx, |session| session.release_enforcement(&port));
                            })),
                    ),
                )
        }));

    div()
        .flex()
        .flex_col()
        .w_full()
        .gap_3()
        .child(card("New rule", new_rule))
        .child(card("Rules", rules_panel))
        .child(card("Enforced ports", ports_panel))
        .child(card(
            "Policy activity",
            Table::new(vec![
                Column::new("Tick", 70.).numeric(),
                Column::new("Rule", 160.),
                Column::new("Anomaly", 170.),
                Column::new("Action", 130.),
                Column::new("Port", 110.),
                Column::new("Outcome", 110.),
                Column::new("Detail", 320.),
            ])
            .empty_message("No policy activity yet.")
            .rows(activity_rows),
        ))
}

fn head(title: &'static str, width: f32) -> impl IntoElement {
    div()
        .w(px(width))
        .text_xs()
        .text_color(color::muted())
        .child(title)
}

fn cell(text: impl Into<String>, width: f32, tint: gpui::Hsla) -> impl IntoElement {
    div()
        .w(px(width))
        .text_sm()
        .text_color(tint)
        .child(text.into())
}

fn empty(message: &'static str) -> impl IntoElement {
    div()
        .py_2()
        .text_sm()
        .text_color(color::muted())
        .child(message)
}
