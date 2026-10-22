//! Ambient effects. Now: falling leaves that drift on a shared wind. Later: wind-swayed plants,
//! wave/shoreline water, waterfalls, fire/campfire light (needs shaders — see CLAUDE.md roadmap).
use crate::world::VoxelWorld;
use bevy::prelude::*;
use common_server::blocks::{self, id};

const MAX_LEAVES: usize = 90;

#[derive(Component)]
struct Leaf {
    vel: Vec3,
    life: f32,
    spin: f32,
}

#[derive(Resource)]
struct LeafAssets {
    mesh: Handle<Mesh>,
    mat: Handle<StandardMaterial>,
}

pub struct FxPlugin;
impl Plugin for FxPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Startup, setup).add_systems(Update, (spawn_leaves, drift));
    }
}

fn setup(mut commands: Commands, mut meshes: ResMut<Assets<Mesh>>, mut mats: ResMut<Assets<StandardMaterial>>) {
    commands.insert_resource(LeafAssets {
        mesh: meshes.add(Cuboid::new(0.08, 0.02, 0.08)),
        mat: mats.add(StandardMaterial { base_color: Color::srgb(0.32, 0.56, 0.2), perceptual_roughness: 0.9, ..default() }),
    });
}

fn rnd(s: &mut u32) -> f32 {
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    (*s >> 8) as f32 / 16_777_216.0
}

fn spawn_leaves(mut commands: Commands, w: Res<VoxelWorld>, a: Res<LeafAssets>, q: Query<(), With<Leaf>>, mut seed: Local<u32>) {
    if *seed == 0 { *seed = 0x9E37_79B9; }
    if q.iter().count() >= MAX_LEAVES { return; }
    for _ in 0..12 {
        let off = Vec3::new(rnd(&mut seed) * 32.0 - 16.0, rnd(&mut seed) * 16.0 - 4.0, rnd(&mut seed) * 32.0 - 16.0);
        let b = (w.focus + off).floor().as_ivec3();
        if w.get(b) == id::LEAVES && w.get(b - IVec3::Y) == id::AIR {
            let at = b.as_vec3() + Vec3::new(rnd(&mut seed), -0.05, rnd(&mut seed));
            let spin = rnd(&mut seed) * 6.0 - 3.0;
            commands.spawn((Leaf { vel: Vec3::ZERO, life: 8.0, spin }, Mesh3d(a.mesh.clone()), MeshMaterial3d(a.mat.clone()), Transform::from_translation(at)));
            break;
        }
    }
}

fn drift(mut commands: Commands, time: Res<Time>, w: Res<VoxelWorld>, mut q: Query<(Entity, &mut Leaf, &mut Transform)>) {
    let (dt, t) = (time.delta_secs(), time.elapsed_secs());
    let wind = Vec3::new((t * 0.3).sin() * 0.9 + 0.7, 0.0, (t * 0.21).cos() * 0.6);
    for (e, mut l, mut tr) in &mut q {
        l.life -= dt;
        let flutter = (t * 3.0 + l.spin * 10.0).sin();
        let target = wind + Vec3::new(flutter * 0.4, -0.7 + flutter * 0.25, 0.0);
        l.vel = l.vel.lerp(target, (dt * 2.0).min(1.0));
        tr.translation += l.vel * dt;
        tr.rotate_y(l.spin * dt);
        tr.rotate_x(flutter * dt * 2.0);
        if l.life <= 0.0 || blocks::is_solid(w.get(tr.translation.floor().as_ivec3())) { commands.entity(e).despawn(); }
    }
}
