//! Mobs (game/entities.js, mobai.js — alpha subset): spawning by time, depth and dimension,
//! villagers around villages, wander / flee / chase AI, gravity + block collision with hop-ups,
//! melee attacks, loot drops, and jointed voxel bodies built from the JS rigs (legs/arms/wings swing).
use crate::player::{box_hits, Player};
use crate::sky::DayTime;
use crate::survival::{DropItem, Hurt};
use crate::world::{to_mesh, ChunkMats, VoxelWorld};
use crate::AppState;
use bevy::prelude::*;
use common_server::blocks::{self, id, Kind};
use common_server::dims;
use common_server::items::{reg, Stack};
use common_server::mesh::{textured_box, MeshBuf};
use common_server::mobs::data;
use std::collections::HashMap;

const CAP: usize = 24;

#[derive(Component)]
pub struct Mob {
    pub kind: usize,
    pub hp: f32,
    pub vel: Vec3,
    pub hurt: f32,
    pub angry: bool,
    yaw: f32,
    cd: f32,
    wander: Vec3,
    think: f32,
    phase: f32,
    on_ground: bool,
    home: Option<Vec3>,
}

#[derive(Component)]
struct Limb {
    mob: Entity,
    kind: u8,
    side: f32,
    front: bool,
}

#[derive(Resource, Default)]
struct Bodies(HashMap<String, Vec<Handle<Mesh>>>);

pub struct MobsPlugin;
impl Plugin for MobsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Bodies>()
            .add_systems(Update, (spawn, think, physics, animate, die).chain().run_if(in_state(AppState::Playing)))
            .add_systems(OnExit(AppState::Playing), clear);
    }
}

fn rnd(s: &mut u32) -> f32 {
    if *s == 0 { *s = 0x2545_F491; }
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    (*s >> 8) as f32 / 16_777_216.0
}

fn pick(list: &[&str], r: f32) -> Option<usize> {
    let v: Vec<usize> = list.iter().filter_map(|k| data().keys.get(*k).copied()).collect();
    (!v.is_empty()).then(|| v[((r * v.len() as f32) as usize).min(v.len() - 1)])
}

fn hw(kind: usize) -> f32 { data().types[kind].w.clamp(0.15, 0.45) }

#[allow(clippy::too_many_arguments)]
fn spawn(
    mut commands: Commands,
    time: Res<Time>,
    day: Res<DayTime>,
    w: Res<VoxelWorld>,
    p: Res<Player>,
    q: Query<(&Mob, &Transform)>,
    mut bodies: ResMut<Bodies>,
    mut meshes: ResMut<Assets<Mesh>>,
    mats: Res<ChunkMats>,
    mut t: Local<f32>,
    mut seed: Local<u32>,
) {
    *t += time.delta_secs();
    if *t < 1.2 { return; }
    *t = 0.0;
    let g = w.generator();
    // villagers keep their village lively
    if let Some(vk) = data().keys.get("villager").copied() {
        for v in g.villages_near(p.pos.x as i32, p.pos.z as i32, 72) {
            let c = Vec3::new(v[0] as f32 + 0.5, v[1] as f32 + 1.0, v[2] as f32 + 0.5);
            let n = q.iter().filter(|(m, tr)| m.kind == vk && tr.translation.distance(c) < 40.0).count();
            if n < 3 && w.is_loaded(c.floor().as_ivec3()) {
                let off = Vec3::new(rnd(&mut seed) * 8.0 - 4.0, 2.0, rnd(&mut seed) * 8.0 - 4.0);
                let e = spawn_mob(&mut commands, vk, c + off, &mut bodies, &mut meshes, &mats);
                commands.entity(e).insert(Home(c));
            }
        }
    }
    if q.iter().count() >= CAP { return; }
    let a = rnd(&mut seed) * std::f32::consts::TAU;
    let d = 18.0 + rnd(&mut seed) * 22.0;
    let (x, z) = ((p.pos.x + a.cos() * d).floor() as i32, (p.pos.z + a.sin() * d).floor() as i32);
    let mut ground = None;
    for y in (p.pos.y as i32 - 16..p.pos.y as i32 + 16).rev() {
        let b = IVec3::new(x, y, z);
        if !w.is_loaded(b) { continue; }
        if blocks::is_solid(w.get(b)) && w.get(b + IVec3::Y) == id::AIR && w.get(b + IVec3::Y * 2) == id::AIR {
            ground = Some(b + IVec3::Y);
            break;
        }
    }
    let Some(gp) = ground else { return };
    let band = dims::band_at(gp.y).map(|b| b.key).unwrap_or("");
    let under = band == "overworld" && gp.y < g.height(gp.x, gp.z) - 5;
    let night = day.0 > 0.52 && day.0 < 0.98;
    let r = rnd(&mut seed);
    let kind = match band {
        "nether" => pick(&["charred_ghoul", "cinder_golem", "magma_slime", "cinder_wisp"], r),
        "skylands" => pick(&["aerbunny", "moa"], r),
        "overworld" if night || under => pick(&["ghoul", "skeleton", "spider", "fuseling", "slime", "witch", "husk", "stray"], r),
        "overworld" if w.get(gp - IVec3::Y) == id::GRASS => {
            pick(&["cow", "pig", "sheep", "chicken", "rabbit", "deer", "boar", "horse", "fox", "wolf", "goat", "turkey", "duck"], r)
        }
        _ => None,
    };
    if let Some(k) = kind { spawn_mob(&mut commands, k, gp.as_vec3() + Vec3::new(0.5, 0.0, 0.5), &mut bodies, &mut meshes, &mats); }
}

#[derive(Component)]
struct Home(Vec3);

fn spawn_mob(commands: &mut Commands, kind: usize, at: Vec3, bodies: &mut Bodies, meshes: &mut Assets<Mesh>, mats: &ChunkMats) -> Entity {
    let ty = &data().types[kind];
    let mob = Mob { kind, hp: ty.hp, vel: Vec3::ZERO, hurt: -10.0, angry: false, yaw: 0.0, cd: 0.0, wander: Vec3::ZERO, think: 0.0, phase: 0.0, on_ground: false, home: None };
    let root = commands.spawn((mob, Transform::from_translation(at), Visibility::default())).id();
    match data().models.get(&ty.model) {
        Some(model) if !model.parts.is_empty() => {
            let hs = bodies
                .0
                .entry(ty.model.clone())
                .or_insert_with(|| {
                    model.parts.iter().map(|pt| {
                        let mut m = MeshBuf::default();
                        textured_box(&mut m, pt.min, pt.max, pt.tex);
                        if m.is_empty() { m.cuboid(pt.min, pt.max, [0.5, 0.4, 0.35]); }
                        meshes.add(to_mesh(m))
                    }).collect()
                })
                .clone();
            commands.entity(root).insert(Transform::from_translation(at).with_scale(Vec3::splat(model.scale)));
            let mut ids: Vec<Entity> = Vec::with_capacity(model.parts.len());
            for (i, part) in model.parts.iter().enumerate() {
                let l = part.limb.as_str();
                let kind = if l.contains("leg") { 1 } else if l.contains("arm") { 2 } else if l.contains("head") { 3 } else if l.contains("wing") { 4 } else { 0 };
                let e = commands
                    .spawn((Mesh3d(hs[i].clone()), MeshMaterial3d(mats.opaque.clone()), Transform::from_translation(Vec3::from(part.pivot)), Limb { mob: root, kind, side: part.side, front: part.front }))
                    .id();
                let parent = part.parent.and_then(|j| ids.get(j).copied()).unwrap_or(root);
                commands.entity(parent).add_child(e);
                ids.push(e);
            }
        }
        _ => {
            let mut m = MeshBuf::default();
            let h = hw(kind);
            m.cuboid([-h, 0.0, -h], [h, ty.h, h], [0.55, 0.45, 0.38]);
            let e = commands.spawn((Mesh3d(meshes.add(to_mesh(m))), MeshMaterial3d(mats.opaque.clone()), Transform::default())).id();
            commands.entity(root).add_child(e);
        }
    }
    root
}

fn turn_to(a: f32, b: f32, k: f32) -> f32 {
    let d = (b - a + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
    a + d * k.min(1.0)
}

fn think(time: Res<Time>, mut q: Query<(&mut Mob, &Transform, Option<&Home>)>, p: Res<Player>, mut hurt: EventWriter<Hurt>, mut seed: Local<u32>) {
    let dt = time.delta_secs().min(0.05);
    for (mut m, t, home) in &mut q {
        let ty = &data().types[m.kind];
        m.cd -= dt;
        m.hurt -= dt;
        m.think -= dt;
        if let Some(h) = home { m.home = Some(h.0); }
        let to = p.pos - t.translation;
        let dist = to.length();
        let flat = Vec3::new(to.x, 0.0, to.z).normalize_or_zero();
        let speed = ty.speed.clamp(0.6, 6.0);
        let aggressive = (ty.hostile || (m.angry && ty.damage > 0.0)) && !p.creative && p.health > 0.0;
        let want = if aggressive && dist < 18.0 {
            if dist < 1.2 + hw(m.kind) && to.y.abs() < 1.8 && m.cd <= 0.0 {
                hurt.write(Hurt(ty.damage.max(1.0)));
                m.cd = 1.0;
            }
            flat * speed * 1.2
        } else if m.hurt > -3.0 && !ty.hostile {
            -flat * speed * 1.6
        } else {
            if m.think <= 0.0 {
                m.think = 2.0 + rnd(&mut seed) * 4.0;
                let a = rnd(&mut seed) * std::f32::consts::TAU;
                m.wander = if rnd(&mut seed) < 0.4 { Vec3::ZERO } else { Vec3::new(a.cos(), 0.0, a.sin()) * speed * 0.5 };
                if let Some(h) = m.home {
                    let back = h - t.translation;
                    if Vec3::new(back.x, 0.0, back.z).length() > 14.0 { m.wander = Vec3::new(back.x, 0.0, back.z).normalize_or_zero() * speed * 0.5; }
                }
            }
            m.wander
        };
        let k = (dt * 6.0).min(1.0);
        m.vel.x += (want.x - m.vel.x) * k;
        m.vel.z += (want.z - m.vel.z) * k;
        if want.length() > 0.05 {
            let target = (-want.x).atan2(-want.z);
            m.yaw = turn_to(m.yaw, target, dt * 8.0);
        }
    }
}

fn physics(time: Res<Time>, w: Res<VoxelWorld>, mut q: Query<(&mut Mob, &mut Transform)>) {
    let dt = time.delta_secs().min(0.05);
    let now = time.elapsed_secs();
    for (mut m, mut t) in &mut q {
        if !w.is_loaded(t.translation.floor().as_ivec3()) { continue; }
        let ty = &data().types[m.kind];
        let (hw, h) = (hw(m.kind), ty.h.clamp(0.3, 3.0));
        let in_liquid = blocks::kind(w.get((t.translation + Vec3::Y * 0.3).floor().as_ivec3())) == Kind::Liquid;
        if ty.flies() {
            m.vel.y += ((now * 0.7 + m.kind as f32).sin() * 0.8 - m.vel.y) * dt;
        } else if in_liquid {
            m.vel.y = (m.vel.y + 14.0 * dt).min(2.0);
        } else {
            m.vel.y = (m.vel.y - 24.0 * dt).max(-30.0);
        }
        let delta = m.vel * dt;
        let mut blocked = false;
        m.on_ground = false;
        for axis in [1, 0, 2] {
            let mut np = t.translation;
            np[axis] += delta[axis];
            if box_hits(&w, np, hw, h) {
                if axis == 1 {
                    if delta.y < 0.0 { m.on_ground = true; }
                    m.vel.y = 0.0;
                } else {
                    blocked = true;
                }
            } else {
                t.translation = np;
            }
        }
        if blocked && m.on_ground { m.vel.y = 7.5; }
        let sp = Vec2::new(m.vel.x, m.vel.z).length();
        m.phase += sp * dt * 4.0;
        t.rotation = Quat::from_rotation_y(m.yaw);
    }
}

fn animate(time: Res<Time>, mobs: Query<&Mob>, mut limbs: Query<(&Limb, &mut Transform), Without<Mob>>) {
    let now = time.elapsed_secs();
    for (l, mut t) in &mut limbs {
        let Ok(m) = mobs.get(l.mob) else { continue };
        let amt = (Vec2::new(m.vel.x, m.vel.z).length() / 1.5).min(1.0) * 0.8;
        let s = m.phase.sin() * amt;
        let off = if (l.side > 0.0) ^ l.front { 1.0 } else { -1.0 };
        t.rotation = match l.kind {
            1 => Quat::from_rotation_x(s * off),
            2 => Quat::from_rotation_x(-s * off),
            3 => Quat::from_rotation_x(if m.hurt > 0.0 { 0.35 } else { 0.0 }),
            4 => Quat::from_rotation_z((now * 10.0).sin() * 0.6 * l.side.signum()),
            _ => t.rotation,
        };
    }
}

fn die(mut commands: Commands, q: Query<(Entity, &Mob, &Transform)>, p: Res<Player>, mut drops: EventWriter<DropItem>, mut seed: Local<u32>) {
    for (e, m, t) in &q {
        let ty = &data().types[m.kind];
        if m.hp <= 0.0 {
            for (k, mn, mx, ch) in &ty.drops {
                if rnd(&mut seed) > *ch { continue; }
                let n = *mn + (rnd(&mut seed) * (mx.saturating_sub(*mn) + 1) as f32) as u32;
                if let (true, Some(item)) = (n > 0, reg().by_key(k)) {
                    drops.write(DropItem { pos: t.translation + Vec3::Y * 0.5, stack: Stack { item, count: n } });
                }
            }
            commands.entity(e).despawn();
        } else if m.home.is_none() && t.translation.distance(p.pos) > 110.0 {
            commands.entity(e).despawn();
        }
    }
}

fn clear(mut commands: Commands, q: Query<Entity, With<Mob>>) {
    for e in &q { commands.entity(e).despawn(); }
}
