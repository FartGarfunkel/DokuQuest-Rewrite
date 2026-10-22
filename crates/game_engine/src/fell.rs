//! Felled trees (game/entities.js "felled trees"): chop a log and every log above it, with its
//! leaves, topples over away from you and breaks into drops (logs; saplings and apples from leaves).
//! Also: sand/gravel fall when unsupported, and saplings grow into trees.
use crate::player::Player;
use crate::survival::DropItem;
use crate::world::{to_mesh, ChunkMats, VoxelWorld};
use crate::AppState;
use bevy::prelude::*;
use common_server::blocks::{self, by_key, def, id, Kind};
use common_server::dims;
use common_server::items::{reg, Stack};
use common_server::mesh::mesh_block;
use std::collections::{HashMap, HashSet};

#[derive(Event)]
pub struct Chopped {
    pub at: IVec3,
    pub dir: Vec3,
}

#[derive(Component)]
struct Falling {
    t: f32,
    axis: Vec3,
    blocks: Vec<(IVec3, u16)>,
}

pub struct FellPlugin;
impl Plugin for FellPlugin {
    fn build(&self, app: &mut App) {
        app.add_event::<Chopped>().add_systems(Update, (fell, topple, grow).chain().run_if(in_state(AppState::Playing)));
    }
}

pub fn is_log(b: u16) -> bool {
    let d = def(b);
    d.kind == Kind::Solid && !d.key.starts_with("stripped") && (d.key == "log" || d.key.ends_with("_log") || d.key.ends_with("_stem") || d.key.ends_with("_wood") || d.key.ends_with("_hyphae"))
}
fn is_leaves(b: u16) -> bool { def(b).key.contains("leaves") }
fn passable(b: u16) -> bool { matches!(blocks::kind(b), Kind::Air | Kind::Liquid | Kind::Plant) }

/// Lets sand, gravel & co. above (or at) `p` drop onto the first solid block below.
pub fn settle(w: &mut VoxelWorld, p: IVec3) {
    let mut at = p;
    if !def(w.get(at)).falls { at += IVec3::Y; }
    while def(w.get(at)).falls {
        let b = w.get(at);
        let mut to = at;
        while to.y > dims::WORLD_MIN_Y && w.is_loaded(to - IVec3::Y) && passable(w.get(to - IVec3::Y)) { to -= IVec3::Y; }
        if to != at {
            w.set(at, id::AIR);
            w.set(to, b);
        }
        at += IVec3::Y;
    }
}

fn rnd(s: &mut u32) -> f32 {
    if *s == 0 { *s = 0x1234_5679; }
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    (*s >> 8) as f32 / 16_777_216.0
}

fn fell(
    mut ev: EventReader<Chopped>,
    mut w: ResMut<VoxelWorld>,
    mut commands: Commands,
    mut meshes: ResMut<Assets<Mesh>>,
    mats: Res<ChunkMats>,
    mut cache: Local<HashMap<u16, Handle<Mesh>>>,
) {
    for c in ev.read() {
        let (mut logs, mut seen, mut stack) = (Vec::new(), HashSet::new(), vec![c.at + IVec3::Y]);
        while let Some(p) = stack.pop() {
            if logs.len() >= 160 || !seen.insert(p) || p.y <= c.at.y || (p.x - c.at.x).abs() > 6 || (p.z - c.at.z).abs() > 6 { continue; }
            if !is_log(w.get(p)) { continue; }
            logs.push(p);
            for dy in 0..=1 { for dz in -1..=1 { for dx in -1..=1 { stack.push(p + IVec3::new(dx, dy, dz)); } } }
        }
        if logs.is_empty() { continue; }
        let mut leaves = Vec::new();
        for &l in &logs {
            for dy in -2..=2 { for dz in -2..=2 { for dx in -2..=2 {
                let q = l + IVec3::new(dx, dy, dz);
                if leaves.len() < 500 && is_leaves(w.get(q)) && seen.insert(q) { leaves.push(q); }
            } } }
        }
        let d = Vec3::new(c.dir.x, 0.0, c.dir.z).normalize_or(Vec3::X);
        let pivot = commands.spawn((Transform::from_translation(c.at.as_vec3() + Vec3::new(0.5, 1.0, 0.5)), Visibility::default())).id();
        let mut parts = Vec::new();
        for p in logs.into_iter().chain(leaves) {
            let b = w.get(p);
            let mesh = cache.entry(b).or_insert_with(|| meshes.add(to_mesh(mesh_block(b, 4)))).clone();
            let off = p - c.at - IVec3::Y;
            let child = commands.spawn((Mesh3d(mesh), MeshMaterial3d(mats.opaque.clone()), Transform::from_translation(off.as_vec3() - Vec3::new(0.5, 0.0, 0.5)))).id();
            commands.entity(pivot).add_child(child);
            parts.push((off, b));
            w.set(p, id::AIR);
        }
        commands.entity(pivot).insert(Falling { t: 0.0, axis: Vec3::Y.cross(d).normalize_or(Vec3::Z), blocks: parts });
    }
}

fn topple(time: Res<Time>, mut commands: Commands, mut q: Query<(Entity, &mut Falling, &mut Transform)>, mut drops: EventWriter<DropItem>, mut seed: Local<u32>) {
    for (e, mut f, mut t) in &mut q {
        f.t += time.delta_secs();
        let a = (f.t * f.t * 1.2).min(std::f32::consts::FRAC_PI_2);
        t.rotation = Quat::from_axis_angle(f.axis, a);
        if a < std::f32::consts::FRAC_PI_2 { continue; }
        for &(o, b) in &f.blocks {
            let at = t.translation + t.rotation * (o.as_vec3() + Vec3::Y * 0.5);
            let item = if is_log(b) {
                reg().of_block(b)
            } else {
                let k = def(b).key;
                let r = rnd(&mut seed);
                if r < 0.06 { by_key(&if k == "leaves" { "sapling".to_string() } else { k.replace("leaves", "sapling") }).and_then(|s| reg().of_block(s)) }
                else if r < 0.08 { reg().by_key("apple") } else { None }
            };
            if let Some(item) = item { drops.write(DropItem { pos: at, stack: Stack { item, count: 1 } }); }
        }
        commands.entity(e).despawn();
    }
}

/// Random ticks near the player: saplings occasionally grow into trees.
fn grow(time: Res<Time>, mut t: Local<f32>, mut w: ResMut<VoxelWorld>, p: Res<Player>, mut seed: Local<u32>) {
    *t += time.delta_secs();
    if *t < 0.25 { return; }
    *t = 0.0;
    let c = p.pos.floor().as_ivec3();
    for _ in 0..64 {
        let q = c + IVec3::new((rnd(&mut seed) * 64.0) as i32 - 32, (rnd(&mut seed) * 24.0) as i32 - 12, (rnd(&mut seed) * 64.0) as i32 - 32);
        let b = w.get(q);
        let k = def(b).key;
        if !k.contains("sapling") || rnd(&mut seed) > 0.01 { continue; }
        let base = if k == "sapling" { String::new() } else { k.replace("_sapling", "_") };
        let (log, leaf) = (by_key(&format!("{base}log")).unwrap_or(id::LOG), by_key(&format!("{base}leaves")).unwrap_or(id::LEAVES));
        let h = 4 + (rnd(&mut seed) * 3.0) as i32;
        if (1..=h + 1).any(|y| !passable(w.get(q + IVec3::Y * y)) && !is_leaves(w.get(q + IVec3::Y * y))) { continue; }
        for y in h - 2..=h + 1 {
            let r: i32 = if y >= h { 1 } else { 2 };
            for dz in -r..=r { for dx in -r..=r {
                if dx.abs() == r && dz.abs() == r && y == h + 1 { continue; }
                let l = q + IVec3::new(dx, y, dz);
                if w.get(l) == id::AIR { w.set(l, leaf); }
            } }
        }
        for y in 0..h { w.set(q + IVec3::Y * y, log); }
    }
}
