//! Topology: the network's shape, what is selected in it, and the two commands
//! that grow it.
//!
//! The graph is painted by hand rather than assembled out of elements. A node is
//! a shape at a fraction of the canvas, and `gpui::canvas` is the cheapest way to
//! say exactly that. Text is the one thing that stays an element, so shaping and
//! font fallback keep behaving the way they do on every other page; the labels
//! are absolutely positioned at the same fractions the paint closure uses, which
//! is why the mapping from a node's normalised coordinates to a fraction of the
//! canvas is width-independent (see [`spread`]).

use std::f32::consts::TAU;

use gpui::{
    BorderStyle, Bounds, ClickEvent, Context, Div, Hsla, IntoElement, ParentElement as _,
    PathBuilder, Pixels, Point, Styled as _, Window, canvas, div, hsla, point, prelude::*, px,
    quad, relative, size,
};
use gpui_component::Disableable as _;
use gpui_component::button::{Button, ButtonVariants as _};
use gpui_component::input::Input;

use crate::app::WireLab;
use crate::ffi::{self, NodeType, SelectionKind};
use crate::format;
use crate::ui::{card, color, field};

/// Canvas geometry. A switch is a box and a host is a circle, exactly as the QML
/// canvas drew them.
const GRAPH_HEIGHT: f32 = 360.;
const SWITCH_WIDTH: f32 = 96.;
const SWITCH_HEIGHT: f32 = 44.;
const HOST_RADIUS: f32 = 24.;
/// A circle is a polygon with enough sides that nobody counts them.
const HOST_SIDES: usize = 28;

/// Node coordinates arrive normalised to `[0, 1]`, which would put an edge node
/// half outside the canvas. Insetting in *fraction* space rather than in pixels
/// keeps the painted shape and its absolutely positioned label agreeing without
/// either of them knowing the canvas width.
fn spread(value: f64, margin: f32) -> f32 {
    margin + (value as f32).clamp(0., 1.) * (1. - 2. * margin)
}

fn tint(color: Hsla, alpha: f32) -> Hsla {
    Hsla { a: alpha, ..color }
}

/// One node, resolved to canvas fractions and to whether it is selected.
#[derive(Clone)]
struct Dot {
    id: String,
    kind: String,
    is_switch: bool,
    fx: f32,
    fy: f32,
    selected: bool,
}

/// One link, resolved to the fractions of both endpoints. A link whose endpoint
/// is missing from the node list is not drawable and never reaches here.
#[derive(Clone)]
struct Segment {
    from: (f32, f32),
    to: (f32, f32),
    latency_ms: i64,
    selected: bool,
}

struct Col {
    title: &'static str,
    width: f32,
    numeric: bool,
}

const NODE_COLUMNS: [Col; 4] = [
    Col {
        title: "Node",
        width: 160.,
        numeric: false,
    },
    Col {
        title: "Type",
        width: 100.,
        numeric: false,
    },
    Col {
        title: "X",
        width: 80.,
        numeric: true,
    },
    Col {
        title: "Y",
        width: 80.,
        numeric: true,
    },
];

const LINK_COLUMNS: [Col; 3] = [
    Col {
        title: "From",
        width: 160.,
        numeric: false,
    },
    Col {
        title: "To",
        width: 160.,
        numeric: false,
    },
    Col {
        title: "Latency",
        width: 120.,
        numeric: true,
    },
];

/// `ui::Table`'s header, duplicated because these two tables need clickable rows
/// and `Table` renders plain strings.
fn header(columns: &[Col]) -> Div {
    div()
        .flex()
        .flex_row()
        .w_full()
        .pb_1()
        .border_b_1()
        .border_color(color::border())
        .children(columns.iter().map(|column| {
            let cell = div()
                .w(px(column.width))
                .text_xs()
                .text_color(color::muted())
                .child(column.title);
            if column.numeric {
                cell.text_right()
            } else {
                cell
            }
        }))
}

fn body_row(cells: Vec<String>, columns: &[Col], index: usize, selected: bool) -> Div {
    div()
        .flex()
        .flex_row()
        .w_full()
        .py_1()
        .cursor_pointer()
        .when(index % 2 == 1, |row| row.bg(hsla(0.62, 0.08, 0.15, 1.0)))
        .when(selected, |row| row.bg(tint(color::accent(), 0.18)))
        .children(cells.into_iter().zip(columns.iter()).map(|(text, column)| {
            let cell = div()
                .w(px(column.width))
                .text_sm()
                .text_color(color::text())
                .child(text);
            if column.numeric {
                cell.text_right()
            } else {
                cell
            }
        }))
}

fn empty_note(message: &'static str) -> Div {
    div()
        .py_2()
        .text_sm()
        .text_color(color::muted())
        .child(message)
}

pub fn render(
    lab: &mut WireLab,
    _window: &mut Window,
    cx: &mut Context<WireLab>,
) -> impl IntoElement {
    let session = &lab.session;
    let selection = session.selection_kind();
    let selected_id = session.selected_id().to_owned();
    let selected_summary = session.selected_summary().to_owned();

    let dots: Vec<Dot> = session
        .nodes()
        .map(|node| Dot {
            id: node.id.to_owned(),
            kind: ffi::label(node.node_type).to_owned(),
            is_switch: node.node_type == NodeType::Switch,
            fx: spread(node.x, 0.08),
            fy: spread(node.y, 0.10),
            selected: selection == SelectionKind::Node && selected_id == node.id,
        })
        .collect();

    let node_rows: Vec<(String, Vec<String>, bool)> = session
        .nodes()
        .map(|node| {
            (
                node.id.to_owned(),
                vec![
                    node.id.to_owned(),
                    ffi::label(node.node_type).to_owned(),
                    format::fixed(node.x, 2),
                    format::fixed(node.y, 2),
                ],
                selection == SelectionKind::Node && selected_id == node.id,
            )
        })
        .collect();

    // The C++ side reports a link's selection as a single string; the QML pane
    // matched it the same way, by looking for both endpoint names in it.
    let link_selected = |from: &str, to: &str| {
        selection == SelectionKind::Link && selected_id.contains(from) && selected_id.contains(to)
    };

    let link_rows: Vec<(String, String, Vec<String>, bool)> = session
        .links()
        .map(|link| {
            (
                link.from.to_owned(),
                link.to.to_owned(),
                vec![
                    link.from.to_owned(),
                    link.to.to_owned(),
                    format!("{} ms", link.latency_ms),
                ],
                link_selected(link.from, link.to),
            )
        })
        .collect();

    let position = |id: &str| {
        dots.iter()
            .find(|dot| dot.id == id)
            .map(|dot| (dot.fx, dot.fy))
    };
    let segments: Vec<Segment> = session
        .links()
        .filter_map(|link| {
            Some(Segment {
                from: position(link.from)?,
                to: position(link.to)?,
                latency_ms: link.latency_ms,
                selected: link_selected(link.from, link.to),
            })
        })
        .collect();

    // Everything the paint closure touches is owned before this point: the
    // closure outlives the borrow of `lab.session` that produced the rows.
    let painted_dots = dots.clone();
    let painted_segments = segments.clone();
    let empty_graph = dots.is_empty();

    let graph = canvas(
        |bounds, _window, _cx| bounds,
        move |bounds: Bounds<Pixels>, _prepainted: Bounds<Pixels>, window, _cx| {
            window.paint_quad(quad(
                bounds,
                px(6.),
                color::surface(),
                px(1.),
                color::border(),
                BorderStyle::Solid,
            ));

            let width = f32::from(bounds.size.width);
            let height = f32::from(bounds.size.height);
            let at = |fx: f32, fy: f32| bounds.origin + point(px(fx * width), px(fy * height));

            // Links first: a node's shape should cover the line, not the reverse.
            for segment in &painted_segments {
                let mut line = PathBuilder::stroke(px(if segment.selected { 2.5 } else { 1.5 }));
                line.move_to(at(segment.from.0, segment.from.1));
                line.line_to(at(segment.to.0, segment.to.1));
                if let Ok(path) = line.build() {
                    let stroke = if segment.selected {
                        color::accent()
                    } else {
                        color::border()
                    };
                    window.paint_path(path, stroke);
                }
            }

            for dot in &painted_dots {
                let center = at(dot.fx, dot.fy);
                let outline = if dot.selected {
                    color::accent()
                } else if dot.is_switch {
                    color::warn()
                } else {
                    color::good()
                };
                let fill = if dot.selected {
                    tint(color::accent(), 0.22)
                } else {
                    color::raised()
                };

                if dot.is_switch {
                    window.paint_quad(quad(
                        Bounds::new(
                            center - point(px(SWITCH_WIDTH / 2.), px(SWITCH_HEIGHT / 2.)),
                            size(px(SWITCH_WIDTH), px(SWITCH_HEIGHT)),
                        ),
                        px(9.),
                        fill,
                        px(1.5),
                        outline,
                        BorderStyle::Solid,
                    ));
                } else {
                    let ring: Vec<Point<Pixels>> = (0..HOST_SIDES)
                        .map(|step| {
                            let angle = step as f32 * TAU / HOST_SIDES as f32;
                            center
                                + point(
                                    px(HOST_RADIUS * angle.cos()),
                                    px(HOST_RADIUS * angle.sin()),
                                )
                        })
                        .collect();

                    let mut disc = PathBuilder::fill();
                    disc.add_polygon(&ring, true);
                    if let Ok(path) = disc.build() {
                        window.paint_path(path, fill);
                    }
                    let mut edge = PathBuilder::stroke(px(1.5));
                    edge.add_polygon(&ring, true);
                    if let Ok(path) = edge.build() {
                        window.paint_path(path, outline);
                    }
                }
            }
        },
    )
    .absolute()
    .top_0()
    .left_0()
    .size_full();

    let canvas_layer = div()
        .relative()
        .w_full()
        .h(px(GRAPH_HEIGHT))
        .child(graph)
        .children(segments.into_iter().map(|segment| {
            let mid_x = (segment.from.0 + segment.to.0) / 2.;
            let mid_y = (segment.from.1 + segment.to.1) / 2.;
            div()
                .absolute()
                .left(relative(mid_x))
                .top(relative(mid_y))
                .ml(px(-26.))
                .mt(px(-9.))
                .w(px(52.))
                .flex()
                .flex_row()
                .justify_center()
                .bg(color::surface())
                .text_xs()
                .text_color(color::muted())
                .child(format!("{} ms", segment.latency_ms))
        }))
        .children(dots.into_iter().map(|dot| {
            div()
                .absolute()
                .left(relative(dot.fx))
                .top(relative(dot.fy))
                .ml(px(-60.))
                .mt(px(-16.))
                .w(px(120.))
                .flex()
                .flex_col()
                .items_center()
                .child(div().text_sm().text_color(color::text()).child(dot.id))
                .child(
                    div()
                        .text_xs()
                        .text_color(color::muted())
                        .child(dot.kind.to_uppercase()),
                )
        }))
        .when(empty_graph, |layer| {
            layer.child(
                div()
                    .absolute()
                    .left_0()
                    .top(relative(0.46))
                    .w_full()
                    .flex()
                    .flex_row()
                    .justify_center()
                    .text_sm()
                    .text_color(color::muted())
                    .child("Open a topology YAML to render the network"),
            )
        });

    let nothing_selected = selection == SelectionKind::None;
    let headline = if nothing_selected {
        "Nothing selected".to_owned()
    } else {
        selected_id
    };

    div()
        .flex()
        .flex_col()
        .w_full()
        .gap_3()
        .child(card("Topology", canvas_layer))
        .child(card(
            "Selection",
            div()
                .flex()
                .flex_col()
                .gap_2()
                .child(div().text_sm().text_color(color::text()).child(headline))
                .child(
                    div()
                        .text_xs()
                        .text_color(color::muted())
                        .child(selected_summary),
                )
                .child(
                    div()
                        .flex()
                        .flex_row()
                        .gap_2()
                        .child(
                            Button::new("clear-selection")
                                .label("Clear selection")
                                .ghost()
                                .disabled(nothing_selected)
                                .on_click(cx.listener(|this, _: &ClickEvent, _window, cx| {
                                    this.command(cx, |session| session.clear_selection());
                                })),
                        )
                        .child(
                            Button::new("remove-selected")
                                .label("Remove selected")
                                .danger()
                                .disabled(nothing_selected)
                                .on_click(cx.listener(|this, _: &ClickEvent, _window, cx| {
                                    this.command(cx, |session| session.remove_selected());
                                })),
                        ),
                ),
        ))
        .child(card(
            "Nodes",
            div()
                .flex()
                .flex_col()
                .w_full()
                .gap_1()
                .child(header(&NODE_COLUMNS))
                .when(node_rows.is_empty(), |list| {
                    list.child(empty_note("Open a topology YAML to see its nodes."))
                })
                .children(node_rows.into_iter().enumerate().map(
                    |(index, (id, cells, selected))| {
                        body_row(cells, &NODE_COLUMNS, index, selected)
                            .id(("node", index))
                            .on_click(cx.listener(move |this, _: &ClickEvent, _window, cx| {
                                let id = id.clone();
                                this.command(cx, |session| session.select_node(&id));
                            }))
                    },
                )),
        ))
        .child(card(
            "Links",
            div()
                .flex()
                .flex_col()
                .w_full()
                .gap_1()
                .child(header(&LINK_COLUMNS))
                .when(link_rows.is_empty(), |list| {
                    list.child(empty_note("This topology has no links."))
                })
                .children(link_rows.into_iter().enumerate().map(
                    |(index, (from, to, cells, selected))| {
                        body_row(cells, &LINK_COLUMNS, index, selected)
                            .id(("link", index))
                            .on_click(cx.listener(move |this, _: &ClickEvent, _window, cx| {
                                let from = from.clone();
                                let to = to.clone();
                                this.command(cx, |session| session.select_link(&from, &to));
                            }))
                    },
                )),
        ))
        .child(card(
            "Add node",
            div()
                .flex()
                .flex_row()
                .flex_wrap()
                .items_end()
                .gap_2()
                .child(
                    div()
                        .w(px(220.))
                        .child(field("Node id", Input::new(&lab.inputs.node_id))),
                )
                .child(
                    Button::new("add-host")
                        .label("Add host")
                        .primary()
                        .on_click(cx.listener(|this, _: &ClickEvent, window, cx| {
                            add_node(this, NodeType::Host, window, cx);
                        })),
                )
                .child(
                    Button::new("add-switch")
                        .label("Add switch")
                        .primary()
                        .on_click(cx.listener(|this, _: &ClickEvent, window, cx| {
                            add_node(this, NodeType::Switch, window, cx);
                        })),
                ),
        ))
        .child(card(
            "Add link",
            div()
                .flex()
                .flex_row()
                .flex_wrap()
                .items_end()
                .gap_2()
                .child(
                    div()
                        .w(px(180.))
                        .child(field("From", Input::new(&lab.inputs.link_from))),
                )
                .child(
                    div()
                        .w(px(180.))
                        .child(field("To", Input::new(&lab.inputs.link_to))),
                )
                .child(
                    div()
                        .w(px(120.))
                        .child(field("Latency ms", Input::new(&lab.inputs.link_latency))),
                )
                .child(
                    Button::new("add-link")
                        .label("Add link")
                        .primary()
                        .on_click(cx.listener(|this, _: &ClickEvent, _window, cx| {
                            let from = this
                                .inputs
                                .link_from
                                .read(cx)
                                .value()
                                .as_str()
                                .trim()
                                .to_owned();
                            let to = this
                                .inputs
                                .link_to
                                .read(cx)
                                .value()
                                .as_str()
                                .trim()
                                .to_owned();
                            let latency = this
                                .inputs
                                .link_latency
                                .read(cx)
                                .value()
                                .as_str()
                                .trim()
                                .parse::<i32>()
                                .unwrap_or(1);
                            if from.is_empty() || to.is_empty() {
                                return;
                            }
                            this.command(cx, |session| session.add_link(&from, &to, latency));
                        })),
                ),
        ))
}

/// Both add buttons differ only in the node type, and the field is cleared after
/// the call the way the QML form did, so the next id can be typed straight in.
fn add_node(
    lab: &mut WireLab,
    node_type: NodeType,
    window: &mut Window,
    cx: &mut Context<WireLab>,
) {
    let id = lab
        .inputs
        .node_id
        .read(cx)
        .value()
        .as_str()
        .trim()
        .to_owned();
    if id.is_empty() {
        return;
    }
    lab.command(cx, |session| session.add_node(&id, node_type));
    lab.inputs
        .node_id
        .update(cx, |state, cx| state.set_value("", window, cx));
}
