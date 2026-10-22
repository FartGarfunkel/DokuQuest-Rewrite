//! DokuQuest client (Bevy 0.16). One feature = one plugin file; see CLAUDE.md for the map.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
mod fx;
mod hud;
mod items;
mod menu;
mod player;
mod sky;
mod world;

use bevy::prelude::*;

#[derive(States, Default, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum AppState {
    #[default]
    Menu,
    Playing,
}

fn main() {
    App::new()
        .add_plugins(DefaultPlugins.set(WindowPlugin {
            primary_window: Some(Window { title: format!("DokuQuest {}", env!("CARGO_PKG_VERSION")), ..default() }),
            ..default()
        }))
        .init_state::<AppState>()
        .add_plugins((world::WorldPlugin, sky::SkyPlugin, player::PlayerPlugin, menu::MenuPlugin, hud::HudPlugin, items::ItemsPlugin, fx::FxPlugin))
        .run();
}
