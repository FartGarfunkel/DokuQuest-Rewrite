//! Your skin (64×64 Minecraft-layout PNG, as the JS skin editor exports) on a voxel body: every skin
//! pixel is a 1/16 voxel face. Seen in third person (F5 / controller Select). Chosen in Options →
//! Choose skin; stored at <data>/skin.png.
use crate::player::Player;
use crate::world::{to_mesh, ChunkMats};
use crate::AppState;
use bevy::prelude::*;
use common_server::mesh::MeshBuf;
use common_server::{save, tiles::lin8};

pub fn path() -> std::path::PathBuf { save::data_dir().join("skin.png") }

#[derive(Resource, Default)]
pub struct View {
    pub third: bool,
}

#[derive(Component)]
struct Body;
#[derive(Component)]
struct Limb(f32);

pub struct SkinPlugin;
impl Plugin for SkinPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<View>()
            .add_systems(Update, (toggle, sync).chain().run_if(in_state(AppState::Playing)))
            .add_systems(OnExit(AppState::Playing), clear);
    }
}

fn toggle(keys: Res<ButtonInput<KeyCode>>, pads: Query<&Gamepad>, mut v: ResMut<View>) {
    if keys.just_pressed(KeyCode::F5) || pads.iter().any(|g| g.just_pressed(GamepadButton::Select)) { v.third = !v.third; }
}

/// A plain default skin when none is chosen.
fn default_skin() -> image::RgbaImage {
    image::RgbaImage::from_fn(64, 64, |_, y| image::Rgba(if y < 16 { [196, 150, 112, 255] } else if y < 32 { [46, 110, 120, 255] } else { [52, 60, 120, 255] }))
}

/// One skin box: `min`/size in pixels (relative to its joint), `uv` = top-left of its unwrap.
fn skin_box(out: &mut MeshBuf, img: &image::RgbaImage, min: [f32; 3], s: [u32; 3], uv: (u32, u32)) {
    let (w, h, d) = (s[0], s[1], s[2]);
    let (u, v) = uv;
    let max = [min[0] + w as f32, min[1] + h as f32, min[2] + d as f32];
    type F = ([f32; 3], [f32; 3], [f32; 3], [f32; 3], u32, u32, u32, u32);
    let faces: [F; 6] = [
        ([0., 0., -1.], [max[0], max[1], min[2]], [-1., 0., 0.], [0., -1., 0.], w, h, u + d, v + d),
        ([0., 0., 1.], [min[0], max[1], max[2]], [1., 0., 0.], [0., -1., 0.], w, h, u + 2 * d + w, v + d),
        ([1., 0., 0.], [max[0], max[1], max[2]], [0., 0., -1.], [0., -1., 0.], d, h, u, v + d),
        ([-1., 0., 0.], [min[0], max[1], min[2]], [0., 0., 1.], [0., -1., 0.], d, h, u + d + w, v + d),
        ([0., 1., 0.], [max[0], max[1], max[2]], [-1., 0., 0.], [0., 0., -1.], w, d, u + d, v),
        ([0., -1., 0.], [max[0], min[1], min[2]], [-1., 0., 0.], [0., 0., 1.], w, d, u + d + w, v),
    ];
    for (n, o, a, b, cols, rows, tx, ty) in faces {
        for j in 0..rows {
            for i in 0..cols {
                let p = img.get_pixel((tx + i).min(img.width() - 1), (ty + j).min(img.height() - 1)).0;
                if p[3] < 128 { continue; }
                let c = [lin8(p[0]), lin8(p[1]), lin8(p[2])];
                let at = |di: f32, dj: f32| std::array::from_fn(|k| (o[k] + a[k] * (i as f32 + di) + b[k] * (j as f32 + dj)) / 16.0);
                out.quad([at(0., 0.), at(1., 0.), at(1., 1.), at(0., 1.)], n, [c; 4]);
            }
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn sync(
    mut commands: Commands,
    view: Res<View>,
    p: Res<Player>,
    time: Res<Time>,
    mut body: Query<&mut Transform, (With<Body>, Without<Limb>)>,
    bodies: Query<Entity, With<Body>>,
    mut limbs: Query<(&Limb, &mut Transform), Without<Body>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mats: Res<ChunkMats>,
    mut phase: Local<f32>,
) {
    if !view.third {
        for e in &bodies { commands.entity(e).despawn(); }
        return;
    }
    let Ok(mut t) = body.single_mut() else {
        let img = image::open(path()).ok().map(|i| i.to_rgba8()).filter(|i| i.width() == 64).unwrap_or_else(default_skin);
        let tall = img.height() >= 64;
        // (pivot, min rel. to pivot, size, uv, swing)
        let parts: [([f32; 3], [f32; 3], [u32; 3], (u32, u32), f32); 6] = [
            ([0., 24., 0.], [-4., 0., -4.], [8, 8, 8], (0, 0), 0.0),
            ([0., 12., 0.], [-4., 0., -2.], [8, 12, 4], (16, 16), 0.0),
            ([2., 12., 0.], [-2., -12., -2.], [4, 12, 4], (0, 16), 1.0),
            ([-2., 12., 0.], [-2., -12., -2.], [4, 12, 4], if tall { (16, 48) } else { (0, 16) }, -1.0),
            ([6., 22., 0.], [-2., -10., -2.], [4, 12, 4], (40, 16), -1.0),
            ([-6., 22., 0.], [-2., -10., -2.], [4, 12, 4], if tall { (32, 48) } else { (40, 16) }, 1.0),
        ];
        let root = commands.spawn((Body, Transform::from_translation(p.pos).with_scale(Vec3::splat(0.9)), Visibility::default())).id();
        for (pivot, min, size, uv, swing) in parts {
            let mut m = MeshBuf::default();
            skin_box(&mut m, &img, min, size, uv);
            let e = commands.spawn((Limb(swing), Mesh3d(meshes.add(to_mesh(m))), MeshMaterial3d(mats.opaque.clone()), Transform::from_translation(Vec3::from(pivot) / 16.0))).id();
            commands.entity(root).add_child(e);
        }
        return;
    };
    t.translation = p.pos;
    t.rotation = Quat::from_rotation_y(p.yaw);
    let sp = Vec2::new(p.vel.x, p.vel.z).length();
    *phase += sp * time.delta_secs() * 2.2;
    let a = phase.sin() * (sp / 4.4).min(1.0) * 0.8;
    for (l, mut lt) in &mut limbs { lt.rotation = Quat::from_rotation_x(a * l.0); }
}

fn clear(mut commands: Commands, q: Query<Entity, With<Body>>) {
    for e in &q { commands.entity(e).despawn(); }
}
