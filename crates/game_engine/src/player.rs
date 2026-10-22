//! First-person player (game/player.js, physics.js): walk / sprint / swim / creative fly, step-up,
//! shaped-block collision, fall damage, mining with break times, placing, eating, attacking mobs,
//! opening crafting tables and furnaces. Reads `Controls`, never raw input.
use crate::controls::Controls;
use crate::fell::{is_log, settle, Chopped};
use crate::sky::DayTime;
use crate::hud::Toast;
use crate::mobs::Mob;
use crate::sky::MainCamera;
use crate::survival::{DropItem, Hurt};
use crate::world::VoxelWorld;
use crate::AppState;
use bevy::prelude::*;
use bevy::window::{CursorGrabMode, PrimaryWindow};
use common_server::blocks::{self, id, Kind};
use common_server::dims;
use common_server::items::{reg, Inventory, ItemDef, Stack};
use common_server::mobs::data as mob_data;

pub const EYE: f32 = 1.62;

/// Which screen is over the game (None = playing).
#[derive(Resource, Default, Clone, Copy, PartialEq, Eq, Debug)]
pub enum Ui {
    #[default]
    None,
    Inventory,
    Crafting,
    Furnace(IVec3),
    Chest(IVec3),
    Pause,
    Dead,
}
pub fn ui_closed(ui: Res<Ui>) -> bool { *ui == Ui::None }

#[derive(Resource)]
pub struct Player {
    /// Feet position.
    pub pos: Vec3,
    pub vel: Vec3,
    pub yaw: f32,
    pub pitch: f32,
    pub on_ground: bool,
    pub flying: bool,
    pub creative: bool,
    pub sprinting: bool,
    pub slot: usize,
    pub swing: f32,
    pub inv: Inventory,
    pub health: f32,
    pub hunger: f32,
    /// Block being mined and progress 0..1.
    pub breaking: Option<(IVec3, f32)>,
    band: Option<usize>,
    air_top: f32,
}

impl Default for Player {
    fn default() -> Self {
        Player {
            pos: Vec3::ZERO, vel: Vec3::ZERO, yaw: 0.0, pitch: 0.0, on_ground: false, flying: false, creative: false, sprinting: false,
            slot: 0, swing: 0.0, inv: Inventory::default(), health: 20.0, hunger: 20.0, breaking: None, band: None, air_top: 0.0,
        }
    }
}

impl Player {
    pub fn held(&self) -> Option<Stack> { self.inv.slots[self.slot] }
    pub fn tool(&self) -> Option<&'static ItemDef> { self.held().map(|s| reg().item(s.item)) }
}

pub struct PlayerPlugin;
impl Plugin for PlayerPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Player>()
            .init_resource::<Ui>()
            .add_systems(OnEnter(AppState::Playing), enter)
            .add_systems(OnExit(AppState::Playing), exit)
            .add_systems(Update, (toggle_ui, grab_cursor).chain().run_if(in_state(AppState::Playing)))
            .add_systems(Update, (look, movement, interact, hotbar).chain().after(grab_cursor).run_if(in_state(AppState::Playing).and(ui_closed)))
            .add_systems(Update, (follow_cam, dimension_toast).after(hotbar).run_if(in_state(AppState::Playing)));
    }
}

fn grab(win: &mut Window, on: bool) {
    win.cursor_options.visible = !on;
    win.cursor_options.grab_mode = if !on { CursorGrabMode::None } else if cfg!(target_os = "macos") { CursorGrabMode::Locked } else { CursorGrabMode::Confined };
}

pub fn sync_meta(p: &Player, w: &mut VoxelWorld, full: bool) {
    w.meta.pos = p.pos.to_array();
    w.meta.yaw = p.yaw;
    w.meta.pitch = p.pitch;
    w.meta.flying = p.flying;
    w.focus = p.pos;
    if full {
        w.meta.inv = p.inv.to_text();
        w.meta.health = p.health.max(0.5);
        w.meta.hunger = p.hunger.max(0.5);
    }
}

fn enter(mut p: ResMut<Player>, w: Res<VoxelWorld>, mut ui: ResMut<Ui>) {
    let m = &w.meta;
    *p = Player {
        pos: Vec3::from(m.pos),
        yaw: m.yaw,
        pitch: m.pitch,
        inv: Inventory::from_text(&m.inv),
        health: if m.health > 0.0 { m.health } else { 20.0 },
        hunger: if m.hunger > 0.0 { m.hunger } else { 20.0 },
        band: dims::band_index(m.pos[1].floor() as i32),
        air_top: m.pos[1],
        ..default()
    };
    *ui = Ui::None;
}

fn exit(p: Res<Player>, mut w: ResMut<VoxelWorld>, mut win: Query<&mut Window, With<PrimaryWindow>>) {
    sync_meta(&p, &mut w, true);
    w.save();
    if let Ok(mut win) = win.single_mut() { grab(&mut win, false); }
}

fn toggle_ui(c: Res<Controls>, mut ui: ResMut<Ui>) {
    let next = if c.pause {
        match *ui { Ui::None => Ui::Pause, Ui::Dead => Ui::Dead, _ => Ui::None }
    } else if c.inventory {
        match *ui { Ui::None => Ui::Inventory, Ui::Inventory | Ui::Crafting | Ui::Furnace(_) | Ui::Chest(_) => Ui::None, u => u }
    } else {
        *ui
    };
    ui.set_if_neq(next);
}

fn grab_cursor(ui: Res<Ui>, mut win: Query<&mut Window, With<PrimaryWindow>>) {
    if ui.is_changed() {
        if let Ok(mut w) = win.single_mut() { grab(&mut w, *ui == Ui::None); }
    }
}

fn look(c: Res<Controls>, mut p: ResMut<Player>) {
    p.yaw += c.look.x;
    p.pitch = (p.pitch + c.look.y).clamp(-1.54, 1.54);
}

/// Player AABB vs solid blocks (partial shapes use their box).
pub fn collides(w: &VoxelWorld, pos: Vec3) -> bool { box_hits(w, pos, 0.3, 1.8) }

pub fn box_hits(w: &VoxelWorld, pos: Vec3, hw: f32, h: f32) -> bool {
    let (a, b) = (pos + Vec3::new(-hw, 0.0, -hw), pos + Vec3::new(hw, h, hw));
    for y in a.y.floor() as i32..=(b.y - 0.001).floor() as i32 {
        for z in a.z.floor() as i32..=(b.z - 0.001).floor() as i32 {
            for x in a.x.floor() as i32..=(b.x - 0.001).floor() as i32 {
                let bl = w.get(IVec3::new(x, y, z));
                if !blocks::is_solid(bl) { continue; }
                let s = blocks::shape_box(bl);
                let o = Vec3::new(x as f32, y as f32, z as f32);
                if a.cmplt(o + Vec3::new(s[3], s[4], s[5])).all() && b.cmpgt(o + Vec3::new(s[0], s[1], s[2])).all() { return true; }
            }
        }
    }
    false
}

fn movement(time: Res<Time>, c: Res<Controls>, mut p: ResMut<Player>, mut w: ResMut<VoxelWorld>, mut hurt: EventWriter<Hurt>, mut toast: ResMut<Toast>, mut t: Local<f32>) {
    let dt = time.delta_secs().min(0.05);
    if c.creative {
        p.creative = !p.creative;
        p.flying = p.flying && p.creative;
        toast.show(if p.creative { "Creative mode".into() } else { "Survival mode".into() });
    }
    if c.fly && p.creative {
        p.flying = !p.flying;
        p.vel = Vec3::ZERO;
    }
    let feet = p.pos.floor().as_ivec3();
    if !w.is_loaded(feet) { return; }
    if collides(&w, p.pos) { p.pos.y += 1.0; }
    let fwd = Vec3::new(-p.yaw.sin(), 0.0, -p.yaw.cos());
    let right = Vec3::new(p.yaw.cos(), 0.0, -p.yaw.sin());
    let wish = (fwd * c.mv.y + right * c.mv.x).clamp_length_max(1.0);
    p.sprinting = c.sprint && c.mv.y > 0.5 && (p.hunger > 6.0 || p.creative);
    let in_liquid = blocks::kind(w.get(feet)) == Kind::Liquid || blocks::kind(w.get(feet + IVec3::Y)) == Kind::Liquid;
    if p.flying {
        let sp = if p.sprinting { 30.0 } else { 12.0 };
        let mut v = wish * sp;
        if c.jump { v.y += sp; }
        if c.down { v.y -= sp; }
        p.vel = v;
    } else {
        let sp = if in_liquid { 3.0 } else if p.sprinting { 6.4 } else if c.down { 1.6 } else { 4.4 };
        p.vel.x = wish.x * sp;
        p.vel.z = wish.z * sp;
        if in_liquid {
            p.vel.y = (p.vel.y - 9.0 * dt).max(-3.0);
            if c.jump { p.vel.y = 3.5; }
        } else {
            p.vel.y = (p.vel.y - 28.0 * dt).max(-50.0);
            if c.jump && p.on_ground { p.vel.y = 8.6; }
        }
    }
    let delta = p.vel * dt;
    let steps = (delta.abs().max_element() / 0.25).ceil().max(1.0) as i32;
    let sd = delta / steps as f32;
    let was_ground = p.on_ground;
    p.on_ground = false;
    for _ in 0..steps {
        for axis in [1, 0, 2] {
            let mut np = p.pos;
            np[axis] += sd[axis];
            if !collides(&w, np) {
                p.pos = np;
                continue;
            }
            if axis == 1 {
                if sd.y < 0.0 { p.on_ground = true; }
                p.vel.y = 0.0;
            } else if (was_ground || p.on_ground) && !p.flying {
                let cur = p.pos;
                if let Some(h) = [0.0625, 0.125, 0.25, 0.5, 0.6].into_iter().find(|h| !collides(&w, np + Vec3::Y * *h) && !collides(&w, cur + Vec3::Y * *h)) {
                    p.pos = np + Vec3::Y * h;
                }
            }
        }
    }
    if p.flying || in_liquid {
        p.air_top = p.pos.y;
    } else if p.on_ground {
        let fall = p.air_top - p.pos.y;
        if fall > 3.5 { hurt.write(Hurt(fall - 3.0)); }
        p.air_top = p.pos.y;
    } else {
        p.air_top = p.air_top.max(p.pos.y);
    }
    if p.pos.y < (dims::WORLD_MIN_Y - 16) as f32 { hurt.write(Hurt(4.0)); }
    *t += dt;
    let full = *t > 2.0;
    if full { *t = 0.0; }
    sync_meta(&p, &mut w, full);
}

/// DDA voxel raycast → (hit block, empty block in front of it). Liquids are ignored.
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

fn ray_box(o: Vec3, d: Vec3, mn: Vec3, mx: Vec3) -> Option<f32> {
    let inv = d.recip();
    let (t1, t2) = ((mn - o) * inv, (mx - o) * inv);
    let (near, far) = (t1.min(t2).max_element(), t1.max(t2).min_element());
    (far >= near.max(0.0)).then_some(near.max(0.0))
}

fn eat(p: &mut Player) -> bool {
    let Some(st) = p.held() else { return false };
    let d = reg().item(st.item);
    if d.food <= 0.0 || p.hunger >= 20.0 { return false; }
    p.hunger = (p.hunger + d.food * 1.2).min(20.0);
    p.health = (p.health + d.food * 0.3).min(20.0);
    let s = p.slot;
    p.inv.take_one(s);
    p.swing = 1.0;
    true
}

#[allow(clippy::too_many_arguments)]
fn interact(
    time: Res<Time>,
    c: Res<Controls>,
    cam: Query<&GlobalTransform, With<MainCamera>>,
    mut p: ResMut<Player>,
    mut w: ResMut<VoxelWorld>,
    mut gizmos: Gizmos,
    mut mobs: Query<(Entity, &Transform, &mut Mob)>,
    mut drops: EventWriter<DropItem>,
    mut ui: ResMut<Ui>,
    mut chopped: EventWriter<Chopped>,
    mut day: ResMut<DayTime>,
    mut toast: ResMut<Toast>,
) {
    let Ok(cam) = cam.single() else { return };
    let (o, dir) = (cam.translation(), cam.forward().as_vec3());
    let hit = raycast(&w, o, dir, 5.0);
    if c.attack_pressed {
        p.swing = 1.0;
        let reach = hit.map(|(h, _)| (h.as_vec3() + Vec3::splat(0.5) - o).length()).unwrap_or(5.0).min(3.5);
        let mut best: Option<(f32, Entity)> = None;
        for (e, t, m) in &mobs {
            let ty = &mob_data().types[m.kind];
            let hw = ty.w.clamp(0.2, 0.6);
            if let Some(d) = ray_box(o, dir, t.translation - Vec3::new(hw, 0.0, hw), t.translation + Vec3::new(hw, ty.h, hw)) {
                if d <= reach && best.is_none_or(|b| d < b.0) { best = Some((d, e)); }
            }
        }
        if let Some((_, e)) = best {
            if let Ok((_, _, mut m)) = mobs.get_mut(e) {
                m.hp -= p.tool().map(|t| t.damage.max(1.0)).unwrap_or(1.0);
                m.hurt = 0.35;
                m.angry = true;
                m.vel += Vec3::new(dir.x, 0.0, dir.z).normalize_or_zero() * 6.0 + Vec3::Y * 4.0;
            }
            return;
        }
    }
    let Some((hit, prev)) = hit else {
        p.breaking = None;
        if c.use_pressed { eat(&mut p); }
        return;
    };
    gizmos.cuboid(Transform::from_translation(hit.as_vec3() + Vec3::splat(0.5)).with_scale(Vec3::splat(1.004)), Color::srgba(0.05, 0.05, 0.05, 0.8));
    if c.attack {
        let b = w.get(hit);
        let mut broke = false;
        if p.creative {
            broke = c.attack_pressed;
        } else {
            let need = reg().break_time(b, p.tool()).max(0.05);
            let prog = match p.breaking { Some((h, v)) if h == hit => v, _ => 0.0 } + time.delta_secs() / need;
            if prog >= 1.0 {
                broke = true;
                if let Some(item) = reg().drop_of(b, p.tool()) {
                    drops.write(DropItem { pos: hit.as_vec3() + Vec3::splat(0.5), stack: Stack { item, count: 1 } });
                }
                p.breaking = None;
            } else {
                p.breaking = Some((hit, prog));
            }
            if p.swing <= 0.0 { p.swing = 1.0; }
        }
        if broke {
            w.set(hit, id::AIR);
            if is_log(b) { chopped.write(Chopped { at: hit, dir }); }
            settle(&mut w, hit);
        }
    } else {
        p.breaking = None;
    }
    if c.use_pressed {
        let b = w.get(hit);
        if b == id::CHEST {
            *ui = Ui::Chest(hit);
            return;
        }
        if let Some(t) = blocks::toggled(b) {
            w.set(hit, t);
            p.swing = 1.0;
            return;
        }
        let bk = blocks::def(b).key;
        if bk == "bed" || bk.ends_with("_bed") {
            w.meta.spawn = [hit.x as f32 + 0.5, hit.y as f32 + 1.0, hit.z as f32 + 0.5];
            if day.0 > 0.5 {
                day.0 = 0.0;
                toast.show("Good morning! Spawn point set.".into());
            } else {
                toast.show("Spawn point set. You can only sleep at night.".into());
            }
            return;
        }
        if b == id::CRAFTING_TABLE {
            *ui = Ui::Crafting;
            return;
        }
        if b == id::FURNACE {
            *ui = Ui::Furnace(hit);
            return;
        }
        if eat(&mut p) { return; }
        let Some(st) = p.held() else { return };
        let Some(pb) = reg().item(st.item).block else { return };
        let there = w.get(prev);
        if there != id::AIR && !matches!(blocks::kind(there), Kind::Liquid | Kind::Plant) { return; }
        let (pmin, pmax) = (p.pos + Vec3::new(-0.3, 0.0, -0.3), p.pos + Vec3::new(0.3, 1.8, 0.3));
        let (bmin, bmax) = (prev.as_vec3(), prev.as_vec3() + Vec3::ONE);
        let overlap = pmin.cmplt(bmax).all() && pmax.cmpgt(bmin).all();
        if !overlap || !blocks::is_solid(pb) {
            w.set(prev, pb);
            settle(&mut w, prev);
            p.swing = 1.0;
            if !p.creative {
                let s = p.slot;
                p.inv.take_one(s);
            }
        }
    }
    if c.pick && p.creative {
        if let Some(item) = reg().of_block(w.get(hit)) {
            let s = p.slot;
            p.inv.slots[s] = Some(Stack { item, count: reg().item(item).max_stack });
        }
    }
}

fn hotbar(c: Res<Controls>, mut p: ResMut<Player>) {
    if let Some(s) = c.slot_set { p.slot = s; }
    if c.slot_delta != 0 { p.slot = (p.slot as i32 + c.slot_delta).rem_euclid(9) as usize; }
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
