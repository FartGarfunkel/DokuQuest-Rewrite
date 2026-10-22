//! Item models, Cube-World style: blocks reuse the chunk mesher, everything else is its own JS sprite
//! extruded into 1/16 voxels. Cached per item. Also the first-person held item and swing.
use crate::player::Player;
use crate::sky::MainCamera;
use crate::world::{to_mesh, ChunkMats};
use crate::AppState;
use bevy::pbr::NotShadowCaster;
use bevy::prelude::*;
use common_server::blocks::{self, Kind};
use common_server::items::reg;
use common_server::mesh::{mesh_block, mesh_sprite, MeshBuf};
use std::collections::HashMap;

#[derive(Resource, Default)]
pub struct ItemMeshes(HashMap<u16, Handle<Mesh>>);

impl ItemMeshes {
    pub fn get(&mut self, item: u16, meshes: &mut Assets<Mesh>) -> Handle<Mesh> {
        self.0
            .entry(item)
            .or_insert_with(|| {
                let mut m = model(item);
                if m.is_empty() { m.cuboid([-0.2; 3], [0.2; 3], [0.6, 0.5, 0.4]); }
                meshes.add(to_mesh(m))
            })
            .clone()
    }
}

fn shift(mut m: MeshBuf, o: [f32; 3]) -> MeshBuf {
    for p in &mut m.pos { for k in 0..3 { p[k] -= o[k]; } }
    m
}

/// Item model centred on the origin, about one unit big.
pub fn model(item: u16) -> MeshBuf {
    let d = reg().item(item);
    match d.block.map(|b| (b, blocks::kind(b))) {
        Some((b, Kind::Solid | Kind::Shape | Kind::Liquid)) => shift(mesh_block(b, 16), [0.5; 3]),
        Some((b, Kind::Plant)) => shift(mesh_sprite(blocks::def(b).faces[0], 1.0), [0.5, 0.5, 0.03]),
        _ => shift(mesh_sprite(d.icon, 1.0), [0.5, 0.5, 0.03]),
    }
}

#[derive(Component)]
struct Held;

pub struct ItemsPlugin;
impl Plugin for ItemsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<ItemMeshes>()
            .add_systems(Update, (refresh, animate).chain().run_if(in_state(AppState::Playing)))
            .add_systems(OnExit(AppState::Playing), clear);
    }
}

#[allow(clippy::too_many_arguments)]
fn refresh(
    mut commands: Commands,
    p: Res<Player>,
    mut shown: Local<Option<Option<u16>>>,
    held: Query<Entity, With<Held>>,
    cam: Query<Entity, With<MainCamera>>,
    mut cache: ResMut<ItemMeshes>,
    mut meshes: ResMut<Assets<Mesh>>,
    mats: Res<ChunkMats>,
) {
    let item = p.held().map(|s| s.item);
    if *shown == Some(item) && (item.is_none() || !held.is_empty()) { return; }
    *shown = Some(item);
    for e in &held { commands.entity(e).despawn(); }
    let (Ok(cam), Some(item)) = (cam.single(), item) else { return };
    let block = reg().item(item).block.is_some_and(|b| matches!(blocks::kind(b), Kind::Solid | Kind::Shape));
    let child = commands
        .spawn((Held, NotShadowCaster, Mesh3d(cache.get(item, &mut meshes)), MeshMaterial3d(mats.opaque.clone()), Transform::from_scale(Vec3::splat(if block { 0.32 } else { 0.55 }))))
        .id();
    commands.entity(cam).add_child(child);
}

fn animate(time: Res<Time>, mut p: ResMut<Player>, mut q: Query<&mut Transform, With<Held>>) {
    p.swing = (p.swing - time.delta_secs() * 4.0).max(0.0);
    let s = (p.swing * std::f32::consts::PI).sin();
    for mut t in &mut q {
        t.rotation = Quat::from_euler(EulerRot::YXZ, -0.6, -s * 1.1 + 0.15, 0.0);
        t.translation = Vec3::new(0.42, -0.42 - s * 0.1, -0.8);
    }
}

fn clear(mut commands: Commands, q: Query<Entity, With<Held>>) {
    for e in &q { commands.entity(e).despawn(); }
}
