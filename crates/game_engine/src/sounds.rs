//! The game's own synthesized sounds (game/sfxlib.js SND_MAT + SND_FX), rendered from the JS synth by
//! tools/port/sounds.py and embedded. Send `Sfx::block(b, "break")` / `Sfx::fx("hurt")`; footsteps
//! are automatic. Unknown block materials fall back to stone.
use crate::menu::Settings;
use crate::player::Player;
use crate::world::VoxelWorld;
use crate::AppState;
use bevy::audio::{AudioPlayer, AudioSource, PlaybackSettings, Volume};
use bevy::prelude::*;
use common_server::blocks;
use std::collections::HashMap;

static BIN: &[u8] = include_bytes!("../data/sounds.bin");
static IDX: &str = include_str!("../data/sounds.tsv");

#[derive(Event)]
pub struct Sfx(pub String);
impl Sfx {
    pub fn block(b: u16, action: &str) -> Sfx { Sfx(format!("{}.{action}", blocks::def(b).sound)) }
    pub fn fx(k: &str) -> Sfx { Sfx(format!("fx.{k}")) }
}

#[derive(Resource, Default)]
struct Bank(HashMap<String, Vec<(Handle<AudioSource>, f32)>>);

pub struct SoundsPlugin;
impl Plugin for SoundsPlugin {
    fn build(&self, app: &mut App) {
        app.add_event::<Sfx>()
            .init_resource::<Bank>()
            .add_systems(Startup, load)
            .add_systems(Update, (footsteps.run_if(in_state(AppState::Playing)), play).chain());
    }
}

fn load(mut bank: ResMut<Bank>, mut assets: ResMut<Assets<AudioSource>>) {
    for l in IDX.lines() {
        let c: Vec<&str> = l.split('\t').collect();
        if c.len() < 4 { continue; }
        let (Ok(o), Ok(n)) = (c[1].parse::<usize>(), c[2].parse::<usize>()) else { continue };
        if o + n > BIN.len() { continue; }
        let h = assets.add(AudioSource { bytes: BIN[o..o + n].into() });
        bank.0.entry(c[0].to_string()).or_default().push((h, c[3].parse().unwrap_or(0.6)));
    }
}

fn play(mut ev: EventReader<Sfx>, bank: Res<Bank>, settings: Res<Settings>, mut commands: Commands, mut n: Local<usize>) {
    for e in ev.read() {
        let list = bank.0.get(&e.0).or_else(|| e.0.split_once('.').and_then(|(_, a)| bank.0.get(&format!("stone.{a}"))));
        let Some(list) = list.filter(|l| !l.is_empty()) else { continue };
        *n += 1;
        let (h, v) = &list[*n % list.len()];
        commands.spawn((AudioPlayer(h.clone()), PlaybackSettings::DESPAWN.with_volume(Volume::Linear(v * settings.volume))));
    }
}

fn footsteps(time: Res<Time>, p: Res<Player>, w: Res<VoxelWorld>, mut t: Local<f32>, mut sfx: EventWriter<Sfx>) {
    let sp = Vec2::new(p.vel.x, p.vel.z).length();
    if !p.on_ground || p.flying || sp < 1.0 {
        *t = 0.25;
        return;
    }
    *t -= time.delta_secs() * sp / 4.4;
    if *t <= 0.0 {
        *t = 0.45;
        let b = w.get((p.pos - Vec3::Y * 0.1).floor().as_ivec3());
        if b != 0 { sfx.write(Sfx::block(b, "step")); }
    }
}
