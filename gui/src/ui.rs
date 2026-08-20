//! Presentation helpers shared by every workspace.
//!
//! The palette and the row/table primitives live here so seven pages cannot
//! drift into seven different looks, and so a page is mostly a description of
//! its data rather than a pile of styling.

use gpui::{Hsla, div, hsla, prelude::*, px, rgb};
use gpui_component::button::{Button, ButtonVariants};

/// Dark palette. WireLab is an instrument panel: the surface recedes, the data
/// carries the contrast.
pub mod color {
    use gpui::{Hsla, hsla};

    pub fn background() -> Hsla {
        hsla(0.62, 0.10, 0.09, 1.0)
    }
    pub fn surface() -> Hsla {
        hsla(0.62, 0.09, 0.13, 1.0)
    }
    pub fn raised() -> Hsla {
        hsla(0.62, 0.08, 0.17, 1.0)
    }
    pub fn border() -> Hsla {
        hsla(0.62, 0.08, 0.24, 1.0)
    }
    pub fn text() -> Hsla {
        hsla(0.62, 0.06, 0.92, 1.0)
    }
    pub fn muted() -> Hsla {
        hsla(0.62, 0.05, 0.62, 1.0)
    }
    pub fn accent() -> Hsla {
        hsla(0.58, 0.72, 0.55, 1.0)
    }
    pub fn good() -> Hsla {
        hsla(0.38, 0.55, 0.50, 1.0)
    }
    pub fn warn() -> Hsla {
        hsla(0.10, 0.80, 0.58, 1.0)
    }
    pub fn bad() -> Hsla {
        hsla(0.00, 0.65, 0.58, 1.0)
    }
}

/// A titled panel. Every workspace is a column of these.
pub fn card(title: impl Into<String>, body: impl IntoElement) -> impl IntoElement {
    div()
        .flex()
        .flex_col()
        .w_full()
        .gap_2()
        .p_3()
        .rounded_md()
        .bg(color::surface())
        .border_1()
        .border_color(color::border())
        .child(
            div()
                .text_sm()
                .text_color(color::muted())
                .child(title.into().to_uppercase()),
        )
        .child(body)
}

/// A single headline number with its label underneath.
pub fn stat(label: impl Into<String>, value: impl Into<String>, tint: Hsla) -> impl IntoElement {
    div()
        .flex()
        .flex_col()
        .gap_1()
        .min_w(px(140.))
        .p_3()
        .rounded_md()
        .bg(color::raised())
        .child(div().text_xl().text_color(tint).child(value.into()))
        .child(
            div()
                .text_xs()
                .text_color(color::muted())
                .child(label.into()),
        )
}

/// A read-only table. Row counts here are bounded by the session (60 metric
/// samples, 100 packets, 200 policy actions), so plain rows beat a virtualized
/// list: no delegate, no entity, no scroll state to keep in sync.
pub struct Table {
    columns: Vec<Column>,
    rows: Vec<Vec<String>>,
    empty_message: String,
}

pub struct Column {
    pub title: String,
    pub width: f32,
    pub numeric: bool,
}

impl Column {
    pub fn new(title: impl Into<String>, width: f32) -> Self {
        Self {
            title: title.into(),
            width,
            numeric: false,
        }
    }

    /// Right-aligned, so digits line up on the decimal point.
    pub fn numeric(mut self) -> Self {
        self.numeric = true;
        self
    }
}

impl Table {
    pub fn new(columns: Vec<Column>) -> Self {
        Self {
            columns,
            rows: Vec::new(),
            empty_message: "Nothing to show yet.".into(),
        }
    }

    pub fn empty_message(mut self, message: impl Into<String>) -> Self {
        self.empty_message = message.into();
        self
    }

    pub fn row(mut self, cells: Vec<String>) -> Self {
        debug_assert_eq!(
            cells.len(),
            self.columns.len(),
            "row width must match the header"
        );
        self.rows.push(cells);
        self
    }

    pub fn rows(mut self, rows: impl IntoIterator<Item = Vec<String>>) -> Self {
        for row in rows {
            self = self.row(row);
        }
        self
    }
}

impl RenderOnce for Table {
    fn render(self, _window: &mut gpui::Window, _cx: &mut gpui::App) -> impl IntoElement {
        let header = div()
            .flex()
            .flex_row()
            .w_full()
            .pb_1()
            .border_b_1()
            .border_color(color::border())
            .children(self.columns.iter().map(|column| {
                let cell = div()
                    .w(px(column.width))
                    .text_xs()
                    .text_color(color::muted())
                    .child(column.title.clone());
                if column.numeric {
                    cell.text_right()
                } else {
                    cell
                }
            }));

        let empty = self.rows.is_empty();
        let columns = self.columns;
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
                        .child(self.empty_message.clone()),
                )
            })
            .children(
                self.rows
                    .into_iter()
                    .enumerate()
                    .map(move |(index, cells)| {
                        div()
                            .flex()
                            .flex_row()
                            .w_full()
                            .py_1()
                            // Zebra striping is what makes a wide row readable across
                            // fourteen columns.
                            .when(index % 2 == 1, |row| row.bg(hsla(0.62, 0.08, 0.15, 1.0)))
                            .children(cells.into_iter().zip(columns.iter()).map(
                                |(cell, column)| {
                                    let element = div()
                                        .w(px(column.width))
                                        .text_sm()
                                        .text_color(color::text())
                                        .child(cell);
                                    if column.numeric {
                                        element.text_right()
                                    } else {
                                        element
                                    }
                                },
                            ))
                    }),
            )
    }
}

impl IntoElement for Table {
    type Element = gpui::Component<Self>;

    fn into_element(self) -> Self::Element {
        gpui::Component::new(self)
    }
}

/// A labelled control, the shape every form row takes.
pub fn field(label: impl Into<String>, control: impl IntoElement) -> impl IntoElement {
    div()
        .flex()
        .flex_col()
        .gap_1()
        .child(
            div()
                .text_xs()
                .text_color(color::muted())
                .child(label.into()),
        )
        .child(control)
}

/// A row of choices behaving like radio buttons. Used for scenario, backend and
/// node type, where the option set comes from the core and is short.
pub fn choices<T: Clone + PartialEq + 'static>(
    id: &'static str,
    options: impl IntoIterator<Item = (String, T)>,
    selected: T,
    on_pick: impl Fn(T, &mut gpui::Window, &mut gpui::App) + Clone + 'static,
) -> impl IntoElement {
    div()
        .flex()
        .flex_row()
        .flex_wrap()
        .gap_1()
        .children(
            options
                .into_iter()
                .enumerate()
                .map(move |(index, (label, value))| {
                    let active = value == selected;
                    let on_pick = on_pick.clone();
                    Button::new((id, index))
                        .label(label)
                        .when(active, |button| button.primary())
                        .when(!active, |button| button.ghost())
                        .on_click(move |_, window, cx| on_pick(value.clone(), window, cx))
                }),
        )
}

/// Monospace cell text, so MAC addresses and IPs align column-wise.
pub fn mono(text: impl Into<String>) -> impl IntoElement {
    div()
        .font_family("Menlo")
        .text_sm()
        .text_color(color::text())
        .child(text.into())
}

pub fn pill(text: impl Into<String>, tint: Hsla) -> impl IntoElement {
    div()
        .px_2()
        .py_0p5()
        .rounded_sm()
        .bg(rgb(0x00000000))
        .border_1()
        .border_color(tint)
        .text_xs()
        .text_color(tint)
        .child(text.into())
}
