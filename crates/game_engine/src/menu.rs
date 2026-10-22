//! Title (ui/title.js) → Singleplayer (select / play / delete worlds; Create World with name, seed and
//! game mode) and Options (render distance, FOV, sensitivity, volume, occlusion, resource pack, skin),
//! plus the in-game Pause and Death screens. 0.9 palette; mouse, keyboard and controller (theme.rs).
use crate::player::{Player, Ui};
use crate::sky::MainCamera;
use crate::sounds::Sfx;
use crate::theme::*;
use crate::world::{demo_meta, set_render_dist, VoxelWorld};
use crate::AppState;
use bevy::core_pipeline::prepass::DepthPrepass;
use bevy::input::keyboard::{Key, KeyboardInput};
use bevy::input::ButtonState;
use bevy::prelude::*;
use bevy::render::experimental::occlusion_culling::OcclusionCulling;
use common_server::save::{self, WorldMeta};
use common_server::worldgen::{Gen, GEN_VERSION};
use std::fs;

#[derive(Resource, Clone, PartialEq)]
pub struct Settings {
    pub sens: f32,
    pub fov: f32,
    pub occlusion: bool,
    /// Resource pack zip in <data>/resourcepacks ("" = first found, "none" = built-in Doku tiles).
    pub pack: String,
    pub rd: i32,
    pub volume: f32,
}
impl Settings {
    fn path() -> std::path::PathBuf { save::data_dir().join("settings.txt") }
    pub fn load() -> Self {
        let mut s = Settings { sens: 1.0, fov: 70.0, occlusion: false, pack: String::new(), rd: 6, volume: 0.8 };
        for l in fs::read_to_string(Self::path()).unwrap_or_default().lines() {
            match l.split_once('=') {
                Some(("sens", v)) => s.sens = v.parse().unwrap_or(1.0),
                Some(("fov", v)) => s.fov = v.parse().unwrap_or(70.0),
                Some(("gpu_occlusion", v)) => s.occlusion = v == "true",
                Some(("pack", v)) => s.pack = v.trim().into(),
                Some(("rd", v)) => s.rd = v.parse().unwrap_or(6),
                Some(("volume", v)) => s.volume = v.parse().unwrap_or(0.8),
                _ => {}
            }
        }
        s
    }
    fn save(&self) {
        let _ = fs::create_dir_all(save::data_dir());
        let _ = fs::write(Self::path(), format!("sens={}\nfov={}\ngpu_occlusion={}\npack={}\nrd={}\nvolume={}\n", self.sens, self.fov, self.occlusion, self.pack, self.rd, self.volume));
    }
}

#[derive(Clone, Copy, PartialEq, Default)]
enum Screen {
    #[default]
    Title,
    Worlds,
    Create,
    Options,
}

#[derive(Resource, Default)]
struct Menu {
    screen: Screen,
    selected: Option<String>,
    confirm: bool,
    name: String,
    seed: String,
    creative: bool,
    field: u8,
    note: String,
}

#[derive(Component)]
struct MenuRoot;
#[derive(Component)]
struct OverlayRoot;

#[derive(Component, Clone)]
enum Btn {
    Single,
    Options,
    Quit,
    Back,
    Select(String),
    Play(String),
    Delete,
    Create,
    Make,
    Field(u8),
    Mode,
    Sens(f32),
    Fov(f32),
    Rd(i32),
    Vol(f32),
    Occl,
    Pack,
    AddPack,
    Skin,
    Folder,
    Resume,
    SaveQuit,
    Respawn,
}

#[derive(Resource, Default)]
struct Pano {
    centre: Vec3,
    yaw: f32,
}

pub struct MenuPlugin;
impl Plugin for MenuPlugin {
    fn build(&self, app: &mut App) {
        app.insert_resource(Settings::load())
            .init_resource::<Menu>()
            .init_resource::<Pano>()
            .add_systems(OnEnter(AppState::Menu), open)
            .add_systems(OnExit(AppState::Menu), close::<MenuRoot>)
            .add_systems(OnExit(AppState::Playing), close::<OverlayRoot>)
            .add_systems(Update, (type_text, build_menu, spin).chain().run_if(in_state(AppState::Menu)))
            .add_systems(Update, build_overlay.run_if(in_state(AppState::Playing)))
            .add_systems(Update, (buttons, apply_settings));
    }
}

fn open(w: Res<VoxelWorld>, mut pano: ResMut<Pano>, mut m: ResMut<Menu>) {
    *pano = Pano { centre: Vec3::from(w.meta.pos) + Vec3::Y * crate::player::EYE, yaw: w.meta.yaw };
    m.screen = Screen::Title;
    m.confirm = false;
}

fn close<T: Component>(mut commands: Commands, q: Query<Entity, With<T>>) {
    for e in &q { commands.entity(e).despawn(); }
}

fn ago(t: u64) -> String {
    match save::now().saturating_sub(t) / 86_400 { 0 => "today".into(), 1 => "yesterday".into(), d => format!("{d} days ago") }
}

fn row(gap: f32) -> Node { Node { column_gap: Val::Px(gap), align_items: AlignItems::Center, ..default() } }

fn build_menu(mut commands: Commands, m: Res<Menu>, roots: Query<Entity, With<MenuRoot>>, w: Res<VoxelWorld>, settings: Res<Settings>) {
    if !m.is_changed() && !settings.is_changed() { return; }
    for e in &roots { commands.entity(e).despawn(); }
    let full = Node { width: Val::Percent(100.0), height: Val::Percent(100.0), flex_direction: FlexDirection::Column, align_items: AlignItems::Center, justify_content: JustifyContent::Center, row_gap: Val::Px(10.0), ..default() };
    let (persistent, name) = (w.persistent, w.meta.name.clone());
    let s = settings.clone();
    commands.spawn((MenuRoot, full)).with_children(|ui| {
        ui.spawn(text("DOKUQUEST", 76.0, EMBER));
        ui.spawn(text(format!("DokuQuest Alpha {} · Rust", crate::VERSION), 16.0, MUTED));
        ui.spawn(Node { height: Val::Px(10.0), ..default() });
        match m.screen {
            Screen::Title => {
                if persistent { ui.spawn(button(Btn::Play(name.clone()), &format!("Continue: {name}"), 340.0, true)); }
                ui.spawn(button(Btn::Single, "Singleplayer", 340.0, !persistent));
                ui.spawn(button(Btn::Options, "Options", 340.0, false));
                ui.spawn(button(Btn::Quit, "Quit Game", 340.0, false));
            }
            Screen::Worlds => {
                ui.spawn(panel_node(6.0)).with_children(|pn| {
                    pn.spawn(text("Select World", 22.0, TEXT));
                    let worlds = save::list_worlds();
                    if worlds.is_empty() { pn.spawn(text("No worlds yet — create one!", 16.0, MUTED)); }
                    for wm in worlds.iter().take(9) {
                        let sel = m.selected.as_deref() == Some(wm.name.as_str());
                        pn.spawn(button(Btn::Select(wm.name.clone()), &format!("{} · seed {} · {}", wm.name, wm.seed, ago(wm.last_played)), 520.0, sel));
                    }
                });
                ui.spawn(row(8.0)).with_children(|r| {
                    let sel = m.selected.clone().unwrap_or_default();
                    r.spawn(button(Btn::Play(sel), "Play Selected World", 260.0, m.selected.is_some()));
                    r.spawn(button(Btn::Create, "Create New World", 260.0, m.selected.is_none()));
                });
                ui.spawn(row(8.0)).with_children(|r| {
                    r.spawn(button(Btn::Delete, if m.confirm { "Click again to DELETE" } else { "Delete" }, 260.0, false));
                    r.spawn(button(Btn::Back, "Back", 260.0, false));
                });
            }
            Screen::Create => {
                ui.spawn(panel_node(8.0)).with_children(|pn| {
                    pn.spawn(text("Create New World", 22.0, TEXT));
                    let caret = |f: u8| if m.field == f { "▌" } else { "" };
                    pn.spawn(text("World name", 15.0, MUTED));
                    pn.spawn(button(Btn::Field(1), &format!("{}{}", if m.name.is_empty() && m.field != 1 { "New World" } else { &m.name }, caret(1)), 420.0, m.field == 1));
                    pn.spawn(text("Seed (leave blank for random)", 15.0, MUTED));
                    pn.spawn(button(Btn::Field(2), &format!("{}{}", m.seed, caret(2)), 420.0, m.field == 2));
                    pn.spawn(button(Btn::Mode, &format!("Game mode: {}", if m.creative { "Creative" } else { "Survival" }), 420.0, false));
                });
                ui.spawn(row(8.0)).with_children(|r| {
                    r.spawn(button(Btn::Make, "Create World", 260.0, true));
                    r.spawn(button(Btn::Single, "Cancel", 260.0, false));
                });
            }
            Screen::Options => {
                ui.spawn(panel_node(6.0)).with_children(|pn| {
                    pn.spawn(text("Options", 22.0, TEXT));
                    for (label, minus, plus) in [
                        (format!("Render distance: {} chunks", s.rd), Btn::Rd(-1), Btn::Rd(1)),
                        (format!("Field of view: {:.0}", s.fov), Btn::Fov(-5.0), Btn::Fov(5.0)),
                        (format!("Mouse sensitivity: {:.1}", s.sens), Btn::Sens(-0.1), Btn::Sens(0.1)),
                        (format!("Volume: {:.0}%", s.volume * 100.0), Btn::Vol(-0.1), Btn::Vol(0.1)),
                    ] {
                        pn.spawn(row(8.0)).with_children(|r| {
                            r.spawn(button(minus, "-", 44.0, false));
                            r.spawn((text(label, 17.0, TEXT), Node { width: Val::Px(300.0), justify_content: JustifyContent::Center, ..default() }));
                            r.spawn(button(plus, "+", 44.0, false));
                        });
                    }
                    pn.spawn(button(Btn::Occl, &format!("GPU occlusion culling (experimental): {}", if s.occlusion { "On" } else { "Off" }), 420.0, false));
                    let pk = match s.pack.as_str() { "" => "Auto (first in folder)".to_string(), "none" => "None (built-in Doku)".to_string(), p => p.to_string() };
                    pn.spawn(button(Btn::Pack, &format!("Resource pack: {pk}"), 520.0, false));
                    pn.spawn(row(8.0)).with_children(|r| {
                        r.spawn(button(Btn::AddPack, "Add resource pack…", 256.0, false));
                        r.spawn(button(Btn::Skin, "Choose skin…", 256.0, false));
                    });
                    pn.spawn(button(Btn::Folder, "Open game folder", 420.0, false));
                    pn.spawn(text(if m.note.is_empty() { "Resource packs apply the next time the game starts." } else { &m.note }, 14.0, MUTED));
                });
                ui.spawn(button(Btn::Back, "Done", 340.0, true));
            }
        }
    });
}

/// Typing into the Create World name / seed fields.
fn type_text(mut ev: EventReader<KeyboardInput>, mut m: ResMut<Menu>) {
    if m.screen != Screen::Create || m.field == 0 { ev.clear(); return; }
    let m = &mut *m;
    for e in ev.read() {
        if e.state != ButtonState::Pressed { continue; }
        let f = m.field;
        let s = if f == 1 { &mut m.name } else { &mut m.seed };
        match &e.logical_key {
            Key::Character(c) => { if s.len() < 32 { s.push_str(c.as_str()); } }
            Key::Space => { if s.len() < 32 { s.push(' '); } }
            Key::Backspace => { s.pop(); }
            Key::Enter | Key::Escape => m.field = 0,
            _ => {}
        }
    }
}

fn build_overlay(mut commands: Commands, ui: Res<Ui>, roots: Query<Entity, With<OverlayRoot>>) {
    if !ui.is_changed() { return; }
    for e in &roots { commands.entity(e).despawn(); }
    let full = Node { position_type: PositionType::Absolute, width: Val::Percent(100.0), height: Val::Percent(100.0), flex_direction: FlexDirection::Column, align_items: AlignItems::Center, justify_content: JustifyContent::Center, row_gap: Val::Px(10.0), ..default() };
    match *ui {
        Ui::Pause => {
            commands.spawn((OverlayRoot, full, BackgroundColor(INK.with_alpha(0.6)))).with_children(|o| {
                o.spawn(text("Game paused", 34.0, TEXT));
                o.spawn(button(Btn::Resume, "Back to Game", 340.0, true));
                o.spawn(button(Btn::SaveQuit, "Save & Quit to Title", 340.0, false));
            });
        }
        Ui::Dead => {
            commands.spawn((OverlayRoot, full, BackgroundColor(Color::srgba(0.35, 0.02, 0.02, 0.55)))).with_children(|o| {
                o.spawn(text("You died!", 44.0, HEART));
                o.spawn(button(Btn::Respawn, "Respawn", 340.0, true));
                o.spawn(button(Btn::SaveQuit, "Title Screen", 340.0, false));
            });
        }
        _ => {}
    }
}

fn spin(time: Res<Time>, mut pano: ResMut<Pano>, mut w: ResMut<VoxelWorld>, mut cam: Query<&mut Transform, With<MainCamera>>) {
    pano.yaw += time.delta_secs() * 0.05;
    w.focus = pano.centre;
    if let Ok(mut t) = cam.single_mut() {
        t.translation = pano.centre;
        t.rotation = Quat::from_euler(EulerRot::YXZ, pano.yaw, -0.1, 0.0);
    }
}

fn seed_of(s: &str) -> u64 {
    let s = s.trim();
    if s.is_empty() { return save::now().wrapping_mul(0x9E3779B97F4A7C15) ^ std::process::id() as u64; }
    s.parse::<u64>().unwrap_or_else(|_| s.bytes().fold(0xcbf29ce484222325u64, |h, b| (h ^ b as u64).wrapping_mul(0x100000001b3)))
}

fn play(name: &str, w: &mut VoxelWorld, next: &mut NextState<AppState>) {
    if let Some(m) = save::load_meta(name) {
        if !(w.persistent && w.meta.name == name) { w.open(m, true); }
        save::set_last_played(name);
        next.set(AppState::Playing);
    }
}

#[allow(clippy::too_many_arguments)]
fn buttons(
    q: Query<(&Interaction, &Btn), Changed<Interaction>>,
    mut m: ResMut<Menu>,
    mut next: ResMut<NextState<AppState>>,
    mut exit: EventWriter<AppExit>,
    mut w: ResMut<VoxelWorld>,
    mut settings: ResMut<Settings>,
    mut ui: ResMut<Ui>,
    mut p: ResMut<Player>,
    mut sfx: EventWriter<Sfx>,
) {
    for (i, b) in &q {
        if *i != Interaction::Pressed { continue; }
        sfx.write(Sfx::fx("click"));
        if !matches!(b, Btn::Delete) { m.confirm = false; }
        match b.clone() {
            Btn::Single => { m.screen = Screen::Worlds; m.field = 0; }
            Btn::Options => { m.screen = Screen::Options; m.note.clear(); }
            Btn::Back => m.screen = Screen::Title,
            Btn::Quit => { exit.write(AppExit::Success); }
            Btn::Select(n) => m.selected = Some(n),
            Btn::Play(n) => { if !n.is_empty() { play(&n, &mut w, &mut next); } }
            Btn::Create => { m.screen = Screen::Create; m.name.clear(); m.seed.clear(); m.creative = false; m.field = 1; }
            Btn::Field(f) => m.field = f,
            Btn::Mode => m.creative = !m.creative,
            Btn::Make => {
                let base = if m.name.trim().is_empty() { "New World".to_string() } else { m.name.trim().to_string() };
                let name = (0..).map(|i| if i == 0 { base.clone() } else { format!("{base} ({i})") }).find(|n| save::load_meta(n).is_none()).unwrap_or_else(|| base.clone());
                let seed = seed_of(&m.seed);
                let meta = WorldMeta { name: name.clone(), seed, gen_version: GEN_VERSION, pos: Gen::new(seed).spawn_point(), last_played: save::now(), creative: m.creative, flying: m.creative, ..default() };
                let _ = save::save_meta(&meta);
                save::set_last_played(&name);
                w.open(meta, true);
                next.set(AppState::Playing);
            }
            Btn::Delete => {
                let Some(n) = m.selected.clone() else { continue };
                if !m.confirm { m.confirm = true; continue; }
                let _ = fs::remove_dir_all(save::world_dir(&n));
                if fs::read_to_string(save::saves_dir().join("last_played.txt")).map(|s| s.trim() == n).unwrap_or(false) {
                    let _ = fs::remove_file(save::saves_dir().join("last_played.txt"));
                }
                if w.meta.name == n { w.open(demo_meta(), false); }
                m.selected = None;
                m.confirm = false;
            }
            Btn::Rd(d) => { settings.rd = (settings.rd + d).clamp(2, 16); settings.save(); }
            Btn::Sens(d) => { settings.sens = (settings.sens + d).clamp(0.2, 3.0); settings.save(); }
            Btn::Fov(d) => { settings.fov = (settings.fov + d).clamp(50.0, 110.0); settings.save(); }
            Btn::Vol(d) => { settings.volume = (settings.volume + d).clamp(0.0, 1.0); settings.save(); }
            Btn::Occl => { settings.occlusion = !settings.occlusion; settings.save(); }
            Btn::Pack => {
                let mut opts = vec![String::new()];
                opts.extend(crate::packs::available());
                opts.push("none".into());
                let i = opts.iter().position(|o| *o == settings.pack).map(|i| (i + 1) % opts.len()).unwrap_or(0);
                settings.pack = opts[i].clone();
                settings.save();
            }
            Btn::AddPack => {
                if let Some(f) = rfd::FileDialog::new().set_title("Choose a resource pack").add_filter("Resource pack", &["zip"]).pick_file() {
                    let d = crate::packs::dir();
                    let _ = fs::create_dir_all(&d);
                    if let Some(n) = f.file_name() {
                        let ok = fs::copy(&f, d.join(n)).is_ok();
                        settings.pack = n.to_string_lossy().into();
                        settings.save();
                        m.note = if ok { format!("Added {} — restart the game to use it.", settings.pack) } else { "Could not copy that pack.".into() };
                    }
                }
            }
            Btn::Skin => {
                if let Some(f) = rfd::FileDialog::new().set_title("Choose your skin").add_filter("Skin (PNG)", &["png"]).pick_file() {
                    let _ = fs::create_dir_all(save::data_dir());
                    m.note = if fs::copy(&f, crate::skin::path()).is_ok() { "Skin saved — press F5 in game to see it.".into() } else { "Could not read that skin.".into() };
                }
            }
            Btn::Folder => {
                let d = save::data_dir();
                let _ = fs::create_dir_all(crate::packs::dir());
                let _ = std::process::Command::new(if cfg!(windows) { "explorer" } else { "xdg-open" }).arg(d).spawn();
            }
            Btn::Resume => *ui = Ui::None,
            Btn::SaveQuit => { *ui = Ui::None; next.set(AppState::Menu); }
            Btn::Respawn => {
                p.pos = if w.meta.spawn != [0.0; 3] { Vec3::from(w.meta.spawn) } else { Vec3::from(w.generator().spawn_point()) };
                p.vel = Vec3::ZERO;
                p.health = 20.0;
                p.hunger = 20.0;
                *ui = Ui::None;
            }
        }
    }
}

/// FOV, render distance and GPU occlusion culling follow the settings.
fn apply_settings(settings: Res<Settings>, mut cams: Query<(Entity, &mut Projection), With<MainCamera>>, mut commands: Commands) {
    if !settings.is_changed() { return; }
    set_render_dist(settings.rd);
    for (e, mut pr) in &mut cams {
        if let Projection::Perspective(pp) = &mut *pr { pp.fov = settings.fov.to_radians(); }
        if settings.occlusion {
            commands.entity(e).insert((DepthPrepass, OcclusionCulling));
        } else {
            commands.entity(e).remove::<(DepthPrepass, OcclusionCulling)>();
        }
    }
}
