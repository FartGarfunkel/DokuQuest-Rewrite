//! Chunk streaming around `VoxelWorld::focus`: async generation + micro-voxel meshing on worker
//! threads, LOD rings, block edits, saving. Runs in every state (the title panorama is the real world).
use crate::AppState;
use bevy::pbr::NotShadowCaster;
use bevy::prelude::*;
use bevy::render::mesh::{Indices, PrimitiveTopology};
use bevy::tasks::AsyncComputeTaskPool;
use common_server::chunk::{Chunk, CS};
use common_server::mesh::{self as vm, MeshBuf};
use common_server::save::{self, WorldMeta};
use common_server::worldgen::{Gen, GEN_VERSION};
use common_server::dims;
use std::collections::{HashMap, HashSet};
use std::sync::{mpsc, Arc, Mutex};

/// Horizontal view distance in chunks (16 blocks each).
pub const RENDER_DIST: i32 = 8;
/// Vertical chunk layers kept above/below the player (stacked dimensions are only loaded nearby).
pub const VERT_DIST: i32 = 3;
const MAX_GEN: usize = 16;
const MAX_MESH: usize = 6;
const CY_MIN: i32 = dims::WORLD_MIN_Y / CS;
const CY_MAX: i32 = dims::WORLD_MAX_Y / CS - 1;

/// Micro-voxels per block edge by chunk distance: 16³ detail where you stand, single colour far away.
pub fn lod_res(d: i32) -> u32 {
    match d { 0 => 16, 1 => 8, 2 => 4, _ => 1 }
}

enum Job {
    Gen(u32, IVec3, Chunk),
    Mesh(u32, IVec3, u32, MeshBuf, MeshBuf),
}

#[derive(Resource)]
pub struct VoxelWorld {
    pub meta: WorldMeta,
    /// false for the demo panorama world (never written to disk).
    pub persistent: bool,
    /// Streaming centre (player or panorama camera).
    pub focus: Vec3,
    generator: Arc<Gen>,
    chunks: HashMap<IVec3, Arc<Chunk>>,
    epoch: u32,
    pending: HashSet<IVec3>,
    meshing: HashSet<IVec3>,
    dirty: HashSet<IVec3>,
    edited: HashSet<IVec3>,
    views: HashMap<IVec3, (u32, Vec<Entity>)>,
    /// Chunk entities to despawn; only `receive` despawns, so spawn/despawn never race.
    trash: Vec<Entity>,
    tx: mpsc::Sender<Job>,
    rx: Mutex<mpsc::Receiver<Job>>,
}

fn neigh() -> impl Iterator<Item = IVec3> {
    (-1..=1).flat_map(|z| (-1..=1).flat_map(move |y| (-1..=1).map(move |x| IVec3::new(x, y, z))))
}

impl VoxelWorld {
    pub fn new(meta: WorldMeta, persistent: bool) -> Self {
        let (tx, rx) = mpsc::channel();
        Self {
            generator: Arc::new(Gen::new(meta.seed)),
            focus: Vec3::from(meta.pos),
            meta,
            persistent,
            chunks: default(),
            epoch: 0,
            pending: default(),
            meshing: default(),
            dirty: default(),
            edited: default(),
            views: default(),
            trash: Vec::new(),
            tx,
            rx: Mutex::new(rx),
        }
    }

    pub fn get(&self, p: IVec3) -> u16 {
        match self.chunks.get(&p.div_euclid(IVec3::splat(CS))) {
            Some(ch) => {
                let l = p.rem_euclid(IVec3::splat(CS));
                ch.get(l.x, l.y, l.z)
            }
            None => 0,
        }
    }

    pub fn is_loaded(&self, p: IVec3) -> bool { self.chunks.contains_key(&p.div_euclid(IVec3::splat(CS))) }

    pub fn set(&mut self, p: IVec3, b: u16) {
        let (c, l) = (p.div_euclid(IVec3::splat(CS)), p.rem_euclid(IVec3::splat(CS)));
        let Some(ch) = self.chunks.get_mut(&c) else { return };
        Arc::make_mut(ch).set(l.x, l.y, l.z, b);
        self.edited.insert(c);
        let touches = |k: i32, lv: i32| k == 0 || (k < 0 && lv == 0) || (k > 0 && lv == CS - 1);
        for d in neigh() {
            if touches(d.x, l.x) && touches(d.y, l.y) && touches(d.z, l.z) { self.dirty.insert(c + d); }
        }
    }

    /// Writes metadata + edited chunks (no-op for the demo world).
    pub fn save(&mut self) {
        if !self.persistent { return; }
        self.meta.last_played = save::now();
        if let Err(e) = save::save_meta(&self.meta) { warn!("save failed: {e}"); }
        save::set_last_played(&self.meta.name);
        for c in self.edited.drain() {
            if let Some(ch) = self.chunks.get(&c) { let _ = save::save_chunk(&self.meta.name, [c.x, c.y, c.z], ch); }
        }
    }

    /// Switches to another world (old chunk meshes are despawned, in-flight jobs ignored).
    pub fn open(&mut self, meta: WorldMeta, persistent: bool) {
        let old: Vec<Entity> = self.views.drain().flat_map(|(_, (_, es))| es).collect();
        self.trash.extend(old);
        self.chunks.clear();
        self.pending.clear();
        self.meshing.clear();
        self.dirty.clear();
        self.edited.clear();
        self.epoch += 1;
        self.generator = Arc::new(Gen::new(meta.seed));
        self.focus = Vec3::from(meta.pos);
        self.meta = meta;
        self.persistent = persistent;
    }

    /// 18³ block array (chunk + border) for the mesher; None while a neighbour is still generating.
    fn padded(&self, k: IVec3) -> Option<Vec<u16>> {
        let mut nb: [Option<&Chunk>; 27] = [None; 27];
        for (i, d) in neigh().enumerate() {
            let q = k + d;
            if q.y < CY_MIN || q.y > CY_MAX { continue; }
            nb[i] = Some(self.chunks.get(&q)?.as_ref());
        }
        let split = |v: i32| if v < 0 { (0usize, v + CS) } else if v >= CS { (2, v - CS) } else { (1, v) };
        let mut p = vec![0u16; vm::PAD * vm::PAD * vm::PAD];
        for y in -1..=CS {
            for z in -1..=CS {
                for x in -1..=CS {
                    let ((ix, lx), (iy, ly), (iz, lz)) = (split(x), split(y), split(z));
                    if let Some(ch) = nb[ix + iy * 3 + iz * 9] { p[vm::pidx(x, y, z)] = ch.get(lx, ly, lz); }
                }
            }
        }
        Some(p)
    }
}

/// Fixed-seed world shown on the title screen until you have played a world.
pub fn demo_meta() -> WorldMeta {
    let seed = 20261022;
    WorldMeta { name: "Panorama".into(), seed, gen_version: GEN_VERSION, pos: Gen::new(seed).spawn_point(), yaw: 0.6, ..default() }
}

#[derive(Resource, Clone)]
pub struct ChunkMats {
    pub opaque: Handle<StandardMaterial>,
    pub water: Handle<StandardMaterial>,
}

#[derive(Component)]
pub struct ChunkMesh;

pub struct WorldPlugin;
impl Plugin for WorldPlugin {
    fn build(&self, app: &mut App) {
        let (meta, persistent) = match save::last_played() { Some(m) => (m, true), None => (demo_meta(), false) };
        app.insert_resource(VoxelWorld::new(meta, persistent))
            .add_systems(Startup, setup_mats)
            .add_systems(Update, (receive, stream, schedule_mesh).chain())
            .add_systems(Update, autosave.run_if(in_state(AppState::Playing)))
            .add_systems(Last, save_on_exit);
    }
}

fn setup_mats(mut commands: Commands, mut mats: ResMut<Assets<StandardMaterial>>) {
    commands.insert_resource(ChunkMats {
        opaque: mats.add(StandardMaterial { base_color: Color::WHITE, perceptual_roughness: 0.92, reflectance: 0.12, ..default() }),
        water: mats.add(StandardMaterial {
            base_color: Color::srgba(1.0, 1.0, 1.0, 0.75),
            alpha_mode: AlphaMode::Blend,
            perceptual_roughness: 0.08,
            reflectance: 0.6,
            ..default()
        }),
    });
}

pub fn to_mesh(b: MeshBuf) -> Mesh {
    Mesh::new(PrimitiveTopology::TriangleList, Default::default())
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, b.pos)
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, b.nrm)
        .with_inserted_attribute(Mesh::ATTRIBUTE_COLOR, b.col)
        .with_inserted_indices(Indices::U32(b.idx))
}

fn centre(w: &VoxelWorld) -> IVec3 { w.focus.floor().as_ivec3().div_euclid(IVec3::splat(CS)) }
fn hdist(a: IVec3, b: IVec3) -> i32 { (a.x - b.x).abs().max((a.z - b.z).abs()) }

fn receive(mut w: ResMut<VoxelWorld>, mut commands: Commands, mut meshes: ResMut<Assets<Mesh>>, mats: Res<ChunkMats>) {
    let w = &mut *w;
    for e in w.trash.drain(..) { commands.entity(e).despawn(); }
    let jobs: Vec<Job> = w.rx.lock().map(|rx| rx.try_iter().collect()).unwrap_or_default();
    for job in jobs {
        match job {
            Job::Gen(e, k, ch) => {
                if e != w.epoch { continue; }
                w.pending.remove(&k);
                w.chunks.insert(k, Arc::new(ch));
                for d in neigh() { w.dirty.insert(k + d); }
            }
            Job::Mesh(e, k, res, solid, water) => {
                if e != w.epoch { continue; }
                w.meshing.remove(&k);
                if !w.chunks.contains_key(&k) { continue; }
                if let Some((_, old)) = w.views.remove(&k) {
                    for en in old { commands.entity(en).despawn(); }
                }
                let t = Transform::from_translation((k * CS).as_vec3());
                let mut es = Vec::new();
                if !solid.is_empty() {
                    es.push(commands.spawn((Mesh3d(meshes.add(to_mesh(solid))), MeshMaterial3d(mats.opaque.clone()), t, ChunkMesh)).id());
                }
                if !water.is_empty() {
                    es.push(commands.spawn((Mesh3d(meshes.add(to_mesh(water))), MeshMaterial3d(mats.water.clone()), t, ChunkMesh, NotShadowCaster)).id());
                }
                w.views.insert(k, (res, es));
            }
        }
    }
}

fn stream(mut w: ResMut<VoxelWorld>, mut commands: Commands) {
    let w = &mut *w;
    let c = centre(w);
    let far: Vec<IVec3> = w.chunks.keys().filter(|k| hdist(**k, c) > RENDER_DIST + 2 || (k.y - c.y).abs() > VERT_DIST + 2).copied().collect();
    for k in far {
        if w.edited.remove(&k) && w.persistent {
            if let Some(ch) = w.chunks.get(&k) { let _ = save::save_chunk(&w.meta.name, [k.x, k.y, k.z], ch); }
        }
        w.chunks.remove(&k);
        w.dirty.remove(&k);
        if let Some((_, es)) = w.views.remove(&k) {
            for e in es { commands.entity(e).despawn(); }
        }
    }
    if w.pending.len() >= MAX_GEN { return; }
    let r = RENDER_DIST + 1;
    let mut want: Vec<(i32, IVec3)> = Vec::new();
    for cy in (c.y - VERT_DIST - 1).max(CY_MIN)..=(c.y + VERT_DIST + 1).min(CY_MAX) {
        for cz in c.z - r..=c.z + r {
            for cx in c.x - r..=c.x + r {
                let k = IVec3::new(cx, cy, cz);
                if !w.chunks.contains_key(&k) && !w.pending.contains(&k) { want.push((hdist(k, c) * 2 + (cy - c.y).abs(), k)); }
            }
        }
    }
    want.sort_unstable_by_key(|x| x.0);
    let pool = AsyncComputeTaskPool::get();
    let room = MAX_GEN - w.pending.len();
    for (_, k) in want.into_iter().take(room) {
        w.pending.insert(k);
        let (tx, generator, epoch) = (w.tx.clone(), w.generator.clone(), w.epoch);
        let name = w.persistent.then(|| w.meta.name.clone());
        pool.spawn(async move {
            let ch = name.and_then(|n| save::load_chunk(&n, [k.x, k.y, k.z])).unwrap_or_else(|| generator.generate(k.x, k.y, k.z));
            let _ = tx.send(Job::Gen(epoch, k, ch));
        })
        .detach();
    }
}

fn schedule_mesh(mut w: ResMut<VoxelWorld>, mut last: Local<Option<IVec3>>) {
    let w = &mut *w;
    let c = centre(w);
    if *last != Some(c) {
        *last = Some(c);
        for (k, (res, _)) in w.views.iter() {
            if lod_res(hdist(*k, c)) != *res { w.dirty.insert(*k); }
        }
    }
    if w.meshing.len() >= MAX_MESH || w.dirty.is_empty() { return; }
    let mut cands: Vec<(i32, IVec3)> =
        w.dirty.iter().filter(|k| !w.meshing.contains(*k)).map(|k| (hdist(*k, c) * 2 + (k.y - c.y).abs(), *k)).collect();
    cands.sort_unstable_by_key(|x| x.0);
    let pool = AsyncComputeTaskPool::get();
    for (_, k) in cands {
        if w.meshing.len() >= MAX_MESH { break; }
        let hd = hdist(k, c);
        let air_no_view = w.chunks.get(&k).is_some_and(|ch| ch.is_air()) && !w.views.contains_key(&k);
        if !w.chunks.contains_key(&k) || hd > RENDER_DIST || (k.y - c.y).abs() > VERT_DIST || air_no_view {
            w.dirty.remove(&k);
            continue;
        }
        let Some(pad) = w.padded(k) else { continue };
        w.dirty.remove(&k);
        w.meshing.insert(k);
        let (tx, e, res) = (w.tx.clone(), w.epoch, lod_res(hd));
        pool.spawn(async move {
            let (s, wa) = vm::mesh_chunk(&pad, res, [k.x * CS, k.y * CS, k.z * CS]);
            let _ = tx.send(Job::Mesh(e, k, res, s, wa));
        })
        .detach();
    }
}

fn autosave(time: Res<Time>, mut t: Local<f32>, mut w: ResMut<VoxelWorld>) {
    *t += time.delta_secs();
    if *t > 120.0 {
        *t = 0.0;
        w.save();
    }
}

fn save_on_exit(mut ev: EventReader<AppExit>, mut w: ResMut<VoxelWorld>) {
    if ev.read().next().is_some() { w.save(); }
}
