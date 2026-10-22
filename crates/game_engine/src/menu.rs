//! Title screen. Behind the buttons is the real world, rotating slowly: the demo world until you have
//! played, then your last-played world, starting from exactly where (and which way) you saved.
use crate::sky::MainCamera;
use crate::world::VoxelWorld;
use crate::AppState;
use bevy::prelude::*;
use common_server::save::{self, WorldMeta};
use common_server::worldgen::{Gen, GEN_VERSION};

#[derive(Component)]
struct MenuRoot;

#[derive(Component, Clone, Copy)]
enum Btn {
    Continue,
    NewWorld,
    Quit,
}

#[derive(Resource, Default)]
struct Pano {
    centre: Vec3,
    yaw: f32,
}

pub struct MenuPlugin;
impl Plugin for MenuPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Pano>()
            .add_systems(OnEnter(AppState::Menu), open)
            .add_systems(OnExit(AppState::Menu), close)
            .add_systems(Update, (spin, buttons).run_if(in_state(AppState::Menu)));
    }
}

fn idle() -> Color { Color::srgba(0.08, 0.06, 0.04, 0.78) }
fn hover() -> Color { Color::srgba(0.32, 0.22, 0.10, 0.9) }

fn open(mut commands: Commands, w: Res<VoxelWorld>, mut pano: ResMut<Pano>) {
    *pano = Pano { centre: Vec3::from(w.meta.pos) + Vec3::Y * crate::player::EYE, yaw: w.meta.yaw };
    let root = Node {
        width: Val::Percent(100.0),
        height: Val::Percent(100.0),
        flex_direction: FlexDirection::Column,
        align_items: AlignItems::Center,
        justify_content: JustifyContent::Center,
        row_gap: Val::Px(14.0),
        ..default()
    };
    let persistent = w.persistent;
    let name = w.meta.name.clone();
    commands.spawn((MenuRoot, root)).with_children(|ui| {
        ui.spawn((Text::new("DokuQuest"), TextFont { font_size: 72.0, ..default() }, TextColor(Color::srgb(0.98, 0.84, 0.45))));
        ui.spawn((
            Text::new(format!("Rust rewrite {} · voxel test build", env!("CARGO_PKG_VERSION"))),
            TextFont { font_size: 18.0, ..default() },
            TextColor(Color::srgb(0.92, 0.92, 0.92)),
        ));
        let mut button = |label: String, b: Btn| {
            ui.spawn((
                Button,
                b,
                Node { width: Val::Px(340.0), height: Val::Px(50.0), justify_content: JustifyContent::Center, align_items: AlignItems::Center, ..default() },
                BackgroundColor(idle()),
            ))
            .with_children(|t| {
                t.spawn((Text::new(label), TextFont { font_size: 24.0, ..default() }, TextColor(Color::WHITE)));
            });
        };
        if persistent { button(format!("Continue: {name}"), Btn::Continue); }
        button("New World".into(), Btn::NewWorld);
        button("Quit".into(), Btn::Quit);
    });
}

fn close(mut commands: Commands, q: Query<Entity, With<MenuRoot>>) {
    for e in &q { commands.entity(e).despawn(); }
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

fn buttons(
    mut q: Query<(&Interaction, &Btn, &mut BackgroundColor), Changed<Interaction>>,
    mut next: ResMut<NextState<AppState>>,
    mut exit: EventWriter<AppExit>,
    mut w: ResMut<VoxelWorld>,
) {
    for (i, b, mut bg) in &mut q {
        match *i {
            Interaction::Hovered => bg.0 = hover(),
            Interaction::None => bg.0 = idle(),
            Interaction::Pressed => match b {
                Btn::Continue => next.set(AppState::Playing),
                Btn::NewWorld => {
                    let meta = new_world_meta();
                    let _ = save::save_meta(&meta);
                    save::set_last_played(&meta.name);
                    w.open(meta, true);
                    next.set(AppState::Playing);
                }
                Btn::Quit => {
                    exit.write(AppExit::Success);
                }
            },
        }
    }
}
