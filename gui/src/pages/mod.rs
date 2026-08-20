//! The seven workspaces.
//!
//! A page is a free function over the root view rather than an entity of its
//! own: pages hold no state the session does not already hold, and threading a
//! `&mut WireLab` through keeps every command one call away from the session.

pub mod dashboard;
pub mod faults;
pub mod packets;
pub mod policies;
pub mod reports;
pub mod topology;
pub mod traffic;

use gpui::{AnyElement, Context, IntoElement, Window};

use crate::app::{Page, WireLab};

pub fn render(lab: &mut WireLab, window: &mut Window, cx: &mut Context<WireLab>) -> AnyElement {
    match lab.page {
        Page::Dashboard => dashboard::render(lab, window, cx).into_any_element(),
        Page::Topology => topology::render(lab, window, cx).into_any_element(),
        Page::Traffic => traffic::render(lab, window, cx).into_any_element(),
        Page::Packets => packets::render(lab, window, cx).into_any_element(),
        Page::Faults => faults::render(lab, window, cx).into_any_element(),
        Page::Policies => policies::render(lab, window, cx).into_any_element(),
        Page::Reports => reports::render(lab, window, cx).into_any_element(),
    }
}
