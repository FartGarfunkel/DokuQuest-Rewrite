//! Title (ui/title.js) → Singleplayer world list / Options, and the in-game Pause and Death screens,
//! in the 0.9.0.90 palette. Behind the title is the real world, slowly rotating: your last-played
//! world from where you saved, or a demo world before you have played.
use crate::player::{Player, Ui};
use crate::sky::MainCamera;
use crate::theme::*;
use crate::world::VoxelWorld;
use crate::AppState;
use bevy::core_pipeline::prepass::DepthPrepass;
use bevy::prelude::*;
use bevy::render::experimental::occlusion_culling::OcclusionCulling;
use common_server::save::{self, WorldMeta};
use common_server::worldgen::{Gen, GEN_VERSION};

#[derive(Resource, Clone, PartialEq)]
pub struct Settings {
    pub sens: f32,
    pub fov: f32,
    pub occlusion: bool,
    /// Resource pack zip in <data>/resourcepacks ("" = first found, "none" = built-in Doku tiles).
    pub pack: String,
}
impl Settings {
    fn path() -> std::path::PathBuf { save::data_dir().join("settings.txt") }
    pub fn load() -> Self {
        let mut s = Settings { sens: 1.0, fov: 70.0, occlusion: true, pack: String::new() };
        for l in std::fs::read_to_string(Self::path()).unwrap_or_default().lines() {
            match l.split_once('=') {
                Some(("sens", v)) => s.sens = v.parse().unwrap_or(1.0),
                Some(("fov", v)) => s.fov = v.parse().unwrap_or(70.0),
                Some(("occlusion", v)) => s.occlusion = v == "true",
                Some(("pack", v)) => s.pack = v.trim().into(),
                _ => {}
            }
        }
        s
    }
    fn save(&self) {
        let _ = std::fs::create_dir_all(save::data_dir());
        let _ = std::fs::write(Self::path(), format!("sens={}\nfov={}\nocclusion={}\npack={}\n", self.sens, self.fov, self.occlusion, self.pack));
    }
}

#[derive(Resource, Default, Clone, Copy, PartialEq)]
enum Screen {
    #[default]
    Title,
    Worlds,
    Options,
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
    NewWorld,
    Play(String),
    Sens(f32),
    Fov(f32),
    Occl,
    Pack,
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
            .init_resource::<Screen>()
            .init_resource::<Pano>()
            .add_systems(OnEnter(AppState::Menu), open)
            .add_systems(OnExit(AppState::Menu), close::<MenuRoot>)
            .add_systems(OnExit(AppState::Playing), close::<OverlayRoot>)
            .add_systems(Update, (build_menu, spin).run_if(in_state(AppState::Menu)))
            .add_systems(Update, build_overlay.run_if(in_state(AppState::Playing)))
            .add_systems(Update, (buttons, apply_settings));
    }
}

fn open(w: Res<VoxelWorld>, mut pano: ResMut<Pano>, mut screen: ResMut<Screen>) {
    *pano = Pano { centre: Vec3::from(w.meta.pos) + Vec3::Y * crate::player::EYE, yaw: w.meta.yaw };
    *screen = Screen::Title;
}

fn close<T: Component>(mut commands: Commands, q: Query<Entity, With<T>>) {
    for e in &q { commands.entity(e).despawn(); }
}

fn ago(t: u64) -> String {
    match save::now().saturating_sub(t) / 86_400 { 0 => "today".into(), 1 => "yesterday".into(), d => format!("{d} days ago") }
}

fn build_menu(mut commands: Commands, screen: Res<Screen>, roots: Query<Entity, With<MenuRoot>>, w: Res<VoxelWorld>, settings: Res<Settings>) {
    if !screen.is_changed() && !settings.is_changed() { return; }
    for e in &roots { commands.entity(e).despawn(); }
    let full = Node { width: Val::Percent(100.0), height: Val::Percent(100.0), flex_direction: FlexDirection::Column, align_items: AlignItems::Center, justify_content: JustifyContent::Center, row_gap: Val::Px(10.0), ..default() };
    let scr = *screen;
    let (persistent, name) = (w.persistent, w.meta.name.clone());
    let s = settings.clone();
    commands.spawn((MenuRoot, full)).with_children(|ui| {
        ui.spawn(text("DOKUQUEST", 76.0, EMBER));
        ui.spawn(text(format!("DokuQuest Alpha {} · Rust", crate::VERSION), 16.0, MUTED));
        ui.spawn(Node { height: Val::Px(14.0), ..default() });
        match scr {
            Screen::Title => {
                if persistent { ui.spawn(button(Btn::Play(name.clone()), &format!("Continue: {name}"), 340.0, true)); }
                ui.spawn(button(Btn::Single, "Singleplayer", 340.0, !persistent));
                ui.spawn(button(Btn::Options, "Options", 340.0, false));
                ui.spawn(button(Btn::Quit, "Quit Game", 340.0, false));
            }
            Screen::Worlds => {
                ui.spawn(panel_node(6.0)).with_children(|pn| {
                    pn.spawn(text("Singleplayer", 22.0, TEXT));
                    let worlds = save::list_worlds();
                    if worlds.is_empty() { pn.spawn(text("No worlds yet — create one!", 16.0, MUTED)); }
                    for m in worlds.iter().take(8) {
                        pn.spawn(button(Btn::Play(m.name.clone()), &format!("{} · {}", m.name, ago(m.last_played)), 420.0, false));
                    }
                });
                ui.spawn(button(Btn::NewWorld, "Create New World", 340.0, true));
                ui.spawn(button(Btn::Back, "Back", 340.0, false));
            }
            Screen::Options => {
                ui.spawn(panel_node(8.0)).with_children(|pn| {
                    pn.spawn(text("Options", 22.0, TEXT));
                    for (label, minus, plus) in [
                        (format!("Mouse sensitivity: {:.1}", s.sens), Btn::Sens(-0.1), Btn::Sens(0.1)),
                        (format!("Field of view: {:.0}", s.fov), Btn::Fov(-5.0), Btn::Fov(5.0)),
                    ] {
                        pn.spawn(Node { column_gap: Val::Px(8.0), align_items: AlignItems::Center, ..default() }).with_children(|r| {
                            r.spawn(button(minus, "-", 44.0, false));
                            r.spawn((text(label, 18.0, TEXT), Node { width: Val::Px(260.0), justify_content: JustifyContent::Center, ..default() }));
                            r.spawn(button(plus, "+", 44.0, false));
                        });
                    }
                    pn.spawn(button(Btn::Occl, &format!("Occlusion culling: {}", if s.occlusion { "On" } else { "Off" }), 360.0, false));
                    let pk = match s.pack.as_str() { "" => "Auto (first in folder)".to_string(), "none" => "None (built-in Doku)".to_string(), p => p.to_string() };
                    pn.spawn(button(Btn::Pack, &format!("Resource pack: {pk}"), 460.0, false));
                    pn.spawn(button(Btn::Folder, "Open resource packs folder", 360.0, false));
                    pn.spawn(text("Resource packs apply the next time the game starts.", 14.0, MUTED));
                });
                ui.spawn(button(Btn::Back, "Done", 340.0, true));
            }
        }
    });
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

fn new_world_meta() -> WorldMeta {
    let n = (1..).find(|i| save::load_meta(&format!("World {i}")).is_none()).unwrap_or(1);
    let seed = save::now().wrapping_mul(0x9E3779B97F4A7C15) ^ std::process::id() as u64;
    WorldMeta { name: format!("World {n}"), seed, gen_version: GEN_VERSION, pos: Gen::new(seed).spawn_point(), last_played: save::now(), ..default() }
}

#[allow(clippy::too_many_arguments)]
fn buttons(
    q: Query<(&Interaction, &Btn), Changed<Interaction>>,
    mut screen: ResMut<Screen>,
    mut next: ResMut<NextState<AppState>>,
    mut exit: EventWriter<AppExit>,
    mut w: ResMut<VoxelWorld>,
    mut settings: ResMut<Settings>,
    mut ui: ResMut<Ui>,
    mut p: ResMut<Player>,
) {
    for (i, b) in &q {
        if *i != Interaction::Pressed { continue; }
        match b.clone() {
            Btn::Single => *screen = Screen::Worlds,
            Btn::Options => *screen = Screen::Options,
            Btn::Back => *screen = Screen::Title,
            Btn::Quit => { exit.write(AppExit::Success); }
            Btn::NewWorld => {
                let meta = new_world_meta();
                let _ = save::save_meta(&meta);
                save::set_last_played(&meta.name);
                w.open(meta, true);
                next.set(AppState::Playing);
            }
            Btn::Play(name) => {
                if let Some(m) = save::load_meta(&name) {
                    if !(w.persistent && w.meta.name == name) { w.open(m, true); }
                    save::set_last_played(&name);
                    next.set(AppState::Playing);
                }
            }
            Btn::Sens(d) => { settings.sens = (settings.sens + d).clamp(0.2, 3.0); settings.save(); }
            Btn::Fov(d) => { settings.fov = (settings.fov + d).clamp(50.0, 110.0); settings.save(); }
            Btn::Occl => { settings.occlusion = !settings.occlusion; settings.save(); }
            Btn::Pack => {
                let mut opts = vec![String::new()];
                opts.extend(crate::packs::available());
                opts.push("none".into());
                let i = opts.iter().position(|o| *o == settings.pack).map(|i| (i + 1) % opts.len()).unwrap_or(0);
                settings.pack = opts[i].clone();
                settings.save();
            }
            Btn::Folder => {
                let d = crate::packs::dir();
                let _ = std::fs::create_dir_all(&d);
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

/// FOV + GPU occlusion culling (depth prepass) follow the settings.
fn apply_settings(settings: Res<Settings>, mut cams: Query<(Entity, &mut Projection), With<MainCamera>>, mut commands: Commands) {
    if !settings.is_changed() { return; }
    for (e, mut pr) in &mut cams {
        if let Projection::Perspective(pp) = &mut *pr { pp.fov = settings.fov.to_radians(); }
        if settings.occlusion {
            commands.entity(e).insert((DepthPrepass, OcclusionCulling));
        } else {
            commands.entity(e).remove::<(DepthPrepass, OcclusionCulling)>();
        }
    }
}
