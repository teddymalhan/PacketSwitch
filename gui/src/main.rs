//! WireLab desktop frontend.
//!
//! The C++ core does all the work; this process is layout, painting, input and
//! the clock. Everything it knows about the lab arrives through
//! `include/wirelab/wirelab_ffi.h`, wrapped in [`ffi::Session`].

mod app;
mod ffi;
mod format;
mod pages;
mod ui;

use gpui::{
    App, AppContext as _, Application, Bounds, KeyBinding, Menu, MenuItem, WindowBounds,
    WindowOptions, actions, px, size,
};
use gpui_component::Root;

use crate::app::WireLab;

actions!(
    wirelab,
    [Quit, OpenTopology, SaveTopology, ExportReport, StopTraffic]
);

fn main() {
    Application::new().run(|cx: &mut App| {
        // Installs the theme global and the component keybindings. Skipping it
        // is a runtime panic the first time a component reads the theme.
        gpui_component::init(cx);

        cx.bind_keys([
            KeyBinding::new("cmd-q", Quit, None),
            KeyBinding::new("cmd-o", OpenTopology, None),
            KeyBinding::new("cmd-s", SaveTopology, None),
            KeyBinding::new("cmd-e", ExportReport, None),
            KeyBinding::new("cmd-.", StopTraffic, None),
        ]);
        cx.on_action(|_: &Quit, cx: &mut App| cx.quit());

        cx.set_menus(vec![
            Menu {
                name: "WireLab".into(),
                items: vec![
                    MenuItem::separator(),
                    MenuItem::action("Quit WireLab", Quit),
                ],
            },
            Menu {
                name: "File".into(),
                items: vec![
                    MenuItem::action("Open Topology…", OpenTopology),
                    MenuItem::action("Save Topology…", SaveTopology),
                    MenuItem::separator(),
                    MenuItem::action("Export Report…", ExportReport),
                ],
            },
            Menu {
                name: "Traffic".into(),
                items: vec![MenuItem::action("Stop Traffic", StopTraffic)],
            },
        ]);

        let bounds = Bounds::centered(None, size(px(1280.), px(860.)), cx);
        cx.open_window(
            WindowOptions {
                window_bounds: Some(WindowBounds::Windowed(bounds)),
                titlebar: Some(gpui::TitlebarOptions {
                    title: Some("WireLab".into()),
                    ..Default::default()
                }),
                ..Default::default()
            },
            |window, cx| {
                let lab = cx.new(|cx| WireLab::new(window, cx));

                // Menu actions are application-level, so they are routed to the
                // one view that owns the session.
                let target = lab.downgrade();
                cx.on_action({
                    let target = target.clone();
                    move |_: &OpenTopology, cx: &mut App| {
                        let _ = target.update(cx, |lab, cx| lab.open_topology_dialog(cx));
                    }
                });
                cx.on_action({
                    let target = target.clone();
                    move |_: &SaveTopology, cx: &mut App| {
                        let _ = target.update(cx, |lab, cx| lab.save_topology_dialog(cx));
                    }
                });
                cx.on_action({
                    let target = target.clone();
                    move |_: &ExportReport, cx: &mut App| {
                        let _ = target.update(cx, |lab, cx| lab.export_report_dialog(cx));
                    }
                });
                cx.on_action({
                    let target = target.clone();
                    move |_: &StopTraffic, cx: &mut App| {
                        let _ = target.update(cx, |lab, cx| {
                            lab.command(cx, |session| session.stop_traffic());
                        });
                    }
                });

                cx.new(|cx| Root::new(gpui::AnyView::from(lab), window, cx))
            },
        )
        .expect("failed to open the WireLab window");

        cx.activate(true);
    });
}
