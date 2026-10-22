//! First-person player: walk / fly / swim, AABB collision, break & place, hotbar, and a toast when you
//! cross into another stacked dimension.
use crate::hud::Toast;
use crate::sky::MainCamera;
use crate::world::VoxelWorld;
use crate::AppState;
use bevy::input::mouse::{AccumulatedMouseMotion, AccumulatedMouseScroll};
use bevy::prelude::*;
use bevy::window::{CursorGrabMode, PrimaryWindow};
use common_server::blocks::{self, id, Kind};
use common_server::dims;

/// Slot 9 (AIR) holds the voxel sword.
pub const HOTBAR: [u16; 9] = [id::GRASS, id::DIRT, id::STONE, id::COBBLESTONE, id::PLANKS, id::LOG, id::BRICKS, id::GLOWSTONE, id::AIR];
pub const EYE: f32 = 1.62;

#[derive(Resource, Default)]
pub struct Player {
    /// Feet position.
    pub pos: Vec3,
    pub vel: Vec3,
    pub yaw: f32,
    pub pitch: f32,
    pub on_ground: bool,
    pub flying: bool,
    pub slot: usize,
    /// 1 → 0 swing animation.
    pub swing: f32,
    band: Option<usize>,
}

pub struct PlayerPlugin;
impl Plugin for PlayerPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Player>()
            .add_systems(OnEnter(AppState::Playing), enter)
            .add_systems(OnExit(AppState::Playing), exit)
            .add_systems(Update, (look, movement, interact, hotbar, follow_cam, dimension_toast).chain().run_if(in_state(AppState::Playing)));
    }
}

fn grab(win: &mut Window, on: bool) {
    win.cursor_options.visible = !on;
    win.cursor_options.grab_mode = if !on {
        CursorGrabMode::None
    } else if cfg!(target_os = "macos") {
        CursorGrabMode::Locked
    } else {
        CursorGrabMode::Confined
    };
}

fn sync_meta(p: &Player, w: &mut VoxelWorld) {
    w.meta.pos = p.pos.to_array();
    w.meta.yaw = p.yaw;
    w.meta.pitch = p.pitch;
    w.meta.flying = p.flying;
    w.focus = p.pos;
}

fn enter(mut p: ResMut<Player>, w: Res<VoxelWorld>, mut win: Query<&mut Window, With<PrimaryWindow>>) {
    let m = &w.meta;
    *p = Player {
        pos: Vec3::from(m.pos),
        yaw: m.yaw,
        pitch: m.pitch,
        flying: m.flying,
        slot: p.slot,
        band: dims::band_index(m.pos[1].floor() as i32),
        ..default()
    };
    if let Ok(mut win) = win.single_mut() { grab(&mut win, true); }
}

fn exit(p: Res<Player>, mut w: ResMut<VoxelWorld>, mut win: Query<&mut Window, With<PrimaryWindow>>) {
    sync_meta(&p, &mut w);
    w.save();
    if let Ok(mut win) = win.single_mut() { grab(&mut win, false); }
}

fn look(mm: Res<AccumulatedMouseMotion>, mut p: ResMut<Player>) {
    let d = mm.delta * 0.0022;
    p.yaw -= d.x;
    p.pitch = (p.pitch - d.y).clamp(-1.54, 1.54);
}

fn collides(w: &VoxelWorld, pos: Vec3) -> bool {
    let (a, b) = (pos + Vec3::new(-0.3, 0.0, -0.3), pos + Vec3::new(0.3, 1.8, 0.3));
    for y in a.y.floor() as i32..=(b.y - 0.001).floor() as i32 {
        for z in a.z.floor() as i32..=(b.z - 0.001).floor() as i32 {
            for x in a.x.floor() as i32..=(b.x - 0.001).floor() as i32 {
                if blocks::is_solid(w.get(IVec3::new(x, y, z))) { return true; }
            }
        }
    }
    false
}

fn movement(time: Res<Time>, keys: Res<ButtonInput<KeyCode>>, mut p: ResMut<Player>, mut w: ResMut<VoxelWorld>, mut next: ResMut<NextState<AppState>>) {
    if keys.just_pressed(KeyCode::Escape) {
        next.set(AppState::Menu);
        return;
    }
    let dt = time.delta_secs().min(0.05);
    if keys.just_pressed(KeyCode::KeyF) {
        p.flying = !p.flying;
        p.vel = Vec3::ZERO;
    }
    let feet = p.pos.floor().as_ivec3();
    if !w.is_loaded(feet) { return; } // wait for terrain under us
    if collides(&w, p.pos) { p.pos.y += 1.0; } // spawned inside terrain → pop up
    let fwd = Vec3::new(-p.yaw.sin(), 0.0, -p.yaw.cos());
    let right = Vec3::new(p.yaw.cos(), 0.0, -p.yaw.sin());
    let mut wish = Vec3::ZERO;
    if keys.pressed(KeyCode::KeyW) { wish += fwd; }
    if keys.pressed(KeyCode::KeyS) { wish -= fwd; }
    if keys.pressed(KeyCode::KeyD) { wish += right; }
    if keys.pressed(KeyCode::KeyA) { wish -= right; }
    let wish = wish.normalize_or_zero();
    let sprint = keys.pressed(KeyCode::ControlLeft);
    let in_liquid = blocks::kind(w.get(feet)) == Kind::Liquid || blocks::kind(w.get(feet + IVec3::Y)) == Kind::Liquid;
    if p.flying {
        let sp = if sprint { 30.0 } else { 12.0 };
        let mut v = wish * sp;
        if keys.pressed(KeyCode::Space) { v.y += sp; }
        if keys.pressed(KeyCode::ShiftLeft) { v.y -= sp; }
        p.vel = v;
    } else {
        let sp = if in_liquid { 3.0 } else if sprint { 7.5 } else { 4.6 };
        p.vel.x = wish.x * sp;
        p.vel.z = wish.z * sp;
        if in_liquid {
            p.vel.y = (p.vel.y - 9.0 * dt).max(-3.0);
            if keys.pressed(KeyCode::Space) { p.vel.y = 3.5; }
        } else {
            p.vel.y = (p.vel.y - 28.0 * dt).max(-50.0);
            if keys.pressed(KeyCode::Space) && p.on_ground { p.vel.y = 8.6; }
        }
    }
    let delta = p.vel * dt;
    let steps = (delta.abs().max_element() / 0.25).ceil().max(1.0) as i32;
    let sd = delta / steps as f32;
    p.on_ground = false;
    for _ in 0..steps {
        for axis in 0..3 {
            let mut np = p.pos;
            np[axis] += sd[axis];
            if collides(&w, np) {
                if axis == 1 {
                    if sd.y < 0.0 { p.on_ground = true; }
                    p.vel.y = 0.0;
                }
            } else {
                p.pos = np;
            }
        }
    }
    sync_meta(&p, &mut w);
}

/// DDA voxel raycast → (hit block, empty block in front of it).
pub fn raycast(w: &VoxelWorld, o: Vec3, d: Vec3, max: f32) -> Option<(IVec3, IVec3)> {
    let mut p = o.floor().as_ivec3();
    let step = IVec3::new(if d.x > 0.0 { 1 } else { -1 }, if d.y > 0.0 { 1 } else { -1 }, if d.z > 0.0 { 1 } else { -1 });
    let inv = Vec3::new(1.0 / d.x.abs(), 1.0 / d.y.abs(), 1.0 / d.z.abs());
    let first = |o: f32, p: i32, d: f32| if d > 0.0 { (p as f32 + 1.0 - o) / d } else if d < 0.0 { (o - p as f32) / -d } else { f32::INFINITY };
    let mut tm = Vec3::new(first(o.x, p.x, d.x), first(o.y, p.y, d.y), first(o.z, p.z, d.z));
    let (mut prev, mut t) = (p, 0.0);
    while t <= max {
        let b = w.get(p);
        if b != id::AIR && blocks::kind(b) != Kind::Liquid { return Some((p, prev)); }
        prev = p;
        if tm.x < tm.y && tm.x < tm.z {
            p.x += step.x;
            t = tm.x;
            tm.x += inv.x;
        } else if tm.y < tm.z {
            p.y += step.y;
            t = tm.y;
            tm.y += inv.y;
        } else {
            p.z += step.z;
            t = tm.z;
            tm.z += inv.z;
        }
    }
    None
}

fn interact(mouse: Res<ButtonInput<MouseButton>>, cam: Query<&GlobalTransform, With<MainCamera>>, mut p: ResMut<Player>, mut w: ResMut<VoxelWorld>, mut gizmos: Gizmos) {
    let Ok(cam) = cam.single() else { return };
    let Some((hit, prev)) = raycast(&w, cam.translation(), cam.forward().as_vec3(), 6.0) else {
        if mouse.just_pressed(MouseButton::Left) { p.swing = 1.0; }
        return;
    };
    gizmos.cuboid(Transform::from_translation(hit.as_vec3() + Vec3::splat(0.5)).with_scale(Vec3::splat(1.004)), Color::srgba(0.05, 0.05, 0.05, 0.8));
    if mouse.just_pressed(MouseButton::Left) {
        w.set(hit, id::AIR);
        p.swing = 1.0;
    } else if mouse.just_pressed(MouseButton::Right) {
        let b = HOTBAR[p.slot];
        p.swing = 1.0;
        if b == id::AIR { return; }
        let (pmin, pmax) = (p.pos + Vec3::new(-0.3, 0.0, -0.3), p.pos + Vec3::new(0.3, 1.8, 0.3));
        let (bmin, bmax) = (prev.as_vec3(), prev.as_vec3() + Vec3::ONE);
        let overlap = pmin.cmplt(bmax).all() && pmax.cmpgt(bmin).all();
        if !overlap || !blocks::is_solid(b) { w.set(prev, b); }
    }
}

fn hotbar(keys: Res<ButtonInput<KeyCode>>, scroll: Res<AccumulatedMouseScroll>, mut p: ResMut<Player>) {
    const KEYS: [KeyCode; 9] = [
        KeyCode::Digit1, KeyCode::Digit2, KeyCode::Digit3, KeyCode::Digit4, KeyCode::Digit5,
        KeyCode::Digit6, KeyCode::Digit7, KeyCode::Digit8, KeyCode::Digit9,
    ];
    for (i, k) in KEYS.iter().enumerate() {
        if keys.just_pressed(*k) { p.slot = i; }
    }
    if scroll.delta.y > 0.0 {
        p.slot = (p.slot + 8) % 9;
    } else if scroll.delta.y < 0.0 {
        p.slot = (p.slot + 1) % 9;
    }
}

fn follow_cam(p: Res<Player>, mut cam: Query<&mut Transform, With<MainCamera>>) {
    if let Ok(mut t) = cam.single_mut() {
        t.translation = p.pos + Vec3::Y * EYE;
        t.rotation = Quat::from_euler(EulerRot::YXZ, p.yaw, p.pitch, 0.0);
    }
}

fn dimension_toast(mut p: ResMut<Player>, mut toast: ResMut<Toast>) {
    let b = dims::band_index(p.pos.y.floor() as i32);
    if b != p.band {
        if let Some(i) = b { toast.show(format!("Entering {}", dims::BANDS[i].name)); }
        p.band = b;
    }
}
