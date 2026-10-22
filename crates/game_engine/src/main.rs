//! DokuQuest client (Bevy 0.16). One feature = one plugin file; see CLAUDE.md for the map.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
mod controls;
mod fx;
mod hud;
mod invui;
mod items;
mod menu;
mod mobs;
mod player;
mod sky;
mod survival;
mod theme;
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
        .add_plugins((world::WorldPlugin, sky::SkyPlugin, controls::ControlsPlugin, player::PlayerPlugin, survival::SurvivalPlugin, mobs::MobsPlugin))
        .add_plugins((invui::InvUiPlugin, menu::MenuPlugin, hud::HudPlugin, items::ItemsPlugin, fx::FxPlugin, theme::ThemePlugin))
        .run();
}
