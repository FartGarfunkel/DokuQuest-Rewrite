//! Automated crash test, run by CI with DQ_SMOKE=1 (software Vulkan, no GPU): new world, walk, jump,
//! mine, place, inventory, crafting table, furnace, chest, pause, third person, creative, death
//! screen, save & quit to title, exit. Any panic fails the job; success logs "SMOKE OK".
use crate::controls::Controls;
use crate::player::{Player, Ui};
use crate::skin::View;
use crate::world::VoxelWorld;
use crate::AppState;
use bevy::prelude::*;
use common_server::items::{reg, Stack};
use common_server::save::{self, WorldMeta};
use common_server::worldgen::{Gen, GEN_VERSION};

pub struct SmokePlugin;
impl Plugin for SmokePlugin {
    fn build(&self, app: &mut App) {
        if std::env::var("DQ_SMOKE").is_ok() { app.add_systems(PreUpdate, drive.after(crate::controls::gather)); }
    }
}

#[allow(clippy::too_many_arguments)]
fn drive(
    time: Res<Time>,
    mut t: Local<f32>,
    mut done: Local<u32>,
    mut next: ResMut<NextState<AppState>>,
    mut w: ResMut<VoxelWorld>,
    mut c: ResMut<Controls>,
    mut ui: ResMut<Ui>,
    mut p: ResMut<Player>,
    mut view: ResMut<View>,
    mut exit: EventWriter<AppExit>,
) {
    *t += time.delta_secs().clamp(0.0, 0.1);
    let s = *t;
    let mut at = |k: u32, when: f32| if *done < k && s >= when { *done = k; info!("SMOKE step {k} at {s:.1}s"); true } else { false };
    if at(1, 3.0) {
        let seed = 12345;
        let meta = WorldMeta { name: "Smoke Test".into(), seed, gen_version: GEN_VERSION, pos: Gen::new(seed).spawn_point(), ..default() };
        let _ = save::save_meta(&meta);
        w.open(meta, true);
        next.set(AppState::Playing);
    }
    if (6.0..12.0).contains(&s) { c.mv = Vec2::Y; c.jump = (s * 2.0) as i32 % 3 == 0; c.look = Vec2::new(0.01, 0.0); }
    if at(2, 12.0) {
        for (k, n) in [("planks", 64), ("log", 16), ("chest", 2), ("torch", 16), ("cobblestone", 32), ("bread", 5), ("iron_sword", 1)] {
            if let Some(item) = reg().by_key(k) { let _ = p.inv.add(Stack { item, count: n }); }
        }
    }
    if at(3, 12.5) { *ui = Ui::Inventory; }
    if at(4, 14.0) { *ui = Ui::None; p.pitch = -0.9; p.slot = 0; }
    if (14.5..17.0).contains(&s) { c.attack = true; c.attack_pressed = s < 14.6; }
    if at(5, 17.5) { c.use_pressed = true; }
    let ahead = p.pos.floor().as_ivec3() + IVec3::new(1, 0, 0);
    if at(6, 18.0) { *ui = Ui::Crafting; }
    if at(7, 19.0) { *ui = Ui::Furnace(ahead); }
    if at(8, 20.0) { *ui = Ui::Chest(ahead); }
    if at(9, 21.0) { *ui = Ui::Pause; }
    if at(10, 22.0) { *ui = Ui::None; view.third = true; c.mv = Vec2::Y; }
    if at(11, 24.0) { view.third = false; c.creative = true; }
    if at(12, 25.0) { c.fly = true; }
    if (25.5..27.0).contains(&s) { c.mv = Vec2::new(0.5, 1.0); c.jump = true; }
    if at(13, 27.5) { p.health = 0.0; p.creative = false; }
    if at(14, 29.0) { *ui = Ui::None; p.health = 20.0; }
    if at(15, 30.0) { next.set(AppState::Menu); }
    if at(16, 33.0) {
        info!("SMOKE OK");
        println!("SMOKE OK");
        exit.write(AppExit::Success);
    }
}
