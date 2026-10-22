//! Survival rules (game/player.js, local.js — no hunger): damage (reduced by armour), regeneration, death, and dropped items that
//! pop out, fall, and fly into your inventory.
use crate::hud::Toast;
use crate::items::ItemMeshes;
use crate::player::{Player, Ui};
use crate::sounds::Sfx;
use crate::world::{ChunkMats, VoxelWorld};
use crate::AppState;
use bevy::prelude::*;
use common_server::blocks;
use common_server::items::{reg, Stack};

#[derive(Event)]
pub struct DropItem {
    pub pos: Vec3,
    pub stack: Stack,
}

#[derive(Event)]
pub struct Hurt(pub f32);

#[derive(Component)]
pub struct Dropped {
    stack: Stack,
    vel: Vec3,
    age: f32,
}

pub struct SurvivalPlugin;
impl Plugin for SurvivalPlugin {
    fn build(&self, app: &mut App) {
        app.add_event::<DropItem>()
            .add_event::<Hurt>()
            .add_systems(Update, (spawn_drops, drops, vitals).run_if(in_state(AppState::Playing)))
            .add_systems(OnExit(AppState::Playing), clear);
    }
}

fn spawn_drops(mut ev: EventReader<DropItem>, mut commands: Commands, mut cache: ResMut<ItemMeshes>, mut meshes: ResMut<Assets<Mesh>>, mats: Res<ChunkMats>, time: Res<Time>) {
    for (i, e) in ev.read().enumerate() {
        let a = time.elapsed_secs() * 7.3 + i as f32 * 2.1;
        commands.spawn((
            Dropped { stack: e.stack, vel: Vec3::new(a.sin() * 1.5, 4.0, a.cos() * 1.5), age: 0.0 },
            Mesh3d(cache.get(e.stack.item, &mut meshes)),
            MeshMaterial3d(mats.opaque.clone()),
            Transform::from_translation(e.pos).with_scale(Vec3::splat(0.3)),
        ));
    }
}

fn drops(mut commands: Commands, time: Res<Time>, w: Res<VoxelWorld>, mut p: ResMut<Player>, mut q: Query<(Entity, &mut Dropped, &mut Transform)>, mut sfx: EventWriter<Sfx>) {
    let dt = time.delta_secs().min(0.05);
    let centre = p.pos + Vec3::Y * 0.8;
    for (e, mut d, mut t) in &mut q {
        d.age += dt;
        let to = centre - t.translation;
        let dist = to.length();
        let magnet = d.age > 0.6 && dist < 1.8;
        if magnet {
            d.vel = to.normalize_or_zero() * 8.0;
        } else {
            d.vel.y -= 20.0 * dt;
            d.vel.x *= 0.92;
            d.vel.z *= 0.92;
        }
        let next = t.translation + d.vel * dt;
        if !magnet && d.vel.y < 0.0 && blocks::is_solid(w.get((next - Vec3::Y * 0.15).floor().as_ivec3())) {
            d.vel = Vec3::ZERO;
        } else {
            t.translation = next;
        }
        t.rotate_y(dt * 1.5);
        if d.age > 0.6 && dist < 0.7 {
            match p.inv.add(d.stack) {
                None => { commands.entity(e).despawn(); sfx.write(Sfx::fx("pop")); }
                Some(rest) => d.stack = rest,
            }
        } else if d.age > 300.0 {
            commands.entity(e).despawn();
        }
    }
}

fn vitals(time: Res<Time>, mut p: ResMut<Player>, mut hurt: EventReader<Hurt>, mut ui: ResMut<Ui>, mut toast: ResMut<Toast>, mut cd: Local<f32>, mut sfx: EventWriter<Sfx>, mut regen: Local<f32>) {
    let dt = time.delta_secs();
    *cd -= dt;
    if p.creative || *ui == Ui::Dead {
        hurt.clear();
        if p.creative { p.health = 20.0; }
        return;
    }
    for h in hurt.read() {
        if *cd <= 0.0 {
            let armour: f32 = p.inv.slots[36..40].iter().flatten().map(|s| reg().item(s.item).armor).sum();
            p.health -= h.0 * (1.0 - (armour * 0.04).min(0.8));
            *cd = 0.45;
            sfx.write(Sfx::fx("hurt"));
        }
    }
    // like game/local.js: no hunger; 1 health back every 4 seconds
    *regen += dt;
    if *regen > 4.0 {
        *regen = 0.0;
        if p.health > 0.0 { p.health = (p.health + 1.0).min(20.0); }
    }
    if p.health <= 0.0 {
        p.health = 0.0;
        *ui = Ui::Dead;
        toast.show("You died!".into());
    }
}

fn clear(mut commands: Commands, q: Query<Entity, With<Dropped>>) {
    for e in &q { commands.entity(e).despawn(); }
}
