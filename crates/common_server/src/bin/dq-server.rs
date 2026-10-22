//! Headless dedicated server (stub). Opens or creates a world and pre-generates the spawn area using
//! the same core as the client. Networking + game ticking are a later chunk (see CLAUDE.md).
use common_server::{chunk::CS, save, worldgen::{Gen, GEN_VERSION}};

fn main() {
    let name = std::env::args().nth(1).unwrap_or_else(|| "Server World".into());
    let meta = save::load_meta(&name).unwrap_or_else(|| {
        let seed = save::now().wrapping_mul(0x9E3779B97F4A7C15);
        save::WorldMeta { name: name.clone(), seed, gen_version: GEN_VERSION, pos: Gen::new(seed).spawn_point(), ..Default::default() }
    });
    let g = Gen::new(meta.seed);
    let (sx, sy, sz) = ((meta.pos[0] as i32).div_euclid(CS), (meta.pos[1] as i32).div_euclid(CS), (meta.pos[2] as i32).div_euclid(CS));
    let t = std::time::Instant::now();
    let mut n = 0;
    for cz in sz - 6..=sz + 6 { for cx in sx - 6..=sx + 6 { for cy in sy - 2..=sy + 2 { let _ = g.generate(cx, cy, cz); n += 1; } } }
    println!("DokuQuest server {} | world '{}' seed {} | {n} spawn chunks in {:?}", env!("CARGO_PKG_VERSION"), meta.name, meta.seed, t.elapsed());
    if let Err(e) = save::save_meta(&meta) { eprintln!("could not save world: {e}"); }
    println!("Saved to {}. Multiplayer networking arrives in a later update.", save::world_dir(&meta.name).display());
}
