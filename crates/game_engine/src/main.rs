//! DokuQuest client (Bevy 0.16). One feature = one plugin file; see CLAUDE.md for the map.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
mod controls;
mod fell;
mod fx;
mod hud;
mod invui;
mod items;
mod menu;
mod mobs;
mod packs;
mod player;
mod sky;
mod survival;
mod theme;
mod world;

use bevy::prelude::*;

/// Release tag when built by the release workflow (e.g. v0.2.14), else the crate version.
pub const VERSION: &str = match option_env!("DQ_VERSION") { Some(v) => v, None => env!("CARGO_PKG_VERSION") };

#[derive(States, Default, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum AppState {
    #[default]
    Menu,
    Playing,
}

fn main() {
    let settings = menu::Settings::load();
    let found = packs::available();
    let pack = match settings.pack.as_str() { "none" => None, p if found.iter().any(|f| f == p) => Some(p.to_string()), _ => found.first().cloned() };
    if let Some(p) = pack {
        match packs::apply(&p) { Ok(n) => println!("Resource pack {p}: {n} textures"), Err(e) => eprintln!("Resource pack {p} failed: {e}") }
    }
    App::new()
        .add_plugins(DefaultPlugins.set(WindowPlugin {
            primary_window: Some(Window { title: format!("DokuQuest {VERSION}"), ..default() }),
            ..default()
        }))
        .init_state::<AppState>()
        .add_plugins((world::WorldPlugin, sky::SkyPlugin, controls::ControlsPlugin, player::PlayerPlugin, survival::SurvivalPlugin, mobs::MobsPlugin))
        .add_plugins((invui::InvUiPlugin, menu::MenuPlugin, hud::HudPlugin, items::ItemsPlugin, fx::FxPlugin, theme::ThemePlugin, fell::FellPlugin))
        .run();
}
