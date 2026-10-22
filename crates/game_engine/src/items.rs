//! Cube-World-style held items: every item is built from tiny voxels. Blocks reuse the chunk mesher;
//! tools/weapons are 16×16 ASCII sprites extruded into 1/16 cubes (add one = add a sprite + palette).
use crate::player::{Player, HOTBAR};
use crate::sky::MainCamera;
use crate::world::{to_mesh, ChunkMats};
use crate::AppState;
use bevy::pbr::NotShadowCaster;
use bevy::prelude::*;
use common_server::blocks::id;
use common_server::mesh::{mesh_block, MeshBuf};
use common_server::style::lin;

const SWORD: [&str; 16] = [
    "..............dw",
    ".............dwb",
    "............dwb.",
    "...........dwb..",
    "..........dwb...",
    ".........dwb....",
    "........dwb.....",
    ".......dwb......",
    "......dwb.......",
    "..g..dwb........",
    "...gdwb.........",
    "....gb..........",
    "...hgg..........",
    "..hh..g.........",
    ".hh.............",
    "rh..............",
];

fn palette(ch: char) -> Option<u32> {
    match ch {
        'w' => Some(0xe8eef2), // edge
        'b' => Some(0xa9b4bd), // blade
        'd' => Some(0x6c7780), // blade shadow
        'g' => Some(0xd9a83a), // gold guard
        'h' => Some(0x6b4423), // leather grip
        'r' => Some(0x9b2c2c), // ruby pommel
        _ => None,
    }
}

/// Extrudes an ASCII sprite: each pixel → a 1/16 voxel, 2 voxels deep.
pub fn sprite_mesh(art: &[&str]) -> MeshBuf {
    let mut m = MeshBuf::default();
    let s = 1.0 / 16.0;
    for (row, line) in art.iter().enumerate() {
        for (x, ch) in line.chars().enumerate() {
            if let Some(hex) = palette(ch) {
                let (x, y) = (x as f32, (15 - row) as f32);
                m.cuboid([x * s, y * s, 0.0], [(x + 1.0) * s, (y + 1.0) * s, 2.0 * s], lin(hex));
            }
        }
    }
    m
}

#[derive(Component)]
struct Held;

pub struct ItemsPlugin;
impl Plugin for ItemsPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (refresh, animate).chain().run_if(in_state(AppState::Playing)))
            .add_systems(OnExit(AppState::Playing), clear);
    }
}

#[allow(clippy::too_many_arguments)]
fn refresh(
    mut commands: Commands,
    p: Res<Player>,
    mut shown: Local<Option<usize>>,
    held: Query<Entity, With<Held>>,
    cam: Query<Entity, With<MainCamera>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mats: Res<ChunkMats>,
) {
    if *shown == Some(p.slot) && !held.is_empty() { return; }
    *shown = Some(p.slot);
    for e in &held { commands.entity(e).despawn(); }
    let Ok(cam) = cam.single() else { return };
    let (buf, scale) = if HOTBAR[p.slot] == id::AIR { (sprite_mesh(&SWORD), 0.55) } else { (mesh_block(HOTBAR[p.slot], 16), 0.26) };
    let child = commands
        .spawn((Held, NotShadowCaster, Mesh3d(meshes.add(to_mesh(buf))), MeshMaterial3d(mats.opaque.clone()), Transform::from_scale(Vec3::splat(scale))))
        .id();
    commands.entity(cam).add_child(child);
}

fn animate(time: Res<Time>, mut p: ResMut<Player>, mut q: Query<&mut Transform, With<Held>>) {
    p.swing = (p.swing - time.delta_secs() * 4.0).max(0.0);
    let s = (p.swing * std::f32::consts::PI).sin();
    for mut t in &mut q {
        t.rotation = Quat::from_euler(EulerRot::YXZ, -0.5, -s * 1.1, 0.0);
        t.translation = Vec3::new(0.3, -0.52 - s * 0.1, -0.85);
    }
}

fn clear(mut commands: Commands, q: Query<Entity, With<Held>>) {
    for e in &q { commands.entity(e).despawn(); }
}
